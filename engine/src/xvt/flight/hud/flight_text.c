#include "xvt/flight/hud/flight_text.h"
#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/cockpit_messages.h"
#include "xvt_runtime/snapshot/cockpit_pages.h"
#endif

#include <string.h>

#include "xvt/flight/flight_surface.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/log/log_both_builds.h"

/* Nonzero while flight text wraps at g_flight_clip_right: a glyph that does not
 * fit moves to the next line, and flight_text_draw_string breaks before a word
 * that does not fit. Set by flight_text_set_word_wrap;
 * flight_loading_pulse_and_draw_progress_screen, fe_disk_io_show_retry_fail_prompt
 * and fe_disk_io_show_fatal_error_message_and_wait_key save it and put it back. */
// GLOBAL: XVT 0x9CD270
int16_t g_flight_word_wrap_enabled;
/* Nonzero while each wrap and newline first fills the rest of the line with
 * g_flight_text_bg_color. Set by flight_text_set_clear_line_background;
 * flight_loading_pulse_and_draw_progress_screen, fe_disk_io_show_retry_fail_prompt
 * and fe_disk_io_show_fatal_error_message_and_wait_key save it and put it back. */
// GLOBAL: XVT 0x9FE7E4
int16_t g_flight_clear_line_bg_enabled;
/* Never set to anything but its starting 0, and never used:
 * flight_loading_pulse_and_draw_progress_screen, fe_disk_io_show_retry_fail_prompt
 * and fe_disk_io_show_fatal_error_message_and_wait_key only save it and put it
 * back. */
// GLOBAL: XVT 0xA07C64
int16_t g_flight_text_unused_state = 0;
/* Text cursor row in pixels on the drawing surface: the top of the next
 * glyph. Written by flight_text_set_cursor and by the four glyph drawers on
 * each newline and wrap; flight_loading_pulse_and_draw_progress_screen,
 * fe_disk_io_show_retry_fail_prompt and fe_disk_io_show_fatal_error_message_and_wait_key
 * save it and put it back. */
// GLOBAL: XVT 0xA08102
int16_t g_flight_cursor_y = 0;
/* Text cursor column in pixels on the drawing surface: the left edge of the
 * next glyph. Written by flight_text_set_cursor and by the four glyph drawers,
 * which advance it by each glyph's width and reset it on newline and wrap;
 * flight_loading_pulse_and_draw_progress_screen, fe_disk_io_show_retry_fail_prompt
 * and fe_disk_io_show_fatal_error_message_and_wait_key save it and put it back. */
// GLOBAL: XVT 0xA08108
int16_t g_flight_cursor_x = 0;
/* Shared buffer where text is built before it is drawn or handed to
 * msg_add_message_ptr. Many functions write it, chiefly flight_text_set_scratch,
 * flight_text_append_scratch_string, flight_text_append_scratch_char,
 * flight_text_format_scratch_int and sprintf calls in hud.c. None of the
 * FlightText functions checks its 256-byte size. */
// GLOBAL: XVT 0x9A1ED0
char g_flight_text_scratch_buffer[256];
/* Palette index of the set pixels of the next glyphs. Many functions write
 * it, chiefly flight_text_set_color; hud_show_flight_message_pane and
 * mfd_draw_message_log_page step it by one. */
// GLOBAL: XVT 0x9A807A
uint8_t g_flight_text_color_index;
/* Palette index of the unset pixels of each glyph cell, and the color
 * flight_sw_fill_rect_or_border8bpp and flight_sw_fill_rect_or_border16bpp fill
 * with, as for the rest of a line or the clip rectangle. Many functions
 * write it, chiefly flight_text_set_background_color. */
// GLOBAL: XVT 0xA08100
uint8_t g_flight_text_bg_color;
/* Palette index of the drop shadow drawn while g_flight_text_shadow_enabled is
 * set. Written by flight_text_set_shadow_color; fe_disk_io_init_global_buffers,
 * fe_disk_io_show_retry_fail_prompt and fe_disk_io_show_fatal_error_message_and_wait_key
 * set it to 0, and those two prompts and
 * flight_loading_pulse_and_draw_progress_screen put back what they saved. */
// GLOBAL: XVT 0x9E8F52
uint8_t g_flight_text_shadow_color;
/* Font size class last given to flight_text_set_font_tier, its only writer,
 * which has the table of fonts; every caller passes 0, 1 or 2.
 * fe_disk_io_lock_global_buffers reads it to choose g_flight_font_glyph_table_sw
 * again after relocking the fonts. */
// GLOBAL: XVT 0x9D80C8
uint8_t g_flight_font_tier = 0;
/* 1 when the current font has lowercase glyphs. When it is 0 and
 * g_flight_font_tier is not 0, lowercase letters draw and measure as capitals.
 * Only flight_text_set_font_tier writes it. */
// GLOBAL: XVT 0x9A1FF4
uint8_t g_flight_font_has_lowercase = 0;
/* Line height of the current font in pixels: 5 for the micro font, 8 for the
 * small and 10 for the medium. Only flight_text_set_font_tier writes it. */
// GLOBAL: XVT 0x9A20AE
uint8_t g_flight_font_line_height = 0;
/* Digit width in pixels that the HUD lays numbers out with: 3 for the micro
 * font, 4 for the small and 5 for the medium. Only flight_text_set_font_tier
 * writes it; nothing derives it from the glyphs. */
// GLOBAL: XVT 0x9ED232
uint8_t g_flight_font_digit_width = 0;
/* Glyph records of the current font, one every g_flight_font_glyph_stride_sw
 * bytes from code 0x20: an advance width byte, a height byte, then the rows.
 * Written by flight_text_set_font_tier, and by fe_disk_io_lock_global_buffers,
 * which picks by g_flight_font_tier with another mapping (0 medium, 1 small,
 * 2 micro) and leaves the stride and line height alone. */
// GLOBAL: XVT 0x9D7674
uint8_t *g_flight_font_glyph_table_sw = 0;
/* Bytes per glyph record in g_flight_font_glyph_table_sw: 42 for the micro font,
 * 66 for the small and 82 for the medium. Only flight_text_set_font_tier writes
 * it. */
// GLOBAL: XVT 0x9D8C02
uint16_t g_flight_font_glyph_stride_sw = 0;
/* The small font, MICRO48.FNT, in the memory of g_flight_small_font_handle.
 * fe_disk_io_init_global_buffers sets it to NULL, then locks the handle and
 * loads the file; fe_disk_io_lock_global_buffers locks it again. */
// GLOBAL: XVT 0x9A7800
uint8_t *g_flight_font_small_sw = 0;
/* The medium font, MICRO64.FNT, in the memory of g_flight_medium_font_handle,
 * locked by fe_disk_io_init_global_buffers and fe_disk_io_lock_global_buffers. At
 * 320x240 fe_disk_io_init_global_buffers loads no file into it. */
// GLOBAL: XVT 0x9E965C
uint8_t *g_flight_font_medium_sw = 0;
/* The micro font, MICRO32.FNT, in the memory of g_flight_micro_font_handle,
 * locked and loaded by fe_disk_io_init_global_buffers and locked again by
 * fe_disk_io_lock_global_buffers. */
// GLOBAL: XVT 0xA07CC0
uint8_t *g_flight_font_micro_sw = 0;
/* Left edge in pixels of the clip rectangle for flight text,
 * fills and lines. Written by flight_text_set_clip_rect;
 * flight_loading_pulse_and_draw_progress_screen, fe_disk_io_show_retry_fail_prompt
 * and fe_disk_io_show_fatal_error_message_and_wait_key save it and put it back. */
