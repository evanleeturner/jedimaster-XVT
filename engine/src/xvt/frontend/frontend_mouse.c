#include "xvt/frontend/frontend_mouse.h"

#include "xvt/frontend/frontend_state.h"

/* Gives the mouse to one control, as a scrollbar thumb drag does: stores gate_id
 * in g_front_state.mouse_input_gate, after which the ungated readers in this file
 * return 0 and only the ...For readers called with that id see clicks. Returns
 * 1. Does not check whether another control holds it; a gate_id of 0 opens the
 * gate. */
// FUNCTION: XVT 0x4DC1C0
int frontend_mouse_set_input_gate(int gate_id)
{
	g_front_state.mouse_input_gate = gate_id;
	return 1;
}

/* Opens the mouse to every control: sets g_front_state.mouse_input_gate to 0.
 * Returns 1. */
// FUNCTION: XVT 0x4DC1D0
int frontend_mouse_clear_input_gate(void)
{
	g_front_state.mouse_input_gate = 0;
	return 1;
}

/* Returns g_front_state.mouse_left_down, 1 while the left button is held, or 0
 * while a control holds the input gate. */
// FUNCTION: XVT 0x4DC1E0
int frontend_mouse_get_left_down(void)
{
	if (g_front_state.mouse_input_gate != 0) {
		return 0;
	}
	return g_front_state.mouse_left_down;
}

/* Returns g_front_state.mouse_right_down, 1 while the right button is held, or 0
 * while a control holds the input gate. */
// FUNCTION: XVT 0x4DC200
int frontend_mouse_get_right_down(void)
{
	if (g_front_state.mouse_input_gate != 0) {
		return 0;
	}
	return g_front_state.mouse_right_down;
}

/* Returns g_front_state.mouse_left_click_latch, 1 when the left button was released
 * since the frame loop last cleared it, or 0 while a control holds the input
 * gate. */
// FUNCTION: XVT 0x4DC220
int frontend_mouse_get_left_click(void)
{
	if (g_front_state.mouse_input_gate != 0) {
		return 0;
	}
	return g_front_state.mouse_left_click_latch;
}

/* Returns g_front_state.mouse_right_click_latch, 1 when the right button was
 * released since the frame loop last cleared it, or 0 while a control holds the
 * input gate. */
// FUNCTION: XVT 0x4DC240
int frontend_mouse_get_right_click(void)
{
	if (g_front_state.mouse_input_gate != 0) {
		return 0;
	}
	return g_front_state.mouse_right_click_latch;
}

/* Returns g_front_state.mouse_left_click_latch when the input gate is open or held
 * by gate_id, else 0. */
// FUNCTION: XVT 0x4DC2A0
int frontend_mouse_get_left_click_for(int gate_id)
{
	if (gate_id == g_front_state.mouse_input_gate ||
	    g_front_state.mouse_input_gate == 0) {
		return g_front_state.mouse_left_click_latch;
	}
	return 0;
}

/* Returns g_front_state.mouse_right_click_latch when the input gate is open or held
 * by gate_id, else 0. */
// FUNCTION: XVT 0x4DC2C0
int frontend_mouse_get_right_click_for(int gate_id)
{
	if (g_front_state.mouse_input_gate == gate_id ||
	    g_front_state.mouse_input_gate == 0) {
		return g_front_state.mouse_right_click_latch;
	}
	return 0;
}

/* Returns 1 when g_front_state.mouse_input_gate equals gate_id, else 0; with a
 * gate_id of 0 that means the gate is open. */
// FUNCTION: XVT 0x4DC2E0
int frontend_mouse_is_gate_owner(int gate_id)
{
	return g_front_state.mouse_input_gate == gate_id;
}

/* Returns 1 when no control holds the input gate, else 0. */
// FUNCTION: XVT 0x4DC300
int frontend_mouse_is_gate_open(void)
{
	return g_front_state.mouse_input_gate == 0;
}

/* Clears both click latches, g_front_state.mouse_left_click_latch and
 * g_front_state.mouse_right_click_latch, so no later reader this frame sees the
 * click. Returns 1. */
// FUNCTION: XVT 0x4DC310
int frontend_mouse_clear_clicks(void)
{
	g_front_state.mouse_left_click_latch = 0;
	g_front_state.mouse_right_click_latch = 0;
	return 1;
}
