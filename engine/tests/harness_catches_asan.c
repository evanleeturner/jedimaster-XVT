/* Reads one byte past a heap block. CTest expects AddressSanitizer to
 * report it; if the report is missing, tests are running without ASan. */
#include <stdlib.h>

int main(void)
{
	char *block = malloc(8);
	volatile char *bytes = block;
	int past_end = bytes[8];
	free(block);
	return past_end == 0x7f;
}
