#ifndef XVT_FLIGHT_AI_PAIFIGHT_H
#define XVT_FLIGHT_AI_PAIFIGHT_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ai_targeting_constant {
	AI_TARGET_ABORT = 251,
	AI_DECOY_RANGE_SMALL = 0x8000,
	AI_DECOY_RANGE_LARGE = 0x10000,
	AI_TARGET_RANGE_MAX = 0x10000,
	AI_TURRET_TARGET_PENALTY = 0x8000,
};

extern int g_gunner_collision_probe_count;

extern int g_paifight_search_origin_x;
extern int g_paifight_search_origin_y;
extern int g_paifight_search_origin_z;

extern uint8_t g_ai_escort_candidate_fg_idx;
extern uint8_t g_paifight_gunner_target_candidate_set[976];

int16_t paifight_scanfortargetorder(void);
int16_t paifight_find_attack_order_target_from_order(uint16_t order_slot);
int16_t paifight_find_nearest_attack_order_target(int16_t target1_type,
						  uint16_t target1,
						  int16_t target_or_mode,
						  int16_t target2_type,
						  uint16_t target2);
int16_t paifight_target_escort_leader_from_order(uint16_t order_slot);
int16_t paifight_target_nearest_escort_leader(int16_t target1_type,
					      uint16_t target1,
					      int16_t target_relation_op,
					      int16_t target2_type,
					      uint16_t target2);
int16_t paifight_find_attacker_of_order_target_from_order(uint16_t order_slot);
int16_t paifight_find_nearest_attacker_of_matching_target(
	int16_t target1_type, uint16_t target1, int16_t target_or_mode,
	int16_t target2_type, uint16_t target2);
int16_t paifight_target_has_attack_capacity(uint16_t target_obj_idx,
					    uint16_t candidate_count);
int16_t paifight_search_order_slot_target(uint16_t order_slot);
int16_t paifight_search_order_slot_remaining_targets(uint16_t order_slot);
int16_t
paifight_count_remaining_order_targets_from_order_slot(uint16_t order_slot);
int16_t paifight_count_remaining_order_targets(int16_t target1_type,
					       uint16_t target1,
					       int16_t target_relation_op,
					       int16_t target2_type,
					       uint16_t target2);
int16_t paifight_escorttargetorder(void);
int16_t paifight_fightershootorder(void);
uint16_t paifight_select_target_component_mesh(uint16_t target_obj_idx);
int16_t paifight_missiledefenseorder(void);
int16_t paifight_gunnerselfdefenseorder(void);
int16_t paifight_gunneroffenseorder(void);
int16_t paifight_find_nearest_gunner_target_in_candidate_set(
	int16_t target1_type, uint16_t target1, int16_t target1_or_target2,
	int16_t target2_type, uint16_t target2, int candidate_set_idx);
void paifight_build_gunner_target_candidate_set(
	int16_t target1_type, uint16_t target1, int16_t target1_or_target2,
	int16_t target2_type, uint16_t target2, uint16_t candidate_set_idx);
int16_t paifight_find_nearest_matching_target_from_origin(
	int16_t target1_type, uint16_t target1, int16_t target1_or_target2,
	int16_t target2_type, uint16_t target2, int16_t require_clear_sweep);
int16_t paifight_coverleaderorder(void);
int16_t paifight_followleadatkorder(void);
int16_t paifight_checkescortorder(void);
int16_t paifight_searchforclosestingroup(int16_t target1_type, uint16_t target1,
					 int16_t target1_or_target2,
					 int16_t target2_type,
					 uint16_t target2);
int16_t paifight_order_slot_has_future_targets(uint16_t order_slot);
int16_t paifight_has_future_fg_targets(int16_t target1_type, uint16_t target1,
				       int16_t target_relation_op,
				       int16_t target2_type, uint16_t target2);

#ifdef __cplusplus
}
#endif

#endif
