/* Tests for xvt/flight/ai/paiman.c, the maneuvers of the computer pilots. Each
 * check builds the world it needs in the game's own tables: three flight
 * groups and a few craft in an object table this file owns, with the 256 slots
 * an object index of one byte can name, and one plan whose bytes change
 * nothing. A maneuver runs as it does in a think: the craft's context is set
 * up first with pai_setupcraftcontext. The X-wing's box is set in the model
 * bounds cache and model 0, every test craft's, has a long name; no game
 * data is read. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/model_bounds.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/ai/paiman.h"
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
	TEST_OBJECT_COUNT = 256,
	TEST_CRAFT_SLOTS = 8, /* The active region's craft slots. */
	TEST_XWING = 1,	      /* Craft type 1 is object type 1, an X-wing. */
	TEST_NO_ORDINAL = 9,  /* A craft ordinal no test craft has. */
	Q15_ONE = 0x7FFF,     /* An axis component of length one. */
	SECOND = 236,	      /* SIMULATION_TICKS_PER_SECOND. */
	TEST_TARGET_SIGNATURE = 0x77,
};

static struct object_record g_test_objects[TEST_OBJECT_COUNT];
static struct mobile_object g_test_mobiles[TEST_OBJECT_COUNT];
static struct craft_data g_test_craft[TEST_CRAFT_SLOTS];
/* Every plan's bytes: keep the target, keep the maneuver. */
static uint8_t g_test_plan[4] = {255, 255, 0, 0};
/* Every in-flight message's text, a system message: with system messages
 * off, the message code drops it. */
static const char g_test_message[] = "\x03message";
/* The long name of model 0, which the game reads from its text files. */
static char g_test_model_name[] = "X-wing";

/* Points the object's axes along the world's: side X, forward Y, up Z. */
static void level_axes(int obj)
{
	struct mobile_object *mobile = &g_test_mobiles[obj];
	mobile->orient_matrix_dirty = 0;
	mobile->cached_side_x = Q15_ONE;
	mobile->cached_fwd_y = Q15_ONE;
	mobile->cached_up_z = Q15_ONE;
}

/* Three flight groups of X-wings on teams and IFFs 0, 1 and 2, none owned by
 * a player and none with a special cargo craft. Every plan points at the
 * plan above, and every in-flight message's text is a plain line. The first
 * eight slots of the object table are the active region's craft slots; every
 * slot has a mobile object with level axes. The local player is slot 7,
 * flying no craft, and the tactical officer is off. The game's random
 * generator starts from a fixed state. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_mission_flight_groups, 0, sizeof g_mission_flight_groups);
	memset(g_mission_fg_stats, 0, sizeof g_mission_fg_stats);
	memset(&g_mission_header, 0, sizeof g_mission_header);
	memset(g_players, 0, sizeof g_players);
	memset(g_plan_table, 0, sizeof g_plan_table);
	for (int plan = 0; plan < 256; ++plan) {
		g_plan_data_ptrs[plan] = g_test_plan;
	}
	for (size_t id = 0; id < sizeof g_str_in_flight_messages /
					 sizeof g_str_in_flight_messages[0];
	     ++id) {
		g_str_in_flight_messages[id] = g_test_message;
	}
	g_model_defs[0].name_long = g_test_model_name;
	g_model_bounds_cached[1] = 1;
	g_model_bounds_min[1].x = -100.0f;
	g_model_bounds_min[1].y = -200.0f;
	g_model_bounds_min[1].z = -50.0f;
	g_model_bounds_max[1].x = 100.0f;
	g_model_bounds_max[1].y = 200.0f;
	g_model_bounds_max[1].z = 50.0f;
	g_object_table = g_test_objects;
	for (int i = 0; i < TEST_OBJECT_COUNT; ++i) {
		g_test_objects[i].player_owner_idx = -1;
		g_test_objects[i].mobj = &g_test_mobiles[i];
		level_axes(i);
	}
	for (int i = 0; i < TEST_CRAFT_SLOTS; ++i) {
		g_test_mobiles[i].p_craft = &g_test_craft[i];
		g_test_craft[i].leader_obj_idx = UINT8_MAX;
		g_test_craft[i].carried_object_index = UINT16_MAX;
		g_test_craft[i].last_attacker_obj_idx = UINT16_MAX;
		g_test_craft[i].ai_flight.max_speed_cache = 100;
		g_test_craft[i].working_subsystems =
			CRAFT_SUBSYSTEM_FLAG_ENGINES;
	}
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
		g_players[i].current_target_object_idx = -1;
		for (int slot = 0; slot < 4; ++slot) {
			g_players[i].target_preset_slot[slot] = -1;
		}
	}
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = TEST_CRAFT_SLOTS;
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

/* Puts an X-wing of flight group fg in slot obj at x along the X axis, with
 * the group's team and IFF. */
static struct craft_data *place_craft(int obj, int fg, int x)
{
	g_test_objects[obj].object_type = 1;
	g_test_objects[obj].genus_id = 1;
	g_test_objects[obj].object_signature = (uint16_t)(100 + obj);
	g_test_objects[obj].flight_group_idx = (uint8_t)fg;
	g_test_objects[obj].world_x = x;
	g_test_mobiles[obj].team = g_mission_flight_groups[fg].fg.team;
	g_test_mobiles[obj].iff = g_mission_flight_groups[fg].fg.iff;
	g_test_craft[obj].craft_ordinal = (uint8_t)obj;
	return &g_test_craft[obj];
}

/* Moves the object to the point, keeping its other fields. */
static void move_to(int obj, int x, int y, int z)
{
	g_test_objects[obj].world_x = x;
	g_test_objects[obj].world_y = y;
	g_test_objects[obj].world_z = z;
}

/* Sets up the think of the craft in slot obj, as pai_update_all_craft_ai
 * does before it runs the craft's plan. */
static void think_as(int obj)
{
	g_cur_craft = &g_test_craft[obj];
	pai_setupcraftcontext((uint16_t)obj);
}

/* Starting maneuver 0 clears the craft's push and per-maneuver counts, sets
 * commanded_speed to five times the order's speed, roll_state 4 and phase 0.
 * Starting the wait maneuver also runs its start function: variable1 times
 * 1,180 ticks and throttle 0. */
