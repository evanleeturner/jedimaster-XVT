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

#include "xvt/assets/model_bounds.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai_boarded.h"
#include "xvt/flight/ai_boarding.h"
#include "xvt/flight/ai_books.h"
#include "xvt/flight/ai_commands.h"
#include "xvt/flight/ai_countermeasures.h"
#include "xvt/flight/ai_dropoff.h"
#include "xvt/flight/ai_formation.h"
#include "xvt/flight/ai_tracer.h"
#include "xvt/flight/ai_warheads.h"
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
 * as a whole. Levels 6 and 7 are the two Sith ranks, 6 the Sith Apprentice and
 * 7 the Sith Lord; no mission file holds them, the promotion at mission start
 * sets them, and they fly with a Jedi's skill. The 1997 AI's target tests also
 * indexed it by the skill tier, 0 to 2. Nothing writes it. */
// GLOBAL: XVT 0x524100
uint16_t g_ai_skill_value_q16_by_level[8] = {0x0000, 0x4000, 0x8000, 0xC000,
					     0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
/* Ticks between two AI thinks of a craft for each flight group AI level, from
 * 708 down to 29; mission_init_flight_group_object_slot copies the entry into the
 * craft's think_interval. Levels 6 and 7, the Sith Apprentice and the Sith
 * Lord, think as often as a Jedi, every 29 ticks. */
// GLOBAL: XVT 0x524110
const uint16_t g_ai_think_interval_by_skill[8] = {
	708, 472, SIMULATION_TICKS_PER_SECOND, 118, 59, 29, 29, 29};

/* The level a flight group counts as in the stat tables: group_ai, or 5, the
 * Jedi, when it is above 5. The tables hold 6 or 7 entries and the pilot
 * record keeps its size, so the Sith ranks 6 and 7 count as Jedi there. */
uint8_t ai_brain_stat_level(uint8_t group_ai)
{
	if (group_ai > AI_STAT_LEVEL_MAX) {
		return AI_STAT_LEVEL_MAX;
	}
	return group_ai;
}

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
	[AI_ACTIVITY_DISABLED] = IFMSG_176_AWAITING_BOARDING,
	[AI_ACTIVITY_LOOKING_TO_BOARD] = IFMSG_170_LOOKING_FOR_CRAFT_TO_BOARD,
	[AI_ACTIVITY_LOOKING_TO_CAPTURE] =
		IFMSG_171_LOOKING_FOR_CRAFT_TO_CAPTURE,
	[AI_ACTIVITY_LOOKING_TO_PICK_UP] =
		IFMSG_172_LOOKING_FOR_CRAFT_TO_PICK_UP,
	[AI_ACTIVITY_DEPLOYING_CRAFT] = IFMSG_189_DEPLOYING_CRAFT,
	[AI_ACTIVITY_SELF_DESTRUCTING] = IFMSG_190_SELF_DESTRUCTING,
	[AI_ACTIVITY_KAMIKAZE_ATTACK] = IFMSG_191_KAMIKAZE_ATTACK,
	[AI_ACTIVITY_FLYING_TO_RENDEZVOUS] =
		IFMSG_174_FLYING_TO_RENDEZVOUS_POINT,
	[AI_ACTIVITY_WAITING_FOR_BOARDING_CRAFT] =
		IFMSG_175_WAITING_FOR_BOARDING_CRAFT,
};

/* Readies the pilots for a new flight. Nothing to ready until the pilot
 * program starts here. */
void ai_brain_start_flight(void) {}

enum {
	/* Craft one question holds: the craft slots mission.c gives the active
	 * region, CRAFT_SLOT_COUNT, so one question holds a mission's craft.
	 * More craft due in one step are asked about in a further question. */
	AI_BRAIN_QUESTION_CRAFT = 32,
	/* The object types the model bounds tables hold; a u8 type can be past
	 * them. */
	AI_BRAIN_BOUNDS_TYPES = 201,
	/* A weapon slot's projectile_type_id for a turret mount. */
	AI_BRAIN_TURRET_PROJECTILE_TYPE = 2,
	/* warhead_launcher_flags as laser_firewarheadsystem reads them: the
	 * launcher's first slot, both slots, or its second slot. */
	AI_BRAIN_LAUNCHER_NORMAL = 1,
	AI_BRAIN_LAUNCHER_PAIRED = 3,
	AI_BRAIN_LAUNCHER_SECOND_SLOT = 0x81,
	/* The teams a craft keeps an attacked-by record for. */
	AI_BRAIN_TEAM_RECORDS = 10,
};

