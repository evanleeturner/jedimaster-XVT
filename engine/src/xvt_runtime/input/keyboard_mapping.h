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

typedef struct XvtKeyboardBinding {
	AeronKeyChord source;
	XvtInputAction action;
} XvtKeyboardBinding;

typedef struct XvtKeyboardBindings {
	XvtKeyboardBinding bindings[XVT_KEYBOARD_BINDING_CAP];
	size_t count;
} XvtKeyboardBindings;

typedef enum XvtKeyboardShortcut {
	XVT_KEYBOARD_SHORTCUT_NONE,
	XVT_KEYBOARD_SHORTCUT_SETTINGS,
	XVT_KEYBOARD_SHORTCUT_DEBUG,
	XVT_KEYBOARD_SHORTCUT_RENDERER,
	XVT_KEYBOARD_SHORTCUT_MOUSE,
} XvtKeyboardShortcut;

/* Whether the grave key is reserved as the debug shortcut. */
void XvtKeyboardMapping_SetPolicy(bool debug_available);
/* The key of this frame's first fresh press of that shortcut; -1 when there is none, the window lacks
 * focus, or the snapshot's key events overflowed. */
int XvtKeyboardMapping_FindShortcutPress(const AeronInputSnapshot *input,
					 XvtKeyboardShortcut shortcut);
/* Escape with any modifiers is SETTINGS; Tab alone is RENDERER; grave is DEBUG when the debug policy
 * allows it; M with Ctrl and Alt is MOUSE; anything else is NONE. */
XvtKeyboardShortcut XvtKeyboardMapping_Shortcut(AeronKeyChord source);
/* true for a named key with modifiers from the four, where a modifier key takes no modifiers, and
 * which is not a shortcut. */
bool XvtKeyboardMapping_SourceValid(AeronKeyChord source);
/* Writes a label such as "Ctrl+Alt+Shift+Super+Key"; Command replaces Super on Apple systems. */
void XvtKeyboardMapping_FormatSource(char *text, size_t capacity,
				     AeronKeyChord source);
/* The index of the binding with exactly that chord, or SIZE_MAX. */
size_t XvtKeyboardMapping_Find(const XvtKeyboardBindings *profile,
			       AeronKeyChord source);
/* true when both hold the same bindings in the same order; sort both first. */
bool XvtKeyboardMapping_Equal(const XvtKeyboardBindings *a,
			      const XvtKeyboardBindings *b);
/* Sorts by action, then key, then modifiers. */
void XvtKeyboardMapping_Sort(XvtKeyboardBindings *profile);
/* Removes the binding at index, keeping the order; an index out of range is ignored. */
void XvtKeyboardMapping_Remove(XvtKeyboardBindings *profile, size_t index);
/* Suspends, then makes profile the active mapping; for a chord bound twice, the later binding wins. The
 * mapping stays disabled until Enable. */
void XvtKeyboardMapping_Install(const XvtKeyboardBindings *profile);
/* Forgets every pressed key, held button and queued key, and disables the mapping. */
void XvtKeyboardMapping_Suspend(void);
/* Does nothing when already in that state; otherwise suspends and sets it. On enabling with input,
 * keys already down are ignored until released. */
void XvtKeyboardMapping_Enable(bool enabled, const AeronInputSnapshot *input);
/* Drops the button presses ReadButtons reported since the last frame. When the snapshot's key events
 * overflowed, an enabled mapping restarts as Enable would. */
void XvtKeyboardMapping_BeginFrame(const AeronInputSnapshot *input);
/* Feeds one key event to an enabled mapping. A fresh press looks up its chord (without the key's own
 * modifier bit) and fires the action, unless the chord is a shortcut; each repeat fires it again, except
 * Pause and Escape; the release ends a held button. A repeat never starts a press. suppressed releases
 * the key's action and ignores the key until it is released. A full queue of 255 keys drops the key with
 * a warning. */
void XvtKeyboardMapping_Event(const AeronKeyEvent *event, bool suppressed);
/* The next queued flight key code, or 0 when the queue is empty. */
uint16_t XvtKeyboardMapping_ReadKey(void);
/* Value 1 (bit 0) fire, value 2 (bit 1) target/roll modifier: set while held, or when pressed since the last
 * BeginFrame, so a tap between two reads is not lost. */
uint16_t XvtKeyboardMapping_ReadButtons(void);

#endif
