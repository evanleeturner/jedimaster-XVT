/* Checks the keyboard bindings in the settings document (xvt_runtime/config/keyboard_config.h) against the
 * promises in its header, on documents this file writes itself. Each check starts from a fresh fixture
 * folder (config_fixture.h); no settings are loaded. */
#define _XOPEN_SOURCE 700

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config_fixture.h"
#include "test_assert.h"
#include "xvt_runtime/config/keyboard_config.h"
#include "xvt_runtime/input/actions.h"
#include "xvt_runtime/input/keyboard_mapping.h"

static AeronKeyChord chord(const char *name, uint8_t modifiers)
{
	AeronKey key;
	XVT_ASSERT_TRUE(AeronKey_FromName(name, &key));
	return (AeronKeyChord){.key = (uint16_t)key, .modifiers = modifiers};
}

/* The action bound to source in profile, or NONE when the source is not bound. */
static xvt_input_action
bound_action(const struct xvt_keyboard_bindings *profile, AeronKeyChord source)
{
	size_t index = xvt_keyboard_mapping_find(profile, source);
	return index == SIZE_MAX ? XVT_INPUT_ACTION_NONE
				 : profile->bindings[index].action;
}

/* Reads text's input.keyboard into *profile and returns what the reader returned; error is cleared first. */
static bool read_text(const char *text, struct xvt_keyboard_bindings *profile,
		      char *error, size_t capacity)
{
	AeronConfigFile *document = fixture_yaml(text);
	error[0] = 0;
	bool read =
		xvt_keyboard_config_read(document, profile, error, capacity);
	AeronConfigFile_Destroy(document);
	return read;
}

static void expect_refused(const char *text)
{
	static struct xvt_keyboard_bindings profile;
	char error[256];
	fixture_case(text);
	XVT_ASSERT_TRUE(!read_text(text, &profile, error, sizeof error));
	XVT_ASSERT_TRUE(error[0] != 0);
	fixture_case(NULL);
}

static void expect_accepted(const char *text)
{
	static struct xvt_keyboard_bindings profile;
	char error[256];
	fixture_case(text);
	bool read = read_text(text, &profile, error, sizeof error);
	if (!read) {
		fprintf(stderr, "refused: %s\n", error);
	}
	XVT_ASSERT_TRUE(read);
	fixture_case(NULL);
}

