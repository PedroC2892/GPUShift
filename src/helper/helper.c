/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * gpushift-helper: the only GPUShift program that changes the system. It is
 * started through pkexec (polkit action org.gpushift.helper) and accepts only:
 *
 *   gpushift-helper apply integrated|hybrid|dedicated
 *   gpushift-helper reset
 *
 * It re-detects and re-validates everything itself and never trusts its
 * caller. Exit codes are the gs_status values from gpushift.h.
 *
 * Test builds (GPUSHIFT_TEST_HOOKS) honour $GPUSHIFT_SYSFS_ROOT and
 * $GPUSHIFT_ETC_ROOT and refuse to run without the latter, so they can never
 * touch the real /etc. Release builds drop both variables.
 */
#include "../lib/internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONTENT_MAX 4096

static const char *const targets[] = { GS_MODPROBE_FILE, GS_UDEV_FILE };
#define NTARGETS (sizeof(targets) / sizeof(targets[0]))

/* Saved contents of the targets, to roll back on failure. */
struct snapshot {
	bool exists[NTARGETS];
	char data[NTARGETS][CONTENT_MAX];
};

static void msg(const char *text)
{
	fprintf(stderr, "gpushift-helper: %s\n", text);
}

/* mkdir -p for an already-resolved path. */
static int mkdirs(const char *path)
{
	char tmp[PATH_MAX];
	gs_strlcpy(tmp, path, sizeof(tmp));
	for (char *p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
			return -1;
		*p = '/';
	}
	return (mkdir(tmp, 0755) < 0 && errno != EEXIST) ? -1 : 0;
}

static int parent_dir(const char *path, char *dir, size_t n)
{
	gs_strlcpy(dir, path, n);
	char *slash = strrchr(dir, '/');
	if (!slash || slash == dir)
		return -1;
	*slash = '\0';
	return 0;
}

/* Writes a resolved path atomically: temporary file in the same directory, fsync, rename. */
static int write_atomic(const char *path, const char *data)
{
	char dir[PATH_MAX], tmp[PATH_MAX + 16];
	if (parent_dir(path, dir, sizeof(dir)) < 0 || mkdirs(dir) < 0)
		return -1;
	snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path);
	int fd = mkstemp(tmp);
	if (fd < 0)
		return -1;
	size_t len = strlen(data), off = 0;
	int ok = fchmod(fd, 0644) == 0;
	while (ok && off < len) {
		ssize_t w = write(fd, data + off, len - off);
		if (w < 0 && errno == EINTR)
			continue;
		ok = w > 0;
		off += ok ? (size_t)w : 0;
	}
	ok = ok && fsync(fd) == 0;
	ok = close(fd) == 0 && ok;
	if (!ok || rename(tmp, path) < 0) {
		unlink(tmp);
		return -1;
	}
	int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dfd >= 0) {
		fsync(dfd);
		close(dfd);
	}
	return 0;
}

static int remove_file(const char *path)
{
	return (unlink(path) < 0 && errno != ENOENT) ? -1 : 0;
}

/* Empty content removes the file. */
static int put_file(const char *path, const char *content)
{
	return content[0] ? write_atomic(path, content) : remove_file(path);
}

static int wpath(char *buf, const char *path)
{
	return gs_wpath(buf, PATH_MAX, "%s", path);
}

static int backup_path(char *buf, const char *target)
{
	const char *base = strrchr(target, '/');
	return gs_wpath(buf, PATH_MAX, GS_BACKUP_DIR "/%s", base ? base + 1 : target);
}

static int take_snapshot(struct snapshot *s)
{
	char path[PATH_MAX];
	for (size_t i = 0; i < NTARGETS; i++) {
		if (wpath(path, targets[i]) < 0)
			return -1;
		s->exists[i] = gs_exists(path);
		s->data[i][0] = '\0';
		if (s->exists[i] && gs_read_file(path, s->data[i], CONTENT_MAX) < 0)
			return -1;
	}
	return 0;
}

static void restore_snapshot(const struct snapshot *s)
{
	char path[PATH_MAX];
	for (size_t i = 0; i < NTARGETS; i++) {
		if (wpath(path, targets[i]) < 0)
			continue;
		if (s->exists[i] ? write_atomic(path, s->data[i]) : remove_file(path))
			fprintf(stderr, "gpushift-helper: could not restore %s\n", path);
	}
}

