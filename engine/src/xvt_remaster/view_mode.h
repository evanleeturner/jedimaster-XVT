#ifndef XVT_REMASTER_VIEW_MODE_H
#define XVT_REMASTER_VIEW_MODE_H
#include "aeron/aeron.h"
#include "xvt_runtime/snapshot/render_snapshot.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Decides what the window shows each frame, the classic renderer or the modern frontend, flight or
 * loading output, crossfading on the renderer shortcut; drives classic suppression (the original flight
 * renderer stops drawing while the modern one is complete) and the movie overlay. Static state. */

/* Resets the blend to classic, clears every flag and stops suppressing the renderer tab. */
void XvtRemasterView_Init(void);
/* Syncs the presentation to the window size. On the renderer shortcut (focused, not already consumed,
 * a scene other than none or a movie, a modern output present, the shortcut allowed) toggles the blend
 * target and logs it: toward modern, invalidates the flight, HUD and CRT and clears world readiness;
 * toward classic while classic flight rendering is suppressed, waits for the next classic frame
 * before fading. Suppresses the renderer tab while the shortcut is held. Suppresses classic flight
 * rendering only when focused, fully modern, not waiting, with a ready flight world at the current
 * presentation size. */
void XvtRemasterView_BeginFrame(const AeronInputSnapshot* input);
/* Whether the modern flight world must be prepared: always, except when the main flight target is
 * presented fully classic with no classic wait pending. */
int XvtRemasterView_NeedsWorld(const XvtRenderSnapshot* snapshot);
/* Whether the flight may present straight to the swapchain: a valid flight scene with a ready world of
 * this mission, fully modern, not waiting, classic suppressed, and the pipeline's direct mode enabled
 * for width x height. */
int XvtRemasterView_Direct(const XvtRenderSnapshot* snapshot, int width, int height);
/* Presents the frame. Records the world ready with its frame serial, mission and view size when
 * world_ready and the flight is valid; ends a classic wait once the classic frame serial moved. In a
 * movie scene, submits the movie overlay as a premultiplied sRGB layer when modern (suppressing the
 * classic subtitles) and returns. Otherwise, once the matching output is ready, selects the display:
 * flight when the main flight target is presented with the flight scene, loading when the main target
 * is presented otherwise, else frontend. The blend advances toward 1 when modern and ready, is held
 * at 1 while waiting for classic, and is held at 1 when not ready while classic is suppressed, which
 * also lifts the suppression and drops world readiness. At alpha 0 the frontend's presented image is
 * released and nothing is shown. With direct at full alpha, submits the direct flight presentation;
 * else submits the output as a texture layer (the frontend in the classic rect as sRGB, the flight as
 * linear sRGB, loading as linear display) tinted by alpha, then the frontend cursor. A failed
 * submission requests a fatal renderer error. */
void XvtRemasterView_Present(const XvtRenderSnapshot* snapshot, int32_t delta_us, int world_ready,
							 int direct);
/* Lifts classic suppression and the renderer-tab suppression and clears the flags. */
void XvtRemasterView_Shutdown(void);
#ifdef __cplusplus
}
#endif
#endif
