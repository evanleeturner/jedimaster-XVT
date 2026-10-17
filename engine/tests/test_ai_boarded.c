/* Tests for xvt/flight/ai_boarded.c, the boarded side of a boarding: a
 * computer craft on Disabled or Await Boarding is switched off, counts the
 * boardings done to it and has its systems back at its order's variable1,
 * and an Await Boarding craft is released when its abort or departure mark is
 * set. The rules are checked against the 1997 plans they were ported from
 * (paiorder_makedisabledorder, paiorder_awaitboardorder and the restore in
 * the abort and stop orders), on worlds this file builds: no game data is
 * read. The two 1997 faults the port does not keep (a count with no end, a
 * captured craft switched off again) are checked gone. */
#include <SDL3/SDL_log.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/ai_boarded.h"
#include "xvt/flight/ai_books.h"
#include "xvt/flight/ai_brain.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/ai/ai_wire.h"
#include "xvt_runtime/log/log.h"

enum {
	SLOT_COUNT = 6,
	GROUP_COUNT = 4,
	TEST_GROUP = 2,
	LEADER = 1,
	FOLLOWER = 2,
	/* The boardings an order asks for, and a systems mask no fault writes. */
	NEEDED = 2,
	SYSTEMS = 0x03FF,
	TEST_TICK = 0x2000,
	DEPARTURE_STARTED = 1,
	DEPARTURE_DUE = 2,
};

static struct object_record g_objects[SLOT_COUNT];
static struct mobile_object g_mobiles[SLOT_COUNT];
static struct craft_data g_craft[SLOT_COUNT];
static char g_test_message[] = "\003message";
static int g_repaired[SLOT_COUNT];

void __wrap_msg_emit_craft_message(uint16_t obj_idx,
				   const struct craft_data *craft,
				   int16_t msg_template_id);

/* Counts the "has been repaired" messages each craft is given. */
void __wrap_msg_emit_craft_message(uint16_t obj_idx,
				   const struct craft_data *craft,
				   int16_t msg_template_id)
{
	(void)craft;
	if (msg_template_id == IFMSG_138_HAS_BEEN_REPAIRED &&
	    obj_idx < SLOT_COUNT) {
		++g_repaired[obj_idx];
	}
}

enum {
	LOG_LINE_CAPACITY = 64,
	LOG_LINE_SIZE = 240,
};

static char g_log_lines[LOG_LINE_CAPACITY][LOG_LINE_SIZE];
static int g_log_line_count;

static void catch_line(void *userdata, int category, SDL_LogPriority priority,
		       const char *message)
{
	static const char prefix[] = "xvt: ";
	(void)userdata;
	(void)category;
	(void)priority;
	if (g_log_line_count >= LOG_LINE_CAPACITY) {
		return;
	}
	if (strncmp(message, prefix, sizeof prefix - 1) == 0) {
		message += sizeof prefix - 1;
	}
	snprintf(g_log_lines[g_log_line_count++], LOG_LINE_SIZE, "%s", message);
}

static int lines_starting(const char *start)
{
	int found = 0;
	for (int line = 0; line < g_log_line_count; ++line) {
		found += strncmp(g_log_lines[line], start, strlen(start)) == 0;
	}
	return found;
}

/* Empty slots, GROUP_COUNT groups with no player, the clocks at 0. */
static void fresh_world(void)
{
	memset(g_objects, 0, sizeof g_objects);
	memset(g_mobiles, 0, sizeof g_mobiles);
	memset(g_craft, 0, sizeof g_craft);
	memset(g_repaired, 0, sizeof g_repaired);
	memset(g_mission_flight_groups, 0, sizeof g_mission_flight_groups);
	memset(g_mission_fg_stats, 0,
	       GROUP_COUNT * sizeof g_mission_fg_stats[0]);
	memset(&g_mission_elapsed_clock, 0, sizeof g_mission_elapsed_clock);
	memset(&g_mission_header, 0, sizeof g_mission_header);
	memset(&g_flight_mission_state, 0, sizeof g_flight_mission_state);
	memset(g_players, 0, sizeof g_players);
	for (int player = 0; player < 8; ++player) {
		g_players[player].object_index = -1;
	}
	g_mission_header.num_flight_groups = GROUP_COUNT;
	g_object_table = g_objects;
	g_mobile_object_pool_base = g_mobiles;
	g_craft_data_pool_base = g_craft;
	g_craft_data_pool_capacity = SLOT_COUNT;
	g_region_main_object_slot_end = SLOT_COUNT;
	g_region_static_object_slot_count = 0;
	g_local_transient_slot_start = 0;
	g_local_debris_slot_end = 0;
	g_mobile_object_char_data_count = 0;
	g_projectile_object_slots_total = 0;
	for (int slot = 0; slot < SLOT_COUNT; ++slot) {
		g_objects[slot].mobj = &g_mobiles[slot];
		g_objects[slot].player_owner_idx = -1;
		g_mobiles[slot].p_craft = &g_craft[slot];
	}
	for (int group = 0; group < GROUP_COUNT; ++group) {
		g_mission_flight_groups[group].player_owner_idx = -1;
	}
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = SLOT_COUNT;
	g_game_time = TEST_TICK;
	g_flight_sim_side_effects_suppressed = 0;
	for (size_t id = 0; id < sizeof g_str_in_flight_messages /
					 sizeof g_str_in_flight_messages[0];
	     ++id) {
		g_str_in_flight_messages[id] = g_test_message;
	}
	g_log_line_count = 0;
	ai_brain_set_exchange(NULL);
}

