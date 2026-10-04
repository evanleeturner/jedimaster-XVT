#include "xvt_app/settings/controller_page.h"

#include <stdio.h>
#include <string.h>

#include "aeron/aeron.h"
#include "xvt_runtime/input/controller_mapping.h"

enum {
	CONTROLLER_PAGE_AXES = 0,
	CONTROLLER_PAGE_BINDINGS,
};

static const char *const k_axis_names[XVT_INPUT_AXIS_COUNT] = {
	"Yaw", "Pitch", "Roll", "Throttle"};

static struct xvt_controller_profile *
xvt_controller_page_active_profile(struct xvt_controller_settings *settings,
				   const AeronControllerSnapshot *controller)
{
	int i = xvt_controller_options_find_model(&settings->draft,
						  controller->guid);
	return i >= 0 ? &settings->draft.models[i].profile
		      : &settings->unconfigured;
}

static const struct xvt_controller_profile *
xvt_controller_page_active_profile_const(
	const struct xvt_controller_settings *settings,
	const AeronControllerSnapshot *controller)
{
	int i = xvt_controller_options_find_model(&settings->draft,
						  controller->guid);
	return i >= 0 ? &settings->draft.models[i].profile
		      : &settings->unconfigured;
}

static const AeronControllerSnapshot *xvt_controller_page_selected_controller(
	struct xvt_controller_settings *settings,
	const AeronInputSnapshot *input)
{
	if (!input) {
		return NULL;
	}
	for (int i = 0; i < AERON_CONTROLLER_MAX; ++i) {
		if (input->controllers[i].connected &&
		    input->controllers[i].instance_id ==
			    settings->selected_instance) {
			return &input->controllers[i];
		}
	}
	return NULL;
}

static void
xvt_controller_settings_apply_draft(struct xvt_controller_settings *settings)
{
	if (settings->selected_guid[0] &&
	    xvt_controller_options_find_model(&settings->draft,
					      settings->selected_guid) < 0) {
		const AeronControllerSnapshot *device =
			xvt_controller_page_selected_controller(
				settings, Aeron_InputSnapshot());
		if (!device ||
		    !xvt_controller_options_ensure_model(
			    &settings->draft, device, settings->error,
			    sizeof settings->error)) {
			return;
		}
		settings->draft.models[settings->draft.count - 1].profile =
			settings->unconfigured;
	}
	settings->dirty = !xvt_controller_options_equals(&settings->draft,
							 &settings->original);
	if (!xvt_controller_options_validate(&settings->draft, settings->error,
					     sizeof settings->error)) {
		return;
	}
	xvt_controller_mapping_set_options(&settings->draft);
	settings->error[0] = 0;
}

static bool xvt_controller_settings_digital_source_equal(
	const AeronControllerDigitalSource *left,
	const AeronControllerDigitalSource *right)
{
	return left->kind == right->kind && left->index == right->index &&
	       (left->kind != AERON_CONTROLLER_DIGITAL_HAT ||
		left->hat_direction == right->hat_direction);
}

static const char *xvt_controller_page_gamepad_axis_display_name(int source)
{
	static const char *const names[AERON_GAMEPAD_AXIS_COUNT] = {
		"Left X",  "Left Y",	   "Right X",
		"Right Y", "Left Trigger", "Right Trigger"};
	return source >= 0 && source < AERON_GAMEPAD_AXIS_COUNT ? names[source]
								: NULL;
}

static const char *xvt_controller_page_gamepad_button_display_name(int source)
{
	static const char *const names[AERON_GAMEPAD_BUTTON_COUNT] = {
		"South",	  "East",	    "West",
		"North",	  "Back",	    "Guide",
		"Start",	  "Left Stick",	    "Right Stick",
		"Left Shoulder",  "Right Shoulder", "D-pad Up",
		"D-pad Down",	  "D-pad Left",	    "D-pad Right",
		"Misc 1",	  "Right Paddle 1", "Left Paddle 1",
		"Right Paddle 2", "Left Paddle 2",  "Touchpad",
		"Misc 2",	  "Misc 3",	    "Misc 4",
		"Misc 5",	  "Misc 6"};
	return source >= 0 && source < AERON_GAMEPAD_BUTTON_COUNT
		       ? names[source]
		       : NULL;
}

static bool xvt_controller_settings_source_available(
	const AeronControllerSnapshot *controller,
	const AeronControllerDigitalSource *source)
{
	if (!controller) {
		return false;
	}
	if (source->kind == AERON_CONTROLLER_DIGITAL_BUTTON) {
		return controller->kind == AERON_CONTROLLER_KIND_GAMEPAD
			       ? source->index < AERON_GAMEPAD_BUTTON_COUNT &&
					 (controller
						  ->gamepad_available_buttons &
					  (1u << source->index))
			       : source->index < controller->button_count;
	}
	if (source->kind == AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE ||
	    source->kind == AERON_CONTROLLER_DIGITAL_AXIS_NEGATIVE) {
		return controller->kind == AERON_CONTROLLER_KIND_GAMEPAD
			       ? source->index < AERON_GAMEPAD_AXIS_COUNT &&
					 (controller->gamepad_available_axes &
					  (1u << source->index))
			       : source->index < controller->axis_count;
	}
	return source->kind == AERON_CONTROLLER_DIGITAL_HAT &&
	       controller->kind == AERON_CONTROLLER_KIND_JOYSTICK &&
	       source->index < controller->hat_count;
}

