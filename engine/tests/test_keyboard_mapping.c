/* Checks the keyboard mapping (xvt_runtime/input/keyboard_mapping.h) against the promises in its header:
 * which chords are reserved shortcuts and when they trigger, which sources are valid and how they are
 * labelled, the binding list helpers, and how key events become queued flight keys and held buttons.
 * Every case installs its own bindings and starts from a suspended mapping. No device is opened: key
 * events and snapshots are built by the test. */
#include "aeron/input.h"
#include "test_assert.h"
#include "xvt_runtime/input/keyboard_mapping.h"
#include "xvt_runtime/runtime/port.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { KEY_M = AERON_KEY_A + ('m' - 'a') };

static AeronInputSnapshot g_input;
static XvtKeyboardBindings g_profile;

static AeronKeyChord Chord(int key, int modifiers)
{
	AeronKeyChord chord = {(uint16_t)key, (uint8_t)modifiers};
	return chord;
}

static void Bind(int key, int modifiers, XvtInputAction action)
{
	g_profile.bindings[g_profile.count].source = Chord(key, modifiers);
	g_profile.bindings[g_profile.count].action = action;
	++g_profile.count;
}

static void Event(int key, int modifiers, int down, int repeat, bool suppressed)
{
	AeronKeyEvent event;
	memset(&event, 0, sizeof event);
	event.chord = Chord(key, modifiers);
	event.down = (uint8_t)down;
	event.repeat = (uint8_t)repeat;
	XvtKeyboardMapping_Event(&event, suppressed);
}

static void Press(int key, int modifiers)
{
	Event(key, modifiers, 1, 0, false);
}

static void Repeat(int key, int modifiers)
{
	Event(key, modifiers, 1, 1, false);
}

static void Release(int key, int modifiers)
{
	Event(key, modifiers, 0, 0, false);
}

/* Reads every queued key; returns how many there were. */
static int DrainKeys(void)
{
	int count = 0;
	while (XvtKeyboardMapping_ReadKey()) {
		++count;
	}
	return count;
}

/* The bindings every event case uses, installed and enabled with no key held. */
static void InstallEnabled(void)
{
	memset(&g_profile, 0, sizeof g_profile);
	Bind(AERON_KEY_A + ('t' - 'a'), 0, XVT_INPUT_ACTION_TARGET_NEXT);
	Bind(AERON_KEY_A + ('b' - 'a'), AERON_KEY_MOD_CTRL,
	     XVT_INPUT_ACTION_THROTTLE_UP);
	Bind(AERON_KEY_A + ('c' - 'a'), 0, XVT_INPUT_ACTION_FIRE_WEAPON);
	Bind(AERON_KEY_A + ('d' - 'a'), 0,
	     XVT_INPUT_ACTION_TARGET_ROLL_MODIFIER);
	Bind(AERON_KEY_A + ('p' - 'a'), AERON_KEY_MOD_ALT,
	     XVT_INPUT_ACTION_PAUSE);
	Bind(AERON_KEY_A + ('q' - 'a'), 0, XVT_INPUT_ACTION_ESCAPE);
	Bind(AERON_KEY_LCTRL, 0, XVT_INPUT_ACTION_THROTTLE_DOWN);
	XvtKeyboardMapping_Install(&g_profile);
	memset(&g_input, 0, sizeof g_input);
	XvtKeyboardMapping_Enable(true, &g_input);
	XvtPort_ConsumeSettingsRequest();
}

static const int kT = AERON_KEY_A + ('t' - 'a');
static const int kB = AERON_KEY_A + ('b' - 'a');
static const int kC = AERON_KEY_A + ('c' - 'a');
static const int kD = AERON_KEY_A + ('d' - 'a');
static const int kP = AERON_KEY_A + ('p' - 'a');
static const int kQ = AERON_KEY_A + ('q' - 'a');

