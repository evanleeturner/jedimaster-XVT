#include "xvt_runtime/input/keyboard_mapping.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/aeron.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/port.h"

struct keyboard_press {
	xvt_input_action action;
	bool down;
	bool ignored;
};

static struct {
	uint16_t actions[AERON_KEY_COUNT][16];
	struct keyboard_press pressed[AERON_KEY_COUNT];
	bool debug_available;
	bool enabled;
	uint16_t holds[2];
	uint8_t queue[256];
	unsigned read;
	unsigned write;
	uint16_t pending;
	uint16_t observed;
} g_keyboard;

void xvt_keyboard_mapping_set_policy(bool debug_available)
{
	g_keyboard.debug_available = debug_available;
}

xvt_keyboard_shortcut xvt_keyboard_mapping_shortcut(AeronKeyChord source)
{
	if (source.key == AERON_KEY_ESCAPE) {
		return XVT_KEYBOARD_SHORTCUT_SETTINGS;
	}
	if (source.key == AERON_KEY_TAB && !source.modifiers) {
		return XVT_KEYBOARD_SHORTCUT_RENDERER;
	}
	if (source.key == AERON_KEY_GRAVE && g_keyboard.debug_available) {
		return XVT_KEYBOARD_SHORTCUT_DEBUG;
	}
	if (source.key == AERON_KEY_A + ('m' - 'a') &&
	    (source.modifiers & (AERON_KEY_MOD_CTRL | AERON_KEY_MOD_ALT)) ==
		    (AERON_KEY_MOD_CTRL | AERON_KEY_MOD_ALT)) {
		return XVT_KEYBOARD_SHORTCUT_MOUSE;
	}
	return XVT_KEYBOARD_SHORTCUT_NONE;
}

int xvt_keyboard_mapping_find_shortcut_press(const AeronInputSnapshot *input,
					     xvt_keyboard_shortcut shortcut)
{
	if (!input || !input->has_focus || input->key_events_overflow) {
		return -1;
	}
	for (uint16_t i = 0; i < input->key_event_count; ++i) {
		const AeronKeyEvent *event = &input->key_events[i];
		if (event->down && !event->repeat &&
		    xvt_keyboard_mapping_shortcut(event->chord) == shortcut) {
			return event->chord.key;
		}
	}
	return -1;
}

bool xvt_keyboard_mapping_source_valid(AeronKeyChord source)
{
	return source.key > 0 && source.key < AERON_KEY_COUNT &&
	       source.modifiers < 16 &&
	       AeronKey_Name((AeronKey)source.key)[0] &&
	       (!AeronKey_Modifier((AeronKey)source.key) ||
		source.modifiers == 0) &&
	       xvt_keyboard_mapping_shortcut(source) ==
		       XVT_KEYBOARD_SHORTCUT_NONE;
}

#if defined(__APPLE__)
#define GUI_MODIFIER_LABEL "Command+"
#else
#define GUI_MODIFIER_LABEL "Super+"
#endif

void xvt_keyboard_mapping_format_source(char *text, size_t capacity,
					AeronKeyChord source)
{
	snprintf(text, capacity, "%s%s%s%s%s",
		 (source.modifiers & AERON_KEY_MOD_CTRL) ? "Ctrl+" : "",
		 (source.modifiers & AERON_KEY_MOD_ALT) ? "Alt+" : "",
		 (source.modifiers & AERON_KEY_MOD_SHIFT) ? "Shift+" : "",
		 (source.modifiers & AERON_KEY_MOD_GUI) ? GUI_MODIFIER_LABEL
							: "",
		 AeronKey_Name((AeronKey)source.key));
}

size_t xvt_keyboard_mapping_find(const struct xvt_keyboard_bindings *profile,
				 AeronKeyChord source)
{
	for (size_t i = 0; i < profile->count; ++i) {
		if (profile->bindings[i].source.key == source.key &&
		    profile->bindings[i].source.modifiers == source.modifiers) {
			return i;
		}
	}
	return SIZE_MAX;
}

static int binding_compare(const void *left, const void *right)
{
	const struct xvt_keyboard_binding *a = left;
	const struct xvt_keyboard_binding *b = right;
	if (a->action != b->action) {
		return (int)a->action - (int)b->action;
	}
	if (a->source.key != b->source.key) {
		return (int)a->source.key - (int)b->source.key;
	}
	return (int)a->source.modifiers - (int)b->source.modifiers;
}

void xvt_keyboard_mapping_sort(struct xvt_keyboard_bindings *profile)
{
	qsort(profile->bindings, profile->count, sizeof profile->bindings[0],
	      binding_compare);
}

bool xvt_keyboard_mapping_equal(const struct xvt_keyboard_bindings *a,
				const struct xvt_keyboard_bindings *b)
{
	if (a->count != b->count) {
		return false;
	}
	for (size_t i = 0; i < a->count; ++i) {
		if (binding_compare(&a->bindings[i], &b->bindings[i])) {
			return false;
		}
	}
	return true;
}

void xvt_keyboard_mapping_remove(struct xvt_keyboard_bindings *profile,
				 size_t index)
{
	if (index >= profile->count) {
		return;
	}
	memmove(&profile->bindings[index], &profile->bindings[index + 1],
		(profile->count - index - 1) * sizeof profile->bindings[0]);
	--profile->count;
}

