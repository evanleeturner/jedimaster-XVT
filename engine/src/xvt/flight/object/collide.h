#ifndef XVT_FLIGHT_OBJECT_COLLIDE_H
#define XVT_FLIGHT_OBJECT_COLLIDE_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const float g_collide_zero_float;
extern int g_collision_segment_start_world_x;
extern int g_collision_segment_start_world_y;
extern int g_collision_segment_start_world_z;
extern int g_collision_probe_world_x;
extern int g_collision_probe_world_y;
extern int g_collision_probe_world_z;
extern int g_collision_sweep_start_x;
extern int g_collision_sweep_start_y;
extern int g_collision_sweep_start_z;
extern int g_collision_sweep_end_x;
extern int g_collision_sweep_end_y;
extern int g_collision_sweep_end_z;
extern int g_approx_dist;
extern int g_collision_is_aim_prediction;
extern int g_collision_hit_offset_x;
extern int g_collision_hit_offset_y;
extern int g_collision_hit_offset_z;
extern int g_collide_sweep_reject_near_start_hits;
extern int g_turret_fire_hull_mesh_ordinal;

void collide_populate_mobile_object_proximity_candidates(
	struct mobile_object_proximity_list *list, uint16_t owner_obj_idx);
void collide_collisions(void);
void collide_insert_mobile_object_proximity_candidate(
	struct mobile_object_proximity_list *list, uint16_t owner_obj_idx,
	uint16_t candidate_obj_idx);
int collide_get_mobile_object_proximity_speed_q12(uint16_t obj_idx);
void collide_reset_object_proximity_for_slot(uint16_t obj_idx);
void collide_reset_neighbor_proximity_lists(uint16_t object_index);
void collide_remove_mobile_object_proximity_candidate(
	struct mobile_object_proximity_list *list, uint16_t candidate_obj_idx);
void collide_apply_craft_impact_bounce(uint16_t craft_obj_idx,
				       uint16_t other_obj_idx);
int16_t collide_test_swept_pair_collision(uint16_t source_obj_idx,
					  uint16_t target_obj_idx);
int16_t collide_checkboxcollision(int radius);
int collide_would_shot_hit_target(uint16_t source_obj_idx,
				  uint16_t target_obj_idx,
				  uint16_t hardpoint_index);
uint16_t collide_craftstarshipcollision(uint16_t source_obj_idx,
					int16_t lookahead_seconds);
void collide_laserhitcraft(uint16_t projectile_obj_idx, uint16_t craft_obj_idx,
			   int16_t hit_mesh_index);
int16_t collide_damagecraft(uint16_t victim_obj_idx, int16_t hit_mesh_index,
			    uint16_t source_obj_idx,
			    uint16_t hit_side_or_damage_amount);
int collide_convert_object_to_explosion(unsigned int object_index,
					uint8_t explosion_object_type);
unsigned int collide_roughdistance3du(unsigned int abs_dx, unsigned int abs_dy,
				      unsigned int abs_dz);
int collide_roughdistance3d(int dx, int dy, int dz);
unsigned int collide_compute_craft_damage_amount(uint16_t victim_obj_idx,
						 uint16_t source_obj_idx);
int collide_check_swept_model_collision(uint16_t source_obj_idx,
					uint16_t target_obj_idx);
int collide_test_sweep_against_opt_node(struct optimized_poly_object *object,
					struct opt_node *node);
int collide_intersect_segment_with_face_plane(const float *face_normal,
					      const float *face_vertex,
					      const float *segment_start,
					      const float *segment_end,
					      float *out_t);
int collide_point_in_face_polygon(const float *face_normal,
				  const float *vertex_coords,
				  const int32_t *face_vertex_indices,
				  float *projected_point);
void collide_apply_engine_wash_damage(int victim_obj_idx, int source_obj_idx);
void collide_apply_hostile_proximity_weapon_disruption(int owner_obj_idx,
						       int hostile_obj_idx);

#ifdef __cplusplus
}
#endif

#endif
