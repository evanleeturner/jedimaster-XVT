#ifndef XVT_RUNTIME_FLIGHT_FRAME_H
#define XVT_RUNTIME_FLIGHT_FRAME_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void XvtFlightFrame_Begin(void);

typedef enum XvtFlightReplayResult {
	XVT_REPLAY_PENDING,
	XVT_REPLAY_IDLE,
	XVT_REPLAY_ADVANCED,
	XVT_REPLAY_TERMINAL
} XvtFlightReplayResult;

XvtFlightReplayResult XvtFlightFrame_ReplayBuffered(void);
void XvtFlightFrame_ResetReplay(void);
int XvtFlightFrame_Tick(void);
uint64_t XvtFlightFrame_NextWakeDelayUs(void);
uint64_t XvtFlightTime_DelayForTicks(unsigned int ticks);
#ifdef __cplusplus
}
#endif
#endif
