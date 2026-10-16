/* The mission's books for the computer craft, ported from the 1997 AI's
 * orders 15, 38, 39, 41 and 46, its into-hyperspace maneuver, its hangar
 * order, its set-speed rule and its count of an order's targets left
 * (paiorder.c, paiman.c and paifight.c, deleted with the 1997 AI). The counts follow the
 * 1997 rules. Three of its faults are not kept: a craft docking took every
 * formation follower of its group into the hangar with it, wherever they
 * were (each craft now docks alone); a follower took its leader's departure
 * mark but not its departure clock, so one that later led withdrew at once
 * (it takes both now); and the hangar order could pick another mothership
 * than the one the craft flew home to (one choice serves both now). A cannons
 * abort no longer tells the player "warheads out". */
#include "xvt/flight/ai_books.h"

#include <stdint.h>

#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/ai_brain.h"
#include "xvt/flight/ai_dropoff.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_geometry.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt_runtime/log/log.h"

enum {
	/* depart_timer_flag once the departure came and its delay runs, and
	 * once the delay has passed and the withdrawal was announced. The 1997
	 * AI kept 1 for both; 2 tells the pilot program the craft may go. */
	BOOKS_DEPARTURE_STARTED = 1,
	BOOKS_DEPARTURE_DUE = 2,
	/* completion_state of a slot whose goal was met, and of a boarding
	 * order's slot with no target left. */
	BOOKS_SLOT_DONE = 2,
	BOOKS_SLOT_NO_BOARDING_TARGET = 3,
	/* A group's orders sit in slots 0 to 2; slot 3 is reached only by the
	 * skip-to-order-4 trigger. */
	BOOKS_ORDER_SLOTS = 3,
	BOOKS_SKIP_SLOT = 3,
	BOOKS_TEAMS = 10,
	/* The team goal events for a departure, a captured craft's departure
	 * and a carried craft's delivery. */
	BOOKS_GOAL_DEPARTED = 12,
	BOOKS_GOAL_CAPTURED_DEPARTED = 44,
	BOOKS_GOAL_DELIVERED = 46,
	/* The speed at which a craft entering hyperspace has left: 0xE10. */
	BOOKS_JUMP_SPEED = 3600,
	/* The 1997 leader's jump timers, in ticks: flight.c speeds a craft
	 * entering hyperspace by 50 while the second runs, by 200 while the
	 * first does, then by 500. */
	BOOKS_JUMP_TIMER = 1652,
	BOOKS_JUMP_SECOND_TIMER = 944,
	/* Rough distance from the inside hangar point within which a craft
	 * docks. */
	BOOKS_DOCK_RANGE = 512,
	BOOKS_DOCK_RANGE_STARSHIP = 1024,
	/* Mission point 13, where a group leaves by hyperspace, as an order
	 * target reference and as a point index. */
	BOOKS_HOME_POINT_REF = 0x800D,
	BOOKS_HOME_POINT = 13,
	BOOKS_SECONDS_PER_MINUTE = 60,
	BOOKS_MINUTES_PER_HOUR = 60,
	/* The freighter's genus id; its shields-out abort, and a starship's,
	 * also holds with both banks empty. */
	BOOKS_GENUS_FREIGHTER = 3,
	/* Entries in g_order_throttle_to_craft_throttle_speed. */
	BOOKS_THROTTLE_ENTRIES = 12,
	/* Mission point 12, where a captured craft leaves by hyperspace when
	 * that point is on, as an order target reference and as a point
	 * index. */
	BOOKS_CAPTURED_HOME_POINT_REF = 0x800C,
	BOOKS_CAPTURED_HOME_POINT = 12,
	/* The books an order's 1997 leader plan kept: its departure (order
	 * 38), its abort (order 15) and the skip-to-order-4 test (orders 40
	 * and 43). */
	BOOKS_KEEPS_DEPARTURE = 0x1,
	BOOKS_KEEPS_ABORT = 0x2,
	BOOKS_KEEPS_SKIP = 0x4,
	BOOKS_KEEPS_ALL = 0x7,
	/* An order's two target pairs, the join that makes a pair "or", and
	 * the object type flag that makes a static object count as a
	 * target. */
	BOOKS_TARGET_PAIRS = 2,
	BOOKS_TARGETS_JOINED_BY_OR = 1,
	BOOKS_STATIC_TARGET_FLAG = 2,
	/* A recharge level step's share of the full speed, an eighth of
	 * 65,536, and the recharge sum at which the full speed is the
	 * model's own. */
	BOOKS_SPEED_STEP = 0x2000,
	BOOKS_RECHARGE_EVEN = 6,
	/* A group's start point is the mission point ref 0x8000 plus a point
	 * index below the group's 22 points. */
	BOOKS_START_POINT_REF = 0x8000,
	BOOKS_MISSION_POINTS = 22,
	/* Genus ids below this are small craft for the docking offsets; the
	 * up offset when the sum is negative; the distance to the board point
	 * within which a boarder is at it; the ready message slots the ask
	 * to stop looks through. */
	BOOKS_DOCK_SMALL_GENUS_LIMIT = 2,
	BOOKS_DOCK_UP_FALLBACK = 0x7000,
	BOOKS_BOARD_POINT_RANGE = 2048,
	BOOKS_READY_MESSAGE_SLOTS = 10,
	/* A Drop Off's drop point is slot 0 of the delivered group raised by
	 * this in Z, and the carrier is at it when its rough polar distance is
	 * below the range, which does not let 2,048 itself in. */
	BOOKS_DROP_POINT_RAISE = 932,
	BOOKS_DROP_POINT_RANGE = 2048,
};

static struct craft_data *books_craft(uint16_t object_index)
{
	return g_object_table[object_index].mobj->p_craft;
}

static struct mission_flight_group *books_group(uint16_t object_index)
{
	return &g_mission_flight_groups[g_object_table[object_index]
						.flight_group_idx];
}

/* Counts one more craft of flight group group with outcome, and marks the
 * outcome for the group's special cargo craft. Returns 1 when the craft is
 * that one, else 0. */
static int books_count(uint16_t group, const struct craft_data *craft,
		       int outcome)
{
	++g_mission_fg_stats[group].outcome_count[outcome];
	if (g_mission_flight_groups[group].fg.special_cargo_craft !=
	    craft->craft_ordinal) {
		return 0;
	}
	g_mission_fg_stats[group].special_cargo_outcome[outcome] = 1;
	return 1;
}

/* Counts a captured craft of flight group group leaving the battle, the 1997
 * rule: departed for its captors' team, team, with that team's goal score,
 * and lost for every other team but its group's own. */
static void books_count_captured(uint16_t group, const struct craft_data *craft,
				 unsigned int team)
{
	int special = g_mission_flight_groups[group].fg.special_cargo_craft ==
		      craft->craft_ordinal;
	++g_mission_fg_stats[group].team_captured_departed_count[team];
	if (special) {
		++g_mission_fg_stats[group]
			  .team_special_cargo_captured_departed[team];
	}
	mission_apply_team_goal_score_for_team(BOOKS_GOAL_CAPTURED_DEPARTED,
					       group, special, (uint8_t)team);
	for (unsigned int other = 0; other < BOOKS_TEAMS; ++other) {
		if (other == team ||
		    g_mission_flight_groups[group].fg.team == other) {
			continue;
		}
		++g_mission_fg_stats[group].team_uncaptured_lost[other];
		if (special) {
			g_mission_fg_stats[group]
				.team_special_cargo_uncaptured_lost[other] = 1;
		}
	}
}

/* Counts the computer craft leaving the battle, as the 1997 AI did when one
 * jumped out or entered its hangar. A craft not captured counts departed,
 * with its group's departure goal score, when it did not skip to order 4 and
 * either its orders are done or it neither aborted nor withdrew; departed
 * with its order incomplete when it skipped to order 4 and neither aborted
 * nor withdrew. A captured craft counts by books_count_captured. */
