/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * gpushift-boot-check: run once per boot by gpushift-boot-check.service,
 * before the display manager. It counts boots after an unconfirmed mode
 * change and reverts it after GS_MAX_BOOT_ATTEMPTS, and resets everything
 * when the kernel command line has gpushift.reset=1. It reboots at most once
 * per change. Exit codes are the gs_status values from gpushift.h.
 */
#include "ops.h"

#include <stdlib.h>
#include <unistd.h>

#ifdef GPUSHIFT_TEST_HOOKS
/* Tests record the reboot instead of doing it. */
static int do_reboot(void)
{
	char path[PATH_MAX];
	if (gs_wpath(path, sizeof(path), "/run/gpushift-test-reboots") < 0)
		return -1;
	FILE *f = fopen(path, "a");
	if (!f)
		return -1;
	fputs("reboot\n", f);
	return fclose(f);
}
#else
static int do_reboot(void)
{
	static const char *const paths[] = { "/usr/bin/systemctl", "/bin/systemctl" };
	static char *const envp[] = { "PATH=/usr/sbin:/usr/bin:/sbin:/bin", NULL };
	for (size_t i = 0; i < 2; i++) {
		if (access(paths[i], X_OK) != 0)
			continue;
		/* --no-block: this oneshot unit must finish for the shutdown to proceed. */
		char *argv[] = { (char *)paths[i], "--no-block", "reboot", NULL };
		return gs_spawn(paths[i], argv, envp, false) == 0 ? 0 : -1;
	}
	return -1;
}
#endif

int main(int argc, char **argv)
{
	(void)argv;
	gs_status st = gs_op_init();
	if (st != GS_OK)
		return st;
	if (argc != 1) {
		fputs("Usage: gpushift-boot-check\n", stderr);
		return GS_ERR_USAGE;
	}
	gs_system *sys = gs_detect();
	if (!sys)
		return GS_ERR_GENERIC;
	st = gs_op_boot_check(sys, do_reboot);
	gs_system_free(sys);
	return st;
}
