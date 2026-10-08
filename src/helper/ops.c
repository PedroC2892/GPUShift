/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Privileged operations shared by gpushift-helper and gpushift-helper-test
 * and by gpushift-boot-check. Every operation re-detects the system and
 * validates the request itself; nothing from the caller is trusted.
 */
#include "ops.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CONTENT_MAX 4096

static const char *const targets[] = { GS_MODPROBE_FILE, GS_UDEV_FILE };
#define NTARGETS (sizeof(targets) / sizeof(targets[0]))

/* Saved contents of the targets, to roll back on failure. */
struct snapshot {
	bool exists[NTARGETS];
	char data[NTARGETS][CONTENT_MAX];
};

void gs_op_msg(const char *text)
{
	fprintf(stderr, "gpushift: %s\n", text);
}
#define msg gs_op_msg

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

static int previous_path(char *buf, const char *target)
{
	const char *base = strrchr(target, '/');
	return gs_wpath(buf, PATH_MAX, GS_PREVIOUS_DIR "/%s", base ? base + 1 : target);
}

/* Keeps the files of the mode being replaced; a missing file means "no file". */
static int save_previous(const struct snapshot *s)
{
	char path[PATH_MAX];
	for (size_t i = 0; i < NTARGETS; i++)
		if (previous_path(path, targets[i]) < 0 ||
		    (s->exists[i] ? write_atomic(path, s->data[i]) : remove_file(path)) < 0)
			return -1;
	return 0;
}

static int write_state(const struct gs_state *st)
{
	char path[PATH_MAX], data[1024];
	return (gs_state_format(st, data, sizeof(data)) < 0 || wpath(path, GS_STATE_FILE) < 0 ||
		write_atomic(path, data) < 0) ? -1 : 0;
}

void gs_op_log(const char *text)
{
	char path[PATH_MAX], stamp[32];
	time_t now = time(NULL);
	struct tm tm;
	strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%SZ", gmtime_r(&now, &tm));
	msg(text); /* stderr ends up in the journal for the systemd unit */
	if (wpath(path, GS_LOG_FILE) < 0)
		return;
	FILE *f = fopen(path, "a");
	if (f) {
		fprintf(f, "%s %s\n", stamp, text);
		fclose(f);
	}
}

typedef int (*path_fn)(char *buf, const char *target);

static int target_path(char *buf, const char *target)
{
	return wpath(buf, target);
}

/* Contents of the targets (or of their copies in another directory, by path function). */
static int take_snapshot_at(struct snapshot *s, path_fn where)
{
	char path[PATH_MAX];
	for (size_t i = 0; i < NTARGETS; i++) {
		if (where(path, targets[i]) < 0)
			return -1;
		s->exists[i] = gs_exists(path);
		s->data[i][0] = '\0';
		if (s->exists[i] && gs_read_file(path, s->data[i], CONTENT_MAX) < 0)
			return -1;
	}
	return 0;
}

static void restore_snapshot_at(const struct snapshot *s, path_fn where)
{
	char path[PATH_MAX];
	for (size_t i = 0; i < NTARGETS; i++) {
		if (where(path, targets[i]) < 0)
			continue;
		if (s->exists[i] ? write_atomic(path, s->data[i]) : remove_file(path))
			fprintf(stderr, "gpushift: could not restore %s\n", path);
	}
}

static int take_snapshot(struct snapshot *s)
{
	return take_snapshot_at(s, target_path);
}

