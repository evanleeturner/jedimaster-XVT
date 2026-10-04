#include "xvt/render/flight_palette.h"

#include "xvt/flight/flight_display.h"
#include "xvt/render/color.h"
#include "xvt/render/renderer.h"

/* Bytes per pixel of the flight frame buffer: 1 in the 8-bit paletted modes, 2
 * in 16-bit color; 1 at start. Four functions write it: flight_main in the
 * original build, xvt_flight_entry_configure in the modern one,
 * flight_display_init and model_preview_load_model. */
// GLOBAL: XVT 0x5233D8
int g_flight_bytes_per_pixel = 1;
/* Flight palette brightness, 256 for 1.0 (eight fraction bits).
 * flight_palette_build_rgb_range and flight_palette_build16_bpp_range scale each
 * color's brightest channel by it, and copy colors unchanged at exactly 256. At
 * flight start flight_main (original build) or xvt_flight_entry_configure
 * (modern) sets it to (setting + 4) << 6 from the solo or multiplayer
 * brightness setting, clamped to 256 to 704; in the 8-bit modes the Alt+B key
 * raises it by 0x40, going from 0x300 back to 0x100 (flight_update_player_step,
 * xvt_flight_sim_update_player_step). */
// GLOBAL: XVT 0x523400
int g_flight_brightness_scale_q8 = 0x100;
/* The flight palette before the brightness adjustment: 256 colors with channels
 * 0 to 63. flight_palette_set_range writes it, flight_palette_apply_to_display sends
 * an adjusted copy to the display, and the color matching code reads it. */
// GLOBAL: XVT 0x9A7BC0
struct rgb_triplet g_sw_palette[256] = {{0}};
/* The flight palette as 16-bit pixels, one per palette index, for the 16-bit
 * drawing code; flight_palette_set_range rebuilds the entries it sets while
 * g_flight_pixel_mode is 2, brightness included. */
// GLOBAL: XVT 0xA00530
uint16_t g_flight_palette16_bpp[256] = {0};
/* flight_palette_apply_to_display clears its 0x1 bit; nothing else reads or writes
 * it. */
// GLOBAL: XVT 0xA081F4
uint8_t g_palette_dirty_flags = 0;

