/* Checks the saved controller settings (xvt_runtime/input/controller_options.h) against the promises in
 * its header: what a valid profile and a valid model list are and each refusal, equality, clearing a
 * profile, finding and adding models, and adding connected gamepads with a default profile. Every case
 * builds its own settings and controller snapshot; nothing is read from disk. */
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "xvt_runtime/input/controller_options.h"

/* Valid GUIDs: 32 lower-case hex digits, in increasing order. */
static const char k_guid_a[] = "0123456789abcdef0123456789abcdea";
static const char k_guid_b[] = "0123456789abcdef0123456789abcdeb";
static const char k_guid_c[] = "0123456789abcdef0123456789abcdec";
static const char k_guid_d[] = "0123456789abcdef0123456789abcded";
static const char k_guid_e[] = "0123456789abcdef0123456789abcdee";

static struct xvt_controller_options g_options;
static AeronInputSnapshot g_input;
static char g_error[128];

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

/* A gamepad profile with yaw on the left stick's X axis and two button bindings. */
static void gamepad_profile(struct xvt_controller_profile *profile)
{
	xvt_controller_options_clear_profile(profile,
					     AERON_CONTROLLER_KIND_GAMEPAD);
	profile->mapping.axes[XVT_INPUT_AXIS_YAW].source =
		AERON_GAMEPAD_AXIS_LEFTX;
	profile->mapping.axes[XVT_INPUT_AXIS_YAW].deadzone = 0.1f;
	profile->bindings[0] = button(AERON_GAMEPAD_BUTTON_SOUTH,
				      XVT_INPUT_ACTION_FIRE_WEAPON);
	profile->bindings[1] =
		button(AERON_GAMEPAD_BUTTON_EAST, XVT_INPUT_ACTION_TARGET_NEXT);
	profile->binding_count = 2;
}

static bool profile_valid(const struct xvt_controller_profile *profile,
			  AeronControllerKind kind)
{
	g_error[0] = '\0';
	bool valid = xvt_controller_options_validate_profile(
		profile, kind, g_error, sizeof g_error);
	if (!valid) {
		XVT_ASSERT_TRUE(g_error[0] != '\0');
	}
	return valid;
}

static bool options_valid(const struct xvt_controller_options *options)
{
	g_error[0] = '\0';
	bool valid = xvt_controller_options_validate(options, g_error,
						     sizeof g_error);
	if (!valid) {
		XVT_ASSERT_TRUE(g_error[0] != '\0');
	}
	return valid;
}

/* One model per GUID given, each a gamepad with a cleared profile and a terminated name. */
static void models(size_t count, const char *const *guids)
{
	memset(&g_options, 0, sizeof g_options);
	for (size_t i = 0; i < count; ++i) {
		struct xvt_controller_model *model = &g_options.models[i];
		memcpy(model->guid, guids[i], sizeof model->guid);
		model->kind = AERON_CONTROLLER_KIND_GAMEPAD;
		xvt_controller_options_clear_profile(
			&model->profile, AERON_CONTROLLER_KIND_GAMEPAD);
	}
	g_options.count = count;
}

/* A connected controller in input slot `slot`. */
static AeronControllerSnapshot *connect(int slot, const char *guid,
					AeronControllerKind kind, uint32_t id)
{
	AeronControllerSnapshot *device = &g_input.controllers[slot];
	memset(device, 0, sizeof *device);
	device->connected = 1;
	device->kind = kind;
	device->instance_id = id;
	memcpy(device->guid, guid, sizeof device->guid);
	return device;
}

static void check_validate_profile_accepts(void)
{
	struct xvt_controller_profile profile;
	gamepad_profile(&profile);
	XVT_ASSERT_TRUE(profile_valid(&profile, AERON_CONTROLLER_KIND_GAMEPAD));

	/* A deadzone of exactly 0 or 1, and a threshold of exactly 1, are inside the limits. */
	profile.mapping.axes[XVT_INPUT_AXIS_YAW].deadzone = 1.0f;
	profile.mapping.axes[XVT_INPUT_AXIS_PITCH].source =
		AERON_GAMEPAD_AXIS_LEFTY;
	profile.bindings[0].source.threshold = 1.0f;
	XVT_ASSERT_TRUE(profile_valid(&profile, AERON_CONTROLLER_KIND_GAMEPAD));

	/* A joystick may bind a hat. */
	xvt_controller_options_clear_profile(&profile,
					     AERON_CONTROLLER_KIND_JOYSTICK);
	profile.bindings[0].source.kind = AERON_CONTROLLER_DIGITAL_HAT;
	profile.bindings[0].source.index = AERON_CONTROLLER_HAT_MAX - 1;
	profile.bindings[0].source.hat_direction = AERON_CONTROLLER_HAT_LEFT;
	profile.bindings[0].source.threshold = 0.5f;
	profile.bindings[0].action = XVT_INPUT_ACTION_VIEW_LEFT_WING;
	profile.binding_count = 1;
	XVT_ASSERT_TRUE(
		profile_valid(&profile, AERON_CONTROLLER_KIND_JOYSTICK));
}

