#ifndef XVT_REMASTER_SKY_H
#define XVT_REMASTER_SKY_H
#include "xvt_remaster/flight.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The flight scene's background: nothing, a cubemap or the starfield by the sky settings, plus the
 * mission's backdrop billboards (planets and suns) on the sky sphere; a hyperspace transition replaces
 * all of it with the hyperspace draw. Static state, one scene at a time. */

/* Sets the frame's background on scene: clears the sky cube, installs the before-opaque hook that draws
 * the hyperspace or the stars, and prepares the hyperspace (0 when that fails); in a transition that
 * is all, returning 1. Otherwise, with the sky enabled: in cube mode, loads the cubemap at the
 * settings' path when the path changed (0 when loading fails, the old cube kept) and sets it with the
 * exposure; in star mode, creates the starfield on first use and prepares it with the exposure,
 * star brightness, view's classic pixel scale and the snapshot's star density (0 when either fails).
 * Then, when the snapshot enables backdrops, adds one sky-stage billboard per backdrop record, taken in
 * order across the six cube faces by the per-face counts, whose direction (the face axis at 8 and the
 * record's two signed 3-bit offsets on the other axes, normalized) has positive view depth, at
 * distance 65536, sized from its type's atlas frame as an angle at the camera's focal length and
 * aspect; a record whose type has no committed atlas is skipped. Returns 1. */
int XvtSky_Prepare(AeronCommandBuffer* cmd, AeronScene3D* scene, const XvtRenderSnapshot* snapshot,
				   const XvtRenderView* view);
/* Destroys the starfield, the cubemap and the hyperspace. */
void XvtSky_Shutdown(void);
#ifdef __cplusplus
}
#endif
#endif
