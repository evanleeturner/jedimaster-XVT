#ifndef XVT_REMASTER_FLIGHT_H
#define XVT_REMASTER_FLIGHT_H
#include "xvt_remaster/render_math.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The flight view. Prepare turns the current and previous snapshots into one prepared frame: the main
 * views, the layouts, the content rectangle, every object's matrices now and before, and the flags that
 * say whether motion advanced and whether a render is due. Render draws that frame, through the map
 * renderer in map mode, else through the flight scene with the sky, lighting, meshes, engine glows and
 * effects, the HUD composited after upscale, and the pipeline's resolve. Static state: one frame. */

/* transform, previous_transform: the object's model matrices for the current and the previous snapshot,
 * equal when no previous pose exists. previous_index: the matching object's index in the previous
 * snapshot (same slot, signature and type), or -1. zero_velocity: no previous pose, so no motion. */
typedef struct XvtPreparedObject {
	float transform[16], previous_transform[16];
	int32_t previous_index;
	uint8_t zero_velocity;
} XvtPreparedObject;

/* view, previous_view: main views from the current and previous cameras, about the current camera's
 * position; equal after a reset. cockpit_layout: the fit of the original screen into the window;
 * frontend_layout: the fit of 640 x 480. content_rect: the whole window, or the centered fitted original
 * frame in map mode and in cockpit views. snapshot_serial, flight_frame_serial: the snapshot prepared.
 * delta_sim_seconds: the view-time advance since the previous snapshot, in units of 236 ticks (the
 * original's assumed rate; real ticks run 250 a second), 0 after a reset; nothing reads it.
 * reset_history: motion history restarts. regenerate_motion: the poses advanced this frame.
 * render_needed: the frame must be drawn again. */
typedef struct XvtPreparedFlight {
	XvtRenderView view, previous_view;
	XvtLayoutTransform cockpit_layout, frontend_layout;
	AeronRectI content_rect;
	XvtPreparedObject objects[XVT_SNAP_OBJECTS];
	uint32_t object_count;
	uint64_t snapshot_serial, flight_frame_serial;
	float delta_sim_seconds;
	/* Host span between changed poses, matching XWA's held-velocity timing. */
	uint64_t velocity_span_us;
	int valid, reset_history, regenerate_motion, render_needed;
} XvtPreparedFlight;

/* Owns all matrices across snapshot rotation. When regenerate_motion is false,
 * retain the prior velocity result instead of resubmitting a stale previous index. */
/* Builds the frame for current at width x height. Returns 1 with the frame invalidated when current is
 * NULL or has no valid flight or camera. A size change within 150 ms of the last size request keeps the
 * old size (an interactive resize). previous counts only when valid with the same mission and world. A
 * reset restarts motion history: no frame yet, a new snapshot without a usable previous, a size, mission,
 * world, asset or config change, a backwards view time, a view discontinuity (player, focus, external,
 * replay, map, HUD or hyperspace state, projection or screen size), or crossing the hyperspace streak
 * end. The frame advances on a reset or on a new snapshot whose pose changed or whose view time moved;
 * otherwise the previous poses stand. render_needed is set by an advance, a scene change (lighting, sky,
 * types, fuselage sequence, hyperspace, or the map in map mode), any temporal mode, an HDR or headroom
 * change, a pause change, or a change of the advance flag unless pause_keep_blur. Returns 0 with the
 * frame invalidated when a view or layout cannot be built. */
int XvtRemasterFlight_Prepare(const XvtRenderSnapshot *current,
			      const XvtRenderSnapshot *previous, int width,
			      int height);
/* The prepared frame, or NULL while invalid. */
const XvtPreparedFlight *XvtRemasterFlight_Current(void);
/* Marks the frame invalid; the next Prepare resets. */
void XvtRemasterFlight_Invalidate(void);
/* Marks the frame as needing a render, for a HUD or CRT change. */
void XvtRemasterFlight_RequestComposition(void);
/* Draws the prepared frame. Returns 1 with no output while there is no frame or no valid flight. In
 * map mode, renders the map when a render is due or no output stands. Otherwise, when a render is due,
 * the output is gone, or the scene must be recreated (size or MSAA change): begins the pipeline, sky,
 * lighting and environment; then, except in a hyperspace transition's streak phase, for every eligible
 * object (statics only mines to small debris; others only craft, projectiles, small debris, explosions
 * and obstacles; local transients only with debris on, outside the proving grounds and transitions)
 * whose selection has a resident mesh: its engine glows (lights always; the sprites unless it is the
 * focus object in an internal view, which draws nothing more), then its mesh with the current and
 * previous poses (the previous mesh table rebuilt when articulation differs or the flight is unlocked;
 * zero velocity when the previous object selects another model), projectiles roll-aligned, emissive and
 * unshadowed, as an instance inside the view or a shadow caster outside; an obstacle adds its hull
 * component as variant 1 and draws the full model only at the checkpoint slots. The previous camera
 * serves motion only with camera blur or a temporal mode on. Then the effects, the HUD hook after
 * upscale, and the pipeline's finish. Returns 0 on any failure, the output invalid. */
int XvtRemasterFlight_Render(AeronCommandBuffer *cmd,
			     const XvtRenderSnapshot *current,
			     const XvtRenderSnapshot *previous);
/* Borrowed tonemapped SDR/HDR presentation output for the composition driver. */
/* The pipeline's output while the last Render succeeded, else NULL. */
AeronTexture *XvtRemasterFlight_Output(void);
/* Shuts down the map, sky, glows and pipeline, destroys the scene and invalidates the frame. */
void XvtRemasterFlight_Shutdown(void);
/* Creates the flight scene at width x height and the current MSAA when it differs, preparing its GPU
 * resources with the flight settings. Returns 0 when creation or preparation fails. */
int XvtRemasterFlight_PrepareResources(int width, int height);
#ifdef __cplusplus
}
#endif
#endif
