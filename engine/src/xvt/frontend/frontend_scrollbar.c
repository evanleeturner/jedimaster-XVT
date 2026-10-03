#include "xvt/frontend/frontend_scrollbar.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/input/keyboard.h"
#include <string.h>

/* Frames left before a held scrollbar arrow steps the value again; the step
 * comes on a frame that finds it 0. One shared by every scrollbar. Only
 * frontend_scrollbar_draw writes it: it loads g_scrollbar_repeat_interval after
 * each step, lowers it by one on each other frame an arrow is held, and sets it
 * to 0 on a click and during a thumb drag. */
// GLOBAL: XVT 0x52C00C
int g_scrollbar_repeat_countdown = 0;
/* The value g_scrollbar_repeat_countdown is loaded with after an arrow step: 0
 * until the first step, then 12, then the previous value shifted right 2 bits
 * but at least 1 (3, then 1). Only frontend_scrollbar_draw writes it, and sets
 * it to 0 on a click and during a thumb drag. */
// GLOBAL: XVT 0x52C010
int g_scrollbar_repeat_interval = 0;

/* Copies the scrollable-control focus list, g_scrollable_control_ids and
 * g_scrollable_control_count, into g_scrollable_control_ids_saved and
 * g_scrollable_control_count_saved. Returns 1. */
// FUNCTION: XVT 0x4D9D10
int frontend_scrollbar_save_state(void)
{
	memcpy(g_scrollable_control_ids_saved, g_scrollable_control_ids,
	       sizeof(g_scrollable_control_ids_saved));
	g_scrollable_control_count_saved = g_scrollable_control_count;
	return 1;
}

/* Copies the focus list saved by frontend_scrollbar_save_state back into
 * g_scrollable_control_ids and g_scrollable_control_count. Returns 1. */
// FUNCTION: XVT 0x4D9D40
int frontend_scrollbar_restore_state(void)
{
	memcpy(g_scrollable_control_ids, g_scrollable_control_ids_saved,
	       sizeof(g_scrollable_control_ids));
	g_scrollable_control_count = g_scrollable_control_count_saved;
	return 1;
}

/* Draws a vertical scrollbar in bar_rect and returns the value the player's
 * input this frame gives it, current_value when none. The arrow buttons are
 * squares as tall as the bar is wide at each end; travel is the bar's height
 * minus two widths, and the thumb is travel / (maximum_exclusive - minimum)
 * pixels tall, at least 1, placed as if minimum were 0 (every caller passes 0).
 * Registers control_id in the Tab focus list and, when a Tab is next in the
 * keyboard buffer, takes it and moves the focus on. A click in the track above
 * or below the thumb moves the value by page_step; holding an arrow moves it by
 * 1 at once, again 13 frames later, then 4 frames later, then every 2 frames.
 * While the bar has the focus, held Page Up and Page Down keys move it by
 * page_step and held Up and Down arrow keys by 1. Each of these starts from
 * current_value, so they do not add up; the last one checked wins. Pressing on
 * the thumb claims the mouse input gate as control_id + 1000; while it holds the
 * gate the value is (cursor y - bar top - width) * (maximum_exclusive - minimum)
 * / travel, and the release opens the gate and clears the clicks. Each change
 * stays from minimum to maximum_exclusive - 1. Fills the track translucent in
 * color (the whole bar during a drag) and outlines the thumb in 0xFFFF, filling
 * it inside, inset 2 pixels, when focused. Draws nothing and returns
 * current_value when the bar is not taller than wide. The original build divides
 * by zero when maximum_exclusive equals minimum; the modern build returns
 * current_value there, drawing nothing, as it does whenever their difference is
 * under 1. */
