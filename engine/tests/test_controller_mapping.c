/* Checks the controller mapping (xvt_runtime/input/controller_mapping.h)
 * against the promises in its header: installing options, reading axes with
 * deadzone and inversion, when it suspends and resumes, arming and releasing
 * bindings, choosing the controller for axes, the throttle lever and its
 * generation, and the menu's buttons and direction. Every case builds its own
 * options and input snapshots, plays the host one frame at a time, and starts
 * from a fresh Init. No flight runs, so no action queues a flight key; held
 * buttons and axes need no flight. */
#include <string.h>

#include "aeron/input.h"
#include "test_assert.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/controller_mapping.h"
#include "xvt_runtime/runtime/flight_task.h"

static const char k_gamepad_guid[] = "0123456789abcdef0123456789abcdea";
static const char k_joystick_guid[] = "0123456789abcdef0123456789abcdeb";
static const char k_other_guid[] = "0123456789abcdef0123456789abcdec";

enum {
	FIRE_BUTTON = AERON_GAMEPAD_BUTTON_WEST,
	MODIFIER_BUTTON = AERON_GAMEPAD_BUTTON_NORTH,
	TARGET_BUTTON = AERON_GAMEPAD_BUTTON_RIGHT_SHOULDER,
	HALF = 16384,
};

static struct xvt_controller_options g_options;
static AeronInputSnapshot g_input;

static struct xvt_input_action_binding button(uint8_t index,
					      xvt_input_action action)
{
	struct xvt_input_action_binding binding;
	memset(&binding, 0, sizeof binding);
	binding.source.kind = AERON_CONTROLLER_DIGITAL_BUTTON;
	binding.source.index = index;
	binding.source.threshold = 0.5f;
	binding.action = action;
	return binding;
}

/* The gamepad model: yaw and pitch on the left stick with a 0.25 deadzone, roll
 * unbound, the throttle on the right trigger; fire, the target/roll modifier
 * and next target on three buttons. */
