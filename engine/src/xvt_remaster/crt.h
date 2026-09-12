#ifndef XVT_REMASTER_CRT_H
#define XVT_REMASTER_CRT_H
#include "xvt_remaster/preview.h"
#ifdef __cplusplus
extern "C" {
#endif
int XvtCrt_PrepareView(const XvtSnapPreview* preview, const XvtSnapCockpitLayout* definition,
					   AeronTexture* color, int width, int height, float scale, float offset_x,
					   float offset_y);
void XvtCrt_Draw(AeronRenderPass* pass);
void XvtCrt_Shutdown(void);
int XvtCrt_PrepareResources(AeronCommandBuffer* cmd, const XvtCockpitResources* resources);
#ifdef __cplusplus
}
#endif
#endif