static void CheckShortcut(void)
{
	XvtKeyboardMapping_SetPolicy(false);
	for (int modifiers = 0; modifiers < 16; ++modifiers) {
		XVT_ASSERT_INT_EQ(XvtKeyboardMapping_Shortcut(
					  Chord(AERON_KEY_ESCAPE, modifiers)),
				  XVT_KEYBOARD_SHORTCUT_SETTINGS);
	}
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_Shortcut(Chord(AERON_KEY_TAB, 0)),
			  XVT_KEYBOARD_SHORTCUT_RENDERER);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_Shortcut(
				  Chord(AERON_KEY_TAB, AERON_KEY_MOD_SHIFT)),
			  XVT_KEYBOARD_SHORTCUT_NONE);
	XVT_ASSERT_INT_EQ(
		XvtKeyboardMapping_Shortcut(
			Chord(KEY_M, AERON_KEY_MOD_CTRL | AERON_KEY_MOD_ALT)),
		XVT_KEYBOARD_SHORTCUT_MOUSE);
	XVT_ASSERT_INT_EQ(
		XvtKeyboardMapping_Shortcut(Chord(KEY_M, AERON_KEY_MOD_CTRL)),
		XVT_KEYBOARD_SHORTCUT_NONE);
	XVT_ASSERT_INT_EQ(
		XvtKeyboardMapping_Shortcut(Chord(KEY_M, AERON_KEY_MOD_ALT)),
		XVT_KEYBOARD_SHORTCUT_NONE);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_Shortcut(Chord(KEY_M, 0)),
			  XVT_KEYBOARD_SHORTCUT_NONE);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_Shortcut(Chord(kT, 0)),
			  XVT_KEYBOARD_SHORTCUT_NONE);

	/* The grave key is DEBUG only while the policy allows it. */
	XVT_ASSERT_INT_EQ(
		XvtKeyboardMapping_Shortcut(Chord(AERON_KEY_GRAVE, 0)),
		XVT_KEYBOARD_SHORTCUT_NONE);
	XvtKeyboardMapping_SetPolicy(true);
	XVT_ASSERT_INT_EQ(
		XvtKeyboardMapping_Shortcut(Chord(AERON_KEY_GRAVE, 0)),
		XVT_KEYBOARD_SHORTCUT_DEBUG);
	XvtKeyboardMapping_SetPolicy(false);
}

static void AddEvent(int key, int modifiers, int down, int repeat)
{
	AeronKeyEvent *event = &g_input.key_events[g_input.key_event_count++];
	event->chord = Chord(key, modifiers);
	event->down = (uint8_t)down;
	event->repeat = (uint8_t)repeat;
}

static void CheckTrigger(void)
{
	memset(&g_input, 0, sizeof g_input);
	g_input.has_focus = 1;
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_FindShortcutPress(
				  &g_input, XVT_KEYBOARD_SHORTCUT_SETTINGS),
			  -1);

	/* A release and a repeat are not fresh presses; the fresh press after them is found. */
	AddEvent(AERON_KEY_ESCAPE, 0, 0, 0);
	AddEvent(AERON_KEY_ESCAPE, 0, 1, 1);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_FindShortcutPress(
				  &g_input, XVT_KEYBOARD_SHORTCUT_SETTINGS),
			  -1);
	AddEvent(KEY_M, AERON_KEY_MOD_CTRL | AERON_KEY_MOD_ALT, 1, 0);
	AddEvent(AERON_KEY_ESCAPE, AERON_KEY_MOD_SHIFT, 1, 0);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_FindShortcutPress(
				  &g_input, XVT_KEYBOARD_SHORTCUT_SETTINGS),
			  AERON_KEY_ESCAPE);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_FindShortcutPress(
				  &g_input, XVT_KEYBOARD_SHORTCUT_MOUSE),
			  KEY_M);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_FindShortcutPress(
				  &g_input, XVT_KEYBOARD_SHORTCUT_RENDERER),
			  -1);

	/* Nothing triggers without focus or after the events overflowed. */
	g_input.has_focus = 0;
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_FindShortcutPress(
				  &g_input, XVT_KEYBOARD_SHORTCUT_SETTINGS),
			  -1);
	g_input.has_focus = 1;
	g_input.key_events_overflow = 1;
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_FindShortcutPress(
				  &g_input, XVT_KEYBOARD_SHORTCUT_SETTINGS),
			  -1);
}