static void check_validate_profile_axis_refusals(void)
{
	struct xvt_controller_profile profile;

	/* Each kind's axis count is the limit: a gamepad has AERON_GAMEPAD_AXIS_COUNT, a joystick
	 * AERON_CONTROLLER_AXIS_MAX. */
	gamepad_profile(&profile);
	profile.mapping.axes[XVT_INPUT_AXIS_YAW].source =
		AERON_GAMEPAD_AXIS_COUNT;
	XVT_ASSERT_TRUE(
		!profile_valid(&profile, AERON_CONTROLLER_KIND_GAMEPAD));
	xvt_controller_options_clear_profile(&profile,
					     AERON_CONTROLLER_KIND_JOYSTICK);
	profile.mapping.axes[XVT_INPUT_AXIS_YAW].source =
		AERON_GAMEPAD_AXIS_COUNT;
	XVT_ASSERT_TRUE(
		profile_valid(&profile, AERON_CONTROLLER_KIND_JOYSTICK));
	profile.mapping.axes[XVT_INPUT_AXIS_YAW].source =
		AERON_CONTROLLER_AXIS_MAX;
	XVT_ASSERT_TRUE(
		!profile_valid(&profile, AERON_CONTROLLER_KIND_JOYSTICK));

	/* -1 is the only negative source. */
	gamepad_profile(&profile);
	profile.mapping.axes[XVT_INPUT_AXIS_YAW].source = -2;
	XVT_ASSERT_TRUE(
		!profile_valid(&profile, AERON_CONTROLLER_KIND_GAMEPAD));

	/* One source drives one axis. */
	gamepad_profile(&profile);
	profile.mapping.axes[XVT_INPUT_AXIS_ROLL].source =
		AERON_GAMEPAD_AXIS_LEFTX;
	XVT_ASSERT_TRUE(
		!profile_valid(&profile, AERON_CONTROLLER_KIND_GAMEPAD));

	/* Deadzones run from 0 to 1, and the throttle's must be 0. */
	gamepad_profile(&profile);
	profile.mapping.axes[XVT_INPUT_AXIS_YAW].deadzone = 1.5f;
	XVT_ASSERT_TRUE(
		!profile_valid(&profile, AERON_CONTROLLER_KIND_GAMEPAD));
	profile.mapping.axes[XVT_INPUT_AXIS_YAW].deadzone = -0.25f;
	XVT_ASSERT_TRUE(
		!profile_valid(&profile, AERON_CONTROLLER_KIND_GAMEPAD));
	gamepad_profile(&profile);
	profile.mapping.axes[XVT_INPUT_AXIS_THROTTLE].source =
		AERON_GAMEPAD_AXIS_RIGHT_TRIGGER;
	XVT_ASSERT_TRUE(profile_valid(&profile, AERON_CONTROLLER_KIND_GAMEPAD));
	profile.mapping.axes[XVT_INPUT_AXIS_THROTTLE].deadzone = 0.1f;
	XVT_ASSERT_TRUE(
		!profile_valid(&profile, AERON_CONTROLLER_KIND_GAMEPAD));
}

