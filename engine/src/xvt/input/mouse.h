#ifndef XVT_INPUT_MOUSE_H
#define XVT_INPUT_MOUSE_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

int mouse_read_position_and_buttons(int16_t *x, int16_t *y);
void mouse_read_delta(int16_t *delta_x, int16_t *delta_y);
int mouse_set_position(int16_t x, int16_t y);
void mouse_set_horizontal_bounds(int16_t min_x, int16_t max_x);
void mouse_set_vertical_bounds(int16_t min_y, int16_t max_y);
void mouse_set_scale_factors(int16_t scale_x, int16_t scale_y);

#ifdef __cplusplus
}
#endif

#endif
