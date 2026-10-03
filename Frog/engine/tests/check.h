/* check.h — the two macros every engine test uses. A test passes when it
 * prints "ALL CHECKS PASSED" and exits 0. */
#pragma once

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                          \
	do {                                                                     \
		g_checks++;                                                          \
		if (!(cond)) {                                                       \
			g_failures++;                                                    \
			fprintf(stderr, "CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		}                                                                    \
	} while (0)

#define CHECK_NEAR(a, b, eps)                                                \
	do {                                                                     \
		g_checks++;                                                          \
		double _a = (double)(a), _b = (double)(b);                           \
		if (!(fabs(_a - _b) <= (eps))) {                                     \
			g_failures++;                                                    \
			fprintf(stderr, "CHECK_NEAR FAILED %s:%d: %s = %.9g vs %s = %.9g\n", \
			        __FILE__, __LINE__, #a, _a, #b, _b);                     \
		}                                                                    \
	} while (0)

static int check_report(const char* name) {
	if (g_failures == 0) {
		printf("%s: %d checks\nALL CHECKS PASSED\n", name, g_checks);
		return 0;
	}
	printf("%s: %d of %d checks FAILED\n", name, g_failures, g_checks);
	return 1;
}
