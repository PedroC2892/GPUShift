/* SPDX-License-Identifier: GPL-3.0-or-later */
/* PCI GPU detection. Read-only, works without root. */
#include "internal.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VENDOR_INTEL 0x8086
#define VENDOR_AMD 0x1002
#define VENDOR_AMD_SOC 0x1022 /* AMD CPU/chipset functions (PSP, USB, ...) */
#define VENDOR_NVIDIA 0x10de

static bool is_gpu_class(unsigned class_code)
{
	unsigned base = class_code >> 8;
	return base == 0x0300 || base == 0x0302 || base == 0x0380;
}

/* Intel discrete GPUs (DG1, Alchemist, Battlemage); every other Intel GPU is an iGPU. */
static bool intel_is_discrete(uint16_t dev)
{
	return (dev >= 0x4905 && dev <= 0x4909) ||
	       (dev >= 0x5690 && dev <= 0x56ff) ||
	       (dev >= 0xe202 && dev <= 0xe2ff);
}

/*
 * An AMD APU's GPU shares its PCI slot with AMD SoC functions (PSP, USB,
 * audio coprocessor) that use the CPU vendor ID; a discrete card does not.
 */
static bool amd_has_soc_sibling(const char *address)
{
	char path[PATH_MAX];
	if (gs_rpath(path, sizeof(path), "/sys/bus/pci/devices") < 0)
		return false;
	DIR *d = opendir(path);
	if (!d)
		return false;
	size_t slot_len = strcspn(address, "."); /* "0000:05:00" */
	bool found = false;
	struct dirent *e;
	while (!found && (e = readdir(d))) {
		unsigned long vendor;
		if (strncmp(e->d_name, address, slot_len) != 0 || e->d_name[slot_len] != '.')
			continue;
		if (gs_rpath(path, sizeof(path), "/sys/bus/pci/devices/%s/vendor", e->d_name) == 0 &&
		    gs_read_ulong(path, &vendor) == 0 && vendor == VENDOR_AMD_SOC)
			found = true;
	}
	closedir(d);
	return found;
}

static gs_gpu_kind guess_kind(const struct gs_gpu *g)
{
	switch (g->vendor_id) {
	case VENDOR_NVIDIA:
		return GS_GPU_DEDICATED;
	case VENDOR_INTEL:
		return intel_is_discrete(g->device_id) ? GS_GPU_DEDICATED : GS_GPU_INTEGRATED;
	case VENDOR_AMD:
		return amd_has_soc_sibling(g->address) ? GS_GPU_INTEGRATED : GS_GPU_DEDICATED;
	default:
		return GS_GPU_UNKNOWN;
	}
}

static void read_driver_version(struct gs_gpu *g, const char *kernel)
{
	char path[PATH_MAX], buf[1024];

	if (strcmp(g->driver, "nvidia") == 0) {
		if (gs_rpath(path, sizeof(path), "/proc/driver/nvidia/version") == 0 &&
		    gs_read_file(path, buf, sizeof(buf)) == 0) {
			/* "NVRM version: NVIDIA UNIX [Open Kernel Module|x86_64 Kernel Module]  550.54.14  ..." */
			if (strstr(buf, "Open Kernel Module"))
				gs_strlcpy(g->driver, "nvidia-open", sizeof(g->driver));
			const char *p = strstr(buf, "Kernel Module");
			if (p) {
				p += strlen("Kernel Module");
				p += strspn(p, " \t");
				size_t len = strcspn(p, " \t\n");
				if (len && len < sizeof(g->driver_version))
					gs_strlcpy(g->driver_version, p, len + 1);
			}
		}
		if (gs_rpath(path, sizeof(path), "/sys/module/nvidia/version") == 0 &&
		    gs_read_line(path, buf, sizeof(buf)) == 0 && buf[0])
			gs_strlcpy(g->driver_version, buf, sizeof(g->driver_version));
		return;
	}
	/* In-kernel drivers follow the kernel version unless the module says otherwise. */
	if (gs_rpath(path, sizeof(path), "/sys/module/%s/version", g->driver) == 0 &&
	    gs_read_line(path, buf, sizeof(buf)) == 0 && buf[0])
		gs_strlcpy(g->driver_version, buf, sizeof(g->driver_version));
	else
		gs_strlcpy(g->driver_version, kernel, sizeof(g->driver_version));
}

