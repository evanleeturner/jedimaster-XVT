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

/* The joystick slot the centering prompt is waiting on, 0 to 2; 2 ends it.
 * Written only by frontend_joystick_begin_centering_prompt, which sets it to 0 and
 * which nothing calls, and frontend_joystick_update_centering_prompt. */
// GLOBAL: XVT 0x665430
int g_frontend_joystick_centering_slot = 0;
/* The centering prompt's background color, the low byte of
 * frontend_display_pack_rgb(0, 0, 255). Written only by
 * frontend_joystick_begin_centering_prompt, which nothing calls. */
// GLOBAL: XVT 0x665434
uint8_t g_frontend_joystick_centering_fill_color = 0;

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
	JOYINFOEX joystick_info;
	JOYCAPSA joystick_caps;
	int device_count;
	int device;
	int slot;
	int initialized_count;

	device_count = (int)joyGetNumDevs();
	if (device_count == 0) {
		return 0;
	}
	slot = 0;
	g_front_state.joystick_present[1] = 0;
	g_front_state.joystick_present[0] = 0;
	initialized_count = 0;
	g_front_state.joystick_init_flags[1] = 1;
	g_front_state.joystick_init_flags[0] = 1;
	for (device = 0; device < device_count; ++device) {
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
#ifdef XVT_MODERN
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
#endif
				g_front_state.joy_device_ids[slot] =
					(unsigned int)device;
				g_front_state.joystick_present[slot] = 1;
				++initialized_count;
				++slot;
				if (slot >= 2) {
					break;
				}
			}
		}
	}
	return initialized_count != 0;
}

/* Only the original build calls this. Returns how many of the two slots hold a
 * joystick, 0 to 2. */
// FUNCTION: XVT 0x4D5FC0
int joystick_get_count(void)
{
	int joystick_count = 0;
	if (g_front_state.joystick_present[0] != 0) {
		joystick_count = 1;
	}
	if (g_front_state.joystick_present[1] != 0) {
		++joystick_count;
	}
	return joystick_count;
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
	JOYINFOEX joystick_info;
	int axis_delta_x;
	int axis_delta_y;
	unsigned int button_mask;
	int button_index;

#ifdef XVT_MODERN
	if ((unsigned int)joy_slot >= 2 ||
	    g_front_state.joystick_present[joy_slot] == 0) {
#else
	if (joy_slot > 1 || g_front_state.joystick_present[joy_slot] == 0) {
#endif
		return;
	}
	joystick_info.dwSize = sizeof(joystick_info);
	joystick_info.dwFlags = JOY_RETURNX | JOY_RETURNY | JOY_RETURNBUTTONS |
				JOY_RETURNPOV | JOY_RETURNCENTERED;
#ifdef XVT_MODERN
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
		return;
	}
#else
	joyGetPosEx(g_front_state.joy_device_ids[joy_slot], &joystick_info);
#endif

	axis_delta_x = (int)joystick_info.dwXpos -
		       g_front_state.joystick_x_center[joy_slot];
	axis_delta_y = (int)joystick_info.dwYpos -
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

	button_mask = 1;
	for (button_index = 0; button_index < 32; ++button_index) {
		g_front_state.joystick_button_released[joy_slot][button_index] =
			(joystick_info.dwButtons & button_mask) == 0 &&
			g_front_state.joystick_button_held[joy_slot]
							  [button_index] == 1;
		g_front_state.joystick_button_held[joy_slot][button_index] =
			(joystick_info.dwButtons & button_mask) != 0;
		button_mask <<= 1;
	}

	if (g_front_state.joystick_has_pov[joy_slot] != 0) {
		if (joystick_info.dwPOV == JOY_POVCENTERED) {
			g_front_state.joystick_pov_direction[joy_slot] = 0;
		} else {
			g_front_state.joystick_pov_direction[joy_slot] =
				(uint8_t)(joystick_info.dwPOV / 0x2328u) + 1;
		}
	}
}

/* Only frontend_joystick_update_centering_prompt calls this, and nothing reaches
 * that. Returns 1 when button 0 of the slot's joystick came up at the last read
 * and no frame has cleared the flag since; 0 for a slot over 1 or without a
 * joystick. */
