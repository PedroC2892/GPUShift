/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Private declarations shared by libgpushift and gpushift-helper. */
#ifndef GPUSHIFT_INTERNAL_H
#define GPUSHIFT_INTERNAL_H

#include "gpushift/gpushift.h"

#include <limits.h>
#include <stdio.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define GS_MAX_GPUS 8

struct gs_gpu {
	char address[32];
	uint16_t vendor_id, device_id;
	unsigned class_code; /* 24-bit PCI class */
	char vendor_name[128], device_name[160];
	gs_gpu_kind kind;
	bool boot_vga;
	char driver[32];          /* empty: no driver */
	char driver_version[96];  /* empty: unknown */
	char runtime_status[24], power_state[16];
	uint64_t vram_bytes;
	bool internal_display;
	bool removed;
};

struct gs_system {
	struct gs_gpu gpus[GS_MAX_GPUS];
	size_t gpu_count;
	char distro[128];
	char kernel[96];
	int chassis_type;
	char session_type[32], desktop[64];
	gs_secure_boot secure_boot;
};

/* Path helpers: "path" is absolute, the result is prefixed with the root. */
const char *gs_read_root(void);   /* $GPUSHIFT_SYSFS_ROOT or "" */
const char *gs_write_root(void);  /* $GPUSHIFT_ETC_ROOT or "" */
int gs_rpath(char *buf, size_t n, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));
int gs_wpath(char *buf, size_t n, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));

/* File helpers on already-resolved paths. Return 0 on success, -1 on error. */
int gs_read_file(const char *path, char *buf, size_t n);  /* NUL-terminated */
int gs_read_line(const char *path, char *buf, size_t n);  /* first line, trimmed */
int gs_read_ulong(const char *path, unsigned long *out);  /* dec or 0x hex */
bool gs_exists(const char *path);
void gs_strlcpy(char *dst, const char *src, size_t n);

/* Detection pieces (detect.c, sysinfo.c). */
void gs_pci_names(struct gs_gpu *gpu);
void gs_nvml_vram(struct gs_gpu *gpu);
void gs_detect_sysinfo(struct gs_system *sys);

#endif