// GLOBAL: XVT 0x9A6FE0
int16_t g_flight_clip_left = 0;
/* Right edge, exclusive, in pixels of the clip rectangle for flight text,
 * fills and lines. Written by flight_text_set_clip_rect;
 * flight_loading_pulse_and_draw_progress_screen, fe_disk_io_show_retry_fail_prompt
 * and fe_disk_io_show_fatal_error_message_and_wait_key save it and put it back. */
// GLOBAL: XVT 0x9D8C00
int16_t g_flight_clip_right = 0;
/* Bottom edge, exclusive, in pixels of the clip rectangle for flight text,
 * fills and lines. Written by flight_text_set_clip_rect;
 * flight_loading_pulse_and_draw_progress_screen, fe_disk_io_show_retry_fail_prompt
 * and fe_disk_io_show_fatal_error_message_and_wait_key save it and put it back. */
// GLOBAL: XVT 0xA07CE4
int16_t g_flight_clip_bottom = 0;
/* Top edge in pixels of the clip rectangle for flight text,
 * fills and lines. Written by flight_text_set_clip_rect;
 * flight_loading_pulse_and_draw_progress_screen, fe_disk_io_show_retry_fail_prompt
 * and fe_disk_io_show_fatal_error_message_and_wait_key save it and put it back. */
// GLOBAL: XVT 0xA0813C
int16_t g_flight_clip_top = 0;
/* Palette indices for the color codes 0x40 to 0x5F that
 * flight_text_set_color, flight_text_set_background_color and
 * flight_text_set_shadow_color accept: 0x40 to 0x53 give 0x2C to 0x3F in order;
 * 0x54 to 0x57 give 0xD5, 0xD5, 0xD4 and 0xD3; 0x58 to 0x5F give 0x2C to
 * 0x2F twice. */
// GLOBAL: XVT 0x524080
const uint8_t g_flight_char_to_color_lut[32] = {
	0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36,
	0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0xd5, 0xd5,
	0xd4, 0xd3, 0x2c, 0x2d, 0x2e, 0x2f, 0x2c, 0x2d, 0x2e, 0x2f,
};
/* Place value by digit position, counted from the right starting at 1: 1, 10,
 * 100, 1,000 and 10,000 for positions 1 to 5. Entry 0 is 1; entries 6 and 7
 * are 0. Read by flight_text_draw_decimal_number and msg_emit_in_flight_message. */
// GLOBAL: XVT 0x520EB0
const uint16_t g_flight_text_decimal_divisors[8] = {1,	  1,	 10, 100,
						    1000, 10000, 0,  0};
/* Nonzero while glyphs get a drop shadow in g_flight_text_shadow_color: each
 * row's set pixels repeated one pixel right on the row below, the drawn cell
 * one pixel wider. No setter; many functions write it directly, chiefly
 * hud.c drawing functions, which set 0 before their text, and
 * flight_alert_draw_box and hud_setup_ready_message_pane_text, which set 1. */
// GLOBAL: XVT 0x9EC464
uint8_t g_flight_text_shadow_enabled = 0;

/* Draws one character of the current font at the text cursor into an 8-bit
 * surface, for fonts whose rows are one byte, stored 2 bytes apart, top bit
 * leftmost. Set pixels take g_flight_text_color_index, shadow pixels
 * g_flight_text_shadow_color and the rest of the cell g_flight_text_bg_color; all
 * clip to the g_flightClip rectangle. Advances g_flight_cursor_x by the glyph's
 * advance width. A newline moves the cursor to g_flight_clip_left and
 * g_flight_font_line_height down; other codes below 0x20 draw nothing. With word
 * wrap on, a glyph that would reach g_flight_clip_right first moves to the next
 * line, the glyph's height plus 2 down, and the cursor does the same after a
 * glyph that reaches it; each newline and wrap first clears the rest of the
 * line when g_flight_clear_line_bg_enabled is set. Nothing calls this:
 * flight_render_install_callbacks never puts it in g_flight_draw_char_fn. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40F050
void flight_text_draw_narrow_glyph8bpp(uint8_t ch)
{
#ifndef XVT_MODERN
	unsigned int pixel_offset;
	unsigned int page;
	int clipped_bottom;
#endif

	if (ch == '\n') {
		if (g_flight_clear_line_bg_enabled != 0) {
			flight_text_clear_remaining_line_background8bpp();
		}
		g_flight_cursor_x = g_flight_clip_left;
		g_flight_cursor_y += g_flight_font_line_height;
		return;
	}
	if (ch < ' ') {
		return;
	}

	uint8_t normalized_char = ch;
	if (g_flight_font_tier != 0 && g_flight_font_has_lowercase == 0 &&
	    ch >= 'a' && ch <= 'z') {
		normalized_char = ch - ('a' - 'A');
	}
	const uint8_t *row_data =
		&g_flight_font_glyph_table_sw[g_flight_font_glyph_stride_sw *
					      (uint8_t)(normalized_char - ' ')];
	uint16_t glyph_advance = *row_data++;
	int glyph_width = glyph_advance;
	uint8_t glyph_height = *row_data++;
	if (glyph_width + g_flight_cursor_x >= g_flight_clip_right &&
	    g_flight_word_wrap_enabled != 0) {
		if (g_flight_clear_line_bg_enabled != 0) {
			flight_text_clear_remaining_line_background8bpp();
		}
		g_flight_cursor_x = g_flight_clip_left;
		g_flight_cursor_y += glyph_height + 2;
	}

#ifdef XVT_MODERN
	xvt_cockpit_pages_record_glyph(normalized_char, glyph_advance,
				       glyph_height, 1);
	xvt_cockpit_messages_record_glyph(normalized_char, glyph_advance,
					  glyph_height, 1);
#endif
	uint8_t shadow_bits = 0;
	int address_each_row_separately = 0;
	int line = g_flight_cursor_y;
	if (line < g_flight_clip_top) {
		line = g_flight_clip_top;
	}
	int line_offset;
#ifdef XVT_MODERN
	/* Fully clipped glyphs still advance the cursor, without looking up a row. */
	line_offset = line < g_flight_clip_bottom
			      ? flight_sw_get_line_offset(line)
			      : 0;
#else
	line_offset = flight_sw_get_line_offset(line);
#endif
#ifndef XVT_MODERN
	if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
	    g_flight_sw_framebuffer_base == g_sw_framebuffer_base) {
		page = line_offset / g_vesa_page_size_bytes;
		line_offset %= g_vesa_page_size_bytes;
		rts_vga2_set_current_page((uint8_t)g_vesa_window,
					  (uint16_t)page);
		clipped_bottom = g_flight_cursor_y + glyph_height + 1;
		if (clipped_bottom > g_flight_clip_bottom) {
			clipped_bottom = g_flight_clip_bottom;
		}
		if (line_offset + g_surface_pitch * (clipped_bottom - line) >
		    0xFFFF) {
			address_each_row_separately = 1;
		}
	}
