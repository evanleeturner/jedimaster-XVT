#ifndef XVT_ASSETS_MEMORY_BUFFER_H
#define XVT_ASSETS_MEMORY_BUFFER_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t MemoryBuffer_ReadByte(const uint8_t* buffer, unsigned int* offset);
uint16_t MemoryBuffer_ReadWord(const uint8_t* buffer, unsigned int* offset);
unsigned int MemoryBuffer_ReadDword(const uint8_t* buffer, unsigned int* offset);
void MemoryBuffer_ReadCount(const uint8_t* buffer, void* destination, unsigned int* offset,
							unsigned int count);
void MemoryBuffer_WriteByte(uint8_t* buffer, unsigned int* offset, uint8_t value);
void MemoryBuffer_WriteWord(uint8_t* buffer, unsigned int* offset, uint16_t value);
void MemoryBuffer_WriteDword(uint8_t* buffer, unsigned int* offset, unsigned int value);
void MemoryBuffer_WriteCount(uint8_t* buffer, const void* source, unsigned int* offset, unsigned int count);

#ifdef __cplusplus
}
#endif

#endif
