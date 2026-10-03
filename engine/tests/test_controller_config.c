/* Checks the controller settings (xvt_runtime/config/controller_config.h) and xvt_config_set_controller
 * against the promises in their headers: the readers and the writer on documents and options this file
 * builds itself, and SetController on settings loaded from a copy of the shipped defaults. Each check starts
 * from a fresh fixture folder (config_fixture.h).
 *
 * Run as "test_controller_config known-failure <check>" for a check that shows the code breaking its
 * header; see main. */
#define _XOPEN_SOURCE 700

#include "config_fixture.h"
#include "test_assert.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/config/controller_config.h"
#include "xvt_runtime/input/controller_options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct xvt_controller_profile g_profile;
static struct xvt_controller_options g_options;

/* Reads the profile at path in text into g_profile and returns what the reader returned. */
static bool read_profile_text(const char *text, const char *path,
			      AeronControllerKind kind)
{
	AeronConfigFile *document = fixture_yaml(text);
	char error[256] = "";
	bool read = xvt_controller_config_read_profile(
		document, path, kind, &g_profile, error, sizeof error);
	XVT_ASSERT_TRUE(read || error[0] != 0);
	AeronConfigFile_Destroy(document);
	return read;
}

/* Parses text's input.controllers into g_options and returns what the parser returned. */
static bool parse_text(const char *text)
{
	AeronConfigFile *document = fixture_yaml(text);
	char error[256] = "";
	bool parsed = xvt_controller_config_parse(document, &g_options, error,
						  sizeof error);
	XVT_ASSERT_TRUE(parsed || error[0] != 0);
	AeronConfigFile_Destroy(document);
	return parsed;
}

static AeronControllerDigitalSource
source(AeronControllerDigitalSourceKind kind, int index, int hat,
       float threshold)
{
	return (AeronControllerDigitalSource){.kind = kind,
					      .index = (uint8_t)index,
					      .hat_direction = (uint8_t)hat,
					      .threshold = threshold};
}

/* 1 when profile binds source to action. Axis thresholds must match too; the thresholds used here are
 * binary fractions, which survive the trip through float exactly. */
static int binds(const struct xvt_controller_profile *profile,
		 xvt_input_action action, AeronControllerDigitalSource source)
{
	for (size_t i = 0; i < profile->binding_count; ++i) {
		const struct xvt_input_action_binding *b =
			&profile->bindings[i];
		if (b->action != action || b->source.kind != source.kind ||
		    b->source.index != source.index) {
			continue;
		}
		if (source.kind == AERON_CONTROLLER_DIGITAL_HAT &&
		    b->source.hat_direction != source.hat_direction) {
			continue;
		}
		if ((source.kind == AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE ||
		     source.kind == AERON_CONTROLLER_DIGITAL_AXIS_NEGATIVE) &&
		    !xvt_test_close(b->source.threshold, source.threshold, 0)) {
			continue;
		}
		return 1;
	}
	return 0;
}

