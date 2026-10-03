#include "xvt/render/color.h"

#include "xvt/render/flight_palette.h"

/* Returns the index, from startIndex up to endIndex - 1, of the color in
 * palette (three bytes per entry, counted from entry 0) nearest target_rgb by
 * the sum of the squared channel differences; the first one on a tie. Returns
 * startIndex when endIndex is not above it. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40E940
unsigned int color_find_nearest_rgb_triplet_index(const uint8_t *target_rgb,
						  const uint8_t *palette,
						  unsigned int start_index,
						  unsigned int end_index)
{
	unsigned int nearest_index;
	unsigned int palette_index;
	int nearest_distance;
	const uint8_t *palette_entry;
	int target_components[3];

	nearest_index = start_index;
	palette_index = start_index;
	nearest_distance = INT32_MAX;
	if (end_index > start_index) {
		int red_distance;
		int blue_distance;
		int distance;

		palette_entry = &palette[3 * start_index];
		target_components[0] = target_rgb[0];
		target_components[1] = target_rgb[1];
		target_components[2] = target_rgb[2];
		do {
			red_distance = target_components[0] - palette_entry[0];
			blue_distance = target_components[2] - palette_entry[2];
			distance = red_distance * red_distance +
				   (target_components[1] - palette_entry[1]) *
					   (target_components[1] -
					    palette_entry[1]) +
				   blue_distance * blue_distance;
			if (nearest_distance > distance) {
				nearest_index = palette_index;
				nearest_distance = distance;
			}
			palette_entry += 3;
			++palette_index;
		} while (end_index > palette_index);
	}
	return nearest_index;
}

/* Fills the 65536 entries of out_table: entry v gets the index, from startIndex
 * up to endIndex - 1, of the g_sw_palette color nearest the 5-6-5 color v, its
 * 5-bit red and blue doubled to the palette's 0 to 63 scale, by
 * color_find_nearest_rgb_triplet_index. fe_disk_io_init_resources is its only
 * caller. */
// FUNCTION: XVT 0x40E9E0
void color_build_rgb565_to_palette_index_lut(uint8_t *out_table,
					     unsigned int start_index,
					     unsigned int end_index)
{
	int rgb565_value;
	int shifted_value;
	uint8_t target_rgb[3];
	uint8_t *output;
	unsigned int first_palette_index;
	unsigned int last_palette_index;

	first_palette_index = start_index;
	last_palette_index = end_index;
	output = out_table;
	rgb565_value = 0;
	do {
		shifted_value = rgb565_value >> 5;
		target_rgb[1] = shifted_value & 0x3F;
		target_rgb[0] = 2 * (((unsigned int)shifted_value >> 6) & 0x1F);
		shifted_value = rgb565_value++;
		target_rgb[2] = 2 * (shifted_value & 0x1F);
		output[rgb565_value - 1] =
			(uint8_t)color_find_nearest_rgb_triplet_index(
				target_rgb, (const uint8_t *)g_sw_palette,
				first_palette_index, last_palette_index);
	} while (rgb565_value < 0x10000);
}

/* Returns, cut to 8 bits, the index from startIndex up to endIndex - 1 of the
 * 5-6-5 color in palette nearest target_red, target_green and target_blue (on the
 * 5-, 6- and 5-bit scales) by the sum of the squared differences: the first
 * exact match at once, else the first nearest. With an empty range it returns
 * an index it never set. opt_model_build_runtime_node is its only caller. */
// FUNCTION: XVT 0x476B00
uint8_t color_find_nearest_rgb565_index(const uint16_t *palette, int target_red,
					int target_green, int target_blue,
					int start_index, int end_index)
{
	int nearest_distance;
	int nearest_index;
	int palette_index;

	nearest_distance = INT32_MAX;
	for (palette_index = start_index; palette_index < end_index;
	     ++palette_index) {
		uint16_t color;
		int blue;
		int green;
		int red;
		int distance;

		color = palette[palette_index];
		blue = color & 0x1F;
		color >>= 5;
		green = color & 0x3F;
		color >>= 6;
		red = color & 0x1F;
		distance = (blue - target_blue) * (blue - target_blue) +
			   (green - target_green) * (green - target_green) +
			   (red - target_red) * (red - target_red);
		if (distance == 0) {
			return (uint8_t)palette_index;
		}
		if (nearest_distance > distance) {
			nearest_distance = distance;
			nearest_index = palette_index;
		}
	}
	return (uint8_t)nearest_index;
}