static void xvt_controller_settings_format_axis_source(
	char *buffer, size_t capacity, int source,
	const AeronControllerSnapshot *controller)
{
	if (source < 0) {
		snprintf(buffer, capacity, "Not Bound");
		return;
	}
	const char *name =
		controller->kind == AERON_CONTROLLER_KIND_GAMEPAD
			? xvt_controller_page_gamepad_axis_display_name(source)
			: NULL;
	const bool available =
		controller->kind == AERON_CONTROLLER_KIND_GAMEPAD
			? source < AERON_GAMEPAD_AXIS_COUNT &&
				  (controller->gamepad_available_axes &
				   (1u << source))
			: source < controller->axis_count;
	if (name) {
		snprintf(buffer, capacity, "%s%s", name,
			 available ? "" : " (Unavailable)");
	} else {
		snprintf(buffer, capacity, "Axis %d%s", source,
			 available ? "" : " (Unavailable)");
	}
}

static const char *xvt_controller_page_hat_direction_name(uint8_t direction)
{
	switch (direction) {
	case AERON_CONTROLLER_HAT_UP:
		return "Up";
	case AERON_CONTROLLER_HAT_RIGHT:
		return "Right";
	case AERON_CONTROLLER_HAT_DOWN:
		return "Down";
	default:
		return "Left";
	}
}

static void xvt_controller_settings_format_digital_source(
	char *buffer, size_t capacity,
	const AeronControllerDigitalSource *source,
	const AeronControllerSnapshot *controller)
{
	const char *name = NULL;
	if (source->kind == AERON_CONTROLLER_DIGITAL_BUTTON) {
		name = controller->kind == AERON_CONTROLLER_KIND_GAMEPAD
			       ? xvt_controller_page_gamepad_button_display_name(
					 source->index)
			       : NULL;
		if (name) {
			snprintf(buffer, capacity, "%s", name);
		} else {
			snprintf(buffer, capacity, "Button %u",
				 (unsigned)source->index);
		}
	} else if (source->kind == AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE ||
		   source->kind == AERON_CONTROLLER_DIGITAL_AXIS_NEGATIVE) {
		name = controller->kind == AERON_CONTROLLER_KIND_GAMEPAD
			       ? xvt_controller_page_gamepad_axis_display_name(
					 source->index)
			       : NULL;
		if (name) {
			snprintf(
				buffer, capacity, "%s %c", name,
				source->kind == AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE
					? '+'
					: '-');
		} else {
			snprintf(
				buffer, capacity, "Axis %u %c",
				(unsigned)source->index,
				source->kind == AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE
					? '+'
					: '-');
		}
	} else {
		snprintf(buffer, capacity, "Hat %u %s", (unsigned)source->index,
			 xvt_controller_page_hat_direction_name(
				 source->hat_direction));
	}
	if (!xvt_controller_settings_source_available(controller, source)) {
		const size_t used = strlen(buffer);
		if (used < capacity) {
			snprintf(buffer + used, capacity - used,
				 " (Unavailable)");
		}
	}
}

static void xvt_controller_settings_append_text(char *buffer, size_t capacity,
						const char *text)
{
	const size_t used = strlen(buffer);
	if (used < capacity) {
		snprintf(buffer + used, capacity - used, "%s%s",
			 used ? ", " : "", text);
	}
}

static void xvt_controller_settings_describe_action_bindings(
	char *buffer, size_t capacity,
	const struct xvt_controller_profile *profile, xvt_input_action action,
	const AeronControllerSnapshot *controller)
{
	buffer[0] = '\0';
	for (size_t index = 0; index < profile->binding_count; ++index) {
		if (profile->bindings[index].action != action) {
			continue;
		}
		char source[96];
		xvt_controller_settings_format_digital_source(
			source, sizeof source, &profile->bindings[index].source,
			controller);
		xvt_controller_settings_append_text(buffer, capacity, source);
	}
	if (!buffer[0]) {
		snprintf(buffer, capacity, "Not Bound");
	}
}

void xvt_controller_settings_open(struct xvt_controller_settings *settings,
				  const struct xvt_settings *config)
{
	if (!settings || !config) {
		return;
	}
	memset(settings, 0, sizeof *settings);
	settings->original = config->controller;
	settings->draft = settings->original;
	settings->editor.highlighted_action_row = SIZE_MAX;
	settings->editor.highlighted_binding_row = SIZE_MAX;
	settings->editor.selected_action = XVT_INPUT_ACTION_NONE;
	settings->pending_axis = XVT_INPUT_AXIS_YAW;
	xvt_controller_options_clear_profile(&settings->unconfigured,
					     AERON_CONTROLLER_KIND_JOYSTICK);
}

void xvt_controller_settings_cancel_capture(
	struct xvt_controller_settings *settings, AeronUiContext *ui)
{
	if (ui) {
		AeronUi_CancelControllerCapture(ui);
	}
	if (settings) {
		settings->axis_conflict_open = 0;
		settings->binding_conflict_open = 0;
		settings->editor.binding_modal_open = 0;
	}
}

