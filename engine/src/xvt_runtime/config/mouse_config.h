#ifndef XVT_MOUSE_CONFIG_H
#define XVT_MOUSE_CONFIG_H
#include <stdbool.h>

#include "aeron/config_file.h"

enum { XVT_MOUSE_SENSITIVITY_MIN = 1, XVT_MOUSE_SENSITIVITY_MAX = 9 };

struct xvt_mouse_options {
	int mouse_flight_enabled;
	int mouse_sensitivity;
	int mouse_invert_y;
};

/* Reads the required input.mouse_flight and input.mouse_invert_y booleans and
 * input.mouse_sensitivity, 1 to 9. On failure *options may be partly
 * written. */
bool xvt_mouse_config_parse(const AeronConfigFile *document,
			    struct xvt_mouse_options *options, char *error,
			    size_t capacity);
/* Stores options in the user overrides, dropping each one that equals the
 * shipped default, in memory only. Needs loaded settings. */
bool xvt_config_set_mouse(const struct xvt_mouse_options *options, char *error,
			  size_t capacity);
#endif
