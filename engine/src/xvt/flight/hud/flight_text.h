#ifndef XVT_FLIGHT_HUD_FLIGHT_TEXT_H
#define XVT_FLIGHT_HUD_FLIGHT_TEXT_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int16_t g_flight_word_wrap_enabled;
extern int16_t g_flight_clear_line_bg_enabled;
extern int16_t g_flight_text_unused_state;
extern int16_t g_flight_cursor_y;
extern int16_t g_flight_cursor_x;
extern uint8_t g_flight_text_color_index;
extern uint8_t g_flight_text_bg_color;
extern uint8_t g_flight_text_shadow_color;
extern uint8_t g_flight_text_shadow_enabled;
extern uint8_t g_flight_font_tier;
extern uint8_t g_flight_font_has_lowercase;
extern uint8_t g_flight_font_line_height;
extern uint8_t g_flight_font_digit_width;
extern uint8_t *g_flight_font_glyph_table_sw;
extern uint16_t g_flight_font_glyph_stride_sw;
extern uint8_t *g_flight_font_small_sw;
extern uint8_t *g_flight_font_medium_sw;
extern uint8_t *g_flight_font_micro_sw;
extern const uint16_t g_flight_text_decimal_divisors[8];
extern const uint8_t g_flight_char_to_color_lut[32];
extern int16_t g_flight_clip_top;
extern int16_t g_flight_clip_bottom;
extern int16_t g_flight_clip_left;
extern int16_t g_flight_clip_right;
extern char g_flight_text_scratch_buffer[256];

void flight_text_draw_narrow_glyph8bpp(uint8_t ch);
void flight_text_draw_wide_glyph8bpp(uint8_t ch);
void flight_text_clear_remaining_line_background8bpp(void);
int16_t flight_text_get_wrap_height_for_string(const char *str);
void flight_text_draw_decimal_number(uint16_t value, unsigned int digit_count,
				     unsigned int min_digits);
uint16_t flight_text_measure_string_width(const char *str);
void flight_text_draw_narrow_glyph(uint8_t ch);
void flight_text_draw_wide_glyph(uint8_t ch);
void flight_text_clear_remaining_line_background(void);
void flight_text_set_cursor(int x, int y);
void flight_text_set_clip_rect(int16_t left, int16_t top, int16_t right,
			       int16_t bottom);
void flight_text_set_color(unsigned int char_or_index);
void flight_text_set_background_color(unsigned int char_or_index);
void flight_text_set_shadow_color(unsigned int char_or_index);
void flight_text_set_word_wrap(int16_t enabled);
void flight_text_set_clear_line_background(int16_t enabled);
void flight_text_set_font_tier(uint8_t tier);
void flight_text_set_scratch(const char *text);
void flight_text_append_scratch_string(const char *text);
void flight_text_append_scratch_char(uint8_t ch);
uint16_t flight_text_format_scratch_int(int value);
void flight_text_draw_string(const char *str);
void flight_text_draw_string_centered(const char *str);
void flight_text_draw_string_right_aligned(const char *str);

#ifdef __cplusplus
}
#endif

#endif