/* Writes entries startIndex to startIndex + count - 1 of src_rgb, channels 0 to
 * 63, into the same entries of dst_rgb, adjusted for g_flight_brightness_scale_q8.
 * At 256 it copies them unchanged. Otherwise it splits each color into a
 * saturation, 63 * (max - min) / max, a hue sector and offset, and a value,
 * g_flight_brightness_scale_q8 * max >> 8 capped at 63, and rebuilds the three
 * channels from them; a gray gets the value in all three. Does nothing for
 * count 0. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40E1B0
void flight_palette_build_rgb_range(const struct rgb_triplet *src_rgb,
				    struct rgb_triplet *dst_rgb,
				    int start_index, int count)
{
	if (g_flight_brightness_scale_q8 == 256) {
		if (count-- == 0) {
			return;
		}
		const struct rgb_triplet *src = &src_rgb[start_index];
		struct rgb_triplet *dst = &dst_rgb[start_index];
		do {
			dst->r = src->r;
			dst->g = src->g;
			dst->b = src->b;
			++src;
			++dst;
		} while (count-- != 0);
		return;
	}

	uint8_t max_channel;
	uint8_t min_channel;
	uint8_t r;
	uint8_t b;
	uint8_t g;
	uint8_t saturation6;
	uint8_t hue_sector;
	uint8_t hue_offset6;
	uint8_t value6;
	uint8_t low_channel;
	uint8_t offset_channel;
	uint8_t inverse_offset_channel;
	{
		if (count-- == 0) {
			return;
		}

		struct rgb_triplet *dst = &dst_rgb[start_index];
		const struct rgb_triplet *src = &src_rgb[start_index];
		do {
			r = src->r;
			g = src->g;
			b = src->b;

			if (r >= g && r >= b) {
				max_channel = r;
			} else {
				max_channel = (g >= r && g >= b) ? g : b;
			}

			if (r <= g && r <= b) {
				min_channel = r;
			} else {
				min_channel = (g <= r && g <= b) ? g : b;
			}

			if (max_channel != 0) {
				saturation6 =
					(uint8_t)(63 *
						  (max_channel - min_channel) /
						  max_channel);
			} else {
				saturation6 = 0;
			}

			if (saturation6 != 0) {
				if (r == max_channel) {
					if (g >= b) {
						hue_offset6 =
							(uint8_t)(63 * (g - b) /
								  (max_channel -
								   min_channel));
						hue_sector = 0;
					} else {
						hue_offset6 =
							(uint8_t)(63 * (g - b) /
									  (max_channel -
									   min_channel) +
								  63);
						hue_sector = 5;
					}
				} else if (g == max_channel) {
					if (b >= r) {
						hue_offset6 =
							(uint8_t)(63 * (b - r) /
								  (max_channel -
								   min_channel));
						hue_sector = 2;
					} else {
						hue_offset6 =
							(uint8_t)(63 * (b - r) /
									  (max_channel -
									   min_channel) +
								  63);
						hue_sector = 1;
					}
				} else if (r >= g) {
					hue_offset6 = (uint8_t)(63 * (r - g) /
								(max_channel -
								 min_channel));
					hue_sector = 4;
				} else {
					hue_offset6 =
						(uint8_t)(63 * (r - g) /
								  (max_channel -
								   min_channel) +
							  63);
					hue_sector = 3;
				}
			}

			value6 =
				(uint8_t)(((unsigned int)
						   g_flight_brightness_scale_q8 *
					   max_channel) >>
					  8);
			if (value6 > 63) {
				value6 = 63;
			}

			if (saturation6 != 0) {
				low_channel =
					(uint8_t)(value6 * (63 - saturation6) /
						  63);
				offset_channel =
					(uint8_t)(value6 *
						  (63 - saturation6 *
								hue_offset6 /
								63) /
						  63);
				inverse_offset_channel =
					(uint8_t)(value6 *
						  (63 -
						   saturation6 *
							   (63 - hue_offset6) /
							   63) /
						  63);

				switch (hue_sector) {
				case 0:
					r = value6;
					g = inverse_offset_channel;
					b = low_channel;
					break;
				case 1:
					r = offset_channel;
					g = value6;
					b = low_channel;
					break;
				case 2:
					r = low_channel;
					g = value6;
					b = inverse_offset_channel;
					break;
				case 3:
					r = low_channel;
					g = offset_channel;
					b = value6;
					break;
				case 4:
					r = inverse_offset_channel;
					g = low_channel;
					b = value6;
					break;
				case 5:
					r = value6;
					g = low_channel;
					b = offset_channel;
					break;
				default:
					break;
				}
			} else {
				r = value6;
				g = value6;
				b = value6;
			}

			dst->r = r;
			dst->g = g;
			dst->b = b;
			++dst;
			++src;
		} while (count-- != 0);
	}
}

/* Sends g_sw_palette, adjusted by flight_palette_build_rgb_range, to the display
 * with flight_display_set_palette_entries when g_flight_bytes_per_pixel is 1, and
 * clears the 0x1 bit of g_palette_dirty_flags. flight_render_install_callbacks
 * installs it as g_flight_reset_palette_fn; flight_main_loop in the original build
 * and xvt_flight_loading_palette in the modern one also call it. */
// FUNCTION: XVT 0x40E590
void flight_palette_apply_to_display(void)
{
	struct rgb_triplet adjusted_palette[256];

	flight_palette_build_rgb_range(g_sw_palette, adjusted_palette, 0, 256);
	if (g_flight_bytes_per_pixel == 1) {
		flight_display_set_palette_entries((uint8_t *)adjusted_palette,
						   0, 256);
	}
	g_palette_dirty_flags &= ~1;
}

