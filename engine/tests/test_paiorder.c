/* Tests for xvt/flight/ai/paiorder.c, the orders of the computer pilots' plans.
 * Each check builds the world it needs in the game's own tables: three flight
 * groups, a few craft in a small object table this file owns, and one plan
 * whose bytes change nothing. An order runs as it does in a think: the craft's
 * context is set up first with pai_setupcraftcontext. No game data is read. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/object_type.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/ai/paiman.h"
#include "xvt/flight/ai/paiorder.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/config.h"
#include "xvt/util/game_rand.h"

enum {
	TEST_OBJECT_COUNT = 8,
	TEST_XWING = 1,	     /* Craft type 1 is object type 1, a starfighter. */
	TEST_NO_ORDINAL = 9, /* A craft ordinal no test craft has. */
	TEST_FAR = 0x100000, /* Far from every hangar point. */
	TICKS_PER_FIVE_SECONDS = 1180, /* Five times 236 ticks a second. */
};

static struct object_record g_test_objects[TEST_OBJECT_COUNT];
static struct mobile_object g_test_mobiles[TEST_OBJECT_COUNT];
static struct craft_data g_test_craft[TEST_OBJECT_COUNT];
/* Every plan's bytes: keep the target, keep the maneuver. */
static uint8_t g_test_plan[4] = {255, 255, 0, 0};
/* Every in-flight message's text, a system message: with system messages
 * off, the message code drops it. */
static const char g_test_message[] = "\x03message";

/* Three flight groups of X-wings on teams and IFFs 0, 1 and 2, none owned by
 * a player and none with a special cargo craft. Every plan points at the
 * plan above, and every in-flight message's text is a plain line. The object
 * table is this file's eight free slots, all of them the active region's
 * craft slots; the local player is slot 7, flying no craft, and the tactical
 * officer is off. The game's random generator starts from a fixed state. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_mission_flight_groups, 0, sizeof g_mission_flight_groups);
	memset(g_mission_fg_stats, 0, sizeof g_mission_fg_stats);
	memset(&g_mission_header, 0, sizeof g_mission_header);
	memset(&g_mission_elapsed_clock, 0, sizeof g_mission_elapsed_clock);
	memset(g_players, 0, sizeof g_players);
	memset(g_plan_table, 0, sizeof g_plan_table);
	memset(g_builtin_plan_id_by_name_index, 0,
	       sizeof g_builtin_plan_id_by_name_index);
	memset(g_order_leader_builtin_plan_name_index, 0,
	       sizeof g_order_leader_builtin_plan_name_index);
	memset(g_msg_arg_table, 0, sizeof g_msg_arg_table);
	for (int plan = 0; plan < 256; ++plan) {
		g_plan_data_ptrs[plan] = g_test_plan;
	}
	for (size_t id = 0; id < sizeof g_str_in_flight_messages /
					 sizeof g_str_in_flight_messages[0];
	     ++id) {
		g_str_in_flight_messages[id] = g_test_message;
	}
	g_object_table = g_test_objects;
	for (int i = 0; i < TEST_OBJECT_COUNT; ++i) {
		g_test_objects[i].player_owner_idx = -1;
		g_test_objects[i].mobj = &g_test_mobiles[i];
		g_test_mobiles[i].p_craft = &g_test_craft[i];
		g_test_craft[i].leader_obj_idx = UINT8_MAX;
		g_test_craft[i].carried_object_index = UINT16_MAX;
		g_test_craft[i].last_attacker_obj_idx = UINT16_MAX;
		g_test_craft[i].ai_flight.max_speed_cache = 100;
	}
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
		for (int slot = 0; slot < 4; ++slot) {
			g_players[i].target_preset_slot[slot] = -1;
		}
	}
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = TEST_OBJECT_COUNT;
	g_mission_header.num_flight_groups = 3;
	for (int fg = 0; fg < 3; ++fg) {
		g_mission_flight_groups[fg].fg.craft_type = TEST_XWING;
		g_mission_flight_groups[fg].fg.team = (uint8_t)fg;
		g_mission_flight_groups[fg].fg.iff = (uint8_t)fg;
		g_mission_flight_groups[fg].fg.special_cargo_craft =
			TEST_NO_ORDINAL;
		g_mission_flight_groups[fg].player_owner_idx = -1;
	}
	g_local_player = 7;
	g_system_message_display_enabled = 0;
	g_game_config.voice_tactical_officer_level = 0;
	g_flight_sim_side_effects_suppressed = 0;
	g_game_rand_value_state = 0x1234;
	g_game_rand_feedback_state = 0x5678;
}

/* Puts a craft of flight group fg in slot obj at x along the X axis, with
 * the group's team and IFF. */
