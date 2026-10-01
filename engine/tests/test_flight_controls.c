/* Checks local flight input (xvt_runtime/input/flight_controls.h) against the promises in its header:
 * packing recorded axes, which player may use the throttle lever and what applying a recorded lever
 * does, the roll step's limits, and what Reset, Recover and a blocked read clear. The test builds its own
 * world (an object table with one craft) and sets the game's input globals and Aeron's input snapshot
 * itself; every case starts from that world with the local player eligible and the window focused.
 *
 * Not checked here: ReadLocal on an open keyboard route and SampleRecorded need loaded settings and the
 * game's DirectInput keyboard device, and a lever position is only sent during a running flight. */
#include "aeron/aeron.h"
#include "aeron/compat/host.h"
#include "test_assert.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/controller_mapping.h"
#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/input/keyboard_mapping.h"
#include "xvt_runtime/timing/flight_timing.h"

#include <stdlib.h>
#include <string.h>

enum { PLAYER = 3, SLOT = 1, SIGNATURE = 0x0155 };

static ObjectRecord g_testObjects[2];
static MobileObject g_testMobiles[2];
static CraftData g_testCraft[1];

static AeronInputSnapshot* Host(void) { return (AeronInputSnapshot*)Aeron_InputSnapshot(); }

/* Player PLAYER flies the craft in main slot SLOT, bound to it by signature; nothing is in the way. */
static void World(void) {
	memset(g_testObjects, 0, sizeof g_testObjects);
	memset(g_testMobiles, 0, sizeof g_testMobiles);
	memset(g_testCraft, 0, sizeof g_testCraft);
	memset(g_players, 0, sizeof g_players);
	memset(&g_flightMissionState, 0, sizeof g_flightMissionState);
	g_flightRuntimeStateInitialized = 0;
	g_dormantFlightRegionSessionEarlyReturnFlag = 0;
	g_objectTable = g_testObjects;
	g_regionMainObjectSlotEnd = 2;
	g_testObjects[SLOT].objectType = 1;
	g_testObjects[SLOT].objectSignature = SIGNATURE;
	g_testObjects[SLOT].mobj = &g_testMobiles[SLOT];
	g_testMobiles[SLOT].pCraft = &g_testCraft[0];
	for (int i = 0; i < 8; ++i)
		g_players[i].objectIndex = -1;
	g_players[PLAYER].connectedFlag = 1;
	g_players[PLAYER].objectIndex = SLOT;
	g_players[PLAYER].boundObjectSignature = SIGNATURE;
	g_players[PLAYER].msgTypeId = FLIGHT_CHAT_RECIPIENT_INACTIVE;
	g_localPlayer = PLAYER;
	g_flightPlayerCount = 1;

	XvtInput_ResetCapture();
	AeronInputSnapshot* host = Host();
	uint64_t frame = host->frame_id;
	memset(host, 0, sizeof *host);
	host->frame_id = frame + 1;
	host->has_focus = 1;
}

static void SetGameInput(void) {
	g_actionKey = 0x41;
	g_ctrlAxisX = 12;
	g_ctrlAxisY = -12;
	g_xvtControlRoll = 9;
	g_keyMods = 3;
	g_mouseButtons = 1;
	g_flightMouseDeltaX = 4;
	g_flightMouseDeltaY = -4;
}

/* A gamepad model with yaw on the left stick and fire on the west button, and that gamepad connected in
 * Aeron's snapshot with the stick pushed and fire held after a first frame with it released. */
static void ControllerFiring(void) {
	static XvtControllerOptions options;
	memset(&options, 0, sizeof options);
	XvtControllerModel* model = &options.models[0];
	memcpy(model->guid, "0123456789abcdef0123456789abcdea", sizeof model->guid);
	model->kind = AERON_CONTROLLER_KIND_GAMEPAD;
	XvtControllerOptions_ClearProfile(&model->profile, AERON_CONTROLLER_KIND_GAMEPAD);
	model->profile.mapping.axes[XVT_INPUT_AXIS_YAW].source = AERON_GAMEPAD_AXIS_LEFTX;
	model->profile.bindings[0].source.kind = AERON_CONTROLLER_DIGITAL_BUTTON;
	model->profile.bindings[0].source.index = AERON_GAMEPAD_BUTTON_WEST;
	model->profile.bindings[0].source.threshold = 0.5f;
	model->profile.bindings[0].action = XVT_INPUT_ACTION_FIRE_WEAPON;
	model->profile.binding_count = 1;
	options.count = 1;
	XvtControllerMapping_Init(&options);

	AeronControllerSnapshot* pad = &Host()->controllers[0];
	pad->connected = 1;
	pad->kind = AERON_CONTROLLER_KIND_GAMEPAD;
	pad->instance_id = 5;
	memcpy(pad->guid, model->guid, sizeof pad->guid);
	pad->gamepad_available_axes = 1u << AERON_GAMEPAD_AXIS_LEFTX;
	pad->gamepad_available_buttons = 1u << AERON_GAMEPAD_BUTTON_WEST;
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = 16384;
	XvtControllerMapping_Update(Host());
	++Host()->frame_id;
	pad->gamepad_buttons = 1u << AERON_GAMEPAD_BUTTON_WEST;
	XvtControllerMapping_Update(Host());
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 1);
	XVT_ASSERT_TRUE(XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW) != 0);
}