static struct craft_data *make_craft(int slot)
{
	g_objects[slot].object_type = 5;
	g_objects[slot].object_signature = (uint16_t)(0x100 + slot);
	g_objects[slot].flight_group_idx = TEST_GROUP;
	g_objects[slot].genus_id = 1;
	struct craft_data *craft = &g_craft[slot];
	craft->object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	craft->leader_obj_idx = UINT8_MAX;
	craft->ai_flight.max_speed_cache = 100;
	craft->system_flags = SYSTEMS;
	craft->working_subsystems = SYSTEMS;
	craft->carried_object_index = UINT16_MAX;
	craft->carrier_obj_idx = UINT16_MAX;
	craft->craft_ordinal = (uint8_t)(slot + 1);
	return craft;
}

/* A working craft in slot LEADER whose group's first order is order, asking
 * for NEEDED boardings. */
static struct craft_data *boarded_on(int order)
{
	fresh_world();
	g_mission_flight_groups[TEST_GROUP].fg.orders[0].order = (uint8_t)order;
	g_mission_flight_groups[TEST_GROUP].fg.orders[0].variable1 = NEEDED;
	return make_craft(LEADER);
}

static void check_switch_off(void)
{
	static const int offs[] = {MISSION_ORDER_DISABLED,
				   MISSION_ORDER_AWAIT_BOARDING};
	for (size_t row = 0; row < 2; ++row) {
		struct craft_data *craft = boarded_on(offs[row]);
		ai_boarded_step(LEADER);
		XVT_ASSERT_INT_EQ(craft->working_subsystems, 0);
		XVT_ASSERT_INT_EQ(lines_starting("ai.self_disabled"), 1);
		/* Off already: switched off again, said once. */
		ai_boarded_step(LEADER);
		XVT_ASSERT_INT_EQ(craft->working_subsystems, 0);
		XVT_ASSERT_INT_EQ(lines_starting("ai.self_disabled"), 1);
	}
	/* Another order is left alone, and so is a player's craft. */
	struct craft_data *craft = boarded_on(MISSION_ORDER_ATTACK);
	ai_boarded_step(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, SYSTEMS);
	craft = boarded_on(MISSION_ORDER_DISABLED);
	g_objects[LEADER].player_owner_idx = 0;
	ai_boarded_step(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, SYSTEMS);
	/* A captured craft: its systems were restored by the capture. */
	craft = boarded_on(MISSION_ORDER_AWAIT_BOARDING);
	craft->captured_by_flight_group = 0x81;
	ai_boarded_step(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, SYSTEMS);
	/* The slot's goal met: left alone. */
	craft = boarded_on(MISSION_ORDER_DISABLED);
	craft->ai_controller.order_progress.goal_progress[0] = NEEDED;
	ai_boarded_step(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, SYSTEMS);
}

