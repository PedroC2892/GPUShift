/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Firmware MUX backends. Each row describes a vendor interface that routes the
 * internal panel to either GPU. To support a new interface, add a row; rows
 * that are not plain sysfs attributes can provide their own functions.
 */
#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static int read_attr(const char *attr, char *buf, size_t n)
{
	char path[PATH_MAX];
	return gs_rpath(path, sizeof(path), "%s", attr) < 0 ? -1 : gs_read_line(path, buf, n);
}

static bool sysfs_probe(const struct gs_mux_backend *b)
{
	char v[16];
	return read_attr(b->mux_path, v, sizeof(v)) == 0 &&
	       (strcmp(v, b->dgpu_value) == 0 || strcmp(v, b->hybrid_value) == 0);
}

static gs_mode sysfs_get(const struct gs_mux_backend *b)
{
	char v[16];
	if (read_attr(b->mux_path, v, sizeof(v)) < 0)
		return GS_MODE_NONE;
	return strcmp(v, b->dgpu_value) == 0 ? GS_MODE_DEDICATED : GS_MODE_HYBRID;
}

int gs_sysfs_write(const char *attr, const char *value)
{
	char path[PATH_MAX];
	if (gs_rpath(path, sizeof(path), "%s", attr) < 0)
		return -1;
	int fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC);
	if (fd < 0)
		return -1;
	size_t len = strlen(value);
	ssize_t w;
	do
		w = write(fd, value, len);
	while (w < 0 && errno == EINTR);
	return (close(fd) == 0 && w == (ssize_t)len) ? 0 : -1;
}

/* Dedicated routes the panel to the dGPU; every other mode routes it to the iGPU. */
static int sysfs_set(const struct gs_mux_backend *b, gs_mode mode)
{
	char v[16];
	/* The dGPU must be powered for the firmware to switch to it. */
	if (b->disable_path && read_attr(b->disable_path, v, sizeof(v)) == 0 &&
	    strcmp(v, "0") != 0 && gs_sysfs_write(b->disable_path, "0") < 0)
		return -1;
	return gs_sysfs_write(b->mux_path,
			      mode == GS_MODE_DEDICATED ? b->dgpu_value : b->hybrid_value);
}

#define SYSFS_BACKEND .probe = sysfs_probe, .get = sysfs_get, .set = sysfs_set

static const struct gs_mux_backend backends[] = {
	{
		/* asus-armoury firmware attributes (Linux 6.17+). */
		.name = "asus-armoury",
		.mux_path = "/sys/class/firmware-attributes/asus-armoury/attributes/gpu_mux_mode/current_value",
		.dgpu_value = "0", .hybrid_value = "1",
		.disable_path = "/sys/class/firmware-attributes/asus-armoury/attributes/dgpu_disable/current_value",
		SYSFS_BACKEND,
	},
	{
		.name = "asus-wmi",
		.mux_path = "/sys/devices/platform/asus-nb-wmi/gpu_mux_mode",
		.dgpu_value = "0", .hybrid_value = "1",
		.disable_path = "/sys/devices/platform/asus-nb-wmi/dgpu_disable",
		SYSFS_BACKEND,
	},
	{
		/* LenovoLegionLinux (legion_laptop module): gsync=1 is dGPU-only. */
		.name = "lenovo-legion",
		.mux_path = "/sys/bus/platform/drivers/legion/PNP0C09:00/gsync",
		.dgpu_value = "1", .hybrid_value = "0",
		SYSFS_BACKEND,
	},
};

const struct gs_mux_backend *gs_mux_probe(void)
{
	for (size_t i = 0; i < sizeof(backends) / sizeof(backends[0]); i++)
		if (backends[i].probe(&backends[i]))
			return &backends[i];
	return NULL;
}

const struct gs_mux_backend *gs_mux_find(const char *name)
{
	for (size_t i = 0; i < sizeof(backends) / sizeof(backends[0]); i++)
		if (strcmp(backends[i].name, name) == 0)
			return &backends[i];
	return NULL;
}
