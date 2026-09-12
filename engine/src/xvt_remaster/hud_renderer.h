#ifndef XVT_REMASTER_HUD_RENDERER_H
#define XVT_REMASTER_HUD_RENDERER_H
#include "xvt_remaster/hud_draw.h"
#include "xvt_remaster/render_math.h"

/* Call after original image synchronization has committed, with no active GPU
 * pass. The supplied CRT texture is linear HDR with transparent background.
 * Markers are complete current world-space geometry, not drawing history.
 * A NULL view supports standalone loading/alerts. Invalidate after cancellation
 * or failure of the command buffer containing prepared list uploads. */
int XvtHudRenderer_Prepare(AeronCommandBuffer* cmd, const XvtCockpitState* state, uint64_t world_generation,
						   const XvtSnapTargetBox* markers, unsigned marker_count, const XvtRenderView* view,
						   AeronTexture* crt_color, int width, int height);
void XvtHudRenderer_Draw(AeronCommandBuffer* cmd, AeronRenderPass* pass, AeronRenderTarget* target);
void XvtHudRenderer_Invalidate(void);
int XvtHudRenderer_NeedsPreparation(const XvtCockpitState* state, uint64_t world_generation,
									const XvtSnapTargetBox* markers, unsigned marker_count,
									const XvtRenderView* view, int crt_visible, int width, int height);
void XvtHudRenderer_Shutdown(void);
#endif
