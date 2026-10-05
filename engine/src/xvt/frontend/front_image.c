#include "xvt/frontend/front_image.h"

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_frontend.h"
#endif

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_assets.h"
#endif
#include <stdlib.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt_runtime/log/log_both_builds.h"

#pragma pack(push, 1)

/* A .bmp file's first header, as front_image_save_bmp_file writes it and
 * front_image_load_bmp_palette_file reads it. */
struct front_image_bmp_file_header {
	/* 0x4D42, "BM": written by the save, checked by the palette load. */
	uint16_t signature;
	uint32_t file_size; /* The file's size in bytes; written, never read. */
	/* Left unset by the save, which writes whatever it holds; not read. */
	uint16_t reserved0;
	/* Left unset by the save, which writes whatever it holds; not read. */
	uint16_t reserved1;
	uint32_t pixel_offset; /* Where the pixels start, 54; not read. */
};

/* A .bmp file's 40-byte info header, as front_image_save_bmp_file writes it and
 * front_image_load_bmp_palette_file reads it. */
struct front_image_bmp_info_header {
	uint32_t header_size; /* This header's size, written as 40; not read. */
	/* Width in pixels, written rounded up to even; not read. */
	int32_t width;
	int32_t height;	 /* Height in pixels; written, not read. */
	uint16_t planes; /* Written as 1; the palette load needs 1. */
	/* Written as 24; the palette load takes 16 entries for 4 and 256 for 8,
	 * and none otherwise. */
	uint16_t bits_per_pixel;
	uint32_t compression; /* Written as 0, none; not read. */
	uint32_t image_size;  /* Pixel bytes after the headers; not read. */
	int32_t pixels_per_meter_x; /* Written as 0; not read. */
	int32_t pixels_per_meter_y; /* Written as 0; not read. */
	uint32_t colors_used;	    /* Written as 0; not read. */
	uint32_t colors_important;  /* Written as 0; not read. */
};

#pragma pack(pop)
typedef char xvt_size_front_image_bmp_file_header
	[(sizeof(struct front_image_bmp_file_header) == 14) ? 1 : -1];
typedef char xvt_size_front_image_bmp_info_header
	[(sizeof(struct front_image_bmp_info_header) == 40) ? 1 : -1];

/* Per palette index of the image being remapped, the display palette index
 * front_image_remap_palette_index chose for it, or 0x100 while none is chosen.
 * front_image_remap_palette sets every entry to 0x100 at the start of each image;
 * only these two functions write it. */
// GLOBAL: XVT 0x664C88
static int16_t g_palette_remap_cache[256] = {0};

/* Loads the .bmp file fileName with front_image_load_bmp_file, remapped to the
 * display palette at 8 bits per pixel and RLE-compressed, and registers it
 * under name in g_front_state.resource_table. Returns 1, or 0 when fileName or
 * name is empty, name is already registered, or the allocation or the load
 * fails. The name is copied with strncpy into 64 bytes, with no NUL when it is
 * 64 characters or longer. Does not check the table's room. */
// FUNCTION: XVT 0x4B4A60
int front_image_register_resource_default(const char *file_name,
					  const char *name)
{
	if (*file_name == '\0') {
		XVT_LOG_WARN("image.register_empty file=\"%s\" resource=\"%s\"",
			     file_name, name);
		return 0;
	}
	if (*name == '\0') {
		XVT_LOG_WARN("image.register_empty file=\"%s\" resource=\"%s\"",
			     file_name, name);
		return 0;
	}
	if (front_image_find_resource_by_name(name) != -1) {
		XVT_LOG_DEBUG("image.name_taken resource=\"%s\" file=\"%s\"",
			      name, file_name);
		return 0;
	}

	struct image_resource *image;
	image = malloc(sizeof(*image));
	if (image == NULL) {
		XVT_LOG_ERROR("image.alloc_failed what=record bytes=%d",
			      (int)sizeof(*image));
		return 0;
	}
	memset(image, 0, sizeof(*image));
	if (front_image_load_bmp_file(file_name, image, 1, 1) == 0) {
		free(image);
		XVT_LOG_DEBUG(
			"image.not_registered resource=\"%s\" file=\"%s\"",
			name, file_name);
		return 0;
	}

	struct front_image_resource_record entry;
	entry.image = image;
	strncpy(entry.name, name, sizeof(entry.name));
	front_image_insert_resource_sorted(&entry);
	XVT_LOG_DEBUG(
		"image.registered resource=\"%s\" file=\"%s\" width=%d height=%d compressed=%d bytes=%d remap=1 rle=1 count=%d",
		name, file_name, image->width, image->height,
		image->is_compressed, image->pixel_data_bytes,
		g_front_state.resource_count);
	return 1;
}

/* Does what front_image_register_resource_default does, with the caller's
 * remap_to_display_palette and compress_rle: each has effect only when it is 1. */
// FUNCTION: XVT 0x4B4B20
int front_image_register_resource(const char *file_name, const char *name,
				  int remap_to_display_palette,
				  int compress_rle)
{
	if (*file_name == '\0') {
		XVT_LOG_WARN("image.register_empty file=\"%s\" resource=\"%s\"",
			     file_name, name);
		return 0;
	}
	if (*name == '\0') {
		XVT_LOG_WARN("image.register_empty file=\"%s\" resource=\"%s\"",
			     file_name, name);
		return 0;
	}
	if (front_image_find_resource_by_name(name) != -1) {
		return 0;
	}

	struct image_resource *image;
	image = malloc(sizeof(*image));
	if (image == NULL) {
		XVT_LOG_ERROR("image.alloc_failed what=record bytes=%d",
			      (int)sizeof(*image));
		return 0;
	}
	memset(image, 0, sizeof(*image));
	if (front_image_load_bmp_file(file_name, image,
				      remap_to_display_palette,
				      compress_rle) == 0) {
		free(image);
		XVT_LOG_DEBUG(
			"image.not_registered resource=\"%s\" file=\"%s\"",
			name, file_name);
		return 0;
	}

	struct front_image_resource_record entry;
	entry.image = image;
	strncpy(entry.name, name, sizeof(entry.name));
	front_image_insert_resource_sorted(&entry);
	XVT_LOG_DEBUG(
		"image.registered resource=\"%s\" file=\"%s\" width=%d height=%d compressed=%d bytes=%d remap=%d rle=%d count=%d",
		name, file_name, image->width, image->height,
		image->is_compressed, image->pixel_data_bytes,
		remap_to_display_palette, compress_rle,
		g_front_state.resource_count);
	return 1;
}

/* Frees the image registered under name, its pixels and its record, and takes
 * the record out of g_front_state.resource_table; does nothing when no image has
 * the name. The modern build also drops the image from its renderer. */
// FUNCTION: XVT 0x4B4BF0
void front_image_free_resource_by_name(const char *name)
{
	int resource_index = front_image_find_resource_by_name(name);
	if (resource_index == -1) {
		return;
	}
	XVT_LOG_DEBUG("image.freed resource=\"%.64s\" index=%d count=%d", name,
		      resource_index, g_front_state.resource_count);

	if (g_front_state.resource_table[resource_index].image != NULL) {
#ifdef XVT_MODERN
		xvt_render_assets_retire_image(
			g_front_state.resource_table[resource_index].image);
#endif
		if (g_front_state.resource_table[resource_index]
			    .image->pixels != NULL) {
			free(g_front_state.resource_table[resource_index]
				     .image->pixels);
			g_front_state.resource_table[resource_index]
				.image->pixels = NULL;
		}
		free(g_front_state.resource_table[resource_index].image);
		g_front_state.resource_table[resource_index].image = NULL;
	}
	front_image_remove_resource_at(resource_index);
}

/* Frees every registered image: passes the name in each of the table's 512
 * slots, from the last to the first, to front_image_free_resource_by_name, which
 * ignores names no longer registered. Does nothing without a table. */
// FUNCTION: XVT 0x4B4C70
void front_image_free_all_resources(void)
{
	if (g_front_state.resource_table == NULL) {
		return;
	}
	XVT_LOG_DEBUG("image.all_freed count=%d", g_front_state.resource_count);

	for (int resource_index = 511; resource_index >= 0; --resource_index) {
		front_image_free_resource_by_name(
			g_front_state.resource_table[resource_index].name);
	}
}

/* Only the original build calls this. Returns 1 when an image is registered
 * under name, 0 when none is or name is empty. */
// FUNCTION: XVT 0x4B4CA0
int front_image_resource_exists(const char *name)
{
	if (*name == '\0') {
		return 0;
	}

	return front_image_find_resource_by_name(name) != -1;
}

/* Sets *out_rect to (0, 0, width, height) of the image registered under name, so
 * right and bottom are one past its last column and row, and returns 1. Sets it
 * to all 0 and returns 0 when name is empty or not registered. */
// FUNCTION: XVT 0x4B4CC0
int front_image_get_resource_rect(const char *name, struct RECT *out_rect)
{
	if (*name == '\0') {
		frontend_draw_rect_assign(out_rect, 0, 0, 0, 0);
		return 0;
	}

	int resource_index = front_image_find_resource_by_name(name);
	if (resource_index == -1) {
		frontend_draw_rect_assign(out_rect, 0, 0, 0, 0);
		return 0;
	}

	frontend_draw_rect_assign(
		out_rect, 0, 0,
		g_front_state.resource_table[resource_index].image->width,
		g_front_state.resource_table[resource_index].image->height);
	return 1;
}

/* Draws the image registered under name with front_image_blit_translucent and
 * returns its result; returns 0 when the name is not registered or the image is
 * RLE-compressed. */
// FUNCTION: XVT 0x4B4D40
int front_image_draw_sprite_translucent(const char *name, int x, int y)
{
	int resource_index = front_image_find_resource_by_name(name);
	if (resource_index == -1) {
		return 0;
	}

	struct image_resource *image =
		g_front_state.resource_table[resource_index].image;
	if (image->is_compressed != 0) {
		return 0;
	}

	return front_image_blit_translucent(image, x, y);
}

/* Draws an uncompressed image with its top-left corner at (x, y), clipped to
 * the clip bounds, skipping pixels of index 0. At 16 bits per pixel each drawn
 * pixel becomes the per-channel average of the screen and the index's color_lut
 * value, as frontend_draw_fill_rect_translucent blends; at 8 bits, despite the
 * name, it writes the index. Returns the clip-edge bits of
 * frontend_draw_rect_clip_to_bounds, or 0 when image is NULL. Does not check that
 * the image is uncompressed. The modern build also records the draw for its
 * renderer. */
// FUNCTION: XVT 0x4B4D90
int front_image_blit_translucent(const struct image_resource *image, int x,
				 int y)
{
	if (image == NULL) {
		return 0;
	}

	{
		struct RECT clipped_rect;
		clipped_rect.left = x;
		int image_width = image->width;
		clipped_rect.top = y;
		clipped_rect.right = x + image_width - 1;
		clipped_rect.bottom = y + image->height - 1;
		struct RECT unclipped_rect;
		frontend_draw_rect_copy(&unclipped_rect, &clipped_rect);
		int clip_result =
			frontend_draw_rect_clip_to_bounds(&clipped_rect);
		if (clipped_rect.right >= clipped_rect.left &&
		    clipped_rect.bottom >= clipped_rect.top) {
			int clip_offset_x =
				clipped_rect.left - unclipped_rect.left;
			int clip_offset_y =
				clipped_rect.top - unclipped_rect.top;
			image_width = image->width;
			int visible_width = image->width + clipped_rect.right -
					    clip_offset_x -
					    unclipped_rect.right;
			int visible_height =
				clipped_rect.bottom + image->height -
				unclipped_rect.bottom - clip_offset_y;

#ifdef XVT_MODERN
			xvt_render_frontend_image(
				image, clip_offset_x, clip_offset_y,
				x + clip_offset_x, y + clip_offset_y,
				visible_width, visible_height,
				XVT_SPRITE_FRONT_TRANSLUCENT, 0);
#endif
			int display_bpp = g_front_state.display_bpp;
			uint8_t *source;
			uint8_t *destination;
			if (display_bpp == 8) {
				source = &image->pixels[image_width *
								clip_offset_y +
							clip_offset_x];
				destination =
					&g_draw_surface_ptr
						[x + clip_offset_x +
						 g_front_state.draw_surface_pitch *
							 (y + clip_offset_y)];
				if (visible_height > 0) {
					int rows_remaining = visible_height;
					do {
						for (int column = 0;
						     visible_width > column;
						     ++column) {
							uint8_t source_pixel =
								source[column];
							if (source_pixel != 0) {
								destination[column] =
									source_pixel;
							}
						}
						destination +=
							g_front_state
								.draw_surface_pitch;
						source += image->width;
						--rows_remaining;
					} while (rows_remaining != 0);
				}
			} else if (display_bpp == 16) {
				source = &image->pixels[image_width *
								clip_offset_y +
							clip_offset_x];
				destination =
					&g_draw_surface_ptr
						[2 * x + 2 * clip_offset_x +
						 g_front_state.draw_surface_pitch *
							 (y + clip_offset_y)];
				int rows_remaining;
				if (g_front_state.pixel_format555 != 0) {
					if (visible_height > 0) {
						rows_remaining =
							clipped_rect.bottom +
							image->height -
							unclipped_rect.bottom -
							clip_offset_y;
						do {
							int column = 0;
							if (visible_width > 0) {
								uint8_t *destination_pixel =
									destination;
								do {
									uint8_t source_pixel = source
										[column];
									if (source_pixel !=
									    0) {
										int source_color =
											image->color_lut
												[source_pixel];
										unsigned int blended =
											(((*(uint16_t
												     *)
												   destination_pixel &
											   0x1F) +
											  8 * ((*(uint16_t
													  *)
													destination_pixel &
												0x3E0) +
											       8 * (*(uint16_t
													      *)
													    destination_pixel &
												    0x7C00u))) >>
											 1) +
											(((source_color &
											   0x1F) +
											  ((source_color &
											    0x7C00)
											   << 6) +
											  8 * (source_color &
											       0x3E0u)) >>
											 1);
										*(uint16_t
											  *)
											destination_pixel =
											(uint16_t)((blended &
												    0x1F) +
												   ((blended >>
												     6) &
												    0x7C00) +
												   ((blended >>
												     3) &
												    0x3E0));
									}
									destination_pixel +=
										2;
									++column;
								} while (
									column <
									visible_width);
							}
							source += image->width;
							destination +=
								g_front_state
									.draw_surface_pitch &
								0xFFFFFFFE;
							--rows_remaining;
						} while (rows_remaining != 0);
					}
				} else if (visible_height > 0) {
					rows_remaining = clipped_rect.bottom +
							 image->height -
							 unclipped_rect.bottom -
							 clip_offset_y;
					do {
						int column = 0;
						if (visible_width > 0) {
							uint8_t *destination_pixel =
								destination;
							do {
								uint8_t source_pixel = source
									[column];
								if (source_pixel !=
								    0) {
									unsigned int blended =
										(((*(uint16_t
											     *)
											   destination_pixel &
										   0x1F) +
										  8 * ((*(uint16_t
												  *)
												destination_pixel &
											0x7E0) +
										       4 * (*(uint16_t
												      *)
												    destination_pixel &
											    0xF800u))) >>
										 1) +
										(((image->color_lut
											   [source_pixel] &
										   0x1F) +
										  32 * (image->color_lut
												[source_pixel] &
											0xF800) +
										  8 * (image->color_lut
											       [source_pixel] &
										       0x7E0u)) >>
										 1);
									*(uint16_t
										  *)
										destination_pixel =
										(uint16_t)((blended &
											    0x1F) +
											   ((blended >>
											     3) &
											    0x7E0) +
											   ((blended >>
											     5) &
											    0xF800));
								}
								destination_pixel +=
									2;
								++column;
							} while (column !=
								 visible_width);
						}
						source += image->width;
						destination +=
							g_front_state
								.draw_surface_pitch &
							0xFFFFFFFE;
						--rows_remaining;
					} while (rows_remaining != 0);
				}
			}
		}
		return clip_result;
	}
}

