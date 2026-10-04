#ifndef XVT_INPUT_JOYSTICK_H
#define XVT_INPUT_JOYSTICK_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct joystick_calibration {
	int axis_range_x; /* wXmax - wXmin; 1 with no device. */
	int axis_range_y; /* wYmax - wYmin; 1 with no device. */
	int axis_range_z; /* wZmax - wZmin; 1 with no device. */
	/* (axis_range_x >> 1) - wXmax, added to a raw X before scaling; 0 with no
	 * device. */
	int axis_normalize_offset_x;
	/* (axis_range_y >> 1) - wYmax; 0 with no device. */
	int axis_normalize_offset_y;
	/* (axis_range_z >> 1) - wZmax; 0 with no device. */
	int axis_normalize_offset_z;
	/* axis_range_x / 20: a raw X no further than this from g_joy_axis_center_x
	 * reads 0. */
	int axis_deadzone_x;
	int axis_deadzone_y; /* axis_range_y / 20, the same for Y. */
	int axis_deadzone_z; /* axis_range_z / 20, the same for Z. */
	int has_pov; /* 1 when the capabilities carry JOYCAPS_HASPOV, else 0. */
};

extern int g_joystick_calibration_initialized[2];
extern struct joystick_calibration g_joystick_calibration;
extern int g_joy_axis_center_z;
extern unsigned int g_joy_device_id[2];
extern int g_joy_axis_center_x;
extern int g_joy_axis_center_y;
extern int g_joystick_active;
extern int g_joy_device_index;

void joystick_poll_scaled_axes(int device_index, int *p_axis_x, int *p_axis_y,
			       int *p_axis_z, int *p_buttons);
int16_t joystick_initialize_backend_stub(void);
int joystick_poll_scaled_axes_if_active(int *p_axis_x, int *p_axis_y,
					int *p_axis_z, int *p_axis_r);

#ifdef __cplusplus
}
#endif

#endif
