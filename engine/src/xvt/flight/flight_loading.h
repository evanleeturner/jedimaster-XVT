#ifndef XVT_FLIGHT_FLIGHT_LOADING_H
#define XVT_FLIGHT_FLIGHT_LOADING_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void flight_loading_reset_progress_state(void);
extern uint32_t g_flight_loading_progress_step;
void flight_loading_pulse_and_draw_progress_screen(void);
void flight_loading_draw_progress_to_completion(void);
int pilot_data_has_network_player_dpid(int dpid);

#ifdef __cplusplus
}
#endif

#endif
