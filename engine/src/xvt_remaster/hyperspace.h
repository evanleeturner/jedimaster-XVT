#ifndef XVT_REMASTER_HYPERSPACE_H
#define XVT_REMASTER_HYPERSPACE_H
#include "xvt_remaster/flight.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The hyperspace transition's two looks: the streak field of the jump's first ticks, additive quads built
 * from the snapshot's streak records, and the tunnel after them, a full-screen shader whose uniforms also
 * drive a small diffuse environment cube that lights the meshes inside it. One transition at a time, in
 * static state: Prepare sets the frame up, the scene's background hook calls Draw. */

/* Clears the frame's streaks, tunnel and lighting, then returns 1 with nothing to draw unless the
 * snapshot's hyperspace phase is the transition. Creates the shaders, environment pipeline, textures and
 * sampler on first use (0 when any fails, all released) and captures scene's jittered view-projection.
 * From XVT_SNAP_HYPERSPACE_STREAK_END ticks on: fills the tunnel uniforms from the render size, view's
 * half-angle tangents and projection offsets, the time past the streak end in units of 236 ticks (the
 * original's assumed tick rate; real ticks run 250 a second), the hyperspace settings and the camera's
 * rows read as columns (right, forward, up), computes the environment cube (0 when the compute pass or a
 * face copy fails) and marks scene as the tunnel's scene.
 * Before it: builds one quad per snapshot streak, the streak's half width to each side, with a length
 * growing quadratically until tick 472 then fixed at 16000, and a shift along world -y growing linearly
 * until tick 472 then quadratically, posed by the streak's roll, in world units, widened for aspect
 * ratios past 4:3; returns 0 when the vertex buffer, allocation or upload fails, 1 with nothing to draw
 * when there are no streaks. */
int xvt_hyperspace_prepare(AeronCommandBuffer *cmd,
			   const struct xvt_render_snapshot *snapshot,
			   AeronScene3D *scene,
			   const struct xvt_render_view *view);
/* The scene's background hook. Creates the pipelines for pass's sample count on first use or a change
 * (on failure marks cmd failed and draws nothing), sets the viewport to the whole target, draws the
 * tunnel as a full-screen triangle when prepared, then the streaks additively. user is unused. */
void xvt_hyperspace_draw(AeronCommandBuffer *cmd, AeronRenderPass *pass,
			 int width, int height, void *user);
/* Releases everything and forgets the frame's state. */
void xvt_hyperspace_shutdown(void);

struct xvt_hyper_lighting {
	AeronTexture *texture;
	AeronSampler *sampler;
	float direction[3], color[3];
};

/* Only the scene prepared for this tunnel may consume its environment. */
/* Returns 1 and fills out with the environment cube and its sampler, direction (0, 1, 0) and color =
 * cap_color * brightness * highlight_strength * mesh_key_strength from the hyperspace settings, when the
 * last Prepare built the tunnel for scene; else 0 with out untouched. */
int xvt_hyperspace_lighting(AeronScene3D *scene,
			    struct xvt_hyper_lighting *out);
#ifdef __cplusplus
}
#endif
#endif