static bool has_internal_panel(const char *address)
{
	static const char *const panels[] = { "-eDP-", "-LVDS-", "-DSI-" };
	char path[PATH_MAX];
	if (gs_rpath(path, sizeof(path), "/sys/bus/pci/devices/%s/drm", address) < 0)
		return false;
	DIR *drm = opendir(path);
	if (!drm)
		return false;
	bool found = false;
	struct dirent *card;
	while (!found && (card = readdir(drm))) {
		if (strncmp(card->d_name, "card", 4) != 0)
			continue;
		char cpath[PATH_MAX];
		if (gs_rpath(cpath, sizeof(cpath), "/sys/bus/pci/devices/%s/drm/%s",
			     address, card->d_name) < 0)
			continue;
		DIR *cd = opendir(cpath);
		if (!cd)
			continue;
		struct dirent *conn;
		while (!found && (conn = readdir(cd))) {
			bool panel = false;
			for (size_t i = 0; i < sizeof(panels) / sizeof(panels[0]); i++)
				panel |= strstr(conn->d_name, panels[i]) != NULL;
			if (!panel)
				continue;
			/* A muxed panel shows up on both GPUs; only the connected one drives it. */
			char spath[PATH_MAX * 2], status[32];
			snprintf(spath, sizeof(spath), "%s/%s/status", cpath, conn->d_name);
			found = gs_read_line(spath, status, sizeof(status)) < 0 ||
				strcmp(status, "disconnected") != 0;
		}
		closedir(cd);
	}
	closedir(drm);
	return found;
}

static void read_gpu(struct gs_gpu *g, const char *kernel)
{
	char path[PATH_MAX], buf[PATH_MAX];
	unsigned long v;

#define DEV_PATH(attr) gs_rpath(path, sizeof(path), "/sys/bus/pci/devices/%s/" attr, g->address)
	if (DEV_PATH("vendor") == 0 && gs_read_ulong(path, &v) == 0)
		g->vendor_id = (uint16_t)v;
	if (DEV_PATH("device") == 0 && gs_read_ulong(path, &v) == 0)
		g->device_id = (uint16_t)v;
	g->boot_vga = DEV_PATH("boot_vga") == 0 && gs_read_ulong(path, &v) == 0 && v == 1;

	if (DEV_PATH("driver") == 0) {
		ssize_t len = readlink(path, buf, sizeof(buf) - 1);
		if (len > 0) {
			buf[len] = '\0';
			const char *base = strrchr(buf, '/');
			gs_strlcpy(g->driver, base ? base + 1 : buf, sizeof(g->driver));
		}
	}
	if (g->driver[0])
		read_driver_version(g, kernel);

	if (DEV_PATH("power/runtime_status") == 0)
		gs_read_line(path, g->runtime_status, sizeof(g->runtime_status));
	if (DEV_PATH("power_state") == 0)
		gs_read_line(path, g->power_state, sizeof(g->power_state));
	if (DEV_PATH("mem_info_vram_total") == 0 && gs_read_ulong(path, &v) == 0)
		g->vram_bytes = v;
#undef DEV_PATH

	g->kind = guess_kind(g);
	g->internal_display = has_internal_panel(g->address);
	gs_pci_names(g);
	if (!g->vram_bytes && strncmp(g->driver, "nvidia", 6) == 0)
		gs_nvml_vram(g);
}

static int cmp_gpu(const void *a, const void *b)
{
	return strcmp(((const struct gs_gpu *)a)->address, ((const struct gs_gpu *)b)->address);
}