static void xvt_controller_settings_reset_device_edit_state(
	struct xvt_controller_settings *settings, AeronUiContext *ui)
{
	AeronUi_CancelControllerCapture(ui);
	settings->editor.highlighted_action_row = SIZE_MAX;
	settings->editor.highlighted_binding_row = SIZE_MAX;
	settings->editor.selected_action = XVT_INPUT_ACTION_NONE;
	settings->editor.binding_modal_open = 0;
	settings->axis_conflict_open = 0;
	settings->binding_conflict_open = 0;
}

bool xvt_controller_settings_commit(struct xvt_controller_settings *settings,
				    char *error, size_t error_capacity)
{
	if (!settings || !xvt_config_settings()) {
		if (error && error_capacity) {
			snprintf(error, error_capacity,
				 "controller settings are unavailable");
		}
		return false;
	}
	if (!settings->dirty) {
		return true;
	}
	const bool result = xvt_config_set_controller(&settings->draft, error,
						      error_capacity);
	if (!result) {
		return false;
	}
	settings->draft = xvt_config_settings()->controller;
	settings->original = settings->draft;
	settings->dirty = false;
	return true;
}

static void xvt_controller_settings_device_selector(
	struct xvt_controller_settings *settings, AeronUiContext *ui,
	const AeronInputSnapshot *input)
{
	enum { CAP = AERON_CONTROLLER_MAX };

	char labels[CAP][192];
	const char *options[CAP];
	const char *guids[CAP];
	uint32_t ids[CAP];
	int count = 0;
	int selected = -1;
	for (int i = 0; i < AERON_CONTROLLER_MAX; ++i) {
		const AeronControllerSnapshot *d = &input->controllers[i];
		if (!d->connected) {
			continue;
		}
		bool duplicate_name = false;
		for (int other = 0; other < AERON_CONTROLLER_MAX; ++other) {
			const AeronControllerSnapshot *candidate =
				&input->controllers[other];
			if (other != i && candidate->connected &&
			    !strcmp(candidate->name, d->name)) {
				duplicate_name = true;
			}
		}
		if (duplicate_name) {
			snprintf(labels[count], sizeof labels[count], "%s #%d",
				 d->name, i + 1);
		} else {
			snprintf(labels[count], sizeof labels[count], "%s",
				 d->name);
		}
		options[count] = labels[count];
		guids[count] = d->guid;
		ids[count] = d->instance_id;
		if (settings->selected_instance == d->instance_id) {
			selected = count;
		}
		++count;
	}
	if (!count) {
		xvt_controller_settings_reset_device_edit_state(settings, ui);
		settings->selected_instance = 0;
		settings->selected_guid[0] = 0;
		AeronUi_Help(ui,
			     "Connect a controller to configure its controls.");
		return;
	}
	bool changed = selected < 0;
	if (selected < 0) {
		selected = 0;
	}
	AeronUi_Header(ui, "Device");
	changed |= AeronUi_Selector(ui, "##controller_device", &selected,
				    options, count) != 0;
	if (changed || settings->selected_instance != ids[selected]) {
		xvt_controller_settings_reset_device_edit_state(settings, ui);
		snprintf(settings->selected_guid,
			 sizeof settings->selected_guid, "%s", guids[selected]);
		settings->selected_instance = ids[selected];
		const AeronControllerSnapshot *selected_device =
			xvt_controller_page_selected_controller(settings,
								input);
		xvt_controller_options_clear_profile(
			&settings->unconfigured,
			selected_device ? selected_device->kind
					: AERON_CONTROLLER_KIND_JOYSTICK);
	}
}

void xvt_controller_settings_discover(
	struct xvt_controller_settings *settings, AeronUiContext *ui,
	const AeronInputSnapshot *input,
	const struct xvt_controller_profile *defaults)
{
	size_t count = settings->draft.count;
	bool ok = xvt_controller_options_add_new_gamepads(
		&settings->draft, defaults, input, settings->error,
		sizeof settings->error);
	if (settings->draft.count != count) {
		xvt_controller_settings_reset_device_edit_state(settings, ui);
		settings->dirty = true;
		xvt_controller_mapping_set_options(&settings->draft);
	}
	if (ok && settings->discover_failed) {
		settings->error[0] = 0;
	}
	settings->discover_failed = !ok;
}

static int xvt_controller_settings_profile_missing_count(
	const struct xvt_controller_profile *profile,
	const AeronControllerSnapshot *controller)
{
	int missing = 0;
	for (int axis = 0; axis < XVT_INPUT_AXIS_COUNT; ++axis) {
		const int source = profile->mapping.axes[axis].source;
		if (source < 0) {
			continue;
		}
		if (controller->kind == AERON_CONTROLLER_KIND_GAMEPAD) {
			if (source >= AERON_GAMEPAD_AXIS_COUNT ||
			    !(controller->gamepad_available_axes &
			      (1u << source))) {
				++missing;
			}
		} else if (source >= controller->axis_count) {
			++missing;
		}
	}
	for (size_t index = 0; index < profile->binding_count; ++index) {
		if (!xvt_controller_settings_source_available(
			    controller, &profile->bindings[index].source)) {
			++missing;
		}
	}
	return missing;
}