static void check_read_forms(void)
{
	fixture_begin();
	static struct xvt_keyboard_bindings profile;
	char error[256];
	/* A key name, or a list of {key, modifiers} sources; an empty list binds nothing. */
	XVT_ASSERT_TRUE(read_text(
		"input:\n"
		"  keyboard:\n"
		"    target_next: \"T\"\n"
		"    fire_weapon:\n"
		"      - {key: \"2\", modifiers: [alt]}\n"
		"      - {key: \"F\", modifiers: [shift, ctrl, alt, gui]}\n"
		"    escape: []\n"
		"    cycle_weapon_group:\n"
		"      - {key: \"B\", modifiers: [shift]}\n"
		"      - {key: \"C\", modifiers: [ctrl]}\n"
		"      - {key: \"D\", modifiers: [alt]}\n"
		"      - {key: \"E\", modifiers: [gui]}\n"
		"      - {key: \"G\"}\n",
		&profile, error, sizeof error));
	XVT_ASSERT_INT_EQ(profile.count, 8);
	XVT_ASSERT_INT_EQ(bound_action(&profile, chord("T", 0)),
			  XVT_INPUT_ACTION_TARGET_NEXT);
	XVT_ASSERT_INT_EQ(bound_action(&profile, chord("2", AERON_KEY_MOD_ALT)),
			  XVT_INPUT_ACTION_FIRE_WEAPON);
	XVT_ASSERT_INT_EQ(
		bound_action(&profile, chord("F", AERON_KEY_MOD_SHIFT |
							  AERON_KEY_MOD_CTRL |
							  AERON_KEY_MOD_ALT |
							  AERON_KEY_MOD_GUI)),
		XVT_INPUT_ACTION_FIRE_WEAPON);
	/* Each modifier name stands for its own modifier. */
	XVT_ASSERT_INT_EQ(
		bound_action(&profile, chord("B", AERON_KEY_MOD_SHIFT)),
		XVT_INPUT_ACTION_CYCLE_WEAPON_GROUP);
	XVT_ASSERT_INT_EQ(
		bound_action(&profile, chord("C", AERON_KEY_MOD_CTRL)),
		XVT_INPUT_ACTION_CYCLE_WEAPON_GROUP);
	XVT_ASSERT_INT_EQ(bound_action(&profile, chord("D", AERON_KEY_MOD_ALT)),
			  XVT_INPUT_ACTION_CYCLE_WEAPON_GROUP);
	XVT_ASSERT_INT_EQ(bound_action(&profile, chord("E", AERON_KEY_MOD_GUI)),
			  XVT_INPUT_ACTION_CYCLE_WEAPON_GROUP);
	XVT_ASSERT_INT_EQ(bound_action(&profile, chord("G", 0)),
			  XVT_INPUT_ACTION_CYCLE_WEAPON_GROUP);
	XVT_ASSERT_INT_EQ(bound_action(&profile, chord("2", 0)),
			  XVT_INPUT_ACTION_NONE);

	/* The profile comes back sorted: sorting it again changes nothing. */
	static struct xvt_keyboard_bindings sorted;
	sorted = profile;
	xvt_keyboard_mapping_sort(&sorted);
	XVT_ASSERT_TRUE(xvt_keyboard_mapping_equal(&profile, &sorted));
	fixture_end();
}

static void check_read_refusals(void)
{
	fixture_begin();
	/* An action that cannot take a key: one that does not exist, and one that is not keyboard-bindable. */
	expect_refused("input:\n  keyboard:\n    fly_backwards: \"A\"\n");
	XVT_ASSERT_TRUE(!xvt_input_actions_keyboard_bindable(
		XVT_INPUT_ACTION_CHAT_SEND));
	expect_refused("input:\n  keyboard:\n    chat_send: \"A\"\n");
	/* An unknown key, as a name and inside a source. */
	expect_refused("input:\n  keyboard:\n    fire_weapon: \"NoSuchKey\"\n");
	expect_refused(
		"input:\n  keyboard:\n    fire_weapon: [{key: \"NoSuchKey\"}]\n");
	/* An unknown source field, an unknown modifier, and a repeated one. */
	expect_refused(
		"input:\n  keyboard:\n    fire_weapon: [{key: \"A\", mods: [shift]}]\n");
	expect_refused(
		"input:\n  keyboard:\n    fire_weapon: [{key: \"A\", modifiers: [hyper]}]\n");
	expect_refused(
		"input:\n  keyboard:\n    fire_weapon: [{key: \"A\", modifiers: [shift, shift]}]\n");
	/* A value that is neither a key name nor a list of sources. */
	expect_refused("input:\n  keyboard:\n    fire_weapon: 5\n");
	/* Reserved sources: Escape, Tab alone, and M with Ctrl and Alt; Tab with Shift is not reserved. */
	expect_refused("input:\n  keyboard:\n    fire_weapon: \"Escape\"\n");
	expect_refused("input:\n  keyboard:\n    fire_weapon: \"Tab\"\n");
	expect_accepted(
		"input:\n  keyboard:\n    fire_weapon: [{key: \"Tab\", modifiers: [shift]}]\n");
	expect_refused(
		"input:\n  keyboard:\n    fire_weapon: [{key: \"M\", modifiers: [ctrl, alt]}]\n");
	/* An unsupported source: a modifier key with a modifier. The modifier key alone is a valid source. */
	expect_refused(
		"input:\n  keyboard:\n    fire_weapon: [{key: \"Left Shift\", modifiers: [ctrl]}]\n");
	expect_accepted(
		"input:\n  keyboard:\n    fire_weapon: \"Left Shift\"\n");
	/* A source bound twice, to two actions or twice to one. */
	expect_refused(
		"input:\n  keyboard:\n    fire_weapon: \"A\"\n    target_next: \"A\"\n");
	expect_refused(
		"input:\n  keyboard:\n    fire_weapon: [{key: \"A\"}, {key: \"A\"}]\n");
	expect_accepted(
		"input:\n  keyboard:\n    fire_weapon: \"A\"\n    target_next: [{key: \"A\", modifiers: [shift]}]\n");
	fixture_end();
}

