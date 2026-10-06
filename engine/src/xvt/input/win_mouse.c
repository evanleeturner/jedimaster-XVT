#include "xvt/input/win_mouse.h"

#include "aeron/aeron.h"
#include "xvt/flight/flight_input.h"
#include "xvt/util/win32.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/runtime/presentation.h"

/* Cursor point the next win_mouse_poll_state measures movement from. Each poll
 * sets it to g_win_mouse_center_pos after warping the cursor there, and
 * win_mouse_set_position sets it; in the modern build a poll also sets it to the
 * polled point while xvt_input_mouse_motion_allowed is false, so that poll finds
 * no movement. */
// GLOBAL: XVT 0x527ED8
struct POINT g_win_mouse_prev_pos;
/* Cursor point win_mouse_poll_state last read: from GetCursorPos in the original
 * build; in the modern one from xvt_presentation_mouse_to_classic, or the old
 * point kept when that call returns 0. win_mouse_set_position also sets it. */
// GLOBAL: XVT 0x527EE0
struct POINT g_win_mouse_cursor_pos;
/* The game's mouse position: each poll adds the movement and clamps it to
 * g_win_mouse_min_x to g_win_mouse_max_x and g_win_mouse_min_y to g_win_mouse_max_y;
 * win_mouse_set_position sets it. Polls return it times g_win_mouse_scale_x and
 * g_win_mouse_scale_y. */
// GLOBAL: XVT 0x527EE8
struct POINT g_win_mouse_pos;
/* Left, right and middle button held, 1 or 0. Only the modern build's
 * win_mouse_poll_state writes it, from Aeron's input snapshot; in the original
 * build nothing in the engine writes it, so it stays 0. */
// GLOBAL: XVT 0x527EF0
static int g_win_mouse_button_down[3] = {0, 0, 0};
/* Left, right and middle button pressed since the last poll, 1 or 0. The modern
 * build's win_mouse_poll_state sets it from Aeron's input snapshot; every poll
 * clears it after copying it out. In the original build nothing else writes
 * it. */
// GLOBAL: XVT 0x527EFC
static int g_win_mouse_button_pressed[3] = {0, 0, 0};
/* Left, right and middle button released since the last poll, written like
 * g_win_mouse_button_pressed. */
// GLOBAL: XVT 0x527F08
static int g_win_mouse_button_released[3] = {0, 0, 0};
/* Lowest X g_win_mouse_pos may take. Only win_mouse_set_horizontal_bounds writes it,
 * and only the uncalled mouse_set_horizontal_bounds calls that, so it stays 0. */
// GLOBAL: XVT 0x527F14
int g_win_mouse_min_x;
/* Highest X g_win_mouse_pos may take; written like g_win_mouse_min_x, so it stays
 * 0. */
// GLOBAL: XVT 0x527F18
int g_win_mouse_max_x;
/* Lowest Y g_win_mouse_pos may take. Only win_mouse_set_vertical_bounds writes it,
 * and only the uncalled mouse_set_vertical_bounds calls that, so it stays 0. */
// GLOBAL: XVT 0x527F1C
int g_win_mouse_min_y;
/* Highest Y g_win_mouse_pos may take; written like g_win_mouse_min_y, so it stays
 * 0. */
// GLOBAL: XVT 0x527F20
int g_win_mouse_max_y;
/* Factor each poll multiplies the X position and movement by. Only
 * win_mouse_set_scale_factors writes it, and only the uncalled
 * mouse_set_scale_factors calls that, so it stays 0 and every X a poll returns is
 * 0. */
// GLOBAL: XVT 0x527F24
int g_win_mouse_scale_x;
/* Factor for the Y position and movement, written like g_win_mouse_scale_x, so it
 * stays 0. */
// GLOBAL: XVT 0x527F28
int g_win_mouse_scale_y;
/* Point each poll warps the cursor to: the middle of the bounds on each axis,
 * (min + max) / 2. Only the two bounds setters write it, so it stays 0, 0. */
// GLOBAL: XVT 0x527F2C
struct POINT g_win_mouse_center_pos;

