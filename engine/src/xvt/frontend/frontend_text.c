#include "xvt/frontend/frontend_text.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/input/keyboard.h"
#include "xvt_runtime/log/log_both_builds.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt_runtime/snapshot/render_frontend.h"

/* g_front_state.text_fade_frames_left as frontend_text_suspend_text_fade saved it, for
 * frontend_text_resume_text_fade to put back. Only the suspend writes it. */
// GLOBAL: XVT 0x665684
static int g_saved_text_fade_frames_left;
/* The field_id of the edit field that takes typed keys and shows a caret.
 * frontend_text_handle_editable_field sets it when a field is clicked; screens
 * also set it directly, chiefly in config.c and frontend.c. 0 at start, so a
 * field with id 0 is active until another is clicked. The modern build's
 * renderer reads it. */
// GLOBAL: XVT 0x52C000
int g_active_text_field_id = 0;
/* The edit fields' text and caret color, frontend_display_pack_rgb(0xFF, 0xFF,
 * 0xFF), computed on the first frontend_text_handle_editable_field call. */
// GLOBAL: XVT 0x52C014
static int g_text_field_color = 0;
/* 1 once frontend_text_handle_editable_field has computed g_text_field_color;
 * nothing sets it back to 0. */
// GLOBAL: XVT 0x52C018
static int g_text_field_color_initialized = 0;
/* The character index frontend_text_handle_editable_field draws the caret at. Only
 * that function writes it: it sets it to the text's length on each call and
 * moves it with each character typed or erased, so it always equals
 * g_text_field_length. */
// GLOBAL: XVT 0x665464
static int g_text_field_cursor_char_index = 0;
/* The length of the text frontend_text_handle_editable_field is editing, set from
 * strlen on each call and kept in step as characters are typed or erased. Only
 * that function writes it. */
// GLOBAL: XVT 0x665680
static int g_text_field_length = 0;

/* Draws and runs a one-line edit field over text for one frame. A release of
 * either mouse button in rect makes field_id the active field,
 * g_active_text_field_id. While it is active the field takes the next character in
 * the keyboard buffer, discarding it when it is one of ignored_chars: a
 * printable character or a space is appended while the text is shorter than
 * max_chars - 1; Backspace erases the last character; Enter or Tab makes the
 * call return 1; Esc is left in the buffer; other characters are discarded.
 * Draws the text in g_text_field_color in the font of size fontSize from
 * (rect->left + 2, rect->top + 2), clipped to that point and rect's
 * bottom-right corner; when the text's width is over (rect->right - rect->left)
 * + 1 it is shifted left by its width + fontSize - (rect->right - rect->left) -
 * 1, keeping its end in view. While active it also draws a caret 2 pixels wide
 * and fontSize + 1 tall after the last character, on frames whose counter
 * modulo 10 is under 5. Returns 0 when Enter or Tab was not taken. The modern
 * build treats every byte from 32 but 127 as printable and records the field
 * for its renderer. */