static void CheckSourceValid(void)
{
	XvtKeyboardMapping_SetPolicy(false);
	/* With no modifiers, a key is valid exactly when it has a name and is not a shortcut. */
	for (int key = 1; key < AERON_KEY_COUNT; ++key) {
		bool named = AeronKey_Name((AeronKey)key)[0] != '\0';
		bool shortcut = XvtKeyboardMapping_Shortcut(Chord(key, 0)) !=
				XVT_KEYBOARD_SHORTCUT_NONE;
		XVT_ASSERT_INT_EQ(XvtKeyboardMapping_SourceValid(Chord(key, 0)),
				  named && !shortcut);
	}
	XVT_ASSERT_TRUE(AeronKey_Name((AeronKey)kT)[0] != '\0');
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_SourceValid(Chord(0, 0)), 0);
	XVT_ASSERT_INT_EQ(
		XvtKeyboardMapping_SourceValid(Chord(AERON_KEY_COUNT, 0)), 0);

	/* Modifiers come from the four. */
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_SourceValid(Chord(kT, 15)), 1);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_SourceValid(Chord(kT, 16)), 0);

	/* A modifier key takes no modifiers. */
	XVT_ASSERT_INT_EQ(
		XvtKeyboardMapping_SourceValid(Chord(AERON_KEY_LSHIFT, 0)), 1);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_SourceValid(
				  Chord(AERON_KEY_LSHIFT, AERON_KEY_MOD_CTRL)),
			  0);

	/* Shortcuts never bind. */
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_SourceValid(
				  Chord(AERON_KEY_ESCAPE, AERON_KEY_MOD_CTRL)),
			  0);
	XVT_ASSERT_INT_EQ(
		XvtKeyboardMapping_SourceValid(
			Chord(KEY_M, AERON_KEY_MOD_CTRL | AERON_KEY_MOD_ALT)),
		0);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_SourceValid(
				  Chord(AERON_KEY_TAB, AERON_KEY_MOD_SHIFT)),
			  1);
}

static void CheckFormatSource(void)
{
	char text[96];
	char expected[96];
	const char *name = AeronKey_Name((AeronKey)kT);
#if defined(__APPLE__)
	const char *gui = "Command+";
#else
	const char *gui = "Super+";
#endif
	XvtKeyboardMapping_FormatSource(
		text, sizeof text,
		Chord(kT, AERON_KEY_MOD_CTRL | AERON_KEY_MOD_ALT |
				  AERON_KEY_MOD_SHIFT | AERON_KEY_MOD_GUI));
	snprintf(expected, sizeof expected, "Ctrl+Alt+Shift+%s%s", gui, name);
	XVT_ASSERT_INT_EQ(strcmp(text, expected), 0);

	XvtKeyboardMapping_FormatSource(text, sizeof text, Chord(kT, 0));
	XVT_ASSERT_INT_EQ(strcmp(text, name), 0);

	XvtKeyboardMapping_FormatSource(
		text, sizeof text,
		Chord(kT, AERON_KEY_MOD_SHIFT | AERON_KEY_MOD_CTRL));
	snprintf(expected, sizeof expected, "Ctrl+Shift+%s", name);
	XVT_ASSERT_INT_EQ(strcmp(text, expected), 0);
}

