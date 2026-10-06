#include "xvt/frontend/frontend_joystick.h"

#include <stdio.h>
#include <string.h>

#include "aeron/compat/mmsystem.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/input/keyboard.h"
#include "xvt_runtime/log/log.h"

/* Finds up to two joysticks through the system's joystick API and records them
 * in g_front_state, in slot order: for each device from 0 that reports its
 * capabilities and then its position, the button count, whether it has a
 * point-of-view hat, its axis ranges, its current x and y as the center, the
 * scales (center - min) / 255 and (max - center) / 255 for each axis, its
 * device id, and joystick_present 1. First clears joystick_present and sets both
 * joystick_init_flags to 1, which nothing reads, unless the system reports no
 * device at all. Returns 1 when it found one or two, else 0. The modern build
 * raises each scale under 1 to 1. */
// FUNCTION: XVT 0x4D5E70
int joystick_init_devices(void)
{
	int device_count = (int)joyGetNumDevs();
	if (device_count == 0) {
		XVT_LOG_INFO("joystick.detected joysticks=%d devices=%d", 0,
			     device_count);
		return 0;
	}
	int slot = 0;
	g_front_state.joystick_present[1] = 0;
	g_front_state.joystick_present[0] = 0;
	int initialized_count = 0;
	g_front_state.joystick_init_flags[1] = 1;
	g_front_state.joystick_init_flags[0] = 1;
	JOYINFOEX joystick_info;
	JOYCAPSA joystick_caps;
	for (int device = 0; device < device_count; ++device) {
		if (joyGetDevCapsA((uint32_t)device, &joystick_caps,
				   sizeof(joystick_caps)) == JOYERR_NOERROR) {
			g_front_state.joystick_button_count[slot] =
				(uint8_t)joystick_caps.wNumButtons;
			joystick_info.dwSize = sizeof(joystick_info);
			joystick_info.dwFlags = JOY_RETURNX | JOY_RETURNY |
						JOY_RETURNBUTTONS |
						JOY_RETURNCENTERED;
			g_front_state.joystick_has_pov[slot] =
				(joystick_caps.wCaps & JOYCAPS_HASPOV) != 0;
			if (joyGetPosEx((uint32_t)device, &joystick_info) ==
			    JOYERR_NOERROR) {
				g_front_state.joystick_x_min[slot] =
					joystick_caps.wXmin;
				g_front_state.joystick_x_max[slot] =
					joystick_caps.wXmax;
				g_front_state.joystick_y_min[slot] =
					joystick_caps.wYmin;
				g_front_state.joystick_y_max[slot] =
					joystick_caps.wYmax;
				g_front_state.joystick_x_center[slot] =
					joystick_info.dwXpos;
				g_front_state.joystick_y_center[slot] =
					joystick_info.dwYpos;
				g_front_state.joystick_x_negative_scale[slot] =
					(joystick_info.dwXpos -
					 joystick_caps.wXmin) /
					255;
				g_front_state.joystick_x_positive_scale[slot] =
					(joystick_caps.wXmax -
					 joystick_info.dwXpos) /
					255;
				g_front_state.joystick_y_negative_scale[slot] =
					(joystick_info.dwYpos -
					 joystick_caps.wYmin) /
					255;
				g_front_state.joystick_y_positive_scale[slot] =
					(joystick_caps.wYmax -
					 joystick_info.dwYpos) /
					255;
				if (g_front_state.joystick_x_negative_scale
						    [slot] < 1 ||
				    g_front_state.joystick_x_positive_scale
						    [slot] < 1 ||
				    g_front_state.joystick_y_negative_scale
						    [slot] < 1 ||
				    g_front_state.joystick_y_positive_scale
						    [slot] < 1) {
					XVT_LOG_WARN(
						"joystick.scale_raised stick=%d x_neg=%d x_pos=%d y_neg=%d y_pos=%d",
						slot,
						g_front_state
							.joystick_x_negative_scale
								[slot],
						g_front_state
							.joystick_x_positive_scale
								[slot],
						g_front_state
							.joystick_y_negative_scale
								[slot],
						g_front_state
							.joystick_y_positive_scale
								[slot]);
				}
				if (g_front_state
					    .joystick_x_negative_scale[slot] <
				    1) {
					g_front_state.joystick_x_negative_scale
						[slot] = 1;
				}
				if (g_front_state
					    .joystick_x_positive_scale[slot] <
				    1) {
					g_front_state.joystick_x_positive_scale
						[slot] = 1;
				}
				if (g_front_state
					    .joystick_y_negative_scale[slot] <
				    1) {
					g_front_state.joystick_y_negative_scale
						[slot] = 1;
				}
				if (g_front_state
					    .joystick_y_positive_scale[slot] <
				    1) {
					g_front_state.joystick_y_positive_scale
						[slot] = 1;
				}
				g_front_state.joy_device_ids[slot] =
					(unsigned int)device;
				g_front_state.joystick_present[slot] = 1;
				XVT_LOG_DEBUG(
					"joystick.found stick=%d id=%d buttons=%d pov=%d x_min=%u x_max=%u y_min=%u y_max=%u x_center=%d y_center=%d x_neg=%d x_pos=%d y_neg=%d y_pos=%d",
					slot, device,
					(int)g_front_state
						.joystick_button_count[slot],
					(int)g_front_state
						.joystick_has_pov[slot],
					(unsigned)g_front_state
						.joystick_x_min[slot],
					(unsigned)g_front_state
						.joystick_x_max[slot],
					(unsigned)g_front_state
						.joystick_y_min[slot],
					(unsigned)g_front_state
						.joystick_y_max[slot],
					g_front_state.joystick_x_center[slot],
					g_front_state.joystick_y_center[slot],
					g_front_state.joystick_x_negative_scale
						[slot],
					g_front_state.joystick_x_positive_scale
						[slot],
					g_front_state.joystick_y_negative_scale
						[slot],
					g_front_state.joystick_y_positive_scale
						[slot]);
				++initialized_count;
				++slot;
				if (slot >= 2) {
					break;
				}
			}
		}
	}
	XVT_LOG_INFO("joystick.detected joysticks=%d devices=%d",
		     initialized_count, device_count);
	return initialized_count != 0;
}