static struct craft_data *place_craft(int obj, int fg, int x)
{
	g_test_objects[obj].object_type = 1;
	g_test_objects[obj].object_signature = (uint16_t)(100 + obj);
	g_test_objects[obj].flight_group_idx = (uint8_t)fg;
	g_test_objects[obj].world_x = x;
	g_test_mobiles[obj].team = g_mission_flight_groups[fg].fg.team;
	g_test_mobiles[obj].iff = g_mission_flight_groups[fg].fg.iff;
	g_test_craft[obj].craft_ordinal = (uint8_t)obj;
	return &g_test_craft[obj];
}

/* Sets up the think of the craft in slot obj, as pai_update_all_craft_ai
 * does before it runs the craft's plan. */
static void think_as(int obj)
{
	g_cur_craft = &g_test_craft[obj];
	pai_setupcraftcontext((uint16_t)obj);
}

/* Order 44 sets lifetime_timer to variable1 times 1,180 ticks when no
 * countdown runs, leaves a running countdown alone, and returns 0. */
static void check_kill_self_delay(void)
{
	fresh_world();
	place_craft(0, 0, 0);
	g_mission_flight_groups[0].fg.orders[0].variable1 = 3;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_killselforder(), 0);
	XVT_ASSERT_INT_EQ(g_test_mobiles[0].lifetime_timer,
			  3 * TICKS_PER_FIVE_SECONDS);

	g_test_mobiles[0].lifetime_timer = 7;
	XVT_ASSERT_INT_EQ(paiorder_killselforder(), 0);
	XVT_ASSERT_INT_EQ(g_test_mobiles[0].lifetime_timer, 7);
}

/* With variable1 0, order 44 picks 2 to 5 units of 1,180 ticks. */
static void check_kill_self_random_delay(void)
{
	fresh_world();
	place_craft(0, 0, 0);
	for (int draw = 0; draw < 16; ++draw) {
		g_test_mobiles[0].lifetime_timer = 0;
		think_as(0);
		paiorder_killselforder();
		uint16_t timer = g_test_mobiles[0].lifetime_timer;
		XVT_ASSERT_INT_EQ(timer % TICKS_PER_FIVE_SECONDS, 0);
		XVT_ASSERT_TRUE(timer >= 2 * TICKS_PER_FIVE_SECONDS);
		XVT_ASSERT_TRUE(timer <= 5 * TICKS_PER_FIVE_SECONDS);
	}
}

/* Order 37 returns 1 when the group's departure_method is set, else 0. */
static void check_hyperspace_departure_test(void)
{
	fresh_world();
	place_craft(0, 0, 0);
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_checkhyperorder(), 0);
	g_mission_flight_groups[0].fg.departure_method = 1;
	XVT_ASSERT_INT_EQ(paiorder_checkhyperorder(), 1);
}

/* Group 0: craft 0 leads, craft 1 follows it. */
static void leader_and_follower(void)
{
	fresh_world();
	place_craft(0, 0, 0);
	place_craft(1, 0, 0)->leader_obj_idx = 0;
}

/* Order 41: a follower takes its leader's go_home_flag and
 * depart_timer_flag, counting the not-departed outcome once, when it takes
 * the second. */
