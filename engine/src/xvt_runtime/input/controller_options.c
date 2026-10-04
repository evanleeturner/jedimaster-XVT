#include "xvt_runtime/input/controller_options.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool
xvt_controller_options_same_source(const AeronControllerDigitalSource *left,
				   const AeronControllerDigitalSource *right)
{
	return left->kind == right->kind && left->index == right->index &&
	       (left->kind != AERON_CONTROLLER_DIGITAL_HAT ||
		left->hat_direction == right->hat_direction);
}

bool xvt_controller_options_profile_equal(
	const struct xvt_controller_profile *left,
	const struct xvt_controller_profile *right)
{
	if (left->binding_count != right->binding_count) {
		return false;
	}
	for (int axis = 0; axis < XVT_INPUT_AXIS_COUNT; ++axis) {
		const struct xvt_input_axis_binding *a =
			&left->mapping.axes[axis];
		const struct xvt_input_axis_binding *b =
			&right->mapping.axes[axis];
		if (a->source != b->source || a->invert != b->invert ||
		    a->deadzone != b->deadzone) {
			return false;
		}
	}
	for (size_t index = 0; index < left->binding_count; ++index) {
		const struct xvt_input_action_binding *a =
			&left->bindings[index];
		const struct xvt_input_action_binding *b =
			&right->bindings[index];
		if (a->action != b->action ||
		    !xvt_controller_options_same_source(&a->source,
							&b->source) ||
		    a->source.threshold != b->source.threshold) {
			return false;
		}
	}
	return true;
}

bool xvt_controller_options_equals(const struct xvt_controller_options *left,
				   const struct xvt_controller_options *right)
{
	if (!left || !right) {
		return left == right;
	}
	if (left->count != right->count) {
		return false;
	}
	for (size_t i = 0; i < left->count; ++i) {
		const struct xvt_controller_model *a = &left->models[i],
						  *b = &right->models[i];
		if (strcmp(a->guid, b->guid) || strcmp(a->name, b->name) ||
		    a->kind != b->kind ||
		    !xvt_controller_options_profile_equal(&a->profile,
							  &b->profile)) {
			return false;
		}
	}
	return true;
}

bool xvt_controller_options_effective_axis_invert(AeronControllerKind kind,
						  xvt_input_axis axis,
						  bool invert)
{
	/* Gamepad Y is positive downward, while positive flight pitch raises the nose. */
	return kind == AERON_CONTROLLER_KIND_GAMEPAD &&
			       axis == XVT_INPUT_AXIS_PITCH
		       ? !invert
		       : invert;
}

static bool xvt_controller_options_validation_error(char *error,
						    size_t capacity,
						    const char *message)
{
	if (error && capacity) {
		snprintf(error, capacity, "%s", message);
	}
	return false;
}

