#ifndef XVT_RUNTIME_SNAPSHOT_COCKPIT_READOUTS_H
#define XVT_RUNTIME_SNAPSHOT_COCKPIT_READOUTS_H
#include "xvt_runtime/snapshot/cockpit_state.h"
#ifdef __cplusplus
extern "C" {
#endif
void XvtCockpitReadouts_Reset(void);
void XvtCockpitReadouts_BeginUpdate(void);
void XvtCockpitReadouts_RecordNumber(XvtCockpitNumberId id, unsigned value, unsigned width, unsigned digits);
void XvtCockpitReadouts_RecordCachedNumber(unsigned binding, unsigned value, unsigned digits);
void XvtCockpitReadouts_BeginTarget(int cmd);
void XvtCockpitReadouts_HideTarget(void);
void XvtCockpitReadouts_RecordTargetCover(unsigned binding);
void XvtCockpitReadouts_RecordArmament(unsigned index, unsigned value);
void XvtCockpitReadouts_ClearOrderRange(void);
void XvtCockpitReadouts_ClearOrderTime(void);
void XvtCockpitReadouts_ClearLauncher(unsigned launcher);
void XvtCockpitReadouts_CopyLauncher(XvtCockpitNumber* number, unsigned launcher);
void XvtCockpitReadouts_RecordCourse(int x, int y, int width, int height);
void XvtCockpitReadouts_CopyState(XvtCockpitState* state);
#ifdef __cplusplus
}
#endif
#endif