/* Draws the part srcRect of the image registered under name with
 * front_image_blit_rect_transparent and returns its result; returns 0 when the
 * name is not registered or the image is RLE-compressed. */
// FUNCTION: XVT 0x4B50B0
int front_image_draw_sprite_rect_transparent(const char *name,
					     const struct RECT *src_rect,
					     int dst_x, int dst_y)
{
	int resource_index = front_image_find_resource_by_name(name);
	if (resource_index == -1) {
		return 0;
	}
	struct image_resource *image =
		g_front_state.resource_table[resource_index].image;
	if (image->is_compressed != 0) {
		return 0;
	}
	return front_image_blit_rect_transparent(image, src_rect, dst_x, dst_y);
}

/* Draws the part *srcRect of an uncompressed image, edges included, with its
 * top-left corner at (dstX, dstY), clipped to the clip bounds and skipping
 * pixels of index 0: at 8 bits per pixel as the index, at 16 as its color_lut
 * value. Returns the clip-edge bits of frontend_draw_rect_clip_to_bounds, or 0 when
 * image is NULL. Does not check that srcRect lies inside the image. The modern
 * build also records the draw for its renderer. */
// FUNCTION: XVT 0x4B5100
int front_image_blit_rect_transparent(const struct image_resource *image,
				      const struct RECT *src_rect, int dst_x,
				      int dst_y)
{
	if (image == NULL) {
		return 0;
	}
	struct RECT destination_rect;
	frontend_draw_rect_copy(&destination_rect, src_rect);
	frontend_draw_rect_offset_xy(&destination_rect, dst_x - src_rect->left,
				     dst_y - src_rect->top);
	struct RECT unclipped_rect;
	frontend_draw_rect_copy(&unclipped_rect, &destination_rect);
	int clip_result = frontend_draw_rect_clip_to_bounds(&destination_rect);
	if (destination_rect.right >= destination_rect.left) {
		if (destination_rect.top <= destination_rect.bottom) {
			int clip_offset_x =
				destination_rect.left - unclipped_rect.left;
			int clip_offset_y =
				destination_rect.top - unclipped_rect.top;
			int source_left = src_rect->left;
			int visible_width = src_rect->right - source_left;
			visible_width -= clip_offset_x;
			visible_width += destination_rect.right + 1;
			visible_width -= unclipped_rect.right;
			int source_top = src_rect->top;
			int visible_height = src_rect->bottom - source_top;
			visible_height += destination_rect.bottom + 1;
			visible_height -= clip_offset_y;
			visible_height -= unclipped_rect.bottom;

#ifdef XVT_MODERN
			xvt_render_frontend_image(
				image, source_left + clip_offset_x,
				source_top + clip_offset_y,
				dst_x + clip_offset_x, dst_y + clip_offset_y,
				visible_width, visible_height,
				XVT_SPRITE_FRONT_KEYED, 0);
#endif
			int display_bpp = g_front_state.display_bpp;
			int rows_remaining;
			switch (display_bpp) {
			case 8: {
				uint8_t *source =
					&image->pixels[image->width *
							       (source_top +
								clip_offset_y) +
						       source_left +
						       clip_offset_x];
				uint8_t *destination =
					&g_draw_surface_ptr
						[dst_x + clip_offset_x +
						 g_front_state.draw_surface_pitch *
							 (dst_y +
							  clip_offset_y)];
				if (visible_height > 0) {
					rows_remaining = visible_height;
					do {
						int column = 0;
						if (visible_width > 0) {
							do {
								if (source[column] !=
								    0) {
									destination[column] = source
										[column];
								}
								++column;
							} while (
								column -
									visible_width <
								0);
						}
						source += image->width;
						destination +=
							g_front_state
								.draw_surface_pitch;
						--rows_remaining;
					} while (rows_remaining != 0);
				}
				break;
			}
			case 16: {
				uint8_t *source =
					&image->pixels[image->width *
							       (source_top +
								clip_offset_y) +
						       source_left +
						       clip_offset_x];
				uint8_t *destination =
					&g_draw_surface_ptr
						[2 * (dst_x + clip_offset_x) +
						 g_front_state.draw_surface_pitch *
							 (dst_y +
							  clip_offset_y)];
				if (visible_height > 0) {
					rows_remaining = visible_height;
					do {
						int column = 0;
						if (visible_width > 0) {
							do {
								if (source[column] !=
								    0) {
									*(uint16_t
										  *)&destination
										[2 *
										 column] =
										(uint16_t)image
											->color_lut
												[source[column]];
								}
								++column;
							} while (column <
								 visible_width);
						}
						source += image->width;
						destination +=
							g_front_state
								.draw_surface_pitch &
							0xFFFFFFFE;
						--rows_remaining;
					} while (rows_remaining != 0);
				}
				break;
			}
			}
		}
	}
	return clip_result;
}

/* Draws the part srcRect of the image registered under name with
 * front_image_blit_rect_tinted and returns its result; returns 0 when the name is
 * not registered or the image is RLE-compressed. */
// FUNCTION: XVT 0x4B52C0
int front_image_draw_sprite_rect_tinted(const char *name,
					const struct RECT *src_rect, int dst_x,
					int dst_y, unsigned int tint_color)
{
	int resource_index = front_image_find_resource_by_name(name);
	if (resource_index == -1) {
		return 0;
	}
	struct image_resource *image =
		g_front_state.resource_table[resource_index].image;
	if (image->is_compressed != 0) {
		return 0;
	}
	return front_image_blit_rect_tinted(image, src_rect, dst_x, dst_y,
					    tint_color);
}

/* Draws the part *srcRect of an uncompressed image, edges included, at (dstX,
 * dstY), clipped, in shades of tint_color, skipping pixels of index 0. At 16
 * bits per pixel the blue field, 0 to 31, of the index's color_lut value is the
 * intensity, and each channel of tint_color is drawn as channel * intensity /
 * 31, in the 555 or 565 layout g_front_state.pixel_format555 names; at 8 bits
 * every drawn pixel is tint_color's low byte. Returns the clip-edge bits of
 * frontend_draw_rect_clip_to_bounds, or 0 when image is NULL. The modern build also
 * records the draw for its renderer. */
// FUNCTION: XVT 0x4B5310
int front_image_blit_rect_tinted(const struct image_resource *image,
				 const struct RECT *src_rect, int dst_x,
				 int dst_y, unsigned int tint_color)
{
	if (image == NULL) {
		return 0;
	}

	const struct RECT *source_rect = src_rect;
	int destination_y = dst_y;
	struct RECT destination_rect;
	frontend_draw_rect_copy(&destination_rect, source_rect);
	frontend_draw_rect_offset_xy(&destination_rect,
				     dst_x - source_rect->left,
				     destination_y - source_rect->top);
	struct RECT unclipped_rect;
	frontend_draw_rect_copy(&unclipped_rect, &destination_rect);
	int clip_result = frontend_draw_rect_clip_to_bounds(&destination_rect);
	unsigned int tint_red;
	unsigned int tint_green;
	if (destination_rect.right >= destination_rect.left) {
		if (destination_rect.top > destination_rect.bottom) {
			return clip_result;
		}

		int clip_offset_x = destination_rect.left - unclipped_rect.left;
		int clip_offset_y = destination_rect.top - unclipped_rect.top;
		int source_left = source_rect->left;
		int visible_width = source_rect->right - source_left -
				    clip_offset_x - unclipped_rect.right +
				    destination_rect.right + 1;
		int source_top = source_rect->top;
		int visible_height = source_rect->bottom - source_top -
				     clip_offset_y - unclipped_rect.bottom +
				     destination_rect.bottom + 1;

#ifdef XVT_MODERN
		xvt_render_frontend_image(
			image, source_left + clip_offset_x,
			source_top + clip_offset_y, dst_x + clip_offset_x,
			dst_y + clip_offset_y, visible_width, visible_height,
			XVT_SPRITE_FRONT_TINTED, tint_color);
#endif
		int display_bpp = g_front_state.display_bpp;
		uint8_t *source;
		uint8_t *destination;
		int column;
		if (display_bpp == 8) {
			destination =
				&g_draw_surface_ptr
					[dst_x + clip_offset_x +
					 g_front_state.draw_surface_pitch *
						 (destination_y +
						  clip_offset_y)];
			source = &image->pixels[image->width * (source_top +
								clip_offset_y) +
						source_left + clip_offset_x];
			if (visible_height > 0) {
				do {
					for (column = 0; column < visible_width;
					     ++column) {
						if (source[column] != 0) {
							destination[column] =
								(uint8_t)
									tint_color;
						}
					}
					destination +=
						g_front_state
							.draw_surface_pitch;
					source += image->width;
					--visible_height;
				} while (visible_height != 0);
			}
		} else if (display_bpp == 16) {
			source = &image->pixels[image->width * (source_top +
								clip_offset_y) +
						source_left + clip_offset_x];
			destination =
				&g_draw_surface_ptr
					[2 * (dst_x + clip_offset_x) +
					 g_front_state.draw_surface_pitch *
						 (destination_y +
						  clip_offset_y)];
			if (visible_height > 0) {
				for (int rows_remaining = visible_height;
				     rows_remaining != 0; --rows_remaining) {
					for (column = 0; column < visible_width;
					     ++column) {
						uint8_t source_pixel =
							source[column];
						if (source_pixel == 0) {
							continue;
						}
						int intensity =
							image->color_lut
								[source_pixel] &
							0x1F;
						if (g_front_state
							    .pixel_format555 !=
						    0) {
							tint_red = tint_color >>
								   10;
							tint_green =
								tint_color &
								0x3E0;
						} else {
							tint_red = tint_color >>
								   11;
							tint_green =
								tint_color &
								0x7E0;
						}
						int red = intensity *
							  (int)tint_red / 31;
						int green =
							intensity *
							(int)(tint_green >> 5) /
							31;
						int blue = intensity *
							   (int)(tint_color &
								 0x1F) /
							   31;
						if (g_front_state
							    .pixel_format555 !=
						    0) {
							red <<= 5;
						} else {
							red <<= 6;
						}
						*(uint16_t *)&destination
							[2 * column] =
							((red + green) << 5) +
							blue;
					}
					source += image->width;
					destination +=
						g_front_state
							.draw_surface_pitch &
						0xFFFFFFFE;
				}
			}
		}
	}

	return clip_result;
}

/* Draws the image registered under name with front_image_blit_transparent and
 * returns its result, or 0 when the name is not registered. */
// FUNCTION: XVT 0x4B5570
int front_image_draw_sprite(const char *name, int x, int y)
{
	int resource_index = front_image_find_resource_by_name(name);
	if (resource_index == -1) {
		return 0;
	}

	return front_image_blit_transparent(
		g_front_state.resource_table[resource_index].image, x, y);
}

/* Draws an image with its top-left corner at (x, y), clipped to the clip
 * bounds, skipping pixels of index 0: an uncompressed image pixel by pixel, at
 * 8 bits per pixel as the index and at 16 as its color_lut value, an
 * RLE-compressed one through front_image_blit_rle8 or front_image_blit_rle16.
 * Returns the clip-edge bits of frontend_draw_rect_clip_to_bounds, or 0 when image
 * is NULL. The modern build also records the draw for its renderer. */
// FUNCTION: XVT 0x4B55B0
int front_image_blit_transparent(const struct image_resource *image, int x,
				 int y)
{
	if (image == NULL) {
		return 0;
	}

	struct RECT clipped_rect;
	clipped_rect.left = x;
	clipped_rect.top = y;
	clipped_rect.right = x + image->width - 1;
	clipped_rect.bottom = y + image->height - 1;
	struct RECT original_rect;
	frontend_draw_rect_copy(&original_rect, &clipped_rect);
	int clip_result = frontend_draw_rect_clip_to_bounds(&clipped_rect);
	if (clipped_rect.right >= clipped_rect.left &&
	    clipped_rect.top <= clipped_rect.bottom) {
		int source_x = clipped_rect.left - original_rect.left;
		int source_y = clipped_rect.top - original_rect.top;
		int width = image->width;
		int visible_width = clipped_rect.right - source_x -
				    original_rect.right + width;
		int visible_height = clipped_rect.bottom + image->height -
				     source_y - original_rect.bottom;

#ifdef XVT_MODERN
		xvt_render_frontend_image(image, source_x, source_y,
					  x + source_x, y + source_y,
					  visible_width, visible_height,
					  XVT_SPRITE_FRONT_KEYED, 0);
#endif
		int display_bpp = g_front_state.display_bpp;
		if (image->is_compressed == 0) {
			const uint8_t *source;
			uint8_t *destination;
			int row;
			int column;
			if (display_bpp == 8) {
				source = &image->pixels[source_y * width +
							source_x];
				destination =
					&g_draw_surface_ptr
						[x + source_x +
						 g_front_state.draw_surface_pitch *
							 (y + source_y)];
				for (row = visible_height; row > 0; --row) {
					for (column = 0; column < visible_width;
					     ++column) {
						uint8_t pixel = source[column];
						if (pixel != 0) {
							destination[column] =
								pixel;
						}
					}
					source += image->width;
					destination +=
						g_front_state
							.draw_surface_pitch;
				}
			} else if (display_bpp == 16) {
				source = &image->pixels[source_y * width +
							source_x];
				destination =
					&g_draw_surface_ptr
						[2 * (x + source_x) +
						 g_front_state.draw_surface_pitch *
							 (y + source_y)];
				for (row = visible_height; row > 0; --row) {
					for (column = 0; column < visible_width;
					     ++column) {
						if (source[column] != 0) {
							*(uint16_t
								  *)&destination
								[2 * column] =
								image->color_lut
									[source[column]];
						}
					}
					source += image->width;
					destination +=
						g_front_state
							.draw_surface_pitch &
						~1;
				}
			}
		} else {
			if (display_bpp == 8) {
				front_image_blit_rle8(image, x + source_x,
						      y + source_y, source_x,
						      source_y, visible_width,
						      visible_height);
			} else if (display_bpp == 16) {
				front_image_blit_rle16(image, x + source_x,
						       y + source_y, source_x,
						       source_y, visible_width,
						       visible_height);
			}
		}
	}

	return clip_result;
}

