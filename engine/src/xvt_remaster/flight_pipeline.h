#ifndef XVT_REMASTER_FLIGHT_PIPELINE_H
#define XVT_REMASTER_FLIGHT_PIPELINE_H
#include "xvt_remaster/flight.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The flight scene's per-frame setup and its output path: temporal and
 * motion-blur settings, bloom, and tonemapping into a retained presentation
 * target with black bars outside the content rectangle, or straight into the
 * swapchain in direct mode; also the standalone HUD target used while loading.
 * Static state: one source, one target. */

/* Sets scene's post settings from the SSAO settings and, with motion, the motion-blur settings at
 * shutter; motion 0 turns blur off. */
void xvt_flight_pipeline_post(AeronScene3D *scene, float shutter, int motion);
/* Starts scene for the prepared frame: temporal mode and sharpness with the
 * host frame delta (16.67 ms on the first call) and history reset on reset; the
 * frame's camera; the mesh sampler; post with a shutter of the motion_blur
 * shutter times 32 ms over the frame's velocity span, 0 on reset, with no span,
 * or when paused or not regenerating motion unless pause_keep_blur; and the
 * motion context (the previous view-projection when regenerating, the current
 * one when not, none on reset). Returns 0 when the scene's Begin or the sampler
 * fails. */
int xvt_flight_pipeline_begin(AeronScene3D *scene,
			      const struct xvt_render_snapshot *snapshot,
			      const struct xvt_prepared_flight *frame,
			      int reset);
/* Renders scene and resolves its color with bloom on. Returns 0 on failure. */
int xvt_flight_pipeline_finish(AeronCommandBuffer *cmd, AeronScene3D *scene);
/* Makes color, width x height, the frame's source: ensures the present chain,
 * sampler, bloom and target (remade on a size change, dropping any source),
 * prepares the black bars from the prepared frame's content rectangle, applies
 * bloom at the bloom_intensity setting when enabled and positive, then either
 * leaves the source for direct presentation when SetDirect enabled it or
 * retains it. Returns 0 on failure. */
int xvt_flight_pipeline_resolve(AeronCommandBuffer *cmd, AeronTexture *color,
				int width, int height, int bloom);
/* The presentation target's texture once a source exists, else NULL. */
AeronTexture *xvt_flight_pipeline_output(void);
/* Releases everything and forgets the source and the direct state. */
void xvt_flight_pipeline_shutdown(void);
/* Turns direct presentation off, then on again when enabled and the swapchain
 * can be rendered to at width x height, creating the direct tonemap chain for
 * the swapchain's format (a failure requests a fatal renderer error). Returns
 * the resulting state. */
int xvt_flight_pipeline_set_direct(int enabled, int width, int height);
/* Tonemaps the source with its bloom into the presentation target and masks the
 * bars, once per source. Returns 1 when there is no source or it is already
 * retained, 0 when the pass cannot begin. */
int xvt_flight_pipeline_retain(AeronCommandBuffer *cmd);
/* Submits the swapchain layer that tonemaps the source with its bars straight
 * into the swapchain at the source's size. Returns 0 without direct mode or a
 * source. */
int xvt_flight_pipeline_submit_direct(void);
/* Whether a source is resolved, not yet retained and not for direct presentation. */
int xvt_flight_pipeline_needs_retain(void);
/* Drops the source references, for when the scene they came from is destroyed. */
void xvt_flight_pipeline_forget_sources(void);
/* Draws the HUD renderer's lists into the presentation target over black and makes that the output:
 * retained, no bloom, no bars, direct off. Returns 0 on failure. */
int xvt_flight_pipeline_draw_standalone_hud(AeronCommandBuffer *cmd, int width,
					    int height);
/* Prepares scene's GPU resources at its target size: the temporal mode (off unless flight), post
 * (motion only with flight) and a square 90-degree camera. Returns 0 on failure. */
int xvt_flight_pipeline_prepare_scene_resources(AeronScene3D *scene,
						int flight);
#ifdef __cplusplus
}
#endif
#endif
