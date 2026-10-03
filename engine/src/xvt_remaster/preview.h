#ifndef XVT_REMASTER_PREVIEW_H
#define XVT_REMASTER_PREVIEW_H
#include "xvt_remaster/ship.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Renders the frontend's model previews and the cockpit CRT's target preview through small scenes: the
 * previews into fixed 2048 x 1536 tonemapped targets, one per snapshot preview slot; the CRT into a
 * scene sized to its mask at the current fit, rendered again only when a dependency changes. */

/* texture: the rendered image. snapshot_serial: the snapshot it came from. draw, destination, mask_index: the
 * preview's placement. width, height: the texture size. */
struct xvt_preview_output {
	AeronTexture *texture;
	uint64_t snapshot_serial;
	struct xvt_snap_draw_header draw;
	struct xvt_snap_rect destination;
	uint8_t mask_index;
	int width, height;
};

/* Render sequentially through one scene into owned per-slot presentation targets. */
/* Clears the slot outputs, then renders each snapshot preview that is valid with a positive destination
 * and a resident mesh: the mesh at the preview's view pose and model scale, a camera from its
 * perspective and center, lit by its lighting through the preview camera, tonemapped into the slot's
 * target. Returns 0 when a scene, target, chain, sampler or render fails. */
int xvt_remaster_preview_render(AeronCommandBuffer *cmd,
				const struct xvt_render_snapshot *snapshot,
				int width, int height);
/* Clears every slot output; not the CRT's. */
void xvt_remaster_preview_begin_frame(void);
/* slot's output when it holds a texture (the CRT is slot XVT_SNAP_PREVIEWS), else NULL. */
const struct xvt_preview_output *xvt_remaster_preview_output(unsigned slot);
/* Releases everything. */
void xvt_remaster_preview_shutdown(void);
/* Releases the preview scene, chain, sampler and slot targets, keeping the CRT. */
void xvt_remaster_preview_release_frontend(void);
/* The CRT's rendered color texture, or NULL while invalid. */
AeronTexture *xvt_remaster_preview_crt_linear(void);
/* Whether the CRT must be rendered: 0, invalidating, without a valid CRT preview or its object in the
 * snapshot; else whether no valid render exists or a dependency changed: the world, mission, config,
 * model and texture generations; the object's component pose revision in an unlocked flight; the
 * preview record less its draw header, marker, component and destination origin; the lighting, types,
 * fuselage sequence, craft slot end, checkpoint, debris and proving-ground flags; the size; and the
 * records of the target object, its and the player's projectiles, and every explosion. */
int xvt_remaster_preview_crt_needs_render(
	const struct xvt_render_snapshot *snapshot, int width, int height);
/* Renders the CRT when needed: the target object's mesh (a projectile roll-aligned) with its animated
 * components, the projectiles it fired and, outside map mode, the player's, and the effects, through a
 * view from the preview camera at its destination size into the CRT scene of the fitted mask size; the
 * dependencies are recorded on success. Returns 1 when not needed; 0, invalidating, on failure. */
int xvt_remaster_preview_render_crt(AeronCommandBuffer *cmd,
				    const struct xvt_render_snapshot *snapshot,
				    int width, int height);
/* Forgets the CRT render and its output. */
void xvt_remaster_preview_invalidate_crt(void);
/* Creates one CRT scene per distinct mask size among the layout's three instrument sets, scaled by the
 * fit of the view's screen into width x height and at the MSAA setting, preparing a new scene's
 * resources; invalidates the CRT when a scene changes; destroys scenes no longer needed. Returns 0 for
 * a zero screen size or a failed scene or preparation. */
int xvt_remaster_preview_prepare_crt_resources(
	const struct xvt_cockpit_resources *resources, int width, int height);
#ifdef __cplusplus
}
#endif
#endif