static void CheckEncodeDecode(void) {
	for (int axis = -128; axis <= 127; ++axis)
		for (int mods = 0; mods < 4; ++mods) {
			FlightInputFrameRecord in, out;
			memset(&in, 0, sizeof in);
			in.axisX = (int8_t)axis;
			in.axisY = (int8_t)(-1 - axis);
			in.axisR = (int8_t)axis;
			in.keyMods = (uint8_t)mods;
			uint8_t bytes[XVT_FLIGHT_AXIS_BYTES + 1];
			memset(bytes, 0xC3, sizeof bytes);
			XvtFlightControls_EncodeAxes(bytes, &in);
			XVT_ASSERT_INT_EQ(bytes[XVT_FLIGHT_AXIS_BYTES], 0xC3);
			XVT_ASSERT_INT_EQ(bytes[0] & 1, mods & 1);
			XVT_ASSERT_INT_EQ(bytes[1] & 1, (mods >> 1) & 1);

			memset(&out, 0, sizeof out);
			XvtFlightControls_DecodeAxes(bytes, &out);
			XVT_ASSERT_INT_EQ(out.keyMods, mods);
			const int8_t sent[3] = { in.axisX, in.axisY, in.axisR };
			const int8_t back[3] = { out.axisX, out.axisY, out.axisR };
			for (int i = 0; i < 3; ++i) {
				/* Axes come back even: an even axis exactly, an odd one off by one. */
				XVT_ASSERT_INT_EQ(back[i] % 2, 0);
				XVT_ASSERT_TRUE(abs(back[i] - sent[i]) <= 1);
				if (sent[i] % 2 == 0)
					XVT_ASSERT_INT_EQ(back[i], sent[i]);
			}
		}
}

static void CheckThrottleEligible(void) {
	World();
	XVT_ASSERT_TRUE(XvtFlightControls_ThrottleEligible(PLAYER));
	XVT_ASSERT_TRUE(!XvtFlightControls_ThrottleEligible(0));

	World();
	g_players[PLAYER].connectedFlag = 0;
	XVT_ASSERT_TRUE(!XvtFlightControls_ThrottleEligible(PLAYER));
	World();
	g_flightMissionState.missionEndPending = 1;
	XVT_ASSERT_TRUE(!XvtFlightControls_ThrottleEligible(PLAYER));
	World();
	g_players[PLAYER].regionSessionId = 1;
	XVT_ASSERT_TRUE(!XvtFlightControls_ThrottleEligible(PLAYER));
	World();
	g_players[PLAYER].hyperspacePhase = 1;
	XVT_ASSERT_TRUE(!XvtFlightControls_ThrottleEligible(PLAYER));
	World();
	g_players[PLAYER].mapCameraState = 1;
	XVT_ASSERT_TRUE(!XvtFlightControls_ThrottleEligible(PLAYER));
	World();
	g_players[PLAYER].viewState.playerInputBlocked = 1;
	XVT_ASSERT_TRUE(!XvtFlightControls_ThrottleEligible(PLAYER));
	World();
	g_players[PLAYER].msgTypeId = FLIGHT_CHAT_RECIPIENT_TEAM;
	XVT_ASSERT_TRUE(!XvtFlightControls_ThrottleEligible(PLAYER));

	/* Their bound, live craft: another signature, an empty slot or no craft record will not do. */
	World();
	g_players[PLAYER].boundObjectSignature = SIGNATURE + 1;
	XVT_ASSERT_TRUE(!XvtFlightControls_ThrottleEligible(PLAYER));
	World();
	g_testObjects[SLOT].objectType = 0;
	XVT_ASSERT_TRUE(!XvtFlightControls_ThrottleEligible(PLAYER));
	World();
	g_testMobiles[SLOT].pCraft = NULL;
	XVT_ASSERT_TRUE(!XvtFlightControls_ThrottleEligible(PLAYER));
}

