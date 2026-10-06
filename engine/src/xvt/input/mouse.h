#ifndef XVT_INPUT_MOUSE_H
#define XVT_INPUT_MOUSE_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

int mouse_read_position_and_buttons(int16_t *x, int16_t *y);
void mouse_read_delta(int16_t *delta_x, int16_t *delta_y);

#ifdef __cplusplus
}
#endif

#endif