static void CheckListHelpers(void)
{
	memset(&g_profile, 0, sizeof g_profile);
	Bind(kT, 0, XVT_INPUT_ACTION_TARGET_PREV);
	Bind(kB, AERON_KEY_MOD_CTRL, XVT_INPUT_ACTION_TARGET_NEXT);
	Bind(kB, 0, XVT_INPUT_ACTION_TARGET_NEXT);
	Bind(kC, 0, XVT_INPUT_ACTION_TARGET_NEXT);

	/* Find matches the chord exactly. */
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_Find(&g_profile, Chord(kB, 0)), 2);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_Find(
				  &g_profile, Chord(kB, AERON_KEY_MOD_CTRL)),
			  1);
	XVT_ASSERT_TRUE(XvtKeyboardMapping_Find(&g_profile,
						Chord(kB, AERON_KEY_MOD_ALT)) ==
			SIZE_MAX);
	XVT_ASSERT_TRUE(XvtKeyboardMapping_Find(&g_profile, Chord(kD, 0)) ==
			SIZE_MAX);

	/* Sort orders by action, then key, then modifiers. */
	XvtKeyboardBindings sorted = g_profile;
	XvtKeyboardMapping_Sort(&sorted);
	XVT_ASSERT_INT_EQ(sorted.count, 4);
	for (size_t i = 1; i < sorted.count; ++i) {
		const XvtKeyboardBinding *a = &sorted.bindings[i - 1];
		const XvtKeyboardBinding *b = &sorted.bindings[i];
		XVT_ASSERT_TRUE(
			a->action < b->action ||
			(a->action == b->action &&
			 (a->source.key < b->source.key ||
			  (a->source.key == b->source.key &&
			   a->source.modifiers <= b->source.modifiers))));
	}

	/* Equal compares in order: the list and its sorted copy differ until both are sorted. */
	XVT_ASSERT_TRUE(XvtKeyboardMapping_Equal(&g_profile, &g_profile));
	XVT_ASSERT_TRUE(!XvtKeyboardMapping_Equal(&g_profile, &sorted));
	XvtKeyboardBindings resorted = g_profile;
	XvtKeyboardMapping_Sort(&resorted);
	XVT_ASSERT_TRUE(XvtKeyboardMapping_Equal(&resorted, &sorted));
	resorted.bindings[3].action = XVT_INPUT_ACTION_TARGET_CLEAR;
	XVT_ASSERT_TRUE(!XvtKeyboardMapping_Equal(&resorted, &sorted));
	resorted = sorted;
	--resorted.count;
	XVT_ASSERT_TRUE(!XvtKeyboardMapping_Equal(&resorted, &sorted));

	/* Remove keeps the order; an index out of range is ignored. */
	XvtKeyboardBindings removed = g_profile;
	XvtKeyboardMapping_Remove(&removed, 1);
	XVT_ASSERT_INT_EQ(removed.count, 3);
	XVT_ASSERT_INT_EQ(removed.bindings[0].source.key, kT);
	XVT_ASSERT_INT_EQ(removed.bindings[1].source.key, kB);
	XVT_ASSERT_INT_EQ(removed.bindings[1].source.modifiers, 0);
	XVT_ASSERT_INT_EQ(removed.bindings[2].source.key, kC);
	XvtKeyboardMapping_Remove(&removed, 3);
	XvtKeyboardMapping_Remove(&removed, SIZE_MAX);
	XVT_ASSERT_INT_EQ(removed.count, 3);
	XvtKeyboardMapping_Remove(&removed, 2);
	XVT_ASSERT_INT_EQ(removed.count, 2);
	XVT_ASSERT_INT_EQ(removed.bindings[1].source.key, kB);
}

static void CheckPressQueuesKey(void)
{
	InstallEnabled();
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadKey(), 0);
	Press(kT, 0);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadKey(),
			  XvtInputActions_Key(XVT_INPUT_ACTION_TARGET_NEXT));
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadKey(), 0);

	/* Each repeat fires again; a release fires nothing. */
	Repeat(kT, 0);
	Repeat(kT, 0);
	Release(kT, 0);
	XVT_ASSERT_INT_EQ(DrainKeys(), 2);

	/* A repeat never starts a press. */
	Repeat(kT, 0);
	XVT_ASSERT_INT_EQ(DrainKeys(), 0);

	/* The chord's modifiers choose the binding. */
	Press(kB, 0);
	XVT_ASSERT_INT_EQ(DrainKeys(), 0);
	Release(kB, 0);
	Press(kB, AERON_KEY_MOD_CTRL);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadKey(),
			  XvtInputActions_Key(XVT_INPUT_ACTION_THROTTLE_UP));
	Release(kB, AERON_KEY_MOD_CTRL);

	/* A modifier key is looked up without its own modifier bit. */
	Press(AERON_KEY_LCTRL, AERON_KEY_MOD_CTRL);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadKey(),
			  XvtInputActions_Key(XVT_INPUT_ACTION_THROTTLE_DOWN));
	Release(AERON_KEY_LCTRL, 0);
}

