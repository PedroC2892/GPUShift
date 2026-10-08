/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GPUSHIFT_TEST_FIXTURE_H
#define GPUSHIFT_TEST_FIXTURE_H

#include "gpushift/gpushift.h"

#include <stdio.h>
#include <stdlib.h>

/* Points the library at tests/fixtures/<name> and runs detection. */
static gs_system *load_fixture(const char *name)
{
	char root[1024];
	snprintf(root, sizeof(root), "%s/%s", GPUSHIFT_FIXTURES, name);
	setenv("GPUSHIFT_SYSFS_ROOT", root, 1);
	gs_system *sys = gs_detect();
	if (!sys) {
		fprintf(stderr, "gs_detect failed for %s\n", name);
		exit(EXIT_FAILURE);
	}
	return sys;
}

#endif