/* The craft due to think that one question asks about, in slot order, and
 * the contacts and statics every question of the step shows. */
struct ai_brain_batch {
	int count;
	struct ai_wire_craft_view views[AI_BRAIN_QUESTION_CRAFT];
	int contact_count;
	struct ai_wire_contact contacts[AI_WIRE_CONTACTS_MAX];
	int static_count;
	struct ai_wire_contact statics[AI_WIRE_STATICS_MAX];
	int tracer_count;
	struct ai_wire_tracer tracers[AI_WIRE_TRACERS_MAX];
};

static ai_brain_exchange_fn g_ai_brain_exchange;

void ai_brain_set_exchange(ai_brain_exchange_fn exchange)
{
	g_ai_brain_exchange = exchange;
}

/* Whether some team has hit the craft: any team's entry in its attacked-by
 * record is set, the 1997 AI's test for a craft under attack. */
static int ai_brain_craft_attacked(const struct craft_data *craft)
{
	for (int team = 0; team < AI_BRAIN_TEAM_RECORDS; ++team) {
		if (craft->attacked_by_team[team] != 0) {
			return 1;
		}
	}
	return 0;
}

/* The object index of the craft that last hit this one, or UINT16_MAX when
 * none did: a craft never hit has no shooter, and an index that is not a
 * craft in the active region's craft slots, one the contact walk would not
 * list, gives none. A slot a new craft took after its shooter died names the
 * new craft. */
static uint16_t ai_brain_hit_by(const struct craft_data *craft)
{
	uint16_t shooter = craft->ai_flight.threat_obj_idx;
	if (!ai_brain_craft_attacked(craft) ||
	    shooter < g_active_region_object_slot_start ||
	    shooter >= g_active_region_craft_object_slot_end ||
	    g_object_table[shooter].object_type == 0 ||
	    g_object_table[shooter].mobj->family != 0) {
		return UINT16_MAX;
	}
	return shooter;
}

/* Whether the order code holds a craft still: Hold Steady and its copies, or
 * the null order, 1997's null and stationary plans. */
static int ai_brain_order_parks(int order_code)
{
	switch (order_code) {
	case MISSION_ORDER_HOLD_STEADY:
	case MISSION_ORDER_HOLD_STEADY_2:
	case MISSION_ORDER_HOLD_STEADY_3:
	case MISSION_ORDER_HOLD_STEADY_4:
	case MISSION_ORDER_HOLD_STEADY_5:
	case MISSION_ORDER_HOLD_STEADY_6:
	case MISSION_ORDER_HOLD_STEADY_7:
	case MISSION_ORDER_NULL:
		return 1;
	default:
		return 0;
	}
}

/* The object index of the craft that boards the contact: the craft that
 * carries it, else the lowest-numbered other craft in the active region whose
 * current order boards and whose target it is; UINT16_MAX for none. */
static uint16_t ai_brain_boarder(uint16_t object_index,
				 const struct craft_data *craft)
{
	if (craft->carrier_obj_idx != UINT16_MAX) {
		return craft->carrier_obj_idx;
	}
	for (uint16_t other = (uint16_t)g_active_region_object_slot_start;
	     other < g_active_region_craft_object_slot_end; ++other) {
		const struct object_record *object = &g_object_table[other];
		if (other == object_index || object->object_type == 0 ||
		    object->mobj->family != 0) {
			continue;
		}
		if (ai_books_order_boards(mission_current_order_code(other)) &&
		    object->mobj->p_craft->ai_controller.target_obj_idx ==
			    object_index) {
			return other;
		}
	}
	return UINT16_MAX;
}

/* The longest side of the object type's model box, the number the flight code
 * takes as the craft's size; 0 for a type past the object type table. */
