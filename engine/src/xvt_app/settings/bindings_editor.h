#ifndef XVT_BINDINGS_EDITOR_H
#define XVT_BINDINGS_EDITOR_H

#include "aeron/scene/ui.h"
#include "xvt_runtime/input/actions.h"

/* The list-and-detail editor shared by the keyboard and controller pages: an action category selector,
 * the action list of that category (activating an action opens its detail modal), the modal's binding
 * list with Remove and Done, and the conflict dialog. The editor holds selection state only; the pages
 * own the bindings. */
typedef struct XvtBindingsEditor {
	int category;
	size_t action_selected;
	size_t binding_selected;
	XvtInputAction selected_action;
	int binding_modal_open;
} XvtBindingsEditor;

/* Clears the editor: the first category, no action selected, no list item highlighted, modal closed. */
void XvtBindingsEditor_Init(XvtBindingsEditor* editor);
/* Selects action, switches to its category, drops the list highlights and, when asked, opens the detail
 * modal. */
void XvtBindingsEditor_Select(XvtBindingsEditor* editor, XvtInputAction action, bool open_modal);
/* Draws the Action Category selector (Weapons, Targets, Throttle, View, Info, System, Comms); a change
 * drops the action selection. */
void XvtBindingsEditor_Category(XvtBindingsEditor* editor, AeronUiContext* ui);
/* Draws the Actions list of items, at least 180 high and otherwise filling the height above
 * trailing_height. When no item is highlighted but an action is selected, highlights that action's item.
 * Activating an item selects its action and opens the detail modal. */
void XvtBindingsEditor_Actions(XvtBindingsEditor* editor, AeronUiContext* ui, const AeronUiListItem* items,
							   size_t count, float trailing_height);
/* Begins the detail modal, titled with the selected action's name: true while the modal is open with an
 * action selected and is drawn this frame; cancel closes it. */
bool XvtBindingsEditor_BeginDetail(XvtBindingsEditor* editor, AeronUiContext* ui);
/* Draws the Current Bindings header and the Bindings list of items, 180 high, with the editor's binding
 * highlight; with no items, drops the highlight and shows empty_text. */
void XvtBindingsEditor_List(XvtBindingsEditor* editor, AeronUiContext* ui, const AeronUiListItem* items,
							size_t count, const char* empty_text);
/* Draws the Remove Binding button; true when pressed. Removes nothing itself. */
bool XvtBindingsEditor_Remove(AeronUiContext* ui);
/* Draws Done, which closes the modal, and ends the modal. */
void XvtBindingsEditor_EndDetail(XvtBindingsEditor* editor, AeronUiContext* ui);
/* Returns true only when Replace is chosen. The dialog owns its open flag. */
/* Draws the CONTROL ALREADY BOUND modal while *open: "<source> is assigned to <previous>. Replace it with
 * <replacement>?", with Replace and Cancel, either of which closes it. */
bool XvtBindingsEditor_Conflict(AeronUiContext* ui, int* open, const char* source, XvtInputAction previous,
								XvtInputAction replacement);
#endif