static void gamepad_model(struct xvt_controller_model *model)
{
	memset(model, 0, sizeof *model);
	memcpy(model->guid, k_gamepad_guid, sizeof k_gamepad_guid);
	model->kind = AERON_CONTROLLER_KIND_GAMEPAD;
	struct xvt_controller_profile *profile = &model->profile;
	xvt_controller_options_clear_profile(profile,
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
		button(FIRE_BUTTON, XVT_INPUT_ACTION_FIRE_WEAPON);
	profile->bindings[1] =
		button(MODIFIER_BUTTON, XVT_INPUT_ACTION_TARGET_ROLL_MODIFIER);
	profile->bindings[2] =
		button(TARGET_BUTTON, XVT_INPUT_ACTION_TARGET_NEXT);
	profile->binding_count = 3;
}

/* The joystick model: roll on axis 0 and fire on button 0; no axis the gamepad model drives. */
static void joystick_model(struct xvt_controller_model *model)
{
	memset(model, 0, sizeof *model);
	memcpy(model->guid, k_joystick_guid, sizeof k_joystick_guid);
	model->kind = AERON_CONTROLLER_KIND_JOYSTICK;
	struct xvt_controller_profile *profile = &model->profile;
	xvt_controller_options_clear_profile(profile,
					     AERON_CONTROLLER_KIND_JOYSTICK);
	profile->mapping.axes[XVT_INPUT_AXIS_ROLL].source = 0;
	profile->bindings[0] = button(0, XVT_INPUT_ACTION_FIRE_WEAPON);
	profile->binding_count = 1;
}

/* Options holding the gamepad model alone, installed by a fresh Init; the window has focus and no
 * controller is connected. */
static void controller_mapping_start(void)
{
	xvt_input_set_captured(false);
	memset(&g_options, 0, sizeof g_options);
	gamepad_model(&g_options.models[0]);
	g_options.count = 1;
	xvt_controller_mapping_init(&g_options);
	uint64_t frame = g_input.frame_id;
	memset(&g_input, 0, sizeof g_input);
	g_input.frame_id = frame;
	g_input.has_focus = 1;
}

/* Options holding both models. */
static void both_models(struct xvt_controller_options *options)
{
	memset(options, 0, sizeof *options);
	gamepad_model(&options->models[0]);
	joystick_model(&options->models[1]);
	options->count = 2;
}

static AeronControllerSnapshot *gamepad(int slot, uint32_t id)
{
	AeronControllerSnapshot *device = &g_input.controllers[slot];
	memset(device, 0, sizeof *device);
	device->connected = 1;
	device->kind = AERON_CONTROLLER_KIND_GAMEPAD;
	device->instance_id = id;
	memcpy(device->guid, k_gamepad_guid, sizeof k_gamepad_guid);
	device->gamepad_available_axes = (1u << AERON_GAMEPAD_AXIS_COUNT) - 1;
	device->gamepad_available_buttons =
		(1u << AERON_GAMEPAD_BUTTON_COUNT) - 1;
	return device;
}

static AeronControllerSnapshot *joystick(int slot, uint32_t id)
{
	AeronControllerSnapshot *device = &g_input.controllers[slot];
	memset(device, 0, sizeof *device);
	device->connected = 1;
	device->kind = AERON_CONTROLLER_KIND_JOYSTICK;
	device->instance_id = id;
	memcpy(device->guid, k_joystick_guid, sizeof k_joystick_guid);
	device->axis_count = 4;
	device->button_count = 8;
	device->hat_count = 1;
	return device;
}

static void hold(AeronControllerSnapshot *device, int button, bool down)
{
	if (down) {
		device->gamepad_buttons |= 1u << button;
	} else {
		device->gamepad_buttons &= ~(1u << button);
	}
}

/* Samples the snapshot as the next input frame. */
static void frame(void)
{
	++g_input.frame_id;
	xvt_controller_mapping_update(&g_input);
}

static void check_options(void)
{
	controller_mapping_start();
	XVT_ASSERT_TRUE(xvt_controller_options_equals(
		xvt_controller_mapping_options(), &g_options));

	/* SetOptions keeps the options; only ApplyPending installs them. */
	struct xvt_controller_options both;
	both_models(&both);
	xvt_controller_mapping_set_options(&both);
	XVT_ASSERT_TRUE(xvt_controller_options_equals(
		xvt_controller_mapping_options(), &g_options));
	xvt_controller_mapping_apply_pending();
	XVT_ASSERT_TRUE(xvt_controller_options_equals(
		xvt_controller_mapping_options(), &both));

	/* Invalid options are dropped. */
	struct xvt_controller_options invalid = g_options;
	invalid.models[0].guid[0] = 'X';
	xvt_controller_mapping_set_options(&invalid);
	xvt_controller_mapping_apply_pending();
	XVT_ASSERT_TRUE(xvt_controller_options_equals(
		xvt_controller_mapping_options(), &both));

	/* Init with invalid options leaves none installed; Shutdown clears them. */
	xvt_controller_mapping_init(&invalid);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_options()->count, 0);
	xvt_controller_mapping_init(&both);
	xvt_controller_mapping_shutdown();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_options()->count, 0);
}