static void check_gamepad_profile(void)
{
	fixture_begin();
	XVT_ASSERT_TRUE(read_profile_text(
		"pad:\n"
		"  axes:\n"
		"    yaw: {source: leftx, invert: true, deadzone: 0.25}\n"
		"    roll: {source: none, invert: false, deadzone: 0}\n"
		"    throttle: {source: righty, invert: false}\n"
		"  buttons:\n"
		"    fire_weapon: [south, {axis: righttrigger, direction: positive}]\n"
		"    target_next: west\n"
		"    target_prev: [east, east]\n",
		"pad", AERON_CONTROLLER_KIND_GAMEPAD));
	const struct xvt_input_axis_binding *axes = g_profile.mapping.axes;
	XVT_ASSERT_INT_EQ(axes[XVT_INPUT_AXIS_YAW].source,
			  AERON_GAMEPAD_AXIS_LEFTX);
	XVT_ASSERT_TRUE(axes[XVT_INPUT_AXIS_YAW].invert);
	XVT_ASSERT_CLOSE(axes[XVT_INPUT_AXIS_YAW].deadzone, 0.25, 0,
			 "0.25 is exact in binary");
	/* "none" unbinds; a throttle without a deadzone has none. */
	XVT_ASSERT_INT_EQ(axes[XVT_INPUT_AXIS_ROLL].source, -1);
	XVT_ASSERT_INT_EQ(axes[XVT_INPUT_AXIS_THROTTLE].source,
			  AERON_GAMEPAD_AXIS_RIGHTY);
	XVT_ASSERT_CLOSE(axes[XVT_INPUT_AXIS_THROTTLE].deadzone, 0, 0,
			 "a cleared deadzone is exactly zero");
	/* An axis left out keeps its cleared value. */
	static struct xvt_controller_profile cleared;
	xvt_controller_options_clear_profile(&cleared,
					     AERON_CONTROLLER_KIND_GAMEPAD);
	XVT_ASSERT_INT_EQ(axes[XVT_INPUT_AXIS_PITCH].source,
			  cleared.mapping.axes[XVT_INPUT_AXIS_PITCH].source);
	XVT_ASSERT_INT_EQ(axes[XVT_INPUT_AXIS_PITCH].invert,
			  cleared.mapping.axes[XVT_INPUT_AXIS_PITCH].invert);
	XVT_ASSERT_CLOSE(axes[XVT_INPUT_AXIS_PITCH].deadzone,
			 cleared.mapping.axes[XVT_INPUT_AXIS_PITCH].deadzone, 0,
			 "a cleared deadzone is exactly zero");

	/* A source repeated for the same action counts once; an axis source's threshold defaults to 0.5. */
	XVT_ASSERT_INT_EQ(g_profile.binding_count, 4);
	XVT_ASSERT_TRUE(binds(&g_profile, XVT_INPUT_ACTION_FIRE_WEAPON,
			      source(AERON_CONTROLLER_DIGITAL_BUTTON,
				     AERON_GAMEPAD_BUTTON_SOUTH, 0, 0)));
	XVT_ASSERT_TRUE(
		binds(&g_profile, XVT_INPUT_ACTION_FIRE_WEAPON,
		      source(AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE,
			     AERON_GAMEPAD_AXIS_RIGHT_TRIGGER, 0, 0.5f)));
	XVT_ASSERT_TRUE(binds(&g_profile, XVT_INPUT_ACTION_TARGET_NEXT,
			      source(AERON_CONTROLLER_DIGITAL_BUTTON,
				     AERON_GAMEPAD_BUTTON_WEST, 0, 0)));
	XVT_ASSERT_TRUE(binds(&g_profile, XVT_INPUT_ACTION_TARGET_PREV,
			      source(AERON_CONTROLLER_DIGITAL_BUTTON,
				     AERON_GAMEPAD_BUTTON_EAST, 0, 0)));
	fixture_end();
}

static void check_joystick_profile(void)
{
	fixture_begin();
	XVT_ASSERT_TRUE(read_profile_text(
		"stick:\n"
		"  axes:\n"
		"    yaw: {source: 0, invert: false, deadzone: 0.5}\n"
		"    pitch: {source: 15, invert: true, deadzone: 1}\n"
		"  buttons:\n"
		"    fire_weapon: {button: 63}\n"
		"    target_next: {hat: 3, direction: left}\n"
		"    target_prev: {axis: 2, direction: negative, threshold: 1}\n"
		"    throttle_up:\n"
		"      - {hat: 0, direction: up}\n"
		"      - {hat: 0, direction: right}\n"
		"      - {hat: 0, direction: down}\n",
		"stick", AERON_CONTROLLER_KIND_JOYSTICK));
	const struct xvt_input_axis_binding *axes = g_profile.mapping.axes;
	XVT_ASSERT_INT_EQ(axes[XVT_INPUT_AXIS_YAW].source, 0);
	XVT_ASSERT_CLOSE(axes[XVT_INPUT_AXIS_YAW].deadzone, 0.5, 0,
			 "0.5 is exact in binary");
	XVT_ASSERT_INT_EQ(axes[XVT_INPUT_AXIS_PITCH].source, 15);
	XVT_ASSERT_TRUE(axes[XVT_INPUT_AXIS_PITCH].invert);
	/* The throttle, left out, keeps its cleared value: a joystick's throttle starts inverted. */
	static struct xvt_controller_profile cleared;
	xvt_controller_options_clear_profile(&cleared,
					     AERON_CONTROLLER_KIND_JOYSTICK);
	XVT_ASSERT_INT_EQ(axes[XVT_INPUT_AXIS_THROTTLE].source, -1);
	XVT_ASSERT_INT_EQ(axes[XVT_INPUT_AXIS_THROTTLE].invert,
			  cleared.mapping.axes[XVT_INPUT_AXIS_THROTTLE].invert);
	XVT_ASSERT_TRUE(axes[XVT_INPUT_AXIS_THROTTLE].invert);

	XVT_ASSERT_INT_EQ(g_profile.binding_count, 6);
	XVT_ASSERT_TRUE(
		binds(&g_profile, XVT_INPUT_ACTION_FIRE_WEAPON,
		      source(AERON_CONTROLLER_DIGITAL_BUTTON, 63, 0, 0)));
	XVT_ASSERT_TRUE(binds(&g_profile, XVT_INPUT_ACTION_TARGET_NEXT,
			      source(AERON_CONTROLLER_DIGITAL_HAT, 3,
				     AERON_CONTROLLER_HAT_LEFT, 0)));
	XVT_ASSERT_TRUE(binds(
		&g_profile, XVT_INPUT_ACTION_TARGET_PREV,
		source(AERON_CONTROLLER_DIGITAL_AXIS_NEGATIVE, 2, 0, 1.0f)));
	XVT_ASSERT_TRUE(binds(&g_profile, XVT_INPUT_ACTION_THROTTLE_UP,
			      source(AERON_CONTROLLER_DIGITAL_HAT, 0,
				     AERON_CONTROLLER_HAT_UP, 0)));
	XVT_ASSERT_TRUE(binds(&g_profile, XVT_INPUT_ACTION_THROTTLE_UP,
			      source(AERON_CONTROLLER_DIGITAL_HAT, 0,
				     AERON_CONTROLLER_HAT_RIGHT, 0)));
	XVT_ASSERT_TRUE(binds(&g_profile, XVT_INPUT_ACTION_THROTTLE_UP,
			      source(AERON_CONTROLLER_DIGITAL_HAT, 0,
				     AERON_CONTROLLER_HAT_DOWN, 0)));
	fixture_end();
}

