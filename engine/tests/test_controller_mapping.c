/* Checks the controller mapping (xvt_runtime/input/controller_mapping.h) against the promises in its
 * header: installing options, reading axes with deadzone and inversion, when it suspends and resumes,
 * arming and releasing bindings, choosing the controller for axes, the throttle lever and its generation,
 * and the menu's buttons and direction. Every case builds its own options and input snapshots, plays the
 * host one frame at a time, and starts from a fresh Init. No flight runs, so no action queues a flight
 * key; held buttons and axes need no flight. */
#include "aeron/input.h"
#include "test_assert.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/controller_mapping.h"
#include "xvt_runtime/runtime/flight_task.h"

#include <string.h>

static const char kGamepadGuid[] = "0123456789abcdef0123456789abcdea";
static const char kJoystickGuid[] = "0123456789abcdef0123456789abcdeb";
static const char kOtherGuid[] = "0123456789abcdef0123456789abcdec";

enum {
	FIRE_BUTTON = AERON_GAMEPAD_BUTTON_WEST,
	MODIFIER_BUTTON = AERON_GAMEPAD_BUTTON_NORTH,
	TARGET_BUTTON = AERON_GAMEPAD_BUTTON_RIGHT_SHOULDER,
	HALF = 16384,
};

static struct XvtControllerOptions g_options;
static AeronInputSnapshot g_input;

static struct XvtInputActionBinding Button(uint8_t index, XvtInputAction action)
{
	struct XvtInputActionBinding binding;
	memset(&binding, 0, sizeof binding);
	binding.source.kind = AERON_CONTROLLER_DIGITAL_BUTTON;
	binding.source.index = index;
	binding.source.threshold = 0.5f;
	binding.action = action;
	return binding;
}

/* The gamepad model: yaw and pitch on the left stick with a 0.25 deadzone, roll unbound, the throttle on
 * the right trigger; fire, the target/roll modifier and next target on three buttons. */
static void GamepadModel(struct XvtControllerModel *model)
{
	memset(model, 0, sizeof *model);
	memcpy(model->guid, kGamepadGuid, sizeof kGamepadGuid);
	model->kind = AERON_CONTROLLER_KIND_GAMEPAD;
	struct XvtControllerProfile *profile = &model->profile;
	XvtControllerOptions_ClearProfile(profile,
					  AERON_CONTROLLER_KIND_GAMEPAD);
	profile->mapping.axes[XVT_INPUT_AXIS_YAW].source =
		AERON_GAMEPAD_AXIS_LEFTX;
	profile->mapping.axes[XVT_INPUT_AXIS_YAW].deadzone = 0.25f;
	profile->mapping.axes[XVT_INPUT_AXIS_PITCH].source =
		AERON_GAMEPAD_AXIS_LEFTY;
	profile->mapping.axes[XVT_INPUT_AXIS_PITCH].deadzone = 0.25f;
	profile->mapping.axes[XVT_INPUT_AXIS_THROTTLE].source =
		AERON_GAMEPAD_AXIS_RIGHT_TRIGGER;
	profile->bindings[0] =
		Button(FIRE_BUTTON, XVT_INPUT_ACTION_FIRE_WEAPON);
	profile->bindings[1] =
		Button(MODIFIER_BUTTON, XVT_INPUT_ACTION_TARGET_ROLL_MODIFIER);
	profile->bindings[2] =
		Button(TARGET_BUTTON, XVT_INPUT_ACTION_TARGET_NEXT);
	profile->binding_count = 3;
}

/* The joystick model: roll on axis 0 and fire on button 0; no axis the gamepad model drives. */
static void JoystickModel(struct XvtControllerModel *model)
{
	memset(model, 0, sizeof *model);
	memcpy(model->guid, kJoystickGuid, sizeof kJoystickGuid);
	model->kind = AERON_CONTROLLER_KIND_JOYSTICK;
	struct XvtControllerProfile *profile = &model->profile;
	XvtControllerOptions_ClearProfile(profile,
					  AERON_CONTROLLER_KIND_JOYSTICK);
	profile->mapping.axes[XVT_INPUT_AXIS_ROLL].source = 0;
	profile->bindings[0] = Button(0, XVT_INPUT_ACTION_FIRE_WEAPON);
	profile->binding_count = 1;
}

