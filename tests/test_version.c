/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gpushift/gpushift.h"
#include "test.h"

int main(void)
{
	CHECK(gs_version()[0] != '\0');
	return TEST_RESULT();
}