static void check_follow_takes_flags(void)
{
	leader_and_follower();
	think_as(1);
	paiorder_completefolloworder();
	XVT_ASSERT_INT_EQ(g_test_craft[1].ai_flight.go_home_flag, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].ai_flight.depart_timer_flag, 0);

	g_test_craft[0].ai_flight.go_home_flag = 1;
	g_test_craft[0].ai_flight.depart_timer_flag = 1;
	think_as(1);
	XVT_ASSERT_INT_EQ(paiorder_completefolloworder(), 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].ai_flight.go_home_flag, 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].ai_flight.depart_timer_flag, 1);
	uint16_t *counts = g_mission_fg_stats[0].outcome_count;
	XVT_ASSERT_INT_EQ(counts[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED], 1);

	think_as(1);
	paiorder_completefolloworder();
	XVT_ASSERT_INT_EQ(counts[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED], 1);

	/* A craft with no leader takes nothing from itself. */
	g_test_craft[0].ai_flight.go_home_flag = 0;
	g_test_craft[0].ai_flight.depart_timer_flag = 0;
	g_test_craft[1].leader_obj_idx = UINT8_MAX;
	think_as(1);
	paiorder_completefolloworder();
	XVT_ASSERT_INT_EQ(counts[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED], 1);
}

/* Order 41: when the leader works on another order slot, the follower moves
 * to it, with current_plan_id the order's leader plan and variable_plan_id
 * its follower plan, and returns 1; on the same slot it returns 0. */
static void check_follow_moves_to_leader_slot(void)
{
	leader_and_follower();
	g_mission_flight_groups[0].fg.orders[1].order = 3;
	g_order_leader_builtin_plan_name_index[3] = 11;
	g_builtin_plan_id_by_name_index[11] = 21;
	int follower_index = g_order_follower_builtin_plan_name_index[3];
	XVT_ASSERT_TRUE(follower_index != 11);
	g_builtin_plan_id_by_name_index[follower_index] = 22;

	think_as(1);
	XVT_ASSERT_INT_EQ(paiorder_completefolloworder(), 0);

	g_test_craft[0].ai_controller.current_order_slot = 1;
	think_as(1);
	XVT_ASSERT_INT_EQ(paiorder_completefolloworder(), 1);
	XVT_ASSERT_INT_EQ(g_pai_context.order_slot, 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].ai_controller.current_order_slot, 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].ai_controller.current_plan_id, 21);
	XVT_ASSERT_INT_EQ(g_pai_context.variable_plan_id, 22);
}

/* Group 0's slot 1 holds order 12, whose leader plan is plan 12, named
 * capldr1pln, and whose follower plan is plan 40. */
static void capital_order_in_slot_1(void)
{
	g_mission_flight_groups[0].fg.orders[1].order = 12;
	g_order_leader_builtin_plan_name_index[12] = 20;
	g_builtin_plan_id_by_name_index[20] = 12;
	int follower_index = g_order_follower_builtin_plan_name_index[12];
	XVT_ASSERT_TRUE(follower_index != 20);
	g_builtin_plan_id_by_name_index[follower_index] = 40;
	strcpy(g_plan_table[12].name, "capldr1pln");
}

/* Order 42 moves the craft to a later slot below 3 whose plan is capldr1pln:
 * the controller's current_order_slot, not g_pai_context.order_slot, then
 * current_plan_id to the leader plan and variable_plan_id to the leader plan
 * for a craft with no leader, the follower plan for a follower. */
static void check_wait_go_other_moves(void)
{
	fresh_world();
	place_craft(0, 0, 0);
	place_craft(1, 0, 0)->leader_obj_idx = 0;
	capital_order_in_slot_1();

	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_waitgootherorder(), 1);
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_controller.current_order_slot, 1);
	XVT_ASSERT_INT_EQ(g_pai_context.order_slot, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_controller.current_plan_id, 12);
	XVT_ASSERT_INT_EQ(g_pai_context.variable_plan_id, 12);

	think_as(1);
	XVT_ASSERT_INT_EQ(paiorder_waitgootherorder(), 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].ai_controller.current_plan_id, 12);
	XVT_ASSERT_INT_EQ(g_pai_context.variable_plan_id, 40);
}