static void check_count(void)
{
	/* States 2 and 3 count, state 1 (Take) never. */
	for (int state = 1; state <= 3; ++state) {
		struct craft_data *craft = boarded_on(MISSION_ORDER_DISABLED);
		craft->boarding_state = (uint8_t)state;
		ai_boarded_step(LEADER);
		XVT_ASSERT_INT_EQ(
			craft->ai_controller.order_progress.goal_progress[0],
			state == 1 ? 0 : 1);
		XVT_ASSERT_INT_EQ(lines_starting("ai.boarding_counted"),
				  state == 1 ? 0 : 1);
		/* Below variable1 the state is cleared, so one boarding counts
		 * once over several steps. */
		XVT_ASSERT_INT_EQ(craft->boarding_state, state == 1 ? 1 : 0);
		ai_boarded_step(LEADER);
		ai_boarded_step(LEADER);
		XVT_ASSERT_INT_EQ(
			craft->ai_controller.order_progress.goal_progress[0],
			state == 1 ? 0 : 1);
		XVT_ASSERT_INT_EQ(craft->working_subsystems, 0);
	}
}

static void check_restore(void)
{
	/* Order 5 at equality: systems back, damage cleared, the message. */
	struct craft_data *craft = boarded_on(MISSION_ORDER_DISABLED);
	craft->subsystem_damage = 7;
	craft->ai_flight.times_boarded = NEEDED;
	craft->ai_controller.order_progress.goal_progress[0] = NEEDED - 1;
	craft->boarding_state = 2;
	ai_boarded_step(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, SYSTEMS);
	XVT_ASSERT_INT_EQ(craft->subsystem_damage, 0);
	XVT_ASSERT_INT_EQ(g_repaired[LEADER], 1);
	XVT_ASSERT_INT_EQ(lines_starting("ai.boarded_restored"), 1);
	/* The goal met, later steps leave it alone. */
	ai_boarded_step(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, SYSTEMS);
	XVT_ASSERT_INT_EQ(g_repaired[LEADER], 1);
	/* Past variable1 it restores, with no message; order 6 never has it. */
	craft = boarded_on(MISSION_ORDER_DISABLED);
	craft->ai_flight.times_boarded = NEEDED + 1;
	craft->ai_controller.order_progress.goal_progress[0] = NEEDED - 1;
	craft->boarding_state = 3;
	ai_boarded_step(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, SYSTEMS);
	XVT_ASSERT_INT_EQ(g_repaired[LEADER], 0);
	craft = boarded_on(MISSION_ORDER_AWAIT_BOARDING);
	craft->ai_flight.times_boarded = NEEDED;
	craft->ai_controller.order_progress.goal_progress[0] = NEEDED - 1;
	craft->boarding_state = 2;
	ai_boarded_step(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, SYSTEMS);
	XVT_ASSERT_INT_EQ(g_repaired[LEADER], 0);
	/* Take counts for the restore (times boarded) and not for the goal. */
	craft = boarded_on(MISSION_ORDER_DISABLED);
	craft->ai_flight.times_boarded = NEEDED;
	craft->boarding_state = 1;
	ai_boarded_step(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, 0);
}

/* A follower, which cannot move its order on, stops counting at variable1. */
static void check_follower_stops(void)
{
	struct craft_data *craft = boarded_on(MISSION_ORDER_AWAIT_BOARDING);
	struct craft_data *follower = make_craft(FOLLOWER);
	follower->leader_obj_idx = LEADER;
	(void)craft;
	follower->ai_flight.times_boarded = NEEDED;
	for (int step = 0; step < 6; ++step) {
		follower->boarding_state = 2;
		ai_boarded_step(FOLLOWER);
	}
	XVT_ASSERT_INT_EQ(
		follower->ai_controller.order_progress.goal_progress[0],
		NEEDED);
	XVT_ASSERT_INT_EQ(follower->working_subsystems, SYSTEMS);
	XVT_ASSERT_INT_EQ(lines_starting("ai.boarding_counted"), NEEDED);
}

/* A craft taken back by its own side is switched off again. */
static void check_taken_back(void)
{
	struct craft_data *craft = boarded_on(MISSION_ORDER_AWAIT_BOARDING);
	craft->ai_flight.mission_aborted_flag = 1;
	ai_boarded_step(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, SYSTEMS);
	craft->ai_flight.mission_aborted_flag = 0;
	ai_boarded_step(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, 0);
	/* A due departure releases it too; Disabled is never released. */
	craft->working_subsystems = SYSTEMS;
	craft->ai_flight.depart_timer_flag = DEPARTURE_DUE;
	ai_boarded_step(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, SYSTEMS);
	craft->ai_flight.depart_timer_flag = DEPARTURE_STARTED;
	ai_boarded_step(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, 0);
	craft = boarded_on(MISSION_ORDER_DISABLED);
	craft->ai_flight.mission_aborted_flag = 1;
	craft->ai_flight.depart_timer_flag = DEPARTURE_DUE;
	ai_boarded_step(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, 0);
}

