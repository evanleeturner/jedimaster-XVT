#ifndef XVT_INPUT_WIN_MOUSE_H
#define XVT_INPUT_WIN_MOUSE_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void win_mouse_poll_state(int *position_x, int *position_y, int *delta_x,
			  int *delta_y, int *button_down, int *button_pressed,
			  int *button_released);
int win_mouse_set_position(int x, int y);
void win_mouse_set_horizontal_bounds(int min_x, int max_x);
void win_mouse_set_vertical_bounds(int min_y, int max_y);
void win_mouse_set_scale_factors(int scale_x, int scale_y);
void win_mouse_poll_position_and_buttons(int16_t *buttons, int16_t *x,
					 int16_t *y);
void win_mouse_poll_button_press(int16_t button_index, int16_t *is_down,
				 int16_t *pressed, int16_t *x, int16_t *y);
void win_mouse_poll_button_release(int16_t button_index, int16_t *is_down,
				   int16_t *released, int16_t *x, int16_t *y);
void win_mouse_poll_movement_delta(int16_t *delta_x, int16_t *delta_y);

#ifdef __cplusplus
}
#endif

#endif
