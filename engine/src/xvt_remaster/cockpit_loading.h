#ifndef XVT_REMASTER_COCKPIT_LOADING_H
#define XVT_REMASTER_COCKPIT_LOADING_H
#include "xvt_runtime/snapshot/render_snapshot.h"
/* Prepares the cockpit's resident GPU resources while a mission loads: the HUD artwork, map icons for
 * every view palette, the CRT masks and scenes, and the flight and map scenes, in one command buffer,
 * once per image generation, resource generation, size and MSAA setting. */

/* Retires the HUD assets and invalidates the HUD when the image asset generation changed. Returns 1
 * doing nothing more for invalid cockpit resources, or when nothing changed since the last
 * preparation. Otherwise prepares everything in one command buffer, submits it, commits the HUD assets
 * and images, marks the cockpit resources prepared for their generation and logs the time at DEBUG.
 * Returns 0, aborting the pending HUD and image assets, when no command buffer can be acquired, a
 * preparation fails (the buffer cancelled), or submission fails. */
int XvtCockpitLoading_Prepare(const XvtRenderSnapshot* snapshot, int width, int height);
/* Forgets what was prepared, so the next Prepare does it all again. */
void XvtCockpitLoading_Reset(void);
#endif
