#include "xvt/render/flight_light.h"

#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/object.h"
#include "xvt/math/math.h"
#include "xvt/math/math3d.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/render_clip.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"

/* Point lights in g_object_point_lights for the object being lit, 0 to 8.
 * flight_light_setup_object_lighting sets it;
 * flight_light_setup_object_lighting_by_index, flight_view_render and
 * flight_map_draw_object_pass set 0. */
// GLOBAL: XVT 0x5235F0
int g_object_point_light_count = 0;
/* Explosions lighting the object being lit, in its own axes, as
 * flight_light_setup_object_lighting found them; it fills up to 8 of the 10
 * entries. */
// GLOBAL: XVT 0x9FD3B0
struct object_point_light g_object_point_lights[10] = {{0}};

/* Object whose lights flight_light_compute_software_face_sample_intensity has
 * cached; flight_light_reset_software_face_sample_cache sets NULL. */
// GLOBAL: XVT 0x51C00C
static struct object_record *g_sw_face_light_cached_object;
/* Face whose view-space normal flight_light_compute_software_face_sample_intensity
 * has cached in g_sw_face_light_face_normal;
 * flight_light_reset_software_face_sample_cache sets NULL. */
// GLOBAL: XVT 0x51C014
static struct scene_face *g_sw_face_light_cached_face;
/* g_object_point_light_count when the cached object's lights were taken. */
// GLOBAL: XVT 0x51C010
static int g_sw_face_light_cached_point_light_count = 0;
/* The cached object's point lights in view space, turned by its mesh's view
 * orientation and moved by its view position. */
// GLOBAL: XVT 0x550C10
static struct opt_vector g_sw_face_light_point_positions[10] = {
	{0.0f, 0.0f, 0.0f}};
/* Intensity of each cached point light, as a float. */
// GLOBAL: XVT 0x550BE0
static float g_sw_face_light_point_intensities[10] = {0.0f};
/* The cached object's light direction in view space, g_objectLightDirection
 * scaled from Q15 and turned by its mesh's view orientation. */
// GLOBAL: XVT 0x550C00
static struct opt_vector g_sw_face_light_dir = {0.0f, 0.0f, 0.0f};
/* View-space normal of the cached face. */
// GLOBAL: XVT 0x550C70
static struct opt_vector g_sw_face_light_face_normal = {0.0f, 0.0f, 0.0f};

/* Clears the face lighting cache: g_sw_face_light_cached_object and
 * g_sw_face_light_cached_face to NULL. sw3d_draw_visible_faces_to_surface calls it
 * before drawing. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4206A0
void flight_light_reset_software_face_sample_cache(void)
{
	g_sw_face_light_cached_object = 0;
	g_sw_face_light_cached_face = 0;
}

/* Light intensity, 0 to 1, at one screen sample of a face for the software
 * renderer; 0 for a face with no mesh. When the face's object is not the cached
 * one it lights it with flight_light_setup_object_lighting and caches its point
 * lights and light direction in view space; it caches the face's view-space
 * normal per face. The sample's view position is z = 1 / reciprocal_depth and x
 * and y = (screen coordinate - viewport middle) * z * g_inv_proj_scale, y also
 * less g_proj_offset_y. With g_specular_enabled it starts from 0.7 times a
 * specular term of the light direction, ((light + eye) . normal / 2) to the
 * 48th power, and returns 1 when that reaches 1. Each point light the face
 * turns toward adds its intensity times (normal . offset) / d^2, d being a
 * rough length of its offset from the sample, plus, with specular on, (normal .
 * v / 2 / e)^48 / e when that is not negative, v being the offset less the
 * sample position and e a rough length of v; the sum stops at 1. There is no
 * diffuse term from the light direction. */
