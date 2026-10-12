#ifndef XVT_FLIGHT_AI_BRAIN_H
#define XVT_FLIGHT_AI_BRAIN_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct craft_data;

void ai_brain_start_flight(void);
void ai_brain_think_due_craft(void);
void ai_brain_restart_craft_plan(unsigned int object_index);
void ai_brain_prepare_craft(uint16_t object_index);
int16_t ai_brain_order_targets_match_object(uint16_t object_index);
int ai_brain_order_gives_plan_on_target(int object_index,
					int leader_plan_name_index,
					int target_object_index);
int16_t ai_brain_find_mine_target(int16_t target1_type, uint16_t target1,
				  int16_t target1_or_target2,
				  int16_t target2_type, uint16_t target2,
				  int16_t require_clear_sweep);
uint16_t ai_brain_effective_skill(struct craft_data *craft);
uint16_t ai_brain_find_mothership(int16_t mothership_flight_group_idx);

#ifdef __cplusplus
}
#endif

#endif