/* Order 42 returns 0 after the skip to order 4, with no capital ship order
 * after the current slot, and for one in slot 3. */
static void check_wait_go_other_stays(void)
{
	fresh_world();
	place_craft(0, 0, 0);
	capital_order_in_slot_1();
	g_test_craft[0].ai_controller.skipped_to_order4 = 1;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_waitgootherorder(), 0);

	g_test_craft[0].ai_controller.skipped_to_order4 = 0;
	g_test_craft[0].ai_controller.current_order_slot = 1;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_waitgootherorder(), 0);

	g_test_craft[0].ai_controller.current_order_slot = 2;
	g_mission_flight_groups[0].fg.orders[3].order = 12;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_waitgootherorder(), 0);
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_controller.current_order_slot, 2);
}

/* Order 15 returns 0 at once for a craft that cannot move. */
static void check_abort_needs_speed(void)
{
	fresh_world();
	place_craft(0, 0, 0)->ai_flight.max_speed_cache = 0;
	g_mission_flight_groups[0].fg.abort_trigger = 2;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_abortmissionorder(), 0);
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.mission_aborted_flag, 0);
}

/* Order 15, trigger 2: the craft aborts once its cannons are down. The first
 * abort counts the aborted outcome, undoes the not-departed count and clears
 * depart_timer_flag; a later one counts nothing more. */
static void check_abort_on_cannons(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, 0, 0);
	g_mission_flight_groups[0].fg.abort_trigger = 2;
	craft->working_subsystems = CRAFT_SUBSYSTEM_FLAG_CANNONS;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_abortmissionorder(), 0);
	XVT_ASSERT_INT_EQ(craft->ai_flight.mission_aborted_flag, 0);

	uint16_t *counts = g_mission_fg_stats[0].outcome_count;
	counts[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED] = 1;
	craft->ai_flight.depart_timer_flag = 1;
	craft->working_subsystems = CRAFT_SUBSYSTEM_FLAG_SHIELDS;
	XVT_ASSERT_INT_EQ(paiorder_abortmissionorder(), 1);
	XVT_ASSERT_INT_EQ(craft->ai_flight.mission_aborted_flag, 1);
	XVT_ASSERT_INT_EQ(counts[FLIGHT_GROUP_OUTCOME_ABORTED], 1);
	XVT_ASSERT_INT_EQ(counts[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED], 0);
	XVT_ASSERT_INT_EQ(craft->ai_flight.depart_timer_flag, 0);

	XVT_ASSERT_INT_EQ(paiorder_abortmissionorder(), 1);
	XVT_ASSERT_INT_EQ(counts[FLIGHT_GROUP_OUTCOME_ABORTED], 1);
}

/* Order 15, trigger 4: the craft aborts once its hull damage reaches half of
 * hull_max; trigger 5, once any team has attacked it. */
static void check_abort_on_hull_and_attack(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, 0, 0);
	g_mission_flight_groups[0].fg.abort_trigger = 4;
	craft->hull_max = 0x10000;
	craft->hull_damage = 0x7FFF;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_abortmissionorder(), 0);
	craft->hull_damage = 0x8000;
	XVT_ASSERT_INT_EQ(paiorder_abortmissionorder(), 1);

	for (int team = 0; team < 10; team += 9) {
		fresh_world();
		craft = place_craft(0, 0, 0);
		g_mission_flight_groups[0].fg.abort_trigger = 5;
		think_as(0);
		XVT_ASSERT_INT_EQ(paiorder_abortmissionorder(), 0);
		craft->attacked_by_team[team] = 1;
		XVT_ASSERT_INT_EQ(paiorder_abortmissionorder(), 1);
	}
}

/* Order 15, trigger 1: in a group no player owns no message 387 is made;
 * in a group player 0 owns, the reason in its last argument is "shields
 * out". */
