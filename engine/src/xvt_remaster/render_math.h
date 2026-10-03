#ifndef XVT_REMASTER_RENDER_MATH_H
#define XVT_REMASTER_RENDER_MATH_H
#include "aeron/scene/scene3d.h"
#include "xvt_runtime/snapshot/render_snapshot.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Float mirrors of the original's camera and object math for the remaster's scene: a scene camera built
 * from a snapshot camera record, world-to-pixel projection through it, the model matrix of a snapshot
 * object, a pose-change test between two snapshots, and the uniform-fit layout that places an authored
 * 2D surface inside a window. Positions are measured from an integer origin (the 32-bit world coordinate
 * minus the origin, as float). Matrices are row-major with vectors as columns: translation sits in
 * elements 3, 7 and 11. */

/* camera: the scene camera, its position relative to origin_world. view_proj: camera's row-major
 * view-projection as AeronScene_ComputeViewProj computes it. classic_pixel_scale: window pixels per pixel
 * of the original screen (BuildView: height over the record's viewport height; BuildMainView: the fit
 * scale). */
typedef struct XvtRenderView {
	AeronSceneCamera camera;
	int32_t origin_world[3];
	float view_proj[16], classic_pixel_scale;
} XvtRenderView;

/* scale: the one uniform factor that fits source inside target, the smaller of the two size ratios. The
 * leftover space on the other axis is what LayoutPoint's anchors distribute. */
typedef struct XvtLayoutTransform {
	float scale, source_width, source_height, target_width, target_height;
} XvtLayoutTransform;

/* Fills out from a snapshot camera record: orientation from the record's three rows (the first normalized,
 * the second made perpendicular to it, the third their cross product), position relative to origin, the
 * vertical half-angle from half the record's viewport height over its focal length (2 to the
 * perspective_shift) times its Q16 aspect (0 and 0xFFFF both count as 1), the horizontal half-angle from
 * width:height, projection offsets from center_x, center_y and projection_offset_y, the viewport the whole
 * width x height, and classic_pixel_scale = height / the record's viewport height. Returns 0 for a NULL or
 * invalid record, a NULL origin or out, or a width, height or record viewport that is not positive (out
 * untouched), or a first row, or a second row once made perpendicular, shorter than 1e-6 or not finite
 * (out zeroed). */
int XvtRenderMath_BuildView(const XvtSnapCamera *camera,
			    const int32_t origin[3], int width, int height,
			    XvtRenderView *out);
/* BuildView, then refitted for the main flight view: the record's screen (screen_width x screen_height) is
 * scaled uniformly to fit width x height, both half-angles are taken from the scaled focal length over the
 * whole window, the projection center is the record's viewport origin plus its center (plus
 * projection_offset_y on y), mapped into the fitted frame, and classic_pixel_scale is the fit scale.
 * Returns 0 as BuildView does, and also when the record's screen size is not positive (out then holds
 * BuildView's result). */
int XvtRenderMath_BuildMainView(const XvtSnapCamera *camera,
				const int32_t origin[3], int width, int height,
				XvtRenderView *out);
/* Projects a world point through view into pixels of view's viewport, y down. Returns 0 without writing
 * for a NULL view, world, x or y. Otherwise *depth (when given) receives the clip w first, the point's
 * distance along the view axis; a point at or behind the camera (w not positive or not finite) then
 * returns 0 with *x and *y untouched. Otherwise both are written and the result is 1 only when both are
 * finite. Does not clip: a point in front of the camera but outside the viewport succeeds with
 * coordinates outside it. */
int XvtRenderMath_ProjectWorld(const XvtRenderView *view,
			       const int32_t world[3], float *x, float *y,
			       float *depth);
/* The model matrix that places object's mesh in the scene: its rotation from the cached Q15 rows when the
 * object has a mobile record whose orientation is not dirty, else from its Q16 pitch, yaw and roll as the
 * original computes them, with the rotation's rows taken in the order 0, 2, 1 as the engine reads them
 * (swapping two axes, so the 3x3 part is mirrored and its determinant is negative); scaled by
 * AERON_OPT_UNITS_PER_METER; translation = world_pos - origin. No argument is checked. */
void XvtRenderMath_ObjectMatrix(const XvtSnapObject *object,
				const int32_t origin[3], float out[16]);
/* Returns 1 when a frame drawn for previous cannot stand for current: either is NULL, flight_valid
 * differs, the camera record differs in any byte, the object count differs, an object at the same index
 * differs in any compared field (every field but prev_world_pos, player_owner, flight_group, iff, team,
 * type_specific_word, move_dirty, move_q15, component_hp, sfoil_state and installed_subsystems), or an
 * unlocked flight's view time moved while the component animation reports movement. Returns 0 when
 * neither has a valid flight. Nothing else in the snapshot is compared. */
int XvtRenderMath_PoseChanged(const XvtRenderSnapshot *current,
			      const XvtRenderSnapshot *previous);
/* Sets out to the uniform fit of a source_width x source_height surface inside target_width x
 * target_height. Returns 0, leaving out untouched, when out is NULL or any size is not positive or not
 * finite. */
int XvtRenderMath_Layout(float source_width, float source_height,
			 float target_width, float target_height,
			 XvtLayoutTransform *out);
/* Anchors 0, 0.5 and 1 align an authored layout with a target edge or center. */
/* Maps a source point into the target: scaled, then offset by the anchor's share of the leftover space on
 * each axis. Nothing is checked; layout must come from a successful Layout. */
void XvtRenderMath_LayoutPoint(const XvtLayoutTransform *layout, float anchor_x,
			       float anchor_y, float x, float y, float *out_x,
			       float *out_y);
/* The inverse of LayoutPoint with the same anchors: a target point back to source coordinates. */
void XvtRenderMath_LayoutInverse(const XvtLayoutTransform *layout,
				 float anchor_x, float anchor_y, float x,
				 float y, float *out_x, float *out_y);
#ifdef __cplusplus
}
#endif
#endif
