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

#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai_books.h"
#include "xvt/flight/ai_countermeasures.h"
#include "xvt/flight/ai_waypoints.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_geometry.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
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
	/* A weapon slot's projectile_type_id for a turret mount. */
	AI_BRAIN_TURRET_PROJECTILE_TYPE = 2,
	/* warhead_launcher_flags as laser_firewarheadsystem reads them: the
	 * launcher's first slot, both slots, or its second slot. */
	AI_BRAIN_LAUNCHER_NORMAL = 1,
	AI_BRAIN_LAUNCHER_PAIRED = 3,
	AI_BRAIN_LAUNCHER_SECOND_SLOT = 0x81,
};

/* The craft due to think that one question asks about, in slot order, and
 * the contacts every question of the step shows. */
struct ai_brain_batch {
	int count;
	struct ai_wire_craft_view views[AI_BRAIN_QUESTION_CRAFT];
	int contact_count;
	struct ai_wire_contact contacts[AI_WIRE_CONTACTS_MAX];
};

static ai_brain_exchange_fn g_ai_brain_exchange;

void ai_brain_set_exchange(ai_brain_exchange_fn exchange)
{
	g_ai_brain_exchange = exchange;
}

/* Writes what the pilot program is told about the object in object_index, a
 * craft in the active region: its target is its player's target when a
 * player flies it, else its AI target; its size is its object type's. */
static void ai_brain_view_contact(uint16_t object_index,
				  struct ai_wire_contact *contact)
{
	const struct object_record *object = &g_object_table[object_index];
	const struct craft_data *craft = object->mobj->p_craft;

	contact->object_index = object_index;
	contact->object_signature = object->object_signature;
	contact->object_type = object->object_type;
	contact->genus = object->genus_id;
	contact->team = object->mobj->team;
	contact->flight_group = (uint8_t)object->flight_group_idx;
	contact->object_kind = craft->object_kind;
	contact->flags = 0;
	contact->target = craft->ai_controller.target_obj_idx;
	if (object->player_owner_idx != -1) {
		contact->flags = AI_WIRE_CONTACT_PLAYER;
		contact->target = (uint16_t)g_players[object->player_owner_idx]
					  .current_target_object_idx;
	}
	contact->x = object->world_x;
	contact->y = object->world_y;
	contact->z = object->world_z;
	contact->yaw = object->yaw;
	contact->pitch = craft->pitch;
	contact->speed = object->mobj->speed;
	contact->last_attacker = craft->last_attacker_obj_idx;
	contact->front_shield = craft->shield_energy[0];
	contact->rear_shield = craft->shield_energy[1];
	contact->hull_damage = craft->hull_damage;
	contact->hull_strength = craft->hull_max;
	contact->activity = craft->ai_controller.activity;
	contact->size = (uint32_t)g_object_type_table[object->object_type]
				.max_bounds_extent;
}

/* Fills the batch's contacts: each craft in the active region's craft slots,
 * by the walk ai_brain_think_due_craft makes, at most AI_WIRE_CONTACTS_MAX. */
static void ai_brain_view_contacts(struct ai_brain_batch *batch)
{
	batch->contact_count = 0;
	for (uint16_t object_index =
		     (uint16_t)g_active_region_object_slot_start;
	     object_index < g_active_region_craft_object_slot_end &&
	     batch->contact_count < AI_WIRE_CONTACTS_MAX;
	     ++object_index) {
		const struct object_record *object =
			&g_object_table[object_index];
		if (object->object_type == 0 || object->mobj->family != 0) {
			continue;
		}
		ai_brain_view_contact(object_index,
				      &batch->contacts[batch->contact_count]);
		++batch->contact_count;
	}
}

/* Whether the object matches the targets of the order in order_slot of the
 * flight group: targets 1 and 2, joined by "or" when target1_or_target2 is
 * 1, else by "and", or targets 3 and 4, joined the same way by
 * target3_or_target4. The 1997 AI's test, pai_current_order_targets_match_
 * object, read the same fields. */
static int ai_brain_order_targets_match(uint16_t flight_group,
					uint8_t order_slot,
					uint16_t object_index)
{
	const struct mission_order *order =
		&g_mission_flight_groups[flight_group].fg.orders[order_slot];
	int first = mission_object_matches_trigger_variable(
		object_index, order->target1_type, order->target1);
	int second = mission_object_matches_trigger_variable(
		object_index, order->target2_type, order->target2);
	int primary = order->target1_or_target2 == 1 ? first | second
						     : first & second;
	first = mission_object_matches_trigger_variable(
		object_index, order->secondary_target_types[0],
		order->secondary_targets[0]);
	second = mission_object_matches_trigger_variable(
		object_index, order->secondary_target_types[1],
		order->secondary_targets[1]);
	int secondary = order->target3_or_target4 == 1 ? first | second
						       : first & second;
	return primary != 0 || secondary != 0;
}

