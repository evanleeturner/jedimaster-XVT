#include "xvt/input/joystick.h"

#include <string.h>

#include "aeron/compat/mmsystem.h"
#include "xvt/frontend/frontend_joystick.h"
#include "xvt_runtime/log/log_both_builds.h"

/* Per joystick index, 0 and 1: 1 once joystick_poll_scaled_axes has calibrated
 * that index. Only that function writes it, and nothing sets it back to 0. */
// GLOBAL: XVT 0x527F48
int g_joystick_calibration_initialized[2] = {0, 0};
/* Axis calibration joystick_poll_scaled_axes takes from the device's capabilities
 * on each index's first poll: ranges 1 and the rest 0 until then, and again
 * when no device answers. One copy serves both indexes, so calibrating index 1
 * rewrites what index 0 stored. */
// GLOBAL: XVT 0x527F50
struct joystick_calibration g_joystick_calibration = {1, 1, 1, 0, 0,
						      0, 0, 0, 0, 0};
/* Raw Z position that counts as centered: (range >> 1) + wZmin from the
 * device's capabilities. Written only by joystick_poll_scaled_axes when it
 * calibrates and a device answers; 0 before. */
// GLOBAL: XVT 0x622CA0
int g_joy_axis_center_z = 0;
/* WinMM joystick id each index, 0 and 1, reads: what joystick_get_device_id(0)
 * returned, or joystick_get_device_id(1) when the first had no capabilities.
 * Written only by joystick_poll_scaled_axes when it calibrates; stays 0 when
 * neither answers. */
// GLOBAL: XVT 0x622CA8
unsigned int g_joy_device_id[2] = {0, 0};
/* Raw X position that counts as centered: (range >> 1) + wXmin. Written like
 * g_joy_axis_center_z. */
// GLOBAL: XVT 0x622CB4
int g_joy_axis_center_x = 0;
/* Raw Y position that counts as centered: (range >> 1) + wYmin. Written like
 * g_joy_axis_center_z. */
// GLOBAL: XVT 0x622CB8
int g_joy_axis_center_y = 0;
/* 1 when the joystick detection found a joystick, else 0. Written only by
 * input_initialize_joystick_backend and input_detect_active_joystick, the first
 * time either runs; joystick_poll_scaled_axes_if_active polls only while it is
 * set. */
// GLOBAL: XVT 0x5280F8
int g_joystick_active = 0;
/* Joystick index, 0 or 1, that joystick_poll_scaled_axes_if_active polls: the first
 * index input_probe_active_joystick_devices found answering. Only that function
 * writes it. */
// GLOBAL: XVT 0x5280FC
int g_joy_device_index = 0;