static void check_axes(void)
{
	controller_mapping_start();
	AeronControllerSnapshot *pad = gamepad(0, 5);
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = HALF;
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTY] = HALF;
	frame();
	int yaw = xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW);
	XVT_ASSERT_TRUE(yaw > 0 && yaw <= 127);

	/* The same stick value gives pitch the opposite sign: a gamepad's Y
	 * points down. The menu sees only the configured inversion. */
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_axis(XVT_INPUT_AXIS_PITCH),
			  -yaw);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_menu_axis(XVT_INPUT_AXIS_YAW),
			  yaw);
	XVT_ASSERT_INT_EQ(
		xvt_controller_mapping_menu_axis(XVT_INPUT_AXIS_PITCH), yaw);

	/* Roll is unbound and the throttle is never an axis. */
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_axis(XVT_INPUT_AXIS_ROLL), 0);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_axis(XVT_INPUT_AXIS_THROTTLE),
			  0);

	/* The ends of the stick stay within -127 to 127. */
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = INT16_MAX;
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTY] = INT16_MIN;
	frame();
	XVT_ASSERT_TRUE(xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW) >= yaw);
	XVT_ASSERT_TRUE(xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW) <= 127);
	XVT_ASSERT_TRUE(xvt_controller_mapping_axis(XVT_INPUT_AXIS_PITCH) > 0);
	XVT_ASSERT_TRUE(xvt_controller_mapping_axis(XVT_INPUT_AXIS_PITCH) <=
			127);
	XVT_ASSERT_TRUE(
		xvt_controller_mapping_menu_axis(XVT_INPUT_AXIS_PITCH) >= -127);

	/* Inside the deadzone the axis is 0. */
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = 6000;
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTY] = -6000;
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW), 0);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_axis(XVT_INPUT_AXIS_PITCH), 0);

	/* Configured inversion flips yaw for flight and menu alike, and cancels
	 * the gamepad's pitch flip. */
	struct xvt_controller_options inverted = g_options;
	inverted.models[0].profile.mapping.axes[XVT_INPUT_AXIS_YAW].invert =
		true;
	inverted.models[0].profile.mapping.axes[XVT_INPUT_AXIS_PITCH].invert =
		true;
	xvt_controller_mapping_set_options(&inverted);
	xvt_controller_mapping_apply_pending();
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = HALF;
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTY] = HALF;
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW),
			  -yaw);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_menu_axis(XVT_INPUT_AXIS_YAW),
			  -yaw);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_axis(XVT_INPUT_AXIS_PITCH),
			  yaw);
	XVT_ASSERT_INT_EQ(
		xvt_controller_mapping_menu_axis(XVT_INPUT_AXIS_PITCH), -yaw);
}

static void check_joystick_pitch_not_flipped(void)
{
	controller_mapping_start();
	struct xvt_controller_options options;
	memset(&options, 0, sizeof options);
	joystick_model(&options.models[0]);
	options.models[0].profile.mapping.axes[XVT_INPUT_AXIS_PITCH].source = 1;
	options.count = 1;
	xvt_controller_mapping_init(&options);
	AeronControllerSnapshot *stick = joystick(0, 5);
	stick->raw_axes[0] = HALF;
	stick->raw_axes[1] = HALF;
	frame();
	int roll = xvt_controller_mapping_axis(XVT_INPUT_AXIS_ROLL);
	XVT_ASSERT_TRUE(roll > 0);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_axis(XVT_INPUT_AXIS_PITCH),
			  roll);
	XVT_ASSERT_INT_EQ(
		xvt_controller_mapping_menu_axis(XVT_INPUT_AXIS_PITCH), roll);
}

static void check_same_frame_does_nothing(void)
{
	controller_mapping_start();
	AeronControllerSnapshot *pad = gamepad(0, 5);
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = HALF;
	frame();
	int yaw = xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW);
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = -HALF;
	xvt_controller_mapping_update(&g_input);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW), yaw);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW),
			  -yaw);
}

static void check_suspends_and_resumes(void)
{
	controller_mapping_start();
	AeronControllerSnapshot *pad = gamepad(0, 5);
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = HALF;
	frame();
	int yaw = xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW);
	XVT_ASSERT_TRUE(yaw != 0);

	/* Without focus: suspended, though the controller is still present. */
	g_input.has_focus = 0;
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW), 0);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_menu_axis(XVT_INPUT_AXIS_YAW),
			  0);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_is_model_connected(), 1);
	g_input.has_focus = 1;
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW), yaw);

	/* While input is captured: suspended. */
	xvt_input_set_captured(true);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW), 0);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_is_model_connected(), 1);
	xvt_input_set_captured(false);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW), yaw);
}

