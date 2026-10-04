#include "xvt_app/settings/bindings_editor.h"

#include <stdio.h>

void xvt_bindings_editor_init(struct xvt_bindings_editor *editor)
{
	*editor = (struct xvt_bindings_editor){
		.highlighted_action_row = SIZE_MAX,
		.highlighted_binding_row = SIZE_MAX};
}

void xvt_bindings_editor_select(struct xvt_bindings_editor *editor,
				xvt_input_action action, bool open_modal)
{
	editor->selected_action = action;
	editor->category = xvt_input_actions_category(action);
	editor->highlighted_action_row = SIZE_MAX;
	editor->highlighted_binding_row = SIZE_MAX;
	if (open_modal) {
		editor->binding_modal_open = 1;
	}
}

void xvt_bindings_editor_category_selector(struct xvt_bindings_editor *editor,
					   AeronUiContext *ui)
{
	static const char *const categories[] = {
		"Weapons", "Targets", "Throttle", "View",
		"Info",	   "System",  "Comms"};
	if (AeronUi_SegmentedSelector(ui, "Action Category", &editor->category,
				      categories,
				      XVT_INPUT_ACTION_CATEGORY_COUNT)) {
		editor->highlighted_action_row = SIZE_MAX;
		editor->selected_action = XVT_INPUT_ACTION_NONE;
	}
}

void xvt_bindings_editor_actions(struct xvt_bindings_editor *editor,
				 AeronUiContext *ui,
				 const AeronUiListItem *items, size_t count,
				 float trailing_height)
{
	if (editor->highlighted_action_row == SIZE_MAX &&
	    editor->selected_action != XVT_INPUT_ACTION_NONE) {
		for (size_t i = 0; i < count; ++i) {
			if (items[i].id == (uint64_t)editor->selected_action) {
				editor->highlighted_action_row = i;
				break;
			}
		}
	}
	float height = AeronUi_AvailableHeight(ui) - trailing_height;
	if (height < 180.0f) {
		height = 180.0f;
	}
	uint32_t result =
		AeronUi_ListBox(ui, "Actions", items, count,
				&editor->highlighted_action_row, height);
	if ((result & AERON_UI_LIST_ACTIVATED) &&
	    editor->highlighted_action_row < count) {
		xvt_bindings_editor_select(
			editor,
			(xvt_input_action)items[editor->highlighted_action_row]
				.id,
			true);
	}
}

bool xvt_bindings_editor_begin_detail(struct xvt_bindings_editor *editor,
				      AeronUiContext *ui)
{
	if (!editor->binding_modal_open ||
	    editor->selected_action == XVT_INPUT_ACTION_NONE) {
		return false;
	}
	return AeronUi_BeginModal(
		       ui,
		       xvt_input_actions_display_name(editor->selected_action),
		       &editor->binding_modal_open,
		       &(AeronUiWindowDesc){.width_ref = 720.0f,
					    .centered = 1}) != 0;
}

void xvt_bindings_editor_binding_list(struct xvt_bindings_editor *editor,
				      AeronUiContext *ui,
				      const AeronUiListItem *items,
				      size_t count, const char *empty_text)
{
	AeronUi_Header(ui, "Current Bindings");
	if (count) {
		AeronUi_ListBox(ui, "Bindings", items, count,
				&editor->highlighted_binding_row, 180.0f);
	} else {
		editor->highlighted_binding_row = SIZE_MAX;
		AeronUi_Help(ui, empty_text);
	}
}

bool xvt_bindings_editor_remove_button(AeronUiContext *ui)
{
	return AeronUi_Button(ui, "Remove Binding") != 0;
}

void xvt_bindings_editor_end_detail(struct xvt_bindings_editor *editor,
				    AeronUiContext *ui)
{
	if (AeronUi_Button(ui, "Done")) {
		editor->binding_modal_open = 0;
	}
	AeronUi_EndModal(ui);
}

bool xvt_bindings_editor_confirm_replace(AeronUiContext *ui, int *open,
					 const char *source,
					 xvt_input_action previous,
					 xvt_input_action replacement)
{
	if (!AeronUi_BeginModal(ui, "CONTROL ALREADY BOUND", open, NULL)) {
		return false;
	}
	char text[512];
	snprintf(text, sizeof text, "%s is assigned to %s. Replace it with %s?",
		 source, xvt_input_actions_display_name(previous),
		 xvt_input_actions_display_name(replacement));
	AeronUi_Error(ui, text);
	AeronUi_BeginColumns(ui, 2, NULL);
	bool replace = AeronUi_Button(ui, "Replace") != 0;
	if (replace) {
		*open = 0;
	}
	AeronUi_NextColumn(ui);
	if (AeronUi_Button(ui, "Cancel")) {
		*open = 0;
	}
	AeronUi_EndColumns(ui);
	AeronUi_EndModal(ui);
	return replace;
}