/* Options holding the gamepad model alone, installed by a fresh Init; the window has focus and no
 * controller is connected. */
static void Start(void)
{
	XvtInput_SetCaptured(false);
	memset(&g_options, 0, sizeof g_options);
	GamepadModel(&g_options.models[0]);
	g_options.count = 1;
	XvtControllerMapping_Init(&g_options);
	uint64_t frame = g_input.frame_id;
	memset(&g_input, 0, sizeof g_input);
	g_input.frame_id = frame;
	g_input.has_focus = 1;
}

/* Options holding both models. */
static void BothModels(struct XvtControllerOptions *options)
{
	memset(options, 0, sizeof *options);
	GamepadModel(&options->models[0]);
	JoystickModel(&options->models[1]);
	options->count = 2;
}

static AeronControllerSnapshot *Gamepad(int slot, uint32_t id)
{
	AeronControllerSnapshot *device = &g_input.controllers[slot];
	memset(device, 0, sizeof *device);
	device->connected = 1;
	device->kind = AERON_CONTROLLER_KIND_GAMEPAD;
	device->instance_id = id;
	memcpy(device->guid, kGamepadGuid, sizeof kGamepadGuid);
	device->gamepad_available_axes = (1u << AERON_GAMEPAD_AXIS_COUNT) - 1;
	device->gamepad_available_buttons =
		(1u << AERON_GAMEPAD_BUTTON_COUNT) - 1;
	return device;
}

static AeronControllerSnapshot *Joystick(int slot, uint32_t id)
{
	AeronControllerSnapshot *device = &g_input.controllers[slot];
	memset(device, 0, sizeof *device);
	device->connected = 1;
	device->kind = AERON_CONTROLLER_KIND_JOYSTICK;
	device->instance_id = id;
	memcpy(device->guid, kJoystickGuid, sizeof kJoystickGuid);
	device->axis_count = 4;
	device->button_count = 8;
	device->hat_count = 1;
	return device;
}

static void Hold(AeronControllerSnapshot *device, int button, bool down)
{
	if (down) {
		device->gamepad_buttons |= 1u << button;
	} else {
		device->gamepad_buttons &= ~(1u << button);
	}
}

/* Samples the snapshot as the next input frame. */
static void Frame(void)
{
	++g_input.frame_id;
	XvtControllerMapping_Update(&g_input);
}

static void CheckOptions(void)
{
	Start();
	XVT_ASSERT_TRUE(XvtControllerOptions_Equals(
		XvtControllerMapping_Options(), &g_options));

	/* SetOptions keeps the options; only ApplyPending installs them. */
	struct XvtControllerOptions both;
	BothModels(&both);
	XvtControllerMapping_SetOptions(&both);
	XVT_ASSERT_TRUE(XvtControllerOptions_Equals(
		XvtControllerMapping_Options(), &g_options));
	XvtControllerMapping_ApplyPending();
	XVT_ASSERT_TRUE(XvtControllerOptions_Equals(
		XvtControllerMapping_Options(), &both));

	/* Invalid options are dropped. */
	struct XvtControllerOptions invalid = g_options;
	invalid.models[0].guid[0] = 'X';
	XvtControllerMapping_SetOptions(&invalid);
	XvtControllerMapping_ApplyPending();
	XVT_ASSERT_TRUE(XvtControllerOptions_Equals(
		XvtControllerMapping_Options(), &both));

	/* Init with invalid options leaves none installed; Shutdown clears them. */
	XvtControllerMapping_Init(&invalid);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Options()->count, 0);
	XvtControllerMapping_Init(&both);
	XvtControllerMapping_Shutdown();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Options()->count, 0);
}

