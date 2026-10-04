/* Checks the keyboard mapping (xvt_runtime/input/keyboard_mapping.h) against the promises in its header:
 * which chords are reserved shortcuts and when they trigger, which sources are valid and how they are
 * labelled, the binding list helpers, and how key events become queued flight keys and held buttons.
 * Every case installs its own bindings and starts from a suspended mapping. No device is opened: key
 * events and snapshots are built by the test. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "aeron/input.h"
#include "test_assert.h"
#include "xvt_runtime/input/keyboard_mapping.h"
#include "xvt_runtime/runtime/port.h"

enum { KEY_M = AERON_KEY_A + ('m' - 'a') };

static AeronInputSnapshot g_input;
static struct xvt_keyboard_bindings g_profile;

static AeronKeyChord chord(int key, int modifiers)
{
	AeronKeyChord chord = {(uint16_t)key, (uint8_t)modifiers};
	return chord;
}

static void bind(int key, int modifiers, xvt_input_action action)
{
	g_profile.bindings[g_profile.count].source = chord(key, modifiers);
	g_profile.bindings[g_profile.count].action = action;
	++g_profile.count;
}

static void event(int key, int modifiers, int down, int repeat, bool suppressed)
{
	AeronKeyEvent event;
	memset(&event, 0, sizeof event);
	event.chord = chord(key, modifiers);
	event.down = (uint8_t)down;
	event.repeat = (uint8_t)repeat;
	xvt_keyboard_mapping_event(&event, suppressed);
}

static void press(int key, int modifiers)
{
	event(key, modifiers, 1, 0, false);
}

static void repeat(int key, int modifiers)
{
	event(key, modifiers, 1, 1, false);
}

static void release(int key, int modifiers)
{
	event(key, modifiers, 0, 0, false);
}

/* Reads every queued key; returns how many there were. */
static int drain_keys(void)
{
	int count = 0;
	while (xvt_keyboard_mapping_read_key()) {
		++count;
	}
	return count;
}

/* The bindings every event case uses, installed and enabled with no key held. */
static void install_enabled(void)
{
	memset(&g_profile, 0, sizeof g_profile);
	bind(AERON_KEY_A + ('t' - 'a'), 0, XVT_INPUT_ACTION_TARGET_NEXT);
	bind(AERON_KEY_A + ('b' - 'a'), AERON_KEY_MOD_CTRL,
	     XVT_INPUT_ACTION_THROTTLE_UP);
	bind(AERON_KEY_A + ('c' - 'a'), 0, XVT_INPUT_ACTION_FIRE_WEAPON);
	bind(AERON_KEY_A + ('d' - 'a'), 0,
	     XVT_INPUT_ACTION_TARGET_ROLL_MODIFIER);
	bind(AERON_KEY_A + ('p' - 'a'), AERON_KEY_MOD_ALT,
	     XVT_INPUT_ACTION_PAUSE);
	bind(AERON_KEY_A + ('q' - 'a'), 0, XVT_INPUT_ACTION_ESCAPE);
	bind(AERON_KEY_LCTRL, 0, XVT_INPUT_ACTION_THROTTLE_DOWN);
	xvt_keyboard_mapping_install(&g_profile);
	memset(&g_input, 0, sizeof g_input);
	xvt_keyboard_mapping_enable(true, &g_input);
	xvt_port_consume_settings_request();
}

static const int k_t = AERON_KEY_A + ('t' - 'a');
static const int k_b = AERON_KEY_A + ('b' - 'a');
static const int k_c = AERON_KEY_A + ('c' - 'a');
static const int k_d = AERON_KEY_A + ('d' - 'a');
static const int k_p = AERON_KEY_A + ('p' - 'a');
static const int k_q = AERON_KEY_A + ('q' - 'a');

