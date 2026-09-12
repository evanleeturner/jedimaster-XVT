#ifndef XVT_FLIGHT_PLAYER_FLIGHT_PLAYER_H
#define XVT_FLIGHT_PLAYER_FLIGHT_PLAYER_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int16_t FlightPlayer_HasDisabledSubsystem(void);
void nullsub_8(const char* message);
void FlightPlayer_IncreaseThrottleSpeed(int16_t step, int playerIdx);
void FlightPlayer_DecreaseThrottleSpeed(int16_t step, int playerIdx);

#ifdef __cplusplus
}
#endif

#endif
