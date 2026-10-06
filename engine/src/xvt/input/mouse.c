#include "xvt/input/mouse.h"

#include "xvt/input/win_mouse.h"

/* Polls the mouse through win_mouse_poll_position_and_buttons: stores the scaled
 * position, cut to 16 bits, in *x and *y and returns the held buttons, 0x1
 * left, 0x2 right, 0x4 middle. Its two callers, flight_input_read and
 * xvt_flight_controls_read_local, call it only while g_flight_mouse_enabled is
 * nonzero, and only flight_input_reset_runtime_state writes that, setting 0. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4A4E70
int mouse_read_position_and_buttons(int16_t *x, int16_t *y)
{
	int16_t buttons;

	win_mouse_poll_position_and_buttons(&buttons, x, y);
	return buttons;
}

/* Polls the mouse through win_mouse_poll_movement_delta and stores the scaled
 * movement since the last poll, cut to 16 bits. Same two callers as
 * mouse_read_position_and_buttons, under the same g_flight_mouse_enabled test. */
// FUNCTION: XVT 0x4A4EA0
void mouse_read_delta(int16_t *delta_x, int16_t *delta_y)
{
	win_mouse_poll_movement_delta(delta_x, delta_y);
}
