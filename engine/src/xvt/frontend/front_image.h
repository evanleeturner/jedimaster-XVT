#ifndef XVT_FRONTEND_FRONT_IMAGE_H
#define XVT_FRONTEND_FRONT_IMAGE_H

#include <stdint.h>
#include <stdio.h>

#include "xvt/assets/file.h"
#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A frontend image: a registered .bmp, a glyph of a font, or a screen area
 * saved by frontend_screen_push_state. */
struct image_resource {
	int width;  /* Width in pixels. */
	int height; /* Height in pixels. */
	/* 1 when pixels holds RLE rows (front_image_compress_rle), 0 when it
	 * holds raw pixels. */
	int is_compressed;
	/* Bytes in pixels: width * height uncompressed, the encoded size after
	 * front_image_compress_rle, 2 * width * height for a 16-bit screen save,
	 * and 0 in the glyphs the text functions build. */
	int pixel_data_bytes;
	/* The pixel data, rows top to bottom: palette indexes, RLE rows, or
	 * 16-bit pixels for a 16-bit screen save. */
	uint8_t *pixels;
	/* For each palette index, the 16-bit display pixel value the blitters
	 * draw at 16 bits per pixel; front_image_load_bmp_file fills it from the
	 * file's palette at that depth only. */
	int color_lut[256];
};

/* One entry of g_front_state.resource_table, which is kept sorted by name. */
struct front_image_resource_record {
	/* The registered name, compared over 64 bytes with strncmp; may lack a
	 * NUL when 64 characters or longer. */
	char name[64];
	struct image_resource
		*image; /* The heap image registered under name. */
};

/* The scratch row front_image_compress_rle encodes into; the first encoded_size
 * bytes of the whole struct are one row record. */
struct front_image_rle_row_buffer {
	/* The row record's size in bytes: these 4, the tokens and the closing
	 * 0x80. */
	int encoded_size;
	uint8_t data[1277]; /* The row's tokens, ending with 0x80. */
};

int front_image_register_resource_default(const char *file_name,
					  const char *name);
int front_image_register_resource(const char *file_name, const char *name,
				  int remap_to_display_palette,
				  int compress_rle);
void front_image_free_resource_by_name(const char *name);
void front_image_free_all_resources(void);
int front_image_get_resource_rect(const char *name, struct RECT *out_rect);
int front_image_draw_sprite_translucent(const char *name, int x, int y);
int front_image_blit_translucent(const struct image_resource *image, int x,
				 int y);
int front_image_draw_sprite_rect_transparent(const char *name,
					     const struct RECT *src_rect,
					     int dst_x, int dst_y);
int front_image_blit_rect_transparent(const struct image_resource *image,
				      const struct RECT *src_rect, int dst_x,
				      int dst_y);
int front_image_draw_sprite_rect_tinted(const char *name,
					const struct RECT *src_rect, int dst_x,
					int dst_y, unsigned int tint_color);
int front_image_blit_rect_tinted(const struct image_resource *image,
				 const struct RECT *src_rect, int dst_x,
				 int dst_y, unsigned int tint_color);
int front_image_draw_sprite(const char *name, int x, int y);
int front_image_blit_transparent(const struct image_resource *image, int x,
				 int y);
void front_image_blit_rle8(const struct image_resource *image, int dest_x,
			   int dest_y, int src_left, int src_top,
			   int visible_width, int visible_height);
void front_image_blit_rle16(const struct image_resource *image, int dest_x,
			    int dest_y, int src_left, int src_top,
			    int visible_width, int visible_height);
int front_image_draw_sprite_opaque(const char *name, int x, int y);
int front_image_blit_opaque(const struct image_resource *image, int x, int y);
void front_image_blit_rle8_opaque(const struct image_resource *image,
				  int dest_x, int dest_y, int src_left,
				  int src_top, int visible_width,
				  int visible_height);
void front_image_blit_rle16_opaque(const struct image_resource *image,
				   int dest_x, int dest_y, int src_left,
				   int src_top, int visible_width,
				   int visible_height);
int front_image_draw_glyph(const struct image_resource *glyph, int x, int y,
			   unsigned int color, int apply_text_fade);
void front_image_blit_glyph_rle_8bpp(const struct image_resource *glyph,
				     int dest_x, int dest_y, int clip_left_skip,
				     int clip_top_skip, int visible_width,
				     int visible_rows, uint8_t color);
void front_image_blit_glyph_rle_16bpp(const struct image_resource *glyph,
				      int dest_x, int dest_y,
				      int clip_left_skip, int clip_top_skip,
				      int visible_width, int visible_rows,
				      unsigned int color);
int front_image_load_bmp_file(const char *file_name,
			      struct image_resource *image,
			      int remap_to_display_palette, int compress_rle);
int front_image_decode_bmp4bpp(xvt_file *stream, void *dst_pixels,
			       const struct BITMAPFILEHEADER *file_header,
			       const struct BITMAPINFOHEADER *info_header);
int front_image_decode_bmp8bpp(xvt_file *stream, void *dst_pixels,
			       const struct BITMAPFILEHEADER *file_header,
			       const struct BITMAPINFOHEADER *info_header);
void front_image_remap_palette(uint8_t *pixels, const uint8_t *src_palette,
			       const struct BITMAPINFOHEADER *info_header);
char front_image_remap_palette_index(const uint8_t *src_rgb, int src_index);
int front_image_compress_rle(struct image_resource *image);
void front_image_insert_resource_sorted(
	const struct front_image_resource_record *entry);
void front_image_remove_resource_at(int index);
int front_image_find_resource_by_name(const char *name);
int front_image_bsearch_resource(
	const struct front_image_resource_record *table, int hi,
	const char *key);
int front_image_save_bmp_file(const char *file_name, const void *pixels,
			      int width, int height, int pitch, int bpp,
			      int is555, const void *palette);
void front_image_read_bmp_palette(xvt_file *stream, uint8_t *dest, int count);
unsigned int front_image_get_faded_glyph_color16(unsigned int color16);
int front_image_load_resource_list(const char *file_name);
int front_image_unload_resource_list(const char *file_name);

#ifdef __cplusplus
}
#endif

#endif