#endif
	if (g_flight_sw_framebuffer_base != g_sw_framebuffer_base) {
		address_each_row_separately = 1;
	}

	int glyph_row_count;
	uint16_t wrap_line_height;
	int draw_x;
	int pixel_count;
	uint8_t glyph_bits;
	uint8_t palette_index;
	uint8_t *row_start;
	if (address_each_row_separately == 0) {
		uint8_t *destination =
			g_flight_sw_framebuffer_base + line_offset;
		wrap_line_height = glyph_height;
		line = g_flight_cursor_y;
		glyph_row_count = wrap_line_height;
		if (g_flight_cursor_y + glyph_row_count > line) {
			do {
				pixel_count = glyph_width;
				glyph_bits = *row_data;
				if (g_flight_text_shadow_enabled != 0) {
					++pixel_count;
				}
				draw_x = g_flight_cursor_x;
				if (draw_x < g_flight_clip_left) {
					if (g_flight_clip_left - draw_x >=
					    pixel_count) {
						break;
					}
					pixel_count = draw_x + pixel_count -
						      g_flight_clip_left;
					glyph_bits <<=
						g_flight_clip_left - draw_x;
					draw_x = g_flight_clip_left;
				}
				if (g_flight_clip_bottom <= line) {
					break;
				}
				if (g_flight_clip_top <= line) {
					if (draw_x + pixel_count >
					    g_flight_clip_right) {
						pixel_count =
							g_flight_clip_right -
							draw_x;
						if (pixel_count <= 0) {
							break;
						}
					}
					row_start = &destination[draw_x];
					while (pixel_count-- != 0) {
						if ((glyph_bits & 0x80u) != 0) {
							palette_index =
								g_flight_text_color_index;
						} else if (
							g_flight_text_shadow_enabled !=
								0 &&
							(shadow_bits & 0x80u) !=
								0) {
							palette_index =
								g_flight_text_shadow_color;
						} else {
							palette_index =
								g_flight_text_bg_color;
						}
						*row_start++ = palette_index;
						glyph_bits <<= 1;
						shadow_bits <<= 1;
					}
					destination +=
						flight_sw_get_line_pitch();
				}
				shadow_bits = *row_data;
				row_data += 2;
				++line;
				shadow_bits >>= 1;
			} while (glyph_row_count + g_flight_cursor_y > line);
		}
	} else {
		line = g_flight_cursor_y;
		wrap_line_height = glyph_height;
		glyph_row_count = wrap_line_height;
		if (g_flight_cursor_y + glyph_row_count > line) {
			do {
				pixel_count = glyph_width;
				glyph_bits = *row_data;
				if (g_flight_text_shadow_enabled != 0) {
					++pixel_count;
				}
				draw_x = g_flight_cursor_x;
				if (draw_x < g_flight_clip_left) {
					if (g_flight_clip_left - draw_x >=
					    pixel_count) {
						break;
					}
					pixel_count = draw_x + pixel_count -
						      g_flight_clip_left;
					glyph_bits <<=
						g_flight_clip_left - draw_x;
					draw_x = g_flight_clip_left;
				}
				if (g_flight_clip_bottom <= line) {
					break;
				}
				if (g_flight_clip_top <= line) {
					if (draw_x + pixel_count >
					    g_flight_clip_right) {
						pixel_count =
							g_flight_clip_right -
							draw_x;
						if (pixel_count <= 0) {
							break;
						}
					}
#ifdef XVT_MODERN
					row_start =
						&g_flight_sw_framebuffer_base
							[flight_sw_get_line_offset(
								 line) +
							 draw_x];
#else
					pixel_offset =
						draw_x +
						flight_sw_get_line_offset(line);
					if (g_flight_resolution_mode !=
						    FLIGHT_RESOLUTION_320X240 &&
					    g_flight_sw_framebuffer_base ==
						    g_sw_framebuffer_base) {
						page = pixel_offset /
						       g_vesa_page_size_bytes;
						pixel_offset %=
							g_vesa_page_size_bytes;
						rts_vga2_set_current_page(
							(uint8_t)g_vesa_window,
							(uint16_t)page);
					}
					row_start =
						&g_flight_sw_framebuffer_base
							[pixel_offset];
#endif
					while (pixel_count-- != 0) {
						if ((glyph_bits & 0x80u) != 0) {
							palette_index =
								g_flight_text_color_index;
						} else if (
							g_flight_text_shadow_enabled !=
								0 &&
							(shadow_bits & 0x80u) !=
								0) {
							palette_index =
								g_flight_text_shadow_color;
						} else {
							palette_index =
								g_flight_text_bg_color;
						}
						*row_start++ = palette_index;
						glyph_bits <<= 1;
						shadow_bits <<= 1;
					}
				}
				shadow_bits = *row_data;
				row_data += 2;
				++line;
				shadow_bits >>= 1;
			} while (glyph_row_count + g_flight_cursor_y > line);
		}
	}

	g_flight_cursor_x += glyph_advance;
	if (g_flight_clip_right <= g_flight_cursor_x &&
	    g_flight_word_wrap_enabled != 0) {
		if (g_flight_clear_line_bg_enabled != 0) {
			flight_text_clear_remaining_line_background8bpp();
		}
		g_flight_cursor_x = g_flight_clip_left;
		g_flight_cursor_y += wrap_line_height + 2;
	}
}

/* Draws one character at the text cursor into an 8-bit surface, as
 * flight_text_draw_narrow_glyph8bpp does, for fonts whose rows are a 32-bit
 * little-endian word, stored 8 bytes apart, top bit leftmost; a newline moves
 * down g_flight_font_line_height plus 1. flight_render_install_callbacks installs
 * it as g_flight_draw_char_fn for pixel modes 0 and 1. In the modern build it
 * also records the glyph for the modern renderer. */
