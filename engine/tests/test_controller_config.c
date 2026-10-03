/* Checks the controller settings (xvt_runtime/config/controller_config.h) and XvtConfig_SetController
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

static struct XvtControllerProfile g_profile;
static struct XvtControllerOptions g_options;

/* Reads the profile at path in text into g_profile and returns what the reader returned. */
static bool ReadProfileText(const char *text, const char *path,
			    AeronControllerKind kind)
{
	AeronConfigFile *document = Fixture_Yaml(text);
	char error[256] = "";
	bool read = XvtControllerConfig_ReadProfile(
		document, path, kind, &g_profile, error, sizeof error);
	XVT_ASSERT_TRUE(read || error[0] != 0);
	AeronConfigFile_Destroy(document);
	return read;
}

/* Parses text's input.controllers into g_options and returns what the parser returned. */
static bool ParseText(const char *text)
{
	AeronConfigFile *document = Fixture_Yaml(text);
	char error[256] = "";
	bool parsed = XvtControllerConfig_Parse(document, &g_options, error,
						sizeof error);
	XVT_ASSERT_TRUE(parsed || error[0] != 0);
	AeronConfigFile_Destroy(document);
	return parsed;
}

static AeronControllerDigitalSource
Source(AeronControllerDigitalSourceKind kind, int index, int hat,
       float threshold)
{
	return (AeronControllerDigitalSource){.kind = kind,
					      .index = (uint8_t)index,
					      .hat_direction = (uint8_t)hat,
					      .threshold = threshold};
}

/* 1 when profile binds source to action. Axis thresholds must match too; the thresholds used here are
 * binary fractions, which survive the trip through float exactly. */