/* Before GPUShift first writes to /etc, keep any file it is about to replace. */
static int backup_originals(const struct snapshot *s)
{
	char path[PATH_MAX];
	for (size_t i = 0; i < NTARGETS; i++) {
		if (!s->exists[i])
			continue;
		if (backup_path(path, targets[i]) < 0)
			return -1;
		if (!gs_exists(path) && write_atomic(path, s->data[i]) < 0)
			return -1;
	}
	return 0;
}

static int run_initramfs(void)
{
	char tool[PATH_MAX];
	const struct gs_initramfs_backend *b = gs_initramfs_find(tool, sizeof(tool));
	if (!b)
		return -1;
	fprintf(stderr, "gpushift-helper: regenerating the initramfs with %s...\n", b->name);
	return b->run(b, tool);
}

static int write_pending(gs_mode from, gs_mode to)
{
	char path[PATH_MAX], data[128];
	if (wpath(path, GS_PENDING_FILE) < 0)
		return -1;
	if (from == to || from == GS_MODE_NONE)
		return remove_file(path);
	snprintf(data, sizeof(data), "from=%s\nto=%s\n", gs_mode_name(from), gs_mode_name(to));
	return write_atomic(path, data);
}

static gs_status check_switchable(const gs_system *sys)
{
	switch (gs_switchability(sys)) {
	case GS_SWITCH_OK: return GS_OK;
	case GS_SWITCH_SINGLE_GPU: return GS_ERR_SINGLE_GPU;
	case GS_SWITCH_CONFLICT: return GS_ERR_CONFLICT;
	default: return GS_ERR_NOT_SWITCHABLE;
	}
}

static gs_status apply(gs_system *sys, gs_mode mode)
{
	static struct snapshot snap;
	char modprobe[CONTENT_MAX], udev[CONTENT_MAX], path[PATH_MAX];
	gs_status st = check_switchable(sys);
	if (st != GS_OK)
		return st;
	if (!gs_mode_available(sys, mode))
		return GS_ERR_MODE_UNAVAILABLE;
	if (!gs_sys_initramfs_tool(sys))
		return GS_ERR_NO_INITRAMFS;
	if (gs_config_build(sys, mode, modprobe, sizeof(modprobe), udev, sizeof(udev)) < 0)
		return GS_ERR_GENERIC;
	if (take_snapshot(&snap) < 0 || (!sys->has_state && backup_originals(&snap) < 0))
		return GS_ERR_IO;

	gs_mode from = gs_current_mode(sys);
	const struct gs_mux_backend *mux = sys->mux;
	gs_mode mux_prev = mux ? mux->get(mux) : GS_MODE_NONE;
	if (mux && mux->set(mux, mode) < 0)
		return GS_ERR_MUX;

	const char *contents[NTARGETS] = { modprobe, udev };
	st = GS_OK;
	for (size_t i = 0; i < NTARGETS && st == GS_OK; i++)
		if (wpath(path, targets[i]) < 0 || put_file(path, contents[i]) < 0)
			st = GS_ERR_IO;
	if (st == GS_OK && run_initramfs() < 0)
		st = GS_ERR_INITRAMFS_FAILED;
	if (st != GS_OK) {
		restore_snapshot(&snap);
		if (mux)
			mux->set(mux, mux_prev);
		return st;
	}

	struct gs_state state = sys->state;
	const struct gs_gpu *dgpu = gs_dgpu(sys);
	char data[1024];
	state.mode = mode;
	gs_strlcpy(state.dgpu, dgpu->address, sizeof(state.dgpu));
	state.dgpu_vendor = dgpu->vendor_id;
	state.dgpu_device = dgpu->device_id;
	if (mux && !sys->has_state) {
		gs_strlcpy(state.mux_backend, mux->name, sizeof(state.mux_backend));
		gs_strlcpy(state.mux_orig, gs_mode_name(mux_prev), sizeof(state.mux_orig));
	}
	if (gs_state_format(&state, data, sizeof(data)) < 0 || wpath(path, GS_STATE_FILE) < 0 ||
	    write_atomic(path, data) < 0 || write_pending(from, mode) < 0)
		return GS_ERR_IO;
	return GS_OK;
}