// FUNCTION: XVT 0x4DA380
int frontend_text_handle_editable_field(const struct RECT *rect, char *text,
					int max_chars, int field_id,
					unsigned int font_size,
					const char *ignored_chars)
{
	if (!g_text_field_color_initialized) {
		g_text_field_color =
			frontend_display_pack_rgb(0xFF, 0xFF, 0xFF);
		g_text_field_color_initialized = 1;
	}
	g_text_field_cursor_char_index = strlen(text);
	g_text_field_length = strlen(text);
	int completed = 0;
	int mouse_x;
	int mouse_y;
	frontend_cursor_get_pos(&mouse_x, &mouse_y);
	if (frontend_draw_point_in_rect(rect, mouse_x, mouse_y) &&
	    (frontend_mouse_get_left_click() ||
	     frontend_mouse_get_right_click())) {
		XVT_LOG_DEBUG("text.field_selected field=%d previous=%d",
			      field_id, g_active_text_field_id);
		g_active_text_field_id = field_id;
	}

	xvt_render_frontend_text_entry(field_id);
	if (g_active_text_field_id == field_id) {
		uint8_t input_char = keyboard_peek_char();
		if (ignored_chars != NULL) {
			for (int ignored_index = 0;
			     ignored_chars[ignored_index] != '\0';
			     ++ignored_index) {
				if ((uint8_t)ignored_chars[ignored_index] ==
				    input_char) {
					input_char = 0;
					keyboard_discard_char();
					break;
				}
			}
		}
		if (input_char != 0) {
			int character_code = input_char;
			if (character_code != 27) {
				keyboard_discard_char();
			}
			if ((input_char >= 32 && input_char != 127) ||
			    input_char == 32) {
				if (max_chars - 1 > g_text_field_length) {
					text[g_text_field_length++] =
						(char)input_char;
					++g_text_field_cursor_char_index;
					text[g_text_field_length] = '\0';
					XVT_LOG_DEBUG(
						"text.field_edited field=%d edit=\"typed\" length=%d max=%d",
						field_id, g_text_field_length,
						max_chars);
				} else {
					XVT_LOG_DEBUG(
						"text.field_edited field=%d edit=\"full\" length=%d max=%d",
						field_id, g_text_field_length,
						max_chars);
				}
			} else if (input_char == 13 || input_char == 9) {
				completed = 1;
			} else if (input_char == 8 &&
				   g_text_field_length != 0) {
				--g_text_field_length;
				--g_text_field_cursor_char_index;
				text[g_text_field_length] = '\0';
				XVT_LOG_DEBUG(
					"text.field_edited field=%d edit=\"erased\" length=%d max=%d",
					field_id, g_text_field_length,
					max_chars);
			}
		}
	}

	int text_width = frontend_text_measure_width(text, font_size);
	int rect_width = rect->right - rect->left;
	/* From here mouse_x holds the text's horizontal scroll: 0, or the
	 * negative shift that keeps the end of text wider than the field in
	 * view. */
	mouse_x = 0;
	if (rect_width + 1 < text_width) {
		mouse_x = rect_width;
		mouse_x -= font_size;
		mouse_x -= text_width;
		++mouse_x;
	}
	struct RECT previous_clip_rect;
	frontend_display_get_screen_clip_rect(&previous_clip_rect);
	struct RECT text_clip_rect;
	frontend_draw_rect_assign(&text_clip_rect, rect->left + 2,
				  rect->top + 2, rect->right, rect->bottom);
	frontend_display_set_screen_clip_rect640x480(&text_clip_rect);
	frontend_text_draw(font_size, text, mouse_x + rect->left + 2,
			   rect->top + 2, g_text_field_color);
	frontend_display_set_screen_clip_rect640x480(&previous_clip_rect);
	if (g_active_text_field_id == field_id) {
		char saved_char = text[g_text_field_cursor_char_index];
		text[g_text_field_cursor_char_index] = '\0';
		int caret_offset_x =
			frontend_text_measure_width(text, font_size);
		text[g_text_field_cursor_char_index] = saved_char;
		if (frontend_display_get_frame_counter() % 10 < 5) {
			/* previous_clip_rect is reused here as the caret's rectangle. */
			frontend_draw_rect_copy(&previous_clip_rect, rect);
			previous_clip_rect.left += mouse_x + caret_offset_x + 1;
			previous_clip_rect.right = previous_clip_rect.left + 1;
			previous_clip_rect.top++;
			previous_clip_rect.bottom =
				previous_clip_rect.top + font_size;
			frontend_draw_rect(&previous_clip_rect, 0, 0,
					   g_text_field_color, 1);
		}
	}

	return completed;
}

/* Makes the font of size point_size, clamped to 1 to 255, ready for the text
 * functions. Returns 1 when it is already loaded. Otherwise it takes the first
 * free of the 10 slots in g_front_state.font_slots, returning 0 when none is
 * free, and returns 1 when frontend_text_load_font_atlas_file loads
 * "times<size>.abp" into it. When that fails the modern build returns 0, and
 * the original build builds the font from Times New Roman through GDI: it
 * measures characters 1 to 255, copies the offscreen surface to the back
 * buffer, draws each white on black on the offscreen surface through a GDI
 * device context, reads its rows from the locked back buffer and encodes them
 * with front_image_encode_glyph_row, then copies the back buffer to the offscreen
 * surface. With all 255 done it registers the font in g_front_state.font_by_size,
 * saves it as "times<size>.abp" and returns 1; when a step fails it frees the
 * glyph memory and returns 0. */
// FUNCTION: XVT 0x4DADA0
int frontend_text_load_font(int point_size)
{

	if (point_size > UINT8_MAX) {
		point_size = UINT8_MAX;
	} else if (point_size <= 0) {
		point_size = 1;
	}

	struct bitmap_font *font = NULL;
	if (g_front_state.font_by_size[point_size] != NULL) {
		return 1;
	}

	int slot_index;
	for (slot_index = 0; slot_index < 10; ++slot_index) {
		if (g_front_state.font_slots[slot_index].in_use == 0) {
			font = &g_front_state.font_slots[slot_index];
			break;
		}
	}
	if (font == NULL) {
		XVT_LOG_WARN("text.font_slots_full points=%d", point_size);
		return 0;
	}

	char scratch_buffer[1024];
	sprintf(scratch_buffer, "times%u.abp", point_size);
	xvt_file *stream = file_open(scratch_buffer, g_file_mode_read_binary);
	if (stream != NULL) {
		file_close(stream);
		if (frontend_text_load_font_atlas_file(scratch_buffer,
						       slot_index) != 0) {
			return 1;
		}
	}

	XVT_LOG_WARN("text.font_missing points=%d", point_size);
	return 0;
}