/* Draws the visible part of an RLE-compressed image at 8 bits per pixel:
 * visible_height rows from row src_top and visible_width columns from column
 * src_left, to (dest_x, dest_y) through g_draw_surface_ptr. Literal and run pixels
 * are written as their palette index; skip tokens leave the screen as it is.
 * When visible_width is the image's width it draws whole rows and ignores
 * src_left. The caller has clipped; nothing is checked. */
// FUNCTION: XVT 0x4B5770
void front_image_blit_rle8(const struct image_resource *image, int dest_x,
			   int dest_y, int src_left, int src_top,
			   int visible_width, int visible_height)
{
	const uint8_t *row;
	uint8_t *destination;
	int row_length;
	uint8_t token;
	uint8_t value;

	if (visible_width == image->width) {
		row = image->pixels;
		destination =
			&g_draw_surface_ptr[g_front_state.draw_surface_pitch *
						    dest_y +
					    dest_x];
		while (src_top > 0) {
#ifdef XVT_MODERN
			memcpy(&row_length, row, sizeof(row_length));
#else
			row_length = *(const int *)row;
#endif
			row += row_length;
			--src_top;
		}

		if (visible_height <= 0) {
			return;
		}

		int fast_rows_remaining = visible_height;
		do {
			int fast_destination_offset = 0;
			row += sizeof(row_length);
			for (;;) {
				token = *row++;
				if (token == 0x80) {
					break;
				}

				if ((token & 0x80u) != 0) {
					token &= 0x7F;
					memcpy(destination +
						       fast_destination_offset,
					       row, token);
					fast_destination_offset += token;
					row += token;
					continue;
				}

				if ((token & 0x40u) != 0) {
					fast_destination_offset += token & 0x3F;
					continue;
				}

				value = *row++;
				memset(destination + fast_destination_offset,
				       value, token);
				fast_destination_offset += token;
			}

			destination += g_front_state.draw_surface_pitch;
			--fast_rows_remaining;
		} while (fast_rows_remaining != 0);
		return;
	}

	row = image->pixels;
	destination =
		&g_draw_surface_ptr[g_front_state.draw_surface_pitch * dest_y +
				    dest_x];
	while (src_top > 0) {
#ifdef XVT_MODERN
		memcpy(&row_length, row, sizeof(row_length));
#else
		row_length = *(const int *)row;
#endif
		row += row_length;
		--src_top;
	}

	if (visible_height <= 0) {
		return;
	}

	int rows_remaining = visible_height;
	int count;
	int end_offset;
	do {
		const uint8_t *row_start = row;
		int destination_offset = 0;
		int token_offset = sizeof(row_length);
#ifdef XVT_MODERN
		memcpy(&row_length, row_start, sizeof(row_length));
#else
		row_length = *(const int *)row_start;
#endif
		uint8_t started = 0;

		for (;;) {
			token = row_start[token_offset++];
			/* Until started is set, destination_offset counts the
			 * source pixels skipped toward src_left; when the row
			 * reaches src_left it is reset to 0 and from then on it
			 * is the destination column. */
			if (!started) {
				if (token == 0x80) {
					break;
				}

				if ((token & 0x80u) != 0) {
					token &= 0x7F;
					count = token;
					end_offset = destination_offset + count;
					if (end_offset >= src_left) {
						count = src_left -
							destination_offset;
						token_offset += count;
						started = 1;
						token -= count;
						destination_offset = 0;
						if (token != 0) {
							token |= 0x80;
						}
					} else {
						destination_offset = end_offset;
						token_offset += count;
					}
				} else if ((token & 0x40u) != 0) {
					token &= 0x3F;
					count = token;
					end_offset = destination_offset + token;
					if (end_offset >= src_left) {
						count = src_left -
							destination_offset;
						started = 1;
						token -= count;
						destination_offset = 0;
						if (token != 0) {
							token |= 0x40;
						}
					} else {
						destination_offset += count;
					}
				} else {
					count = token;
					end_offset = destination_offset + token;
					if (end_offset >= src_left) {
						count = src_left -
							destination_offset;
						started = 1;
						token -= count;
						destination_offset = 0;
						if (token == 0) {
							++token_offset;
						}
					} else {
						++token_offset;
						destination_offset += count;
					}
				}
			}

			if (started != 1 || token == 0) {
				continue;
			}
			if (token == 0x80) {
				break;
			}

			if ((token & 0x80u) != 0) {
				token &= 0x7F;
				end_offset = destination_offset + token;
				if (end_offset >= visible_width) {
					token = (uint8_t)(visible_width -
							  destination_offset);
					memcpy(destination + destination_offset,
					       row_start + token_offset, token);
					break;
				}

				memcpy(destination + destination_offset,
				       row_start + token_offset, token);
				destination_offset = end_offset;
				token_offset += token;
			} else if ((token & 0x40u) != 0) {
				token &= 0x3F;
				destination_offset += token;
				if (destination_offset >= visible_width) {
					break;
				}
			} else {
				value = row_start[token_offset++];
				count = token;
				end_offset = destination_offset + count;
				if (end_offset >= visible_width) {
					token = (uint8_t)(visible_width -
							  destination_offset);
					memset(destination + destination_offset,
					       value, token);
					break;
				}

				memset(destination + destination_offset, value,
				       count);
				destination_offset = end_offset;
			}
		}

		row += row_length;
		destination += g_front_state.draw_surface_pitch;
		--rows_remaining;
	} while (rows_remaining != 0);
}

/* Does what front_image_blit_rle8 does at 16 bits per pixel, writing each pixel's
 * color_lut value. */
// FUNCTION: XVT 0x4B5A80
void front_image_blit_rle16(const struct image_resource *image, int dest_x,
			    int dest_y, int src_left, int src_top,
			    int visible_width, int visible_height)
{
	const uint8_t *row;
	uint8_t *destination;
	int row_length;
	uint8_t token;
	int color;
	int pixel_index;
	uint16_t *fill_destination;
	int fill_count;

	if (visible_width == image->width) {
		row = image->pixels;
		destination =
			&g_draw_surface_ptr[g_front_state.draw_surface_pitch *
						    dest_y +
					    2 * dest_x];
		while (src_top > 0) {
#ifdef XVT_MODERN
			memcpy(&row_length, row, sizeof(row_length));
#else
			row_length = *(const int *)row;
#endif
			row += row_length;
			--src_top;
		}

		if (visible_height <= 0) {
			return;
		}

		int fast_rows_remaining = visible_height;
		do {
			int fast_destination_offset = 0;
			row += sizeof(row_length);
			for (;;) {
				token = *row++;
				if (token == 0x80) {
					break;
				}

				if ((token & 0x80u) != 0) {
					token &= 0x7F;
					for (pixel_index = 0;
					     pixel_index < token;
					     ++pixel_index) {
						*(uint16_t *)&destination
							[2 *
							 (fast_destination_offset +
							  pixel_index)] =
							image->color_lut
								[row[pixel_index]];
					}
					fast_destination_offset += token;
					row += token;
				} else if ((token & 0x40u) != 0) {
					fast_destination_offset += token & 0x3F;
				} else {
					color = image->color_lut[*row++];
					if (token != 0) {
						fill_destination = (uint16_t
									    *)&destination
							[2 *
							 fast_destination_offset];
						for (fill_count = token;
						     fill_count != 0;
						     --fill_count) {
							*fill_destination++ =
								(uint16_t)color;
						}
					}
					fast_destination_offset += token;
				}
			}

			destination += g_front_state.draw_surface_pitch & ~1;
			--fast_rows_remaining;
		} while (fast_rows_remaining != 0);
		return;
	}

	row = image->pixels;
	destination =
		&g_draw_surface_ptr[g_front_state.draw_surface_pitch * dest_y +
				    2 * dest_x];
	while (src_top > 0) {
#ifdef XVT_MODERN
		memcpy(&row_length, row, sizeof(row_length));
#else
		row_length = *(const int *)row;
#endif
		row += row_length;
		--src_top;
	}

	if (visible_height <= 0) {
		return;
	}

	int rows_remaining = visible_height;
	int count;
	int end_offset;
	do {
		const uint8_t *row_start = row;
		int destination_offset = 0;
		int token_offset = sizeof(row_length);
#ifdef XVT_MODERN
		memcpy(&row_length, row_start, sizeof(row_length));
#else
		row_length = *(const int *)row_start;
#endif
		uint8_t started = 0;

		for (;;) {
			token = row_start[token_offset++];
			/* Until started is set, destination_offset counts the
			 * source pixels skipped toward src_left; when the row
			 * reaches src_left it is reset to 0 and from then on it
			 * is the destination column. */
			if (!started) {
				if (token == 0x80) {
					break;
				}
				if ((token & 0x80u) != 0) {
					token &= 0x7F;
					count = token;
					end_offset = destination_offset + count;
					if (end_offset >= src_left) {
						count = src_left -
							destination_offset;
						token_offset += count;
						started = 1;
						token -= count;
						destination_offset = 0;
						if (token != 0) {
							token |= 0x80;
						}
					} else {
						destination_offset = end_offset;
						token_offset += count;
					}
				} else if ((token & 0x40u) != 0) {
					token &= 0x3F;
					count = token;
					end_offset = destination_offset + token;
					if (end_offset >= src_left) {
						count = src_left -
							destination_offset;
						started = 1;
						token -= count;
						destination_offset = 0;
						if (token != 0) {
							token |= 0x40;
						}
					} else {
						destination_offset += count;
					}
				} else {
					count = token;
					end_offset = destination_offset + token;
					if (end_offset >= src_left) {
						count = src_left -
							destination_offset;
						started = 1;
						token -= count;
						destination_offset = 0;
						if (token == 0) {
							++token_offset;
						}
					} else {
						++token_offset;
						destination_offset += count;
					}
				}
			}

			if (started != 1 || token == 0) {
				continue;
			}
			if (token == 0x80) {
				break;
			}

			if ((token & 0x80u) != 0) {
				token &= 0x7F;
				end_offset = destination_offset + token;
				if (end_offset >= visible_width) {
					count = (uint8_t)(visible_width -
							  destination_offset);
					if (count != 0) {
						pixel_index = 0;
						do {
							*(uint16_t
								  *)&destination
								[2 *
								 (destination_offset +
								  pixel_index)] =
								image->color_lut
									[row_start
										 [token_offset +
										  pixel_index]];
							++pixel_index;
						} while (pixel_index < count);
					}
					break;
				}

				pixel_index = 0;
				if (token != 0) {
					do {
						*(uint16_t *)&destination
							[2 *
							 (destination_offset +
							  pixel_index)] =
							image->color_lut
								[row_start
									 [token_offset +
									  pixel_index]];
						++pixel_index;
					} while (pixel_index < token);
				}
				destination_offset = end_offset;
				token_offset += token;
			} else if ((token & 0x40u) != 0) {
				token &= 0x3F;
				destination_offset += token;
				if (destination_offset >= visible_width) {
					break;
				}
			} else {
				color = image->color_lut
						[row_start[token_offset++]];
				end_offset = destination_offset + token;
				if (end_offset >= visible_width) {
					count = (uint8_t)(visible_width -
							  destination_offset);
					if (count != 0) {
						fill_destination = (uint16_t
									    *)&destination
							[2 *
							 destination_offset];
						for (fill_count = count;
						     fill_count != 0;
						     --fill_count) {
							*fill_destination++ =
								(uint16_t)color;
						}
					}
					break;
				}

				if (token != 0) {
					fill_destination =
						(uint16_t *)&destination
							[2 *
							 destination_offset];
					for (fill_count = token;
					     fill_count != 0; --fill_count) {
						*fill_destination++ =
							(uint16_t)color;
					}
				}
				destination_offset = end_offset;
			}
		}

		row += row_length;
		destination += g_front_state.draw_surface_pitch & ~1;
		--rows_remaining;
	} while (rows_remaining != 0);
}

/* Draws the image registered under name with front_image_blit_opaque and returns
 * its result, or 0 when the name is not registered. */
// FUNCTION: XVT 0x4B5DE0
int front_image_draw_sprite_opaque(const char *name, int x, int y)
{
	int resource_index = front_image_find_resource_by_name(name);
	if (resource_index == -1) {
		return 0;
	}

	return front_image_blit_opaque(
		g_front_state.resource_table[resource_index].image, x, y);
}

/* Draws an image with its top-left corner at (x, y), clipped to the clip
 * bounds, every pixel included: an uncompressed image at 8 bits per pixel as
 * indexes and at 16 as color_lut values, an RLE-compressed one through
 * front_image_blit_rle8_opaque or front_image_blit_rle16_opaque. Returns the
 * clip-edge bits of frontend_draw_rect_clip_to_bounds, or 0 when image is NULL. The
 * modern build also records the draw for its renderer. */
