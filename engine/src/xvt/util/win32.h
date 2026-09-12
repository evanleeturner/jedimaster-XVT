#ifndef XVT_UTIL_WIN32_H
#define XVT_UTIL_WIN32_H

#include "aeron/compat/win_types.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Win32 ABI types referenced by recovered code. */
typedef struct RECT {
	int32_t left;
	int32_t top;
	int32_t right;
	int32_t bottom;
} RECT;

typedef struct POINT {
	int32_t x;
	int32_t y;
} POINT;

typedef struct SIZE {
	int32_t cx;
	int32_t cy;
} SIZE;

typedef struct BITMAP {
	int32_t bmType;
	int32_t bmWidth;
	int32_t bmHeight;
	int32_t bmWidthBytes;
	uint16_t bmPlanes;
	uint16_t bmBitsPixel;
	void* bmBits;
} BITMAP;

#pragma pack(push, 1)

typedef struct BITMAPFILEHEADER {
	uint16_t bfType;
	uint32_t bfSize;
	uint16_t bfReserved1;
	uint16_t bfReserved2;
	uint32_t bfOffBits;
} BITMAPFILEHEADER;

typedef struct BITMAPINFOHEADER {
	uint32_t biSize;
	int32_t biWidth;
	int32_t biHeight;
	uint16_t biPlanes;
	uint16_t biBitCount;
	uint32_t biCompression;
	uint32_t biSizeImage;
	int32_t biXPelsPerMeter;
	int32_t biYPelsPerMeter;
	uint32_t biClrUsed;
	uint32_t biClrImportant;
} BITMAPINFOHEADER;

#pragma pack(pop)

typedef char xvt_size_BITMAPFILEHEADER[(sizeof(BITMAPFILEHEADER) == 14) ? 1 : -1];
typedef char xvt_size_BITMAPINFOHEADER[(sizeof(BITMAPINFOHEADER) == 40) ? 1 : -1];

int Win32_CreateProcessFromCommandLine(char* commandLine);

#ifdef __cplusplus
}
#endif

#endif
