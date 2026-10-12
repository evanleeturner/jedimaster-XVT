/* The one way into the computer pilots from the rest of the engine. The 1997
 * AI that answered here is gone. On each reference step the craft due to
 * think are asked about through the exchange ai_brain_set_exchange sets, the
 * way to the pilot program; while none is set no craft decides anything, and
 * each keeps the speed and heading it spawned with and AI_ACTIVITY_NONE. The
 * questions below about orders and targets answer no until the pilot program
 * answers them. */
#include "xvt/flight/ai_brain.h"

#include <limits.h>
#include <string.h>

#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_geometry.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/math/math.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/util/game_rand.h"
#include "xvt_runtime/ai/ai_wire.h"
#include "xvt_runtime/log/log.h"

/* AI skill as a fraction of 65,536 for each flight group AI level:
 * mission_init_flight_group_object_slot copies the entry for g_spawn_group_ai into a
 * new craft's ai_skill. Levels 4 and 5 hold 0xFFFF, which math2_fraction treats
 * as a whole; entries 6 and 7 are 0. The 1997 AI's target tests also indexed
 * it by the skill tier, 0 to 2. Nothing writes it. */
// GLOBAL: XVT 0x524100
uint16_t g_ai_skill_value_q16_by_level[8] = {0x0000, 0x4000, 0x8000, 0xC000,
					     0xFFFF, 0xFFFF, 0x0000, 0x0000};
/* Ticks between two AI thinks of a craft for each flight group AI level, from
 * 708 down to 29; mission_init_flight_group_object_slot copies the entry into the
 * craft's think_interval. Entries 6 and 7 are 0. */
// GLOBAL: XVT 0x524110
const uint16_t g_ai_think_interval_by_skill[8] = {
	708, 472, SIMULATION_TICKS_PER_SECOND, 118, 59, 29, 0, 0};

/* The HUD status line of each activity, an index in g_str_in_flight_messages:
 * chosen by meaning among the in-flight status lines; the 1997 table
 * that keyed them by plan id needed the retail plan file's order. */
const uint8_t g_ai_activity_status_message_id[AI_ACTIVITY_COUNT] = {
	[AI_ACTIVITY_NONE] = IFMSG_159_HOLDING_STEADY,
	[AI_ACTIVITY_HOLDING_STATION] = IFMSG_177_HOLDING_STATION,
	[AI_ACTIVITY_IN_FORMATION] = IFMSG_183_FLYING_FLIGHT_PLAN,
	[AI_ACTIVITY_WAITING_FOR_GO] = IFMSG_187_AWAITING_FURTHER_ORDERS,
	[AI_ACTIVITY_WAITING] = IFMSG_188_WAITING,
	[AI_ACTIVITY_ARRIVING_FROM_HYPERSPACE] =
		IFMSG_182_HYPERING_INTO_COMBAT_AREA,
	[AI_ACTIVITY_LEAVING_BY_HYPERSPACE] =
		IFMSG_181_HYPERING_OUT_OF_COMBAT_AREA,
	[AI_ACTIVITY_LEAVING_HANGAR] = IFMSG_180_EXITING_HANGAR,
	[AI_ACTIVITY_ENTERING_HANGAR] = IFMSG_179_ENTERING_HANGAR,
	[AI_ACTIVITY_GOING_HOME] = IFMSG_178_FLYING_HOME,
	[AI_ACTIVITY_GOING_HOME_EVADING] = IFMSG_178_FLYING_HOME,
	[AI_ACTIVITY_BOARDING] = IFMSG_173_DOCKING,
	[AI_ACTIVITY_DISABLED] = IFMSG_159_HOLDING_STEADY,
};

/* Readies the pilots for a new flight. Nothing to ready until the pilot
 * program starts here. */
void ai_brain_start_flight(void) {}

enum {
	/* Craft one question holds: the craft slots mission.c gives the active
	 * region, CRAFT_SLOT_COUNT, so one question holds a mission's craft.
	 * More craft due in one step are asked about in a further question. */
	AI_BRAIN_QUESTION_CRAFT = 32,
};