static uint32_t ai_brain_view_size(uint8_t object_type)
{
	if (object_type >= AI_BRAIN_BOUNDS_TYPES) {
		return 0;
	}
	return (uint32_t)g_object_type_table[object_type].max_bounds_extent;
}

/* Writes the box around the object type's model into the contact: the
 * smallest and largest corners, each component cut toward zero, in the
 * model's own axes. It reads the bounds table only for a type whose bounds are
 * cached (the model loader fills them for each loaded model; this does not
 * fill them), and sends 0, 0, 0 and 0, 0, 0 for a type past the table, a type
 * not cached, and an empty model, whose smallest corner is above its largest. */
static void ai_brain_view_box(uint8_t object_type,
			      struct ai_wire_contact *contact)
{
	for (int axis = 0; axis < AI_WIRE_POINT_AXES; ++axis) {
		contact->box_min[axis] = 0;
		contact->box_max[axis] = 0;
	}
	if (object_type >= AI_BRAIN_BOUNDS_TYPES ||
	    g_model_bounds_cached[object_type] != 1) {
		return;
	}
	const struct opt_vector *smallest = &g_model_bounds_min[object_type];
	const struct opt_vector *largest = &g_model_bounds_max[object_type];
	if (smallest->x > largest->x || smallest->y > largest->y ||
	    smallest->z > largest->z) {
		return;
	}
	contact->box_min[0] = (int32_t)smallest->x;
	contact->box_min[1] = (int32_t)smallest->y;
	contact->box_min[2] = (int32_t)smallest->z;
	contact->box_max[0] = (int32_t)largest->x;
	contact->box_max[1] = (int32_t)largest->y;
	contact->box_max[2] = (int32_t)largest->z;
}

/* Writes the contact's turn numbers and keeps the craft's sight of them. The
 * contact must hold the craft's signature, yaw, pitch and speed. The turn is
 * the difference from the craft's last sight (craft->ai_seen): yaw and pitch
 * mod 65,536 read signed, the speed difference, and the ticks between the two
 * sights, at most 65,535. A first sight, and a craft whose signature differs
 * from the last sight's (a new craft in a used slot), shows 0 in all four,
 * and so does a sight before the last one's tick (a restarted clock). A sight
 * on the same tick shows the four numbers the last sight showed and keeps
 * that sight, so a second look in one step never reads as flying straight.
 * The history lives in the craft record, which rides saves and rewinds. */
static void ai_brain_view_turn(uint16_t object_index, uint32_t tick,
			       struct ai_wire_contact *contact)
{
	struct ai_seen *seen =
		&g_object_table[object_index].mobj->p_craft->ai_seen;
	int32_t now = (int32_t)tick;

	if (seen->seen == 0 || seen->signature != contact->object_signature ||
	    now < seen->tick) {
		seen->yaw_turned = 0;
		seen->pitch_turned = 0;
		seen->speed_changed = 0;
		seen->turn_ticks = 0;
	} else if (now > seen->tick) {
		int32_t elapsed = now - seen->tick;
		seen->yaw_turned =
			(int16_t)(uint16_t)(contact->yaw - seen->yaw);
		seen->pitch_turned =
			(int16_t)(uint16_t)(contact->pitch - seen->pitch);
		seen->speed_changed =
			(int16_t)(uint16_t)(contact->speed - seen->speed);
		seen->turn_ticks =
			elapsed > UINT16_MAX ? UINT16_MAX : (uint16_t)elapsed;
	}
	if (seen->seen == 0 || seen->signature != contact->object_signature ||
	    now != seen->tick) {
		seen->seen = 1;
		seen->tick = now;
		seen->signature = contact->object_signature;
		seen->yaw = contact->yaw;
		seen->pitch = contact->pitch;
		seen->speed = contact->speed;
	}
	contact->yaw_turned = seen->yaw_turned;
	contact->pitch_turned = seen->pitch_turned;
	contact->speed_changed = seen->speed_changed;
	contact->turn_ticks = seen->turn_ticks;
}

