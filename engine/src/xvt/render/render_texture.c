#include "xvt/render/render_texture.h"

#include <stdint.h>
#include <string.h>

#include "xvt/render/flight_palette.h"
#include "xvt/render/renderer.h"
#include "xvt/render/std3d.h"
#include "xvt/util/debug_console.h"
#include "xvt_runtime/compat/pointer_key.h"

/* Key of each entry of g_render_texture_cache, the address of the image data it
 * was made from; NULL for an unclaimed entry.
 * render_texture_find_or_allocate_cache_entry writes it and clears it all when
 * g_render_texture_cache_cursor is -1. */
// GLOBAL: XVT 0xA68750
static const void *g_render_texture_cache_keys[1024] = {0};
/* The hardware texture cache, 1024 entries found through
 * g_render_texture_cache_keys by open addressing; std3D fills an entry when it
 * uploads a texture. */
// GLOBAL: XVT 0xA69750
static struct std3d_tex_cache_node g_render_texture_cache[1024] = {0};
/* Index of the cache entry render_texture_find_or_allocate_cache_entry last
 * returned, 0 to 1023; -1, set by renderer_init_d3d_device, makes the next lookup
 * clear the cache. */
// GLOBAL: XVT 0x52F864
int g_render_texture_cache_cursor = 0;
/* Buffer render_texture_get_or_create_bitmap decodes a run-length image into, 8
 * bits per pixel, up to 65536 pixels, before the upload. */
// GLOBAL: XVT 0x52F8F0
static uint8_t g_render_texture_decode_scratch[65536] = {0};
/* Buffer render_texture_get_or_create_color_key copies an image into with its
 * transparent pixels set to index 0, up to 65536 pixels, before the upload. */
// GLOBAL: XVT 0x53F978
static uint8_t g_render_texture_color_key_scratch[65536] = {0};
/* Mask of the run-length bits in a run byte, by run-length format 0 to 8:
 * (1 << format) - 1. */
// GLOBAL: XVT 0x51A530
static const uint8_t g_bitmap_rle_run_length_mask_by_format[9] = {
	0, 1, 3, 7, 15, 31, 63, 127, 255};
/* Shift that takes the color offset out of a run byte, by run-length format 0
 * to 8: the format itself. */
// GLOBAL: XVT 0x51A540
static const uint8_t g_bitmap_rle_color_index_shift_by_format[9] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8};

