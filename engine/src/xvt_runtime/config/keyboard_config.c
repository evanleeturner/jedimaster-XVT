#include "xvt_runtime/config/keyboard_config.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/aeron.h"

static bool config_error(char *error, size_t capacity, const char *format, ...)
{
	if (error && capacity) {
		va_list args;
		va_start(args, format);
		vsnprintf(error, capacity, format, args);
		va_end(args);
	}
	return false;
}

static const char *const modifier_names[] = {"shift", "ctrl", "alt", "gui"};

static bool read_source(const AeronConfigNode *node, AeronKeyChord *source,
			char *error, size_t capacity)
{
	if (AeronConfigNode_Type(node) != AERON_CONFIG_MAP) {
		return config_error(error, capacity,
				    "keyboard source must be a key mapping");
	}
	for (size_t i = 0; i < AeronConfigNode_MapCount(node); ++i) {
		const char *name = AeronConfigNode_MapKeyAt(node, i);
		if (strcmp(name, "key") && strcmp(name, "modifiers")) {
			return config_error(
				error, capacity,
				"unknown keyboard source field '%s'", name);
		}
	}
	const char *key = AeronConfigNode_String(
		AeronConfigNode_MapGet(node, "key"), NULL);
	AeronKey parsed;
	if (!key || !AeronKey_FromName(key, &parsed)) {
		return config_error(error, capacity,
				    "unknown keyboard key '%s'",
				    key ? key : "");
	}
	*source = (AeronKeyChord){.key = (uint16_t)parsed};
	const AeronConfigNode *mods = AeronConfigNode_MapGet(node, "modifiers");
	if (mods && AeronConfigNode_Type(mods) != AERON_CONFIG_SEQUENCE) {
		return config_error(error, capacity,
				    "keyboard modifiers must be a sequence");
	}
	for (size_t i = 0; i < AeronConfigNode_SequenceCount(mods); ++i) {
		const char *name = AeronConfigNode_String(
			AeronConfigNode_SequenceGet(mods, i), NULL);
		int bit = 0;
		for (; bit < 4; ++bit) {
			if (name && !strcmp(name, modifier_names[bit])) {
				break;
			}
		}
		if (bit == 4 || (source->modifiers & (1u << bit))) {
			return config_error(
				error, capacity,
				"invalid or repeated keyboard modifier '%s'",
				name ? name : "");
		}
		source->modifiers |= (uint8_t)(1u << bit);
	}
	return true;
}

static bool add_source(struct xvt_keyboard_bindings *profile,
		       xvt_input_action action, AeronKeyChord source,
		       char *error, size_t capacity)
{
	if (!xvt_keyboard_mapping_source_valid(source)) {
		return config_error(
			error, capacity,
			"keyboard source '%s' is reserved or unsupported",
			AeronKey_Name((AeronKey)source.key));
	}
	if (xvt_keyboard_mapping_find(profile, source) != SIZE_MAX) {
		return config_error(error, capacity,
				    "keyboard source '%s' is bound twice",
				    AeronKey_Name((AeronKey)source.key));
	}
	if (profile->count == XVT_KEYBOARD_BINDING_CAP) {
		return config_error(error, capacity,
				    "keyboard binding capacity exceeded");
	}
	profile->bindings[profile->count++] =
		(struct xvt_keyboard_binding){source, action};
	return true;
}

bool xvt_keyboard_config_read(const AeronConfigFile *document,
			      struct xvt_keyboard_bindings *profile,
			      char *error, size_t capacity)
{
	memset(profile, 0, sizeof *profile);
	const AeronConfigNode *map =
		AeronConfigFile_GetNode(document, "input.keyboard");
	if (AeronConfigNode_Type(map) != AERON_CONFIG_MAP) {
		return config_error(error, capacity,
				    "input.keyboard must be a mapping");
	}
	for (size_t i = 0; i < AeronConfigNode_MapCount(map); ++i) {
		const char *name = AeronConfigNode_MapKeyAt(map, i);
		const xvt_input_action action =
			xvt_input_actions_from_name(name);
		const AeronConfigNode *value =
			AeronConfigNode_MapValueAt(map, i);
		if (!xvt_input_actions_keyboard_bindable(action)) {
			return config_error(error, capacity,
					    "unknown keyboard action '%s'",
					    name);
		}
		if (AeronConfigNode_Type(value) == AERON_CONFIG_STRING) {
			AeronKey key;
			const char *label = AeronConfigNode_String(value, NULL);
			if (!AeronKey_FromName(label, &key)) {
				return config_error(error, capacity,
						    "unknown keyboard key '%s'",
						    label);
			}
			if (!add_source(profile, action,
					(AeronKeyChord){.key = (uint16_t)key},
					error, capacity)) {
				return false;
			}
		} else if (AeronConfigNode_Type(value) ==
			   AERON_CONFIG_SEQUENCE) {
			for (size_t j = 0;
			     j < AeronConfigNode_SequenceCount(value); ++j) {
				AeronKeyChord source;
				if (!read_source(AeronConfigNode_SequenceGet(
							 value, j),
						 &source, error, capacity) ||
				    !add_source(profile, action, source, error,
						capacity)) {
					return false;
				}
			}
		} else {
			return config_error(
				error, capacity,
				"keyboard action '%s' requires a key name or sequence",
				name);
		}
	}
	xvt_keyboard_mapping_sort(profile);
	return true;
}