/* Frees every loaded font: the glyph memory of each slot whose inUse is 1,
 * which it then marks free, and clears g_front_state.font_by_size. The modern
 * build also drops each font from its renderer. */
// FUNCTION: XVT 0x4DB420
void frontend_text_free_all_fonts(void)
{
	for (int index = 0; index < 10; ++index) {
		if (g_front_state.font_slots[index].in_use == 1) {
			if (g_front_state.font_slots[index].p_glyph_bits != 0) {
				free(g_front_state.font_slots[index]
					     .p_glyph_bits);
				g_front_state.font_slots[index].p_glyph_bits =
					0;
			}
			xvt_render_assets_retire_image(
				&g_front_state.font_slots[index]);
			g_front_state.font_slots[index].in_use = 0;
		}
	}
	memset(g_front_state.font_by_size, 0,
	       sizeof(g_front_state.font_by_size));
}

/* Nothing calls this. Frees the first loaded font whose point_size equals
 * point_size truncated to 8 bits, marks its slot free and clears
 * g_front_state.font_by_size[point_size], not checking that point_size is under 256.
 * The modern build also drops the font from its renderer. */
// FUNCTION: XVT 0x4DB470
void frontend_text_free_font(unsigned int point_size)
{
	for (int index = 0; index < 10; ++index) {
		if (g_front_state.font_slots[index].in_use == 1 &&
		    g_front_state.font_slots[index].point_size ==
			    (uint8_t)point_size) {
			free(g_front_state.font_slots[index].p_glyph_bits);
			g_front_state.font_slots[index].p_glyph_bits = 0;
			xvt_render_assets_retire_image(
				&g_front_state.font_slots[index]);
			g_front_state.font_slots[index].in_use = 0;
			g_front_state.font_by_size[point_size] = 0;
			return;
		}
	}
}

/* Draws str from (x, y), the first glyph's top-left corner, in the font of size
 * fontSize, through front_image_draw_glyph with the text fade applied. A byte 1
 * goes back to color; bytes 2 to 6 switch to g_front_state.text_color_codes[1] to
 * [5]; every other byte is drawn as a glyph and advances x by its width plus
 * the font's char_spacing. Stops when x reaches 640. Returns the glyphs'
 * clip-edge bits ORed together (0x1 left, 0x2 top, 0x4 right, 0x8 bottom); 0
 * when str is NULL, fontSize is outside 0 to 255 or that font is not loaded. */
// FUNCTION: XVT 0x4DB4E0
int frontend_text_draw(int font_size, const char *str, int x, int y, int color)
{
	if (str == NULL) {
		return 0;
	}
	if (font_size < 0 || font_size > 255) {
		return 0;
	}
	struct bitmap_font *font = g_front_state.font_by_size[font_size];
	if (font == NULL) {
		return 0;
	}

	int current_color = color;
	int text_index = 0;
	int result = 0;
	struct image_resource glyph;
	if (*str != '\0') {
		do {
			if (x >= 640) {
				break;
			}
			uint8_t character = (uint8_t)str[text_index];
			if (character == 1) {
				current_color = color;
			} else if (character >= 2 && character <= 6) {
				current_color =
					g_front_state
						.text_color_codes[character -
								  1];
			} else {
				glyph.width = font->glyph_width[character];
				glyph.height = font->glyph_height[character];
				glyph.pixel_data_bytes = 0;
				glyph.is_compressed = 1;
				glyph.pixels = &font->p_glyph_bits
							[font->glyph_bit_offset
								 [character]];
				result |= front_image_draw_glyph(
					&glyph, x, y, current_color, 1);
				x += font->char_spacing +
				     font->glyph_width[(
					     uint8_t)str[text_index]];
			}
			++text_index;
		} while (str[text_index] != '\0');
	}
	return result;
}

/* Draws str like frontend_text_draw, centered in rect: from x rect->left +
 * ((rect->right - rect->left) >> 1) - (text width >> 1) and y rect->top +
 * ((rect->bottom - rect->top) >> 1) - (font height >> 1). Returns what
 * frontend_text_draw would. */
