#ifndef XVT_RUNTIME_CONFIG_CONTROLLER_CONFIG_H
#define XVT_RUNTIME_CONFIG_CONTROLLER_CONFIG_H
#include "aeron/config_file.h"
#include "xvt_runtime/input/controller_options.h"
/* Controller models in the settings document. Each has a guid, a name, a layout (gamepad or joystick),
 * four axes and button bindings. A gamepad names its axes and buttons; a joystick numbers them and also
 * has hats. Error messages here carry the file and line only for a setting of the wrong type. */

/* Reads the required input.controllers list, at most XVT_CONTROLLER_MODEL_CAP models with only the five
 * fields above; guids are stored in lower case. Each profile is read by ReadProfile, then the whole set
 * is validated. */
bool XvtControllerConfig_Parse(const AeronConfigFile* document, XvtControllerOptions* out, char* error,
							   size_t capacity);
/* Replaces input.controllers with options: every model with all four axes (throttle without a
 * deadzone) and its bindings grouped by action in a fixed order. It does not validate; callers do. */
bool XvtControllerConfig_Write(AeronConfigFile* document, const XvtControllerOptions* options,
							   AeronConfigError* error);
/* Reads the required mapping at path into *profile for a gamepad or joystick layout, after clearing it.
 * path.axes holds yaw, pitch, roll and throttle, each with source ("none" unbinds), invert and deadzone
 * from 0 to 1 (throttle's must be absent or 0); an axis left out keeps its cleared value. path.buttons
 * maps action names to one source or a list: a gamepad button name, {button} on a joystick, {axis,
 * direction positive or negative, threshold in (0, 1], default 0.5}, or {hat, direction up, right, down
 * or left} on a joystick. A source bound to two actions fails; one repeated for the same action is
 * ignored. Unknown fields fail, and the profile is validated at the end. */
bool XvtControllerConfig_ReadProfile(const AeronConfigFile* document, const char* path,
									 AeronControllerKind kind, XvtControllerProfile* profile, char* error,
									 size_t capacity);
#endif