static void CheckAxes(void)
{
	Start();
	AeronControllerSnapshot *pad = Gamepad(0, 5);
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = HALF;
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTY] = HALF;
	Frame();
	int yaw = XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW);
	XVT_ASSERT_TRUE(yaw > 0 && yaw <= 127);

	/* The same stick value gives pitch the opposite sign: a gamepad's Y points down. The menu sees only
	 * the configured inversion. */
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Axis(XVT_INPUT_AXIS_PITCH),
			  -yaw);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuAxis(XVT_INPUT_AXIS_YAW),
			  yaw);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuAxis(XVT_INPUT_AXIS_PITCH),
			  yaw);

	/* Roll is unbound and the throttle is never an axis. */
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Axis(XVT_INPUT_AXIS_ROLL), 0);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Axis(XVT_INPUT_AXIS_THROTTLE),
			  0);

	/* The ends of the stick stay within -127 to 127. */
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = INT16_MAX;
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTY] = INT16_MIN;
	Frame();
	XVT_ASSERT_TRUE(XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW) >= yaw);
	XVT_ASSERT_TRUE(XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW) <= 127);
	XVT_ASSERT_TRUE(XvtControllerMapping_Axis(XVT_INPUT_AXIS_PITCH) > 0);
	XVT_ASSERT_TRUE(XvtControllerMapping_Axis(XVT_INPUT_AXIS_PITCH) <= 127);
	XVT_ASSERT_TRUE(XvtControllerMapping_MenuAxis(XVT_INPUT_AXIS_PITCH) >=
			-127);

	/* Inside the deadzone the axis is 0. */
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = 6000;
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTY] = -6000;
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW), 0);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Axis(XVT_INPUT_AXIS_PITCH), 0);

	/* Configured inversion flips yaw for flight and menu alike, and cancels the gamepad's pitch flip. */
	struct XvtControllerOptions inverted = g_options;
	inverted.models[0].profile.mapping.axes[XVT_INPUT_AXIS_YAW].invert =
		true;
	inverted.models[0].profile.mapping.axes[XVT_INPUT_AXIS_PITCH].invert =
		true;
	XvtControllerMapping_SetOptions(&inverted);
	XvtControllerMapping_ApplyPending();
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = HALF;
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTY] = HALF;
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW), -yaw);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuAxis(XVT_INPUT_AXIS_YAW),
			  -yaw);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Axis(XVT_INPUT_AXIS_PITCH), yaw);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuAxis(XVT_INPUT_AXIS_PITCH),
			  -yaw);
}

static void CheckJoystickPitchNotFlipped(void)
{
	Start();
	struct XvtControllerOptions options;
	memset(&options, 0, sizeof options);
	JoystickModel(&options.models[0]);
	options.models[0].profile.mapping.axes[XVT_INPUT_AXIS_PITCH].source = 1;
	options.count = 1;
	XvtControllerMapping_Init(&options);
	AeronControllerSnapshot *stick = Joystick(0, 5);
	stick->raw_axes[0] = HALF;
	stick->raw_axes[1] = HALF;
	Frame();
	int roll = XvtControllerMapping_Axis(XVT_INPUT_AXIS_ROLL);
	XVT_ASSERT_TRUE(roll > 0);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Axis(XVT_INPUT_AXIS_PITCH),
			  roll);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuAxis(XVT_INPUT_AXIS_PITCH),
			  roll);
}

static void CheckSameFrameDoesNothing(void)
{
	Start();
	AeronControllerSnapshot *pad = Gamepad(0, 5);
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = HALF;
	Frame();
	int yaw = XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW);
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = -HALF;
	XvtControllerMapping_Update(&g_input);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW), yaw);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW), -yaw);
}

static void CheckSuspendsAndResumes(void)
{
	Start();
	AeronControllerSnapshot *pad = Gamepad(0, 5);
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = HALF;
	Frame();
	int yaw = XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW);
	XVT_ASSERT_TRUE(yaw != 0);

	/* Without focus: suspended, though the controller is still present. */
	g_input.has_focus = 0;
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW), 0);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuAxis(XVT_INPUT_AXIS_YAW), 0);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_IsModelConnected(), 1);
	g_input.has_focus = 1;
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW), yaw);

	/* While input is captured: suspended. */
	XvtInput_SetCaptured(true);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW), 0);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_IsModelConnected(), 1);
	XvtInput_SetCaptured(false);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW), yaw);
}

