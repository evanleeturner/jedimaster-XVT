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
	int32_t left;	/* X of the left edge. */
	int32_t top;	/* Y of the top edge. */
	int32_t right;	/* X of the right edge. */
	int32_t bottom; /* Y of the bottom edge. */
} RECT;

typedef struct POINT {
	int32_t x; /* Horizontal coordinate. */
	int32_t y; /* Vertical coordinate. */
} POINT;

typedef struct SIZE {
	int32_t cx; /* Width. */
	int32_t cy; /* Height. */
} SIZE;

typedef struct BITMAP {
	int32_t bmType;	      /* Bitmap type; nothing reads it. */
	int32_t bmWidth;      /* Width in pixels. */
	int32_t bmHeight;     /* Height in pixels. */
	int32_t bmWidthBytes; /* Bytes per row; nothing reads it. */
	uint16_t bmPlanes;    /* Color planes; nothing reads it. */
	uint16_t bmBitsPixel; /* Bits per pixel; nothing reads it. */
	void *bmBits;	      /* The pixels; nothing reads it. */
} BITMAP;

#pragma pack(push, 1)

typedef struct BITMAPFILEHEADER {
	/* 0x4D42 ("BM") in a bitmap file; the loader checks it. */
	uint16_t bfType;
	uint32_t bfSize;      /* File size in bytes. */
	uint16_t bfReserved1; /* Reserved; nothing reads it. */
	uint16_t bfReserved2; /* Reserved; nothing reads it. */
	uint32_t bfOffBits;   /* Offset in the file of the pixels. */
} BITMAPFILEHEADER;

typedef struct BITMAPINFOHEADER {
	uint32_t biSize; /* Bytes in this header; nothing reads it. */
	int32_t biWidth; /* Width in pixels. */
	/* Height in pixels; the decoders take the rows as stored bottom up. */
	int32_t biHeight;
	/* Color planes; FrontImage_LoadBmpFile decodes only 1. */
	uint16_t biPlanes;
	/* Bits per pixel; FrontImage_LoadBmpFile decodes 4 and 8. */
	uint16_t biBitCount;
	/* 0 for plain rows, 1 for run-length rows; only
	 * FrontImage_DecodeBmp8bpp decodes 1. */
	uint32_t biCompression;
	/* Bytes of pixels; read only for a run-length image. */
	uint32_t biSizeImage;
	int32_t biXPelsPerMeter; /* Horizontal resolution; nothing reads it. */
	int32_t biYPelsPerMeter; /* Vertical resolution; nothing reads it. */
	uint32_t biClrUsed;	 /* Palette colors used; nothing reads it. */
	uint32_t biClrImportant; /* Palette colors needed; nothing reads it. */
} BITMAPINFOHEADER;

#pragma pack(pop)

typedef char
	xvt_size_BITMAPFILEHEADER[(sizeof(BITMAPFILEHEADER) == 14) ? 1 : -1];
typedef char
	xvt_size_BITMAPINFOHEADER[(sizeof(BITMAPINFOHEADER) == 40) ? 1 : -1];

int Win32_CreateProcessFromCommandLine(char *commandLine);

#ifdef __cplusplus
}
#endif

#endif