static void check_shortcut(void)
{
	xvt_keyboard_mapping_set_policy(false);
	for (int modifiers = 0; modifiers < 16; ++modifiers) {
		XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_shortcut(
					  chord(AERON_KEY_ESCAPE, modifiers)),
				  XVT_KEYBOARD_SHORTCUT_SETTINGS);
	}
	XVT_ASSERT_INT_EQ(
		xvt_keyboard_mapping_shortcut(chord(AERON_KEY_TAB, 0)),
		XVT_KEYBOARD_SHORTCUT_RENDERER);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_shortcut(
				  chord(AERON_KEY_TAB, AERON_KEY_MOD_SHIFT)),
			  XVT_KEYBOARD_SHORTCUT_NONE);
	XVT_ASSERT_INT_EQ(
		xvt_keyboard_mapping_shortcut(
			chord(KEY_M, AERON_KEY_MOD_CTRL | AERON_KEY_MOD_ALT)),
		XVT_KEYBOARD_SHORTCUT_MOUSE);
	XVT_ASSERT_INT_EQ(
		xvt_keyboard_mapping_shortcut(chord(KEY_M, AERON_KEY_MOD_CTRL)),
		XVT_KEYBOARD_SHORTCUT_NONE);
	XVT_ASSERT_INT_EQ(
		xvt_keyboard_mapping_shortcut(chord(KEY_M, AERON_KEY_MOD_ALT)),
		XVT_KEYBOARD_SHORTCUT_NONE);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_shortcut(chord(KEY_M, 0)),
			  XVT_KEYBOARD_SHORTCUT_NONE);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_shortcut(chord(k_t, 0)),
			  XVT_KEYBOARD_SHORTCUT_NONE);

	/* The grave key is DEBUG only while the policy allows it. */
	XVT_ASSERT_INT_EQ(
		xvt_keyboard_mapping_shortcut(chord(AERON_KEY_GRAVE, 0)),
		XVT_KEYBOARD_SHORTCUT_NONE);
	xvt_keyboard_mapping_set_policy(true);
	XVT_ASSERT_INT_EQ(
		xvt_keyboard_mapping_shortcut(chord(AERON_KEY_GRAVE, 0)),
		XVT_KEYBOARD_SHORTCUT_DEBUG);
	xvt_keyboard_mapping_set_policy(false);
}

static void add_event(int key, int modifiers, int down, int repeat)
{
	AeronKeyEvent *event = &g_input.key_events[g_input.key_event_count++];
	event->chord = chord(key, modifiers);
	event->down = (uint8_t)down;
	event->repeat = (uint8_t)repeat;
}

static void check_trigger(void)
{
	memset(&g_input, 0, sizeof g_input);
	g_input.has_focus = 1;
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_find_shortcut_press(
				  &g_input, XVT_KEYBOARD_SHORTCUT_SETTINGS),
			  -1);

	/* A release and a repeat are not fresh presses; the fresh press after them is found. */
	add_event(AERON_KEY_ESCAPE, 0, 0, 0);
	add_event(AERON_KEY_ESCAPE, 0, 1, 1);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_find_shortcut_press(
				  &g_input, XVT_KEYBOARD_SHORTCUT_SETTINGS),
			  -1);
	add_event(KEY_M, AERON_KEY_MOD_CTRL | AERON_KEY_MOD_ALT, 1, 0);
	add_event(AERON_KEY_ESCAPE, AERON_KEY_MOD_SHIFT, 1, 0);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_find_shortcut_press(
				  &g_input, XVT_KEYBOARD_SHORTCUT_SETTINGS),
			  AERON_KEY_ESCAPE);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_find_shortcut_press(
				  &g_input, XVT_KEYBOARD_SHORTCUT_MOUSE),
			  KEY_M);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_find_shortcut_press(
				  &g_input, XVT_KEYBOARD_SHORTCUT_RENDERER),
			  -1);

	/* Nothing triggers without focus or after the events overflowed. */
	g_input.has_focus = 0;
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_find_shortcut_press(
				  &g_input, XVT_KEYBOARD_SHORTCUT_SETTINGS),
			  -1);
	g_input.has_focus = 1;
	g_input.key_events_overflow = 1;
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_find_shortcut_press(
				  &g_input, XVT_KEYBOARD_SHORTCUT_SETTINGS),
			  -1);
}

