#include "xvt_runtime/input/capture.h"
#include "aeron/aeron.h"
#include "aeron/compat/dinput.h"
#include "aeron/compat/host.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/player/player.h"
#include "xvt/input/dinput.h"
#include "xvt/input/keyboard.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/input/controller_mapping.h"
#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/input/keyboard_mapping.h"
#include "xvt_runtime/input/mouse_flight.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/flight_sim.h"
#include "xvt_runtime/runtime/flight_task.h"
#include <string.h>
static bool g_captured, g_rendererTab;
static uint8_t g_blockedKeys[AERON_KEY_COUNT];
static uint32_t g_blockedMouse;
static uint64_t g_mouseResumeFrame = UINT64_MAX;
static bool g_mouseReleased, g_mouseCaptureFailed, g_mouseSession;
static int g_mouseContext = -1;
static XvtMouseOptions g_mouseOptions;

enum { MOUSE_CAPTURE_KEY = AERON_KEY_A + ('m' - 'a') };

enum { MOUSE_CONTROL_SHIP, MOUSE_CONTROL_EXTERNAL, MOUSE_CONTROL_MAP };

void XvtInput_FlushRawKeyboard(void) {
	if (g_dinputKeyboardDevice) {
		uint32_t count = UINT32_MAX;
		g_dinputKeyboardDevice->lpVtbl->GetDeviceData(g_dinputKeyboardDevice, sizeof(DIDEVICEOBJECTDATA),
													  NULL, &count, 0);
	}
	g_dinputShiftDown = g_dinputCtrlDown = g_dinputAltDown = 0;
	g_keyReady = 0;
	g_lastKeyCode = 0;
	Keyboard_FlushCharBuffer();
}

static void XvtInput_ApplyKeySuppression(void) {
	for (int key = 0; key < AERON_KEY_COUNT; ++key)
		AeronCompat_SetKeySuppressed(key, g_captured || g_blockedKeys[key] ||
											  (key == AERON_KEY_TAB && g_rendererTab));
}

void XvtInput_SuppressKey(int key) {
	if ((unsigned)key >= AERON_KEY_COUNT)
		return;
	g_blockedKeys[key] = 1;
	XvtInput_ApplyKeySuppression();
}

void XvtInput_BlockHeldKeys(void) {
	const AeronInputSnapshot* input = Aeron_InputSnapshot();
	if (input)
		for (int key = 0; key < AERON_KEY_COUNT; ++key)
			g_blockedKeys[key] |= input->key_down[key];
	XvtInput_ApplyKeySuppression();
}

void XvtInput_FlushKeyboard(void) {
	XvtKeyboardMapping_Suspend();
	XvtInput_BlockHeldKeys();
	XvtInput_FlushRawKeyboard();
}

void XvtInput_SetCaptured(bool capture) {
	if (capture == g_captured)
		return;
	g_captured = capture;
	const AeronInputSnapshot* input = Aeron_InputSnapshot();
	if (input) {
		for (int key = 0; key < AERON_KEY_COUNT; ++key)
			g_blockedKeys[key] = input->key_down[key];
		g_blockedMouse = input->mouse.buttons;
		g_mouseResumeFrame = input->frame_id;
	}
	XvtInput_FlushKeyboard();
	g_actionKey = 0;
	g_ctrlAxisX = g_ctrlAxisY = 0;
	g_keyMods = 0;
	g_mouseButtons = 0;
	g_flightMouseDeltaX = g_flightMouseDeltaY = 0;
	XvtFlightControls_Reset();
	XvtMouseFlight_Reset();
	if (capture)
		Aeron_SetRelativeMouseMode(0);
	if (capture)
		XvtControllerMapping_Suspend();
	FlightInput_ResetControlState();
	XvtInput_ApplyKeySuppression();
}

void XvtInput_BeginCaptureFrame(const AeronInputSnapshot* input, bool capture) {
	if (input) {
		for (int key = 0; key < AERON_KEY_COUNT; ++key)
			if (!input->key_down[key] && !input->key_released[key])
				g_blockedKeys[key] = 0;
		g_blockedMouse &= input->mouse.buttons | input->mouse.released_buttons;
	}
	XvtInput_SetCaptured(capture);
	XvtInput_ApplyKeySuppression();
	if (capture)
		XvtInput_FlushKeyboard();
}

bool XvtInput_IsCaptured(void) { return g_captured; }

bool XvtInput_MouseMotionAllowed(void) {
	const AeronInputSnapshot* input = Aeron_InputSnapshot();
	return !g_captured && input && input->has_focus && input->frame_id != g_mouseResumeFrame;
}

