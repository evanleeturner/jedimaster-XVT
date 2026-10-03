#ifndef XVT_KEYBOARD_SETTINGS_H
#define XVT_KEYBOARD_SETTINGS_H
#include "xvt_app/settings/bindings_editor.h"
#include "xvt_runtime/config/config.h"

/* The Keyboard page: edits a draft of the keyboard bindings through the shared editor, with Find
 * Binding, Add Binding and Remove, the conflict dialog and Restore Defaults. A chord
 * reserved as an application shortcut, or otherwise unsupported, is refused. The draft is stored and
 * installed when the menu closes. */
/* pending is the chord awaiting the conflict dialog; restore_defaults marks a draft that is the shipped
 * set, so Commit removes the user's bindings instead of storing it. */
struct XvtKeyboardSettings {
	struct XvtKeyboardBindings original;
	struct XvtKeyboardBindings draft;
	struct XvtBindingsEditor editor;
	AeronKeyChord pending_chord;
	XvtInputAction conflicting_action;
	int conflict_open;
	int restore_open;
	bool dirty;
	bool restore_defaults;
	char error[512];
};

/* Zeroes settings, takes config's keyboard bindings as the original and the draft, and clears the
 * editor. */
void XvtKeyboardSettings_Open(struct XvtKeyboardSettings *settings,
			      const struct XvtSettings *config);
/* Draws the help text, the category selector, Find Binding (a captured chord selects its action, or
 * reports that it is not bound), the bindable actions of the category with their chords (or "Not
 * Bound"), the error with Dismiss Error, and Restore Defaults, which cancels any
 * capture and opens the restore modal. A captured chord that is an application shortcut, or unsupported, is
 * refused with a message. */
void XvtKeyboardSettings_Draw(struct XvtKeyboardSettings *settings,
			      AeronUiContext *ui);
/* Draws at most one modal: the conflict dialog while open (Replace rebinds the pending chord to the
 * selected action); else the restore modal while open (Restore takes the shipped
 * bindings, marks the draft dirty and clears the editor); else the detail modal of the selected action: its
 * chords, Remove, and Add Binding, where a captured chord bound to another action opens the conflict dialog,
 * one bound to this action is highlighted, and a draft of 256 bindings is refused with a message. An add,
 * remove or replace re-sorts the draft and recomputes whether it differs from the original. */
void XvtKeyboardSettings_DrawModals(struct XvtKeyboardSettings *settings,
				    AeronUiContext *ui);
/* Cancels the UI's keyboard capture and closes the detail, conflict and restore modals. */
void XvtKeyboardSettings_CancelCapture(struct XvtKeyboardSettings *settings,
				       AeronUiContext *ui);
/* Nothing when the draft is unchanged. Removes the user's keyboard bindings when the draft is the
 * shipped set (by restore or by equality), else stores the draft; false with error on failure. On
 * success installs the resulting bindings as the active mapping and makes them the original and the
 * draft. */
bool XvtKeyboardSettings_Commit(struct XvtKeyboardSettings *settings,
				char *error, size_t capacity);
#endif