static void check_validate_profile_binding_refusals(void)
{
	struct xvt_controller_profile profile;

	gamepad_profile(&profile);
	profile.binding_count = XVT_CONTROLLER_BINDING_CAP + 1;
	XVT_ASSERT_TRUE(
		!profile_valid(&profile, AERON_CONTROLLER_KIND_GAMEPAD));

	/* Each binding needs an action. */
	gamepad_profile(&profile);
	profile.bindings[1].action = XVT_INPUT_ACTION_NONE;
	XVT_ASSERT_TRUE(
		!profile_valid(&profile, AERON_CONTROLLER_KIND_GAMEPAD));
	profile.bindings[1].action = XVT_INPUT_ACTION_COUNT;
	XVT_ASSERT_TRUE(
		!profile_valid(&profile, AERON_CONTROLLER_KIND_GAMEPAD));

	/* The threshold lies in (0, 1]. */
	gamepad_profile(&profile);
	profile.bindings[1].source.threshold = 0.0f;
	XVT_ASSERT_TRUE(
		!profile_valid(&profile, AERON_CONTROLLER_KIND_GAMEPAD));
	profile.bindings[1].source.threshold = 1.25f;
	XVT_ASSERT_TRUE(
		!profile_valid(&profile, AERON_CONTROLLER_KIND_GAMEPAD));

	/* Button indexes stay within the kind's button count. */
	gamepad_profile(&profile);
	profile.bindings[1].source.index = AERON_GAMEPAD_BUTTON_COUNT;
	XVT_ASSERT_TRUE(
		!profile_valid(&profile, AERON_CONTROLLER_KIND_GAMEPAD));
	xvt_controller_options_clear_profile(&profile,
					     AERON_CONTROLLER_KIND_JOYSTICK);
	profile.bindings[0] = button(AERON_CONTROLLER_BUTTON_MAX - 1,
				     XVT_INPUT_ACTION_TARGET_NEXT);
	profile.binding_count = 1;
	XVT_ASSERT_TRUE(
		profile_valid(&profile, AERON_CONTROLLER_KIND_JOYSTICK));
	profile.bindings[0].source.index = AERON_CONTROLLER_BUTTON_MAX;
	XVT_ASSERT_TRUE(
		!profile_valid(&profile, AERON_CONTROLLER_KIND_JOYSTICK));

	/* A digital axis stays within the kind's axis count. */
	gamepad_profile(&profile);
	profile.bindings[1].source.kind =
		AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE;
	profile.bindings[1].source.index = AERON_GAMEPAD_AXIS_COUNT;
	XVT_ASSERT_TRUE(
		!profile_valid(&profile, AERON_CONTROLLER_KIND_GAMEPAD));

	/* A hat only on a joystick, and only one of its hats. */
	gamepad_profile(&profile);
	profile.bindings[1].source.kind = AERON_CONTROLLER_DIGITAL_HAT;
	profile.bindings[1].source.index = 0;
	profile.bindings[1].source.hat_direction = AERON_CONTROLLER_HAT_UP;
	XVT_ASSERT_TRUE(
		!profile_valid(&profile, AERON_CONTROLLER_KIND_GAMEPAD));
	xvt_controller_options_clear_profile(&profile,
					     AERON_CONTROLLER_KIND_JOYSTICK);
	profile.bindings[0].source.kind = AERON_CONTROLLER_DIGITAL_HAT;
	profile.bindings[0].source.index = AERON_CONTROLLER_HAT_MAX;
	profile.bindings[0].source.hat_direction = AERON_CONTROLLER_HAT_UP;
	profile.bindings[0].source.threshold = 0.5f;
	profile.bindings[0].action = XVT_INPUT_ACTION_TARGET_NEXT;
	profile.binding_count = 1;
	XVT_ASSERT_TRUE(
		!profile_valid(&profile, AERON_CONTROLLER_KIND_JOYSTICK));

	/* No source bound twice. */
	gamepad_profile(&profile);
	profile.bindings[1].source.index = AERON_GAMEPAD_BUTTON_SOUTH;
	XVT_ASSERT_TRUE(
		!profile_valid(&profile, AERON_CONTROLLER_KIND_GAMEPAD));
}

static void check_validate_profile_error_capacity(void)
{
	/* A refusal's message stays inside the capacity it is given. */
	struct xvt_controller_profile profile;
	gamepad_profile(&profile);
	profile.bindings[1].action = XVT_INPUT_ACTION_NONE;
	char *small = malloc(8);
	XVT_ASSERT_TRUE(small != NULL);
	XVT_ASSERT_TRUE(!xvt_controller_options_validate_profile(
		&profile, AERON_CONTROLLER_KIND_GAMEPAD, small, 8));
	XVT_ASSERT_TRUE(memchr(small, '\0', 8) != NULL);
	free(small);
}