// FUNCTION: XVT 0x4B5E20
int front_image_blit_opaque(const struct image_resource *image, int x, int y)
{
	if (image == NULL) {
		return 0;
	}

	struct RECT clipped_rect;
	clipped_rect.left = x;
	clipped_rect.top = y;
	clipped_rect.right = x + image->width - 1;
	clipped_rect.bottom = y + image->height - 1;
	struct RECT original_rect;
	frontend_draw_rect_copy(&original_rect, &clipped_rect);
	int clip_result = frontend_draw_rect_clip_to_bounds(&clipped_rect);
	if (clipped_rect.right >= clipped_rect.left &&
	    clipped_rect.top <= clipped_rect.bottom) {
		int source_x = clipped_rect.left - original_rect.left;
		int source_y = clipped_rect.top - original_rect.top;
		int width = image->width;
		int visible_width = clipped_rect.right - source_x -
				    original_rect.right + width;
		int visible_height = clipped_rect.bottom + image->height -
				     source_y - original_rect.bottom;

#ifdef XVT_MODERN
		xvt_render_frontend_image(image, source_x, source_y,
					  x + source_x, y + source_y,
					  visible_width, visible_height,
					  XVT_SPRITE_FRONT_OPAQUE, 0);
#endif
		int display_bpp = g_front_state.display_bpp;
		if (image->is_compressed == 0) {
			const uint8_t *source;
			uint8_t *destination;
			int column;
			if (display_bpp == 8) {
				source = &image->pixels[source_y * width +
							source_x];
				destination =
					&g_draw_surface_ptr
						[x + source_x +
						 g_front_state.draw_surface_pitch *
							 (y + source_y)];
				if (visible_height > 0) {
					do {
						for (column = 0;
						     column < visible_width;
						     ++column) {
							destination[column] =
								source[column];
						}
						source += image->width;
						destination +=
							g_front_state
								.draw_surface_pitch;
						--visible_height;
					} while (visible_height != 0);
				}
			} else if (display_bpp == 16) {
				source = &image->pixels[source_y * width +
							source_x];
				destination =
					&g_draw_surface_ptr
						[2 * (x + source_x) +
						 g_front_state.draw_surface_pitch *
							 (y + source_y)];
				if (visible_height > 0) {
					do {
						for (column = 0;
						     column < visible_width;
						     ++column) {
							*(uint16_t
								  *)&destination
								[2 * column] =
								image->color_lut
									[source[column]];
						}
						source += image->width;
						destination +=
							g_front_state
								.draw_surface_pitch &
							0xFFFFFFFE;
						--visible_height;
					} while (visible_height != 0);
				}
			}
		} else if (display_bpp == 8) {
			front_image_blit_rle8_opaque(
				image, x + source_x, y + source_y, source_x,
				source_y, visible_width, visible_height);
		} else if (display_bpp == 16) {
			front_image_blit_rle16_opaque(
				image, x + source_x, y + source_y, source_x,
				source_y, visible_width, visible_height);
		}
	}

	return clip_result;
}

/* Does what front_image_blit_rle8 does, but fills the pixels of skip tokens with
 * index 0 instead of leaving them. */
// FUNCTION: XVT 0x4B5FE0
void front_image_blit_rle8_opaque(const struct image_resource *image,
				  int dest_x, int dest_y, int src_left,
				  int src_top, int visible_width,
				  int visible_height)
{
	const uint8_t *row = image->pixels;
	uint8_t *destination;
	int row_length;
	uint8_t token;
	uint8_t value;
	if (visible_width == image->width) {
		destination =
			&g_draw_surface_ptr[g_front_state.draw_surface_pitch *
						    dest_y +
					    dest_x];
		while (src_top > 0) {
#ifdef XVT_MODERN
			memcpy(&row_length, row, sizeof(row_length));
#else
			row_length = *(const int *)row;
#endif
			row += row_length;
			--src_top;
		}

		if (visible_height <= 0) {
			return;
		}

		int fast_rows_remaining = visible_height;
		do {
			int fast_destination_offset = 0;
			row += sizeof(row_length);
			for (;;) {
				token = *row++;
				if (token == 0x80) {
					break;
				}

				if ((token & 0x80u) != 0) {
					token &= 0x7F;
					memcpy(destination +
						       fast_destination_offset,
					       row, token);
					fast_destination_offset += token;
					row += token;
					continue;
				}

				if ((token & 0x40u) != 0) {
					token &= 0x3F;
					memset(destination +
						       fast_destination_offset,
					       0, token);
					fast_destination_offset += token;
					continue;
				}

				value = *row++;
				memset(destination + fast_destination_offset,
				       value, token);
				fast_destination_offset += token;
			}

			destination += g_front_state.draw_surface_pitch;
			--fast_rows_remaining;
		} while (fast_rows_remaining != 0);
		return;
	}

	destination =
		&g_draw_surface_ptr[g_front_state.draw_surface_pitch * dest_y +
				    dest_x];
	while (src_top > 0) {
#ifdef XVT_MODERN
		memcpy(&row_length, row, sizeof(row_length));
#else
		row_length = *(const int *)row;
#endif
		row += row_length;
		--src_top;
	}

	if (visible_height <= 0) {
		return;
	}

	int rows_remaining = visible_height;
	int count;
	int end_offset;
	do {
		const uint8_t *row_start = row;
		int destination_offset = 0;
		int token_offset = sizeof(row_length);
#ifdef XVT_MODERN
		memcpy(&row_length, row_start, sizeof(row_length));
#else
		row_length = *(const int *)row_start;
#endif
		uint8_t started = 0;

		for (;;) {
			token = row_start[token_offset++];
			/* Until started is set, destination_offset counts the
			 * source pixels skipped toward src_left; when the row
			 * reaches src_left it is reset to 0 and from then on it
			 * is the destination column. */
			if (!started) {
				if (token == 0x80) {
					break;
				}

				if ((token & 0x80u) != 0) {
					token &= 0x7F;
					count = token;
					end_offset = destination_offset + count;
					if (end_offset >= src_left) {
						count = src_left -
							destination_offset;
						token_offset += count;
						started = 1;
						token -= count;
						destination_offset = 0;
						if (token != 0) {
							token |= 0x80;
						}
					} else {
						destination_offset = end_offset;
						token_offset += count;
					}
				} else if ((token & 0x40u) != 0) {
					token &= 0x3F;
					count = token;
					end_offset = destination_offset + token;
					if (end_offset >= src_left) {
						count = src_left -
							destination_offset;
						started = 1;
						token -= count;
						destination_offset = 0;
						if (token != 0) {
							token |= 0x40;
						}
					} else {
						destination_offset += count;
					}
				} else {
					count = token;
					end_offset = destination_offset + token;
					if (end_offset >= src_left) {
						count = src_left -
							destination_offset;
						started = 1;
						token -= count;
						destination_offset = 0;
						if (token == 0) {
							++token_offset;
						}
					} else {
						++token_offset;
						destination_offset += count;
					}
				}
			}

			if (started != 1 || token == 0) {
				continue;
			}
			if (token == 0x80) {
				break;
			}

			if ((token & 0x80u) != 0) {
				token &= 0x7F;
				end_offset = destination_offset + token;
				if (end_offset >= visible_width) {
					token = (uint8_t)(visible_width -
							  destination_offset);
					memcpy(destination + destination_offset,
					       row_start + token_offset, token);
					break;
				}

				memcpy(destination + destination_offset,
				       row_start + token_offset, token);
				destination_offset = end_offset;
				token_offset += token;
			} else if ((token & 0x40u) != 0) {
				token &= 0x3F;
				end_offset = destination_offset + token;
				if (end_offset >= visible_width) {
					token = (uint8_t)(visible_width -
							  destination_offset);
					memset(destination + destination_offset,
					       0, token);
					break;
				}

				memset(destination + destination_offset, 0,
				       token);
				destination_offset = end_offset;
			} else {
				value = row_start[token_offset++];
				count = token;
				end_offset = destination_offset + count;
				if (end_offset >= visible_width) {
					token = (uint8_t)(visible_width -
							  destination_offset);
					memset(destination + destination_offset,
					       value, token);
					break;
				}

				memset(destination + destination_offset, value,
				       count);
				destination_offset = end_offset;
			}
		}

		row += row_length;
		destination += g_front_state.draw_surface_pitch;
		--rows_remaining;
	} while (rows_remaining != 0);
}

#ifndef XVT_MODERN
#pragma optimize("y", off)
#endif
/* Does what front_image_blit_rle16 does, but fills the pixels of skip tokens with
 * color_lut[0] instead of leaving them. */
// FUNCTION: XVT 0x4B6340
void front_image_blit_rle16_opaque(const struct image_resource *image,
				   int dest_x, int dest_y, int src_left,
				   int src_top, int visible_width,
				   int visible_height)
{
	const uint8_t *row;
	uint8_t *destination;
	int row_length;
	uint8_t token;
	int color;
	int pixel_index;
	uint16_t *fill_destination;
	int fill_count;
	int source_rows_to_skip;

	if (visible_width == image->width) {
		row = image->pixels;
		destination =
			&g_draw_surface_ptr[g_front_state.draw_surface_pitch *
						    dest_y +
					    2 * dest_x];
		source_rows_to_skip = src_top;
		if (source_rows_to_skip > 0) {
			do {
#ifdef XVT_MODERN
				memcpy(&row_length, row, sizeof(row_length));
#else
				row_length = *(const int *)row;
#endif
				row += row_length;
				--source_rows_to_skip;
			} while (source_rows_to_skip != 0);
		}

		if (visible_height <= 0) {
			return;
		}

		int fast_rows_remaining = visible_height;
		do {
			int fast_destination_offset = 0;
			row += sizeof(row_length);
			for (;;) {
				token = *row++;
				if (token == 0x80) {
					break;
				}

				if ((token & 0x80u) != 0) {
					token &= 0x7F;
					for (pixel_index = 0;
					     pixel_index < token;
					     ++pixel_index) {
						*(uint16_t *)&destination
							[2 *
							 (fast_destination_offset +
							  pixel_index)] =
							image->color_lut
								[row[pixel_index]];
					}
					fast_destination_offset += token;
					row += token;
				} else if ((token & 0x40u) != 0) {
					token &= 0x3F;
					color = image->color_lut[0];
					if (token != 0) {
						fill_destination = (uint16_t
									    *)&destination
							[2 *
							 fast_destination_offset];
						for (fill_count = token;
						     fill_count != 0;
						     --fill_count) {
							*fill_destination++ =
								(uint16_t)color;
						}
					}
					fast_destination_offset += token;
				} else {
					color = image->color_lut[*row++];
					if (token != 0) {
						fill_destination = (uint16_t
									    *)&destination
							[2 *
							 fast_destination_offset];
						for (fill_count = token;
						     fill_count != 0;
						     --fill_count) {
							*fill_destination++ =
								(uint16_t)color;
						}
					}
					fast_destination_offset += token;
				}
			}

			destination += g_front_state.draw_surface_pitch & ~1;
			--fast_rows_remaining;
		} while (fast_rows_remaining != 0);
		return;
	}

	row = image->pixels;
	destination =
		&g_draw_surface_ptr[g_front_state.draw_surface_pitch * dest_y +
				    2 * dest_x];
	source_rows_to_skip = src_top;
	if (source_rows_to_skip > 0) {
		do {
#ifdef XVT_MODERN
			memcpy(&row_length, row, sizeof(row_length));
#else
			row_length = *(const int *)row;
#endif
			--source_rows_to_skip;
			row += row_length;
		} while (source_rows_to_skip != 0);
	}

	if (visible_height <= 0) {
		return;
	}

	int rows_remaining = visible_height;
	int count;
	int end_offset;
	do {
		const uint8_t *row_start = row;
		int destination_offset = 0;
		int token_offset = sizeof(row_length);
#ifdef XVT_MODERN
		memcpy(&row_length, row_start, sizeof(row_length));
#else
		row_length = *(const int *)row_start;
#endif
		uint8_t started = 0;

		for (;;) {
			token = row_start[token_offset++];
			/* Until started is set, destination_offset counts the
			 * source pixels skipped toward src_left; when the row
			 * reaches src_left it is reset to 0 and from then on it
			 * is the destination column. */
			if (started == 0) {
				if (token == 0x80) {
					break;
				}
				if ((token & 0x80u) != 0) {
					token &= 0x7F;
					count = token;
					end_offset = destination_offset + count;
					if (end_offset >= src_left) {
						count = src_left -
							destination_offset;
						token_offset += count;
						started = 1;
						token -= count;
						destination_offset = 0;
						if (token != 0) {
							token |= 0x80;
						}
					} else {
						destination_offset = end_offset;
						token_offset += count;
					}
				} else if ((token & 0x40u) != 0) {
					token &= 0x3F;
					count = token;
					end_offset = destination_offset + token;
					if (end_offset >= src_left) {
						count = src_left -
							destination_offset;
						started = 1;
						token -= count;
						destination_offset = 0;
						if (token != 0) {
							token |= 0x40;
						}
					} else {
						destination_offset += count;
					}
				} else {
					count = token;
					end_offset = destination_offset + token;
					if (end_offset >= src_left) {
						count = src_left -
							destination_offset;
						started = 1;
						token -= count;
						destination_offset = 0;
						if (token == 0) {
							++token_offset;
						}
					} else {
						++token_offset;
						destination_offset += count;
					}
				}
			}

			if (started != 1 || token == 0) {
				continue;
			}
			if (token == 0x80) {
				break;
			}

			if ((token & 0x80u) != 0) {
				token &= 0x7F;
				end_offset = destination_offset + token;
				if (end_offset >= visible_width) {
					count = (uint8_t)(visible_width -
							  destination_offset);
					for (pixel_index = 0;
					     pixel_index < count;
					     ++pixel_index) {
						*(uint16_t *)&destination
							[2 *
							 (destination_offset +
							  pixel_index)] =
							image->color_lut
								[row_start
									 [token_offset +
									  pixel_index]];
					}
					break;
				}

				for (pixel_index = 0; pixel_index < token;
				     ++pixel_index) {
					*(uint16_t *)&destination
						[2 * (destination_offset +
						      pixel_index)] =
						image->color_lut
							[row_start
								 [token_offset +
								  pixel_index]];
				}
				destination_offset = end_offset;
				token_offset += token;
			} else if ((token & 0x40u) != 0) {
				token &= 0x3F;
				end_offset = destination_offset + token;
				color = image->color_lut[0];
				if (end_offset >= visible_width) {
					count = (uint8_t)(visible_width -
							  destination_offset);
					fill_destination =
						(uint16_t *)&destination
							[2 *
							 destination_offset];
					for (fill_count = count;
					     fill_count != 0; --fill_count) {
						*fill_destination++ =
							(uint16_t)color;
					}
					break;
				}

				fill_destination = (uint16_t *)&destination
					[2 * destination_offset];
				for (fill_count = token; fill_count != 0;
				     --fill_count) {
					*fill_destination++ = (uint16_t)color;
				}
				destination_offset = end_offset;
			} else {
				color = image->color_lut
						[row_start[token_offset++]];
				end_offset = destination_offset + token;
				if (end_offset >= visible_width) {
					count = (uint8_t)(visible_width -
							  destination_offset);
					fill_destination =
						(uint16_t *)&destination
							[2 *
							 destination_offset];
					for (fill_count = count;
					     fill_count != 0; --fill_count) {
						*fill_destination++ =
							(uint16_t)color;
					}
					break;
				}

				fill_destination = (uint16_t *)&destination
					[2 * destination_offset];
				for (fill_count = token; fill_count != 0;
				     --fill_count) {
					*fill_destination++ = (uint16_t)color;
				}
				destination_offset = end_offset;
			}
		}

		row += row_length;
		destination += g_front_state.draw_surface_pitch & ~1;
		--rows_remaining;
	} while (rows_remaining != 0);
}
#ifndef XVT_MODERN
#pragma optimize("y", on)
#endif

