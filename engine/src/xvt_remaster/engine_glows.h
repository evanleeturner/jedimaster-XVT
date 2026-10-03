#ifndef XVT_REMASTER_ENGINE_GLOWS_H
#define XVT_REMASTER_ENGINE_GLOWS_H
#include "xvt_remaster/ship.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Engine glows for a craft's mesh: each OPT engine-glow emitter becomes an overlay billboard built as the
 * original's projected quad (a rectangle for an elongated emitter, a view-aligned diamond for a round
 * one, extruded along its look axis) over a procedural radial coverage mask, XvT having no glow atlas;
 * large emitters also add point lights. */

/* For a craft object whose mesh has emitters, else nothing. Adds a point light per enabled, visible
 * emitter wider or taller than 2000 units while the engines work and the craft has power (16 less the
 * laser, shield and beam redirects, times overdrive_off), in the emitter's core color. Then, with draw,
 * a positive glow scale and a positive engine_emissive_strength setting, submits one billboard per
 * enabled, visible emitter that lies in front of the eye and spans at least a pixel at the classic
 * focal length. The glow scale is throttle times power with a per-tick flicker, never under 0.35, or,
 * entering or leaving hyperspace, 1 at max_speed rising to 12 across a window of 4 * (3600 -
 * max_speed), clamped to 0..12; it is 0 without engines, engine output or a craft record. Colors are
 * the emitter's authored sRGB core and outer colors,
 * linearized, premultiplied and scaled by engine_emissive_strength. Returns 0 only when the coverage
 * mask cannot be created on first use; otherwise 1, including when nothing applies. */
int XvtEngineGlows_Submit(AeronCommandBuffer *cmd, AeronScene3D *scene,
			  const struct XvtRenderSnapshot *snapshot,
			  const struct XvtSnapObject *object,
			  const struct XvtMeshAsset *asset,
			  const AeronSceneMeshTable *table,
			  const float transform[16], int draw);
/* Destroys the coverage mask. */
void XvtEngineGlows_Shutdown(void);
#ifdef __cplusplus
}
#endif
#endif
