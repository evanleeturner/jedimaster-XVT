#include "xvt/frontend/frontend_button.h"

#include <stdio.h>
#include <string.h>

#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt_runtime/log/log_both_builds.h"

/* 1 once frontend_button_draw_sprite_and_tooltip has computed
 * g_front_button_rect_gray_color. Only that function writes it, and nothing sets it
 * back to 0. */
// GLOBAL: XVT 0x52C01C
static int g_front_button_rect_gray_color_initialized = 0;
/* The tooltip box's outline color, RGB 0x60, 0x60, 0x60 as a display pixel
 * value from frontend_display_pack_rgb. Set once, by
 * frontend_button_draw_sprite_and_tooltip. */
// GLOBAL: XVT 0x52C020
static int g_front_button_rect_gray_color = 0;

/* RGB 5, 0x63, 0x6D as a display pixel value from frontend_display_pack_rgb: a
 * text button's outer fill and text when not pressed, its inner fill when
 * pressed. Set once, by frontend_button_draw_text_button_state. */
// GLOBAL: XVT 0x665460
static int g_front_button_dark_color = 0;

/* Nonzero while frontend_button_draw_sprite_and_tooltip also draws
 * g_button_overlay_text over each sprite button's rect. Set to 1 by
 * frontend_button_enable_overlay_text and to 0 by
 * frontend_button_disable_overlay_text; 0 until the first enable. */
// GLOBAL: XVT 0x665468
static int g_button_overlay_text_enabled;
/* The label drawn over sprite buttons while overlay text is enabled: the
 * caller's pointer, not a copy, chiefly to frontend string table entries. Only
 * frontend_button_set_overlay_text writes it. */
// GLOBAL: XVT 0x66546C
const char *g_button_overlay_text;
/* 1 once frontend_button_draw_text_button_state has computed g_front_button_dark_color
 * and g_front_button_light_color. Only that function writes it, and nothing sets
 * it back to 0. */
// GLOBAL: XVT 0x665570
static int g_front_button_colors_initialized = 0;
/* Nonzero to make the next frontend_button_draw_overlay_text draw the pressed
 * colors. Set to 1 by frontend_button_use_pressed_overlay_style; set to 0 by every
 * frontend_button_draw_overlay_text and by frontend_button_enable_overlay_text. */
// GLOBAL: XVT 0x665574
static int g_button_overlay_pressed_style;
/* RGB 0x63, 0xE7, 0xF7 as a display pixel value from frontend_display_pack_rgb: a
 * text button's inner fill when not pressed, its outer fill and text when
 * pressed. Set once, by frontend_button_draw_text_button_state. */
// GLOBAL: XVT 0x665578
static int g_front_button_light_color = 0;
/* Per button slot, 1 when a mouse button was held over that button at its last
 * update, so the press sound plays once per press. Indexed by the callers'
 * held_state_slot; only frontend_button_handle_text_button and
 * frontend_button_handle_sprite_button write it. */
// GLOBAL: XVT 0x665580
static uint8_t g_button_held_state[256] = {0};

/* Draws and runs a text button for one frame. While the cursor is over rect and
 * a mouse button is held it draws the button pressed and, at the start of the
 * hold, plays click_sound_name when g_game_config.sfx_datapad_enabled is set, at
 * volume 12 * g_game_config.sfx_datapad_volume; otherwise it draws it unpressed.
 * Returns 1 when the left button was released over it this frame, else 2 when
 * the right one was, else 0. Writes g_button_held_state[held_state_slot], not
 * checking that the slot is under 256. unused_color is passed on and never
 * used. */