static void CheckApplyThrottle(void) {
	FlightInputFrameRecord record;
	memset(&record, 0, sizeof record);
	record.flags = XVT_INPUT_THROTTLE_PRESENT;
	record.throttle = 1234;

	World();
	g_testCraft[0].throttleSpeed = 7;
	XvtFlightControls_ApplyThrottle(PLAYER, &record);
	XVT_ASSERT_INT_EQ(g_testCraft[0].throttleSpeed, 1234);

	/* No lever position recorded: the craft keeps its throttle. */
	World();
	g_testCraft[0].throttleSpeed = 7;
	record.flags = 0;
	XvtFlightControls_ApplyThrottle(PLAYER, &record);
	XVT_ASSERT_INT_EQ(g_testCraft[0].throttleSpeed, 7);

	/* Not eligible: the same. */
	World();
	g_testCraft[0].throttleSpeed = 7;
	record.flags = XVT_INPUT_THROTTLE_PRESENT;
	g_players[PLAYER].hyperspacePhase = 1;
	XvtFlightControls_ApplyThrottle(PLAYER, &record);
	XVT_ASSERT_INT_EQ(g_testCraft[0].throttleSpeed, 7);
}

static void CheckSampleThrottleSendsNothing(void) {
	FlightInputFrameRecord record;

	/* No lever was read: no controller mapping is installed. */
	World();
	XvtControllerMapping_Shutdown();
	memset(&record, 0, sizeof record);
	record.flags = 0xFF;
	XvtFlightControls_SampleThrottle(&record);
	XVT_ASSERT_INT_EQ(record.flags & XVT_INPUT_THROTTLE_PRESENT, 0);

	/* The local player is not eligible. */
	World();
	g_players[PLAYER].connectedFlag = 0;
	memset(&record, 0, sizeof record);
	record.flags = 0xFF;
	XvtFlightControls_SampleThrottle(&record);
	XVT_ASSERT_INT_EQ(record.flags & XVT_INPUT_THROTTLE_PRESENT, 0);

	/* Alt-P in a one-player flight. */
	World();
	memset(&record, 0, sizeof record);
	record.key = FLIGHT_KEY_ALT_P;
	record.flags = 0xFF;
	XvtFlightControls_SampleThrottle(&record);
	XVT_ASSERT_INT_EQ(record.flags & XVT_INPUT_THROTTLE_PRESENT, 0);
}

static void CheckRollStep(void) {
	/* Steps from one tick to one second of ticks, and rates up to twice the unit rate: full deflection
	 * then fits the 16-bit result. These relationships hold for each. */
	XVT_ASSERT_INT_EQ(XvtFlightTiming_IsUnlocked(), 0);
	static const uint16_t kTicks[] = { 1, 4, 59, SIMULATION_TICKS_PER_SECOND };
	static const uint16_t kRates[] = { 0x1C00, 0x3800, 0x5555, 0x7000 };
	static const int16_t kModifiers[] = { -30000, -500, -1, 0, 1, 500, 30000 };
	for (size_t t = 0; t < sizeof kTicks / sizeof kTicks[0]; ++t)
		for (size_t r = 0; r < sizeof kRates / sizeof kRates[0]; ++r) {
			g_elapsedTicks = kTicks[t];
			uint16_t rate = kRates[r];

			/* With the roll axis at 0 the step is modifier_step, whatever it is. */
			g_xvtControlRoll = 0;
			for (size_t m = 0; m < sizeof kModifiers / sizeof kModifiers[0]; ++m)
				XVT_ASSERT_INT_EQ(XvtFlightControls_RollStep(0, rate, kModifiers[m]), kModifiers[m]);

			/* Full deflection is the limit, in both directions. */
			g_xvtControlRoll = 127;
			int full = XvtFlightControls_RollStep(0, rate, 0);
			XVT_ASSERT_TRUE(full >= 0);
			XVT_ASSERT_INT_EQ(XvtFlightControls_RollStep(0, rate, 30000), full);
			g_xvtControlRoll = -127;
			XVT_ASSERT_INT_EQ(XvtFlightControls_RollStep(0, rate, -30000), -full);

			for (int roll = -127; roll <= 127; roll += 6) {
				g_xvtControlRoll = (int16_t)roll;
				int plain = XvtFlightControls_RollStep(0, rate, 0);
				/* The step follows the axis's direction and never passes full deflection. */
				XVT_ASSERT_TRUE(roll > 0 ? plain >= 0 : plain <= 0);
				for (size_t m = 0; m < sizeof kModifiers / sizeof kModifiers[0]; ++m) {
					int step = XvtFlightControls_RollStep(0, rate, kModifiers[m]);
					XVT_ASSERT_TRUE(step >= -full && step <= full);
					/* modifier_step adds on, while the sum stays inside the limit. */
					if (plain + kModifiers[m] >= -full && plain + kModifiers[m] <= full)
						XVT_ASSERT_INT_EQ(step, plain + kModifiers[m]);
				}
			}
		}
	g_xvtControlRoll = 0;
}

