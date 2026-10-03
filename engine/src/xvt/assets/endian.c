#include "xvt/assets/endian.h"

/* Returns value with its two bytes swapped. Nothing calls this. */
// FUNCTION: XVT 0x4CC8F0
uint16_t endian_swap16(uint16_t value)
{
	return (uint16_t)((value >> 8) | (value << 8));
}

/* Returns value with its four bytes in reverse order. Nothing calls this. */
// FUNCTION: XVT 0x4CC900
unsigned int endian_swap32(unsigned int value)
{
	unsigned int high_byte;
	unsigned int middle_byte;
	unsigned int result;

	result = ((value << 16) | (value & 0xff00u)) << 8;
	middle_byte = (value & 0xff0000u) >> 8;
	high_byte = value >> 24;
	result |= middle_byte;
	result |= high_byte;
	return result;
}