// FUNCTION: XVT 0x4DB640
int frontend_text_draw_centered(int font_size, const char *str,
				const struct RECT *rect, int color)
{
	if (str == NULL) {
		return 0;
	}
	if (font_size < 0 || font_size > 255) {
		return 0;
	}
	struct bitmap_font *font = g_front_state.font_by_size[font_size];
	if (font == NULL) {
		return 0;
	}

	int current_color = color;
	int x = rect->left + ((rect->right - rect->left) >> 1) -
		(frontend_text_measure_width(str, font_size) >> 1);
	int text_index = 0;
	int y = rect->top + ((rect->bottom - rect->top) >> 1);
	int result = 0;
	y -= frontend_text_get_font_height(font_size) >> 1;
	struct image_resource glyph;
	if (*str != '\0') {
		do {
			if (x >= 640) {
				break;
			}
			uint8_t character = (uint8_t)str[text_index];
			if (character == 1) {
				current_color = color;
			} else if (character >= 2 && character <= 6) {
				current_color =
					g_front_state
						.text_color_codes[character -
								  1];
			} else {
				glyph.width = font->glyph_width[character];
				glyph.height = font->glyph_height[character];
				glyph.pixel_data_bytes = 0;
				glyph.is_compressed = 1;
				glyph.pixels = &font->p_glyph_bits
							[font->glyph_bit_offset
								 [character]];
				result |= front_image_draw_glyph(
					&glyph, x, y, current_color, 1);
				x += font->char_spacing +
				     font->glyph_width[(
					     uint8_t)str[text_index]];
			}
			++text_index;
		} while (str[text_index] != '\0');
	}
	return result;
}

/* Draws str like frontend_text_draw in rect: centered across it the way
 * frontend_text_draw_centered does when center_h is nonzero, else from rect->left,
 * and centered down it when center_v is nonzero, else from rect->top. Returns
 * what frontend_text_draw would. */
// FUNCTION: XVT 0x4DB7D0
int frontend_text_draw_aligned_in_rect(int font_size, const char *str,
				       const struct RECT *rect, int center_h,
				       int center_v, int color)
{
	if (str == NULL) {
		return 0;
	}
	if (font_size < 0 || font_size > 255) {
		return 0;
	}
	struct bitmap_font *font = g_front_state.font_by_size[font_size];
	if (font == NULL) {
		return 0;
	}

	int current_color = color;
	int half_width = frontend_text_measure_width(str, font_size) >> 1;
	int x;
	if (center_h) {
		x = rect->left + ((rect->right - rect->left) >> 1) - half_width;
	} else {
		x = rect->left;
	}
	int y;
	if (center_v) {
		y = rect->top + ((rect->bottom - rect->top) >> 1);
		y -= frontend_text_get_font_height(font_size) >> 1;
	} else {
		y = rect->top;
	}

	int text_index = 0;
	int result = 0;
	struct image_resource glyph;
	if (*str != '\0') {
		do {
			if (x >= 640) {
				break;
			}
			uint8_t character = (uint8_t)str[text_index];
			if (character == 1) {
				current_color = color;
			} else if (character >= 2 && character <= 6) {
				current_color =
					g_front_state
						.text_color_codes[character -
								  1];
			} else {
				glyph.width = font->glyph_width[character];
				glyph.height = font->glyph_height[character];
				glyph.pixel_data_bytes = 0;
				glyph.is_compressed = 1;
				glyph.pixels = &font->p_glyph_bits
							[font->glyph_bit_offset
								 [character]];
				result |= front_image_draw_glyph(
					&glyph, x, y, current_color, 1);
				x += font->char_spacing +
				     font->glyph_width[(
					     uint8_t)str[text_index]];
			}
			++text_index;
		} while (str[text_index] != '\0');
	}
	return result;
}

/* Nothing calls this. Draws lineCount strings from lines, one under another,
 * font height + line_spacing apart, each like frontend_text_draw, centered across
 * rect when center_horizontally is nonzero and the block centered down it when
 * center_vertically is nonzero. Here bytes 2 to 7 are color codes, and 7 reads
 * g_front_state.text_color_codes[6], one past the array's end; the color carries
 * from one line to the next. Returns the glyphs' clip-edge bits ORed together,
 * with 0x4 also set for a line that reached x 640; 0 when lines is NULL,
 * fontSize is outside 0 to 255 or that font is not loaded. */