static void check_bindings_arm(void)
{
	controller_mapping_start();
	/* A button held when the controller appears cannot fire until it is released. */
	AeronControllerSnapshot *pad = gamepad(0, 5);
	hold(pad, FIRE_BUTTON, true);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 0);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 0);
	hold(pad, FIRE_BUTTON, false);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 0);

	/* Fire is bit 1 and the target/roll modifier bit 2 while held. */
	hold(pad, FIRE_BUTTON, true);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 1);
	hold(pad, MODIFIER_BUTTON, true);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 3);
	hold(pad, FIRE_BUTTON, false);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 2);
	hold(pad, MODIFIER_BUTTON, false);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 0);

	/* Bindings are read from every matching controller. */
	AeronControllerSnapshot *second = gamepad(1, 9);
	frame();
	hold(second, FIRE_BUTTON, true);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 1);

	/* A controller that goes away releases its actions. */
	second->connected = 0;
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 0);
}

static void check_no_flight_key_without_flight(void)
{
	controller_mapping_start();
	XVT_ASSERT_INT_EQ(xvt_flight_task_is_active(), 0);
	AeronControllerSnapshot *pad = gamepad(0, 5);
	frame();
	hold(pad, TARGET_BUTTON, true);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_read_key(), 0);
}

static void check_release_commands(void)
{
	controller_mapping_start();
	AeronControllerSnapshot *pad = gamepad(0, 5);
	frame();
	hold(pad, FIRE_BUTTON, true);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 1);

	/* Held buttons empty, and a button still held must be released before it fires again. */
	xvt_controller_mapping_drop_commands();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 0);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 0);
	hold(pad, FIRE_BUTTON, false);
	frame();
	hold(pad, FIRE_BUTTON, true);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 1);
}

static void check_suspend_drops_state(void)
{
	controller_mapping_start();
	AeronControllerSnapshot *pad = gamepad(0, 5);
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = HALF;
	frame();
	hold(pad, FIRE_BUTTON, true);
	frame();
	int yaw = xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 1);

	xvt_controller_mapping_suspend();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 0);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW), 0);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_menu_axis(XVT_INPUT_AXIS_YAW),
			  0);

	/* The next Update resumes; the dropped controller state means the
	 * still-held button waits for a release, as for a controller that just
	 * appeared. */
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW), yaw);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 0);
	hold(pad, FIRE_BUTTON, false);
	frame();
	hold(pad, FIRE_BUTTON, true);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 1);
}

static void check_resolve(void)
{
	controller_mapping_start();
	const struct xvt_controller_model *model = &g_options.models[0];
	XVT_ASSERT_TRUE(xvt_controller_mapping_resolve(model, &g_input, 0) ==
			NULL);
	gamepad(0, 9);
	gamepad(1, 4);
	AeronControllerSnapshot *wrong_kind = gamepad(2, 2);
	wrong_kind->kind = AERON_CONTROLLER_KIND_JOYSTICK;
	gamepad(3, 1)->connected = 0;

	XVT_ASSERT_TRUE(xvt_controller_mapping_resolve(model, &g_input, 0) ==
			&g_input.controllers[1]);
	XVT_ASSERT_TRUE(xvt_controller_mapping_resolve(model, &g_input, 9) ==
			&g_input.controllers[0]);
	XVT_ASSERT_TRUE(xvt_controller_mapping_resolve(model, &g_input, 2) ==
			&g_input.controllers[1]);
	XVT_ASSERT_TRUE(xvt_controller_mapping_resolve(model, &g_input, 1) ==
			&g_input.controllers[1]);

	struct xvt_controller_model other = *model;
	memcpy(other.guid, k_other_guid, sizeof k_other_guid);
	XVT_ASSERT_TRUE(xvt_controller_mapping_resolve(&other, &g_input, 0) ==
			NULL);
}