static void check_abort_reason_shields(void)
{
	fresh_world();
	place_craft(0, 0, 0);
	g_mission_flight_groups[0].fg.abort_trigger = 1;
	g_msg_arg_table[2] = 0x1234;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_abortmissionorder(), 1);
	XVT_ASSERT_INT_EQ(g_msg_arg_table[2], 0x1234);

	fresh_world();
	place_craft(0, 0, 0);
	g_mission_flight_groups[0].fg.abort_trigger = 1;
	g_mission_flight_groups[0].player_owner_idx = 0;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_abortmissionorder(), 1);
	XVT_ASSERT_INT_EQ(g_msg_arg_table[2], IFMSG_390_SHIELDS_OUT);
}

/* Group 0 departs at 1:10 with a delay of 80 seconds. */
static void timed_departure(void)
{
	fresh_world();
	place_craft(0, 0, 0);
	g_mission_flight_groups[0].fg.departure_clock_min = 1;
	g_mission_flight_groups[0].fg.departure_clock_sec = 10;
	g_mission_flight_groups[0].fg.departure_delay_minutes = 1;
	g_mission_flight_groups[0].fg.departure_delay_seconds = 20;
}

/* Order 38: at the departure time the craft records the clock, counts the
 * not-departed outcome once and sets depart_timer_flag; it returns 1 once
 * the delay has passed since then. */
static void check_departure_timing(void)
{
	timed_departure();
	struct ai_flight_state *flight = &g_test_craft[0].ai_flight;
	uint16_t *counts = g_mission_fg_stats[0].outcome_count;
	g_mission_elapsed_clock.minutes = 1;
	g_mission_elapsed_clock.seconds = 9;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_stopgohomeorder(), 0);
	XVT_ASSERT_INT_EQ(flight->depart_timer_flag, 0);

	g_mission_elapsed_clock.seconds = 10;
	XVT_ASSERT_INT_EQ(paiorder_stopgohomeorder(), 0);
	XVT_ASSERT_INT_EQ(flight->depart_timer_flag, 1);
	XVT_ASSERT_INT_EQ(flight->depart_clock_minutes, 1);
	XVT_ASSERT_INT_EQ(flight->depart_clock_seconds, 10);
	XVT_ASSERT_INT_EQ(counts[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED], 1);

	g_mission_elapsed_clock.minutes = 2;
	g_mission_elapsed_clock.seconds = 29;
	XVT_ASSERT_INT_EQ(paiorder_stopgohomeorder(), 0);
	XVT_ASSERT_INT_EQ(flight->depart_clock_seconds, 10);
	XVT_ASSERT_INT_EQ(counts[FLIGHT_GROUP_OUTCOME_NOT_DEPARTED], 1);

	g_mission_elapsed_clock.seconds = 30;
	XVT_ASSERT_INT_EQ(paiorder_stopgohomeorder(), 1);
}

/* Order 38 with a departure time of 1:01 and a delay of one second: the
 * departure starts at 1:01 and the craft withdraws at 1:02. A departure time
 * of 0:01 starts at 0:01. */
static void check_departure_short_times(void)
{
	timed_departure();
	g_mission_flight_groups[0].fg.departure_clock_sec = 1;
	g_mission_flight_groups[0].fg.departure_delay_minutes = 0;
	g_mission_flight_groups[0].fg.departure_delay_seconds = 1;
	g_mission_elapsed_clock.minutes = 1;
	g_mission_elapsed_clock.seconds = 1;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_stopgohomeorder(), 0);
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.depart_timer_flag, 1);
	g_mission_elapsed_clock.seconds = 2;
	XVT_ASSERT_INT_EQ(paiorder_stopgohomeorder(), 1);

	timed_departure();
	g_mission_flight_groups[0].fg.departure_clock_min = 0;
	g_mission_flight_groups[0].fg.departure_clock_sec = 1;
	g_mission_elapsed_clock.seconds = 1;
	think_as(0);
	paiorder_stopgohomeorder();
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.depart_timer_flag, 1);
}

/* Order 38 returns 0 at once for a craft that cannot move. */
static void check_departure_needs_speed(void)
{
	timed_departure();
	g_test_craft[0].ai_flight.max_speed_cache = 0;
	g_mission_elapsed_clock.minutes = 5;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_stopgohomeorder(), 0);
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.depart_timer_flag, 0);
}

