#include "xvt/assets/endian.h"

// FUNCTION: XVT 0x4CC8F0
uint16_t Endian_Swap16(uint16_t value) { return (uint16_t)((value >> 8) | (value << 8)); }

// FUNCTION: XVT 0x4CC900
unsigned int Endian_Swap32(unsigned int value) {
	unsigned int highByte;
	unsigned int middleByte;
	unsigned int result;

	result = ((value << 16) | (value & 0xff00u)) << 8;
	middleByte = (value & 0xff0000u) >> 8;
	highByte = value >> 24;
	result |= middleByte;
	result |= highByte;
	return result;
}