// FUNCTION: XVT 0x4206B0
float flight_light_compute_software_face_sample_intensity(
	struct scene_face *face, int screen_x, int screen_y,
	float reciprocal_depth)
{
	struct scene_mesh *mesh;
	struct opt_vector normal;
	struct opt_vector vector;
	float screen_to_view_scale;
	float sample_x;
	float sample_y;
	float sample_z;
	float intensity;
	float dx;
	float dy;
	float dz;
	float specular;
	float contribution;
	float component_x;
	float component_y;
	float component_z;
	float distance;
	float reciprocal;
	float light_dot;
	int light_index;

	mesh = face->p_mesh;
	if (mesh == NULL) {
		return g_render_zero_float;
	}
	if (mesh->p_object != g_sw_face_light_cached_object) {
		flight_light_setup_object_lighting(mesh->p_object);
		g_sw_face_light_cached_point_light_count =
			g_object_point_light_count;
		g_sw_face_light_cached_object = mesh->p_object;
		for (light_index = 0; g_object_point_light_count > light_index;
		     ++light_index) {
			struct opt_vector *position =
				&g_sw_face_light_point_positions[light_index];

			position->x =
				(float)g_object_point_lights[light_index].x;
			position->y =
				(float)g_object_point_lights[light_index].y;
			position->z =
				(float)g_object_point_lights[light_index].z;
			math3d_rotate_vec3(&position->x, mesh->view_orient);
			position->x += mesh->view_pos_x;
			position->y += mesh->view_pos_y;
			position->z += mesh->view_pos_z;
			g_sw_face_light_point_intensities[light_index] =
				(float)g_object_point_lights[light_index]
					.intensity;
		}
		g_sw_face_light_dir.x = (float)g_object_light_direction_x *
					g_render_light_direction_unit_scale;
		g_sw_face_light_dir.y = (float)g_object_light_direction_y *
					g_render_light_direction_unit_scale;
		g_sw_face_light_dir.z = (float)g_object_light_direction_z *
					g_render_light_direction_unit_scale;
		math3d_rotate_vec3(&g_sw_face_light_dir.x, mesh->view_orient);
	}

	sample_z = 1.0f / reciprocal_depth;
	screen_to_view_scale = sample_z * g_inv_proj_scale;
	sample_x = (float)(screen_x - (g_flight_vp_width >> 1)) *
		   screen_to_view_scale;
	sample_y = (float)(screen_y - (g_flight_vp_height >> 1) -
			   g_proj_offset_y) *
		   screen_to_view_scale;
	if (face != g_sw_face_light_cached_face) {
		g_sw_face_light_cached_face = face;
		vector = mesh->p_face_normals[face->face_index];
		math3d_rotate_vec3(&vector.x, mesh->view_orient);
		normal = vector;
		g_sw_face_light_face_normal = vector;
	} else {
		normal = g_sw_face_light_face_normal;
	}
	intensity = 0.0f;

	if (g_specular_enabled != 0) {
		/* vector is reused here for the direction from the sample point to the eye, normalized by an
		 * estimated length, for the specular half vector. */
		vector.x = -sample_x;
		component_x = vector.x;
		vector.y = -sample_y;
		component_y = vector.y;
		vector.z = -sample_z;
		component_z = vector.z;
		if (vector.x < 0.0f) {
			component_x = -vector.x;
		}
		if (vector.y < 0.0f) {
			component_y = -vector.y;
		}
		if (vector.z < 0.0f) {
			component_z = -vector.z;
		}
		if (component_y <= component_x && component_z <= component_x) {
			distance = component_x * 0.92640001f +
				   (component_z + component_y) * 0.3872f;
		} else if (component_y >= component_x &&
			   component_z <= component_y) {
			distance = component_y * 0.92640001f +
				   (component_z + component_x) * 0.3872f;
		} else {
			distance = component_z * 0.92640001f +
				   (component_y + component_x) * 0.3872f;
		}
		reciprocal = 1.0f / distance;
		vector.x *= reciprocal;
		vector.y *= reciprocal;
		vector.z *= reciprocal;
		specular = (g_sw_face_light_dir.x + vector.x) * normal.x +
			   (g_sw_face_light_dir.y + vector.y) * normal.y +
			   (g_sw_face_light_dir.z + vector.z) * normal.z;
		if (specular > g_render_zero_float) {
			specular *= g_render_half_float;
			specular = specular * specular * specular;
			specular *= specular;
			specular *= specular;
			specular *= specular;
			specular *= specular;
		} else {
			specular = 0.0f;
		}
		contribution = specular * 0.7f;
		if (contribution > g_render_zero_float) {
			intensity = contribution;
			if (intensity >= 1.0f) {
				return 1.0f;
			}
		}
	}

	for (light_index = 0;
	     light_index < g_sw_face_light_cached_point_light_count;
	     ++light_index) {
		const struct opt_vector *position =
			&g_sw_face_light_point_positions[light_index];

		dx = position->x - sample_x;
		dy = position->y - sample_y;
		dz = position->z - sample_z;
		light_dot = normal.x * dx + normal.y * dy + normal.z * dz;
		if (light_dot > g_render_zero_float) {
			component_x = dx;
			component_y = dy;
			component_z = dz;
			if (dx < 0.0f) {
				component_x = -dx;
			}
			if (dy < 0.0f) {
				component_y = -dy;
			}
			if (dz < 0.0f) {
				component_z = -dz;
			}
			if (component_y <= component_x &&
			    component_z <= component_x) {
				distance =
					component_x +
					(component_z + component_y) *
						g_render_rough_distance_scale;
			} else if (component_y >= component_x &&
				   component_z <= component_y) {
				distance =
					component_y +
					(component_z + component_x) *
						g_render_rough_distance_scale;
			} else {
				distance =
					component_z +
					(component_y + component_x) *
						g_render_rough_distance_scale;
			}
			light_dot = light_dot / (distance * distance);
			if (g_specular_enabled != 0) {
				float half_dot;
				float cosine;

				dx -= sample_x;
				dy -= sample_y;
				dz -= sample_z;
				half_dot = (normal.x * dx + normal.y * dy +
					    normal.z * dz) *
					   g_render_half_float;
				component_x = dx;
				component_y = dy;
				component_z = dz;
				if (dx < 0.0f) {
					component_x = -dx;
				}
				if (dy < 0.0f) {
					component_y = -dy;
				}
				if (dz < 0.0f) {
					component_z = -dz;
				}
				if (component_y <= component_x &&
				    component_z <= component_x) {
					distance =
						component_x *
							g_render_specular_approx_max_component_scale +
						(component_z + component_y) *
							g_render_specular_approx_other_components_scale;
				} else if (component_y >= component_x &&
					   component_z <= component_y) {
					distance =
						component_y *
							g_render_specular_approx_max_component_scale +
						(component_z + component_x) *
							g_render_specular_approx_other_components_scale;
				} else {
					distance =
						component_z *
							g_render_specular_approx_max_component_scale +
						(component_y + component_x) *
							g_render_specular_approx_other_components_scale;
				}
				reciprocal = 1.0f / distance;
				cosine = half_dot * reciprocal;
				if (cosine >= g_render_zero_float) {
					specular = cosine * cosine * cosine;
					specular *= specular;
					specular *= specular;
					specular *= specular;
					specular *= specular;
					specular *= reciprocal;
				} else {
					specular = 0.0f;
				}
			} else {
				specular = 0.0f;
			}
			contribution = light_dot + specular;
			if (contribution > g_render_zero_float) {
				intensity += g_sw_face_light_point_intensities
						     [light_index] *
					     contribution;
				if (intensity >= 1.0f) {
					intensity = 1.0f;
					break;
				}
			}
		}
	}
	return intensity;
}

