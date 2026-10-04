#include "xvt/assets/model_texture.h"

#include <stdlib.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/fediskio.h"
#include "xvt/render/color.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/std3d.h"
#include "xvt/util/debug_console.h"

/* Returns 1 when the 3D device's opaque texture format has 5 green bits
 * (g_p_fmt_opaque_texture), else 0. Its one caller, display_is_pixel_format555,
 * returns the same. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40DD40
int model_texture_is_hardware_format555(void)
{
	return g_p_fmt_opaque_texture->color_info.green_bpp == 5;
}

/* Marks a texture palette's see-through colors for the 3D card; the palette is
 * 16 sub-palettes of 256 RGB565 colors. Below g_texture_resolution_level 2 it
 * only sets palette[2304] to 0. At level 2 it sets to 0 each of the first 256
 * colors that is near black (its three 5-bit components' squares adding to
 * under 32) or that lies more than 16, in squared distance, from the same entry
 * of any of sub-palettes 1 to 6; each other color takes sub-palette 10's entry.
 * Then palette[256] gets the first index set to 0 (0xFFFF when none) and
 * palette[2304] the count set to 0, or 0 when all 256 were. The renderer reads
 * palette[2304] as the opaque half's transparent-index slot. */
// FUNCTION: XVT 0x40DD50
void model_texture_filter_hardware_palette(uint16_t *palette)
{
	if (g_texture_resolution_level != 2) {
		palette[2304] = 0;
		return;
	}
	{
		int cleared_count = 0;
		int palette_index = 0;
		int first_cleared_index = -1;
		uint16_t *entry = palette;
		do {
			uint16_t color = *entry;
			int blue = color & 0x1F;
			color >>= 6;
			int green = color & 0x1F;
			color >>= 5;
			int red = color & 0x1F;
			int color_magnitude = blue * blue;
			color_magnitude += green * green;
			color_magnitude += red * red;
			if (color_magnitude < 32) {
				*entry = 0;
				++cleared_count;
				if (first_cleared_index == -1) {
					first_cleared_index = palette_index;
				}
			} else {
				int plane_index = 1;
				uint16_t *comparison_entry = entry + 256;
				for (;;) {
					uint16_t comparison_color =
						*comparison_entry;
					int blue_delta =
						(comparison_color & 0x1F) -
						blue;
					comparison_color >>= 6;
					int green_delta =
						(comparison_color & 0x1F) -
						green;
					comparison_color >>= 5;
					int red_delta =
						(comparison_color & 0x1F) - red;
					int color_distance =
						blue_delta * blue_delta;
					color_distance +=
						green_delta * green_delta;
					color_distance += red_delta * red_delta;
					if (color_distance > 16) {
						*entry = 0;
						++cleared_count;
						if (first_cleared_index == -1) {
							first_cleared_index =
								palette_index;
						}
						break;
					}
					comparison_entry += 256;
					++plane_index;
					if (plane_index >= 7) {
						break;
					}
				}
				if (plane_index == 7) {
					*entry = entry[2560];
				}
			}
			++entry;
			++palette_index;
		} while (palette_index < 256);

		if (cleared_count < 256) {
			debug_printf("%x:AlphaTex!(%d)\n", palette,
				     cleared_count);
		} else {
			debug_printf("%x:No Alpha\n", palette);
			cleared_count = 0;
		}
		palette[256] = (uint16_t)first_cleared_index;
		palette[2304] = (uint16_t)cleared_count;
	}
}

/* Turns width by height pixels of 24-bit color at rgb24 into a paletted texture
 * at dst. Each pixel, its bytes taken in reverse order and cut to 5 bits,
 * becomes the index of an equal color in a palette built as it goes, the first
 * pixel's color at 0; once 256 colors are taken, a new color gets the nearest
 * one. After the texels it writes 16 shades of the 256 colors, shade by shade:
 * 4096 bytes of the nearest g_sw_palette index from 0x40 to 0xFF, then 4096
 * RGB565 colors with the green's low bit 0. Shades 0 to 7 turn a component c
 * into ((c << 7) + (((c * shade) & 0xFFFFF8) << 4)) >> 8, shades 8 to 15 into
 * ((c << 8) + ((((31 - c) * (shade - 8)) & 0xFFFFF8) << 5)) >> 8. Palette
 * entries past the colors taken come from uninitialized stack bytes. */