static void xvt_controller_settings_controller_warning(
	const struct xvt_controller_settings *settings, AeronUiContext *ui,
	const AeronControllerSnapshot *controller)
{
	if (!controller) {
		AeronUi_Help(ui,
			     "Select a connected device to assign controls.");
		return;
	}

	const int missing = xvt_controller_settings_profile_missing_count(
		xvt_controller_page_active_profile_const(settings, controller),
		controller);
	if (missing && controller->controls_truncated) {
		char text[256];
		snprintf(
			text, sizeof text,
			"%d configured controls are unavailable; this controller also exposes more controls than "
			"the game supports.",
			missing);
		AeronUi_Error(ui, text);
	} else if (missing) {
		char text[128];
		snprintf(text, sizeof text,
			 "%d configured controls are unavailable.", missing);
		AeronUi_Error(ui, text);
	} else if (controller->controls_truncated) {
		AeronUi_Error(
			ui,
			"This controller exposes more controls than the game supports.");
	}
}

static float xvt_controller_settings_controller_axis_value(
	const AeronControllerSnapshot *controller, int source)
{
	if (!controller || source < 0) {
		return 0.0f;
	}
	int value;
	if (controller->kind == AERON_CONTROLLER_KIND_GAMEPAD) {
		if (source >= AERON_GAMEPAD_AXIS_COUNT ||
		    !(controller->gamepad_available_axes & (1u << source))) {
			return 0.0f;
		}
		value = controller->gamepad_axes[source];
	} else {
		if (source >= controller->axis_count ||
		    source >= AERON_CONTROLLER_AXIS_MAX) {
			return 0.0f;
		}
		value = controller->raw_axes[source];
	}
	return value < 0 ? (float)value / 32768.0f : (float)value / 32767.0f;
}

static void
xvt_controller_settings_install_axis(struct xvt_controller_settings *settings,
				     const AeronControllerSnapshot *controller,
				     xvt_input_axis axis, int source)
{
	struct xvt_controller_options candidate = settings->draft;
	int model =
		xvt_controller_options_find_model(&candidate, controller->guid);
	if (model < 0) {
		if (!xvt_controller_options_ensure_model(
			    &candidate, controller, settings->error,
			    sizeof settings->error)) {
			return;
		}
		model = (int)candidate.count - 1;
		candidate.models[model].profile = settings->unconfigured;
	}
	struct xvt_controller_profile *p = &candidate.models[model].profile;
	for (size_t i = 0; i < candidate.count; ++i) {
		candidate.models[i].profile.mapping.axes[axis].source = -1;
	}
	for (int i = 0; i < XVT_INPUT_AXIS_COUNT; ++i) {
		if (p->mapping.axes[i].source == source) {
			p->mapping.axes[i].source = -1;
		}
	}
	p->mapping.axes[axis].source = (int8_t)source;
	if (!xvt_controller_options_validate(&candidate, settings->error,
					     sizeof settings->error)) {
		return;
	}
	settings->draft = candidate;
	xvt_controller_settings_apply_draft(settings);
}

static void xvt_controller_settings_assign_captured_axis(
	struct xvt_controller_settings *settings,
	const AeronControllerSnapshot *controller, xvt_input_axis axis,
	int source)
{
	settings->conflict_text[0] = 0;
	const struct xvt_controller_profile *p =
		xvt_controller_page_active_profile_const(settings, controller);
	for (size_t i = 0; i < settings->draft.count; ++i) {
		const struct xvt_controller_model *m =
			&settings->draft.models[i];
		if (strcmp(m->guid, controller->guid) &&
		    m->profile.mapping.axes[axis].source >= 0) {
			char text[192];
			snprintf(text, sizeof text, "%s on %s",
				 k_axis_names[axis], m->name);
			xvt_controller_settings_append_text(
				settings->conflict_text,
				sizeof settings->conflict_text, text);
		}
	}
	for (int i = 0; i < XVT_INPUT_AXIS_COUNT; ++i) {
		if (i != (int)axis && p->mapping.axes[i].source == source) {
			xvt_controller_settings_append_text(
				settings->conflict_text,
				sizeof settings->conflict_text,
				k_axis_names[i]);
		}
	}
	if (settings->conflict_text[0]) {
		settings->pending_axis = axis;
		settings->pending_axis_source = source;
		settings->axis_conflict_open = 1;
	} else {
		xvt_controller_settings_install_axis(settings, controller, axis,
						     source);
	}
}

