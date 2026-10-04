#ifndef XVT_REMASTER_H
#define XVT_REMASTER_H

#include <stdint.h>

struct AeronInputSnapshot;
struct AeronTexture;

#ifdef __cplusplus
extern "C" {
#endif

/* The modern renderer's driver: initialization against the Aeron host, the
 * per-frame sequence (settings sync, view-mode input, asset synchronization,
 * flight preparation, cockpit resource loading, CRT, HUD, flight and frontend
 * rendering, presentation) and the retained outputs. */

/* Aeron and snapshot storage must outlive the initialized driver. */
/* Returns 1 at once when already initialized; 0 without a host logical size,
 * when the render settings cannot be applied, or when the asset cache fails to
 * initialize (undone again). */
int xvt_remaster_init(void);
/* Called before the port tick; scene readiness controls classic suppression. */
/* Nothing before Init. A failed settings sync requests a fatal renderer error. */
void xvt_remaster_begin_frame(const struct AeronInputSnapshot *input);
/* Consume the committed snapshot after the port tick and before Aeron_Present. */
/* Nothing before Init or without a snapshot. Advances the component animation
 * and clears the preview outputs; a changed scene kind invalidates the flight;
 * no presentation size invalidates it and returns. When the world or a loading
 * cockpit needs assets, or the frontend must replay, synchronizes the images,
 * the OPT models (64 MiB and 4096 copies per batch, repeated while previews
 * wait) and textures, and the frontend's images, in submitted batches. Prepares
 * the flight frame when the world is needed and assets are ready; prepares the
 * cockpit's loading resources; then, when the flight must render, a standalone
 * HUD changed, the frontend replays, or a source awaits retention, records the
 * CRT, the HUD, the flight or the standalone HUD, the retention, and the
 * previews and frontend replay in one command buffer and submits it. Releases
 * the frontend surfaces once the snapshot says so, marks the snapshot's assets
 * consumed, and presents through the view mode. Every failure requests a fatal
 * renderer error and returns. */
void xvt_remaster_frame(int32_t delta_us);
/* Shuts every module down; safe before Init. */
void xvt_remaster_shutdown(void);
/* Borrowed retained output; frontend/movie artwork is linear SDR content. */
/* NULL without a snapshot or in a movie scene; the frontend's presented image
 * unless the main flight target is presented, then the flight pipeline's
 * output. */
struct AeronTexture *xvt_remaster_output(void);
/* The frontend's movie overlay in a movie scene, else NULL. */
struct AeronTexture *xvt_remaster_movie_overlay(void);

#ifdef __cplusplus
}
#endif

#endif