static void check_init_maneuver(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, 0, 0);
	g_mission_flight_groups[0].fg.orders[0].speed = 7;
	g_mission_flight_groups[0].fg.orders[0].variable1 = 2;
	craft->push_accum_x = 5;
	craft->push_accum_y = 6;
	craft->push_accum_z = 7;
	craft->ai_flight.hits_this_maneuver = 3;
	craft->ai_flight.warheads_fired_this_maneuver = 4;
	craft->warhead_lock_ticks = 9;
	craft->ai_controller.maneuver_phase = 2;
	think_as(0);
	paiman_initmaneuver();
	XVT_ASSERT_INT_EQ(craft->push_accum_x, 0);
	XVT_ASSERT_INT_EQ(craft->push_accum_y, 0);
	XVT_ASSERT_INT_EQ(craft->push_accum_z, 0);
	XVT_ASSERT_INT_EQ(craft->ai_flight.hits_this_maneuver, 0);
	XVT_ASSERT_INT_EQ(craft->ai_flight.warheads_fired_this_maneuver, 0);
	XVT_ASSERT_INT_EQ(craft->warhead_lock_ticks, 0);
	XVT_ASSERT_INT_EQ(craft->commanded_speed, 35);
	XVT_ASSERT_INT_EQ(craft->ai_flight.roll_state, 4);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_phase, 0);

	craft->throttle_speed = 99;
	craft->ai_controller.maneuver_mode = AI_MANEUVER_MODE_WAIT;
	paiman_initmaneuver();
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_timer, 2 * 1180);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0);
}

/* A turn within 0x300 of the heading snaps the yaw there and sets turn_state
 * 3; a wider one sets turn_state 2 with the turn step. */
static void check_set_turn(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, 0, 0);
	think_as(0);
	g_test_objects[0].yaw = 0x1000;
	craft->ai_controller.target_xy_angle = 0x1300;
	paiman_setturn(0x1234);
	XVT_ASSERT_INT_EQ(g_test_objects[0].yaw, 0x1300);
	XVT_ASSERT_INT_EQ(craft->ai_flight.turn_state, 3);
	XVT_ASSERT_INT_EQ(g_test_mobiles[0].orient_matrix_dirty, 1);
	XVT_ASSERT_INT_EQ(g_test_mobiles[0].move_vector_dirty, 1);

	g_test_objects[0].yaw = 0x1600;
	craft->ai_controller.target_xy_angle = 0x1300;
	paiman_setturn(0x1234);
	XVT_ASSERT_INT_EQ(g_test_objects[0].yaw, 0x1300);

	craft->ai_controller.target_xy_angle = 0x1601;
	paiman_setturn(0x1234);
	XVT_ASSERT_INT_EQ(g_test_objects[0].yaw, 0x1300);
	XVT_ASSERT_INT_EQ(craft->ai_flight.turn_state, 2);
	XVT_ASSERT_INT_EQ(craft->ai_flight.turn_step, 0x1234);
}

/* With the three recharge levels summing to 6 the full speed is
 * max_speed_cache: full throttle at it, half throttle at half of it. One
 * step below 6 raises the full speed by an eighth, 12.5 rounded to 13, so
 * 100 becomes 113. The throttle goes to g_cur_craft. */
static void check_set_speed(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, 0, 0);
	struct craft_data *other = place_craft(1, 0, 0);
	other->shield_recharge_level = 2;
	other->beam_recharge_level = 2;
	other->laser_recharge_level = 2;
	think_as(0);
	paiman_setspeed(1, 100);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0xFFFF);
	XVT_ASSERT_INT_EQ(other->throttle_speed, 0);
	paiman_setspeed(1, 50);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0x8000);

	other->laser_recharge_level = 1;
	paiman_setspeed(1, 113);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0xFFFF);
	paiman_setspeed(1, 112);
	XVT_ASSERT_TRUE(craft->throttle_speed < 0xFFFF);
}

/* A weave of speed away from a craft whose Z push is below 0: a push of 50
 * to 81 up, a heading 384 to 639 off the yaw, and the next weave in 118
 * ticks. The turn is within 0x300, so the yaw snaps to the new heading. */
static void check_speed_away_weave(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, 0, 0);
	for (int draw = 0; draw < 1024; ++draw) {
		g_test_objects[0].yaw = 0x2000;
		think_as(0);
		craft->push_accum_z = -1;
		paiman_setup_speed_away_turn(0);
		XVT_ASSERT_TRUE(craft->push_accum_z >= 50);
		XVT_ASSERT_TRUE(craft->push_accum_z <= 81);
		uint16_t turn =
			(uint16_t)(craft->ai_controller.target_xy_angle -
				   0x2000);
		XVT_ASSERT_INT_EQ(g_test_objects[0].yaw,
				  craft->ai_controller.target_xy_angle);
		XVT_ASSERT_TRUE(turn >= 384);
		XVT_ASSERT_TRUE(turn <= 639);
		XVT_ASSERT_INT_EQ(craft->ai_controller.secondary_maneuver_timer,
				  118);
	}
}

/* From a Z push of 0 the weave turns the other way: a heading 384 to 639
 * below the yaw. */
static void check_speed_away_other_side(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, 0, 0);
	for (int draw = 0; draw < 64; ++draw) {
		g_test_objects[0].yaw = 0x2000;
		think_as(0);
		craft->push_accum_z = 0;
		paiman_setup_speed_away_turn(0);
		uint16_t turn =
			(uint16_t)(0x2000 -
				   craft->ai_controller.target_xy_angle);
		XVT_ASSERT_TRUE(turn >= 384);
		XVT_ASSERT_TRUE(turn <= 639);
	}
}

/* Group 0's mission points 4 and 5, enabled. */
static void two_waypoints(void)
{
	struct xvt_flight_group *fg = &g_mission_flight_groups[0].fg;
	fg->mission_point_enabled[4] = 1;
	fg->mission_point_x[4] = 10;
	fg->mission_point_enabled[5] = 1;
	fg->mission_point_x[5] = 20;
	fg->mission_point_y[5] = 3;
}

/* The next waypoint is the next enabled point, which the craft targets, its
 * aim point there; past the last it goes back to 4, and on formldr1pln that
 * counts a round in the order slot's goal_progress. */