static void check_analog_controller(void)
{
	controller_mapping_start();
	AeronControllerSnapshot *high = gamepad(0, 9);
	AeronControllerSnapshot *low = gamepad(1, 6);
	high->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = -HALF;
	low->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = HALF;
	frame();
	XVT_ASSERT_INT_EQ(
		xvt_controller_mapping_analog_instance(k_gamepad_guid), 6);
	XVT_ASSERT_TRUE(xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW) > 0);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_analog_instance(k_other_guid),
			  0);

	/* A new controller with a lower id does not take over from the one used last. */
	AeronControllerSnapshot *lowest = gamepad(2, 3);
	lowest->gamepad_axes[AERON_GAMEPAD_AXIS_LEFTX] = -HALF;
	frame();
	XVT_ASSERT_INT_EQ(
		xvt_controller_mapping_analog_instance(k_gamepad_guid), 6);
	XVT_ASSERT_TRUE(xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW) > 0);

	/* When it goes away, the lowest id left takes over. */
	low->connected = 0;
	frame();
	XVT_ASSERT_INT_EQ(
		xvt_controller_mapping_analog_instance(k_gamepad_guid), 3);
	XVT_ASSERT_TRUE(xvt_controller_mapping_axis(XVT_INPUT_AXIS_YAW) < 0);
}

static void check_apply_pending_releases_changed(void)
{
	controller_mapping_start();
	AeronControllerSnapshot *pad = gamepad(0, 5);
	frame();
	hold(pad, FIRE_BUTTON, true);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 1);

	/* Adding another model leaves this controller's state and axis controller alone. */
	struct xvt_controller_options both;
	both_models(&both);
	xvt_controller_mapping_set_options(&both);
	xvt_controller_mapping_apply_pending();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 1);
	XVT_ASSERT_INT_EQ(
		xvt_controller_mapping_analog_instance(k_gamepad_guid), 5);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 1);

	/* Changing its model releases its actions. */
	both.models[0].profile.mapping.axes[XVT_INPUT_AXIS_YAW].deadzone = 0.5f;
	xvt_controller_mapping_set_options(&both);
	xvt_controller_mapping_apply_pending();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 0);

	/* So does removing its model. */
	hold(pad, FIRE_BUTTON, false);
	frame();
	hold(pad, FIRE_BUTTON, true);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 1);
	struct xvt_controller_options joystick_only;
	memset(&joystick_only, 0, sizeof joystick_only);
	joystick_model(&joystick_only.models[0]);
	joystick_only.count = 1;
	xvt_controller_mapping_set_options(&joystick_only);
	xvt_controller_mapping_apply_pending();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_modifiers(), 0);
}

static void check_throttle_position(void)
{
	const AeronControllerKind joystick = AERON_CONTROLLER_KIND_JOYSTICK;

	/* A full axis: its ends, and the 328 at each end that snap to it. */
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_throttle_position(
				  INT16_MIN, joystick, 0, false),
			  0);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_throttle_position(
				  INT16_MAX, joystick, 0, false),
			  UINT16_MAX);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_throttle_position(
				  INT16_MIN + 328, joystick, 0, false),
			  0);
	XVT_ASSERT_TRUE(xvt_controller_mapping_throttle_position(
				INT16_MIN + 329, joystick, 0, false) > 0);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_throttle_position(
				  INT16_MAX - 328, joystick, 0, false),
			  UINT16_MAX);
	XVT_ASSERT_TRUE(xvt_controller_mapping_throttle_position(
				INT16_MAX - 329, joystick, 0, false) <
			UINT16_MAX);

	const AeronControllerKind gamepad = AERON_CONTROLLER_KIND_GAMEPAD;
	/* A gamepad stick is a full axis too; a trigger spans 0 to 32767. */
	XVT_ASSERT_INT_EQ(
		xvt_controller_mapping_throttle_position(
			INT16_MIN, gamepad, AERON_GAMEPAD_AXIS_LEFTY, false),
		0);
	XVT_ASSERT_INT_EQ(
		xvt_controller_mapping_throttle_position(
			0, gamepad, AERON_GAMEPAD_AXIS_LEFT_TRIGGER, false),
		0);
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_throttle_position(
				  INT16_MAX, gamepad,
				  AERON_GAMEPAD_AXIS_RIGHT_TRIGGER, false),
			  UINT16_MAX);

	/* The travel between the ends rises with the input, and invert flips it. */
	uint16_t previous = 0;
	for (int raw = INT16_MIN; raw <= INT16_MAX; ++raw) {
		uint16_t position = xvt_controller_mapping_throttle_position(
			(int16_t)raw, joystick, 0, false);
		XVT_ASSERT_TRUE(position >= previous);
		XVT_ASSERT_INT_EQ(xvt_controller_mapping_throttle_position(
					  (int16_t)raw, joystick, 0, true),
				  UINT16_MAX - position);
		previous = position;
	}
	previous = 0;
	for (int raw = 0; raw <= INT16_MAX; ++raw) {
		uint16_t position = xvt_controller_mapping_throttle_position(
			(int16_t)raw, gamepad, AERON_GAMEPAD_AXIS_RIGHT_TRIGGER,
			false);
		XVT_ASSERT_TRUE(position >= previous);
		XVT_ASSERT_INT_EQ(xvt_controller_mapping_throttle_position(
					  (int16_t)raw, gamepad,
					  AERON_GAMEPAD_AXIS_RIGHT_TRIGGER,
					  true),
				  UINT16_MAX - position);
		previous = position;
	}
}

