#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_state.h"

// FUNCTION: XVT 0x4DC1C0
int FrontendMouse_SetInputGate(int gateId) {
	g_frontState.mouseInputGate = gateId;
	return 1;
}

// FUNCTION: XVT 0x4DC1D0
int FrontendMouse_ClearInputGate(void) {
	g_frontState.mouseInputGate = 0;
	return 1;
}

// FUNCTION: XVT 0x4DC1E0
int FrontendMouse_GetLeftDown(void) {
	if (g_frontState.mouseInputGate != 0) {
		return 0;
	}
	return g_frontState.mouseLeftDown;
}

// FUNCTION: XVT 0x4DC200
int FrontendMouse_GetRightDown(void) {
	if (g_frontState.mouseInputGate != 0) {
		return 0;
	}
	return g_frontState.mouseRightDown;
}

// FUNCTION: XVT 0x4DC220
int FrontendMouse_GetLeftClick(void) {
	if (g_frontState.mouseInputGate != 0) {
		return 0;
	}
	return g_frontState.mouseClickLatch;
}

// FUNCTION: XVT 0x4DC240
int FrontendMouse_GetRightClick(void) {
	if (g_frontState.mouseInputGate != 0) {
		return 0;
	}
	return g_frontState.mouseRightClickLatch;
}

// FUNCTION: XVT 0x4DC2A0
int FrontendMouse_GetLeftClickFor(int gateId) {
	if (gateId == g_frontState.mouseInputGate || g_frontState.mouseInputGate == 0) {
		return g_frontState.mouseClickLatch;
	}
	return 0;
}

// FUNCTION: XVT 0x4DC2C0
int FrontendMouse_GetRightClickFor(int gateId) {
	if (g_frontState.mouseInputGate == gateId || g_frontState.mouseInputGate == 0) {
		return g_frontState.mouseRightClickLatch;
	}
	return 0;
}

// FUNCTION: XVT 0x4DC2E0
int FrontendMouse_IsGateOwner(int gateId) { return g_frontState.mouseInputGate == gateId; }

// FUNCTION: XVT 0x4DC300
int FrontendMouse_IsGateOpen(void) { return g_frontState.mouseInputGate == 0; }

// FUNCTION: XVT 0x4DC310
int FrontendMouse_ClearClicks(void) {
	g_frontState.mouseClickLatch = 0;
	g_frontState.mouseRightClickLatch = 0;
	return 1;
}
