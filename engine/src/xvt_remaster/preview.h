#ifndef XVT_REMASTER_PREVIEW_H
#define XVT_REMASTER_PREVIEW_H
#include "xvt_remaster/ship.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct XvtPreviewOutput {
	AeronTexture* texture;
	uint64_t tick_index;
	XvtSnapDrawHeader draw;
	XvtSnapRect destination;
	uint8_t mask_index;
	int width, height;
} XvtPreviewOutput;

/* Render sequentially through one scene into owned per-slot presentation targets. */
int XvtRemasterPreview_Render(AeronCommandBuffer* cmd, const XvtRenderSnapshot* snapshot, int width,
							  int height);
void XvtRemasterPreview_BeginFrame(void);
const XvtPreviewOutput* XvtRemasterPreview_Output(unsigned slot);
void XvtRemasterPreview_Shutdown(void);
void XvtRemasterPreview_ReleaseFrontend(void);
AeronTexture* XvtRemasterPreview_CrtLinear(void);
int XvtRemasterPreview_CrtNeedsRender(const XvtRenderSnapshot* snapshot, int width, int height);
int XvtRemasterPreview_RenderCrt(AeronCommandBuffer* cmd, const XvtRenderSnapshot* snapshot, int width,
								 int height);
void XvtRemasterPreview_InvalidateCrt(void);
int XvtRemasterPreview_PrepareCrtResources(const XvtCockpitResources* resources, int width, int height);
#ifdef __cplusplus
}
#endif
#endif