static void check_effective_axis_invert(void)
{
	for (int axis = 0; axis < XVT_INPUT_AXIS_COUNT; ++axis) {
		for (int invert = 0; invert < 2; ++invert) {
			bool flipped = axis == XVT_INPUT_AXIS_PITCH;
			XVT_ASSERT_INT_EQ(
				xvt_controller_options_effective_axis_invert(
					AERON_CONTROLLER_KIND_GAMEPAD,
					(xvt_input_axis)axis, invert != 0),
				flipped ? !invert : invert);
			XVT_ASSERT_INT_EQ(
				xvt_controller_options_effective_axis_invert(
					AERON_CONTROLLER_KIND_JOYSTICK,
					(xvt_input_axis)axis, invert != 0),
				invert);
		}
	}
}

static void check_profile_equal(void)
{
	struct xvt_controller_profile left, right;
	gamepad_profile(&left);
	gamepad_profile(&right);
	XVT_ASSERT_TRUE(xvt_controller_options_profile_equal(&left, &right));

	right.mapping.axes[XVT_INPUT_AXIS_YAW].invert = true;
	XVT_ASSERT_TRUE(!xvt_controller_options_profile_equal(&left, &right));
	gamepad_profile(&right);
	right.mapping.axes[XVT_INPUT_AXIS_YAW].deadzone = 0.2f;
	XVT_ASSERT_TRUE(!xvt_controller_options_profile_equal(&left, &right));
	gamepad_profile(&right);
	right.mapping.axes[XVT_INPUT_AXIS_YAW].source =
		AERON_GAMEPAD_AXIS_RIGHTX;
	XVT_ASSERT_TRUE(!xvt_controller_options_profile_equal(&left, &right));

	/* Bindings compare in order, thresholds included. */
	gamepad_profile(&right);
	right.bindings[1].source.threshold = 0.75f;
	XVT_ASSERT_TRUE(!xvt_controller_options_profile_equal(&left, &right));
	gamepad_profile(&right);
	right.bindings[1].action = XVT_INPUT_ACTION_TARGET_PREV;
	XVT_ASSERT_TRUE(!xvt_controller_options_profile_equal(&left, &right));
	gamepad_profile(&right);
	struct xvt_input_action_binding first = right.bindings[0];
	right.bindings[0] = right.bindings[1];
	right.bindings[1] = first;
	XVT_ASSERT_TRUE(!xvt_controller_options_profile_equal(&left, &right));
	gamepad_profile(&right);
	right.binding_count = 1;
	XVT_ASSERT_TRUE(!xvt_controller_options_profile_equal(&left, &right));
}

static void check_equals(void)
{
	XVT_ASSERT_TRUE(xvt_controller_options_equals(NULL, NULL));
	const char *guids[] = {k_guid_a, k_guid_b};
	models(2, guids);
	struct xvt_controller_options other = g_options;
	XVT_ASSERT_TRUE(!xvt_controller_options_equals(&g_options, NULL));
	XVT_ASSERT_TRUE(!xvt_controller_options_equals(NULL, &g_options));
	XVT_ASSERT_TRUE(xvt_controller_options_equals(&g_options, &other));

	/* The same models in another order are not equal. */
	other.models[0] = g_options.models[1];
	other.models[1] = g_options.models[0];
	XVT_ASSERT_TRUE(!xvt_controller_options_equals(&g_options, &other));

	other = g_options;
	other.count = 1;
	XVT_ASSERT_TRUE(!xvt_controller_options_equals(&g_options, &other));
	other = g_options;
	other.models[1].profile.mapping.axes[XVT_INPUT_AXIS_ROLL].source =
		AERON_GAMEPAD_AXIS_RIGHTX;
	XVT_ASSERT_TRUE(!xvt_controller_options_equals(&g_options, &other));
}