/* Group 0 leaves by mothership: group 2's craft in slot 5. */
static void mothership_world(void)
{
	fresh_world();
	g_mission_flight_groups[0].fg.departure_method = 1;
	g_mission_flight_groups[0].fg.departure_mothership = 2;
	place_craft(5, 2, 0);
}

/* Order 4 targets the departure mothership, at its outside hangar point, and
 * returns 1 once within 2,048 of it; it sets separation to 1. */
static void check_fly_home_to_mothership(void)
{
	mothership_world();
	struct craft_data *craft = place_craft(0, 0, TEST_FAR);
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_flyhomeorder(), 0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_obj_idx, 5);
	XVT_ASSERT_INT_EQ(craft->ai_controller.has_live_target, 0);
	XVT_ASSERT_INT_EQ(craft->ai_flight.separation, 1);

	g_test_objects[0].world_x = craft->ai_controller.aim_point_x + 2048;
	g_test_objects[0].world_y = craft->ai_controller.aim_point_y;
	g_test_objects[0].world_z = craft->ai_controller.aim_point_z;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_flyhomeorder(), 0);
	g_test_objects[0].world_x -= 1;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_flyhomeorder(), 1);
}

/* Order 4 does not count a mothership in a player's group: the craft
 * targets mission point 13 when enabled, else its group's current point, and
 * the order returns 0. */
static void check_fly_home_skips_player_mothership(void)
{
	mothership_world();
	g_mission_flight_groups[2].player_owner_idx = 0;
	struct craft_data *craft = place_craft(0, 0, 0);
	g_test_objects[5].world_x = 0;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_flyhomeorder(), 0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_obj_idx, 0x8000);

	g_mission_flight_groups[0].fg.mission_point_enabled[13] = 1;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_flyhomeorder(), 0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_obj_idx, 0x800D);
}

/* Makes the craft in slot obj a follower of leader, flying in formation. */
static void follow(int obj, int leader)
{
	g_test_craft[obj].leader_obj_idx = (uint8_t)leader;
	g_test_craft[obj].ai_controller.maneuver_mode =
		AI_MANEUVER_MODE_FOLLOW_LEADER;
}

/* Order 21 removes the craft within 512 of its mothership's inside hangar
 * point, with every computer-flown follower of its group in the
 * follow-leader maneuver, and counts each as departed; a follower flying
 * another maneuver stays, and an empty slot counts for nothing. The order
 * returns 0, with the craft's think_interval 29 and separation 1. */
static void check_hangar_takes_followers(void)
{
	mothership_world();
	place_craft(0, 0, TEST_FAR);
	place_craft(1, 0, TEST_FAR);
	place_craft(2, 0, TEST_FAR);
	place_craft(3, 0, TEST_FAR);
	follow(1, 0);
	follow(2, 0);
	follow(3, 0);
	g_test_craft[3].ai_controller.maneuver_mode = AI_MANEUVER_MODE_ATTACK;
	follow(6, 0);
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_enterhangarorder(), 0);
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_controller.target_obj_idx, 5);
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_controller.think_interval, 29);
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.separation, 1);
	XVT_ASSERT_INT_EQ(g_test_objects[0].object_type, 1);

	struct ai_controller *controller = &g_test_craft[0].ai_controller;
	g_test_objects[0].world_x = controller->aim_point_x + 512;
	g_test_objects[0].world_y = controller->aim_point_y;
	g_test_objects[0].world_z = controller->aim_point_z;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_enterhangarorder(), 0);
	XVT_ASSERT_INT_EQ(g_test_objects[0].object_type, 1);
	g_test_objects[0].world_x -= 1;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_enterhangarorder(), 0);
	XVT_ASSERT_INT_EQ(g_test_objects[0].object_type, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[1].object_type, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[2].object_type, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[3].object_type, 1);
	XVT_ASSERT_INT_EQ(g_test_objects[5].object_type, 1);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[0]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_DEPARTED],
			  3);
}

