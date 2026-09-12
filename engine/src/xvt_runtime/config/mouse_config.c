#include "xvt_runtime/config/mouse_config.h"
#include "xvt_runtime/config/config.h"

static const char* const paths[] = { "input.mouse_flight", "input.mouse_sensitivity",
									 "input.mouse_invert_y" };

bool XvtMouseConfig_Parse(const AeronConfigFile* document, XvtMouseOptions* options, char* error,
						  size_t capacity) {
	for (unsigned i = 0; i < 3; ++i) {
		const AeronConfigNode* node = AeronConfigFile_GetNode(document, paths[i]);
		AeronConfigNodeType type = AeronConfigNode_Type(node);
		bool valid = false;
		if (i == 0 || i == 2) {
			valid = type == AERON_CONFIG_BOOL;
			if (i == 0)
				options->mouse_flight_enabled = AeronConfigNode_Bool(node, false);
			else
				options->mouse_invert_y = AeronConfigNode_Bool(node, false);
		} else {
			int64_t sensitivity = AeronConfigNode_Int(node, 0);
			valid = type == AERON_CONFIG_INT && sensitivity >= 1 && sensitivity <= 9;
			options->mouse_sensitivity = valid ? (int)sensitivity : 5;
		}
		if (!valid)
			return XvtSettings_NodeError(document, paths[i], "invalid mouse setting", error, capacity);
	}
	return true;
}

bool XvtConfig_SetMouse(const XvtMouseOptions* options, char* error, size_t capacity) {
	AeronConfigFile* candidate = NULL;
	AeronConfigError detail;
	if (!AeronConfigFile_Clone(XvtConfig_UserDocument(), &candidate, &detail))
		return XvtSettings_FileError(&detail, error, capacity);
	const XvtMouseOptions* defaults = &XvtConfig_DefaultSettings()->mouse;
	bool success = AeronConfigFile_SetBool(candidate, paths[0], options->mouse_flight_enabled, &detail) &&
				   AeronConfigFile_SetInt(candidate, paths[1], options->mouse_sensitivity, &detail) &&
				   AeronConfigFile_SetBool(candidate, paths[2], options->mouse_invert_y, &detail);
	const bool equal[] = { options->mouse_flight_enabled == defaults->mouse_flight_enabled,
						   options->mouse_sensitivity == defaults->mouse_sensitivity,
						   options->mouse_invert_y == defaults->mouse_invert_y };
	for (unsigned i = 0; i < 3 && success; ++i)
		if (equal[i])
			success = AeronConfigFile_Remove(candidate, paths[i], &detail);
	if (success)
		success = XvtConfig_UpdateUser(candidate, 0, error, capacity);
	else
		XvtSettings_FileError(&detail, error, capacity);
	AeronConfigFile_Destroy(candidate);
	return success;
}