/* Finds the texture cache entry keyed by cache_key, an image's address, or
 * claims a free one for it. When g_render_texture_cache_cursor is -1 it first
 * clears the 1024 keys and every entry's b_cached. It looks from slot
 * xvt_pointer_key_low_bits(cache_key) & 1023 onward, wrapping, for the key, then
 * from the same slot for an entry with b_cached 0, records the key there and
 * returns that entry, leaving its index in g_render_texture_cache_cursor. When all
 * 1024 are cached it writes a line to the debug console with
 * debug_console_write_text and returns the start slot's entry with its key
 * unchanged. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4079F0
struct std3d_tex_cache_node *
render_texture_find_or_allocate_cache_entry(const void *cache_key)
{
	int probe_count;

	if (g_render_texture_cache_cursor == -1) {
		memset(g_render_texture_cache_keys, 0,
		       sizeof(g_render_texture_cache_keys));
		for (probe_count = 0; probe_count < 1024; ++probe_count) {
			g_render_texture_cache[probe_count].b_cached = 0;
		}
	}

	int hash_slot = xvt_pointer_key_low_bits(cache_key) & 1023;
	probe_count = 0;
	g_render_texture_cache_cursor = hash_slot;
	do {
		if (g_render_texture_cache_keys
			    [g_render_texture_cache_cursor] == cache_key) {
			break;
		}
		++g_render_texture_cache_cursor;
		if (g_render_texture_cache_cursor == 1024) {
			g_render_texture_cache_cursor = 0;
		}
		++probe_count;
	} while (probe_count < 1024);
	if (probe_count == 1024) {
		g_render_texture_cache_cursor = hash_slot;
		for (probe_count = 0; probe_count < 1024; ++probe_count) {
			if (g_render_texture_cache
				    [g_render_texture_cache_cursor]
					    .b_cached == 0) {
				break;
			}
			++g_render_texture_cache_cursor;
			if (g_render_texture_cache_cursor == 1024) {
				g_render_texture_cache_cursor = 0;
			}
		}
		if (probe_count == 1024) {
			debug_console_write_text(
				"\n\n\n\n\n\nRAN OUT OF MYCACHETEXTURES!!!\n");
			return &g_render_texture_cache
				[g_render_texture_cache_cursor];
		}
	}

	g_render_texture_cache_keys[g_render_texture_cache_cursor] = cache_key;
	return &g_render_texture_cache[g_render_texture_cache_cursor];
}

/* Returns the hardware texture for a run-length image, decoding and uploading
 * it the first time. Returns NULL when width * height is over 65536 or
 * std3d_add_to_texture_cache fails. An entry already cached for pixels is
 * refreshed with std3d_cache_texture_surface and returned. Otherwise it decodes
 * into g_render_texture_decode_scratch, 8 bits per pixel: each row runs to a 0xFE
 * byte; 0xFB sets the color base from the next two bytes, low byte first; 0xFC
 * writes input[1] + 1 pixels of 0; 0xFD writes input[1] + 1 pixels of color
 * input[2]; any other byte writes (byte & mask) + 1 pixels of color
 * base + (byte >> shift), mask and shift taken by rle_format from the two
 * tables. Runs are cut at the row's width, short rows are filled with 0, and a
 * 0xFF at a row's start ends the image, the rest filled with 0. It sets
 * palette[0] to the 16-bit pixel of g_flight_transparent_color_index and converts
 * colors 0 to the highest one used (std3d_convert_palette_to1555 when the device
 * takes alpha textures and not color-key ones, else
 * std3d_copy_palette_to_scratch16) and uploads with color keying. While the device
 * has color-key textures it turns its alpha-texture flag off for the call.
 * render_quad_draw_rotated_sprite is its only caller. */