/* The craft due to think that one question asks about, in slot order. */
struct ai_brain_batch {
	int count;
	struct ai_wire_craft_view views[AI_BRAIN_QUESTION_CRAFT];
};

static ai_brain_exchange_fn g_ai_brain_exchange;

void ai_brain_set_exchange(ai_brain_exchange_fn exchange)
{
	g_ai_brain_exchange = exchange;
}

/* Writes what the pilot program is told about the craft in object_index. The
 * pitch is the craft record's, the one the steering turns; the heading and
 * roll are the object's. */
static void ai_brain_view_craft(uint16_t object_index,
				struct ai_wire_craft_view *view)
{
	const struct object_record *object = &g_object_table[object_index];
	const struct craft_data *craft = object->mobj->p_craft;
	const struct ai_controller *controller = &craft->ai_controller;

	view->object_index = object_index;
	view->object_signature = object->object_signature;
	view->object_type = object->object_type;
	view->flight_group = object->flight_group_idx;
	view->order_code = (uint8_t)mission_current_order_code(object_index);
	view->order_slot = controller->current_order_slot;
	view->activity = controller->activity;
	view->team = object->mobj->team;
	view->skill = craft->ai_skill;
	view->think_interval = (uint16_t)controller->think_interval;
	view->throttle = craft->throttle_speed;
	view->x = object->world_x;
	view->y = object->world_y;
	view->z = object->world_z;
	view->yaw = object->yaw;
	view->pitch = craft->pitch;
	view->roll = object->roll;
	view->speed = object->mobj->speed;
	view->random_seed = controller->saved_rand_seed;
	memcpy(view->notebook, craft->ai_notebook, sizeof view->notebook);
}

/* Turns the craft toward the steer action's angles at its model's full
 * rates: the heading with turn_state 2; the pitch with pitch_state 2 to raise
 * it, 1 to lower it and 3 when it is there, never through a loop; the roll
 * with roll_state 1. The steering in flight.c stops each at its angle. */
static void ai_brain_apply_steer(struct craft_data *craft,
				 const struct ai_wire_craft_answer *answer)
{
	struct ai_controller *controller = &craft->ai_controller;
	struct ai_flight_state *flight = &craft->ai_flight;

	controller->target_xy_angle = answer->steer_yaw;
	flight->turn_state = 2;
	flight->turn_step = -1; /* 0xFFFF as the steering reads it: all. */

	controller->target_z_angle = answer->steer_pitch;
	flight->pitch_through_loop = 0;
	flight->pitch_step_scale = UINT16_MAX;
	if (craft->pitch < answer->steer_pitch) {
		flight->pitch_state = 2;
	} else if (craft->pitch > answer->steer_pitch) {
		flight->pitch_state = 1;
	} else {
		flight->pitch_state = 3;
	}

	controller->target_roll = answer->steer_roll;
	flight->roll_state = 1;
	flight->roll_step = UINT16_MAX;
}

/* Applies one craft answer the reader accepted: the notebook always, then
 * each action the pilot program sent. */
static void ai_brain_apply_answer(const struct ai_wire_craft_answer *answer)
{
	struct craft_data *craft =
		g_object_table[answer->object_index].mobj->p_craft;
	struct ai_controller *controller = &craft->ai_controller;

	memcpy(craft->ai_notebook, answer->notebook, sizeof craft->ai_notebook);
	if ((answer->actions & (1u << AI_WIRE_ACTION_STEER)) != 0) {
		ai_brain_apply_steer(craft, answer);
	}
	if ((answer->actions & (1u << AI_WIRE_ACTION_THROTTLE)) != 0) {
		craft->throttle_speed = answer->throttle;
	}
	if ((answer->actions & (1u << AI_WIRE_ACTION_ACTIVITY)) != 0) {
		controller->activity = answer->activity;
	}
	if ((answer->actions & (1u << AI_WIRE_ACTION_THINK_INTERVAL)) != 0) {
		controller->think_interval = answer->think_interval;
	}
	XVT_LOG_DEBUG(
		"ai.craft_answered object=%d actions=%u yaw=%u pitch=%u roll=%u throttle=%u activity=%d interval=%d predicted=%d",
		(int)answer->object_index, answer->actions,
		(unsigned)controller->target_xy_angle,
		(unsigned)controller->target_z_angle,
		(unsigned)controller->target_roll,
		(unsigned)craft->throttle_speed, (int)controller->activity,
		controller->think_interval,
		g_flight_sim_side_effects_suppressed);
}

