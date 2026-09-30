/* A deliberately failing test. CTest expects it to fail; if it ever
 * passes, the assertions have stopped reporting failures. */
#include "test_assert.h"

int main(void) {
	XVT_ASSERT_INT_EQ(1, 2);
	return 0;
}
