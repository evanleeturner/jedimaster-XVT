#ifndef XVT_UTIL_WIN32_H
#define XVT_UTIL_WIN32_H

#include <stdint.h>

#include "aeron/compat/win_types.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Win32 ABI types referenced by recovered code. */
struct RECT {
	int32_t left;	/* X of the left edge. */
	int32_t top;	/* Y of the top edge. */
	int32_t right;	/* X of the right edge. */
	int32_t bottom; /* Y of the bottom edge. */
};

struct POINT {
	int32_t x; /* Horizontal coordinate. */
	int32_t y; /* Vertical coordinate. */
};

struct SIZE {
	int32_t cx; /* Width. */
	int32_t cy; /* Height. */
};

/* drift-ok: camelcase -- a copy of Windows' BITMAP */
struct BITMAP {
	int32_t bmType;	      /* Bitmap type; nothing reads it. */
	int32_t bmWidth;      /* Width in pixels. */
	int32_t bmHeight;     /* Height in pixels. */
	int32_t bmWidthBytes; /* Bytes per row; nothing reads it. */
	uint16_t bmPlanes;    /* Color planes; nothing reads it. */
	uint16_t bmBitsPixel; /* Bits per pixel; nothing reads it. */
	void *bmBits;	      /* The pixels; nothing reads it. */
};

#pragma pack(push, 1)

/* drift-ok: camelcase -- a copy of Windows' BITMAPFILEHEADER */
struct BITMAPFILEHEADER {
	/* 0x4D42 ("BM") in a bitmap file; the loader checks it. */
	uint16_t bfType;
	uint32_t bfSize;      /* File size in bytes. */
	uint16_t bfReserved1; /* Reserved; nothing reads it. */
	uint16_t bfReserved2; /* Reserved; nothing reads it. */
	uint32_t bfOffBits;   /* Offset in the file of the pixels. */
};

/* drift-ok: camelcase -- a copy of Windows' BITMAPINFOHEADER */
struct BITMAPINFOHEADER {
	uint32_t biSize; /* Bytes in this header; nothing reads it. */
	int32_t biWidth; /* Width in pixels. */
	/* Height in pixels; the decoders take the rows as stored bottom up. */
	int32_t biHeight;
	/* Color planes; front_image_load_bmp_file decodes only 1. */
	uint16_t biPlanes;
	/* Bits per pixel; front_image_load_bmp_file decodes 4 and 8. */
	uint16_t biBitCount;
	/* 0 for plain rows, 1 for run-length rows; only
	 * front_image_decode_bmp8bpp decodes 1. */
	uint32_t biCompression;
	/* Bytes of pixels; read only for a run-length image. */
	uint32_t biSizeImage;
	int32_t biXPelsPerMeter; /* Horizontal resolution; nothing reads it. */
	int32_t biYPelsPerMeter; /* Vertical resolution; nothing reads it. */
	uint32_t biClrUsed;	 /* Palette colors used; nothing reads it. */
	uint32_t biClrImportant; /* Palette colors needed; nothing reads it. */
};

#pragma pack(pop)

typedef char xvt_size_bitmapfileheader[(sizeof(struct BITMAPFILEHEADER) == 14)
					       ? 1
					       : -1];
typedef char xvt_size_bitmapinfoheader[(sizeof(struct BITMAPINFOHEADER) == 40)
					       ? 1
					       : -1];

int win32_create_process_from_command_line(char *command_line);

#ifdef __cplusplus
}
#endif

#endif