static void check_source_valid(void)
{
	xvt_keyboard_mapping_set_policy(false);
	/* With no modifiers, a key is valid exactly when it has a name and is not a shortcut. */
	for (int key = 1; key < AERON_KEY_COUNT; ++key) {
		bool named = AeronKey_Name((AeronKey)key)[0] != '\0';
		bool shortcut = xvt_keyboard_mapping_shortcut(chord(key, 0)) !=
				XVT_KEYBOARD_SHORTCUT_NONE;
		XVT_ASSERT_INT_EQ(
			xvt_keyboard_mapping_source_valid(chord(key, 0)),
			named && !shortcut);
	}
	XVT_ASSERT_TRUE(AeronKey_Name((AeronKey)k_t)[0] != '\0');
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_source_valid(chord(0, 0)), 0);
	XVT_ASSERT_INT_EQ(
		xvt_keyboard_mapping_source_valid(chord(AERON_KEY_COUNT, 0)),
		0);

	/* Modifiers come from the four. */
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_source_valid(chord(k_t, 15)), 1);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_source_valid(chord(k_t, 16)), 0);

	/* A modifier key takes no modifiers. */
	XVT_ASSERT_INT_EQ(
		xvt_keyboard_mapping_source_valid(chord(AERON_KEY_LSHIFT, 0)),
		1);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_source_valid(
				  chord(AERON_KEY_LSHIFT, AERON_KEY_MOD_CTRL)),
			  0);

	/* Shortcuts never bind. */
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_source_valid(
				  chord(AERON_KEY_ESCAPE, AERON_KEY_MOD_CTRL)),
			  0);
	XVT_ASSERT_INT_EQ(
		xvt_keyboard_mapping_source_valid(
			chord(KEY_M, AERON_KEY_MOD_CTRL | AERON_KEY_MOD_ALT)),
		0);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_source_valid(
				  chord(AERON_KEY_TAB, AERON_KEY_MOD_SHIFT)),
			  1);
}

static void check_format_source(void)
{
	const char *name = AeronKey_Name((AeronKey)k_t);
#if defined(__APPLE__)
	const char *gui = "Command+";
#else
	const char *gui = "Super+";
#endif
	char text[96];
	xvt_keyboard_mapping_format_source(
		text, sizeof text,
		chord(k_t, AERON_KEY_MOD_CTRL | AERON_KEY_MOD_ALT |
				   AERON_KEY_MOD_SHIFT | AERON_KEY_MOD_GUI));
	char expected[96];
	snprintf(expected, sizeof expected, "Ctrl+Alt+Shift+%s%s", gui, name);
	XVT_ASSERT_INT_EQ(strcmp(text, expected), 0);

	xvt_keyboard_mapping_format_source(text, sizeof text, chord(k_t, 0));
	XVT_ASSERT_INT_EQ(strcmp(text, name), 0);

	xvt_keyboard_mapping_format_source(
		text, sizeof text,
		chord(k_t, AERON_KEY_MOD_SHIFT | AERON_KEY_MOD_CTRL));
	snprintf(expected, sizeof expected, "Ctrl+Shift+%s", name);
	XVT_ASSERT_INT_EQ(strcmp(text, expected), 0);
}