// FUNCTION: XVT 0x4DB980
int frontend_text_draw_line_array_in_rect(int font_size, const char **lines,
					  int line_count,
					  const struct RECT *rect, int color,
					  int center_horizontally,
					  int center_vertically,
					  int line_spacing)
{
	const char **line_cursor = lines;
	if (line_cursor == NULL) {
		return 0;
	}
	if (font_size < 0 || font_size > 255) {
		return 0;
	}
	struct bitmap_font *font = g_front_state.font_by_size[font_size];
	if (font == NULL) {
		return 0;
	}

	int draw_status = 0;
	int current_color = color;
	int draw_y;
	if (center_vertically) {
		draw_y = rect->top + ((rect->bottom - rect->top) >> 1);
		int font_height = frontend_text_get_font_height(font_size);
		draw_y -= (line_spacing * (line_count - 1) +
			   line_count * font_height) >>
			  1;
	} else {
		draw_y = rect->top;
	}
	int draw_x;
	struct image_resource glyph;
	if (line_count > 0) {
		int lines_remaining = line_count;
		do {
			int half_line_width =
				frontend_text_measure_width(*line_cursor,
							    font_size) >>
				1;
			if (center_horizontally) {
				draw_x = rect->left +
					 ((rect->right - rect->left) >> 1) -
					 half_line_width;
			} else {
				draw_x = rect->left;
			}
			int char_index = 0;
			if (**line_cursor != '\0') {
				do {
					if (draw_x >= 640) {
						break;
					}
					const char *char_ptr =
						&(*line_cursor)[char_index];
					uint8_t character = (uint8_t)*char_ptr;
					if (character == 1) {
						current_color = color;
					} else if (character >= 2 &&
						   character <= 7) {
						current_color =
							g_front_state.text_color_codes
								[character - 1];
					} else {
						glyph.width =
							font->glyph_width
								[character];
						glyph.height = font->glyph_height[(
							uint8_t)*char_ptr];
						glyph.pixel_data_bytes = 0;
						glyph.is_compressed = 1;
						glyph.pixels =
							&font->p_glyph_bits[font->glyph_bit_offset[(
								uint8_t)*char_ptr]];
						draw_status |=
							front_image_draw_glyph(
								&glyph, draw_x,
								draw_y,
								current_color,
								1);
						draw_x +=
							font->char_spacing +
							font->glyph_width
								[(uint8_t)(*line_cursor)
									 [char_index]];
					}
					++char_index;
				} while ((*line_cursor)[char_index] != '\0');
			}
			if (draw_x >= 640) {
				draw_status |= 4;
			}
			++line_cursor;
			draw_y += frontend_text_get_font_height(font_size) +
				  line_spacing;
			--lines_remaining;
		} while (lines_remaining != 0);
	}

	return draw_status;
}

/* Draws str in rect with word wrap, clipping to rect meanwhile, and returns the
 * number of line breaks it made, wrapped or forced, which is the index of the
 * last line. Words end at a space, a line feed, a '$' or the string's last
 * byte; a line feed or a '$' also breaks the line, and spaces at a line's start
 * are skipped. A word that would end past rect->right - fontSize starts a new
 * line, and a word still too long breaks inside. Lines are fontSize +
 * line_spacing apart; lines before first_visible_line are laid out but not drawn
 * and take no height, which is how callers scroll. Bytes 1 to 7 change the
 * color as in frontend_text_draw, 7 reading g_front_state.text_color_codes[6], one
 * past the array's end. Returns 0 when str is NULL or empty, fontSize is
 * outside 0 to 255 or that font is not loaded. */