/* Asks the pilot program about the batch's craft at tick and applies its
 * answer when the reader accepts it whole; a refused answer applies nothing.
 * Either way each craft's think timer then grows by its think interval, the
 * new one where the answer set it. Empties the batch. */
static void ai_brain_ask(struct ai_brain_batch *batch, uint32_t tick)
{
	static uint8_t
		question[AI_WIRE_HEAD_BYTES +
			 AI_BRAIN_QUESTION_CRAFT * AI_WIRE_CRAFT_VIEW_BYTES];
	static uint8_t answer[AI_WIRE_HEAD_BYTES +
			      AI_BRAIN_QUESTION_CRAFT *
				      AI_WIRE_CRAFT_ANSWER_BYTES_MAX];
	static struct ai_wire_craft_answer answers[AI_BRAIN_QUESTION_CRAFT];

	size_t question_length = ai_wire_write_question(
		question, sizeof question, tick, batch->views, batch->count);
	size_t answer_length = g_ai_brain_exchange(question, question_length,
						   answer, sizeof answer);
	enum ai_wire_result result =
		ai_wire_read_answer(answer, answer_length, tick, batch->views,
				    batch->count, answers);
	XVT_LOG_DEBUG(
		"ai.think tick=%u craft=%d question_bytes=%d answer_bytes=%d predicted=%d",
		(unsigned)tick, batch->count, (int)question_length,
		(int)answer_length, g_flight_sim_side_effects_suppressed);
	if (result == AI_WIRE_OK) {
		for (int index = 0; index < batch->count; ++index) {
			ai_brain_apply_answer(&answers[index]);
		}
	} else {
		XVT_LOG_ERROR(
			"ai.answer_refused tick=%u craft=%d result=%d answer_bytes=%d predicted=%d",
			(unsigned)tick, batch->count, (int)result,
			(int)answer_length,
			g_flight_sim_side_effects_suppressed);
	}
	for (int index = 0; index < batch->count; ++index) {
		struct ai_controller *controller =
			&g_object_table[batch->views[index].object_index]
				 .mobj->p_craft->ai_controller;
		controller->think_timer += controller->think_interval;
	}
	batch->count = 0;
}

/* Asks the pilot program about each computer craft whose think timer has run
 * out, once per reference step, by the 1997 AI's rule for which craft are
 * due: it walks the active region's craft slots and skips empty slots,
 * objects whose mobile object family is not 0, craft breaking up or
 * exploding, and craft whose think_timer is above 0. A craft a player flies
 * is not asked, but its timer grows by its think interval as an asked
 * craft's does. Does nothing while no exchange is set. */