/* Copies count colors from rgb_triples into g_sw_palette from entry startIdx,
 * read as unsigned 16 bits; while g_flight_pixel_mode is 2 it also rebuilds those
 * entries of g_flight_palette16_bpp with flight_palette_build16_bpp_range. Does not
 * send the colors to the display or check that they stay under 256. Installed
 * as g_flight_set_palette_range_fn. */
// FUNCTION: XVT 0x40E5E0
void flight_palette_set_range(struct rgb_triplet *rgb_triples,
			      int16_t start_idx, uint16_t count)
{
	uint16_t palette_index = (uint16_t)start_idx;
	int end_index = palette_index + count;
	while (palette_index < end_index) {
		g_sw_palette[palette_index].r = rgb_triples->r;
		g_sw_palette[palette_index].g = rgb_triples->g;
		g_sw_palette[palette_index].b = rgb_triples->b;
		++palette_index;
		++rgb_triples;
	}

	if (g_flight_pixel_mode == 2) {
		flight_palette_build16_bpp_range(g_sw_palette,
						 g_flight_palette16_bpp,
						 (uint16_t)start_idx, count);
	}
}

/* Copies the 256 colors of g_sw_palette to dst_palette. Installed as
 * g_flight_get_palette_fn. */
// FUNCTION: XVT 0x40E660
void flight_palette_get_full(struct rgb_triplet *dst_palette)
{
	uint16_t index = 0;
	do {
		dst_palette->r = g_sw_palette[index].r;
		dst_palette->g = g_sw_palette[index].g;
		dst_palette->b = g_sw_palette[index].b;
		++dst_palette;
		++index;
	} while (index < 256);
}

/* Calls flight_palette_set_range for all 256 colors. Installed as
 * g_flight_set_palette_fn. */
// FUNCTION: XVT 0x40E6A0
void flight_palette_set_full(struct rgb_triplet *rgb_triples)
{
	flight_palette_set_range(rgb_triples, 0, 256);
}

/* Calls flight_palette_apply_to_display when g_flight_bytes_per_pixel is 1.
 * flight_wnd_proc calls it on message 0x311; in the original build
 * flight_pump_window_messages calls it after bringing the flight window back to
 * the foreground. */
// FUNCTION: XVT 0x449100
void flight_palette_reset_if8_bit(void)
{
	if (g_flight_bytes_per_pixel == 1) {
		flight_palette_apply_to_display();
	}
}

/* Packs entries startIndex to startIndex + count - 1 of src_rgb, channels 0 to
 * 63, into 16-bit pixels in the same entries of dst16: 5-6-5 as (r >> 1, g,
 * b >> 1), or 5-5-5 with each channel >> 1 when display_is_pixel_format555 is
 * true. At brightness 256 the colors go in unchanged and it returns the last
 * pixel packed, or startIndex + count cut to 16 bits when it packed none.
 * Otherwise each color first gets the adjustment flight_palette_build_rgb_range
 * makes, and it returns 0, packing nothing for count 0. */
