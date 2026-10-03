/* Checks the test harness itself: each assertion passes on a true
 * statement, and the tolerance compare rejects what it should. */
#include "test_assert.h"

int main(void)
{
	XVT_ASSERT_TRUE(1 + 1 == 2);
	XVT_ASSERT_INT_EQ(-3 * 7, -21);

	/* 0.1 + 0.2 misses 0.3 by one rounding step of a double, about 1.1e-16
	 * relative; 1e-15 allows that step and nothing larger. */
	XVT_ASSERT_CLOSE(0.1 + 0.2, 0.3, 1e-15,
			 "one rounding step of double addition");

	XVT_ASSERT_TRUE(xvt_test_close(0.0, 0.0, 0.0));
	XVT_ASSERT_TRUE(!xvt_test_close(1.0, 1.0 + 1e-9, 1e-12));
	XVT_ASSERT_TRUE(!xvt_test_close(NAN, NAN, 1.0));
	XVT_ASSERT_TRUE(!xvt_test_close(1.0, NAN, 1.0));
	return 0;
}