/* Whether a craft of team other is hostile to a craft of team own: another
 * team that own's record in the mission's teams does not mark allied, the rule
 * the HUD's colors and the player's nearest-enemy search use. A team past the
 * mission's records is not hostile. */
static int ai_brain_teams_hostile(uint8_t own, uint8_t other)
{
	const size_t teams = sizeof g_mission_teams / sizeof g_mission_teams[0];
	return own != other && own < teams &&
	       other < sizeof g_mission_teams[own].allies &&
	       g_mission_teams[own].allies[other] == 0;
}

/* Writes the craft's weapons into its view: which weapon slots are turrets,
 * its cannon groups' shot types, and per warhead launcher its warhead type,
 * the warheads left in its two slots (at most 255) and its cooldown. */
static void ai_brain_view_weapons(const struct craft_data *craft,
				  struct ai_wire_craft_view *view)
{
	unsigned int turrets = 0;
	for (int slot = 0;
	     slot < craft->laser_slot_count && slot < AI_WIRE_WEAPON_SLOTS;
	     ++slot) {
		if (craft->weapon_slots[slot].projectile_type_id ==
		    AI_BRAIN_TURRET_PROJECTILE_TYPE) {
			turrets |= 1u << slot;
		}
	}
	view->turret_slot_mask = (uint16_t)turrets;
	view->cannon_group_count = craft->cannon_group_count;
	view->warhead_launcher_count = craft->warhead_launcher_count;
	for (int group = 0; group < AI_WIRE_CANNON_GROUPS; ++group) {
		view->cannon_type[group] =
			craft->laser_state.projectile_type_id[group];
	}
	for (int launcher = 0; launcher < AI_WIRE_WARHEAD_LAUNCHERS;
	     ++launcher) {
		view->warhead_type[launcher] = 0;
		view->warheads_left[launcher] = 0;
		view->launcher_cooldown[launcher] = 0;
		if (launcher >= craft->warhead_launcher_count) {
			continue;
		}
		int first = g_model_defs[craft->model_index]
				    .warhead_launcher_first_slot[launcher];
		int left = craft->weapon_slots[first].ammo_count +
			   craft->weapon_slots[first + 1].ammo_count;
		int16_t cooldown =
			craft->warhead_launcher_cooldown_ticks[launcher];
		view->warhead_type[launcher] =
			craft->warhead_slot_type_ids[launcher];
		view->warheads_left[launcher] =
			(uint8_t)(left > UINT8_MAX ? UINT8_MAX : left);
		view->launcher_cooldown[launcher] =
			(uint16_t)(cooldown > 0 ? cooldown : 0);
	}
}

/* Writes what the pilot program is told about the craft in object_index,
 * keeping its books first (ai_books_view_craft), which can move a follower
 * to its leader's order slot. The pitch is the craft record's, the one the
 * steering turns; the heading and
 * roll are the object's. Bit n of the order target mask is set when the
 * batch's contact n matches the targets of the craft's current order, and bit
 * n of the enemy mask when contact n is hostile to the craft. The leader is
 * the craft's leader_obj_idx, 0xFFFF when it leads. Its countermeasures
 * and the homing shots aimed at it come from
 * ai_countermeasures_view_craft, its group's waypoints from
 * ai_waypoints_view_craft. */
static void ai_brain_view_craft(uint16_t object_index,
				const struct ai_brain_batch *batch,
				struct ai_wire_craft_view *view)
{
	const struct object_record *object = &g_object_table[object_index];
	const struct craft_data *craft = object->mobj->p_craft;
	const struct ai_controller *controller = &craft->ai_controller;