struct source_yaml {
	AeronConfigMapValue fields[2];
	AeronConfigValue key;
	AeronConfigValue modifiers;
	AeronConfigValue modifier_values[4];
};

bool xvt_keyboard_config_write(AeronConfigFile *document,
			       const struct xvt_keyboard_bindings *profile,
			       AeronConfigError *error)
{
	if (profile->count > XVT_KEYBOARD_BINDING_CAP) {
		snprintf(error->message, sizeof error->message,
			 "keyboard binding capacity exceeded");
		return false;
	}
	for (size_t i = 0; i < profile->count; ++i) {
		if (!xvt_input_actions_keyboard_bindable(
			    profile->bindings[i].action) ||
		    !xvt_keyboard_mapping_source_valid(
			    profile->bindings[i].source)) {
			snprintf(error->message, sizeof error->message,
				 "invalid keyboard action");
			return false;
		}
	}
	struct source_yaml *scratch =
		calloc(XVT_KEYBOARD_BINDING_CAP, sizeof *scratch);
	if (!scratch) {
		snprintf(error->message, sizeof error->message,
			 "keyboard serialization allocation failed");
		return false;
	}
	AeronConfigValue sources[XVT_KEYBOARD_BINDING_CAP];
	AeronConfigValue actions[XVT_INPUT_ACTION_COUNT - 1];
	AeronConfigMapValue entries[XVT_INPUT_ACTION_COUNT - 1];
	size_t used = 0, entry_count = 0;
	for (int action = XVT_INPUT_ACTION_NONE + 1;
	     action < XVT_INPUT_ACTION_COUNT; ++action) {
		if (!xvt_input_actions_keyboard_bindable(
			    (xvt_input_action)action)) {
			continue;
		}
		size_t first = used;
		for (size_t i = 0; i < profile->count; ++i) {
			const struct xvt_keyboard_binding *b =
				&profile->bindings[i];
			if ((int)b->action != action) {
				continue;
			}
			if (used == XVT_KEYBOARD_BINDING_CAP) {
				free(scratch);
				snprintf(error->message, sizeof error->message,
					 "keyboard binding capacity exceeded");
				return false;
			}
			struct source_yaml *s = &scratch[used];
			s->key = (AeronConfigValue){
				.type = AERON_CONFIG_STRING,
				.value.string_value =
					AeronKey_Name((AeronKey)b->source.key)};
			size_t mods = 0;
			for (int bit = 0; bit < 4; ++bit) {
				if (b->source.modifiers & (1u << bit)) {
					s->modifier_values
						[mods++] = (AeronConfigValue){
						.type = AERON_CONFIG_STRING,
						.value.string_value =
							modifier_names[bit]};
				}
			}
			s->modifiers = (AeronConfigValue){
				.type = AERON_CONFIG_SEQUENCE,
				.value.sequence = {s->modifier_values, mods}};
			s->fields[0] = (AeronConfigMapValue){"key", &s->key};
			s->fields[1] = (AeronConfigMapValue){"modifiers",
							     &s->modifiers};
			sources[used++] = (AeronConfigValue){
				.type = AERON_CONFIG_MAP,
				.value.map = {s->fields, mods ? 2 : 1}};
		}
		actions[action - 1] = (AeronConfigValue){
			.type = AERON_CONFIG_SEQUENCE,
			.value.sequence = {sources + first, used - first}};
		entries[entry_count++] = (AeronConfigMapValue){
			xvt_input_actions_to_name((xvt_input_action)action),
			&actions[action - 1]};
	}
	AeronConfigValue value = {.type = AERON_CONFIG_MAP,
				  .value.map = {entries, entry_count}};
	bool ok = AeronConfigFile_SetValue(document, "input.keyboard", &value,
					   error) != 0;
	free(scratch);
	return ok;
}

bool xvt_keyboard_config_resolve(const struct xvt_keyboard_bindings *defaults,
				 const AeronConfigFile *user,
				 AeronConfigFile *merged, char *error,
				 size_t capacity)
{
	if (!AeronConfigFile_Has(user, "input.keyboard")) {
		return true;
	}
	struct xvt_keyboard_bindings effective;
	if (!xvt_keyboard_config_read(user, &effective, error, capacity)) {
		return false;
	}
	const AeronConfigNode *overrides =
		AeronConfigFile_GetNode(user, "input.keyboard");
	for (size_t i = 0; i < defaults->count; ++i) {
		const struct xvt_keyboard_binding *binding =
			&defaults->bindings[i];
		/* Explicit action lists replace their defaults, including empty lists.
		 * Explicit sources also displace matching sources from inherited actions. */
		if (AeronConfigNode_MapGet(
			    overrides,
			    xvt_input_actions_to_name(binding->action)) ||
		    xvt_keyboard_mapping_find(&effective, binding->source) !=
			    SIZE_MAX) {
			continue;
		}
		if (!add_source(&effective, binding->action, binding->source,
				error, capacity)) {
			return false;
		}
	}
	AeronConfigError detail = {0};
	if (!xvt_keyboard_config_write(merged, &effective, &detail)) {
		return config_error(error, capacity, "%s", detail.message);
	}
	return true;
}