static void CheckBindingsArm(void)
{
	Start();
	/* A button held when the controller appears cannot fire until it is released. */
	AeronControllerSnapshot *pad = Gamepad(0, 5);
	Hold(pad, FIRE_BUTTON, true);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 0);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 0);
	Hold(pad, FIRE_BUTTON, false);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 0);

	/* Fire is bit 1 and the target/roll modifier bit 2 while held. */
	Hold(pad, FIRE_BUTTON, true);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 1);
	Hold(pad, MODIFIER_BUTTON, true);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 3);
	Hold(pad, FIRE_BUTTON, false);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 2);
	Hold(pad, MODIFIER_BUTTON, false);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 0);

	/* Bindings are read from every matching controller. */
	AeronControllerSnapshot *second = Gamepad(1, 9);
	Frame();
	Hold(second, FIRE_BUTTON, true);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 1);

	/* A controller that goes away releases its actions. */
	second->connected = 0;
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 0);
}

static void CheckNoFlightKeyWithoutFlight(void)
{
	Start();
	XVT_ASSERT_INT_EQ(XvtFlightTask_IsActive(), 0);
	AeronControllerSnapshot *pad = Gamepad(0, 5);
	Frame();
	Hold(pad, TARGET_BUTTON, true);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_ReadKey(), 0);
}

static void CheckReleaseCommands(void)
{
	Start();
	AeronControllerSnapshot *pad = Gamepad(0, 5);
	Frame();
	Hold(pad, FIRE_BUTTON, true);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 1);

	/* Held buttons empty, and a button still held must be released before it fires again. */
	XvtControllerMapping_DropCommands();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 0);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 0);
	Hold(pad, FIRE_BUTTON, false);
	Frame();
	Hold(pad, FIRE_BUTTON, true);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 1);
}

static void CheckSuspendDropsState(void)
{
	Start();
	AeronControllerSnapshot *pad = Gamepad(0, 5);
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = HALF;
	Frame();
	Hold(pad, FIRE_BUTTON, true);
	Frame();
	int yaw = XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 1);

	XvtControllerMapping_Suspend();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 0);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW), 0);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuAxis(XVT_INPUT_AXIS_YAW), 0);

	/* The next Update resumes; the dropped controller state means the still-held button waits for a
	 * release, as for a controller that just appeared. */
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW), yaw);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 0);
	Hold(pad, FIRE_BUTTON, false);
	Frame();
	Hold(pad, FIRE_BUTTON, true);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 1);
}

static void CheckResolve(void)
{
	Start();
	const struct XvtControllerModel *model = &g_options.models[0];
	XVT_ASSERT_TRUE(XvtControllerMapping_Resolve(model, &g_input, 0) ==
			NULL);
	Gamepad(0, 9);
	Gamepad(1, 4);
	AeronControllerSnapshot *wrongKind = Gamepad(2, 2);
	wrongKind->kind = AERON_CONTROLLER_KIND_JOYSTICK;
	Gamepad(3, 1)->connected = 0;

	XVT_ASSERT_TRUE(XvtControllerMapping_Resolve(model, &g_input, 0) ==
			&g_input.controllers[1]);
	XVT_ASSERT_TRUE(XvtControllerMapping_Resolve(model, &g_input, 9) ==
			&g_input.controllers[0]);
	XVT_ASSERT_TRUE(XvtControllerMapping_Resolve(model, &g_input, 2) ==
			&g_input.controllers[1]);
	XVT_ASSERT_TRUE(XvtControllerMapping_Resolve(model, &g_input, 1) ==
			&g_input.controllers[1]);

	struct XvtControllerModel other = *model;
	memcpy(other.guid, kOtherGuid, sizeof kOtherGuid);
	XVT_ASSERT_TRUE(XvtControllerMapping_Resolve(&other, &g_input, 0) ==
			NULL);
}