static void books_count_departure(uint16_t object_index)
{
	const struct object_record *object = &g_object_table[object_index];
	const struct craft_data *craft = object->mobj->p_craft;
	const struct ai_flight_state *flight = &craft->ai_flight;
	uint16_t group = object->flight_group_idx;
	unsigned int team = object->mobj->team;
	int skipped = craft->ai_controller.skipped_to_order4;
	int flown = flight->mission_aborted_flag == 0 &&
		    flight->depart_timer_flag == 0;
	if (craft->captured_by_flight_group != 0) {
		books_count_captured(group, craft, team);
	} else {
		if (skipped == 0 && (flight->go_home_flag != 0 || flown)) {
			int special = books_count(
				group, craft, FLIGHT_GROUP_OUTCOME_DEPARTED);
			mission_apply_team_goal_score_all_enabled_teams(
				BOOKS_GOAL_DEPARTED, group, special);
		}
		if (skipped == 1 && flown) {
			books_count(
				group, craft,
				FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE);
		}
	}
	XVT_LOG_DEBUG(
		"ai.departure_counted object=%d fg=%d departed=%u incomplete=%u home=%d aborted=%d departing=%d skipped=%d captured=%d team_departed=%d predicted=%d",
		(int)object_index, (int)group,
		(unsigned)g_mission_fg_stats[group]
			.outcome_count[FLIGHT_GROUP_OUTCOME_DEPARTED],
		(unsigned)g_mission_fg_stats[group].outcome_count
			[FLIGHT_GROUP_OUTCOME_DEPARTED_WITH_ORDER_INCOMPLETE],
		(int)flight->go_home_flag, (int)flight->mission_aborted_flag,
		(int)flight->depart_timer_flag, skipped,
		(int)craft->captured_by_flight_group,
		(int)g_mission_fg_stats[group]
			.team_captured_departed_count[team],
		g_flight_sim_side_effects_suppressed);
}

/* Takes the object the craft in object_index carries out of the battle with
 * it, as 1997 did: records outcome for it; a captured one counts as a
 * captured craft departing and as not taken by its destination, any other as
 * taken by its destination, with the delivery goal score; then it is freed.
 * Nothing when the craft carries nothing. */
static void books_carry_out(uint16_t object_index, uint16_t outcome)
{
	uint16_t carried = g_object_table[object_index]
				   .mobj->p_craft->carried_object_index;
	if (carried == UINT16_MAX || g_object_table[carried].mobj == NULL) {
		return;
	}
	uint16_t group = g_object_table[carried].flight_group_idx;
	struct craft_data *craft = g_object_table[carried].mobj->p_craft;
	mission_record_craft_outcome(carried, group, outcome);
	if (craft->captured_by_flight_group != 0) {
		books_count_captured(group, craft,
				     g_object_table[carried].mobj->team);
		books_count(group, craft,
			    FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION);
	} else {
		int special = books_count(
			group, craft,
			FLIGHT_GROUP_OUTCOME_CAPTURED_BY_DESTINATION);
		mission_apply_team_goal_score_all_enabled_teams(
			BOOKS_GOAL_DELIVERED, group, special);
	}
	XVT_LOG_DEBUG(
		"ai.cargo_departed object=%d carried=%d fg=%d captured=%d delivered=%u undelivered=%u predicted=%d",
		(int)object_index, (int)carried, (int)group,
		(int)craft->captured_by_flight_group,
		(unsigned)g_mission_fg_stats[group].outcome_count
			[FLIGHT_GROUP_OUTCOME_CAPTURED_BY_DESTINATION],
		(unsigned)g_mission_fg_stats[group].outcome_count
			[FLIGHT_GROUP_OUTCOME_NOT_CAPTURED_BY_DESTINATION],
		g_flight_sim_side_effects_suppressed);
	g_object_table[carried].object_type = 0;
	craft_free_linked_objects(craft);
}

/* Shows the craft's message, records outcome for it, and frees it and then
 * the object it carries, in the 1997 order. */
static void books_leave(uint16_t object_index, uint16_t outcome, int message)
{
	struct object_record *object = &g_object_table[object_index];
	struct craft_data *craft = object->mobj->p_craft;
	msg_emit_craft_message(object_index, craft, message);
	mission_record_craft_outcome(object_index, object->flight_group_idx,
				     outcome);
	object->object_type = 0;
	craft_free_linked_objects(craft);
	books_carry_out(object_index, outcome);
}

/* The books the 1997 leader plan of order kept, BOOKS_KEEPS_ bits, read
 * from the retail plan file: none for Hold Steady and its copies, Go Home,
 * SS Go Home, Self Destruct and the null order; departure and abort for
 * Rendezvous; the skip test alone for Disabled and Await Boarding; departure
 * and the skip test for Kamikaze; all three for every other order. A code no
 * order has keeps none. The followers' plans kept the same abort. */
static unsigned int books_kept_by_order(uint8_t order)
{
	switch (order) {
	case MISSION_ORDER_HOLD_STEADY:
	case MISSION_ORDER_GO_HOME:
	case MISSION_ORDER_HOLD_STEADY_2:
	case MISSION_ORDER_SS_GO_HOME:
	case MISSION_ORDER_HOLD_STEADY_3:
	case MISSION_ORDER_HOLD_STEADY_4:
	case MISSION_ORDER_HOLD_STEADY_5:
	case MISSION_ORDER_HOLD_STEADY_6:
	case MISSION_ORDER_SELF_DESTRUCT:
	case MISSION_ORDER_HOLD_STEADY_7:
	case MISSION_ORDER_NULL:
		return 0;
	case MISSION_ORDER_RENDEZVOUS:
		return BOOKS_KEEPS_DEPARTURE | BOOKS_KEEPS_ABORT;
	case MISSION_ORDER_DISABLED:
	case MISSION_ORDER_AWAIT_BOARDING:
		return BOOKS_KEEPS_SKIP;
	case MISSION_ORDER_KAMIKAZE:
		return BOOKS_KEEPS_DEPARTURE | BOOKS_KEEPS_SKIP;
	default:
		return order < MISSION_ORDER_NULL ? BOOKS_KEEPS_ALL : 0;
	}
}

/* The books the craft's current order kept. */
static unsigned int books_kept(uint16_t object_index)
{
	uint8_t slot =
		books_craft(object_index)->ai_controller.current_order_slot;
	return books_kept_by_order(
		books_group(object_index)->fg.orders[slot].order);
}

/* Whether flight group group matches a pair of order targets, joined by "or"
 * when join is BOOKS_TARGETS_JOINED_BY_OR, else by "and", as the 1997 count
 * joined them. */
static int books_targets_match(uint16_t group, uint8_t first_type,
			       uint8_t first, uint8_t join, uint8_t second_type,
			       uint8_t second)
{
	int16_t matches = mission_flight_group_matches_trigger_variable(
		group, first_type, first);
	int16_t also = mission_flight_group_matches_trigger_variable(
		group, second_type, second);
	if (join == BOOKS_TARGETS_JOINED_BY_OR) {
		return (matches | also) != 0;
	}
	return (matches & also) != 0;
}

/* Whether the order's target pair, 0 for targets 1 and 2 and 1 for targets 3
 * and 4, matches flight group group. */
int ai_books_pair_matches(const struct mission_order *order, int pair,
			  uint16_t group)
{
	if (pair == 0) {
		return books_targets_match(group, order->target1_type,
					   order->target1,
					   order->target1_or_target2,
					   order->target2_type, order->target2);
	}
	return books_targets_match(
		group, order->secondary_target_types[0],
		order->secondary_targets[0], order->target3_or_target4,
		order->secondary_target_types[1], order->secondary_targets[1]);
}

/* Whether the object may count as a target left, the 1997 targetability
 * test: a live object, and for a craft one not entering hyperspace, of kind
 * 1, breaking up or exploding, and for a computer craft one whose hull damage
 * is at most its hull. The 1997 test also passed over a craft with its decoy
 * beam on beyond a range of the asking craft; a decoy no longer ends an
 * order. */
static int books_target_counts(uint16_t object_index)
{
	const struct object_record *object = &g_object_table[object_index];
	if (object->object_type == 0) {
		return 0;
	}
	if (object->mobj == NULL || object->mobj->p_craft == NULL) {
		return 1;
	}
	const struct craft_data *craft = object->mobj->p_craft;
	uint8_t kind = craft->object_kind;
	if (kind == CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE ||
	    kind == CRAFT_OBJECT_KIND_UNKNOWN_1 ||
	    kind == CRAFT_OBJECT_KIND_BREAKING_UP ||
	    kind == CRAFT_OBJECT_KIND_EXPLODING) {
		return 0;
	}
	return object->player_owner_idx != -1 ||
	       craft->hull_damage <= craft->hull_max;
}