/* Draws a glyph image with its top-left corner at (x, y), clipped to the clip
 * bounds: every nonzero pixel in color, through front_image_blit_glyph_rle_8bpp or
 * _16bpp when it is RLE-compressed. At 16 bits per pixel, with apply_text_fade
 * nonzero while g_front_state.text_fade_frames_left is not 0, the color is first
 * faded by front_image_get_faded_glyph_color16. Returns the clip-edge bits of
 * frontend_draw_rect_clip_to_bounds, or 0 when glyph is NULL. The modern build also
 * records the glyph for its renderer. */
// FUNCTION: XVT 0x4B6780
int front_image_draw_glyph(const struct image_resource *glyph, int x, int y,
			   unsigned int color, int apply_text_fade)
{
	if (glyph == NULL) {
		return 0;
	}

	struct RECT clipped_rect;
	clipped_rect.left = x;
	clipped_rect.top = y;
	clipped_rect.right = x + glyph->width - 1;
	clipped_rect.bottom = y + glyph->height - 1;
	struct RECT original_rect;
	frontend_draw_rect_copy(&original_rect, &clipped_rect);
	int clip_result = frontend_draw_rect_clip_to_bounds(&clipped_rect);
	if (clipped_rect.right < clipped_rect.left) {
		return clip_result;
	}
	if (clipped_rect.bottom < clipped_rect.top) {
		return clip_result;
	}

#ifdef XVT_MODERN
	xvt_render_frontend_glyph(glyph, x, y, color, apply_text_fade);
#endif
	{
		int clip_left_skip = clipped_rect.left - original_rect.left;
		int clip_top_skip = clipped_rect.top - original_rect.top;
		int visible_width = clipped_rect.right - clip_left_skip -
				    original_rect.right + glyph->width;
		int visible_rows = clipped_rect.bottom + glyph->height -
				   original_rect.bottom - clip_top_skip;
		int display_bpp = g_front_state.display_bpp;
		if (glyph->is_compressed == 0) {
			switch (display_bpp) {
			case 8: {
				uint8_t *source =
					&glyph->pixels[glyph->width *
							       clip_top_skip +
						       clip_left_skip];
				uint8_t *destination =
					&g_draw_surface_ptr
						[x + clip_left_skip +
						 g_front_state.draw_surface_pitch *
							 (y + clip_top_skip)];
				if (visible_rows > 0) {
					do {
						for (int column = 0;
						     column < visible_width;
						     ++column) {
							if (source[column] !=
							    0) {
								destination[column] =
									(uint8_t)
										color;
							}
						}
						source += glyph->width;
						destination +=
							g_front_state
								.draw_surface_pitch;
						--visible_rows;
					} while (visible_rows != 0);
				}
				break;
			}
			case 16: {
				uint16_t draw_color = (uint16_t)color;
				if (apply_text_fade != 0 &&
				    g_front_state.text_fade_frames_left != 0) {
					draw_color = (uint16_t)
						front_image_get_faded_glyph_color16(
							color);
				}
				uint8_t *source =
					&glyph->pixels[glyph->width *
							       clip_top_skip +
						       clip_left_skip];
				uint8_t *destination =
					&g_draw_surface_ptr
						[2 * (x + clip_left_skip) +
						 g_front_state.draw_surface_pitch *
							 (y + clip_top_skip)];
				if (visible_rows > 0) {
					do {
						for (int column = 0;
						     column < visible_width;
						     ++column) {
							if (source[column] !=
							    0) {
								*(uint16_t
									  *)&destination
									[2 *
									 column] =
									draw_color;
							}
						}
						source += glyph->width;
						destination +=
							g_front_state
								.draw_surface_pitch &
							0xFFFFFFFE;
						--visible_rows;
					} while (visible_rows != 0);
				}
				break;
			}
			}
		} else {
			switch (display_bpp) {
			case 8:
				front_image_blit_glyph_rle_8bpp(
					glyph, x + clip_left_skip,
					y + clip_top_skip, clip_left_skip,
					clip_top_skip, visible_width,
					visible_rows, (uint8_t)color);
				break;
			case 16: {
				unsigned int draw_color = color;
				if (apply_text_fade != 0 &&
				    g_front_state.text_fade_frames_left != 0) {
					draw_color =
						front_image_get_faded_glyph_color16(
							color);
				}
				front_image_blit_glyph_rle_16bpp(
					glyph, x + clip_left_skip,
					y + clip_top_skip, clip_left_skip,
					clip_top_skip, visible_width,
					visible_rows, draw_color);
				break;
			}
			}
		}
	}
	return clip_result;
}

/* Draws the visible part of an RLE-compressed glyph at 8 bits per pixel, the
 * way front_image_blit_rle8 crops it (clip_left_skip and clip_top_skip are its
 * src_left and src_top): the pixels of literal and run tokens in color, skip
 * tokens left as they are. When visible_width is the glyph's width it draws
 * whole rows. */
// FUNCTION: XVT 0x4B69B0
void front_image_blit_glyph_rle_8bpp(const struct image_resource *glyph,
				     int dest_x, int dest_y, int clip_left_skip,
				     int clip_top_skip, int visible_width,
				     int visible_rows, uint8_t color)
{
	const uint8_t *row;
	uint8_t *destination;
	uint8_t token;
	int destination_offset;
	int y;

	if (visible_width == glyph->width) {
		row = glyph->pixels;
		destination =
			&g_draw_surface_ptr[dest_x +
					    g_front_state.draw_surface_pitch *
						    dest_y];
		for (y = 0; y < clip_top_skip; ++y) {
			int row_length;

#ifdef XVT_MODERN
			memcpy(&row_length, row, sizeof(row_length));
#else
			row_length = *(const int *)row;
#endif
			row += row_length;
		}

		if (visible_rows <= 0) {
			return;
		}

		y = visible_rows;
		destination_offset = 0;
		row += 4;
		for (;;) {
			token = *row++;
			if (token != 0x80) {
				int count;

				if ((token & 0x80) != 0) {
					token &= 0x7F;
					count = token;
					memset(destination + destination_offset,
					       color, count);
					destination_offset += count;
					row += count;
					continue;
				}

				if ((token & 0x40) != 0) {
					destination_offset += token & 0x3F;
					continue;
				}

				count = token;
				++row;
				memset(destination + destination_offset, color,
				       count);
				destination_offset += count;
				continue;
			}

			destination += g_front_state.draw_surface_pitch;
			if (--y == 0) {
				return;
			}
			destination_offset = 0;
			row += 4;
		}
	}

	row = glyph->pixels;
	destination =
		&g_draw_surface_ptr[dest_x +
				    g_front_state.draw_surface_pitch * dest_y];
	for (y = 0; y < clip_top_skip; ++y) {
		int row_length;

#ifdef XVT_MODERN
		memcpy(&row_length, row, sizeof(row_length));
#else
		row_length = *(const int *)row;
#endif
		row += row_length;
	}

	for (y = 0; y < visible_rows; ++y) {
		destination_offset = 0;
		int token_offset = 4;
		char started = 0;
		int row_length;
#ifdef XVT_MODERN
		memcpy(&row_length, row, sizeof(row_length));
#else
		row_length = *(const int *)row;
#endif
		for (;;) {
			token = row[token_offset++];
			/* Until started is set, destination_offset counts the
			 * source pixels skipped toward clip_left_skip; when the
			 * row reaches clip_left_skip it is reset to 0 and from
			 * then on it is the destination column. */
			if (started == 0) {
				if (token == 0x80) {
					break;
				}

				if ((token & 0x80) != 0) {
					token &= 0x7F;
					if (clip_left_skip <=
					    destination_offset + token) {
						int skip = clip_left_skip -
							   destination_offset;
						started = 1;
						destination_offset = 0;
						token_offset += skip;
						token -= (uint8_t)skip;
						if (token != 0) {
							token |= 0x80;
						}
					} else {
						destination_offset += token;
						token_offset += token;
					}
				} else if ((token & 0x40) != 0) {
					token &= 0x3F;
					if (clip_left_skip <=
					    destination_offset + token) {
						int skip = clip_left_skip -
							   destination_offset;
						started = 1;
						destination_offset = 0;
						token -= (uint8_t)skip;
						if (token != 0) {
							token |= 0x40;
						}
					} else {
						destination_offset += token;
					}
				} else {
					if (clip_left_skip <=
					    destination_offset + token) {
						int skip = clip_left_skip -
							   destination_offset;
						started = 1;
						destination_offset = 0;
						token -= (uint8_t)skip;
						if (token == 0) {
							++token_offset;
						}
					} else {
						++token_offset;
						destination_offset += token;
					}
				}
			}

			if (started != 1 || token == 0) {
				continue;
			}
			if (token == 0x80) {
				break;
			}

			if ((token & 0x80) != 0) {
				token &= 0x7F;
				if (destination_offset + token >=
				    visible_width) {
					token = (uint8_t)(visible_width -
							  destination_offset);
					memset(destination + destination_offset,
					       color, token);
					break;
				}

				memset(destination + destination_offset, color,
				       token);
				destination_offset += token;
				token_offset += token;
			} else if ((token & 0x40) != 0) {
				destination_offset += token & 0x3F;
				if (destination_offset >= visible_width) {
					break;
				}
			} else {
				++token_offset;
				if (destination_offset + token >=
				    visible_width) {
					token = (uint8_t)(visible_width -
							  destination_offset);
					memset(destination + destination_offset,
					       color, token);
					break;
				}

				memset(destination + destination_offset, color,
				       token);
				destination_offset += token;
			}
		}

		row += row_length;
		destination += g_front_state.draw_surface_pitch;
	}
}

/* Does what front_image_blit_glyph_rle_8bpp does at 16 bits per pixel. */
// FUNCTION: XVT 0x4B6CA0
void front_image_blit_glyph_rle_16bpp(const struct image_resource *glyph,
				      int dest_x, int dest_y,
				      int clip_left_skip, int clip_top_skip,
				      int visible_width, int visible_rows,
				      unsigned int color)
{
	const uint8_t *row;
	uint8_t *destination;
	uint8_t token;
	int destination_offset;
	int y;

	if (visible_width == glyph->width) {
		row = glyph->pixels;
		destination =
			&g_draw_surface_ptr[2 * dest_x +
					    g_front_state.draw_surface_pitch *
						    dest_y];
		for (y = clip_top_skip; y > 0; --y) {
			int row_length;

#ifdef XVT_MODERN
			memcpy(&row_length, row, sizeof(row_length));
#else
			row_length = *(const int *)row;
#endif
			row += row_length;
		}

		if (visible_rows > 0) {
			y = visible_rows;
			unsigned int destination_pitch =
				g_front_state.draw_surface_pitch & 0xFFFFFFFE;
			do {
				destination_offset = 0;
				row += 4;
				for (;;) {
					token = *row++;
					if (token == 0x80) {
						break;
					}

					int count;
					if ((token & 0x80) != 0) {
						count = token & 0x7F;
						uint16_t *fill_destination =
							(uint16_t *)&destination
								[2 *
								 destination_offset];
						int fill_count = count;
						while (fill_count > 0) {
							*fill_destination =
								(uint16_t)color;
							++fill_destination;
							--fill_count;
						}
						destination_offset += count;
						row += count;
					} else if ((token & 0x40) != 0) {
						destination_offset +=
							token & 0x3F;
					} else {
						count = token;
						++row;
						uint16_t *fill_destination =
							(uint16_t *)&destination
								[2 *
								 destination_offset];
						int fill_count = count;
						while (fill_count > 0) {
							*fill_destination =
								(uint16_t)color;
							++fill_destination;
							--fill_count;
						}
						destination_offset += count;
					}
				}
				destination += destination_pitch;
				--y;
			} while (y != 0);
		}
		return;
	}

	row = glyph->pixels;
	destination =
		&g_draw_surface_ptr[2 * dest_x +
				    g_front_state.draw_surface_pitch * dest_y];
	for (y = clip_top_skip; y > 0; --y) {
		int row_length;

#ifdef XVT_MODERN
		memcpy(&row_length, row, sizeof(row_length));
#else
		row_length = *(const int *)row;
#endif
		row += row_length;
	}

	if (visible_rows > 0) {
		y = visible_rows;
		unsigned int destination_pitch =
			g_front_state.draw_surface_pitch & 0xFFFFFFFE;
		do {
			destination_offset = 0;
			int token_offset = 4;
			char started = 0;
			int row_length;
#ifdef XVT_MODERN
			memcpy(&row_length, row, sizeof(row_length));
#else
			row_length = *(const int *)row;
#endif
			for (;;) {
				token = row[token_offset++];
				/* Until started is set, destination_offset
				 * counts the source pixels skipped toward
				 * clip_left_skip; when the row reaches
				 * clip_left_skip it is reset to 0 and from then
				 * on it is the destination column. */
				if (started == 0) {
					if (token == 0x80) {
						break;
					}

					if ((token & 0x80) != 0) {
						token &= 0x7F;
						if (clip_left_skip <=
						    destination_offset +
							    token) {
							int skip =
								clip_left_skip -
								destination_offset;
							started = 1;
							destination_offset = 0;
							token_offset += skip;
							token -= (uint8_t)skip;
							if (token != 0) {
								token |= 0x80;
							}
						} else {
							destination_offset +=
								token;
							token_offset += token;
						}
					} else if ((token & 0x40) != 0) {
						token &= 0x3F;
						if (clip_left_skip <=
						    destination_offset +
							    token) {
							int skip =
								clip_left_skip -
								destination_offset;
							started = 1;
							destination_offset = 0;
							token -= (uint8_t)skip;
							if (token != 0) {
								token |= 0x40;
							}
						} else {
							destination_offset +=
								token;
						}
					} else {
						if (clip_left_skip <=
						    destination_offset +
							    token) {
							int skip =
								clip_left_skip -
								destination_offset;
							started = 1;
							destination_offset = 0;
							token -= (uint8_t)skip;
							if (token == 0) {
								++token_offset;
							}
						} else {
							++token_offset;
							destination_offset +=
								token;
						}
					}
				}

				if (started != 1 || token == 0) {
					continue;
				}
				if (token == 0x80) {
					break;
				}

				if ((token & 0x80) != 0) {
					int count = token & 0x7F;
					uint16_t *fill_destination;
					int fill_count;
					if (destination_offset + count >=
					    visible_width) {
						count = (uint8_t)(visible_width -
								  destination_offset);
						fill_destination = (uint16_t
									    *)&destination
							[2 *
							 destination_offset];
						fill_count = count;
						while (fill_count > 0) {
							*fill_destination =
								(uint16_t)color;
							++fill_destination;
							--fill_count;
						}
						break;
					}
					fill_destination =
						(uint16_t *)&destination
							[2 *
							 destination_offset];
					fill_count = count;
					while (fill_count > 0) {
						*fill_destination =
							(uint16_t)color;
						++fill_destination;
						--fill_count;
					}
					destination_offset += count;
					token_offset += count;
				} else if ((token & 0x40) != 0) {
					destination_offset += token & 0x3F;
					if (destination_offset >=
					    visible_width) {
						break;
					}
				} else {
					++token_offset;
					int count = token;
					uint16_t *fill_destination;
					int fill_count;
					if (destination_offset + count >=
					    visible_width) {
						count = (uint8_t)(visible_width -
								  destination_offset);
						fill_destination = (uint16_t
									    *)&destination
							[2 *
							 destination_offset];
						fill_count = count;
						while (fill_count > 0) {
							*fill_destination =
								(uint16_t)color;
							++fill_destination;
							--fill_count;
						}
						break;
					}
					fill_destination =
						(uint16_t *)&destination
							[2 *
							 destination_offset];
					fill_count = count;
					while (fill_count > 0) {
						*fill_destination =
							(uint16_t)color;
						++fill_destination;
						--fill_count;
					}
					destination_offset += count;
				}
			}

			row += row_length;
			destination += destination_pitch;
			--y;
		} while (y != 0);
	}
}

