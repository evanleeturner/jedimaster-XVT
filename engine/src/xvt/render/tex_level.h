#ifndef XVT_RENDER_TEX_LEVEL_H
#define XVT_RENDER_TEX_LEVEL_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

struct tex_level_header {
	/* Bytes of the block as read; the converted palettes go right after
	 * them. */
	uint32_t data_size;
	/* Entry 0 is the count of colors fe_disk_io_load_resources makes room for
	 * after the block, g_flight_bytes_per_pixel bytes each, for the converted
	 * palettes; it reads the count from the file ahead of the rest and
	 * stores it here through the block's second word. Entry 1 comes from
	 * the file with the rest of the block, and nothing reads it. Neither
	 * entry is used by name. */
	uint32_t reserved04[2];
	/* Offset of the block's own palette from the header, 4 bytes per
	 * color. */
	uint32_t palette_offset;
	/* Offset from the header of the table of image offsets, one 32-bit
	 * offset from the header per image. */
	uint32_t image_offset_table_offset;
	/* Offset from the header of the block palette converted for the
	 * display; set by the TexLevel_Convert functions when the palette is
	 * 24-bit. */
	uint32_t converted_palette_offset;
	uint32_t image_count;	/* Images in the offset table. */
	uint32_t reserved1c[4]; /* Nothing reads or writes it by name. */
	/* 24 when the block's palette is to be converted. */
	uint32_t bits_per_pixel;
	uint32_t palette_color_count; /* Colors in the block's own palette. */
};

struct tex_level_image_header {
	uint32_t reserved00; /* Nothing reads or writes it by name. */
	/* Offset of the image's palette from this header, 4 bytes per color. */
	uint32_t palette_offset;
	/* Offset of the encoded pixels from this header. */
	uint32_t encoded_image_offset;
	/* Offset from this header of the palette converted for the display; set
	 * by the TexLevel_Convert functions for every image. */
	uint32_t converted_palette_offset;
	uint32_t width;		/* Width in pixels. */
	uint32_t height;	/* Height in pixels. */
	uint32_t reserved18[2]; /* Nothing reads or writes it by name. */
	/* How the pixels are encoded; the RLE readers index their tables by
	 * it. */
	uint32_t packing_mode;
	/* 24 when the image's palette is to be converted. */
	uint32_t bits_per_pixel;
	uint32_t palette_color_count; /* Colors in the image's palette. */
};

#pragma pack(pop)
typedef char xvt_size_tex_level_header[(sizeof(struct tex_level_header) == 0x34)
					       ? 1
					       : -1];
typedef char xvt_size_tex_level_image_header
	[(sizeof(struct tex_level_image_header) == 0x2C) ? 1 : -1];

unsigned int tex_level_convert24_bpp_palettes_to16_bpp(unsigned int *tex_level);
unsigned int tex_level_convert24_bpp_palettes_to8_bpp(unsigned int *tex_level);

#ifdef __cplusplus
}
#endif

#endif