// FUNCTION: XVT 0x4720D0
void model_texture_build_paletted_shade_table(uint8_t *dst,
					      const uint8_t *rgb24, int width,
					      int height)
{
	uint8_t local_palette[256 * 3];

	const uint8_t *source_pixel = rgb24;
	local_palette[0] = source_pixel[2] >> 3;
	unsigned int palette_size = 1;
	local_palette[1] = source_pixel[1] >> 3;
	int pixel_count = width * height;
	local_palette[2] = source_pixel[0] >> 3;
	source_pixel += 3;
	uint8_t *dst_texel = dst + 1;
	dst[0] = 0;
	uint8_t target_rgb[3];
	if ((unsigned int)pixel_count > 1) {
		int remaining_pixels = pixel_count - 1;
		do {
			target_rgb[0] = source_pixel[2] >> 3;
			target_rgb[1] = source_pixel[1] >> 3;
			target_rgb[2] = source_pixel[0] >> 3;
			uint8_t palette_index =
				(uint8_t)color_find_nearest_rgb_triplet_index(
					target_rgb, local_palette, 0,
					palette_size);
			if ((local_palette[3 * palette_index] !=
				     target_rgb[0] ||
			     local_palette[3 * palette_index + 1] !=
				     target_rgb[1] ||
			     local_palette[3 * palette_index + 2] !=
				     target_rgb[2]) &&
			    palette_size < 256) {
				palette_index = (uint8_t)palette_size++;
				local_palette[3 * palette_index] =
					target_rgb[0];
				local_palette[3 * palette_index + 1] =
					target_rgb[1];
				local_palette[3 * palette_index + 2] =
					target_rgb[2];
			}
			*dst_texel++ = palette_index;
			source_pixel += 3;
			--remaining_pixels;
		} while (remaining_pixels != 0);
	}

	{
		const uint8_t *palette_entry = local_palette;
		uint8_t *indexed_shade_entry = &dst[pixel_count];
		uint16_t *packed_shade_entry =
			(uint16_t *)&dst[pixel_count + 4096];
		do {
			unsigned int shade = 0;
			uint16_t *packed_entry = packed_shade_entry;
			uint8_t *indexed_entry = indexed_shade_entry;
			do {
				int palette_component;
				int scaled_component;

				if (shade < 8) {
					scaled_component = shade;
					palette_component = palette_entry[0];
					scaled_component *= palette_component;
					scaled_component =
						(palette_component << 7) +
						((scaled_component & 0xFFFFF8)
						 << 4);
					palette_component = palette_entry[1];
					target_rgb[0] =
						(uint8_t)(scaled_component >>
							  8);
					scaled_component =
						palette_component * shade;
					scaled_component =
						(palette_component << 7) +
						((scaled_component & 0xFFFFF8)
						 << 4);
					palette_component = palette_entry[2];
					target_rgb[1] =
						(uint8_t)(scaled_component >>
							  8);
					scaled_component =
						palette_component * shade;
					scaled_component =
						(palette_component << 7) +
						((scaled_component & 0xFFFFF8)
						 << 4);
				} else {
					palette_component = palette_entry[0];
					unsigned int light_shade = shade - 8;
					scaled_component =
						light_shade *
						(31 - palette_component);
					scaled_component =
						(palette_component << 8) +
						((scaled_component & 0xFFFFF8)
						 << 5);
					palette_component = palette_entry[1];
					target_rgb[0] =
						(uint8_t)(scaled_component >>
							  8);
					scaled_component =
						light_shade *
						(31 - palette_component);
					scaled_component =
						(palette_component << 8) +
						((scaled_component & 0xFFFFF8)
						 << 5);
					palette_component = palette_entry[2];
					target_rgb[1] =
						(uint8_t)(scaled_component >>
							  8);
					scaled_component =
						light_shade *
						(31 - palette_component);
					scaled_component =
						(palette_component << 8) +
						((scaled_component & 0xFFFFF8)
						 << 5);
				}
				target_rgb[2] =
					(uint8_t)(scaled_component >> 8);
				*packed_entry = (uint16_t)(target_rgb[2] +
							   ((target_rgb[1] +
							     32 * target_rgb[0])
							    << 6));
				target_rgb[0] *= 2;
				target_rgb[1] *= 2;
				target_rgb[2] *= 2;
				*indexed_entry = (uint8_t)
					color_find_nearest_rgb_triplet_index(
						target_rgb,
						(const uint8_t *)g_sw_palette,
						0x40, 0x100);
				indexed_entry += 256;
				packed_entry += 256;
				++shade;
			} while (shade < 16);
			++packed_shade_entry;
			++indexed_shade_entry;
			palette_entry += 3;
		} while (palette_entry < local_palette + sizeof(local_palette));
	}
}

