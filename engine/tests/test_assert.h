/* Minimal assertions for the OpenXvT unit tests.
 *
 * Each test is a plain C program. It returns 0 when every check holds. The
 * first check that fails prints the file, the line and both values, then
 * exits with status 1, which CTest reports as a failure.
 *
 * Integers and flags compare exactly. Floating-point values compare with
 * XVT_ASSERT_CLOSE, a relative tolerance and a sentence saying why that
 * tolerance is right. NaN never compares close to anything, NaN included.
 * Test programs must not be built with -ffast-math: it folds isnan() to
 * false. */
#ifndef XVT_TESTS_TEST_ASSERT_H
#define XVT_TESTS_TEST_ASSERT_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* Returns 1 when actual is within rel_tol of expected, relative to the
 * larger magnitude of the two; 0 otherwise, and always 0 when either value
 * is NaN. Two exact zeros are close. */
static inline int xvt_test_close(double actual, double expected, double rel_tol)
{
	if (isnan(actual) || isnan(expected)) {
		return 0;
	}
	double scale = fmax(fabs(actual), fabs(expected));
	return fabs(actual - expected) <= rel_tol * scale;
}

static inline void xvt_test_check(int ok, const char *file, int line,
				  const char *expr)
{
	if (!ok) {
		fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expr);
		exit(1);
	}
}

static inline void xvt_test_check_int(long long actual, long long expected,
				      const char *file, int line,
				      const char *expr)
{
	if (actual != expected) {
		fprintf(stderr, "%s:%d: %s: %lld != %lld\n", file, line, expr,
			actual, expected);
		exit(1);
	}
}

static inline void xvt_test_check_close(double actual, double expected,
					double rel_tol, const char *why,
					const char *file, int line,
					const char *expr)
{
	if (!xvt_test_close(actual, expected, rel_tol)) {
		fprintf(stderr,
			"%s:%d: %s: %.17g not within %g of %.17g (%s)\n", file,
			line, expr, actual, rel_tol, expected, why);
		exit(1);
	}
}

#define XVT_ASSERT_TRUE(cond)                                                  \
	xvt_test_check((cond) ? 1 : 0, __FILE__, __LINE__, #cond)
#define XVT_ASSERT_INT_EQ(actual, expected)                                    \
	xvt_test_check_int((long long)(actual), (long long)(expected),         \
			   __FILE__, __LINE__, #actual)
#define XVT_ASSERT_CLOSE(actual, expected, rel_tol, why)                       \
	xvt_test_check_close((actual), (expected), (rel_tol), (why), __FILE__, \
			     __LINE__, #actual)

#endif
