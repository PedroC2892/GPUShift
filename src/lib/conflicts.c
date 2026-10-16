/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Detection of other GPU switching tools that would fight over the same config. */
#include "internal.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

struct tool {
	const char *name;
	const char *process;    /* daemon name as argv[0] or argv[1] (scripts) */
	const char *files[4];   /* config that only exists while the tool is in use */
};

static const struct tool tools[] = {
	{ "envycontrol", NULL, { "/etc/modprobe.d/blacklist-nvidia.conf",
				 "/lib/udev/rules.d/50-remove-nvidia.rules",
				 "/lib/udev/rules.d/80-nvidia-pm.rules" } },
	{ "optimus-manager", "optimus-manager-daemon",
	  { "/etc/X11/xorg.conf.d/10-optimus-manager.conf" } },
	{ "supergfxctl", "supergfxd", { 0 } },
	{ "system76-power", "system76-power", { 0 } },
	{ "nvidia-prime", NULL, { "/etc/prime-discrete" } },
	{ "bumblebee", "bumblebeed", { 0 } },
};

static bool is_named(const char *arg, const char *name)
{
	const char *base = strrchr(arg, '/');
	return strcmp(base ? base + 1 : arg, name) == 0;
}

/* Returns true if any process has "name" as argv[0] or argv[1]. */
static bool process_running(DIR *proc, const char *name)
{
	struct dirent *e;
	rewinddir(proc);
	while ((e = readdir(proc))) {
		if (!isdigit((unsigned char)e->d_name[0]))
			continue;
		char path[PATH_MAX], cmd[1024];
		if (gs_rpath(path, sizeof(path), "/proc/%s/cmdline", e->d_name) < 0 ||
		    gs_read_file(path, cmd, sizeof(cmd)) < 0 || !cmd[0])
			continue;
		size_t len0 = strlen(cmd);
		if (is_named(cmd, name) || (len0 + 1 < sizeof(cmd) && is_named(cmd + len0 + 1, name)))
			return true;
	}
	return false;
}

/* X drivers that only drive the dGPU's vendor; none when both GPUs share a vendor. */
static bool dgpu_x_driver(const char *name, const struct gs_gpu *igpu, const struct gs_gpu *dgpu)
{
	static const char *const nvidia[] = { "nvidia", "nouveau", NULL };
	static const char *const amd[] = { "amdgpu", "radeon", "ati", NULL };
	const char *const *names = dgpu->vendor_id == 0x10de ? nvidia :
				   dgpu->vendor_id == 0x1002 ? amd : NULL;
	if (igpu->vendor_id == dgpu->vendor_id)
		return false;
	for (size_t i = 0; names && names[i]; i++)
		if (strcasecmp(name, names[i]) == 0)
			return true;
	return false;
}

/* Xorg "PCI:bus[@domain]:dev:func" (decimal) against "dddd:bb:dd.f" (hex). */
static bool busid_is(const char *busid, const char *address)
{
	unsigned dom, bus, dev, fn, b, d, f, dm = 0;
	if (sscanf(address, "%x:%x:%x.%x", &dom, &bus, &dev, &fn) != 4 ||
	    strncasecmp(busid, "PCI:", 4) != 0)
		return false;
	if (sscanf(busid + 4, "%u@%u:%u:%u", &b, &dm, &d, &f) != 4 &&
	    (dm = 0, sscanf(busid + 4, "%u:%u:%u", &b, &d, &f) != 3))
		return false;
	return b == bus && d == dev && f == fn && dm == dom;
}

/*
 * A Device section with the dGPU's driver or BusID: X builds its screen on the
 * dGPU only, which has no panel in Hybrid mode and does not exist in Integrated.
 */
static bool xorg_file_pins(const char *path, const struct gs_gpu *igpu, const struct gs_gpu *dgpu)
{
	char buf[32768];
	bool device = false;
	if (gs_read_file(path, buf, sizeof(buf)) < 0)
		return false;
	for (char *save = NULL, *line = strtok_r(buf, "\n", &save); line;
	     line = strtok_r(NULL, "\n", &save)) {
		line[strcspn(line, "#")] = '\0';
		char *kw = line + strspn(line, " \t"), *val = strchr(kw, '"'), *end;
		kw[strcspn(kw, " \t\"")] = '\0';
		if (val && (end = strchr(++val, '"')))
			*end = '\0';
		else
			val = "";
		if (strcasecmp(kw, "Section") == 0)
			device = strcasecmp(val, "Device") == 0;
		else if (strcasecmp(kw, "EndSection") == 0)
			device = false;
		else if (device && ((strcasecmp(kw, "Driver") == 0 && dgpu_x_driver(val, igpu, dgpu)) ||
				    (strcasecmp(kw, "BusID") == 0 && busid_is(val, dgpu->address))))
			return true;
	}
	return false;
}

static void detect_xorg_pin(struct gs_system *sys)
{
	const struct gs_gpu *igpu = gs_igpu(sys), *dgpu = gs_dgpu(sys);
	char path[PATH_MAX], name[sizeof(sys->xorg_pin)];
	if (!igpu || !dgpu)
		return;
	if (gs_rpath(path, sizeof(path), "/etc/X11/xorg.conf") == 0 && xorg_file_pins(path, igpu, dgpu)) {
		gs_strlcpy(sys->xorg_pin, "/etc/X11/xorg.conf", sizeof(sys->xorg_pin));
		return;
	}
	DIR *d = gs_rpath(path, sizeof(path), "/etc/X11/xorg.conf.d") == 0 ? opendir(path) : NULL;
	struct dirent *e;
	while (d && (e = readdir(d))) {
		size_t len = strlen(e->d_name);
		/* Xorg only reads *.conf from this directory. */
		if (len <= 5 || strcmp(e->d_name + len - 5, ".conf") != 0 ||
		    snprintf(name, sizeof(name), "/etc/X11/xorg.conf.d/%s", e->d_name) >= (int)sizeof(name) ||
		    gs_rpath(path, sizeof(path), "%s", name) < 0 || !xorg_file_pins(path, igpu, dgpu))
			continue;
		gs_strlcpy(sys->xorg_pin, name, sizeof(sys->xorg_pin));
		break;
	}
	if (d)
		closedir(d);
}

void gs_detect_conflicts(struct gs_system *sys)
{
	char path[PATH_MAX];
	DIR *proc = gs_rpath(path, sizeof(path), "/proc") == 0 ? opendir(path) : NULL;

	for (size_t i = 0; i < sizeof(tools) / sizeof(tools[0]); i++) {
		bool active = proc && tools[i].process && process_running(proc, tools[i].process);
		for (size_t f = 0; !active && f < 4 && tools[i].files[f]; f++)
			active = gs_rpath(path, sizeof(path), "%s", tools[i].files[f]) == 0 &&
				 gs_exists(path);
		if (active)
			sys->conflicts[sys->conflict_count++] = tools[i].name;
	}
	sys->switcheroo = proc && process_running(proc, "switcheroo-control");
	if (proc)
		closedir(proc);
	detect_xorg_pin(sys);
}

size_t gs_conflict_count(const gs_system *sys) { return sys->conflict_count; }

const char *gs_conflict_name(const gs_system *sys, size_t i)
{
	return i < sys->conflict_count ? sys->conflicts[i] : NULL;
}

bool gs_sys_switcheroo(const gs_system *sys) { return sys->switcheroo; }

const char *gs_sys_xorg_pin(const gs_system *sys) { return sys->xorg_pin[0] ? sys->xorg_pin : NULL; }