static void check_advance_waypoint(void)
{
	fresh_world();
	two_waypoints();
	struct craft_data *craft = place_craft(0, 0, 0);
	struct ai_controller *controller = &craft->ai_controller;
	strcpy(g_plan_table[3].name, "formldr1pln");
	controller->current_plan_id = 3;
	controller->waypoint_index = 4;
	controller->target_signature = 9;
	controller->has_live_target = 1;
	think_as(0);
	paiman_advance_order_waypoint(0);
	XVT_ASSERT_INT_EQ(controller->waypoint_index, 5);
	XVT_ASSERT_INT_EQ(controller->target_obj_idx, 0x8005);
	XVT_ASSERT_INT_EQ(controller->target_signature, 0);
	XVT_ASSERT_INT_EQ(controller->has_live_target, 0);
	XVT_ASSERT_INT_EQ(controller->aim_point_x, 20 * 256);
	XVT_ASSERT_INT_EQ(controller->aim_point_y, -3 * 256);
	XVT_ASSERT_INT_EQ(controller->order_progress.goal_progress[0], 0);

	paiman_advance_order_waypoint(0);
	XVT_ASSERT_INT_EQ(controller->waypoint_index, 4);
	XVT_ASSERT_INT_EQ(controller->target_obj_idx, 0x8004);
	XVT_ASSERT_INT_EQ(controller->aim_point_x, 10 * 256);
	XVT_ASSERT_INT_EQ(controller->order_progress.goal_progress[0], 1);

	/* Point 11 is the last waypoint, even with point 12 enabled. */
	g_mission_flight_groups[0].fg.mission_point_enabled[11] = 1;
	g_mission_flight_groups[0].fg.mission_point_enabled[12] = 1;
	controller->waypoint_index = 10;
	paiman_advance_order_waypoint(0);
	XVT_ASSERT_INT_EQ(controller->waypoint_index, 11);
	paiman_advance_order_waypoint(0);
	XVT_ASSERT_INT_EQ(controller->waypoint_index, 4);
	XVT_ASSERT_INT_EQ(controller->order_progress.goal_progress[0], 2);

	strcpy(g_plan_table[3].name, "attackldr1pln");
	controller->waypoint_index = 11;
	paiman_advance_order_waypoint(0);
	XVT_ASSERT_INT_EQ(controller->waypoint_index, 4);
	XVT_ASSERT_INT_EQ(controller->order_progress.goal_progress[0], 2);
}

/* Cruise near its aim point moves the craft to its next waypoint; a fighter
 * then flies at the order's throttle. */
static void check_cruise_next_waypoint(void)
{
	fresh_world();
	two_waypoints();
	g_mission_flight_groups[0].fg.orders[0].throttle = 5;
	struct craft_data *craft = place_craft(0, 0, 10 * 256 + 0xFFF);
	craft->ai_controller.waypoint_index = 4;
	craft->ai_controller.aim_point_x = 10 * 256;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiman_cruisemaneuver(), 0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.waypoint_index, 5);
	XVT_ASSERT_INT_EQ(craft->throttle_speed,
			  g_order_throttle_to_craft_throttle_speed[5]);

	move_to(0, 20 * 256, -3 * 256 - 0x1000, 0);
	think_as(0);
	paiman_cruisemaneuver();
	XVT_ASSERT_INT_EQ(craft->ai_controller.waypoint_index, 5);
}

/* A starship whose only waypoint is the one it reached stops there: turn
 * done, throttle 0, and a push onto the aim point. */
static void check_cruise_starship_stops(void)
{
	fresh_world();
	g_mission_flight_groups[0].fg.mission_point_enabled[4] = 1;
	g_mission_flight_groups[0].fg.mission_point_x[4] = 10;
	g_mission_flight_groups[0].fg.orders[0].throttle = 5;
	struct craft_data *craft = place_craft(0, 0, 10 * 256 + 0x1FFF);
	g_test_objects[0].genus_id = 4;
	craft->ai_controller.waypoint_index = 4;
	craft->ai_controller.aim_point_x = 10 * 256;
	craft->throttle_speed = 1;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiman_cruisemaneuver(), 0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.waypoint_index, 4);
	XVT_ASSERT_INT_EQ(craft->ai_flight.turn_state, 3);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0);
	XVT_ASSERT_INT_EQ(craft->push_accum_x, -0x1FFF);
	XVT_ASSERT_INT_EQ(craft->push_accum_y, 0);
	XVT_ASSERT_INT_EQ(craft->push_accum_z, 0);

	/* A freighter stops the same way, 0xFFF off; a fighter flies on. */
	g_test_objects[0].genus_id = 3;
	g_test_objects[0].world_x = 10 * 256 + 0xFFF;
	craft->throttle_speed = 1;
	think_as(0);
	paiman_cruisemaneuver();
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0);
	XVT_ASSERT_INT_EQ(craft->push_accum_x, -0xFFF);
	g_test_objects[0].genus_id = 1;
	craft->push_accum_x = 0;
	think_as(0);
	paiman_cruisemaneuver();
	XVT_ASSERT_INT_EQ(craft->throttle_speed,
			  g_order_throttle_to_craft_throttle_speed[5]);
	XVT_ASSERT_INT_EQ(craft->push_accum_x, 0);
}

/* Away from its aim point a starship turning more than 0x1000 off course
 * has throttle 0; within 0x1000, or a fighter, the order's throttle. */
static void check_cruise_starship_turns(void)
{
	fresh_world();
	g_mission_flight_groups[0].fg.orders[0].throttle = 5;
	struct craft_data *craft = place_craft(0, 0, 0x10000);
	g_test_objects[0].genus_id = 4;
	craft->ai_controller.secondary_maneuver_timer = 5;
	craft->ai_flight.turn_state = 2;
	craft->ai_controller.target_xy_angle = 0x1000;
	think_as(0);
	paiman_cruisemaneuver();
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0);
	craft->ai_controller.target_xy_angle = 0x0FFF;
	paiman_cruisemaneuver();
	XVT_ASSERT_INT_EQ(craft->throttle_speed,
			  g_order_throttle_to_craft_throttle_speed[5]);
	g_test_objects[0].genus_id = 1;
	craft->ai_controller.target_xy_angle = 0x1000;
	craft->throttle_speed = 0;
	paiman_cruisemaneuver();
	XVT_ASSERT_INT_EQ(craft->throttle_speed,
			  g_order_throttle_to_craft_throttle_speed[5]);
}

