#include "xvt_app/settings/keyboard_page.h"
#include <stdio.h>
#include <string.h>

void XvtKeyboardSettings_Open(XvtKeyboardSettings* settings, const XvtSettings* config) {
	memset(settings, 0, sizeof *settings);
	settings->original = settings->draft = config->keyboard;
	XvtBindingsEditor_Init(&settings->editor);
}

void XvtKeyboardSettings_CancelCapture(XvtKeyboardSettings* settings, AeronUiContext* ui) {
	AeronUi_CancelKeyboardCapture(ui);
	settings->editor.binding_modal_open = 0;
	settings->conflict_open = settings->restore_open = 0;
}

static void Changed(XvtKeyboardSettings* settings) {
	XvtKeyboardMapping_Sort(&settings->draft);
	settings->dirty =
		settings->restore_defaults || !XvtKeyboardMapping_Equal(&settings->original, &settings->draft);
	settings->restore_defaults = false;
	settings->editor.binding_selected = SIZE_MAX;
	settings->error[0] = 0;
}

static bool Captured(XvtKeyboardSettings* settings, AeronUiContext* ui, const char* label,
					 const char* display, AeronKeyChord* chord) {
	if (AeronUi_KeyboardCapture(ui, label, display, chord) != AERON_UI_KEYBOARD_CAPTURE_CAPTURED)
		return false;
	if (XvtKeyboardMapping_Shortcut(*chord) != XVT_KEYBOARD_SHORTCUT_NONE) {
		snprintf(settings->error, sizeof settings->error,
				 "This key combination is reserved for the application.");
		return false;
	}
	if (!XvtKeyboardMapping_SourceValid(*chord)) {
		snprintf(settings->error, sizeof settings->error, "This key combination is not supported.");
		return false;
	}
	settings->error[0] = 0;
	return true;
}

static void AddBinding(XvtKeyboardSettings* settings, AeronKeyChord source) {
	size_t existing = XvtKeyboardMapping_Find(&settings->draft, source);
	if (existing != SIZE_MAX) {
		XvtInputAction action = settings->draft.bindings[existing].action;
		if (action == settings->editor.selected_action) {
			size_t position = 0;
			for (size_t i = 0; i < existing; ++i)
				position += settings->draft.bindings[i].action == action;
			settings->editor.binding_selected = position;
		} else {
			settings->pending = source;
			settings->conflicting_action = action;
			settings->conflict_open = 1;
		}
		return;
	}
	if (settings->draft.count == XVT_KEYBOARD_BINDING_CAP) {
		snprintf(settings->error, sizeof settings->error, "The keyboard has reached its binding capacity.");
		return;
	}
	settings->draft.bindings[settings->draft.count++] =
		(XvtKeyboardBinding) { source, settings->editor.selected_action };
	Changed(settings);
}

static void DescribeAction(const XvtKeyboardBindings* profile, XvtInputAction action, char* text,
						   size_t capacity) {
	text[0] = 0;
	for (size_t i = 0; i < profile->count; ++i) {
		if (profile->bindings[i].action != action)
			continue;
		char label[128];
		XvtKeyboardMapping_FormatSource(label, sizeof label, profile->bindings[i].source);
		size_t length = strlen(text);
		int written = snprintf(text + length, capacity - length, "%s%s", length ? "   " : "", label);
		if (written < 0 || (size_t)written >= capacity - length)
			break;
	}
	if (!text[0])
		snprintf(text, capacity, "Not Bound");
}

void XvtKeyboardSettings_Draw(XvtKeyboardSettings* settings, AeronUiContext* ui) {
	AeronUi_Help(ui, "Flight and map controls share the original commands. Changing a binding changes both "
					 "uses shown in its name. Chat typing and navigation use the standard keys. Escape opens "
					 "settings; Tab switches renderers.");
	XvtBindingsEditor_Category(&settings->editor, ui);
	AeronKeyChord source;
	if (Captured(settings, ui, "Find Binding...", "Press to identify", &source)) {
		size_t index = XvtKeyboardMapping_Find(&settings->draft, source);
		if (index == SIZE_MAX)
			snprintf(settings->error, sizeof settings->error, "This key combination is not bound.");
		else
			XvtBindingsEditor_Select(&settings->editor, settings->draft.bindings[index].action, false);
	}
	AeronUi_Spacer(ui, 8.0f);
	AeronUiListItem items[XVT_INPUT_ACTION_COUNT - 1];
	char details[XVT_INPUT_ACTION_COUNT - 1][256];
	size_t count = 0;
	for (int action = XVT_INPUT_ACTION_NONE + 1; action < XVT_INPUT_ACTION_COUNT; ++action) {
		if (!XvtInputActions_KeyboardBindable((XvtInputAction)action) ||
			(int)XvtInputActions_Category((XvtInputAction)action) != settings->editor.category)
			continue;
		DescribeAction(&settings->draft, (XvtInputAction)action, details[count], sizeof details[count]);
		items[count] = (AeronUiListItem) { .id = (uint64_t)action,
										   .label = XvtInputActions_DisplayName((XvtInputAction)action),
										   .detail = details[count] };
		++count;
	}
	float trailing_height = 143.0f;
	if (settings->error[0])
		trailing_height += AeronUi_MeasureHelpHeight(ui, settings->error, 0.0f) + 60.0f;
	XvtBindingsEditor_Actions(&settings->editor, ui, items, count, trailing_height);
	if (settings->error[0]) {
		AeronUi_Error(ui, settings->error);
		if (AeronUi_Button(ui, "Dismiss Error"))
			settings->error[0] = 0;
	}
	if (AeronUi_Button(ui, "Restore Defaults")) {
		AeronUi_CancelKeyboardCapture(ui);
		settings->restore_open = 1;
	}
}