// FUNCTION: XVT 0x4DBBD0
int frontend_text_draw_wrapped(int font_size, const char *str,
			       const struct RECT *rect, int color,
			       int line_spacing, int first_visible_line)
{
	if (str == NULL) {
		return 0;
	}
	if (*str == '\0') {
		return 0;
	}
	if (font_size < 0 || font_size > 255) {
		return 0;
	}
	struct bitmap_font *font = g_front_state.font_by_size[font_size];
	if (font == NULL) {
		return 0;
	}

	int index = 0;
	int x = rect->left;
	int line_index = 0;
	int word_length = 0;
	int at_line_start = 1;
	unsigned int current_color = (unsigned int)color;
	int y = rect->top;
	struct RECT saved_clip;
	frontend_display_get_screen_clip_rect(&saved_clip);
	frontend_display_set_screen_clip_rect640x480(rect);

	char word[256];
	struct image_resource glyph;
	do {
		unsigned int next_color = current_color;
		uint8_t current_char = (uint8_t)str[index];
		if (current_char == 1) {
			next_color = (unsigned int)color;
		} else if (current_char >= 2 && current_char <= 7) {
			next_color =
				(unsigned int)g_front_state
					.text_color_codes[current_char - 1];
		} else if (current_char != ' ' && current_char != '\n' &&
			   str[index + 1] != '\0' && current_char != '$') {
			at_line_start = 0;
			word[word_length++] = (char)current_char;
		} else {
			int i;

			int *char_index_ptr = &i;
			if (at_line_start == 1 && current_char == ' ') {
				++index;
				continue;
			}

			if (current_char != '\n' && current_char != '$') {
				word[word_length++] = (char)current_char;
			}
			word[word_length] = '\0';
			if (x + frontend_text_measure_width(word, font_size) >
			    rect->right - font_size) {
				if (line_index >= first_visible_line) {
					y += font_size + line_spacing;
				}
				++line_index;
				at_line_start = 1;
				x = rect->left;
			}

			for (*char_index_ptr = 0; *char_index_ptr < word_length;
			     ++*char_index_ptr) {
				if (line_index < first_visible_line) {
					x += font->glyph_width
						     [(uint8_t)word
							      [*char_index_ptr]] +
					     font->char_spacing;
					if (x <= rect->right) {
						continue;
					}
				} else {
					uint8_t glyph_char =
						(uint8_t)word[*char_index_ptr];
					glyph.width =
						font->glyph_width[glyph_char];
					glyph.height =
						font->glyph_height[glyph_char];
					glyph.pixel_data_bytes = 0;
					glyph.is_compressed = 1;
					glyph.pixels =
						&font->p_glyph_bits
							 [font->glyph_bit_offset
								  [glyph_char]];
					front_image_draw_glyph(
						&glyph, x, y, current_color, 1);
					x += font->glyph_width[glyph_char] +
					     font->char_spacing;
					if (x <= rect->right - font_size) {
						continue;
					}
				}

				if (line_index >= first_visible_line) {
					y += font_size + line_spacing;
				}
				++line_index;
				x = rect->left;
			}

			if (current_char == '\n' || current_char == '$') {
				if (line_index >= first_visible_line) {
					y += font_size + line_spacing;
				}
				++line_index;
				at_line_start = 1;
				x = rect->left;
			}
			word_length = 0;
		}

		current_color = next_color;
		++index;
	} while (str[index] != '\0');

	frontend_display_set_screen_clip_rect640x480(&saved_clip);
	return line_index;
}

/* Returns the font of size fontSize's height, its glyph_height[0], or 0 when
 * fontSize is outside 0 to 255 or that font is not loaded. */
// FUNCTION: XVT 0x4DBF70
int frontend_text_get_font_height(int font_size)
{
	if (font_size < 0 || font_size > 255) {
		return 0;
	}
	struct bitmap_font *font = g_front_state.font_by_size[font_size];
	if (font == 0) {
		return 0;
	}
	return font->glyph_height[0];
}

/* Returns str's width in pixels in the font of size fontSize: the widths of all
 * bytes over 6, each plus the font's char_spacing, less one char_spacing. Returns
 * 0 when str is NULL, fontSize is outside 0 to 255 or that font is not loaded,
 * and -char_spacing for an empty string. */
// FUNCTION: XVT 0x4DBFA0
int frontend_text_measure_width(const char *str, int font_size)
{
	if (str == 0) {
		return 0;
	}
	if (font_size < 0 || font_size > 255) {
		return 0;
	}

	struct bitmap_font *font = g_front_state.font_by_size[font_size];
	if (font == 0) {
		return 0;
	}

	int width = 0;
	int index = 0;
	if (*str != '\0') {
		do {
			if ((uint8_t)str[index] > 6u) {
				width += font->glyph_width[(uint8_t)str[index]];
				width += font->char_spacing;
			}
			++index;
		} while (str[index] != '\0');
	}

	return width - font->char_spacing;
}

/* Only frontend_text_load_font calls this, in the original build. Writes the font
 * to fileName: the first 0x60B (1,547) bytes of the bitmap_font, with the glyph
 * pointer at its start replaced by glyph_blob_size, then glyph_blob_size bytes of
 * glyph rows. Writes nothing when font is NULL or the file does not open; does
 * not check the writes. The header copy assumes the 32-bit layout, with a
 * 4-byte pointer. */
// FUNCTION: XVT 0x4DC020
void frontend_text_save_font_atlas_file(const char *file_name, void **font,
					unsigned int glyph_blob_size)
{
	if (font != NULL) {
		xvt_file *stream = file_open(file_name, "wb");
		if (stream != NULL) {
			uint8_t disk_header[0x60B];
			memcpy(disk_header, font, sizeof(disk_header));
			*(unsigned int *)disk_header = glyph_blob_size;
			file_write_bytes(stream, disk_header,
					 sizeof(disk_header));
			file_write_bytes(stream, *font, glyph_blob_size);
			file_close(stream);
		}
	}
}

