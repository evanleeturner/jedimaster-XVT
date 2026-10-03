/* Checks the keyboard bindings in the settings document (xvt_runtime/config/keyboard_config.h) against the
 * promises in its header, on documents this file writes itself. Each check starts from a fresh fixture
 * folder (config_fixture.h); no settings are loaded. */
#define _XOPEN_SOURCE 700

#include "config_fixture.h"
#include "test_assert.h"
#include "xvt_runtime/config/keyboard_config.h"
#include "xvt_runtime/input/actions.h"
#include "xvt_runtime/input/keyboard_mapping.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AeronKeyChord Chord(const char *name, uint8_t modifiers)
{
	AeronKey key;
	XVT_ASSERT_TRUE(AeronKey_FromName(name, &key));
	return (AeronKeyChord){.key = (uint16_t)key, .modifiers = modifiers};
}

/* The action bound to source in profile, or NONE when the source is not bound. */
static XvtInputAction BoundAction(const struct XvtKeyboardBindings *profile,
				  AeronKeyChord source)
{
	size_t index = XvtKeyboardMapping_Find(profile, source);
	return index == SIZE_MAX ? XVT_INPUT_ACTION_NONE
				 : profile->bindings[index].action;
}

/* Reads text's input.keyboard into *profile and returns what the reader returned; error is cleared first. */
static bool ReadText(const char *text, struct XvtKeyboardBindings *profile,
		     char *error, size_t capacity)
{
	AeronConfigFile *document = Fixture_Yaml(text);
	error[0] = 0;
	bool read = XvtKeyboardConfig_Read(document, profile, error, capacity);
	AeronConfigFile_Destroy(document);
	return read;
}

static void ExpectRefused(const char *text)
{
	static struct XvtKeyboardBindings profile;
	char error[256];
	Fixture_Case(text);
	XVT_ASSERT_TRUE(!ReadText(text, &profile, error, sizeof error));
	XVT_ASSERT_TRUE(error[0] != 0);
	Fixture_Case(NULL);
}

static void ExpectAccepted(const char *text)
{
	static struct XvtKeyboardBindings profile;
	char error[256];
	Fixture_Case(text);
	bool read = ReadText(text, &profile, error, sizeof error);
	if (!read) {
		fprintf(stderr, "refused: %s\n", error);
	}
	XVT_ASSERT_TRUE(read);
	Fixture_Case(NULL);
}

