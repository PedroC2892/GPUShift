/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * libgpushift public API.
 *
 * All read paths are resolved below $GPUSHIFT_SYSFS_ROOT (default "/"), so the
 * library can be pointed at a fake /sys, /proc and /etc tree for testing.
 */
#ifndef GPUSHIFT_H
#define GPUSHIFT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gs_system gs_system;
typedef struct gs_gpu gs_gpu;

typedef enum {
	GS_GPU_UNKNOWN = 0,
	GS_GPU_INTEGRATED,
	GS_GPU_DEDICATED,
} gs_gpu_kind;

typedef enum {
	GS_SECURE_BOOT_UNKNOWN = 0,
	GS_SECURE_BOOT_ENABLED,
	GS_SECURE_BOOT_DISABLED,
	GS_SECURE_BOOT_LEGACY_BIOS, /* no EFI firmware interface */
} gs_secure_boot;

const char *gs_version(void);

/* Scans the system. Returns NULL only on allocation failure. */
gs_system *gs_detect(void);
void gs_system_free(gs_system *sys);

size_t gs_gpu_count(const gs_system *sys);
const gs_gpu *gs_gpu_at(const gs_system *sys, size_t index);

const char *gs_gpu_address(const gs_gpu *gpu);      /* "0000:01:00.0" */
uint16_t gs_gpu_vendor_id(const gs_gpu *gpu);
uint16_t gs_gpu_device_id(const gs_gpu *gpu);
const char *gs_gpu_vendor_name(const gs_gpu *gpu);
const char *gs_gpu_device_name(const gs_gpu *gpu);
gs_gpu_kind gs_gpu_get_kind(const gs_gpu *gpu);
const char *gs_gpu_kind_name(gs_gpu_kind kind);     /* "integrated", ... */
bool gs_gpu_boot_vga(const gs_gpu *gpu);
const char *gs_gpu_driver(const gs_gpu *gpu);       /* NULL if no driver */
const char *gs_gpu_driver_version(const gs_gpu *gpu); /* NULL if unknown */
const char *gs_gpu_runtime_status(const gs_gpu *gpu); /* NULL if unknown */
const char *gs_gpu_power_state(const gs_gpu *gpu);  /* NULL if unknown */
uint64_t gs_gpu_vram_bytes(const gs_gpu *gpu);      /* 0 if unknown */
bool gs_gpu_internal_display(const gs_gpu *gpu);
/* True for a dGPU that GPUShift removed from the PCI bus (Integrated mode). */
bool gs_gpu_removed(const gs_gpu *gpu);

const char *gs_sys_distro(const gs_system *sys);
const char *gs_sys_kernel(const gs_system *sys);
int gs_sys_chassis_type(const gs_system *sys);      /* SMBIOS code, 0 unknown */
const char *gs_sys_chassis_name(const gs_system *sys);
bool gs_sys_is_laptop(const gs_system *sys);
const char *gs_sys_session_type(const gs_system *sys); /* NULL if unknown */
const char *gs_sys_desktop(const gs_system *sys);      /* NULL if unknown */
gs_secure_boot gs_sys_secure_boot(const gs_system *sys);
const char *gs_secure_boot_name(gs_secure_boot sb);

#ifdef __cplusplus
}
#endif

#endif