/* Loads a 4- or 8-bit .bmp file into image as one palette index per pixel, rows
 * top to bottom, and returns 1. Needs the "BM" signature and 1 plane; reads the
 * palette, decodes the pixels with front_image_decode_bmp4bpp or
 * front_image_decode_bmp8bpp, then, at 8 bits per pixel with
 * remap_to_display_palette 1, maps the indexes to the display palette
 * (front_image_remap_palette), or at 16 bits fills image->color_lut from the
 * file's palette in the 555 or 565 layout. Sets width, height, pixels,
 * isCompressed 0 and pixel_data_bytes, and with compress_rle 1 compresses it
 * (front_image_compress_rle). Returns 0, freeing what it allocated, when the file
 * does not open, the signature, plane count or bit depth is wrong, or the pixel
 * allocation or decoding fails. Does not handle a top-down file's negative
 * height. The modern build also registers the image with its renderer. */
// FUNCTION: XVT 0x4B6FB0
int front_image_load_bmp_file(const char *file_name,
			      struct image_resource *image,
			      int remap_to_display_palette, int compress_rle)
{
	uint8_t *pixels = NULL;
	xvt_file *stream = file_open(file_name, "rb");
	int result = 0;
	uint8_t palette[256 * 4];
	memset(palette, 0, sizeof(palette));
	struct BITMAPINFOHEADER info_header;
	if (stream != NULL) {
		struct BITMAPFILEHEADER file_header;
		file_read_bytes(stream, &file_header, sizeof(file_header));
		if (file_header.bfType == 0x4D42) {
			file_read_bytes(stream, &info_header,
					sizeof(info_header));
			int row_padding = info_header.biWidth % 4;
			if (row_padding != 0) {
				row_padding = 4 - row_padding;
			}
			pixels = malloc(row_padding +
					info_header.biHeight *
						info_header.biWidth);
			if (pixels != NULL) {
				memset(pixels, 0,
				       row_padding +
					       info_header.biHeight *
						       info_header.biWidth);
				if (info_header.biPlanes == 1) {
					unsigned int bits_per_pixel =
						info_header.biBitCount;
					switch (bits_per_pixel) {
					case 4:
						front_image_read_bmp_palette(
							stream, palette, 16);
						result =
							front_image_decode_bmp4bpp(
								stream, pixels,
								&file_header,
								&info_header);
						if (result == 1 &&
						    info_header.biCompression !=
							    0) {
							XVT_LOG_WARN(
								"image.compression_unsupported file=\"%s\" bpp=4 compression=%u",
								file_name,
								(unsigned)info_header
									.biCompression);
						}
						break;
					case 8:
						front_image_read_bmp_palette(
							stream, palette, 256);
						result =
							front_image_decode_bmp8bpp(
								stream, pixels,
								&file_header,
								&info_header);
						if (result == 1 &&
						    info_header.biCompression >
							    1) {
							XVT_LOG_WARN(
								"image.compression_unsupported file=\"%s\" bpp=8 compression=%u",
								file_name,
								(unsigned)info_header
									.biCompression);
						}
						break;
					}
					if (bits_per_pixel != 4 &&
					    bits_per_pixel != 8) {
						XVT_LOG_WARN(
							"image.bmp_unsupported file=\"%s\" planes=%d bpp=%d compression=%u",
							file_name,
							(int)info_header
								.biPlanes,
							(int)bits_per_pixel,
							(unsigned)info_header
								.biCompression);
					}

					int display_bpp =
						g_front_state.display_bpp;
					switch (display_bpp) {
					case 8:
						if (result == 1 &&
						    remap_to_display_palette ==
							    1) {
							front_image_remap_palette(
								pixels, palette,
								&info_header);
						}
						break;

					case 16: {
						uint8_t *palette_entry =
							palette;
						int *color_entry =
							image->color_lut;
						do {
							if (g_front_state
								    .pixel_format555 !=
							    0) {
								int green =
									palette_entry
										[1] >>
									3;
								int color =
									green;
								int red =
									palette_entry
										[0] >>
									3;
								red <<= 5;
								color += red;
								int blue =
									palette_entry
										[2] >>
									3;
								color <<= 5;
								*color_entry =
									color +
									blue;
							} else {
								int red =
									palette_entry
										[0] >>
									3;
								int color =
									red
									<< 6;
								int green =
									palette_entry
										[1] >>
									2;
								color += green;
								int blue =
									palette_entry
										[2] >>
									3;
								color <<= 5;
								*color_entry =
									color +
									blue;
							}
							palette_entry += 4;
							++color_entry;
						} while (
							palette_entry <
							palette +
								sizeof(palette));
						break;
					}
					}
				} else {
					XVT_LOG_WARN(
						"image.bmp_unsupported file=\"%s\" planes=%d bpp=%d compression=%u",
						file_name,
						(int)info_header.biPlanes,
						(int)info_header.biBitCount,
						(unsigned)info_header
							.biCompression);
				}
			} else {
				XVT_LOG_ERROR(
					"image.alloc_failed what=pixels bytes=%d",
					row_padding +
						info_header.biHeight *
							info_header.biWidth);
			}
		} else {
			XVT_LOG_WARN("image.not_bmp file=\"%s\" magic=%#x",
				     file_name, (unsigned)file_header.bfType);
		}
		file_close(stream);
	} else {
		XVT_LOG_WARN("image.open_failed file=\"%s\"", file_name);
	}

	if (result != 1) {
		if (pixels != NULL) {
			free(pixels);
		}
		return 0;
	}

	{
		int image_height = info_header.biHeight;
		image->width = info_header.biWidth;
		image->height = image_height;
		image->pixels = pixels;
		image->is_compressed = 0;
		image->pixel_data_bytes = image->height * info_header.biWidth;
		if (compress_rle == 1) {
			front_image_compress_rle(image);
		}
	}
#ifdef XVT_MODERN
	xvt_render_assets_register_frontend_image(
		image, file_name, remap_to_display_palette,
		g_front_state.pixel_format555);
#endif
	return 1;
}

/* Reads a 4-bit .bmp's pixel data, bfSize - bfOffBits bytes from the current
 * position, and unpacks it into dst_pixels as one index per pixel, the file's
 * bottom row last. Only uncompressed data (biCompression 0) is decoded;
 * anything else leaves dst_pixels as it is. The row padding it skips is computed
 * from biWidth >> 1, which is right for even widths. Returns 1, or 0 when the
 * data cannot be allocated. */
// FUNCTION: XVT 0x4B7240
int front_image_decode_bmp4bpp(xvt_file *stream, void *dst_pixels,
			       const struct BITMAPFILEHEADER *file_header,
			       const struct BITMAPINFOHEADER *info_header)
{
	size_t data_size = file_header->bfSize - file_header->bfOffBits;
	uint8_t *data = malloc(data_size);
	if (data == NULL) {
		XVT_LOG_ERROR("image.alloc_failed what=file_data bytes=%d",
			      (int)data_size);
		return 0;
	}

	if (info_header->biCompression == 0) {
		int src_offset = 0;
		file_read_bytes(stream, data, data_size);
		for (int16_t row = 0; row < info_header->biHeight; ++row) {
			int dst_row_offset = info_header->biWidth *
					     (info_header->biHeight - row - 1);
			for (int16_t column = 0; column < info_header->biWidth;
			     ++column) {
				if ((column & 1) != 0) {
					((uint8_t *)dst_pixels)[dst_row_offset +
								column] =
						data[src_offset] & 0x0F;
					++src_offset;
				} else {
					((uint8_t *)dst_pixels)[dst_row_offset +
								column] =
						data[src_offset] >> 4;
				}
			}

			if ((info_header->biWidth & 1) != 0) {
				++src_offset;
			}
			if ((info_header->biWidth & 6) != 0) {
				src_offset +=
					(int16_t)(4 -
						  ((info_header->biWidth >> 1) &
						   3));
			}
		}
	}

	free(data);
	return 1;
}

/* Reads an 8-bit .bmp's pixels into dst_pixels as one index per pixel, the
 * file's bottom row last: uncompressed rows (biCompression 0), skipping each
 * row's padding to 4 bytes, or RLE8 data (biCompression 1) of biSizeImage bytes
 * with its runs, end-of-line, end-of-bitmap, delta and absolute codes. Other
 * compressions decode nothing. Returns 1, or 0 when a compressed file's buffer
 * cannot be allocated. Does not check that the data stays inside dst_pixels. */
// FUNCTION: XVT 0x4B7320
int front_image_decode_bmp8bpp(xvt_file *stream, void *dst_pixels,
			       const struct BITMAPFILEHEADER *file_header,
			       const struct BITMAPINFOHEADER *info_header)
{
	(void)file_header;

	int row_padding = info_header->biWidth % 4;
	if (row_padding != 0) {
		row_padding = 4 - row_padding;
	}

	uint8_t *data = NULL;
	if (info_header->biCompression != 0) {
		data = malloc(info_header->biSizeImage);
		if (data == NULL) {
			XVT_LOG_ERROR(
				"image.alloc_failed what=file_data bytes=%d",
				(int)info_header->biSizeImage);
			return 0;
		}
	}

	uint8_t padding_buffer[4];
	switch (info_header->biCompression) {
	case 0: {
		for (int16_t row = 0; row < info_header->biHeight; ++row) {
			file_read_bytes(stream,
					(uint8_t *)dst_pixels +
						info_header->biWidth *
							(info_header->biHeight -
							 row - 1),
					info_header->biWidth);
			file_read_bytes(stream, padding_buffer, row_padding);
		}
		break;
	}
	case 1: {
		file_read_bytes(stream, data, info_header->biSizeImage);
		int source_offset = 0;
		int16_t decode_complete = 0;
		int destination_offset =
			info_header->biWidth * (info_header->biHeight - 1);
		int row_start_offset = destination_offset;
		do {
			uint8_t run_length = data[source_offset];
			++source_offset;
			if (run_length == 0) {
				uint8_t escape_code = data[source_offset];
				++source_offset;
				switch (escape_code) {
				case 0:
					row_start_offset -=
						info_header->biWidth;
					destination_offset = row_start_offset;
					break;
				case 1:
					decode_complete = 1;
					break;
				case 2: {
					uint8_t delta_x = data[source_offset];
					++source_offset;
					uint8_t delta_y = data[source_offset];
					++source_offset;
					int column_offset = destination_offset -
							    row_start_offset;
					row_start_offset -=
						delta_y * info_header->biWidth;
					destination_offset = delta_x +
							     row_start_offset +
							     column_offset;
					break;
				}
				default: {
					for (int16_t run_index = 0;
					     run_index < escape_code;
					     ++run_index) {
						((uint8_t *)dst_pixels)
							[destination_offset] = data
								[source_offset];
						++source_offset;
						++destination_offset;
					}
					if ((escape_code & 1) != 0) {
						++source_offset;
					}
					break;
				}
				}
			} else {
				uint8_t run_value = data[source_offset];
				++source_offset;
				for (int16_t run_index = 0;
				     run_index < run_length; ++run_index) {
					((uint8_t *)dst_pixels)
						[destination_offset] =
							run_value;
					++destination_offset;
				}
			}
		} while (decode_complete == 0);
		if (source_offset > (int)info_header->biSizeImage) {
			XVT_LOG_ERROR("image.bmp_overrun read=%d size=%u",
				      source_offset,
				      (unsigned)info_header->biSizeImage);
		}
		break;
	}
	default:
		break;
	}

	if (data != NULL) {
		free(data);
	}
	return 1;
}

/* Replaces every pixel's index with the display palette index nearest its color
 * in src_palette (red, green, blue and a spare byte per entry), through
 * front_image_remap_palette_index; index 0 stays 0. First sets every entry of
 * g_palette_remap_cache to 0x100, so each image is mapped afresh. */
