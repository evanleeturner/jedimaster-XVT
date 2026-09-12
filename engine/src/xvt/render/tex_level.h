#ifndef XVT_RENDER_TEX_LEVEL_H
#define XVT_RENDER_TEX_LEVEL_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

typedef struct TexLevelHeader {
	uint32_t dataSize;
	uint32_t reserved04[2];
	uint32_t paletteOffset;
	uint32_t imageOffsetTableOffset;
	uint32_t palette8Offset;
	uint32_t imageCount;
	uint32_t reserved1C[4];
	uint32_t bitsPerPixel;
	uint32_t paletteColorCount;
} TexLevelHeader;

typedef struct TexLevelImageHeader {
	uint32_t reserved00;
	uint32_t paletteOffset;
	uint32_t encodedImageOffset;
	uint32_t palette8Offset;
	uint32_t width;
	uint32_t height;
	uint32_t reserved18[2];
	uint32_t packingMode;
	uint32_t bitsPerPixel;
	uint32_t paletteColorCount;
} TexLevelImageHeader;

#pragma pack(pop)
typedef char xvt_size_TexLevelHeader[(sizeof(TexLevelHeader) == 0x34) ? 1 : -1];
typedef char xvt_size_TexLevelImageHeader[(sizeof(TexLevelImageHeader) == 0x2C) ? 1 : -1];

unsigned int TexLevel_Convert24BppPalettesTo16Bpp(unsigned int* texLevel);
unsigned int TexLevel_Convert24BppPalettesTo8Bpp(unsigned int* texLevel);

#ifdef __cplusplus
}
#endif

#endif