// FUNCTION: XVT 0x40F520
void flight_text_draw_wide_glyph8bpp(uint8_t ch)
{
#ifndef XVT_MODERN
	unsigned int pixel_offset;
	unsigned int page;
	int clipped_bottom;
#endif

	if (ch == '\n') {
		if (g_flight_clear_line_bg_enabled != 0) {
			flight_text_clear_remaining_line_background8bpp();
		}
		g_flight_cursor_x = g_flight_clip_left;
		g_flight_cursor_y += g_flight_font_line_height + 1;
		return;
	}
	if (ch < ' ') {
		return;
	}

	uint8_t normalized_char = ch;
	if (g_flight_font_tier != 0 && g_flight_font_has_lowercase == 0 &&
	    ch >= 'a' && ch <= 'z') {
		normalized_char = ch - ('a' - 'A');
	}
	const uint8_t *row_data =
		&g_flight_font_glyph_table_sw[g_flight_font_glyph_stride_sw *
					      (uint8_t)(normalized_char - ' ')];
	uint16_t glyph_advance = *row_data++;
	int glyph_width = glyph_advance;
	uint8_t glyph_height = *row_data++;
	if (glyph_width + g_flight_cursor_x >= g_flight_clip_right &&
	    g_flight_word_wrap_enabled != 0) {
		if (g_flight_clear_line_bg_enabled != 0) {
			flight_text_clear_remaining_line_background8bpp();
		}
		g_flight_cursor_x = g_flight_clip_left;
		g_flight_cursor_y += glyph_height + 2;
	}

	int address_each_row_separately = 0;
#ifdef XVT_MODERN
	xvt_cockpit_pages_record_glyph(normalized_char, glyph_advance,
				       glyph_height, 0);
	xvt_cockpit_messages_record_glyph(normalized_char, glyph_advance,
					  glyph_height, 0);
#endif
	uint32_t shadow_bits = 0;
	int line = g_flight_cursor_y;
	if (line < g_flight_clip_top) {
		line = g_flight_clip_top;
	}
	int line_offset;
#ifdef XVT_MODERN
	/* Fully clipped glyphs still advance the cursor, without looking up a row. */
	line_offset = line < g_flight_clip_bottom
			      ? flight_sw_get_line_offset(line)
			      : 0;
#else
	line_offset = flight_sw_get_line_offset(line);
#endif
#ifndef XVT_MODERN
	if (g_flight_resolution_mode != FLIGHT_RESOLUTION_320X240 &&
	    g_flight_sw_framebuffer_base == g_sw_framebuffer_base) {
		page = line_offset / g_vesa_page_size_bytes;
		line_offset %= g_vesa_page_size_bytes;
		rts_vga2_set_current_page((uint8_t)g_vesa_window,
					  (uint16_t)page);
		clipped_bottom = g_flight_cursor_y + glyph_height + 1;
		if (clipped_bottom > g_flight_clip_bottom) {
			clipped_bottom = g_flight_clip_bottom;
		}
		if (line_offset + g_surface_pitch * (clipped_bottom - line) >
		    0xFFFF) {
			address_each_row_separately = 1;
		}
	}
#endif
	if (g_flight_sw_framebuffer_base != g_sw_framebuffer_base) {
		address_each_row_separately = 1;
	}

	int glyph_row_count;
	uint16_t wrap_line_height;
	int draw_x;
	int pixel_count;
	uint32_t glyph_bits;
	uint8_t palette_index;
	uint8_t *row_start;
	if (address_each_row_separately == 0) {
		uint8_t *destination =
			g_flight_sw_framebuffer_base + line_offset;
		wrap_line_height = glyph_height;
		line = g_flight_cursor_y;
		glyph_row_count = wrap_line_height;
		if (g_flight_cursor_y + glyph_row_count > line) {
			do {
				pixel_count = glyph_width;
#ifdef XVT_MODERN
				memcpy(&glyph_bits, row_data,
				       sizeof(glyph_bits));
#else
				glyph_bits = *(const uint32_t *)row_data;
#endif
				if (g_flight_text_shadow_enabled != 0) {
					++pixel_count;
				}
				draw_x = g_flight_cursor_x;
				if (draw_x < g_flight_clip_left) {
					if (g_flight_clip_left - draw_x >=
					    pixel_count) {
						break;
					}
					pixel_count = draw_x + pixel_count -
						      g_flight_clip_left;
					glyph_bits <<=
						g_flight_clip_left - draw_x;
					draw_x = g_flight_clip_left;
				}
				if (g_flight_clip_bottom <= line) {
					break;
				}
				if (g_flight_clip_top <= line) {
					if (draw_x + pixel_count >
					    g_flight_clip_right) {
						pixel_count =
							g_flight_clip_right -
							draw_x;
						if (pixel_count <= 0) {
							break;
						}
					}
					row_start = &destination[draw_x];
					while (pixel_count-- != 0) {
						if ((glyph_bits &
						     0x80000000u) != 0) {
							palette_index =
								g_flight_text_color_index;
						} else if (
							g_flight_text_shadow_enabled !=
								0 &&
							(shadow_bits &
							 0x80000000u) != 0) {
							palette_index =
								g_flight_text_shadow_color;
						} else {
							palette_index =
								g_flight_text_bg_color;
						}
						*row_start++ = palette_index;
						glyph_bits <<= 1;
						shadow_bits <<= 1;
					}
					destination +=
						flight_sw_get_line_pitch();
				}
#ifdef XVT_MODERN
				memcpy(&shadow_bits, row_data,
				       sizeof(shadow_bits));
#else
				shadow_bits = *(const uint32_t *)row_data;
#endif
				row_data += 8;
				++line;
				shadow_bits >>= 1;
			} while (glyph_row_count + g_flight_cursor_y > line);
		}
	} else {
		line = g_flight_cursor_y;
		wrap_line_height = glyph_height;
		glyph_row_count = wrap_line_height;
		if (g_flight_cursor_y + glyph_row_count > line) {
			do {
				pixel_count = glyph_width;
#ifdef XVT_MODERN
				memcpy(&glyph_bits, row_data,
				       sizeof(glyph_bits));
#else
				glyph_bits = *(const uint32_t *)row_data;
#endif
				if (g_flight_text_shadow_enabled != 0) {
					++pixel_count;
				}
				draw_x = g_flight_cursor_x;
				if (draw_x < g_flight_clip_left) {
					if (g_flight_clip_left - draw_x >=
					    pixel_count) {
						break;
					}
					pixel_count = draw_x + pixel_count -
						      g_flight_clip_left;
					glyph_bits <<=
						g_flight_clip_left - draw_x;
					draw_x = g_flight_clip_left;
				}
				if (g_flight_clip_bottom <= line) {
					break;
				}
				if (g_flight_clip_top <= line) {
					if (draw_x + pixel_count >
					    g_flight_clip_right) {
						pixel_count =
							g_flight_clip_right -
							draw_x;
						if (pixel_count <= 0) {
							break;
						}
					}
#ifdef XVT_MODERN
					row_start =
						&g_flight_sw_framebuffer_base
							[flight_sw_get_line_offset(
								 line) +
							 draw_x];
#else
					pixel_offset =
						draw_x +
						flight_sw_get_line_offset(line);
					if (g_flight_resolution_mode !=
						    FLIGHT_RESOLUTION_320X240 &&
					    g_flight_sw_framebuffer_base ==
						    g_sw_framebuffer_base) {
						page = pixel_offset /
						       g_vesa_page_size_bytes;
						pixel_offset %=
							g_vesa_page_size_bytes;
						rts_vga2_set_current_page(
							(uint8_t)g_vesa_window,
							(uint16_t)page);
					}
					row_start =
						&g_flight_sw_framebuffer_base
							[pixel_offset];
#endif
					while (pixel_count-- != 0) {
						if ((glyph_bits &
						     0x80000000u) != 0) {
							palette_index =
								g_flight_text_color_index;
						} else if (
							g_flight_text_shadow_enabled !=
								0 &&
							(shadow_bits &
							 0x80000000u) != 0) {
							palette_index =
								g_flight_text_shadow_color;
						} else {
							palette_index =
								g_flight_text_bg_color;
						}
						*row_start++ = palette_index;
						glyph_bits <<= 1;
						shadow_bits <<= 1;
					}
				}
#ifdef XVT_MODERN
				memcpy(&shadow_bits, row_data,
				       sizeof(shadow_bits));
#else
				shadow_bits = *(const uint32_t *)row_data;
#endif
				row_data += 8;
				++line;
				shadow_bits >>= 1;
			} while (glyph_row_count + g_flight_cursor_y > line);
		}
	}

	g_flight_cursor_x += glyph_advance;
	if (g_flight_clip_right <= g_flight_cursor_x &&
	    g_flight_word_wrap_enabled != 0) {
		if (g_flight_clear_line_bg_enabled != 0) {
			flight_text_clear_remaining_line_background8bpp();
		}
		g_flight_cursor_x = g_flight_clip_left;
		g_flight_cursor_y += wrap_line_height + 2;
	}
}

/* Fills from the text cursor to g_flight_clip_right, g_flight_font_line_height
 * rows down, clipped to the g_flightClip rectangle, with g_flight_text_bg_color
 * on an 8-bit surface. Writes the g_flightFillRect 8bpp edges; does nothing
 * when the cursor is at or past g_flight_clip_right. */
// FUNCTION: XVT 0x410110
void flight_text_clear_remaining_line_background8bpp(void)
{

	if (g_flight_clip_right <= g_flight_cursor_x) {
		return;
	}
	g_flight_fill_rect_right8bpp = g_flight_clip_right;
	g_flight_fill_rect_left8bpp = g_flight_cursor_x;
	g_flight_fill_rect_top8bpp = g_flight_cursor_y;
	uint16_t bottom = g_flight_font_line_height + g_flight_cursor_y;
	if (g_flight_fill_rect_left8bpp < g_flight_clip_left) {
		g_flight_fill_rect_left8bpp = g_flight_clip_left;
	}
	if (g_flight_clip_top > g_flight_fill_rect_top8bpp) {
		g_flight_fill_rect_top8bpp = g_flight_clip_top;
	}
	g_flight_fill_rect_bottom8bpp = bottom;
	if (bottom > g_flight_clip_bottom) {
		bottom = g_flight_clip_bottom;
		g_flight_fill_rect_bottom8bpp = bottom;
	}
	if (g_flight_fill_rect_top8bpp < g_flight_fill_rect_bottom8bpp) {
		flight_sw_fill_rect_or_border8bpp(0);
	}
}

