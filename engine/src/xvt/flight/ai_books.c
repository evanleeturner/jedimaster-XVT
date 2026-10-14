/* The mission's books for the computer craft, ported from the 1997 AI's
 * orders 15, 38, 39 and 41, its into-hyperspace maneuver and its hangar order
 * (paiorder.c and paiman.c, deleted with the 1997 AI). The counts follow the
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
 * and the orders-done, aborted, departure-due, skip-due and skipped bits. */
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
	view->books = (uint16_t)books;
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
	if (books_by_mothership(object_index)) {
		books |= AI_WIRE_BOOKS_BY_MOTHERSHIP;
	} else if (active &&
		   g_model_defs[craft->model_index].has_hyperdrive != 0) {
		books |= AI_WIRE_BOOKS_MAY_JUMP;
	}
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
	if (books_group(object_index)
		    ->fg.mission_point_enabled[BOOKS_HOME_POINT] != 0) {
		mission_resolve_object_or_mission_point_world_loc(
			BOOKS_HOME_POINT_REF, object->flight_group_idx);
		view->home_point[0] = g_world_loc_x;
		view->home_point[1] = g_world_loc_y;
		view->home_point[2] = g_world_loc_z;
		books |= AI_WIRE_BOOKS_HOME_POINT;
	}
	view->books = (uint16_t)books;
}

void ai_books_view_craft(uint16_t object_index, struct ai_wire_craft_view *view)
{
	const struct craft_data *craft = books_craft(object_index);
	if (books_on_orders(craft)) {
		if (craft->leader_obj_idx != UINT8_MAX) {
			books_follow_leader(object_index);
		} else {
			books_keep_departure(object_index);
		}
	}
	/* As in 1997, a departure that came due ends the craft's orders
	 * before its abort is tested. */
	if (books_on_orders(craft)) {
		books_keep_abort(object_index);
	}
	books_view_orders(object_index, view);
	books_view_home(object_index, view);
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