uint32_t XvtInput_FilterMouseButtons(uint32_t buttons) { return g_captured ? 0 : buttons & ~g_blockedMouse; }

void XvtInput_SuppressRendererTab(bool suppress) {
	g_rendererTab = suppress;
	AeronCompat_SetKeySuppressed(AERON_KEY_TAB, g_captured || g_blockedKeys[AERON_KEY_TAB] || g_rendererTab);
}

void XvtInput_ResetCapture(void) {
	g_captured = g_rendererTab = false;
	g_blockedMouse = 0;
	g_mouseResumeFrame = UINT64_MAX;
	memset(g_blockedKeys, 0, sizeof g_blockedKeys);
	XvtInput_ApplyKeySuppression();
	g_mouseReleased = g_mouseCaptureFailed = g_mouseSession = false;
	g_mouseContext = -1;
	memset(&g_mouseOptions, 0, sizeof g_mouseOptions);
	XvtMouseFlight_Reset();
	Aeron_SetRelativeMouseMode(0);
}

bool XvtInput_MouseFlightAllowed(void) {
	return g_mouseOptions.mouse_flight_enabled && XvtFlightTask_IsActive() && !XvtFlightTask_IsLoading() &&
		   !XvtFlightSim_IsPaused() && !XvtDialog_IsActive() && !Aeron_DebugUiVisible() &&
		   XvtInput_MouseMotionAllowed() && !g_mouseReleased && !g_mouseCaptureFailed;
}

void XvtInput_UpdateMouseCapture(const AeronInputSnapshot* input) {
	const XvtSettings* settings = XvtConfig_Settings();
	if (!settings)
		return;
	if (memcmp(&g_mouseOptions, &settings->mouse, sizeof g_mouseOptions)) {
		g_mouseOptions = settings->mouse;
		XvtMouseFlight_SetOptions(&g_mouseOptions);
		g_mouseCaptureFailed = false;
		g_blockedMouse |= input ? input->mouse.buttons : 0;
		g_mouseResumeFrame = input ? input->frame_id : UINT64_MAX;
	}
	bool session = XvtFlightTask_IsActive() && !XvtFlightTask_IsLoading();
	if (session != g_mouseSession) {
		g_mouseSession = session;
		g_mouseReleased = g_mouseCaptureFailed = false;
		g_mouseContext = -1;
		XvtMouseFlight_Reset();
	}
	if (session && (unsigned)g_localPlayer < 8) {
		int context = g_players[g_localPlayer].mapCameraState                   ? MOUSE_CONTROL_MAP
					  : g_players[g_localPlayer].viewState.externalCameraActive ? MOUSE_CONTROL_EXTERNAL
																				: MOUSE_CONTROL_SHIP;
		if (context != g_mouseContext) {
			g_mouseContext = context;
			XvtMouseFlight_Reset();
		}
	}
	if (session && g_mouseOptions.mouse_flight_enabled && input && input->has_focus && !g_captured &&
		!XvtDialog_IsActive() && !Aeron_DebugUiVisible()) {
		bool chord = !g_blockedKeys[MOUSE_CAPTURE_KEY] &&
					 XvtKeyboardMapping_Trigger(input, XVT_KEYBOARD_SHORTCUT_MOUSE) >= 0;
		bool click = g_mouseReleased && input->mouse.inside_content && input->mouse.pressed_buttons;
		if (chord || click) {
			g_mouseReleased = chord ? !g_mouseReleased : false;
			g_mouseCaptureFailed = false;
			g_blockedKeys[MOUSE_CAPTURE_KEY] |= chord;
			g_blockedMouse |= input->mouse.buttons | input->mouse.pressed_buttons;
			g_mouseResumeFrame = input->frame_id;
			XvtMouseFlight_Reset();
			XvtInput_ApplyKeySuppression();
		}
	}
	bool capture = XvtInput_MouseFlightAllowed();
	bool was_relative = Aeron_RelativeMouseMode() != 0;
	if (capture != was_relative) {
		if (!Aeron_SetRelativeMouseMode(capture) && capture) {
			g_mouseCaptureFailed = true;
			g_mouseReleased = true;
			Aeron_LogError("xvt.input", "Cannot capture flight mouse; retry with Ctrl+Alt+M");
		}
		XvtMouseFlight_Reset();
		g_blockedMouse |= input ? input->mouse.buttons : 0;
	}
	if (g_mouseOptions.mouse_flight_enabled && session)
		Aeron_SetHostCursorVisible(!Aeron_RelativeMouseMode() && !XvtDialog_IsActive());
	else if (was_relative)
		Aeron_SetHostCursorVisible(0);
	XvtMouseFlight_Pump();
}
