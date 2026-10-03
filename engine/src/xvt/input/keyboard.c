#include "xvt/input/keyboard.h"
#include "xvt/frontend/frontend_state.h"

/* Returns 1 when the 0x80 bit of g_frontState.keyState for the virtual-key code
 * is set, else 0. The original build fills that table with GetKeyboardState on
 * each front-end frame, the modern build in XvtInput_Update. */
// FUNCTION: XVT 0x4DCA90
int Keyboard_IsKeyDown(uint8_t virtualKey)
{
	return (g_frontState.keyState[virtualKey] & 0x80u) != 0;
}

/* Takes the oldest character from the front end's typed-character ring,
 * g_frontState.charRingBuffer: returns it and advances
 * g_frontState.charReadIdx, wrapping it to 0 at 1024. Returns 0 when the ring
 * is empty. */
// FUNCTION: XVT 0x4DCAD0
char Keyboard_DequeueChar(void)
{
	if (g_frontState.charWriteIdx == g_frontState.charReadIdx) {
		return 0;
	}

	{
		char result =
			g_frontState.charRingBuffer[g_frontState.charReadIdx];

		++g_frontState.charReadIdx;
		if (g_frontState.charReadIdx == 1024) {
			g_frontState.charReadIdx = 0;
		}
		return result;
	}
}

/* Returns the oldest character in g_frontState.charRingBuffer without taking
 * it, or 0 when the ring is empty. */
// FUNCTION: XVT 0x4DCB10
char Keyboard_PeekChar(void)
{
	if (g_frontState.charWriteIdx == g_frontState.charReadIdx) {
		return 0;
	}
	return g_frontState.charRingBuffer[g_frontState.charReadIdx];
}

/* Empties the typed-character ring by setting g_frontState.charReadIdx and
 * g_frontState.charWriteIdx to 0. Returns 1. */
// FUNCTION: XVT 0x4DCB30
int Keyboard_FlushCharBuffer(void)
{
	g_frontState.charReadIdx = 0;
	g_frontState.charWriteIdx = 0;
	return 1;
}

/* Drops the oldest character from the typed-character ring, advancing
 * g_frontState.charReadIdx and wrapping it to 0 at 1024, and returns 1; returns
 * 0 when the ring is empty. */
// FUNCTION: XVT 0x4DCB50
int Keyboard_DiscardChar(void)
{
	if (g_frontState.charWriteIdx == g_frontState.charReadIdx) {
		return 0;
	}
	if (++g_frontState.charReadIdx == 1024) {
		g_frontState.charReadIdx = 0;
	}
	return 1;
}