// FUNCTION: XVT 0x4D61E0
int joystick_is_button0_released(int joystick_slot)
{
#ifdef XVT_MODERN
	if ((unsigned int)joystick_slot >= 2) {
#else
	if (joystick_slot > 1) {
#endif
		return 0;
	}
	if (g_front_state.joystick_present[joystick_slot] == 0) {
		return 0;
	}
	return g_front_state.joystick_button_released[joystick_slot][0];
}

/* Only frontend_joystick_update_centering_prompt calls this, and nothing reaches
 * that. Returns 1 when button 1 of the slot's joystick came up at the last read
 * and no frame has cleared the flag since; 0 for a slot over 1 or without a
 * joystick. */
// FUNCTION: XVT 0x4D6210
int joystick_is_button1_released(int joystick_slot)
{
#ifdef XVT_MODERN
	if ((unsigned int)joystick_slot >= 2) {
#else
	if (joystick_slot > 1) {
#endif
		return 0;
	}
	if (g_front_state.joystick_present[joystick_slot] == 0) {
		return 0;
	}
	return g_front_state.joystick_button_released[joystick_slot][1];
}

/* Returns the lowest-numbered button, 0 to 31, held at the slot's last read, or
 * -1 when none is, the slot is over 1 or it has no joystick. */
// FUNCTION: XVT 0x4D6240
int joystick_get_first_pressed_button(int joy_slot)
{
	int button_index;

#ifdef XVT_MODERN
	if ((unsigned int)joy_slot >= 2) {
#else
	if (joy_slot > 1) {
#endif
		return -1;
	}
	if (g_front_state.joystick_present[joy_slot] == 0) {
		return -1;
	}

	for (button_index = 0; button_index < 32; ++button_index) {
		if (g_front_state.joystick_button_held[joy_slot]
						      [button_index] != 0) {
			return button_index;
		}
	}
	return -1;
}

/* Nothing calls this. Returns the lowest-numbered button, 0 to 31, whose
 * released flag is set, or -1 when none is, the slot is over 1 or it has no
 * joystick. */
// FUNCTION: XVT 0x4D6280
int joystick_get_first_released_button(int joystick_slot)
{
	int button_index;

#ifdef XVT_MODERN
	if ((unsigned int)joystick_slot >= 2) {
#else
	if (joystick_slot > 1) {
#endif
		return -1;
	}
	if (g_front_state.joystick_present[joystick_slot] == 0) {
		return -1;
	}

	for (button_index = 0; button_index < 32; ++button_index) {
		if (g_front_state.joystick_button_released[joystick_slot]
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
#ifdef XVT_MODERN
	if ((unsigned int)joy_slot >= 2) {
#else
	if (joy_slot > 1) {
#endif
		return 0;
	}
	return g_front_state.joystick_pov_direction[joy_slot];
}

/* Returns 1 when the slot's joystick reported a point-of-view hat, else 0; 0
 * for a slot over 1. */
// FUNCTION: XVT 0x4D62E0
int joystick_has_pov(int joy_slot)
{
#ifdef XVT_MODERN
	if ((unsigned int)joy_slot >= 2) {
#else
	if (joy_slot > 1) {
#endif
		return 0;
	}
	return g_front_state.joystick_has_pov[joy_slot];
}

/* Returns the button count the slot's joystick reported, or 0 for a slot over
 * 1. */
// FUNCTION: XVT 0x4D6300
int joystick_get_button_count(int joy_slot)
{
#ifdef XVT_MODERN
	if ((unsigned int)joy_slot >= 2) {
#else
	if (joy_slot > 1) {
#endif
		return 0;
	}
	return g_front_state.joystick_button_count[joy_slot];
}

/* Nothing calls this. Sets g_frontend_joystick_centering_slot to 0 and
 * g_frontend_joystick_centering_fill_color to the low byte of
 * frontend_display_pack_rgb(0, 0, 255), then queues
 * frontend_joystick_update_centering_prompt as a screen over the rect from (120,
 * 190) to (520, 290) and returns frontend_screen_queue_push's 1. */
// FUNCTION: XVT 0x4D6320
int frontend_joystick_begin_centering_prompt(void)
{
	struct RECT screen_rect;
	int fill_color;

	screen_rect.left = 120;
	screen_rect.right = 520;
	screen_rect.top = 190;
	screen_rect.bottom = 290;
	fill_color = frontend_display_pack_rgb(0, 0, 255);
	g_frontend_joystick_centering_fill_color = (uint8_t)fill_color;
	g_frontend_joystick_centering_slot = 0;
	return frontend_screen_queue_push(
		frontend_joystick_update_centering_prompt, &screen_rect);
}

/* The centering prompt's frame function, reached only through
 * frontend_joystick_begin_centering_prompt, which nothing calls. For each slot
 * with a joystick in turn it fills the screen's rect in
 * g_frontend_joystick_centering_fill_color, draws "Center joystick <n> and press a
 * button." centered in the size-20 font in color 255, and waits for button 0 or
 * 1 to come up; empty slots are skipped. After slot 1 it flushes the typed
 * characters and pops the screen. It records no center: the centers stay what
 * joystick_init_devices read. Returns 0. Ignores frame_counter. */
// FUNCTION: XVT 0x4D6380
int frontend_joystick_update_centering_prompt(int frame_counter)
{
	int joystick_slot;
	struct RECT *screen_rect;
	char prompt_text[100];

	(void)frame_counter;
	joystick_slot = g_frontend_joystick_centering_slot;
	if (g_front_state
		    .joystick_present[g_frontend_joystick_centering_slot] ==
	    0) {
		++joystick_slot;
	} else {
		screen_rect =
			&g_front_state
				 .screen_states[g_front_state.screen_stack_top -
						1]
				 .saved_rect;
		frontend_draw_rect(screen_rect, 0, 0,
				   g_frontend_joystick_centering_fill_color, 1);
		sprintf(prompt_text, "Center joystick %d and press a button.",
			g_frontend_joystick_centering_slot + 1);
		frontend_text_draw_centered(20, prompt_text, screen_rect, 255);
		if (joystick_is_button0_released(
			    g_frontend_joystick_centering_slot) != 0 ||
		    joystick_is_button1_released(
			    g_frontend_joystick_centering_slot) != 0) {
			joystick_slot = g_frontend_joystick_centering_slot + 1;
		} else {
			joystick_slot = g_frontend_joystick_centering_slot;
		}
	}
	g_frontend_joystick_centering_slot = joystick_slot;
	if (joystick_slot >= 2) {
		keyboard_flush_char_buffer();
		frontend_screen_pop_state();
	}
	return 0;
}

/* Returns the system device id of the joystick in slot joy_slot, or 0 for a slot
 * out of range: over 1 in the modern build, over 2 in the original build, which
 * for slot 2 reads joy_device_ids[2], past the array's end. Does not check that a
 * joystick is present. */
// FUNCTION: XVT 0x4D6440
unsigned int joystick_get_device_id(int joy_slot)
{
#ifdef XVT_MODERN
	if ((unsigned int)joy_slot >= 2) {
#else
	if (joy_slot > 2) {
#endif
		return 0;
	}
	return g_front_state.joy_device_ids[joy_slot];
}