/* Reads joystick slot joy_slot's position, buttons and hat into g_front_state;
 * the frame loops call it for both slots every 100 ms. Does nothing for a slot
 * over 1 or without a joystick. Sets joystick_axis_x and joystick_axis_y to the
 * offset from the center divided by that side's scale, or 0 while the offset is
 * from -1000 to 1000. For each of 32 buttons sets joystick_button_held to 1 when
 * it is down and joystick_button_released to 1 when it is up now but was held at
 * the last read. With a hat it sets joystick_pov_direction to 0 when centered,
 * else to dwPOV / 9000 + 1, dwPOV being hundredths of a degree clockwise from
 * forward. The modern build also refuses negative slots, and a failed read
 * clears the slot's buttons and axes and marks it absent; the original build
 * does not check the read. */
// FUNCTION: XVT 0x4D5FE0
void joystick_update_state(int joy_slot)
{
	if ((unsigned int)joy_slot >= 2 ||
	    g_front_state.joystick_present[joy_slot] == 0) {
		return;
	}
	JOYINFOEX joystick_info;
	joystick_info.dwSize = sizeof(joystick_info);
	joystick_info.dwFlags = JOY_RETURNX | JOY_RETURNY | JOY_RETURNBUTTONS |
				JOY_RETURNPOV | JOY_RETURNCENTERED;
	if (joyGetPosEx(g_front_state.joy_device_ids[joy_slot],
			&joystick_info) != JOYERR_NOERROR) {
		memset(g_front_state.joystick_button_held[joy_slot], 0,
		       sizeof(g_front_state.joystick_button_held[joy_slot]));
		memset(g_front_state.joystick_button_released[joy_slot], 0,
		       sizeof(g_front_state
				      .joystick_button_released[joy_slot]));
		g_front_state.joystick_axis_x[joy_slot] = 0;
		g_front_state.joystick_axis_y[joy_slot] = 0;
		g_front_state.joystick_present[joy_slot] = 0;
		XVT_LOG_WARN("joystick.lost stick=%d id=%u", joy_slot,
			     g_front_state.joy_device_ids[joy_slot]);
		return;
	}

	int axis_delta_x = (int)joystick_info.dwXpos -
			   g_front_state.joystick_x_center[joy_slot];
	int axis_delta_y = (int)joystick_info.dwYpos -
			   g_front_state.joystick_y_center[joy_slot];
	if (axis_delta_x > 1000 || axis_delta_x < -1000) {
		if (axis_delta_x < 0) {
			g_front_state.joystick_axis_x[joy_slot] =
				axis_delta_x /
				g_front_state
					.joystick_x_negative_scale[joy_slot];
		} else {
			g_front_state.joystick_axis_x[joy_slot] =
				axis_delta_x /
				g_front_state
					.joystick_x_positive_scale[joy_slot];
		}
	} else {
		g_front_state.joystick_axis_x[joy_slot] = 0;
	}
	if (axis_delta_y > 1000 || axis_delta_y < -1000) {
		if (axis_delta_y < 0) {
			g_front_state.joystick_axis_y[joy_slot] =
				axis_delta_y /
				g_front_state
					.joystick_y_negative_scale[joy_slot];
		} else {
			g_front_state.joystick_axis_y[joy_slot] =
				axis_delta_y /
				g_front_state
					.joystick_y_positive_scale[joy_slot];
		}
	} else {
		g_front_state.joystick_axis_y[joy_slot] = 0;
	}

	unsigned int button_mask = 1;
	for (int button_index = 0; button_index < 32; ++button_index) {
		g_front_state.joystick_button_released[joy_slot][button_index] =
			(joystick_info.dwButtons & button_mask) == 0 &&
			g_front_state.joystick_button_held[joy_slot]
							  [button_index] == 1;
		if (((joystick_info.dwButtons & button_mask) != 0) !=
		    (g_front_state.joystick_button_held[joy_slot]
						       [button_index] != 0)) {
			XVT_LOG_DEBUG(
				"joystick.button stick=%d button=%d down=%d",
				joy_slot, button_index,
				(joystick_info.dwButtons & button_mask) != 0);
		}
		g_front_state.joystick_button_held[joy_slot][button_index] =
			(joystick_info.dwButtons & button_mask) != 0;
		button_mask <<= 1;
	}

	if (g_front_state.joystick_has_pov[joy_slot] != 0) {
		if (joystick_info.dwPOV == JOY_POVCENTERED) {
			if (g_front_state.joystick_pov_direction[joy_slot] !=
			    0) {
				XVT_LOG_DEBUG(
					"joystick.hat stick=%d position=%d",
					joy_slot, 0);
			}
			g_front_state.joystick_pov_direction[joy_slot] = 0;
		} else {
			if ((int)g_front_state
				    .joystick_pov_direction[joy_slot] !=
			    (int)(uint8_t)(joystick_info.dwPOV / 0x2328u) + 1) {
				XVT_LOG_DEBUG(
					"joystick.hat stick=%d position=%d",
					joy_slot,
					(int)(uint8_t)(joystick_info.dwPOV /
						       0x2328u) +
						1);
			}
			g_front_state.joystick_pov_direction[joy_slot] =
				(uint8_t)(joystick_info.dwPOV / 0x2328u) + 1;
		}
	}
}

