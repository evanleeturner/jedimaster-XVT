#ifndef XVT_REMASTER_CRT_H
#define XVT_REMASTER_CRT_H
#include "xvt_remaster/preview.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The cockpit's CRT screen: a rendered preview texture drawn through one of the cockpit layout's three
 * run-length masks into the HUD pass. PrepareResources builds the pipeline once and the masks whenever
 * the layout changes; PrepareView places the quad for one frame; Draw records it. All state is static:
 * one CRT at a time. */

/* Places the CRT for this frame: preview's destination rect, scaled by scale and shifted by offset_x and
 * offset_y, over a width x height target, drawn through the mask at preview->mask_index. Returns 1 with
 * no CRT to draw when preview or color is NULL. Returns 0, also leaving nothing to draw, for a target or
 * destination size that is not positive, a destination larger than the preview camera's screen, a mask
 * index of 3 or more, a mask byte count over 480, or a mask slot that PrepareResources has not built
 * from definition's bytes at exactly the destination's size. */
int XvtCrt_PrepareView(const struct XvtSnapPreview *preview,
		       const struct XvtSnapCockpitLayout *layout,
		       AeronTexture *color, int width, int height, float scale,
		       float offset_x, float offset_y);
/* Records the quad the last PrepareView placed into pass, premultiplied-alpha blended, the color sampled
 * linearly and the mask by nearest texel, over the whole target. Does nothing when the last PrepareView
 * left nothing to draw. */
void XvtCrt_Draw(AeronRenderPass *pass);
/* Destroys the masks, pipeline, shaders and samplers and forgets the placed CRT. */
void XvtCrt_Shutdown(void);
/* Creates the pipeline, shaders and samplers on the first call, then decodes each of the layout's three
 * masks whose size is set (the layout's element 2 of each instrument set gives it: selector as width,
 * color_index as height) into an alpha texture when the held one differs in size or bytes; no bytes mean
 * a fully open mask. A slot with no size keeps whatever it held. Returns 0 when a GPU resource or buffer
 * cannot be created, a mask's byte count exceeds the layout's 480-byte field, or its run-length data
 * ends early. */
int XvtCrt_PrepareResources(AeronCommandBuffer *cmd,
			    const struct XvtCockpitResources *resources);
#ifdef __cplusplus
}
#endif
#endif