static void check_throttle_sample(void)
{
	controller_mapping_start();
	AeronControllerSnapshot *pad = gamepad(0, 5);
	pad->gamepad_axes[AERON_GAMEPAD_AXIS_RIGHT_TRIGGER] = 16000;
	frame();
	uint16_t position = 1;
	uint32_t before = 0;
	XVT_ASSERT_TRUE(
		xvt_controller_mapping_throttle_sample(&position, &before));
	XVT_ASSERT_INT_EQ(position,
			  xvt_controller_mapping_throttle_position(
				  16000, AERON_CONTROLLER_KIND_GAMEPAD,
				  AERON_GAMEPAD_AXIS_RIGHT_TRIGGER, false));

	/* Nothing changed: the generation holds. */
	frame();
	uint32_t generation = 0;
	XVT_ASSERT_TRUE(
		xvt_controller_mapping_throttle_sample(&position, &generation));
	XVT_ASSERT_INT_EQ(generation, before);

	/* Suspending changes it. */
	xvt_controller_mapping_suspend();
	xvt_controller_mapping_throttle_sample(&position, &generation);
	XVT_ASSERT_TRUE(generation != before);
	frame();
	XVT_ASSERT_TRUE(
		xvt_controller_mapping_throttle_sample(&position, &before));

	/* Another controller for the throttle changes it. */
	gamepad(0, 8)->gamepad_axes[AERON_GAMEPAD_AXIS_RIGHT_TRIGGER] = 16000;
	frame();
	XVT_ASSERT_TRUE(
		xvt_controller_mapping_throttle_sample(&position, &generation));
	XVT_ASSERT_TRUE(generation != before);
	before = generation;

	/* A change of the throttle's binding changes it, and the new binding applies. */
	struct xvt_controller_options inverted = g_options;
	inverted.models[0]
		.profile.mapping.axes[XVT_INPUT_AXIS_THROTTLE]
		.invert = true;
	xvt_controller_mapping_set_options(&inverted);
	xvt_controller_mapping_apply_pending();
	frame();
	XVT_ASSERT_TRUE(
		xvt_controller_mapping_throttle_sample(&position, &generation));
	XVT_ASSERT_TRUE(generation != before);
	XVT_ASSERT_INT_EQ(position,
			  xvt_controller_mapping_throttle_position(
				  16000, AERON_CONTROLLER_KIND_GAMEPAD,
				  AERON_GAMEPAD_AXIS_RIGHT_TRIGGER, true));

	/* No lever read this frame: false. */
	g_input.controllers[0].connected = 0;
	frame();
	XVT_ASSERT_TRUE(!xvt_controller_mapping_throttle_sample(&position,
								&generation));
}

static void check_present(void)
{
	controller_mapping_start();
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_is_model_connected(), 0);
	AeronControllerSnapshot *pad = gamepad(0, 5);
	memcpy(pad->guid, k_other_guid, sizeof k_other_guid);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_is_model_connected(), 0);
	gamepad(0, 5);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_is_model_connected(), 1);
}