static void check_clear_profile(void)
{
	for (int k = 0; k < 2; ++k) {
		AeronControllerKind kind = k ? AERON_CONTROLLER_KIND_JOYSTICK
					     : AERON_CONTROLLER_KIND_GAMEPAD;
		struct xvt_controller_profile profile;
		memset(&profile, 0xA5, sizeof profile);
		xvt_controller_options_clear_profile(&profile, kind);
		XVT_ASSERT_INT_EQ(profile.binding_count, 0);
		for (int axis = 0; axis < XVT_INPUT_AXIS_COUNT; ++axis) {
			const struct xvt_input_axis_binding *binding =
				&profile.mapping.axes[axis];
			XVT_ASSERT_INT_EQ(binding->source, -1);
			XVT_ASSERT_CLOSE(binding->deadzone, 0.0, 0.0,
					 "a cleared profile holds exact zeros");
			bool inverted =
				kind == AERON_CONTROLLER_KIND_JOYSTICK &&
				axis == XVT_INPUT_AXIS_THROTTLE;
			XVT_ASSERT_INT_EQ(binding->invert, inverted);
		}
		for (size_t i = 0; i < XVT_CONTROLLER_BINDING_CAP; ++i) {
			XVT_ASSERT_INT_EQ(profile.bindings[i].action,
					  XVT_INPUT_ACTION_NONE);
			XVT_ASSERT_INT_EQ(profile.bindings[i].source.kind,
					  AERON_CONTROLLER_DIGITAL_NONE);
		}
	}
}

static void check_find_model(void)
{
	const char *guids[] = {k_guid_a, k_guid_b};
	models(2, guids);
	XVT_ASSERT_INT_EQ(
		xvt_controller_options_find_model(&g_options, k_guid_a), 0);
	XVT_ASSERT_INT_EQ(
		xvt_controller_options_find_model(&g_options, k_guid_b), 1);
	XVT_ASSERT_INT_EQ(
		xvt_controller_options_find_model(&g_options, k_guid_c), -1);

	/* Exactly that GUID: a prefix or another case does not match. */
	char changed[33];
	memcpy(changed, k_guid_a, sizeof changed);
	changed[31] = '\0';
	XVT_ASSERT_INT_EQ(
		xvt_controller_options_find_model(&g_options, changed), -1);
	memcpy(changed, k_guid_a, sizeof changed);
	changed[10] = 'A';
	XVT_ASSERT_INT_EQ(
		xvt_controller_options_find_model(&g_options, changed), -1);
}

static void check_validate(void)
{
	const char *guids[] = {k_guid_a, k_guid_b};
	models(0, guids);
	XVT_ASSERT_TRUE(options_valid(&g_options));
	models(2, guids);
	XVT_ASSERT_TRUE(options_valid(&g_options));

	/* GUIDs: 32 lower-case hex digits, not all zero, not repeated. */
	models(2, guids);
	g_options.models[1].guid[5] = 'A';
	XVT_ASSERT_TRUE(!options_valid(&g_options));
	g_options.models[1].guid[5] = 'g';
	XVT_ASSERT_TRUE(!options_valid(&g_options));
	g_options.models[1].guid[5] = '5';
	g_options.models[1].guid[31] = '\0';
	XVT_ASSERT_TRUE(!options_valid(&g_options));
	memset(g_options.models[1].guid, '0', 32);
	XVT_ASSERT_TRUE(!options_valid(&g_options));
	memcpy(g_options.models[1].guid, k_guid_a, sizeof k_guid_a);
	XVT_ASSERT_TRUE(!options_valid(&g_options));

	/* The name must be terminated. */
	models(2, guids);
	memset(g_options.models[0].name, 'x', sizeof g_options.models[0].name);
	XVT_ASSERT_TRUE(!options_valid(&g_options));

	/* Each profile must be valid. */
	models(2, guids);
	g_options.models[1].profile.mapping.axes[XVT_INPUT_AXIS_YAW].source =
		AERON_GAMEPAD_AXIS_LEFTX;
	g_options.models[1].profile.mapping.axes[XVT_INPUT_AXIS_YAW].deadzone =
		2.0f;
	XVT_ASSERT_TRUE(!options_valid(&g_options));

	/* A flight axis is driven by one model only; different axes on different models are fine. */
	models(2, guids);
	g_options.models[0].profile.mapping.axes[XVT_INPUT_AXIS_YAW].source =
		AERON_GAMEPAD_AXIS_LEFTX;
	g_options.models[1].profile.mapping.axes[XVT_INPUT_AXIS_PITCH].source =
		AERON_GAMEPAD_AXIS_LEFTY;
	XVT_ASSERT_TRUE(options_valid(&g_options));
	g_options.models[1].profile.mapping.axes[XVT_INPUT_AXIS_YAW].source =
		AERON_GAMEPAD_AXIS_RIGHTX;
	XVT_ASSERT_TRUE(!options_valid(&g_options));

	/* At most XVT_CONTROLLER_MODEL_CAP models. */
	models(2, guids);
	g_options.count = XVT_CONTROLLER_MODEL_CAP + 1;
	XVT_ASSERT_TRUE(!options_valid(&g_options));
}