/* Writes what the pilot program is told about the object in object_index, a
 * craft in the active region: its target is its player's target when a
 * player flies it, else its AI target; its size is its object type's, and
 * so is its box (ai_brain_view_box); its roll is the object's; the homing
 * warheads in flight at it, and the pulses among them, come from
 * ai_warheads_view_contact. Its weapon inhibit timer is the craft record's,
 * the field's 16 bits read unsigned as the flight code counts it down. Its
 * flags say whether its current order holds it still or waits for boarding,
 * and its boarder is the craft carrying it or the lowest-numbered craft on a
 * boarding order aimed at it. Its four turn numbers come from
 * ai_brain_view_turn. */
static void ai_brain_view_contact(uint16_t object_index, uint32_t tick,
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
		contact->flags |= AI_WIRE_CONTACT_PLAYER;
		contact->target = (uint16_t)g_players[object->player_owner_idx]
					  .current_target_object_idx;
	}
	if (craft->working_subsystems == 0) {
		contact->flags |= AI_WIRE_CONTACT_DISABLED;
	}
	if (ai_brain_craft_attacked(craft)) {
		contact->flags |= AI_WIRE_CONTACT_ATTACKED;
	}
	int order_code = mission_current_order_code(object_index);
	if (ai_brain_order_parks(order_code)) {
		contact->flags |= AI_WIRE_CONTACT_PARKED;
	}
	if (order_code == MISSION_ORDER_DISABLED ||
	    order_code == MISSION_ORDER_AWAIT_BOARDING) {
		contact->flags |= AI_WIRE_CONTACT_AWAITING;
	}
	contact->boarder = ai_brain_boarder(object_index, craft);
	contact->weapon_inhibit_timer =
		(uint16_t)craft->weapon_fire_inhibit_timer;
	contact->hit_by = ai_brain_hit_by(craft);
	contact->x = object->world_x;
	contact->y = object->world_y;
	contact->z = object->world_z;
	contact->yaw = object->yaw;
	contact->pitch = craft->pitch;
	contact->roll = object->roll;
	contact->speed = object->mobj->speed;
	contact->last_attacker = craft->last_attacker_obj_idx;
	contact->front_shield = craft->shield_energy[0];
	contact->rear_shield = craft->shield_energy[1];
	contact->hull_damage = craft->hull_damage;
	contact->hull_strength = craft->hull_max;
	contact->activity = craft->ai_controller.activity;
	contact->size = ai_brain_view_size(object->object_type);
	ai_brain_view_box(object->object_type, contact);
	ai_warheads_view_contact(object_index, contact);
	ai_brain_view_turn(object_index, tick, contact);
}

/* Fills the batch's contacts: each craft in the active region's craft slots,
 * by the walk ai_brain_think_due_craft makes, at most AI_WIRE_CONTACTS_MAX. */
static void ai_brain_view_contacts(struct ai_brain_batch *batch, uint32_t tick)
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
		ai_brain_view_contact(object_index, tick,
				      &batch->contacts[batch->contact_count]);
		++batch->contact_count;
	}
}

/* Writes what the pilot program is told about the static object in
 * object_index, a static slot whose type counts as an order target. A static
 * has no mobile object and no craft record, so only what the object record
 * and its flight group hold is given: the genus is its type's (0 is a
 * starfighter), the team is the group's, and the
 * disabled flag is set when the object's working-systems word is 0, after an
 * ion hit. Its box (ai_brain_view_box) and roll are given as a craft's are. The
 * rest reads 0, and the target, last attacker and hit by read UINT16_MAX. */
static void ai_brain_view_static(uint16_t object_index,
				 struct ai_wire_contact *contact)
{
	const struct object_record *object = &g_object_table[object_index];

	memset(contact, 0, sizeof *contact);
	contact->object_index = object_index;
	contact->object_signature = object->object_signature;
	contact->object_type = object->object_type;
	contact->genus = g_object_type_table[object->object_type].genus_id;
	contact->team =
		g_mission_flight_groups[object->flight_group_idx].fg.team;
	contact->flight_group = (uint8_t)object->flight_group_idx;
	contact->flags = AI_WIRE_CONTACT_STATIC;
	if (object->type_specific_word == 0) {
		contact->flags |= AI_WIRE_CONTACT_DISABLED;
	}
	contact->x = object->world_x;
	contact->y = object->world_y;
	contact->z = object->world_z;
	contact->yaw = object->yaw;
	contact->pitch = object->pitch;
	contact->roll = object->roll;
	contact->target = UINT16_MAX;
	contact->last_attacker = UINT16_MAX;
	contact->hit_by = UINT16_MAX;
	contact->boarder = UINT16_MAX;
	contact->size = ai_brain_view_size(object->object_type);
	ai_brain_view_box(object->object_type, contact);
}