/* Loads a font file written by frontend_text_save_font_atlas_file into
 * g_front_state.font_slots[slotIndex]: the 0x60B-byte header (the original build
 * reads it over the slot as it lies in memory, the modern build field by
 * field), then a glyph block of the size the header's first 4 bytes give.
 * Registers the slot in g_front_state.font_by_size under the file's point_size, not
 * checking that it is under 256, and returns 1. Returns 0 when the file does
 * not open, when the modern build cannot read the header, or when the glyph
 * block cannot be allocated, which also sets the slot's inUse to 0. The slot's
 * inUse comes from the file. The modern build also registers the font with its
 * renderer. */
// FUNCTION: XVT 0x4DC0A0
int frontend_text_load_font_atlas_file(const char *file_name, int slot_index)
{
	uint8_t disk_header[0x60B];
	uint32_t disk_glyph_blob_size;

	struct bitmap_font *font = &g_front_state.font_slots[slot_index];
	xvt_file *stream = file_open(file_name, "rb");
	if (stream == NULL) {
		return 0;
	}

	size_t glyph_blob_size;
	if (!file_read_bytes(stream, disk_header, sizeof(disk_header))) {
		XVT_LOG_DEBUG("text.font_file_short file=\"%s\"", file_name);
		file_close(stream);
		return 0;
	}
	memcpy(&disk_glyph_blob_size, &disk_header[0],
	       sizeof(disk_glyph_blob_size));
	memcpy(font->glyph_bit_offset, &disk_header[4],
	       sizeof(font->glyph_bit_offset));
	memcpy(font->glyph_height, &disk_header[0x404],
	       sizeof(font->glyph_height));
	memcpy(font->glyph_width, &disk_header[0x504],
	       sizeof(font->glyph_width));
	memcpy(&font->point_size, &disk_header[0x604],
	       sizeof(font->point_size));
	font->in_use = disk_header[0x608];
	font->char_spacing = disk_header[0x609];
	font->field_60a = disk_header[0x60A];
	glyph_blob_size = disk_glyph_blob_size;
	void *glyph_bits = malloc(glyph_blob_size);
	font->p_glyph_bits = glyph_bits;
	if (glyph_bits == NULL) {
		XVT_LOG_ERROR("text.font_alloc_failed file=\"%s\" bytes=%u",
			      file_name, (unsigned)glyph_blob_size);
		font->in_use = 0;
		file_close(stream);
		return 0;
	}

	file_read_bytes(stream, glyph_bits, glyph_blob_size);
	if (font->point_size > 255) {
		XVT_LOG_ERROR("text.font_points_invalid file=\"%s\" points=%u",
			      file_name, font->point_size);
	}
	g_front_state.font_by_size[font->point_size] = font;
	XVT_LOG_DEBUG(
		"text.font_loaded file=\"%s\" points=%u font_slot=%d bytes=%u height=%d spacing=%d in_use=%d",
		file_name, font->point_size, slot_index,
		(unsigned)glyph_blob_size, (int)font->glyph_height[0],
		(int)font->char_spacing, (int)font->in_use);
	file_close(stream);
	xvt_render_assets_register_image(font, 0, file_name, XVT_IMAGE_ABP, 0,
					 256, (uint16_t)font->point_size, 0, 0);
	xvt_render_frontend_font_loaded(font);
	return 1;
}

/* Starts a text fade-in lasting frames frames: clears
 * g_front_state.text_fade_color_cache and sets text_fade_frames_left and
 * text_fade_frame_count to frames. Returns 1. */
// FUNCTION: XVT 0x4DC140
int frontend_text_start_text_fade_in(int frames)
{
	memset(g_front_state.text_fade_color_cache, 0,
	       sizeof(g_front_state.text_fade_color_cache));
	g_front_state.text_fade_frames_left = frames;
	g_front_state.text_fade_frame_count = frames;
	return 1;
}

/* Ends any text fade: sets g_front_state.text_fade_frames_left and
 * text_fade_frame_count to 0. Returns 1. */
// FUNCTION: XVT 0x4DC170
int frontend_text_stop_text_fade(void)
{
	g_front_state.text_fade_frames_left = 0;
	g_front_state.text_fade_frame_count = 0;
	return 1;
}

/* Pauses the text fade: saves g_front_state.text_fade_frames_left in
 * g_saved_text_fade_frames_left and sets it to 0. Returns 1. */
// FUNCTION: XVT 0x4DC190
int frontend_text_suspend_text_fade(void)
{
	g_saved_text_fade_frames_left = g_front_state.text_fade_frames_left;
	g_front_state.text_fade_frames_left = 0;
	return 1;
}

/* Puts back the g_front_state.text_fade_frames_left that
 * frontend_text_suspend_text_fade saved. Returns 1. */
