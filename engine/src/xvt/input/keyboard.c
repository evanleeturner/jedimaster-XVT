#include "xvt/input/keyboard.h"

#include "xvt/frontend/frontend_state.h"

/* Returns 1 when the 0x80 bit of g_front_state.key_state for the virtual-key
 * code is set, else 0. xvt_input_update fills that table on each front-end
 * frame. */
// FUNCTION: XVT 0x4DCA90
int keyboard_is_key_down(uint8_t virtual_key)
{
	return (g_front_state.key_state[virtual_key] & 0x80u) != 0;
}

/* Takes the oldest character from the front end's typed-character ring,
 * g_front_state.char_ring_buffer: returns it and advances
 * g_front_state.char_read_idx, wrapping it to 0 at 1024. Returns 0 when the ring
 * is empty. */
// FUNCTION: XVT 0x4DCAD0
char keyboard_dequeue_char(void)
{
	if (g_front_state.char_write_idx == g_front_state.char_read_idx) {
		return 0;
	}

	{
		char result =
			g_front_state
				.char_ring_buffer[g_front_state.char_read_idx];

		++g_front_state.char_read_idx;
		if (g_front_state.char_read_idx == 1024) {
			g_front_state.char_read_idx = 0;
		}
		return result;
	}
}

/* Returns the oldest character in g_front_state.char_ring_buffer without taking
 * it, or 0 when the ring is empty. */
// FUNCTION: XVT 0x4DCB10
char keyboard_peek_char(void)
{
	if (g_front_state.char_write_idx == g_front_state.char_read_idx) {
		return 0;
	}
	return g_front_state.char_ring_buffer[g_front_state.char_read_idx];
}

/* Empties the typed-character ring by setting g_front_state.char_read_idx and
 * g_front_state.char_write_idx to 0. Returns 1. */
// FUNCTION: XVT 0x4DCB30
int keyboard_flush_char_buffer(void)
{
	g_front_state.char_read_idx = 0;
	g_front_state.char_write_idx = 0;
	return 1;
}

/* Drops the oldest character from the typed-character ring, advancing
 * g_front_state.char_read_idx and wrapping it to 0 at 1024, and returns 1; returns
 * 0 when the ring is empty. */
// FUNCTION: XVT 0x4DCB50
int keyboard_discard_char(void)
{
	if (g_front_state.char_write_idx == g_front_state.char_read_idx) {
		return 0;
	}
	if (++g_front_state.char_read_idx == 1024) {
		g_front_state.char_read_idx = 0;
	}
	return 1;
}