/* Craft 0 escorts group 1, whose leaderless craft 1 stands at the origin,
 * from station 0 of its order. */
static struct craft_data *escort_world(int x)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, 0, x);
	place_craft(1, 1, 0);
	craft->ai_controller.escort_target_fg = 1;
	return craft;
}

/* Far from the escorted craft the escort aims at it with no push, at full
 * throttle beyond 0x10000, else a quarter. */
static void check_escort_far(void)
{
	struct craft_data *craft = escort_world(0x10001);
	craft->push_accum_x = 5;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiman_escortmaneuver(), 0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.aim_point_x, 0);
	XVT_ASSERT_INT_EQ(craft->push_accum_x, 0);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0xFFFF);

	move_to(0, 0x10000, 0, 0);
	think_as(0);
	paiman_escortmaneuver();
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0x4000);
	uint16_t heading = craft->ai_controller.target_xy_angle;

	/* A disabled escorted craft is flown at even close by, a quarter turn
	 * off. */
	g_test_craft[1].working_subsystems = 0;
	think_as(0);
	paiman_escortmaneuver();
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_xy_angle,
			  (uint16_t)(heading + 0x4000));
	move_to(0, 0x1000, 0, 0);
	craft->push_accum_x = 5;
	think_as(0);
	paiman_escortmaneuver();
	XVT_ASSERT_INT_EQ(craft->push_accum_x, 0);
}

/* At 0x8000 from the escorted craft the escort is close enough to hold its
 * station, here station 27, on the escorted craft itself. */
static void check_escort_range_edge(void)
{
	struct craft_data *craft = escort_world(0x8000);
	g_mission_flight_groups[0].fg.orders[0].variable1 = 27;
	think_as(0);
	paiman_escortmaneuver();
	XVT_ASSERT_INT_EQ(craft->push_accum_x, -0x8000);
}

/* Close to the escorted craft the escort is pushed to its station: the
 * escorted craft's position less its own, plus the station's offsets turned
 * to the escorted craft's axes. Station 27 has no offset. */
static void check_escort_station(void)
{
	struct craft_data *craft = escort_world(0x1000);
	g_test_objects[1].world_y = 50;
	for (int station = 0; station < 28; station += 27) {
		g_mission_flight_groups[0].fg.orders[0].variable1 =
			(uint8_t)station;
		think_as(0);
		paiman_escortmaneuver();
		pai_calcrotatedpoint(
			&g_test_objects[1],
			g_ai_escort_station_offset_x_by_variable[station],
			g_ai_escort_station_offset_y_by_variable[station],
			g_ai_escort_station_offset_z_by_variable[station]);
		XVT_ASSERT_INT_EQ(craft->push_accum_x, -0x1000 + g_rotated_x);
		XVT_ASSERT_INT_EQ(craft->push_accum_y, 50 + g_rotated_y);
		XVT_ASSERT_INT_EQ(craft->push_accum_z, g_rotated_z);
	}
	XVT_ASSERT_INT_EQ(craft->push_accum_x, -0x1000);
}

/* Close by, the escort takes the escorted craft's heading, or the heading
 * it turns to while it turns. */
static void check_escort_heading(void)
{
	struct craft_data *craft = escort_world(0x1000);
	g_test_objects[1].yaw = 0x4000;
	think_as(0);
	paiman_escortmaneuver();
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_xy_angle, 0x4000);
	XVT_ASSERT_INT_EQ(craft->ai_flight.turn_state, 2);

	g_test_craft[1].ai_flight.turn_state = 2;
	g_test_craft[1].ai_controller.target_xy_angle = 0x6000;
	think_as(0);
	paiman_escortmaneuver();
	XVT_ASSERT_INT_EQ(craft->ai_controller.target_xy_angle, 0x6000);
}

/* With no craft of the escorted group the escort steers at its aim point at
 * half throttle. */
static void check_escort_alone(void)
{
	struct craft_data *craft = escort_world(0x1000);
	craft->ai_controller.escort_target_fg = 2;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiman_escortmaneuver(), 0);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0x8000);
}

/* Craft 0 of group 0 boards craft 1 of group 1 on the named plan, from far
 * along X; the order's variable1 is 2. */
static struct craft_data *board_world(const char *plan_name)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, 0, 0x10000);
	struct craft_data *target = place_craft(1, 1, 0);
	target->object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	g_test_objects[1].object_signature = TEST_TARGET_SIGNATURE;
	strcpy(g_plan_table[5].name, plan_name);
	craft->ai_controller.current_plan_id = 5;
	craft->ai_controller.target_obj_idx = 1;
	craft->ai_controller.maneuver_mode = AI_MANEUVER_MODE_BOARD;
	g_mission_flight_groups[0].fg.orders[0].variable1 = 2;
	return craft;
}

/* Stage 0 aims at the approach point, the same point on each think while
 * the target holds still, and returns the craft's aim point. */
static const struct ai_controller *board_approach(int x_from_point)
{
	think_as(0);
	paiman_boardmaneuver();
	struct ai_controller *controller = &g_test_craft[0].ai_controller;
	move_to(0, controller->aim_point_x + x_from_point,
		controller->aim_point_y, controller->aim_point_z);
	think_as(0);
	XVT_ASSERT_INT_EQ(paiman_boardmaneuver(), 0);
	return controller;
}

/* Stage 0 flies at half throttle beyond 0x2000 of the approach point, a
 * quarter beyond 2,048, and within it sets throttle 0 and moves to stage
 * 1. */
static void check_board_approach(void)
{
	struct craft_data *craft = board_world("boardtocontactpln");
	board_approach(0x3000);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0x8000);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_phase, 0);
	board_approach(0x2000);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0x4000);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_phase, 0);
	board_approach(2049);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0x4000);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_phase, 0);
	/* A computer pilot's target that moves does not hold the craft. */
	g_test_mobiles[1].speed = 5;
	board_approach(2048);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_phase, 1);
}

/* At the approach point the craft waits while a player's target moves. */
static void check_board_waits_for_player(void)
{
	struct craft_data *craft = board_world("boardtocontactpln");
	g_test_objects[1].player_owner_idx = 0;
	g_test_mobiles[1].speed = 5;
	board_approach(0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_phase, 0);
	g_test_mobiles[1].speed = 0;
	think_as(0);
	paiman_boardmaneuver();
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_phase, 1);
}

