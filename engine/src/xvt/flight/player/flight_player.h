#ifndef XVT_FLIGHT_PLAYER_FLIGHT_PLAYER_H
#define XVT_FLIGHT_PLAYER_FLIGHT_PLAYER_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int16_t flight_player_has_disabled_subsystem(void);
void nullsub_8(const char *message);
void flight_player_increase_throttle_speed(int16_t step, int player_idx);
void flight_player_decrease_throttle_speed(int16_t step, int player_idx);

#ifdef __cplusplus
}
#endif

#endif
