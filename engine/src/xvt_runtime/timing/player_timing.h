#ifndef XVT_RUNTIME_PLAYER_TIMING_H
#define XVT_RUNTIME_PLAYER_TIMING_H
#include "xvt_runtime/timing/flight_state.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	XVT_PLAYER_YAW,
	XVT_PLAYER_PITCH,
	XVT_PLAYER_ROLL,
	XVT_PLAYER_CAMERA_YAW,
	XVT_PLAYER_CAMERA_PITCH,
	XVT_PLAYER_DISTANCE,
	XVT_PLAYER_ZOOM,
	XVT_PLAYER_SLEW_YAW,
	XVT_PLAYER_SLEW_PITCH,
	XVT_PLAYER_CHANNELS
};

void XvtPlayerTiming_Reset(void);
void XvtPlayerTiming_BeginWorld(void);
void XvtPlayerTiming_ResetControls(void);
void XvtPlayerTiming_BeginControls(unsigned player);
void XvtPlayerTiming_Clear(unsigned player, unsigned channel);
int XvtPlayerTiming_Scale(unsigned player, unsigned channel, int value, unsigned elapsed, unsigned divisor);
int XvtPlayerTiming_Slew(unsigned player, unsigned channel, int difference);
unsigned XvtPlayerTiming_LockHalf(unsigned player, unsigned mode);
int XvtPlayerTiming_RecordRecovery(unsigned player, int32_t position[3]);
void XvtPlayerTiming_Recover(unsigned player);
/* Canonical schema-2 record, 116 bytes. Decode validates before installation. */
void XvtPlayerTiming_Encode(unsigned slot, XvtPlayerTimingWire* out);
int XvtPlayerTiming_Decode(const XvtPlayerTimingWire* record, int apply);
void XvtPlayerTiming_ResetShared(void);

#ifdef __cplusplus
}
#endif

#endif
