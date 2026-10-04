#ifndef XVT_FLIGHT_FLIGHT_HYPERSPACE_H
#define XVT_FLIGHT_FLIGHT_HYPERSPACE_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

void flight_hyperspace_draw_transition_effect_object(void);
void flight_hyperspace_request_transition_effect_initialization(void);
void flight_hyperspace_render_transition_effect(void);

#ifdef __cplusplus
}
#endif

#endif
