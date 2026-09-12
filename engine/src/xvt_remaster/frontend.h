#ifndef XVT_REMASTER_FRONTEND_H
#define XVT_REMASTER_FRONTEND_H
#include "xvt_remaster/ui_draw.h"
#ifdef __cplusplus
extern "C" {
#endif
int XvtFrontend_NeedsReplay(const XvtRenderSnapshot* snapshot, int width, int height);
int XvtFrontend_AssetsNeedPreparation(const XvtRenderSnapshot* snapshot);
int XvtFrontend_PrepareAssets(AeronCommandBuffer* cmd, const XvtRenderSnapshot* snapshot);
int XvtFrontend_Replay(AeronCommandBuffer* cmd, const XvtRenderSnapshot* snapshot, int width, int height);
AeronTexture* XvtFrontend_Output(void);
AeronTexture* XvtFrontend_MovieOverlay(void);
void XvtFrontend_PresentCursor(float opacity);
void XvtFrontend_Shutdown(void);
/* Retire working surfaces at launch, then the held image after presentation switches owners. */
void XvtFrontend_ReleaseForFlight(void);
void XvtFrontend_ReleasePresented(void);
#ifdef __cplusplus
}
#endif
#endif