static gs_status reset(gs_system *sys)
{
	static struct snapshot snap;
	char path[PATH_MAX], backup[PATH_MAX], data[CONTENT_MAX];
	if (take_snapshot(&snap) < 0)
		return GS_ERR_IO;
	bool any = sys->has_state;
	for (size_t i = 0; i < NTARGETS; i++)
		any |= snap.exists[i];
	if (!any) {
		msg("nothing to reset");
		return GS_OK;
	}
	if (!gs_sys_initramfs_tool(sys))
		return GS_ERR_NO_INITRAMFS;

	gs_mode from = gs_current_mode(sys), orig = GS_MODE_NONE;
	const struct gs_mux_backend *mux = gs_mux_find(sys->state.mux_backend);
	gs_mode mux_prev = mux ? mux->get(mux) : GS_MODE_NONE;
	if (mux && gs_mode_from_name(sys->state.mux_orig, &orig) && mux->set(mux, orig) < 0)
		return GS_ERR_MUX;

	gs_status st = GS_OK;
	for (size_t i = 0; i < NTARGETS && st == GS_OK; i++) {
		if (wpath(path, targets[i]) < 0 || backup_path(backup, targets[i]) < 0)
			st = GS_ERR_IO;
		else if (gs_exists(backup))
			st = (gs_read_file(backup, data, sizeof(data)) < 0 ||
			      write_atomic(path, data) < 0) ? GS_ERR_IO : GS_OK;
		else if (remove_file(path) < 0)
			st = GS_ERR_IO;
	}
	if (st == GS_OK && run_initramfs() < 0)
		st = GS_ERR_INITRAMFS_FAILED;
	if (st != GS_OK) {
		restore_snapshot(&snap);
		if (mux && orig != GS_MODE_NONE)
			mux->set(mux, mux_prev);
		return st;
	}

	for (size_t i = 0; i < NTARGETS; i++)
		if (backup_path(backup, targets[i]) == 0)
			remove_file(backup);
	if (wpath(path, GS_STATE_FILE) < 0 || remove_file(path) < 0)
		return GS_ERR_IO;
	static const char *const dirs[] = { GS_BACKUP_DIR, "/var/lib/gpushift" };
	for (size_t i = 0; i < 2; i++)
		if (wpath(path, dirs[i]) == 0)
			rmdir(path);
	return write_pending(from, GS_MODE_DEFAULT) < 0 ? GS_ERR_IO : GS_OK;
}

static void usage(void)
{
	fputs("Usage: gpushift-helper apply integrated|hybrid|dedicated\n"
	      "       gpushift-helper reset\n", stderr);
}

int main(int argc, char **argv)
{
	umask(022);
#ifdef GPUSHIFT_TEST_HOOKS
	const char *root = getenv("GPUSHIFT_ETC_ROOT");
	if (!root || root[0] != '/') {
		msg("test build: refusing to run without an absolute GPUSHIFT_ETC_ROOT");
		return GS_ERR_USAGE;
	}
#else
	unsetenv("GPUSHIFT_SYSFS_ROOT");
	unsetenv("GPUSHIFT_ETC_ROOT");
#endif

	gs_mode mode = GS_MODE_NONE;
	bool is_apply = argc == 3 && strcmp(argv[1], "apply") == 0;
	if (is_apply ? !gs_mode_from_name(argv[2], &mode) || mode == GS_MODE_DEFAULT
		     : !(argc == 2 && strcmp(argv[1], "reset") == 0)) {
		usage();
		return GS_ERR_USAGE;
	}
#ifndef GPUSHIFT_TEST_HOOKS
	if (geteuid() != 0) {
		msg("must be run as root (through pkexec)");
		return GS_ERR_PERMISSION;
	}
#endif

	gs_system *sys = gs_detect();
	if (!sys)
		return GS_ERR_GENERIC;
	gs_status st = is_apply ? apply(sys, mode) : reset(sys);
	gs_system_free(sys);
	if (st != GS_OK)
		fprintf(stderr, "gpushift-helper: %s\n", gs_strerror(st));
	else
		msg("done; reboot to apply the change");
	return st;
}