/* A gamepad profile at "pad" with yaw on leftx and the given buttons block. */
static bool pad_with_buttons(const char *buttons)
{
	char text[512];
	snprintf(
		text, sizeof text,
		"pad:\n  axes:\n    yaw: {source: leftx, invert: false, deadzone: 0}\n  buttons:\n%s",
		buttons);
	return read_profile_text(text, "pad", AERON_CONTROLLER_KIND_GAMEPAD);
}

/* A joystick profile at "stick" with the given buttons block. */
static bool stick_with_buttons(const char *buttons)
{
	char text[512];
	snprintf(text, sizeof text, "stick:\n  buttons:\n%s", buttons);
	return read_profile_text(text, "stick", AERON_CONTROLLER_KIND_JOYSTICK);
}

/* A gamepad profile at "pad" with the given axes block. */
static bool pad_with_axes(const char *axes)
{
	char text[512];
	snprintf(text, sizeof text, "pad:\n  axes:\n%s", axes);
	return read_profile_text(text, "pad", AERON_CONTROLLER_KIND_GAMEPAD);
}

static void check_profile_refusals(void)
{
	fixture_begin();
	/* The layout must be gamepad or joystick, and the mapping must be there. */
	XVT_ASSERT_TRUE(pad_with_buttons("    fire_weapon: south\n"));
	XVT_ASSERT_TRUE(
		!read_profile_text("pad:\n  buttons:\n    fire_weapon: south\n",
				   "pad", AERON_CONTROLLER_KIND_NONE));
	XVT_ASSERT_TRUE(
		!read_profile_text("pad:\n  buttons:\n    fire_weapon: south\n",
				   "absent", AERON_CONTROLLER_KIND_GAMEPAD));
	XVT_ASSERT_TRUE(!read_profile_text("pad: 5\n", "pad",
					   AERON_CONTROLLER_KIND_GAMEPAD));

	/* Axes: known names and fields only, invert and deadzone present, deadzone 0 to 1, throttle's 0. */
	XVT_ASSERT_TRUE(!pad_with_axes(
		"    strafe: {source: leftx, invert: false, deadzone: 0}\n"));
	XVT_ASSERT_TRUE(!pad_with_axes(
		"    yaw: {source: leftx, invert: false, deadzone: 0, gain: 2}\n"));
	XVT_ASSERT_TRUE(
		!pad_with_axes("    yaw: {source: leftx, deadzone: 0}\n"));
	XVT_ASSERT_TRUE(
		!pad_with_axes("    yaw: {source: leftx, invert: false}\n"));
	XVT_ASSERT_TRUE(pad_with_axes(
		"    yaw: {source: leftx, invert: false, deadzone: 1}\n"));
	XVT_ASSERT_TRUE(!pad_with_axes(
		"    yaw: {source: leftx, invert: false, deadzone: 1.5}\n"));
	XVT_ASSERT_TRUE(!pad_with_axes(
		"    yaw: {source: leftx, invert: false, deadzone: -0.25}\n"));
	XVT_ASSERT_TRUE(pad_with_axes(
		"    throttle: {source: righty, invert: false, deadzone: 0}\n"));
	XVT_ASSERT_TRUE(!pad_with_axes(
		"    throttle: {source: righty, invert: false, deadzone: 0.25}\n"));
	/* Axis sources: a gamepad axis name; on a joystick an index within its axes; never shared. */
	XVT_ASSERT_TRUE(!pad_with_axes(
		"    yaw: {source: middlex, invert: false, deadzone: 0}\n"));
	XVT_ASSERT_TRUE(!pad_with_axes(
		"    yaw: {source: leftx, invert: false, deadzone: 0}\n"
		"    pitch: {source: leftx, invert: false, deadzone: 0}\n"));
	XVT_ASSERT_TRUE(read_profile_text(
		"stick:\n  axes:\n    yaw: {source: 15, invert: false, deadzone: 0}\n",
		"stick", AERON_CONTROLLER_KIND_JOYSTICK));
	XVT_ASSERT_TRUE(!read_profile_text(
		"stick:\n  axes:\n    yaw: {source: 16, invert: false, deadzone: 0}\n",
		"stick", AERON_CONTROLLER_KIND_JOYSTICK));

	/* Buttons: known actions; a button name only on a gamepad; {button} and hats only on a joystick. */
	XVT_ASSERT_TRUE(!pad_with_buttons("    fly_backwards: south\n"));
	XVT_ASSERT_TRUE(!pad_with_buttons("    fire_weapon: purple\n"));
	XVT_ASSERT_TRUE(!pad_with_buttons("    fire_weapon: {button: 1}\n"));
	XVT_ASSERT_TRUE(!pad_with_buttons(
		"    fire_weapon: {hat: 0, direction: up}\n"));
	XVT_ASSERT_TRUE(!stick_with_buttons("    fire_weapon: south\n"));
	XVT_ASSERT_TRUE(!stick_with_buttons("    fire_weapon: {button: 64}\n"));
	XVT_ASSERT_TRUE(!stick_with_buttons(
		"    fire_weapon: {hat: 4, direction: up}\n"));
	/* Directions, and thresholds in (0, 1]. */
	XVT_ASSERT_TRUE(!stick_with_buttons(
		"    fire_weapon: {axis: 1, direction: sideways}\n"));
	XVT_ASSERT_TRUE(!stick_with_buttons(
		"    fire_weapon: {hat: 0, direction: upleft}\n"));
	XVT_ASSERT_TRUE(!stick_with_buttons(
		"    fire_weapon: {axis: 1, direction: positive, threshold: 0}\n"));
	XVT_ASSERT_TRUE(!stick_with_buttons(
		"    fire_weapon: {axis: 1, direction: positive, threshold: 1.5}\n"));
	/* An axis source takes no fourth field; its axis is a name on a gamepad
	 * and an index within the axes on a joystick. */
	XVT_ASSERT_TRUE(!stick_with_buttons(
		"    fire_weapon: {axis: 1, direction: positive, threshold: 0.5, extra: 2}\n"));
	XVT_ASSERT_TRUE(!pad_with_buttons(
		"    fire_weapon: {axis: 1, direction: positive}\n"));
	XVT_ASSERT_TRUE(!pad_with_buttons(
		"    fire_weapon: {axis: middlex, direction: positive}\n"));
	XVT_ASSERT_TRUE(!stick_with_buttons(
		"    fire_weapon: {axis: 16, direction: positive}\n"));
	/* A mapping that is none of the source forms. */
	XVT_ASSERT_TRUE(
		!stick_with_buttons("    fire_weapon: {direction: up}\n"));
	/* A source bound to two actions. */
	XVT_ASSERT_TRUE(!pad_with_buttons(
		"    fire_weapon: south\n    target_next: south\n"));
	/* Unknown fields in a button or hat source. */
	XVT_ASSERT_TRUE(!stick_with_buttons(
		"    fire_weapon: {button: 1, extra: 2}\n"));
	XVT_ASSERT_TRUE(!stick_with_buttons(
		"    fire_weapon: {hat: 0, direction: up, extra: 2}\n"));
	/* An unknown field beside the axes and buttons of the gamepad defaults. */
	XVT_ASSERT_TRUE(!read_profile_text(
		"input:\n  gamepad_defaults:\n    buttons: {}\n    color: red\n",
		"input.gamepad_defaults", AERON_CONTROLLER_KIND_GAMEPAD));
	fixture_end();
}