// FUNCTION: XVT 0x4D9D70
int frontend_scrollbar_draw(const struct RECT *bar_rect, int current_value,
			    int maximum_exclusive, int minimum, int page_step,
			    unsigned int color, int control_id)
{
	struct RECT thumb;

	struct {
		/* The part of the bar being drawn or tested: the track, an
		 * arrow button or the thumb during a drag. */
		struct RECT part_rect;
		int cursor_x; /* Cursor x, from frontend_cursor_get_pos. */
		/* control_id + 1000, the id this bar holds the mouse input gate
		 * with during a thumb drag. */
		int gate_id;
	} draw_state;

	int cursor_y;
	int width;
	int height;
	int travel;
	int range;
	int thumb_size;
	int top;
	int value;

	frontend_register_scrollable_control(control_id);
	frontend_cursor_get_pos(&draw_state.cursor_x, &cursor_y);
	width = bar_rect->right - bar_rect->left;
	height = bar_rect->bottom - bar_rect->top;
	travel = height - 2 * width;
	range = maximum_exclusive - minimum;
#ifdef XVT_MODERN
	if (range <= 0) {
		return current_value;
	}
#endif
	thumb_size = travel / range;
	if (thumb_size < 1) {
		thumb_size = 1;
	}
	value = current_value;
	if (height > width) {
		if (keyboard_peek_char() == 9) {
			keyboard_discard_char();
			frontend_cycle_scrollable_focus();
		}
		draw_state.gate_id = control_id + 1000;

		/* Draw and update the ordinary scrollbar while no thumb drag owns the input gate. */
		if (!frontend_mouse_is_gate_owner(draw_state.gate_id)) {
			if (frontend_mouse_get_left_click() ||
			    frontend_mouse_get_right_click()) {
				g_scrollbar_repeat_countdown = 0;
				g_scrollbar_repeat_interval = 0;
			}
			frontend_draw_rect_copy(&draw_state.part_rect,
						bar_rect);
			frontend_draw_rect_assign(
				&thumb, draw_state.part_rect.left,
				width + travel * current_value / range +
					draw_state.part_rect.top,
				draw_state.part_rect.right,
				width + travel * current_value / range +
					draw_state.part_rect.top + thumb_size);
			draw_state.part_rect.top += width;
			draw_state.part_rect.bottom -= width;
			frontend_draw_fill_rect_translucent(
				&draw_state.part_rect, 0, 0, color);
			if (frontend_draw_point_in_rect(&draw_state.part_rect,
							draw_state.cursor_x,
							cursor_y) &&
			    (frontend_mouse_get_left_click() ||
			     frontend_mouse_get_right_click())) {
				if (cursor_y < thumb.top) {
					value = current_value - page_step;
					if (current_value - page_step <
					    minimum) {
						value = minimum;
					}
				} else if (cursor_y > thumb.bottom) {
					value = current_value + page_step;
					if (maximum_exclusive <= value) {
						value = maximum_exclusive - 1;
					}
				}
			}
			if (g_scrollable_control_ids[0] == control_id) {
				if (keyboard_is_key_down(0x21)) {
					value = current_value - page_step;
					if (current_value - page_step <
					    minimum) {
						value = minimum;
					}
				} else if (keyboard_is_key_down(0x22)) {
					value = current_value + page_step;
					if (maximum_exclusive <= value) {
						value = maximum_exclusive - 1;
					}
				}
			}
			frontend_draw_rect_assign(&draw_state.part_rect,
						  bar_rect->left, bar_rect->top,
						  bar_rect->right,
						  bar_rect->top + width);
			if (frontend_draw_point_in_rect(&draw_state.part_rect,
							draw_state.cursor_x,
							cursor_y) &&
			    (frontend_mouse_get_left_down() ||
			     frontend_mouse_get_right_down())) {
				front_image_draw_sprite("slideud",
							bar_rect->left,
							bar_rect->top);
				if (g_scrollbar_repeat_countdown == 0) {
					if (current_value > minimum) {
						value = current_value - 1;
					}
					if (g_scrollbar_repeat_interval == 0) {
						g_scrollbar_repeat_interval =
							12;
					} else {
						g_scrollbar_repeat_interval >>=
							2;
						if (g_scrollbar_repeat_interval ==
						    0) {
							g_scrollbar_repeat_interval =
								1;
						}
					}
					g_scrollbar_repeat_countdown =
						g_scrollbar_repeat_interval;
				} else {
					--g_scrollbar_repeat_countdown;
				}
			} else {
				front_image_draw_sprite("slideuu",
							bar_rect->left,
							bar_rect->top);
			}
			if (g_scrollable_control_ids[0] == control_id) {
				if (keyboard_is_key_down(0x26)) {
					if (current_value > minimum) {
						value = current_value - 1;
					}
				}
			}
			frontend_draw_rect_assign(
				&draw_state.part_rect, bar_rect->left,
				bar_rect->bottom - width, bar_rect->right,
				bar_rect->bottom);
			if (frontend_draw_point_in_rect(&draw_state.part_rect,
							draw_state.cursor_x,
							cursor_y) &&
			    (frontend_mouse_get_left_down() ||
			     frontend_mouse_get_right_down())) {
				front_image_draw_sprite(
					"slidedd", bar_rect->left,
					bar_rect->bottom - width);
				if (g_scrollbar_repeat_countdown == 0) {
					if (current_value <
					    maximum_exclusive - 1) {
						value = current_value + 1;
					}
					if (g_scrollbar_repeat_interval == 0) {
						g_scrollbar_repeat_interval =
							12;
					} else {
						g_scrollbar_repeat_interval >>=
							2;
						if (g_scrollbar_repeat_interval ==
						    0) {
							g_scrollbar_repeat_interval =
								1;
						}
					}
					g_scrollbar_repeat_countdown =
						g_scrollbar_repeat_interval;
				} else {
					--g_scrollbar_repeat_countdown;
				}
			} else {
				front_image_draw_sprite(
					"slidedu", bar_rect->left,
					bar_rect->bottom - width);
			}
			if (g_scrollable_control_ids[0] == control_id) {
				if (keyboard_is_key_down(0x28) &&
				    current_value < maximum_exclusive - 1) {
					value = current_value + 1;
				}
			}
			frontend_draw_rect(&thumb, 0, 0, 0xFFFF, 0);
			if (frontend_is_scrollable_control_focused(
				    control_id)) {
				frontend_draw_rect_inset_xy(&thumb, 2, 2);
				frontend_draw_rect(&thumb, 0, 0,
						   g_color_pale_cyan, 1);
				frontend_draw_rect_inset_xy(&thumb, -2, -2);
			}
			if (frontend_mouse_is_gate_open() &&
			    frontend_draw_point_in_rect(
				    &thumb, draw_state.cursor_x, cursor_y) &&
			    (frontend_mouse_get_left_down() ||
			     frontend_mouse_get_right_down())) {
				frontend_mouse_set_input_gate(
					draw_state.gate_id);
			}
		} else {
			int thumb_y;

			g_scrollbar_repeat_countdown = 0;
			g_scrollbar_repeat_interval = 0;
			if (frontend_mouse_get_left_click_for(
				    draw_state.gate_id) ||
			    frontend_mouse_get_right_click_for(
				    draw_state.gate_id)) {
				frontend_mouse_clear_input_gate();
				frontend_mouse_clear_clicks();
			}
			frontend_draw_rect_copy(&draw_state.part_rect,
						bar_rect);
			frontend_draw_fill_rect_translucent(
				&draw_state.part_rect, 0, 0, color);
			frontend_draw_rect_assign(&draw_state.part_rect,
						  bar_rect->left, bar_rect->top,
						  bar_rect->right,
						  bar_rect->top + width);
			front_image_draw_sprite("slideuu", bar_rect->left,
						bar_rect->top);
			frontend_draw_rect_assign(
				&draw_state.part_rect, bar_rect->left,
				bar_rect->bottom - width, bar_rect->right,
				bar_rect->bottom);
			front_image_draw_sprite("slidedu", bar_rect->left,
						bar_rect->bottom - width);
			top = bar_rect->top;
			value = (cursor_y - width - top) * range / travel;
			if (value < minimum) {
				value = minimum;
			}
			if (value >= maximum_exclusive) {
				value = maximum_exclusive - 1;
			}
			thumb_y = cursor_y;
			if (thumb_y < top + width) {
				thumb_y = top + width;
			} else if (thumb_y >=
				   bar_rect->bottom - width - thumb_size) {
				thumb_y = bar_rect->bottom - width -
					  thumb_size - 1;
			}
			frontend_draw_rect_assign(
				&draw_state.part_rect, bar_rect->left, thumb_y,
				bar_rect->right, thumb_y + thumb_size);
			frontend_draw_rect(&draw_state.part_rect, 0, 0, 0xFFFF,
					   0);
			if (frontend_is_scrollable_control_focused(
				    control_id)) {
				frontend_draw_rect_inset_xy(
					&draw_state.part_rect, 2, 2);
				frontend_draw_rect(&draw_state.part_rect, 0, 0,
						   g_color_teal, 1);
				frontend_draw_rect_inset_xy(
					&draw_state.part_rect, -2, -2);
			}
		}
	}
	return value;
}