/* Whether a target of the order's pair is in the battle now, the 1997
 * count: a craft of another group in the active region's craft slots that
 * matches and may count, for a Disable order one in working order not
 * captured by the asking craft's team; or a static object whose type counts
 * as a target. */
static int books_pair_has_target_now(uint16_t object_index,
				     const struct mission_order *order,
				     int pair)
{
	const struct object_record *asking = &g_object_table[object_index];
	int working_only = order->order == MISSION_ORDER_DISABLE;
	for (uint16_t slot = (uint16_t)g_active_region_object_slot_start;
	     slot < g_active_region_craft_object_slot_end; ++slot) {
		const struct object_record *object = &g_object_table[slot];
		if (object->object_type == 0 ||
		    object->flight_group_idx == asking->flight_group_idx ||
		    !ai_books_pair_matches(order, pair,
					   object->flight_group_idx) ||
		    !books_target_counts(slot)) {
			continue;
		}
		const struct craft_data *craft = object->mobj->p_craft;
		if (!working_only || craft == NULL ||
		    (craft->working_subsystems != 0 &&
		     (craft->captured_by_flight_group == 0 ||
		      asking->mobj->team != object->mobj->team))) {
			return 1;
		}
	}
	int end = g_region_main_object_slot_end +
		  g_region_static_object_slot_count;
	for (int slot = g_region_main_object_slot_end; slot < end; ++slot) {
		const struct object_record *object = &g_object_table[slot];
		if (ai_books_static_is_target((uint16_t)slot) &&
		    ai_books_pair_matches(order, pair,
					  object->flight_group_idx)) {
			return 1;
		}
	}
	return 0;
}

/* Whether a flight group that matches the order's pair is still to arrive,
 * the 1997 test: one that may arrive, or a player's, that has not arrived or
 * has waves still to come. */
static int books_pair_has_target_later(const struct mission_order *order,
				       int pair)
{
	for (uint16_t group = 0; group < g_mission_header.num_flight_groups;
	     ++group) {
		const struct mission_fg_runtime_stats *stats =
			&g_mission_fg_stats[group];
		if ((stats->arrival_enabled != 0 ||
		     g_mission_flight_groups[group].player_owner_idx != -1) &&
		    ai_books_pair_matches(order, pair, group) &&
		    (stats->has_arrived == 0 || stats->waves_remaining != 0)) {
			return 1;
		}
	}
	return 0;
}

/* Whether the order in slot has a target in the battle now in either of its
 * target pairs: books_targets_left without the groups still to arrive. */
static int books_slot_targets_now(uint16_t object_index, int slot)
{
	const struct mission_order *order =
		&books_group(object_index)->fg.orders[slot];
	for (int pair = 0; pair < BOOKS_TARGET_PAIRS; ++pair) {
		if (books_pair_has_target_now(object_index, order, pair)) {
			return 1;
		}
	}
	return 0;
}

/* Whether the craft's current order still has a target in either of its
 * target pairs, now or later: the 1997 test that ended the Attack, Attack
 * Escorts, Protect, Escort and Disable orders and a starship's protect,
 * attack and disable orders. Attack Escorts' own search, which made the
 * nearest escort leader the target, is not kept: its targets count as any
 * order's do. */
static int books_targets_left(uint16_t object_index)
{
	uint8_t slot =
		books_craft(object_index)->ai_controller.current_order_slot;
	const struct mission_order *order =
		&books_group(object_index)->fg.orders[slot];
	for (int pair = 0; pair < BOOKS_TARGET_PAIRS; ++pair) {
		if (books_pair_has_target_now(object_index, order, pair) ||
		    books_pair_has_target_later(order, pair)) {
			return 1;
		}
	}
	return 0;
}

/* The speed full throttle gives the craft, the 1997 set-speed rule: its
 * model's top speed, raised by an eighth for each step the sum of its
 * shield, beam and laser recharge levels falls below 6, or lowered by an
 * eighth for each step above. */
static uint16_t books_full_speed(const struct craft_data *craft)
{
	uint16_t top = craft->ai_flight.max_speed_cache;
	int below = BOOKS_RECHARGE_EVEN - (int)craft->shield_recharge_level -
		    (int)craft->beam_recharge_level -
		    (int)craft->laser_recharge_level;
	uint16_t steps =
		(uint16_t)((below < 0 ? -below : below) * BOOKS_SPEED_STEP);
	uint16_t share = (uint16_t)math2_fraction(steps, top);
	return (uint16_t)(below < 0 ? top - share : top + share);
}

/* Whether flight group group has arrived in full, with none of its craft
 * still to come. */
static int books_group_all_arrived(int group)
{
	const struct mission_fg_runtime_stats *stats =
		&g_mission_fg_stats[group];
	return stats->outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] ==
	       stats->outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED];
}

/* Whether every mothership group a craft whose group leaves by mothership
 * could go home into has arrived in full, the 1997 order 46's test: for a
 * captured craft its captured departure group; else its departure group and
 * its alternate group when used. */
static int books_motherships_all_arrived(uint16_t object_index)
{
	const struct mission_flight_group *group = books_group(object_index);
	if (books_craft(object_index)->captured_by_flight_group != 0) {
		return books_group_all_arrived(
			group->fg.captured_departure_mothership);
	}
	return (group->fg.departure_method == 0 ||
		books_group_all_arrived(group->fg.departure_mothership)) &&
	       (group->fg.alternate_mothership_used == 0 ||
		books_group_all_arrived(group->fg.alternate_mothership));
}

/* Whether every other flight group that departs into the craft's group as
 * its mothership has arrived, has no waves left and has no craft left in the
 * active region's craft slots, the 1997 order 32's test. A group whose
 * arrival is off and that no player owns is left out, as is a group that does
 * not leave by mothership or leaves into another group. A group that leaves
 * into this one and has not arrived yet, or has a wave left, or still has a
 * craft in the battle, makes the answer no. */
static int books_all_returned(uint16_t object_index)
{
	uint16_t own_group = g_object_table[object_index].flight_group_idx;
	for (uint16_t group = 0; group < g_mission_header.num_flight_groups;
	     ++group) {
		const struct mission_flight_group *other =
			&g_mission_flight_groups[group];
		if ((g_mission_fg_stats[group].arrival_enabled == 0 &&
		     other->player_owner_idx == -1) ||
		    group == own_group || other->fg.departure_method == 0 ||
		    other->fg.departure_mothership != own_group) {
			continue;
		}
		if (g_mission_fg_stats[group].has_arrived == 0 ||
		    g_mission_fg_stats[group].waves_remaining != 0) {
			return 0;
		}
		for (uint16_t slot = g_active_region_object_slot_start;
		     slot < g_active_region_craft_object_slot_end; ++slot) {
			if (g_object_table[slot].object_type != 0 &&
			    g_object_table[slot].flight_group_idx == group) {
				return 0;
			}
		}
	}
	return 1;
}

/* Whether every other flight group that arrives from the craft's group as
 * its mothership has arrived with no waves left, the 1997 order 33's test.
 * A group whose arrival is off and that no player owns is left out, as is a
 * group that does not arrive by mothership or arrives from another group.
 * Unlike books_all_returned it does not ask that no craft of the group is
 * left in the battle, and it looks at the arrival fields, not the departure
 * ones. A craft whose group launches no other group gets yes. */
static int books_all_launched(uint16_t object_index)
{
	uint16_t own_group = g_object_table[object_index].flight_group_idx;
	for (uint16_t group = 0; group < g_mission_header.num_flight_groups;
	     ++group) {
		const struct mission_flight_group *other =
			&g_mission_flight_groups[group];
		if ((g_mission_fg_stats[group].arrival_enabled == 0 &&
		     other->player_owner_idx == -1) ||
		    group == own_group || other->fg.arrival_method == 0 ||
		    other->fg.arrival_mothership != own_group) {
			continue;
		}
		if (g_mission_fg_stats[group].has_arrived == 0 ||
		    g_mission_fg_stats[group].waves_remaining != 0) {
			return 0;
		}
	}
	return 1;
}

/* The mission point reference of the craft's home point, where it leaves by
 * hyperspace, the 1997 home target's: point 12 for a captured craft when that
 * point is on, else point 13 when it is on; 0 for none. */
static unsigned int books_home_point_ref(uint16_t object_index)
{
	const struct mission_flight_group *group = books_group(object_index);
	if (books_craft(object_index)->captured_by_flight_group != 0 &&
	    group->fg.mission_point_enabled[BOOKS_CAPTURED_HOME_POINT] != 0) {
		return BOOKS_CAPTURED_HOME_POINT_REF;
	}
	return group->fg.mission_point_enabled[BOOKS_HOME_POINT] != 0
		       ? BOOKS_HOME_POINT_REF
		       : 0;
}

