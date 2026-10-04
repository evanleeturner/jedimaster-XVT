#ifndef XVT_RUNTIME_SNAPSHOT_RENDER_CAPTURE_H
#define XVT_RUNTIME_SNAPSHOT_RENDER_CAPTURE_H
#include "xvt_runtime/snapshot/render_snapshot.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Flight-view capture for the renderer.
 *
 * Flow per classic frame: BeginClassicFrame, capture_view fills one pending view from the live
 * world, the Hyperspace/Crt/CrtMarker calls add to it, SealView marks it ready, and Presented
 * copies it into the writer snapshot only when the flip succeeded; EndPresentation then
 * discards it. Per host frame, BeginFrame
 * discards an unpublished view and Commit carries the previous snapshot's view forward when
 * no view was published.
 *
 * Invariants:
 *   - the mission generation rises on BeginMission; the world generation rises on every
 *     world change, network correction, and view whose game time went backward
 *   - a carried-forward view always has the current mission and world generation */

/* Nest around classic overlay drawing. Each Begin re-enables classic flight rendering; the
 * outermost also retains the cockpit's last presented frame. End never drops below zero. */
void xvt_render_capture_begin_overlay(void);
void xvt_render_capture_end_overlay(void);
/* Resets cockpit capture, message progress, the pending view, generations, serials and the
 * overlay depth, and deactivates capture. Does not reset the network pose history. */
void xvt_render_capture_reset(void);
/* Discards the pending view and the published mark; called by xvt_render_snapshot_begin_frame. */
void xvt_render_capture_begin_frame(void);
/* Both re-enable classic rendering and end with WorldChanged. BeginMission also resets HUD
 * capture, raises the mission generation and activates capture; EndMission deactivates it, so
 * commits publish no flight view. */
void xvt_render_capture_begin_mission(void);
void xvt_render_capture_end_mission(void);
/* Resets cockpit capture, raises the world generation, forgets the network pose history and
 * the last view time, discards the pending view, and clears the writer's flight and camera
 * validity. */
void xvt_render_capture_world_changed(void);
/* Returns the game time of the last published view, or -1 when none since the last world
 * change or Init. */
int xvt_render_capture_last_view_tick(void);
/* Network profile only, and only when the game time equals the authoritative tick: flags a
 * correction if any non-local slot below XVT_SNAP_OBJECTS differs from the pose recorded then
 * in type, signature, position, orientation or craft mesh rotation. */
void xvt_render_capture_check_network_correction(void);
/* Network profile only. Applies a flagged correction as a world change without the cockpit
 * reset, then records every slot's pose below XVT_SNAP_OBJECTS as the candidate for the
 * current game time. The candidate becomes authoritative when a view of that time is
 * presented. */
void xvt_render_capture_complete_network_world(void);
/* Fills the pending view from the live world: camera, lighting, sky,
 * hyperspace, each occupied slot up to the object table's capacity, the map and
 * the model types. Objects past XVT_SNAP_OBJECTS and model types with an
 * unsupported frame sequence or palette are counted as dropped. The view is
 * valid when the viewport and screen sizes are nonzero. It is left invalid and
 * empty when capture is inactive, no frame is open, the object table is missing
 * or stale, a slot range is negative, or the local player index is out of
 * range. Also sets the draw scope to world, or to map when the map is
 * active. */
void xvt_render_capture_capture_view(void);
/* Begins the cockpit and HUD frames and invalidates the pending CRT preview. */
void xvt_render_capture_begin_classic_frame(void);
/* Appends a model preview straight to the writer snapshot, not the pending view, with a
 * 640x480 camera. Does nothing without an open frame, with an empty rectangle, or with a bad
 * local player index; counts a drop when the preview list is full. The preview is valid only
 * when handle has a nonzero asset id. */
void xvt_render_capture_frontend_preview(uint16_t handle,
					 const float position[3],
					 const float orientation[9],
					 float scale, uint16_t node_switch,
					 int x, int y, int width, int height);
/* Captures the targeting CRT preview of the local player's current target into
 * the pending view. Does nothing, leaving the old preview, when capture is
 * inactive, the object table is missing, the rectangle is empty, or the local
 * player index is bad. Leaves it cleared and invalid when the target slot is
 * empty or its type is out of range. masked is ignored: the mask always follows
 * the HUD instrument set. */
void xvt_render_capture_crt(int x, int y, int width, int height, int masked);
/* Sets the CRT component marker at this offset from the CRT camera position. Does not check
 * that a CRT preview was captured. */
void xvt_render_capture_crt_marker(int x, int y, int z);
/* Copies the hyperspace streaks into the pending view. Copies nothing when the view is invalid
 * or count exceeds XVT_SNAP_STREAKS. */
void xvt_render_capture_hyperspace(unsigned count, const int *x, const int *y,
				   const int *z, const int *half_width,
				   const int *roll);
/* Returns the next draw-order number in the open frame, starting at 0. Returns 0 when no frame
 * is open, which a caller cannot tell from the first number. */
uint32_t xvt_render_snapshot_next_order(void);
/* Hands the CRT preview to xvt_cockpit_seal and marks the pending view ready if it is valid. */
void xvt_render_capture_seal_view(void);
/* Called after each flip. Does nothing when the flip failed or no frame is open. Without a
 * sealed view, inside an overlay it calls xvt_cockpit_presented(1) and routes the frontend to
 * the flight or loading scene; otherwise it does nothing. With a sealed view it copies the
 * view and the HUD into the writer snapshot, raises the flight frame serial, adds the view's
 * drops to the writer's count, and logs a warning when that count is nonzero and differs from
 * the last published view's. A later successful flip in the same frame replaces the view; a
 * failed one does not. In the network profile, a view at the candidate tick promotes the
 * candidate poses to authoritative. */
void xvt_render_capture_presented(int succeeded);
/* Discards the pending view, sealed or not. */
void xvt_render_capture_end_presentation(void);
/* Called by xvt_render_snapshot_commit. Exports the cockpit and stamps the generations and
 * serial into out. While capture is inactive it clears out's flight view. When no view was
 * published this frame, it copies previous's view, target boxes included, if previous is valid
 * and has the same mission and world generation; otherwise out keeps no flight view. */
void xvt_render_capture_commit(struct xvt_render_snapshot *out,
			       const struct xvt_render_snapshot *previous);
/* Capture-side access only; invalid outside an open host frame. */
/* Returns NULL outside an open frame. */
struct xvt_render_snapshot *xvt_render_snapshot_writer(void);
#ifdef __cplusplus
}
#endif
#endif