static uint16_t button_bit(xvt_input_action action)
{
	return action == XVT_INPUT_ACTION_FIRE_WEAPON		 ? 1
	       : action == XVT_INPUT_ACTION_TARGET_ROLL_MODIFIER ? 2
								 : 0;
}

static void dispatch(xvt_input_action action, bool down, bool repeat)
{
	uint16_t bit = button_bit(action);
	if (bit) {
		if (!repeat) {
			unsigned index = bit == 1 ? 0 : 1;
			if (down) {
				++g_keyboard.holds[index];
			} else if (g_keyboard.holds[index]) {
				--g_keyboard.holds[index];
			}
		}
		return;
	}
	if (!down || (repeat && (action == XVT_INPUT_ACTION_PAUSE ||
				 action == XVT_INPUT_ACTION_ESCAPE))) {
		return;
	}
	if (action == XVT_INPUT_ACTION_ESCAPE) {
		xvt_port_request_settings();
		return;
	}
	unsigned next = (g_keyboard.write + 1) % 256;
	if (next == g_keyboard.read) {
		XVT_LOG_WARN("input.queue_full queue=keyboard");
		return;
	}
	g_keyboard.queue[g_keyboard.write] =
		(uint8_t)xvt_input_actions_key(action);
	g_keyboard.write = next;
}

void xvt_keyboard_mapping_suspend(void)
{
	memset(g_keyboard.pressed, 0, sizeof g_keyboard.pressed);
	memset(g_keyboard.holds, 0, sizeof g_keyboard.holds);
	g_keyboard.read = 0;
	g_keyboard.write = 0;
	g_keyboard.pending = 0;
	g_keyboard.observed = 0;
	g_keyboard.enabled = false;
}

static void compile(uint16_t table[AERON_KEY_COUNT][16],
		    const struct xvt_keyboard_bindings *profile)
{
	memset(table, 0, sizeof g_keyboard.actions);
	for (size_t i = 0; i < profile->count; ++i) {
		const struct xvt_keyboard_binding *b = &profile->bindings[i];
		table[b->source.key][b->source.modifiers] = (uint16_t)b->action;
	}
}

void xvt_keyboard_mapping_install(const struct xvt_keyboard_bindings *profile)
{
	xvt_keyboard_mapping_suspend();
	compile(g_keyboard.actions, profile);
}

void xvt_keyboard_mapping_enable(bool enabled, const AeronInputSnapshot *input)
{
	if (enabled == g_keyboard.enabled) {
		return;
	}
	xvt_keyboard_mapping_suspend();
	g_keyboard.enabled = enabled;
	if (enabled && input) {
		for (int key = 0; key < AERON_KEY_COUNT; ++key) {
			g_keyboard.pressed[key].ignored =
				input->key_down[key] != 0;
		}
	}
}

void xvt_keyboard_mapping_begin_frame(const AeronInputSnapshot *input)
{
	g_keyboard.pending &= (uint16_t)~g_keyboard.observed;
	g_keyboard.observed = 0;
	if (input->key_events_overflow && g_keyboard.enabled) {
		xvt_keyboard_mapping_suspend();
		xvt_keyboard_mapping_enable(true, input);
	}
}

void xvt_keyboard_mapping_event(const AeronKeyEvent *event, bool suppressed)
{
	if (!g_keyboard.enabled || event->chord.key >= AERON_KEY_COUNT) {
		return;
	}
	struct keyboard_press *press = &g_keyboard.pressed[event->chord.key];
	if (!event->down) {
		if (press->down && press->action != XVT_INPUT_ACTION_NONE) {
			dispatch(press->action, false, false);
		}
		*press = (struct keyboard_press){0};
		return;
	}
	if (press->ignored) {
		return;
	}
	if (suppressed) {
		if (press->down && press->action != XVT_INPUT_ACTION_NONE) {
			dispatch(press->action, false, false);
		}
		*press = (struct keyboard_press){.ignored = true};
		return;
	}
	if (!press->down) {
		if (event->repeat) {
			return; /* Resuming never turns typematic into a fresh press. */
		}
		AeronKeyChord chord = event->chord;
		chord.modifiers &=
			(uint8_t)~AeronKey_Modifier((AeronKey)chord.key);
		press->down = true;
		if (xvt_keyboard_mapping_shortcut(event->chord) !=
		    XVT_KEYBOARD_SHORTCUT_NONE) {
			return;
		}
		press->action =
			(xvt_input_action)
				g_keyboard.actions[chord.key][chord.modifiers];
		if (press->action != XVT_INPUT_ACTION_NONE) {
			g_keyboard.pending |= button_bit(press->action);
		}
	} else if (!event->repeat) {
		return;
	}
	if (press->action != XVT_INPUT_ACTION_NONE) {
		dispatch(press->action, true, event->repeat != 0);
	}
}

uint16_t xvt_keyboard_mapping_read_key(void)
{
	if (g_keyboard.read == g_keyboard.write) {
		return 0;
	}
	uint16_t key = g_keyboard.queue[g_keyboard.read];
	g_keyboard.read = (g_keyboard.read + 1) % 256;
	return key;
}

uint16_t xvt_keyboard_mapping_read_buttons(void)
{
	g_keyboard.observed |= g_keyboard.pending;
	return (g_keyboard.holds[0] ? 1 : 0) | (g_keyboard.holds[1] ? 2 : 0) |
	       g_keyboard.pending;
}