/* Reads joystick index 0 or 1 (any nonzero deviceIndex counts as 1) through
 * WinMM. On an index's first call it calibrates: it asks joyGetDevCapsA for the
 * device joystick_get_device_id(0) names, then for joystick_get_device_id(1),
 * whatever the index, and from the first that answers stores the device in
 * g_joy_device_id and the ranges (max - min), deadzones (range / 20), normalize
 * offsets ((range >> 1) - max) and hat flag in g_joystick_calibration, and the
 * centers in g_joy_axis_center_x, Y and Z; with neither answering it sets the
 * ranges to 1 and the rest of g_joystick_calibration to 0. Then it reads
 * joyGetPosEx. An axis whose distance from its center is over its deadzone
 * gives (int)(255u * (offset + position) / range), worked in unsigned
 * arithmetic; any other gives 0, and Z also needs a range over 0. A position
 * under max - (range >> 1) wraps in that arithmetic and gives a large positive
 * value, not a negative one: for a device reporting 0 to 65535, as Aeron's
 * joyGetDevCapsA does, positions under 32768 give 65409 to 65536 and the rest
 * 0 to 127. *pButtons gets the low 16 button bits, plus
 * 0x10000 << (dwPOV / 9000) when the device has a hat that is not centered.
 * When joyGetPosEx fails, the three axes are 32000 and *pButtons is 0. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4AABA0
void joystick_poll_scaled_axes(int device_index, int *p_axis_x, int *p_axis_y,
			       int *p_axis_z, int *p_buttons)
{
	device_index = device_index != 0;
	if (g_joystick_calibration_initialized[device_index] == 0) {
		g_joystick_calibration_initialized[device_index] = 1;
		JOYCAPSA joystick_caps;
		memset(&joystick_caps, 0, sizeof(joystick_caps));
		if (joyGetDevCapsA(joystick_get_device_id(0), &joystick_caps,
				   sizeof(joystick_caps)) == JOYERR_NOERROR) {
			g_joy_device_id[device_index] =
				joystick_get_device_id(0);
			g_joystick_calibration.has_pov =
				(joystick_caps.wCaps & JOYCAPS_HASPOV) != 0;
			g_joystick_calibration.axis_range_x =
				(int)(joystick_caps.wXmax -
				      joystick_caps.wXmin);
			g_joystick_calibration.axis_range_y =
				(int)(joystick_caps.wYmax -
				      joystick_caps.wYmin);
			g_joystick_calibration.axis_range_z =
				(int)(joystick_caps.wZmax -
				      joystick_caps.wZmin);
			g_joystick_calibration.axis_normalize_offset_x =
				(g_joystick_calibration.axis_range_x >> 1) -
				(int)joystick_caps.wXmax;
			g_joystick_calibration.axis_normalize_offset_y =
				(g_joystick_calibration.axis_range_y >> 1) -
				(int)joystick_caps.wYmax;
			g_joystick_calibration.axis_normalize_offset_z =
				(g_joystick_calibration.axis_range_z >> 1) -
				(int)joystick_caps.wZmax;
			g_joystick_calibration.axis_deadzone_x =
				g_joystick_calibration.axis_range_x / 20;
			g_joystick_calibration.axis_deadzone_y =
				g_joystick_calibration.axis_range_y / 20;
			g_joystick_calibration.axis_deadzone_z =
				g_joystick_calibration.axis_range_z / 20;
			g_joy_axis_center_x =
				(g_joystick_calibration.axis_range_x >> 1) +
				(int)joystick_caps.wXmin;
			g_joy_axis_center_y =
				(g_joystick_calibration.axis_range_y >> 1) +
				(int)joystick_caps.wYmin;
			g_joy_axis_center_z =
				(g_joystick_calibration.axis_range_z >> 1) +
				(int)joystick_caps.wZmin;
			XVT_LOG_DEBUG(
				"joystick.calibrated index=%d id=%u x_range=%d y_range=%d z_range=%d pov=%d",
				device_index,
				(unsigned)g_joy_device_id[device_index],
				g_joystick_calibration.axis_range_x,
				g_joystick_calibration.axis_range_y,
				g_joystick_calibration.axis_range_z,
				g_joystick_calibration.has_pov);
		} else if (joyGetDevCapsA(
				   joystick_get_device_id(1), &joystick_caps,
				   sizeof(joystick_caps)) == JOYERR_NOERROR) {
			g_joy_device_id[device_index] =
				joystick_get_device_id(1);
			g_joystick_calibration.has_pov =
				(joystick_caps.wCaps & JOYCAPS_HASPOV) != 0;
			g_joystick_calibration.axis_range_x =
				(int)(joystick_caps.wXmax -
				      joystick_caps.wXmin);
			g_joystick_calibration.axis_range_y =
				(int)(joystick_caps.wYmax -
				      joystick_caps.wYmin);
			g_joystick_calibration.axis_range_z =
				(int)(joystick_caps.wZmax -
				      joystick_caps.wZmin);
			g_joystick_calibration.axis_normalize_offset_x =
				(g_joystick_calibration.axis_range_x >> 1) -
				(int)joystick_caps.wXmax;
			g_joystick_calibration.axis_normalize_offset_y =
				(g_joystick_calibration.axis_range_y >> 1) -
				(int)joystick_caps.wYmax;
			g_joystick_calibration.axis_normalize_offset_z =
				(g_joystick_calibration.axis_range_z >> 1) -
				(int)joystick_caps.wZmax;
			g_joystick_calibration.axis_deadzone_x =
				g_joystick_calibration.axis_range_x / 20;
			g_joystick_calibration.axis_deadzone_y =
				g_joystick_calibration.axis_range_y / 20;
			g_joystick_calibration.axis_deadzone_z =
				g_joystick_calibration.axis_range_z / 20;
			g_joy_axis_center_x =
				(g_joystick_calibration.axis_range_x >> 1) +
				(int)joystick_caps.wXmin;
			g_joy_axis_center_y =
				(g_joystick_calibration.axis_range_y >> 1) +
				(int)joystick_caps.wYmin;
			g_joy_axis_center_z =
				(g_joystick_calibration.axis_range_z >> 1) +
				(int)joystick_caps.wZmin;
			XVT_LOG_DEBUG(
				"joystick.calibrated index=%d id=%u x_range=%d y_range=%d z_range=%d pov=%d",
				device_index,
				(unsigned)g_joy_device_id[device_index],
				g_joystick_calibration.axis_range_x,
				g_joystick_calibration.axis_range_y,
				g_joystick_calibration.axis_range_z,
				g_joystick_calibration.has_pov);
		} else {
			g_joystick_calibration.has_pov = 0;
			g_joystick_calibration.axis_range_x = 1;
			g_joystick_calibration.axis_range_y = 1;
			g_joystick_calibration.axis_range_z = 1;
			g_joystick_calibration.axis_normalize_offset_x = 0;
			g_joystick_calibration.axis_normalize_offset_y = 0;
			g_joystick_calibration.axis_normalize_offset_z = 0;
			g_joystick_calibration.axis_deadzone_x = 0;
			g_joystick_calibration.axis_deadzone_y = 0;
			g_joystick_calibration.axis_deadzone_z = 0;
			XVT_LOG_DEBUG("joystick.calibration_empty index=%d",
				      device_index);
		}
	}

	*p_buttons = 0;
	JOYINFOEX joystick_info;
	memset(&joystick_info, 0, sizeof(joystick_info));
	joystick_info.dwSize = sizeof(joystick_info);
	joystick_info.dwFlags = JOY_RETURNX | JOY_RETURNY | JOY_RETURNZ |
				JOY_RETURNBUTTONS | JOY_RETURNPOV |
				JOY_RETURNCENTERED;
	if (joyGetPosEx(g_joy_device_id[device_index], &joystick_info) ==
	    JOYERR_NOERROR) {
		int abs_delta = (int)joystick_info.dwXpos - g_joy_axis_center_x;
		if (abs_delta < 0) {
			abs_delta = -abs_delta;
		}
		if (abs_delta > g_joystick_calibration.axis_deadzone_x) {
			*p_axis_x = (int)(255u *
					  (g_joystick_calibration
						   .axis_normalize_offset_x +
					   joystick_info.dwXpos) /
					  g_joystick_calibration.axis_range_x);
		} else {
			*p_axis_x = 0;
		}

		abs_delta = (int)joystick_info.dwYpos - g_joy_axis_center_y;
		if (abs_delta < 0) {
			abs_delta = -abs_delta;
		}
		if (abs_delta > g_joystick_calibration.axis_deadzone_y) {
			*p_axis_y = (int)(255u *
					  (g_joystick_calibration
						   .axis_normalize_offset_y +
					   joystick_info.dwYpos) /
					  g_joystick_calibration.axis_range_y);
		} else {
			*p_axis_y = 0;
		}

		abs_delta = (int)joystick_info.dwZpos - g_joy_axis_center_z;
		if (abs_delta < 0) {
			abs_delta = -abs_delta;
		}
		if (abs_delta > g_joystick_calibration.axis_deadzone_z &&
		    g_joystick_calibration.axis_range_z > 0) {
			*p_axis_z = (int)(255u *
					  (g_joystick_calibration
						   .axis_normalize_offset_z +
					   joystick_info.dwZpos) /
					  g_joystick_calibration.axis_range_z);
		} else {
			*p_axis_z = 0;
		}

		*p_buttons = (int)(joystick_info.dwButtons & 0xffff);
		if (g_joystick_calibration.has_pov != 0 &&
		    joystick_info.dwPOV != JOY_POVCENTERED) {
			*p_buttons |= 0x10000
				      << (joystick_info.dwPOV / 0x2328u);
		}
	} else {
		*p_axis_x = 32000;
		*p_axis_y = 32000;
		*p_axis_z = 32000;
	}
}

/* Returns 1 and does nothing else. */
// FUNCTION: XVT 0x4AC830
int16_t joystick_initialize_backend_stub(void) { return 1; }

/* When g_joystick_active is 0, sets the three axes to 0 and returns 0 without
 * polling. Otherwise polls index g_joy_device_index with joystick_poll_scaled_axes
 * and returns its buttons. p_axis_r is ignored. Only the original build reaches
 * its one call, in flight_input_read; the modern build returns before it. */
// FUNCTION: XVT 0x4ACB90
int joystick_poll_scaled_axes_if_active(int *p_axis_x, int *p_axis_y,
					int *p_axis_z, const int *p_axis_r)
{
	(void)p_axis_r;

	if (g_joystick_active == 0) {
		*p_axis_x = 0;
		*p_axis_y = 0;
		*p_axis_z = 0;
		return 0;
	}

	int buttons;
	joystick_poll_scaled_axes(g_joy_device_index, p_axis_x, p_axis_y,
				  p_axis_z, &buttons);
	return buttons;
}