// FUNCTION: XVT 0x407AF0
struct std3d_tex_cache_node *
render_texture_get_or_create_bitmap(int width, int height, uint16_t *palette,
				    const uint8_t *pixels, int rle_format)
{
	enum {
		BITMAP_RLE_SET_COLOR_BASE = 0xFB,
		BITMAP_RLE_TRANSPARENT_RUN = 0xFC,
		BITMAP_RLE_SOLID_RUN = 0xFD,
		BITMAP_RLE_END_ROW = 0xFE,
		BITMAP_RLE_END_IMAGE = 0xFF,
		INDEXED_TEXTURE_BITS_PER_PIXEL = 8,
		MAX_BITMAP_PIXELS = 65536
	};

	if (width * height > MAX_BITMAP_PIXELS) {
		debug_printf("Error: Bitmap too large! (%d,%d)\n", width,
			     height);
		return NULL;
	}
	const uint8_t *input = pixels;
	struct std3d_tex_cache_node *node =
		render_texture_find_or_allocate_cache_entry(pixels);
	if (node->b_cached != 0) {
		std3d_cache_texture_surface(node);
		return node;
	}
	uint8_t *output = g_render_texture_decode_scratch;
	int color_base = 0;
	uint8_t color;
	uint8_t run_length;
	unsigned int max_color = 0;
	int row;
	for (row = 0; row < height; ++row) {
		if (*input == BITMAP_RLE_END_IMAGE) {
			break;
		}
		int column = 0;
		uint8_t *row_end = output + width;
		while (*input != BITMAP_RLE_END_ROW) {
			if (*input == BITMAP_RLE_SET_COLOR_BASE) {
				color_base = input[1] + (input[2] << 8);
				input += 3;
			} else if (*input == BITMAP_RLE_TRANSPARENT_RUN) {
				run_length = input[1] + 1;
				input += 2;
				if (column < width) {
					column += run_length;
					if (width < column) {
						column -= run_length;
						run_length = (uint8_t)(width -
								       column);
						column = width;
					}
					while (run_length-- != 0) {
						*output++ = 0;
					}
				}
			} else {
				if (*input == BITMAP_RLE_SOLID_RUN) {
					run_length = input[1] + 1;
					color = input[2];
					input += 3;
				} else {
					color = (uint8_t)(color_base +
							  (*input >>
							   g_bitmap_rle_color_index_shift_by_format
								   [rle_format]));
					run_length =
						(*input &
						 g_bitmap_rle_run_length_mask_by_format
							 [rle_format]) +
						1;
					++input;
				}
				if (max_color < color) {
					max_color = color;
				}
				if (column < width) {
					column += run_length;
					if (width < column) {
						column -= run_length;
						run_length = (uint8_t)(width -
								       column);
						column = width;
					}
					while (run_length-- != 0) {
						*output++ = color;
					}
				}
			}
		}
		++input;
		if (output < row_end) {
			int pad_count = row_end - output;
			memset(output, 0, (size_t)pad_count);
			output += pad_count;
		}
	}
	if (row < height) {
		memset(output, 0, (size_t)width * (size_t)(height - row));
	}
	struct std3dv_buffer source;
	memset(&source, 0, sizeof(source));
	source.storage_type = 0;
	source.raster.width = (unsigned int)width;
	source.raster.height = (unsigned int)height;
	source.raster.row_pitch = (unsigned int)width;
	source.pixels = g_render_texture_decode_scratch;
	source.raster.bpp = 8;
	source.raster.color_mode = STDCOLOR_PAL;
	int old_alpha_texture = g_p_std3d_cur_device->caps.b_alpha_texture;
	if (g_p_std3d_cur_device->caps.b_color_key_texture != 0) {
		g_p_std3d_cur_device->caps.b_alpha_texture = 0;
	}
	if (g_p_std3d_cur_device->caps.b_alpha_texture != 0) {
		palette[0] = g_flight_palette16_bpp
			[g_flight_transparent_color_index];
		std3d_convert_palette_to1555(palette, (int)max_color + 1);
	} else {
		palette[0] = g_flight_palette16_bpp
			[g_flight_transparent_color_index];
		std3d_copy_palette_to_scratch16(palette, (int)max_color + 1);
	}
	if (std3d_add_to_texture_cache(&source, node, 1, 0) == 0) {
		debug_printf(
			"AddToTextureCache returned NULL! (colorkey, (%d,%d))\n",
			width, height);
		if (g_p_std3d_cur_device->caps.b_color_key_texture != 0) {
			g_p_std3d_cur_device->caps.b_alpha_texture =
				old_alpha_texture;
		}
		return NULL;
	}
	if (g_p_std3d_cur_device->caps.b_color_key_texture != 0) {
		g_p_std3d_cur_device->caps.b_alpha_texture = old_alpha_texture;
	}
	return node;
}

/* Returns the hardware texture for an 8-bit image drawn without transparency:
 * the cached entry for pixels, refreshed with std3d_cache_texture_surface, or a
 * new upload of pixels with its 256 palette colors copied by
 * std3d_copy_palette_to_scratch16. Returns NULL when the upload fails. */
// FUNCTION: XVT 0x407E40
struct std3d_tex_cache_node *render_texture_get_or_create_opaque(
	int width, int height, const uint16_t *palette, const uint8_t *pixels)
{
	enum { INDEXED_TEXTURE_BITS_PER_PIXEL = 8, PALETTE_COLOR_COUNT = 256 };

	struct std3d_tex_cache_node *node =
		render_texture_find_or_allocate_cache_entry(pixels);
	if (node->b_cached != 0) {
		std3d_cache_texture_surface(node);
		return node;
	}
	struct std3dv_buffer source;
	memset(&source, 0, sizeof(source));
	source.pixels = (void *)pixels;
	source.storage_type = 0;
	source.raster.width = (unsigned int)width;
	source.raster.height = (unsigned int)height;
	source.raster.row_pitch = (unsigned int)width;
	source.raster.bpp = INDEXED_TEXTURE_BITS_PER_PIXEL;
	source.raster.color_mode = STDCOLOR_PAL;
	std3d_copy_palette_to_scratch16(palette, PALETTE_COLOR_COUNT);
	if (std3d_add_to_texture_cache(&source, node, 0, 0) == 0) {
		debug_printf(
			"AddToTextureCache returned NULL! (nokey (%d,%d))\n",
			width, height);
		return NULL;
	}
	return node;
}