/* Whether the craft still flies its orders, the only time the 1997 plans
 * kept its books: it is active, its activity is none of going home, entering
 * a hangar or leaving by hyperspace, and it has not aborted, withdrawn or
 * finished its orders. */
static int books_on_orders(const struct craft_data *craft)
{
	uint8_t activity = craft->ai_controller.activity;
	return craft->object_kind == CRAFT_OBJECT_KIND_ACTIVE &&
	       activity != AI_ACTIVITY_GOING_HOME &&
	       activity != AI_ACTIVITY_GOING_HOME_EVADING &&
	       activity != AI_ACTIVITY_ENTERING_HANGAR &&
	       activity != AI_ACTIVITY_LEAVING_BY_HYPERSPACE &&
	       craft->ai_flight.mission_aborted_flag == 0 &&
	       craft->ai_flight.depart_timer_flag != BOOKS_DEPARTURE_DUE &&
	       craft->ai_flight.go_home_flag == 0;
}

/* Whether the group's departure comes now, order 38's 1997 test: its
 * departure time reached on the mission clock, minutes and seconds only, or
 * its departure trigger pair, with a condition in either half, holding. */
static int books_departure_comes(const struct mission_flight_group *group)
{
	uint8_t minutes = group->fg.departure_clock_min;
	uint8_t seconds = group->fg.departure_clock_sec;
	int comes = minutes + seconds != 0 &&
		    (g_mission_elapsed_clock.minutes > minutes ||
		     (g_mission_elapsed_clock.minutes == minutes &&
		      g_mission_elapsed_clock.seconds >= seconds));
	const struct mission_trigger_pair *trigger =
		&group->fg.departure_trigger;
	if ((trigger->triggers[0].condition != 0 ||
	     trigger->triggers[1].condition != 0) &&
	    (mission_evaluate_trigger_pair(trigger, 0) & 1) != 0) {
		comes = 1;
	}
	return comes;
}

/* Has the tactical officer announce the craft's withdrawal and tells the
 * local player, with the craft's model and group: message 385 when the HUD
 * shows no number for the craft, else 386. */
static void books_announce_withdrawal(uint16_t object_index)
{
	const struct craft_data *craft = books_craft(object_index);
	uint16_t group = g_object_table[object_index].flight_group_idx;
	fsfx_speak_tactical_officer_event(TACTICAL_VOICE_STATUS,
					  TACTICAL_MSG_WITHDRAWING,
					  object_index, UINT16_MAX);
	g_msg_sender_iff = (uint8_t)g_object_table[object_index].mobj->iff;
	msg_add_message_ptr(0, &g_model_defs[craft->model_index]);
	msg_add_message_ptr(1, &g_mission_flight_groups[group]);
	if (hud_mission_fg_get_craft_number_if_shown(group, craft) == 0) {
		msg_emit_in_flight_message(
			IFMSG_385_ARG_ARG_WITHDRAWING_FROM_COMBAT_AREA,
			g_local_player);
	} else {
		msg_emit_in_flight_message(
			IFMSG_386_FLIGHT_GROUP_ARG_ARG_WITHDRAWING_FROM_COMBAT_AREA,
			g_local_player);
	}
}

/* A leader's departure, order 38's 1997 rule, for a craft that can move:
 * when its group's departure comes it takes the mission clock as its
 * departure clock, its group counts one more craft not yet departed, and it
 * is marked departing; once the group's departure delay has passed since that
 * clock, by order 38's sum of the clock's fields, its withdrawal is announced
 * and it is marked due. */
static void books_keep_departure(uint16_t object_index)
{
	struct craft_data *craft = books_craft(object_index);
	struct ai_flight_state *flight = &craft->ai_flight;
	const struct mission_flight_group *group = books_group(object_index);
	uint16_t group_index = g_object_table[object_index].flight_group_idx;
	if (flight->max_speed_cache == 0) {
		return;
	}
	if (flight->depart_timer_flag == 0 && books_departure_comes(group)) {
		flight->depart_clock_hours = g_mission_elapsed_clock.hours;
		flight->depart_clock_minutes = g_mission_elapsed_clock.minutes;
		flight->depart_clock_seconds = g_mission_elapsed_clock.seconds;
		books_count(group_index, craft,
			    FLIGHT_GROUP_OUTCOME_NOT_DEPARTED);
		flight->depart_timer_flag = BOOKS_DEPARTURE_STARTED;
		XVT_LOG_DEBUG(
			"ai.departure_started object=%d fg=%d from=\"own\" count=%u minutes=%d seconds=%d predicted=%d",
			(int)object_index, (int)group_index,
			(unsigned)g_mission_fg_stats[group_index].outcome_count
				[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED],
			(int)flight->depart_clock_minutes,
			(int)flight->depart_clock_seconds,
			g_flight_sim_side_effects_suppressed);
	}
	if (flight->depart_timer_flag != BOOKS_DEPARTURE_STARTED) {
		return;
	}
	unsigned int delay =
		BOOKS_SECONDS_PER_MINUTE * group->fg.departure_delay_minutes +
		group->fg.departure_delay_seconds;
	unsigned int elapsed = BOOKS_SECONDS_PER_MINUTE *
				       (g_mission_elapsed_clock.minutes +
					BOOKS_MINUTES_PER_HOUR *
						(g_mission_elapsed_clock.hours -
						 flight->depart_clock_hours) -
					flight->depart_clock_minutes) -
			       flight->depart_clock_seconds +
			       g_mission_elapsed_clock.seconds;
	if (delay != 0 && elapsed < delay) {
		return;
	}
	flight->depart_timer_flag = BOOKS_DEPARTURE_DUE;
	books_announce_withdrawal(object_index);
	if (g_flight_sim_side_effects_suppressed == 0) {
		XVT_LOG_INFO("ai.withdrew object=%d fg=%d tick=%d",
			     (int)object_index, (int)group_index, g_game_time);
	}
	XVT_LOG_DEBUG(
		"ai.withdraw_timing object=%d delay=%u elapsed=%u predicted=%d",
		(int)object_index, delay, elapsed,
		g_flight_sim_side_effects_suppressed);
}

/* A follower's books, order 41's 1997 rule: it takes its leader's
 * orders-done mark; its departure mark with the leader's departure clock,
 * its group counting one more craft not yet departed when it first departs;
 * and its leader's order slot. */
static void books_follow_leader(uint16_t object_index)
{
	struct craft_data *craft = books_craft(object_index);
	struct ai_flight_state *flight = &craft->ai_flight;
	const struct craft_data *leader = books_craft(craft->leader_obj_idx);
	uint16_t group_index = g_object_table[object_index].flight_group_idx;
	if (leader->ai_flight.go_home_flag == 1 && flight->go_home_flag == 0) {
		flight->go_home_flag = 1;
		XVT_LOG_DEBUG(
			"ai.orders_done object=%d fg=%d from=\"leader\" predicted=%d",
			(int)object_index, (int)group_index,
			g_flight_sim_side_effects_suppressed);
	}
	uint8_t departing = leader->ai_flight.depart_timer_flag;
	if (departing != 0 && flight->depart_timer_flag != departing) {
		if (flight->depart_timer_flag == 0) {
			books_count(group_index, craft,
				    FLIGHT_GROUP_OUTCOME_NOT_DEPARTED);
		}
		flight->depart_timer_flag = departing;
		flight->depart_clock_hours =
			leader->ai_flight.depart_clock_hours;
		flight->depart_clock_minutes =
			leader->ai_flight.depart_clock_minutes;
		flight->depart_clock_seconds =
			leader->ai_flight.depart_clock_seconds;
		XVT_LOG_DEBUG(
			"ai.departure_started object=%d fg=%d from=\"leader\" count=%u minutes=%d seconds=%d predicted=%d",
			(int)object_index, (int)group_index,
			(unsigned)g_mission_fg_stats[group_index].outcome_count
				[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED],
			(int)flight->depart_clock_minutes,
			(int)flight->depart_clock_seconds,
			g_flight_sim_side_effects_suppressed);
	}
	uint8_t slot = leader->ai_controller.current_order_slot;
	if (craft->ai_controller.current_order_slot != slot) {
		XVT_LOG_DEBUG(
			"ai.order_moved object=%d from=%d to=%d state=0 why=\"leader\" predicted=%d",
			(int)object_index,
			(int)craft->ai_controller.current_order_slot, (int)slot,
			g_flight_sim_side_effects_suppressed);
		craft->ai_controller.current_order_slot = slot;
	}
}