static void Detail(XvtKeyboardSettings* settings, AeronUiContext* ui) {
	if (!XvtBindingsEditor_BeginDetail(&settings->editor, ui))
		return;
	AeronUiListItem items[XVT_KEYBOARD_BINDING_CAP];
	char labels[XVT_KEYBOARD_BINDING_CAP][128];
	size_t count = 0;
	for (size_t i = 0; i < settings->draft.count; ++i) {
		if (settings->draft.bindings[i].action != settings->editor.selected_action)
			continue;
		XvtKeyboardMapping_FormatSource(labels[count], sizeof labels[count],
										settings->draft.bindings[i].source);
		items[count] = (AeronUiListItem) { .id = i, .label = labels[count] };
		++count;
	}
	XvtBindingsEditor_List(&settings->editor, ui, items, count, "This action has no keyboard binding.");
	if (settings->editor.binding_selected < count && XvtBindingsEditor_Remove(ui)) {
		XvtKeyboardMapping_Remove(&settings->draft, (size_t)items[settings->editor.binding_selected].id);
		Changed(settings);
	}
	AeronUi_Separator(ui);
	AeronKeyChord source;
	if (Captured(settings, ui, "Add Binding...", "Press to add", &source))
		AddBinding(settings, source);
	if (settings->error[0])
		AeronUi_Error(ui, settings->error);
	XvtBindingsEditor_EndDetail(&settings->editor, ui);
}

static void Restore(XvtKeyboardSettings* settings, AeronUiContext* ui) {
	if (!AeronUi_BeginModal(ui, "RESTORE KEYBOARD DEFAULTS", &settings->restore_open, NULL))
		return;
	AeronUi_Help(ui, "Replace all keyboard bindings with the shipped defaults?");
	AeronUi_BeginColumns(ui, 2, NULL);
	if (AeronUi_Button(ui, "Restore")) {
		settings->draft = XvtConfig_DefaultSettings()->keyboard;
		settings->restore_defaults = settings->dirty = true;
		settings->restore_open = 0;
		settings->error[0] = 0;
		XvtBindingsEditor_Init(&settings->editor);
	}
	AeronUi_NextColumn(ui);
	if (AeronUi_Button(ui, "Cancel"))
		settings->restore_open = 0;
	AeronUi_EndColumns(ui);
	AeronUi_EndModal(ui);
}

void XvtKeyboardSettings_DrawModals(XvtKeyboardSettings* settings, AeronUiContext* ui) {
	if (settings->conflict_open) {
		char label[128];
		XvtKeyboardMapping_FormatSource(label, sizeof label, settings->pending);
		if (XvtBindingsEditor_Conflict(ui, &settings->conflict_open, label, settings->conflicting_action,
									   settings->editor.selected_action)) {
			size_t existing = XvtKeyboardMapping_Find(&settings->draft, settings->pending);
			if (existing != SIZE_MAX) {
				settings->draft.bindings[existing].action = settings->editor.selected_action;
				Changed(settings);
			}
		}
	} else if (settings->restore_open) {
		Restore(settings, ui);
	} else {
		Detail(settings, ui);
	}
}

bool XvtKeyboardSettings_Commit(XvtKeyboardSettings* settings, char* error, size_t capacity) {
	if (!settings->dirty)
		return true;
	bool ok = (settings->restore_defaults ||
			   XvtKeyboardMapping_Equal(&settings->draft, &XvtConfig_DefaultSettings()->keyboard))
				  ? XvtConfig_RestoreKeyboard(error, capacity)
				  : XvtConfig_SetKeyboard(&settings->draft, error, capacity);
	if (!ok)
		return false;
	XvtKeyboardMapping_Install(&XvtConfig_Settings()->keyboard);
	settings->draft = settings->original = XvtConfig_Settings()->keyboard;
	settings->restore_defaults = settings->dirty = false;
	return true;
}