/* Returns the color-keyed hardware texture for an 8-bit image, cached under
 * pixels + 1 so it does not share the opaque texture's entry. It copies the
 * image into g_render_texture_color_key_scratch, every pixel whose palette color is
 * 0 becoming index 0 and every other pixel of index 0 becoming the index
 * palette[256] holds; returns NULL when no pixel is visible. For the upload
 * palette entry 0 is the 16-bit pixel of g_flight_transparent_color_index and that
 * index holds the old color 0; the 256 colors are converted as in
 * render_texture_get_or_create_bitmap. Afterwards palette[0] is put back and the
 * moved entry set to 0. Returns NULL when the upload fails. Reads palette[256],
 * past the 256 colors. */
// FUNCTION: XVT 0x407F10
struct std3d_tex_cache_node *
render_texture_get_or_create_color_key(int width, int height, uint16_t *palette,
				       const uint8_t *pixels)
{
	enum { INDEXED_TEXTURE_BITS_PER_PIXEL = 8, PALETTE_COLOR_COUNT = 256 };

	struct std3d_tex_cache_node *node =
		render_texture_find_or_allocate_cache_entry(pixels + 1);
	if (node->b_cached != 0) {
		std3d_cache_texture_surface(node);
		return node;
	}
	struct std3dv_buffer source;
	memset(&source, 0, sizeof(source));
	int transparent_index = palette[PALETTE_COLOR_COUNT];
	int has_visible_pixels = 0;
	int pixel_count = width * height;
	for (int pixel_index = 0; pixel_index < pixel_count; ++pixel_index) {
		uint8_t color_index = *pixels++;
		if (palette[color_index] == 0) {
			g_render_texture_color_key_scratch[pixel_index] = 0;
		} else {
			has_visible_pixels = 1;
			if (color_index != 0) {
				g_render_texture_color_key_scratch
					[pixel_index] = color_index;
			} else {
				g_render_texture_color_key_scratch
					[pixel_index] =
						(uint8_t)transparent_index;
			}
		}
	}
	if (has_visible_pixels == 0) {
		return NULL;
	}
	source.storage_type = 0;
	source.raster.width = (unsigned int)width;
	source.raster.height = (unsigned int)height;
	source.raster.row_pitch = (unsigned int)width;
	source.raster.color_mode = STDCOLOR_PAL;
	source.pixels = g_render_texture_color_key_scratch;
	source.raster.bpp = INDEXED_TEXTURE_BITS_PER_PIXEL;
	int old_alpha_texture = g_p_std3d_cur_device->caps.b_alpha_texture;
	if (g_p_std3d_cur_device->caps.b_color_key_texture != 0) {
		g_p_std3d_cur_device->caps.b_alpha_texture = 0;
	}
	if (g_p_std3d_cur_device->caps.b_alpha_texture != 0) {
		palette[transparent_index] = palette[0];
		palette[0] = g_flight_palette16_bpp
			[g_flight_transparent_color_index];
		std3d_convert_palette_to1555(palette, PALETTE_COLOR_COUNT);
		palette[0] = palette[transparent_index];
		palette[transparent_index] = 0;
	} else {
		uint16_t *transparent_color = &palette[transparent_index];
		*transparent_color = palette[0];
		palette[0] = g_flight_palette16_bpp
			[g_flight_transparent_color_index];
		std3d_copy_palette_to_scratch16(palette, PALETTE_COLOR_COUNT);
		palette[0] = *transparent_color;
		*transparent_color = 0;
	}
	if (std3d_add_to_texture_cache(&source, node, 1, 0) == 0) {
		debug_printf(
			"AlphaTex:AddToTextureCache returned NULL! (colorkey, (%d,%d))\n",
			width, height);
		if (g_p_std3d_cur_device->caps.b_color_key_texture != 0) {
			g_p_std3d_cur_device->caps.b_alpha_texture =
				old_alpha_texture;
		}
		return NULL;
	}
	if (g_p_std3d_cur_device->caps.b_color_key_texture != 0) {
		g_p_std3d_cur_device->caps.b_alpha_texture = old_alpha_texture;
	}
	return node;
}
