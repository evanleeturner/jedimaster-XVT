#ifndef XVT_REMASTER_CONFIG_H
#define XVT_REMASTER_CONFIG_H

#include "xvt_runtime/config/settings.h"
#include "xvt_runtime/config/video_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The remaster's effective render settings: the settings document's render
 * section, overlaid by the video menu's override, fitted to the device and
 * applied to the presentation (vsync, HDR, SDR gamma, paper white), the tonemap
 * and the mesh sampler. Readers take Effective() and watch Generation() for
 * change. */

/* Apply a new requested generation at a host frame boundary. */
/* On the first call or when the settings document's generation changed, applies
 * the document's render section overlaid by the last successful ApplyVideo:
 * MSAA forced to 1 under any temporal mode and halved until the color and depth
 * formats both support it; a new mesh sampler when anisotropy changed; the
 * vsync divisor and HDR output set; SDR gamma (negative means 2.2) and paper
 * white set, except on Apple; the tonemap applied; the output's actual HDR,
 * gamma and paper white read back; the generation incremented when the result
 * differs from the current settings. Otherwise only the read-back is refreshed,
 * with an increment when it changed. Returns 0 when the settings document is
 * unavailable or the apply fails (logged; a refused display change restores the
 * previous vsync and HDR, and a failed HDR restore requests a fatal renderer
 * error), else 1. */
int xvt_remaster_config_sync(void);
/* Validates requested (error written on failure), switches fullscreen when it
 * differs, applies the document's render section overlaid by requested as Sync
 * does, and on success keeps requested as the override every later Sync
 * re-applies. Returns false, with error written and the fullscreen change
 * undone (a failed undo requests a fatal renderer error), when validation or
 * the apply fails. previous is unused. */
bool xvt_remaster_config_apply_video(const struct xvt_video_settings *previous,
				     const struct xvt_video_settings *requested,
				     char *error, size_t capacity);
/* The settings last applied, or NULL before the first successful Sync or ApplyVideo. */
const struct xvt_render_settings *xvt_remaster_config_effective(void);
/* 0 before the first apply, then incremented each time the effective settings change, the read-back
 * included. */
uint64_t xvt_remaster_config_generation(void);
/* Borrowed until the next settings boundary; bind on every scene begin. */
/* The anisotropic mesh sampler, or NULL when anisotropy is off or nothing is applied yet. */
AeronSampler *xvt_remaster_config_mesh_sampler(void);
/* Destroys the sampler and forgets every applied setting and the video
 * override: Effective returns NULL and the generation restarts at 0. */
void xvt_remaster_config_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
