#ifndef XVT_MOUSE_CONFIG_H
#define XVT_MOUSE_CONFIG_H
#include "aeron/config_file.h"
#include <stdbool.h>

enum { XVT_MOUSE_SENSITIVITY_MIN = 1, XVT_MOUSE_SENSITIVITY_MAX = 9 };

typedef struct XvtMouseOptions {
	int mouse_flight_enabled, mouse_sensitivity, mouse_invert_y;
} XvtMouseOptions;

bool XvtMouseConfig_Parse(const AeronConfigFile* document, XvtMouseOptions* options, char* error,
						  size_t capacity);
bool XvtConfig_SetMouse(const XvtMouseOptions* options, char* error, size_t capacity);
#endif