void ai_brain_think_due_craft(void)
{
	static struct ai_brain_batch batch;

	if (g_ai_brain_exchange == NULL) {
		return;
	}
	uint32_t tick = (uint32_t)g_game_time;
	for (uint16_t object_index =
		     (uint16_t)g_active_region_object_slot_start;
	     object_index < g_active_region_craft_object_slot_end;
	     ++object_index) {
		struct object_record *object = &g_object_table[object_index];
		if (object->object_type == 0 || object->mobj->family != 0) {
			continue;
		}
		struct craft_data *craft = object->mobj->p_craft;
		struct ai_controller *controller = &craft->ai_controller;
		if (craft->object_kind == CRAFT_OBJECT_KIND_BREAKING_UP ||
		    craft->object_kind == CRAFT_OBJECT_KIND_EXPLODING ||
		    controller->think_timer > 0) {
			continue;
		}
		if (object->player_owner_idx != -1) {
			controller->think_timer += controller->think_interval;
			continue;
		}
		ai_brain_view_craft(object_index, &batch.views[batch.count]);
		++batch.count;
		if (batch.count == AI_BRAIN_QUESTION_CRAFT) {
			ai_brain_ask(&batch, tick);
		}
	}
	if (batch.count > 0) {
		ai_brain_ask(&batch, tick);
	}
}

/* Whether the object matches the targets of the order g_cur_craft works on.
 * Answers 0 until the pilot program answers. */
int16_t ai_brain_order_targets_match_object(uint16_t object_index)
{
	(void)object_index;
	return 0;
}

/* Whether one of the object's flight group orders in slots 0 to 2 would send
 * it after the target with the leader plan named by leader_plan_name_index
 * in 1997 terms. Answers 0 until the pilot program answers. */
int ai_brain_order_gives_plan_on_target(int object_index,
					int leader_plan_name_index,
					int target_object_index)
{
	(void)object_index;
	(void)leader_plan_name_index;
	(void)target_object_index;
	return 0;
}

/* A mine's target search: the nearest targetable object that matches the
 * targets. Answers 0xFFFF, no target, until the pilot program answers, so
 * mines hold fire. */
int16_t ai_brain_find_mine_target(int16_t target1_type, uint16_t target1,
				  int16_t target1_or_target2,
				  int16_t target2_type, uint16_t target2,
				  int16_t require_clear_sweep)
{
	(void)target1_type;
	(void)target1;
	(void)target1_or_target2;
	(void)target2_type;
	(void)target2;
	(void)require_clear_sweep;
	return -1;
}

/* Returns the AI skill the craft flies with: the skill in the character data of
 * the object effective_ai_object_link points to, while that object still has the
 * saved signature and has character data, else the craft's own ai_skill. Clears
 * effective_ai_object_link when the signature no longer matches. */
// FUNCTION: XVT 0x404620
uint16_t ai_brain_effective_skill(struct craft_data *craft)
{
	struct object_record *linked_object = craft->effective_ai_object_link;
	if (linked_object == 0) {
		return craft->ai_skill;
	}

	if (linked_object->object_signature !=
	    craft->effective_ai_object_signature) {
		craft->effective_ai_object_link = 0;
		return craft->ai_skill;
	}

	if (linked_object->mobj == 0) {
		return craft->ai_skill;
	}
	if (linked_object->mobj->p_char_data != 0) {
		return linked_object->mobj->p_char_data->skill_value;
	}

	return craft->ai_skill;
}

/* Returns the index of the first craft in the active region's craft slots that
 * belongs to the flight group, is not breaking up or exploding, and has no
 * leader; 0xFFFF when there is none. Any flight group works; the callers pass a
 * mothership's. */
// FUNCTION: XVT 0x402E20
uint16_t ai_brain_find_mothership(int16_t mothership_flight_group_idx)
{
	uint16_t object_index = (uint16_t)g_active_region_object_slot_start;
	while (object_index < g_active_region_craft_object_slot_end) {
		struct object_record *object = &g_object_table[object_index];
		if (object->object_type != 0) {
			struct craft_data *craft = object->mobj->p_craft;
			uint8_t object_kind = craft->object_kind;
			if (object_kind != CRAFT_OBJECT_KIND_EXPLODING &&
			    object_kind != CRAFT_OBJECT_KIND_BREAKING_UP &&
			    object->flight_group_idx ==
				    (uint16_t)mothership_flight_group_idx &&
			    (uint8_t)craft->leader_obj_idx == UINT8_MAX) {
				return object_index;
			}
		}

		++object_index;
	}

	return UINT16_MAX;
}