static void CheckAnalogController(void)
{
	Start();
	AeronControllerSnapshot *high = Gamepad(0, 9);
	AeronControllerSnapshot *low = Gamepad(1, 6);
	high->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = -HALF;
	low->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = HALF;
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_AnalogInstance(kGamepadGuid), 6);
	XVT_ASSERT_TRUE(XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW) > 0);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_AnalogInstance(kOtherGuid), 0);

	/* A new controller with a lower id does not take over from the one used last. */
	AeronControllerSnapshot *lowest = Gamepad(2, 3);
	lowest->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = -HALF;
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_AnalogInstance(kGamepadGuid), 6);
	XVT_ASSERT_TRUE(XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW) > 0);

	/* When it goes away, the lowest id left takes over. */
	low->connected = 0;
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_AnalogInstance(kGamepadGuid), 3);
	XVT_ASSERT_TRUE(XvtControllerMapping_Axis(XVT_INPUT_AXIS_YAW) < 0);
}

static void CheckApplyPendingReleasesChanged(void)
{
	Start();
	AeronControllerSnapshot *pad = Gamepad(0, 5);
	Frame();
	Hold(pad, FIRE_BUTTON, true);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 1);

	/* Adding another model leaves this controller's state and axis controller alone. */
	struct XvtControllerOptions both;
	BothModels(&both);
	XvtControllerMapping_SetOptions(&both);
	XvtControllerMapping_ApplyPending();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 1);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_AnalogInstance(kGamepadGuid), 5);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 1);

	/* Changing its model releases its actions. */
	both.models[0].profile.mapping.axes[XVT_INPUT_AXIS_YAW].deadzone = 0.5f;
	XvtControllerMapping_SetOptions(&both);
	XvtControllerMapping_ApplyPending();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 0);

	/* So does removing its model. */
	Hold(pad, FIRE_BUTTON, false);
	Frame();
	Hold(pad, FIRE_BUTTON, true);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 1);
	struct XvtControllerOptions joystickOnly;
	memset(&joystickOnly, 0, sizeof joystickOnly);
	JoystickModel(&joystickOnly.models[0]);
	joystickOnly.count = 1;
	XvtControllerMapping_SetOptions(&joystickOnly);
	XvtControllerMapping_ApplyPending();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_Modifiers(), 0);
}

static void CheckThrottlePosition(void)
{
	const AeronControllerKind joystick = AERON_CONTROLLER_KIND_JOYSTICK;
	const AeronControllerKind gamepad = AERON_CONTROLLER_KIND_GAMEPAD;

	/* A full axis: its ends, and the 328 at each end that snap to it. */
	XVT_ASSERT_INT_EQ(XvtControllerMapping_ThrottlePosition(
				  INT16_MIN, joystick, 0, false),
			  0);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_ThrottlePosition(
				  INT16_MAX, joystick, 0, false),
			  UINT16_MAX);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_ThrottlePosition(
				  INT16_MIN + 328, joystick, 0, false),
			  0);
	XVT_ASSERT_TRUE(XvtControllerMapping_ThrottlePosition(
				INT16_MIN + 329, joystick, 0, false) > 0);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_ThrottlePosition(
				  INT16_MAX - 328, joystick, 0, false),
			  UINT16_MAX);
	XVT_ASSERT_TRUE(
		XvtControllerMapping_ThrottlePosition(INT16_MAX - 329, joystick,
						      0, false) < UINT16_MAX);

	/* A gamepad stick is a full axis too; a trigger spans 0 to 32767. */
	XVT_ASSERT_INT_EQ(
		XvtControllerMapping_ThrottlePosition(
			INT16_MIN, gamepad, AERON_GAMEPAD_AXIS_LEFTY, false),
		0);
	XVT_ASSERT_INT_EQ(
		XvtControllerMapping_ThrottlePosition(
			0, gamepad, AERON_GAMEPAD_AXIS_LEFT_TRIGGER, false),
		0);
	XVT_ASSERT_INT_EQ(XvtControllerMapping_ThrottlePosition(
				  INT16_MAX, gamepad,
				  AERON_GAMEPAD_AXIS_RIGHT_TRIGGER, false),
			  UINT16_MAX);

	/* The travel between the ends rises with the input, and invert flips it. */
	uint16_t previous = 0;
	for (int raw = INT16_MIN; raw <= INT16_MAX; ++raw) {
		uint16_t position = XvtControllerMapping_ThrottlePosition(
			(int16_t)raw, joystick, 0, false);
		XVT_ASSERT_TRUE(position >= previous);
		XVT_ASSERT_INT_EQ(XvtControllerMapping_ThrottlePosition(
					  (int16_t)raw, joystick, 0, true),
				  UINT16_MAX - position);
		previous = position;
	}
	previous = 0;
	for (int raw = 0; raw <= INT16_MAX; ++raw) {
		uint16_t position = XvtControllerMapping_ThrottlePosition(
			(int16_t)raw, gamepad, AERON_GAMEPAD_AXIS_RIGHT_TRIGGER,
			false);
		XVT_ASSERT_TRUE(position >= previous);
		XVT_ASSERT_INT_EQ(XvtControllerMapping_ThrottlePosition(
					  (int16_t)raw, gamepad,
					  AERON_GAMEPAD_AXIS_RIGHT_TRIGGER,
					  true),
				  UINT16_MAX - position);
		previous = position;
	}
}

