/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Minimal assertion helpers shared by the unit tests. */
#ifndef GPUSHIFT_TEST_H
#define GPUSHIFT_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int test_failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
		test_failures++; \
	} \
} while (0)

#define CHECK_STR(a, b) do { \
	const char *a_ = (a), *b_ = (b); \
	if (!a_ || !b_ || strcmp(a_, b_) != 0) { \
		fprintf(stderr, "%s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, \
			a_ ? a_ : "(null)", b_ ? b_ : "(null)"); \
		test_failures++; \
	} \
} while (0)

#define TEST_RESULT() (test_failures ? EXIT_FAILURE : EXIT_SUCCESS)

#endif
