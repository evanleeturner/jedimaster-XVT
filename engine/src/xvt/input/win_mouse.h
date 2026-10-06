#ifndef XVT_INPUT_WIN_MOUSE_H
#define XVT_INPUT_WIN_MOUSE_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

void win_mouse_poll_state(int *position_x, int *position_y, int *delta_x,
			  int *delta_y, int *button_down, int *button_pressed,
			  int *button_released);
void win_mouse_poll_position_and_buttons(int16_t *buttons, int16_t *x,
					 int16_t *y);
void win_mouse_poll_movement_delta(int16_t *delta_x, int16_t *delta_y);

#ifdef __cplusplus
}
#endif

#endif