// FUNCTION: XVT 0x4DA650
int frontend_button_handle_text_button(const struct RECT *rect,
				       const char *text, int font_size,
				       int unused_color, int held_state_slot,
				       const char *click_sound_name)
{
	int cursor_x;
	int cursor_y;

	frontend_cursor_get_pos(&cursor_x, &cursor_y);
	if (frontend_draw_point_in_rect(rect, cursor_x, cursor_y)) {
		if (frontend_mouse_get_left_down() != 0 ||
		    frontend_mouse_get_right_down() != 0) {
			frontend_button_draw_text_button_state(
				rect, text, font_size, unused_color, 1);
			if (g_button_held_state[held_state_slot] == 0 &&
			    g_game_config.sfx_datapad_enabled != 0) {
				frontend_sound_play_ui_sound(
					click_sound_name, 1, 0, 255,
					12 * g_game_config.sfx_datapad_volume,
					63);
			}
			g_button_held_state[held_state_slot] = 1;
		} else {
			frontend_button_draw_text_button_state(
				rect, text, font_size, unused_color, 0);
			g_button_held_state[held_state_slot] = 0;
		}
		if (frontend_mouse_get_left_click() != 0) {
			XVT_LOG_DEBUG(
				"ui.text_button_clicked button=%d right=%d caption=\"%s\"",
				held_state_slot, 0, text != NULL ? text : "");
			return 1;
		}
		if (frontend_mouse_get_right_click() != 0) {
			XVT_LOG_DEBUG(
				"ui.text_button_clicked button=%d right=%d caption=\"%s\"",
				held_state_slot, 1, text != NULL ? text : "");
			return 2;
		}
	} else {
		g_button_held_state[held_state_slot] = 0;
		frontend_button_draw_text_button_state(rect, text, font_size,
						       unused_color, 0);
	}
	return 0;
}

/* Draws and runs a sprite button for one frame through
 * frontend_button_draw_sprite_and_tooltip. While the cursor is over rect: when
 * either mouse button was released this frame it draws pressed_sprite in the
 * pressed overlay style and returns 1; while a button is held it draws it the
 * same way and, at the start of the hold, plays press_sound_name when
 * g_game_config.sfx_datapad_enabled is set, at volume 12 *
 * g_game_config.sfx_datapad_volume. Otherwise it draws normal_sprite. Returns 0 on
 * every other path. Writes g_button_held_state[held_state_slot], except on the
 * release frame, not checking that the slot is under 256. unused_color is passed
 * on and never used. */
// FUNCTION: XVT 0x4DA780
int frontend_button_handle_sprite_button(
	struct RECT *rect, const char *normal_sprite,
	const char *pressed_sprite, const char *tooltip_text, int font_size,
	int unused_color, int held_state_slot, const char *press_sound_name)
{
	int cursor_x;
	int cursor_y;

	frontend_cursor_get_pos(&cursor_x, &cursor_y);
	if (frontend_draw_point_in_rect(rect, cursor_x, cursor_y)) {
		if (frontend_mouse_get_left_click() != 0 ||
		    frontend_mouse_get_right_click() != 0) {
			XVT_LOG_DEBUG(
				"ui.sprite_button_clicked button=%d sprite=\"%s\" caption=\"%s\"",
				held_state_slot,
				normal_sprite != NULL ? normal_sprite : "",
				tooltip_text != NULL ? tooltip_text : "");
			frontend_button_use_pressed_overlay_style();
			frontend_button_draw_sprite_and_tooltip(
				rect, pressed_sprite, tooltip_text, font_size,
				unused_color);
			return 1;
		}
		if (frontend_mouse_get_left_down() != 0 ||
		    frontend_mouse_get_right_down() != 0) {
			frontend_button_use_pressed_overlay_style();
			frontend_button_draw_sprite_and_tooltip(
				rect, pressed_sprite, tooltip_text, font_size,
				unused_color);
			if (g_button_held_state[held_state_slot] == 0 &&
			    g_game_config.sfx_datapad_enabled != 0) {
				frontend_sound_play_ui_sound(
					press_sound_name, 1, 0, 255,
					12 * g_game_config.sfx_datapad_volume,
					63);
			}
			g_button_held_state[held_state_slot] = 1;
			return 0;
		}
		frontend_button_draw_sprite_and_tooltip(rect, normal_sprite,
							tooltip_text, font_size,
							unused_color);
		g_button_held_state[held_state_slot] = 0;
		return 0;
	}

	g_button_held_state[held_state_slot] = 0;
	frontend_button_draw_sprite_and_tooltip(
		rect, normal_sprite, tooltip_text, font_size, unused_color);
	return 0;
}

/* Draws a text button: rect filled in one button color, an inner rect inset 3
 * pixels (1 when rect's bottom - top is 14 or less) filled in the other, and
 * text centered in rect in the outer color. With is_pressed nonzero the outer
 * color is g_front_button_light_color, else g_front_button_dark_color; the first call
 * computes both. Returns frontend_text_draw_centered's result. */
