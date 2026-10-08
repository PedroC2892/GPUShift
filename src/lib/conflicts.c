/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Detection of other GPU switching tools that would fight over the same config. */
#include "internal.h"

#include <ctype.h>
#include <dirent.h>
#include <string.h>

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
}

size_t gs_conflict_count(const gs_system *sys) { return sys->conflict_count; }

const char *gs_conflict_name(const gs_system *sys, size_t i)
{
	return i < sys->conflict_count ? sys->conflicts[i] : NULL;
}

bool gs_sys_switcheroo(const gs_system *sys) { return sys->switcheroo; }