/* Collects the explosions that light an object into g_object_point_lights and
 * g_object_point_light_count: every object in a main slot of the explosion genus
 * within max_bounds_extent + 0x4000 of the object, by collide_roughdistance3d, up
 * to 8. Each light's position is its offset in the object's axes, (offset .
 * side, -(offset . forward), offset . up) in Q15; for an object without a mobj
 * the axes come from fview_set_object_transform, which also rewrites the current
 * object matrix. Intensity by explosion type and frame (type_specific_byte[0]):
 * types 127 to 130 give 192 at frame 2, 320 at 3, 480 at 4, 320 at 5 to 8, 192
 * at 9, 96 at 10, 48 at 11 and 16 at any other, times (effect_size + 4) / 4 when
 * the explosion's effect_size is 4 or more; types 131 and 132 give 48 at frame
 * 2, 96 at 3, 64 at 4, 32 at 5 and 16 at any other; any other type gives
 * g_flight_brightness_scale_q8 - 256. Every intensity is then multiplied by 8.
 * Sets the count to 0 and stops when g_local_lights_enabled is 0. */
// FUNCTION: XVT 0x44F880
void flight_light_setup_object_lighting(const struct object_record *object)
{
	int light_count;
	int object_idx;
	unsigned int max_distance;
	int world_x;
	int world_y;
	int world_z;
	struct object_record *light_object;

	g_object_point_light_count = 0;
	if (g_local_lights_enabled == 0) {
		return;
	}
	max_distance =
		g_object_type_table[object->object_type].max_bounds_extent +
		0x4000;
	world_x = object->world_x;
	world_y = object->world_y;
	world_z = object->world_z;
	light_object = g_object_table;
	light_count = 0;
	object_idx = 0;
	if (g_region_main_object_slot_end > 0) {
		do {
			int delta_x;
			int delta_y;
			int delta_z;

			if (light_object->object_type != 0 &&
			    light_object->genus_id == CRAFT_GENUS_EXPLOSION) {
				delta_x = light_object->world_x - world_x;
				delta_y = light_object->world_y - world_y;
				delta_z = light_object->world_z - world_z;
				if ((unsigned int)collide_roughdistance3d(
					    delta_x, delta_y, delta_z) <
				    max_distance) {
					if (object->mobj != NULL) {
						g_object_point_lights[light_count]
							.x = math_dot3q15(
							delta_x, delta_y,
							delta_z,
							object->mobj
								->cached_side_x,
							object->mobj
								->cached_side_y,
							object->mobj
								->cached_side_z);
						g_object_point_lights[light_count]
							.y = -math_dot3q15(
							delta_x, delta_y,
							delta_z,
							object->mobj
								->cached_fwd_x,
							object->mobj
								->cached_fwd_y,
							object->mobj
								->cached_fwd_z);
						g_object_point_lights[light_count]
							.z = math_dot3q15(
							delta_x, delta_y,
							delta_z,
							object->mobj
								->cached_up_x,
							object->mobj
								->cached_up_y,
							object->mobj
								->cached_up_z);
					} else {
						fview_set_object_transform(
							object->roll,
							object->pitch,
							object->yaw, 0, NULL);
						g_object_point_lights[light_count]
							.x = math_dot3q15(
							delta_x, delta_y,
							delta_z,
							g_fview_side_x_q15,
							g_fview_side_y_q15,
							g_fview_side_z_q15);
						g_object_point_lights[light_count]
							.y = -math_dot3q15(
							delta_x, delta_y,
							delta_z,
							g_fview_forward_x_q15,
							g_fview_forward_y_q15,
							g_fview_forward_z_q15);
						g_object_point_lights[light_count]
							.z = math_dot3q15(
							delta_x, delta_y,
							delta_z,
							g_fview_up_x_q15,
							g_fview_up_y_q15,
							g_fview_up_z_q15);
					}

					g_object_point_lights[light_count]
						.intensity = 16;
					switch (light_object->object_type) {
					case 127:
					case 128:
					case 129:
					case 130:
						switch (light_object
								->type_specific_byte
									[0]) {
						case 2:
							g_object_point_lights
								[light_count]
									.intensity =
								192;
							break;
						case 3:
							g_object_point_lights
								[light_count]
									.intensity =
								320;
							break;
						case 4:
							g_object_point_lights
								[light_count]
									.intensity =
								480;
							break;
						case 5:
						case 6:
						case 7:
						case 8:
							g_object_point_lights
								[light_count]
									.intensity =
								320;
							break;
						case 9:
							g_object_point_lights
								[light_count]
									.intensity =
								192;
							break;
						case 10:
							g_object_point_lights
								[light_count]
									.intensity =
								96;
							break;
						case 11:
							g_object_point_lights
								[light_count]
									.intensity =
								48;
							break;
						default:
							break;
						}
						if (light_object->mobj !=
							    NULL &&
						    light_object->mobj
								    ->effect_size >=
							    4) {
							g_object_point_lights
								[light_count]
									.intensity *=
								(light_object
									 ->mobj
									 ->effect_size +
								 4) /
								4;
						}
						break;
					case 131:
					case 132:
						switch (light_object
								->type_specific_byte
									[0]) {
						case 2:
							g_object_point_lights
								[light_count]
									.intensity =
								48;
							break;
						case 3:
							g_object_point_lights
								[light_count]
									.intensity =
								96;
							break;
						case 4:
							g_object_point_lights
								[light_count]
									.intensity =
								64;
							break;
						case 5:
							g_object_point_lights
								[light_count]
									.intensity =
								32;
							break;
						default:
							break;
						}
						break;
					default:
						g_object_point_lights
							[light_count]
								.intensity =
							g_flight_brightness_scale_q8 -
							256;
						break;
					}
					g_object_point_lights[light_count]
						.intensity *= 8;
					++light_count;
					if (light_count == 8) {
						break;
					}
				}
			}
			++object_idx;
			++light_object;
		} while (object_idx < g_region_main_object_slot_end);
	}
	g_object_point_light_count = light_count;
}

/* Sets g_object_point_light_count to 0, then, with g_local_lights_enabled set and
 * object_index under g_region_main_object_slot_end + g_region_static_object_slot_count,
 * calls flight_light_setup_object_lighting for that object. hud_update3d_crt is its
 * only caller. */
// FUNCTION: XVT 0x44FDF0
void flight_light_setup_object_lighting_by_index(unsigned int object_index)
{
	g_object_point_light_count = 0;
	if (g_local_lights_enabled != 0 &&
	    (unsigned int)(g_region_main_object_slot_end +
			   g_region_static_object_slot_count) > object_index) {
		flight_light_setup_object_lighting(
			&g_object_table[object_index]);
	}
}