/* Returns the lowest-numbered button, 0 to 31, held at the slot's last read, or
 * -1 when none is, the slot is over 1 or it has no joystick. */
// FUNCTION: XVT 0x4D6240
int joystick_get_first_pressed_button(int joy_slot)
{
	if ((unsigned int)joy_slot >= 2) {
		return -1;
	}
	if (g_front_state.joystick_present[joy_slot] == 0) {
		return -1;
	}

	for (int button_index = 0; button_index < 32; ++button_index) {
		if (g_front_state.joystick_button_held[joy_slot]
						      [button_index] != 0) {
			return button_index;
		}
	}
	return -1;
}

/* Returns the slot's joystick_pov_direction, 0 when the hat is centered or
 * absent, else dwPOV / 9000 + 1 from the last read; 0 for a slot over 1. Does
 * not check that a joystick is present. */
// FUNCTION: XVT 0x4D62C0
int joystick_get_pov_direction(int joy_slot)
{
	if ((unsigned int)joy_slot >= 2) {
		return 0;
	}
	return g_front_state.joystick_pov_direction[joy_slot];
}

/* Returns 1 when the slot's joystick reported a point-of-view hat, else 0; 0
 * for a slot over 1. */
// FUNCTION: XVT 0x4D62E0
int joystick_has_pov(int joy_slot)
{
	if ((unsigned int)joy_slot >= 2) {
		return 0;
	}
	return g_front_state.joystick_has_pov[joy_slot];
}

/* Returns the button count the slot's joystick reported, or 0 for a slot over
 * 1. */
// FUNCTION: XVT 0x4D6300
int joystick_get_button_count(int joy_slot)
{
	if ((unsigned int)joy_slot >= 2) {
		return 0;
	}
	return g_front_state.joystick_button_count[joy_slot];
}

/* Returns the system device id of the joystick in slot joy_slot, or 0 for a slot
 * out of range: over 1 in the modern build, over 2 in the original build, which
 * for slot 2 reads joy_device_ids[2], past the array's end. Does not check that a
 * joystick is present. */
// FUNCTION: XVT 0x4D6440
unsigned int joystick_get_device_id(int joy_slot)
{
	if ((unsigned int)joy_slot >= 2) {
		return 0;
	}
	return g_front_state.joy_device_ids[joy_slot];
}