static void check_add_model(void)
{
	const char *guids[] = {k_guid_a};
	models(1, guids);
	memset(&g_input, 0, sizeof g_input);
	AeronControllerSnapshot *joystick =
		connect(0, k_guid_b, AERON_CONTROLLER_KIND_JOYSTICK, 4);

	XVT_ASSERT_TRUE(xvt_controller_options_ensure_model(
		&g_options, joystick, g_error, sizeof g_error));
	XVT_ASSERT_INT_EQ(g_options.count, 2);
	const struct xvt_controller_model *added = &g_options.models[1];
	XVT_ASSERT_INT_EQ(strcmp(added->guid, k_guid_b), 0);
	XVT_ASSERT_INT_EQ(added->kind, AERON_CONTROLLER_KIND_JOYSTICK);
	struct xvt_controller_profile cleared;
	xvt_controller_options_clear_profile(&cleared,
					     AERON_CONTROLLER_KIND_JOYSTICK);
	XVT_ASSERT_TRUE(xvt_controller_options_profile_equal(&added->profile,
							     &cleared));
	XVT_ASSERT_TRUE(options_valid(&g_options));

	/* A GUID already listed adds nothing and succeeds. */
	XVT_ASSERT_TRUE(xvt_controller_options_ensure_model(
		&g_options, joystick, g_error, sizeof g_error));
	XVT_ASSERT_INT_EQ(g_options.count, 2);

	/* A device without a valid GUID fails. */
	AeronControllerSnapshot *unnamed =
		connect(1, k_guid_c, AERON_CONTROLLER_KIND_GAMEPAD, 5);
	unnamed->guid[0] = 'Z';
	g_error[0] = '\0';
	XVT_ASSERT_TRUE(!xvt_controller_options_ensure_model(
		&g_options, unnamed, g_error, sizeof g_error));
	XVT_ASSERT_TRUE(g_error[0] != '\0');
	XVT_ASSERT_INT_EQ(g_options.count, 2);

	/* A full list fails. */
	AeronControllerSnapshot *gamepad =
		connect(1, k_guid_c, AERON_CONTROLLER_KIND_GAMEPAD, 5);
	g_options.count = XVT_CONTROLLER_MODEL_CAP;
	for (size_t i = 2; i < XVT_CONTROLLER_MODEL_CAP; ++i) {
		memcpy(g_options.models[i].guid, k_guid_e, sizeof k_guid_e);
		g_options.models[i].guid[0] = (char)('0' + i);
	}
	g_error[0] = '\0';
	XVT_ASSERT_TRUE(!xvt_controller_options_ensure_model(
		&g_options, gamepad, g_error, sizeof g_error));
	XVT_ASSERT_TRUE(g_error[0] != '\0');
	XVT_ASSERT_INT_EQ(g_options.count, XVT_CONTROLLER_MODEL_CAP);
}

