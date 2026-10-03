#ifndef XVT_ASSETS_MEMORY_BUFFER_H
#define XVT_ASSETS_MEMORY_BUFFER_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t memory_buffer_read_byte(const uint8_t *buffer, unsigned int *offset);
uint16_t memory_buffer_read_word(const uint8_t *buffer, unsigned int *offset);
unsigned int memory_buffer_read_dword(const uint8_t *buffer,
				      unsigned int *offset);
void memory_buffer_read_bytes(const uint8_t *buffer, void *destination,
			      unsigned int *offset, unsigned int count);
void memory_buffer_write_byte(uint8_t *buffer, unsigned int *offset,
			      uint8_t value);
void memory_buffer_write_word(uint8_t *buffer, unsigned int *offset,
			      uint16_t value);
void memory_buffer_write_dword(uint8_t *buffer, unsigned int *offset,
			       unsigned int value);
void memory_buffer_write_bytes(uint8_t *buffer, const void *source,
			       unsigned int *offset, unsigned int count);

#ifdef __cplusplus
}
#endif

#endif