static void CheckPauseAndEscape(void)
{
	InstallEnabled();
	/* Pause fires on the press but not on repeats. */
	Press(kP, AERON_KEY_MOD_ALT);
	Repeat(kP, AERON_KEY_MOD_ALT);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadKey(),
			  XvtInputActions_Key(XVT_INPUT_ACTION_PAUSE));
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadKey(), 0);
	Release(kP, AERON_KEY_MOD_ALT);

	/* The Escape action asks the port for settings on the press, not on repeats, and queues nothing. */
	Press(kQ, 0);
	XVT_ASSERT_INT_EQ(XvtPort_ConsumeSettingsRequest(), 1);
	Repeat(kQ, 0);
	XVT_ASSERT_INT_EQ(XvtPort_ConsumeSettingsRequest(), 0);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadKey(), 0);
	Release(kQ, 0);
}

static void CheckShortcutsNeverFire(void)
{
	memset(&g_profile, 0, sizeof g_profile);
	Bind(AERON_KEY_TAB, 0, XVT_INPUT_ACTION_TARGET_NEXT);
	Bind(AERON_KEY_ESCAPE, 0, XVT_INPUT_ACTION_TARGET_NEXT);
	XvtKeyboardMapping_Install(&g_profile);
	memset(&g_input, 0, sizeof g_input);
	XvtKeyboardMapping_Enable(true, &g_input);
	Press(AERON_KEY_TAB, 0);
	Repeat(AERON_KEY_TAB, 0);
	Press(AERON_KEY_ESCAPE, 0);
	XVT_ASSERT_INT_EQ(DrainKeys(), 0);
}

static void CheckLaterBindingWins(void)
{
	memset(&g_profile, 0, sizeof g_profile);
	Bind(kT, 0, XVT_INPUT_ACTION_TARGET_NEXT);
	Bind(kT, 0, XVT_INPUT_ACTION_TARGET_PREV);
	XvtKeyboardMapping_Install(&g_profile);
	memset(&g_input, 0, sizeof g_input);
	XvtKeyboardMapping_Enable(true, &g_input);
	Press(kT, 0);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadKey(),
			  XvtInputActions_Key(XVT_INPUT_ACTION_TARGET_PREV));
}

static void CheckHeldButtons(void)
{
	InstallEnabled();
	/* Fire and the target/roll modifier are bits 1 and 2 while held, and queue no key. */
	Press(kC, 0);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadButtons(), 1);
	Press(kD, 0);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadButtons(), 3);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadKey(), 0);
	XvtKeyboardMapping_BeginFrame(&g_input);
	Repeat(kC, 0);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadButtons(), 3);
	Release(kC, 0);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadButtons(), 2);
	Release(kD, 0);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadButtons(), 0);

	/* A tap between two reads is still reported once, then dropped at the next frame. */
	XvtKeyboardMapping_BeginFrame(&g_input);
	Press(kC, 0);
	Release(kC, 0);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadButtons(), 1);
	XvtKeyboardMapping_BeginFrame(&g_input);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadButtons(), 0);

	/* A tap nobody read yet survives the frame boundary. */
	Press(kD, 0);
	Release(kD, 0);
	XvtKeyboardMapping_BeginFrame(&g_input);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadButtons(), 2);
	XvtKeyboardMapping_BeginFrame(&g_input);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadButtons(), 0);
}

static void CheckSuppressedEvent(void)
{
	InstallEnabled();
	Press(kD, 0);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadButtons(), 2);
	XvtKeyboardMapping_BeginFrame(&g_input);

	/* A suppressed event releases the key's action and the key is ignored until released. */
	Event(kD, 0, 1, 1, true);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadButtons(), 0);
	Press(kD, 0);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadButtons(), 0);
	Release(kD, 0);
	Press(kD, 0);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadButtons(), 2);
	Release(kD, 0);

	Event(kT, 0, 1, 0, true);
	Press(kT, 0);
	XVT_ASSERT_INT_EQ(DrainKeys(), 0);
	Release(kT, 0);
	Press(kT, 0);
	XVT_ASSERT_INT_EQ(DrainKeys(), 1);
}