// FUNCTION: XVT 0x449410
int16_t flight_palette_build16_bpp_range(struct rgb_triplet *src_rgb,
					 uint16_t *dst16, int start_index,
					 int count)
{
	struct rgb_triplet *src;
	uint16_t *dst;
	uint16_t packed_color;

	if (g_flight_brightness_scale_q8 == 256) {
		int end_index = start_index + count;
		int16_t result = (int16_t)end_index;
		if (start_index < end_index) {
			dst = &dst16[start_index];
			src = &src_rgb[start_index];
			int remaining = count;
			do {
				if (display_is_pixel_format555()) {
					packed_color =
						(uint16_t)((((src->r & 0x7Eu)
							     << 9) &
							    0x7FFFu) |
							   (src->b >> 1) |
							   (16 *
							    (src->g & 0xFEu)));
				} else {
					packed_color =
						(uint16_t)(((src->r >> 1)
							    << 11) |
							   (src->g << 5) |
							   (src->b >> 1));
				}
				*dst = packed_color;
				result = (int16_t)packed_color;
				++dst;
				++src;
				--remaining;
			} while (remaining != 0);
		}
		return result;
	}

	if (count-- == 0) {
		return 0;
	}

	dst = &dst16[start_index];
	src = &src_rgb[start_index];
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t max_channel;
	uint8_t min_channel;
	uint8_t saturation6;
	uint8_t hue_sector;
	uint8_t hue_offset6;
	uint8_t value6;
	uint8_t low_channel;
	uint8_t offset_channel;
	uint8_t inverse_offset_channel;
	do {
		r = src->r;
		g = src->g;
		b = src->b;

		if (r < g || r < b) {
			if (g < r || g < b) {
				max_channel = b;
			} else {
				max_channel = g;
			}
		} else {
			max_channel = r;
		}

		if (r > g || r > b) {
			if (g > r || g > b) {
				min_channel = b;
			} else {
				min_channel = g;
			}
		} else {
			min_channel = r;
		}

		if (max_channel != 0) {
			saturation6 =
				(uint8_t)(63 * (max_channel - min_channel) /
					  max_channel);
		} else {
			saturation6 = 0;
		}

		if (saturation6 != 0) {
			if (r == max_channel) {
				if (g >= b) {
					hue_sector = 0;
					hue_offset6 = (uint8_t)(63 * (g - b) /
								(max_channel -
								 min_channel));
				} else {
					hue_sector = 5;
					hue_offset6 =
						(uint8_t)(63 * (g - b) /
								  (max_channel -
								   min_channel) +
							  63);
				}
			} else if (g == max_channel) {
				if (b >= r) {
					hue_sector = 2;
					hue_offset6 = (uint8_t)(63 * (b - r) /
								(max_channel -
								 min_channel));
				} else {
					hue_sector = 1;
					hue_offset6 =
						(uint8_t)(63 * (b - r) /
								  (max_channel -
								   min_channel) +
							  63);
				}
			} else if (r >= g) {
				hue_sector = 4;
				hue_offset6 =
					(uint8_t)(63 * (r - g) /
						  (max_channel - min_channel));
			} else {
				hue_sector = 3;
				hue_offset6 = (uint8_t)(63 * (r - g) /
								(max_channel -
								 min_channel) +
							63);
			}
		}

		value6 = (uint8_t)(((unsigned int)g_flight_brightness_scale_q8 *
				    max_channel) >>
				   8);
		if (value6 > 63) {
			value6 = 63;
		}

		if (saturation6 != 0) {
			low_channel =
				(uint8_t)(value6 * (63 - saturation6) / 63);
			offset_channel =
				(uint8_t)(value6 *
					  (63 -
					   saturation6 * hue_offset6 / 63) /
					  63);
			inverse_offset_channel =
				(uint8_t)(value6 *
					  (63 - saturation6 *
							(63 - hue_offset6) /
							63) /
					  63);
			switch (hue_sector) {
			case 0:
				r = value6;
				g = inverse_offset_channel;
				b = low_channel;
				break;
			case 1:
				r = offset_channel;
				g = value6;
				b = low_channel;
				break;
			case 2:
				r = low_channel;
				g = value6;
				b = inverse_offset_channel;
				break;
			case 3:
				r = low_channel;
				g = offset_channel;
				b = value6;
				break;
			case 4:
				r = inverse_offset_channel;
				g = low_channel;
				b = value6;
				break;
			case 5:
				r = value6;
				g = low_channel;
				b = offset_channel;
				break;
			default:
				break;
			}
		} else {
			r = value6;
			g = value6;
			b = value6;
		}

		if (display_is_pixel_format555()) {
			packed_color =
				(uint16_t)((((r & 0x7Eu) << 9) & 0x7FFFu) |
					   (b >> 1) | (16 * (g & 0xFEu)));
		} else {
			packed_color = (uint16_t)(((r >> 1) << 11) | (g << 5) |
						  (b >> 1));
		}
		*dst = packed_color;
		++dst;
		++src;
	} while (count-- != 0);
	return 0;
}