// FUNCTION: XVT 0x4B7510
void front_image_remap_palette(uint8_t *pixels, const uint8_t *src_palette,
			       const struct BITMAPINFOHEADER *info_header)
{
	int width = info_header->biWidth;
	int height = info_header->biHeight;
	for (int i = 0; i < 256; ++i) {
		g_palette_remap_cache[i] = 0x100;
	}

	if (height > 0) {
		int rows = height;
		do {
			if (width > 0) {
				int x = width;
				do {
					uint8_t src_index = *pixels;
					*pixels =
						front_image_remap_palette_index(
							&src_palette[4 *
								     src_index],
							src_index);
					++pixels;
					--x;
				} while (x != 0);
			}

			--rows;
		} while (rows != 0);
	}
}

/* Returns the display palette index for source index srcIndex, whose color is
 * src_rgb: the one cached in g_palette_remap_cache, else 0 for index 0, else
 * frontend_display_pack_rgb's nearest entry, which it caches. */
// FUNCTION: XVT 0x4B7570
char front_image_remap_palette_index(const uint8_t *src_rgb, int src_index)
{
	int16_t *cached_index = &g_palette_remap_cache[src_index];
	if (*cached_index < 256) {
		return (char)*cached_index;
	}
	if (src_index == 0) {
		return 0;
	}

	int value =
		frontend_display_pack_rgb(src_rgb[0], src_rgb[1], src_rgb[2]);
	*cached_index = (int16_t)value;
	return (char)value;
}

/* Replaces an image's one-byte-per-pixel data with RLE rows, encoded one row at
 * a time in g_front_state.rle_row_buffer. Each row is a 4-byte size, counting
 * itself and the end byte, then tokens: 0x40 + n for n pixels of index 0 (left
 * as they are by the transparent blitters), n and a value for a run of 3 to 63
 * equal pixels, 0x80 + n and n bytes for up to 127 literal pixels, and 0x80 to
 * end the row. Encodes at most 640 pixels of each row. Returns 1 and sets
 * isCompressed to 1 and pixel_data_bytes to the encoded size, freeing the old
 * pixels. Returns 0 with the image unchanged when the encoding would be larger
 * than width * height bytes, and returns 0 after setting isCompressed to 0 when
 * the buffer cannot be allocated. Checks the old pointer instead of realloc's
 * result when shrinking, so a failed shrink leaves pixels NULL. */
// FUNCTION: XVT 0x4B75B0
int front_image_compress_rle(struct image_resource *image)
{
	int source_size = image->width * image->height;
	uint8_t *compressed_pixels = malloc(source_size);
	if (compressed_pixels == NULL) {
		XVT_LOG_ERROR(
			"image.alloc_failed what=compressed_copy bytes=%d",
			source_size);
		image->is_compressed = 0;
		return 0;
	}

	int compressed_size = 0;
	uint8_t *compressed_write = compressed_pixels;
	uint8_t *source_row = image->pixels;
	int encoded_width = 640;
	if (image->width <= encoded_width) {
		encoded_width = image->width;
	}

	int row = 0;
	if (image->height > 0) {
		do {
			uint8_t *source = source_row;
			int encoded_bytes = 0;
			int consumed = 0;
			uint8_t *token_write =
				g_front_state.rle_row_buffer.data;
			uint8_t value = *source_row;
			uint8_t *last_token = g_front_state.rle_row_buffer.data;
			g_front_state.rle_row_buffer.data[0] = 0;

			int copy_index;
			int row_size;
			if (encoded_width > 0) {
				for (;;) {
					int run_length;

					for (run_length = 0; run_length < 63;
					     ++run_length) {
						if (consumed >= encoded_width) {
							break;
						}
						if (source[run_length] !=
						    value) {
							break;
						}
						++consumed;
					}

					if (value != 0) {
						if (run_length > 2) {
							*token_write = (uint8_t)
								run_length;
							encoded_bytes += 2;
							last_token =
								token_write;
							token_write[1] = value;
							token_write += 2;
						} else {
							if ((*last_token &
							     0x80u) == 0) {
								++encoded_bytes;
								*token_write =
									(uint8_t)(run_length |
										  0x80);
								last_token =
									token_write++;
								for (copy_index =
									     0;
								     copy_index <
								     run_length;
								     ++copy_index) {
									token_write[copy_index] = source
										[copy_index];
								}
							} else {
								uint8_t run_length_byte =
									(uint8_t)(run_length +
										  (*last_token &
										   0x7f));
								if (run_length_byte <
								    0x80) {
									*last_token =
										(uint8_t)(run_length_byte |
											  0x80);
									for (copy_index =
										     0;
									     copy_index <
									     run_length;
									     ++copy_index) {
										token_write[copy_index] = source
											[copy_index];
									}
								} else {
									++encoded_bytes;
									*token_write =
										(uint8_t)(run_length |
											  0x80);
									last_token =
										token_write++;
									for (copy_index =
										     0;
									     copy_index <
									     run_length;
									     ++copy_index) {
										token_write[copy_index] = source
											[copy_index];
									}
								}
							}
							token_write +=
								run_length;
							encoded_bytes +=
								run_length;
						}
					} else {
						++encoded_bytes;
						last_token = token_write;
						*token_write++ =
							(uint8_t)(run_length |
								  0x40);
					}

					if (consumed >= encoded_width) {
						break;
					}
					source += run_length;
					value = *source;
				}

				*token_write = 0x80;
				row_size = encoded_bytes + 5;
				g_front_state.rle_row_buffer.encoded_size =
					row_size;
			} else {
				g_front_state.rle_row_buffer.data[0] = 0x80;
				g_front_state.rle_row_buffer.encoded_size = 5;
				row_size = 5;
			}

			if (source_size < compressed_size + row_size) {
				free(compressed_pixels);
				compressed_size =
					image->width * image->height + 1;
				break;
			}
			memcpy(compressed_write, &g_front_state.rle_row_buffer,
			       row_size);
			compressed_write += row_size;
			compressed_size += row_size;
			++row;
			source_row += image->width;
		} while (row < image->height);
	}

	if (image->width * image->height < compressed_size) {
		XVT_LOG_DEBUG("image.rle_skipped width=%d height=%d row=%d",
			      image->width, image->height, row);
		return 0;
	}

	{
		uint8_t *resized_pixels =
			realloc(compressed_pixels, compressed_size);
		if (compressed_pixels == NULL) {
			free(compressed_pixels);
			image->is_compressed = 0;
			return 0;
		}
		if (resized_pixels == NULL) {
			XVT_LOG_ERROR("image.rle_shrink_failed bytes=%d",
				      compressed_size);
		}
		free(image->pixels);
		image->pixels = resized_pixels;
		image->is_compressed = 1;
		image->pixel_data_bytes = compressed_size;
	}
	return 1;
}

/* Only frontend_text_load_font calls this, in the original build. Encodes one row
 * of width one-byte pixels into *row_buffer in front_image_compress_rle's format,
 * with no 640-pixel limit, sets row_buffer->encoded_size and returns it, the
 * row's size in bytes. Does not check that the row fits the buffer. */
// FUNCTION: XVT 0x4B7840
int front_image_encode_glyph_row(struct front_image_rle_row_buffer *row_buffer,
				 const uint8_t *src_pixels, int width)
{
	int encoded_bytes = 0;
	const uint8_t *source = src_pixels;
	int consumed = 0;
	uint8_t value = *source;
	row_buffer->data[0] = 0;
	uint8_t *token_write = row_buffer->data;
	uint8_t *last_token = token_write;

	if (width <= 0) {
		*token_write = 0x80;
		row_buffer->encoded_size = 5;
		return 5;
	}

	int copy_index;
	for (;;) {
		int run_length;

		for (run_length = 0; run_length < 63; ++run_length) {
			if (consumed >= width) {
				break;
			}
			if (source[run_length] != value) {
				break;
			}
			++consumed;
		}

		if (value == 0) {
			last_token = token_write;
			*token_write++ = (uint8_t)(run_length | 0x40);
			++encoded_bytes;
		} else if (run_length > 2) {
			++encoded_bytes;
			*token_write = (uint8_t)run_length;
			++encoded_bytes;
			last_token = token_write;
			token_write[1] = value;
			token_write += 2;
		} else {
			if ((*last_token & 0x80u) == 0) {
				*token_write = (uint8_t)(run_length | 0x80);
				++encoded_bytes;
				last_token = token_write++;
				for (copy_index = 0; copy_index < run_length;
				     ++copy_index) {
					token_write[copy_index] =
						source[copy_index];
				}
			} else {
				uint8_t run_length_byte =
					(uint8_t)(run_length +
						  (*last_token & 0x7f));
				if (run_length_byte < 0x80) {
					*last_token =
						(uint8_t)(run_length_byte |
							  0x80);
					for (copy_index = 0;
					     copy_index < run_length;
					     ++copy_index) {
						token_write[copy_index] =
							source[copy_index];
					}
				} else {
					*token_write =
						(uint8_t)(run_length | 0x80);
					++encoded_bytes;
					last_token = token_write++;
					for (copy_index = 0;
					     copy_index < run_length;
					     ++copy_index) {
						token_write[copy_index] =
							source[copy_index];
					}
				}
			}
			token_write += run_length;
			encoded_bytes += run_length;
		}

		if (consumed >= width) {
			break;
		}
		source += run_length;
		value = *source;
	}

	*token_write = 0x80;
	int row_size = encoded_bytes + 5;
	row_buffer->encoded_size = row_size;
	return row_size;
}

/* Copies *entry into g_front_state.resource_table at its place in name order
 * (strncmp over 64 bytes, after equal names), moving later records up one, and
 * raises g_front_state.resource_count. Does not check that the table has room. */
// FUNCTION: XVT 0x4B7970
void front_image_insert_resource_sorted(
	const struct front_image_resource_record *entry)
{
	if (g_front_state.resource_count >= 512) {
		XVT_LOG_ERROR("image.table_full count=%d",
			      g_front_state.resource_count);
	}
	int insert_index = 0;
	while (g_front_state.resource_count > insert_index) {
		if (strncmp(entry->name,
			    g_front_state.resource_table[insert_index].name,
			    sizeof(entry->name)) < 0) {
			break;
		}
		++insert_index;
	}

	if (g_front_state.resource_count > insert_index) {
		int entries_to_shift =
			g_front_state.resource_count - insert_index;
		int destination_index = g_front_state.resource_count;
		do {
			g_front_state.resource_table[destination_index] =
				g_front_state
					.resource_table[destination_index - 1];
			--destination_index;
			--entries_to_shift;
		} while (entries_to_shift != 0);
	}
	g_front_state.resource_table[insert_index] = *entry;
	++g_front_state.resource_count;
}

/* Removes the record at index from g_front_state.resource_table, moving later
 * records down one, and lowers g_front_state.resource_count. Does nothing for an
 * index outside 0 to resource_count - 1. Frees nothing; the slot past the new
 * end keeps a copy of the last record. */
// FUNCTION: XVT 0x4B7A10
void front_image_remove_resource_at(int index)
{
	int current_index = index;
	if (index < 0) {
		return;
	}
	if (g_front_state.resource_count <= index) {
		return;
	}
	if (g_front_state.resource_count - 1 > index) {
		int destination_index = index;
		do {
			struct front_image_resource_record *resource =
				&g_front_state
					 .resource_table[destination_index];
			*resource = resource[1];
			++current_index;
			++destination_index;
		} while (g_front_state.resource_count - 1 > current_index);
	}
	--g_front_state.resource_count;
}

/* Returns the index in g_front_state.resource_table of the record named name, by
 * binary search (front_image_bsearch_resource), or -1 when there is none or name
 * is NULL. */
// FUNCTION: XVT 0x4B7A70
int front_image_find_resource_by_name(const char *name)
{
	if (name == NULL) {
		return -1;
	}

	return front_image_bsearch_resource(g_front_state.resource_table,
					    g_front_state.resource_count - 1,
					    name);
}

/* Binary-searches table[0] to table[hi], sorted by name, for key, comparing 64
 * bytes with strncmp. Returns the matching index, or -1 when none matches or hi
 * is under 0. */
// FUNCTION: XVT 0x4B7AA0
int front_image_bsearch_resource(
	const struct front_image_resource_record *table, int hi,
	const char *key)
{
	int base_index = 0;
	int search_hi = hi;
	while (1) {
		if (search_hi < 0) {
			return -1;
		}
		int middle = search_hi >> 1;
		const struct front_image_resource_record *middle_entry =
			&table[middle];
		int comparison = strncmp(middle_entry->name, key,
					 sizeof(middle_entry->name));
		if (comparison == 0) {
			return middle + base_index;
		}
		if (search_hi <= 0) {
			return -1;
		}
		if (comparison < 0) {
			base_index += middle + 1;
			search_hi -= middle + 1;
			table = middle_entry + 1;
		} else {
			search_hi = middle - 1;
		}
	}
}

/* Writes width by height pixels, rows pitch bytes apart, to fileName as a
 * 24-bit .bmp and returns 1. At 8 bits per pixel each index's first three
 * palette bytes are written in their stored order, which the file reads as
 * blue, green and red; at 16 the 555 or 565 fields named by is555 are widened
 * to 8 bits with zero low bits. An odd width gets one black pixel added to each
 * row and is recorded as width + 1. Writes the pixels after a 54-byte gap, then
 * the two headers; the file header's reserved fields are written unset. Returns
 * 0 when bpp is 8 without a palette, the file does not open, or a write fails.
 * Other depths write the headers with no pixels. */