bool xvt_controller_options_validate_profile(
	const struct xvt_controller_profile *profile, AeronControllerKind kind,
	char *error, size_t capacity)
{
	const int axis_limit = kind == AERON_CONTROLLER_KIND_GAMEPAD
				       ? AERON_GAMEPAD_AXIS_COUNT
				       : AERON_CONTROLLER_AXIS_MAX;
	const int button_limit = kind == AERON_CONTROLLER_KIND_GAMEPAD
					 ? AERON_GAMEPAD_BUTTON_COUNT
					 : AERON_CONTROLLER_BUTTON_MAX;
	if (!profile || (kind != AERON_CONTROLLER_KIND_GAMEPAD &&
			 kind != AERON_CONTROLLER_KIND_JOYSTICK)) {
		return xvt_controller_options_validation_error(
			error, capacity, "invalid controller profile");
	}
	if (profile->binding_count > XVT_CONTROLLER_BINDING_CAP) {
		return xvt_controller_options_validation_error(
			error, capacity,
			"controller binding capacity exceeded");
	}
	for (int axis = 0; axis < XVT_INPUT_AXIS_COUNT; ++axis) {
		const struct xvt_input_axis_binding *binding =
			&profile->mapping.axes[axis];
		if (binding->source < -1 || binding->source >= axis_limit ||
		    !isfinite(binding->deadzone) || binding->deadzone < 0.0f ||
		    binding->deadzone >
			    (axis == XVT_INPUT_AXIS_THROTTLE ? 0.0f : 1.0f)) {
			return xvt_controller_options_validation_error(
				error, capacity,
				"controller axis binding is invalid");
		}
		if (binding->source < 0) {
			continue;
		}
		for (int prior = 0; prior < axis; ++prior) {
			if (profile->mapping.axes[prior].source ==
			    binding->source) {
				return xvt_controller_options_validation_error(
					error, capacity,
					"controller axis source is assigned more than once");
			}
		}
	}
	for (size_t index = 0; index < profile->binding_count; ++index) {
		const struct xvt_input_action_binding *binding =
			&profile->bindings[index];
		const AeronControllerDigitalSource *source = &binding->source;
		if (binding->action <= XVT_INPUT_ACTION_NONE ||
		    binding->action >= XVT_INPUT_ACTION_COUNT) {
			return xvt_controller_options_validation_error(
				error, capacity,
				"controller action binding is invalid");
		}
		if (!isfinite(source->threshold) || source->threshold <= 0.0f ||
		    source->threshold > 1.0f) {
			return xvt_controller_options_validation_error(
				error, capacity,
				"controller digital threshold is invalid");
		}
		switch (source->kind) {
		case AERON_CONTROLLER_DIGITAL_BUTTON:
			if (source->index >= button_limit) {
				return xvt_controller_options_validation_error(
					error, capacity,
					"controller button index is out of range");
			}
			break;
		case AERON_CONTROLLER_DIGITAL_AXIS_POSITIVE:
		case AERON_CONTROLLER_DIGITAL_AXIS_NEGATIVE:
			if (source->index >= axis_limit) {
				return xvt_controller_options_validation_error(
					error, capacity,
					"controller digital axis is invalid");
			}
			break;
		case AERON_CONTROLLER_DIGITAL_HAT:
			if (kind != AERON_CONTROLLER_KIND_JOYSTICK ||
			    source->index >= AERON_CONTROLLER_HAT_MAX ||
			    (source->hat_direction != AERON_CONTROLLER_HAT_UP &&
			     source->hat_direction !=
				     AERON_CONTROLLER_HAT_RIGHT &&
			     source->hat_direction !=
				     AERON_CONTROLLER_HAT_DOWN &&
			     source->hat_direction !=
				     AERON_CONTROLLER_HAT_LEFT)) {
				return xvt_controller_options_validation_error(
					error, capacity,
					"controller hat binding is invalid");
			}
			break;
		default:
			return xvt_controller_options_validation_error(
				error, capacity,
				"controller source kind is invalid");
		}
		for (size_t prior = 0; prior < index; ++prior) {
			if (xvt_controller_options_same_source(
				    source, &profile->bindings[prior].source)) {
				return xvt_controller_options_validation_error(
					error, capacity,
					"controller source is bound more than once");
			}
		}
	}
	return true;
}

void xvt_controller_options_clear_profile(
	struct xvt_controller_profile *profile, AeronControllerKind kind)
{
	memset(profile, 0, sizeof *profile);
	for (int i = 0; i < XVT_INPUT_AXIS_COUNT; ++i) {
		profile->mapping.axes[i].source = -1;
	}
	/* Raw HOTAS levers commonly decrease their reported value toward full power. */
	profile->mapping.axes[XVT_INPUT_AXIS_THROTTLE].invert =
		kind == AERON_CONTROLLER_KIND_JOYSTICK;
}

int xvt_controller_options_find_model(
	const struct xvt_controller_options *options, const char *guid)
{
	if (options && guid) {
		for (size_t i = 0; i < options->count; ++i) {
			if (!strcmp(options->models[i].guid, guid)) {
				return (int)i;
			}
		}
	}
	return -1;
}

static bool guid_valid(const char *guid)
{
	bool nonzero = false;
	for (int i = 0; i < 32; ++i) {
		if (!((guid[i] >= '0' && guid[i] <= '9') ||
		      (guid[i] >= 'a' && guid[i] <= 'f'))) {
			return false;
		}
		nonzero |= guid[i] != '0';
	}
	return nonzero && guid[32] == 0;
}

