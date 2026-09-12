#ifndef XVT_RUNTIME_SNAPSHOT_COCKPIT_CAPTURE_H
#define XVT_RUNTIME_SNAPSHOT_COCKPIT_CAPTURE_H

#include "xvt_runtime/snapshot/cockpit_state.h"

#ifdef __cplusplus
extern "C" {
#endif
void XvtCockpit_Reset(void);
void XvtCockpit_BeginFrame(void);
void XvtCockpit_RefreshInstruments(int player);
void XvtCockpit_LatchComposition(void);
void XvtCockpit_LatchPages(void);
void XvtCockpit_LatchLauncher(unsigned launcher, int x, int y, int width, int height);
void XvtCockpit_LatchMessages(void);
void XvtCockpit_BeginMessagePlacement(void);
void XvtCockpit_LatchMessage(XvtCockpitMessageId pane, int source_x, int source_y, int x, int y, int width,
							 int height);
void XvtCockpit_RetainPresentedFrame(void);
void XvtCockpit_Seal(const XvtSnapPreview* crt);
void XvtCockpit_Presented(int standalone_overlay);
void XvtCockpit_Export(XvtCockpitState* destination);
void XvtCockpit_ExportResources(XvtCockpitResources* destination);
void XvtCockpit_ResourcesPrepared(uint64_t generation);
int XvtCockpit_LoadingAssetsReady(void);
void XvtCockpit_CopyState(XvtCockpitState* destination, const XvtCockpitState* source);
#ifdef __cplusplus
}
#endif
#endif
