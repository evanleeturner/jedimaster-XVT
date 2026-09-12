#ifndef XVT_BINDINGS_EDITOR_H
#define XVT_BINDINGS_EDITOR_H

#include "aeron/scene/ui.h"
#include "xvt_runtime/input/actions.h"

typedef struct XvtBindingsEditor {
	int category;
	size_t action_selected;
	size_t binding_selected;
	XvtInputAction selected_action;
	int binding_modal_open;
} XvtBindingsEditor;

void XvtBindingsEditor_Init(XvtBindingsEditor* editor);
void XvtBindingsEditor_Select(XvtBindingsEditor* editor, XvtInputAction action, bool open_modal);
void XvtBindingsEditor_Category(XvtBindingsEditor* editor, AeronUiContext* ui);
void XvtBindingsEditor_Actions(XvtBindingsEditor* editor, AeronUiContext* ui, const AeronUiListItem* items,
							   size_t count, float trailing_height);
bool XvtBindingsEditor_BeginDetail(XvtBindingsEditor* editor, AeronUiContext* ui);
void XvtBindingsEditor_List(XvtBindingsEditor* editor, AeronUiContext* ui, const AeronUiListItem* items,
							size_t count, const char* empty_text);
bool XvtBindingsEditor_Remove(AeronUiContext* ui);
void XvtBindingsEditor_EndDetail(XvtBindingsEditor* editor, AeronUiContext* ui);
/* Returns true only when Replace is chosen. The dialog owns its open flag. */
bool XvtBindingsEditor_Conflict(AeronUiContext* ui, int* open, const char* source, XvtInputAction previous,
								XvtInputAction replacement);
#endif
