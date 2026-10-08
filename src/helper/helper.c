/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * gpushift-helper: the only GPUShift program that changes the system from a
 * user session. It is started through pkexec and accepts only:
 *
 *   gpushift-helper apply integrated|hybrid|dedicated   (org.gpushift.helper)
 *   gpushift-helper reset [--force]                     (org.gpushift.helper)
 *   gpushift-helper confirm                             (org.gpushift.confirm)
 *   gpushift-helper revert                              (org.gpushift.revert)
 *
 * Exit codes are the gs_status values from gpushift.h.
 *
 * Test builds (GPUSHIFT_TEST_HOOKS) honour $GPUSHIFT_SYSFS_ROOT and
 * $GPUSHIFT_ETC_ROOT and refuse to run without the latter, so they can never
 * touch the real /etc. Release builds drop both variables.
 */
#include "ops.h"

#include <string.h>

static void usage(void)
{
	fputs("Usage: gpushift-helper apply integrated|hybrid|dedicated\n"
	      "       gpushift-helper reset [--force]\n"
	      "       gpushift-helper confirm|revert\n", stderr);
}

int main(int argc, char **argv)
{
	gs_status st = gs_op_init();
	if (st != GS_OK)
		return st;

	static const char *const simple[] = { "reset", "confirm", "revert" };
	gs_mode mode = GS_MODE_NONE;
	const char *cmd = argc >= 2 ? argv[1] : "";
	bool valid = false;
	if (strcmp(cmd, "apply") == 0)
		valid = argc == 3 && gs_mode_from_name(argv[2], &mode) && mode != GS_MODE_DEFAULT;
	for (size_t i = 0; i < 3; i++)
		valid |= argc == 2 && strcmp(cmd, simple[i]) == 0;
	bool force = argc == 3 && strcmp(cmd, "reset") == 0 && strcmp(argv[2], "--force") == 0;
	valid |= force;
	if (!valid) {
		usage();
		return GS_ERR_USAGE;
	}

	gs_system *sys = gs_detect();
	if (!sys)
		return GS_ERR_GENERIC;
	if (mode != GS_MODE_NONE)
		st = gs_op_apply(sys, mode);
	else if (strcmp(cmd, "reset") == 0)
		st = gs_op_reset(sys, force);
	else if (strcmp(cmd, "confirm") == 0)
		st = gs_op_confirm(sys);
	else
		st = gs_op_revert(sys);
	gs_system_free(sys);
	if (st != GS_OK)
		gs_op_msg(gs_strerror(st));
	else if (strcmp(cmd, "confirm") != 0)
		gs_op_msg("done; reboot to apply the change");
	return st;
}