static void CheckThrottleSample(void)
{
	Start();
	uint16_t position = 1;
	uint32_t generation = 0, before = 0;
	AeronControllerSnapshot *pad = Gamepad(0, 5);
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_RIGHT_TRIGGER] = 16000;
	Frame();
	XVT_ASSERT_TRUE(
		XvtControllerMapping_ThrottleSample(&position, &before));
	XVT_ASSERT_INT_EQ(position,
			  XvtControllerMapping_ThrottlePosition(
				  16000, AERON_CONTROLLER_KIND_GAMEPAD,
				  AERON_GAMEPAD_AXIS_RIGHT_TRIGGER, false));

	/* Nothing changed: the generation holds. */
	Frame();
	XVT_ASSERT_TRUE(
		XvtControllerMapping_ThrottleSample(&position, &generation));
	XVT_ASSERT_INT_EQ(generation, before);

	/* Suspending changes it. */
	XvtControllerMapping_Suspend();
	XvtControllerMapping_ThrottleSample(&position, &generation);
	XVT_ASSERT_TRUE(generation != before);
	Frame();
	XVT_ASSERT_TRUE(
		XvtControllerMapping_ThrottleSample(&position, &before));

	/* Another controller for the throttle changes it. */
	Gamepad(0, 8)->gamepad_axes[AERON_GAMEPAD_AXIS_RIGHT_TRIGGER] = 16000;
	Frame();
	XVT_ASSERT_TRUE(
		XvtControllerMapping_ThrottleSample(&position, &generation));
	XVT_ASSERT_TRUE(generation != before);
	before = generation;

	/* A change of the throttle's binding changes it, and the new binding applies. */
	struct XvtControllerOptions inverted = g_options;
	inverted.models[0]
		.profile.mapping.axes[XVT_INPUT_AXIS_THROTTLE]
		.invert = true;
	XvtControllerMapping_SetOptions(&inverted);
	XvtControllerMapping_ApplyPending();
	Frame();
	XVT_ASSERT_TRUE(
		XvtControllerMapping_ThrottleSample(&position, &generation));
	XVT_ASSERT_TRUE(generation != before);
	XVT_ASSERT_INT_EQ(position,
			  XvtControllerMapping_ThrottlePosition(
				  16000, AERON_CONTROLLER_KIND_GAMEPAD,
				  AERON_GAMEPAD_AXIS_RIGHT_TRIGGER, true));

	/* No lever read this frame: false. */
	g_input.controllers[0].connected = 0;
	Frame();
	XVT_ASSERT_TRUE(
		!XvtControllerMapping_ThrottleSample(&position, &generation));
}

static void CheckPresent(void)
{
	Start();
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_IsModelConnected(), 0);
	AeronControllerSnapshot *pad = Gamepad(0, 5);
	memcpy(pad->guid, kOtherGuid, sizeof kOtherGuid);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_IsModelConnected(), 0);
	Gamepad(0, 5);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_IsModelConnected(), 1);
}

