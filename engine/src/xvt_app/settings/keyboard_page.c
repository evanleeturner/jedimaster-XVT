#include "xvt_app/settings/keyboard_page.h"

#include <stdio.h>
#include <string.h>

void xvt_keyboard_settings_open(struct xvt_keyboard_settings *settings,
				const struct xvt_settings *config)
{
	memset(settings, 0, sizeof *settings);
	settings->draft = config->keyboard;
	settings->original = settings->draft;
	xvt_bindings_editor_init(&settings->editor);
}

void xvt_keyboard_settings_cancel_capture(
	struct xvt_keyboard_settings *settings, AeronUiContext *ui)
{
	AeronUi_CancelKeyboardCapture(ui);
	settings->editor.binding_modal_open = 0;
	settings->conflict_open = 0;
	settings->restore_open = 0;
}

static void xvt_keyboard_settings_refresh_draft_state(
	struct xvt_keyboard_settings *settings)
{
	xvt_keyboard_mapping_sort(&settings->draft);
	settings->dirty = settings->restore_defaults ||
			  !xvt_keyboard_mapping_equal(&settings->original,
						      &settings->draft);
	settings->restore_defaults = false;
	settings->editor.highlighted_binding_row = SIZE_MAX;
	settings->error[0] = 0;
}

static bool
xvt_keyboard_settings_capture_chord(struct xvt_keyboard_settings *settings,
				    AeronUiContext *ui, const char *label,
				    const char *display, AeronKeyChord *chord)
{
	if (AeronUi_KeyboardCapture(ui, label, display, chord) !=
	    AERON_UI_KEYBOARD_CAPTURE_CAPTURED) {
		return false;
	}
	if (xvt_keyboard_mapping_shortcut(*chord) !=
	    XVT_KEYBOARD_SHORTCUT_NONE) {
		snprintf(
			settings->error, sizeof settings->error,
			"This key combination is reserved for the application.");
		return false;
	}
	if (!xvt_keyboard_mapping_source_valid(*chord)) {
		snprintf(settings->error, sizeof settings->error,
			 "This key combination is not supported.");
		return false;
	}
	settings->error[0] = 0;
	return true;
}

static void add_binding(struct xvt_keyboard_settings *settings,
			AeronKeyChord source)
{
	size_t existing = xvt_keyboard_mapping_find(&settings->draft, source);
	if (existing != SIZE_MAX) {
		xvt_input_action action =
			settings->draft.bindings[existing].action;
		if (action == settings->editor.selected_action) {
			size_t position = 0;
			for (size_t i = 0; i < existing; ++i) {
				position +=
					settings->draft.bindings[i].action ==
					action;
			}
			settings->editor.highlighted_binding_row = position;
		} else {
			settings->pending_chord = source;
			settings->conflicting_action = action;
			settings->conflict_open = 1;
		}
		return;
	}
	if (settings->draft.count == XVT_KEYBOARD_BINDING_CAP) {
		snprintf(settings->error, sizeof settings->error,
			 "The keyboard has reached its binding capacity.");
		return;
	}
	settings->draft.bindings[settings->draft.count++] =
		(struct xvt_keyboard_binding){source,
					      settings->editor.selected_action};
	xvt_keyboard_settings_refresh_draft_state(settings);
}

static void describe_action(const struct xvt_keyboard_bindings *bindings,
			    xvt_input_action action, char *text,
			    size_t capacity)
{
	text[0] = 0;
	for (size_t i = 0; i < bindings->count; ++i) {
		if (bindings->bindings[i].action != action) {
			continue;
		}
		char label[128];
		xvt_keyboard_mapping_format_source(
			label, sizeof label, bindings->bindings[i].source);
		size_t length = strlen(text);
		int written = snprintf(text + length, capacity - length, "%s%s",
				       length ? "   " : "", label);
		if (written < 0 || (size_t)written >= capacity - length) {
			break;
		}
	}
	if (!text[0]) {
		snprintf(text, capacity, "Not Bound");
	}
}

void xvt_keyboard_settings_draw(struct xvt_keyboard_settings *settings,
				AeronUiContext *ui)
{
	AeronUi_Help(
		ui,
		"Flight and map controls share the original commands. Changing a binding changes both uses shown in its name. Chat typing and navigation use the standard keys. Escape opens settings; Tab switches renderers.");
	xvt_bindings_editor_category_selector(&settings->editor, ui);
	AeronKeyChord source;
	if (xvt_keyboard_settings_capture_chord(settings, ui, "Find Binding...",
						"Press to identify", &source)) {
		size_t index =
			xvt_keyboard_mapping_find(&settings->draft, source);
		if (index == SIZE_MAX) {
			snprintf(settings->error, sizeof settings->error,
				 "This key combination is not bound.");
		} else {
			xvt_bindings_editor_select(
				&settings->editor,
				settings->draft.bindings[index].action, false);
		}
	}
	AeronUi_Spacer(ui, 8.0f);
	AeronUiListItem items[XVT_INPUT_ACTION_COUNT - 1];
	char details[XVT_INPUT_ACTION_COUNT - 1][256];
	size_t count = 0;
	for (int action = XVT_INPUT_ACTION_NONE + 1;
	     action < XVT_INPUT_ACTION_COUNT; ++action) {
		if (!xvt_input_actions_keyboard_bindable(
			    (xvt_input_action)action) ||
		    (int)xvt_input_actions_category((xvt_input_action)action) !=
			    settings->editor.category) {
			continue;
		}
		describe_action(&settings->draft, (xvt_input_action)action,
				details[count], sizeof details[count]);
		items[count] = (AeronUiListItem){
			.id = (uint64_t)action,
			.label = xvt_input_actions_display_name(
				(xvt_input_action)action),
			.detail = details[count]};
		++count;
	}
	float trailing_height = 143.0f;
	if (settings->error[0]) {
		trailing_height +=
			AeronUi_MeasureHelpHeight(ui, settings->error, 0.0f) +
			60.0f;
	}
	xvt_bindings_editor_actions(&settings->editor, ui, items, count,
				    trailing_height);
	if (settings->error[0]) {
		AeronUi_Error(ui, settings->error);
		if (AeronUi_Button(ui, "Dismiss Error")) {
			settings->error[0] = 0;
		}
	}
	if (AeronUi_Button(ui, "Restore Defaults")) {
		AeronUi_CancelKeyboardCapture(ui);
		settings->restore_open = 1;
	}
}

