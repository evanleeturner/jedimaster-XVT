#ifndef XVT_RUNTIME_SNAPSHOT_COCKPIT_MESSAGES_H
#define XVT_RUNTIME_SNAPSHOT_COCKPIT_MESSAGES_H
#include "xvt_runtime/snapshot/cockpit_state.h"
#ifdef __cplusplus
extern "C" {
#endif
void XvtCockpitMessages_Reset(void);
void XvtCockpitMessages_ResetWorking(void);
void XvtCockpitMessages_BeginFlightFrame(void);
void XvtCockpitMessages_BeginMessage(int pane_type);
void XvtCockpitMessages_RecordReveal(unsigned characters);
void XvtCockpitMessages_EndMessage(void);
void XvtCockpitMessages_Clear(XvtCockpitMessageId pane);
void XvtCockpitMessages_BeginPlacement(void);
void XvtCockpitMessages_Latch(XvtCockpitMessageId pane, int source_x, int source_y, int x, int y, int width,
							  int height);
void XvtCockpitMessages_RecordGlyph(unsigned character, unsigned advance, unsigned height, int narrow);
void XvtCockpitMessages_BeginAlert(void);
void XvtCockpitMessages_BeginAlertLine(int mode, int x, int y, int width, int height);
void XvtCockpitMessages_EndAlertLine(void);
void XvtCockpitMessages_EndAlert(void);
void XvtCockpitMessages_RecordProgress(unsigned step, int x, int y, int width, int height, int filled_width);
void XvtCockpitMessages_ClearProgress(void);
void XvtCockpitMessages_BeginLoadingText(void);
void XvtCockpitMessages_EndLoadingText(void);
void XvtCockpitMessages_Export(XvtCockpitState* state);
#ifdef __cplusplus
}
#endif
#endif