/* Returns g_flight_font_line_height plus 1 when str, drawn from g_flight_cursor_x,
 * would end beyond g_flight_clip_right - 11; else 0, and 0 for NULL. */
// FUNCTION: XVT 0x415C40
int16_t flight_text_get_wrap_height_for_string(const char *str)
{
	if (str == 0) {
		return 0;
	}
	if (g_flight_cursor_x + flight_text_measure_string_width(str) >
	    g_flight_clip_right - 11) {
		return g_flight_font_line_height + 1;
	}

	return 0;
}

/* Draws value right-aligned in digit_count places through g_flight_draw_char_fn,
 * with zeros in front shown as spaces except in the last minDigits places. When
 * value needs more places, the first place shows 9 and the rest are right. The
 * value 0xFFFF draws digit_count zeros in color code '@' without shadow, then
 * puts back g_flight_text_shadow_enabled and g_flight_text_color_index. Does not
 * check digit_count: g_flight_text_decimal_divisors serves 1 to 5 places, holds 0
 * for 6 and 7, and ends there. */
// FUNCTION: XVT 0x4277F0
void flight_text_draw_decimal_number(uint16_t value, unsigned int digit_count,
				     unsigned int min_digits)
{
	unsigned int digit_index;

	if (value == UINT16_MAX) {
		uint16_t saved_shadow = g_flight_text_shadow_enabled;
		uint16_t saved_color = g_flight_text_color_index;
		g_flight_text_shadow_enabled = 0;
		flight_text_set_color('@');
		for (digit_index = digit_count; digit_index != 0;
		     --digit_index) {
			g_flight_draw_char_fn('0');
		}
		g_flight_text_shadow_enabled = saved_shadow;
		g_flight_text_color_index = saved_color;
		return;
	}
	if (digit_count > 5) {
		XVT_LOG_ERROR("hud.digit_count_invalid count=%u value=%u",
			      digit_count, (unsigned)value);
	}

	int16_t started = 0;
	for (digit_index = digit_count; digit_index != 0; --digit_index) {
		uint16_t divisor = g_flight_text_decimal_divisors[digit_index];
		uint16_t digit = value / divisor;
		divisor *= digit;
		value -= divisor;
		if (started != 0 || digit_index <= min_digits || digit != 0) {
			started = 1;
			if (digit > 9) {
				digit = 9;
			}
			digit += '0';
		} else {
			digit = ' ';
		}
		g_flight_draw_char_fn((uint8_t)digit);
	}
}

/* Returns the width in pixels of str in the current font, up to its end or
 * first newline: the sum of each glyph's advance byte. Skips codes below
 * 0x20, and a 0xFE color escape with the byte after it; lowercase letters
 * count as capitals as they draw. Does not check str for NULL. */
// FUNCTION: XVT 0x4278C0
uint16_t flight_text_measure_string_width(const char *str)
{
	uint16_t total_width = 0;
	uint8_t ch = (uint8_t)*str;
	const char *cursor = str + 1;
	while (ch != '\0') {
		if (ch == '\n') {
			break;
		}
		if (ch >= 0x20u) {
			if (ch == 0xfeu) {
				++cursor;
			} else {
				if (g_flight_font_tier != 0 &&
				    g_flight_font_has_lowercase == 0 &&
				    ch >= 'a' && ch <= 'z') {
					ch -= 32;
				}
				total_width += g_flight_font_glyph_table_sw
					[g_flight_font_glyph_stride_sw *
					 (uint8_t)(ch - 32)];
			}
		}
		ch = (uint8_t)*cursor++;
	}

	return total_width;
}

/* The 16-bit surface version of flight_text_draw_narrow_glyph8bpp: the same
 * glyph rows, colors, shadow, clipping, cursor moves and wrap, with each
 * palette index turned into a pixel through g_flight_palette16_bpp. Nothing
 * calls this: flight_render_install_callbacks never puts it in
 * g_flight_draw_char_fn. */
// FUNCTION: XVT 0x449F70
void flight_text_draw_narrow_glyph(uint8_t ch)
{
#ifndef XVT_MODERN
	unsigned int page;
#endif

	if (ch == '\n') {
		if (g_flight_clear_line_bg_enabled != 0) {
			flight_text_clear_remaining_line_background();
		}
		g_flight_cursor_x = g_flight_clip_left;
		g_flight_cursor_y += g_flight_font_line_height;
		return;
	}
	if (ch < ' ') {
		return;
	}

	uint8_t normalized_char = ch;
	if (g_flight_font_tier != 0 && g_flight_font_has_lowercase == 0 &&
	    ch >= 'a' && ch <= 'z') {
		normalized_char = ch - ('a' - 'A');
	}
	const uint8_t *glyph_data =
		&g_flight_font_glyph_table_sw[g_flight_font_glyph_stride_sw *
					      (uint8_t)(normalized_char - ' ')];
	int16_t glyph_advance = *glyph_data++;
	uint8_t glyph_height = *glyph_data++;
	const uint8_t *row_data = glyph_data;
	int glyph_width = glyph_advance;
	if (glyph_width + g_flight_cursor_x >= g_flight_clip_right &&
	    g_flight_word_wrap_enabled != 0) {
		if (g_flight_clear_line_bg_enabled != 0) {
			flight_text_clear_remaining_line_background();
		}
		g_flight_cursor_x = g_flight_clip_left;
		g_flight_cursor_y += glyph_height + 2;
	}

#ifdef XVT_MODERN
	xvt_cockpit_pages_record_glyph(normalized_char, glyph_advance,
				       glyph_height, 1);
	xvt_cockpit_messages_record_glyph(normalized_char, glyph_advance,
					  glyph_height, 1);
#endif
	uint8_t shadow_bits = 0;
	int line = g_flight_cursor_y;
	int16_t wrap_line_height = glyph_height;
	int glyph_row_count = glyph_height;
	unsigned int pixel_offset;
	unsigned int palette_index;
	uint16_t *destination;
	if (glyph_row_count + g_flight_cursor_y > line) {
		do {
			int pixel_count = glyph_width;
			uint8_t glyph_bits = *row_data;
			if (g_flight_text_shadow_enabled != 0) {
				++pixel_count;
			}
			int draw_x = g_flight_cursor_x;
			if (g_flight_cursor_x < g_flight_clip_left) {
				if (g_flight_clip_left - g_flight_cursor_x >=
				    pixel_count) {
					break;
				}
				pixel_count = g_flight_cursor_x + pixel_count -
					      g_flight_clip_left;
				draw_x = g_flight_clip_left;
				glyph_bits <<=
					g_flight_clip_left - g_flight_cursor_x;
			}
			if (g_flight_clip_bottom <= line) {
				break;
			}
			if (g_flight_clip_top > line) {
				pixel_count = 0;
			}
			if (pixel_count + draw_x > g_flight_clip_right) {
				pixel_count = g_flight_clip_right - draw_x;
				if (pixel_count <= 0) {
					break;
				}
			}

#ifndef XVT_MODERN
			pixel_offset =
				flight_sw_get_line_offset(line) + 2 * draw_x;
			if (g_flight_resolution_mode !=
				    FLIGHT_RESOLUTION_320X240 &&
			    g_flight_sw_framebuffer_base ==
				    g_sw_framebuffer_base) {
				page = pixel_offset / g_vesa_page_size_bytes;
				pixel_offset %= g_vesa_page_size_bytes;
				rts_vga2_set_current_page(
					(uint8_t)g_vesa_window, (uint16_t)page);
			}
			destination =
				&((uint16_t *)g_flight_sw_framebuffer_base)
					[pixel_offset / 2];
#endif
			int pixels_remaining = pixel_count;
			if (pixels_remaining != 0) {
#ifdef XVT_MODERN
				/* The original looks up negative rows even when
				 * top clipping leaves no pixels. Keep
				 * row/shadow progression outside this drawing
				 * block. */
				pixel_offset = flight_sw_get_line_offset(line) +
					       2 * draw_x;
				destination = &((
					uint16_t *)g_flight_sw_framebuffer_base)
						      [pixel_offset / 2];
#endif
				--pixels_remaining;
				do {
					if ((glyph_bits & 0x80u) != 0) {
						palette_index =
							g_flight_text_color_index;
					} else if (
						g_flight_text_shadow_enabled !=
							0 &&
						(shadow_bits & 0x80u) != 0) {
						palette_index =
							g_flight_text_shadow_color;
					} else {
						palette_index =
							g_flight_text_bg_color;
					}
					*destination++ = g_flight_palette16_bpp
						[palette_index];
					shadow_bits <<= 1;
					glyph_bits <<= 1;
				} while (pixels_remaining-- != 0);
			}
			shadow_bits = *row_data >> 1;
			row_data += 2;
			++line;
		} while (glyph_row_count + g_flight_cursor_y > line);
	}

	g_flight_cursor_x += glyph_advance;
	if (g_flight_clip_right <= g_flight_cursor_x &&
	    g_flight_word_wrap_enabled != 0) {
		if (g_flight_clear_line_bg_enabled != 0) {
			flight_text_clear_remaining_line_background();
		}
		g_flight_cursor_x = g_flight_clip_left;
		g_flight_cursor_y += wrap_line_height + 2;
	}
}