static void check_read_keeps_earlier_bindings(void)
{
	fixture_begin();
	static struct xvt_keyboard_bindings profile;
	char error[256];
	XVT_ASSERT_TRUE(!read_text(
		"input:\n  keyboard:\n    fire_weapon: \"A\"\n    target_next: \"NoSuchKey\"\n",
		&profile, error, sizeof error));
	XVT_ASSERT_INT_EQ(profile.count, 1);
	XVT_ASSERT_INT_EQ(bound_action(&profile, chord("A", 0)),
			  XVT_INPUT_ACTION_FIRE_WEAPON);
	fixture_end();
}

/* A document binding count distinct chords to fire_weapon: each letter with each set of modifiers, leaving
 * out M with Ctrl and Alt, which is reserved. */
static char *many_bindings(int count)
{
	static const char *const modifiers[16] = {"",
						  "shift",
						  "ctrl",
						  "shift, ctrl",
						  "alt",
						  "shift, alt",
						  "ctrl, alt",
						  "shift, ctrl, alt",
						  "gui",
						  "shift, gui",
						  "ctrl, gui",
						  "shift, ctrl, gui",
						  "alt, gui",
						  "shift, alt, gui",
						  "ctrl, alt, gui",
						  "shift, ctrl, alt, gui"};
	size_t capacity = 64 + (size_t)count * 64;
	char *text = malloc(capacity);
	XVT_ASSERT_TRUE(text != NULL);
	size_t used = (size_t)snprintf(
		text, capacity, "input:\n  keyboard:\n    fire_weapon:\n");
	int written = 0;
	for (int set = 0; set < 16 && written < count; ++set) {
		for (char letter = 'A'; letter <= 'Z' && written < count;
		     ++letter) {
			if (letter == 'M' && (set & 6) == 6) {
				continue;
			}
			used += (size_t)snprintf(
				text + used, capacity - used,
				"      - {key: \"%c\", modifiers: [%s]}\n",
				letter, modifiers[set]);
			++written;
		}
	}
	XVT_ASSERT_INT_EQ(written, count);
	return text;
}

static void check_read_capacity(void)
{
	fixture_begin();
	static struct xvt_keyboard_bindings profile;
	char error[256];
	char *text = many_bindings(XVT_KEYBOARD_BINDING_CAP);
	XVT_ASSERT_TRUE(read_text(text, &profile, error, sizeof error));
	XVT_ASSERT_INT_EQ(profile.count, XVT_KEYBOARD_BINDING_CAP);
	free(text);

	/* One more is refused, and the profile keeps the bindings read before the one that did not fit. */
	text = many_bindings(XVT_KEYBOARD_BINDING_CAP + 1);
	XVT_ASSERT_TRUE(!read_text(text, &profile, error, sizeof error));
	XVT_ASSERT_TRUE(error[0] != 0);
	XVT_ASSERT_INT_EQ(profile.count, XVT_KEYBOARD_BINDING_CAP);
	free(text);
	fixture_end();
}

