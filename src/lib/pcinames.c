/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Human-readable names (libpci) and optional NVIDIA VRAM query (NVML via dlopen). */
#include "internal.h"

#include <dlfcn.h>
#include <pci/pci.h>
#include <stdlib.h>
#include <string.h>

static void quiet(char *msg, ...)
{
	(void)msg;
}

/* libpci only reports allocation failures through this hook. */
__attribute__((noreturn)) static void fatal(char *msg, ...)
{
	(void)msg;
	abort();
}

static void fallback_vendor(struct gs_gpu *g)
{
	switch (g->vendor_id) {
	case 0x8086: gs_strlcpy(g->vendor_name, "Intel Corporation", sizeof(g->vendor_name)); break;
	case 0x1002: gs_strlcpy(g->vendor_name, "Advanced Micro Devices, Inc. [AMD/ATI]", sizeof(g->vendor_name)); break;
	case 0x10de: gs_strlcpy(g->vendor_name, "NVIDIA Corporation", sizeof(g->vendor_name)); break;
	default: snprintf(g->vendor_name, sizeof(g->vendor_name), "Vendor %04x", g->vendor_id);
	}
}

void gs_pci_names(struct gs_gpu *g)
{
	/* Only the pci.ids database is used, so no pci_init()/bus access is needed. */
	struct pci_access *pacc = pci_alloc();
	if (pacc) {
		pacc->error = fatal;
		pacc->warning = quiet;
		pacc->debug = quiet;
		char buf[256];
		const char *s = pci_lookup_name(pacc, buf, sizeof(buf),
						PCI_LOOKUP_VENDOR | PCI_LOOKUP_NO_NUMBERS, g->vendor_id);
		if (s)
			gs_strlcpy(g->vendor_name, s, sizeof(g->vendor_name));
		s = pci_lookup_name(pacc, buf, sizeof(buf), PCI_LOOKUP_DEVICE | PCI_LOOKUP_NO_NUMBERS,
				    g->vendor_id, g->device_id);
		if (s)
			gs_strlcpy(g->device_name, s, sizeof(g->device_name));
		pci_cleanup(pacc);
	}
	if (!g->vendor_name[0])
		fallback_vendor(g);
	if (!g->device_name[0])
		snprintf(g->device_name, sizeof(g->device_name), "Device %04x", g->device_id);
}

typedef struct { unsigned long long total, free, used; } nvml_memory;

/*
 * Querying NVML wakes a runtime-suspended GPU, so it is only done when the
 * dGPU is already active, and never on a fake sysfs tree.
 */
void gs_nvml_vram(struct gs_gpu *g)
{
	if (gs_read_root()[0] || strcmp(g->runtime_status, "active") != 0)
		return;
	void *lib = dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
	if (!lib)
		return;
	int (*init)(void), (*shutdown)(void);
	int (*by_bus)(const char *, void **);
	int (*meminfo)(void *, nvml_memory *);
	/* POSIX-sanctioned way to turn dlsym()'s void * into a function pointer. */
	*(void **)&init = dlsym(lib, "nvmlInit_v2");
	*(void **)&shutdown = dlsym(lib, "nvmlShutdown");
	*(void **)&by_bus = dlsym(lib, "nvmlDeviceGetHandleByPciBusId_v2");
	*(void **)&meminfo = dlsym(lib, "nvmlDeviceGetMemoryInfo");
	if (init && shutdown && by_bus && meminfo && init() == 0) {
		void *dev;
		nvml_memory mem;
		if (by_bus(g->address, &dev) == 0 && meminfo(dev, &mem) == 0)
			g->vram_bytes = mem.total;
		shutdown();
	}
	dlclose(lib);
}