	ai_books_view_craft(object_index, view);
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
	view->order_target_mask = 0;
	view->enemy_mask = 0;
	for (int contact = 0; contact < batch->contact_count; ++contact) {
		if (ai_brain_order_targets_match(
			    object->flight_group_idx,
			    controller->current_order_slot,
			    batch->contacts[contact].object_index)) {
			view->order_target_mask |= 1u << contact;
		}
		if (ai_brain_teams_hostile(view->team,
					   batch->contacts[contact].team)) {
			view->enemy_mask |= 1u << contact;
		}
	}
	view->leader = craft->leader_obj_idx == UINT8_MAX
			       ? UINT16_MAX
			       : craft->leader_obj_idx;
	ai_brain_view_weapons(craft, view);
	ai_countermeasures_view_craft(object_index, view);
	ai_waypoints_view_craft(object_index, view);
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

/* Sets the craft's AI target, the object its shots and warheads fly at, with
 * the target's signature; UINT16_MAX clears it. */
static void ai_brain_apply_target(uint16_t object_index,
				  struct ai_controller *controller,
				  uint16_t target)
{
	controller->target_obj_idx = target;
	controller->target_signature =
		target == UINT16_MAX ? 0
				     : g_object_table[target].object_signature;
	controller->has_live_target = target != UINT16_MAX;
	XVT_LOG_DEBUG("ai.craft_target object=%d target=%u predicted=%d",
		      (int)object_index, (unsigned)target,
		      g_flight_sim_side_effects_suppressed);
}

/* Sets each cannon group's link mode and burst, as the 1997 AI did; the
 * weapons loop in laser_weaponsfire fires a burst while its mode is not 0. */
static void ai_brain_apply_lasers(uint16_t object_index,
				  struct craft_data *craft,
				  const struct ai_wire_craft_answer *answer)
{
	for (int group = 0;
	     group < craft->cannon_group_count && group < AI_WIRE_CANNON_GROUPS;
	     ++group) {
		craft->laser_state.link_mode[group] = answer->laser_mode[group];
		craft->laser_state.burst_remaining[group] =
			answer->laser_burst[group];
	}
	XVT_LOG_DEBUG(
		"ai.craft_lasers object=%d mode_first=%u burst_first=%u mode_second=%u burst_second=%u predicted=%d",
		(int)object_index, (unsigned)answer->laser_mode[0],
		(unsigned)answer->laser_burst[0],
		(unsigned)answer->laser_mode[1],
		(unsigned)answer->laser_burst[1],
		g_flight_sim_side_effects_suppressed);
}

/* Fires the answer's warhead launcher through laser_firewarheadsystem, as the
 * 1997 AI did, unless the craft is not active or its weapons are held: both
 * slots when paired, else the first, or the second when it holds more. The
 * reader let through only a launcher the view showed ready. */
static void ai_brain_apply_warhead(uint16_t object_index,
				   struct craft_data *craft,
				   const struct ai_wire_craft_answer *answer)
{
	unsigned int launcher = answer->warhead_launcher;
	int fired = 0;
	if (craft->object_kind == CRAFT_OBJECT_KIND_ACTIVE &&
	    craft->weapon_fire_inhibit_timer == 0) {
		int first = g_model_defs[craft->model_index]
				    .warhead_launcher_first_slot[launcher];
		int8_t flags = AI_BRAIN_LAUNCHER_NORMAL;
		if (answer->warhead_paired != 0) {
			flags = AI_BRAIN_LAUNCHER_PAIRED;
		} else if (craft->weapon_slots[first].ammo_count <
			   craft->weapon_slots[first + 1].ammo_count) {
			flags = (int8_t)AI_BRAIN_LAUNCHER_SECOND_SLOT;
		}
		craft->warhead_launcher_flags[launcher] = flags;
		struct craft_data *kept = g_cur_craft;
		laser_firewarheadsystem(object_index, launcher);
		g_cur_craft = kept;
		fired = 1;
	}
	XVT_LOG_DEBUG(
		"ai.craft_warhead object=%d launcher=%u paired=%u fired=%d predicted=%d",
		(int)object_index, launcher, (unsigned)answer->warhead_paired,
		fired, g_flight_sim_side_effects_suppressed);
}

/* Aims each named turret slot at its target, which laser_weaponsfire then
 * fires at while it is not UINT16_MAX. */
static void ai_brain_apply_turrets(uint16_t object_index,
				   struct craft_data *craft,
				   const struct ai_wire_craft_answer *answer)
{
	for (int turret = 0; turret < answer->turret_count; ++turret) {
		craft->turret_target_states[answer->turret_slot[turret]]
			.target_obj_idx = answer->turret_target[turret];
		XVT_LOG_DEBUG(
			"ai.craft_turret object=%d slot=%u target=%u predicted=%d",
			(int)object_index,
			(unsigned)answer->turret_slot[turret],
			(unsigned)answer->turret_target[turret],
			g_flight_sim_side_effects_suppressed);
	}
}

/* Marks the craft arriving from hyperspace, or active again, as the answer's
 * arrival says, and sets its speed at once, as the 1997 AI's arrival from
 * hyperspace did. Only a craft that was arriving from hyperspace when it was
 * asked about (was_arriving) and is active or arriving changes; any other
 * keeps its kind and speed, so the pilot program sets no speed outside an
 * arrival at the game's levels. A harder level may lift that later. */
static void ai_brain_apply_arrival(uint16_t object_index,
				   struct craft_data *craft,
				   const struct ai_wire_craft_answer *answer,
				   int was_arriving)
{
	if (was_arriving &&
	    (craft->object_kind == CRAFT_OBJECT_KIND_ACTIVE ||
	     craft->object_kind ==
		     CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE)) {
		craft->object_kind =
			answer->arriving != 0
				? CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE
				: CRAFT_OBJECT_KIND_ACTIVE;
		g_object_table[object_index].mobj->speed = answer->speed;
	}
	XVT_LOG_DEBUG(
		"ai.craft_arrival object=%d arriving=%d kind=%d speed=%u predicted=%d",
		(int)object_index, (int)answer->arriving,
		(int)craft->object_kind,
		(unsigned)g_object_table[object_index].mobj->speed,
		g_flight_sim_side_effects_suppressed);
}

/* Applies one craft answer the reader accepted: the notebook always, then
 * each action the pilot program sent, a steer only to a craft not entering
 * hyperspace; the order, depart and dock actions last, through ai_books. */
static void ai_brain_apply_answer(const struct ai_wire_craft_answer *answer)
{
	struct craft_data *craft =
		g_object_table[answer->object_index].mobj->p_craft;
	struct ai_controller *controller = &craft->ai_controller;

	/* The activity as asked about: the activity action below may change it
	 * before the arrival action is applied. */
	int was_arriving =
		controller->activity == AI_ACTIVITY_ARRIVING_FROM_HYPERSPACE;
	memcpy(craft->ai_notebook, answer->notebook, sizeof craft->ai_notebook);
	if ((answer->actions & (1u << AI_WIRE_ACTION_STEER)) != 0 &&
	    craft->object_kind != CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE) {
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
	uint16_t object_index = answer->object_index;
	if ((answer->actions & (1u << AI_WIRE_ACTION_TARGET)) != 0) {
		ai_brain_apply_target(object_index, controller, answer->target);
	}
	if ((answer->actions & (1u << AI_WIRE_ACTION_LASERS)) != 0) {
		ai_brain_apply_lasers(object_index, craft, answer);
	}
	if ((answer->actions & (1u << AI_WIRE_ACTION_WARHEAD)) != 0) {
		ai_brain_apply_warhead(object_index, craft, answer);
	}
	if ((answer->actions & (1u << AI_WIRE_ACTION_TURRETS)) != 0) {
		ai_brain_apply_turrets(object_index, craft, answer);
	}
	if ((answer->actions & (1u << AI_WIRE_ACTION_ARRIVAL)) != 0) {
		ai_brain_apply_arrival(object_index, craft, answer,
				       was_arriving);
	}
	if ((answer->actions & (1u << AI_WIRE_ACTION_ORDER)) != 0) {
		ai_books_apply_order(object_index, answer->order_slot,
				     answer->order_state);
	}
	if ((answer->actions & (1u << AI_WIRE_ACTION_DEPART)) != 0) {
		ai_books_depart(object_index);
	}
	if ((answer->actions & (1u << AI_WIRE_ACTION_DOCK)) != 0) {
		ai_books_dock(object_index);
	}
	if ((answer->actions & (1u << AI_WIRE_ACTION_COUNTERMEASURE)) != 0) {
		ai_countermeasures_launch(object_index);
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
			 AI_BRAIN_QUESTION_CRAFT * AI_WIRE_CRAFT_VIEW_BYTES +
			 AI_WIRE_CONTACT_HEAD_BYTES +
			 AI_WIRE_CONTACTS_MAX * AI_WIRE_CONTACT_BYTES];
	static uint8_t answer[AI_WIRE_HEAD_BYTES +
			      AI_BRAIN_QUESTION_CRAFT *
				      AI_WIRE_CRAFT_ANSWER_BYTES_MAX];
	static struct ai_wire_craft_answer answers[AI_BRAIN_QUESTION_CRAFT];

	const struct ai_wire_question asked = {tick, batch->count, batch->views,
					       batch->contact_count,
					       batch->contacts};
	size_t question_length =
		ai_wire_write_question(question, sizeof question, &asked);
	size_t answer_length = g_ai_brain_exchange(question, question_length,
						   answer, sizeof answer);
	enum ai_wire_result result =
		ai_wire_read_answer(answer, answer_length, &asked, answers);
	XVT_LOG_DEBUG(
		"ai.think tick=%u craft=%d contacts=%d question_bytes=%d answer_bytes=%d predicted=%d",
		(unsigned)tick, batch->count, batch->contact_count,
		(int)question_length, (int)answer_length,
		g_flight_sim_side_effects_suppressed);
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

/* Whether the slot holds a craft that is not breaking up or exploding. */
static int ai_brain_is_live_craft(uint16_t object_index)
{
	const struct object_record *object = &g_object_table[object_index];
	if (object->object_type == 0 || object->mobj->family != 0) {
		return 0;
	}
	craft_object_kind kind = object->mobj->p_craft->object_kind;
	return kind != CRAFT_OBJECT_KIND_BREAKING_UP &&
	       kind != CRAFT_OBJECT_KIND_EXPLODING;
}

/* Whether the slot holds a live craft of the flight group whose
 * leader_obj_idx is leader. */
static int ai_brain_follows(uint16_t object_index, uint8_t leader,
			    uint16_t flight_group)
{
	return ai_brain_is_live_craft(object_index) &&
	       g_object_table[object_index].flight_group_idx == flight_group &&
	       g_object_table[object_index].mobj->p_craft->leader_obj_idx ==
		       leader;
}

/* Names a new leader for each flight whose leader is lost, the mission's
 * rule, never the pilot program's: a leader is lost when its slot no longer
 * holds a live craft of its followers' flight group that leads (leader_obj_idx
 * 255). Its live followers in the active region then follow the one with the
 * lowest readout number, craft_index_in_group (on a tie the lowest slot),
 * which leads. */
static void ai_brain_name_lost_leaders(void)
{
	const uint16_t start = (uint16_t)g_active_region_object_slot_start;
	const uint16_t end = g_active_region_craft_object_slot_end;
	for (uint16_t follower = start; follower < end; ++follower) {
		if (!ai_brain_is_live_craft(follower)) {
			continue;
		}
		const struct object_record *object = &g_object_table[follower];
		uint8_t lost = object->mobj->p_craft->leader_obj_idx;
		uint16_t flight_group = object->flight_group_idx;
		/* A leader is a live craft of the group that follows no one. */
		if (lost == UINT8_MAX ||
		    ai_brain_follows(lost, UINT8_MAX, flight_group)) {
			continue;
		}
		/* Followers in lower slots already follow a new leader. */
		uint16_t successor = follower;
		int lowest = object->mobj->p_craft->craft_index_in_group;
		for (uint16_t other = follower; other < end; ++other) {
			if (!ai_brain_follows(other, lost, flight_group)) {
				continue;
			}
			struct craft_data *craft =
				g_object_table[other].mobj->p_craft;
			if (craft->craft_index_in_group < lowest) {
				successor = other;
				lowest = craft->craft_index_in_group;
			}
		}
		int followers = 0;
		for (uint16_t other = follower; other < end; ++other) {
			if (!ai_brain_follows(other, lost, flight_group)) {
				continue;
			}
			g_object_table[other].mobj->p_craft->leader_obj_idx =
				other == successor ? UINT8_MAX
						   : (uint8_t)successor;
			++followers;
		}
		XVT_LOG_DEBUG(
			"ai.leader_named object=%d lost=%d group=%d followers=%d predicted=%d",
			(int)successor, (int)lost, (int)flight_group,
			followers - 1, g_flight_sim_side_effects_suppressed);
	}
}

/* Asks the pilot program about each computer craft whose think timer has run
 * out, once per reference step, by the 1997 AI's rule for which craft are
 * due: it walks the active region's craft slots and skips empty slots,
 * objects whose mobile object family is not 0, craft breaking up or
 * exploding, and craft whose think_timer is above 0. A craft a player flies
 * is not asked, but its timer grows by its think interval as an asked
 * craft's does. Every question of the step shows the contacts as they were
 * at its start; before it, each computer craft entering hyperspace at the
 * jump speed leaves the battle (ai_books_finish_jumps), then a flight whose
 * leader is lost gets a new one (ai_brain_name_lost_leaders). Does nothing
 * while no exchange is set. */
void ai_brain_think_due_craft(void)
{
	static struct ai_brain_batch batch;

	if (g_ai_brain_exchange == NULL) {
		return;
	}
	uint32_t tick = (uint32_t)g_game_time;
	ai_books_finish_jumps();
	ai_brain_name_lost_leaders();
	ai_brain_view_contacts(&batch);
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
		ai_brain_view_craft(object_index, &batch,
				    &batch.views[batch.count]);
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