static void xvt_controller_settings_axis_editor(
	struct xvt_controller_settings *settings, AeronUiContext *ui,
	const AeronControllerSnapshot *controller, xvt_input_axis axis)
{
	const AeronControllerKind kind =
		controller ? controller->kind : AERON_CONTROLLER_KIND_NONE;
	struct xvt_controller_profile *profile =
		xvt_controller_page_active_profile(settings, controller);

	struct xvt_input_axis_binding *binding = &profile->mapping.axes[axis];
	char source_label[128];
	xvt_controller_settings_format_axis_source(
		source_label, sizeof source_label, binding->source, controller);
	AeronUi_PushId(ui, axis);
	const AeronUiControllerCaptureDesc desc = {
		controller->connected ? controller->instance_id : 0,
		AERON_UI_CONTROLLER_CAPTURE_ANALOG_AXIS};
	AeronUiControllerInput captured;
	const AeronUiControllerCaptureResult capture =
		AeronUi_ControllerCapture(ui, "Source", source_label, &desc,
					  &captured);
	if (capture == AERON_UI_CONTROLLER_CAPTURE_CAPTURED &&
	    captured.controller_kind == kind &&
	    captured.instance_id == controller->instance_id) {
		xvt_controller_settings_assign_captured_axis(
			settings, controller, axis, captured.value.axis);
	}
	profile = xvt_controller_page_active_profile(settings, controller);
	binding = &profile->mapping.axes[axis];
	float live = xvt_controller_settings_controller_axis_value(
		controller, binding->source);
	if (xvt_controller_options_effective_axis_invert(kind, axis,
							 binding->invert)) {
		live = -live;
	}
	if (axis == XVT_INPUT_AXIS_THROTTLE) {
		if (Aeron_ControllerAxisAvailable(controller,
						  binding->source)) {
			const int16_t raw = Aeron_ControllerAxisValue(
				controller, binding->source);
			const uint16_t position =
				xvt_controller_mapping_throttle_position(
					raw, kind, binding->source,
					binding->invert);
			AeronUi_PercentageMeter(ui, "Position",
						(float)position / UINT16_MAX);
		} else {
			AeronUi_Help(
				ui,
				"Assign an available throttle axis to see its position.");
		}
	} else {
		AeronUi_ControllerAxisMeter(ui, "Input", live,
					    binding->deadzone);
	}
	int invert = binding->invert;
	if (AeronUi_Toggle(ui, "Invert", &invert)) {
		binding->invert = invert != 0;
		xvt_controller_settings_apply_draft(settings);
	}
	if (axis != XVT_INPUT_AXIS_THROTTLE) {
		float deadzone_percent = binding->deadzone * 100.0f;
		const float minimum = 0.0f;
		if (AeronUi_SliderFloat(ui, "Deadzone", &deadzone_percent,
					minimum, 100.0f, 1.0f, "%.1f%%")) {
			binding->deadzone = deadzone_percent / 100.0f;
			xvt_controller_settings_apply_draft(settings);
		}
	}
	if (AeronUi_ButtonEnabled(ui, "Clear Binding", binding->source >= 0)) {
		binding->source = -1;
		xvt_controller_settings_apply_draft(settings);
	}
	AeronUi_PopId(ui);
}

static void
xvt_controller_settings_axis_page(struct xvt_controller_settings *settings,
				  AeronUiContext *ui,
				  const AeronControllerSnapshot *controller)
{
	if (AeronUi_SegmentedSelector(ui, "Flight Axis",
				      &settings->selected_axis, k_axis_names,
				      XVT_INPUT_AXIS_COUNT)) {
		AeronUi_CancelControllerCapture(ui);
	}
	xvt_controller_settings_axis_editor(
		settings, ui, controller,
		(xvt_input_axis)settings->selected_axis);
}

static void xvt_controller_settings_axis_conflict_modal(
	struct xvt_controller_settings *settings, AeronUiContext *ui,
	const AeronControllerSnapshot *controller)
{
	if (!AeronUi_BeginModal(ui, "AXIS ALREADY ASSIGNED",
				&settings->axis_conflict_open, NULL)) {
		return;
	}
	char text[640];
	snprintf(text, sizeof text, "Replace these assignments with %s: %s?",
		 k_axis_names[settings->pending_axis], settings->conflict_text);
	AeronUi_Error(ui, text);
	AeronUi_BeginColumns(ui, 2, NULL);
	if (AeronUi_Button(ui, "Replace")) {
		xvt_controller_settings_install_axis(
			settings, controller, settings->pending_axis,
			settings->pending_axis_source);
		settings->axis_conflict_open = 0;
	}
	AeronUi_NextColumn(ui);
	if (AeronUi_Button(ui, "Cancel")) {
		settings->axis_conflict_open = 0;
	}
	AeronUi_EndColumns(ui);
	AeronUi_EndModal(ui);
}

static size_t xvt_controller_settings_find_source_binding(
	const struct xvt_controller_profile *profile,
	const AeronControllerDigitalSource *source)
{
	for (size_t index = 0; index < profile->binding_count; ++index) {
		if (xvt_controller_settings_digital_source_equal(
			    &profile->bindings[index].source, source)) {
			return index;
		}
	}
	return SIZE_MAX;
}

static void
xvt_controller_settings_remove_binding(struct xvt_controller_profile *profile,
				       size_t index)
{
	if (index >= profile->binding_count) {
		return;
	}
	if (index + 1 < profile->binding_count) {
		memmove(&profile->bindings[index],
			&profile->bindings[index + 1],
			(profile->binding_count - index - 1) *
				sizeof profile->bindings[0]);
	}
	--profile->binding_count;
}

