#include "xvt_runtime/input/flight_controls.h"
#include "aeron/aeron.h"
#include "aeron/debug.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/input/dinput.h"
#include "xvt/input/mouse.h"
#include "xvt/math/math2.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/controller_mapping.h"
#include "xvt_runtime/input/input_bridge.h"
#include "xvt_runtime/input/keyboard_mapping.h"
#include "xvt_runtime/input/mouse_flight.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/flight_sim.h"
#include "xvt_runtime/runtime/flight_task.h"
#include "xvt_runtime/runtime/movie_task.h"
#include "xvt_runtime/runtime/port.h"
#include "xvt_runtime/runtime/resync_task.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/player_timing.h"
#include <string.h>
int16_t g_xvtControlRoll;

static struct {
	bool valid;
	uint16_t position;
	uint32_t generation, signature;
	int object;
} g_throttleBaseline;

static void XvtFlightControls_ResetThrottle(void) { g_throttleBaseline.valid = false; }

void XvtFlightControls_Reset(void) {
	XvtPlayerTiming_ResetControls();
	g_xvtControlRoll = 0;
	XvtFlightControls_ResetThrottle();
}

bool XvtFlightControls_ThrottleEligible(unsigned player) {
	if (player >= 8 || !g_players[player].connectedFlag || g_flightMissionState.missionEndPending ||
		(g_flightRuntimeStateInitialized > 1 && g_dormantFlightRegionSessionEarlyReturnFlag) ||
		g_players[player].regionSessionId || g_players[player].hyperspacePhase ||
		g_players[player].mapCameraState || g_players[player].viewState.playerInputBlocked ||
		g_players[player].msgTypeId != FLIGHT_CHAT_RECIPIENT_INACTIVE)
		return false;
	int index = g_players[player].objectIndex;
	if (!g_objectTable || index < 0 || index >= g_regionMainObjectSlotEnd)
		return false;
	const ObjectRecord* object = &g_objectTable[index];
	return object->objectType && object->objectSignature == g_players[player].boundObjectSignature &&
		   object->mobj && object->mobj->pCraft;
}

static bool XvtFlightControls_LocalThrottleEligible(void) {
	const AeronInputSnapshot* input = Aeron_InputSnapshot();
	return input && input->has_focus && !XvtInput_IsCaptured() && !Aeron_DebugUiVisible() &&
		   XvtInput_ReconcileKeyboard() != XVT_KEYBOARD_BLOCKED && XvtFlightTask_IsActive() &&
		   !XvtFlightTask_IsLoading() && !XvtFlightSim_IsPaused() && !XvtDialog_IsActive() &&
		   !XvtMovieTask_IsActive() && !XvtResync_IsActive() &&
		   XvtFlightControls_ThrottleEligible((unsigned)g_localPlayer);
}

void XvtFlightControls_UpdateThrottleContext(void) {
	if (!XvtFlightControls_LocalThrottleEligible())
		XvtFlightControls_ResetThrottle();
}

void XvtFlightControls_SampleThrottle(FlightInputFrameRecord* record) {
	uint16_t position;
	uint32_t generation;
	record->flags = 0;
	record->throttle = 0;
	bool pause =
		record->key == FLIGHT_KEY_ALT_P && g_flightPlayerCount == 1 && !XvtPort_NetworkRequiresProgress();
	if (pause || !XvtFlightControls_LocalThrottleEligible() ||
		!XvtControllerMapping_ThrottleSample(&position, &generation)) {
		XvtFlightControls_ResetThrottle();
		return;
	}
	int object = g_players[g_localPlayer].objectIndex;
	uint32_t signature = g_players[g_localPlayer].boundObjectSignature;
	if (!g_throttleBaseline.valid || g_throttleBaseline.generation != generation ||
		g_throttleBaseline.object != object || g_throttleBaseline.signature != signature) {
		g_throttleBaseline.valid = true;
		g_throttleBaseline.position = position;
		g_throttleBaseline.generation = generation;
		g_throttleBaseline.object = object;
		g_throttleBaseline.signature = signature;
		return;
	}
	int delta = (int)position - g_throttleBaseline.position;
	if (delta < 0)
		delta = -delta;
	/* 0.1% jitter threshold; small movements accumulate against the last accepted position. */
	if (delta && (delta >= 66 || position == 0 || position == UINT16_MAX)) {
		g_throttleBaseline.position = position;
		record->flags = XVT_INPUT_THROTTLE_PRESENT;
		record->throttle = position;
	}
}