static void CheckMenuButtons(void)
{
	Start();
	/* Held when the controller appeared: hidden until released. */
	AeronControllerSnapshot *pad = Gamepad(0, 5);
	Hold(pad, AERON_GAMEPAD_BUTTON_SOUTH, true);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuButtons(), 0);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuButtons(), 0);
	Hold(pad, AERON_GAMEPAD_BUTTON_SOUTH, false);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuButtons(), 0);

	Hold(pad, AERON_GAMEPAD_BUTTON_SOUTH, true);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuButtons(), 1);
	Hold(pad, AERON_GAMEPAD_BUTTON_EAST, true);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuButtons(), 3);
	Hold(pad, AERON_GAMEPAD_BUTTON_SOUTH, false);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuButtons(), 2);

	/* A joystick's buttons 0 and 1. */
	struct XvtControllerOptions both;
	BothModels(&both);
	XvtControllerMapping_Init(&both);
	memset(&g_input.controllers, 0, sizeof g_input.controllers);
	AeronControllerSnapshot *stick = Joystick(0, 7);
	Frame();
	stick->raw_buttons = 1;
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuButtons(), 1);
	stick->raw_buttons = 2;
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuButtons(), 2);
}

static void CheckMenuHat(void)
{
	Start();
	AeronControllerSnapshot *pad = Gamepad(0, 5);
	Hold(pad, AERON_GAMEPAD_BUTTON_DPAD_UP, true);
	Frame();
	XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuHat(), 0);
	Hold(pad, AERON_GAMEPAD_BUTTON_DPAD_UP, false);
	Frame();

	static const struct {
		int button;
		uint8_t direction;
	} kPad[] = {{AERON_GAMEPAD_BUTTON_DPAD_UP, 1},
		    {AERON_GAMEPAD_BUTTON_DPAD_RIGHT, 2},
		    {AERON_GAMEPAD_BUTTON_DPAD_DOWN, 4},
		    {AERON_GAMEPAD_BUTTON_DPAD_LEFT, 8}};

	for (size_t i = 0; i < sizeof kPad / sizeof kPad[0]; ++i) {
		Hold(pad, kPad[i].button, true);
		Frame();
		XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuHat(),
				  kPad[i].direction);
		Hold(pad, kPad[i].button, false);
		Frame();
		XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuHat(), 0);
	}

	/* A joystick's first hat, with the same bits. */
	struct XvtControllerOptions both;
	BothModels(&both);
	XvtControllerMapping_Init(&both);
	memset(&g_input.controllers, 0, sizeof g_input.controllers);
	AeronControllerSnapshot *stick = Joystick(0, 7);
	Frame();
	static const uint8_t kHat[] = {
		AERON_CONTROLLER_HAT_UP, AERON_CONTROLLER_HAT_RIGHT,
		AERON_CONTROLLER_HAT_DOWN, AERON_CONTROLLER_HAT_LEFT};
	for (size_t i = 0; i < sizeof kHat / sizeof kHat[0]; ++i) {
		stick->raw_hats[0] = kHat[i];
		Frame();
		XVT_ASSERT_INT_EQ(XvtControllerMapping_MenuHat(),
				  kPad[i].direction);
		stick->raw_hats[0] = AERON_CONTROLLER_HAT_CENTERED;
		Frame();
	}
}

int main(void)
{
	CheckOptions();
	CheckAxes();
	CheckJoystickPitchNotFlipped();
	CheckSameFrameDoesNothing();
	CheckSuspendsAndResumes();
	CheckBindingsArm();
	CheckNoFlightKeyWithoutFlight();
	CheckReleaseCommands();
	CheckSuspendDropsState();
	CheckResolve();
	CheckAnalogController();
	CheckApplyPendingReleasesChanged();
	CheckThrottlePosition();
	CheckThrottleSample();
	CheckPresent();
	CheckMenuButtons();
	CheckMenuHat();
	XvtControllerMapping_Shutdown();
	return 0;
}
