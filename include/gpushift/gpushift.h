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

/* Status codes. They are also the exit codes of gpushift and gpushift-helper. */
typedef enum {
	GS_OK = 0,
	GS_ERR_GENERIC = 1,
	GS_ERR_USAGE = 2,
	GS_ERR_SINGLE_GPU = 3,       /* only one GPU: nothing to switch */
	GS_ERR_NOT_SWITCHABLE = 4,   /* desktop or unsupported GPU combination */
	GS_ERR_MODE_UNAVAILABLE = 5, /* the mode is not supported here */
	GS_ERR_CONFLICT = 6,         /* another GPU switching tool is active */
	GS_ERR_NO_INITRAMFS = 7,     /* no supported initramfs generator found */
	GS_ERR_IO = 8,               /* reading or writing a file failed */
	GS_ERR_INITRAMFS_FAILED = 9, /* the generator failed; changes rolled back */
	GS_ERR_MUX = 10,             /* writing the firmware MUX failed */
	GS_ERR_PERMISSION = 11,      /* the helper was not run as root */
	GS_ERR_AUTH = 12,            /* polkit authentication denied or cancelled */
} gs_status;

typedef enum {
	GS_MODE_NONE = 0,
	GS_MODE_INTEGRATED, /* dGPU powered off and removed from the bus */
	GS_MODE_HYBRID,     /* iGPU drives the display, dGPU via PRIME offload */
	GS_MODE_DEDICATED,  /* firmware MUX routes the display to the dGPU */
	GS_MODE_DEFAULT,    /* only as a pending mode: GPUShift config removed */
} gs_mode;
#define GS_MODE_COUNT 3 /* switchable modes: integrated, hybrid, dedicated */

typedef enum {
	GS_SWITCH_OK = 0,
	GS_SWITCH_SINGLE_GPU,  /* exactly one GPU */
	GS_SWITCH_DESKTOP,     /* not a laptop: information only */
	GS_SWITCH_UNSUPPORTED, /* not one iGPU + one dGPU, or no safe mode */
	GS_SWITCH_CONFLICT,    /* another switching tool is active */
} gs_switch;

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

/* Firmware MUX backend name ("asus-wmi", ...), or NULL if there is none. */
const char *gs_sys_mux_backend(const gs_system *sys);
/* Active conflicting tools (envycontrol, supergfxctl, ...). */
size_t gs_conflict_count(const gs_system *sys);
const char *gs_conflict_name(const gs_system *sys, size_t index);
/* switcheroo-control is compatible; reported for information only. */
bool gs_sys_switcheroo(const gs_system *sys);

const char *gs_strerror(gs_status status);
const char *gs_mode_name(gs_mode mode);
bool gs_mode_from_name(const char *name, gs_mode *mode);

gs_switch gs_switchability(const gs_system *sys);
const char *gs_switch_message(gs_switch sw);
/* Fills "modes" (room for GS_MODE_COUNT) with the modes this system supports. */
size_t gs_list_modes(const gs_system *sys, gs_mode *modes);
bool gs_mode_available(const gs_system *sys, gs_mode mode);
gs_mode gs_current_mode(const gs_system *sys);
/* Mode that becomes active after the next reboot, or GS_MODE_NONE. */
gs_mode gs_pending_mode(const gs_system *sys);

#ifdef __cplusplus
}
#endif

#endif
