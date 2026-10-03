#ifndef XVT_REMASTER_LIGHTING_H
#define XVT_REMASTER_LIGHTING_H
#include "xvt_remaster/ship.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Scene lighting for one flight frame: the directional shadow from the snapshot's light direction, the
 * clustered point-light setup from the point-light settings, and the point lights objects emit
 * (explosions and projectiles here; engine glows add theirs through AddPoint). The shading environment
 * itself (sun, ambient, SSAO and point-light parameters) is XvtRemasterShip_SetEnvironment, defined in
 * this file's source and declared in ship.h. Every call reads the effective render settings, which must
 * exist. */

/* Configures scene for the frame. The directional shadow takes the scene.shadows settings, is enabled
 * only when the setting and the snapshot's directional_enabled both are, points along the snapshot's
 * Q15 direction normalized, and is centered on the camera; an active hyperspace tunnel replaces the
 * direction with the tunnel's and ignores the snapshot's flag; a zero direction disables it. Then the
 * clustered point lights are set from point_lights; returns 0 when the scene rejects them (depth slices
 * outside 4 to 64, or a negative or non-finite minimum distance or cap), the shadow already set. With
 * point lights enabled, adds one light per non-static object that is an explosion (the original's frame
 * curve, scaled up by light_scale from 4, times 8, in OpenTIE's explosion color, no minimum range) or a
 * projectile (XWA's color per object type; intensity 200 for lasers and countermeasures, 250 for
 * torpedoes, pulses, missiles, bombs and rockets; other types give no light), placed relative to the
 * camera. Returns 1. */
int XvtLighting_Begin(AeronScene3D *scene,
		      const struct XvtRenderSnapshot *snapshot);
/* Adds one point light at position (relative to the scene origin): color times intensity times the
 * point_lights scale setting, radius = the larger of minimum_range and intensity * 50, times the
 * range_scale setting. Does nothing when point lights are disabled, intensity is not positive or not
 * finite, the scale setting is not positive, or any resulting value is not positive or not finite. The
 * scene drops lights past its cap with a once-per-frame warning; the drop is not reported here. */
void XvtLighting_AddPoint(AeronScene3D *scene, const float position[3],
			  const float color[3], float intensity,
			  float minimum_range);
#ifdef __cplusplus
}
#endif
#endif
