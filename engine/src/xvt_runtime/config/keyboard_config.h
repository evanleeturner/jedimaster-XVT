#ifndef XVT_KEYBOARD_CONFIG_H
#define XVT_KEYBOARD_CONFIG_H
#include "aeron/config_file.h"
#include "xvt_runtime/input/keyboard_mapping.h"

bool XvtKeyboardConfig_Read(const AeronConfigFile* document, XvtKeyboardBindings* profile, char* error,
							size_t capacity);
bool XvtKeyboardConfig_Write(AeronConfigFile* document, const XvtKeyboardBindings* profile,
							 AeronConfigError* error);
/* Resolve user precedence into the temporary merged document without changing user overrides. */
bool XvtKeyboardConfig_Resolve(const XvtKeyboardBindings* defaults, const AeronConfigFile* user,
							   AeronConfigFile* merged, char* error, size_t capacity);
#endif
