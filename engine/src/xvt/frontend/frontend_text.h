#ifndef XVT_FRONTEND_FRONTEND_TEXT_H
#define XVT_FRONTEND_FRONTEND_TEXT_H

#include <stdint.h>

#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct bitmap_font {
	/* Each glyph's rows, encoded by front_image_encode_glyph_row, end to end;
	 * a font built through GDI starts at glyph 1. */
	/* Runtime pointer to the compressed glyph-byte blob; serialized in the
	 * .ABP header as the blob byte count. */
	uint8_t *p_glyph_bits;
	unsigned int glyph_bit_offset
		[256]; ///< Per-character byte offsets into p_glyph_bits.
	/* Each character's height in pixels. Entry 0 is the font's height that
	 * frontend_text_get_font_height returns; a font built through GDI copies
	 * it from entry 1. */
	uint8_t glyph_height[256];
	/* Each character's width in pixels, its advance before char_spacing; 0
	 * for character 0 in a font built through GDI. */
	uint8_t glyph_width[256];
	/* The size the font was built at, its index in
	 * g_front_state.font_by_size. */
	unsigned int point_size;
	/* 1 while the slot holds a font; the free functions compare it with 1,
	 * and a loaded file sets it from its own header. */
	/* Font-slot occupancy flag; scanned when allocating and cleared when
	 * freeing. */
	uint8_t in_use;
	/* Pixels added after each glyph's width: 0 in a font built through GDI,
	 * else what the file holds. */
	uint8_t char_spacing;
	/* frontend_text_load_font sets it to 1 and a loaded file sets it from its
	 * header. No code reads the field itself;
	 * frontend_text_save_font_atlas_file copies it with the header. */
	/* Set to 1 for generated fonts and persisted in the 1547-byte .ABP
	 * header; no runtime consumer is identified. */
	uint8_t field_60a;
};

typedef uint16_t text_fade_color_cache[65536];

extern int g_active_text_field_id;

int frontend_text_handle_editable_field(const struct RECT *rect, char *text,
					int max_chars, int field_id,
					unsigned int font_size,
					const char *ignored_chars);
int frontend_text_load_font(int point_size);
void frontend_text_free_all_fonts(void);
int frontend_text_draw(int font_size, const char *str, int x, int y, int color);
int frontend_text_draw_centered(int font_size, const char *str,
				const struct RECT *rect, int color);
int frontend_text_draw_aligned_in_rect(int font_size, const char *str,
				       const struct RECT *rect, int center_h,
				       int center_v, int color);
int frontend_text_draw_wrapped(int font_size, const char *str,
			       const struct RECT *rect, int color,
			       int line_spacing, int first_visible_line);
int frontend_text_get_font_height(int font_size);
int frontend_text_measure_width(const char *str, int font_size);
int frontend_text_load_font_atlas_file(const char *file_name, int slot_index);
int frontend_text_start_text_fade_in(int frames);
int frontend_text_stop_text_fade(void);
int frontend_text_suspend_text_fade(void);
int frontend_text_resume_text_fade(void);
void frontend_text_draw_formatted_wrapped_text(const struct RECT *rect,
					       const uint8_t *text,
					       int suppress_centered_headings);

#ifdef __cplusplus
}
#endif

#endif