/* Stage 1 pushes the craft onto the docking point; less than 16 from it,
 * the three axes added, it clears the push and moves to stage 2 for 1,180
 * ticks times variable1, counting its group's FAILED_MISSION and the target
 * group's COMPLETED_MISSION outcomes, each once. */
static void check_board_docks(void)
{
	struct craft_data *craft = board_world("boardtocontactpln");
	struct ai_controller *controller = &craft->ai_controller;
	controller->maneuver_phase = 1;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiman_boardmaneuver(), 0);
	XVT_ASSERT_INT_EQ(controller->maneuver_phase, 1);
	int dock_x = g_test_objects[0].world_x + craft->push_accum_x;
	int dock_y = g_test_objects[0].world_y + craft->push_accum_y;
	int dock_z = g_test_objects[0].world_z + craft->push_accum_z;
	move_to(0, dock_x + 5, dock_y - 6, dock_z + 5);
	think_as(0);
	XVT_ASSERT_INT_EQ(paiman_boardmaneuver(), 0);
	XVT_ASSERT_INT_EQ(controller->maneuver_phase, 1);
	move_to(0, dock_x + 5, dock_y - 5, dock_z + 5);
	think_as(0);
	XVT_ASSERT_INT_EQ(paiman_boardmaneuver(), 0);
	XVT_ASSERT_INT_EQ(controller->maneuver_phase, 2);
	XVT_ASSERT_INT_EQ(controller->maneuver_timer, 2 * 1180);
	XVT_ASSERT_INT_EQ(craft->push_accum_z, 0);
	uint16_t *own = g_mission_fg_stats[0].outcome_count;
	uint16_t *target = g_mission_fg_stats[1].outcome_count;
	XVT_ASSERT_INT_EQ(own[FLIGHT_GROUP_OUTCOME_FAILED_MISSION], 1);
	XVT_ASSERT_INT_EQ(target[FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION], 1);

	controller->maneuver_phase = 1;
	think_as(0);
	paiman_boardmaneuver();
	XVT_ASSERT_INT_EQ(own[FLIGHT_GROUP_OUTCOME_FAILED_MISSION], 1);
	XVT_ASSERT_INT_EQ(target[FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION], 1);
}

/* Runs the end of the docking: stage 2 with its timer run out. */
static void finish_docking(void)
{
	g_test_craft[0].ai_controller.maneuver_phase = 2;
	g_test_craft[0].ai_controller.maneuver_timer = 0;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiman_boardmaneuver(), 0);
}

/* While the stage 2 timer runs, a craft not on boardtogivepln waits. */
static void check_board_transfer_waits(void)
{
	struct craft_data *craft = board_world("boardtocontactpln");
	craft->ai_controller.maneuver_phase = 2;
	craft->ai_controller.maneuver_timer = 5;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiman_boardmaneuver(), 0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_phase, 2);
	XVT_ASSERT_INT_EQ(craft->ai_controller.order_progress.goal_progress[0],
			  0);
}

/* While the stage 2 timer runs on boardtogivepln with player 0's craft,
 * each time secondary_maneuver_timer runs out one system that is fitted but
 * down comes back, the timer is held at 1,416 ticks and the next step comes
 * in 472. A craft on another plan does nothing. */
static void check_board_give_repairs_step(void)
{
	for (int give = 0; give < 2; ++give) {
		struct craft_data *craft = board_world(
			give != 0 ? "boardtogivepln" : "boardtotakepln");
		struct craft_data *target = &g_test_craft[1];
		g_test_objects[1].player_owner_idx = 0;
		target->system_flags = CRAFT_SUBSYSTEM_FLAG_SHIELDS |
				       CRAFT_SUBSYSTEM_FLAG_CANNONS;
		target->working_subsystems = 0;
		target->cm_type_id = COUNTERMEASURE_TYPE_NONE;
		struct ai_controller *controller = &craft->ai_controller;
		controller->maneuver_phase = 2;
		controller->maneuver_timer = 5000;
		think_as(0);
		XVT_ASSERT_INT_EQ(paiman_boardmaneuver(), 0);
		if (give == 0) {
			XVT_ASSERT_INT_EQ(target->working_subsystems, 0);
			XVT_ASSERT_INT_EQ(controller->maneuver_timer, 5000);
			continue;
		}
		XVT_ASSERT_INT_EQ(target->working_subsystems,
				  CRAFT_SUBSYSTEM_FLAG_SHIELDS);
		XVT_ASSERT_INT_EQ(controller->maneuver_timer, 1416);
		XVT_ASSERT_INT_EQ(controller->secondary_maneuver_timer, 472);
		controller->secondary_maneuver_timer = 0;
		paiman_boardmaneuver();
		XVT_ASSERT_INT_EQ(target->working_subsystems,
				  target->system_flags);
	}
}

/* The end of a contact docking: the target's boarding_state is 2, a round
 * is added to goal_progress, the target's signature joins the docked list,
 * the docked outcome is counted at the first docking and the boarded one at
 * each, and stage 3 starts for 2,360 ticks. A player's target is no longer
 * the craft's candidate. */
static void check_board_contact_done(void)
{
	struct craft_data *craft = board_world("boardtocontactpln");
	g_test_objects[1].player_owner_idx = 0;
	craft->ai_controller.candidate_target_idx = 1;
	finish_docking();
	struct ai_controller *controller = &craft->ai_controller;
	XVT_ASSERT_INT_EQ(g_test_craft[1].boarding_state, 2);
	XVT_ASSERT_INT_EQ(controller->order_progress.goal_progress[0], 1);
	XVT_ASSERT_INT_EQ(craft->ai_flight.docked_target_count, 1);
	XVT_ASSERT_INT_EQ(craft->ai_flight.docked_target_signatures[0],
			  TEST_TARGET_SIGNATURE);
	XVT_ASSERT_INT_EQ(g_test_craft[1].ai_flight.times_boarded, 1);
	uint16_t *own = g_mission_fg_stats[0].outcome_count;
	uint16_t *target = g_mission_fg_stats[1].outcome_count;
	XVT_ASSERT_INT_EQ(own[FLIGHT_GROUP_OUTCOME_DOCKED], 1);
	XVT_ASSERT_INT_EQ(target[FLIGHT_GROUP_OUTCOME_BOARDED], 1);
	XVT_ASSERT_INT_EQ(controller->maneuver_phase, 3);
	XVT_ASSERT_INT_EQ(controller->maneuver_timer, 2360);
	XVT_ASSERT_INT_EQ(controller->candidate_target_idx, UINT16_MAX);

	g_test_objects[1].object_signature = 0x78;
	finish_docking();
	XVT_ASSERT_INT_EQ(craft->ai_flight.docked_target_count, 2);
	XVT_ASSERT_INT_EQ(craft->ai_flight.docked_target_signatures[1], 0x78);
	XVT_ASSERT_INT_EQ(own[FLIGHT_GROUP_OUTCOME_DOCKED], 1);
	XVT_ASSERT_INT_EQ(target[FLIGHT_GROUP_OUTCOME_BOARDED], 2);
}

