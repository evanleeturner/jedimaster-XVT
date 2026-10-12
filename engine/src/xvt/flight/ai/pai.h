#ifndef XVT_FLIGHT_AI_PAI_H
#define XVT_FLIGHT_AI_PAI_H

#include <stdint.h>
#include <stdio.h>

#include "xvt/assets/file.h"
#include "xvt/flight/ai/paiorder.h"
#include "xvt/flight/ai_state.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct pai_plan_token_def {
	/* Token as the plan text spells it; empty in the end entry. */
	char name[80];
	int16_t value; /* Byte the token compiles to. */
};

#pragma pack(push, 1)

struct pai_plan_record {
	/* Plan name, up to 79 characters; empty for a free entry. */
	char name[80];
	/* 1 once the plan text defined the plan, 0 while it is only named. */
	uint8_t is_defined;
	/* Where the plan's bytes start in g_plan_order_data. */
	uint32_t data_offset;
};

#pragma pack(pop)
typedef char xvt_size_pai_plan_record[(sizeof(struct pai_plan_record) == 85)
					      ? 1
					      : -1];

struct pai_context {
	uint16_t object_index;		  /* Object index of the craft. */
	struct craft_data *craft;	  /* The craft's data. */
	struct ai_controller *controller; /* The craft's AI controller. */
	/* The leader's object index, 255 when the craft has none. */
	uint16_t leader_object_index;
	/* The leader's craft data, or the craft's own when it has no leader. */
	struct craft_data *leader_or_self_craft;
	uint16_t craft_flight_group_index; /* The craft's flight group. */
	/* Order slot being worked on: the craft's current_order_slot at setup,
	 * moved by the order-switching orders. */
	uint16_t order_slot;
	/* X of the craft at setup; range tests measure from it. */
	int32_t craft_position_x;
	int32_t craft_position_y; /* Y of the craft at setup. */
	int32_t craft_position_z; /* Z of the craft at setup. */
	/* Skill tier, 0 to 2, of the craft's effective skill. */
	uint16_t skill_tier;
	/* Maneuver byte of the running plan; orders compare maneuver_mode with
	 * it to tell whether the craft still flies its plan's own maneuver. */
	uint16_t initial_maneuver_id;
	/* Next order id in the running plan's bytes; pai_process_plan walks
	 * it. */
	uint8_t *plan_cursor;
	/* 1 when target searches skip craft with no working subsystems; the
	 * disable plans and laser_update_mine_weapon_fire set it, setup clears
	 * it. */
	uint8_t require_undisabled_target;
	/* The plan "variablepln" stands for: nullpln's id at setup, then the
	 * plan an order switch picks for the new order. */
	uint8_t variable_plan_id;
	/* Tests the target searches apply: 1 attack capacity, 4 skill range,
	 * 0x10 range and enemy, 0x20 measure from targetSearchOrigin; 2 is set
	 * but nothing tests it. Setup leaves it as it was. */
	uint8_t target_search_flags;
	/* X of the point searches measure from with target_search_flags 0x20, a
	 * turret hardpoint or the craft's position, set by
	 * paifight_gunneroffenseorder. */
	int32_t target_search_origin_x;
	int32_t target_search_origin_y; /* Y of that search origin. */
	int32_t target_search_origin_z; /* Z of that search origin. */
};

extern struct pai_context g_pai_context;
extern int g_pai_skip_to_order4_checked;
extern uint16_t g_ai_skill_value_q16_by_level[8];
extern const uint16_t g_ai_think_interval_by_skill[8];
extern struct pai_plan_record g_plan_table[256];
extern uint8_t g_plan_order_data[0x20000];
extern uint8_t *g_plan_data_ptrs[256];
extern int g_plan_count;
extern const char *const g_builtin_plan_name_table[76];

enum { PAI_PLAN_REPORT_MESSAGE_COUNT = 74 };

extern const uint8_t
	g_plan_report_message_id_by_plan_id[PAI_PLAN_REPORT_MESSAGE_COUNT];
extern uint8_t g_builtin_plan_id_by_name_index[256];
extern uint8_t g_order_leader_builtin_plan_name_index[40];
extern const uint8_t g_order_follower_builtin_plan_name_index[40];

void pai_update_all_craft_ai(void);
void pai_apply_running_plan_target_and_maneuver(unsigned int object_idx);
void pai_process_plan(void);
void pai_setupcraftcontext(uint16_t object_idx);
int pai_skill_value_to_tier(uint16_t skill_value);
uint16_t pai_find_mothership_object(int16_t mothership_flight_group_idx);
int pai_is_object_targetable_near_craft(int unused_craft_obj_idx,
					unsigned int obj_idx, int expand_range);
int16_t pai_is_object_within_skill_range_of_craft(uint16_t obj_idx);
int16_t pai_order_slot_can_board_target(uint16_t order_slot);
int16_t pai_find_boarding_target_from_order(uint16_t order_slot);
int pai_is_object_within_range_of_craft(unsigned int obj_idx,
					unsigned int max_rough_distance);
void pai_update_aim_point_from_order_target(void);
void pai_set_flight_group_formation(unsigned int flight_group_idx,
				    unsigned int formation_type,
				    unsigned int formation_spacing);
void pai_calc_angles_to_aim_point(void);
int16_t pai_find_nearest_boarding_target(uint16_t target1_type,
					 uint16_t target1,
					 int16_t target_or_mode,
					 uint16_t target2_type,
					 uint16_t target2);
int16_t pai_is_plan_complete_for_order_slot(uint16_t plan_id,
					    uint16_t order_slot);
int16_t pai_is_boarding_plan_complete_for_order_slot(uint16_t plan_id,
						     uint16_t order_slot);
int16_t pai_current_order_targets_match_object(uint16_t object_idx);
uint16_t pai_get_effective_skill_value(struct craft_data *craft);
int pai_setup_context_and_find_order_plan_on_target(int object_idx,
						    int leader_plan_name_index,
						    int target_obj_idx);
int pai_find_plan_table_index_by_name(const char *plan_name);
int pai_find_free_plan_table_index(void);
int pai_find_target_token_index(const char *token);
int pai_find_maneuver_token_index(const char *token);
int pai_find_order_token_index(const char *token);
int pai_read_plan_text_token(char *token, xvt_file *stream);
int pai_compile_plans_from_text(const char *base_name);
int pai_loadplans(const char *base_name);
void pai_cache_builtin_plan_ids(void);
uint8_t *pai_getplandataptrbyname(const char *plan_name);
int pai_find_plan_id_by_name_or_zero(const char *plan_name);

#ifdef __cplusplus
}
#endif

#endif