/* Whether any warhead launcher of the craft with a warhead type still holds
 * a warhead in one of its slots. */
static int books_has_warheads(const struct craft_data *craft)
{
	for (int launcher = 0; launcher < craft->warhead_launcher_count;
	     ++launcher) {
		if (craft->warhead_slot_type_ids[launcher] == 0) {
			continue;
		}
		const struct model_def *model =
			&g_model_defs[craft->model_index];
		for (int slot = model->warhead_launcher_first_slot[launcher];
		     slot <= model->warhead_launcher_last_slot[launcher];
		     ++slot) {
			if (craft->weapon_slots[slot].ammo_count != 0) {
				return 1;
			}
		}
	}
	return 0;
}

/* Whether the craft's shields, both banks, are at most fraction of its most. */
static int books_shields_at_most(uint16_t object_index, uint16_t fraction)
{
	const struct craft_data *craft = books_craft(object_index);
	return (unsigned int)(craft->shield_energy[0] +
			      craft->shield_energy[1]) <=
	       (uint16_t)math2_fraction(
		       (uint16_t)craft_get_object_max_shield(object_index),
		       fraction);
}

/* Whether the group's abort trigger holds for the craft, order 15's 1997
 * tests, with the reason message 387 gives the group's player: 1 shields out
 * (the shield system down, or both banks empty on a freighter or starship),
 * 2 cannons down, 3 warheads out (the launcher system down, or no warhead
 * left), 4 hull damage at half its strength, 5 attacked by any team, 6 and 7
 * shields at half and a quarter of their most, 8 and 9 hull damage at a
 * quarter and three quarters. 1997 gave trigger 2 the warheads-out reason. */
static int books_abort_holds(uint16_t object_index, uint16_t *reason)
{
	const struct craft_data *craft = books_craft(object_index);
	uint8_t genus = g_object_table[object_index].genus_id;
	switch (books_group(object_index)->fg.abort_trigger) {
	case 1:
		*reason = IFMSG_390_SHIELDS_OUT;
		return (craft->working_subsystems &
			CRAFT_SUBSYSTEM_FLAG_SHIELDS) == 0 ||
		       ((genus == BOOKS_GENUS_FREIGHTER ||
			 genus == CRAFT_GENUS_STARSHIP) &&
			craft->shield_energy[0] + craft->shield_energy[1] == 0);
	case 2:
		*reason = IFMSG_087_DAMAGED_AND_INOPERATIVE;
		return (craft->working_subsystems &
			CRAFT_SUBSYSTEM_FLAG_CANNONS) == 0;
	case 3:
		*reason = IFMSG_394_WARHEADS_OUT;
		return (craft->working_subsystems &
			CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER) == 0 ||
		       !books_has_warheads(craft);
	case 4:
		*reason = IFMSG_392_HULL_AT_50;
		return math2_longfraction(craft->hull_max, 0x8000) <=
		       craft->hull_damage;
	case 5:
		*reason = IFMSG_395_UNDER_ATTACK;
		for (int team = 0; team < BOOKS_TEAMS; ++team) {
			if (craft->attacked_by_team[team] != 0) {
				return 1;
			}
		}
		return 0;
	case 6:
		*reason = IFMSG_388_SHIELDS_AT_50;
		return books_shields_at_most(object_index, 0x8000);
	case 7:
		*reason = IFMSG_389_SHIELDS_AT_25;
		return books_shields_at_most(object_index, 0x4000);
	case 8:
		*reason = IFMSG_391_HULL_AT_75;
		return math2_longfraction(craft->hull_max, 0x4000) <=
		       craft->hull_damage;
	case 9:
		*reason = IFMSG_393_HULL_AT_25;
		return math2_longfraction(craft->hull_max, 0xC000) <=
		       craft->hull_damage;
	default:
		return 0;
	}
}

/* A craft's abort, order 15's 1997 rule, for a craft that can move: when its
 * group's abort trigger holds, its group counts one more aborted craft, a
 * departure under way is undone (one fewer not yet departed), the tactical
 * officer announces the withdrawal, the group's player is told why with
 * message 387, and the craft is marked aborted. */
static void books_keep_abort(uint16_t object_index)
{
	struct craft_data *craft = books_craft(object_index);
	const struct mission_flight_group *group = books_group(object_index);
	uint16_t group_index = g_object_table[object_index].flight_group_idx;
	uint16_t reason = 0;
	if (craft->ai_flight.max_speed_cache == 0 ||
	    !books_abort_holds(object_index, &reason)) {
		return;
	}
	int special =
		books_count(group_index, craft, FLIGHT_GROUP_OUTCOME_ABORTED);
	if (g_flight_sim_side_effects_suppressed == 0) {
		XVT_LOG_INFO(
			"ai.mission_aborted object=%d fg=%d trigger=%d aborted=%u departing=%d tick=%d",
			(int)object_index, (int)group_index,
			(int)group->fg.abort_trigger,
			(unsigned)g_mission_fg_stats[group_index]
				.outcome_count[FLIGHT_GROUP_OUTCOME_ABORTED],
			(int)craft->ai_flight.depart_timer_flag, g_game_time);
	}
	if (craft->ai_flight.depart_timer_flag != 0) {
		--g_mission_fg_stats[group_index]
			  .outcome_count[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED];
		if (special) {
			g_mission_fg_stats[group_index].special_cargo_outcome
				[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED] = 0;
		}
		craft->ai_flight.depart_timer_flag = 0;
	}
	fsfx_speak_tactical_officer_event(TACTICAL_VOICE_STATUS,
					  TACTICAL_MSG_WITHDRAWING,
					  object_index, UINT16_MAX);
	if (group->player_owner_idx != -1) {
		g_msg_sender_iff =
			(uint8_t)g_object_table[object_index].mobj->iff;
		msg_add_message_ptr(0, group->fg.name);
		g_msg_arg_table[1] =
			(uint16_t)hud_mission_fg_get_craft_number_if_shown(
				group_index, craft);
		g_msg_arg_table[2] = reason;
		msg_emit_in_flight_message(
			IFMSG_387_WINGMAN_ARG_ARG_ABORTING_MISSION_ARG,
			group->player_owner_idx);
	}
	XVT_LOG_DEBUG(
		"ai.abort_state object=%d front=%d rear=%d hull=%u hull_max=%u special=%d reason=%u predicted=%d",
		(int)object_index, craft->shield_energy[0],
		craft->shield_energy[1], (unsigned)craft->hull_damage,
		(unsigned)craft->hull_max, special, (unsigned)reason,
		g_flight_sim_side_effects_suppressed);
	craft->ai_flight.mission_aborted_flag = 1;
}

/* Whether the craft may skip to order 4: it leads or flies alone, flies its
 * orders and has not skipped, and its group's skip-to-order-4 trigger pair,
 * with a condition other than always true in either half, holds. */
static int books_skip_due(uint16_t object_index)
{
	const struct craft_data *craft = books_craft(object_index);
	const struct mission_trigger_pair *skip =
		&books_group(object_index)->fg.skip_to_order4;
	return craft->leader_obj_idx == UINT8_MAX &&
	       (books_kept(object_index) & BOOKS_KEEPS_SKIP) != 0 &&
	       craft->ai_controller.skipped_to_order4 == 0 &&
	       books_on_orders(craft) &&
	       (skip->triggers[0].condition != MISSION_COND_ALWAYS_TRUE ||
		skip->triggers[1].condition != MISSION_COND_ALWAYS_TRUE) &&
	       (mission_evaluate_trigger_pair(skip, 0) & 1) != 0;
}

/* Marks the craft's orders done, order 39's 1997 rule, after slot was marked
 * done: for a craft that can move and did not skip to order 4, once every
 * order its group has in slots 0 to 2 is done (state 2). */