/* The 16-bit surface version of flight_text_draw_wide_glyph8bpp, with each
 * palette index turned into a pixel through g_flight_palette16_bpp.
 * flight_render_install_callbacks installs it as g_flight_draw_char_fn for pixel
 * mode 2. In the modern build it also records the glyph for the modern
 * renderer. */
// FUNCTION: XVT 0x44A270
void flight_text_draw_wide_glyph(uint8_t ch)
{
#ifdef XVT_MODERN
	const uint8_t *row_data;
#else
	const uint32_t *row_data;
#endif
#ifndef XVT_MODERN
	unsigned int page;
#endif

	if (ch == '\n') {
		if (g_flight_clear_line_bg_enabled != 0) {
			flight_text_clear_remaining_line_background();
		}
		g_flight_cursor_x = g_flight_clip_left;
		g_flight_cursor_y += g_flight_font_line_height + 1;
		return;
	}
	if (ch < ' ') {
		return;
	}

	uint8_t normalized_char = ch;
	if (g_flight_font_tier != 0 && g_flight_font_has_lowercase == 0 &&
	    normalized_char >= 'a' && normalized_char <= 'z') {
		normalized_char -= 'a' - 'A';
	}
	const uint8_t *glyph_data =
		&g_flight_font_glyph_table_sw[g_flight_font_glyph_stride_sw *
					      (uint8_t)(normalized_char - ' ')];
	int16_t glyph_advance = *glyph_data++;
	uint8_t glyph_height = *glyph_data++;
#ifdef XVT_MODERN
	row_data = glyph_data;
#else
	row_data = (const uint32_t *)glyph_data;
#endif
	int glyph_width = glyph_advance;
	if (glyph_width + g_flight_cursor_x >= g_flight_clip_right &&
	    g_flight_word_wrap_enabled != 0) {
		if (g_flight_clear_line_bg_enabled != 0) {
			flight_text_clear_remaining_line_background();
		}
		g_flight_cursor_x = g_flight_clip_left;
		g_flight_cursor_y += glyph_height + 2;
	}

#ifdef XVT_MODERN
	xvt_cockpit_pages_record_glyph(normalized_char, glyph_advance,
				       glyph_height, 0);
	xvt_cockpit_messages_record_glyph(normalized_char, glyph_advance,
					  glyph_height, 0);
#endif
	uint32_t shadow_bits = 0;
	int16_t wrap_line_height = glyph_height;
	int line = g_flight_cursor_y;
	int glyph_row_count = glyph_height;
	uint32_t glyph_bits;
	unsigned int pixel_offset;
	unsigned int palette_index;
	uint16_t *destination;
	if (g_flight_cursor_y + glyph_row_count > g_flight_cursor_y) {
		do {
			int pixel_count = glyph_width;
#ifdef XVT_MODERN
			memcpy(&glyph_bits, row_data, sizeof(glyph_bits));
#else
			glyph_bits = *row_data;
#endif
			if (g_flight_text_shadow_enabled != 0) {
				++pixel_count;
			}
			int draw_x = g_flight_cursor_x;
			if (g_flight_cursor_x < g_flight_clip_left) {
				if (g_flight_clip_left - g_flight_cursor_x >=
				    pixel_count) {
					break;
				}
				pixel_count = g_flight_cursor_x + pixel_count -
					      g_flight_clip_left;
				draw_x = g_flight_clip_left;
				glyph_bits <<=
					g_flight_clip_left - g_flight_cursor_x;
			}
			if (g_flight_clip_bottom <= line) {
				break;
			}
			if (g_flight_clip_top > line) {
				pixel_count = 0;
			}
			if (pixel_count + draw_x > g_flight_clip_right) {
				pixel_count = g_flight_clip_right - draw_x;
				if (pixel_count <= 0) {
					break;
				}
			}

#ifndef XVT_MODERN
			pixel_offset =
				flight_sw_get_line_offset(line) + 2 * draw_x;
			if (g_flight_resolution_mode !=
				    FLIGHT_RESOLUTION_320X240 &&
			    g_flight_sw_framebuffer_base ==
				    g_sw_framebuffer_base) {
				page = pixel_offset / g_vesa_page_size_bytes;
				pixel_offset %= g_vesa_page_size_bytes;
				rts_vga2_set_current_page(
					(uint8_t)g_vesa_window, (uint16_t)page);
			}
			destination =
				&((uint16_t *)g_flight_sw_framebuffer_base)
					[pixel_offset / 2];
#endif
			int pixels_remaining = pixel_count;
			if (pixels_remaining != 0) {
#ifdef XVT_MODERN
				/* The original looks up negative rows even when
				 * top clipping leaves no pixels. Keep
				 * row/shadow progression outside this drawing
				 * block. */
				pixel_offset = flight_sw_get_line_offset(line) +
					       2 * draw_x;
				destination = &((
					uint16_t *)g_flight_sw_framebuffer_base)
						      [pixel_offset / 2];
#endif
				--pixels_remaining;
				do {
					if ((glyph_bits & 0x80000000u) != 0) {
						palette_index =
							g_flight_text_color_index;
					} else if (
						g_flight_text_shadow_enabled !=
							0 &&
						(shadow_bits & 0x80000000u) !=
							0) {
						palette_index =
							g_flight_text_shadow_color;
					} else {
						palette_index =
							g_flight_text_bg_color;
					}
					*destination++ = g_flight_palette16_bpp
						[palette_index];
					glyph_bits <<= 1;
					shadow_bits <<= 1;
				} while (pixels_remaining-- != 0);
			}
#ifdef XVT_MODERN
			memcpy(&shadow_bits, row_data, sizeof(shadow_bits));
			row_data += 8;
#else
			shadow_bits = *row_data;
			row_data += 2;
#endif
			++line;
			shadow_bits >>= 1;
		} while (glyph_row_count + g_flight_cursor_y > line);
	}

	g_flight_cursor_x += glyph_advance;
	if (g_flight_clip_right <= g_flight_cursor_x &&
	    g_flight_word_wrap_enabled != 0) {
		if (g_flight_clear_line_bg_enabled != 0) {
			flight_text_clear_remaining_line_background();
		}
		g_flight_cursor_x = g_flight_clip_left;
		g_flight_cursor_y += wrap_line_height + 2;
	}
}

