#include "xvt/assets/memory_buffer.h"
#ifdef XVT_MODERN
#include <string.h>
#endif

// FUNCTION: XVT 0x4CC7D0
uint8_t MemoryBuffer_ReadByte(const uint8_t* buffer, unsigned int* offset) {
	unsigned int position = *offset;
	uint8_t value = buffer[position];

	*offset = position + 1;
	return value;
}

// FUNCTION: XVT 0x4CC7F0
uint16_t MemoryBuffer_ReadWord(const uint8_t* buffer, unsigned int* offset) {
#ifdef XVT_MODERN
	unsigned int position = *offset;
	uint16_t value;

	memcpy(&value, buffer + position, sizeof(value));
	*offset = position + sizeof(value);
	return value;
#else
	uint16_t value = *(const uint16_t*)&buffer[*offset];

	*offset += sizeof(value);
	return value;
#endif
}

// FUNCTION: XVT 0x4CC810
unsigned int MemoryBuffer_ReadDword(const uint8_t* buffer, unsigned int* offset) {
#ifdef XVT_MODERN
	unsigned int position = *offset;
	unsigned int value;

	memcpy(&value, buffer + position, sizeof(value));
	*offset = position + sizeof(value);
	return value;
#else
	unsigned int value = *(const unsigned int*)&buffer[*offset];

	*offset += sizeof(value);
	return value;
#endif
}

// FUNCTION: XVT 0x4CC830
void MemoryBuffer_ReadCount(const uint8_t* buffer, void* destination, unsigned int* offset,
							unsigned int count) {
	memcpy(destination, &buffer[*offset], count);
	*offset += count;
}

// FUNCTION: XVT 0x4CC860
void MemoryBuffer_WriteByte(uint8_t* buffer, unsigned int* offset, uint8_t value) {
	buffer[(*offset)++] = value;
}

// FUNCTION: XVT 0x4CC880
void MemoryBuffer_WriteWord(uint8_t* buffer, unsigned int* offset, uint16_t value) {
#ifdef XVT_MODERN
	unsigned int position = *offset;

	memcpy(buffer + position, &value, sizeof(value));
	*offset = position + sizeof(value);
#else
	*(uint16_t*)&buffer[*offset] = value;
	*offset += sizeof(value);
#endif
}

// FUNCTION: XVT 0x4CC8A0
void MemoryBuffer_WriteDword(uint8_t* buffer, unsigned int* offset, unsigned int value) {
#ifdef XVT_MODERN
	unsigned int position = *offset;

	memcpy(buffer + position, &value, sizeof(value));
	*offset = position + sizeof(value);
#else
	*(unsigned int*)&buffer[*offset] = value;
	*offset += sizeof(value);
#endif
}

// FUNCTION: XVT 0x4CC8C0
void MemoryBuffer_WriteCount(uint8_t* buffer, const void* source, unsigned int* offset, unsigned int count) {
	memcpy(&buffer[*offset], source, count);
	*offset += count;
}