/* Known failure. The header says unknown fields fail. An axis source with a misspelt threshold field,
 * {axis, direction, threshhold}, is accepted: the misspelt field is ignored and the default threshold of
 * 0.5 is used. */
static void check_axis_source_unknown_field(void)
{
	fixture_begin();
	XVT_ASSERT_TRUE(!stick_with_buttons(
		"    fire_weapon: {axis: 1, direction: positive, threshhold: 0.9}\n"));
	fixture_end();
}

/* A guid of 32 hex digits whose last two are number in hex. */
static void controller_config_guid(char *guid, int number)
{
	snprintf(guid, 33, "%030d%02x", 0, number);
}

/* A controllers document with count gamepad models and no axes, each with its own guid. */
static char *gamepad_models(int count)
{
	size_t capacity = 64 + (size_t)count * 128;
	char *text = malloc(capacity);
	XVT_ASSERT_TRUE(text != NULL);
	size_t used =
		(size_t)snprintf(text, capacity, "input:\n  controllers:\n");
	for (int i = 1; i <= count; ++i) {
		char guid[33];
		controller_config_guid(guid, i);
		used += (size_t)snprintf(
			text + used, capacity - used,
			"    - {guid: \"%s\", name: \"Pad %d\", layout: gamepad}\n",
			guid, i);
	}
	return text;
}

