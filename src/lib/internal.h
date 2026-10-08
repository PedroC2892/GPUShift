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

/* Persistent state, /var/lib/gpushift/state. */
struct gs_state {
	gs_mode mode;
	char dgpu[32];               /* PCI address of the managed dGPU */
	unsigned dgpu_vendor, dgpu_device;
	char mux_backend[32];
	char mux_orig[16];           /* MUX value before GPUShift changed it */
	/* Confirmation of the last change (see gpushift-boot-check). */
	bool pending;                /* applied, not yet confirmed after a reboot */
	int boot_attempts;           /* boots since the change without confirmation */
	gs_mode previous_mode;       /* mode to revert to */
	bool auto_reboot;            /* boot check already rebooted once for this change */
};

struct gs_mux_backend {
	const char *name;
	const char *mux_path;     /* sysfs attribute selecting the display GPU */
	const char *dgpu_value;   /* value routing the panel to the dGPU */
	const char *hybrid_value; /* value routing the panel to the iGPU */
	const char *disable_path; /* optional attribute powering the dGPU off */
	bool (*probe)(const struct gs_mux_backend *b);
	gs_mode (*get)(const struct gs_mux_backend *b);
	int (*set)(const struct gs_mux_backend *b, gs_mode mode);
};

struct gs_system {
	struct gs_gpu gpus[GS_MAX_GPUS];
	size_t gpu_count;
	char distro[128];
	char kernel[96];
	int chassis_type;
	char session_type[32], desktop[64];
	gs_secure_boot secure_boot;

	struct gs_state state;
	bool has_state;
	gs_mode pending_from, pending_to;
	const struct gs_mux_backend *mux;
	const char *conflicts[8];
	size_t conflict_count;
	bool switcheroo;
	char initramfs[24];  /* name of the initramfs generator found, if any */
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
void gs_detect_conflicts(struct gs_system *sys);
const struct gs_mux_backend *gs_mux_probe(void);
const struct gs_mux_backend *gs_mux_find(const char *name);
int gs_sysfs_write(const char *path, const char *value);

/* State files (state.c). */
#define GS_STATE_FILE "/var/lib/gpushift/state"
#define GS_PENDING_FILE "/run/gpushift/pending"
#define GS_LOG_FILE "/var/lib/gpushift/log"
#define GS_RECOVERY_FILE "/var/lib/gpushift/RECOVERY.txt"
int gs_state_load(struct gs_state *st);  /* 0: loaded, -1: absent/invalid */
int gs_state_format(const struct gs_state *st, char *buf, size_t n);
void gs_load_state(struct gs_system *sys);

/* Files written by gpushift-helper (below $GPUSHIFT_ETC_ROOT in test builds). */
#define GS_MODPROBE_FILE GPUSHIFT_MODPROBE_DIR "/gpushift.conf"
#define GS_UDEV_FILE GPUSHIFT_UDEV_RULES_DIR "/50-gpushift.rules"
#define GS_BACKUP_DIR "/var/lib/gpushift/backup"
#define GS_PREVIOUS_DIR GS_BACKUP_DIR "/previous" /* files of previous_mode */

/* Builds the file contents for a mode; an empty string means "no file". */
int gs_config_build(const gs_system *sys, gs_mode mode,
		    char *modprobe, size_t modprobe_len, char *udev, size_t udev_len);
bool gs_valid_pci_address(const char *address);

/* Initramfs generators (initramfs.c). */
struct gs_initramfs_backend {
	const char *name;
	const char *binary;       /* looked up in the fixed system directories */
	const char *runner;       /* optional absolute program to run instead */
	const char *args[4];      /* arguments after argv[0] */
	int (*run)(const struct gs_initramfs_backend *b, const char *path);
};
const struct gs_initramfs_backend *gs_initramfs_find(char *path, size_t n);

/* Runs an absolute program without a shell; returns its exit status or -1. */
int gs_spawn(const char *path, char *const argv[], char *const envp[]);

/* The single integrated and dedicated GPU of a switchable system, or NULL. */
const struct gs_gpu *gs_igpu(const gs_system *sys);
const struct gs_gpu *gs_dgpu(const gs_system *sys);

#endif