static void xvt_controller_settings_add_captured_binding(
	struct xvt_controller_settings *settings,
	const AeronControllerSnapshot *controller,
	const AeronControllerDigitalSource *source)
{
	struct xvt_controller_profile *profile =
		xvt_controller_page_active_profile(settings, controller);
	const size_t existing =
		xvt_controller_settings_find_source_binding(profile, source);
	if (existing != SIZE_MAX) {
		if (profile->bindings[existing].action ==
		    settings->editor.selected_action) {
			size_t position = 0;
			for (size_t index = 0; index < existing; ++index) {
				if (profile->bindings[index].action ==
				    settings->editor.selected_action) {
					++position;
				}
			}
			settings->editor.highlighted_binding_row = position;
			return;
		}
		settings->pending_digital = *source;
		settings->conflicting_action =
			profile->bindings[existing].action;
		settings->binding_conflict_open = 1;
		return;
	}
	if (profile->binding_count >= XVT_CONTROLLER_BINDING_CAP) {
		snprintf(
			settings->error, sizeof settings->error,
			"You've reached the maximum number of control assignments. Remove one before adding another.");
		return;
	}
	profile->bindings[profile->binding_count++] =
		(struct xvt_input_action_binding){
			.source = *source,
			.action = settings->editor.selected_action};
	settings->editor.highlighted_binding_row = SIZE_MAX;
	xvt_controller_settings_apply_draft(settings);
}

static void xvt_controller_settings_find_binding_capture(
	struct xvt_controller_settings *settings, AeronUiContext *ui,
	const AeronControllerSnapshot *controller)
{
	const AeronControllerKind kind = controller->kind;
	const AeronUiControllerCaptureDesc desc = {
		controller->instance_id, AERON_UI_CONTROLLER_CAPTURE_DIGITAL};
	AeronUiControllerInput captured;
	const AeronUiControllerCaptureResult result = AeronUi_ControllerCapture(
		ui, "Find Binding...", "Press to identify", &desc, &captured);
	if (result != AERON_UI_CONTROLLER_CAPTURE_CAPTURED ||
	    captured.controller_kind != kind) {
		return;
	}
	const struct xvt_controller_profile *profile =
		xvt_controller_page_active_profile_const(settings, controller);
	const size_t binding = xvt_controller_settings_find_source_binding(
		profile, &captured.value.digital);
	if (binding == SIZE_MAX) {
		snprintf(settings->error, sizeof settings->error,
			 "This control is not bound.");
		return;
	}
	xvt_bindings_editor_select(&settings->editor,
				   profile->bindings[binding].action, false);
	settings->error[0] = '\0';
}

static void xvt_controller_settings_bindings_page(
	struct xvt_controller_settings *settings, AeronUiContext *ui,
	const AeronControllerSnapshot *controller, float trailing_height_ref)
{
	xvt_bindings_editor_category_selector(&settings->editor, ui);
	xvt_controller_settings_find_binding_capture(settings, ui, controller);
	AeronUi_Spacer(ui, 8.0f);
	AeronUiListItem items[XVT_INPUT_ACTION_COUNT - 1];
	char details[XVT_INPUT_ACTION_COUNT - 1][256];
	size_t count = 0;
	const struct xvt_controller_profile *profile =
		xvt_controller_page_active_profile_const(settings, controller);
	for (int action = XVT_INPUT_ACTION_NONE + 1;
	     action < XVT_INPUT_ACTION_COUNT; ++action) {
		if ((int)xvt_input_actions_category((xvt_input_action)action) !=
		    settings->editor.category) {
			continue;
		}
		xvt_controller_settings_describe_action_bindings(
			details[count], sizeof details[count], profile,
			(xvt_input_action)action, controller);
		items[count] = (AeronUiListItem){
			.id = (uint64_t)action,
			.label = xvt_input_actions_display_name(
				(xvt_input_action)action),
			.detail = details[count]};
		++count;
	}
	xvt_bindings_editor_actions(&settings->editor, ui, items, count,
				    trailing_height_ref);
}

static size_t xvt_controller_settings_build_action_binding_items(
	const struct xvt_controller_settings *settings,
	const AeronControllerSnapshot *controller, AeronUiListItem *items,
	char labels[][128], size_t *profile_indices)
{
	const struct xvt_controller_profile *profile =
		xvt_controller_page_active_profile_const(settings, controller);
	size_t count = 0;
	for (size_t index = 0; index < profile->binding_count; ++index) {
		if (profile->bindings[index].action !=
		    settings->editor.selected_action) {
			continue;
		}
		xvt_controller_settings_format_digital_source(
			labels[count], 128, &profile->bindings[index].source,
			controller);
		items[count] = (AeronUiListItem){
			.id = index, .label = labels[count], .detail = NULL};
		profile_indices[count] = index;
		++count;
	}
	return count;
}

