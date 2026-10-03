/* Overflows a signed int. CTest expects UndefinedBehaviorSanitizer to
 * report it; if the report is missing, tests are running without UBSan. */
#include <limits.h>

int main(void)
{
	volatile int value = INT_MAX;
	value = value + 1;
	return value == 0;
}
