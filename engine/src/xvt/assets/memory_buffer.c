#include "xvt/assets/memory_buffer.h"

#include <string.h>

/* Returns the byte at buffer + *offset and adds 1 to *offset. Nothing calls
 * this. */
// FUNCTION: XVT 0x4CC7D0
uint8_t memory_buffer_read_byte(const uint8_t *buffer, unsigned int *offset)
{
	unsigned int position = *offset;
	uint8_t value = buffer[position];

	*offset = position + 1;
	return value;
}

/* Returns the 16-bit value at buffer + *offset, in the machine's byte order,
 * and adds 2 to *offset. Nothing calls this. */
// FUNCTION: XVT 0x4CC7F0
uint16_t memory_buffer_read_word(const uint8_t *buffer, unsigned int *offset)
{
	unsigned int position = *offset;
	uint16_t value;

	memcpy(&value, buffer + position, sizeof(value));
	*offset = position + sizeof(value);
	return value;
}

/* Returns the 32-bit value at buffer + *offset, in the machine's byte order,
 * and adds 4 to *offset. Nothing calls this. */
// FUNCTION: XVT 0x4CC810
unsigned int memory_buffer_read_dword(const uint8_t *buffer,
				      unsigned int *offset)
{
	unsigned int position = *offset;
	unsigned int value;

	memcpy(&value, buffer + position, sizeof(value));
	*offset = position + sizeof(value);
	return value;
}

/* Copies count bytes from buffer + *offset to destination and adds count to
 * *offset. Nothing calls this. */
// FUNCTION: XVT 0x4CC830
void memory_buffer_read_bytes(const uint8_t *buffer, void *destination,
			      unsigned int *offset, unsigned int count)
{
	memcpy(destination, &buffer[*offset], count);
	*offset += count;
}

/* Stores value at buffer + *offset and adds 1 to *offset. Nothing calls
 * this. */
// FUNCTION: XVT 0x4CC860
void memory_buffer_write_byte(uint8_t *buffer, unsigned int *offset,
			      uint8_t value)
{
	buffer[(*offset)++] = value;
}

/* Stores value at buffer + *offset, in the machine's byte order, and adds 2 to
 * *offset. Nothing calls this. */
// FUNCTION: XVT 0x4CC880
void memory_buffer_write_word(uint8_t *buffer, unsigned int *offset,
			      uint16_t value)
{
	unsigned int position = *offset;

	memcpy(buffer + position, &value, sizeof(value));
	*offset = position + sizeof(value);
}

/* Stores value at buffer + *offset, in the machine's byte order, and adds 4 to
 * *offset. Nothing calls this. */
// FUNCTION: XVT 0x4CC8A0
void memory_buffer_write_dword(uint8_t *buffer, unsigned int *offset,
			       unsigned int value)
{
	unsigned int position = *offset;

	memcpy(buffer + position, &value, sizeof(value));
	*offset = position + sizeof(value);
}

/* Copies count bytes from source to buffer + *offset and adds count to *offset.
 * Nothing calls this. */
// FUNCTION: XVT 0x4CC8C0
void memory_buffer_write_bytes(uint8_t *buffer, const void *source,
			       unsigned int *offset, unsigned int count)
{
	memcpy(&buffer[*offset], source, count);
	*offset += count;
}