void XvtFlightControls_ApplyThrottle(unsigned player, const FlightInputFrameRecord* input) {
	if ((input->flags & XVT_INPUT_THROTTLE_PRESENT) && XvtFlightControls_ThrottleEligible(player))
		g_objectTable[g_players[player].objectIndex].mobj->pCraft->throttleSpeed = input->throttle;
}

uint16_t XvtFlightControls_ReadLocal(void) {
	XvtKeyboardRoute keyboard = XvtInput_ReconcileKeyboard();
	g_ctrlAxisX = g_ctrlAxisY = g_xvtControlRoll = 0;
	g_keyMods = g_mouseButtons = g_actionKey = 0;
	g_flightMouseDeltaX = g_flightMouseDeltaY = 0;
	if (keyboard == XVT_KEYBOARD_BLOCKED) {
		XvtFlightControls_Reset();
		return 0;
	}
	g_ctrlAxisX = (int16_t)XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW);
	g_ctrlAxisY = (int16_t)XvtControllerMapping_Axis(XVT_INPUT_AXIS_PITCH);
	g_xvtControlRoll = (int16_t)XvtControllerMapping_Axis(XVT_INPUT_AXIS_ROLL);
	g_keyMods = XvtControllerMapping_Modifiers();
	if (keyboard == XVT_KEYBOARD_GAMEPLAY)
		g_keyMods |= XvtKeyboardMapping_ReadButtons();
	if (XvtConfig_Settings()->mouse.mouse_flight_enabled) {
		if (XvtMouseFlight_Sample()) {
			int yaw, pitch, roll;
			XvtMouseFlight_GetAxes(&yaw, &pitch, &roll);
			if (yaw)
				g_ctrlAxisX = (int16_t)yaw;
			if (pitch)
				g_ctrlAxisY = (int16_t)pitch;
			if (roll)
				g_xvtControlRoll = (int16_t)roll;
			g_keyMods |= XvtMouseFlight_ButtonsMask();
		}
	} else if (g_joystickEnabled) {
		g_mouseButtons = (uint16_t)Mouse_ReadPositionAndButtons(&g_flightMouseX, &g_flightMouseY);
		Mouse_ReadDelta(&g_flightMouseDeltaX, &g_flightMouseDeltaY);
		if (g_flightMouseDeltaX < -191)
			g_flightMouseDeltaX = -191;
		if (g_flightMouseDeltaX > 191)
			g_flightMouseDeltaX = 191;
		if (g_flightMouseDeltaY < -127)
			g_flightMouseDeltaY = -127;
		if (g_flightMouseDeltaY > 127)
			g_flightMouseDeltaY = 127;
	}
	uint16_t key = keyboard == XVT_KEYBOARD_GAMEPLAY ? XvtKeyboardMapping_ReadKey()
													 : (DInput_HasKeyReady() ? DInput_GetKey() : 0);
	if (!key)
		key = XvtControllerMapping_ReadKey();
	if (!key)
		key = XvtMouseFlight_ReadKey();
	g_actionKey = key;
	return g_actionKey;
}

void XvtFlightControls_EncodeAxes(uint8_t* bytes, const FlightInputFrameRecord* input) {
	bytes[0] = ((uint8_t)input->axisX & 0xfeu) | (input->keyMods & 1u);
	bytes[1] = ((uint8_t)input->axisY & 0xfeu) | ((input->keyMods >> 1) & 1u);
	bytes[2] = (uint8_t)input->axisR & 0xfeu;
}