/* Reads the mouse once and puts the cursor back at the center. The original
 * build first handles one window message, waiting for it in GetMessageA unless
 * g_flight_input_non_blocking_msg_pump is set and PeekMessageA finds none, and
 * returns writing nothing when GetMessageA returns 0; then it reads the cursor
 * with GetCursorPos. The modern build reads Aeron's input snapshot: while input
 * is captured, the window lacks focus or there is no snapshot, it returns
 * g_win_mouse_pos times the scale factors, no movement and no buttons, and writes
 * no global; otherwise it maps the cursor with xvt_presentation_mouse_to_classic
 * and sets the three button arrays from the filtered buttons. Both then store
 * the point in g_win_mouse_cursor_pos, return its offset from g_win_mouse_prev_pos as
 * the movement, add that to g_win_mouse_pos and clamp it to the bounds, move the
 * cursor to g_win_mouse_center_pos (SetCursorPos, or xvt_presentation_warp_classic
 * in the modern build) and set g_win_mouse_prev_pos there, copy the button arrays
 * out and clear the pressed and released ones. Position and movement come back
 * multiplied by g_win_mouse_scale_x and g_win_mouse_scale_y. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4AA910
void win_mouse_poll_state(int *position_x, int *position_y, int *delta_x,
			  int *delta_y, int *button_down, int *button_pressed,
			  int *button_released)
{
	struct POINT point;

	const AeronInputSnapshot *input = Aeron_InputSnapshot();
	if (xvt_input_is_captured() || !input || !input->has_focus) {
		*position_x = g_win_mouse_pos.x * g_win_mouse_scale_x;
		*position_y = g_win_mouse_pos.y * g_win_mouse_scale_y;
		*delta_x = 0;
		*delta_y = 0;
		button_down[0] = 0;
		button_down[1] = 0;
		button_down[2] = 0;
		button_pressed[0] = 0;
		button_pressed[1] = 0;
		button_pressed[2] = 0;
		button_released[0] = 0;
		button_released[1] = 0;
		button_released[2] = 0;
		return;
	}
	if (!xvt_presentation_mouse_to_classic(input, &point.x, &point.y)) {
		point = g_win_mouse_cursor_pos;
	}
	g_win_mouse_button_down[0] =
		(xvt_input_filter_mouse_buttons(input->mouse.buttons) &
		 AERON_MOUSE_BUTTON_LEFT) != 0;
	g_win_mouse_button_down[1] =
		(xvt_input_filter_mouse_buttons(input->mouse.buttons) &
		 AERON_MOUSE_BUTTON_RIGHT) != 0;
	g_win_mouse_button_down[2] =
		(xvt_input_filter_mouse_buttons(input->mouse.buttons) &
		 AERON_MOUSE_BUTTON_MIDDLE) != 0;
	g_win_mouse_button_pressed[0] =
		(xvt_input_filter_mouse_buttons(input->mouse.pressed_buttons) &
		 AERON_MOUSE_BUTTON_LEFT) != 0;
	g_win_mouse_button_pressed[1] =
		(xvt_input_filter_mouse_buttons(input->mouse.pressed_buttons) &
		 AERON_MOUSE_BUTTON_RIGHT) != 0;
	g_win_mouse_button_pressed[2] =
		(xvt_input_filter_mouse_buttons(input->mouse.pressed_buttons) &
		 AERON_MOUSE_BUTTON_MIDDLE) != 0;
	g_win_mouse_button_released[0] =
		(xvt_input_filter_mouse_buttons(input->mouse.released_buttons) &
		 AERON_MOUSE_BUTTON_LEFT) != 0;
	g_win_mouse_button_released[1] =
		(xvt_input_filter_mouse_buttons(input->mouse.released_buttons) &
		 AERON_MOUSE_BUTTON_RIGHT) != 0;
	g_win_mouse_button_released[2] =
		(xvt_input_filter_mouse_buttons(input->mouse.released_buttons) &
		 AERON_MOUSE_BUTTON_MIDDLE) != 0;

	g_win_mouse_cursor_pos = point;
	if (!xvt_input_mouse_motion_allowed()) {
		g_win_mouse_prev_pos = point;
	}
	*delta_x = point.x - g_win_mouse_prev_pos.x;
	*delta_y = g_win_mouse_cursor_pos.y - g_win_mouse_prev_pos.y;
	g_win_mouse_pos.x += *delta_x;
	g_win_mouse_pos.y += *delta_y;
	if (g_win_mouse_pos.x < g_win_mouse_min_x) {
		g_win_mouse_pos.x = g_win_mouse_min_x;
	}
	if (g_win_mouse_pos.x > g_win_mouse_max_x) {
		g_win_mouse_pos.x = g_win_mouse_max_x;
	}
	if (g_win_mouse_pos.y < g_win_mouse_min_y) {
		g_win_mouse_pos.y = g_win_mouse_min_y;
	}
	if (g_win_mouse_pos.y > g_win_mouse_max_y) {
		g_win_mouse_pos.y = g_win_mouse_max_y;
	}
	*position_x = g_win_mouse_pos.x;
	*position_y = g_win_mouse_pos.y;

	xvt_presentation_warp_classic(g_win_mouse_center_pos.x,
				      g_win_mouse_center_pos.y);
	g_win_mouse_prev_pos = g_win_mouse_center_pos;
	button_down[0] = g_win_mouse_button_down[0];
	button_down[1] = g_win_mouse_button_down[1];
	button_down[2] = g_win_mouse_button_down[2];
	button_pressed[0] = g_win_mouse_button_pressed[0];
	button_pressed[1] = g_win_mouse_button_pressed[1];
	button_pressed[2] = g_win_mouse_button_pressed[2];
	button_released[0] = g_win_mouse_button_released[0];
	button_released[1] = g_win_mouse_button_released[1];
	button_released[2] = g_win_mouse_button_released[2];
	g_win_mouse_button_pressed[0] = 0;
	g_win_mouse_button_pressed[1] = 0;
	g_win_mouse_button_pressed[2] = 0;
	g_win_mouse_button_released[0] = 0;
	g_win_mouse_button_released[1] = 0;
	g_win_mouse_button_released[2] = 0;
	*delta_x *= g_win_mouse_scale_x;
	*delta_y *= g_win_mouse_scale_y;
	*position_x *= g_win_mouse_scale_x;
	*position_y *= g_win_mouse_scale_y;
}

/* Polls with win_mouse_poll_state and stores the scaled position, cut to 16 bits,
 * and the held buttons as 0x1 left, 0x2 right, 0x4 middle.
 * mouse_read_position_and_buttons is its only caller. */