/* Fills the batch's statics: each static slot, in slot order, whose object
 * counts as an order target (ai_books_static_is_target), at most
 * AI_WIRE_STATICS_MAX. */
static void ai_brain_view_statics(struct ai_brain_batch *batch)
{
	batch->static_count = 0;
	const int end = g_region_main_object_slot_end +
			g_region_static_object_slot_count;
	for (int slot = g_region_main_object_slot_end;
	     slot < end && batch->static_count < AI_WIRE_STATICS_MAX; ++slot) {
		if (!ai_books_static_is_target((uint16_t)slot)) {
			continue;
		}
		ai_brain_view_static((uint16_t)slot,
				     &batch->statics[batch->static_count]);
		++batch->static_count;
	}
}

/* Writes what the pilot program is told about the tracer shot in slot: its
 * place, heading and speed, the craft that fired it, and its age and the
 * ticks it has left (its type's life, laser_get_projectile_lifetime_ticks,
 * is both added). */
static void ai_brain_view_tracer(int slot, struct ai_wire_tracer *tracer)
{
	const struct object_record *object = &g_object_table[slot];
	const struct mobile_object *shot = object->mobj;
	uint16_t life =
		laser_get_projectile_lifetime_ticks(object->object_type);

	tracer->object_index = (uint16_t)slot;
	tracer->source = shot->source_obj_idx;
	tracer->object_type = object->object_type;
	tracer->x = object->world_x;
	tracer->y = object->world_y;
	tracer->z = object->world_z;
	tracer->yaw = object->yaw;
	tracer->pitch = object->pitch;
	tracer->speed = shot->speed;
	tracer->age = life > shot->lifetime_timer
			      ? (uint16_t)(life - shot->lifetime_timer)
			      : 0;
	tracer->life_left = shot->lifetime_timer;
}

/* Fills the batch's tracers: each tracer shot (ai_tracer_slot_is_tracer)
 * whose source is a craft at XVT_SITH_LORD_LEVEL, in slot order, at most
 * AI_WIRE_TRACERS_MAX; none while no Lord flies. */
static void ai_brain_view_tracers(struct ai_brain_batch *batch)
{
	batch->tracer_count = 0;
	for (int slot = g_projectile_object_slot_start;
	     slot < g_projectile_object_slot_end &&
	     batch->tracer_count < AI_WIRE_TRACERS_MAX;
	     ++slot) {
		if (!ai_tracer_slot_is_tracer(slot) ||
		    !ai_tracer_craft_has_rank(
			    g_object_table[slot].mobj->source_obj_idx,
			    XVT_SITH_LORD_LEVEL)) {
			continue;
		}
		ai_brain_view_tracer(slot,
				     &batch->tracers[batch->tracer_count]);
		++batch->tracer_count;
	}
}

/* Whether the object matches one pair of the targets of an order: pair 0 is
 * targets 1 and 2, joined by "or" when target1_or_target2 is 1, else by "and",
 * and pair 1 is targets 3 and 4, joined the same way by target3_or_target4.
 * The 1997 AI's test, pai_current_order_targets_match_object, read the same
 * fields. */
static int ai_brain_pair_matches(const struct mission_order *order, int pair,
				 uint16_t object_index)
{
	int first;
	int second;
	int joined_by_or;
	if (pair == 0) {
		first = mission_object_matches_trigger_variable(
			object_index, order->target1_type, order->target1);
		second = mission_object_matches_trigger_variable(
			object_index, order->target2_type, order->target2);
		joined_by_or = order->target1_or_target2 == 1;
	} else {
		first = mission_object_matches_trigger_variable(
			object_index, order->secondary_target_types[0],
			order->secondary_targets[0]);
		second = mission_object_matches_trigger_variable(
			object_index, order->secondary_target_types[1],
			order->secondary_targets[1]);
		joined_by_or = order->target3_or_target4 == 1;
	}
	return joined_by_or ? first | second : first & second;
}