void XvtFlightControls_DecodeAxes(const uint8_t* bytes, FlightInputFrameRecord* input) {
	input->axisX = (int8_t)(bytes[0] & 0xfeu);
	input->axisY = (int8_t)(bytes[1] & 0xfeu);
	input->axisR = (int8_t)(bytes[2] & 0xfeu);
	input->keyMods = (bytes[0] & 1u) | ((bytes[1] & 1u) << 1);
}

int16_t XvtFlightControls_RollStep(unsigned player, uint16_t roll_rate, int16_t modifier_step) {
	if (!g_xvtControlRoll) {
		XvtPlayerTiming_Clear(player, XVT_PLAYER_ROLL);
		return modifier_step;
	}
	int raw = g_xvtControlRoll * 120;
	unsigned magnitude = (unsigned)(raw < 0 ? -raw : raw);
	unsigned whole = roll_rate / 0x3800;
	uint16_t fraction = (uint16_t)MATH2_divide(roll_rate % 0x3800, 0x3800);
	int target = (int)(magnitude * whole + MATH2_fraction(magnitude, fraction));
	if (raw < 0)
		target = -target;
	int step = XvtFlightTiming_IsUnlocked()
				   ? XvtPlayerTiming_Scale(player, XVT_PLAYER_ROLL, (int16_t)target, g_elapsedTicks, 236)
				   : Player_ScaleControlStepByElapsedTicks((int16_t)target);
	int limit = Player_ScaleControlStepByElapsedTicks(
		(int16_t)(127 * 120 * whole + MATH2_fraction(127 * 120, fraction)));
	step += modifier_step;
	if (step > limit)
		step = limit;
	if (step < -limit)
		step = -limit;
	return (int16_t)step;
}

void XvtFlightControls_SampleRecorded(FlightInputFrameRecord* input) {
	enum {
		AXIS_QUANTIZATION_MASK = 0xfe,
		RECORDED_MODIFIERS = 3,
		YAW_AXIS_SCALE = 120,
		PITCH_AXIS_SCALE = 50,
		MOUSE_YAW_SCALE = 128,
		MOUSE_PITCH_SCALE = 64,
		MAX_AXIS = INT8_MAX & AXIS_QUANTIZATION_MASK
	};

	FlightInput_Read(-2);
	memset(input, 0, sizeof *input);
	input->key = (uint8_t)g_actionKey;
	input->axisX = (int8_t)(g_ctrlAxisX & AXIS_QUANTIZATION_MASK);
	input->axisY = (int8_t)(g_ctrlAxisY & AXIS_QUANTIZATION_MASK);
	input->axisR = (int8_t)(g_xvtControlRoll & AXIS_QUANTIZATION_MASK);
	input->keyMods = (g_keyMods | g_mouseButtons) & RECORDED_MODIFIERS;
	XvtFlightControls_SampleThrottle(input);
	if (g_joystickEnabled) {
		int yaw = g_flightMouseDeltaX * MOUSE_YAW_SCALE / YAW_AXIS_SCALE,
			pitch = g_flightMouseDeltaY * MOUSE_PITCH_SCALE / PITCH_AXIS_SCALE;
		if (yaw)
			input->axisX = (int8_t)((yaw < INT8_MIN   ? INT8_MIN
									 : yaw > MAX_AXIS ? MAX_AXIS
													  : yaw) &
									AXIS_QUANTIZATION_MASK);
		if (pitch)
			input->axisY = (int8_t)((pitch < INT8_MIN   ? INT8_MIN
									 : pitch > MAX_AXIS ? MAX_AXIS
														: pitch) &
									AXIS_QUANTIZATION_MASK);
	}
}

void XvtFlightControls_Recover(void) {
	XvtInput_FlushKeyboard();
	XvtControllerMapping_ReleaseCommands();
	XvtMouseFlight_DiscardPending();
	g_actionKey = 0;
	XvtFlightControls_ResetThrottle();
}