static void check_menu_buttons(void)
{
	controller_mapping_start();
	/* Held when the controller appeared: hidden until released. */
	AeronControllerSnapshot *pad = gamepad(0, 5);
	hold(pad, AERON_GAMEPAD_BUTTON_SOUTH, true);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_menu_buttons(), 0);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_menu_buttons(), 0);
	hold(pad, AERON_GAMEPAD_BUTTON_SOUTH, false);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_menu_buttons(), 0);

	hold(pad, AERON_GAMEPAD_BUTTON_SOUTH, true);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_menu_buttons(), 1);
	hold(pad, AERON_GAMEPAD_BUTTON_EAST, true);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_menu_buttons(), 3);
	hold(pad, AERON_GAMEPAD_BUTTON_SOUTH, false);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_menu_buttons(), 2);

	/* A joystick's buttons 0 and 1. */
	struct xvt_controller_options both;
	both_models(&both);
	xvt_controller_mapping_init(&both);
	memset(&g_input.controllers, 0, sizeof g_input.controllers);
	AeronControllerSnapshot *stick = joystick(0, 7);
	frame();
	stick->raw_buttons = 1;
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_menu_buttons(), 1);
	stick->raw_buttons = 2;
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_menu_buttons(), 2);
}

static void check_menu_hat(void)
{
	controller_mapping_start();
	AeronControllerSnapshot *pad = gamepad(0, 5);
	hold(pad, AERON_GAMEPAD_BUTTON_DPAD_UP, true);
	frame();
	XVT_ASSERT_INT_EQ(xvt_controller_mapping_menu_hat(), 0);
	hold(pad, AERON_GAMEPAD_BUTTON_DPAD_UP, false);
	frame();

	static const struct {
		int button;
		uint8_t direction;
	} k_pad[] = {{AERON_GAMEPAD_BUTTON_DPAD_UP, 1},
		     {AERON_GAMEPAD_BUTTON_DPAD_RIGHT, 2},
		     {AERON_GAMEPAD_BUTTON_DPAD_DOWN, 4},
		     {AERON_GAMEPAD_BUTTON_DPAD_LEFT, 8}};

	for (size_t i = 0; i < sizeof k_pad / sizeof k_pad[0]; ++i) {
		hold(pad, k_pad[i].button, true);
		frame();
		XVT_ASSERT_INT_EQ(xvt_controller_mapping_menu_hat(),
				  k_pad[i].direction);
		hold(pad, k_pad[i].button, false);
		frame();
		XVT_ASSERT_INT_EQ(xvt_controller_mapping_menu_hat(), 0);
	}

	/* A joystick's first hat, with the same bits. */
	struct xvt_controller_options both;
	both_models(&both);
	xvt_controller_mapping_init(&both);
	memset(&g_input.controllers, 0, sizeof g_input.controllers);
	AeronControllerSnapshot *stick = joystick(0, 7);
	frame();
	static const uint8_t k_hat[] = {
		AERON_CONTROLLER_HAT_UP, AERON_CONTROLLER_HAT_RIGHT,
		AERON_CONTROLLER_HAT_DOWN, AERON_CONTROLLER_HAT_LEFT};
	for (size_t i = 0; i < sizeof k_hat / sizeof k_hat[0]; ++i) {
		stick->raw_hats[0] = k_hat[i];
		frame();
		XVT_ASSERT_INT_EQ(xvt_controller_mapping_menu_hat(),
				  k_pad[i].direction);
		stick->raw_hats[0] = AERON_CONTROLLER_HAT_CENTERED;
		frame();
	}
}

int main(void)
{
	check_options();
	check_axes();
	check_joystick_pitch_not_flipped();
	check_same_frame_does_nothing();
	check_suspends_and_resumes();
	check_bindings_arm();
	check_no_flight_key_without_flight();
	check_release_commands();
	check_suspend_drops_state();
	check_resolve();
	check_analog_controller();
	check_apply_pending_releases_changed();
	check_throttle_position();
	check_throttle_sample();
	check_present();
	check_menu_buttons();
	check_menu_hat();
	xvt_controller_mapping_shutdown();
	return 0;
}