static void check_list_helpers(void)
{
	memset(&g_profile, 0, sizeof g_profile);
	bind(k_t, 0, XVT_INPUT_ACTION_TARGET_PREV);
	bind(k_b, AERON_KEY_MOD_CTRL, XVT_INPUT_ACTION_TARGET_NEXT);
	bind(k_b, 0, XVT_INPUT_ACTION_TARGET_NEXT);
	bind(k_c, 0, XVT_INPUT_ACTION_TARGET_NEXT);

	/* Find matches the chord exactly. */
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_find(&g_profile, chord(k_b, 0)),
			  2);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_find(
				  &g_profile, chord(k_b, AERON_KEY_MOD_CTRL)),
			  1);
	XVT_ASSERT_TRUE(xvt_keyboard_mapping_find(
				&g_profile, chord(k_b, AERON_KEY_MOD_ALT)) ==
			SIZE_MAX);
	XVT_ASSERT_TRUE(xvt_keyboard_mapping_find(&g_profile, chord(k_d, 0)) ==
			SIZE_MAX);

	/* Sort orders by action, then key, then modifiers. */
	struct xvt_keyboard_bindings sorted = g_profile;
	xvt_keyboard_mapping_sort(&sorted);
	XVT_ASSERT_INT_EQ(sorted.count, 4);
	for (size_t i = 1; i < sorted.count; ++i) {
		const struct xvt_keyboard_binding *a = &sorted.bindings[i - 1];
		const struct xvt_keyboard_binding *b = &sorted.bindings[i];
		XVT_ASSERT_TRUE(
			a->action < b->action ||
			(a->action == b->action &&
			 (a->source.key < b->source.key ||
			  (a->source.key == b->source.key &&
			   a->source.modifiers <= b->source.modifiers))));
	}

	/* Equal compares in order: the list and its sorted copy differ until both are sorted. */
	XVT_ASSERT_TRUE(xvt_keyboard_mapping_equal(&g_profile, &g_profile));
	XVT_ASSERT_TRUE(!xvt_keyboard_mapping_equal(&g_profile, &sorted));
	struct xvt_keyboard_bindings resorted = g_profile;
	xvt_keyboard_mapping_sort(&resorted);
	XVT_ASSERT_TRUE(xvt_keyboard_mapping_equal(&resorted, &sorted));
	resorted.bindings[3].action = XVT_INPUT_ACTION_TARGET_CLEAR;
	XVT_ASSERT_TRUE(!xvt_keyboard_mapping_equal(&resorted, &sorted));
	resorted = sorted;
	--resorted.count;
	XVT_ASSERT_TRUE(!xvt_keyboard_mapping_equal(&resorted, &sorted));

	/* Remove keeps the order; an index out of range is ignored. */
	struct xvt_keyboard_bindings removed = g_profile;
	xvt_keyboard_mapping_remove(&removed, 1);
	XVT_ASSERT_INT_EQ(removed.count, 3);
	XVT_ASSERT_INT_EQ(removed.bindings[0].source.key, k_t);
	XVT_ASSERT_INT_EQ(removed.bindings[1].source.key, k_b);
	XVT_ASSERT_INT_EQ(removed.bindings[1].source.modifiers, 0);
	XVT_ASSERT_INT_EQ(removed.bindings[2].source.key, k_c);
	xvt_keyboard_mapping_remove(&removed, 3);
	xvt_keyboard_mapping_remove(&removed, SIZE_MAX);
	XVT_ASSERT_INT_EQ(removed.count, 3);
	xvt_keyboard_mapping_remove(&removed, 2);
	XVT_ASSERT_INT_EQ(removed.count, 2);
	XVT_ASSERT_INT_EQ(removed.bindings[1].source.key, k_b);
}

static void check_press_queues_key(void)
{
	install_enabled();
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_key(), 0);
	press(k_t, 0);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_key(),
			  xvt_input_actions_key(XVT_INPUT_ACTION_TARGET_NEXT));
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_key(), 0);

	/* Each repeat fires again; a release fires nothing. */
	repeat(k_t, 0);
	repeat(k_t, 0);
	release(k_t, 0);
	XVT_ASSERT_INT_EQ(drain_keys(), 2);

	/* A repeat never starts a press. */
	repeat(k_t, 0);
	XVT_ASSERT_INT_EQ(drain_keys(), 0);

	/* The chord's modifiers choose the binding. */
	press(k_b, 0);
	XVT_ASSERT_INT_EQ(drain_keys(), 0);
	release(k_b, 0);
	press(k_b, AERON_KEY_MOD_CTRL);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_key(),
			  xvt_input_actions_key(XVT_INPUT_ACTION_THROTTLE_UP));
	release(k_b, AERON_KEY_MOD_CTRL);

	/* A modifier key is looked up without its own modifier bit. */
	press(AERON_KEY_LCTRL, AERON_KEY_MOD_CTRL);
	XVT_ASSERT_INT_EQ(
		xvt_keyboard_mapping_read_key(),
		xvt_input_actions_key(XVT_INPUT_ACTION_THROTTLE_DOWN));
	release(AERON_KEY_LCTRL, 0);
}

