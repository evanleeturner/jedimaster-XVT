#include "xvt/render/tex_level.h"

#include "xvt/flight/flight.h"
#include "xvt/render/color.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/image_quantizer.h"

/* Builds the 16-bit palettes of a loaded texture block (a tex_level_header, its
 * images after it) in the space after its data, at header->dataSize, with
 * flight_palette_build16_bpp_range. Each 24-bit palette, 4 bytes per color of
 * which the first three are red, green and blue, each >> 2 to the 0 to 63
 * scale, is converted when it holds under 1024 colors. When the block's own
 * palette is 24-bit it sets header->converted_palette_offset to the output first.
 * For every image, found through the offset table, it sets
 * converted_palette_offset, counted from the image header, to the next output
 * position and converts a 24-bit image's palette there. Returns
 * header->image_count. fe_disk_io_load_resources calls it when
 * g_flight_bytes_per_pixel is 2. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40E6C0
unsigned int tex_level_convert24_bpp_palettes_to16_bpp(unsigned int *tex_level)
{
	struct tex_level_header *header = (struct tex_level_header *)tex_level;
	uint16_t *output_palette16 =
		(uint16_t *)((uint8_t *)header + header->data_size);
	uint8_t *source_palette_rgba;
	unsigned int palette_color_count;
	struct rgb_triplet *rgb_cursor;
	struct rgb_triplet src_rgb[1024];
	if (header->bits_per_pixel == 24) {
		source_palette_rgba =
			(uint8_t *)header + header->palette_offset;
		header->converted_palette_offset =
			(uint32_t)((uint8_t *)output_palette16 -
				   (uint8_t *)header);
		palette_color_count = header->palette_color_count;
		if (palette_color_count < 1024) {
			if (palette_color_count != 0) {
				rgb_cursor = src_rgb;
				unsigned int entries_remaining =
					palette_color_count;
				do {
					rgb_cursor->r =
						*source_palette_rgba++ >> 2;
					rgb_cursor->g =
						*source_palette_rgba++ >> 2;
					rgb_cursor->b =
						*source_palette_rgba++ >> 2;
					++source_palette_rgba;
					++rgb_cursor;
				} while (--entries_remaining != 0);
			}
			flight_palette_build16_bpp_range(src_rgb,
							 output_palette16, 0,
							 palette_color_count);
			output_palette16 += header->palette_color_count;
		}
	}

	unsigned int image_index = 0;
	unsigned int result = header->image_count;
	if (result != 0) {
		do {
			struct tex_level_image_header *image =
				(struct tex_level_image_header
					 *)((uint8_t *)header +
					    *(uint32_t
						      *)((uint8_t *)header +
							 header->image_offset_table_offset +
							 image_index *
								 sizeof(uint32_t)));
			image->converted_palette_offset =
				(uint32_t)((uint8_t *)output_palette16 -
					   (uint8_t *)image);
			if (image->bits_per_pixel == 24) {
				palette_color_count =
					image->palette_color_count;
				source_palette_rgba = (uint8_t *)image +
						      image->palette_offset;
				if (palette_color_count < 1024) {
					unsigned int palette_index = 0;
					if (palette_color_count != 0) {
						rgb_cursor = src_rgb;
						do {
							rgb_cursor->r =
								*source_palette_rgba++ >>
								2;
							rgb_cursor->g =
								*source_palette_rgba++ >>
								2;
							rgb_cursor->b =
								*source_palette_rgba++ >>
								2;
							++source_palette_rgba;
							++rgb_cursor;
							++palette_index;
						} while (
							image->palette_color_count >
							palette_index);
					}
					flight_palette_build16_bpp_range(
						src_rgb, output_palette16, 0,
						image->palette_color_count);
					output_palette16 +=
						image->palette_color_count;
				}
			}
			result = image_index + 1;
			image_index = result;
		} while (header->image_count > result);
	}
	return result;
}

/* While a mission palette is being collected (g_generate_mission_palette), this also hands each 24-bit image
 * to image_quantizer_classify_encoded_tex_level_image, which counts its colors into the palette being built. */
/* Builds the 8-bit palettes of a loaded texture block in the space after its
 * data, at header->dataSize: each color of a 24-bit palette, read as in
 * tex_level_convert24_bpp_palettes_to16_bpp, becomes the index of the nearest
 * g_sw_palette color from 0x40 to 0xFF (color_find_nearest_rgb_triplet_index). Sets
 * header->converted_palette_offset when the block's palette is 24-bit and every
 * image's converted_palette_offset to the next output position, converting each
 * 24-bit image's palette; there is no limit on the count. Returns the number of
 * images. fe_disk_io_load_resources calls it when g_flight_bytes_per_pixel is not
 * 2. */
// FUNCTION: XVT 0x40E7F0
unsigned int tex_level_convert24_bpp_palettes_to8_bpp(unsigned int *tex_level)
{
	struct tex_level_header *header = (struct tex_level_header *)tex_level;
	uint8_t *output_palette8 = (uint8_t *)header + header->data_size;
	const uint8_t *source_palette_rgba;
	unsigned int palette_index;
	struct rgb_triplet target_rgb;
	if (header->bits_per_pixel == 24) {
		source_palette_rgba =
			(const uint8_t *)header + header->palette_offset;
		header->converted_palette_offset =
			(uint32_t)(output_palette8 - (uint8_t *)header);
		palette_index = 0;
		while (palette_index < header->palette_color_count) {
			target_rgb.r = source_palette_rgba[0] >> 2;
			target_rgb.g = source_palette_rgba[1] >> 2;
			target_rgb.b = source_palette_rgba[2] >> 2;
			*output_palette8 =
				(uint8_t)color_find_nearest_rgb_triplet_index(
					(const uint8_t *)&target_rgb,
					(const uint8_t *)g_sw_palette, 0x40,
					0x100);
			source_palette_rgba += 4;
			++output_palette8;
			++palette_index;
		}
	}

	unsigned int image_index = 0;
	while (image_index < header->image_count) {
		struct tex_level_image_header *image =
			(struct tex_level_image_header
				 *)((uint8_t *)header +
				    *(uint32_t
					      *)((uint8_t *)&tex_level
							 [image_index] +
						 header->image_offset_table_offset));
		image->converted_palette_offset =
			(uint32_t)(output_palette8 - (uint8_t *)image);
		if (image->bits_per_pixel == 24) {
			source_palette_rgba =
				(const uint8_t *)image + image->palette_offset;
			if (g_generate_mission_palette != 0) {
				image_quantizer_classify_encoded_tex_level_image(
					(const uint8_t *)image +
						image->encoded_image_offset,
					source_palette_rgba, image->width,
					image->height, image->packing_mode);
			}
			palette_index = 0;
			while (palette_index < image->palette_color_count) {
				target_rgb.r = source_palette_rgba[0] >> 2;
				target_rgb.g = source_palette_rgba[1] >> 2;
				target_rgb.b = source_palette_rgba[2] >> 2;
				*output_palette8 = (uint8_t)
					color_find_nearest_rgb_triplet_index(
						(const uint8_t *)&target_rgb,
						(const uint8_t *)g_sw_palette,
						0x40, 0x100);
				source_palette_rgba += 4;
				++output_palette8;
				++palette_index;
			}
		}
		++image_index;
	}
	return image_index;
}
