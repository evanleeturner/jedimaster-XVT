#ifndef XVT_UTIL_WIN32_H
#define XVT_UTIL_WIN32_H

#include "aeron/compat/win_types.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

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

struct BITMAP {
	int32_t bm_type;	/* Bitmap type; nothing reads it. */
	int32_t bm_width;	/* Width in pixels. */
	int32_t bm_height;	/* Height in pixels. */
	int32_t bm_width_bytes; /* Bytes per row; nothing reads it. */
	uint16_t bm_planes;	/* Color planes; nothing reads it. */
	uint16_t bm_bits_pixel; /* Bits per pixel; nothing reads it. */
	void *bm_bits;		/* The pixels; nothing reads it. */
};

#pragma pack(push, 1)

struct BITMAPFILEHEADER {
	/* 0x4D42 ("BM") in a bitmap file; the loader checks it. */
	uint16_t bf_type;
	uint32_t bf_size;      /* File size in bytes. */
	uint16_t bf_reserved1; /* Reserved; nothing reads it. */
	uint16_t bf_reserved2; /* Reserved; nothing reads it. */
	uint32_t bf_off_bits;  /* Offset in the file of the pixels. */
};

struct BITMAPINFOHEADER {
	uint32_t bi_size; /* Bytes in this header; nothing reads it. */
	int32_t bi_width; /* Width in pixels. */
	/* Height in pixels; the decoders take the rows as stored bottom up. */
	int32_t bi_height;
	/* Color planes; front_image_load_bmp_file decodes only 1. */
	uint16_t bi_planes;
	/* Bits per pixel; front_image_load_bmp_file decodes 4 and 8. */
	uint16_t bi_bit_count;
	/* 0 for plain rows, 1 for run-length rows; only
	 * front_image_decode_bmp8bpp decodes 1. */
	uint32_t bi_compression;
	/* Bytes of pixels; read only for a run-length image. */
	uint32_t bi_size_image;
	int32_t bi_x_pels_per_meter; /* Horizontal resolution; nothing reads it. */
	int32_t bi_y_pels_per_meter; /* Vertical resolution; nothing reads it. */
	uint32_t bi_clr_used; /* Palette colors used; nothing reads it. */
	uint32_t
		bi_clr_important; /* Palette colors needed; nothing reads it. */
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