static void check_pause_and_escape(void)
{
	install_enabled();
	/* Pause fires on the press but not on repeats. */
	press(k_p, AERON_KEY_MOD_ALT);
	repeat(k_p, AERON_KEY_MOD_ALT);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_key(),
			  xvt_input_actions_key(XVT_INPUT_ACTION_PAUSE));
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_key(), 0);
	release(k_p, AERON_KEY_MOD_ALT);

	/* The Escape action asks the port for settings on the press, not on repeats, and queues nothing. */
	press(k_q, 0);
	XVT_ASSERT_INT_EQ(xvt_port_consume_settings_request(), 1);
	repeat(k_q, 0);
	XVT_ASSERT_INT_EQ(xvt_port_consume_settings_request(), 0);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_key(), 0);
	release(k_q, 0);
}

static void check_shortcuts_never_fire(void)
{
	memset(&g_profile, 0, sizeof g_profile);
	bind(AERON_KEY_TAB, 0, XVT_INPUT_ACTION_TARGET_NEXT);
	bind(AERON_KEY_ESCAPE, 0, XVT_INPUT_ACTION_TARGET_NEXT);
	xvt_keyboard_mapping_install(&g_profile);
	memset(&g_input, 0, sizeof g_input);
	xvt_keyboard_mapping_enable(true, &g_input);
	press(AERON_KEY_TAB, 0);
	repeat(AERON_KEY_TAB, 0);
	press(AERON_KEY_ESCAPE, 0);
	XVT_ASSERT_INT_EQ(drain_keys(), 0);
}

static void check_later_binding_wins(void)
{
	memset(&g_profile, 0, sizeof g_profile);
	bind(k_t, 0, XVT_INPUT_ACTION_TARGET_NEXT);
	bind(k_t, 0, XVT_INPUT_ACTION_TARGET_PREV);
	xvt_keyboard_mapping_install(&g_profile);
	memset(&g_input, 0, sizeof g_input);
	xvt_keyboard_mapping_enable(true, &g_input);
	press(k_t, 0);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_key(),
			  xvt_input_actions_key(XVT_INPUT_ACTION_TARGET_PREV));
}

static void check_held_buttons(void)
{
	install_enabled();
	/* Fire and the target/roll modifier are bits 1 and 2 while held, and queue no key. */
	press(k_c, 0);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_buttons(), 1);
	press(k_d, 0);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_buttons(), 3);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_key(), 0);
	xvt_keyboard_mapping_begin_frame(&g_input);
	repeat(k_c, 0);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_buttons(), 3);
	release(k_c, 0);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_buttons(), 2);
	release(k_d, 0);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_buttons(), 0);

	/* A tap between two reads is still reported once, then dropped at the next frame. */
	xvt_keyboard_mapping_begin_frame(&g_input);
	press(k_c, 0);
	release(k_c, 0);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_buttons(), 1);
	xvt_keyboard_mapping_begin_frame(&g_input);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_buttons(), 0);

	/* A tap nobody read yet survives the frame boundary. */
	press(k_d, 0);
	release(k_d, 0);
	xvt_keyboard_mapping_begin_frame(&g_input);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_buttons(), 2);
	xvt_keyboard_mapping_begin_frame(&g_input);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_buttons(), 0);
}

static void check_suppressed_event(void)
{
	install_enabled();
	press(k_d, 0);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_buttons(), 2);
	xvt_keyboard_mapping_begin_frame(&g_input);

	/* A suppressed event releases the key's action and the key is ignored until released. */
	event(k_d, 0, 1, 1, true);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_buttons(), 0);
	press(k_d, 0);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_buttons(), 0);
	release(k_d, 0);
	press(k_d, 0);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_buttons(), 2);
	release(k_d, 0);

	event(k_t, 0, 1, 0, true);
	press(k_t, 0);
	XVT_ASSERT_INT_EQ(drain_keys(), 0);
	release(k_t, 0);
	press(k_t, 0);
	XVT_ASSERT_INT_EQ(drain_keys(), 1);
}