static void books_check_orders_done(uint16_t object_index, uint8_t slot)
{
	struct craft_data *craft = books_craft(object_index);
	const struct ai_order_progress *progress =
		&craft->ai_controller.order_progress;
	const struct mission_flight_group *group = books_group(object_index);
	uint8_t state = progress->completion_state[slot];
	if ((state != BOOKS_SLOT_DONE &&
	     state != BOOKS_SLOT_NO_BOARDING_TARGET) ||
	    craft->ai_flight.max_speed_cache == 0 ||
	    craft->ai_controller.skipped_to_order4 != 0 ||
	    craft->ai_flight.go_home_flag != 0) {
		return;
	}
	int orders = 0;
	int done = 0;
	for (int order = 0; order < BOOKS_ORDER_SLOTS; ++order) {
		if (group->fg.orders[order].order != 0) {
			++orders;
			done += progress->completion_state[order] ==
				BOOKS_SLOT_DONE;
		}
	}
	if (orders == 0 || done != orders) {
		return;
	}
	craft->ai_flight.go_home_flag = 1;
	XVT_LOG_DEBUG(
		"ai.orders_done object=%d fg=%d from=\"own\" predicted=%d",
		(int)object_index,
		(int)g_object_table[object_index].flight_group_idx,
		g_flight_sim_side_effects_suppressed);
}

/* Whether the craft's group leaves by mothership: for a captured craft, its
 * group's captured departure; else its departure method. */
static int books_by_mothership(uint16_t object_index)
{
	const struct mission_flight_group *group = books_group(object_index);
	if (books_craft(object_index)->captured_by_flight_group != 0) {
		return group->fg.captured_depart_via_mothership != 0;
	}
	return group->fg.departure_method != 0;
}

/* The craft the computer craft goes home into, the 1997 fly-home order's
 * choice, and in outcome the outcome docking there records: for a captured
 * craft, the leader of the captured departure group when its group leaves
 * that way; else the departure group's leader when its group leaves by
 * mothership, else, or with none there, the alternate group's when the group
 * uses one. A craft of a player's flight group is never one. UINT16_MAX for
 * none. */
static uint16_t books_mothership(uint16_t object_index, uint16_t *outcome)
{
	const struct mission_flight_group *group = books_group(object_index);
	uint16_t mothership = UINT16_MAX;
	*outcome = FLIGHT_GROUP_OUTCOME_PRIMARY_MOTHERSHIP_DEPENDENT;
	if (books_craft(object_index)->captured_by_flight_group != 0) {
		*outcome = FLIGHT_GROUP_OUTCOME_CAPTURED_MOTHERSHIP_DEPENDENT;
		if (group->fg.captured_depart_via_mothership != 0) {
			mothership = ai_brain_find_mothership(
				group->fg.captured_departure_mothership);
		}
	} else {
		if (group->fg.departure_method != 0) {
			mothership = ai_brain_find_mothership(
				group->fg.departure_mothership);
		}
		if (mothership == UINT16_MAX &&
		    group->fg.alternate_mothership_used != 0) {
			mothership = ai_brain_find_mothership(
				group->fg.alternate_mothership);
			*outcome =
				FLIGHT_GROUP_OUTCOME_ALTERNATE_MOTHERSHIP_DEPENDENT;
		}
	}
	if (mothership != UINT16_MAX &&
	    g_mission_flight_groups[g_object_table[mothership].flight_group_idx]
			    .player_owner_idx != -1) {
		mothership = UINT16_MAX;
	}
	return mothership;
}

/* Writes into point the world position of the point side, up and forward of
 * the mothership's model axes. */
static void books_world_point(uint16_t mothership, int side, int up,
			      int forward, int32_t point[3])
{
	const struct object_record *object = &g_object_table[mothership];
	flight_geometry_local_to_world(object, side, up, forward);
	point[0] = g_rotated_x + object->world_x;
	point[1] = g_rotated_y + object->world_y;
	point[2] = g_rotated_z + object->world_z;
}

/* Writes the view's order fields and starts its books bits: each slot's
 * order code and completion state; the current slot's throttle as the craft
 * throttle it gives (0 for an index past the table), variables and speed;
 * and the orders-done, aborted, departure-due, skip-due, skipped,
 * targets-left and all-returned bits. */
static void books_view_orders(uint16_t object_index,
			      struct ai_wire_craft_view *view)
{
	const struct craft_data *craft = books_craft(object_index);
	const struct ai_controller *controller = &craft->ai_controller;
	const struct mission_flight_group *group = books_group(object_index);
	for (int slot = 0; slot < AI_WIRE_ORDER_SLOTS; ++slot) {
		view->order_codes[slot] = group->fg.orders[slot].order;
		view->order_states[slot] =
			controller->order_progress.completion_state[slot];
	}
	const struct mission_order *order =
		&group->fg.orders[controller->current_order_slot];
	view->order_throttle =
		order->throttle < BOOKS_THROTTLE_ENTRIES
			? g_order_throttle_to_craft_throttle_speed
				  [order->throttle]
			: 0;
	view->order_variable1 = order->variable1;
	view->order_variable2 = order->variable2;
	view->order_speed = order->speed;
	view->full_speed = books_full_speed(craft);
	unsigned int books = 0;
	books |= craft->ai_flight.go_home_flag != 0 ? AI_WIRE_BOOKS_ORDERS_DONE
						    : 0;
	books |= craft->ai_flight.mission_aborted_flag != 0
			 ? AI_WIRE_BOOKS_ABORTED
			 : 0;
	books |= craft->ai_flight.depart_timer_flag == BOOKS_DEPARTURE_DUE
			 ? AI_WIRE_BOOKS_DEPARTURE_DUE
			 : 0;
	books |= books_skip_due(object_index) ? AI_WIRE_BOOKS_SKIP_DUE : 0;
	books |= controller->skipped_to_order4 != 0 ? AI_WIRE_BOOKS_SKIPPED : 0;
	books |= books_targets_left(object_index) ? AI_WIRE_BOOKS_TARGETS_LEFT
						  : 0;
	if (books_all_returned(object_index)) {
		books |= AI_WIRE_BOOKS_ALL_RETURNED;
	}
	if (books_all_launched(object_index)) {
		books |= AI_WIRE_BOOKS_ALL_LAUNCHED;
	}
	view->books = books;
	view->targets_now = 0;
	for (int slot = 0; slot < BOOKS_ORDER_SLOTS; ++slot) {
		if (books_slot_targets_now(object_index, slot)) {
			view->targets_now |= (uint8_t)(1u << slot);
		}
	}
}

/* Writes the view's start point and its books bit: the world position of
 * the mission point the craft's group arrived at, once the group has arrived
 * and its start point reference names one of its points; else 0, 0, 0 and the
 * bit clear. mission_init sets the reference once for every group, point 0 or
 * a random enabled variant 1 to 3, and nothing changes it. */
static void books_view_start_point(uint16_t object_index,
				   struct ai_wire_craft_view *view)
{
	const struct object_record *object = &g_object_table[object_index];
	const uint16_t group = object->flight_group_idx;
	unsigned int ref = g_mission_fg_stats[group].current_mission_point_ref;
	for (int axis = 0; axis < AI_WIRE_POINT_AXES; ++axis) {
		view->start_point[axis] = 0;
	}
	view->books &= ~(uint32_t)AI_WIRE_BOOKS_START_POINT;
	if (g_mission_fg_stats[group].has_arrived == 0 ||
	    ref < BOOKS_START_POINT_REF ||
	    ref >= BOOKS_START_POINT_REF + BOOKS_MISSION_POINTS) {
		return;
	}
	mission_resolve_object_or_mission_point_world_loc(BOOKS_START_POINT_REF,
							  group);
	view->start_point[0] = g_world_loc_x;
	view->start_point[1] = g_world_loc_y;
	view->start_point[2] = g_world_loc_z;
	view->books |= AI_WIRE_BOOKS_START_POINT;
}

/* Writes the view's way home and adds its bits: how its group leaves; may
 * jump for an active craft whose model has a hyperdrive and whose group
 * leaves by hyperspace; its mothership with its outside and inside hangar
 * points, and at hangar for an active craft within the dock range of the
 * inside one, by rough distance; and its group's mission point 13 when that
 * point is on. Points not given are 0. */