static void
xvt_keyboard_settings_detail_modal(struct xvt_keyboard_settings *settings,
				   AeronUiContext *ui)
{
	if (!xvt_bindings_editor_begin_detail(&settings->editor, ui)) {
		return;
	}
	AeronUiListItem items[XVT_KEYBOARD_BINDING_CAP];
	char labels[XVT_KEYBOARD_BINDING_CAP][128];
	size_t count = 0;
	for (size_t i = 0; i < settings->draft.count; ++i) {
		if (settings->draft.bindings[i].action !=
		    settings->editor.selected_action) {
			continue;
		}
		xvt_keyboard_mapping_format_source(
			labels[count], sizeof labels[count],
			settings->draft.bindings[i].source);
		items[count] =
			(AeronUiListItem){.id = i, .label = labels[count]};
		++count;
	}
	xvt_bindings_editor_binding_list(
		&settings->editor, ui, items, count,
		"This action has no keyboard binding.");
	if (settings->editor.highlighted_binding_row < count &&
	    xvt_bindings_editor_remove_button(ui)) {
		xvt_keyboard_mapping_remove(
			&settings->draft,
			(size_t)items[settings->editor.highlighted_binding_row]
				.id);
		xvt_keyboard_settings_refresh_draft_state(settings);
	}
	AeronUi_Separator(ui);
	AeronKeyChord source;
	if (xvt_keyboard_settings_capture_chord(settings, ui, "Add Binding...",
						"Press to add", &source)) {
		add_binding(settings, source);
	}
	if (settings->error[0]) {
		AeronUi_Error(ui, settings->error);
	}
	xvt_bindings_editor_end_detail(&settings->editor, ui);
}

static void
xvt_keyboard_settings_restore_modal(struct xvt_keyboard_settings *settings,
				    AeronUiContext *ui)
{
	if (!AeronUi_BeginModal(ui, "RESTORE KEYBOARD DEFAULTS",
				&settings->restore_open, NULL)) {
		return;
	}
	AeronUi_Help(
		ui, "Replace all keyboard bindings with the shipped defaults?");
	AeronUi_BeginColumns(ui, 2, NULL);
	if (AeronUi_Button(ui, "Restore")) {
		settings->draft = xvt_config_default_settings()->keyboard;
		settings->restore_defaults = true;
		settings->dirty = true;
		settings->restore_open = 0;
		settings->error[0] = 0;
		xvt_bindings_editor_init(&settings->editor);
	}
	AeronUi_NextColumn(ui);
	if (AeronUi_Button(ui, "Cancel")) {
		settings->restore_open = 0;
	}
	AeronUi_EndColumns(ui);
	AeronUi_EndModal(ui);
}

void xvt_keyboard_settings_draw_modals(struct xvt_keyboard_settings *settings,
				       AeronUiContext *ui)
{
	if (settings->conflict_open) {
		char label[128];
		xvt_keyboard_mapping_format_source(label, sizeof label,
						   settings->pending_chord);
		if (xvt_bindings_editor_confirm_replace(
			    ui, &settings->conflict_open, label,
			    settings->conflicting_action,
			    settings->editor.selected_action)) {
			size_t existing = xvt_keyboard_mapping_find(
				&settings->draft, settings->pending_chord);
			if (existing != SIZE_MAX) {
				settings->draft.bindings[existing].action =
					settings->editor.selected_action;
				xvt_keyboard_settings_refresh_draft_state(
					settings);
			}
		}
	} else if (settings->restore_open) {
		xvt_keyboard_settings_restore_modal(settings, ui);
	} else {
		xvt_keyboard_settings_detail_modal(settings, ui);
	}
}

bool xvt_keyboard_settings_commit(struct xvt_keyboard_settings *settings,
				  char *error, size_t capacity)
{
	if (!settings->dirty) {
		return true;
	}
	bool ok = (settings->restore_defaults ||
		   xvt_keyboard_mapping_equal(
			   &settings->draft,
			   &xvt_config_default_settings()->keyboard))
			  ? xvt_config_restore_keyboard(error, capacity)
			  : xvt_config_set_keyboard(&settings->draft, error,
						    capacity);
	if (!ok) {
		return false;
	}
	xvt_keyboard_mapping_install(&xvt_config_settings()->keyboard);
	settings->original = xvt_config_settings()->keyboard;
	settings->draft = settings->original;
	settings->restore_defaults = false;
	settings->dirty = false;
	return true;
}