/* A docking with a craft of the same IFF counts it inspected by the
 * boarding craft's team: that team's identified order is one past the
 * highest any team has, and the inspected outcome is counted once. */
static void check_board_inspects(void)
{
	board_world("boardtocontactpln");
	g_test_mobiles[1].iff = 0;
	g_test_craft[1].identified_order_by_team[1] = 1;
	g_test_craft[1].identified_order_by_team[4] = 3;
	finish_docking();
	XVT_ASSERT_INT_EQ(g_test_craft[1].identified_order_by_team[0], 4);
	uint16_t *target = g_mission_fg_stats[1].outcome_count;
	XVT_ASSERT_INT_EQ(target[FLIGHT_GROUP_OUTCOME_INSPECTED], 1);
	finish_docking();
	XVT_ASSERT_INT_EQ(target[FLIGHT_GROUP_OUTCOME_INSPECTED], 1);
}

/* The end of a give docking with player 0's craft, on another machine: the
 * target takes the special cargo name and boarding_state 2, the reload craft
 * boarding_state 1, and every system of the target is repaired, its display
 * slots in order. */
static void check_board_give_repairs(void)
{
	struct craft_data *craft = board_world("boardtogivepln");
	struct craft_data *target = &g_test_craft[1];
	g_test_objects[1].player_owner_idx = 0;
	strcpy(craft->special_cargo_name, "Spare parts");
	target->system_health[3] = 10;
	target->system_repair_seconds[3] = 30;
	target->system_display_slot_by_system[3] = 7;
	finish_docking();
	XVT_ASSERT_TRUE(strcmp(target->special_cargo_name, "Spare parts") == 0);
	XVT_ASSERT_INT_EQ(target->boarding_state, 2);
	XVT_ASSERT_INT_EQ(craft->boarding_state, 1);
	for (int system = 0; system < CRAFT_SUBSYSTEM_COUNT; ++system) {
		XVT_ASSERT_INT_EQ(target->system_health[system], 100);
		XVT_ASSERT_INT_EQ(target->system_repair_seconds[system], 0);
		XVT_ASSERT_INT_EQ(target->system_display_slot_by_system[system],
				  system);
	}
}

/* The end of a destroy docking with a target already counting down: the
 * countdown becomes 15 to 30 seconds, and the target runs selfdestroypln. */
static void check_board_destroy_countdown(void)
{
	board_world("boardtodestroypln");
	strcpy(g_plan_table[9].name, "selfdestroypln");
	g_test_mobiles[1].lifetime_timer = 1;
	finish_docking();
	XVT_ASSERT_TRUE(g_test_mobiles[1].lifetime_timer >= 15 * SECOND);
	XVT_ASSERT_TRUE(g_test_mobiles[1].lifetime_timer <= 30 * SECOND);
	XVT_ASSERT_INT_EQ(g_test_craft[1].ai_controller.running_plan_id, 9);
}

/* Stage 3 pushes the craft to a point 0x4000 along its up axis from the
 * target; when its timer runs out it drops the target and returns 1. */
static void check_board_separates(void)
{
	struct craft_data *craft = board_world("boardtocontactpln");
	struct ai_controller *controller = &craft->ai_controller;
	controller->maneuver_phase = 3;
	controller->maneuver_timer = 10;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiman_boardmaneuver(), 0);
	pai_calcrotatedpoint(&g_test_objects[0], 0, 0x4000, 0);
	XVT_ASSERT_INT_EQ(craft->push_accum_x, g_rotated_x - 0x10000);
	XVT_ASSERT_INT_EQ(craft->push_accum_z, g_rotated_z);

	controller->maneuver_timer = 0;
	controller->target_signature = TEST_TARGET_SIGNATURE;
	controller->has_live_target = 1;
	XVT_ASSERT_INT_EQ(paiman_boardmaneuver(), 1);
	XVT_ASSERT_INT_EQ(controller->target_obj_idx, UINT16_MAX);
	XVT_ASSERT_INT_EQ(controller->target_signature, 0);
	XVT_ASSERT_INT_EQ(controller->has_live_target, 0);
}

/* Craft 0 of group 0 drops off group 2, its order's variable2 being 3, at
 * formation place 1. */
static struct craft_data *dropoff_world(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, 0, 0x5000);
	g_mission_flight_groups[0].fg.orders[0].variable2 = 3;
	g_mission_flight_groups[2].fg.mission_point_x[0] = 4;
	g_mission_fg_stats[2].current_mission_point_ref = 0x8000;
	craft->ai_controller.waypoint_index = 1;
	return craft;
}

/* At phase 0 the craft is pushed to the place
 * mission_resolve_formation_slot_world_loc gives around group 2's leaderless
 * craft, turned a quarter, plus the negated minimum Z of its own box; the
 * order slot's goal_progress is the place. */
static void check_dropoff_push(void)
{
	struct craft_data *craft = dropoff_world();
	place_craft(3, 2, 0x100);
	g_test_craft[3].leader_obj_idx = 4;
	place_craft(4, 2, 0x200);
	g_test_mobiles[4].cached_side_x = 0;
	g_test_mobiles[4].cached_side_y = Q15_ONE;
	g_test_mobiles[4].cached_fwd_x = -Q15_ONE;
	g_test_mobiles[4].cached_fwd_y = 0;
	think_as(0);
	XVT_ASSERT_INT_EQ(paiman_dropoffmaneuver(), 0);
	mission_resolve_formation_slot_world_loc(2, 1, 4);
	XVT_ASSERT_INT_EQ(craft->push_accum_x, g_world_loc_x - 0x5000);
	XVT_ASSERT_INT_EQ(craft->push_accum_y, g_world_loc_y);
	XVT_ASSERT_INT_EQ(craft->push_accum_z, g_world_loc_z + 50);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_phase, 0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.order_progress.goal_progress[0],
			  1);
}

