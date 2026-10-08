/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Unprivileged side: runs gpushift-helper through pkexec. */
#include "internal.h"

#include <unistd.h>

extern char **environ;

static gs_status run_helper(const char *cmd, const char *arg)
{
	static const char *const pkexec_paths[] = { "/usr/bin/pkexec", "/bin/pkexec" };
	char *argv[5];
	size_t i = 0;
	bool via_pkexec = geteuid() != 0;

	if (via_pkexec) {
		for (size_t p = 0; p < 2 && i == 0; p++)
			if (access(pkexec_paths[p], X_OK) == 0)
				argv[i++] = (char *)pkexec_paths[p];
		if (i == 0)
			return GS_ERR_PERMISSION;
	}
	argv[i++] = (char *)GPUSHIFT_HELPER_PATH;
	argv[i++] = (char *)cmd;
	if (arg)
		argv[i++] = (char *)arg;
	argv[i] = NULL;

	int rc = gs_spawn(argv[0], argv, environ);
	if (rc < 0)
		return GS_ERR_GENERIC;
	/* pkexec: 126 = dialog dismissed, 127 = not authorized. */
	if (via_pkexec && (rc == 126 || rc == 127))
		return GS_ERR_AUTH;
	return rc <= GS_ERR_NOT_AWAITING ? (gs_status)rc : GS_ERR_GENERIC;
}

gs_status gs_apply_mode(gs_mode mode)
{
	if (mode != GS_MODE_INTEGRATED && mode != GS_MODE_HYBRID && mode != GS_MODE_DEDICATED)
		return GS_ERR_MODE_UNAVAILABLE;
	return run_helper("apply", gs_mode_name(mode));
}

gs_status gs_reset(void)
{
	return run_helper("reset", NULL);
}

gs_status gs_confirm(void)
{
	return run_helper("confirm", NULL);
}

gs_status gs_revert(void)
{
	return run_helper("revert", NULL);
}
