#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_state.h"

/* Gives the mouse to one control, as a scrollbar thumb drag does: stores gateId
 * in g_frontState.mouseInputGate, after which the ungated readers in this file
 * return 0 and only the ...For readers called with that id see clicks. Returns
 * 1. Does not check whether another control holds it; a gateId of 0 opens the
 * gate. */
// FUNCTION: XVT 0x4DC1C0
int FrontendMouse_SetInputGate(int gateId)
{
	g_frontState.mouseInputGate = gateId;
	return 1;
}

/* Opens the mouse to every control: sets g_frontState.mouseInputGate to 0.
 * Returns 1. */
// FUNCTION: XVT 0x4DC1D0
int FrontendMouse_ClearInputGate(void)
{
	g_frontState.mouseInputGate = 0;
	return 1;
}

/* Returns g_frontState.mouseLeftDown, 1 while the left button is held, or 0
 * while a control holds the input gate. */
// FUNCTION: XVT 0x4DC1E0
int FrontendMouse_GetLeftDown(void)
{
	if (g_frontState.mouseInputGate != 0) {
		return 0;
	}
	return g_frontState.mouseLeftDown;
}

/* Returns g_frontState.mouseRightDown, 1 while the right button is held, or 0
 * while a control holds the input gate. */
// FUNCTION: XVT 0x4DC200
int FrontendMouse_GetRightDown(void)
{
	if (g_frontState.mouseInputGate != 0) {
		return 0;
	}
	return g_frontState.mouseRightDown;
}

/* Returns g_frontState.mouseLeftClickLatch, 1 when the left button was released
 * since the frame loop last cleared it, or 0 while a control holds the input
 * gate. */
// FUNCTION: XVT 0x4DC220
int FrontendMouse_GetLeftClick(void)
{
	if (g_frontState.mouseInputGate != 0) {
		return 0;
	}
	return g_frontState.mouseLeftClickLatch;
}

/* Returns g_frontState.mouseRightClickLatch, 1 when the right button was
 * released since the frame loop last cleared it, or 0 while a control holds the
 * input gate. */
// FUNCTION: XVT 0x4DC240
int FrontendMouse_GetRightClick(void)
{
	if (g_frontState.mouseInputGate != 0) {
		return 0;
	}
	return g_frontState.mouseRightClickLatch;
}

/* Returns g_frontState.mouseLeftClickLatch when the input gate is open or held
 * by gateId, else 0. */
// FUNCTION: XVT 0x4DC2A0
int FrontendMouse_GetLeftClickFor(int gateId)
{
	if (gateId == g_frontState.mouseInputGate ||
	    g_frontState.mouseInputGate == 0) {
		return g_frontState.mouseLeftClickLatch;
	}
	return 0;
}

/* Returns g_frontState.mouseRightClickLatch when the input gate is open or held
 * by gateId, else 0. */
// FUNCTION: XVT 0x4DC2C0
int FrontendMouse_GetRightClickFor(int gateId)
{
	if (g_frontState.mouseInputGate == gateId ||
	    g_frontState.mouseInputGate == 0) {
		return g_frontState.mouseRightClickLatch;
	}
	return 0;
}

/* Returns 1 when g_frontState.mouseInputGate equals gateId, else 0; with a
 * gateId of 0 that means the gate is open. */
// FUNCTION: XVT 0x4DC2E0
int FrontendMouse_IsGateOwner(int gateId)
{
	return g_frontState.mouseInputGate == gateId;
}

/* Returns 1 when no control holds the input gate, else 0. */
// FUNCTION: XVT 0x4DC300
int FrontendMouse_IsGateOpen(void) { return g_frontState.mouseInputGate == 0; }

/* Clears both click latches, g_frontState.mouseLeftClickLatch and
 * g_frontState.mouseRightClickLatch, so no later reader this frame sees the
 * click. Returns 1. */
// FUNCTION: XVT 0x4DC310
int FrontendMouse_ClearClicks(void)
{
	g_frontState.mouseLeftClickLatch = 0;
	g_frontState.mouseRightClickLatch = 0;
	return 1;
}