static void restore_snapshot(const struct snapshot *s)
{
	restore_snapshot_at(s, target_path);
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
	fprintf(stderr, "gpushift: regenerating the initramfs with %s...\n", b->name);
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

/* Undoes the intent written by an apply that failed: state and previous files. */
static void restore_old_state(const gs_system *sys, const struct snapshot *prev)
{
	char path[PATH_MAX];
	restore_snapshot_at(prev, previous_path);
	if (sys->has_state ? write_state(&sys->state) < 0
			   : (wpath(path, GS_STATE_FILE) < 0 || remove_file(path) < 0))
		msg("could not restore the previous state file");
}

gs_status gs_op_apply(gs_system *sys, gs_mode mode)
{
	static struct snapshot snap, prev_snap;
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

	/* A change not yet booted keeps the previous mode it would revert to. */
	bool keep_previous = sys->has_state && sys->state.pending && sys->pending_to != GS_MODE_NONE;
	struct gs_state state = sys->state;
	const struct gs_gpu *dgpu = gs_dgpu(sys);
	state.mode = mode;
	state.previous_mode = keep_previous ? sys->state.previous_mode : from;
	state.pending = state.previous_mode != mode;
	state.boot_attempts = 0;
	state.auto_reboot = false;
	gs_strlcpy(state.dgpu, dgpu->address, sizeof(state.dgpu));
	state.dgpu_vendor = dgpu->vendor_id;
	state.dgpu_device = dgpu->device_id;
	if (gs_dgpu_functions(sys, state.dgpu_functions, sizeof(state.dgpu_functions)) < 0)
		return GS_ERR_GENERIC;
	if (mux && !sys->has_state) {
		gs_strlcpy(state.mux_backend, mux->name, sizeof(state.mux_backend));
		gs_strlcpy(state.mux_orig, gs_mode_name(mux_prev), sizeof(state.mux_orig));
	}

	/*
	 * Intent first: the state (with the original MUX value) and the files to
	 * revert to are on disk before anything changes, so a crash or power loss
	 * from here on still leaves an unconfirmed change the boot check reverts.
	 */
	if (take_snapshot_at(&prev_snap, previous_path) < 0)
		return GS_ERR_IO;
	if ((!keep_previous && save_previous(&snap) < 0) || write_state(&state) < 0) {
		restore_old_state(sys, &prev_snap);
		return GS_ERR_IO;
	}

	st = GS_OK;
	if (mux && mux->set(mux, mode) < 0)
		st = GS_ERR_MUX;
	const char *contents[NTARGETS] = { modprobe, udev };
	for (size_t i = 0; i < NTARGETS && st == GS_OK; i++)
		if (wpath(path, targets[i]) < 0 || put_file(path, contents[i]) < 0)
			st = GS_ERR_IO;
	if (st == GS_OK && run_initramfs() < 0)
		st = GS_ERR_INITRAMFS_FAILED;
	if (st != GS_OK) {
		restore_snapshot(&snap);
		if (mux)
			mux->set(mux, mux_prev);
		restore_old_state(sys, &prev_snap);
		return st;
	}

	if (write_pending(from, mode) < 0 ||
	    wpath(path, GS_RECOVERY_FILE) < 0 || write_atomic(path, gs_recovery_text()) < 0)
		return GS_ERR_IO;
	return GS_OK;
}

gs_status gs_op_reset(gs_system *sys)
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

	for (size_t i = 0; i < NTARGETS; i++) {
		if (backup_path(backup, targets[i]) == 0)
			remove_file(backup);
		if (previous_path(backup, targets[i]) == 0)
			remove_file(backup);
	}
	if (wpath(path, GS_STATE_FILE) < 0 || remove_file(path) < 0)
		return GS_ERR_IO;
	static const char *const files[] = { GS_LOG_FILE, GS_RECOVERY_FILE };
	for (size_t i = 0; i < 2; i++)
		if (wpath(path, files[i]) == 0)
			remove_file(path);
	static const char *const dirs[] = { GS_PREVIOUS_DIR, GS_BACKUP_DIR, "/var/lib/gpushift" };
	for (size_t i = 0; i < 3; i++)
		if (wpath(path, dirs[i]) == 0)
			rmdir(path);
	return write_pending(from, GS_MODE_DEFAULT) < 0 ? GS_ERR_IO : GS_OK;
}

gs_status gs_op_confirm(gs_system *sys)
{
	if (!gs_awaiting_confirmation(sys))
		return GS_ERR_NOT_AWAITING;
	struct gs_state state = sys->state;
	state.pending = false;
	state.boot_attempts = 0;
	if (write_state(&state) < 0)
		return GS_ERR_IO;
	gs_op_log("mode change confirmed");
	return GS_OK;
}