static void check_write_round_trip(void)
{
	fixture_begin();
	AeronConfigFile *document = fixture_yaml("input:\n"
						 "  mouse_flight: true\n"
						 "  keyboard:\n"
						 "    fire_weapon: \"A\"\n"
						 "    no_such_action: \"B\"\n");
	static struct xvt_keyboard_bindings profile, read;
	memset(&profile, 0, sizeof profile);
	profile.bindings[0] = (struct xvt_keyboard_binding){
		chord("Z", 0), XVT_INPUT_ACTION_TARGET_NEXT};
	profile.bindings[1] = (struct xvt_keyboard_binding){
		chord("Q", AERON_KEY_MOD_SHIFT | AERON_KEY_MOD_CTRL),
		XVT_INPUT_ACTION_FIRE_WEAPON};
	profile.bindings[2] = (struct xvt_keyboard_binding){
		chord("F1", 0), XVT_INPUT_ACTION_FIRE_WEAPON};
	profile.count = 3;
	AeronConfigError error;
	memset(&error, 0, sizeof error);
	XVT_ASSERT_TRUE(xvt_keyboard_config_write(document, &profile, &error));

	/* input.keyboard is replaced; the rest of the document stays. */
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(document,
					     "input.keyboard.no_such_action"));
	XVT_ASSERT_INT_EQ(
		AeronConfigFile_GetBool(document, "input.mouse_flight", 0), 1);

	/* Every action that can take a key is listed; one with no source as an empty list. */
	for (int action = XVT_INPUT_ACTION_NONE + 1;
	     action < XVT_INPUT_ACTION_COUNT; ++action) {
		if (!xvt_input_actions_keyboard_bindable(
			    (xvt_input_action)action)) {
			continue;
		}
		char path[160];
		snprintf(path, sizeof path, "input.keyboard.%s",
			 xvt_input_actions_to_name((xvt_input_action)action));
		const AeronConfigNode *node =
			AeronConfigFile_GetNode(document, path);
		XVT_ASSERT_INT_EQ(AeronConfigNode_Type(node),
				  AERON_CONFIG_SEQUENCE);
		if (action != XVT_INPUT_ACTION_TARGET_NEXT &&
		    action != XVT_INPUT_ACTION_FIRE_WEAPON) {
			XVT_ASSERT_INT_EQ(AeronConfigNode_SequenceCount(node),
					  0);
		}
	}

	/* Reading the written bindings back gives the same bindings. */
	char message[256] = "";
	XVT_ASSERT_TRUE(xvt_keyboard_config_read(document, &read, message,
						 sizeof message));
	xvt_keyboard_mapping_sort(&profile);
	XVT_ASSERT_TRUE(xvt_keyboard_mapping_equal(&read, &profile));
	AeronConfigFile_Destroy(document);
	fixture_end();
}

static void expect_write_refused(const struct xvt_keyboard_bindings *profile)
{
	AeronConfigFile *document = fixture_yaml("version: 3\n");
	AeronConfigError error;
	memset(&error, 0, sizeof error);
	XVT_ASSERT_TRUE(!xvt_keyboard_config_write(document, profile, &error));
	XVT_ASSERT_TRUE(error.message[0] != 0);
	AeronConfigFile_Destroy(document);
}

static void check_write_refusals(void)
{
	fixture_begin();
	static struct xvt_keyboard_bindings profile;
	memset(&profile, 0, sizeof profile);
	profile.bindings[0] = (struct xvt_keyboard_binding){
		chord("A", 0), XVT_INPUT_ACTION_FIRE_WEAPON};
	profile.count = 1;

	/* Too many bindings. */
	profile.count = XVT_KEYBOARD_BINDING_CAP + 1;
	expect_write_refused(&profile);
	profile.count = 1;
	/* An action that cannot take a key. */
	profile.bindings[0].action = XVT_INPUT_ACTION_NONE;
	expect_write_refused(&profile);
	profile.bindings[0].action = XVT_INPUT_ACTION_CHAT_CANCEL;
	expect_write_refused(&profile);
	profile.bindings[0].action = XVT_INPUT_ACTION_FIRE_WEAPON;
	/* A reserved source, and a source that names no key. */
	profile.bindings[0].source = chord("Escape", 0);
	expect_write_refused(&profile);
	profile.bindings[0].source = (AeronKeyChord){.key = 0, .modifiers = 0};
	expect_write_refused(&profile);
	fixture_end();
}