static void CheckSuspendAndEnable(void)
{
	/* Installing leaves the mapping disabled until Enable. */
	InstallEnabled();
	XvtKeyboardMapping_Install(&g_profile);
	Press(kT, 0);
	XVT_ASSERT_INT_EQ(DrainKeys(), 0);

	/* Install forgets queued keys and held buttons. */
	InstallEnabled();
	Press(kT, 0);
	Press(kC, 0);
	XvtKeyboardMapping_Install(&g_profile);
	XVT_ASSERT_INT_EQ(DrainKeys(), 0);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadButtons(), 0);

	/* Suspend forgets everything and disables. */
	InstallEnabled();
	Press(kT, 0);
	Press(kC, 0);
	XvtKeyboardMapping_Suspend();
	XVT_ASSERT_INT_EQ(DrainKeys(), 0);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadButtons(), 0);
	Release(kT, 0);
	Press(kT, 0);
	XVT_ASSERT_INT_EQ(DrainKeys(), 0);

	/* Enable in the state it already has does nothing. */
	InstallEnabled();
	Press(kT, 0);
	XvtKeyboardMapping_Enable(true, &g_input);
	XVT_ASSERT_INT_EQ(DrainKeys(), 1);

	/* Disabling suspends. */
	Press(kC, 0);
	XvtKeyboardMapping_Enable(false, &g_input);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadButtons(), 0);
	Release(kC, 0);
	Press(kC, 0);
	XVT_ASSERT_INT_EQ(XvtKeyboardMapping_ReadButtons(), 0);

	/* Keys already down when the mapping is enabled are ignored until released. */
	XvtKeyboardMapping_Suspend();
	memset(&g_input, 0, sizeof g_input);
	g_input.key_down[kT] = 1;
	XvtKeyboardMapping_Enable(true, &g_input);
	Press(kT, 0);
	XVT_ASSERT_INT_EQ(DrainKeys(), 0);
	Release(kT, 0);
	Press(kT, 0);
	XVT_ASSERT_INT_EQ(DrainKeys(), 1);
}

static void CheckOverflowRestarts(void)
{
	InstallEnabled();
	Press(kT, 0);
	Press(kB, AERON_KEY_MOD_CTRL);

	/* After overflowed events the mapping restarts as Enable would: the queue empties and keys down in
	 * the snapshot are ignored until released. */
	g_input.key_events_overflow = 1;
	g_input.key_down[kB] = 1;
	XvtKeyboardMapping_BeginFrame(&g_input);
	XVT_ASSERT_INT_EQ(DrainKeys(), 0);
	Press(kB, AERON_KEY_MOD_CTRL);
	XVT_ASSERT_INT_EQ(DrainKeys(), 0);
	Release(kB, AERON_KEY_MOD_CTRL);
	Press(kB, AERON_KEY_MOD_CTRL);
	XVT_ASSERT_INT_EQ(DrainKeys(), 1);

	/* A key the snapshot shows up starts afresh: its repeat starts nothing, its press fires. */
	Repeat(kT, 0);
	XVT_ASSERT_INT_EQ(DrainKeys(), 0);
	Press(kT, 0);
	XVT_ASSERT_INT_EQ(DrainKeys(), 1);
}

static void CheckQueueLimit(void)
{
	InstallEnabled();
	Press(kT, 0);
	for (int i = 0; i < 299; ++i) {
		Repeat(kT, 0);
	}
	XVT_ASSERT_INT_EQ(DrainKeys(), 255);
}

int main(void)
{
	CheckShortcut();
	CheckTrigger();
	CheckSourceValid();
	CheckFormatSource();
	CheckListHelpers();
	CheckPressQueuesKey();
	CheckPauseAndEscape();
	CheckShortcutsNeverFire();
	CheckLaterBindingWins();
	CheckHeldButtons();
	CheckSuppressedEvent();
	CheckSuspendAndEnable();
	CheckOverflowRestarts();
	CheckQueueLimit();
	return 0;
}