static void check_parse(void)
{
	fixture_begin();
	XVT_ASSERT_TRUE(parse_text(
		"input:\n"
		"  controllers:\n"
		"    - guid: \"0123456789ABCDEF0123456789ABCDEF\"\n"
		"      name: \"Test Pad\"\n"
		"      layout: gamepad\n"
		"      axes:\n"
		"        yaw: {source: leftx, invert: false, deadzone: 0.25}\n"
		"      buttons:\n"
		"        fire_weapon: south\n"
		"    - guid: \"00000000000000000000000000000002\"\n"
		"      name: \"Test Stick\"\n"
		"      layout: joystick\n"
		"      axes:\n"
		"        pitch: {source: 1, invert: false, deadzone: 0}\n"));
	XVT_ASSERT_INT_EQ(g_options.count, 2);
	/* guids are stored in lower case. */
	XVT_ASSERT_INT_EQ(strcmp(g_options.models[0].guid,
				 "0123456789abcdef0123456789abcdef"),
			  0);
	XVT_ASSERT_INT_EQ(strcmp(g_options.models[0].name, "Test Pad"), 0);
	XVT_ASSERT_INT_EQ(g_options.models[0].kind,
			  AERON_CONTROLLER_KIND_GAMEPAD);
	XVT_ASSERT_INT_EQ(g_options.models[0]
				  .profile.mapping.axes[XVT_INPUT_AXIS_YAW]
				  .source,
			  AERON_GAMEPAD_AXIS_LEFTX);
	XVT_ASSERT_TRUE(binds(&g_options.models[0].profile,
			      XVT_INPUT_ACTION_FIRE_WEAPON,
			      source(AERON_CONTROLLER_DIGITAL_BUTTON,
				     AERON_GAMEPAD_BUTTON_SOUTH, 0, 0)));
	XVT_ASSERT_INT_EQ(strcmp(g_options.models[1].guid,
				 "00000000000000000000000000000002"),
			  0);
	XVT_ASSERT_INT_EQ(g_options.models[1].kind,
			  AERON_CONTROLLER_KIND_JOYSTICK);
	XVT_ASSERT_INT_EQ(g_options.models[1]
				  .profile.mapping.axes[XVT_INPUT_AXIS_PITCH]
				  .source,
			  1);

	/* An empty list is a valid list. */
	XVT_ASSERT_TRUE(parse_text("input:\n  controllers: []\n"));
	XVT_ASSERT_INT_EQ(g_options.count, 0);

	/* At most XVT_CONTROLLER_MODEL_CAP models. */
	char *text = gamepad_models(XVT_CONTROLLER_MODEL_CAP);
	XVT_ASSERT_TRUE(parse_text(text));
	XVT_ASSERT_INT_EQ(g_options.count, XVT_CONTROLLER_MODEL_CAP);
	free(text);
	text = gamepad_models(XVT_CONTROLLER_MODEL_CAP + 1);
	XVT_ASSERT_TRUE(!parse_text(text));
	free(text);
	fixture_end();
}