/* The 16-bit surface version of
 * flight_text_clear_remaining_line_background8bpp: fills the rest of the line
 * with g_flight_text_bg_color and writes the g_flightFillRect 16bpp edges. */
// FUNCTION: XVT 0x44AA50
void flight_text_clear_remaining_line_background(void)
{

	int16_t cursor_x = g_flight_cursor_x;
	if (g_flight_clip_right <= cursor_x) {
		return;
	}

	int16_t cursor_y = g_flight_cursor_y;
	uint16_t clipped_top = cursor_y;
	g_flight_fill_rect_right16bpp = g_flight_clip_right;
	g_flight_fill_rect_left16bpp = cursor_x;
	uint16_t clipped_bottom = g_flight_font_line_height + cursor_y;
	if (g_flight_clip_left > (int)(uint16_t)cursor_x) {
		g_flight_fill_rect_left16bpp = g_flight_clip_left;
	}

	g_flight_fill_rect_top16bpp = cursor_y;
	if (g_flight_clip_top > (int)g_flight_fill_rect_top16bpp) {
		clipped_top = g_flight_clip_top;
	}

	g_flight_fill_rect_bottom16bpp = g_flight_font_line_height + cursor_y;
	if (g_flight_clip_bottom < (int)g_flight_fill_rect_bottom16bpp) {
		clipped_bottom = g_flight_clip_bottom;
	}
	g_flight_fill_rect_bottom16bpp = clipped_bottom;
	g_flight_fill_rect_top16bpp = clipped_top;
	if (clipped_bottom > clipped_top) {
		flight_sw_fill_rect_or_border16bpp(0);
	}
}

/* Moves the text cursor to x, y; no checks. */
// FUNCTION: XVT 0x4A9500
void flight_text_set_cursor(int x, int y)
{
	g_flight_cursor_y = y;
	g_flight_cursor_x = x;
}

/* Sets the g_flightClip rectangle; right and bottom are exclusive. Raises a
 * negative left or top to 0 and lowers right and bottom to g_screen_width and
 * g_screen_height; a negative right or bottom, compared unsigned, also becomes
 * the screen's width or height. Does not check that left is below right or
 * top below bottom. */
// FUNCTION: XVT 0x4A9520
void flight_text_set_clip_rect(int16_t left, int16_t top, int16_t right,
			       int16_t bottom)
{
	if (top < 0) {
		top = 0;
	}
	if (left < 0) {
		left = 0;
	}
	if ((unsigned int)right > g_screen_width) {
		right = (int16_t)g_screen_width;
	}
	if ((unsigned int)bottom > g_screen_height) {
		bottom = (int16_t)g_screen_height;
	}

	g_flight_clip_top = top;
	g_flight_clip_bottom = bottom;
	g_flight_clip_left = left;
	g_flight_clip_right = right;
}

/* Sets g_flight_text_color_index: a code from 0x40 up, other than
 * g_flight_transparent_color_index, is looked up in g_flight_char_to_color_lut;
 * anything else is the palette index itself. Does not check a code above
 * 0x5F against the table's 32 entries. */
// FUNCTION: XVT 0x4A9590
void flight_text_set_color(unsigned int char_or_index)
{
	if (char_or_index >= 0x40u &&
	    char_or_index != g_flight_transparent_color_index) {
		g_flight_text_color_index =
			g_flight_char_to_color_lut[char_or_index - 0x40u];
	} else {
		g_flight_text_color_index = (uint8_t)char_or_index;
	}
}

/* Sets g_flight_text_bg_color from a color code or palette index, as
 * flight_text_set_color reads them, with the same missing check. */
// FUNCTION: XVT 0x4A95C0
void flight_text_set_background_color(unsigned int char_or_index)
{
	if (char_or_index >= 0x40u &&
	    char_or_index != g_flight_transparent_color_index) {
		g_flight_text_bg_color =
			g_flight_char_to_color_lut[char_or_index - 0x40u];
	} else {
		g_flight_text_bg_color = (uint8_t)char_or_index;
	}
}

/* Sets g_flight_text_shadow_color from a color code or palette index, as
 * flight_text_set_color reads them, with the same missing check. */
// FUNCTION: XVT 0x4A95F0
void flight_text_set_shadow_color(unsigned int char_or_index)
{
	if (char_or_index >= 0x40u &&
	    char_or_index != g_flight_transparent_color_index) {
		g_flight_text_shadow_color =
			g_flight_char_to_color_lut[char_or_index - 0x40u];
	} else {
		g_flight_text_shadow_color = (uint8_t)char_or_index;
	}
}

/* Sets g_flight_word_wrap_enabled. */
// FUNCTION: XVT 0x4A9620
void flight_text_set_word_wrap(int16_t enabled)
{
	g_flight_word_wrap_enabled = enabled;
}

/* Sets g_flight_clear_line_bg_enabled. */
// FUNCTION: XVT 0x4A9630
void flight_text_set_clear_line_background(int16_t enabled)
{
	g_flight_clear_line_bg_enabled = enabled;
}

/* Selects the flight text font: stores tier in g_flight_font_tier and, for
 * tiers 0 to 2, sets g_flight_font_glyph_table_sw, g_flight_font_glyph_stride_sw,
 * g_flight_font_line_height, g_flight_font_digit_width and, but for one case,
 * g_flight_font_has_lowercase by g_flight_resolution_mode. At 320x240 every tier
 * gets the micro font. Tier 0 gets the micro font at 480x360 and the small
 * one at 640x480; tiers 1 and 2 are alike: the small font at 480x360 and the
 * medium one at 640x480. The small font at 480x360 leaves
 * g_flight_font_has_lowercase as it was. Any other tier or resolution mode
 * changes only g_flight_font_tier. */
// FUNCTION: XVT 0x4A9640
void flight_text_set_font_tier(uint8_t tier)
{
	g_flight_font_tier = tier;
	switch (tier) {
	case 0:
		switch (g_flight_resolution_mode) {
		case FLIGHT_RESOLUTION_320X240:
		case FLIGHT_RESOLUTION_480X360:
			g_flight_font_line_height = 5;
			g_flight_font_has_lowercase = 0;
			g_flight_font_digit_width = 3;
			g_flight_font_glyph_stride_sw = 42;
			g_flight_font_glyph_table_sw = g_flight_font_micro_sw;
			break;
		case FLIGHT_RESOLUTION_640X480:
			g_flight_font_line_height = 8;
			g_flight_font_has_lowercase = 1;
			g_flight_font_digit_width = 4;
			g_flight_font_glyph_stride_sw = 66;
			g_flight_font_glyph_table_sw = g_flight_font_small_sw;
			break;
		}
		break;
	case 1:
		switch (g_flight_resolution_mode) {
		case FLIGHT_RESOLUTION_320X240:
			g_flight_font_line_height = 5;
			g_flight_font_has_lowercase = 0;
			g_flight_font_digit_width = 3;
			g_flight_font_glyph_stride_sw = 42;
			g_flight_font_glyph_table_sw = g_flight_font_micro_sw;
			break;
		case FLIGHT_RESOLUTION_640X480:
			g_flight_font_line_height = 10;
			g_flight_font_has_lowercase = 1;
			g_flight_font_digit_width = 5;
			g_flight_font_glyph_stride_sw = 82;
			g_flight_font_glyph_table_sw = g_flight_font_medium_sw;
			break;
		case FLIGHT_RESOLUTION_480X360:
			g_flight_font_line_height = 8;
			g_flight_font_digit_width = 4;
			g_flight_font_glyph_stride_sw = 66;
			g_flight_font_glyph_table_sw = g_flight_font_small_sw;
			break;
		}
		break;
	case 2:
		switch (g_flight_resolution_mode) {
		case FLIGHT_RESOLUTION_320X240:
			g_flight_font_line_height = 5;
			g_flight_font_has_lowercase = 0;
			g_flight_font_digit_width = 3;
			g_flight_font_glyph_stride_sw = 42;
			g_flight_font_glyph_table_sw = g_flight_font_micro_sw;
			break;
		case FLIGHT_RESOLUTION_640X480:
			g_flight_font_line_height = 10;
			g_flight_font_has_lowercase = 1;
			g_flight_font_digit_width = 5;
			g_flight_font_glyph_stride_sw = 82;
			g_flight_font_glyph_table_sw = g_flight_font_medium_sw;
			break;
		case FLIGHT_RESOLUTION_480X360:
			g_flight_font_line_height = 8;
			g_flight_font_digit_width = 4;
			g_flight_font_glyph_stride_sw = 66;
			g_flight_font_glyph_table_sw = g_flight_font_small_sw;
			break;
		}
		break;
	}
}

