#ifndef XVT_RUNTIME_SNAPSHOT_COCKPIT_INSTRUMENTS_H
#define XVT_RUNTIME_SNAPSHOT_COCKPIT_INSTRUMENTS_H

#include "xvt_runtime/snapshot/cockpit_state.h"

#ifdef __cplusplus
extern "C" {
#endif
void XvtCockpitInstruments_BeginUpdate(int player);
void XvtCockpitInstruments_RecordLaserLock(unsigned slot, unsigned state);
void XvtCockpitInstruments_RecordThreats(unsigned attack, unsigned laser, unsigned beam, unsigned warhead);
void XvtCockpitInstruments_RecordRadar(int object, int front, int index, int x, int y, int color);
void XvtCockpitInstruments_CompleteRadar(void);
void XvtCockpitInstruments_Build(XvtCockpitState* state);
#ifdef __cplusplus
}
#endif
#endif
