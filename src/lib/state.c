/* SPDX-License-Identifier: GPL-3.0-or-later */
/* key=value state files written by gpushift-helper. */
#include "internal.h"

#include <stdlib.h>
#include <string.h>

/* Calls fn(key, value) for every "key=value" line of the file. */
static int parse_kv(const char *path, void (*fn)(void *, const char *, const char *), void *ctx)
{
	char buf[2048];
	if (gs_read_file(path, buf, sizeof(buf)) < 0)
		return -1;
	char *save = NULL;
	for (char *line = strtok_r(buf, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		char *eq = strchr(line, '=');
		if (!eq || line[0] == '#')
			continue;
		*eq = '\0';
		fn(ctx, line, eq + 1);
	}
	return 0;
}

static void state_kv(void *ctx, const char *key, const char *value)
{
	struct gs_state *st = ctx;
	if (strcmp(key, "mode") == 0)
		gs_mode_from_name(value, &st->mode);
	else if (strcmp(key, "dgpu") == 0)
		gs_strlcpy(st->dgpu, value, sizeof(st->dgpu));
	else if (strcmp(key, "dgpu_vendor") == 0)
		st->dgpu_vendor = (unsigned)strtoul(value, NULL, 16);
	else if (strcmp(key, "dgpu_device") == 0)
		st->dgpu_device = (unsigned)strtoul(value, NULL, 16);
	else if (strcmp(key, "dgpu_functions") == 0)
		gs_strlcpy(st->dgpu_functions, value, sizeof(st->dgpu_functions));
	else if (strcmp(key, "mux_backend") == 0)
		gs_strlcpy(st->mux_backend, value, sizeof(st->mux_backend));
	else if (strcmp(key, "mux_orig") == 0)
		gs_strlcpy(st->mux_orig, value, sizeof(st->mux_orig));
	else if (strcmp(key, "pending") == 0)
		st->pending = strcmp(value, "1") == 0;
	else if (strcmp(key, "boot_attempts") == 0)
		st->boot_attempts = atoi(value);
	else if (strcmp(key, "previous_mode") == 0)
		gs_mode_from_name(value, &st->previous_mode);
	else if (strcmp(key, "auto_reboot") == 0)
		st->auto_reboot = strcmp(value, "1") == 0;
}

int gs_state_load(struct gs_state *st)
{
	char path[PATH_MAX];
	memset(st, 0, sizeof(*st));
	if (gs_wpath(path, sizeof(path), GS_STATE_FILE) < 0 || parse_kv(path, state_kv, st) < 0)
		return -1;
	if (st->mode == GS_MODE_NONE) {
		memset(st, 0, sizeof(*st));
		return -1;
	}
	return 0;
}

int gs_state_format(const struct gs_state *st, char *buf, size_t n)
{
	int r = snprintf(buf, n,
			 "# Managed by gpushift-helper. Do not edit; use 'gpushift reset'.\n"
			 "version=1\nmode=%s\ndgpu=%s\ndgpu_vendor=%04x\ndgpu_device=%04x\n"
			 "dgpu_functions=%s\n"
			 "mux_backend=%s\nmux_orig=%s\n"
			 "pending=%d\nboot_attempts=%d\nprevious_mode=%s\nauto_reboot=%d\n",
			 gs_mode_name(st->mode), st->dgpu, st->dgpu_vendor, st->dgpu_device,
			 st->dgpu_functions, st->mux_backend, st->mux_orig, st->pending, st->boot_attempts,
			 gs_mode_name(st->previous_mode), st->auto_reboot);
	return (r < 0 || (size_t)r >= n) ? -1 : 0;
}

static void pending_kv(void *ctx, const char *key, const char *value)
{
	struct gs_system *sys = ctx;
	if (strcmp(key, "from") == 0)
		gs_mode_from_name(value, &sys->pending_from);
	else if (strcmp(key, "to") == 0)
		gs_mode_from_name(value, &sys->pending_to);
}

/* The pending marker lives in /run, a tmpfs, so a reboot clears it. */
void gs_load_state(struct gs_system *sys)
{
	char path[PATH_MAX];
	sys->has_state = gs_state_load(&sys->state) == 0;
	if (gs_wpath(path, sizeof(path), GS_PENDING_FILE) == 0)
		parse_kv(path, pending_kv, sys);
	if (sys->pending_to == GS_MODE_NONE)
		sys->pending_from = GS_MODE_NONE;
}
