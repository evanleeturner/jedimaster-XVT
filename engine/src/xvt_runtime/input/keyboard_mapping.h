#ifndef XVT_KEYBOARD_MAPPING_H
#define XVT_KEYBOARD_MAPPING_H

#include "aeron/input.h"
#include "xvt_runtime/input/actions.h"
#include <stddef.h>

/* Turns key events into the original game's input. Each bound chord, a key plus modifiers, names an
 * action; a press queues the action's flight key code for ReadKey. Fire and the target/roll modifier
 * are held buttons read through ReadButtons instead, and Escape asks the port to open settings. A few
 * chords are reserved shortcuts (see Shortcut) and never bind. One global mapping; not thread-safe. */

#define XVT_KEYBOARD_BINDING_CAP 256

struct xvt_keyboard_binding {
	AeronKeyChord source;
	xvt_input_action action;
};

struct xvt_keyboard_bindings {
	struct xvt_keyboard_binding bindings[XVT_KEYBOARD_BINDING_CAP];
	size_t count;
};

typedef enum xvt_keyboard_shortcut {
	XVT_KEYBOARD_SHORTCUT_NONE,
	XVT_KEYBOARD_SHORTCUT_SETTINGS,
	XVT_KEYBOARD_SHORTCUT_DEBUG,
	XVT_KEYBOARD_SHORTCUT_RENDERER,
	XVT_KEYBOARD_SHORTCUT_MOUSE,
} xvt_keyboard_shortcut;

/* Whether the grave key is reserved as the debug shortcut. */
void xvt_keyboard_mapping_set_policy(bool debug_available);
/* The key of this frame's first fresh press of that shortcut; -1 when there is none, the window lacks
 * focus, or the snapshot's key events overflowed. */
int xvt_keyboard_mapping_find_shortcut_press(const AeronInputSnapshot *input,
					     xvt_keyboard_shortcut shortcut);
/* Escape with any modifiers is SETTINGS; Tab alone is RENDERER; grave is DEBUG when the debug policy
 * allows it; M with Ctrl and Alt is MOUSE; anything else is NONE. */
xvt_keyboard_shortcut xvt_keyboard_mapping_shortcut(AeronKeyChord source);
/* true for a named key with modifiers from the four, where a modifier key takes no modifiers, and
 * which is not a shortcut. */
bool xvt_keyboard_mapping_source_valid(AeronKeyChord source);
/* Writes a label such as "Ctrl+Alt+Shift+Super+Key"; Command replaces Super on Apple systems. */
void xvt_keyboard_mapping_format_source(char *text, size_t capacity,
					AeronKeyChord source);
/* The index of the binding with exactly that chord, or SIZE_MAX. */
size_t xvt_keyboard_mapping_find(const struct xvt_keyboard_bindings *profile,
				 AeronKeyChord source);
/* true when both hold the same bindings in the same order; sort both first. */
bool xvt_keyboard_mapping_equal(const struct xvt_keyboard_bindings *a,
				const struct xvt_keyboard_bindings *b);
/* Sorts by action, then key, then modifiers. */
void xvt_keyboard_mapping_sort(struct xvt_keyboard_bindings *profile);
/* Removes the binding at index, keeping the order; an index out of range is ignored. */
void xvt_keyboard_mapping_remove(struct xvt_keyboard_bindings *profile,
				 size_t index);
/* Suspends, then makes profile the active mapping; for a chord bound twice, the later binding wins. The
 * mapping stays disabled until Enable. */
void xvt_keyboard_mapping_install(const struct xvt_keyboard_bindings *profile);
/* Forgets every pressed key, held button and queued key, and disables the mapping. */
void xvt_keyboard_mapping_suspend(void);
/* Does nothing when already in that state; otherwise suspends and sets it. On enabling with input,
 * keys already down are ignored until released. */
void xvt_keyboard_mapping_enable(bool enabled, const AeronInputSnapshot *input);
/* Drops the button presses ReadButtons reported since the last frame. When the snapshot's key events
 * overflowed, an enabled mapping restarts as Enable would. */
void xvt_keyboard_mapping_begin_frame(const AeronInputSnapshot *input);
/* Feeds one key event to an enabled mapping. A fresh press looks up its chord (without the key's own
 * modifier bit) and fires the action, unless the chord is a shortcut; each repeat fires it again, except
 * Pause and Escape; the release ends a held button. A repeat never starts a press. suppressed releases
 * the key's action and ignores the key until it is released. A full queue of 255 keys drops the key with
 * a warning. */
void xvt_keyboard_mapping_event(const AeronKeyEvent *event, bool suppressed);
/* The next queued flight key code, or 0 when the queue is empty. */
uint16_t xvt_keyboard_mapping_read_key(void);
/* Value 1 (bit 0) fire, value 2 (bit 1) target/roll modifier: set while held, or when pressed since the last
 * BeginFrame, so a tap between two reads is not lost. */
uint16_t xvt_keyboard_mapping_read_buttons(void);

#endif
