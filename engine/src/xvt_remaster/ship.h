#ifndef XVT_REMASTER_SHIP_H
#define XVT_REMASTER_SHIP_H
#include "xvt_remaster/assets.h"
#include "xvt_remaster/render_math.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Picks the mesh a snapshot object draws with, builds its per-component mesh table (articulation and
 * visibility), bounds and culls the instance, and sets the frame's shading environment. */

/* asset_id: the OPT model asset. component: one component index drawn alone, or UINT16_MAX for the whole
 * model. */
typedef struct XvtShipSelection {
	uint64_t asset_id;
	uint16_t component;
} XvtShipSelection;

/* Resolves what draws object. A component object with a mobile record draws component type_specific[0]
 * >> 1 of its source type's model. A mine, satellite, debris or explosion whose type has no frame
 * sequence draws the whole model only when static with a zero frame; with a sequence, the entry at
 * type_specific[0] is the component (statics still draw the whole model), and a texture-frame entry
 * (bit 15 set) is refused, that being a billboard. Craft, projectiles and obstacles draw the whole
 * model; every other genus draws nothing. Returns 1 with out set, else 0 with out partly written: a NULL
 * argument, a type or source type out of range, a frame index past the sequence, a refused genus or
 * entry, or a type with no model asset. */
int XvtRemasterShip_Select(const XvtRenderSnapshot* snapshot, const XvtSnapObject* object,
						   XvtShipSelection* out);
/* Fills out for every mesh slot: visible when the slot is below asset's component count and either the
 * whole model is drawn (component UINT16_MAX) or it is that component. For a craft object drawing the
 * whole model, a slot with a nonzero component_state is hidden, and a slot with a rotation axis turns
 * about its pivot by its angle (visual_angles when given, else the object's mesh_rotation byte) in
 * -1/256 turns. A B-wing's bridge component angle also turns every slot about -y through the origin.
 * Every slot is marked emissive. object may be NULL: visibility only. */
void XvtRemasterShip_BuildMeshTable(const XvtMeshAsset* asset, const XvtSnapObject* object,
									uint16_t component, const float* visual_angles, AeronSceneMeshTable* out);
/* Conservative articulated sphere, in scene units, around instance translation. */
/* The largest, over visible slots, of the slot-transformed bound-box center's distance from the model
 * origin plus half the box diagonal, times the longest column of transform's rotation part. 0 when no
 * slot is visible. */
float XvtRemasterShip_Radius(const XvtMeshAsset* asset, const AeronSceneMeshTable* table,
							 const float transform[16]);
/* Returns 0 when a sphere of radius at transform's translation lies wholly beyond one of view's six
 * frustum planes (left, right, top, bottom, near, and the reversed-Z far), else 1; a sphere that only
 * clips a corner still passes. */
int XvtRemasterShip_Visible(const XvtRenderView* view, const float transform[16], float radius);
/* Sets the frame's fragment shading environment (uniform slot 1) from the lighting, scene and
 * point_lights settings: intensity, specular multiplier, wrap and spec_geom_adapt; SSAO intensity (0
 * when its quality is off), power, direct term and the render size; the point-light minimum distance,
 * specular weight, diffuse wrap and cap; camera_position when given; the sun along lighting's Q15
 * direction normalized, rotated by eye_camera's rows when given, white when directional_enabled and
 * black otherwise; the ambient color on all six directions. The environment map is cleared and the PBR
 * debug views are set to the inverse of spec_geom_adapt. Without eye_camera, an active hyperspace
 * tunnel instead supplies the sun direction and color, its texture as the environment map with a fixed
 * basis, and the hyperspace mesh_ambient_strength. */
void XvtRemasterShip_SetEnvironment(AeronScene3D* scene, const XvtSnapLighting* lighting,
									const XvtSnapCamera* eye_camera, const float camera_position[3]);
#ifdef __cplusplus
}
#endif
#endif