static int Binds(const struct XvtControllerProfile *profile,
		 XvtInputAction action, AeronControllerDigitalSource source)
{
	for (size_t i = 0; i < profile->binding_count; ++i) {
		const struct XvtInputActionBinding *b = &profile->bindings[i];
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

static void CheckGamepadProfile(void)
{
	Fixture_Begin();
	XVT_ASSERT_TRUE(ReadProfileText(
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
	const struct XvtInputAxisBinding *axes = g_profile.mapping.axes;
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
	static struct XvtControllerProfile cleared;
	XvtControllerOptions_ClearProfile(&cleared,
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
	XVT_ASSERT_TRUE(Binds(&g_profile, XVT_INPUT_ACTION_FIRE_WEAPON,
			      Source(AERON_CONTROLLER_DIGITAL_BUTTON,
				     AERON_GAMEPAD_BUTTON_SOUTH, 0, 0)));
	XVT_ASSERT_TRUE(
		Binds(&g_profile, XVT_INPUT_ACTION_FIRE_WEAPON,
		      Source(AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE,
			     AERON_GAMEPAD_AXIS_RIGHT_TRIGGER, 0, 0.5f)));
	XVT_ASSERT_TRUE(Binds(&g_profile, XVT_INPUT_ACTION_TARGET_NEXT,
			      Source(AERON_CONTROLLER_DIGITAL_BUTTON,
				     AERON_GAMEPAD_BUTTON_WEST, 0, 0)));
	XVT_ASSERT_TRUE(Binds(&g_profile, XVT_INPUT_ACTION_TARGET_PREV,
			      Source(AERON_CONTROLLER_DIGITAL_BUTTON,
				     AERON_GAMEPAD_BUTTON_EAST, 0, 0)));
	Fixture_End();
}

static void CheckJoystickProfile(void)
{
	Fixture_Begin();
	XVT_ASSERT_TRUE(ReadProfileText(
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
	const struct XvtInputAxisBinding *axes = g_profile.mapping.axes;
	XVT_ASSERT_INT_EQ(axes[XVT_INPUT_AXIS_YAW].source, 0);
	XVT_ASSERT_CLOSE(axes[XVT_INPUT_AXIS_YAW].deadzone, 0.5, 0,
			 "0.5 is exact in binary");
	XVT_ASSERT_INT_EQ(axes[XVT_INPUT_AXIS_PITCH].source, 15);
	XVT_ASSERT_TRUE(axes[XVT_INPUT_AXIS_PITCH].invert);
	/* The throttle, left out, keeps its cleared value: a joystick's throttle starts inverted. */
	static struct XvtControllerProfile cleared;
	XvtControllerOptions_ClearProfile(&cleared,
					  AERON_CONTROLLER_KIND_JOYSTICK);
	XVT_ASSERT_INT_EQ(axes[XVT_INPUT_AXIS_THROTTLE].source, -1);
	XVT_ASSERT_INT_EQ(axes[XVT_INPUT_AXIS_THROTTLE].invert,
			  cleared.mapping.axes[XVT_INPUT_AXIS_THROTTLE].invert);
	XVT_ASSERT_TRUE(axes[XVT_INPUT_AXIS_THROTTLE].invert);

	XVT_ASSERT_INT_EQ(g_profile.binding_count, 6);
	XVT_ASSERT_TRUE(
		Binds(&g_profile, XVT_INPUT_ACTION_FIRE_WEAPON,
		      Source(AERON_CONTROLLER_DIGITAL_BUTTON, 63, 0, 0)));
	XVT_ASSERT_TRUE(Binds(&g_profile, XVT_INPUT_ACTION_TARGET_NEXT,
			      Source(AERON_CONTROLLER_DIGITAL_HAT, 3,
				     AERON_CONTROLLER_HAT_LEFT, 0)));
	XVT_ASSERT_TRUE(Binds(
		&g_profile, XVT_INPUT_ACTION_TARGET_PREV,
		Source(AERON_CONTROLLER_DIGITAL_AXIS_NEGATIVE, 2, 0, 1.0f)));
	XVT_ASSERT_TRUE(Binds(&g_profile, XVT_INPUT_ACTION_THROTTLE_UP,
			      Source(AERON_CONTROLLER_DIGITAL_HAT, 0,
				     AERON_CONTROLLER_HAT_UP, 0)));
	XVT_ASSERT_TRUE(Binds(&g_profile, XVT_INPUT_ACTION_THROTTLE_UP,
			      Source(AERON_CONTROLLER_DIGITAL_HAT, 0,
				     AERON_CONTROLLER_HAT_RIGHT, 0)));
	XVT_ASSERT_TRUE(Binds(&g_profile, XVT_INPUT_ACTION_THROTTLE_UP,
			      Source(AERON_CONTROLLER_DIGITAL_HAT, 0,
				     AERON_CONTROLLER_HAT_DOWN, 0)));
	Fixture_End();
}

/* A gamepad profile at "pad" with yaw on leftx and the given buttons block. */
static bool PadWithButtons(const char *buttons)
{
	char text[512];
	snprintf(
		text, sizeof text,
		"pad:\n  axes:\n    yaw: {source: leftx, invert: false, deadzone: 0}\n  buttons:\n%s",
		buttons);
	return ReadProfileText(text, "pad", AERON_CONTROLLER_KIND_GAMEPAD);
}

/* A joystick profile at "stick" with the given buttons block. */
static bool StickWithButtons(const char *buttons)
{
	char text[512];
	snprintf(text, sizeof text, "stick:\n  buttons:\n%s", buttons);
	return ReadProfileText(text, "stick", AERON_CONTROLLER_KIND_JOYSTICK);
}

/* A gamepad profile at "pad" with the given axes block. */
static bool PadWithAxes(const char *axes)
{
	char text[512];
	snprintf(text, sizeof text, "pad:\n  axes:\n%s", axes);
	return ReadProfileText(text, "pad", AERON_CONTROLLER_KIND_GAMEPAD);
}

static void CheckProfileRefusals(void)
{
	Fixture_Begin();
	/* The layout must be gamepad or joystick, and the mapping must be there. */
	XVT_ASSERT_TRUE(PadWithButtons("    fire_weapon: south\n"));
	XVT_ASSERT_TRUE(
		!ReadProfileText("pad:\n  buttons:\n    fire_weapon: south\n",
				 "pad", AERON_CONTROLLER_KIND_NONE));
	XVT_ASSERT_TRUE(
		!ReadProfileText("pad:\n  buttons:\n    fire_weapon: south\n",
				 "absent", AERON_CONTROLLER_KIND_GAMEPAD));
	XVT_ASSERT_TRUE(!ReadProfileText("pad: 5\n", "pad",
					 AERON_CONTROLLER_KIND_GAMEPAD));

	/* Axes: known names and fields only, invert and deadzone present, deadzone 0 to 1, throttle's 0. */
	XVT_ASSERT_TRUE(!PadWithAxes(
		"    strafe: {source: leftx, invert: false, deadzone: 0}\n"));
	XVT_ASSERT_TRUE(!PadWithAxes(
		"    yaw: {source: leftx, invert: false, deadzone: 0, gain: 2}\n"));
	XVT_ASSERT_TRUE(
		!PadWithAxes("    yaw: {source: leftx, deadzone: 0}\n"));
	XVT_ASSERT_TRUE(
		!PadWithAxes("    yaw: {source: leftx, invert: false}\n"));
	XVT_ASSERT_TRUE(PadWithAxes(
		"    yaw: {source: leftx, invert: false, deadzone: 1}\n"));
	XVT_ASSERT_TRUE(!PadWithAxes(
		"    yaw: {source: leftx, invert: false, deadzone: 1.5}\n"));
	XVT_ASSERT_TRUE(!PadWithAxes(
		"    yaw: {source: leftx, invert: false, deadzone: -0.25}\n"));
	XVT_ASSERT_TRUE(PadWithAxes(
		"    throttle: {source: righty, invert: false, deadzone: 0}\n"));
	XVT_ASSERT_TRUE(!PadWithAxes(
		"    throttle: {source: righty, invert: false, deadzone: 0.25}\n"));
	/* Axis sources: a gamepad axis name; on a joystick an index within its axes; never shared. */
	XVT_ASSERT_TRUE(!PadWithAxes(
		"    yaw: {source: middlex, invert: false, deadzone: 0}\n"));
	XVT_ASSERT_TRUE(!PadWithAxes(
		"    yaw: {source: leftx, invert: false, deadzone: 0}\n"
		"    pitch: {source: leftx, invert: false, deadzone: 0}\n"));
	XVT_ASSERT_TRUE(ReadProfileText(
		"stick:\n  axes:\n    yaw: {source: 15, invert: false, deadzone: 0}\n",
		"stick", AERON_CONTROLLER_KIND_JOYSTICK));
	XVT_ASSERT_TRUE(!ReadProfileText(
		"stick:\n  axes:\n    yaw: {source: 16, invert: false, deadzone: 0}\n",
		"stick", AERON_CONTROLLER_KIND_JOYSTICK));

	/* Buttons: known actions; a button name only on a gamepad; {button} and hats only on a joystick. */
	XVT_ASSERT_TRUE(!PadWithButtons("    fly_backwards: south\n"));
	XVT_ASSERT_TRUE(!PadWithButtons("    fire_weapon: purple\n"));
	XVT_ASSERT_TRUE(!PadWithButtons("    fire_weapon: {button: 1}\n"));
	XVT_ASSERT_TRUE(
		!PadWithButtons("    fire_weapon: {hat: 0, direction: up}\n"));
	XVT_ASSERT_TRUE(!StickWithButtons("    fire_weapon: south\n"));
	XVT_ASSERT_TRUE(!StickWithButtons("    fire_weapon: {button: 64}\n"));
	XVT_ASSERT_TRUE(!StickWithButtons(
		"    fire_weapon: {hat: 4, direction: up}\n"));
	/* Directions, and thresholds in (0, 1]. */
	XVT_ASSERT_TRUE(!StickWithButtons(
		"    fire_weapon: {axis: 1, direction: sideways}\n"));
	XVT_ASSERT_TRUE(!StickWithButtons(
		"    fire_weapon: {hat: 0, direction: upleft}\n"));
	XVT_ASSERT_TRUE(!StickWithButtons(
		"    fire_weapon: {axis: 1, direction: positive, threshold: 0}\n"));
	XVT_ASSERT_TRUE(!StickWithButtons(
		"    fire_weapon: {axis: 1, direction: positive, threshold: 1.5}\n"));
	/* An axis source takes no fourth field; its axis is a name on a gamepad
	 * and an index within the axes on a joystick. */
	XVT_ASSERT_TRUE(!StickWithButtons(
		"    fire_weapon: {axis: 1, direction: positive, threshold: 0.5, extra: 2}\n"));
	XVT_ASSERT_TRUE(!PadWithButtons(
		"    fire_weapon: {axis: 1, direction: positive}\n"));
	XVT_ASSERT_TRUE(!PadWithButtons(
		"    fire_weapon: {axis: middlex, direction: positive}\n"));
	XVT_ASSERT_TRUE(!StickWithButtons(
		"    fire_weapon: {axis: 16, direction: positive}\n"));
	/* A mapping that is none of the source forms. */
	XVT_ASSERT_TRUE(
		!StickWithButtons("    fire_weapon: {direction: up}\n"));
	/* A source bound to two actions. */
	XVT_ASSERT_TRUE(!PadWithButtons(
		"    fire_weapon: south\n    target_next: south\n"));
	/* Unknown fields in a button or hat source. */
	XVT_ASSERT_TRUE(
		!StickWithButtons("    fire_weapon: {button: 1, extra: 2}\n"));
	XVT_ASSERT_TRUE(!StickWithButtons(
		"    fire_weapon: {hat: 0, direction: up, extra: 2}\n"));
	/* An unknown field beside the axes and buttons of the gamepad defaults. */
	XVT_ASSERT_TRUE(!ReadProfileText(
		"input:\n  gamepad_defaults:\n    buttons: {}\n    color: red\n",
		"input.gamepad_defaults", AERON_CONTROLLER_KIND_GAMEPAD));
	Fixture_End();
}

/* Known failure. The header says unknown fields fail. An axis source with a misspelt threshold field,
 * {axis, direction, threshhold}, is accepted: the misspelt field is ignored and the default threshold of
 * 0.5 is used. */
static void CheckAxisSourceUnknownField(void)
{
	Fixture_Begin();
	XVT_ASSERT_TRUE(!StickWithButtons(
		"    fire_weapon: {axis: 1, direction: positive, threshhold: 0.9}\n"));
	Fixture_End();
}

/* A guid of 32 hex digits whose last two are number in hex. */
static void Guid(char *guid, int number)
{
	snprintf(guid, 33, "%030d%02x", 0, number);
}

/* A controllers document with count gamepad models and no axes, each with its own guid. */
static char *GamepadModels(int count)
{
	size_t capacity = 64 + (size_t)count * 128;
	char *text = malloc(capacity);
	XVT_ASSERT_TRUE(text != NULL);
	size_t used =
		(size_t)snprintf(text, capacity, "input:\n  controllers:\n");
	for (int i = 1; i <= count; ++i) {
		char guid[33];
		Guid(guid, i);
		used += (size_t)snprintf(
			text + used, capacity - used,
			"    - {guid: \"%s\", name: \"Pad %d\", layout: gamepad}\n",
			guid, i);
	}
	return text;
}

static void CheckParse(void)
{
	Fixture_Begin();
	XVT_ASSERT_TRUE(ParseText(
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
	XVT_ASSERT_TRUE(Binds(&g_options.models[0].profile,
			      XVT_INPUT_ACTION_FIRE_WEAPON,
			      Source(AERON_CONTROLLER_DIGITAL_BUTTON,
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
	XVT_ASSERT_TRUE(ParseText("input:\n  controllers: []\n"));
	XVT_ASSERT_INT_EQ(g_options.count, 0);

	/* At most XVT_CONTROLLER_MODEL_CAP models. */
	char *text = GamepadModels(XVT_CONTROLLER_MODEL_CAP);
	XVT_ASSERT_TRUE(ParseText(text));
	XVT_ASSERT_INT_EQ(g_options.count, XVT_CONTROLLER_MODEL_CAP);
	free(text);
	text = GamepadModels(XVT_CONTROLLER_MODEL_CAP + 1);
	XVT_ASSERT_TRUE(!ParseText(text));
	free(text);
	Fixture_End();
}

static void CheckParseRefusals(void)
{
	Fixture_Begin();
	const char *pad = "\"00000000000000000000000000000001\"";
	char text[512];
	/* The list is required, and must be a list. */
	XVT_ASSERT_TRUE(!ParseText("input:\n  mouse_flight: true\n"));
	XVT_ASSERT_TRUE(!ParseText("input:\n  controllers: {}\n"));
	/* Only the five fields; guid and name are required, the layout is gamepad or joystick. */
	snprintf(
		text, sizeof text,
		"input:\n  controllers:\n    - {guid: %s, name: P, layout: gamepad, color: red}\n",
		pad);
	XVT_ASSERT_TRUE(!ParseText(text));
	XVT_ASSERT_TRUE(!ParseText(
		"input:\n  controllers:\n    - {name: P, layout: gamepad}\n"));
	snprintf(text, sizeof text,
		 "input:\n  controllers:\n    - {guid: %s, layout: gamepad}\n",
		 pad);
	XVT_ASSERT_TRUE(!ParseText(text));
	snprintf(
		text, sizeof text,
		"input:\n  controllers:\n    - {guid: %s, name: P, layout: wheel}\n",
		pad);
	XVT_ASSERT_TRUE(!ParseText(text));
	/* The whole set is validated: guids of 32 hex digits, not all zero, not repeated. */
	XVT_ASSERT_TRUE(!ParseText(
		"input:\n  controllers:\n    - {guid: \"abc\", name: P, layout: gamepad}\n"));
	XVT_ASSERT_TRUE(!ParseText(
		"input:\n  controllers:\n"
		"    - {guid: \"00000000000000000000000000000000\", name: P, layout: gamepad}\n"));
	snprintf(
		text, sizeof text,
		"input:\n  controllers:\n    - {guid: %s, name: P, layout: gamepad}\n"
		"    - {guid: %s, name: Q, layout: gamepad}\n",
		pad, pad);
	XVT_ASSERT_TRUE(!ParseText(text));
	/* No flight axis driven by two models. */
	XVT_ASSERT_TRUE(!ParseText(
		"input:\n  controllers:\n"
		"    - guid: \"00000000000000000000000000000001\"\n"
		"      name: P\n"
		"      layout: gamepad\n"
		"      axes: {yaw: {source: leftx, invert: false, deadzone: 0}}\n"
		"    - guid: \"00000000000000000000000000000002\"\n"
		"      name: Q\n"
		"      layout: gamepad\n"
		"      axes: {yaw: {source: rightx, invert: false, deadzone: 0}}\n"));
	Fixture_End();
}

/* Two valid models: a gamepad whose bindings list fire_weapon, then target_next, then fire_weapon again,
 * and a joystick with a hat, a button and an axis source. Button and hat thresholds are 0.5, the default. */
static void TwoModels(struct XvtControllerOptions *options)
{
	memset(options, 0, sizeof *options);
	options->count = 2;
	struct XvtControllerModel *pad = &options->models[0];
	strcpy(pad->guid, "0123456789abcdef0123456789abcdef");
	strcpy(pad->name, "Test Pad");
	pad->kind = AERON_CONTROLLER_KIND_GAMEPAD;
	XvtControllerOptions_ClearProfile(&pad->profile,
					  AERON_CONTROLLER_KIND_GAMEPAD);
	pad->profile.mapping.axes[XVT_INPUT_AXIS_YAW] =
		(struct XvtInputAxisBinding){.source = AERON_GAMEPAD_AXIS_LEFTX,
					     .invert = true,
					     .deadzone = 0.25f};
	pad->profile.mapping.axes[XVT_INPUT_AXIS_THROTTLE] =
		(struct XvtInputAxisBinding){.source =
						     AERON_GAMEPAD_AXIS_RIGHTY,
					     .invert = false,
					     .deadzone = 0.0f};
	pad->profile.bindings[0] = (struct XvtInputActionBinding){
		Source(AERON_CONTROLLER_DIGITAL_BUTTON,
		       AERON_GAMEPAD_BUTTON_SOUTH, 0, 0.5f),
		XVT_INPUT_ACTION_FIRE_WEAPON};
	pad->profile.bindings[1] = (struct XvtInputActionBinding){
		Source(AERON_CONTROLLER_DIGITAL_BUTTON,
		       AERON_GAMEPAD_BUTTON_WEST, 0, 0.5f),
		XVT_INPUT_ACTION_TARGET_NEXT};
	pad->profile.bindings[2] = (struct XvtInputActionBinding){
		Source(AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE,
		       AERON_GAMEPAD_AXIS_RIGHT_TRIGGER, 0, 0.75f),
		XVT_INPUT_ACTION_FIRE_WEAPON};
	pad->profile.binding_count = 3;

	struct XvtControllerModel *stick = &options->models[1];
	strcpy(stick->guid, "00000000000000000000000000000002");
	strcpy(stick->name, "Test Stick");
	stick->kind = AERON_CONTROLLER_KIND_JOYSTICK;
	XvtControllerOptions_ClearProfile(&stick->profile,
					  AERON_CONTROLLER_KIND_JOYSTICK);
	stick->profile.mapping.axes[XVT_INPUT_AXIS_PITCH] =
		(struct XvtInputAxisBinding){
			.source = 3, .invert = false, .deadzone = 0.5f};
	stick->profile.bindings[0] = (struct XvtInputActionBinding){
		Source(AERON_CONTROLLER_DIGITAL_HAT, 1,
		       AERON_CONTROLLER_HAT_DOWN, 0.5f),
		XVT_INPUT_ACTION_TARGET_PREV};
	stick->profile.bindings[1] = (struct XvtInputActionBinding){
		Source(AERON_CONTROLLER_DIGITAL_BUTTON, 7, 0, 0.5f),
		XVT_INPUT_ACTION_THROTTLE_UP};
	stick->profile.bindings[2] = (struct XvtInputActionBinding){
		Source(AERON_CONTROLLER_DIGITAL_AXIS_NEGATIVE, 2, 0, 1.0f),
		XVT_INPUT_ACTION_THROTTLE_DOWN};
	stick->profile.binding_count = 3;
}

static void CheckWrite(void)
{
	Fixture_Begin();
	static struct XvtControllerOptions options, reordered, parsed;
	TwoModels(&options);
	const char *start =
		"input:\n  mouse_flight: true\n  controllers: [{guid: x}, {guid: y}, {guid: z}]\n";
	AeronConfigFile *document = Fixture_Yaml(start);
	AeronConfigError detail;
	XVT_ASSERT_TRUE(XvtControllerConfig_Write(document, &options, &detail));

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
	XVT_ASSERT_TRUE(XvtControllerConfig_Parse(document, &parsed, error,
						  sizeof error));
	XVT_ASSERT_INT_EQ(parsed.count, options.count);
	for (size_t m = 0; m < options.count; ++m) {
		const struct XvtControllerModel *want = &options.models[m];
		const struct XvtControllerModel *got = &parsed.models[m];
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
			XVT_ASSERT_TRUE(Binds(
				&got->profile, want->profile.bindings[b].action,
				want->profile.bindings[b].source));
		}
	}

	/* The order is fixed: the same bindings listed in another order are written the same way. */
	reordered = options;
	struct XvtControllerProfile *profile = &reordered.models[0].profile;
	struct XvtInputActionBinding first = profile->bindings[0];
	profile->bindings[0] = profile->bindings[2];
	profile->bindings[2] = first;
	AeronConfigFile *other = Fixture_Yaml(start);
	XVT_ASSERT_TRUE(XvtControllerConfig_Write(other, &reordered, &detail));
	XVT_ASSERT_TRUE(Fixture_SameDocument(document, other));
	AeronConfigFile_Destroy(other);
	AeronConfigFile_Destroy(document);

	/* The writer does not validate. */
	reordered = options;
	strcpy(reordered.models[0].guid, "NOT-A-GUID");
	document = Fixture_Yaml("version: 3\n");
	XVT_ASSERT_TRUE(
		XvtControllerConfig_Write(document, &reordered, &detail));
	XVT_ASSERT_INT_EQ(
		strcmp(AeronConfigFile_GetString(
			       document, "input.controllers[0].guid", ""),
		       "NOT-A-GUID"),
		0);
	AeronConfigFile_Destroy(document);
	Fixture_End();
}

static void CheckSetController(void)
{
	Fixture_Begin();
	static struct XvtControllerOptions options, expected;
	char error[512];
	TwoModels(&options);
	/* Needs loaded settings. */
	XVT_ASSERT_TRUE(
		!XvtConfig_SetController(&options, error, sizeof error));

	Fixture_Load();
	uint64_t generation = XvtConfig_Generation();
	XVT_ASSERT_TRUE(XvtConfig_SetController(&options, error, sizeof error));
	XVT_ASSERT_TRUE(XvtConfig_Generation() > generation);
	XVT_ASSERT_TRUE(!Fixture_Exists("user/config.yaml"));
	/* The settings now hold the list as the writer stores it. */
	AeronConfigFile *written = Fixture_Yaml("version: 3\n");
	AeronConfigError detail;
	XVT_ASSERT_TRUE(XvtControllerConfig_Write(written, &options, &detail));
	XVT_ASSERT_TRUE(XvtControllerConfig_Parse(written, &expected, error,
						  sizeof error));
	AeronConfigFile_Destroy(written);
	XVT_ASSERT_TRUE(XvtControllerOptions_Equals(
		&XvtConfig_Settings()->controller, &expected));

	/* Invalid options are refused before anything changes. */
	generation = XvtConfig_Generation();
	AeronConfigFile *before = Fixture_UserCopy();
	strcpy(options.models[0].guid, "0123456789ABCDEF0123456789ABCDEF");
	XVT_ASSERT_TRUE(
		!XvtConfig_SetController(&options, error, sizeof error));
	XVT_ASSERT_INT_EQ(XvtConfig_Generation(), generation);
	XVT_ASSERT_TRUE(Fixture_SameDocument(XvtConfig_UserDocument(), before));
	XVT_ASSERT_TRUE(XvtControllerOptions_Equals(
		&XvtConfig_Settings()->controller, &expected));
	AeronConfigFile_Destroy(before);
	Fixture_End();
}

/* With no arguments, runs every check that holds. "known-failure <name>" runs only that check, which shows
 * the code breaking its header; a name not listed here returns 0. */
int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		if (strcmp(argv[2], "axis_source_unknown_field") == 0) {
			CheckAxisSourceUnknownField();
		}
		return 0;
	}
	CheckGamepadProfile();
	CheckJoystickProfile();
	CheckProfileRefusals();
	CheckParse();
	CheckParseRefusals();
	CheckWrite();
	CheckSetController();
	return 0;
}