/* At phase 1 the craft waits for its timer, then goes back to phase 0 at
 * the next place. */
static void check_dropoff_next_place(void)
{
	struct craft_data *craft = dropoff_world();
	struct ai_controller *controller = &craft->ai_controller;
	controller->maneuver_phase = 1;
	controller->maneuver_timer = 3;
	think_as(0);
	paiman_dropoffmaneuver();
	XVT_ASSERT_INT_EQ(controller->maneuver_phase, 1);
	XVT_ASSERT_INT_EQ(controller->waypoint_index, 1);
	controller->maneuver_timer = 0;
	paiman_dropoffmaneuver();
	XVT_ASSERT_INT_EQ(controller->maneuver_phase, 0);
	XVT_ASSERT_INT_EQ(controller->waypoint_index, 2);
	XVT_ASSERT_INT_EQ(controller->order_progress.goal_progress[0], 2);
}

/* Craft 1 follows craft 0 in formation 0. */
static struct craft_data *formation_world(int ordinal)
{
	fresh_world();
	place_craft(0, 0, 0x1000);
	g_test_objects[0].world_y = 0x2000;
	struct craft_data *craft = place_craft(1, 0, 0);
	craft->leader_obj_idx = 0;
	craft->craft_ordinal = (uint8_t)ordinal;
	return craft;
}

/* Place 0 of a formation is the leader's own: the follower there is pushed
 * onto its leader. Another place is pushed elsewhere. */
static void check_formation_place(void)
{
	struct craft_data *craft = formation_world(0);
	think_as(1);
	paiman_calcformation();
	XVT_ASSERT_INT_EQ(craft->push_accum_x, 0x1000);
	XVT_ASSERT_INT_EQ(craft->push_accum_y, 0x2000);
	XVT_ASSERT_INT_EQ(craft->push_accum_z, 0);

	craft = formation_world(1);
	think_as(1);
	paiman_calcformation();
	XVT_ASSERT_TRUE(craft->push_accum_x != 0x1000 ||
			craft->push_accum_y != 0x2000 ||
			craft->push_accum_z != 0);
}

/* Known failure escort_player_craft_push, issue #45: before pushing the
 * escort to its station the maneuver tests whether the escorted craft is a
 * player's craft at speed 10 or less, and then leaves the push at 0. Its
 * comment says it reads the escorted craft's leader index for that, always
 * 255: the escort of player 0's craft at speed 5 is pushed anyway. */
static void check_escort_holds_off_slow_player(void)
{
	struct craft_data *craft = escort_world(0x1000);
	g_test_objects[1].player_owner_idx = 0;
	g_test_mobiles[1].speed = 5;
	think_as(0);
	paiman_escortmaneuver();
	XVT_ASSERT_INT_EQ(craft->push_accum_x, 0);
	XVT_ASSERT_INT_EQ(craft->push_accum_y, 0);
	XVT_ASSERT_INT_EQ(craft->push_accum_z, 0);
}

/* Known failure escort_station_past_table, issue #204: the 28 stations are
 * 0 to 27, and the maneuver's comment says variable1 is not checked below
 * 28. Station 28 reads its offsets past the end of the three tables, and the
 * sanitizer stops the program on the read. The push should stay within a
 * station's reach of the escorted craft. */
static void check_escort_station_in_table(void)
{
	struct craft_data *craft = escort_world(0x1000);
	g_mission_flight_groups[0].fg.orders[0].variable1 = 28;
	think_as(0);
	paiman_escortmaneuver();
	XVT_ASSERT_TRUE(abs(craft->push_accum_x + 0x1000) <= 3072);
	XVT_ASSERT_TRUE(abs(craft->push_accum_y) <= 3072);
	XVT_ASSERT_TRUE(abs(craft->push_accum_z) <= 3072);
}

/* Known failure speed_away_push_sign, issue #46: a weave of speed away is a
 * Z push of 50 to 81, negated when the craft's Z push is 0 or more. Its
 * comment says the push is kept in 16 unsigned bits: from a push of 0 the
 * negated one is stored as 65,536 less it, a large push up. */
static void check_speed_away_push_down(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, 0, 0);
	think_as(0);
	paiman_setup_speed_away_turn(0);
	XVT_ASSERT_TRUE(craft->push_accum_z <= -50);
	XVT_ASSERT_TRUE(craft->push_accum_z >= -81);
}

/* Known failure board_approach_full_throttle, issue #47: stage 0 sets full
 * throttle beyond 0x4000 of the approach point. Its comment says the half
 * throttle for beyond 0x2000 replaces it at once, so the long approach is
 * flown at half throttle. */
static void check_board_far_full_throttle(void)
{
	struct craft_data *craft = board_world("boardtocontactpln");
	board_approach(0x5000);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0xFFFF);
}

/* Known failure dropoff_no_basis, issue #48: with no leaderless craft of
 * the delivered group yet, the drop-off point should be that group's mission
 * point, as paiorder_dropoffdestorder works it out for the same group with
 * no basis craft (0xFFFF). The maneuver's comment says it passes 255
 * instead, so the place turns with object 255, here turned a quarter. */
static void check_dropoff_without_basis(void)
{
	struct craft_data *craft = dropoff_world();
	struct mobile_object *unrelated = &g_test_mobiles[255];
	unrelated->cached_side_x = 0;
	unrelated->cached_side_y = Q15_ONE;
	unrelated->cached_fwd_x = -Q15_ONE;
	unrelated->cached_fwd_y = 0;
	think_as(0);
	paiman_dropoffmaneuver();
	mission_resolve_formation_slot_world_loc(2, 1, UINT16_MAX);
	XVT_ASSERT_INT_EQ(craft->push_accum_x, g_world_loc_x - 0x5000);
	XVT_ASSERT_INT_EQ(craft->push_accum_y, g_world_loc_y);
}