static void CheckReadForms(void)
{
	Fixture_Begin();
	static struct XvtKeyboardBindings profile;
	char error[256];
	/* A key name, or a list of {key, modifiers} sources; an empty list binds nothing. */
	XVT_ASSERT_TRUE(ReadText(
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
	XVT_ASSERT_INT_EQ(BoundAction(&profile, Chord("T", 0)),
			  XVT_INPUT_ACTION_TARGET_NEXT);
	XVT_ASSERT_INT_EQ(BoundAction(&profile, Chord("2", AERON_KEY_MOD_ALT)),
			  XVT_INPUT_ACTION_FIRE_WEAPON);
	XVT_ASSERT_INT_EQ(
		BoundAction(&profile, Chord("F", AERON_KEY_MOD_SHIFT |
							 AERON_KEY_MOD_CTRL |
							 AERON_KEY_MOD_ALT |
							 AERON_KEY_MOD_GUI)),
		XVT_INPUT_ACTION_FIRE_WEAPON);
	/* Each modifier name stands for its own modifier. */
	XVT_ASSERT_INT_EQ(
		BoundAction(&profile, Chord("B", AERON_KEY_MOD_SHIFT)),
		XVT_INPUT_ACTION_CYCLE_WEAPON_GROUP);
	XVT_ASSERT_INT_EQ(BoundAction(&profile, Chord("C", AERON_KEY_MOD_CTRL)),
			  XVT_INPUT_ACTION_CYCLE_WEAPON_GROUP);
	XVT_ASSERT_INT_EQ(BoundAction(&profile, Chord("D", AERON_KEY_MOD_ALT)),
			  XVT_INPUT_ACTION_CYCLE_WEAPON_GROUP);
	XVT_ASSERT_INT_EQ(BoundAction(&profile, Chord("E", AERON_KEY_MOD_GUI)),
			  XVT_INPUT_ACTION_CYCLE_WEAPON_GROUP);
	XVT_ASSERT_INT_EQ(BoundAction(&profile, Chord("G", 0)),
			  XVT_INPUT_ACTION_CYCLE_WEAPON_GROUP);
	XVT_ASSERT_INT_EQ(BoundAction(&profile, Chord("2", 0)),
			  XVT_INPUT_ACTION_NONE);

	/* The profile comes back sorted: sorting it again changes nothing. */
	static struct XvtKeyboardBindings sorted;
	sorted = profile;
	XvtKeyboardMapping_Sort(&sorted);
	XVT_ASSERT_TRUE(XvtKeyboardMapping_Equal(&profile, &sorted));
	Fixture_End();
}

static void CheckReadRefusals(void)
{
	Fixture_Begin();
	/* An action that cannot take a key: one that does not exist, and one that is not keyboard-bindable. */
	ExpectRefused("input:\n  keyboard:\n    fly_backwards: \"A\"\n");
	XVT_ASSERT_TRUE(
		!XvtInputActions_KeyboardBindable(XVT_INPUT_ACTION_CHAT_SEND));
	ExpectRefused("input:\n  keyboard:\n    chat_send: \"A\"\n");
	/* An unknown key, as a name and inside a source. */
	ExpectRefused("input:\n  keyboard:\n    fire_weapon: \"NoSuchKey\"\n");
	ExpectRefused(
		"input:\n  keyboard:\n    fire_weapon: [{key: \"NoSuchKey\"}]\n");
	/* An unknown source field, an unknown modifier, and a repeated one. */
	ExpectRefused(
		"input:\n  keyboard:\n    fire_weapon: [{key: \"A\", mods: [shift]}]\n");
	ExpectRefused(
		"input:\n  keyboard:\n    fire_weapon: [{key: \"A\", modifiers: [hyper]}]\n");
	ExpectRefused(
		"input:\n  keyboard:\n    fire_weapon: [{key: \"A\", modifiers: [shift, shift]}]\n");
	/* A value that is neither a key name nor a list of sources. */
	ExpectRefused("input:\n  keyboard:\n    fire_weapon: 5\n");
	/* Reserved sources: Escape, Tab alone, and M with Ctrl and Alt; Tab with Shift is not reserved. */
	ExpectRefused("input:\n  keyboard:\n    fire_weapon: \"Escape\"\n");
	ExpectRefused("input:\n  keyboard:\n    fire_weapon: \"Tab\"\n");
	ExpectAccepted(
		"input:\n  keyboard:\n    fire_weapon: [{key: \"Tab\", modifiers: [shift]}]\n");
	ExpectRefused(
		"input:\n  keyboard:\n    fire_weapon: [{key: \"M\", modifiers: [ctrl, alt]}]\n");
	/* An unsupported source: a modifier key with a modifier. The modifier key alone is a valid source. */
	ExpectRefused(
		"input:\n  keyboard:\n    fire_weapon: [{key: \"Left Shift\", modifiers: [ctrl]}]\n");
	ExpectAccepted(
		"input:\n  keyboard:\n    fire_weapon: \"Left Shift\"\n");
	/* A source bound twice, to two actions or twice to one. */
	ExpectRefused(
		"input:\n  keyboard:\n    fire_weapon: \"A\"\n    target_next: \"A\"\n");
	ExpectRefused(
		"input:\n  keyboard:\n    fire_weapon: [{key: \"A\"}, {key: \"A\"}]\n");
	ExpectAccepted(
		"input:\n  keyboard:\n    fire_weapon: \"A\"\n    target_next: [{key: \"A\", modifiers: [shift]}]\n");
	Fixture_End();
}

static void CheckReadKeepsEarlierBindings(void)
{
	Fixture_Begin();
	static struct XvtKeyboardBindings profile;
	char error[256];
	XVT_ASSERT_TRUE(!ReadText(
		"input:\n  keyboard:\n    fire_weapon: \"A\"\n    target_next: \"NoSuchKey\"\n",
		&profile, error, sizeof error));
	XVT_ASSERT_INT_EQ(profile.count, 1);
	XVT_ASSERT_INT_EQ(BoundAction(&profile, Chord("A", 0)),
			  XVT_INPUT_ACTION_FIRE_WEAPON);
	Fixture_End();
}

/* A document binding count distinct chords to fire_weapon: each letter with each set of modifiers, leaving
 * out M with Ctrl and Alt, which is reserved. */
static char *ManyBindings(int count)
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

static void CheckReadCapacity(void)
{
	Fixture_Begin();
	static struct XvtKeyboardBindings profile;
	char error[256];
	char *text = ManyBindings(XVT_KEYBOARD_BINDING_CAP);
	XVT_ASSERT_TRUE(ReadText(text, &profile, error, sizeof error));
	XVT_ASSERT_INT_EQ(profile.count, XVT_KEYBOARD_BINDING_CAP);
	free(text);

	/* One more is refused, and the profile keeps the bindings read before the one that did not fit. */
	text = ManyBindings(XVT_KEYBOARD_BINDING_CAP + 1);
	XVT_ASSERT_TRUE(!ReadText(text, &profile, error, sizeof error));
	XVT_ASSERT_TRUE(error[0] != 0);
	XVT_ASSERT_INT_EQ(profile.count, XVT_KEYBOARD_BINDING_CAP);
	free(text);
	Fixture_End();
}

static void CheckWriteRoundTrip(void)
{
	Fixture_Begin();
	AeronConfigFile *document = Fixture_Yaml("input:\n"
						 "  mouse_flight: true\n"
						 "  keyboard:\n"
						 "    fire_weapon: \"A\"\n"
						 "    no_such_action: \"B\"\n");
	static struct XvtKeyboardBindings profile, read;
	memset(&profile, 0, sizeof profile);
	profile.bindings[0] = (struct XvtKeyboardBinding){
		Chord("Z", 0), XVT_INPUT_ACTION_TARGET_NEXT};
	profile.bindings[1] = (struct XvtKeyboardBinding){
		Chord("Q", AERON_KEY_MOD_SHIFT | AERON_KEY_MOD_CTRL),
		XVT_INPUT_ACTION_FIRE_WEAPON};
	profile.bindings[2] = (struct XvtKeyboardBinding){
		Chord("F1", 0), XVT_INPUT_ACTION_FIRE_WEAPON};
	profile.count = 3;
	AeronConfigError error;
	memset(&error, 0, sizeof error);
	XVT_ASSERT_TRUE(XvtKeyboardConfig_Write(document, &profile, &error));

	/* input.keyboard is replaced; the rest of the document stays. */
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(document,
					     "input.keyboard.no_such_action"));
	XVT_ASSERT_INT_EQ(
		AeronConfigFile_GetBool(document, "input.mouse_flight", 0), 1);

	/* Every action that can take a key is listed; one with no source as an empty list. */
	for (int action = XVT_INPUT_ACTION_NONE + 1;
	     action < XVT_INPUT_ACTION_COUNT; ++action) {
		if (!XvtInputActions_KeyboardBindable((XvtInputAction)action)) {
			continue;
		}
		char path[160];
		snprintf(path, sizeof path, "input.keyboard.%s",
			 XvtInputActions_ToName((XvtInputAction)action));
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
	XVT_ASSERT_TRUE(XvtKeyboardConfig_Read(document, &read, message,
					       sizeof message));
	XvtKeyboardMapping_Sort(&profile);
	XVT_ASSERT_TRUE(XvtKeyboardMapping_Equal(&read, &profile));
	AeronConfigFile_Destroy(document);
	Fixture_End();
}

static void ExpectWriteRefused(const struct XvtKeyboardBindings *profile)
{
	AeronConfigFile *document = Fixture_Yaml("version: 3\n");
	AeronConfigError error;
	memset(&error, 0, sizeof error);
	XVT_ASSERT_TRUE(!XvtKeyboardConfig_Write(document, profile, &error));
	XVT_ASSERT_TRUE(error.message[0] != 0);
	AeronConfigFile_Destroy(document);
}

static void CheckWriteRefusals(void)
{
	Fixture_Begin();
	static struct XvtKeyboardBindings profile;
	memset(&profile, 0, sizeof profile);
	profile.bindings[0] = (struct XvtKeyboardBinding){
		Chord("A", 0), XVT_INPUT_ACTION_FIRE_WEAPON};
	profile.count = 1;

	/* Too many bindings. */
	profile.count = XVT_KEYBOARD_BINDING_CAP + 1;
	ExpectWriteRefused(&profile);
	profile.count = 1;
	/* An action that cannot take a key. */
	profile.bindings[0].action = XVT_INPUT_ACTION_NONE;
	ExpectWriteRefused(&profile);
	profile.bindings[0].action = XVT_INPUT_ACTION_CHAT_CANCEL;
	ExpectWriteRefused(&profile);
	profile.bindings[0].action = XVT_INPUT_ACTION_FIRE_WEAPON;
	/* A reserved source, and a source that names no key. */
	profile.bindings[0].source = Chord("Escape", 0);
	ExpectWriteRefused(&profile);
	profile.bindings[0].source = (AeronKeyChord){.key = 0, .modifiers = 0};
	ExpectWriteRefused(&profile);
	Fixture_End();
}

static void CheckResolve(void)
{
	Fixture_Begin();
	static struct XvtKeyboardBindings defaults, effective;
	memset(&defaults, 0, sizeof defaults);
	defaults.bindings[0] = (struct XvtKeyboardBinding){
		Chord("A", 0), XVT_INPUT_ACTION_FIRE_WEAPON};
	defaults.bindings[1] = (struct XvtKeyboardBinding){
		Chord("B", 0), XVT_INPUT_ACTION_CYCLE_WEAPON_GROUP};
	defaults.bindings[2] = (struct XvtKeyboardBinding){
		Chord("C", 0), XVT_INPUT_ACTION_TOGGLE_BEAM};
	defaults.bindings[3] = (struct XvtKeyboardBinding){
		Chord("D", 0), XVT_INPUT_ACTION_TARGET_NEXT};
	defaults.count = 4;

	/* The user lists fire_weapon with no source and binds C to cycle_weapon_group. */
	AeronConfigFile *user = Fixture_Yaml(
		"version: 3\ninput:\n  keyboard:\n    fire_weapon: []\n    cycle_weapon_group: \"C\"\n");
	AeronConfigFile *user_before = Fixture_Yaml(
		"version: 3\ninput:\n  keyboard:\n    fire_weapon: []\n    cycle_weapon_group: \"C\"\n");
	AeronConfigFile *merged =
		Fixture_Yaml("input:\n  keyboard:\n    fire_weapon: \"Z\"\n");
	char error[256] = "";
	XVT_ASSERT_TRUE(XvtKeyboardConfig_Resolve(&defaults, user, merged,
						  error, sizeof error));
	XVT_ASSERT_TRUE(Fixture_SameDocument(user, user_before));

	/* fire_weapon and cycle_weapon_group lose their defaults because the user lists them; toggle_beam
	 * loses C because the user bound it; target_next keeps D. */
	XVT_ASSERT_TRUE(XvtKeyboardConfig_Read(merged, &effective, error,
					       sizeof error));
	XVT_ASSERT_INT_EQ(effective.count, 2);
	XVT_ASSERT_INT_EQ(BoundAction(&effective, Chord("C", 0)),
			  XVT_INPUT_ACTION_CYCLE_WEAPON_GROUP);
	XVT_ASSERT_INT_EQ(BoundAction(&effective, Chord("D", 0)),
			  XVT_INPUT_ACTION_TARGET_NEXT);
	AeronConfigFile_Destroy(user);
	AeronConfigFile_Destroy(user_before);
	AeronConfigFile_Destroy(merged);

	/* Without user bindings nothing changes. */
	user = Fixture_Yaml("version: 3\ninput:\n  mouse_flight: true\n");
	merged = Fixture_Yaml("input:\n  keyboard:\n    fire_weapon: \"Z\"\n");
	AeronConfigFile *merged_before =
		Fixture_Yaml("input:\n  keyboard:\n    fire_weapon: \"Z\"\n");
	XVT_ASSERT_TRUE(XvtKeyboardConfig_Resolve(&defaults, user, merged,
						  error, sizeof error));
	XVT_ASSERT_TRUE(Fixture_SameDocument(merged, merged_before));
	AeronConfigFile_Destroy(user);
	AeronConfigFile_Destroy(merged);
	AeronConfigFile_Destroy(merged_before);
	Fixture_End();
}

int main(void)
{
	CheckReadForms();
	CheckReadRefusals();
	CheckReadKeepsEarlierBindings();
	CheckReadCapacity();
	CheckWriteRoundTrip();
	CheckWriteRefusals();
	CheckResolve();
	return 0;
}