/* Copies text into g_flight_text_scratch_buffer, or empties it for NULL. Does
 * not check the buffer's 256-byte size. */
// FUNCTION: XVT 0x4A97F0
void flight_text_set_scratch(const char *text)
{
	char *destination = g_flight_text_scratch_buffer;
	if (text != 0) {
		while (*text != '\0') {
			*destination++ = *text++;
		}
	}
	*destination = '\0';
}

/* Appends text, or nothing for NULL, to g_flight_text_scratch_buffer. Does not
 * check the buffer's 256-byte size. */
// FUNCTION: XVT 0x4A9820
void flight_text_append_scratch_string(const char *text)
{
	char *destination = g_flight_text_scratch_buffer;
	while (*destination != '\0') {
		destination++;
	}
	if (text != 0) {
		while (*text != '\0') {
			*destination++ = *text++;
		}
	}
	*destination = '\0';
}

/* Appends one character to g_flight_text_scratch_buffer. Does not check the
 * buffer's 256-byte size. */
// FUNCTION: XVT 0x4A9860
void flight_text_append_scratch_char(uint8_t ch)
{
	char *destination = g_flight_text_scratch_buffer;
	while (*destination != '\0') {
		destination++;
	}
	destination[0] = ch;
	destination[1] = '\0';
}

/* Writes value in decimal, with a '-' in front when negative, over
 * g_flight_text_scratch_buffer and returns the characters written, sign
 * included. Does not handle INT_MIN, whose negation overflows. */
// FUNCTION: XVT 0x4A9890
uint16_t flight_text_format_scratch_int(int value)
{
	int16_t is_negative = 0;
	char *output = g_flight_text_scratch_buffer;
	int magnitude = value;
	if (magnitude < 0) {
		magnitude = -magnitude;
		g_flight_text_scratch_buffer[0] = '-';
		output = &g_flight_text_scratch_buffer[1];
		is_negative = 1;
	}
	uint16_t digit_count;
	if (magnitude != 0) {
		digit_count = 0;
		int remaining_value = magnitude;
		if (magnitude > 0) {
			do {
				++digit_count;
				remaining_value /= 10;
			} while (remaining_value > 0);
		}
		int digit_index = 0;
		if (digit_count != 0) {
			int expanded_digit_count = digit_count;
			do {
				int16_t digit = (int16_t)(magnitude % 10);
				magnitude /= 10;
				char *digit_position = &output[-digit_index++];
				digit_position[expanded_digit_count - 1] =
					(char)(digit + '0');
			} while (digit_index < expanded_digit_count);
		}
	} else {
		digit_count = 1;
		*output = '0';
	}
	output[digit_count] = '\0';
	if (is_negative != 0) {
		++digit_count;
	}
	return digit_count;
}

/* Draws str at the text cursor through g_flight_draw_char_fn. A 0xFE byte sets
 * the text color from the byte after it; a byte below 0x10, newline
 * included, sets the color to that value; both go through
 * flight_text_set_color. With word wrap on, a space draws as a newline when
 * it and the word after it would end beyond g_flight_clip_right - 2. Returns
 * at once for an empty string; does not check str for NULL. The modern build
 * reads at most 79 characters of that word; the original does not bound it,
 * and a word of 80 or more overruns the 80-byte buffer it is copied into. */
// FUNCTION: XVT 0x4A9960
void flight_text_draw_string(const char *str)
{
	if (*str == '\0') {
		return;
	}

	char word_buffer[80];
	do {
		if ((uint8_t)*str == 0xfeu) {
			++str;
			flight_text_set_color((uint8_t)*str);
		} else if ((uint8_t)*str < 0x10u) {
			flight_text_set_color((uint8_t)*str);
		} else if ((uint8_t)*str == ' ' &&
			   g_flight_word_wrap_enabled != 0) {
			const char *word_scan = str + 1;
			uint16_t word_length = 0;
			while (*word_scan != ' ' && *word_scan != '\0'
#ifdef XVT_MODERN
			       && word_length <
					  (uint16_t)(sizeof(word_buffer) - 1u)
#endif
			) {
				word_buffer[word_length] = *word_scan;
				++word_length;
				++word_scan;
			}
			word_buffer[word_length] = '\0';

			int next_word_width =
				flight_text_measure_string_width(word_buffer);
			if (g_flight_cursor_x +
				    flight_text_measure_string_width(" ") +
				    next_word_width >
			    g_flight_clip_right - 2) {
				g_flight_draw_char_fn('\n');
			} else {
				g_flight_draw_char_fn((uint8_t)*str);
			}
		} else {
			g_flight_draw_char_fn((uint8_t)*str);
		}
	} while (*++str != '\0');
}

/* Draws str through flight_text_draw_string on the cursor's row, centered
 * between g_flight_clip_left and g_flight_clip_right. A start left of
 * g_flight_clip_left moves to it, but a start below 0 is kept. */
// FUNCTION: XVT 0x4A9A50
void flight_text_draw_string_centered(const char *str)
{
	uint16_t half_width = flight_text_measure_string_width(str) >> 1;
	uint16_t candidate_x = (uint16_t)(((int)g_flight_clip_right +
					   (int)g_flight_clip_left) /
						  2 -
					  half_width);
	if (candidate_x < (int)g_flight_clip_left) {
		candidate_x = g_flight_clip_left;
	}
	int cursor_y = g_flight_cursor_y;
	flight_text_set_cursor((int16_t)candidate_x, (int16_t)cursor_y);
	flight_text_draw_string(str);
}

/* Draws str through flight_text_draw_string on the cursor's row, ending 2
 * pixels short of g_flight_clip_right. A start left of g_flight_clip_left, or
 * below 0, moves to g_flight_clip_left. */
// FUNCTION: XVT 0x4A9AC0
void flight_text_draw_string_right_aligned(const char *str)
{
	uint16_t width = flight_text_measure_string_width(str) + 2;
	uint16_t cursor_x = g_flight_clip_right;
	cursor_x -= width;
	if (cursor_x >= 0x8000u) {
		cursor_x = 0;
	}
	if (g_flight_clip_left > (int)cursor_x) {
		cursor_x = g_flight_clip_left;
	}
	int cursor_y = g_flight_cursor_y;
	flight_text_set_cursor(cursor_x, cursor_y);
	flight_text_draw_string(str);
}