gs_system *gs_detect(void)
{
	gs_system *sys = calloc(1, sizeof(*sys));
	if (!sys)
		return NULL;
	gs_detect_sysinfo(sys);

	char path[PATH_MAX];
	DIR *d = gs_rpath(path, sizeof(path), "/sys/bus/pci/devices") == 0 ? opendir(path) : NULL;
	struct dirent *e;
	while (d && (e = readdir(d)) && sys->gpu_count < GS_MAX_GPUS) {
		unsigned long cls;
		if (e->d_name[0] == '.' ||
		    gs_rpath(path, sizeof(path), "/sys/bus/pci/devices/%s/class", e->d_name) < 0 ||
		    gs_read_ulong(path, &cls) < 0 || !is_gpu_class((unsigned)cls))
			continue;
		struct gs_gpu *g = &sys->gpus[sys->gpu_count++];
		gs_strlcpy(g->address, e->d_name, sizeof(g->address));
		g->class_code = (unsigned)cls;
		read_gpu(g, sys->kernel);
	}
	if (d)
		closedir(d);
	gs_load_state(sys);
	/* A dGPU removed by Integrated mode is still managed, so keep showing it. */
	bool present = !sys->state.dgpu[0];
	for (size_t i = 0; i < sys->gpu_count && !present; i++)
		present = strcmp(sys->gpus[i].address, sys->state.dgpu) == 0;
	if (!present && sys->gpu_count < GS_MAX_GPUS) {
		struct gs_gpu *g = &sys->gpus[sys->gpu_count++];
		gs_strlcpy(g->address, sys->state.dgpu, sizeof(g->address));
		g->vendor_id = (uint16_t)sys->state.dgpu_vendor;
		g->device_id = (uint16_t)sys->state.dgpu_device;
		g->kind = GS_GPU_DEDICATED;
		g->removed = true;
		gs_pci_names(g);
	}
	qsort(sys->gpus, sys->gpu_count, sizeof(sys->gpus[0]), cmp_gpu);
	sys->mux = gs_mux_probe();
	const struct gs_initramfs_backend *ird = gs_initramfs_find(path, sizeof(path));
	if (ird)
		gs_strlcpy(sys->initramfs, ird->name, sizeof(sys->initramfs));
	gs_detect_conflicts(sys);
	return sys;
}

void gs_system_free(gs_system *sys)
{
	free(sys);
}

size_t gs_gpu_count(const gs_system *sys) { return sys->gpu_count; }

const gs_gpu *gs_gpu_at(const gs_system *sys, size_t i)
{
	return i < sys->gpu_count ? &sys->gpus[i] : NULL;
}

static const char *opt(const char *s) { return s[0] ? s : NULL; }

const char *gs_gpu_address(const gs_gpu *g) { return g->address; }
uint16_t gs_gpu_vendor_id(const gs_gpu *g) { return g->vendor_id; }
uint16_t gs_gpu_device_id(const gs_gpu *g) { return g->device_id; }
const char *gs_gpu_vendor_name(const gs_gpu *g) { return g->vendor_name; }
const char *gs_gpu_device_name(const gs_gpu *g) { return g->device_name; }
gs_gpu_kind gs_gpu_get_kind(const gs_gpu *g) { return g->kind; }
bool gs_gpu_boot_vga(const gs_gpu *g) { return g->boot_vga; }
const char *gs_gpu_driver(const gs_gpu *g) { return opt(g->driver); }
const char *gs_gpu_driver_version(const gs_gpu *g) { return opt(g->driver_version); }
const char *gs_gpu_runtime_status(const gs_gpu *g) { return opt(g->runtime_status); }
const char *gs_gpu_power_state(const gs_gpu *g) { return opt(g->power_state); }
uint64_t gs_gpu_vram_bytes(const gs_gpu *g) { return g->vram_bytes; }
bool gs_gpu_internal_display(const gs_gpu *g) { return g->internal_display; }
bool gs_gpu_removed(const gs_gpu *g) { return g->removed; }
const char *gs_sys_initramfs_tool(const gs_system *sys) { return opt(sys->initramfs); }

const char *gs_gpu_kind_name(gs_gpu_kind kind)
{
	switch (kind) {
	case GS_GPU_INTEGRATED: return "integrated";
	case GS_GPU_DEDICATED: return "dedicated";
	default: return "unknown";
	}
}