static void check_resolve(void)
{
	fixture_begin();
	static struct xvt_keyboard_bindings defaults, effective;
	memset(&defaults, 0, sizeof defaults);
	defaults.bindings[0] = (struct xvt_keyboard_binding){
		chord("A", 0), XVT_INPUT_ACTION_FIRE_WEAPON};
	defaults.bindings[1] = (struct xvt_keyboard_binding){
		chord("B", 0), XVT_INPUT_ACTION_CYCLE_WEAPON_GROUP};
	defaults.bindings[2] = (struct xvt_keyboard_binding){
		chord("C", 0), XVT_INPUT_ACTION_TOGGLE_BEAM};
	defaults.bindings[3] = (struct xvt_keyboard_binding){
		chord("D", 0), XVT_INPUT_ACTION_TARGET_NEXT};
	defaults.count = 4;

	/* The user lists fire_weapon with no source and binds C to cycle_weapon_group. */
	AeronConfigFile *user = fixture_yaml(
		"version: 3\ninput:\n  keyboard:\n    fire_weapon: []\n    cycle_weapon_group: \"C\"\n");
	AeronConfigFile *user_before = fixture_yaml(
		"version: 3\ninput:\n  keyboard:\n    fire_weapon: []\n    cycle_weapon_group: \"C\"\n");
	AeronConfigFile *merged =
		fixture_yaml("input:\n  keyboard:\n    fire_weapon: \"Z\"\n");
	char error[256] = "";
	XVT_ASSERT_TRUE(xvt_keyboard_config_resolve(&defaults, user, merged,
						    error, sizeof error));
	XVT_ASSERT_TRUE(fixture_same_document(user, user_before));

	/* fire_weapon and cycle_weapon_group lose their defaults because the user lists them; toggle_beam
	 * loses C because the user bound it; target_next keeps D. */
	XVT_ASSERT_TRUE(xvt_keyboard_config_read(merged, &effective, error,
						 sizeof error));
	XVT_ASSERT_INT_EQ(effective.count, 2);
	XVT_ASSERT_INT_EQ(bound_action(&effective, chord("C", 0)),
			  XVT_INPUT_ACTION_CYCLE_WEAPON_GROUP);
	XVT_ASSERT_INT_EQ(bound_action(&effective, chord("D", 0)),
			  XVT_INPUT_ACTION_TARGET_NEXT);
	AeronConfigFile_Destroy(user);
	AeronConfigFile_Destroy(user_before);
	AeronConfigFile_Destroy(merged);

	/* Without user bindings nothing changes. */
	user = fixture_yaml("version: 3\ninput:\n  mouse_flight: true\n");
	merged = fixture_yaml("input:\n  keyboard:\n    fire_weapon: \"Z\"\n");
	AeronConfigFile *merged_before =
		fixture_yaml("input:\n  keyboard:\n    fire_weapon: \"Z\"\n");
	XVT_ASSERT_TRUE(xvt_keyboard_config_resolve(&defaults, user, merged,
						    error, sizeof error));
	XVT_ASSERT_TRUE(fixture_same_document(merged, merged_before));
	AeronConfigFile_Destroy(user);
	AeronConfigFile_Destroy(merged);
	AeronConfigFile_Destroy(merged_before);
	fixture_end();
}

int main(void)
{
	check_read_forms();
	check_read_refusals();
	check_read_keeps_earlier_bindings();
	check_read_capacity();
	check_write_round_trip();
	check_write_refusals();
	check_resolve();
	return 0;
}
