#ifndef XVT_FLIGHT_AI_BRAIN_H
#define XVT_FLIGHT_AI_BRAIN_H

#include <stddef.h>
#include <stdint.h>

#include "xvt/flight/ai_state.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct craft_data;

/* Sends one question to the pilot program and reads its answer: question
 * holds question_length bytes, and the answer goes into answer. Returns the
 * answer's length, at most answer_capacity; 0 when no answer came. */
typedef size_t (*ai_brain_exchange_fn)(const uint8_t *question,
				       size_t question_length, uint8_t *answer,
				       size_t answer_capacity);

extern uint16_t g_ai_skill_value_q16_by_level[8];
extern const uint16_t g_ai_think_interval_by_skill[8];
/* The highest level the stat tables and the Jedi rules know. */
#define AI_STAT_LEVEL_MAX 5
/* The level a flight group counts as in the damage, kill and rating tables
 * and in the shield rules: group_ai, or AI_STAT_LEVEL_MAX when it is above
 * it, so a Sith Apprentice (6) or Sith Lord (7) counts as a Jedi. */
uint8_t ai_brain_stat_level(uint8_t group_ai);
extern const uint8_t g_ai_activity_status_message_id[AI_ACTIVITY_COUNT];

void ai_brain_start_flight(void);
/* Sets the exchange ai_brain_think_due_craft asks through; NULL, the value at
 * start, asks nothing. */
void ai_brain_set_exchange(ai_brain_exchange_fn exchange);
void ai_brain_think_due_craft(void);
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