static void check_release_direct(void)
{
	struct craft_data *craft = boarded_on(MISSION_ORDER_AWAIT_BOARDING);
	craft->working_subsystems = 0;
	ai_boarded_release(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, SYSTEMS);
	XVT_ASSERT_INT_EQ(lines_starting("ai.boarded_released"), 1);
	/* Damaged: nothing. Order 5: never released. */
	craft = boarded_on(MISSION_ORDER_AWAIT_BOARDING);
	craft->working_subsystems = 0;
	craft->subsystem_damage = 4;
	ai_boarded_release(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, 0);
	craft = boarded_on(MISSION_ORDER_DISABLED);
	craft->working_subsystems = 0;
	ai_boarded_release(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, 0);
	XVT_ASSERT_INT_EQ(lines_starting("ai.boarded_released"), 0);
}

static struct ai_wire_craft_view view_of(int slot)
{
	struct ai_wire_craft_view view;
	memset(&view, 0xEE, sizeof view);
	ai_books_view_craft((uint16_t)slot, &view);
	return view;
}

static void clock_at(int hours, int minutes, int seconds)
{
	g_mission_elapsed_clock.hours = (uint8_t)hours;
	g_mission_elapsed_clock.minutes = (uint8_t)minutes;
	g_mission_elapsed_clock.seconds = (uint8_t)seconds;
}

/* The departure coming due releases an Await Boarding craft once, and a
 * ion hit after stays. */
static void check_release_on_departure(void)
{
	struct craft_data *craft = boarded_on(MISSION_ORDER_AWAIT_BOARDING);
	struct mission_flight_group *group =
		&g_mission_flight_groups[TEST_GROUP];
	group->fg.departure_clock_min = 1;
	group->fg.departure_clock_sec = 30;
	group->fg.departure_delay_seconds = 20;
	ai_boarded_step(LEADER);
	clock_at(0, 1, 30);
	view_of(LEADER);
	XVT_ASSERT_INT_EQ(craft->ai_flight.depart_timer_flag,
			  DEPARTURE_STARTED);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, 0);
	clock_at(0, 1, 50);
	view_of(LEADER);
	XVT_ASSERT_INT_EQ(craft->ai_flight.depart_timer_flag, DEPARTURE_DUE);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, SYSTEMS);
	XVT_ASSERT_INT_EQ(lines_starting("ai.boarded_released"), 1);
	/* An ion hit after the release stays: neither the step nor later
	 * books put the systems back. */
	craft->working_subsystems = 0x1;
	craft->subsystem_damage = 3;
	ai_boarded_step(LEADER);
	clock_at(0, 2, 10);
	view_of(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, 0x1);
	XVT_ASSERT_INT_EQ(lines_starting("ai.boarded_released"), 1);
}

/* A follower taking its leader's mark is released only when it is due. */
static void check_release_on_follower(void)
{
	struct craft_data *leader = boarded_on(MISSION_ORDER_AWAIT_BOARDING);
	struct craft_data *follower = make_craft(FOLLOWER);
	follower->leader_obj_idx = LEADER;
	leader->ai_flight.depart_timer_flag = DEPARTURE_STARTED;
	follower->working_subsystems = 0;
	view_of(FOLLOWER);
	XVT_ASSERT_INT_EQ(follower->ai_flight.depart_timer_flag,
			  DEPARTURE_STARTED);
	XVT_ASSERT_INT_EQ(follower->working_subsystems, 0);
	leader->ai_flight.depart_timer_flag = DEPARTURE_DUE;
	view_of(FOLLOWER);
	XVT_ASSERT_INT_EQ(follower->ai_flight.depart_timer_flag, DEPARTURE_DUE);
	XVT_ASSERT_INT_EQ(follower->working_subsystems, SYSTEMS);
	XVT_ASSERT_INT_EQ(lines_starting("ai.boarded_released"), 1);
}

/* The abort mark going up releases an Await Boarding craft, and never a
 * Disabled one (whose plan kept no abort). */