static void check_parse_refusals(void)
{
	fixture_begin();
	const char *pad = "\"00000000000000000000000000000001\"";
	char text[512];
	/* The list is required, and must be a list. */
	XVT_ASSERT_TRUE(!parse_text("input:\n  mouse_flight: true\n"));
	XVT_ASSERT_TRUE(!parse_text("input:\n  controllers: {}\n"));
	/* Only the five fields; guid and name are required, the layout is gamepad or joystick. */
	snprintf(
		text, sizeof text,
		"input:\n  controllers:\n    - {guid: %s, name: P, layout: gamepad, color: red}\n",
		pad);
	XVT_ASSERT_TRUE(!parse_text(text));
	XVT_ASSERT_TRUE(!parse_text(
		"input:\n  controllers:\n    - {name: P, layout: gamepad}\n"));
	snprintf(text, sizeof text,
		 "input:\n  controllers:\n    - {guid: %s, layout: gamepad}\n",
		 pad);
	XVT_ASSERT_TRUE(!parse_text(text));
	snprintf(
		text, sizeof text,
		"input:\n  controllers:\n    - {guid: %s, name: P, layout: wheel}\n",
		pad);
	XVT_ASSERT_TRUE(!parse_text(text));
	/* The whole set is validated: guids of 32 hex digits, not all zero, not repeated. */
	XVT_ASSERT_TRUE(!parse_text(
		"input:\n  controllers:\n    - {guid: \"abc\", name: P, layout: gamepad}\n"));
	XVT_ASSERT_TRUE(!parse_text(
		"input:\n  controllers:\n"
		"    - {guid: \"00000000000000000000000000000000\", name: P, layout: gamepad}\n"));
	snprintf(
		text, sizeof text,
		"input:\n  controllers:\n    - {guid: %s, name: P, layout: gamepad}\n"
		"    - {guid: %s, name: Q, layout: gamepad}\n",
		pad, pad);
	XVT_ASSERT_TRUE(!parse_text(text));
	/* No flight axis driven by two models. */
	XVT_ASSERT_TRUE(!parse_text(
		"input:\n  controllers:\n"
		"    - guid: \"00000000000000000000000000000001\"\n"
		"      name: P\n"
		"      layout: gamepad\n"
		"      axes: {yaw: {source: leftx, invert: false, deadzone: 0}}\n"
		"    - guid: \"00000000000000000000000000000002\"\n"
		"      name: Q\n"
		"      layout: gamepad\n"
		"      axes: {yaw: {source: rightx, invert: false, deadzone: 0}}\n"));
	fixture_end();
}

/* Two valid models: a gamepad whose bindings list fire_weapon, then target_next, then fire_weapon again,
 * and a joystick with a hat, a button and an axis source. Button and hat thresholds are 0.5, the default. */
static void two_models(struct xvt_controller_options *options)
{
	memset(options, 0, sizeof *options);
	options->count = 2;
	struct xvt_controller_model *pad = &options->models[0];
	strcpy(pad->guid, "0123456789abcdef0123456789abcdef");
	strcpy(pad->name, "Test Pad");
	pad->kind = AERON_CONTROLLER_KIND_GAMEPAD;
	xvt_controller_options_clear_profile(&pad->profile,
					     AERON_CONTROLLER_KIND_GAMEPAD);
	pad->profile.mapping.axes[XVT_INPUT_AXIS_YAW] =
		(struct xvt_input_axis_binding){
			.source = AERON_GAMEPAD_AXIS_LEFTX,
			.invert = true,
			.deadzone = 0.25f};
	pad->profile.mapping.axes[XVT_INPUT_AXIS_THROTTLE] =
		(struct xvt_input_axis_binding){
			.source = AERON_GAMEPAD_AXIS_RIGHTY,
			.invert = false,
			.deadzone = 0.0f};
	pad->profile.bindings[0] = (struct xvt_input_action_binding){
		source(AERON_CONTROLLER_DIGITAL_BUTTON,
		       AERON_GAMEPAD_BUTTON_SOUTH, 0, 0.5f),
		XVT_INPUT_ACTION_FIRE_WEAPON};
	pad->profile.bindings[1] = (struct xvt_input_action_binding){
		source(AERON_CONTROLLER_DIGITAL_BUTTON,
		       AERON_GAMEPAD_BUTTON_WEST, 0, 0.5f),
		XVT_INPUT_ACTION_TARGET_NEXT};
	pad->profile.bindings[2] = (struct xvt_input_action_binding){
		source(AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE,
		       AERON_GAMEPAD_AXIS_RIGHT_TRIGGER, 0, 0.75f),
		XVT_INPUT_ACTION_FIRE_WEAPON};
	pad->profile.binding_count = 3;

	struct xvt_controller_model *stick = &options->models[1];
	strcpy(stick->guid, "00000000000000000000000000000002");
	strcpy(stick->name, "Test Stick");
	stick->kind = AERON_CONTROLLER_KIND_JOYSTICK;
	xvt_controller_options_clear_profile(&stick->profile,
					     AERON_CONTROLLER_KIND_JOYSTICK);
	stick->profile.mapping.axes[XVT_INPUT_AXIS_PITCH] =
		(struct xvt_input_axis_binding){
			.source = 3, .invert = false, .deadzone = 0.5f};
	stick->profile.bindings[0] = (struct xvt_input_action_binding){
		source(AERON_CONTROLLER_DIGITAL_HAT, 1,
		       AERON_CONTROLLER_HAT_DOWN, 0.5f),
		XVT_INPUT_ACTION_TARGET_PREV};
	stick->profile.bindings[1] = (struct xvt_input_action_binding){
		source(AERON_CONTROLLER_DIGITAL_BUTTON, 7, 0, 0.5f),
		XVT_INPUT_ACTION_THROTTLE_UP};
	stick->profile.bindings[2] = (struct xvt_input_action_binding){
		source(AERON_CONTROLLER_DIGITAL_AXIS_NEGATIVE, 2, 0, 1.0f),
		XVT_INPUT_ACTION_THROTTLE_DOWN};
	stick->profile.binding_count = 3;
}