static void CheckReset(void) {
	World();
	g_xvtControlRoll = -40;
	XvtFlightControls_Reset();
	XVT_ASSERT_INT_EQ(g_xvtControlRoll, 0);
}

static void CheckBlockedReadClears(void) {
	/* Without focus the keyboard route blocks: the read returns 0 and clears the game's input, even with
	 * a controller pushing yaw and holding fire. */
	World();
	ControllerFiring();
	SetGameInput();
	Host()->has_focus = 0;
	XVT_ASSERT_INT_EQ(XvtFlightControls_ReadLocal(), 0);
	XVT_ASSERT_INT_EQ(g_ctrlAxisX, 0);
	XVT_ASSERT_INT_EQ(g_ctrlAxisY, 0);
	XVT_ASSERT_INT_EQ(g_xvtControlRoll, 0);
	XVT_ASSERT_INT_EQ(g_keyMods, 0);
	XVT_ASSERT_INT_EQ(g_mouseButtons, 0);
	XVT_ASSERT_INT_EQ(g_actionKey, 0);
	XVT_ASSERT_INT_EQ(g_flightMouseDeltaX, 0);
	XVT_ASSERT_INT_EQ(g_flightMouseDeltaY, 0);

	/* Captured input blocks it the same way. */
	World();
	ControllerFiring();
	XvtInput_SetCaptured(true);
	SetGameInput();
	XVT_ASSERT_INT_EQ(XvtFlightControls_ReadLocal(), 0);
	XVT_ASSERT_INT_EQ(g_ctrlAxisX, 0);
	XVT_ASSERT_INT_EQ(g_keyMods, 0);
	XVT_ASSERT_INT_EQ(g_actionKey, 0);
	XvtInput_SetCaptured(false);
	XvtControllerMapping_Shutdown();
}

static void CheckRecover(void) {
	World();
	ControllerFiring();

	static XvtKeyboardBindings profile;
	memset(&profile, 0, sizeof profile);
	profile.bindings[0].source.key = AERON_KEY_A;
	profile.bindings[0].action = XVT_INPUT_ACTION_TARGET_NEXT;
	profile.count = 1;
	XvtKeyboardMapping_Install(&profile);
	XvtKeyboardMapping_Enable(true, Host());
	AeronKeyEvent press;
	memset(&press, 0, sizeof press);
	press.chord.key = AERON_KEY_A;
	press.down = 1;
	XvtKeyboardMapping_Event(&press, false);
	Host()->key_down[AERON_KEY_A + 1] = 1;
	g_actionKey = 0x41;

	XvtFlightControls_Recover();
	XVT_ASSERT_INT_EQ(g_actionKey, 0);
	/* The keyboard is flushed: the queued key is gone and the held key is blocked from the game. */
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadKey(), 0);
	XVT_ASSERT_INT_EQ(AeronCompat_IsKeySuppressed(AERON_KEY_A + 1), 1);
	/* Controller commands are released: fire is no longer held, and stays so while the button is. */
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 0);
	++Host()->frame_id;
	XvtControllerMapping_Update(Host());
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 0);
	XvtControllerMapping_Shutdown();
	XvtKeyboardMapping_Suspend();
}

/* Known failure: the header gives roll_rate no upper limit, but above about 0x7866 full deflection no longer
 * fits the 16-bit step, and the limit itself wraps negative. At 0x8000 a left roll comes back as a right
 * roll. The step is the roll axis scaled by positive factors and limited to full deflection, so it must
 * keep the axis's sign. */
static void KnownFailureRollStepFastRate(void) {
	XVT_ASSERT_INT_EQ(XvtFlightTiming_IsUnlocked(), 0);
	g_elapsedTicks = 59;
	g_xvtControlRoll = -60;
	int step = XvtFlightControls_RollStep(0, 0x8000, 0);
	g_xvtControlRoll = 0;
	XVT_ASSERT_TRUE(step <= 0);
}

/* Runs every check, or with "known-failure <name>" only that known failure; an unknown name passes. */
int main(int argc, char** argv) {
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		if (strcmp(argv[2], "roll_step_fast_rate") == 0)
			KnownFailureRollStepFastRate();
		return 0;
	}
	CheckEncodeDecode();
	CheckThrottleEligible();
	CheckApplyThrottle();
	CheckSampleThrottleSendsNothing();
	CheckRollStep();
	CheckReset();
	CheckBlockedReadClears();
	CheckRecover();
	XvtInput_ResetCapture();
	return 0;
}
