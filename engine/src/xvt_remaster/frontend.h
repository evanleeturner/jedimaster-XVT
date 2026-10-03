#ifndef XVT_REMASTER_FRONTEND_H
#define XVT_REMASTER_FRONTEND_H
#include "xvt_remaster/ui_draw.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Replays the original frontend's 2D drawing (sprites, glyphs, paint, surface copies, surface events and
 * model previews, merged by z-order) into retained surfaces: back, offscreen, backup, presented, movie
 * and the saved screen-stack images, at the 640 x 480 frame's fit into the window. The cursor is kept
 * as a layer of its own. Static state. */

/* Whether the snapshot carries frontend drawing not yet replayed for its serial (a frontend-scoped sprite,
 * glyph, paint or copy, a frontend surface event, or any preview), or, after a first replay, the fitted
 * frame size changed while the main flight target is not the presented one. */
int XvtFrontend_NeedsReplay(const XvtRenderSnapshot *snapshot, int width,
			    int height);
/* Whether a frontend-scoped sprite lacks its committed frontend image variant. */
int XvtFrontend_AssetsNeedPreparation(const XvtRenderSnapshot *snapshot);
/* Builds the frontend image variant of every frontend-scoped sprite. Returns 0, marking cmd failed,
 * when one fails. */
int XvtFrontend_PrepareAssets(AeronCommandBuffer *cmd,
			      const XvtRenderSnapshot *snapshot);
/* Sizes the surfaces to the 640 x 480 fit (copying existing ones over) and replays the snapshot's
 * frontend drawing in z-order into their targets, once per snapshot. The cursor sprite is kept aside and
 * baked into its own target at each present; a present copies the back surface into the presented one;
 * a movie present only marks the movie surface shown; a reset clears every working surface but the
 * presented and saved ones; another event clears its target to the event's color; a copy moves a
 * region between targets, a saved slot taking the destination bounds as its own and being destroyed
 * when copied out of. Returns 0 when a surface cannot be made, a sprite has no image, or a copy
 * fails. */
int XvtFrontend_Replay(AeronCommandBuffer *cmd,
		       const XvtRenderSnapshot *snapshot, int width,
		       int height);
/* The presented surface's texture after a present, else NULL. */
AeronTexture *XvtFrontend_Output(void);
/* The movie surface's texture after a movie present, else NULL. */
AeronTexture *XvtFrontend_MovieOverlay(void);
/* Submits the baked cursor as a premultiplied sRGB layer in the classic rect at the cursor's position
 * (the live frontend cursor position at full opacity), cut to its clip within 640 x 480, tinted by
 * opacity. Nothing without a presented frame and a baked cursor, at zero opacity, with input captured,
 * the debug UI visible, or relative mouse mode. A failed submission requests a fatal renderer error. */
void XvtFrontend_PresentCursor(float opacity);
/* Destroys every surface, the draw list and the cursor. */
void XvtFrontend_Shutdown(void);
/* Retire working surfaces at launch, then the held image after presentation switches owners. */
/* Destroys every surface but the presented one and the saved slots, and the draw list, and arms
 * ReleasePresented. */
void XvtFrontend_ReleaseForFlight(void);
/* After ReleaseForFlight, destroys the presented surface and the cursor; otherwise nothing. */
void XvtFrontend_ReleasePresented(void);
#ifdef __cplusplus
}
#endif
#endif