/* Whether the object matches the targets of the order in order_slot of the
 * flight group: either pair of them (ai_brain_pair_matches). */
static int ai_brain_order_targets_match(uint16_t flight_group,
					uint8_t order_slot,
					uint16_t object_index)
{
	const struct mission_order *order =
		&g_mission_flight_groups[flight_group].fg.orders[order_slot];
	return ai_brain_pair_matches(order, 0, object_index) != 0 ||
	       ai_brain_pair_matches(order, 1, object_index) != 0;
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
 * n of the enemy mask when contact n is hostile to the craft; bit n of the
 * static target mask is set when the batch's static n matches them. On Attack
 * Escorts the targets are the craft that escort a group the order names
 * (ai_books_escorts_pair), any craft of such a flight, and no static is one:
 * the static target mask is 0. The leader is the craft's leader_obj_idx,
 * 0xFFFF when it leads. Its countermeasures
 * and the homing shots aimed at it come from
 * ai_countermeasures_view_craft, its group's waypoints from
 * ai_waypoints_view_craft, a player's commands from
 * ai_commands_view_craft, its formation place from ai_formation_view_craft,
 * its escorted craft and station from ai_formation_view_escort, its start
 * point and the orders with a target now from ai_books_view_craft, and its
 * lock and skill level from ai_warheads_view_craft. */
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
	view->order_first_pair_mask = 0;
	view->enemy_mask = 0;
	view->static_target_mask = 0;
	const struct mission_order *order =
		&g_mission_flight_groups[object->flight_group_idx]
			 .fg.orders[controller->current_order_slot];
	const int escorters = order->order == MISSION_ORDER_ATTACK_ESCORTS;
	for (int contact = 0; contact < batch->contact_count; ++contact) {
		const uint16_t other = batch->contacts[contact].object_index;
		if (escorters) {
			if (g_object_table[other].flight_group_idx ==
			    object->flight_group_idx) {
				/* Never its own flight's craft. */
			} else if (ai_books_escorts_pair(other, order, 0) !=
				   0) {
				view->order_first_pair_mask |= 1u << contact;
				view->order_target_mask |= 1u << contact;
			} else if (ai_books_escorts_pair(other, order, 1) !=
				   0) {
				view->order_target_mask |= 1u << contact;
			}
		} else if (ai_brain_pair_matches(order, 0, other) != 0) {
			view->order_first_pair_mask |= 1u << contact;
			view->order_target_mask |= 1u << contact;
		} else if (ai_brain_pair_matches(order, 1, other) != 0) {
			view->order_target_mask |= 1u << contact;
		}
		if (ai_brain_teams_hostile(view->team,
					   batch->contacts[contact].team)) {
			view->enemy_mask |= 1u << contact;
		}
	}
	for (int target = 0; target < batch->static_count && !escorters;
	     ++target) {
		if (ai_brain_order_targets_match(
			    object->flight_group_idx,
			    controller->current_order_slot,
			    batch->statics[target].object_index)) {
			view->static_target_mask |= (uint64_t)1 << target;
		}
	}
	view->leader = craft->leader_obj_idx == UINT8_MAX
			       ? UINT16_MAX
			       : craft->leader_obj_idx;
	ai_brain_view_weapons(craft, view);
	ai_countermeasures_view_craft(object_index, view);
	ai_waypoints_view_craft(object_index, view);
	ai_commands_view_craft(object_index, view);
	ai_formation_view_craft(object_index, view);
	ai_formation_view_escort(object_index, view);
	ai_warheads_view_craft(object_index, view);
	view->maneuver_timer =
		craft->ai_controller.maneuver_timer < 0
			? 0
			: (uint32_t)craft->ai_controller.maneuver_timer;
	view->board_phase = ai_boarding_phase(object_index);
	view->drop_phase = ai_dropoff_phase(object_index);
	view->lifetime_timer = object->mobj->lifetime_timer;
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
 * fires at while it is not UINT16_MAX, and sets whether it fires ion shots:
 * laser_fireturretslot reads the slot's ammo_count as the ion flag, and a
 * turret slot holds nothing else there. */
static void ai_brain_apply_turrets(uint16_t object_index,
				   struct craft_data *craft,
				   const struct ai_wire_craft_answer *answer)
{
	for (int turret = 0; turret < answer->turret_count; ++turret) {
		uint8_t slot = answer->turret_slot[turret];
		uint8_t ion = answer->turret_ion[turret];
		craft->turret_target_states[slot].target_obj_idx =
			answer->turret_target[turret];
		craft->weapon_slots[slot].ammo_count = ion;
		XVT_LOG_DEBUG(
			"ai.craft_turret object=%d slot=%u target=%u ion=%u predicted=%d",
			(int)object_index,
			(unsigned)answer->turret_slot[turret],
			(unsigned)answer->turret_target[turret],
			(unsigned)answer->turret_ion[turret],
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

/* Starts the craft's self destruct: sets its object's lifetime timer to the
 * answer's units of 1,180 ticks when the timer is 0, which object.c counts
 * down before it destroys the craft as it does any whose lifetime ends. A
 * running timer is kept. The reader let through only 1 to 55 units, which fit
 * the 16-bit timer. */
static void
ai_brain_apply_self_destruct(uint16_t object_index,
			     const struct ai_wire_craft_answer *answer)
{
	struct mobile_object *mobile = g_object_table[object_index].mobj;
	const unsigned int units = answer->self_destruct_units;
	const unsigned int unit = AI_WIRE_SELF_DESTRUCT_UNIT_TICKS;
	int started = mobile->lifetime_timer == 0;
	if (started) {
		mobile->lifetime_timer = (uint16_t)(units * unit);
	}
	XVT_LOG_DEBUG(
		"ai.craft_self_destruct object=%d units=%u started=%d timer=%u predicted=%d",
		(int)object_index, (unsigned)answer->self_destruct_units,
		started, (unsigned)mobile->lifetime_timer,
		g_flight_sim_side_effects_suppressed);
}

/* Applies one craft answer the reader accepted: the notebook always, then
 * each action the pilot program sent, a steer only to a craft not entering
 * hyperspace, an activity after the formation books take its change
 * (ai_formation_activity_changed); the order, depart and dock actions
 * last, through ai_books. */
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
	/* While the engine docks or drops the craft it alone steers and sets
	 * the throttle (ai_wire_engine_flies, from the live craft). */
	const int engine_flies =
		ai_boarding_phase(answer->object_index) !=
			AI_BOARDING_PHASE_NONE ||
		ai_dropoff_phase(answer->object_index) != AI_DROPOFF_PHASE_NONE;
	if ((answer->actions & (1u << AI_WIRE_ACTION_STEER)) != 0 &&
	    craft->object_kind != CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE &&
	    !engine_flies) {
		ai_brain_apply_steer(craft, answer);
	}
	if ((answer->actions & (1u << AI_WIRE_ACTION_THROTTLE)) != 0 &&
	    !engine_flies) {
		craft->throttle_speed = answer->throttle;
	}
	if ((answer->actions & (1u << AI_WIRE_ACTION_ACTIVITY)) != 0) {
		ai_formation_activity_changed(answer->object_index,
					      controller->activity,
					      answer->activity);
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
	if ((answer->actions & (1u << AI_WIRE_ACTION_EVADED)) != 0) {
		ai_commands_evaded(object_index);
	}
	if ((answer->actions & (1u << AI_WIRE_ACTION_MANEUVER_TIMER)) != 0) {
		controller->maneuver_timer = (int)answer->maneuver_timer;
	}
	if ((answer->actions & (1u << AI_WIRE_ACTION_SELF_DESTRUCT)) != 0) {
		ai_brain_apply_self_destruct(object_index, answer);
	}
	if ((answer->actions & (1u << AI_WIRE_ACTION_BOARD)) != 0) {
		if (answer->board_object == UINT16_MAX) {
			ai_boarding_stop(object_index);
		} else {
			ai_boarding_start(object_index, answer->board_object);
		}
	}
	if ((answer->actions & (1u << AI_WIRE_ACTION_DROP_OFF)) != 0) {
		if (answer->drop_off == 0) {
			ai_dropoff_stop(object_index);
		} else {
			ai_dropoff_start(object_index);
		}
	}
	ai_boarding_sync_mode(object_index);
	ai_dropoff_sync_mode(object_index);
	XVT_LOG_DEBUG(
		"ai.craft_answered object=%d actions=%u yaw=%u pitch=%u roll=%u throttle=%u activity=%d interval=%d timer=%d predicted=%d",
		(int)answer->object_index, answer->actions,
		(unsigned)controller->target_xy_angle,
		(unsigned)controller->target_z_angle,
		(unsigned)controller->target_roll,
		(unsigned)craft->throttle_speed, (int)controller->activity,
		controller->think_interval, craft->ai_controller.maneuver_timer,
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
			 AI_WIRE_CONTACTS_MAX * AI_WIRE_CONTACT_BYTES +
			 AI_WIRE_CONTACT_HEAD_BYTES +
			 AI_WIRE_STATICS_MAX * AI_WIRE_CONTACT_BYTES +
			 AI_WIRE_CONTACT_HEAD_BYTES +
			 AI_WIRE_TRACERS_MAX * AI_WIRE_TRACER_BYTES];
	static uint8_t answer[AI_WIRE_HEAD_BYTES +
			      AI_BRAIN_QUESTION_CRAFT *
				      AI_WIRE_CRAFT_ANSWER_BYTES_MAX];
	static struct ai_wire_craft_answer answers[AI_BRAIN_QUESTION_CRAFT];

	const struct ai_wire_question asked = {tick,
					       batch->count,
					       batch->views,
					       batch->contact_count,
					       batch->contacts,
					       batch->static_count,
					       batch->statics,
					       g_mission_file_version,
					       batch->tracer_count,
					       batch->tracers};
	size_t question_length =
		ai_wire_write_question(question, sizeof question, &asked);
	size_t answer_length = g_ai_brain_exchange(question, question_length,
						   answer, sizeof answer);
	enum ai_wire_result result =
		ai_wire_read_answer(answer, answer_length, &asked, answers);
	XVT_LOG_DEBUG(
		"ai.think tick=%u craft=%d contacts=%d statics=%d question_bytes=%d answer_bytes=%d predicted=%d",
		(unsigned)tick, batch->count, batch->contact_count,
		batch->static_count, (int)question_length, (int)answer_length,
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
	ai_brain_view_contacts(&batch, tick);
	ai_brain_view_statics(&batch);
	ai_brain_view_tracers(&batch);
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
		ai_boarding_step(object_index);
		ai_boarded_step(object_index);
		ai_dropoff_step(object_index);
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

/* Whether the object matches the targets of the order g_cur_craft works on,
 * the rule the order target mask uses: either pair of its flight group's
 * order in the craft's current order slot. Answers 0 when g_cur_craft is not
 * a craft of the active region. */
int16_t ai_brain_order_targets_match_object(uint16_t object_index)
{
	if (object_index == UINT16_MAX) {
		return 0;
	}
	for (uint16_t slot = (uint16_t)g_active_region_object_slot_start;
	     slot < g_active_region_craft_object_slot_end; ++slot) {
		const struct object_record *holder = &g_object_table[slot];
		if (holder->object_type == 0 || holder->mobj->family != 0 ||
		    holder->mobj->p_craft != g_cur_craft) {
			continue;
		}
		return (int16_t)ai_brain_order_targets_match(
			holder->flight_group_idx,
			g_cur_craft->ai_controller.current_order_slot,
			object_index);
	}
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

uint16_t ai_brain_attack_target(const struct craft_data *craft)
{
	const struct ai_controller *controller = &craft->ai_controller;

	switch (controller->activity) {
	case AI_ACTIVITY_GOING_HOME:
	case AI_ACTIVITY_GOING_HOME_EVADING:
	case AI_ACTIVITY_ENTERING_HANGAR:
		return UINT16_MAX;
	default:
		return controller->target_obj_idx;
	}
}