/* Restores previous_mode from GS_PREVIOUS_DIR. The caller checks the state is pending. */
static gs_status revert(gs_system *sys, const char *reason)
{
	static struct snapshot snap;
	char path[PATH_MAX], prev[PATH_MAX], data[CONTENT_MAX];
	if (!gs_sys_initramfs_tool(sys))
		return GS_ERR_NO_INITRAMFS;
	if (take_snapshot(&snap) < 0)
		return GS_ERR_IO;

	gs_mode running = gs_current_mode(sys), target = sys->state.previous_mode;
	const struct gs_mux_backend *mux = sys->mux;
	gs_mode mux_prev = mux ? mux->get(mux) : GS_MODE_NONE;
	if (mux && target != GS_MODE_NONE && mux->set(mux, target) < 0)
		return GS_ERR_MUX;

	gs_status st = GS_OK;
	for (size_t i = 0; i < NTARGETS && st == GS_OK; i++) {
		if (wpath(path, targets[i]) < 0 || previous_path(prev, targets[i]) < 0)
			st = GS_ERR_IO;
		else if (gs_exists(prev))
			st = (gs_read_file(prev, data, sizeof(data)) < 0 ||
			      write_atomic(path, data) < 0) ? GS_ERR_IO : GS_OK;
		else if (remove_file(path) < 0)
			st = GS_ERR_IO;
	}
	if (st == GS_OK && run_initramfs() < 0)
		st = GS_ERR_INITRAMFS_FAILED;
	if (st != GS_OK) {
		restore_snapshot(&snap);
		if (mux)
			mux->set(mux, mux_prev);
		return st;
	}

	struct gs_state state = sys->state;
	state.mode = target;
	state.pending = false;
	state.boot_attempts = 0;
	char line[256];
	snprintf(line, sizeof(line), "reverted to %s mode: %s", gs_mode_name(target), reason);
	gs_op_log(line);
	return (write_state(&state) < 0 || write_pending(running, target) < 0) ? GS_ERR_IO : GS_OK;
}

gs_status gs_op_revert(gs_system *sys)
{
	if (!gs_awaiting_confirmation(sys))
		return GS_ERR_NOT_AWAITING;
	return revert(sys, "requested from the user session");
}

static bool cmdline_has(const char *token)
{
	char path[PATH_MAX], buf[4096];
	if (gs_rpath(path, sizeof(path), "/proc/cmdline") < 0 || gs_read_file(path, buf, sizeof(buf)) < 0)
		return false;
	for (char *save = NULL, *w = strtok_r(buf, " \t\n", &save); w; w = strtok_r(NULL, " \t\n", &save))
		if (strcmp(w, token) == 0)
			return true;
	return false;
}

gs_status gs_op_boot_check(gs_system *sys, int (*reboot_fn)(void))
{
	char path[PATH_MAX], line[160];

	if (cmdline_has("gpushift.reset=1")) {
		bool changed = sys->has_state;
		for (size_t i = 0; i < NTARGETS; i++)
			changed |= wpath(path, targets[i]) == 0 && gs_exists(path);
		gs_op_log("gpushift.reset=1 on the kernel command line: resetting");
		gs_status st = gs_op_reset(sys);
		/* Rebooting only when something changed means a permanent parameter cannot loop. */
		if (st == GS_OK && changed && reboot_fn() < 0)
			gs_op_log("reboot failed; reboot manually to finish the reset");
		return st;
	}
	if (!sys->has_state || !sys->state.pending)
		return GS_OK;

	struct gs_state *state = &sys->state;
	state->boot_attempts++;
	snprintf(line, sizeof(line), "unconfirmed boot %d of %d after switching to %s mode",
		 state->boot_attempts, GS_MAX_BOOT_ATTEMPTS, gs_mode_name(state->mode));
	gs_op_log(line);
	if (state->boot_attempts < GS_MAX_BOOT_ATTEMPTS)
		return write_state(state) < 0 ? GS_ERR_IO : GS_OK;
	if (state->auto_reboot) {
		gs_op_log("already rebooted once for this change; not rebooting again, use 'gpushift reset'");
		return write_state(state) < 0 ? GS_ERR_IO : GS_OK;
	}
	/* Recorded before rebooting so a failed revert can never cause a reboot loop. */
	state->auto_reboot = true;
	if (write_state(state) < 0)
		return GS_ERR_IO;
	snprintf(line, sizeof(line), "no confirmation after %d boots", GS_MAX_BOOT_ATTEMPTS);
	gs_status st = revert(sys, line);
	if (st != GS_OK) {
		gs_op_log(gs_strerror(st));
		return st;
	}
	if (reboot_fn() < 0)
		gs_op_log("reboot failed; reboot manually to finish the revert");
	return GS_OK;
}

/* Common start of the privileged programs; returns GS_OK or the exit status. */
gs_status gs_op_init(void)
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
	if (geteuid() != 0) {
		msg("must be run as root");
		return GS_ERR_PERMISSION;
	}
#endif
	return GS_OK;
}