#ifndef XVT_MODERN
/* Loads a texture file into dst as a packed OPT texture and returns its bytes:
 * a 24-byte opt_texture_data header with palette 256 and inline_palette_count 16,
 * the texels, 4096 bytes of g_sw_palette shade indices and 8192 bytes of RGB565
 * shades. A name ending in "rgb", ignoring case, is tried first as the same
 * name ending in "tex". A .tex file gives its own header and texels (dataSize
 * bytes when texture_size equals width times height, else width times height),
 * then the RGB565 shades, leaving the 4096 bytes before them unwritten. An .rgb
 * file has a 512-byte header with a big-endian width and height at bytes 6 and
 * 8, then three planes of one byte per pixel; it is turned into texels and
 * shades in place much as model_texture_build_paletted_shade_table does, without
 * its 0xFFFFF8 masks, and texture_size and dataSize keep the header's bytes 8 to
 * 15. Either file returns its texel bytes plus 12312. A missing file or another
 * extension gives the 8 by 8 texture of g_default_white_texture_rgb24 and returns
 * 12376. Only the original build calls this. */
// FUNCTION: XVT 0x479E80
size_t model_texture_load_rgb_or_tex_file(uint8_t *dst, const char *file_name)
{
	char path[256];

	strcpy(path, file_name);
	char *extension = path + strlen(path) - 3;
	xvt_file *stream;
	int pixel_count;
	if (_strcmpi(extension, g_ext_rgb) == 0) {
		extension[0] = 't';
		extension[1] = 'e';
		extension[2] = 'x';
		fe_disk_io_open_global_stream(path, g_file_mode_read_binary, 0,
					      0);
		stream = (xvt_file *)g_stream;
		extension[0] = 'r';
		extension[1] = 'g';
		extension[2] = 'b';
		if (stream == NULL) {
			fe_disk_io_open_global_stream(
				path, g_file_mode_read_binary, 0, 0);
			stream = (xvt_file *)g_stream;
			if (stream == NULL) {
				uint8_t *white_texels = dst + 24;

				((unsigned int *)dst)[4] = 8;
				((unsigned int *)dst)[5] = 8;
				((unsigned int *)dst)[0] = 256;
				((unsigned int *)dst)[1] = 16;
				model_texture_build_paletted_shade_table(
					white_texels,
					g_default_white_texture_rgb24, 8, 8);
				return 12376;
			}
			{
				FILE_RAW_READ(dst, 512, 1, stream);
				int width =
					((unsigned int)dst[6] << 8) + dst[7];
				int height =
					((unsigned int)dst[8] << 8) + dst[9];
				pixel_count = width * height;
				uint8_t *source_pixel = dst + 24;
				((unsigned int *)dst)[4] = width;
				((unsigned int *)dst)[5] = height;
				FILE_RAW_READ(source_pixel, pixel_count, 3,
					      stream);
				FILE_RAW_CLOSE(stream);

				/* The three colour planes are converted to
				 * palette indices in place. */
				int palette_size = 1;
				uint8_t local_palette[256 * 3];
				local_palette[0] = source_pixel[0] >> 3;
				local_palette[1] =
					source_pixel[pixel_count] >> 3;
				local_palette[2] =
					source_pixel[pixel_count * 2] >> 3;
				uint8_t *serialized_end =
					source_pixel + pixel_count;
				dst[24] = 0;
				++source_pixel;
				uint8_t *dst_texel = source_pixel;
				uint8_t target_rgb[3];
				if (pixel_count > 1) {
					int remaining_pixels = pixel_count - 1;
					do {
						target_rgb[0] =
							source_pixel[0] >> 3;
						target_rgb[1] =
							source_pixel
								[pixel_count] >>
							3;
						target_rgb[2] =
							source_pixel
								[pixel_count *
								 2] >>
							3;
						uint8_t palette_index = (uint8_t)
							color_find_nearest_rgb_triplet_index(
								target_rgb,
								local_palette,
								0,
								palette_size);
						if ((local_palette[3 *
								   palette_index] !=
							     target_rgb[0] ||
						     local_palette[3 * palette_index +
								   1] !=
							     target_rgb[1] ||
						     local_palette[3 * palette_index +
								   2] !=
							     target_rgb[2]) &&
						    palette_size < 256) {
							palette_index = (uint8_t)
								palette_size++;
							local_palette[3 *
								      palette_index] =
								target_rgb[0];
							local_palette
								[3 * palette_index +
								 1] = target_rgb
									[1];
							local_palette
								[3 * palette_index +
								 2] = target_rgb
									[2];
						}
						*dst_texel++ = palette_index;
						++source_pixel;
						--remaining_pixels;
					} while (remaining_pixels != 0);
				}

				uint8_t *indexed_shade_entry = serialized_end;
				serialized_end += 4096;
				uint16_t *packed_shade_entry =
					(uint16_t *)serialized_end;
				serialized_end += 8192;
				((unsigned int *)dst)[0] = 256;
				((unsigned int *)dst)[1] = 16;
				const uint8_t *palette_entry = local_palette;
				do {
					int shade = 0;
					uint8_t *indexed_entry =
						indexed_shade_entry;
					uint16_t *packed_entry =
						packed_shade_entry;
					do {
						int palette_component;
						int scaled_component;

						if (shade < 8) {
							palette_component =
								palette_entry
									[0];
							scaled_component =
								palette_component *
								shade;
							scaled_component =
								(palette_component
								 << 7) +
								(scaled_component
								 << 8) / 16;
							palette_component =
								palette_entry
									[1];
							target_rgb[0] =
								(uint8_t)(scaled_component >>
									  8);
							scaled_component =
								palette_component *
								shade;
							scaled_component =
								(palette_component
								 << 7) +
								(scaled_component
								 << 8) / 16;
							palette_component =
								palette_entry
									[2];
							target_rgb[1] =
								(uint8_t)(scaled_component >>
									  8);
							scaled_component =
								palette_component *
								shade;
							scaled_component =
								(palette_component
								 << 7) +
								(scaled_component
								 << 8) / 16;
						} else {
							palette_component =
								palette_entry
									[0];
							scaled_component =
								(31 -
								 palette_component) *
								(shade - 8);
							scaled_component =
								(palette_component
								 << 8) +
								(scaled_component
								 << 8) / 8;
							palette_component =
								palette_entry
									[1];
							target_rgb[0] =
								(uint8_t)(scaled_component >>
									  8);
							scaled_component =
								(31 -
								 palette_component) *
								(shade - 8);
							scaled_component =
								(palette_component
								 << 8) +
								(scaled_component
								 << 8) / 8;
							palette_component =
								palette_entry
									[2];
							target_rgb[1] =
								(uint8_t)(scaled_component >>
									  8);
							scaled_component =
								(31 -
								 palette_component) *
								(shade - 8);
							scaled_component =
								(palette_component
								 << 8) +
								(scaled_component
								 << 8) / 8;
						}
						target_rgb[2] =
							(uint8_t)(scaled_component >>
								  8);
						*packed_entry =
							(uint16_t)(((32 * target_rgb[0] +
								     target_rgb
									     [1])
								    << 6) +
								   target_rgb
									   [2]);
						target_rgb[0] *= 2;
						target_rgb[1] *= 2;
						target_rgb[2] *= 2;
						*indexed_entry = (uint8_t)
							color_find_nearest_rgb_triplet_index(
								target_rgb,
								(const uint8_t
									 *)
									g_sw_palette,
								0x40, 0x100);
						indexed_entry += 256;
						packed_entry += 256;
						++shade;
					} while (shade < 16);
					++packed_shade_entry;
					++indexed_shade_entry;
					palette_entry += 3;
				} while (palette_entry <
					 local_palette + sizeof(local_palette));

				return serialized_end - dst;
			}
		}
	} else {
		if (_strcmpi(extension, g_ext_tex) != 0) {
			uint8_t *white_texels = dst + 24;

			((unsigned int *)dst)[4] = 8;
			((unsigned int *)dst)[5] = 8;
			((unsigned int *)dst)[0] = 256;
			((unsigned int *)dst)[1] = 16;
			model_texture_build_paletted_shade_table(
				white_texels, g_default_white_texture_rgb24, 8,
				8);
			return 12376;
		}
		fe_disk_io_open_global_stream(path, g_file_mode_read_binary, 0,
					      0);
		stream = (xvt_file *)g_stream;
		if (stream == NULL) {
			uint8_t *white_texels = dst + 24;

			((unsigned int *)dst)[4] = 8;
			((unsigned int *)dst)[5] = 8;
			((unsigned int *)dst)[0] = 256;
			((unsigned int *)dst)[1] = 16;
			model_texture_build_paletted_shade_table(
				white_texels, g_default_white_texture_rgb24, 8,
				8);
			return 12376;
		}
	}

	uint8_t *texels = dst + 24;
	FILE_RAW_READ(dst, 24, 1, stream);
	pixel_count = ((unsigned int *)dst)[4] * ((unsigned int *)dst)[5];
	if (((unsigned int *)dst)[2] == (unsigned int)pixel_count) {
		pixel_count = ((unsigned int *)dst)[3];
	}
	((unsigned int *)dst)[0] = 256;
	((unsigned int *)dst)[1] = 16;
	FILE_RAW_READ(texels, pixel_count, 1, stream);
	texels += pixel_count + 4096;
	FILE_RAW_READ(texels, 8192, 1, stream);
	FILE_RAW_CLOSE(stream);
	return pixel_count + 12312;
}
#endif