/* Order 21 with no mothership, no craft of group 2 being present, sets the
 * speed to 35, as paiman_setspeed sets it, and returns 1. */
static void check_hangar_without_mothership(void)
{
	fresh_world();
	g_mission_flight_groups[0].fg.departure_mothership = 2;
	struct craft_data *craft = place_craft(0, 0, 0);
	think_as(0);
	paiman_setspeed(0, 35);
	uint16_t speed_35_throttle = craft->throttle_speed;
	craft->throttle_speed = 0;
	XVT_ASSERT_INT_EQ(paiorder_enterhangarorder(), 1);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, speed_35_throttle);
	XVT_ASSERT_INT_EQ(g_test_objects[0].object_type, 1);
}

/* Known failure self_destruct_delay_wraps, issue #53: order 44 promises a
 * countdown of variable1 times 1,180 ticks, and a longer order never makes
 * a shorter countdown. Its comment says the product is not checked against
 * the 16-bit timer: 56 units, 66,080 ticks, wrap to 544, so the craft
 * destroys itself sooner than with 55. */
static void check_self_destruct_delay_kept(void)
{
	fresh_world();
	place_craft(0, 0, 0);
	g_mission_flight_groups[0].fg.orders[0].variable1 = 55;
	think_as(0);
	paiorder_killselforder();
	uint16_t delay_55 = g_test_mobiles[0].lifetime_timer;
	g_test_mobiles[0].lifetime_timer = 0;
	g_mission_flight_groups[0].fg.orders[0].variable1 = 56;
	paiorder_killselforder();
	XVT_ASSERT_TRUE(g_test_mobiles[0].lifetime_timer >= delay_55);
}

/* Known failure follower_departure_time, issue #212: pai.h gives
 * depart_clock_minutes and depart_clock_seconds as the mission clock when the
 * craft's departure started, and order 38 withdraws a craft once the delay
 * has passed since then. A follower takes its leader's departure, started at
 * 1:10, but not its time, so when it leads the group itself at 1:20 its
 * 80-second delay counts from 0:00 and it withdraws at once. */
static void check_follower_departure_time(void)
{
	timed_departure();
	place_craft(1, 0, 0)->leader_obj_idx = 0;
	g_mission_elapsed_clock.minutes = 1;
	g_mission_elapsed_clock.seconds = 10;
	think_as(0);
	paiorder_stopgohomeorder();
	think_as(1);
	paiorder_completefolloworder();
	XVT_ASSERT_INT_EQ(g_test_craft[1].ai_flight.depart_timer_flag, 1);

	g_test_craft[1].leader_obj_idx = UINT8_MAX;
	g_mission_elapsed_clock.seconds = 20;
	think_as(1);
	XVT_ASSERT_INT_EQ(paiorder_stopgohomeorder(), 0);
}

/* Known failure wait_go_other_by_order_code, issue #44: order 42 moves the
 * craft to a later order whose plan is one of the capital ship plans. Its
 * comment says it reads the plan name at the order number instead of the
 * order's plan id. Slot 1 holds order 8, whose leader plan is plan 30,
 * capescortersldr1pln; plan 8 is formevadeldr1pln, so the craft stays. */
static void check_wait_go_other_by_plan(void)
{
	fresh_world();
	place_craft(0, 0, 0);
	g_mission_flight_groups[0].fg.orders[1].order = 8;
	g_order_leader_builtin_plan_name_index[8] = 25;
	g_builtin_plan_id_by_name_index[25] = 30;
	strcpy(g_plan_table[30].name, "capescortersldr1pln");
	strcpy(g_plan_table[8].name, "formevadeldr1pln");
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_waitgootherorder(), 1);
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_controller.current_order_slot, 1);
}

/* Known failure cannons_abort_reason, issue #214: order 15's comment says
 * trigger 2 tests the cannons but reports "warheads out". A wingman in
 * player 0's group whose cannons are down tells the player its warheads are
 * out; that reason belongs to trigger 3. */