// FUNCTION: XVT 0x4DA8E0
int frontend_button_draw_text_button_state(const struct RECT *rect,
					   const char *text, int font_size,
					   int unused_color, char is_pressed)
{
	(void)unused_color;

	if (!g_front_button_colors_initialized) {
		g_front_button_colors_initialized = 1;
		g_front_button_dark_color =
			frontend_display_pack_rgb(5, 0x63, 0x6D);
		g_front_button_light_color =
			frontend_display_pack_rgb(0x63, 0xE7, 0xF7);
	}

	struct RECT inner_rect;
	int text_color;
	if (is_pressed) {
		frontend_draw_rect(rect, 0, 0, g_front_button_light_color, 1);
		frontend_draw_rect_copy(&inner_rect, rect);
		if (rect->bottom - rect->top > 14) {
			frontend_draw_rect_inset_xy(&inner_rect, 3, 3);
		} else {
			frontend_draw_rect_inset_xy(&inner_rect, 1, 1);
		}
		frontend_draw_rect(&inner_rect, 0, 0, g_front_button_dark_color,
				   1);
		text_color = g_front_button_light_color;
	} else {
		frontend_draw_rect(rect, 0, 0, g_front_button_dark_color, 1);
		frontend_draw_rect_copy(&inner_rect, rect);
		if (rect->bottom - rect->top > 14) {
			frontend_draw_rect_inset_xy(&inner_rect, 3, 3);
		} else {
			frontend_draw_rect_inset_xy(&inner_rect, 1, 1);
		}
		frontend_draw_rect(&inner_rect, 0, 0,
				   g_front_button_light_color, 1);
		text_color = g_front_button_dark_color;
	}

	return frontend_text_draw_centered(font_size, text, rect, text_color);
}

/* Draws sprite_name with its top-left corner at (0, 0), then, while overlay text
 * is enabled, g_button_overlay_text centered in rect through
 * frontend_button_draw_overlay_text. When tooltip_text is not NULL and the cursor
 * is in rect, it also draws a tooltip box whose top-left corner is the cursor
 * moved right and down by the cursor's size, with right = left + text_width + 5
 * and bottom = top + fontSize + 3: filled 0xFFFF, outlined in
 * g_front_button_rect_gray_color, the text centered in color 0. When left +
 * text_width + 5 reaches 640 the left becomes 634 - text_width, and when top +
 * fontSize + 5 reaches 480 the top becomes 474 - fontSize. The first call
 * computes the gray. unused_color is never used. */
// FUNCTION: XVT 0x4DAA20
void frontend_button_draw_sprite_and_tooltip(struct RECT *rect,
					     const char *sprite_name,
					     const char *tooltip_text,
					     int font_size, int unused_color)
{
	(void)unused_color;

	front_image_draw_sprite(sprite_name, 0, 0);
	if (g_front_button_rect_gray_color_initialized == 0) {
		g_front_button_rect_gray_color_initialized = 1;
		g_front_button_rect_gray_color =
			frontend_display_pack_rgb(0x60, 0x60, 0x60);
	}

	int tooltip_left;
	int tooltip_top;
	frontend_cursor_get_pos(&tooltip_left, &tooltip_top);
	if (g_button_overlay_text_enabled != 0) {
		frontend_button_draw_overlay_text(rect, g_button_overlay_text);
	}
	if (tooltip_text == NULL ||
	    !frontend_draw_point_in_rect(rect, tooltip_left, tooltip_top)) {
		return;
	}

	int text_width = frontend_text_measure_width(tooltip_text, font_size);
	int cursor_width;
	int cursor_height;
	frontend_cursor_get_dimensions(&cursor_width, &cursor_height);
	tooltip_left += cursor_width;
	tooltip_top += cursor_height;
	if (text_width + tooltip_left + 5 >= 640) {
		tooltip_left = 634 - text_width;
	}
	if ((uint32_t)(font_size + tooltip_top + 5) >= 480) {
		tooltip_top = 474 - font_size;
	}

	struct RECT tooltip_rect;
	frontend_draw_rect_assign(&tooltip_rect, tooltip_left, tooltip_top,
				  text_width + tooltip_left + 5,
				  font_size + tooltip_top + 3);
	frontend_draw_rect(&tooltip_rect, 0, 0, 0xFFFF, 1);
	frontend_draw_rect_outline(&tooltip_rect, 0, 0,
				   g_front_button_rect_gray_color);
	frontend_text_draw_centered(font_size, tooltip_text, &tooltip_rect, 0);
}