static void check_release_on_abort(void)
{
	struct craft_data *craft = boarded_on(MISSION_ORDER_AWAIT_BOARDING);
	g_mission_flight_groups[TEST_GROUP].fg.abort_trigger = 2;
	craft->working_subsystems = 0;
	view_of(LEADER);
	XVT_ASSERT_INT_EQ(craft->ai_flight.mission_aborted_flag, 1);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, SYSTEMS);
	XVT_ASSERT_INT_EQ(lines_starting("ai.boarded_released"), 1);
	/* The mark holds: no second release. */
	craft->working_subsystems = 0;
	view_of(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, 0);
	XVT_ASSERT_INT_EQ(lines_starting("ai.boarded_released"), 1);
	craft = boarded_on(MISSION_ORDER_DISABLED);
	g_mission_flight_groups[TEST_GROUP].fg.abort_trigger = 2;
	craft->working_subsystems = 0;
	view_of(LEADER);
	XVT_ASSERT_INT_EQ(craft->ai_flight.mission_aborted_flag, 0);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, 0);
}

/* A Rendezvous craft counts the boardings done to it and is never switched
 * off or restored: not at the start, not when a boarding lands, not at
 * variable1. A captured craft and a slot whose goal is met are left alone. */
static void check_rendezvous_counts_only(void)
{
	struct craft_data *craft = boarded_on(MISSION_ORDER_RENDEZVOUS);
	ai_boarded_step(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, SYSTEMS);
	XVT_ASSERT_INT_EQ(lines_starting("ai.self_disabled"), 0);
	/* States 2 and 3 count, state 1 never; below variable1 the state is
	 * cleared. */
	for (int state = 1; state <= 3; ++state) {
		craft = boarded_on(MISSION_ORDER_RENDEZVOUS);
		craft->boarding_state = (uint8_t)state;
		ai_boarded_step(LEADER);
		XVT_ASSERT_INT_EQ(
			craft->ai_controller.order_progress.goal_progress[0],
			state == 1 ? 0 : 1);
		XVT_ASSERT_INT_EQ(craft->boarding_state, state == 1 ? 1 : 0);
		ai_boarded_step(LEADER);
		XVT_ASSERT_INT_EQ(
			craft->ai_controller.order_progress.goal_progress[0],
			state == 1 ? 0 : 1);
		XVT_ASSERT_INT_EQ(craft->working_subsystems, SYSTEMS);
	}
	/* At variable1 nothing is restored: damage stays, no message, no line. */
	craft = boarded_on(MISSION_ORDER_RENDEZVOUS);
	craft->subsystem_damage = 7;
	craft->working_subsystems = SYSTEMS - 1;
	craft->ai_flight.times_boarded = NEEDED;
	craft->ai_controller.order_progress.goal_progress[0] = NEEDED - 1;
	craft->boarding_state = 2;
	ai_boarded_step(LEADER);
	XVT_ASSERT_INT_EQ(craft->ai_controller.order_progress.goal_progress[0],
			  NEEDED);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, SYSTEMS - 1);
	XVT_ASSERT_INT_EQ(craft->subsystem_damage, 7);
	XVT_ASSERT_INT_EQ(g_repaired[LEADER], 0);
	XVT_ASSERT_INT_EQ(lines_starting("ai.boarded_restored"), 0);
	/* The goal met: a further boarding is not counted. */
	craft->boarding_state = 2;
	ai_boarded_step(LEADER);
	XVT_ASSERT_INT_EQ(craft->ai_controller.order_progress.goal_progress[0],
			  NEEDED);
	/* Captured: left alone. */
	craft = boarded_on(MISSION_ORDER_RENDEZVOUS);
	craft->captured_by_flight_group = 0x81;
	craft->boarding_state = 2;
	ai_boarded_step(LEADER);
	XVT_ASSERT_INT_EQ(craft->ai_controller.order_progress.goal_progress[0],
			  0);
	XVT_ASSERT_INT_EQ(craft->boarding_state, 2);
	/* Never released: an abort mark leaves its systems as they are. */
	craft = boarded_on(MISSION_ORDER_RENDEZVOUS);
	craft->working_subsystems = SYSTEMS - 1;
	ai_boarded_release(LEADER);
	XVT_ASSERT_INT_EQ(craft->working_subsystems, SYSTEMS - 1);
	XVT_ASSERT_INT_EQ(lines_starting("ai.boarded_released"), 0);
}

int main(void)
{
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	SDL_SetLogOutputFunction(catch_line, NULL);
	check_switch_off();
	check_count();
	check_restore();
	check_rendezvous_counts_only();
	check_follower_stops();
	check_taken_back();
	check_release_direct();
	check_release_on_departure();
	check_release_on_follower();
	check_release_on_abort();
	return 0;
}