static void check_cannons_abort_reason(void)
{
	fresh_world();
	place_craft(0, 0, 0);
	g_mission_flight_groups[0].fg.abort_trigger = 2;
	g_mission_flight_groups[0].player_owner_idx = 0;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiorder_abortmissionorder(), 1);
	XVT_ASSERT_TRUE(g_msg_arg_table[2] != IFMSG_394_WARHEADS_OUT);
}

/* Known failure hangar_takes_other_followers, issue #211: order 21 removes
 * with the docking craft the followers flying behind it. Its comment does
 * not say whose followers: craft 1 follows craft 0 and docks alone, and
 * craft 2, still in formation behind craft 0 far from the hangar, is removed
 * with it. */
static void check_hangar_keeps_other_followers(void)
{
	mothership_world();
	place_craft(0, 0, TEST_FAR);
	place_craft(1, 0, TEST_FAR);
	place_craft(2, 0, TEST_FAR);
	follow(1, 0);
	follow(2, 0);
	think_as(1);
	paiorder_enterhangarorder();
	g_test_objects[1].world_x = g_test_craft[1].ai_controller.aim_point_x;
	g_test_objects[1].world_y = g_test_craft[1].ai_controller.aim_point_y;
	g_test_objects[1].world_z = g_test_craft[1].ai_controller.aim_point_z;
	think_as(1);
	paiorder_enterhangarorder();
	XVT_ASSERT_INT_EQ(g_test_objects[1].object_type, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[2].object_type, 1);
}

/* Known failure hangar_departure_by_hyperspace, issue #213: group 0 leaves
 * by hyperspace, departure_method 0, and uses an alternate mothership, group
 * 1's craft in slot 4; its record also names group 2 as departure
 * mothership. Order 4 steers the craft to the alternate; order 21, which
 * the plans run after it, should fly it into that same hangar, and turns it
 * toward group 2's craft instead. */
static void check_hangar_follows_fly_home(void)
{
	fresh_world();
	g_mission_flight_groups[0].fg.departure_mothership = 2;
	g_mission_flight_groups[0].fg.alternate_mothership_used = 1;
	g_mission_flight_groups[0].fg.alternate_mothership = 1;
	place_craft(4, 1, 0);
	place_craft(5, 2, 0);
	struct craft_data *craft = place_craft(0, 0, TEST_FAR);
	think_as(0);
	paiorder_flyhomeorder();
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_obj_idx, 4);
	think_as(0);
	paiorder_enterhangarorder();
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_obj_idx, 4);
}

int main(int argc, char **argv)
{
	/* "known-failure <check>" runs one check the code is known to fail; an
	 * unknown name runs nothing. */
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		static const struct {
			const char *name;
			void (*check)(void);
		} known_failures[] = {
			{"self_destruct_delay_wraps",
			 check_self_destruct_delay_kept},
			{"follower_departure_time",
			 check_follower_departure_time},
			{"wait_go_other_by_order_code",
			 check_wait_go_other_by_plan},
			{"cannons_abort_reason", check_cannons_abort_reason},
			{"hangar_takes_other_followers",
			 check_hangar_keeps_other_followers},
			{"hangar_departure_by_hyperspace",
			 check_hangar_follows_fly_home},
		};
		for (size_t i = 0;
		     i < sizeof known_failures / sizeof known_failures[0];
		     ++i) {
			if (strcmp(argv[2], known_failures[i].name) == 0) {
				known_failures[i].check();
			}
		}
		return 0;
	}
	check_kill_self_delay();
	check_kill_self_random_delay();
	check_hyperspace_departure_test();
	check_follow_takes_flags();
	check_follow_moves_to_leader_slot();
	check_wait_go_other_moves();
	check_wait_go_other_stays();
	check_abort_needs_speed();
	check_abort_on_cannons();
	check_abort_on_hull_and_attack();
	check_abort_reason_shields();
	check_departure_timing();
	check_departure_short_times();
	check_departure_needs_speed();
	check_fly_home_to_mothership();
	check_fly_home_skips_player_mothership();
	check_hangar_takes_followers();
	check_hangar_without_mothership();
	return 0;
}