static void books_view_home(uint16_t object_index,
			    struct ai_wire_craft_view *view)
{
	const struct object_record *object = &g_object_table[object_index];
	const struct craft_data *craft = object->mobj->p_craft;
	int active = craft->object_kind == CRAFT_OBJECT_KIND_ACTIVE;
	unsigned int books = view->books;
	uint16_t outcome;
	uint16_t mothership = books_mothership(object_index, &outcome);
	for (int axis = 0; axis < 3; ++axis) {
		view->outside_hangar[axis] = 0;
		view->inside_hangar[axis] = 0;
		view->home_point[axis] = 0;
	}
	int may_jump =
		active && g_model_defs[craft->model_index].has_hyperdrive != 0;
	if (books_by_mothership(object_index)) {
		books |= AI_WIRE_BOOKS_BY_MOTHERSHIP;
		may_jump = may_jump && mothership == UINT16_MAX &&
			   books_motherships_all_arrived(object_index);
	}
	books |= may_jump ? AI_WIRE_BOOKS_MAY_JUMP : 0;
	view->mothership = mothership;
	if (mothership != UINT16_MAX) {
		const struct model_hangar_points *hangar =
			&g_model_defs[g_object_table[mothership]
					      .mobj->p_craft->model_index]
				 .hangar_points;
		books_world_point(mothership, hangar->outside.side,
				  hangar->outside.up, hangar->outside.forward,
				  view->outside_hangar);
		books_world_point(mothership, hangar->inside.side,
				  hangar->inside.up, hangar->inside.forward,
				  view->inside_hangar);
		trig2_ctop(view->inside_hangar[0] - object->world_x,
			   view->inside_hangar[1] - object->world_y,
			   view->inside_hangar[2] - object->world_z);
		int range = object->genus_id == CRAFT_GENUS_STARSHIP
				    ? BOOKS_DOCK_RANGE_STARSHIP
				    : BOOKS_DOCK_RANGE;
		if (active && trig2_polardistance < range) {
			books |= AI_WIRE_BOOKS_AT_HANGAR;
		}
	}
	unsigned int home = books_home_point_ref(object_index);
	if (home != 0) {
		mission_resolve_object_or_mission_point_world_loc(
			home, object->flight_group_idx);
		view->home_point[0] = g_world_loc_x;
		view->home_point[1] = g_world_loc_y;
		view->home_point[2] = g_world_loc_z;
		books |= AI_WIRE_BOOKS_HOME_POINT;
	}
	view->books = books;
}

int ai_books_order_boards(int order_code)
{
	return (order_code >= MISSION_ORDER_BOARD_AND_GIVE_CARGO &&
		order_code <= MISSION_ORDER_PICK_UP) ||
	       order_code == MISSION_ORDER_SS_BOARD ||
	       order_code == MISSION_ORDER_BOARD_TO_REPAIR;
}

/* The world point a craft of the genus and model of the boarder waits at
 * above the target's docking hatch, 1997's approach point: the up offset
 * depends on whether the target and the boarder are small craft (genus below
 * 2) or big ones, in 16-bit arithmetic as the 1997 code did, and falls back to
 * 0x7000 when it is negative. Writes it into point. */
static void books_approach_point(uint16_t object_index, uint16_t target,
				 int32_t point[AI_WIRE_POINT_AXES])
{
	const struct object_record *object = &g_object_table[object_index];
	const struct object_record *aimed = &g_object_table[target];
	const struct model_def *own =
		&g_model_defs[books_craft(object_index)->model_index];
	const struct model_def *other =
		&g_model_defs[books_craft(target)->model_index];
	int16_t initial;
	if (aimed->genus_id < BOOKS_DOCK_SMALL_GENUS_LIMIT) {
		initial =
			(int16_t)(other->dock_from_up[0] - own->dock_to_up[0]);
	} else if (object->genus_id < BOOKS_DOCK_SMALL_GENUS_LIMIT) {
		initial =
			(int16_t)(other->dock_from_up[1] +
				  other->dock_from_up[0] - own->dock_to_up[1]);
	} else {
		initial = (int16_t)(2 * other->dock_from_up[1] -
				    own->dock_to_up[1]);
	}
	int16_t extra = (int16_t)(other->dock_from_up[1] - own->dock_to_up[1]);
	int16_t up = (int16_t)(extra + extra + initial);
	if (up < 0) {
		up = BOOKS_DOCK_UP_FALLBACK;
	}
	flight_geometry_local_to_world_i16(aimed, 0, up, other->dock_forward);
	point[0] = g_rotated_x + aimed->world_x;
	point[1] = g_rotated_y + aimed->world_y;
	point[2] = g_rotated_z + aimed->world_z;
}

/* Asks the player flying the moving target to stop, 1997's ask, when that
 * player is the local one and the ask is not already queued: the message
 * (set the throttle to 0 so reload craft can dock) and the warning sound. */
static void books_ask_player_to_stop(uint16_t object_index, uint16_t target)
{
	int player = g_object_table[target].player_owner_idx;
	if (player != g_local_player ||
	    g_flight_sim_side_effects_suppressed != 0) {
		return;
	}
	for (int slot = 0; slot < BOOKS_READY_MESSAGE_SLOTS; ++slot) {
		if (g_ready_message_pane_queue[slot].state_or_message_id ==
		    IFMSG_260_SET_YOUR_THROTTLE_TO_0_SO_RELOAD_CRAFT_CAN_DOCK_WITH_YOU) {
			return;
		}
	}
	g_msg_sender_iff = books_group(object_index)->fg.iff;
	msg_emit_in_flight_message(
		IFMSG_260_SET_YOUR_THROTTLE_TO_0_SO_RELOAD_CRAFT_CAN_DOCK_WITH_YOU,
		g_local_player);
	fsfx_play_sound(FLIGHT_SOUND_GENERAL_WARNING, -1, g_local_player);
	XVT_LOG_DEBUG("ai.board_asked_stop object=%d target=%d slot=%d",
		      (int)object_index, (int)target, g_local_player);
}

uint32_t ai_books_board_bits(uint16_t object_index,
			     int32_t point[AI_WIRE_POINT_AXES],
			     uint16_t *ask_target)
{
	const struct object_record *object = &g_object_table[object_index];
	const uint16_t target =
		books_craft(object_index)->ai_controller.target_obj_idx;
	for (int axis = 0; axis < AI_WIRE_POINT_AXES; ++axis) {
		point[axis] = 0;
	}
	*ask_target = UINT16_MAX;
	if (!ai_books_order_boards(mission_current_order_code(object_index)) ||
	    target == object_index ||
	    target < g_active_region_object_slot_start ||
	    target >= g_active_region_craft_object_slot_end ||
	    g_object_table[target].object_type == 0 ||
	    g_object_table[target].mobj->family != 0) {
		return 0;
	}
	books_approach_point(object_index, target, point);
	trig2_ctop(point[0] - object->world_x, point[1] - object->world_y,
		   point[2] - object->world_z);
	if (trig2_polardistance > BOOKS_BOARD_POINT_RANGE) {
		return AI_WIRE_BOOKS_BOARD_POINT;
	}
	if (g_object_table[target].player_owner_idx != -1 &&
	    g_object_table[target].mobj->speed != 0) {
		*ask_target = target;
		return AI_WIRE_BOOKS_BOARD_POINT;
	}
	return AI_WIRE_BOOKS_BOARD_POINT | AI_WIRE_BOOKS_AT_BOARD_POINT;
}

/* A delivered group's total the 8-bit counters cannot reach. */
enum { BOOKS_DROP_COUNTER_MAX = 255 };

/* Groups whose over-large total was already warned of, a bit for each; only
 * the log line reads it. */
static uint64_t books_drop_total_warned;