bool xvt_controller_options_validate(
	const struct xvt_controller_options *options, char *error,
	size_t capacity)
{
	if (!options || options->count > XVT_CONTROLLER_MODEL_CAP) {
		return xvt_controller_options_validation_error(
			error, capacity, "controller model capacity exceeded");
	}
	for (size_t i = 0; i < options->count; ++i) {
		const struct xvt_controller_model *m = &options->models[i];
		if (!guid_valid(m->guid) ||
		    !memchr(m->name, 0, sizeof m->name)) {
			return xvt_controller_options_validation_error(
				error, capacity,
				"invalid controller model identity");
		}
		if (!xvt_controller_options_validate_profile(
			    &m->profile, m->kind, error, capacity)) {
			return false;
		}
		for (size_t j = 0; j < i; ++j) {
			if (!strcmp(m->guid, options->models[j].guid)) {
				return xvt_controller_options_validation_error(
					error, capacity,
					"duplicate controller model GUID");
			}
			for (int axis = 0; axis < XVT_INPUT_AXIS_COUNT;
			     ++axis) {
				if (m->profile.mapping.axes[axis].source >= 0 &&
				    options->models[j]
						    .profile.mapping.axes[axis]
						    .source >= 0) {
					return xvt_controller_options_validation_error(
						error, capacity,
						"logical axis has multiple model owners");
				}
			}
		}
	}
	return true;
}

bool xvt_controller_options_ensure_model(struct xvt_controller_options *options,
					 const AeronControllerSnapshot *device,
					 char *error, size_t capacity)
{
	if (!device || !guid_valid(device->guid)) {
		return xvt_controller_options_validation_error(
			error, capacity, "controller has no usable model GUID");
	}
	if (xvt_controller_options_find_model(options, device->guid) >= 0) {
		return true;
	}
	if (options->count >= XVT_CONTROLLER_MODEL_CAP) {
		return xvt_controller_options_validation_error(
			error, capacity, "controller model capacity exceeded");
	}
	struct xvt_controller_model *m = &options->models[options->count++];
	memset(m, 0, sizeof *m);
	memcpy(m->guid, device->guid, sizeof m->guid);
	snprintf(m->name, sizeof m->name, "%s", device->name);
	m->kind = device->kind;
	xvt_controller_options_clear_profile(&m->profile, device->kind);
	return true;
}

bool xvt_controller_options_add_new_gamepads(
	struct xvt_controller_options *options,
	const struct xvt_controller_profile *defaults,
	const AeronInputSnapshot *input, char *error, size_t capacity)
{
	const AeronControllerSnapshot *sorted[AERON_CONTROLLER_MAX];
	int count = 0;
	if (!input) {
		return true;
	}
	for (int slot = 0; slot < AERON_CONTROLLER_MAX; ++slot) {
		const AeronControllerSnapshot *d = &input->controllers[slot];
		if (!d->connected || d->kind != AERON_CONTROLLER_KIND_GAMEPAD) {
			continue;
		}
		int j = count++;
		while (j > 0 && strcmp(sorted[j - 1]->guid, d->guid) > 0) {
			sorted[j] = sorted[j - 1];
			--j;
		}
		sorted[j] = d;
	}
	for (int i = 0; i < count; ++i) {
		if (xvt_controller_options_find_model(options,
						      sorted[i]->guid) >= 0) {
			continue;
		}
		if (!xvt_controller_options_ensure_model(options, sorted[i],
							 error, capacity)) {
			return false;
		}
		struct xvt_controller_profile *p =
			&options->models[options->count - 1].profile;
		*p = *defaults;
		for (size_t j = 0; j + 1 < options->count; ++j) {
			for (int axis = 0; axis < XVT_INPUT_AXIS_COUNT;
			     ++axis) {
				if (options->models[j]
					    .profile.mapping.axes[axis]
					    .source >= 0) {
					p->mapping.axes[axis].source = -1;
				}
			}
		}
	}
	return true;
}
