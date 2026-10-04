#ifndef XVT_REMASTER_EFFECTS_H
#define XVT_REMASTER_EFFECTS_H
#include "aeron/scene/billboard.h"
#include "xvt_remaster/flight.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The texture-frame billboards the original draws for explosions, debris,
 * backdrops and engine flames, taken from the snapshot's type sequences and the
 * committed image atlases, sized and faced as the original's quads and
 * submitted to the scene far to near. Also the map's per-object effects and the
 * roll-aligned matrix projectile meshes draw with. */

/* texture: the atlas page. u0, v0, u1, v1: the frame's rectangle on it. width, height: its classic
 * pixel size. */
struct xvt_effect_frame {
	AeronTexture *texture;
	float u0;
	float v0;
	float u1;
	float v1;
	int width;
	int height;
};

/* Finds frame id frame in object type type's texture atlas (the committed image
 * of the type's texture asset) and fills out. Returns 0 with out untouched for
 * a type out of range, a type without a committed atlas, or a frame id the
 * atlas does not hold. */
int xvt_effects_frame(const struct xvt_render_snapshot *snapshot, unsigned type,
		      unsigned frame, struct xvt_effect_frame *out);
/* Writes the four corners of a camera-facing quad at center: half_w along
 * camera's first row vector and half_h along its second, rotated by angle
 * (radians) in that plane with the camera's Q16 aspect applied to the rotated
 * terms, in the order (+, +), (-, +), (-, -), (+, -). Nothing is checked. */
void xvt_effects_quad(const struct xvt_snap_camera *camera,
		      const float center[3], float half_w, float half_h,
		      float angle, float out[4][3]);
/* Sets billboard's texture, alpha blending, frame's rectangle at the four
 * corners in Quad's order, and one color at every corner: intensity on the
 * color channels, alpha on alpha. */
void xvt_effects_set_frame(AeronSceneBillboardDesc *billboard,
			   const struct xvt_effect_frame *frame,
			   float intensity, float alpha);
/* Submits the frame's effect billboards to scene, objects taken far to near by
 * view depth. Skipped: the camera's focus object in an internal view (unless
 * drawing for the CRT); local transients when debris is off, in the proving
 * grounds, or during a hyperspace transition; for the CRT, every object but its
 * own and explosions. Small debris, explosions and static mines, satellites and
 * debris draw their type's sequence frame at type_specific[0] (a component
 * object: the follow-up type's frame at type_specific[1]). A craft inside the
 * sky's craft slot range (an obstacle only at the checkpoint slots) then draws
 * one engine flame per intact fuselage mesh, the frame from fuselage_sequence
 * at its flame state (the component_state entry past its last component, when
 * under 25), each rolled by its ordinal times the craft's roll. A sprite needs
 * a texture-frame code and a committed frame; it is sized from the type's
 * max_extent over its depth and the frame's classic size at the camera's focal
 * length (the original's integer arithmetic, light_scale scaling sprites and
 * 256 the flames), faced to the camera and rolled to the object's orientation;
 * an explosion fades by the original's 32-frame envelope and glows by
 * explosion_emissive_strength. Objects at or behind the eye or past 2^31 deep
 * are skipped. With regenerate, a previous snapshot of the same mission and
 * world, and previous_camera, a matching previous object's corners become the
 * billboard's previous position. */
void xvt_effects_submit(AeronScene3D *scene,
			const struct xvt_render_snapshot *current,
			const struct xvt_render_snapshot *previous,
			const struct xvt_snap_camera *camera,
			const struct xvt_snap_camera *previous_camera,
			int regenerate, const struct xvt_snap_preview *crt);
/* ObjectMatrix for object with its roll turned so the mesh's up axis faces
 * camera: the roll is offset by the angle of the camera direction in the
 * object's side-up plane less a quarter turn, and the orientation is rebuilt
 * from the angles, never the cached rows. */
void xvt_effects_projectile_matrix(const struct xvt_snap_object *object,
				   const int32_t camera[3],
				   const int32_t origin[3], float out[16]);
/* Submits object's sprite, when its type's sequence frame is a texture frame,
 * and every engine flame it has (a craft inside the sky's craft slot range, as
 * in Submit, without the obstacle rule), for the map: the snapshot's camera, no
 * previous frame, no CRT filter, no focus, transient or genus skip. */
void xvt_effects_map_object(AeronScene3D *scene,
			    const struct xvt_render_snapshot *s,
			    const struct xvt_snap_object *object);
#ifdef __cplusplus
}
#endif
#endif