uint32_t ai_books_drop_bits(uint16_t object_index, uint8_t drop_phase,
			    int32_t point[AI_WIRE_POINT_AXES])
{
	const struct object_record *object = &g_object_table[object_index];
	const struct craft_data *craft = books_craft(object_index);
	const struct ai_controller *controller = &craft->ai_controller;
	for (int axis = 0; axis < AI_WIRE_POINT_AXES; ++axis) {
		point[axis] = 0;
	}
	if (mission_current_order_code(object_index) !=
	    MISSION_ORDER_DROP_OFF) {
		return 0;
	}
	const unsigned int variable2 =
		books_group(object_index)
			->fg.orders[controller->current_order_slot]
			.variable2;
	const int in_mission =
		variable2 >= 1 &&
		(int)variable2 <= g_mission_header.num_flight_groups;
	if (!in_mission) {
		return drop_phase == 0 ? AI_WIRE_BOOKS_ALL_DROPPED : 0;
	}
	const uint16_t group = (uint16_t)(variable2 - 1);
	const unsigned int total =
		g_mission_fg_stats[group]
			.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL];
	const unsigned int delivered =
		controller->order_progress
			.goal_progress[controller->current_order_slot];
	const int arrived =
		g_mission_fg_stats[group]
			.outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED] != 0;
	uint32_t bits = 0;
	if (delivered >= total) {
		bits |= AI_WIRE_BOOKS_ALL_DROPPED;
	}
	if (total > BOOKS_DROP_COUNTER_MAX) {
		if (drop_phase == 0) {
			bits |= AI_WIRE_BOOKS_ALL_DROPPED;
		}
		if (g_flight_sim_side_effects_suppressed == 0 && group < 64 &&
		    (books_drop_total_warned & ((uint64_t)1 << group)) == 0) {
			books_drop_total_warned |= (uint64_t)1 << group;
			XVT_LOG_WARN(
				"ai.dropoff_total_too_big object=%d group=%d total=%u max=%d",
				(int)object_index, (int)group, total,
				BOOKS_DROP_COUNTER_MAX);
		}
		return bits;
	}
	if (drop_phase == 0 && arrived) {
		bits |= AI_WIRE_BOOKS_ALL_DROPPED;
	}
	if (delivered >= total || craft->leader_obj_idx != UINT8_MAX) {
		return bits;
	}
	mission_resolve_formation_slot_world_loc(group, 0, UINT16_MAX);
	point[0] = g_world_loc_x;
	point[1] = g_world_loc_y;
	point[2] = g_world_loc_z + BOOKS_DROP_POINT_RAISE;
	bits |= AI_WIRE_BOOKS_DROP_POINT;
	trig2_ctop(point[0] - object->world_x, point[1] - object->world_y,
		   point[2] - object->world_z);
	if (trig2_polardistance < BOOKS_DROP_POINT_RANGE && !arrived) {
		bits |= AI_WIRE_BOOKS_AT_DROP_POINT;
	}
	return bits;
}

/* Writes what a boarder sees: the board point and the books' two bits for the
 * craft's AI target (ai_books_board_bits), asking a moving player's craft to
 * stop; each order slot's goal progress; and the signatures it has docked
 * with. */
static void books_view_boarding(uint16_t object_index,
				struct ai_wire_craft_view *view)
{
	const struct craft_data *craft = books_craft(object_index);
	uint16_t ask_target;
	view->books |= ai_books_board_bits(object_index, view->board_point,
					   &ask_target);
	if (ask_target != UINT16_MAX) {
		books_ask_player_to_stop(object_index, ask_target);
	}
	for (int slot = 0; slot < AI_WIRE_ORDER_SLOTS; ++slot) {
		view->order_progress[slot] =
			craft->ai_controller.order_progress.goal_progress[slot];
	}
	view->boarded_count = craft->ai_flight.docked_target_count;
	if (view->boarded_count > AI_WIRE_BOARDED_MAX) {
		view->boarded_count = AI_WIRE_BOARDED_MAX;
	}
	for (int slot = 0; slot < AI_WIRE_BOARDED_MAX; ++slot) {
		view->boarded[slot] =
			slot < view->boarded_count
				? craft->ai_flight
					  .docked_target_signatures[slot]
				: 0;
	}
	if (craft->captured_by_flight_group != 0) {
		view->books |= AI_WIRE_BOOKS_CAPTURED;
	}
	view->books |= ai_books_drop_bits(
		object_index, ai_dropoff_phase(object_index), view->drop_point);
}

void ai_books_view_craft(uint16_t object_index, struct ai_wire_craft_view *view)
{
	const struct craft_data *craft = books_craft(object_index);
	if (books_on_orders(craft)) {
		if (craft->leader_obj_idx != UINT8_MAX) {
			books_follow_leader(object_index);
		} else if ((books_kept(object_index) & BOOKS_KEEPS_DEPARTURE) !=
			   0) {
			books_keep_departure(object_index);
		}
	}
	/* As in 1997, a departure that came due ends the craft's orders
	 * before its abort is tested. */
	if (books_on_orders(craft) &&
	    (books_kept(object_index) & BOOKS_KEEPS_ABORT) != 0) {
		books_keep_abort(object_index);
	}
	books_view_orders(object_index, view);
	books_view_home(object_index, view);
	books_view_start_point(object_index, view);
	books_view_boarding(object_index, view);
}

int ai_books_static_is_target(uint16_t object_index)
{
	const struct object_record *object = &g_object_table[object_index];
	return object->object_type != 0 &&
	       (g_object_type_table[object->object_type].behavior_flags &
		BOOKS_STATIC_TARGET_FLAG) != 0 &&
	       books_target_counts(object_index);
}

void ai_books_apply_order(uint16_t object_index, uint8_t slot, uint8_t state)
{
	struct ai_controller *controller =
		&books_craft(object_index)->ai_controller;
	uint8_t from = controller->current_order_slot;
	if (state != 0) {
		controller->order_progress.completion_state[from] = state;
	}
	if (slot == BOOKS_SKIP_SLOT && from != BOOKS_SKIP_SLOT) {
		controller->skipped_to_order4 = 1;
	}
	controller->current_order_slot = slot;
	books_check_orders_done(object_index, from);
	XVT_LOG_DEBUG(
		"ai.order_moved object=%d from=%d to=%d state=%d why=\"program\" predicted=%d",
		(int)object_index, (int)from, (int)slot, (int)state,
		g_flight_sim_side_effects_suppressed);
}

void ai_books_depart(uint16_t object_index)
{
	struct craft_data *craft = books_craft(object_index);
	craft->object_kind = CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE;
	craft->ai_flight.roll_state = 0;
	craft->ai_flight.pitch_state = 0;
	craft->ai_flight.turn_state = 0;
	craft->ai_controller.maneuver_timer = BOOKS_JUMP_TIMER;
	craft->ai_controller.secondary_maneuver_timer = BOOKS_JUMP_SECOND_TIMER;
	craft->throttle_speed = UINT16_MAX;
	XVT_LOG_DEBUG("ai.craft_depart object=%d speed=%u predicted=%d",
		      (int)object_index,
		      (unsigned)g_object_table[object_index].mobj->speed,
		      g_flight_sim_side_effects_suppressed);
}

void ai_books_dock(uint16_t object_index)
{
	uint16_t outcome;
	uint16_t mothership = books_mothership(object_index, &outcome);
	XVT_LOG_DEBUG(
		"ai.craft_dock object=%d hangar=%d docked=%d predicted=%d",
		(int)object_index,
		mothership == UINT16_MAX ? -1 : (int)mothership,
		mothership != UINT16_MAX, g_flight_sim_side_effects_suppressed);
	if (mothership == UINT16_MAX) {
		return;
	}
	books_count_departure(object_index);
	books_leave(object_index, outcome, IFMSG_141_HAS_ENTERED_HANGAR);
	if (g_flight_sim_side_effects_suppressed == 0) {
		XVT_LOG_INFO(
			"ai.entered_hangar object=%d fg=%d hangar=%d outcome=%d tick=%d",
			(int)object_index,
			(int)g_object_table[object_index].flight_group_idx,
			(int)mothership, (int)outcome, g_game_time);
	}
}

void ai_books_finish_jumps(void)
{
	for (uint16_t object_index =
		     (uint16_t)g_active_region_object_slot_start;
	     object_index < g_active_region_craft_object_slot_end;
	     ++object_index) {
		const struct object_record *object =
			&g_object_table[object_index];
		if (object->object_type == 0 || object->mobj->family != 0 ||
		    object->player_owner_idx != -1 ||
		    object->mobj->p_craft->object_kind !=
			    CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE ||
		    object->mobj->speed < BOOKS_JUMP_SPEED) {
			continue;
		}
		const struct craft_data *craft = object->mobj->p_craft;
		books_count_departure(object_index);
		if (g_flight_sim_side_effects_suppressed == 0) {
			XVT_LOG_INFO(
				"ai.hyperspace_departed object=%d fg=%d craft=%d captured=%d carried=%d tick=%d",
				(int)object_index,
				(int)object->flight_group_idx,
				(int)object->object_type,
				craft->captured_by_flight_group != 0,
				craft->carried_object_index == UINT16_MAX
					? -1
					: (int)craft->carried_object_index,
				g_game_time);
		}
		books_leave(object_index, FLIGHT_GROUP_OUTCOME_LEFT_REGION,
			    IFMSG_135_HAS_ENTERED_HYPERSPACE);
	}
}