static void check_initialize_gamepads(void)
{
	/* The existing model, a joystick, drives yaw. */
	const char *guids[] = {k_guid_e};
	models(1, guids);
	g_options.models[0].kind = AERON_CONTROLLER_KIND_JOYSTICK;
	xvt_controller_options_clear_profile(&g_options.models[0].profile,
					     AERON_CONTROLLER_KIND_JOYSTICK);
	g_options.models[0].profile.mapping.axes[XVT_INPUT_AXIS_YAW].source = 0;

	/* Defaults bind yaw, pitch and roll, and two buttons. */
	struct xvt_controller_profile defaults;
	gamepad_profile(&defaults);
	defaults.mapping.axes[XVT_INPUT_AXIS_PITCH].source =
		AERON_GAMEPAD_AXIS_LEFTY;
	defaults.mapping.axes[XVT_INPUT_AXIS_ROLL].source =
		AERON_GAMEPAD_AXIS_RIGHTX;

	/* Two new gamepads, connected out of GUID order; a joystick and a disconnected gamepad are skipped,
	 * and a gamepad that already has a model gains no second one. */
	memset(&g_input, 0, sizeof g_input);
	connect(0, k_guid_c, AERON_CONTROLLER_KIND_GAMEPAD, 1);
	connect(1, k_guid_d, AERON_CONTROLLER_KIND_JOYSTICK, 2);
	connect(2, k_guid_b, AERON_CONTROLLER_KIND_GAMEPAD, 3);
	connect(3, k_guid_a, AERON_CONTROLLER_KIND_GAMEPAD, 4)->connected = 0;

	XVT_ASSERT_TRUE(xvt_controller_options_add_new_gamepads(
		&g_options, &defaults, &g_input, g_error, sizeof g_error));
	XVT_ASSERT_INT_EQ(g_options.count, 3);
	XVT_ASSERT_INT_EQ(strcmp(g_options.models[1].guid, k_guid_b), 0);
	XVT_ASSERT_INT_EQ(strcmp(g_options.models[2].guid, k_guid_c), 0);
	for (size_t i = 1; i < 3; ++i) {
		const struct xvt_controller_profile *profile =
			&g_options.models[i].profile;
		XVT_ASSERT_INT_EQ(g_options.models[i].kind,
				  AERON_CONTROLLER_KIND_GAMEPAD);
		XVT_ASSERT_INT_EQ(profile->binding_count,
				  defaults.binding_count);
		for (size_t b = 0; b < defaults.binding_count; ++b) {
			XVT_ASSERT_INT_EQ(profile->bindings[b].action,
					  defaults.bindings[b].action);
			XVT_ASSERT_INT_EQ(profile->bindings[b].source.kind,
					  defaults.bindings[b].source.kind);
			XVT_ASSERT_INT_EQ(profile->bindings[b].source.index,
					  defaults.bindings[b].source.index);
			XVT_ASSERT_CLOSE(
				profile->bindings[b].source.threshold,
				defaults.bindings[b].source.threshold, 0.0,
				"a copied threshold is the same float");
		}
	}

	/* The first new model leaves yaw unbound, since the joystick drives it, and keeps the rest; the
	 * second finds every default axis driven already. */
	const struct xvt_input_axis_binding *first =
		g_options.models[1].profile.mapping.axes;
	XVT_ASSERT_INT_EQ(first[XVT_INPUT_AXIS_YAW].source, -1);
	XVT_ASSERT_INT_EQ(first[XVT_INPUT_AXIS_PITCH].source,
			  AERON_GAMEPAD_AXIS_LEFTY);
	XVT_ASSERT_INT_EQ(first[XVT_INPUT_AXIS_ROLL].source,
			  AERON_GAMEPAD_AXIS_RIGHTX);
	const struct xvt_input_axis_binding *second =
		g_options.models[2].profile.mapping.axes;
	for (int axis = 0; axis < XVT_INPUT_AXIS_COUNT; ++axis) {
		XVT_ASSERT_INT_EQ(second[axis].source, -1);
	}
	XVT_ASSERT_TRUE(options_valid(&g_options));

	/* Running again adds nothing. */
	XVT_ASSERT_TRUE(xvt_controller_options_add_new_gamepads(
		&g_options, &defaults, &g_input, g_error, sizeof g_error));
	XVT_ASSERT_INT_EQ(g_options.count, 3);

	/* NULL input is a success that adds nothing. */
	XVT_ASSERT_TRUE(xvt_controller_options_add_new_gamepads(
		&g_options, &defaults, NULL, g_error, sizeof g_error));
	XVT_ASSERT_INT_EQ(g_options.count, 3);

	/* A gamepad that cannot be added fails the call. */
	connect(1, k_guid_d, AERON_CONTROLLER_KIND_GAMEPAD, 2)->guid[3] = 'X';
	g_error[0] = '\0';
	XVT_ASSERT_TRUE(!xvt_controller_options_add_new_gamepads(
		&g_options, &defaults, &g_input, g_error, sizeof g_error));
	XVT_ASSERT_TRUE(g_error[0] != '\0');
}

int main(void)
{
	check_validate_profile_accepts();
	check_validate_profile_axis_refusals();
	check_validate_profile_binding_refusals();
	check_validate_profile_error_capacity();
	check_effective_axis_invert();
	check_profile_equal();
	check_equals();
	check_clear_profile();
	check_find_model();
	check_validate();
	check_add_model();
	check_initialize_gamepads();
	return 0;
}