/* Known failure tenth_docking_remembered, issue #54: pai.h gives the docked
 * list as the objects the craft has docked with, so it does not board them
 * again, and the search reads the first docked_target_count entries. Its
 * field comment says a tenth docking writes slot 9, which no search reads:
 * after nine dockings, the tenth target is not among them. */
static void check_tenth_docking_remembered(void)
{
	struct craft_data *craft = board_world("boardtocontactpln");
	for (int slot = 0; slot < 9; ++slot) {
		craft->ai_flight.docked_target_signatures[slot] =
			(uint16_t)(slot + 1);
	}
	craft->ai_flight.docked_target_count = 9;
	finish_docking();
	int remembered = 0;
	for (int slot = 0; slot < craft->ai_flight.docked_target_count;
	     ++slot) {
		if (craft->ai_flight.docked_target_signatures[slot] ==
		    TEST_TARGET_SIGNATURE) {
			remembered = 1;
		}
	}
	XVT_ASSERT_INT_EQ(remembered, 1);
}

/* Known failure formation_seventh_place, issue #138: g_form_pos_x, Y and Z
 * give each of the 34 formations a place for craft_ordinal 0 to 5, and the
 * function's comment says it checks neither. A seventh craft, ordinal 6, in
 * formation 33 reads past the end of the tables, and the sanitizer stops
 * the program on the read. It should be pushed to one of its formation's six
 * places. */
static void check_formation_seventh_place(void)
{
	int place_x[6];
	int place_y[6];
	for (int ordinal = 0; ordinal < 6; ++ordinal) {
		struct craft_data *craft = formation_world(ordinal);
		craft->ai_flight.formation_type = 33;
		think_as(1);
		paiman_calcformation();
		place_x[ordinal] = craft->push_accum_x;
		place_y[ordinal] = craft->push_accum_y;
	}
	struct craft_data *craft = formation_world(6);
	craft->ai_flight.formation_type = 33;
	think_as(1);
	paiman_calcformation();
	int in_formation = 0;
	for (int ordinal = 0; ordinal < 6; ++ordinal) {
		if (craft->push_accum_x == place_x[ordinal] &&
		    craft->push_accum_y == place_y[ordinal]) {
			in_formation = 1;
		}
	}
	XVT_ASSERT_INT_EQ(in_formation, 1);
}

/* Known failure reload_display_other_machine, issue #203: the end of a give
 * docking with a player's craft runs in every machine's simulation, and the
 * craft records must come out the same on each. Player 0's X-wing has lost
 * its displays; on this machine, whose player is 7, they stay off, where
 * player 0's own machine turns every installed one back on. */
static void check_reload_restores_displays(void)
{
	board_world("boardtogivepln");
	struct craft_data *target = &g_test_craft[1];
	g_test_objects[1].player_owner_idx = 0;
	g_players[0].object_index = 1;
	target->damage_stats.installed_hud_feature_mask = 0x1FFF;
	target->damage_stats.active_hud_feature_mask = 0x0003;
	finish_docking();
	XVT_ASSERT_INT_EQ(target->damage_stats.active_hud_feature_mask, 0x1FFF);
}

/* Known failure single_waypoint_round, issue #205: goal_progress counts the
 * rounds flown over the waypoints. With waypoint 4 the only one, a craft
 * near it counts a round on each think: two thinks without moving count
 * two rounds. */
static void check_single_waypoint_one_round(void)
{
	fresh_world();
	g_mission_flight_groups[0].fg.mission_point_enabled[4] = 1;
	g_mission_flight_groups[0].fg.mission_point_x[4] = 10;
	struct craft_data *craft = place_craft(0, 0, 10 * 256 + 0x800);
	strcpy(g_plan_table[3].name, "formldr1pln");
	craft->ai_controller.current_plan_id = 3;
	craft->ai_controller.waypoint_index = 4;
	craft->ai_controller.aim_point_x = 10 * 256;
	think_as(0);
	paiman_cruisemaneuver();
	think_as(0);
	paiman_cruisemaneuver();
	XVT_ASSERT_INT_EQ(craft->ai_controller.order_progress.goal_progress[0],
			  1);
}

/* Known failure destroy_delay_without_countdown, issue #206: a craft boarded
 * to be destroyed gets a self-destruct delay of 15 to 30 seconds. The delay
 * is written only over a countdown already running, so a target with none,
 * as every craft has from its spawn, gets no delay. */
static void check_destroy_delay_set(void)
{
	board_world("boardtodestroypln");
	strcpy(g_plan_table[9].name, "selfdestroypln");
	finish_docking();
	XVT_ASSERT_TRUE(g_test_mobiles[1].lifetime_timer >= 15 * SECOND);
	XVT_ASSERT_TRUE(g_test_mobiles[1].lifetime_timer <= 30 * SECOND);
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
			{"escort_player_craft_push",
			 check_escort_holds_off_slow_player},
			{"escort_station_past_table",
			 check_escort_station_in_table},
			{"speed_away_push_sign", check_speed_away_push_down},
			{"board_approach_full_throttle",
			 check_board_far_full_throttle},
			{"dropoff_no_basis", check_dropoff_without_basis},
			{"tenth_docking_remembered",
			 check_tenth_docking_remembered},
			{"formation_seventh_place",
			 check_formation_seventh_place},
			{"reload_display_other_machine",
			 check_reload_restores_displays},
			{"single_waypoint_round",
			 check_single_waypoint_one_round},
			{"destroy_delay_without_countdown",
			 check_destroy_delay_set},
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
	check_init_maneuver();
	check_set_turn();
	check_set_speed();
	check_speed_away_weave();
	check_speed_away_other_side();
	check_advance_waypoint();
	check_cruise_next_waypoint();
	check_cruise_starship_stops();
	check_cruise_starship_turns();
	check_escort_far();
	check_escort_station();
	check_escort_range_edge();
	check_escort_heading();
	check_escort_alone();
	check_board_approach();
	check_board_waits_for_player();
	check_board_docks();
	check_board_transfer_waits();
	check_board_give_repairs_step();
	check_board_contact_done();
	check_board_inspects();
	check_board_give_repairs();
	check_board_destroy_countdown();
	check_board_separates();
	check_dropoff_push();
	check_dropoff_next_place();
	check_formation_place();
	return 0;
}