/* Draws the state sprites of an eight-slot navigation bar from slot_states: the
 * sprite "active<n>" for each FRONTEND_NAVIGATION_SLOT_ACTIVE slot n, 1 to 8,
 * then, for each FRONTEND_NAVIGATION_SLOT_SELECTED slot, the sprite
 * "<n>lita<s>", s being the state value of the slot before it, and
 * "<n>litb<s>", s being that of the slot after it. For these two the slots are
 * taken in the order 1 to 5, 8, 6, 7, n counts positions in that order, the
 * first and seventh positions get no "lita" and the sixth and eighth no "litb".
 * Every sprite is drawn at (0, 0); the names are built in
 * g_frontend_scratch_buffer. */
// FUNCTION: XVT 0x4DABA0
void frontend_button_draw_eight_slot_navigation_state(
	const frontend_navigation_slot_state *slot_states)
{
	frontend_navigation_slot_state states[8];

	memcpy(states, slot_states, sizeof(states));
	int index;
	for (index = 0; index < 8; ++index) {
		if (states[index] == FRONTEND_NAVIGATION_SLOT_ACTIVE) {
			sprintf(g_frontend_scratch_buffer, "active%u",
				index + 1);
			front_image_draw_sprite(g_frontend_scratch_buffer, 0,
						0);
		}
	}

	frontend_navigation_slot_state saved_state = states[7];
	states[7] = states[6];
	states[6] = states[5];
	states[5] = saved_state;
	index = 0;
	frontend_navigation_slot_state *state = states;
	do {
		if (*state == FRONTEND_NAVIGATION_SLOT_SELECTED) {
			if (state != states && state != &states[6]) {
				sprintf(g_frontend_scratch_buffer, "%ulita%u",
					index + 1, states[index - 1]);
				front_image_draw_sprite(
					g_frontend_scratch_buffer, 0, 0);
			}
			if (state != &states[7] && state != &states[5]) {
				sprintf(g_frontend_scratch_buffer, "%ulitb%u",
					index + 1, state[1]);
				front_image_draw_sprite(
					g_frontend_scratch_buffer, 0, 0);
			}
		}
		++state;
		++index;
	} while (state < states + 8);
}

/* Draws str centered in rect in the size-10 font twice: a shadow 2 pixels right
 * and down, then the text. The text is g_color_pale_cyan on a g_color_teal shadow,
 * or the reverse after frontend_button_use_pressed_overlay_style; the call then
 * sets g_button_overlay_pressed_style to 0. Moves *rect and moves it back. Returns
 * the second frontend_text_draw_centered result. */
// FUNCTION: XVT 0x4DACA0
int frontend_button_draw_overlay_text(struct RECT *rect, const char *str)
{
	int result;

	if (g_button_overlay_pressed_style) {
		frontend_draw_rect_offset_xy(rect, 2, 2);
		frontend_text_draw_centered(10, str, rect, g_color_pale_cyan);
		frontend_draw_rect_offset_xy(rect, -2, -2);
		result = frontend_text_draw_centered(10, str, rect,
						     g_color_teal);
	} else {
		frontend_draw_rect_offset_xy(rect, 2, 2);
		frontend_text_draw_centered(10, str, rect, g_color_teal);
		frontend_draw_rect_offset_xy(rect, -2, -2);
		result = frontend_text_draw_centered(10, str, rect,
						     g_color_pale_cyan);
	}

	g_button_overlay_pressed_style = 0;
	return result;
}

/* Turns overlay text on and drops the pressed style: sets
 * g_button_overlay_text_enabled to 1 and g_button_overlay_pressed_style to 0. */
// FUNCTION: XVT 0x4DAD40
void frontend_button_enable_overlay_text(void)
{
	g_button_overlay_text_enabled = 1;
	g_button_overlay_pressed_style = 0;
}

/* Turns overlay text off: sets g_button_overlay_text_enabled to 0. */
// FUNCTION: XVT 0x4DAD60
void frontend_button_disable_overlay_text(void)
{
	g_button_overlay_text_enabled = 0;
}

/* Stores text in g_button_overlay_text, the pointer and not a copy, and returns
 * it. */
// FUNCTION: XVT 0x4DAD70
const char *frontend_button_set_overlay_text(const char *text)
{
	return g_button_overlay_text = text;
}

/* Sets g_button_overlay_pressed_style to 1, so the next
 * frontend_button_draw_overlay_text swaps its colors. */
// FUNCTION: XVT 0x4DAD80
void frontend_button_use_pressed_overlay_style(void)
{
	g_button_overlay_pressed_style = 1;
}

/* Returns g_button_overlay_text_enabled. */
// FUNCTION: XVT 0x4DAD90
int frontend_button_is_overlay_text_enabled(void)
{
	return g_button_overlay_text_enabled;
}