static void check_write(void)
{
	fixture_begin();
	static struct xvt_controller_options options, reordered, parsed;
	two_models(&options);
	const char *start =
		"input:\n  mouse_flight: true\n  controllers: [{guid: x}, {guid: y}, {guid: z}]\n";
	AeronConfigFile *document = fixture_yaml(start);
	AeronConfigError detail;
	XVT_ASSERT_TRUE(
		xvt_controller_config_write(document, &options, &detail));

	/* input.controllers is replaced; the rest of the document stays. */
	XVT_ASSERT_INT_EQ(AeronConfigNode_SequenceCount(AeronConfigFile_GetNode(
				  document, "input.controllers")),
			  2);
	XVT_ASSERT_INT_EQ(
		AeronConfigFile_GetBool(document, "input.mouse_flight", 0), 1);

	/* Every model has all four axes, and the throttle has no deadzone. */
	static const char *const axes[] = {"yaw", "pitch", "roll", "throttle"};
	for (int model = 0; model < 2; ++model) {
		for (int axis = 0; axis < 4; ++axis) {
			char path[128];
			snprintf(path, sizeof path,
				 "input.controllers[%d].axes.%s.source", model,
				 axes[axis]);
			XVT_ASSERT_TRUE(AeronConfigFile_Has(document, path));
			snprintf(path, sizeof path,
				 "input.controllers[%d].axes.%s.invert", model,
				 axes[axis]);
			XVT_ASSERT_TRUE(AeronConfigFile_Has(document, path));
			snprintf(path, sizeof path,
				 "input.controllers[%d].axes.%s.deadzone",
				 model, axes[axis]);
			XVT_ASSERT_INT_EQ(AeronConfigFile_Has(document, path),
					  axis != XVT_INPUT_AXIS_THROTTLE);
		}
	}
	/* The gamepad's three bindings are grouped under their two actions. */
	const AeronConfigNode *buttons = AeronConfigFile_GetNode(
		document, "input.controllers[0].buttons");
	XVT_ASSERT_INT_EQ(AeronConfigNode_MapCount(buttons), 2);
	XVT_ASSERT_INT_EQ(AeronConfigNode_SequenceCount(AeronConfigNode_MapGet(
				  buttons, "fire_weapon")),
			  2);

	/* Reading the list back gives the same models. */
	char error[256] = "";
	XVT_ASSERT_TRUE(xvt_controller_config_parse(document, &parsed, error,
						    sizeof error));
	XVT_ASSERT_INT_EQ(parsed.count, options.count);
	for (size_t m = 0; m < options.count; ++m) {
		const struct xvt_controller_model *want = &options.models[m];
		const struct xvt_controller_model *got = &parsed.models[m];
		XVT_ASSERT_INT_EQ(strcmp(got->guid, want->guid), 0);
		XVT_ASSERT_INT_EQ(strcmp(got->name, want->name), 0);
		XVT_ASSERT_INT_EQ(got->kind, want->kind);
		for (int axis = 0; axis < XVT_INPUT_AXIS_COUNT; ++axis) {
			XVT_ASSERT_INT_EQ(
				got->profile.mapping.axes[axis].source,
				want->profile.mapping.axes[axis].source);
			XVT_ASSERT_INT_EQ(
				got->profile.mapping.axes[axis].invert,
				want->profile.mapping.axes[axis].invert);
			XVT_ASSERT_CLOSE(
				got->profile.mapping.axes[axis].deadzone,
				want->profile.mapping.axes[axis].deadzone, 0,
				"the deadzones here are binary fractions, which survive the trip exactly");
		}
		XVT_ASSERT_INT_EQ(got->profile.binding_count,
				  want->profile.binding_count);
		for (size_t b = 0; b < want->profile.binding_count; ++b) {
			XVT_ASSERT_TRUE(binds(
				&got->profile, want->profile.bindings[b].action,
				want->profile.bindings[b].source));
		}
	}

	/* The order is fixed: the same bindings listed in another order are written the same way. */
	reordered = options;
	struct xvt_controller_profile *profile = &reordered.models[0].profile;
	struct xvt_input_action_binding first = profile->bindings[0];
	profile->bindings[0] = profile->bindings[2];
	profile->bindings[2] = first;
	AeronConfigFile *other = fixture_yaml(start);
	XVT_ASSERT_TRUE(
		xvt_controller_config_write(other, &reordered, &detail));
	XVT_ASSERT_TRUE(fixture_same_document(document, other));
	AeronConfigFile_Destroy(other);
	AeronConfigFile_Destroy(document);

	/* The writer does not validate. */
	reordered = options;
	strcpy(reordered.models[0].guid, "NOT-A-GUID");
	document = fixture_yaml("version: 3\n");
	XVT_ASSERT_TRUE(
		xvt_controller_config_write(document, &reordered, &detail));
	XVT_ASSERT_INT_EQ(
		strcmp(AeronConfigFile_GetString(
			       document, "input.controllers[0].guid", ""),
		       "NOT-A-GUID"),
		0);
	AeronConfigFile_Destroy(document);
	fixture_end();
}