// FUNCTION: XVT 0x4DC1B0
int frontend_text_resume_text_fade(void)
{
	g_front_state.text_fade_frames_left = g_saved_text_fade_frames_left;
	return 1;
}

/* Draws text in rect with word wrap in the size-10 font, one line every 14
 * pixels from rect->top down with no bottom limit, each centered down a rect
 * from its top to top + 13. A '$' ends a line and the text ends at its NUL. A
 * line whose measured width reaches rect's width is cut at the last space seen.
 * '[' and ']' become color bytes 2 and 1, so text between them is drawn in
 * g_front_state.text_color_codes[1], and a line that starts inside brackets begins
 * with a 2. Lines are drawn in 0xFFFF from rect->left, centered down their
 * rect; with suppress_centered_headings 0 a line starting with '>' is drawn
 * without it, centered, in g_color_yellow and 1 pixel lower, and the '$' is
 * dropped, while with it nonzero the '$' is drawn as a glyph. Does not check
 * that a line fits its 320-byte buffer. */
// FUNCTION: XVT 0x4F8620
void frontend_text_draw_formatted_wrapped_text(const struct RECT *rect,
					       const uint8_t *text,
					       int suppress_centered_headings)
{
	struct RECT draw_rect;

	frontend_draw_rect_copy(&draw_rect, rect);
	int16_t line_start = -1;
	int16_t scan_index = 0;
	int16_t segment_start = 0;
	draw_rect.bottom = draw_rect.top + 13;
	char line_buffer[320];
	line_buffer[0] = '\0';
	int16_t rect_width = (int16_t)draw_rect.right - (int16_t)draw_rect.left;
	int16_t line_end = -1;
	int16_t last_space = 0;
	int16_t done = 0;
	do {
		int saved_scan_index = scan_index;
		const uint8_t *current_ptr = &text[scan_index];
		uint8_t current_char = *current_ptr;
		if (*current_ptr == '$' || current_char == '\0') {
			line_start = segment_start;
			if ((int16_t)suppress_centered_headings != 0 ||
			    current_char != '$') {
				line_end = scan_index;
			} else {
				line_end = scan_index - 1;
			}
			segment_start = ++scan_index;
			if (text[scan_index] == '\0') {
				done = 1;
			}
		} else {
			if (current_char == ' ') {
				last_space = scan_index;
			}
			if (isspace(current_char)) {
				++scan_index;
				line_buffer[saved_scan_index - segment_start] =
					(char)*current_ptr;
			} else {
				while (!isspace(text[scan_index]) &&
				       text[scan_index] != '$' &&
				       text[scan_index] != '\0') {
					if (text[scan_index] == '[') {
						line_buffer[scan_index -
							    segment_start] = 2;
					} else if (text[scan_index] == ']') {
						line_buffer[scan_index -
							    segment_start] = 1;
					} else {
						line_buffer[scan_index -
							    segment_start] =
							(char)text[scan_index];
					}
					++scan_index;
				}
			}
			line_buffer[scan_index - segment_start] = '\0';
			if (rect_width <= (int16_t)frontend_text_measure_width(
						  line_buffer, 10)) {
				line_start = segment_start;
				segment_start = last_space + 1;
				scan_index = last_space + 1;
				line_end = last_space;
			}
		}

		int16_t color_active = 0;
		for (int16_t source_index = 0; line_start > source_index;
		     ++source_index) {
			if (text[source_index] == '[') {
				color_active = 1;
			}
			if (text[source_index] == ']') {
				color_active = 0;
			}
		}
		if (line_start != -1) {
			int16_t output_index = 0;
			if (color_active) {
				line_buffer[output_index++] = 2;
			}
			line_buffer[output_index] = '\0';
			while (line_end >= line_start) {
				current_char = text[line_start];
				if (current_char == '[') {
					line_buffer[output_index] = 2;
				} else if (current_char == ']') {
					line_buffer[output_index] = 1;
				} else {
					line_buffer[output_index] =
						(char)current_char;
				}
				++output_index;
				++line_start;
			}
			line_buffer[output_index] = '\0';
			if ((int16_t)suppress_centered_headings != 0 ||
			    line_buffer[0] != '>') {
				frontend_text_draw_aligned_in_rect(
					10, line_buffer, &draw_rect, 0, 1,
					0xFFFF);
			} else {
				frontend_draw_rect_offset_xy(&draw_rect, 0, 1);
				frontend_text_draw_centered(10, &line_buffer[1],
							    &draw_rect,
							    g_color_yellow);
				frontend_draw_rect_offset_xy(&draw_rect, 0, -1);
			}
			line_start = -1;
			line_end = -1;
			frontend_draw_rect_offset_xy(&draw_rect, 0, 14);
		}
	} while (!done);
}