static void check_suspend_and_enable(void)
{
	/* Installing leaves the mapping disabled until Enable. */
	install_enabled();
	xvt_keyboard_mapping_install(&g_profile);
	press(k_t, 0);
	XVT_ASSERT_INT_EQ(drain_keys(), 0);

	/* Install forgets queued keys and held buttons. */
	install_enabled();
	press(k_t, 0);
	press(k_c, 0);
	xvt_keyboard_mapping_install(&g_profile);
	XVT_ASSERT_INT_EQ(drain_keys(), 0);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_buttons(), 0);

	/* Suspend forgets everything and disables. */
	install_enabled();
	press(k_t, 0);
	press(k_c, 0);
	xvt_keyboard_mapping_suspend();
	XVT_ASSERT_INT_EQ(drain_keys(), 0);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_buttons(), 0);
	release(k_t, 0);
	press(k_t, 0);
	XVT_ASSERT_INT_EQ(drain_keys(), 0);

	/* Enable in the state it already has does nothing. */
	install_enabled();
	press(k_t, 0);
	xvt_keyboard_mapping_enable(true, &g_input);
	XVT_ASSERT_INT_EQ(drain_keys(), 1);

	/* Disabling suspends. */
	press(k_c, 0);
	xvt_keyboard_mapping_enable(false, &g_input);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_buttons(), 0);
	release(k_c, 0);
	press(k_c, 0);
	XVT_ASSERT_INT_EQ(xvt_keyboard_mapping_read_buttons(), 0);

	/* Keys already down when the mapping is enabled are ignored until released. */
	xvt_keyboard_mapping_suspend();
	memset(&g_input, 0, sizeof g_input);
	g_input.key_down[k_t] = 1;
	xvt_keyboard_mapping_enable(true, &g_input);
	press(k_t, 0);
	XVT_ASSERT_INT_EQ(drain_keys(), 0);
	release(k_t, 0);
	press(k_t, 0);
	XVT_ASSERT_INT_EQ(drain_keys(), 1);
}

static void check_overflow_restarts(void)
{
	install_enabled();
	press(k_t, 0);
	press(k_b, AERON_KEY_MOD_CTRL);

	/* After overflowed events the mapping restarts as Enable would: the queue empties and keys down in
	 * the snapshot are ignored until released. */
	g_input.key_events_overflow = 1;
	g_input.key_down[k_b] = 1;
	xvt_keyboard_mapping_begin_frame(&g_input);
	XVT_ASSERT_INT_EQ(drain_keys(), 0);
	press(k_b, AERON_KEY_MOD_CTRL);
	XVT_ASSERT_INT_EQ(drain_keys(), 0);
	release(k_b, AERON_KEY_MOD_CTRL);
	press(k_b, AERON_KEY_MOD_CTRL);
	XVT_ASSERT_INT_EQ(drain_keys(), 1);

	/* A key the snapshot shows up starts afresh: its repeat starts nothing, its press fires. */
	repeat(k_t, 0);
	XVT_ASSERT_INT_EQ(drain_keys(), 0);
	press(k_t, 0);
	XVT_ASSERT_INT_EQ(drain_keys(), 1);
}

static void check_queue_limit(void)
{
	install_enabled();
	press(k_t, 0);
	for (int i = 0; i < 299; ++i) {
		repeat(k_t, 0);
	}
	XVT_ASSERT_INT_EQ(drain_keys(), 255);
}

int main(void)
{
	check_shortcut();
	check_trigger();
	check_source_valid();
	check_format_source();
	check_list_helpers();
	check_press_queues_key();
	check_pause_and_escape();
	check_shortcuts_never_fire();
	check_later_binding_wins();
	check_held_buttons();
	check_suppressed_event();
	check_suspend_and_enable();
	check_overflow_restarts();
	check_queue_limit();
	return 0;
}