static void xvt_controller_settings_binding_detail_modal(
	struct xvt_controller_settings *settings, AeronUiContext *ui,
	const AeronControllerSnapshot *controller)
{
	if (!xvt_bindings_editor_begin_detail(&settings->editor, ui)) {
		return;
	}
	struct xvt_controller_profile *profile =
		xvt_controller_page_active_profile(settings, controller);
	AeronUiListItem items[XVT_CONTROLLER_BINDING_CAP];
	char labels[XVT_CONTROLLER_BINDING_CAP][128];
	size_t profile_indices[XVT_CONTROLLER_BINDING_CAP];
	const size_t count = xvt_controller_settings_build_action_binding_items(
		settings, controller, items, labels, profile_indices);
	xvt_bindings_editor_binding_list(&settings->editor, ui, items, count,
					 "No bindings assigned.");
	if (settings->editor.highlighted_binding_row < count) {
		const size_t profile_index =
			profile_indices[settings->editor
						.highlighted_binding_row];
		AeronControllerDigitalSource *source =
			&profile->bindings[profile_index].source;
		if (source->kind == AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE ||
		    source->kind == AERON_CONTROLLER_DIGITAL_AXIS_NEGATIVE) {
			int threshold =
				(int)(source->threshold * 100.0f + 0.5f);
			if (AeronUi_SliderInt(ui, "Axis Threshold", &threshold,
					      5, 100, 5, "%d%%")) {
				source->threshold = (float)threshold / 100.0f;
				xvt_controller_settings_apply_draft(settings);
			}
		}
		if (xvt_bindings_editor_remove_button(ui)) {
			xvt_controller_settings_remove_binding(profile,
							       profile_index);
			settings->editor.highlighted_binding_row = SIZE_MAX;
			xvt_controller_settings_apply_draft(settings);
		}
	}
	AeronUi_Separator(ui);
	const AeronControllerKind kind = controller->kind;
	const bool has_capacity =
		profile->binding_count < XVT_CONTROLLER_BINDING_CAP;
	const AeronUiControllerCaptureDesc desc = {
		has_capacity ? controller->instance_id : 0,
		AERON_UI_CONTROLLER_CAPTURE_DIGITAL};
	AeronUiControllerInput captured;
	const AeronUiControllerCaptureResult capture =
		AeronUi_ControllerCapture(ui, "Add Binding...", "Press to add",
					  &desc, &captured);
	if (capture == AERON_UI_CONTROLLER_CAPTURE_CAPTURED &&
	    captured.controller_kind == kind) {
		xvt_controller_settings_add_captured_binding(
			settings, controller, &captured.value.digital);
	}
	if (!has_capacity) {
		AeronUi_Error(
			ui,
			"You've reached the maximum number of control assignments. Remove one before adding another.");
	}
	xvt_bindings_editor_end_detail(&settings->editor, ui);
}

static void xvt_controller_settings_binding_conflict_modal(
	struct xvt_controller_settings *settings, AeronUiContext *ui,
	const AeronControllerSnapshot *controller)
{
	char label[128];
	xvt_controller_settings_format_digital_source(
		label, sizeof label, &settings->pending_digital, controller);
	if (xvt_bindings_editor_confirm_replace(
		    ui, &settings->binding_conflict_open, label,
		    settings->conflicting_action,
		    settings->editor.selected_action)) {
		struct xvt_controller_profile *profile =
			xvt_controller_page_active_profile(settings,
							   controller);
		size_t existing = xvt_controller_settings_find_source_binding(
			profile, &settings->pending_digital);
		if (existing != SIZE_MAX) {
			profile->bindings[existing].action =
				settings->editor.selected_action;
			settings->editor.highlighted_binding_row = SIZE_MAX;
			xvt_controller_settings_apply_draft(settings);
		}
	}
}

/* Unbinds every axis in the selected model's profile that another model in candidate already has
 * bound, so restored gamepad defaults never take an axis away from another controller. */
static void xvt_controller_settings_clear_axes_used_by_others(
	const struct xvt_controller_options *candidate, int model,
	struct xvt_controller_model *selected)
{
	for (size_t i = 0; i < candidate->count; ++i) {
		if (i != (size_t)model) {
			for (int axis = 0; axis < XVT_INPUT_AXIS_COUNT;
			     ++axis) {
				if (candidate->models[i]
					    .profile.mapping.axes[axis]
					    .source >= 0) {
					selected->profile.mapping.axes[axis]
						.source = -1;
				}
			}
		}
	}
}

static void
xvt_controller_settings_restore_modal(struct xvt_controller_settings *settings,
				      AeronUiContext *ui,
				      const struct xvt_settings *config)
{
	const AeronControllerSnapshot *device =
		xvt_controller_page_selected_controller(settings,
							Aeron_InputSnapshot());
	if (!device) {
		settings->restore_modal_open = 0;
		return;
	}
	if (!AeronUi_BeginModal(ui, "RESTORE CONTROLLER DEFAULTS",
				&settings->restore_modal_open, NULL)) {
		return;
	}
	AeronUi_Help(
		ui,
		"Reset this model's bindings? Other controllers keep their assignments.");
	AeronUi_BeginColumns(ui, 2, NULL);
	if (AeronUi_Button(ui, "Reset to defaults")) {
		struct xvt_controller_options candidate = settings->draft;
		if (xvt_controller_options_ensure_model(
			    &candidate, device, settings->error,
			    sizeof settings->error)) {
			int model = xvt_controller_options_find_model(
				&candidate, device->guid);
			struct xvt_controller_model *selected =
				&candidate.models[model];
			selected->kind = device->kind;
			xvt_controller_options_clear_profile(&selected->profile,
							     device->kind);
			if (device->kind == AERON_CONTROLLER_KIND_GAMEPAD) {
				selected->profile = config->gamepad_defaults;
				xvt_controller_settings_clear_axes_used_by_others(
					&candidate, model, selected);
			}
			settings->draft = candidate;
			xvt_controller_settings_apply_draft(settings);
			xvt_controller_settings_reset_device_edit_state(
				settings, ui);
		}
		settings->restore_modal_open = 0;
	}
	AeronUi_NextColumn(ui);
	if (AeronUi_Button(ui, "Cancel")) {
		settings->restore_modal_open = 0;
	}
	AeronUi_EndColumns(ui);
	AeronUi_EndModal(ui);
}