// FUNCTION: XVT 0x4AC860
void win_mouse_poll_position_and_buttons(int16_t *buttons, int16_t *x,
					 int16_t *y)
{
	int position_x;
	int position_y;
	int button_down[3];
	int delta_x;
	int delta_y;
	int button_pressed[3];
	int button_released[3];

	win_mouse_poll_state(&position_x, &position_y, &delta_x, &delta_y,
			     button_down, button_pressed, button_released);
	*x = (int16_t)position_x;
	*y = (int16_t)position_y;
	*buttons = (int16_t)(button_down[0] |
			     2 * (button_down[1] | 2 * button_down[2]));
}

/* Polls with win_mouse_poll_state and stores the scaled movement, cut to 16 bits.
 * mouse_read_delta is its only caller. */
// FUNCTION: XVT 0x4ACA20
void win_mouse_poll_movement_delta(int16_t *delta_x, int16_t *delta_y)
{
	int polled_delta_x;
	int polled_delta_y;
	int position_x;
	int position_y;
	int button_down[3];
	int button_pressed[3];
	int button_released[3];

	win_mouse_poll_state(&position_x, &position_y, &polled_delta_x,
			     &polled_delta_y, button_down, button_pressed,
			     button_released);
	*delta_x = (int16_t)polled_delta_x;
	*delta_y = (int16_t)polled_delta_y;
}
