#include "xvt/input/keyboard.h"
#include "xvt/frontend/frontend_state.h"

// FUNCTION: XVT 0x4DCA90
int Keyboard_IsKeyDown(uint8_t virtualKey) { return (g_frontState.keyState[virtualKey] & 0x80u) != 0; }

// FUNCTION: XVT 0x4DCAD0
char Keyboard_DequeueChar(void) {
	if (g_frontState.charWriteIdx == g_frontState.charReadIdx)
		return 0;

	{
		char result = g_frontState.charRingBuffer[g_frontState.charReadIdx];

		++g_frontState.charReadIdx;
		if (g_frontState.charReadIdx == 1024)
			g_frontState.charReadIdx = 0;
		return result;
	}
}

// FUNCTION: XVT 0x4DCB10
char Keyboard_PeekChar(void) {
	if (g_frontState.charWriteIdx == g_frontState.charReadIdx)
		return 0;
	return g_frontState.charRingBuffer[g_frontState.charReadIdx];
}

// FUNCTION: XVT 0x4DCB30
int Keyboard_FlushCharBuffer(void) {
	g_frontState.charReadIdx = 0;
	g_frontState.charWriteIdx = 0;
	return 1;
}

// FUNCTION: XVT 0x4DCB50
int Keyboard_DiscardChar(void) {
	if (g_frontState.charWriteIdx == g_frontState.charReadIdx) {
		return 0;
	}
	if (++g_frontState.charReadIdx == 1024) {
		g_frontState.charReadIdx = 0;
	}
	return 1;
}