void xvt_controller_settings_draw(struct xvt_controller_settings *settings,
				  AeronUiContext *ui,
				  const AeronInputSnapshot *input)
{
	static const char *const pages[] = {"Axes", "Bindings"};
	if (!settings || !ui || !input) {
		return;
	}
	xvt_controller_settings_device_selector(settings, ui, input);
	const AeronControllerSnapshot *controller =
		xvt_controller_page_selected_controller(settings, input);
	const uint32_t active_instance =
		controller ? controller->instance_id : 0;
	if (active_instance != settings->active_instance ||
	    (controller && controller->kind != settings->active_kind)) {
		xvt_controller_settings_reset_device_edit_state(settings, ui);
		settings->active_instance = active_instance;
		settings->active_kind = controller ? controller->kind
						   : AERON_CONTROLLER_KIND_NONE;
	}
	if (controller) {
		int matches = 0;
		for (int i = 0; i < AERON_CONTROLLER_MAX; ++i) {
			if (input->controllers[i].connected &&
			    !strcmp(input->controllers[i].guid,
				    controller->guid)) {
				++matches;
			}
		}
		if (matches > 1) {
			AeronUi_Help(
				ui,
				"Bindings shared by all controllers of this model.");
			uint32_t preferred =
				xvt_controller_mapping_analog_instance(
					controller->guid);
			int model = xvt_controller_options_find_model(
				&settings->draft, controller->guid);
			const AeronControllerSnapshot *analog =
				model >= 0 ? xvt_controller_mapping_resolve(
						     &settings->draft
							      .models[model],
						     input, preferred)
					   : NULL;
			for (int i = 0; analog && i < AERON_CONTROLLER_MAX;
			     ++i) {
				if (analog == &input->controllers[i]) {
					char text[160];
					snprintf(text, sizeof text,
						 "Analog input: %s #%d",
						 analog->name, i + 1);
					AeronUi_Help(ui, text);
				}
			}
		}
	}
	int model = controller ? xvt_controller_options_find_model(
					 &settings->draft, controller->guid)
			       : -1;
	bool compatible =
		!controller || model < 0 ||
		settings->draft.models[model].kind == controller->kind;
	if (!compatible) {
		xvt_controller_settings_reset_device_edit_state(settings, ui);
		AeronUi_Error(
			ui,
			"Saved bindings use a different device type. Restore Controller Defaults to "
			"configure this device.");
	} else {
		xvt_controller_settings_controller_warning(settings, ui,
							   controller);
	}
	if (controller && compatible &&
	    AeronUi_SegmentedSelector(ui, "Controller Page", &settings->page,
				      pages, 2)) {
		AeronUi_CancelControllerCapture(ui);
		settings->editor.binding_modal_open = 0;
	}
	float trailing_height = 143.0f;
	if (settings->error[0]) {
		trailing_height +=
			AeronUi_MeasureHelpHeight(ui, settings->error, 0.0f) +
			60.0f;
	}
	if (controller && compatible &&
	    settings->page == CONTROLLER_PAGE_AXES) {
		float scroll_height =
			AeronUi_AvailableHeight(ui) - trailing_height;
		if (scroll_height < 180.0f) {
			scroll_height = 180.0f;
		}
		if (AeronUi_BeginScroll(ui, "Flight Axes", scroll_height)) {
			xvt_controller_settings_axis_page(settings, ui,
							  controller);
			AeronUi_EndScroll(ui);
		}
	} else if (controller && compatible) {
		xvt_controller_settings_bindings_page(settings, ui, controller,
						      trailing_height);
	}
	if (settings->error[0]) {
		AeronUi_Error(ui, settings->error);
		if (AeronUi_Button(ui, "Dismiss Error")) {
			settings->error[0] = '\0';
		}
	}
	if (AeronUi_ButtonEnabled(ui, "Restore Controller Defaults",
				  controller != NULL)) {
		settings->restore_modal_open = 1;
	}
}

void xvt_controller_settings_draw_modals(
	struct xvt_controller_settings *settings, AeronUiContext *ui,
	const AeronInputSnapshot *input, const struct xvt_settings *config)
{
	if (!settings || !ui || !input || !config) {
		return;
	}
	const AeronControllerSnapshot *controller =
		xvt_controller_page_selected_controller(settings, input);
	int model = controller ? xvt_controller_options_find_model(
					 &settings->draft, controller->guid)
			       : -1;
	bool compatible = controller &&
			  (model < 0 || settings->draft.models[model].kind ==
						controller->kind);
	if (!compatible) {
		xvt_controller_settings_cancel_capture(settings, ui);
	}
	if (settings->axis_conflict_open) {
		xvt_controller_settings_axis_conflict_modal(settings, ui,
							    controller);
		return;
	}
	if (settings->binding_conflict_open) {
		xvt_controller_settings_binding_conflict_modal(settings, ui,
							       controller);
		return;
	}
	if (settings->restore_modal_open) {
		xvt_controller_settings_restore_modal(settings, ui, config);
		return;
	}
	if (compatible) {
		xvt_controller_settings_binding_detail_modal(settings, ui,
							     controller);
	}
}