static void check_set_controller(void)
{
	fixture_begin();
	static struct xvt_controller_options options, expected;
	char error[512];
	two_models(&options);
	/* Needs loaded settings. */
	XVT_ASSERT_TRUE(
		!xvt_config_set_controller(&options, error, sizeof error));

	fixture_load();
	uint64_t generation = xvt_config_generation();
	XVT_ASSERT_TRUE(
		xvt_config_set_controller(&options, error, sizeof error));
	XVT_ASSERT_TRUE(xvt_config_generation() > generation);
	XVT_ASSERT_TRUE(!fixture_exists("user/config.yaml"));
	/* The settings now hold the list as the writer stores it. */
	AeronConfigFile *written = fixture_yaml("version: 3\n");
	AeronConfigError detail;
	XVT_ASSERT_TRUE(
		xvt_controller_config_write(written, &options, &detail));
	XVT_ASSERT_TRUE(xvt_controller_config_parse(written, &expected, error,
						    sizeof error));
	AeronConfigFile_Destroy(written);
	XVT_ASSERT_TRUE(xvt_controller_options_equals(
		&xvt_config_settings()->controller, &expected));

	/* Invalid options are refused before anything changes. */
	generation = xvt_config_generation();
	AeronConfigFile *before = fixture_user_copy();
	strcpy(options.models[0].guid, "0123456789ABCDEF0123456789ABCDEF");
	XVT_ASSERT_TRUE(
		!xvt_config_set_controller(&options, error, sizeof error));
	XVT_ASSERT_INT_EQ(xvt_config_generation(), generation);
	XVT_ASSERT_TRUE(
		fixture_same_document(xvt_config_user_document(), before));
	XVT_ASSERT_TRUE(xvt_controller_options_equals(
		&xvt_config_settings()->controller, &expected));
	AeronConfigFile_Destroy(before);
	fixture_end();
}

/* With no arguments, runs every check that holds. "known-failure <name>" runs only that check, which shows
 * the code breaking its header; a name not listed here returns 0. */
int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		if (strcmp(argv[2], "axis_source_unknown_field") == 0) {
			check_axis_source_unknown_field();
		}
		return 0;
	}
	check_gamepad_profile();
	check_joystick_profile();
	check_profile_refusals();
	check_parse();
	check_parse_refusals();
	check_write();
	check_set_controller();
	return 0;
}
