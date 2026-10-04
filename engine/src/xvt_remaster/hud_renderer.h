#ifndef XVT_REMASTER_HUD_RENDERER_H
#define XVT_REMASTER_HUD_RENDERER_H
#include "xvt_remaster/hud_draw.h"
#include "xvt_remaster/render_math.h"
/* Builds and draws the semantic cockpit: two 2D draw lists, before and after
 * the CRT, prepared once per distinct key (the state's generations, the view,
 * the markers, the size and the CRT visibility) and replayed until the key
 * changes. Static state: one prepared HUD. */

/* Call after original image synchronization has committed, with no active GPU
 * pass. The supplied CRT texture is linear HDR with transparent background.
 * Markers are complete current world-space geometry, not drawing history.
 * A NULL view supports standalone loading/alerts. Invalidate after cancellation
 * or failure of the command buffer containing prepared list uploads. */
/* Returns 0 for a NULL cmd or state, a size that is not positive, more than
 * XVT_SNAP_TARGET_BOXES markers or a count without markers; and 0 marking cmd
 * failed when a list cannot be created or the artwork selection, the CRT
 * placement, a pane or a list upload fails. An invalid state invalidates and
 * returns 1; a changed world generation invalidates first. With a view the
 * layout is compiled and cached, without one only its source size is set. The
 * CRT is placed from the state's preview when crt_color is given. When the key
 * equals the last prepared one the lists stand; otherwise they are rebuilt in
 * order: world markers, base artwork, covers, radar, widgets, readouts before
 * the CRT, the CRT marker when visible, the mouse stick, readouts after the
 * CRT, messages, pages, overlays. Returns 1 ready to draw. */
int xvt_hud_renderer_prepare(AeronCommandBuffer *cmd,
			     const struct xvt_cockpit_state *state,
			     uint64_t world_generation,
			     const struct xvt_snap_target_box *markers,
			     unsigned marker_count,
			     const struct xvt_render_view *view,
			     AeronTexture *crt_color, int width, int height);
/* Renders the before list, the CRT, then the after list into pass; nothing
 * unless the last Prepare left the HUD ready. */
void xvt_hud_renderer_draw(AeronCommandBuffer *cmd, AeronRenderPass *pass,
			   AeronRenderTarget *target);
/* Forgets the prepared lists and the cached layout; the next Prepare rebuilds. */
void xvt_hud_renderer_invalidate(void);
/* Whether Prepare would rebuild: 1 for a NULL state or too many markers; for an
 * invalid state, whether a HUD is still ready (so a Prepare can clear it); else
 * whether nothing is prepared, the world generation changed, or the key
 * differs. */
int xvt_hud_renderer_needs_preparation(
	const struct xvt_cockpit_state *state, uint64_t world_generation,
	const struct xvt_snap_target_box *markers, unsigned marker_count,
	const struct xvt_render_view *view, int crt_visible, int width,
	int height);
/* Destroys the lists, the HUD assets and the CRT, and invalidates. */
void xvt_hud_renderer_shutdown(void);
#endif