// FUNCTION: XVT 0x4B7B10
int front_image_save_bmp_file(const char *file_name, const void *pixels,
			      int width, int height, int pitch, int bpp,
			      int is555, const void *palette)
{
	const uint8_t *color_table = (const uint8_t *)palette;
	if (bpp == 8 && color_table == NULL) {
		XVT_LOG_ERROR(
			"image.save_failed file=\"%s\" step=palette bpp=%d row=-1",
			file_name, bpp);
		return 0;
	}

	xvt_file *stream = file_open(file_name, "wb");
	if (stream == NULL) {
		XVT_LOG_ERROR(
			"image.save_failed file=\"%s\" step=open bpp=%d row=-1",
			file_name, bpp);
		return 0;
	}

	file_seek(stream, 54, SEEK_SET);
	int file_size = 54;

	int y;
	int x;
	int ok;
	if (bpp != 8 && bpp != 16) {
		XVT_LOG_WARN("image.save_depth_unsupported file=\"%s\" bpp=%d",
			     file_name, bpp);
	}
	switch (bpp) {
	case 8:
		for (y = height - 1; y >= 0; y--) {
			const uint8_t *row =
				(const uint8_t *)pixels + y * pitch;

			for (x = 0; x < width; x++) {
				ok = file_write_byte(stream,
						     color_table[4 * *row]);
				if (ok == 0) {
					XVT_LOG_ERROR(
						"image.save_failed file=\"%s\" step=pixels bpp=%d row=%d",
						file_name, bpp, y);
					file_close(stream);
					return 0;
				}
				ok = file_write_byte(stream,
						     color_table[4 * *row + 1]);
				if (ok == 0) {
					XVT_LOG_ERROR(
						"image.save_failed file=\"%s\" step=pixels bpp=%d row=%d",
						file_name, bpp, y);
					file_close(stream);
					return 0;
				}
				ok = file_write_byte(stream,
						     color_table[4 * *row + 2]);
				if (ok == 0) {
					XVT_LOG_ERROR(
						"image.save_failed file=\"%s\" step=pixels bpp=%d row=%d",
						file_name, bpp, y);
					file_close(stream);
					return 0;
				}
				file_size++;
				file_size++;
				file_size++;
				row++;
			}

			if ((x & 1) != 0) {
				ok = file_write_byte(stream, 0);
				if (ok == 0) {
					XVT_LOG_ERROR(
						"image.save_failed file=\"%s\" step=pixels bpp=%d row=%d",
						file_name, bpp, y);
					file_close(stream);
					return 0;
				}
				ok = file_write_byte(stream, 0);
				if (ok == 0) {
					XVT_LOG_ERROR(
						"image.save_failed file=\"%s\" step=pixels bpp=%d row=%d",
						file_name, bpp, y);
					file_close(stream);
					return 0;
				}
				ok = file_write_byte(stream, 0);
				if (ok == 0) {
					XVT_LOG_ERROR(
						"image.save_failed file=\"%s\" step=pixels bpp=%d row=%d",
						file_name, bpp, y);
					file_close(stream);
					return 0;
				}
				file_size++;
				file_size++;
				file_size++;
			}
		}
		break;

	case 16:
		for (y = height - 1; y >= 0; y--) {
			const uint16_t *row =
				(const uint16_t *)((const uint8_t *)pixels +
						   y * pitch);

			for (x = 0; x < width; x++) {
				uint16_t value = *row;
				uint16_t green_red = value >> 5;
				uint16_t green;
				uint16_t red;
				if (is555) {
					green = 8 * green_red;
					red = green_red >> 5;
				} else {
					green = 4 * green_red;
					red = green_red >> 6;
				}

				ok = file_write_byte(stream, 8 * value);
				if (ok == 0) {
					XVT_LOG_ERROR(
						"image.save_failed file=\"%s\" step=pixels bpp=%d row=%d",
						file_name, bpp, y);
					file_close(stream);
					return 0;
				}
				ok = file_write_byte(stream, green);
				if (ok == 0) {
					XVT_LOG_ERROR(
						"image.save_failed file=\"%s\" step=pixels bpp=%d row=%d",
						file_name, bpp, y);
					file_close(stream);
					return 0;
				}
				ok = file_write_byte(stream, 8 * red);
				if (ok == 0) {
					XVT_LOG_ERROR(
						"image.save_failed file=\"%s\" step=pixels bpp=%d row=%d",
						file_name, bpp, y);
					file_close(stream);
					return 0;
				}
				file_size++;
				file_size++;
				file_size++;
				row++;
			}

			if ((x & 1) != 0) {
				ok = file_write_byte(stream, 0);
				if (ok == 0) {
					XVT_LOG_ERROR(
						"image.save_failed file=\"%s\" step=pixels bpp=%d row=%d",
						file_name, bpp, y);
					file_close(stream);
					return 0;
				}
				ok = file_write_byte(stream, 0);
				if (ok == 0) {
					XVT_LOG_ERROR(
						"image.save_failed file=\"%s\" step=pixels bpp=%d row=%d",
						file_name, bpp, y);
					file_close(stream);
					return 0;
				}
				ok = file_write_byte(stream, 0);
				if (ok == 0) {
					XVT_LOG_ERROR(
						"image.save_failed file=\"%s\" step=pixels bpp=%d row=%d",
						file_name, bpp, y);
					file_close(stream);
					return 0;
				}
				file_size++;
				file_size++;
				file_size++;
			}
		}
		break;
	}

	file_seek(stream, 0, SEEK_SET);
	struct front_image_bmp_file_header file_header;
	file_header.signature = 0x4d42;
	file_header.file_size = (uint32_t)file_size;
	file_header.pixel_offset = 54;
	struct front_image_bmp_info_header info_header;
	info_header.header_size = 40;

	if ((width & 1) != 0) {
		info_header.width = width + 1;
	} else {
		info_header.width = width;
	}
	info_header.height = height;
	info_header.planes = 1;
	info_header.bits_per_pixel = 24;
	info_header.compression = 0;
	info_header.image_size = (uint32_t)(file_size - 54);
	info_header.pixels_per_meter_x = 0;
	info_header.pixels_per_meter_y = 0;
	info_header.colors_used = 0;
	info_header.colors_important = 0;

	ok = file_write_bytes(stream, &file_header, sizeof(file_header));
	if (ok == 0) {
		XVT_LOG_ERROR(
			"image.save_failed file=\"%s\" step=headers bpp=%d row=-1",
			file_name, bpp);
		file_close(stream);
		return 0;
	}
	ok = file_write_bytes(stream, &info_header, sizeof(info_header));
	if (ok == 0) {
		XVT_LOG_ERROR(
			"image.save_failed file=\"%s\" step=headers bpp=%d row=-1",
			file_name, bpp);
		file_close(stream);
		return 0;
	}

	file_close(stream);
	XVT_LOG_INFO(
		"image.saved file=\"%s\" width=%d height=%d bpp=%d bytes=%d",
		file_name, width, height, bpp, file_size);
	return 1;
}

/* Nothing calls this. Reads the palette of a 4- or 8-bit .bmp into dest_rgba
 * through front_image_read_bmp_palette, which writes 1,024 bytes, and returns 1;
 * returns 0 when dest_rgba is NULL, the file does not open, or the signature,
 * plane count or bit depth is wrong. */
// FUNCTION: XVT 0x4D6D30
int front_image_load_bmp_palette_file(const char *file_name, uint8_t *dest_rgba)
{
	if (dest_rgba == NULL) {
		return 0;
	}

	xvt_file *stream = file_open(file_name, "rb");
	int result = 0;
	if (stream != NULL) {
		struct front_image_bmp_file_header file_header;
		file_read_bytes(stream, &file_header, sizeof(file_header));
		if (file_header.signature == 0x4D42) {
			struct front_image_bmp_info_header info_header;
			file_read_bytes(stream, &info_header,
					sizeof(info_header));
			if (info_header.planes == 1) {
				unsigned int bits_per_pixel =
					info_header.bits_per_pixel;
				switch (bits_per_pixel) {
				case 4:
					front_image_read_bmp_palette(
						stream, dest_rgba, 16);
					result = 1;
					break;
				case 8:
					front_image_read_bmp_palette(
						stream, dest_rgba, 256);
					result = 1;
					break;
				}
			}
		}
		file_close(stream);
	}
	return result;
}

/* Zeroes 256 entries of 4 bytes at dest, then reads count .bmp palette entries,
 * stored blue, green, red, spare, into the first count entries as red, green,
 * blue and 0. Does not check the reads. */
// FUNCTION: XVT 0x4D6DD0
void front_image_read_bmp_palette(xvt_file *stream, uint8_t *dest, int count)
{
	memset(dest, 0, 256 * 4);
	uint8_t entry[4];
	for (int i = 0; i < count; i++) {
		file_read_bytes(stream, entry, sizeof(entry));
		uint8_t blue = entry[0];
		uint8_t green = entry[1];
		uint8_t red = entry[2];
		dest[i * 4] = red;
		dest[i * 4 + 1] = green;
		dest[i * 4 + 2] = blue;
		dest[i * 4 + 3] = 0;
	}
}

/* Returns the 16-bit color color16 at the current step of the text fade-in:
 * each channel times (text_fade_frame_count - text_fade_frames_left) /
 * text_fade_frame_count, in the layout g_front_state.pixel_format555 names, or 1
 * when the whole result is 0. Caches the result in
 * g_front_state.text_fade_color_cache[color16], which the frame loops clear each
 * frame while a fade runs, and returns a cached nonzero value as it is. Does
 * not check text_fade_frame_count for 0. */
// FUNCTION: XVT 0x4D6F50
unsigned int front_image_get_faded_glyph_color16(unsigned int color16)
{
	unsigned int color = color16;
	uint16_t *cache_entry = &g_front_state.text_fade_color_cache[color];
	uint16_t cached_color = *cache_entry;
	if (cached_color != 0) {
		return cached_color;
	}

	unsigned int blue = color;
	unsigned int green;
	/* From here color holds only the red channel: masked and shifted down,
	 * faded, then shifted back into place for packing. */
	if (g_front_state.pixel_format555 != 0) {
		blue &= 0x1F;
		green = color;
		green &= 0x3E0;
		color &= 0x7C00;
		green >>= 5;
		color >>= 10;
	} else {
		blue &= 0x1F;
		green = color;
		green &= 0x7E0;
		color &= 0xF800;
		green >>= 5;
		color >>= 11;
	}

	unsigned int fade_multiplier = g_front_state.text_fade_frame_count -
				       g_front_state.text_fade_frames_left;
	blue = fade_multiplier * blue /
	       (unsigned int)g_front_state.text_fade_frame_count;
	green = fade_multiplier * green /
		(unsigned int)g_front_state.text_fade_frame_count;
	color = fade_multiplier * color /
		(unsigned int)g_front_state.text_fade_frame_count;
	if (g_front_state.pixel_format555 != 0) {
		color = (color & 0x1F) << 5;
		green &= 0x1F;
		blue &= 0x1F;
	} else {
		color = (color & 0x1F) << 6;
		green &= 0x3F;
		blue &= 0x1F;
	}

	unsigned int result = ((color + green) << 5) + blue;
	if (result == 0) {
		result = 1;
	}
	*cache_entry = (uint16_t)result;
	return result;
}

/* Registers the images a list file names: skips its first line, then reads
 * lines of a .bmp file name, an image name and a compress flag, and passes each
 * to front_image_register_resource with no palette remap. Returns 1 at the end of
 * the file, or 0 when the file does not open or a line does not hold the three
 * fields; a failed registration is not reported. The original build reads the
 * names with no length limit into 256-byte buffers. */
// FUNCTION: XVT 0x4DF880
int front_image_load_resource_list(const char *file_name)
{
	xvt_file *stream = file_open(file_name, "r");
	if (stream == NULL) {
		XVT_LOG_ERROR("image.list_missing list=\"%s\" action=load",
			      file_name);
		return 0;
	}
	char resource_file_name[256];
	if (FILE_GETS(resource_file_name, 255, stream) == NULL) {
		XVT_LOG_DEBUG(
			"image.list_done list=\"%s\" action=load end=empty count=%d",
			file_name, g_front_state.resource_count);
		file_close(stream);
		return 1;
	}

	int compress_rle;
	char resource_name[256];
	int field_count;
	for (;;) {
#ifdef XVT_MODERN
		field_count = FILE_SCANF(stream, "%255s %255s %d\n",
					 resource_file_name, resource_name,
					 &compress_rle);
#else
		field_count =
			FILE_SCANF(stream, "%s %s %d\n", resource_file_name,
				   resource_name, &compress_rle);
#endif
		if (field_count == EOF) {
			XVT_LOG_DEBUG(
				"image.list_done list=\"%s\" action=load end=eof count=%d",
				file_name, g_front_state.resource_count);
			file_close(stream);
			return 1;
		}
		if (field_count != 3) {
			if (field_count == 1 && resource_file_name[0] == 0x1A &&
			    resource_file_name[1] == '\0') {
				XVT_LOG_DEBUG(
					"image.list_done list=\"%s\" action=load end=mark count=%d",
					file_name,
					g_front_state.resource_count);
			}
			if (field_count != 1 || resource_file_name[0] != 0x1A ||
			    resource_file_name[1] != '\0') {
				XVT_LOG_WARN(
					"image.list_line_bad list=\"%s\" action=load got=%d",
					file_name, field_count);
			}
			file_close(stream);
			return 0;
		}
		front_image_register_resource(resource_file_name, resource_name,
					      0, compress_rle);
	}
}

/* Frees the images a list file names, read the way front_image_load_resource_list
 * reads them, with front_image_free_resource_by_name. Returns 1 at the end of the
 * file, or 0 when the file does not open or a line does not hold the three
 * fields. */
// FUNCTION: XVT 0x4DF950
int front_image_unload_resource_list(const char *file_name)
{
	xvt_file *stream = file_open(file_name, "r");
	if (stream == NULL) {
		XVT_LOG_ERROR("image.list_missing list=\"%s\" action=unload",
			      file_name);
		return 0;
	}
	char resource_file_name[256];
	if (FILE_GETS(resource_file_name, 255, stream) == NULL) {
		XVT_LOG_DEBUG(
			"image.list_done list=\"%s\" action=unload end=empty count=%d",
			file_name, g_front_state.resource_count);
		file_close(stream);
		return 1;
	}

	int ignored_flags;
	char resource_name[256];
	int field_count;
	for (;;) {
#ifdef XVT_MODERN
		field_count = FILE_SCANF(stream, "%255s %255s %d\n",
					 resource_file_name, resource_name,
					 &ignored_flags);
#else
		field_count =
			FILE_SCANF(stream, "%s %s %d\n", resource_file_name,
				   resource_name, &ignored_flags);
#endif
		if (field_count == EOF) {
			XVT_LOG_DEBUG(
				"image.list_done list=\"%s\" action=unload end=eof count=%d",
				file_name, g_front_state.resource_count);
			file_close(stream);
			return 1;
		}
		if (field_count != 3) {
			if (field_count == 1 && resource_file_name[0] == 0x1A &&
			    resource_file_name[1] == '\0') {
				XVT_LOG_DEBUG(
					"image.list_done list=\"%s\" action=unload end=mark count=%d",
					file_name,
					g_front_state.resource_count);
			}
			if (field_count != 1 || resource_file_name[0] != 0x1A ||
			    resource_file_name[1] != '\0') {
				XVT_LOG_WARN(
					"image.list_line_bad list=\"%s\" action=unload got=%d",
					file_name, field_count);
			}
			file_close(stream);
			return 0;
		}
		front_image_free_resource_by_name(resource_name);
	}
}
