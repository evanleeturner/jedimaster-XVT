/* Tests for xvt/flight/object/collide.c, the collision tests and what a hit
 * does. Each check builds the flight it needs in the game's own tables: a small
 * object table this file owns, with two craft slots, three shot slots, two
 * static slots and one free debris slot, the craft, guidance and player
 * records behind them, and the flight group, object type and model figures
 * the code reads. The model walk checks build small model trees in memory.
 * No game data is read.
 *
 * Checked: a hit on a craft (collide_damagecraft and collide_laserhitcraft:
 * the damage by source and its tallies, the shields, the ion damage, the
 * hull, the kill and how a craft ends, what a shot does to its target), the
 * collision step (collide_collisions: the pairs it tests, the player's
 * engine wash scan, inspections and hangar offers), the contact tests and
 * the bounce, engine wash and weapon jamming, the neighbour lists, and the
 * model walk down to the faces.
 *
 * Not checked here: the sounds and voice lines a hit asks for, which queue
 * only from the game's loaded sound tables, so only the state around them is
 * read; the proving grounds' lines, which run only while a flag that nothing
 * sets is on; the last branch of collide_damagecraft, which needs the flag
 * that makes the function return at once.
 *
 * POSIX only, for the alarm that stops a check whose call does not return. */
#define _POSIX_C_SOURCE 200809L

#include <SDL3/SDL_log.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "test_assert.h"
#include "xvt/assets/model_mesh.h"
#include "xvt/assets/object_genus.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai_brain.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/config.h"
#include "xvt/math/math2.h"
#include "xvt/math/trig2.h"
#include "xvt/util/game_rand.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/log/log.h"

enum {
	TEST_SLOTS = 8,
	TEST_CRAFT_END = 2,  /* Slots 0 and 1 hold craft. */
	TEST_SHOT_START = 2, /* Slots 2 to 4 hold shots and effects. */
	TEST_MAIN_END = 5,   /* Slots 5 and 6 hold static objects. */
	TEST_STATIC_COUNT = 2,
	TEST_SHOT = 2,
	TEST_OTHER_SHOT = 3,
	TEST_MINE = 5,
	TEST_DEBRIS = 7, /* The one free slot a detached wing can take. */
	TEST_FIGHTER_TYPE = 1,
	TEST_MINE_TYPE = 75,
	TEST_LATE_TYPE = 73,
	TEST_FIGHTER_EXTENT = 400,
	TEST_MINE_EXTENT = 800,
	TEST_MODEL = 1,
	TEST_MODEL_MAX_SPEED = 100,
	TEST_EXPLOSION_TYPE = 129,
};

/* A system message (pane type 3): with system messages off, the message
 * code builds it, logs it and drops it. */
static char g_test_message[] = "\003message";

enum {
	MODEL_MESH_SLOTS = 8,
	WING_LEFT = 1,
	WING_RIGHT = 2,
	TORN_OFF_STATE = 4,
	BREAKUP_STEP_STATE = 2,
};

static struct opt_node g_craft_model_nodes[MODEL_MESH_SLOTS];
static struct opt_node *g_craft_model_roots[MODEL_MESH_SLOTS];
static struct mesh_descriptor g_craft_model_descriptors[MODEL_MESH_SLOTS];
static uint16_t g_craft_model_handle;
/* The object type craft_model gives the model to: the starfighter type, or
 * TEST_LATE_TYPE, an object type past the 73 that have a mesh cache. */
static int g_craft_model_type = TEST_FIGHTER_TYPE;

static struct object_record g_test_objects[TEST_SLOTS];
static struct mobile_object g_test_mobiles[TEST_MAIN_END];
static struct mobile_object g_test_debris_mobile;
static struct craft_data g_test_craft[TEST_MAIN_END];
static struct warhead_guidance_state
	g_test_guidance[TEST_MAIN_END - TEST_SHOT_START];

static void stop_on_alarm(int signal_number)
{
	static const char message[] =
		"check failed: the call did not return within the time allowed\n";
	(void)signal_number;
	if (write(2, message, sizeof message - 1) < 0) {
		_exit(1);
	}
	_exit(1);
}

/* For a check whose call may never return: the program fails after the given
 * seconds. */
static void fail_after_seconds(unsigned int seconds)
{
	signal(SIGALRM, stop_on_alarm);
	alarm(seconds);
}

/* The engine's log lines, kept without Aeron's "xvt: " prefix; lines past the
 * capacity are dropped. Checks read the values the DEBUG lines record. */
enum {
	LOG_LINE_CAPACITY = 400,
	LOG_LINE_SIZE = 320,
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

/* The last kept line that starts with start (an event and any of its first
 * fields), or NULL. */
static const char *find_line(const char *start)
{
	size_t length = strlen(start);
	for (int i = g_log_line_count - 1; i >= 0; --i) {
		if (strncmp(g_log_lines[i], start, length) == 0) {
			return g_log_lines[i];
		}
	}
	return NULL;
}

/* 1 when the last kept line that starts with start also holds fields. */
static int line_holds(const char *start, const char *fields)
{
	const char *line = find_line(start);
	return line != NULL && strstr(line, fields) != NULL;
}

/* The number of kept lines that start with start. */
static int count_lines(const char *start)
{
	size_t length = strlen(start);
	int count = 0;
	for (int i = 0; i < g_log_line_count; ++i) {
		if (strncmp(g_log_lines[i], start, length) == 0) {
			++count;
		}
	}
	return count;
}

static void place(int slot, int x, int y, int z)
{
	g_test_objects[slot].world_x = x;
	g_test_objects[slot].world_y = y;
	g_test_objects[slot].world_z = z;
}

/* Two computer-flown starfighters in flight groups 0 and 1 (teams 0 and 1),
 * at the origin and 20,000 units along X, neither moving; empty shot and
 * static slots; every proximity list empty and not due for a rebuild. Sounds
 * are off, so nothing is queued. No player flies, the mission's running
 * figures are clear, collisions are off, and the models and system messages
 * the checks set are put back to none. */
static void fresh_world(void)
{
	g_log_line_count = 0;
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(&g_test_debris_mobile, 0, sizeof g_test_debris_mobile);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_players, 0, sizeof g_players);
	memset(g_player_flight_transient_timers, 0,
	       sizeof g_player_flight_transient_timers);
	memset(g_object_slot_range_by_genus, 0,
	       sizeof g_object_slot_range_by_genus);
	memset(g_test_guidance, 0, sizeof g_test_guidance);
	memset(g_mission_flight_groups, 0,
	       3 * sizeof g_mission_flight_groups[0]);
	memset(g_mission_fg_stats, 0, sizeof g_mission_fg_stats);
	g_object_table = g_test_objects;
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = TEST_CRAFT_END;
	g_projectile_object_slot_start = TEST_SHOT_START;
	g_projectile_object_slot_end = TEST_MAIN_END;
	g_region_main_object_slot_end = TEST_MAIN_END;
	g_region_static_object_slot_count = TEST_STATIC_COUNT;
	g_projectile_guidance_states = g_test_guidance;
	g_object_type_table[TEST_FIGHTER_TYPE].max_bounds_extent =
		TEST_FIGHTER_EXTENT;
	g_object_type_table[TEST_FIGHTER_TYPE].model_index = TEST_MODEL;
	g_object_type_table[TEST_MINE_TYPE].max_bounds_extent =
		TEST_MINE_EXTENT;
	g_model_defs[TEST_MODEL].max_speed = TEST_MODEL_MAX_SPEED;
	g_loaded_models[TEST_FIGHTER_TYPE] = 0;
	g_object_type_table[TEST_FIGHTER_TYPE].asset_flags &= ~1;
	g_object_type_mesh_cache[TEST_FIGHTER_TYPE].mesh_count = 0;
	g_loaded_models[TEST_LATE_TYPE] = 0;
	g_object_type_table[TEST_LATE_TYPE].asset_flags &= ~1;
	static const int model_types[] = {30, 49, 54, 91};
	for (size_t i = 0; i < sizeof model_types / sizeof model_types[0];
	     ++i) {
		g_loaded_models[model_types[i]] = 0;
		g_object_type_table[model_types[i]].asset_flags &= ~1;
		if (model_types[i] < 73) {
			g_object_type_mesh_cache[model_types[i]].mesh_count = 0;
		}
	}
	g_craft_model_type = TEST_FIGHTER_TYPE;
	for (size_t id = 0; id < sizeof g_str_in_flight_messages /
					 sizeof g_str_in_flight_messages[0];
	     ++id) {
		g_str_in_flight_messages[id] = g_test_message;
	}
	g_system_message_display_enabled = 0;
	g_replay_view_mode = 0;
	g_flight_conf_sfx_enabled = 0;
	g_flight_sfx_side_effect_gate = 0;
	g_flight_sim_side_effects_suppressed = 0;
	memset(&g_flight_mission_state.runtime, 0,
	       sizeof g_flight_mission_state.runtime);
	g_flight_mission_state.proving_grounds_mode_active = 0;
	g_flight_mission_state.craft_impact_bounce_enabled = 0;
	g_flight_mission_state.collisions_enabled = 0;
	g_flight_mission_state.difficulty = 0;
	g_game_config.voice_tactical_officer_level = 0;
	g_mission_file_version = 0;
	g_cache_resolved_opt_node_refs = 0;
	g_elapsed_ticks = 1;
	g_local_player = 0;
	for (int group = 0; group < 3; ++group) {
		g_mission_flight_groups[group].player_owner_idx = -1;
		g_mission_flight_groups[group].fg.team = (uint8_t)group;
	}
	for (int slot = 0; slot < TEST_MAIN_END; ++slot) {
		g_test_objects[slot].player_owner_idx = -1;
		g_test_objects[slot].mobj = &g_test_mobiles[slot];
		g_test_mobiles[slot].proximity_list.rebuild_ticks = 0x7FFF;
	}
	for (int slot = TEST_MAIN_END; slot < TEST_SLOTS; ++slot) {
		g_test_objects[slot].player_owner_idx = -1;
	}
	g_test_objects[TEST_DEBRIS].mobj = &g_test_debris_mobile;
	g_object_slot_range_by_genus[CRAFT_GENUS_SMALL_DEBRIS].start =
		TEST_DEBRIS;
	g_object_slot_range_by_genus[CRAFT_GENUS_SMALL_DEBRIS].end =
		TEST_DEBRIS + 1;
	for (int player = 0; player < 8; ++player) {
		g_players[player].object_index = -1;
		g_players[player].current_target_object_idx = -1;
	}
	for (int slot = 0; slot < TEST_CRAFT_END; ++slot) {
		g_test_objects[slot].object_type = TEST_FIGHTER_TYPE;
		g_test_objects[slot].genus_id = CRAFT_GENUS_STARFIGHTER;
		g_test_objects[slot].flight_group_idx = (uint8_t)slot;
		g_test_mobiles[slot].seconds_alive = 10;
		g_test_mobiles[slot].team = (uint8_t)slot;
		g_test_mobiles[slot].p_craft = &g_test_craft[slot];
		g_test_craft[slot].craft_ordinal = 1;
		g_test_craft[slot].last_attacker_obj_idx = UINT16_MAX;
		g_test_craft[slot].carrier_obj_idx = UINT16_MAX;
		g_test_craft[slot].ai_controller.target_obj_idx = UINT16_MAX;
		g_test_craft[slot].ai_flight.impact_obj_idx = UINT16_MAX;
	}
	place(1, 20000, 0, 0);
}

/* A shot of type in slot, fired by source, moving this step from (x0, y0,
 * z0) to (x1, y1, z1), not homing. */
static void place_shot(int slot, uint8_t type, int source, int x0, int y0,
		       int z0, int x1, int y1, int z1)
{
	g_test_objects[slot].object_type = type;
	g_test_objects[slot].genus_id = CRAFT_GENUS_OTHER_PROJECTILE;
	place(slot, x1, y1, z1);
	g_test_mobiles[slot].prev_world_x = x0;
	g_test_mobiles[slot].prev_world_y = y0;
	g_test_mobiles[slot].prev_world_z = z0;
	g_test_mobiles[slot].family = 1;
	g_test_mobiles[slot].seconds_alive = 1;
	g_test_mobiles[slot].source_obj_idx = (uint16_t)source;
	g_test_mobiles[slot].p_warhead_guidance =
		&g_test_guidance[slot - TEST_SHOT_START];
	g_test_guidance[slot - TEST_SHOT_START].source_player_idx = -1;
	g_test_guidance[slot - TEST_SHOT_START].target_obj_idx = UINT16_MAX;
}

/* A working mine of flight group 2 in the first static slot. Its type is no
 * space craft, so its kill is scored without a craft record. */
static void place_mine(int x, int y, int z)
{
	g_mission_flight_groups[2].fg.craft_type = TEST_MINE_TYPE;
	g_object_type_table[TEST_MINE_TYPE].family_id = CRAFT_FAMILY_WEAPON;
	g_test_objects[TEST_MINE].object_type = TEST_MINE_TYPE;
	g_test_objects[TEST_MINE].genus_id = CRAFT_GENUS_MINE;
	g_test_objects[TEST_MINE].flight_group_idx = 2;
	g_test_objects[TEST_MINE].type_specific_word = 1023;
	place(TEST_MINE, x, y, z);
}

/* ------------------------------------------------------------------------ */
/* Rough distances. */

static void check_rough_distance(void)
{
	/* The largest plus a quarter of each of the others. */
	XVT_ASSERT_INT_EQ(collide_roughdistance3du(400, 40, 80), 400 + 10 + 20);
	XVT_ASSERT_INT_EQ(collide_roughdistance3du(40, 400, 80), 400 + 10 + 20);
	XVT_ASSERT_INT_EQ(collide_roughdistance3du(40, 80, 400), 400 + 10 + 20);
	/* On a tie for the largest, abs_dz is the base. */
	XVT_ASSERT_INT_EQ(collide_roughdistance3du(100, 100, 0), 50);
	XVT_ASSERT_INT_EQ(collide_roughdistance3du(0, 100, 100), 125);

	/* The signed form takes the absolute values first. */
	XVT_ASSERT_INT_EQ(collide_roughdistance3d(-400, 40, -80), 430);
	XVT_ASSERT_INT_EQ(collide_roughdistance3d(40, -400, 80), 430);
	XVT_ASSERT_INT_EQ(collide_roughdistance3d(-40, 80, -400), 430);
	XVT_ASSERT_INT_EQ(collide_roughdistance3d(-100, 100, 0), 50);
}

/* ------------------------------------------------------------------------ */
/* The proximity lists. */

static void check_proximity_speed(void)
{
	fresh_world();
	/* A space craft: the larger of its model's max_speed and its speed. */
	XVT_ASSERT_INT_EQ(collide_get_mobile_object_proximity_speed_q12(0),
			  TEST_MODEL_MAX_SPEED << 12);
	g_test_mobiles[0].speed = TEST_MODEL_MAX_SPEED + 30;
	XVT_ASSERT_INT_EQ(collide_get_mobile_object_proximity_speed_q12(0),
			  (TEST_MODEL_MAX_SPEED + 30) << 12);

	/* A weapon: its guidance cruise_speed, or its speed without one. */
	place_shot(TEST_SHOT, PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0, 0, 0, 0, 0,
		   0, 0);
	g_test_mobiles[TEST_SHOT].speed = 900;
	g_test_guidance[0].cruise_speed = 700;
	XVT_ASSERT_INT_EQ(
		collide_get_mobile_object_proximity_speed_q12(TEST_SHOT),
		700 << 12);
	g_test_mobiles[TEST_SHOT].p_warhead_guidance = NULL;
	XVT_ASSERT_INT_EQ(
		collide_get_mobile_object_proximity_speed_q12(TEST_SHOT),
		900 << 12);

	/* Any other family, or no mobile_object: 0. */
	g_test_mobiles[TEST_SHOT].family = 5;
	XVT_ASSERT_INT_EQ(
		collide_get_mobile_object_proximity_speed_q12(TEST_SHOT), 0);
	place_mine(0, 0, 0);
	XVT_ASSERT_INT_EQ(
		collide_get_mobile_object_proximity_speed_q12(TEST_MINE), 0);
}

/* The contact ticks the insert comment gives for two objects whose bounds
 * lie clearance apart, closing at the two speeds (in the game's units). */
static int expected_contact_ticks(int clearance, int speed_a, int speed_b)
{
	return 13275 * (clearance >> 8) / (((speed_a + speed_b) << 12) >> 8);
}

static void check_insert_orders_by_contact(void)
{
	fresh_world();
	struct mobile_object_proximity_list *list =
		&g_test_mobiles[0].proximity_list;

	/* Craft 1 lies 20,000 units off, its bounds 19,200 apart. */
	collide_insert_mobile_object_proximity_candidate(list, 0, 1);
	XVT_ASSERT_INT_EQ(list->count, 1);
	XVT_ASSERT_INT_EQ(list->obj_idx[0], 1);
	int far_ticks = expected_contact_ticks(20000 - 2 * TEST_FIGHTER_EXTENT,
					       TEST_MODEL_MAX_SPEED,
					       TEST_MODEL_MAX_SPEED);
	XVT_ASSERT_INT_EQ(list->contact_ticks[0], far_ticks);

	/* A shot closer in, at 8,000 units and speed 600, goes first. */
	place_shot(TEST_SHOT, PROJECTILE_OBJECT_TYPE_REBEL_LASER, 1, 8000, 0, 0,
		   8000, 0, 0);
	g_test_mobiles[TEST_SHOT].p_warhead_guidance = NULL;
	g_test_mobiles[TEST_SHOT].speed = 600;
	g_object_type_table[PROJECTILE_OBJECT_TYPE_REBEL_LASER]
		.max_bounds_extent = 0;
	collide_insert_mobile_object_proximity_candidate(list, 0, TEST_SHOT);
	XVT_ASSERT_INT_EQ(list->count, 2);
	XVT_ASSERT_INT_EQ(list->obj_idx[0], TEST_SHOT);
	XVT_ASSERT_INT_EQ(list->contact_ticks[0],
			  expected_contact_ticks(8000 - TEST_FIGHTER_EXTENT,
						 600, TEST_MODEL_MAX_SPEED));
	XVT_ASSERT_INT_EQ(list->obj_idx[1], 1);

	/* Overlapping bounds give 0, whatever the speeds. */
	place(TEST_SHOT, 100, 0, 0);
	g_test_mobiles[TEST_SHOT].speed = 0;
	collide_insert_mobile_object_proximity_candidate(list, 0, TEST_SHOT);
	XVT_ASSERT_INT_EQ(list->count, 2);
	XVT_ASSERT_INT_EQ(list->obj_idx[0], TEST_SHOT);
	XVT_ASSERT_INT_EQ(list->contact_ticks[0], 0);

	/* An entry already there moves to its new place, not added twice. */
	place(TEST_SHOT, 60000, 0, 0);
	collide_insert_mobile_object_proximity_candidate(list, 0, TEST_SHOT);
	XVT_ASSERT_INT_EQ(list->count, 2);
	XVT_ASSERT_INT_EQ(list->obj_idx[0], 1);
	XVT_ASSERT_INT_EQ(list->contact_ticks[0], far_ticks);
	XVT_ASSERT_INT_EQ(list->obj_idx[1], TEST_SHOT);
	XVT_ASSERT_TRUE(list->contact_ticks[1] > far_ticks);
}

static void check_insert_motionless_apart(void)
{
	/* Bounds apart and no speed between them: nothing is added. */
	fresh_world();
	place_mine(10000, 0, 0);
	struct mobile_object_proximity_list *list =
		&g_test_mobiles[TEST_SHOT].proximity_list;
	place_shot(TEST_SHOT, PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0, 0, 0, 0, 0,
		   0, 0);
	collide_insert_mobile_object_proximity_candidate(list, TEST_SHOT,
							 TEST_MINE);
	XVT_ASSERT_INT_EQ(list->count, 0);
}

static void check_insert_full_list(void)
{
	fresh_world();
	struct mobile_object_proximity_list *list =
		&g_test_mobiles[0].proximity_list;
	/* Sixteen entries due at 10, 20, ... 160 ticks. */
	list->count = 16;
	for (int entry = 0; entry < 16; ++entry) {
		list->obj_idx[entry] = (uint16_t)(100 + entry);
		list->contact_ticks[entry] = 10 * (entry + 1);
	}
	list->rebuild_ticks = 0x7FFF;

	/* Craft 1, due later than every entry, is left out, and the rebuild
	 * comes when it would have been due. */
	int far_ticks = expected_contact_ticks(20000 - 2 * TEST_FIGHTER_EXTENT,
					       TEST_MODEL_MAX_SPEED,
					       TEST_MODEL_MAX_SPEED);
	XVT_ASSERT_TRUE(far_ticks > 160);
	collide_insert_mobile_object_proximity_candidate(list, 0, 1);
	XVT_ASSERT_INT_EQ(list->count, 16);
	for (int entry = 0; entry < 16; ++entry) {
		XVT_ASSERT_INT_EQ(list->obj_idx[entry], 100 + entry);
	}
	XVT_ASSERT_INT_EQ(list->rebuild_ticks, far_ticks);

	/* Craft 1 due sooner than the last entry: the last is dropped, and the
	 * rebuild comes when that one would have been due. */
	place(1, 3000, 0, 0);
	int near_ticks = expected_contact_ticks(3000 - 2 * TEST_FIGHTER_EXTENT,
						TEST_MODEL_MAX_SPEED,
						TEST_MODEL_MAX_SPEED);
	XVT_ASSERT_TRUE(near_ticks > 30 && near_ticks < 40);
	list->rebuild_ticks = 0x7FFF;
	collide_insert_mobile_object_proximity_candidate(list, 0, 1);
	XVT_ASSERT_INT_EQ(list->count, 16);
	XVT_ASSERT_INT_EQ(list->obj_idx[2], 102);
	XVT_ASSERT_INT_EQ(list->obj_idx[3], 1);
	XVT_ASSERT_INT_EQ(list->contact_ticks[3], near_ticks);
	XVT_ASSERT_INT_EQ(list->obj_idx[4], 103);
	XVT_ASSERT_INT_EQ(list->obj_idx[15], 114);
	XVT_ASSERT_INT_EQ(list->rebuild_ticks, 160);
}

static void check_remove_candidate(void)
{
	fresh_world();
	struct mobile_object_proximity_list *list =
		&g_test_mobiles[0].proximity_list;
	list->count = 3;
	for (int entry = 0; entry < 3; ++entry) {
		list->obj_idx[entry] = (uint16_t)(10 + entry);
		list->contact_ticks[entry] = 5 * (entry + 1);
	}

	/* Not there: nothing changes. */
	collide_remove_mobile_object_proximity_candidate(list, 99);
	XVT_ASSERT_INT_EQ(list->count, 3);

	/* Later entries move up. */
	collide_remove_mobile_object_proximity_candidate(list, 10);
	XVT_ASSERT_INT_EQ(list->count, 2);
	XVT_ASSERT_INT_EQ(list->obj_idx[0], 11);
	XVT_ASSERT_INT_EQ(list->contact_ticks[0], 10);
	XVT_ASSERT_INT_EQ(list->obj_idx[1], 12);
	XVT_ASSERT_INT_EQ(list->contact_ticks[1], 15);

	collide_remove_mobile_object_proximity_candidate(list, 12);
	XVT_ASSERT_INT_EQ(list->count, 1);
	XVT_ASSERT_INT_EQ(list->obj_idx[0], 11);
}

static void check_reset_slot(void)
{
	fresh_world();
	/* A mobile object's own list is emptied and due for a rebuild. */
	g_test_mobiles[1].proximity_list.count = 4;
	g_test_mobiles[1].proximity_list.rebuild_ticks = 50;
	collide_reset_object_proximity_for_slot(1);
	XVT_ASSERT_INT_EQ(g_test_mobiles[1].proximity_list.count, 0);
	XVT_ASSERT_INT_EQ(g_test_mobiles[1].proximity_list.rebuild_ticks, 0);

	/* A static object goes into the list of every player's craft, and of
	 * no other. Craft 0 is a player's, 1,000 units from the mine. */
	g_test_objects[0].player_owner_idx = 0;
	place_mine(1000, 0, 0);
	collide_reset_object_proximity_for_slot(TEST_MINE);
	XVT_ASSERT_INT_EQ(g_test_mobiles[0].proximity_list.count, 1);
	XVT_ASSERT_INT_EQ(g_test_mobiles[0].proximity_list.obj_idx[0],
			  TEST_MINE);
	XVT_ASSERT_INT_EQ(g_test_mobiles[1].proximity_list.count, 0);
}

static void check_reset_neighbors(void)
{
	fresh_world();
	place_mine(1000, 0, 0);
	struct mobile_object_proximity_list *list =
		&g_test_mobiles[0].proximity_list;
	list->count = 2;
	list->obj_idx[0] = 1;
	list->obj_idx[1] = TEST_MINE;
	list->contact_ticks[0] = 7;
	list->contact_ticks[1] = 9;
	list->rebuild_ticks = 40;
	g_test_mobiles[1].proximity_list.count = 3;
	g_test_mobiles[1].proximity_list.rebuild_ticks = 50;

	collide_reset_neighbor_proximity_lists(0);
	/* Each neighbour rebuilds its own list. */
	XVT_ASSERT_INT_EQ(g_test_mobiles[1].proximity_list.count, 0);
	XVT_ASSERT_INT_EQ(g_test_mobiles[1].proximity_list.rebuild_ticks, 0);
	/* Craft 0's list is left as it is. */
	XVT_ASSERT_INT_EQ(list->count, 2);
	XVT_ASSERT_INT_EQ(list->obj_idx[0], 1);
	XVT_ASSERT_INT_EQ(list->obj_idx[1], TEST_MINE);
	XVT_ASSERT_INT_EQ(list->contact_ticks[0], 7);
	XVT_ASSERT_INT_EQ(list->contact_ticks[1], 9);
	XVT_ASSERT_INT_EQ(list->rebuild_ticks, 40);

	/* No mobile_object: nothing happens. */
	collide_reset_neighbor_proximity_lists(TEST_MINE);
}

/* ------------------------------------------------------------------------ */
/* Explosions and damage figures. */

static void check_convert_to_explosion(void)
{
	fresh_world();
	place_shot(TEST_SHOT, WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 0, 0, 0,
		   0, 500, 0, 0);
	g_test_objects[TEST_SHOT].type_specific_byte[0] = 7;
	g_test_objects[TEST_SHOT].roll = 99;
	g_test_mobiles[TEST_SHOT].effect_size = 3;
	g_test_mobiles[TEST_SHOT].speed = 900;
	g_test_mobiles[TEST_SHOT].seconds_alive = 4;
	g_test_mobiles[TEST_SHOT].lifetime_timer = 300;
	g_test_mobiles[TEST_SHOT].roll_impulse_rate = 12;

	/* With sounds off, fsfx_play_sound queues nothing and returns 0. */
	XVT_ASSERT_INT_EQ(collide_convert_object_to_explosion(
				  TEST_SHOT, TEST_EXPLOSION_TYPE),
			  0);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].object_type,
			  TEST_EXPLOSION_TYPE);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].genus_id, 13);
	XVT_ASSERT_INT_EQ(g_test_mobiles[TEST_SHOT].family, 5);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].type_specific_byte[0], 2);
	XVT_ASSERT_INT_EQ(g_test_mobiles[TEST_SHOT].effect_size, 0);
	XVT_ASSERT_INT_EQ(g_test_mobiles[TEST_SHOT].speed, 0);
	XVT_ASSERT_INT_EQ(g_test_mobiles[TEST_SHOT].seconds_alive, 0);
	XVT_ASSERT_INT_EQ(g_test_mobiles[TEST_SHOT].lifetime_timer, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].roll, 0);
	XVT_ASSERT_INT_EQ(g_test_mobiles[TEST_SHOT].roll_impulse_rate, 0);
	XVT_ASSERT_INT_EQ(g_test_mobiles[TEST_SHOT].orient_matrix_dirty, 1);
	/* Where it was is left alone. */
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].world_x, 500);
}

static void check_craft_damage_amount(void)
{
	fresh_world();
	g_test_mobiles[0].damage_amount = 1200;
	/* Outside mission file version 14: the damage_amount alone. */
	XVT_ASSERT_INT_EQ(collide_compute_craft_damage_amount(1, 0), 1200);

	/* A Super Star Destroyer's is 16 times its damage_amount. */
	g_test_objects[0].object_type = 54;
	XVT_ASSERT_INT_EQ(collide_compute_craft_damage_amount(1, 0), 16 * 1200);
	g_test_objects[0].object_type = TEST_FIGHTER_TYPE;

	/* Version 14, against a starship: plus, per launcher, an eighth of the
	 * rounds in its first and last slots times the warhead's damage. */
	g_mission_file_version = 14;
	g_test_objects[1].genus_id = CRAFT_GENUS_STARSHIP;
	g_test_craft[0].warhead_launcher_count = 2;
	g_model_defs[TEST_MODEL].warhead_launcher_first_slot[0] = 2;
	g_model_defs[TEST_MODEL].warhead_launcher_last_slot[0] = 3;
	g_model_defs[TEST_MODEL].warhead_launcher_first_slot[1] = 4;
	g_model_defs[TEST_MODEL].warhead_launcher_last_slot[1] = 5;
	g_test_craft[0].weapon_slots[2].ammo_count = 3;
	g_test_craft[0].weapon_slots[3].ammo_count = 2;
	g_test_craft[0].weapon_slots[4].ammo_count = 1;
	g_test_craft[0].weapon_slots[5].ammo_count = 0;
	g_test_craft[0].warhead_slot_type_ids[0] =
		WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE;
	g_test_craft[0].warhead_slot_type_ids[1] =
		WARHEAD_OBJECT_TYPE_PROTON_TORPEDO;
	unsigned int missile =
		g_projectile_type_data
			.damage[WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE -
				PROJECTILE_OBJECT_TYPE_FIRST];
	unsigned int torpedo =
		g_projectile_type_data
			.damage[WARHEAD_OBJECT_TYPE_PROTON_TORPEDO -
				PROJECTILE_OBJECT_TYPE_FIRST];
	XVT_ASSERT_INT_EQ(collide_compute_craft_damage_amount(1, 0),
			  1200 + ((5 * missile) >> 3) + ((1 * torpedo) >> 3));

	/* A platform counts as a starship; a starfighter gets no warheads. */
	g_test_objects[1].genus_id = CRAFT_GENUS_PLATFORM;
	XVT_ASSERT_INT_EQ(collide_compute_craft_damage_amount(1, 0),
			  1200 + ((5 * missile) >> 3) + ((1 * torpedo) >> 3));
	g_test_objects[1].genus_id = CRAFT_GENUS_STARFIGHTER;
	XVT_ASSERT_INT_EQ(collide_compute_craft_damage_amount(1, 0), 1200);

	/* A Dreadnaught with a space bomb launcher, ramming a Super Star
	 * Destroyer: 4,800,000 for that launcher. */
	g_test_objects[0].object_type = 48;
	g_test_objects[0].genus_id = CRAFT_GENUS_STARSHIP;
	g_object_type_table[48].model_index = TEST_MODEL;
	g_test_objects[1].object_type = 54;
	g_test_objects[1].genus_id = CRAFT_GENUS_STARSHIP;
	g_test_craft[0].warhead_launcher_count = 1;
	g_test_craft[0].warhead_slot_type_ids[0] =
		WARHEAD_OBJECT_TYPE_SPACE_BOMB;
	XVT_ASSERT_INT_EQ(collide_compute_craft_damage_amount(1, 0),
			  1200 + 4800000);
	/* Not while it breaks up. */
	g_test_craft[0].object_kind = CRAFT_OBJECT_KIND_BREAKING_UP;
	unsigned int bomb =
		g_projectile_type_data.damage[WARHEAD_OBJECT_TYPE_SPACE_BOMB -
					      PROJECTILE_OBJECT_TYPE_FIRST];
	XVT_ASSERT_INT_EQ(collide_compute_craft_damage_amount(1, 0),
			  1200 + ((5 * bomb) >> 3));
	g_mission_file_version = 0;
}

/* A laser shot of damage_amount 250 from craft 0 (flight group 0, AI level
 * 2) hitting craft 1, whose front shield holds 1,000 and rear 500. */
static void laser_at_craft(void)
{
	fresh_world();
	place_shot(TEST_SHOT, PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0, 19000, 0,
		   0, 20000, 0, 0);
	g_test_mobiles[TEST_SHOT].damage_amount = 250;
	g_mission_flight_groups[0].fg.group_ai = 2;
	g_test_craft[1].shield_energy[0] = 1000;
	g_test_craft[1].shield_energy[1] = 500;
	g_test_craft[1].hull_max = 100;
	g_test_craft[1].ai_controller.saved_rand_seed = 77;
	g_game_rand_feedback_state = 5;
}

static void check_damage_shield_takes(void)
{
	/* The shield on the hit side takes it all; the hull is untouched. */
	laser_at_craft();
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, TEST_SHOT, 0), 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 250);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[1], 500);
	XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 0);
	/* Tallied by source: the shot's craft, its flight group and AI
	 * level. */
	struct craft_damage_stats *stats = &g_test_craft[1].damage_stats;
	XVT_ASSERT_INT_EQ(stats->damage_received_total, 250);
	XVT_ASSERT_INT_EQ(stats->damage_from_flight_group_amount[0], 250);
	XVT_ASSERT_INT_EQ(stats->damage_from_ai_skill[2], 250);
	/* The shared random state is put back. */
	XVT_ASSERT_INT_EQ(g_game_rand_feedback_state, 5);

	/* Side 1 is the rear. */
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, TEST_SHOT, 1), 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 250);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[1], 500 - 250);
	XVT_ASSERT_INT_EQ(stats->damage_received_total, 500);
}

static void check_damage_scaled_by_genus(void)
{
	/* Divided by 16 for a starship or platform, by 4 for a freighter. */
	laser_at_craft();
	g_test_objects[1].genus_id = CRAFT_GENUS_STARSHIP;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 250 / 16);
	laser_at_craft();
	g_test_objects[1].genus_id = CRAFT_GENUS_PLATFORM;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 250 / 16);
	laser_at_craft();
	g_test_objects[1].genus_id = CRAFT_GENUS_FREIGHTER;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 250 / 4);

	/* A static object deals 4 times its max_bounds_extent, counted as a
	 * collision when it is normal debris. */
	laser_at_craft();
	place_mine(20000, 0, 0);
	g_test_objects[TEST_MINE].genus_id = CRAFT_GENUS_NORMAL_DEBRIS;
	g_object_type_table[TEST_MINE_TYPE].max_bounds_extent = 200;
	collide_damagecraft(1, -1, TEST_MINE, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 4 * 200);
	XVT_ASSERT_INT_EQ(g_test_craft[1].damage_stats.damage_from_collision,
			  4 * 200);
	g_object_type_table[TEST_MINE_TYPE].max_bounds_extent =
		TEST_MINE_EXTENT;
}

static void check_damage_refused(void)
{
	/* While side effects are suppressed: 1 at once, nothing dealt. */
	laser_at_craft();
	g_flight_sim_side_effects_suppressed = 1;
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, TEST_SHOT, 0), 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000);
	XVT_ASSERT_INT_EQ(g_test_craft[1].damage_stats.damage_received_total,
			  0);
	g_flight_sim_side_effects_suppressed = 0;

	/* The victim's flight group, or a source craft's, has status 20. */
	laser_at_craft();
	g_mission_flight_groups[1].fg.status1 = 20;
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, TEST_SHOT, 0), 1);
	g_mission_flight_groups[1].fg.status1 = 0;
	g_mission_flight_groups[1].fg.status2 = 20;
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, TEST_SHOT, 0), 1);
	g_mission_flight_groups[1].fg.status2 = 0;
	g_mission_flight_groups[0].fg.status1 = 20;
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, 0, 0), 1);
	g_mission_flight_groups[0].fg.status1 = 0;
	g_mission_flight_groups[0].fg.status2 = 20;
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, 0, 0), 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000);
	XVT_ASSERT_INT_EQ(g_test_craft[1].damage_stats.damage_received_total,
			  0);

	/* With no status 20, the craft's ram deals its damage_amount, counted
	 * as a collision. */
	g_mission_flight_groups[0].fg.status2 = 0;
	g_test_mobiles[0].damage_amount = 300;
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, 0, 0), 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 300);
	XVT_ASSERT_INT_EQ(g_test_craft[1].damage_stats.damage_from_collision,
			  300);
}

/* ------------------------------------------------------------------------ */
/* The face tests. */

static void check_face_plane_crossing(void)
{
	/* The plane z = 0. */
	const float normal[3] = {0.0f, 0.0f, 1.0f};
	const float vertex[3] = {0.0f, 0.0f, 0.0f};
	float t = 7.0f;

	/* Ends on opposite sides: the crossing's fraction from the start. */
	const float below[3] = {0.0f, 0.0f, -100.0f};
	const float above[3] = {0.0f, 0.0f, 300.0f};
	XVT_ASSERT_INT_EQ(collide_intersect_segment_with_face_plane(
				  normal, vertex, below, above, &t),
			  1);
	XVT_ASSERT_CLOSE(t, 0.25, 1e-6, "100 of 400 units, exact in float");
	XVT_ASSERT_INT_EQ(collide_intersect_segment_with_face_plane(
				  normal, vertex, above, below, &t),
			  1);
	XVT_ASSERT_CLOSE(t, 0.75, 1e-6, "300 of 400 units, exact in float");

	/* Both on one side: 0, and *out_t left alone. */
	const float higher[3] = {0.0f, 0.0f, 500.0f};
	t = 7.0f;
	XVT_ASSERT_INT_EQ(collide_intersect_segment_with_face_plane(
				  normal, vertex, above, higher, &t),
			  0);
	XVT_ASSERT_CLOSE(t, 7.0, 0,
			 "left alone: the value set before the call");

	/* Within 10 of the plane counts as on it: the start first. */
	const float near_start[3] = {0.0f, 0.0f, 9.0f};
	const float near_end[3] = {0.0f, 0.0f, -9.5f};
	XVT_ASSERT_INT_EQ(collide_intersect_segment_with_face_plane(
				  normal, vertex, near_start, near_end, &t),
			  1);
	XVT_ASSERT_CLOSE(t, 0.0, 0, "the start on the plane sets exactly 0");
	XVT_ASSERT_INT_EQ(collide_intersect_segment_with_face_plane(
				  normal, vertex, higher, near_end, &t),
			  1);
	XVT_ASSERT_CLOSE(t, 1.0, 0, "the end on the plane sets exactly 1");
	/* 11 units off is not on it. */
	const float off_plane[3] = {0.0f, 0.0f, 11.0f};
	XVT_ASSERT_INT_EQ(collide_intersect_segment_with_face_plane(
				  normal, vertex, off_plane, higher, &t),
			  0);
}

static void check_point_in_face(void)
{
	/* A triangle and a square in the plane z = 0, seen along Z. */
	const float vertices[12] = {
		0.0f,	0.0f,	0.0f, 100.0f, 0.0f,   0.0f,
		100.0f, 100.0f, 0.0f, 0.0f,   100.0f, 0.0f,
	};
	const float normal[3] = {0.0f, 0.0f, 1.0f};
	const int32_t triangle[4] = {0, 1, 2, -1};
	const int32_t square[4] = {0, 1, 2, 3};

	float point[3] = {60.0f, 20.0f, 0.0f};
	XVT_ASSERT_INT_EQ(collide_point_in_face_polygon(normal, vertices,
							triangle, point),
			  1);
	/* The two coordinates kept, X and Y, are moved to [1] and [2]. */
	XVT_ASSERT_CLOSE(point[1], 60.0, 0,
			 "a copied coordinate, not a computed one");
	XVT_ASSERT_CLOSE(point[2], 20.0, 0,
			 "a copied coordinate, not a computed one");

	/* Inside the square but outside the triangle. */
	float corner[3] = {20.0f, 60.0f, 0.0f};
	XVT_ASSERT_INT_EQ(collide_point_in_face_polygon(normal, vertices,
							triangle, corner),
			  0);
	float corner_again[3] = {20.0f, 60.0f, 0.0f};
	XVT_ASSERT_INT_EQ(collide_point_in_face_polygon(normal, vertices,
							square, corner_again),
			  1);
	float outside[3] = {150.0f, 50.0f, 0.0f};
	XVT_ASSERT_INT_EQ(collide_point_in_face_polygon(normal, vertices,
							square, outside),
			  0);

	/* The same square in the plane x = 0, seen along X: Y and Z kept. */
	const float wall[12] = {
		0.0f, 0.0f,   0.0f,   0.0f, 100.0f, 0.0f,
		0.0f, 100.0f, 100.0f, 0.0f, 0.0f,   100.0f,
	};
	const float wall_normal[3] = {-1.0f, 0.0f, 0.0f};
	float on_wall[3] = {0.0f, 30.0f, 70.0f};
	XVT_ASSERT_INT_EQ(collide_point_in_face_polygon(wall_normal, wall,
							square, on_wall),
			  1);
	XVT_ASSERT_CLOSE(on_wall[1], 30.0, 0,
			 "a copied coordinate, not a computed one");
	XVT_ASSERT_CLOSE(on_wall[2], 70.0, 0,
			 "a copied coordinate, not a computed one");
	float off_wall[3] = {0.0f, 130.0f, 70.0f};
	XVT_ASSERT_INT_EQ(collide_point_in_face_polygon(wall_normal, wall,
							square, off_wall),
			  0);

	/* The normal's sign does not matter. */
	const float wall_out[3] = {1.0f, 0.0f, 0.0f};
	float on_wall_out[3] = {0.0f, 30.0f, 70.0f};
	XVT_ASSERT_INT_EQ(collide_point_in_face_polygon(wall_out, wall, square,
							on_wall_out),
			  1);
	XVT_ASSERT_CLOSE(on_wall_out[1], 30.0, 0,
			 "a copied coordinate, not a computed one");
	XVT_ASSERT_CLOSE(on_wall_out[2], 70.0, 0,
			 "a copied coordinate, not a computed one");

	/* In the plane y = 0, seen along Y: X and Z kept. */
	const float floor_square[12] = {
		0.0f,	0.0f, 0.0f,   100.0f, 0.0f, 0.0f,
		100.0f, 0.0f, 100.0f, 0.0f,   0.0f, 100.0f,
	};
	const float floor_normal[3] = {0.0f, 1.0f, 0.0f};
	float on_floor[3] = {40.0f, 0.0f, 80.0f};
	XVT_ASSERT_INT_EQ(collide_point_in_face_polygon(
				  floor_normal, floor_square, square, on_floor),
			  1);
	XVT_ASSERT_CLOSE(on_floor[1], 40.0, 0,
			 "a copied coordinate, not a computed one");
	XVT_ASSERT_CLOSE(on_floor[2], 80.0, 0,
			 "a copied coordinate, not a computed one");
	float off_floor[3] = {40.0f, 0.0f, 180.0f};
	XVT_ASSERT_INT_EQ(collide_point_in_face_polygon(floor_normal,
							floor_square, square,
							off_floor),
			  0);

	/* Either winding: the triangle and the square walked the other way. */
	const int32_t triangle_back[4] = {0, 2, 1, -1};
	const int32_t square_back[4] = {0, 3, 2, 1};
	float inside[3] = {60.0f, 20.0f, 0.0f};
	XVT_ASSERT_INT_EQ(collide_point_in_face_polygon(normal, vertices,
							triangle_back, inside),
			  1);
	float beyond_edge[3] = {120.0f, 20.0f, 0.0f};
	XVT_ASSERT_INT_EQ(collide_point_in_face_polygon(
				  normal, vertices, triangle_back, beyond_edge),
			  0);
	float beyond_side[3] = {50.0f, -20.0f, 0.0f};
	XVT_ASSERT_INT_EQ(collide_point_in_face_polygon(
				  normal, vertices, triangle_back, beyond_side),
			  0);
	float above_diagonal[3] = {20.0f, 60.0f, 0.0f};
	XVT_ASSERT_INT_EQ(collide_point_in_face_polygon(normal, vertices,
							triangle_back,
							above_diagonal),
			  0);
	float in_square[3] = {20.0f, 60.0f, 0.0f};
	XVT_ASSERT_INT_EQ(collide_point_in_face_polygon(normal, vertices,
							square_back, in_square),
			  1);
	float past_top[3] = {20.0f, 160.0f, 0.0f};
	XVT_ASSERT_INT_EQ(collide_point_in_face_polygon(normal, vertices,
							square_back, past_top),
			  0);
}

/* ------------------------------------------------------------------------ */
/* The model walk. The sweep it tests is the one the model test
 * (collide_check_swept_model_collision) last moved into a target's own axes:
 * set_walk_sweep makes it run from (100, 100, 100) to (-100, -100, -100). */

/* Mesh vertex nodes: a vertex, then the box's low and high corners. */
static struct opt_vector g_around_box[3] = {
	{0, 0, 0}, {-200, -200, -200}, {200, 200, 200}};
static struct opt_vector g_beside_box[3] = {
	{0, 0, 0}, {150, -200, -200}, {200, 200, 200}};

/* Boxes the sweep lies wholly beside: above it, then below it, on X, Y and
 * Z. */
static struct opt_vector g_off_boxes[6][3] = {
	{{0, 0, 0}, {150, -200, -200}, {200, 200, 200}},
	{{0, 0, 0}, {-200, -200, -200}, {-150, 200, 200}},
	{{0, 0, 0}, {-200, 150, -200}, {200, 200, 200}},
	{{0, 0, 0}, {-200, -200, -200}, {200, -150, 200}},
	{{0, 0, 0}, {-200, -200, 150}, {200, 200, 200}},
	{{0, 0, 0}, {-200, -200, -200}, {200, 200, -150}},
};

/* Boxes the sweep is not wholly beside: on each axis one touching its start
 * from above and one touching its end from below, then on each axis one
 * holding its start but not its end. */
static struct opt_vector g_near_boxes[9][3] = {
	{{0, 0, 0}, {100, -200, -200}, {150, 200, 200}},
	{{0, 0, 0}, {-200, -200, -200}, {-100, 200, 200}},
	{{0, 0, 0}, {-200, 100, -200}, {200, 150, 200}},
	{{0, 0, 0}, {-200, -200, -200}, {200, -100, 200}},
	{{0, 0, 0}, {-200, -200, 100}, {200, 200, 150}},
	{{0, 0, 0}, {-200, -200, -200}, {200, 200, -100}},
	{{0, 0, 0}, {50, -200, -200}, {150, 200, 200}},
	{{0, 0, 0}, {-200, 50, -200}, {200, 150, 200}},
	{{0, 0, 0}, {-200, -200, 50}, {200, 200, 150}},
};

/* Craft 1, at (20,000, 0, 0), has side, forward and up axes of half length
 * along X, Y and Z, and an object type whose asset_flags bit 0 is clear, so the
 * model test returns 0 once it has moved the sweep into the model's axes (side,
 * backward, up). The sweep runs from (20,200, -200, 200) to (19,800, 200,
 * -200). */
static void set_walk_sweep(void)
{
	g_test_mobiles[1].cached_side_x = 0x4000;
	g_test_mobiles[1].cached_fwd_y = 0x4000;
	g_test_mobiles[1].cached_up_z = 0x4000;
	g_object_type_table[TEST_FIGHTER_TYPE].asset_flags = 0;
	g_collision_segment_start_world_x = 20200;
	g_collision_segment_start_world_y = -200;
	g_collision_segment_start_world_z = 200;
	g_collision_probe_world_x = 19800;
	g_collision_probe_world_y = 200;
	g_collision_probe_world_z = -200;
	XVT_ASSERT_INT_EQ(collide_check_swept_model_collision(0, 1), 0);
}

static void mesh_node(struct opt_node *node, char *name, struct opt_vector *box)
{
	memset(node, 0, sizeof *node);
	node->p_name = name;
	node->node_type = OPT_MESHVERTS;
	node->payload_count = 3;
	node->payload = box;
}

static void reference_node(struct opt_node *node, char *name, char *target)
{
	memset(node, 0, sizeof *node);
	node->p_name = name;
	node->node_type = OPT_NODEREF;
	node->payload = target;
}

static char g_name_mesh[] = "mesh";
static char g_name_ref[] = "ref";
static char g_name_a[] = "a";
static char g_name_b[] = "b";
static char g_name_missing[] = "missing";

static void check_model_walk(void)
{
	fresh_world();
	struct opt_node beside;
	struct opt_node around;
	struct opt_node group;
	struct opt_node reference;
	struct opt_node *roots[2];
	struct optimized_poly_object model;
	memset(&model, 0, sizeof model);
	model.root_nodes = roots;

	set_walk_sweep();

	/* A mesh whose box the sweep lies wholly beside stops the walk. */
	mesh_node(&beside, g_name_mesh, g_beside_box);
	mesh_node(&around, NULL, g_around_box);
	XVT_ASSERT_INT_EQ(collide_test_sweep_against_opt_node(&model, &beside),
			  1);
	XVT_ASSERT_INT_EQ(collide_test_sweep_against_opt_node(&model, &around),
			  0);
	XVT_ASSERT_INT_EQ(collide_test_sweep_against_opt_node(&model, NULL), 0);
	/* Beside it on either side of any axis. */
	for (int box = 0; box < 6; ++box) {
		struct opt_node off;
		mesh_node(&off, NULL, g_off_boxes[box]);
		XVT_ASSERT_INT_EQ(
			collide_test_sweep_against_opt_node(&model, &off), 1);
	}
	/* Touching it, or holding one end of it, is not beside it. */
	for (int box = 0; box < 9; ++box) {
		struct opt_node near;
		mesh_node(&near, NULL, g_near_boxes[box]);
		XVT_ASSERT_INT_EQ(
			collide_test_sweep_against_opt_node(&model, &near), 0);
	}

	/* A face group walks its first child only. */
	struct opt_node *first_around[2] = {&around, &beside};
	memset(&group, 0, sizeof group);
	group.node_type = OPT_FACEGROUP;
	group.child_count = 2;
	group.p_children = first_around;
	XVT_ASSERT_INT_EQ(collide_test_sweep_against_opt_node(&model, &group),
			  0);
	struct opt_node *first_beside[2] = {&beside, &around};
	group.p_children = first_beside;
	XVT_ASSERT_INT_EQ(collide_test_sweep_against_opt_node(&model, &group),
			  1);

	/* Any other node walks every child, and a stop passes up. */
	group.node_type = OPT_NODEREF + 100;
	group.p_children = first_around;
	XVT_ASSERT_INT_EQ(collide_test_sweep_against_opt_node(&model, &group),
			  1);
	group.p_children = first_beside;
	XVT_ASSERT_INT_EQ(collide_test_sweep_against_opt_node(&model, &group),
			  1);
	struct opt_node *both_around[2] = {&around, &around};
	group.p_children = both_around;
	XVT_ASSERT_INT_EQ(collide_test_sweep_against_opt_node(&model, &group),
			  0);

	/* A reference is followed to the node of that name. */
	reference_node(&reference, g_name_ref, g_name_mesh);
	roots[0] = &reference;
	roots[1] = &beside;
	model.root_node_count = 2;
	XVT_ASSERT_INT_EQ(
		collide_test_sweep_against_opt_node(&model, &reference), 1);
	/* A reference to a name no node has ends the branch. */
	reference_node(&reference, g_name_ref, g_name_missing);
	XVT_ASSERT_INT_EQ(
		collide_test_sweep_against_opt_node(&model, &reference), 0);
}

/* Known failure self_reference_hangs, issue #252: the walk returns at the end
 * of a branch or on a missing node, but a reference whose own name is the name
 * it points at resolves to itself, and the walk follows it for good. */
static void check_self_reference_returns(void)
{
	fresh_world();
	struct opt_node reference;
	struct opt_node *roots[1] = {&reference};
	struct optimized_poly_object model;
	memset(&model, 0, sizeof model);
	model.root_nodes = roots;
	model.root_node_count = 1;
	reference_node(&reference, g_name_ref, g_name_ref);
	fail_after_seconds(5);
	XVT_ASSERT_INT_EQ(
		collide_test_sweep_against_opt_node(&model, &reference), 0);
}

/* Known failure reference_pair_hangs, issue #252: two references that name
 * each other send the walk back and forth for good. */
static void check_reference_pair_returns(void)
{
	fresh_world();
	struct opt_node to_b;
	struct opt_node to_a;
	struct opt_node *roots[2] = {&to_b, &to_a};
	struct optimized_poly_object model;
	memset(&model, 0, sizeof model);
	model.root_nodes = roots;
	model.root_node_count = 2;
	reference_node(&to_b, g_name_a, g_name_b);
	reference_node(&to_a, g_name_b, g_name_a);
	fail_after_seconds(5);
	XVT_ASSERT_INT_EQ(collide_test_sweep_against_opt_node(&model, &to_b),
			  0);
}

/* ------------------------------------------------------------------------ */
/* Shots that hit. */

/* A laser shot from craft 0 crossing a mine this step: from 1,000 units short
 * of it to 1,000 past. The shot's proximity list holds the mine, due now. The
 * craft aims at the mine, so the static test does not pass it over. */
static void shot_at_mine(void)
{
	fresh_world();
	place_mine(1000, 0, 0);
	place_shot(TEST_SHOT, PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0, 0, 0, 0,
		   2000, 0, 0);
	g_test_craft[0].ai_controller.target_obj_idx = TEST_MINE;
	struct mobile_object_proximity_list *list =
		&g_test_mobiles[TEST_SHOT].proximity_list;
	list->count = 1;
	list->obj_idx[0] = TEST_MINE;
	list->contact_ticks[0] = 0;
}

static void check_shot_hits_static(void)
{
	shot_at_mine();
	collide_collisions();
	/* The mine is destroyed: its slot emptied, a destroyed outcome for its
	 * flight group. */
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_MINE].object_type, 0);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[2]
			.outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED],
		1);
	/* The shot became the impact effect, and its list was emptied. */
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].genus_id,
			  CRAFT_GENUS_EXPLOSION);
	XVT_ASSERT_INT_EQ(g_test_mobiles[TEST_SHOT].proximity_list.count, 0);
	XVT_ASSERT_INT_EQ(
		g_test_mobiles[TEST_SHOT].proximity_list.rebuild_ticks, 0);
}

/* Known failure static_hit_uncounted, issue #180: a shot that hits a static
 * object goes to static_apply_static_hit with its hit stats recorded through
 * mission_record_projectile_hit_stats, which counts a laser hit for the craft
 * that fired it. The stats are recorded after the shot's slot has become the
 * impact effect, whose type counts nothing. */
static void check_static_hit_counted(void)
{
	shot_at_mine();
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_MINE].object_type, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[0].weapon_stats.laser_hits_scored, 1);
}

/* Shot 2 of owner_type from craft 0 crosses the path of shot 3 of
 * other_type from craft 1, which sits at (1,000, 0, 0); shot 2's proximity
 * list holds shot 3, due now. */
static void shots_cross(uint8_t owner_type, uint8_t other_type)
{
	fresh_world();
	g_object_type_table[owner_type].max_bounds_extent = 400;
	g_object_type_table[other_type].max_bounds_extent = 400;
	place_shot(TEST_SHOT, owner_type, 0, 0, 0, 0, 2000, 0, 0);
	place_shot(TEST_OTHER_SHOT, other_type, 1, 1000, 0, 0, 1000, 0, 0);
	struct mobile_object_proximity_list *list =
		&g_test_mobiles[TEST_SHOT].proximity_list;
	list->count = 1;
	list->obj_idx[0] = TEST_OTHER_SHOT;
	list->contact_ticks[0] = 0;
	collide_collisions();
}

static void check_shots_collide(void)
{
	/* A cannon shot meeting a warhead: the cannon shot's hit counts, it
	 * becomes a laser impact and the warhead an explosion. */
	shots_cross(PROJECTILE_OBJECT_TYPE_REBEL_LASER,
		    WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE);
	XVT_ASSERT_INT_EQ(g_test_craft[0].weapon_stats.laser_hits_scored, 1);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].object_type, 131);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].genus_id,
			  CRAFT_GENUS_EXPLOSION);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_OTHER_SHOT].object_type, 129);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_OTHER_SHOT].genus_id,
			  CRAFT_GENUS_EXPLOSION);

	/* An ion shot becomes an ion impact. */
	shots_cross(PROJECTILE_OBJECT_TYPE_ION_LASER,
		    WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE);
	XVT_ASSERT_INT_EQ(g_test_craft[0].weapon_stats.ion_hits_scored, 1);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].object_type, 132);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_OTHER_SHOT].object_type, 129);

	/* A warhead meeting a cannon shot: the cannon shot's hit counts, for
	 * the craft that fired it, and both explode. */
	shots_cross(WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE,
		    PROJECTILE_OBJECT_TYPE_IMPERIAL_LASER);
	XVT_ASSERT_INT_EQ(g_test_craft[1].weapon_stats.laser_hits_scored, 1);
	XVT_ASSERT_INT_EQ(g_test_craft[0].weapon_stats.warhead_hits_scored, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].object_type, 129);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_OTHER_SHOT].object_type, 129);
}

/* A magnetic pulse from craft 0 hitting craft 1; nothing aims at craft 1 yet.
 * The pulse homes on craft 1 at level 3. */
static void pulse_at_craft(void)
{
	fresh_world();
	place_shot(TEST_SHOT, WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE, 0, 19000, 0,
		   0, 20000, 0, 0);
	g_test_guidance[0].homing_tier = 3;
	g_test_guidance[0].target_obj_idx = 1;
	g_test_mobiles[TEST_SHOT].lifetime_timer = 40;
	g_test_mobiles[TEST_SHOT].effect_size = 3;
	g_test_craft[1].ai_flight.threat_obj_idx = 9;
	g_test_mobiles[1].speed = 55;
	g_test_objects[1].pitch = 0x1234;
	g_test_objects[1].yaw = 0x4321;
	g_collision_segment_start_world_x = 19000;
	g_collision_segment_start_world_y = 0;
	g_collision_segment_start_world_z = 0;
	g_collision_probe_world_x = 20000;
	g_collision_probe_world_y = 0;
	g_collision_probe_world_z = 0;
	g_collision_hit_offset_x = 600;
	g_collision_hit_offset_y = 10;
	g_collision_hit_offset_z = -20;
}

static void check_pulse_hit(void)
{
	pulse_at_craft();
	g_test_craft[1].weapon_fire_inhibit_timer = 100;
	collide_laserhitcraft(TEST_SHOT, 1, -1);

	/* The first hit from team 0 marks the craft and counts an attacked
	 * outcome. */
	XVT_ASSERT_INT_EQ(g_test_craft[1].attacked_by_team[0] & 1, 1);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ATTACKED],
			  1);
	/* A starfighter firing becomes the attacker; the threat and the hit
	 * count follow. */
	XVT_ASSERT_INT_EQ(g_test_craft[1].last_attacker_obj_idx, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].ai_flight.threat_obj_idx, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].ai_flight.hits_this_maneuver, 1);
	/* A pulse on a computer-flown starfighter adds 3,540 ticks. */
	XVT_ASSERT_INT_EQ(g_test_craft[1].weapon_fire_inhibit_timer,
			  100 + 3540);
	/* The shot becomes a warhead's impact effect at the hit point, moving
	 * with the craft. */
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].object_type, 129);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].genus_id, 13);
	XVT_ASSERT_INT_EQ(g_test_mobiles[TEST_SHOT].family, 5);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].type_specific_byte[0], 2);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].world_x, 19000 + 600);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].world_y, 10);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].world_z, -20);
	XVT_ASSERT_INT_EQ(g_test_mobiles[TEST_SHOT].speed, 55);
	XVT_ASSERT_INT_EQ(g_test_mobiles[TEST_SHOT].seconds_alive, 0);
	XVT_ASSERT_INT_EQ(g_test_mobiles[TEST_SHOT].lifetime_timer, 0);
	XVT_ASSERT_INT_EQ(g_test_mobiles[TEST_SHOT].effect_size, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].pitch, 0x1234);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].yaw, 0x4321);

	/* A second hit from the same team counts no second outcome, and the
	 * recorded attacker of a computer-flown craft stays. */
	place_shot(TEST_OTHER_SHOT, WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE, 0,
		   19000, 0, 0, 20000, 0, 0);
	g_test_craft[1].last_attacker_obj_idx = 7;
	collide_laserhitcraft(TEST_OTHER_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ATTACKED],
			  1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].last_attacker_obj_idx, 7);
	XVT_ASSERT_INT_EQ(g_test_craft[1].ai_flight.hits_this_maneuver, 2);
	XVT_ASSERT_INT_EQ(g_test_craft[1].weapon_fire_inhibit_timer,
			  100 + 2 * 3540);
}

static void check_pulse_on_large_craft(void)
{
	/* Other than a starfighter, transport or utility vehicle: 7,080. */
	pulse_at_craft();
	g_test_objects[1].genus_id = CRAFT_GENUS_STARSHIP;
	g_test_craft[1].weapon_fire_inhibit_timer = 0;
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].weapon_fire_inhibit_timer, 7080);
}

/* Comment: a magnetic pulse adds 3,540 ticks to an AI craft's
 * weapon_fire_inhibit_timer, or 7,080 for a craft other than a starfighter,
 * transport or utility vehicle. */
static void check_pulse_inhibit_by_genus(void)
{
	/* Other than a starfighter, transport or utility vehicle: 7,080. */
	static const struct {
		uint8_t genus;
		int ticks;
	} genera[] = {
		{CRAFT_GENUS_STARFIGHTER, 3540},
		{CRAFT_GENUS_TRANSPORT, 3540},
		{CRAFT_GENUS_UTILITY_VEHICLE, 3540},
		{CRAFT_GENUS_STARSHIP, 7080},
		{CRAFT_GENUS_FREIGHTER, 7080},
		{CRAFT_GENUS_PLATFORM, 7080},
	};
	for (size_t i = 0; i < sizeof genera / sizeof genera[0]; ++i) {
		pulse_at_craft();
		g_test_objects[1].genus_id = genera[i].genus;
		g_test_craft[1].weapon_fire_inhibit_timer = 0;
		collide_laserhitcraft(TEST_SHOT, 1, -1);
		XVT_ASSERT_INT_EQ(g_test_craft[1].weapon_fire_inhibit_timer,
				  genera[i].ticks);
	}
}

static void check_hit_from_itself(void)
{
	/* A shot from the craft itself does nothing. */
	pulse_at_craft();
	g_test_mobiles[TEST_SHOT].source_obj_idx = 1;
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].object_type,
			  WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE);
	XVT_ASSERT_INT_EQ(g_test_craft[1].ai_flight.hits_this_maneuver, 0);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ATTACKED],
			  0);
}

/* Whether the guidance record of slot still reads as a homing warhead aimed
 * at target, as the computer pilots' threat scans read it. */
static int homes_on(int slot, uint16_t target)
{
	const struct warhead_guidance_state *guidance =
		g_test_mobiles[slot].p_warhead_guidance;
	return guidance != NULL && guidance->homing_tier != 0 &&
	       guidance->target_obj_idx == target;
}

/* Known failure impact_keeps_homing, issue #210: the shot becomes an impact
 * effect, but its guidance record keeps the warhead's homing level and
 * target, so the computer pilots' scans take the effect for a warhead still
 * coming at the craft. The rule rests on the issue; the comment on
 * mobile_object.p_warhead_guidance says only that the effect keeps the
 * pointer. */
static void check_impact_stops_homing(void)
{
	pulse_at_craft();
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].object_type, 129);
	XVT_ASSERT_TRUE(!homes_on(TEST_SHOT, 1));
}

/* Known failure explosion_keeps_homing, issue #210: a warhead turned into an
 * explosion in place keeps its homing level and target, as above. */
static void check_explosion_stops_homing(void)
{
	fresh_world();
	place_shot(TEST_SHOT, WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 0, 0, 0,
		   0, 500, 0, 0);
	g_test_guidance[0].homing_tier = 3;
	g_test_guidance[0].target_obj_idx = 1;
	collide_convert_object_to_explosion(TEST_SHOT, TEST_EXPLOSION_TYPE);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].genus_id, 13);
	XVT_ASSERT_TRUE(!homes_on(TEST_SHOT, 1));
}

/* ------------------------------------------------------------------------ */
/* collide_damagecraft: who dealt the damage, and how much. */

/* Player flies craft slot: the slot keeps its flight group and team. */
static void player_flies(int slot, int player)
{
	g_test_objects[slot].player_owner_idx = player;
	g_players[player].object_index = slot;
	g_players[player].team = (int16_t)slot;
	g_players[player].participation_state = 1;
	g_players[player].bound_flight_group_idx = (uint16_t)slot;
}

/* The tallies of damage_stats that name a source, in the order total,
 * collision, starship, mine; fails when one differs. */
static void expect_tallies(const struct craft_damage_stats *stats, int total,
			   int collision, int starship, int mine)
{
	XVT_ASSERT_INT_EQ(stats->damage_received_total, total);
	XVT_ASSERT_INT_EQ(stats->damage_from_collision, collision);
	XVT_ASSERT_INT_EQ(stats->damage_from_starship, starship);
	XVT_ASSERT_INT_EQ(stats->damage_from_mine, mine);
}

static int tallied_for_players(const struct craft_damage_stats *stats)
{
	int sum = 0;
	for (int player = 0; player < 8; ++player) {
		sum += stats->damage_from_player[player];
	}
	return sum;
}

static int tallied_for_ai(const struct craft_damage_stats *stats)
{
	int sum = 0;
	for (int level = 0; level < 6; ++level) {
		sum += stats->damage_from_ai_skill[level];
	}
	return sum;
}

/* The Sith ranks, AI levels 6 and 7, are tallied as Jedi (level 5): the
 * table holds 6 levels, and a level past it would write over installed_hud_feature_mask,
 * the field after it. Levels 0 to 5 keep their own entry. */
static void check_tally_sith_levels(void)
{
	for (int level = 0; level <= 7; ++level) {
		laser_at_craft();
		g_mission_flight_groups[0].fg.group_ai = (uint8_t)level;
		g_test_craft[1].damage_stats.installed_hud_feature_mask =
			0x1234;
		XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, TEST_SHOT, 0), 1);
		const struct craft_damage_stats *stats =
			&g_test_craft[1].damage_stats;
		int counted = level > 5 ? 5 : level;
		for (int entry = 0; entry < 6; ++entry) {
			XVT_ASSERT_INT_EQ(stats->damage_from_ai_skill[entry],
					  entry == counted ? 250 : 0);
		}
		XVT_ASSERT_INT_EQ(tallied_for_ai(stats), 250);
		XVT_ASSERT_INT_EQ(stats->installed_hud_feature_mask, 0x1234);
	}
}

/* damage.h: damage_from_player names the player flying the craft that hit,
 * or for a shot the player flying its firer or the one its guidance record
 * names; a craft's hit on another also counts in damage_from_collision, and
 * the attacker's flight group gets the amount. */
static void check_tally_player_sources(void)
{
	/* A craft flown by player 3 rams craft 1. */
	laser_at_craft();
	player_flies(0, 3);
	g_test_mobiles[0].damage_amount = 300;
	collide_damagecraft(1, -1, 0, 0);
	struct craft_damage_stats *stats = &g_test_craft[1].damage_stats;
	XVT_ASSERT_INT_EQ(stats->damage_from_player[3], 300);
	XVT_ASSERT_INT_EQ(tallied_for_players(stats), 300);
	XVT_ASSERT_INT_EQ(tallied_for_ai(stats), 0);
	XVT_ASSERT_INT_EQ(stats->damage_from_flight_group_amount[0], 300);
	expect_tallies(stats, 300, 300, 0, 0);

	/* A shot whose guidance record names player 4: that player, not the
	 * one flying its firer. */
	laser_at_craft();
	player_flies(0, 2);
	g_test_guidance[0].source_player_idx = 4;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(stats->damage_from_player[4], 250);
	XVT_ASSERT_INT_EQ(tallied_for_players(stats), 250);
	XVT_ASSERT_INT_EQ(tallied_for_ai(stats), 0);
	XVT_ASSERT_INT_EQ(stats->damage_from_flight_group_amount[0], 250);
	expect_tallies(stats, 250, 0, 0, 0);

	/* A shot with no guidance record: the player flying its firer. */
	laser_at_craft();
	player_flies(0, 2);
	g_test_mobiles[TEST_SHOT].p_warhead_guidance = NULL;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(stats->damage_from_player[2], 250);
	XVT_ASSERT_INT_EQ(tallied_for_players(stats), 250);
	expect_tallies(stats, 250, 0, 0, 0);
}

/* damage.h: damage_from_collision is a craft's hit whoever flew it;
 * damage_from_starship and damage_from_mine are shots from sources that are
 * neither a player nor a starfighter, by whether the source sits in a
 * static slot; engine wash counts as a starship's. */
static void check_tally_other_sources(void)
{
	struct craft_damage_stats *stats = &g_test_craft[1].damage_stats;

	/* A computer-flown craft rams craft 1. */
	laser_at_craft();
	g_test_mobiles[0].damage_amount = 300;
	collide_damagecraft(1, -1, 0, 0);
	XVT_ASSERT_INT_EQ(tallied_for_players(stats), 0);
	XVT_ASSERT_INT_EQ(tallied_for_ai(stats), 0);
	XVT_ASSERT_INT_EQ(stats->damage_from_flight_group_amount[0], 300);
	expect_tallies(stats, 300, 300, 0, 0);

	/* A shot from a computer-flown starship. */
	laser_at_craft();
	g_test_objects[0].genus_id = CRAFT_GENUS_STARSHIP;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(tallied_for_ai(stats), 0);
	XVT_ASSERT_INT_EQ(stats->damage_from_flight_group_amount[0], 250);
	expect_tallies(stats, 250, 0, 250, 0);

	/* A shot from a static slot, a mine's: the mine tally, and its own
	 * flight group's. */
	laser_at_craft();
	place_mine(20000, 0, 0);
	g_test_mobiles[TEST_SHOT].source_obj_idx = TEST_MINE;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(stats->damage_from_flight_group_amount[2], 250);
	XVT_ASSERT_INT_EQ(stats->damage_from_flight_group_amount[0], 0);
	expect_tallies(stats, 250, 0, 0, 250);

	/* A static object that is not debris: in the total only. */
	laser_at_craft();
	g_test_craft[1].hull_max = 1000000;
	place_mine(20000, 0, 0);
	collide_damagecraft(1, -1, TEST_MINE, 0);
	XVT_ASSERT_INT_EQ(tallied_for_players(stats), 0);
	XVT_ASSERT_INT_EQ(tallied_for_ai(stats), 0);
	expect_tallies(stats, 4 * TEST_MINE_EXTENT, 0, 0, 0);

	/* Engine wash of 300: the starship tally, no flight group's. */
	laser_at_craft();
	collide_damagecraft(1, -1, UINT16_MAX - 1, 300);
	XVT_ASSERT_INT_EQ(stats->damage_from_flight_group_amount[0], 0);
	XVT_ASSERT_INT_EQ(stats->damage_from_flight_group_amount[1], 0);
	XVT_ASSERT_INT_EQ(tallied_for_players(stats), 0);
	expect_tallies(stats, 300, 0, 300, 0);

	/* No source at all: the total only. */
	laser_at_craft();
	g_test_craft[1].hull_max = 1000000;
	collide_damagecraft(1, -1, UINT16_MAX, 0);
	XVT_ASSERT_INT_EQ(tallied_for_players(stats), 0);
	XVT_ASSERT_INT_EQ(tallied_for_ai(stats), 0);
	expect_tallies(stats, 0x20000, 0, 0, 0);
}

/* damage.h: damage_received_by_player_owned_craft is the part of the total
 * taken while a player flew the craft. */
static void check_tally_player_owned_victim(void)
{
	laser_at_craft();
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	struct craft_damage_stats *stats = &g_test_craft[1].damage_stats;
	XVT_ASSERT_INT_EQ(stats->damage_received_by_player_owned_craft, 0);
	player_flies(1, 5);
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(stats->damage_received_total, 500);
	XVT_ASSERT_INT_EQ(stats->damage_received_by_player_owned_craft, 250);
}

/* The damage comment: no source is 0x20000, and a static object deals 4
 * times its max_bounds_extent, or 0x20000 from 0x8000 up. */
static void check_damage_fixed_amounts(void)
{
	/* No source: 0x20000, shared out as shield first, then hull. */
	laser_at_craft();
	g_test_craft[1].hull_max = 1000000;
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, UINT16_MAX, 0), 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[1], 500);
	XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 0x20000 - 1000);
	XVT_ASSERT_TRUE(line_holds("combat.damage_taken", "amount=131072"));
	XVT_ASSERT_TRUE(line_holds("combat.damage_taken", "through=130072"));

	/* A static object of extent 0x7FFF deals 4 times that; of 0x8000, the
	 * default. */
	int extent[2] = {0x7FFF, 0x8000};
	int expected[2] = {4 * 0x7FFF, 0x20000};
	for (int case_index = 0; case_index < 2; ++case_index) {
		laser_at_craft();
		g_test_craft[1].hull_max = 1000000;
		place_mine(20000, 0, 0);
		g_object_type_table[TEST_MINE_TYPE].max_bounds_extent =
			extent[case_index];
		collide_damagecraft(1, -1, TEST_MINE, 0);
		XVT_ASSERT_INT_EQ(
			g_test_craft[1].damage_stats.damage_received_total,
			expected[case_index]);
		XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage,
				  expected[case_index] - 1000);
	}
	g_object_type_table[TEST_MINE_TYPE].max_bounds_extent =
		TEST_MINE_EXTENT;
}

/* The engine wash: source UINT16_MAX - 1 makes the hit_side_or_damage_amount
 * argument the damage, taken by the front shield; a starship's is divided by
 * 16 like any other. */
static void check_damage_engine_wash(void)
{
	laser_at_craft();
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, UINT16_MAX - 1, 300), 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 300);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[1], 500);
	XVT_ASSERT_TRUE(line_holds("combat.damage_taken", "wash=1"));
	XVT_ASSERT_TRUE(line_holds("combat.damage_taken", "amount=300"));

	laser_at_craft();
	g_test_objects[1].genus_id = CRAFT_GENUS_STARSHIP;
	collide_damagecraft(1, -1, UINT16_MAX - 1, 320);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 320 / 16);
	XVT_ASSERT_INT_EQ(g_test_craft[1].damage_stats.damage_from_starship,
			  320 / 16);

	/* A shot is not wash. */
	laser_at_craft();
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_TRUE(line_holds("combat.damage_taken", "wash=0"));
}

/* The damage comment: in mission file version 14 the Muurian and Corellian
 * transports take a quarter, as a freighter does; in no other version. */
static void check_damage_transport_scale(void)
{
	int types[2] = {CRAFT_SPECIES_MUURIAN_TRANSPORT,
			CRAFT_SPECIES_CORELLIAN_TRANSPORT};
	for (int case_index = 0; case_index < 2; ++case_index) {
		laser_at_craft();
		g_test_objects[1].object_type = (uint8_t)types[case_index];
		g_mission_file_version = 14;
		collide_damagecraft(1, -1, TEST_SHOT, 0);
		XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0],
				  1000 - 250 / 4);
		laser_at_craft();
		g_test_objects[1].object_type = (uint8_t)types[case_index];
		g_mission_file_version = 13;
		collide_damagecraft(1, -1, TEST_SHOT, 0);
		XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 250);
	}
	g_mission_file_version = 0;
}

/* ------------------------------------------------------------------------ */
/* collide_damagecraft: shields, hull and the systems that fail. */

/* Every subsystem of a fighter installed and working: all the bits of
 * CRAFT_SUBSYSTEM_FLAGS_ALL. */
static void all_systems_work(struct craft_data *craft)
{
	craft->system_flags = CRAFT_SUBSYSTEM_FLAGS_ALL;
	craft->working_subsystems = CRAFT_SUBSYSTEM_FLAGS_ALL;
}

/* Comment: the shield on the hit side takes what it can; what gets through
 * goes to the hull. A hit exactly the shield's size gets nothing through. */
static void check_damage_hull_through_shield(void)
{
	laser_at_craft();
	g_test_craft[1].hull_max = 1000;
	g_test_craft[1].system_damage_hull_threshold = 1000;
	g_test_craft[1].shield_energy[0] = 100;
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, TEST_SHOT, 0), 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 0);
	/* A computer-flown craft's shields are not evened. */
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[1], 500);
	XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 150);
	XVT_ASSERT_TRUE(
		line_holds("combat.hull_hit", "damage=150 hull=150 max=1000"));
	XVT_ASSERT_TRUE(line_holds("combat.damage_taken", "through=150"));

	/* Each hit adds to the hull. */
	g_test_craft[1].shield_energy[0] = 0;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 150 + 250);

	/* The shield exactly the damage's size: emptied, nothing through. */
	laser_at_craft();
	g_test_craft[1].hull_max = 1000;
	g_test_craft[1].shield_energy[0] = 250;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 0);

	/* The rear shield takes a rear hit, the front a front hit. */
	laser_at_craft();
	g_test_craft[1].hull_max = 1000;
	g_test_craft[1].shield_energy[1] = 100;
	collide_damagecraft(1, -1, TEST_SHOT, 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[1], 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 150);
}

/* Comment: a player's craft evens its two shields after a hit. A hit the
 * shield takes whole, leaving it below the other, moves half the difference
 * across; a hit that empties it moves half the other's energy over. Both mark
 * the shield-hit flash and set even distribution. */
static void check_damage_player_shields_even(void)
{
	/* Front 300, rear 900; 100 off the front leaves 200: half of the 700
	 * between them moves across. */
	laser_at_craft();
	player_flies(1, 1);
	g_test_mobiles[TEST_SHOT].damage_amount = 100;
	g_test_craft[1].shield_energy[0] = 300;
	g_test_craft[1].shield_energy[1] = 900;
	g_test_craft[1].shield_distrib_mode = SHIELD_DISTRIBUTION_FULLY_FORWARD;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 550);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[1], 550);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_distrib_mode,
			  SHIELD_DISTRIBUTION_EVEN);
	XVT_ASSERT_INT_EQ(
		g_player_flight_transient_timers[1].shield_hit_flash_timer, 59);
	XVT_ASSERT_INT_EQ(g_last_shield_damage_side, 0);

	/* A rear hit that leaves the rear above the front moves nothing, and
	 * leaves the distribution alone; the flash names the rear. */
	laser_at_craft();
	player_flies(1, 1);
	g_test_mobiles[TEST_SHOT].damage_amount = 100;
	g_test_craft[1].shield_energy[0] = 500;
	g_test_craft[1].shield_energy[1] = 900;
	g_test_craft[1].shield_distrib_mode = SHIELD_DISTRIBUTION_FULLY_FORWARD;
	collide_damagecraft(1, -1, TEST_SHOT, 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 500);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[1], 800);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_distrib_mode,
			  SHIELD_DISTRIBUTION_FULLY_FORWARD);
	XVT_ASSERT_INT_EQ(g_last_shield_damage_side, 1);

	/* A hit that empties the front shield: the rear gives half its energy
	 * to the front; the part through goes to the hull. */
	laser_at_craft();
	player_flies(1, 1);
	g_test_craft[1].hull_max = 1000;
	g_test_craft[1].system_damage_hull_threshold = 1000;
	g_test_craft[1].shield_energy[0] = 100;
	g_test_craft[1].shield_distrib_mode = SHIELD_DISTRIBUTION_FULLY_FORWARD;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 250);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[1], 250);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_distrib_mode,
			  SHIELD_DISTRIBUTION_EVEN);
	XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 150);
	/* A computer-flown craft keeps its shields and distribution, and has
	 * no flash. */
	laser_at_craft();
	g_test_craft[1].shield_energy[0] = 300;
	g_test_craft[1].shield_energy[1] = 900;
	g_test_craft[1].shield_distrib_mode = SHIELD_DISTRIBUTION_FULLY_FORWARD;
	g_test_mobiles[TEST_SHOT].damage_amount = 100;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 200);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[1], 900);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_distrib_mode,
			  SHIELD_DISTRIBUTION_FULLY_FORWARD);
}

/* Comment: hull damage on a player's craft marks the hull-hit flash and, one
 * time in 4, knocks out a random system; the return is 0 when a hull sound
 * played, which the local player's craft hears. With the craft's own seed at
 * 0 every draw is 0, so the first of the random choices (the engines) goes;
 * from 0x7FFF none. */
static void check_damage_hull_hit_on_player(void)
{
	laser_at_craft();
	player_flies(1, 1);
	g_test_craft[1].hull_max = 1000;
	g_test_craft[1].system_damage_hull_threshold = 1000;
	g_test_craft[1].shield_energy[1] = 100;
	g_test_craft[1].ai_controller.saved_rand_seed = 0;
	all_systems_work(&g_test_craft[1]);
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, TEST_SHOT, 1), 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 150);
	XVT_ASSERT_INT_EQ(
		g_player_flight_transient_timers[1].hull_hit_flash_timer, 59);
	XVT_ASSERT_INT_EQ(g_test_craft[1].working_subsystems,
			  CRAFT_SUBSYSTEM_FLAGS_ALL ^
				  g_subsystem_id_to_flag[0]);
	XVT_ASSERT_INT_EQ(g_test_craft[1].system_health[0], 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].system_repair_seconds[0],
			  g_subsystem_repair_duration[0]);
	XVT_ASSERT_TRUE(line_holds("combat.system_failed", "system=0"));
	XVT_ASSERT_TRUE(line_holds("combat.system_failed", "cause=\"hull\""));

	/* A second hit adds to the flash. */
	g_test_craft[1].shield_energy[1] = 0;
	g_test_craft[1].ai_controller.saved_rand_seed = 0x7FFF;
	collide_damagecraft(1, -1, TEST_SHOT, 1);
	XVT_ASSERT_INT_EQ(
		g_player_flight_transient_timers[1].hull_hit_flash_timer, 118);
	/* The seed 0x7FFF is past the 1 in 4: no second failure. */
	XVT_ASSERT_INT_EQ(g_test_craft[1].working_subsystems,
			  CRAFT_SUBSYSTEM_FLAGS_ALL ^
				  g_subsystem_id_to_flag[0]);

	/* The local player's craft hears the hull, one of two sounds: return
	 * 0. */
	for (int sound = 0; sound < 2; ++sound) {
		laser_at_craft();
		player_flies(1, 0);
		g_test_craft[1].hull_max = 1000;
		g_test_craft[1].system_damage_hull_threshold = 1000;
		g_test_craft[1].shield_energy[1] = 0;
		g_test_craft[1].ai_controller.saved_rand_seed = 0x7FFF;
		g_game_rand2_feedback_state = sound == 0 ? 0 : 0x8000;
		XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, TEST_SHOT, 1), 0);
		XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 250);
	}
}

/* Comment: once hull_damage has reached system_damage_hull_threshold, a hit
 * can knock out HUD features: one picked by the craft's seed from
 * g_subsystem_failure_hud_mask_by_random_slot, only if the craft has it
 * installed. A computer-flown craft's first draw is the seed itself, so seeds
 * 0 to 15 name the slots in turn. */
static void check_damage_hud_feature_loss(void)
{
	for (int slot = 0; slot < 16; ++slot) {
		uint16_t lost =
			g_subsystem_failure_hud_mask_by_random_slot[slot];

		laser_at_craft();
		g_test_craft[1].hull_max = 1000;
		g_test_craft[1].system_damage_hull_threshold = 0;
		g_test_craft[1].shield_energy[0] = 0;
		g_test_craft[1].ai_controller.saved_rand_seed = (uint16_t)slot;
		g_test_craft[1].damage_stats.installed_hud_feature_mask =
			0xFFFF;
		g_test_craft[1].damage_stats.active_hud_feature_mask = 0xFFFF;
		collide_damagecraft(1, -1, TEST_SHOT, 0);
		XVT_ASSERT_INT_EQ(
			g_test_craft[1].damage_stats.active_hud_feature_mask,
			0xFFFF & ~lost);
		XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 250);

		/* A feature the craft does not have is not lost. */
		laser_at_craft();
		g_test_craft[1].hull_max = 1000;
		g_test_craft[1].system_damage_hull_threshold = 0;
		g_test_craft[1].shield_energy[0] = 0;
		g_test_craft[1].ai_controller.saved_rand_seed = (uint16_t)slot;
		g_test_craft[1].damage_stats.installed_hud_feature_mask =
			0xFFFF & ~lost;
		g_test_craft[1].damage_stats.active_hud_feature_mask =
			0xFFFF & ~lost;
		collide_damagecraft(1, -1, TEST_SHOT, 0);
		XVT_ASSERT_INT_EQ(
			g_test_craft[1].damage_stats.active_hud_feature_mask,
			0xFFFF & ~lost);
	}

	/* Below the threshold no feature goes. */
	laser_at_craft();
	g_test_craft[1].hull_max = 1000;
	g_test_craft[1].system_damage_hull_threshold = 500;
	g_test_craft[1].shield_energy[0] = 0;
	g_test_craft[1].ai_controller.saved_rand_seed = 1;
	g_test_craft[1].damage_stats.installed_hud_feature_mask = 0xFFFF;
	g_test_craft[1].damage_stats.active_hud_feature_mask = 0xFFFF;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].damage_stats.active_hud_feature_mask,
			  0xFFFF);
	/* The second hit reaches it. */
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 500);
	XVT_ASSERT_TRUE(g_test_craft[1].damage_stats.active_hud_feature_mask !=
			0xFFFF);
}

/* Player 1 flies craft 1, of object_type, whose hull and shields give no
 * cover; the craft's seed is seed. Returns the HUD features the hit knocked
 * out, and the value collide_damagecraft returned in *result. */
static int hud_lost_by_player_craft(int object_type, int seed, int16_t *result)
{
	laser_at_craft();
	player_flies(1, 1);
	g_test_objects[1].object_type = (uint8_t)object_type;
	g_test_craft[1].hull_max = 100000;
	g_test_craft[1].system_damage_hull_threshold = 0;
	g_test_craft[1].shield_energy[1] = 0;
	g_test_craft[1].ai_controller.saved_rand_seed = (uint16_t)seed;
	g_test_craft[1].damage_stats.installed_hud_feature_mask = 0xFFFF;
	g_test_craft[1].damage_stats.active_hud_feature_mask = 0xFFFF;
	/* A rear hit, so no draw is made for the front's sound. */
	*result = collide_damagecraft(1, -1, TEST_SHOT, 1);
	return 0xFFFF & ~g_test_craft[1].damage_stats.active_hud_feature_mask;
}

/* Comment: a feature lost on a player's craft plays the internal-hit sound,
 * so the return is 0. In a starfighter of object types 1 to 4 or 14, a lost
 * display brings its related ones with it (log field lost): 0x2, 0x4 and 0x8
 * all of 0xE; 0x80 and 0x100 of 0x180; 0x200 to 0x1000 of 0xE00. Other
 * craft lose the one the table names. The same seed names the same table
 * slot for both craft. */
static void check_damage_hud_loss_player_craft(void)
{
	static const int widened[][2] = {
		{0x2, 0xE},	{0x4, 0xE},	{0x8, 0xE},
		{0x80, 0x180},	{0x100, 0x180}, {0x200, 0xE00},
		{0x400, 0xE00}, {0x800, 0xE00}, {0x1000, 0xE00},
	};
	int widened_count = 0;
	int plain_count = 0;
	for (int seed = 0; seed < 1024; ++seed) {
		int16_t other_result;
		int16_t fighter_result;
		int other = hud_lost_by_player_craft(6, seed, &other_result);
		int fighter =
			hud_lost_by_player_craft(1, seed, &fighter_result);
		int expected = other;
		for (size_t i = 0; i < sizeof widened / sizeof widened[0];
		     ++i) {
			if (widened[i][0] == other) {
				expected = widened[i][1];
			}
		}
		XVT_ASSERT_INT_EQ(fighter, expected);
		if (other == 0) {
			XVT_ASSERT_INT_EQ(other_result, 1);
			XVT_ASSERT_INT_EQ(fighter_result, 1);
			continue;
		}
		XVT_ASSERT_INT_EQ(other_result, 0);
		XVT_ASSERT_INT_EQ(fighter_result, 0);
		if (fighter != other) {
			++widened_count;
		} else {
			++plain_count;
		}
	}
	XVT_ASSERT_TRUE(widened_count > 0);
	XVT_ASSERT_TRUE(plain_count > 0);
}

/* ------------------------------------------------------------------------ */
/* collide_damagecraft: ion hits. */

enum {
	TEST_ION_STRENGTH = 100,
};

/* An ion shot of type, damage_amount damage, from craft 0 onto craft 1,
 * whose shields are down, whose model's system_strength is TEST_ION_STRENGTH
 * and whose subsystems all work; no hull damage can end it. */
static void ion_at_craft(uint8_t type, int damage_amount)
{
	laser_at_craft();
	g_test_objects[TEST_SHOT].object_type = type;
	g_test_mobiles[TEST_SHOT].damage_amount = (uint16_t)damage_amount;
	g_test_craft[1].model_index = TEST_MODEL;
	g_model_defs[TEST_MODEL].system_strength = TEST_ION_STRENGTH;
	g_test_craft[1].hull_max = 100000;
	g_test_craft[1].shield_energy[0] = 0;
	g_test_craft[1].shield_energy[1] = 0;
	all_systems_work(&g_test_craft[1]);
}

/* Comment: an ion shot adds 1, 2 or 4 to subsystem_damage (while under
 * 1,000) instead of hurting the hull. */
static void check_damage_ion_counts_up(void)
{
	static const struct {
		uint8_t type;
		int step;
	} kinds[] = {
		{PROJECTILE_OBJECT_TYPE_ION_LASER, 1},
		{PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER, 2},
		{PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER_2, 4},
	};
	for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; ++i) {
		ion_at_craft(kinds[i].type, 250);
		g_model_defs[TEST_MODEL].system_strength = 5000;
		g_test_craft[1].subsystem_damage = 20;
		XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, TEST_SHOT, 0), 1);
		XVT_ASSERT_INT_EQ(g_test_craft[1].subsystem_damage,
				  20 + kinds[i].step);
		XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 0);
		XVT_ASSERT_INT_EQ(g_test_craft[1].working_subsystems,
				  CRAFT_SUBSYSTEM_FLAGS_ALL);
		XVT_ASSERT_TRUE(line_holds("combat.ion_hit", "ion="));

		/* Not once it has reached 1,000. */
		ion_at_craft(kinds[i].type, 250);
		g_model_defs[TEST_MODEL].system_strength = 5000;
		g_test_craft[1].subsystem_damage = 1000;
		collide_damagecraft(1, -1, TEST_SHOT, 0);
		XVT_ASSERT_INT_EQ(g_test_craft[1].subsystem_damage, 1000);
		g_test_craft[1].subsystem_damage = 999;
		collide_damagecraft(1, -1, TEST_SHOT, 0);
		XVT_ASSERT_INT_EQ(g_test_craft[1].subsystem_damage,
				  999 + kinds[i].step);
	}

	/* On the local player's craft the hit is heard and the indicators
	 * redrawn; the count goes up the same. */
	ion_at_craft(PROJECTILE_OBJECT_TYPE_ION_LASER, 250);
	g_model_defs[TEST_MODEL].system_strength = 5000;
	player_flies(1, 0);
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].subsystem_damage, 1);

	/* An ion shot that the shield takes whole counts for nothing. */
	ion_at_craft(PROJECTILE_OBJECT_TYPE_ION_LASER, 250);
	g_test_craft[1].shield_energy[0] = 1000;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].subsystem_damage, 0);
}

/* Comment: once the model's system_strength is within 10 of the ion damage,
 * one working system goes per 200 damage (rounded up) in the order of the
 * subsystem table; on a player's craft each is also marked failed with its
 * repair time. The damage that counts is what got through the shield. */
static void check_damage_ion_knocks_out_systems(void)
{
	/* Remaining strength 11: nothing yet. */
	ion_at_craft(PROJECTILE_OBJECT_TYPE_ION_LASER, 250);
	g_test_craft[1].subsystem_damage = TEST_ION_STRENGTH - 12;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].working_subsystems,
			  CRAFT_SUBSYSTEM_FLAGS_ALL);

	/* Remaining strength 10: one system per 200 through; 250 through shield
	 * 100 is 150 through: one. */
	ion_at_craft(PROJECTILE_OBJECT_TYPE_ION_LASER, 250);
	g_test_craft[1].subsystem_damage = TEST_ION_STRENGTH - 11;
	g_test_craft[1].shield_energy[0] = 100;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].working_subsystems,
			  CRAFT_SUBSYSTEM_FLAGS_ALL ^
				  g_subsystem_id_to_flag[0]);
	/* A computer-flown craft's repair records are left alone. */
	XVT_ASSERT_INT_EQ(g_test_craft[1].system_repair_seconds[0], 0);

	/* 450 through: three (201 to 400 would be two, 401 to 600 three). */
	ion_at_craft(PROJECTILE_OBJECT_TYPE_ION_LASER, 550);
	g_test_craft[1].subsystem_damage = TEST_ION_STRENGTH - 11;
	g_test_craft[1].shield_energy[0] = 100;
	player_flies(1, 1);
	for (int system = 0; system < 3; ++system) {
		g_test_craft[1].system_health[system] = 55;
	}
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].working_subsystems,
			  CRAFT_SUBSYSTEM_FLAGS_ALL ^
				  (g_subsystem_id_to_flag[0] |
				   g_subsystem_id_to_flag[1] |
				   g_subsystem_id_to_flag[2]));
	for (int system = 0; system < 3; ++system) {
		XVT_ASSERT_INT_EQ(g_test_craft[1].system_health[system], 0);
		XVT_ASSERT_INT_EQ(g_test_craft[1].system_repair_seconds[system],
				  g_subsystem_repair_duration[system]);
	}
	XVT_ASSERT_INT_EQ(g_test_craft[1].system_health[3], 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].system_repair_seconds[3], 0);
	XVT_ASSERT_TRUE(line_holds("combat.ion_hit", "remaining="));

	/* 200 through is one; 201 two. */
	for (int through = 200; through <= 201; ++through) {
		ion_at_craft(PROJECTILE_OBJECT_TYPE_ION_LASER, through);
		g_test_craft[1].subsystem_damage = TEST_ION_STRENGTH - 11;
		collide_damagecraft(1, -1, TEST_SHOT, 0);
		int working = g_test_craft[1].working_subsystems;
		int knocked = 0;
		for (int system = 0; system < CRAFT_SUBSYSTEM_COUNT; ++system) {
			knocked +=
				(working & g_subsystem_id_to_flag[system]) == 0;
		}
		XVT_ASSERT_INT_EQ(knocked, through == 200 ? 1 : 2);
	}
}

/* Comment: a craft left with no working system is disabled: both shields
 * 0, subsystem_damage at the model's system_strength, a disabled outcome
 * for its flight group; a computer-flown craft's turrets lose their
 * targets and its cannon groups their links. */
static void check_damage_ion_disables_craft(void)
{
	ion_at_craft(PROJECTILE_OBJECT_TYPE_ION_LASER, 2001);
	g_test_craft[1].subsystem_damage = TEST_ION_STRENGTH - 11;
	g_test_craft[1].laser_slot_count = 2;
	g_test_craft[1].cannon_group_count = 2;
	g_test_craft[1].weapon_slots[0].projectile_type_id = 2;
	g_test_craft[1].weapon_slots[1].projectile_type_id = 1;
	g_test_craft[1].turret_target_states[0].target_obj_idx = 0;
	g_test_craft[1].turret_target_states[1].target_obj_idx = 0;
	g_test_craft[1].laser_state.link_mode[0] = 1;
	g_test_craft[1].laser_state.link_mode[1] = 1;
	g_mission_flight_groups[1].fg.special_cargo_craft =
		g_test_craft[1].craft_ordinal;
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, TEST_SHOT, 0), 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].working_subsystems, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].subsystem_damage, TEST_ION_STRENGTH);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[1], 0);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_DISABLED],
			  1);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[1]
			.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_DISABLED],
		1);
	XVT_ASSERT_INT_EQ(
		g_test_craft[1].turret_target_states[0].target_obj_idx,
		UINT16_MAX);
	XVT_ASSERT_INT_EQ(
		g_test_craft[1].turret_target_states[1].target_obj_idx, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].laser_state.link_mode[0], 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].laser_state.link_mode[1], 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 0);
	XVT_ASSERT_TRUE(line_holds("combat.craft_disabled", "object=1 fg=1"));
	XVT_ASSERT_TRUE(line_holds("combat.craft_disabled", "count=1"));

	/* A player's craft with the strength used up loses every system with
	 * its repair marks, and is disabled; it keeps its turret targets. */
	ion_at_craft(PROJECTILE_OBJECT_TYPE_ION_LASER, 30);
	g_test_craft[1].subsystem_damage = TEST_ION_STRENGTH - 1;
	player_flies(1, 1);
	g_test_craft[1].laser_slot_count = 1;
	g_test_craft[1].weapon_slots[0].projectile_type_id = 2;
	g_test_craft[1].turret_target_states[0].target_obj_idx = 0;
	for (int system = 0; system < CRAFT_SUBSYSTEM_COUNT; ++system) {
		g_test_craft[1].system_health[system] = 40;
	}
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].working_subsystems, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].subsystem_damage, TEST_ION_STRENGTH);
	for (int system = 0; system < CRAFT_SUBSYSTEM_COUNT; ++system) {
		XVT_ASSERT_INT_EQ(g_test_craft[1].system_health[system], 0);
		XVT_ASSERT_INT_EQ(g_test_craft[1].system_repair_seconds[system],
				  g_subsystem_repair_duration[system]);
	}
	XVT_ASSERT_INT_EQ(
		g_test_craft[1].turret_target_states[0].target_obj_idx, 0);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_DISABLED],
			  1);

	/* A craft with no working system to begin with is not counted
	 * disabled again... it was already: no second outcome from a hit that
	 * finds nothing to knock out. */
	ion_at_craft(PROJECTILE_OBJECT_TYPE_ION_LASER, 30);
	g_test_craft[1].subsystem_damage = TEST_ION_STRENGTH;
	g_test_craft[1].working_subsystems = 0;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_DISABLED],
			  0);
}

/* ------------------------------------------------------------------------ */
/* collide_damagecraft: the kill. */

enum {
	TEST_TUMBLE_LIMIT = 1000,
};

/* Craft 0 (group 0, AI level 2) shoots craft 1 (group 1), whose shields are
 * down and whose hull has 100 to give; the shot deals 250. Craft 1's model
 * limits its tumble to TEST_TUMBLE_LIMIT. */
static void killing_shot(void)
{
	laser_at_craft();
	g_test_craft[1].shield_energy[0] = 0;
	g_test_craft[1].shield_energy[1] = 0;
	g_test_craft[1].model_index = TEST_MODEL;
	g_model_defs[TEST_MODEL].max_tumble_rate = TEST_TUMBLE_LIMIT;
	g_test_mobiles[1].speed = 50;
}

/* The same as killing_shot, with player 0 flying craft 0, a team hostile to
 * craft 1's, and mission scoring set up as for a kill credit. */
static void player_kills_craft(void)
{
	killing_shot();
	g_mission_teams[0].allies[0] = 1;
	g_mission_teams[1].allies[1] = 1;
	g_mission_teams[0].allies[1] = 0;
	g_mission_teams[1].allies[0] = 0;
	g_flight_mission_state.difficulty = 1;
	player_flies(0, 0);
	g_players[0].pilot_rating = 5;
	/* The kill's rating points divide by the weight of the model of the
	 * craft type that player 0's flight group flies: a craft type whose
	 * object type is TEST_FIGHTER_TYPE, which has the model of the
	 * victim. */
	int craft_type = 1;
	while (craft_type < 96 &&
	       g_craft_type_to_object_type[craft_type] != TEST_FIGHTER_TYPE) {
		++craft_type;
	}
	XVT_ASSERT_TRUE(craft_type < 96);
	g_mission_flight_groups[0].fg.craft_type = (craft_species)craft_type;
	g_model_defs[TEST_MODEL].rating_weight = 5;
	memset(&g_players[0].mission_stats, 0,
	       sizeof g_players[0].mission_stats);
	g_test_guidance[0].source_player_idx = 0;
}

/* Comment: when hull_damage reaches hull_max on an active craft, the kill
 * is credited, the loss recorded, a destroyed outcome counted, the links to
 * the craft cleared and messages sent. */
static void check_kill_records(void)
{
	player_kills_craft();
	g_test_craft[0].carried_object_index = 1;
	g_test_craft[0].carrier_obj_idx = 1;
	g_test_craft[1].carried_object_index = 0;
	XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
			  CRAFT_OBJECT_KIND_ACTIVE);
	collide_damagecraft(1, -1, TEST_SHOT, 0);

	XVT_ASSERT_TRUE(g_test_craft[1].hull_damage >=
			g_test_craft[1].hull_max);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[1]
			.outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED],
		1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].mission_accounting_done, 1);
	/* The player whose shot did all the damage gets the kill's score. */
	XVT_ASSERT_INT_EQ(g_players[0].mission_stats.mission_score,
			  mission_compute_kill_score_for_object(1));
	XVT_ASSERT_TRUE(g_players[0].mission_stats.mission_score > 0);
	/* The loss is counted against the victim's team, as a loss. */
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime.team_kill_stats[3][1],
			  1);
	/* The craft it carried, and the carrier it rode, forget it. */
	XVT_ASSERT_INT_EQ(g_test_craft[0].carried_object_index, UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_test_craft[0].carrier_obj_idx, UINT16_MAX);
	XVT_ASSERT_TRUE(count_lines("msg.message_built") >= 1);
	XVT_ASSERT_TRUE(line_holds("combat.craft_destroyed", "object=1 fg=1"));
	XVT_ASSERT_TRUE(
		line_holds("battle.killing_blow",
			   "victim=1 by=0 attacker=0 weapon=\"cannon\""));
}

/* The killing blow's log line names what struck: a shot from a cannon or a
 * warhead, a collision, a static object, the engine wash or nothing. */
static void check_kill_weapon_names(void)
{
	static const struct {
		const char *weapon;
		uint16_t source;
		uint8_t shot_type;
	} cases[] = {
		{"cannon", TEST_SHOT, PROJECTILE_OBJECT_TYPE_REBEL_LASER},
		{"warhead", TEST_SHOT, WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE},
		{"collision", 0, 0},
		{"object", TEST_MINE, 0},
		{"wash", UINT16_MAX - 1, 0},
		{"none", UINT16_MAX, 0},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		killing_shot();
		g_test_mobiles[0].damage_amount = 250;
		place_mine(20000, 0, 0);
		g_object_type_table[TEST_MINE_TYPE].max_bounds_extent = 200;
		if (cases[i].shot_type != 0) {
			g_test_objects[TEST_SHOT].object_type =
				cases[i].shot_type;
		}
		collide_damagecraft(1, -1, cases[i].source,
				    cases[i].source == UINT16_MAX - 1 ? 300
								      : 0);
		char expected[32];
		snprintf(expected, sizeof expected, "weapon=\"%s\"",
			 cases[i].weapon);
		XVT_ASSERT_TRUE(line_holds("battle.killing_blow", expected));
		XVT_ASSERT_INT_EQ(
			g_mission_fg_stats[1]
				.outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED],
			1);
	}
	g_object_type_table[TEST_MINE_TYPE].max_bounds_extent =
		TEST_MINE_EXTENT;
}

/* Comment: only a craft that is active or entering hyperspace is killed; one
 * in another state takes the damage and is not counted lost. */
static void check_kill_needs_live_craft(void)
{
	killing_shot();
	g_test_craft[1].object_kind = CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[1]
			.outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED],
		1);
	XVT_ASSERT_TRUE(line_holds("combat.craft_destroyed", "hyperspace=1"));

	static const uint8_t others[] = {
		CRAFT_OBJECT_KIND_DISABLED,
		CRAFT_OBJECT_KIND_BREAKING_UP,
		CRAFT_OBJECT_KIND_EXPLODING,
		CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE,
	};
	for (size_t i = 0; i < sizeof others; ++i) {
		killing_shot();
		g_test_craft[1].object_kind = others[i];
		collide_damagecraft(1, -1, TEST_SHOT, 0);
		XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 250);
		XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind, others[i]);
		XVT_ASSERT_INT_EQ(
			g_mission_fg_stats[1]
				.outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED],
			0);
		XVT_ASSERT_INT_EQ(g_test_craft[1].mission_accounting_done, 0);
	}
}

/* Comment: a craft without a fuselage tumbles and breaks up after a time:
 * object_kind breaking up, a roll within the model's tumble limit in either
 * direction, and a life of 8 to 15 seconds, or by the flight group's craft
 * explosion time when it has one. */
static void check_kill_tumbles_without_fuselage(void)
{
	int turned[2] = {0, 0};
	for (int seed = 0; seed < 64; ++seed) {
		killing_shot();
		g_test_craft[1].ai_controller.saved_rand_seed = (uint16_t)seed;
		XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, TEST_SHOT, 0), 1);
		XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
				  CRAFT_OBJECT_KIND_BREAKING_UP);
		int roll = g_test_mobiles[1].roll_impulse_rate;
		XVT_ASSERT_TRUE(roll != 0);
		XVT_ASSERT_TRUE(roll >= -TEST_TUMBLE_LIMIT &&
				roll <= TEST_TUMBLE_LIMIT);
		++turned[roll < 0];
		int life = g_test_mobiles[1].lifetime_timer;
		XVT_ASSERT_INT_EQ(life % SIMULATION_TICKS_PER_SECOND, 0);
		XVT_ASSERT_TRUE(life >= 8 * SIMULATION_TICKS_PER_SECOND &&
				life <= 15 * SIMULATION_TICKS_PER_SECOND);
		XVT_ASSERT_TRUE(line_holds("combat.craft_breakup", "state=3"));
	}
	XVT_ASSERT_TRUE(turned[0] > 0);
	XVT_ASSERT_TRUE(turned[1] > 0);

	/* The flight group's craft explosion time, in 5-second steps. */
	killing_shot();
	g_mission_flight_groups[1].fg.craft_explosion_time = 3;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_mobiles[1].lifetime_timer, 1180 * 3 - 1179);
	XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
			  CRAFT_OBJECT_KIND_BREAKING_UP);
}

/* ------------------------------------------------------------------------ */
/* collide_damagecraft: how a destroyed craft ends. */

/* Gives the starfighter object type a model of count meshes, mesh i of
 * component type types[i], with center x center_x[i] and the component
 * flags flags[i]. */
static void craft_model(int count, const int *types, const int *center_x,
			const int *flags)
{
	struct optimized_poly_object *model =
		memory_get_handle_block(g_craft_model_handle);
	memset(model, 0, sizeof *model);
	model->self_marker = model;
	model->root_node_count = count;
	model->root_nodes = g_craft_model_roots;
	if (g_craft_model_type < 73) {
		g_object_type_mesh_cache[g_craft_model_type].mesh_count = count;
	}
	for (int i = 0; i < count; ++i) {
		memset(&g_craft_model_nodes[i], 0,
		       sizeof g_craft_model_nodes[i]);
		memset(&g_craft_model_descriptors[i], 0,
		       sizeof g_craft_model_descriptors[i]);
		g_craft_model_nodes[i].node_type = OPT_MESHDESC;
		g_craft_model_nodes[i].payload = &g_craft_model_descriptors[i];
		g_craft_model_roots[i] = &g_craft_model_nodes[i];
		g_craft_model_descriptors[i].mesh_type = types[i];
		g_craft_model_descriptors[i].component_flags = flags[i];
		g_craft_model_descriptors[i].center.x = (float)center_x[i];
		if (g_craft_model_type < 73) {
			g_object_type_mesh_cache[g_craft_model_type]
				.mesh_types[i] = types[i];
			g_object_type_mesh_cache[g_craft_model_type]
				.mesh_descriptors[i] =
				&g_craft_model_descriptors[i];
		}
	}
	g_loaded_models[g_craft_model_type] = g_craft_model_handle;
	g_object_type_table[g_craft_model_type].asset_flags |= 1;
}

/* A fuselage and a wing on each side: mesh 0 the fuselage, mesh 1 the wing at
 * x -100, mesh 2 the wing at x 100. */
static void winged_craft_model(void)
{
	static const int types[3] = {MESH_COMPONENT_03_FUSELAGE,
				     MESH_COMPONENT_02_WING,
				     MESH_COMPONENT_02_WING};
	static const int center_x[3] = {0, -100, 100};
	static const int flags[3] = {0, 0, 0};
	craft_model(3, types, center_x, flags);
}

/* A fuselage alone. */
static void fuselage_craft_model(void)
{
	static const int types[1] = {MESH_COMPONENT_03_FUSELAGE};
	static const int center_x[1] = {0};
	static const int flags[1] = {0};
	craft_model(1, types, center_x, flags);
}

/* Comment: a moving craft hit by something small breaks up, tearing off a
 * wing when one is left, on a randomly chosen side: the wing becomes a piece
 * of debris, the craft is marked breaking up, and the craft's life is
 * a whole number of seconds. */
static void check_kill_tears_off_wing(void)
{
	/* A craft of an object type with a mesh cache, and one past them. */
	for (int type_index = 0; type_index < 2; ++type_index) {
		int type = type_index == 0 ? TEST_FIGHTER_TYPE : TEST_LATE_TYPE;
		int torn[3] = {0, 0, 0};
		for (int seed = 0; seed < 64; ++seed) {
			killing_shot();
			g_craft_model_type = type;
			g_test_objects[1].object_type = (uint8_t)type;
			winged_craft_model();
			/* Either of the two breakup sounds. */
			g_game_rand2_feedback_state =
				(seed & 1) != 0 ? 0x8000 : 0;
			g_test_craft[1].ai_controller.saved_rand_seed =
				(uint16_t)seed;
			XVT_ASSERT_INT_EQ(
				collide_damagecraft(1, -1, TEST_SHOT, 0), 0);
			XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
					  CRAFT_OBJECT_KIND_BREAKING_UP);
			int left = g_test_craft[1].component_state[WING_LEFT];
			int right = g_test_craft[1].component_state[WING_RIGHT];
			/* Exactly one wing is torn off. */
			XVT_ASSERT_TRUE((left == TORN_OFF_STATE) !=
					(right == TORN_OFF_STATE));
			XVT_ASSERT_INT_EQ(left + right, TORN_OFF_STATE);
			++torn[left == TORN_OFF_STATE ? WING_LEFT : WING_RIGHT];
			int wing =
				left == TORN_OFF_STATE ? WING_LEFT : WING_RIGHT;
			/* The step after the last mesh marks the breakup. */
			XVT_ASSERT_INT_EQ(g_test_craft[1].component_state[3],
					  BREAKUP_STEP_STATE);
			/* The debris slot holds the wing: its mesh twice, drawn
			 * from the breakup step. */
			XVT_ASSERT_INT_EQ(
				g_test_objects[TEST_DEBRIS].object_type,
				CRAFT_SPECIES_COMPONENT);
			XVT_ASSERT_INT_EQ(g_test_objects[TEST_DEBRIS]
						  .type_specific_byte[0],
					  2 * wing);
			XVT_ASSERT_INT_EQ(g_test_objects[TEST_DEBRIS]
						  .type_specific_byte[1],
					  BREAKUP_STEP_STATE);
			int life = g_test_mobiles[1].lifetime_timer;
			XVT_ASSERT_INT_EQ(life % SIMULATION_TICKS_PER_SECOND,
					  0);
			XVT_ASSERT_TRUE(
				life >= SIMULATION_TICKS_PER_SECOND &&
				life <= 11 * SIMULATION_TICKS_PER_SECOND);
			XVT_ASSERT_TRUE(
				g_test_debris_mobile.roll_impulse_rate != 0);
		}
		XVT_ASSERT_TRUE(torn[WING_LEFT] > 0);
		XVT_ASSERT_TRUE(torn[WING_RIGHT] > 0);
	}
}

/* Comment: a wing that is not left, or a debris slot that is not free, means
 * no wing is torn off; the craft still breaks up. A craft with a fuselage
 * alone has no wing to lose. */
static void check_kill_without_a_wing_to_tear(void)
{
	/* Both wings already gone. */
	killing_shot();
	winged_craft_model();
	g_test_craft[1].component_state[WING_LEFT] = TORN_OFF_STATE;
	g_test_craft[1].component_state[WING_RIGHT] = TORN_OFF_STATE;
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, TEST_SHOT, 0), 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
			  CRAFT_OBJECT_KIND_BREAKING_UP);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_DEBRIS].object_type, 0);

	/* No free debris slot. */
	killing_shot();
	winged_craft_model();
	g_test_objects[TEST_DEBRIS].object_type = 9;
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, TEST_SHOT, 0), 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
			  CRAFT_OBJECT_KIND_BREAKING_UP);
	XVT_ASSERT_INT_EQ(g_test_craft[1].component_state[WING_LEFT], 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].component_state[WING_RIGHT], 0);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_DEBRIS].object_type, 9);

	/* One mesh. */
	killing_shot();
	fuselage_craft_model();
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, TEST_SHOT, 0), 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
			  CRAFT_OBJECT_KIND_BREAKING_UP);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_DEBRIS].object_type, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].component_state[1],
			  BREAKUP_STEP_STATE);
}

/* Comment: a craft that is not moving, or struck by a large craft or by a
 * container, explodes at once: object_kind exploding, a life of 1 tick and
 * the explosion sound (return 0). */
static void check_kill_explodes_at_once(void)
{
	/* Not moving. */
	killing_shot();
	winged_craft_model();
	g_test_mobiles[1].speed = 0;
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, TEST_SHOT, 0), 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
			  CRAFT_OBJECT_KIND_EXPLODING);
	XVT_ASSERT_INT_EQ(g_test_mobiles[1].lifetime_timer, 1);
	XVT_ASSERT_TRUE(
		line_holds("combat.craft_breakup", "state=4 lifetime=1"));
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_DEBRIS].object_type, 0);

	/* Rammed by a large space craft: the source's type is wider than 1,095
	 * units. */
	killing_shot();
	winged_craft_model();
	g_test_mobiles[0].damage_amount = 250;
	g_object_type_table[TEST_FIGHTER_TYPE].max_bounds_extent = 1096;
	g_object_type_table[TEST_FIGHTER_TYPE].family_id =
		CRAFT_FAMILY_SPACE_CRAFT;
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, 0, 0), 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
			  CRAFT_OBJECT_KIND_EXPLODING);
	/* At 1,095 it breaks up. */
	killing_shot();
	winged_craft_model();
	g_test_mobiles[0].damage_amount = 250;
	g_object_type_table[TEST_FIGHTER_TYPE].max_bounds_extent = 1095;
	collide_damagecraft(1, -1, 0, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
			  CRAFT_OBJECT_KIND_BREAKING_UP);
	/* A large source that is not a space craft breaks it up too. */
	killing_shot();
	winged_craft_model();
	g_test_mobiles[0].damage_amount = 250;
	g_object_type_table[TEST_FIGHTER_TYPE].max_bounds_extent = 1096;
	g_object_type_table[TEST_FIGHTER_TYPE].family_id = CRAFT_FAMILY_DEBRIS;
	collide_damagecraft(1, -1, 0, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
			  CRAFT_OBJECT_KIND_BREAKING_UP);
	g_object_type_table[TEST_FIGHTER_TYPE].max_bounds_extent =
		TEST_FIGHTER_EXTENT;
	g_object_type_table[TEST_FIGHTER_TYPE].family_id = 0;

	/* Rammed by a container of class H, object type 58. */
	killing_shot();
	winged_craft_model();
	g_test_objects[0].object_type = 58;
	g_test_mobiles[0].damage_amount = 250;
	collide_damagecraft(1, -1, 0, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
			  CRAFT_OBJECT_KIND_EXPLODING);
}

/* Comment: for a player, one time in 4 the craft explodes at once instead of
 * breaking up; a breaking-up player's craft lasts 708 ticks. A computer-flown
 * craft never explodes at once for that reason. */
static void check_kill_of_player_craft(void)
{
	int exploded = 0;
	int broke_up = 0;
	for (int seed = 0; seed < 512; ++seed) {
		killing_shot();
		winged_craft_model();
		player_flies(1, 1);
		g_test_craft[1].ai_controller.saved_rand_seed = (uint16_t)seed;
		int16_t result = collide_damagecraft(1, -1, TEST_SHOT, 0);
		if (g_test_craft[1].object_kind ==
		    CRAFT_OBJECT_KIND_EXPLODING) {
			++exploded;
			XVT_ASSERT_INT_EQ(result, 0);
			XVT_ASSERT_INT_EQ(g_test_mobiles[1].lifetime_timer, 1);
		} else {
			++broke_up;
			XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
					  CRAFT_OBJECT_KIND_BREAKING_UP);
			XVT_ASSERT_INT_EQ(g_test_mobiles[1].lifetime_timer,
					  708);
		}
	}
	/* About one in 4. */
	XVT_ASSERT_TRUE(exploded * 8 > 512 && exploded * 2 < 512);
	XVT_ASSERT_INT_EQ(exploded + broke_up, 512);
}

/* ------------------------------------------------------------------------ */
/* collide_laserhitcraft: who hit, and what the hit does. */

enum {
	FORWARD_X = 0x7FFF, /* The Q15 one a craft facing along +X has. */
	TEST_CLOCK_SECOND = 3723,
};

/* The step's hit geometry for a shot moving along +X from 19,000 to 20,000
 * into craft 1: the hit point lies 600 units on. Craft 1 faces along +X. */
static void hit_geometry(void)
{
	g_collision_segment_start_world_x = 19000;
	g_collision_segment_start_world_y = 0;
	g_collision_segment_start_world_z = 0;
	g_collision_probe_world_x = 20000;
	g_collision_probe_world_y = 0;
	g_collision_probe_world_z = 0;
	g_collision_hit_offset_x = 600;
	g_collision_hit_offset_y = 0;
	g_collision_hit_offset_z = 0;
	g_test_mobiles[1].cached_fwd_x = FORWARD_X;
	g_test_mobiles[1].orient_matrix_dirty = 0;
	g_mission_elapsed_clock.hours = 1;
	g_mission_elapsed_clock.minutes = 2;
	g_mission_elapsed_clock.seconds = 3;
}

/* The same hit as laser_at_craft, with the geometry set; the hull is deep
 * enough to survive. */
static void laser_hits_craft(void)
{
	laser_at_craft();
	hit_geometry();
	g_test_craft[1].hull_max = 100000;
}

/* Comment: at tactical officer voice level 2 the first hit from a team
 * names the attacker when the craft is on the local player's team and not a
 * starfighter. The hit marks the craft and counts the outcome whatever the
 * speech. The shots are a missile, a torpedo, a rocket, a bomb, then a
 * cannon shot from a starfighter, a starship and a static source. */
static void check_hit_attacker_named(void)
{
	static const struct {
		uint8_t shot_type;
		int source;
		uint8_t source_genus;
	} cases[] = {
		{WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 0,
		 CRAFT_GENUS_STARFIGHTER},
		{WARHEAD_OBJECT_TYPE_ADVANCED_CONCUSSION_MISSILE, 0,
		 CRAFT_GENUS_STARFIGHTER},
		{WARHEAD_OBJECT_TYPE_PROTON_TORPEDO, 0,
		 CRAFT_GENUS_STARFIGHTER},
		{WARHEAD_OBJECT_TYPE_ADVANCED_PROTON_TORPEDO, 0,
		 CRAFT_GENUS_STARFIGHTER},
		{WARHEAD_OBJECT_TYPE_HEAVY_ROCKET, 0, CRAFT_GENUS_STARFIGHTER},
		{WARHEAD_OBJECT_TYPE_SPACE_BOMB, 0, CRAFT_GENUS_STARFIGHTER},
		{PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0,
		 CRAFT_GENUS_STARFIGHTER},
		{PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0, CRAFT_GENUS_STARSHIP},
		{PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0, CRAFT_GENUS_TRANSPORT},
		{PROJECTILE_OBJECT_TYPE_REBEL_LASER, TEST_MINE,
		 CRAFT_GENUS_MINE},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		laser_hits_craft();
		place_mine(20000, 0, 0);
		g_test_objects[TEST_SHOT].object_type = cases[i].shot_type;
		g_test_mobiles[TEST_SHOT].source_obj_idx =
			(uint16_t)cases[i].source;
		g_test_objects[cases[i].source].genus_id =
			cases[i].source_genus;
		g_test_objects[1].genus_id = CRAFT_GENUS_STARSHIP;
		g_game_config.voice_tactical_officer_level = 2;
		g_players[0].object_index = 1;
		g_players[0].team = 1;
		g_flight_mission_state.runtime.team_fg_designation_code[1][1] =
			1;
		g_mission_flight_groups[1].fg.special_cargo_craft =
			g_test_craft[1].craft_ordinal;
		collide_laserhitcraft(TEST_SHOT, 1, -1);
		/* The mine belongs to flight group 2, team 2. */
		XVT_ASSERT_INT_EQ(
			g_test_craft[1].attacked_by_team
					[cases[i].source == TEST_MINE ? 2 : 0] &
				1,
			1);
		XVT_ASSERT_INT_EQ(
			g_mission_fg_stats[1]
				.outcome_count[FLIGHT_GROUP_OUTCOME_ATTACKED],
			1);
		/* The group's special cargo craft is marked attacked. */
		XVT_ASSERT_INT_EQ(g_mission_fg_stats[1].special_cargo_outcome
					  [FLIGHT_GROUP_OUTCOME_ATTACKED],
				  1);
		XVT_ASSERT_TRUE(line_holds("combat.craft_attacked", "count=1"));
		XVT_ASSERT_TRUE(
			line_holds("combat.craft_attacked", "special=1"));
	}
	g_game_config.voice_tactical_officer_level = 0;

	/* Any other craft's group is not the special cargo. */
	laser_hits_craft();
	g_mission_flight_groups[1].fg.special_cargo_craft =
		(uint8_t)(g_test_craft[1].craft_ordinal + 1);
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[1]
			.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_ATTACKED],
		0);
	XVT_ASSERT_TRUE(line_holds("combat.craft_attacked", "special=0"));
}

/* Comment: a player's shot scores the attacked goal once per team: the 0x80
 * bit of attacked_by_team marks it done, and the outcome counts once per
 * team whoever shoots. A computer-flown craft's shot scores nothing. */
static void check_hit_attacked_goal_once(void)
{
	laser_hits_craft();
	player_flies(0, 2);
	g_test_craft[1].attacked_by_team[0] = 0;
	/* A bonus goal (kind 2, amount 18) for the attack pays the player 250
	 * times its points, and the team too. */
	struct flight_group_goal *goal =
		&g_mission_flight_groups[1].fg.goals[0];
	goal->enabled_teams[0] = 1;
	goal->event_condition = MISSION_COND_ATTACKED;
	goal->goal_kind = 2;
	goal->amount = 18;
	goal->points = 2;
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(((uint8_t)g_test_craft[1].attacked_by_team[0]) & 0x81,
			  0x81);
	XVT_ASSERT_INT_EQ(g_players[2].mission_stats.mission_score, 250 * 2);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
				  .team_scores[TEAM_SCORE_MISSION][0],
			  250 * 2);
	/* A second shot from the same team leaves the mark and the count, and
	 * scores nothing more. */
	place_shot(TEST_OTHER_SHOT, PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0,
		   19000, 0, 0, 20000, 0, 0);
	collide_laserhitcraft(TEST_OTHER_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(((uint8_t)g_test_craft[1].attacked_by_team[0]) & 0x81,
			  0x81);
	XVT_ASSERT_INT_EQ(g_players[2].mission_stats.mission_score, 250 * 2);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
				  .team_scores[TEAM_SCORE_MISSION][0],
			  250 * 2);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ATTACKED],
			  1);

	/* A shot from no player: the bit stays clear. */
	laser_hits_craft();
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(((uint8_t)g_test_craft[1].attacked_by_team[0]) & 0x80,
			  0);
	XVT_ASSERT_INT_EQ(((uint8_t)g_test_craft[1].attacked_by_team[0]) & 0x01,
			  1);
}

/* Comment: with no attacker recorded, a firing craft other than a freighter,
 * starship or platform becomes last_attacker_obj_idx with
 * last_hit_mission_second; a source in a static slot or one of those genera
 * does not. */
static void check_hit_records_attacker(void)
{
	static const struct {
		uint8_t genus;
		int recorded;
	} sources[] = {
		{CRAFT_GENUS_STARFIGHTER, 1}, {CRAFT_GENUS_TRANSPORT, 1},
		{CRAFT_GENUS_STARSHIP, 0},    {CRAFT_GENUS_PLATFORM, 0},
		{CRAFT_GENUS_FREIGHTER, 0},
	};
	for (size_t i = 0; i < sizeof sources / sizeof sources[0]; ++i) {
		laser_hits_craft();
		g_test_objects[0].genus_id = sources[i].genus;
		g_test_craft[1].last_hit_mission_second = 0;
		collide_laserhitcraft(TEST_SHOT, 1, -1);
		XVT_ASSERT_INT_EQ(g_test_craft[1].last_attacker_obj_idx,
				  sources[i].recorded ? 0 : UINT16_MAX);
		XVT_ASSERT_INT_EQ(g_test_craft[1].last_hit_mission_second,
				  sources[i].recorded ? TEST_CLOCK_SECOND : 0);
		/* The threat is the source whether or not it is recorded. */
		XVT_ASSERT_INT_EQ(g_test_craft[1].ai_flight.threat_obj_idx, 0);
	}

	/* A source in a static slot is no attacker, though a threat. */
	laser_hits_craft();
	place_mine(20000, 0, 0);
	g_test_mobiles[TEST_SHOT].source_obj_idx = TEST_MINE;
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].last_attacker_obj_idx, UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_test_craft[1].ai_flight.threat_obj_idx, TEST_MINE);
	/* Likewise when the static slot is the first one past the craft's. */
	laser_hits_craft();
	place_mine(20000, 0, 0);
	g_test_mobiles[TEST_SHOT].source_obj_idx = TEST_MINE;
	g_active_region_craft_object_slot_end = TEST_MINE;
	g_test_objects[TEST_MINE].mobj = &g_test_mobiles[TEST_MINE - 1];
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].last_attacker_obj_idx, UINT16_MAX);
	g_test_objects[TEST_MINE].mobj = NULL;

	/* With one recorded, a player's craft takes each new attacker, with the
	 * time; a computer-flown craft keeps the first. */
	for (int owned = 0; owned < 2; ++owned) {
		laser_hits_craft();
		g_test_craft[1].last_attacker_obj_idx = 7;
		g_test_craft[1].last_hit_mission_second = 5;
		if (owned) {
			player_flies(1, 1);
		}
		collide_laserhitcraft(TEST_SHOT, 1, -1);
		XVT_ASSERT_INT_EQ(g_test_craft[1].last_attacker_obj_idx,
				  owned ? 0 : 7);
		XVT_ASSERT_INT_EQ(g_test_craft[1].last_hit_mission_second,
				  owned ? TEST_CLOCK_SECOND : 5);
	}
}

/* Comment: a friendly player's shots on a craft other than a starfighter
 * count only once warhead hits fill the 3-bit count in attacked_by_team:
 * cannon shots add nothing, a warhead adds 4 and the count holds at 7; the
 * attacker is recorded when the count has reached 7, and always for a hostile
 * player or a starfighter. */
static void check_hit_friendly_player_counts(void)
{
	/* Player 2 flies craft 0 on team 0; craft 1 is team 1, allied to
	 * team 0 in this check. */
	laser_hits_craft();
	player_flies(0, 2);
	g_mission_teams[0].allies[1] = 1;
	g_test_objects[1].genus_id = CRAFT_GENUS_TRANSPORT;
	/* A cannon shot from a friendly player: nothing recorded, count 0. */
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].last_attacker_obj_idx, UINT16_MAX);
	XVT_ASSERT_INT_EQ(
		((uint8_t)g_test_craft[1].attacked_by_team[0] >> 4) & 7, 0);

	/* A warhead: count 4, not recorded; the next: 7, recorded. */
	g_test_objects[TEST_SHOT].object_type =
		WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE;
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(
		((uint8_t)g_test_craft[1].attacked_by_team[0] >> 4) & 7, 4);
	XVT_ASSERT_INT_EQ(g_test_craft[1].last_attacker_obj_idx, UINT16_MAX);
	g_test_objects[TEST_SHOT].object_type =
		WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE;
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(
		((uint8_t)g_test_craft[1].attacked_by_team[0] >> 4) & 7, 7);
	XVT_ASSERT_INT_EQ(g_test_craft[1].last_attacker_obj_idx, 0);
	/* The goal mark and first-hit bit are kept. */
	XVT_ASSERT_INT_EQ(((uint8_t)g_test_craft[1].attacked_by_team[0]) & 0x81,
			  0x81);

	/* A friendly player's hit on a starfighter is recorded at once. */
	laser_hits_craft();
	player_flies(0, 2);
	g_mission_teams[0].allies[1] = 1;
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].last_attacker_obj_idx, 0);

	/* A hostile player's hit on a transport too. */
	laser_hits_craft();
	player_flies(0, 2);
	g_mission_teams[0].allies[1] = 0;
	g_test_objects[1].genus_id = CRAFT_GENUS_TRANSPORT;
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].last_attacker_obj_idx, 0);

	/* The same team: friendly without any alliance. */
	laser_hits_craft();
	player_flies(0, 2);
	g_test_mobiles[0].team = 1;
	g_test_objects[1].genus_id = CRAFT_GENUS_TRANSPORT;
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].last_attacker_obj_idx, UINT16_MAX);
	g_mission_teams[0].allies[1] = 0;

	/* A count of 5 is past the laser limit: a warhead adds nothing more,
	 * and the attacker is not recorded. */
	laser_hits_craft();
	player_flies(0, 2);
	g_test_mobiles[0].team = 1;
	g_test_objects[1].genus_id = CRAFT_GENUS_TRANSPORT;
	g_test_craft[1].attacked_by_team[0] = 0x51;
	g_test_objects[TEST_SHOT].object_type =
		WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE;
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(
		((uint8_t)g_test_craft[1].attacked_by_team[0] >> 4) & 7, 5);
	XVT_ASSERT_INT_EQ(g_test_craft[1].last_attacker_obj_idx, UINT16_MAX);

	/* A computer-flown firer, friendly to a transport: recorded at once,
	 * with no count kept. */
	laser_hits_craft();
	g_test_mobiles[0].team = 1;
	g_test_objects[1].genus_id = CRAFT_GENUS_TRANSPORT;
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].last_attacker_obj_idx, 0);
	XVT_ASSERT_INT_EQ(
		((uint8_t)g_test_craft[1].attacked_by_team[1] >> 4) & 7, 0);
}

/* Comment: a hit on a player's craft takes the rear shield (side 1) when the
 * shot travels the way the craft faces, else the front; a computer-flown
 * craft's hit is always on side 0 (front). */
static void check_hit_side_by_travel(void)
{
	/* Travelling along +X, the way craft 1 faces: the rear shield. */
	laser_hits_craft();
	player_flies(1, 1);
	/* The rear is the stronger bank, so evening leaves it alone. */
	g_test_craft[1].shield_energy[0] = 500;
	g_test_craft[1].shield_energy[1] = 1000;
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 500);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[1], 1000 - 250);
	XVT_ASSERT_TRUE(line_holds("combat.shot_hit", "side=1"));

	/* Travelling against it: the front. */
	laser_hits_craft();
	player_flies(1, 1);
	g_collision_probe_world_x = 18000;
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 250);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[1], 500);
	XVT_ASSERT_TRUE(line_holds("combat.shot_hit", "side=0"));

	/* A computer-flown craft: the front whichever way. */
	laser_hits_craft();
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 250);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[1], 500);

	/* A craft whose axes are out of date has them recomputed first. */
	laser_hits_craft();
	g_test_mobiles[1].orient_matrix_dirty = 1;
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(g_test_mobiles[1].orient_matrix_dirty, 0);
}

/* Comment: active chaff stops a warhead coming from behind (the shot
 * travelling the way the craft faces), with a message to the craft's
 * player, and no damage is dealt; a cannon shot, a warhead from the front,
 * spent chaff, or another countermeasure does not stop. The shot becomes its
 * impact effect either way. */
static void check_hit_chaff(void)
{
	static const struct {
		uint8_t shot_type;
		int toward_front;
		int chaff_seconds;
		countermeasure_type countermeasure;
		int stopped;
	} cases[] = {
		{WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 0, 5,
		 COUNTERMEASURE_TYPE_CHAFF, 1},
		{WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 1, 5,
		 COUNTERMEASURE_TYPE_CHAFF, 0},
		{WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 0, 0,
		 COUNTERMEASURE_TYPE_CHAFF, 0},
		{PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0, 5,
		 COUNTERMEASURE_TYPE_CHAFF, 0},
		{WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 0, 5,
		 COUNTERMEASURE_TYPE_CHAFF + 1, 0},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		laser_hits_craft();
		g_test_objects[TEST_SHOT].object_type = cases[i].shot_type;
		g_test_mobiles[TEST_SHOT].damage_amount = 250;
		if (cases[i].toward_front) {
			g_collision_probe_world_x = 18000;
		}
		g_test_craft[1].cm_type_id = cases[i].countermeasure;
		g_test_craft[1].chaff_active_seconds =
			(uint16_t)cases[i].chaff_seconds;
		player_flies(1, 0);
		collide_laserhitcraft(TEST_SHOT, 1, -1);
		int taken = 1000 - g_test_craft[1].shield_energy[0] + 500 -
			    g_test_craft[1].shield_energy[1];
		XVT_ASSERT_INT_EQ(taken, cases[i].stopped ? 0 : 250);
		XVT_ASSERT_INT_EQ(count_lines("combat.warhead_scattered"),
				  cases[i].stopped);
		/* The shot ends as an impact effect even when scattered. */
		XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].genus_id,
				  CRAFT_GENUS_EXPLOSION);
		XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].world_x, 19600);
	}
}

/* Comment: the shot becomes object type 129 from a warhead, 132 when ion
 * and 131 when a laser, at the hit point. */
static void check_hit_impact_effect_types(void)
{
	static const struct {
		uint8_t shot_type;
		int effect;
	} cases[] = {
		{PROJECTILE_OBJECT_TYPE_REBEL_LASER, 131},
		{PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER, 131},
		{PROJECTILE_OBJECT_TYPE_IMPERIAL_LASER, 131},
		{PROJECTILE_OBJECT_TYPE_IMPERIAL_TURBO_LASER, 131},
		{PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER_2, 131},
		{PROJECTILE_OBJECT_TYPE_IMPERIAL_TURBO_LASER_2, 131},
		{PROJECTILE_OBJECT_TYPE_ION_LASER, 132},
		{PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER, 132},
		{WARHEAD_OBJECT_TYPE_PROTON_TORPEDO, 129},
		{WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 129},
		{WARHEAD_OBJECT_TYPE_ADVANCED_PROTON_TORPEDO, 129},
		{WARHEAD_OBJECT_TYPE_ADVANCED_CONCUSSION_MISSILE, 129},
		{WARHEAD_OBJECT_TYPE_SPACE_BOMB, 129},
		{WARHEAD_OBJECT_TYPE_HEAVY_ROCKET, 129},
		{WARHEAD_OBJECT_TYPE_MAGNETIC_PULSE, 129},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		laser_hits_craft();
		g_test_objects[TEST_SHOT].object_type = cases[i].shot_type;
		collide_laserhitcraft(TEST_SHOT, 1, -1);
		XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].object_type,
				  cases[i].effect);
		XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].genus_id,
				  CRAFT_GENUS_EXPLOSION);
		XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].world_x, 19600);
	}
}

/* Comment: a magnetic pulse on a player's craft empties its cannons' charge
 * and knocks out the cannon system if it worked; with the cannons already
 * out it only drains. The weapon_fire_inhibit_timer of a computer-flown craft
 * holds at 0xFFFF rather than wrapping. */
static void check_pulse_on_player_craft(void)
{
	/* A working cannon system: its charge is gone, the system out and
	 * marked for repair. */
	pulse_at_craft();
	hit_geometry();
	player_flies(1, 0);
	all_systems_work(&g_test_craft[1]);
	g_test_craft[1].laser_slot_count = 2;
	g_test_craft[1].weapon_slots[0].laser_charge = 90;
	g_test_craft[1].weapon_slots[1].laser_charge = 80;
	g_test_craft[1].weapon_slots[2].laser_charge = 70;
	g_test_craft[1].system_health[3] = 50;
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].weapon_slots[0].laser_charge, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].weapon_slots[1].laser_charge, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].weapon_slots[2].laser_charge, 70);
	XVT_ASSERT_INT_EQ(g_test_craft[1].working_subsystems,
			  CRAFT_SUBSYSTEM_FLAGS_ALL ^
				  CRAFT_SUBSYSTEM_FLAG_CANNONS);
	XVT_ASSERT_INT_EQ(g_test_craft[1].system_health[3], 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].system_repair_seconds[3],
			  g_subsystem_repair_duration[3]);
	XVT_ASSERT_TRUE(line_holds("combat.system_failed", "cause=\"pulse\""));
	XVT_ASSERT_INT_EQ(count_lines("msg.message_built"), 1);
	/* Nothing is added to a player's inhibit timer. */
	XVT_ASSERT_INT_EQ(g_test_craft[1].weapon_fire_inhibit_timer, 0);

	/* Cannons already out: the drain message, no repair mark. */
	pulse_at_craft();
	hit_geometry();
	player_flies(1, 0);
	all_systems_work(&g_test_craft[1]);
	g_test_craft[1].working_subsystems &= ~CRAFT_SUBSYSTEM_FLAG_CANNONS;
	g_test_craft[1].system_repair_seconds[3] = 0;
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].system_repair_seconds[3], 0);
	XVT_ASSERT_INT_EQ(count_lines("msg.message_built"), 1);
	XVT_ASSERT_INT_EQ(count_lines("combat.system_failed"), 0);

	/* A pulse on the other player's craft: no message for the local one. */
	pulse_at_craft();
	hit_geometry();
	player_flies(1, 1);
	all_systems_work(&g_test_craft[1]);
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].working_subsystems,
			  CRAFT_SUBSYSTEM_FLAGS_ALL ^
				  CRAFT_SUBSYSTEM_FLAG_CANNONS);
	XVT_ASSERT_INT_EQ(count_lines("msg.message_built"), 0);

	/* The AI craft's timer holds at the top. */
	pulse_at_craft();
	hit_geometry();
	g_test_craft[1].weapon_fire_inhibit_timer = -1000;
	collide_laserhitcraft(TEST_SHOT, 1, -1);
	XVT_ASSERT_INT_EQ((uint16_t)g_test_craft[1].weapon_fire_inhibit_timer,
			  0xFFFF);
}

/* ------------------------------------------------------------------------ */
/* collide_damagecraft: hits on a mesh, and what the local player sees. */

enum {
	MESH_EXPLOSION_BIT = 1,
	MESH_DAMAGEABLE_BIT = 2,
};

/* A hull mesh and two gun meshes for the starfighter object type: mesh 1
 * has component flags first, meshes 2 and 3 flags guns. g_cur_craft is craft
 * 1, as the model walk leaves it. */
static void gun_craft_model(int first, int guns)
{
	static const int types[3] = {MESH_COMPONENT_01_MAIN_HULL,
				     MESH_COMPONENT_05_LASR_GUN,
				     MESH_COMPONENT_05_LASR_GUN};
	static const int center_x[3] = {0, 0, 0};
	int flags[3] = {first, guns, guns};
	craft_model(3, types, center_x, flags);
	g_cur_craft = &g_test_craft[1];
}

/* Comment: a hit on a mesh whose explosion type has bit 0 set goes first to
 * craft_damage_component, on difficulty 0 always, otherwise only with both
 * shields down. A component that takes the whole hit leaves nothing for the
 * shield; a destroyed one passes on only the rest; a mesh without the bit
 * is not looked at. */
static void check_damage_component_first(void)
{
	/* Difficulty 0: the component takes the hit while the shield is up. */
	laser_at_craft();
	gun_craft_model(0, MESH_EXPLOSION_BIT);
	g_test_craft[1].hull_max = 100000;
	g_flight_mission_state.difficulty = 0;
	g_test_craft[1].component_hp[1] = 20;
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, 2, TEST_SHOT, 0), 1);
	/* 16 * 20 = 320 over 250 left: a quarter of the component stays,
	 * rounded down: (320 - 250) / 16. */
	XVT_ASSERT_INT_EQ(g_test_craft[1].component_hp[1], (320 - 250) / 16);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000);
	XVT_ASSERT_INT_EQ(g_test_craft[1].damage_stats.damage_received_total,
			  250);

	/* A component with too little left is destroyed and the shield takes
	 * the rest. */
	laser_at_craft();
	gun_craft_model(0, MESH_EXPLOSION_BIT);
	g_test_craft[1].hull_max = 100000;
	g_flight_mission_state.difficulty = 0;
	g_test_craft[1].component_hp[1] = 10;
	collide_damagecraft(1, 2, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].component_hp[1], 0);
	XVT_ASSERT_TRUE(g_test_craft[1].shield_energy[0] < 1000);
	XVT_ASSERT_TRUE(g_test_craft[1].shield_energy[0] > 1000 - 250);

	/* A mesh without the bit: the shield takes it all. */
	laser_at_craft();
	gun_craft_model(0, MESH_DAMAGEABLE_BIT);
	g_test_craft[1].hull_max = 100000;
	g_flight_mission_state.difficulty = 0;
	g_test_craft[1].component_hp[1] = 20;
	collide_damagecraft(1, 2, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].component_hp[1], 20);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 250);

	/* Any other difficulty: not while a shield holds energy; with both
	 * down, yes. */
	laser_at_craft();
	gun_craft_model(0, MESH_EXPLOSION_BIT);
	g_test_craft[1].hull_max = 100000;
	g_flight_mission_state.difficulty = 1;
	g_test_craft[1].component_hp[1] = 20;
	collide_damagecraft(1, 2, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].component_hp[1], 20);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 250);
	laser_at_craft();
	gun_craft_model(0, MESH_EXPLOSION_BIT);
	g_test_craft[1].hull_max = 100000;
	g_flight_mission_state.difficulty = 1;
	g_test_craft[1].shield_energy[0] = 0;
	g_test_craft[1].shield_energy[1] = 0;
	g_test_craft[1].component_hp[1] = 20;
	collide_damagecraft(1, 2, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].component_hp[1], (320 - 250) / 16);
	XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 0);
	/* Only one shield down is not both. */
	laser_at_craft();
	gun_craft_model(0, MESH_EXPLOSION_BIT);
	g_test_craft[1].hull_max = 100000;
	g_flight_mission_state.difficulty = 1;
	g_test_craft[1].shield_energy[0] = 0;
	g_test_craft[1].component_hp[1] = 20;
	collide_damagecraft(1, 2, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].component_hp[1], 20);
	g_flight_mission_state.difficulty = 0;
}

/* Comment: hull damage on a damageable mesh goes to the component too, and
 * the hull takes the rest; an obstacle takes nothing at all. */
static void check_damage_component_in_hull(void)
{
	/* Shields down; the mesh is damageable only: the hull takes the part
	 * the component cannot. */
	laser_at_craft();
	gun_craft_model(0, MESH_DAMAGEABLE_BIT);
	g_test_craft[1].hull_max = 100000;
	g_test_craft[1].system_damage_hull_threshold = 100000;
	g_test_craft[1].shield_energy[0] = 0;
	g_test_craft[1].shield_energy[1] = 0;
	g_test_craft[1].component_hp[1] = 10;
	collide_damagecraft(1, 2, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].component_hp[1], 0);
	XVT_ASSERT_TRUE(g_test_craft[1].hull_damage > 0);
	XVT_ASSERT_TRUE(g_test_craft[1].hull_damage < 250);

	laser_at_craft();
	gun_craft_model(0, MESH_DAMAGEABLE_BIT);
	g_test_craft[1].hull_max = 100000;
	g_test_craft[1].system_damage_hull_threshold = 100000;
	g_test_craft[1].shield_energy[0] = 0;
	g_test_craft[1].shield_energy[1] = 0;
	g_test_craft[1].component_hp[1] = 20;
	collide_damagecraft(1, 2, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].component_hp[1], (320 - 250) / 16);
	XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 0);

	/* An obstacle takes nothing. */
	laser_at_craft();
	g_test_objects[1].genus_id = CRAFT_GENUS_OBSTACLE;
	g_test_craft[1].hull_max = 100000;
	g_test_craft[1].shield_energy[0] = 0;
	g_test_craft[1].shield_energy[1] = 0;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].damage_stats.damage_received_total,
			  250);
}

/* The tactical officer is on at level 2 and the local player flies craft 0
 * on team 0, so lines about craft 1 (team 1, designation 1) are spoken. */
static void officer_speaks(void)
{
	g_game_config.voice_tactical_officer_level = 2;
	g_players[0].object_index = 0;
	g_players[0].team = 0;
	for (int team = 0; team < 2; ++team) {
		for (int group = 0; group < 2; ++group) {
			g_flight_mission_state.runtime
				.team_fg_designation_code[team][group] = 1;
		}
	}
}

/* Comment: tactical officer reports come as hull_damage passes thresholds
 * (95%, 75% and 25% of hull_max, on a craft that is not a starfighter), and a
 * shield going out. Speech cannot be read back here; the hull sums can: each
 * hit adds to hull_damage and no hit ends the craft before hull_max. */
static void check_damage_hull_thresholds(void)
{
	static const int hits[] = {240, 20, 300, 200, 160, 45};
	laser_at_craft();
	officer_speaks();
	g_test_objects[1].genus_id = CRAFT_GENUS_TRANSPORT;
	g_test_craft[1].hull_max = 1000;
	g_test_craft[1].system_damage_hull_threshold = 1000;
	g_test_craft[1].shield_energy[0] = 10;
	g_test_craft[1].shield_energy[1] = 0;
	int expected = 0;
	/* The first hit empties the shield, and 230 reaches the hull. */
	for (size_t i = 0; i < sizeof hits / sizeof hits[0]; ++i) {
		g_test_mobiles[TEST_SHOT].damage_amount = (uint16_t)hits[i];
		collide_damagecraft(1, -1, TEST_SHOT, 0);
		expected += hits[i] - (i == 0 ? 10 : 0);
		XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, expected);
		XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
				  CRAFT_OBJECT_KIND_ACTIVE);
	}
	/* 955 in all: past 95% of 1,000, short of the end. */
	XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 955);

	/* A shield that still has energy when it goes out: reported once; a
	 * hit that leaves it in place reports nothing. */
	laser_at_craft();
	officer_speaks();
	g_test_objects[1].genus_id = CRAFT_GENUS_TRANSPORT;
	g_test_craft[1].hull_max = 1000;
	g_test_craft[1].shield_energy[0] = 100;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 150);
	g_game_config.voice_tactical_officer_level = 0;
}

/* Comment: a disabled craft sends the officer's line and a warning tone for
 * the local player's own side; the hit still disables it. */
static void check_damage_disabled_with_officer(void)
{
	for (int own_side = 0; own_side < 3; ++own_side) {
		ion_at_craft(PROJECTILE_OBJECT_TYPE_ION_LASER, 2001);
		officer_speaks();
		g_test_craft[1].subsystem_damage = TEST_ION_STRENGTH - 1;
		if (own_side != 0) {
			g_players[0].team = 1;
		}
		/* The local player flies a type 1 craft, which has the R2
		 * warning, or a type 6 craft, which has the general one. */
		g_test_objects[0].object_type = own_side == 2 ? 6 : 1;
		collide_damagecraft(1, -1, TEST_SHOT, 0);
		XVT_ASSERT_INT_EQ(g_test_craft[1].working_subsystems, 0);
		XVT_ASSERT_INT_EQ(
			g_mission_fg_stats[1]
				.outcome_count[FLIGHT_GROUP_OUTCOME_DISABLED],
			1);
		g_game_config.voice_tactical_officer_level = 0;
	}
	/* A player with no craft of its own to hear: still disabled. */
	ion_at_craft(PROJECTILE_OBJECT_TYPE_ION_LASER, 2001);
	officer_speaks();
	g_players[0].object_index = 1;
	g_test_objects[1].object_type = 1;
	g_test_craft[1].subsystem_damage = TEST_ION_STRENGTH - 1;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].working_subsystems, 0);
	g_game_config.voice_tactical_officer_level = 0;
}

/* Comment: the local player's own craft is told when a system fails, and
 * its HUD indicators are redrawn when features or systems go. */
static void check_damage_local_player_systems(void)
{
	/* Hull damage knocks out the engines (seed 0) of the local player's
	 * craft; the message follows. */
	laser_at_craft();
	player_flies(1, 0);
	g_players[0].awaiting_new_craft = 0;
	g_test_craft[1].hull_max = 1000;
	g_test_craft[1].system_damage_hull_threshold = 1000;
	g_test_craft[1].shield_energy[1] = 0;
	g_test_craft[1].ai_controller.saved_rand_seed = 0;
	all_systems_work(&g_test_craft[1]);
	collide_damagecraft(1, -1, TEST_SHOT, 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].working_subsystems,
			  CRAFT_SUBSYSTEM_FLAGS_ALL ^
				  g_subsystem_id_to_flag[0]);
	XVT_ASSERT_INT_EQ(count_lines("msg.message_built"), 1);

	/* A craft waiting for its replacement hears nothing. */
	laser_at_craft();
	player_flies(1, 0);
	g_players[0].awaiting_new_craft = 1;
	g_test_craft[1].hull_max = 1000;
	g_test_craft[1].system_damage_hull_threshold = 1000;
	g_test_craft[1].shield_energy[1] = 0;
	g_test_craft[1].ai_controller.saved_rand_seed = 0;
	all_systems_work(&g_test_craft[1]);
	collide_damagecraft(1, -1, TEST_SHOT, 1);
	XVT_ASSERT_INT_EQ(count_lines("msg.message_built"), 0);
	g_players[0].awaiting_new_craft = 0;

	/* Features lost on the local player's craft: the indicators redrawn
	 * (the call is made while the craft is local, not in a replay). */
	laser_at_craft();
	player_flies(1, 0);
	g_test_craft[1].hull_max = 100000;
	g_test_craft[1].system_damage_hull_threshold = 0;
	g_test_craft[1].shield_energy[1] = 0;
	g_test_craft[1].ai_controller.saved_rand_seed = 0x7FFF;
	g_test_craft[1].damage_stats.installed_hud_feature_mask = 0xFFFF;
	g_test_craft[1].damage_stats.active_hud_feature_mask = 0xFFFF;
	collide_damagecraft(1, -1, TEST_SHOT, 1);
	XVT_ASSERT_TRUE(g_test_craft[1].damage_stats.active_hud_feature_mask !=
			0xFFFF);
}

/* Comment: a kill is reported to the local player by the wingman and
 * tactical officer lines, whatever their speech: the craft is destroyed the
 * same in every case: its own flight group the local player's (a loss), its
 * killer in the local player's group but not the local player (a kill by a
 * wingman, of a starfighter or something larger), and the officer on, the
 * victim on the player's side or an enemy, the player in either kind of
 * craft. */
static void check_kill_is_reported_to_player(void)
{
	static const struct {
		int bound_group;
		uint8_t victim_genus;
		int allied;
		int officer;
		uint8_t local_type;
	} cases[] = {
		{1, CRAFT_GENUS_STARFIGHTER, 0, 0, 1},
		{0, CRAFT_GENUS_STARFIGHTER, 0, 0, 1},
		{0, CRAFT_GENUS_TRANSPORT, 0, 0, 1},
		{0, CRAFT_GENUS_STARSHIP, 0, 0, 1},
		{2, CRAFT_GENUS_STARFIGHTER, 0, 1, 1},
		{2, CRAFT_GENUS_STARFIGHTER, 1, 1, 1},
		{2, CRAFT_GENUS_STARFIGHTER, 1, 1, 6},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		killing_shot();
		/* Enough to end a starship, which takes a sixteenth. */
		g_test_mobiles[TEST_SHOT].damage_amount = 5000;
		g_test_objects[1].genus_id = cases[i].victim_genus;
		g_players[0].bound_flight_group_idx =
			(uint16_t)cases[i].bound_group;
		g_players[0].object_index = cases[i].officer ? 0 : -1;
		g_players[0].team = 0;
		g_test_objects[0].object_type = cases[i].local_type;
		g_mission_teams[0].allies[1] = (uint8_t)cases[i].allied;
		if (cases[i].officer) {
			officer_speaks();
			g_players[0].object_index = 0;
		}
		collide_damagecraft(1, -1, TEST_SHOT, 0);
		XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
				  CRAFT_OBJECT_KIND_BREAKING_UP);
		XVT_ASSERT_INT_EQ(
			g_mission_fg_stats[1]
				.outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED],
			1);
		g_mission_teams[0].allies[1] = 0;
		g_game_config.voice_tactical_officer_level = 0;
	}
}

/* Comment: when a player's craft is destroyed the player is put to waiting
 * for a new craft (player.h: awaiting_new_craft 1), and the player's scan
 * for arriving craft restarts. */
static void check_kill_of_players_own_craft(void)
{
	for (int local = 0; local < 2; ++local) {
		killing_shot();
		player_flies(1, local);
		g_players[local].awaiting_new_craft = 0;
		g_flight_global_countdown_timers
			.mission_arrival_trigger_scan_timer = 77;
		collide_damagecraft(1, -1, TEST_SHOT, 0);
		XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
				  CRAFT_OBJECT_KIND_BREAKING_UP);
		XVT_ASSERT_INT_EQ(g_players[local].awaiting_new_craft, 1);
		XVT_ASSERT_INT_EQ(g_flight_global_countdown_timers
					  .mission_arrival_trigger_scan_timer,
				  0);
	}
}

/* ------------------------------------------------------------------------ */
/* collide_apply_craft_impact_bounce. */

enum {
	BOUNCE_CRAFT_SPEED = 100,
	BOUNCE_OTHER_SPEED = 30,
};

/* The heading difference diff (in 65,536ths of a turn) folded into a quarter
 * turn: mirrored into a half turn, then what lies past the quarter reflected
 * back, as the bounce comment says. */
static int fold_into_quarter(unsigned int diff)
{
	diff &= 0xFFFF;
	if (diff >= 0x8000) {
		diff = 0x10000 - diff;
	}
	if (diff > 0x4000) {
		diff = 0x8000 - diff;
	}
	return (int)diff;
}

static int abs_int(int value) { return value < 0 ? -value : value; }

/* Craft 1, a starfighter at (100, 0, 0) flying at BOUNCE_CRAFT_SPEED, and
 * another object in slot other apart from it (or touching it) with
 * the given heading: a craft 0 flying at BOUNCE_OTHER_SPEED, a shot in slot 2
 * of speed BOUNCE_OTHER_SPEED, or a static object in slot 5. */
static void bounce_setup(int other, unsigned int other_yaw, int touching)
{
	fresh_world();
	g_test_objects[1].yaw = 0x2000;
	g_test_mobiles[1].speed = BOUNCE_CRAFT_SPEED;
	g_test_mobiles[1].move_vector_dirty = 1;
	place(1, touching ? 5 : 100, 0, 0);
	g_test_mobiles[1].prev_world_x = touching ? 5 : 100;
	if (other == TEST_MINE) {
		place_mine(0, 0, 0);
	} else if (other == TEST_SHOT) {
		place_shot(TEST_SHOT, WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 0,
			   0, 0, 0, 0, 0, 0);
	}
	if (other != TEST_MINE) {
		g_test_mobiles[other].speed = BOUNCE_OTHER_SPEED;
		g_test_mobiles[other].move_vector_dirty = 1;
	}
	g_test_objects[other].yaw = (uint16_t)other_yaw;
	g_test_craft[1].ai_flight.impact_obj_idx = UINT16_MAX;
	g_test_craft[0].ai_flight.impact_obj_idx = UINT16_MAX;
}

/* Comment: the bounce does nothing unless the craft is a starfighter,
 * transport or utility vehicle. */
static void check_bounce_only_small_craft(void)
{
	static const struct {
		uint8_t genus;
		int bounces;
	} genera[] = {
		{CRAFT_GENUS_STARFIGHTER, 1},	  {CRAFT_GENUS_TRANSPORT, 1},
		{CRAFT_GENUS_UTILITY_VEHICLE, 1}, {CRAFT_GENUS_STARSHIP, 0},
		{CRAFT_GENUS_FREIGHTER, 0},	  {CRAFT_GENUS_PLATFORM, 0},
	};
	for (size_t i = 0; i < sizeof genera / sizeof genera[0]; ++i) {
		bounce_setup(0, 0x2000, 0);
		g_test_objects[1].genus_id = genera[i].genus;
		g_test_mobiles[1].roll_impulse_rate = 0;
		collide_apply_craft_impact_bounce(1, 0);
		XVT_ASSERT_INT_EQ(g_test_craft[1].ai_flight.impact_obj_idx,
				  genera[i].bounces ? 0 : UINT16_MAX);
		XVT_ASSERT_INT_EQ(g_test_mobiles[1].roll_impulse_rate != 0,
				  genera[i].bounces);
	}
}

/* Comment: the closing speed is the craft's speed less the other's speed
 * times the cosine of their heading difference folded into a quarter turn
 * (the other's counted only when it is a craft), in size; each craft that
 * bounces spins at 100 times that, at most 0x7FFF; the craft and a craft it
 * hits record each other as their impact object. */
static void check_bounce_roll_spin(void)
{
	/* Headings whose difference from the craft's 0x2000 does not fold to
	 * exactly a quarter turn, where the cosine is 0 and the table's
	 * rounding decides the sign. */
	static const unsigned int headings[] = {0x2000, 0x4000, 0x8000, 0xA000,
						0xC000, 0x0000, 0x3000, 0x7000,
						0x1000, 0x5000, 0x9000};
	for (int touching = 0; touching < 2; ++touching) {
		for (size_t i = 0; i < sizeof headings / sizeof headings[0];
		     ++i) {
			int fold = fold_into_quarter(headings[i] - 0x2000);
			int closing = abs_int(
				BOUNCE_CRAFT_SPEED -
				(int)(int16_t)trig2_cosinewordmult(
					BOUNCE_OTHER_SPEED, (int16_t)fold));

			/* Another craft: counted, and it spins too. */
			bounce_setup(0, headings[i], touching);
			collide_apply_craft_impact_bounce(1, 0);
			XVT_ASSERT_INT_EQ(
				abs_int(g_test_mobiles[1].roll_impulse_rate),
				100 * closing);
			XVT_ASSERT_INT_EQ(
				abs_int(g_test_mobiles[0].roll_impulse_rate),
				100 * closing);
			XVT_ASSERT_INT_EQ(
				g_test_craft[1].ai_flight.impact_obj_idx, 0);
			XVT_ASSERT_INT_EQ(
				g_test_craft[0].ai_flight.impact_obj_idx, 1);
			XVT_ASSERT_TRUE(find_line("combat.craft_bounced") !=
					NULL);

			/* A shot: its speed is not counted, and it does not
			 * spin. */
			bounce_setup(TEST_SHOT, headings[i], touching);
			collide_apply_craft_impact_bounce(1, TEST_SHOT);
			XVT_ASSERT_INT_EQ(
				abs_int(g_test_mobiles[1].roll_impulse_rate),
				100 * BOUNCE_CRAFT_SPEED);
			XVT_ASSERT_INT_EQ(
				g_test_mobiles[TEST_SHOT].roll_impulse_rate, 0);
			XVT_ASSERT_INT_EQ(
				g_test_craft[1].ai_flight.impact_obj_idx,
				TEST_SHOT);
			XVT_ASSERT_INT_EQ(
				g_test_craft[0].ai_flight.impact_obj_idx,
				UINT16_MAX);

			/* A static object, with no mobile_object. */
			bounce_setup(TEST_MINE, headings[i], touching);
			collide_apply_craft_impact_bounce(1, TEST_MINE);
			XVT_ASSERT_INT_EQ(
				abs_int(g_test_mobiles[1].roll_impulse_rate),
				100 * BOUNCE_CRAFT_SPEED);
			XVT_ASSERT_INT_EQ(
				g_test_craft[1].ai_flight.impact_obj_idx,
				TEST_MINE);
		}
	}

	/* The spin stops at 0x7FFF: 327 gives 32,700, 328 more than can be
	 * held. */
	for (int speed = 327; speed <= 328; ++speed) {
		bounce_setup(TEST_MINE, 0, 0);
		g_test_mobiles[1].speed = speed;
		collide_apply_craft_impact_bounce(1, TEST_MINE);
		XVT_ASSERT_INT_EQ(abs_int(g_test_mobiles[1].roll_impulse_rate),
				  speed == 327 ? 32700 : 0x7FFF);
	}
	/* Headings a quarter turn apart: the other's speed does not count, give
	 * or take the table's rounding of 1. */
	for (unsigned int heading = 0x4000; heading <= 0xC000;
	     heading += 0x8000) {
		bounce_setup(0, 0x2000 + heading, 0);
		collide_apply_craft_impact_bounce(1, 0);
		XVT_ASSERT_TRUE(abs_int(g_test_mobiles[1].roll_impulse_rate) >=
				100 * (BOUNCE_CRAFT_SPEED - 1));
		XVT_ASSERT_TRUE(abs_int(g_test_mobiles[1].roll_impulse_rate) <=
				100 * (BOUNCE_CRAFT_SPEED + 1));
	}
	/* Two craft closing fast: both spins stop at 0x7FFF, whatever
	 * their moves were. */
	bounce_setup(0, 0x2000, 0);
	g_test_mobiles[1].speed = 500;
	g_test_mobiles[0].move_vector_dirty = 0;
	g_test_mobiles[1].move_vector_dirty = 0;
	g_test_mobiles[0].move_x = -300;
	g_test_mobiles[0].move_y = -300;
	g_test_mobiles[1].move_x = 200;
	g_test_mobiles[1].move_y = -200;
	collide_apply_craft_impact_bounce(1, 0);
	XVT_ASSERT_INT_EQ(abs_int(g_test_mobiles[1].roll_impulse_rate), 0x7FFF);
	XVT_ASSERT_INT_EQ(abs_int(g_test_mobiles[0].roll_impulse_rate), 0x7FFF);
	/* A craft hit from behind at less than the other's speed still closes
	 * by the difference. */
	bounce_setup(0, 0x2000, 0);
	g_test_mobiles[1].speed = 10;
	g_test_mobiles[0].speed = 60;
	collide_apply_craft_impact_bounce(1, 0);
	XVT_ASSERT_INT_EQ(
		abs_int(g_test_mobiles[1].roll_impulse_rate),
		100 * abs_int(10 - (int)(int16_t)trig2_cosinewordmult(60, 0)));
}

/* Comment: a warhead hit bounces an active craft when
 * craft_impact_bounce_enabled is set, and not otherwise, nor a craft in any
 * other state, nor a hit by a cannon shot. The craft records the shot as its
 * impact object. */
static void check_hit_bounce_conditions(void)
{
	static const struct {
		int enabled;
		uint8_t kind;
		uint8_t shot_type;
		int bounces;
	} cases[] = {
		{1, CRAFT_OBJECT_KIND_ACTIVE,
		 WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 1},
		{0, CRAFT_OBJECT_KIND_ACTIVE,
		 WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 0},
		{1, CRAFT_OBJECT_KIND_DISABLED,
		 WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 0},
		{1, CRAFT_OBJECT_KIND_ACTIVE,
		 PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		laser_hits_craft();
		g_test_objects[TEST_SHOT].object_type = cases[i].shot_type;
		g_test_mobiles[TEST_SHOT].speed = 40;
		g_test_mobiles[1].speed = 50;
		g_flight_mission_state.craft_impact_bounce_enabled =
			(uint8_t)cases[i].enabled;
		g_test_craft[1].object_kind = cases[i].kind;
		g_test_craft[1].ai_flight.impact_obj_idx = UINT16_MAX;
		collide_laserhitcraft(TEST_SHOT, 1, -1);
		XVT_ASSERT_INT_EQ(g_test_craft[1].ai_flight.impact_obj_idx,
				  cases[i].bounces ? TEST_SHOT : UINT16_MAX);
	}
	g_flight_mission_state.craft_impact_bounce_enabled = 0;
}

/* ------------------------------------------------------------------------ */
/* collide_apply_engine_wash_damage and
 * collide_apply_hostile_proximity_weapon_disruption: effects on craft
 * nearby. */

enum {
	TEST_SHIP_TYPE = 30,
	SHIP_EXTENT = 2000,
	JAMMED = 163840,
	REPAIR_YARD_TYPE = 91,
	X7_FACTORY_TYPE = 69,
	INTERDICTOR_TYPE = 51,
	VICTORY_TYPE = 52,
	IMPERIAL_TYPE = 53,
	SUPER_TYPE = 54,
	CALAMARI_TYPE = 49,
};

/* Sets mesh i of the model craft_model built to the box (min..max), center
 * (cx, 0, cz). */
static void mesh_box(int mesh, float min_x, float min_y, float min_z,
		     float max_x, float max_y, float max_z, float cx, float cz)
{
	struct mesh_descriptor *descriptor = &g_craft_model_descriptors[mesh];
	descriptor->box_min.x = min_x;
	descriptor->box_min.y = min_y;
	descriptor->box_min.z = min_z;
	descriptor->box_max.x = max_x;
	descriptor->box_max.y = max_y;
	descriptor->box_max.z = max_z;
	descriptor->center.x = cx;
	descriptor->center.z = cz;
}

/* Both craft of the wash and jam checks sit away from the origin, so that a
 * difference of their positions is not mistaken for either one. */
enum {
	NEAR_OFFSET_X = 7000,
	NEAR_OFFSET_Y = -5000,
	NEAR_OFFSET_Z = 3000,
};

static void place_rel(int slot, int x, int y, int z)
{
	place(slot, NEAR_OFFSET_X + x, NEAR_OFFSET_Y + y, NEAR_OFFSET_Z + z);
}

/* Craft 1 is a ship of ship_type with one engine mesh (box +-100 along its
 * length, +-50 across), working systems and axes along the world's; craft 0
 * is a fighter with its shields up at the origin; ship at the origin too. */
static void wash_world(int ship_type)
{
	static const int types[1] = {MESH_COMPONENT_06_ENGINE};
	static const int center_x[1] = {0};
	static const int flags[1] = {0};
	fresh_world();
	g_craft_model_type = ship_type;
	craft_model(1, types, center_x, flags);
	mesh_box(0, -50, -100, -50, 50, 100, 50, 0, 0);
	g_object_type_table[ship_type].max_bounds_extent = SHIP_EXTENT;
	g_test_objects[1].object_type = (uint8_t)ship_type;
	g_test_objects[1].genus_id = CRAFT_GENUS_STARSHIP;
	g_test_craft[1].working_subsystems = 1;
	g_test_mobiles[1].cached_side_x = FORWARD_X;
	g_test_mobiles[1].cached_fwd_y = FORWARD_X;
	g_test_mobiles[1].cached_up_z = FORWARD_X;
	place_rel(1, 0, 0, 0);
	place_rel(0, 0, 0, 0);
	g_test_craft[0].shield_energy[0] = 1000;
	g_test_craft[0].shield_energy[1] = 500;
	g_test_craft[0].hull_max = 100000;
	g_test_craft[0].system_damage_hull_threshold = 100000;
}

/* The front shield energy craft 0 lost to a wash from the ship when at
 * (x, y, z) (the ship faces +Y, so negative y is behind it). */
static int wash_at(int x, int y, int z)
{
	g_test_craft[0].shield_energy[0] = 1000;
	g_test_craft[0].damage_stats.damage_received_total = 0;
	g_test_craft[0].damage_stats.damage_from_starship = 0;
	place_rel(0, x, y, z);
	collide_apply_engine_wash_damage(0, 1);
	return 1000 - g_test_craft[0].shield_energy[0];
}

/* Comment: a victim behind an engine takes damage that falls off with depth
 * into the wash and with distance from its center, at most 64; none when
 * ahead of the ship, beyond 3 times its extent, outside the wash's length or
 * to the side of it. The hit goes through collide_damagecraft as engine wash
 * (the starship tally). */
static void check_wash_falls_off(void)
{
	wash_world(TEST_SHIP_TYPE);
	int near_engine = wash_at(0, -300, 0);
	int deep = wash_at(0, -1000, 0);
	int off_center = wash_at(60, -300, 0);
	XVT_ASSERT_TRUE(near_engine > 0);
	XVT_ASSERT_TRUE(near_engine <= 64);
	/* Less with depth, and less off the wash's center line. */
	XVT_ASSERT_TRUE(deep > 0);
	XVT_ASSERT_TRUE(deep < near_engine);
	XVT_ASSERT_TRUE(off_center > 0);
	XVT_ASSERT_TRUE(off_center < near_engine);
	/* Past the wash's length (8 times the engine's 200 = 1,600 behind its
	 * start), far to the side, above, ahead of the ship, out of range: none
	 * (the shield is untouched). */
	XVT_ASSERT_INT_EQ(wash_at(0, -1800, 0), 0);
	XVT_ASSERT_INT_EQ(wash_at(900, -300, 0), 0);
	XVT_ASSERT_INT_EQ(wash_at(0, -300, 900), 0);
	XVT_ASSERT_INT_EQ(wash_at(0, 2500, 0), 0);
	XVT_ASSERT_INT_EQ(wash_at(0, -6500, 0), 0);
	/* The ship's wash is tallied as a starship's. */
	wash_at(0, -300, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[0].damage_stats.damage_from_starship,
			  near_engine);
	XVT_ASSERT_TRUE(find_line("combat.damage_taken") != NULL);

	/* A source without a mobile_object, or too far, does nothing. */
	wash_world(TEST_SHIP_TYPE);
	g_test_objects[TEST_MINE].object_type = TEST_SHIP_TYPE;
	g_object_type_table[TEST_SHIP_TYPE].max_bounds_extent = SHIP_EXTENT;
	g_test_objects[TEST_MINE].genus_id = CRAFT_GENUS_STARSHIP;
	place_rel(TEST_MINE, 0, 0, 0);
	place_rel(0, 0, -300, 0);
	collide_apply_engine_wash_damage(0, TEST_MINE);
	XVT_ASSERT_INT_EQ(g_test_craft[0].shield_energy[0], 1000);
}

/* Comment: at most 64, with shields down an eighth and at least 1, and a
 * player's engine_wash_source_obj_idx and engine_wash_strength keep the
 * strongest wash. */
static void check_wash_limits_and_player_record(void)
{
	/* A huge engine: the 64 cap. */
	wash_world(TEST_SHIP_TYPE);
	g_object_type_table[TEST_SHIP_TYPE].max_bounds_extent = 40000;
	mesh_box(0, -6000, -6000, -6000, 6000, 6000, 6000, 0, 0);
	XVT_ASSERT_INT_EQ(wash_at(0, -7000, 0), 64);

	/* Shields down: an eighth; a bigger ship, so that is more than 1. */
	wash_world(TEST_SHIP_TYPE);
	g_object_type_table[TEST_SHIP_TYPE].max_bounds_extent = 4000;
	mesh_box(0, -500, -500, -500, 500, 500, 500, 0, 0);
	int full = wash_at(0, -300, 0);
	XVT_ASSERT_TRUE(full >= 16);
	g_test_craft[0].shield_energy[0] = 0;
	g_test_craft[0].shield_energy[1] = 0;
	place_rel(0, 0, -300, 0);
	collide_apply_engine_wash_damage(0, 1);
	XVT_ASSERT_INT_EQ(g_test_craft[0].hull_damage, full >> 3);

	/* At the very end of the wash the figure works out to 0, and a wash
	 * does at least 1, with shields up or down. */
	wash_world(TEST_SHIP_TYPE);
	XVT_ASSERT_INT_EQ(wash_at(0, -1450, 0), 1);
	g_test_craft[0].shield_energy[0] = 0;
	g_test_craft[0].shield_energy[1] = 0;
	place_rel(0, 0, -1450, 0);
	collide_apply_engine_wash_damage(0, 1);
	XVT_ASSERT_INT_EQ(g_test_craft[0].hull_damage, 1);

	/* A player's record keeps the strongest wash. */
	wash_world(TEST_SHIP_TYPE);
	player_flies(0, 1);
	g_players[1].engine_wash_source_obj_idx = -1;
	int strong = wash_at(0, -300, 0);
	XVT_ASSERT_INT_EQ(g_players[1].engine_wash_strength, strong);
	XVT_ASSERT_INT_EQ(g_players[1].engine_wash_source_obj_idx, 1);
	XVT_ASSERT_TRUE(find_line("combat.engine_wash") != NULL);
	wash_at(0, -1000, 0);
	XVT_ASSERT_INT_EQ(g_players[1].engine_wash_strength, strong);
	/* A stronger one takes over. */
	g_players[1].engine_wash_strength = 1;
	g_players[1].engine_wash_source_obj_idx = -1;
	wash_at(0, -300, 0);
	XVT_ASSERT_INT_EQ(g_players[1].engine_wash_source_obj_idx, 1);
}

/* Comment: a Super Star Destroyer's wash is cut to 3/32 and placed
 * differently (nothing above its engine), and a Calamari Cruiser's lower
 * engines pass over victims above them. */
static void check_wash_large_ship_cases(void)
{
	/* Super Star Destroyer: damage below the engine, nothing above. */
	wash_world(SUPER_TYPE);
	mesh_box(0, -3000, -3000, -3000, 3000, 3000, 3000, 0, 0);
	g_object_type_table[SUPER_TYPE].max_bounds_extent = 30000;
	g_test_objects[1].object_type = SUPER_TYPE;
	int below = wash_at(0, -3500, -100);
	XVT_ASSERT_TRUE(below > 0);
	XVT_ASSERT_INT_EQ(wash_at(0, -3500, 3500), 0);

	/* Calamari Cruiser: engines under its middle pass over a victim above
	 * them. */
	wash_world(CALAMARI_TYPE);
	mesh_box(0, -50, -100, -50, 50, 100, 50, 0, -10);
	XVT_ASSERT_TRUE(wash_at(0, -300, 0) > 0);
	XVT_ASSERT_INT_EQ(wash_at(0, -300, 80), 0);
	/* An engine above the middle does not. */
	mesh_box(0, -50, -100, -50, 50, 100, 50, 0, 10);
	XVT_ASSERT_TRUE(wash_at(0, -300, 80) > 0);
}

/* The hostile craft 1 of hostile_type and extent extent, with working
 * systems, axes along the world's, its hangar entry at the origin; craft 0
 * at (x, y, z) with its weapons not jammed and chaff out. */
static void jam_world(int hostile_type, int extent, int x, int y, int z)
{
	fresh_world();
	g_object_type_table[hostile_type].max_bounds_extent = extent;
	g_test_objects[1].object_type = (uint8_t)hostile_type;
	g_test_craft[1].working_subsystems = 1;
	g_test_craft[1].model_index = TEST_MODEL;
	g_test_mobiles[1].cached_side_x = FORWARD_X;
	g_test_mobiles[1].cached_fwd_y = FORWARD_X;
	g_test_mobiles[1].cached_up_z = FORWARD_X;
	memset(&g_model_defs[TEST_MODEL].hangar_points, 0,
	       sizeof g_model_defs[TEST_MODEL].hangar_points);
	place_rel(1, 0, 0, 0);
	place_rel(0, x, y, z);
	g_test_craft[0].beam_effect_accum[2] = 7;
	g_test_craft[0].chaff_active_seconds = 5;
}

static int is_jammed(void)
{
	return g_test_craft[0].beam_effect_accum[2] == JAMMED;
}

/* Comment: a craft at a hostile craft's hangar has its weapons jammed
 * (beam_effect_accum[2] 163,840, chaff ended): at an X/7 factory within
 * 4,096 units; elsewhere within a sixth of the extent of its hangar entry
 * point; never above an Imperial or Victory Star Destroyer or an Interdictor;
 * near a Super Star Destroyer's entry point and level with it to three
 * quarters of its height; at a repair yard also near a hangar mesh. */
static void check_jam_near_hangar(void)
{
	/* X/7 factory: the edge is 4,096. */
	jam_world(X7_FACTORY_TYPE, 20000, 4096, 0, 0);
	collide_apply_hostile_proximity_weapon_disruption(0, 1);
	XVT_ASSERT_TRUE(is_jammed());
	XVT_ASSERT_INT_EQ(g_test_craft[0].chaff_active_seconds, 0);
	XVT_ASSERT_TRUE(find_line("combat.weapons_jammed") != NULL);
	jam_world(X7_FACTORY_TYPE, 20000, 4097, 0, 0);
	collide_apply_hostile_proximity_weapon_disruption(0, 1);
	XVT_ASSERT_TRUE(!is_jammed());
	XVT_ASSERT_INT_EQ(g_test_craft[0].chaff_active_seconds, 5);

	/* Another craft: a sixth of its extent from the entry point. */
	jam_world(TEST_FIGHTER_TYPE, 6000, 900, 0, 0);
	collide_apply_hostile_proximity_weapon_disruption(0, 1);
	XVT_ASSERT_TRUE(is_jammed());
	jam_world(TEST_FIGHTER_TYPE, 6000, 1100, 0, 0);
	collide_apply_hostile_proximity_weapon_disruption(0, 1);
	XVT_ASSERT_TRUE(!is_jammed());
	/* The entry point is not the craft's center. */
	jam_world(TEST_FIGHTER_TYPE, 6000, 3900, 0, 0);
	g_model_defs[TEST_MODEL].hangar_points.inside.side = 3000;
	collide_apply_hostile_proximity_weapon_disruption(0, 1);
	XVT_ASSERT_TRUE(is_jammed());

	/* Beyond twice the extent, nothing; likewise a hostile without
	 * working systems or a mobile_object. */
	jam_world(X7_FACTORY_TYPE, 1000, 2001, 0, 0);
	collide_apply_hostile_proximity_weapon_disruption(0, 1);
	XVT_ASSERT_TRUE(!is_jammed());
	jam_world(X7_FACTORY_TYPE, 20000, 100, 0, 0);
	g_test_craft[1].working_subsystems = 0;
	collide_apply_hostile_proximity_weapon_disruption(0, 1);
	XVT_ASSERT_TRUE(!is_jammed());
	jam_world(X7_FACTORY_TYPE, 20000, 100, 0, 0);
	g_test_objects[TEST_MINE].object_type = X7_FACTORY_TYPE;
	place_rel(TEST_MINE, 0, 0, 0);
	collide_apply_hostile_proximity_weapon_disruption(0, TEST_MINE);
	XVT_ASSERT_TRUE(!is_jammed());

	/* Never above these Star Destroyers' hangars, though below is fine. */
	static const int destroyers[] = {IMPERIAL_TYPE, VICTORY_TYPE,
					 INTERDICTOR_TYPE};
	for (size_t i = 0; i < sizeof destroyers / sizeof destroyers[0]; ++i) {
		jam_world(destroyers[i], 6000, 0, 0, 500);
		collide_apply_hostile_proximity_weapon_disruption(0, 1);
		XVT_ASSERT_TRUE(!is_jammed());
		jam_world(destroyers[i], 6000, 0, 0, -500);
		collide_apply_hostile_proximity_weapon_disruption(0, 1);
		XVT_ASSERT_TRUE(is_jammed());
	}
}

/* Comment: at a Super Star Destroyer the craft must be near the entry point
 * (an eighth of the extent) and level with it to three quarters of the
 * point's height. */
static void check_jam_at_super_destroyer(void)
{
	/* The entry point 400 below the center: level within 300. */
	jam_world(SUPER_TYPE, 8000, 0, 0, -400);
	g_model_defs[TEST_MODEL].hangar_points.inside.up = -400;
	collide_apply_hostile_proximity_weapon_disruption(0, 1);
	XVT_ASSERT_TRUE(is_jammed());
	jam_world(SUPER_TYPE, 8000, 0, 0, -400 + 301);
	g_model_defs[TEST_MODEL].hangar_points.inside.up = -400;
	collide_apply_hostile_proximity_weapon_disruption(0, 1);
	XVT_ASSERT_TRUE(!is_jammed());
	jam_world(SUPER_TYPE, 8000, 0, 0, -400 + 299);
	g_model_defs[TEST_MODEL].hangar_points.inside.up = -400;
	collide_apply_hostile_proximity_weapon_disruption(0, 1);
	XVT_ASSERT_TRUE(is_jammed());
	/* Level, but not near: an eighth of 8,000 is 1,000. */
	jam_world(SUPER_TYPE, 8000, 1100, 0, -400);
	g_model_defs[TEST_MODEL].hangar_points.inside.up = -400;
	collide_apply_hostile_proximity_weapon_disruption(0, 1);
	XVT_ASSERT_TRUE(!is_jammed());
	/* Above the middle is not excluded for this one. */
	jam_world(SUPER_TYPE, 8000, 0, 0, 300);
	g_model_defs[TEST_MODEL].hangar_points.inside.up = 300;
	collide_apply_hostile_proximity_weapon_disruption(0, 1);
	XVT_ASSERT_TRUE(is_jammed());
}

/* Comment: at a repair yard the craft is near the entry point (a sixth of
 * the extent) or near a hangar mesh's center. */
static void check_jam_at_repair_yard(void)
{
	static const int types[2] = {MESH_COMPONENT_01_MAIN_HULL,
				     MESH_COMPONENT_16_HANGAR};
	static const int center_x[2] = {0, 2500};
	static const int flags[2] = {0, 0};

	/* Near the entry point, no mesh needed. */
	jam_world(REPAIR_YARD_TYPE, 6000, 3000, 0, 0);
	g_model_defs[TEST_MODEL].hangar_points.inside.side = 3000;
	collide_apply_hostile_proximity_weapon_disruption(0, 1);
	XVT_ASSERT_TRUE(is_jammed());

	/* Away from it, but at the hangar mesh: jammed. */
	jam_world(REPAIR_YARD_TYPE, 6000, 2500, 0, 0);
	g_model_defs[TEST_MODEL].hangar_points.inside.side = -3000;
	g_craft_model_type = REPAIR_YARD_TYPE;
	craft_model(2, types, center_x, flags);
	collide_apply_hostile_proximity_weapon_disruption(0, 1);
	XVT_ASSERT_TRUE(is_jammed());

	/* Away from both: not. */
	jam_world(REPAIR_YARD_TYPE, 6000, 0, 0, 0);
	g_model_defs[TEST_MODEL].hangar_points.inside.side = -3000;
	g_craft_model_type = REPAIR_YARD_TYPE;
	craft_model(2, types, center_x, flags);
	collide_apply_hostile_proximity_weapon_disruption(0, 1);
	XVT_ASSERT_TRUE(!is_jammed());

	/* A yard whose model has no hangar mesh: not. */
	jam_world(REPAIR_YARD_TYPE, 6000, 0, 0, 0);
	g_model_defs[TEST_MODEL].hangar_points.inside.side = -3000;
	g_craft_model_type = REPAIR_YARD_TYPE;
	craft_model(1, types, center_x, flags);
	collide_apply_hostile_proximity_weapon_disruption(0, 1);
	XVT_ASSERT_TRUE(!is_jammed());
}

/* ------------------------------------------------------------------------ */
/* collide_collisions: the pairs a step tests. */

/* Slot owner's proximity list holds slot candidate, due now; the shot-free
 * pairs all start from this. */
static void list_holds(int owner, int candidate)
{
	struct mobile_object_proximity_list *list =
		&g_test_mobiles[owner].proximity_list;
	list->count = 1;
	list->obj_idx[0] = (uint16_t)candidate;
	list->contact_ticks[0] = 0;
}

/* Craft 0, flown by the local player 0 and facing +X, and craft 1, a
 * computer-flown starfighter 100 units off, neither moving; collisions are
 * on; each deals 300 (craft 0) or 200 (craft 1) when it rams. Shields 1,000
 * front and 500 rear on both, a deep hull. */
static void craft_meet(void)
{
	fresh_world();
	player_flies(0, 0);
	place(0, 0, 0, 0);
	place(1, 100, 0, 0);
	g_test_mobiles[1].prev_world_x = 100;
	g_test_mobiles[0].cached_fwd_x = FORWARD_X;
	g_test_mobiles[0].damage_amount = 300;
	g_test_mobiles[1].damage_amount = 200;
	g_test_mobiles[0].speed = 50;
	g_test_mobiles[1].speed = 20;
	for (int slot = 0; slot < 2; ++slot) {
		g_test_craft[slot].shield_energy[0] = 1000;
		g_test_craft[slot].shield_energy[1] = 500;
		g_test_craft[slot].hull_max = 100000;
	}
	g_flight_mission_state.collisions_enabled = 1;
	list_holds(0, 1);
}

/* Comment: a player's craft against a mobile object uses the swept pair
 * test; a hit on a starfighter, transport or utility vehicle bounces both
 * (each records the other as its impact object), and, with collisions on,
 * collide_damagecraft damages both: the other from the player's craft, the
 * player's from the other on the rear shield when the other moves against
 * the way the player faces. */
static void check_collisions_player_meets_craft(void)
{
	craft_meet();
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.impact_obj_idx, 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].ai_flight.impact_obj_idx, 0);
	/* They close at 50 less 20, side by side: each spins at 100 times
	 * that. */
	XVT_ASSERT_INT_EQ(abs_int(g_test_mobiles[0].roll_impulse_rate), 3000);
	XVT_ASSERT_INT_EQ(abs_int(g_test_mobiles[1].roll_impulse_rate), 3000);
	XVT_ASSERT_TRUE(find_line("combat.craft_bounced") != NULL);
	/* The player is told of the collision. */
	XVT_ASSERT_INT_EQ(count_lines("msg.message_built"), 1);
	/* Each takes the other's ram on its front shield. */
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 300);
	XVT_ASSERT_TRUE(line_holds("combat.damage_taken", "object=0 source=1"));
	XVT_ASSERT_TRUE(line_holds("combat.damage_taken", "side=0"));

	/* The other moving back against the player's heading: the rear
	 * shield takes it. */
	craft_meet();
	g_test_mobiles[1].prev_world_x = 110;
	collide_collisions();
	/* The player's shields then even out, so read the side from the log. */
	XVT_ASSERT_TRUE(line_holds("combat.damage_taken", "object=0 source=1"));
	XVT_ASSERT_TRUE(line_holds("combat.damage_taken", "side=1"));

	/* Collisions off: the bounce, no damage. */
	craft_meet();
	g_flight_mission_state.collisions_enabled = 0;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.impact_obj_idx, 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000);
	XVT_ASSERT_INT_EQ(g_test_craft[0].shield_energy[0], 1000);

	/* Collisions off, but the other is a starship, platform or
	 * freighter: no bounce, and damage. */
	static const uint8_t large[] = {CRAFT_GENUS_STARSHIP,
					CRAFT_GENUS_PLATFORM,
					CRAFT_GENUS_FREIGHTER};
	for (size_t i = 0; i < sizeof large; ++i) {
		craft_meet();
		g_flight_mission_state.collisions_enabled = 0;
		g_test_objects[1].genus_id = large[i];
		collide_collisions();
		XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.impact_obj_idx,
				  UINT16_MAX);
		XVT_ASSERT_TRUE(g_test_craft[1].shield_energy[0] < 1000);
		XVT_ASSERT_TRUE(g_test_craft[0].shield_energy[0] < 1000);
	}

	/* The player's axes are recomputed before the damage side is read. */
	craft_meet();
	g_test_mobiles[0].orient_matrix_dirty = 1;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_mobiles[0].orient_matrix_dirty, 0);
	XVT_ASSERT_TRUE(g_test_craft[1].shield_energy[0] < 1000);

	/* Out of reach: nothing. */
	craft_meet();
	place(1, 5000, 0, 0);
	g_test_mobiles[1].prev_world_x = 5000;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.impact_obj_idx, UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000);
}

/* Comment: pairs the craft's activity or order wants left alone go back in
 * the list untested: a craft the player's craft has just bounced off, and a
 * craft whose order is to board the player's craft and give it cargo. */
static void check_collisions_player_pair_left_alone(void)
{
	craft_meet();
	g_test_craft[0].ai_flight.impact_obj_idx = 1;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].ai_flight.impact_obj_idx, UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000);
	XVT_ASSERT_INT_EQ(g_test_craft[0].shield_energy[0], 1000);
	/* It stays in the list. */
	XVT_ASSERT_INT_EQ(g_test_mobiles[0].proximity_list.count, 1);
	XVT_ASSERT_INT_EQ(g_test_mobiles[0].proximity_list.obj_idx[0], 1);

	craft_meet();
	g_mission_flight_groups[1].fg.orders[0].order =
		MISSION_ORDER_BOARD_AND_GIVE_CARGO;
	g_test_craft[1].ai_controller.target_obj_idx = 0;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000);
	XVT_ASSERT_INT_EQ(g_test_mobiles[0].proximity_list.count, 1);
	/* Aimed at some other craft, it is tested. */
	craft_meet();
	g_mission_flight_groups[1].fg.orders[0].order =
		MISSION_ORDER_BOARD_AND_GIVE_CARGO;
	g_test_craft[1].ai_controller.target_obj_idx = 1;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 300);
}

/* Static slot 5 holds a mine at (1,000, 0, 0); craft 0 flies from the origin
 * through it to (2,000, 0, 0); its list holds the mine. */
static void craft_meets_mine(void)
{
	fresh_world();
	place_mine(1000, 0, 0);
	place(0, 2000, 0, 0);
	g_test_craft[0].shield_energy[0] = 5000;
	g_test_craft[0].shield_energy[1] = 5000;
	g_test_craft[0].hull_max = 100000;
	g_test_craft[0].ai_controller.target_obj_idx = TEST_MINE;
	g_object_type_table[TEST_MINE_TYPE].max_bounds_extent = 200;
	g_flight_mission_state.collisions_enabled = 1;
	list_holds(0, TEST_MINE);
}

/* Comment: a player's craft against a static object bounces off it and,
 * with collisions on, destroys the mine (static_apply_static_hit, unless a
 * flight group has status 20) and takes damage from it. */
static void check_collisions_player_meets_static(void)
{
	craft_meets_mine();
	player_flies(0, 0);
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.impact_obj_idx, TEST_MINE);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_MINE].object_type, 0);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[2]
			.outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED],
		1);
	/* A protected flight group: the mine stays, and deals 4 times its
	 * extent to the craft. */
	for (int group = 0; group < 2; ++group) {
		craft_meets_mine();
		player_flies(0, 0);
		g_mission_flight_groups[group == 0 ? 0 : 2].fg.status1 = 20;
		collide_collisions();
		XVT_ASSERT_INT_EQ(g_test_objects[TEST_MINE].object_type,
				  TEST_MINE_TYPE);
		XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.impact_obj_idx,
				  TEST_MINE);
		/* The craft's own group protected, the craft takes nothing; the
		 * mine's, it still does. */
		XVT_ASSERT_TRUE(
			line_holds(group == 0 ? "combat.damage_refused"
					      : "combat.damage_taken",
				   group == 0 ? "reason=\"victim_protected\""
					      : "amount=800"));
	}

	/* Collisions off: the bounce only. */
	craft_meets_mine();
	player_flies(0, 0);
	g_flight_mission_state.collisions_enabled = 0;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.impact_obj_idx, TEST_MINE);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_MINE].object_type,
			  TEST_MINE_TYPE);
	XVT_ASSERT_INT_EQ(g_test_craft[0].shield_energy[0], 5000);

	/* A craft that misses the mine does nothing. */
	craft_meets_mine();
	player_flies(0, 0);
	place_mine(1000, 9000, 0);
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.impact_obj_idx, UINT16_MAX);
}

/* Comment: a computer-flown starfighter is tested only against static
 * objects: damage when collisions are on, else a bounce. One aimed at some
 * other object passes a mine by (static_test_swept_static_collision). */
static void check_collisions_ai_craft_meets_static(void)
{
	craft_meets_mine();
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[0].shield_energy[0], 5000 - 4 * 200);
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.impact_obj_idx, UINT16_MAX);

	craft_meets_mine();
	g_flight_mission_state.collisions_enabled = 0;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[0].shield_energy[0], 5000);
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.impact_obj_idx, TEST_MINE);

	craft_meets_mine();
	g_test_craft[0].ai_controller.target_obj_idx = 1;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[0].shield_energy[0], 5000);
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.impact_obj_idx, UINT16_MAX);

	/* Another craft in the list is not tested at all. */
	craft_meet();
	g_test_objects[0].player_owner_idx = -1;
	g_players[0].object_index = -1;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[0].shield_energy[0], 1000);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000);
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.impact_obj_idx, UINT16_MAX);
}

/* Craft 0, a computer-flown starship, rams craft 1 100 units from it. */
static void starship_meets_craft(void)
{
	craft_meet();
	g_test_objects[0].player_owner_idx = -1;
	g_test_objects[0].genus_id = CRAFT_GENUS_STARSHIP;
	g_players[0].object_index = -1;
	g_test_mobiles[0].damage_amount = 800;
}

static int untouched(void)
{
	return g_test_craft[0].shield_energy[0] == 1000 &&
	       g_test_craft[1].shield_energy[0] == 1000;
}

/* Comment: an AI freighter, starship or platform is tested against craft
 * that are not docking, launching, entering a hangar, boarding, jumping or
 * dropping off, and a hit damages both. The pairs for those goings-on go
 * back in the list untested. */
static void check_collisions_capital_ship_meets_craft(void)
{
	/* A plain hit: the craft takes 800; the starship takes the craft's
	 * 200 less a sixteenth. */
	starship_meets_craft();
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 800);
	XVT_ASSERT_INT_EQ(g_test_craft[0].shield_energy[0], 1000 - 200 / 16);

	/* A starship boarding some other craft is not passed over. */
	starship_meets_craft();
	g_test_craft[0].ai_controller.maneuver_mode = AI_MANEUVER_MODE_BOARD;
	g_test_craft[0].ai_controller.target_obj_idx = 0;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 800);

	/* A platform and a freighter the same. */
	static const uint8_t large[] = {CRAFT_GENUS_PLATFORM,
					CRAFT_GENUS_FREIGHTER};
	for (size_t i = 0; i < sizeof large; ++i) {
		starship_meets_craft();
		g_test_objects[0].genus_id = large[i];
		collide_collisions();
		XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 800);
		XVT_ASSERT_TRUE(g_test_craft[0].shield_energy[0] < 1000);
	}

	/* Out of reach. */
	starship_meets_craft();
	place(1, 5000, 0, 0);
	g_test_mobiles[1].prev_world_x = 5000;
	collide_collisions();
	XVT_ASSERT_TRUE(untouched());

	/* The ship's own goings-on. */
	for (int own = 0; own < 4; ++own) {
		starship_meets_craft();
		if (own == 0) {
			g_test_craft[0].ai_controller.maneuver_mode =
				AI_MANEUVER_MODE_DROPOFF;
		} else if (own == 1) {
			g_test_craft[0].carrier_obj_idx = 1;
		} else if (own == 2) {
			g_test_craft[0].carried_object_index = 1;
		} else {
			g_test_craft[0].ai_controller.maneuver_mode =
				AI_MANEUVER_MODE_BOARD;
			g_test_craft[0].ai_controller.target_obj_idx = 1;
		}
		collide_collisions();
		XVT_ASSERT_TRUE(untouched());
	}

	/* The craft's: leaving or entering a hangar, arriving from hyperspace,
	 * a leader's hangar activity, a carrier, boarding, jumping, dropping
	 * off. */
	static const uint8_t activities[] = {
		AI_ACTIVITY_LEAVING_HANGAR,
		AI_ACTIVITY_ARRIVING_FROM_HYPERSPACE,
		AI_ACTIVITY_ENTERING_HANGAR};
	for (size_t i = 0; i < sizeof activities / sizeof activities[0]; ++i) {
		starship_meets_craft();
		g_test_craft[1].ai_controller.activity = activities[i];
		collide_collisions();
		XVT_ASSERT_TRUE(untouched());
	}
	for (size_t i = 0; i < 2; ++i) {
		starship_meets_craft();
		g_test_craft[1].ai_controller.activity =
			AI_ACTIVITY_GOING_HOME_EVADING;
		g_test_craft[1].leader_obj_idx = 0;
		g_test_craft[0].ai_controller.activity =
			i == 0 ? AI_ACTIVITY_LEAVING_HANGAR
			       : AI_ACTIVITY_ENTERING_HANGAR;
		collide_collisions();
		XVT_ASSERT_TRUE(untouched());
	}
	/* A leader that is doing something else leaves the follower tested. */
	starship_meets_craft();
	g_test_craft[1].ai_controller.activity = AI_ACTIVITY_GOING_HOME_EVADING;
	g_test_craft[1].leader_obj_idx = 0;
	g_test_craft[0].ai_controller.activity = AI_ACTIVITY_NONE;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 800);
	/* So does going home without a leader, the leader's hangar activity
	 * notwithstanding. */
	starship_meets_craft();
	g_test_craft[1].ai_controller.activity = AI_ACTIVITY_GOING_HOME_EVADING;
	g_test_craft[1].leader_obj_idx = UINT8_MAX;
	g_test_craft[0].ai_controller.activity = AI_ACTIVITY_ENTERING_HANGAR;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 800);
	for (int mode = 0; mode < 4; ++mode) {
		starship_meets_craft();
		if (mode == 0) {
			g_test_craft[1].carrier_obj_idx = 0;
		} else if (mode == 1) {
			g_test_craft[1].ai_controller.maneuver_mode =
				AI_MANEUVER_MODE_BOARD;
		} else if (mode == 2) {
			g_test_craft[1].object_kind =
				CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE;
		} else {
			g_test_craft[1].ai_controller.maneuver_mode =
				AI_MANEUVER_MODE_DROPOFF;
		}
		collide_collisions();
		XVT_ASSERT_TRUE(untouched());
	}
	/* The controls: a craft of kind active that stalled out of a jump, or
	 * one flying out to its jump point at activity 6, is tested. */
	starship_meets_craft();
	g_test_craft[1].object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 800);
	starship_meets_craft();
	g_test_craft[1].ai_controller.activity =
		AI_ACTIVITY_LEAVING_BY_HYPERSPACE;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 800);
}

/* Shot 2, a laser of craft 0's, moving through craft 1 at (20,000, 0, 0);
 * its list holds craft 1, due now. */
static void laser_meets_craft(void)
{
	laser_at_craft();
	g_test_craft[1].hull_max = 100000;
	list_holds(TEST_SHOT, 1);
}

/* Comment: a shot that hits a craft goes to collide_laserhitcraft, after
 * the hit is counted for its firer (mission_record_projectile_hit_stats);
 * a warhead from a player's flight group is logged as struck. A shot that
 * misses, or that a boarding craft is waiting for, is not. */
static void check_collisions_shot_meets_craft(void)
{
	laser_meets_craft();
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 250);
	XVT_ASSERT_INT_EQ(g_test_craft[0].weapon_stats.laser_hits_scored, 1);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].object_type, 131);
	XVT_ASSERT_INT_EQ(g_test_craft[1].attacked_by_team[0] & 1, 1);
	XVT_ASSERT_TRUE(find_line("battle.warhead_struck") == NULL);

	/* A warhead of a player's group: struck, and a warhead hit. */
	laser_meets_craft();
	g_test_objects[TEST_SHOT].object_type =
		WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE;
	g_mission_flight_groups[0].player_owner_idx = 3;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[0].weapon_stats.warhead_hits_scored, 1);
	XVT_ASSERT_TRUE(
		line_holds("battle.warhead_struck", "by=3 warhead=144"));
	XVT_ASSERT_TRUE(line_holds("battle.warhead_struck", "target=1"));
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].object_type, 129);

	/* A warhead of a computer-flown group is not logged. */
	laser_meets_craft();
	g_test_objects[TEST_SHOT].object_type =
		WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[0].weapon_stats.warhead_hits_scored, 1);
	XVT_ASSERT_TRUE(find_line("battle.warhead_struck") == NULL);

	/* A miss. */
	laser_meets_craft();
	place(1, 20000, 9000, 0);
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].object_type,
			  PROJECTILE_OBJECT_TYPE_REBEL_LASER);

	/* A player's shot, with the craft boarding to give to it: left. */
	laser_meets_craft();
	player_flies(0, 2);
	g_mission_flight_groups[1].fg.orders[0].order =
		MISSION_ORDER_BOARD_AND_GIVE_CARGO;
	g_test_craft[1].ai_controller.maneuver_mode = AI_MANEUVER_MODE_BOARD;
	g_test_craft[1].ai_controller.maneuver_phase = 2;
	g_test_craft[1].ai_controller.target_obj_idx = TEST_SHOT;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000);
	XVT_ASSERT_INT_EQ(g_test_mobiles[TEST_SHOT].proximity_list.count, 1);
	/* In another phase, it is tested. */
	laser_meets_craft();
	player_flies(0, 2);
	g_mission_flight_groups[1].fg.orders[0].order =
		MISSION_ORDER_BOARD_AND_GIVE_CARGO;
	g_test_craft[1].ai_controller.maneuver_mode = AI_MANEUVER_MODE_BOARD;
	g_test_craft[1].ai_controller.maneuver_phase = 3;
	g_test_craft[1].ai_controller.target_obj_idx = TEST_SHOT;
	collide_collisions();
	XVT_ASSERT_TRUE(g_test_craft[1].shield_energy[0] < 1000);
}

/* Two shots of craft 0 (slot 2) and craft 1 (slot 3) crossing at (1,000, 0,
 * 0); shot 2's list holds shot 3. */
static void shots_meet(uint8_t type, uint8_t other_type)
{
	shots_cross(type, other_type);
}

/* Comment: a warhead that hits another warhead counts the hit for the
 * warhead aimed at the other (the owner's firer first); both become
 * explosions; a cannon shot meeting a cannon shot counts for the owner's
 * and the other explodes as a warhead does. */
static void check_collisions_shot_meets_shot(void)
{
	/* The owner's warhead is aimed at the other: the owner's firer
	 * scores. */
	fresh_world();
	g_object_type_table[WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE]
		.max_bounds_extent = 400;
	g_object_type_table[WARHEAD_OBJECT_TYPE_PROTON_TORPEDO]
		.max_bounds_extent = 400;
	place_shot(TEST_SHOT, WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 0, 0, 0,
		   0, 2000, 0, 0);
	place_shot(TEST_OTHER_SHOT, WARHEAD_OBJECT_TYPE_PROTON_TORPEDO, 1, 1000,
		   0, 0, 1000, 0, 0);
	g_test_guidance[0].target_obj_idx = TEST_OTHER_SHOT;
	list_holds(TEST_SHOT, TEST_OTHER_SHOT);
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[0].weapon_stats.warhead_hits_scored, 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].weapon_stats.warhead_hits_scored, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].object_type, 129);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_OTHER_SHOT].object_type, 129);

	/* The other's warhead is aimed at the owner: the other's firer
	 * scores. */
	fresh_world();
	g_object_type_table[WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE]
		.max_bounds_extent = 400;
	g_object_type_table[WARHEAD_OBJECT_TYPE_PROTON_TORPEDO]
		.max_bounds_extent = 400;
	place_shot(TEST_SHOT, WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 0, 0, 0,
		   0, 2000, 0, 0);
	place_shot(TEST_OTHER_SHOT, WARHEAD_OBJECT_TYPE_PROTON_TORPEDO, 1, 1000,
		   0, 0, 1000, 0, 0);
	g_test_guidance[1].target_obj_idx = TEST_SHOT;
	list_holds(TEST_SHOT, TEST_OTHER_SHOT);
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[0].weapon_stats.warhead_hits_scored, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].weapon_stats.warhead_hits_scored, 1);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].object_type, 129);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_OTHER_SHOT].object_type, 129);

	/* Aimed at neither: nobody scores; both explode. */
	fresh_world();
	g_object_type_table[WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE]
		.max_bounds_extent = 400;
	g_object_type_table[WARHEAD_OBJECT_TYPE_PROTON_TORPEDO]
		.max_bounds_extent = 400;
	place_shot(TEST_SHOT, WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 0, 0, 0,
		   0, 2000, 0, 0);
	place_shot(TEST_OTHER_SHOT, WARHEAD_OBJECT_TYPE_PROTON_TORPEDO, 1, 1000,
		   0, 0, 1000, 0, 0);
	list_holds(TEST_SHOT, TEST_OTHER_SHOT);
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[0].weapon_stats.warhead_hits_scored, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].weapon_stats.warhead_hits_scored, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].object_type, 129);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_OTHER_SHOT].object_type, 129);

	/* Two cannon shots: the owner's hit counts and it becomes a laser
	 * impact; the other explodes as a warhead does. */
	shots_meet(PROJECTILE_OBJECT_TYPE_REBEL_LASER,
		   PROJECTILE_OBJECT_TYPE_IMPERIAL_LASER);
	XVT_ASSERT_INT_EQ(g_test_craft[0].weapon_stats.laser_hits_scored, 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].weapon_stats.laser_hits_scored, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].object_type, 131);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_OTHER_SHOT].object_type, 129);
	shots_meet(PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER,
		   PROJECTILE_OBJECT_TYPE_IMPERIAL_LASER);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].object_type, 132);
	XVT_ASSERT_TRUE(find_line("combat.shots_collided") != NULL);
}

/* Comment: a pair where either object is younger than 3 simulated seconds
 * is passed over (untested, nothing counted down for a young candidate)
 * unless a shot is in it; a candidate whose time has not come is not
 * tested; one that is gone, or an explosion, is dropped from the list. */
static void check_collisions_young_and_not_due(void)
{
	/* A young candidate: untouched, and its due time not counted down. */
	craft_meet();
	g_test_mobiles[1].seconds_alive = 2;
	g_test_mobiles[0].proximity_list.contact_ticks[0] = 0;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000);
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.impact_obj_idx, UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_test_mobiles[0].proximity_list.contact_ticks[0], 0);
	/* At 3 seconds it is old enough. */
	craft_meet();
	g_test_mobiles[1].seconds_alive = 3;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 300);

	/* A young owner: the pair is put back with a fresh due time. */
	craft_meet();
	place(1, 3000, 0, 0);
	g_test_mobiles[1].prev_world_x = 3000;
	g_test_mobiles[0].seconds_alive = 2;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000);
	XVT_ASSERT_INT_EQ(g_test_mobiles[0].proximity_list.count, 1);
	XVT_ASSERT_INT_EQ(g_test_mobiles[0].proximity_list.contact_ticks[0],
			  expected_contact_ticks(3000 - 2 * TEST_FIGHTER_EXTENT,
						 TEST_MODEL_MAX_SPEED,
						 TEST_MODEL_MAX_SPEED));

	/* A young owner whose pair would hit is passed over. */
	craft_meet();
	g_test_mobiles[0].seconds_alive = 0;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000);
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.impact_obj_idx, UINT16_MAX);

	/* A candidate not yet due: counted down by the step, not tested. */
	craft_meet();
	g_test_mobiles[0].proximity_list.contact_ticks[0] = 5;
	g_elapsed_ticks = 2;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_mobiles[0].proximity_list.contact_ticks[0], 3);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000);
	/* Due after one more. */
	g_test_mobiles[0].proximity_list.contact_ticks[0] = 2;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 300);

	/* A candidate that is gone, or an explosion, is dropped from the list
	 * and the next is tested. */
	for (int explosion = 0; explosion < 2; ++explosion) {
		craft_meet();
		g_test_objects[TEST_MINE].object_type = 0;
		struct mobile_object_proximity_list *list =
			&g_test_mobiles[0].proximity_list;
		list->count = 2;
		list->obj_idx[0] = TEST_SHOT;
		list->contact_ticks[0] = 0;
		list->obj_idx[1] = 1;
		list->contact_ticks[1] = 0;
		if (explosion) {
			g_test_objects[TEST_SHOT].object_type =
				TEST_EXPLOSION_TYPE;
			g_test_objects[TEST_SHOT].genus_id =
				CRAFT_GENUS_EXPLOSION;
		}
		collide_collisions();
		XVT_ASSERT_INT_EQ(list->count, 1);
		XVT_ASSERT_INT_EQ(list->obj_idx[0], 1);
		XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 300);
	}
}

/* ------------------------------------------------------------------------ */
/* collide_collisions: what a player's craft does each step. */

/* Comment: an object that is gone or an explosion is passed over, and so is
 * a player's craft in hyperspace (phase 2): the step's countdown of its
 * proximity list does not run for them. */
static void check_collisions_skips_hidden_objects(void)
{
	for (int skipped = 0; skipped < 4; ++skipped) {
		fresh_world();
		for (int slot = 0; slot < TEST_MAIN_END; ++slot) {
			g_test_mobiles[slot].proximity_list.rebuild_ticks = 5;
		}
		if (skipped == 1) {
			g_test_objects[1].object_type = 0;
		} else if (skipped == 2) {
			g_test_objects[1].genus_id = CRAFT_GENUS_EXPLOSION;
		} else if (skipped == 3) {
			player_flies(1, 1);
			g_players[1].hyperspace_phase = 2;
		}
		collide_collisions();
		/* Craft 0 is always counted down. */
		XVT_ASSERT_INT_EQ(
			g_test_mobiles[0].proximity_list.rebuild_ticks, 4);
		XVT_ASSERT_INT_EQ(
			g_test_mobiles[1].proximity_list.rebuild_ticks,
			skipped == 0 ? 4 : 5);
	}
	/* A player's craft in another phase is counted down. */
	fresh_world();
	player_flies(1, 1);
	g_players[1].hyperspace_phase = 1;
	g_test_mobiles[1].proximity_list.rebuild_ticks = 5;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_mobiles[1].proximity_list.rebuild_ticks, 4);
}

/* Comment: every 29 ticks it clears a player's engine wash and rescans
 * every freighter and starship with working systems. The scan has not come
 * until the game time passes next_engine_wash_check_time. */
static void check_collisions_engine_wash_scan(void)
{
	static const struct {
		uint8_t genus;
		int working;
		int scanned;
	} ships[] = {
		{CRAFT_GENUS_STARSHIP, 1, 1}, {CRAFT_GENUS_FREIGHTER, 1, 1},
		{CRAFT_GENUS_PLATFORM, 1, 0}, {CRAFT_GENUS_TRANSPORT, 1, 0},
		{CRAFT_GENUS_STARSHIP, 0, 0},
	};
	for (size_t i = 0; i < sizeof ships / sizeof ships[0]; ++i) {
		wash_world(TEST_SHIP_TYPE);
		g_test_objects[1].genus_id = ships[i].genus;
		g_test_craft[1].working_subsystems = (uint16_t)ships[i].working;
		player_flies(0, 0);
		place_rel(0, 0, -300, 0);
		/* A third craft slot that holds nothing is passed over. */
		g_active_region_craft_object_slot_end = 3;
		g_game_time = 100;
		g_players[0].next_engine_wash_check_time = 50;
		g_players[0].engine_wash_strength = 9;
		g_players[0].engine_wash_source_obj_idx = 1;
		collide_collisions();
		/* The check is rescheduled 29 ticks on, the wash recomputed. */
		XVT_ASSERT_INT_EQ(g_players[0].next_engine_wash_check_time,
				  129);
		XVT_ASSERT_TRUE(g_players[0].engine_wash_strength != 9);
		if (ships[i].scanned) {
			XVT_ASSERT_TRUE(g_players[0].engine_wash_strength > 0);
			XVT_ASSERT_INT_EQ(
				g_players[0].engine_wash_source_obj_idx, 1);
			XVT_ASSERT_TRUE(
				g_test_craft[0].shield_energy[0] +
					g_test_craft[0].shield_energy[1] <
				1500);
		} else {
			XVT_ASSERT_INT_EQ(g_players[0].engine_wash_strength, 0);
			XVT_ASSERT_INT_EQ(
				g_players[0].engine_wash_source_obj_idx, -1);
			XVT_ASSERT_INT_EQ(
				g_test_craft[0].shield_energy[0] +
					g_test_craft[0].shield_energy[1],
				1500);
		}
	}
	/* Not due yet: nothing is touched. */
	wash_world(TEST_SHIP_TYPE);
	player_flies(0, 0);
	place_rel(0, 0, -300, 0);
	g_game_time = 100;
	g_players[0].next_engine_wash_check_time = 100;
	g_players[0].engine_wash_strength = 9;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_players[0].next_engine_wash_check_time, 100);
	XVT_ASSERT_INT_EQ(g_players[0].engine_wash_strength, 9);
	XVT_ASSERT_INT_EQ(g_test_craft[0].shield_energy[0], 1000);
	g_game_time = 0;
}

/* Player 0 flies craft 0 (team 0); the target is craft 1, 1,000 units off,
 * on team 1 in flight group 1, known to its own team only. */
static void inspection_world(void)
{
	fresh_world();
	player_flies(0, 0);
	place(1, 1000, 0, 0);
	g_test_mobiles[1].prev_world_x = 1000;
	g_players[0].current_target_object_idx = 1;
	g_test_craft[1].identified_order_by_team[1] = 1;
	g_hud_cached_target_object_idx = 0;
}

/* Comment: the current target is inspected, once per team, when within 4
 * times its max_bounds_extent (halved above 3,000): the team's order among
 * those who inspected it, the counts of the inspection for its flight group,
 * the team and the player, and the messages. */
static void check_collisions_inspects_target(void)
{
	inspection_world();
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].identified_order_by_team[0], 2);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[1]
			.outcome_count[FLIGHT_GROUP_OUTCOME_INSPECTED],
		1);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1].team_inspected[0], 1);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
				  .team_fg_inspected_captured_counts[0][0][1],
			  1);
	XVT_ASSERT_INT_EQ(g_players[0].per_mission_kills.num_craft_inspected,
			  1);
	XVT_ASSERT_INT_EQ(g_players[0].per_mission_kills.num_special_inspected,
			  0);
	XVT_ASSERT_INT_EQ(g_hud_cached_target_object_idx, -3);
	XVT_ASSERT_TRUE(line_holds("player.inspection_counted",
				   "slot=0 object=1 fg=1 team=0 order=2"));
	XVT_ASSERT_TRUE(find_line("player.craft_inspected") != NULL);
	/* A starfighter with no goal about it: no message. */
	XVT_ASSERT_INT_EQ(count_lines("msg.message_built"), 0);

	/* Once the team has it, a second step does not count again. */
	collide_collisions();
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[1]
			.outcome_count[FLIGHT_GROUP_OUTCOME_INSPECTED],
		1);

	/* The next team's inspection takes the next order. */
	inspection_world();
	g_test_craft[1].identified_order_by_team[2] = 5;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].identified_order_by_team[0], 6);

	/* A player's craft whose own team (craft 0's mobile team) already
	 * knows the target does not inspect it. */
	inspection_world();
	g_test_craft[1].identified_order_by_team[0] = 1;
	collide_collisions();
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[1]
			.outcome_count[FLIGHT_GROUP_OUTCOME_INSPECTED],
		0);

	/* A target breaking up or exploding is not inspected. */
	for (int kind = 0; kind < 2; ++kind) {
		inspection_world();
		g_test_craft[1].object_kind =
			kind == 0 ? CRAFT_OBJECT_KIND_BREAKING_UP
				  : CRAFT_OBJECT_KIND_EXPLODING;
		collide_collisions();
		XVT_ASSERT_INT_EQ(
			g_mission_fg_stats[1]
				.outcome_count[FLIGHT_GROUP_OUTCOME_INSPECTED],
			0);
	}
}

/* Comment: the inspection range is 4 times the extent: 1,599 units for 400
 * is in, 1,600 is out; above 3,000 the extent is halved first. */
static void check_collisions_inspection_range(void)
{
	static const struct {
		int extent;
		int distance;
		int inspected;
	} cases[] = {
		{400, 1599, 1},	  {400, 1600, 0},  {3000, 11999, 1},
		{3000, 12000, 0}, {4000, 7999, 1}, {4000, 8000, 0},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		inspection_world();
		g_object_type_table[TEST_FIGHTER_TYPE].max_bounds_extent =
			cases[i].extent;
		place(1, cases[i].distance, 0, 0);
		g_test_mobiles[1].prev_world_x = cases[i].distance;
		collide_collisions();
		XVT_ASSERT_INT_EQ(
			g_mission_fg_stats[1]
				.outcome_count[FLIGHT_GROUP_OUTCOME_INSPECTED],
			cases[i].inspected);
	}
}

/* Comment: inspecting a special cargo craft is counted for the group and
 * the player, and the goal score and message follow when a goal asks for
 * the inspection and the player is the local one: the messages are the goal's
 * (357, or 358 for special cargo) and the craft's (142). */
static void check_collisions_inspection_goal_and_messages(void)
{
	/* A goal on the target's group asks for inspection: its completion
	 * message, the craft message, the target description refreshed. */
	inspection_world();
	g_mission_flight_groups[1].fg.goals[0].enabled_teams[0] = 1;
	g_mission_flight_groups[1].fg.goals[0].event_condition =
		MISSION_COND_INSPECTED;
	g_mission_flight_groups[1].fg.goals[0].goal_kind = 0;
	collide_collisions();
	XVT_ASSERT_INT_EQ(count_lines("msg.message_built"), 2);
	XVT_ASSERT_INT_EQ(g_player_flight_transient_timers[0]
				  .target_description_refresh_timer,
			  944);

	/* A bonus goal (kind 2, amount 18) for the inspection pays the player
	 * 250 times its points. In a melee with other players the inspector is
	 * also told its place: one message more. */
	for (int mode = 0; mode < 4; ++mode) {
		/* 0: one player; 1: a melee of two; 2: two players, not a
		 * melee; 3: a melee of one. */
		int melee = mode == 1;
		inspection_world();
		struct flight_group_goal *goal =
			&g_mission_flight_groups[1].fg.goals[0];
		goal->enabled_teams[0] = 1;
		goal->event_condition = MISSION_COND_INSPECTED;
		goal->goal_kind = 2;
		goal->amount = 18;
		goal->points = 2;
		g_flight_player_count = (mode == 1 || mode == 2) ? 2 : 1;
		g_mission_header.mission_type =
			(mode == 1 || mode == 3) ? MISSION_TYPE_MELEE : 0;
		collide_collisions();
		XVT_ASSERT_INT_EQ(g_players[0].mission_stats.mission_score,
				  250 * 2);
		XVT_ASSERT_INT_EQ(count_lines("msg.message_built"),
				  3 + (melee ? 1 : 0));
		XVT_ASSERT_INT_EQ(g_player_flight_transient_timers[0]
					  .target_description_refresh_timer,
				  944);
	}
	g_flight_player_count = 1;
	g_mission_header.mission_type = 0;

	/* A goal for the special cargo of the group: asked for by the special
	 * cargo craft's inspection, with its own message. */
	inspection_world();
	g_mission_flight_groups[1].fg.special_cargo_craft =
		g_test_craft[1].craft_ordinal;
	g_mission_flight_groups[1].fg.goals[0].enabled_teams[0] = 1;
	g_mission_flight_groups[1].fg.goals[0].goal_kind = 0;
	g_mission_flight_groups[1].fg.goals[0].amount =
		GOAL_AMT_ALL_SPECIAL_CARGO;
	collide_collisions();
	XVT_ASSERT_INT_EQ(count_lines("msg.message_built"), 2);
	XVT_ASSERT_INT_EQ(g_player_flight_transient_timers[0]
				  .target_description_refresh_timer,
			  944);

	/* The special cargo craft: counted for the group, team and player. */
	inspection_world();
	g_mission_flight_groups[1].fg.special_cargo_craft =
		g_test_craft[1].craft_ordinal;
	collide_collisions();
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[1]
			.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_INSPECTED],
		1);
	XVT_ASSERT_INT_EQ(g_players[0].per_mission_kills.num_special_inspected,
			  1);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[1].team_special_cargo_inspected[0],
			  1);
	XVT_ASSERT_TRUE(line_holds("player.craft_inspected", "special=1"));

	/* Not a starfighter: the craft message alone, with no goal. */
	inspection_world();
	g_test_objects[1].genus_id = CRAFT_GENUS_STARSHIP;
	collide_collisions();
	XVT_ASSERT_INT_EQ(count_lines("msg.message_built"), 1);
	XVT_ASSERT_INT_EQ(g_player_flight_transient_timers[0]
				  .target_description_refresh_timer,
			  0);

	/* A player's craft that is not the local one gets no message. */
	inspection_world();
	g_test_objects[0].player_owner_idx = 1;
	g_players[1] = g_players[0];
	g_players[0].object_index = -1;
	g_test_objects[1].genus_id = CRAFT_GENUS_STARSHIP;
	collide_collisions();
	XVT_ASSERT_INT_EQ(count_lines("msg.message_built"), 0);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[1]
			.outcome_count[FLIGHT_GROUP_OUTCOME_INSPECTED],
		1);
}

/* Player 0 flies craft 0, alive 45 seconds, whose flight group departs
 * through the hangar of flight group 1, which is craft 1, 5,000 units away
 * with its hangar entry at its center. */
static void hangar_world(void)
{
	fresh_world();
	player_flies(0, 0);
	g_test_mobiles[0].seconds_alive = 45;
	g_mission_flight_groups[0].fg.departure_method = 1;
	g_mission_flight_groups[0].fg.departure_mothership = 1;
	place(1, 5000, 0, 0);
	g_test_mobiles[1].prev_world_x = 5000;
	g_test_mobiles[1].cached_side_x = FORWARD_X;
	g_test_mobiles[1].cached_fwd_y = FORWARD_X;
	g_test_mobiles[1].cached_up_z = FORWARD_X;
	g_test_craft[1].model_index = TEST_MODEL;
	memset(&g_model_defs[TEST_MODEL].hangar_points, 0,
	       sizeof g_model_defs[TEST_MODEL].hangar_points);
}

/* Comment: after 45 simulated seconds of life a player's craft is offered the
 * hangar of its flight group's departure (or alternate) mothership when the
 * hangar entry is within 0x2000 units, or 0x4000 for a starship while the
 * team's first goal status is 1; pending_action_id 2 (enter the hangar), the
 * mothership as its parameter and a prompt time. */
static void check_collisions_hangar_offer(void)
{
	static const struct {
		int distance;
		uint8_t genus;
		int goal_status;
		int offered;
	} ranges[] = {
		{5000, CRAFT_GENUS_STARSHIP, 0, 1},
		{0x1FFF, CRAFT_GENUS_STARFIGHTER, 0, 1},
		{0x2000, CRAFT_GENUS_STARFIGHTER, 0, 0},
		{0x3000, CRAFT_GENUS_STARSHIP, 0, 0},
		{0x3000, CRAFT_GENUS_STARSHIP, 1, 1},
		{0x3FFF, CRAFT_GENUS_STARSHIP, 1, 1},
		{0x4000, CRAFT_GENUS_STARSHIP, 1, 0},
		{0x3000, CRAFT_GENUS_STARFIGHTER, 1, 0},
	};
	for (size_t i = 0; i < sizeof ranges / sizeof ranges[0]; ++i) {
		hangar_world();
		place(1, ranges[i].distance, 0, 0);
		g_test_mobiles[1].prev_world_x = ranges[i].distance;
		g_test_objects[1].genus_id = ranges[i].genus;
		g_flight_mission_state.runtime.team_goal_status[0][0] =
			(uint8_t)ranges[i].goal_status;
		collide_collisions();
		XVT_ASSERT_INT_EQ(g_players[0].pending_action_id,
				  ranges[i].offered ? 2 : 0);
		if (ranges[i].offered) {
			XVT_ASSERT_INT_EQ(g_players[0].pending_action_param, 1);
			XVT_ASSERT_INT_EQ(g_players[0].pending_action_timer,
					  1888);
			XVT_ASSERT_INT_EQ(count_lines("msg.message_built"), 1);
			XVT_ASSERT_TRUE(find_line("player.hangar_offered") !=
					NULL);
		}
	}

	/* Too young; already a pending request; no departure hangar; the
	 * mothership not active; another group; not a player's craft. */
	for (int refusal = 0; refusal < 7; ++refusal) {
		hangar_world();
		if (refusal == 0) {
			g_test_mobiles[0].seconds_alive = 44;
		} else if (refusal == 1) {
			g_players[0].pending_action_id = 5;
		} else if (refusal == 2) {
			g_mission_flight_groups[0].fg.departure_method = 0;
		} else if (refusal == 3) {
			g_test_craft[1].object_kind =
				CRAFT_OBJECT_KIND_DISABLED;
		} else if (refusal == 4) {
			g_mission_flight_groups[0].fg.departure_mothership = 2;
		} else if (refusal == 5) {
			g_test_objects[1].object_type = 0;
		} else {
			g_test_objects[0].player_owner_idx = -1;
			g_players[0].object_index = -1;
		}
		collide_collisions();
		XVT_ASSERT_INT_EQ(g_players[0].pending_action_id,
				  refusal == 1 ? 5 : 0);
	}

	/* The alternate mothership, when the departure one does not offer. */
	hangar_world();
	g_mission_flight_groups[0].fg.departure_method = 0;
	g_mission_flight_groups[0].fg.alternate_mothership_used = 1;
	g_mission_flight_groups[0].fg.alternate_mothership = 1;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_players[0].pending_action_id, 2);
	XVT_ASSERT_TRUE(line_holds("player.hangar_offered", "pass=1"));

	/* A request the local player did not make is not announced. */
	hangar_world();
	g_test_objects[0].player_owner_idx = 1;
	g_players[1] = g_players[0];
	g_players[0].object_index = -1;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_players[1].pending_action_id, 2);
	XVT_ASSERT_INT_EQ(count_lines("msg.message_built"), 0);
}

/* ------------------------------------------------------------------------ */
/* The neighbour lists: collide_populate_mobile_object_proximity_candidates
 * and the rest. */

static int list_has(const struct mobile_object_proximity_list *list, int slot)
{
	for (int entry = 0; entry < list->count; ++entry) {
		if (list->obj_idx[entry] == slot) {
			return 1;
		}
	}
	return 0;
}

/* Four craft slots, 0 to 3, and one shot slot, 4: craft 0 and 3 are
 * computer-flown starfighters of flight group 0, craft 1 a starfighter of
 * group 1 and craft 2 one of group 2; the static slots hold type 100 and
 * type 106 objects; every list is empty. */
static void four_craft_world(void)
{
	fresh_world();
	g_active_region_craft_object_slot_end = 4;
	g_projectile_object_slot_start = 4;
	g_projectile_object_slot_end = 5;
	for (int slot = 2; slot < 4; ++slot) {
		g_test_objects[slot].object_type = TEST_FIGHTER_TYPE;
		g_test_objects[slot].genus_id = CRAFT_GENUS_STARFIGHTER;
		g_test_mobiles[slot].p_craft = &g_test_craft[slot];
		g_test_craft[slot].carrier_obj_idx = UINT16_MAX;
	}
	g_test_objects[2].flight_group_idx = 2;
	g_test_objects[3].flight_group_idx = 0;
	g_test_objects[TEST_MINE].object_type = 100;
	g_test_objects[TEST_MINE + 1].object_type = 106;
	for (int slot = 0; slot < TEST_MAIN_END; ++slot) {
		g_test_mobiles[slot].proximity_list.count = 0;
	}
}

static void populate(int owner)
{
	collide_populate_mobile_object_proximity_candidates(
		&g_test_mobiles[owner].proximity_list, (uint16_t)owner);
}

/* Comment: a player's craft with working systems gets every live craft of
 * another flight group except explosions, and every static object. */
static void check_populate_for_player_craft(void)
{
	four_craft_world();
	player_flies(1, 1);
	g_test_craft[1].working_subsystems = 1;
	g_test_objects[3].object_type = 0;
	populate(1);
	const struct mobile_object_proximity_list *list =
		&g_test_mobiles[1].proximity_list;
	XVT_ASSERT_TRUE(list_has(list, 0));
	XVT_ASSERT_TRUE(list_has(list, 2));
	XVT_ASSERT_TRUE(!list_has(list, 1));
	XVT_ASSERT_TRUE(!list_has(list, 3));
	XVT_ASSERT_TRUE(list_has(list, TEST_MINE));
	/* Even the static object of type 106. */
	XVT_ASSERT_TRUE(list_has(list, TEST_MINE + 1));
	XVT_ASSERT_INT_EQ(list->count, 4);

	/* An explosion and a craft of the player's own group are left out. */
	four_craft_world();
	player_flies(1, 1);
	g_test_craft[1].working_subsystems = 1;
	g_test_objects[2].genus_id = CRAFT_GENUS_EXPLOSION;
	g_test_objects[3].flight_group_idx = 1;
	populate(1);
	XVT_ASSERT_TRUE(list_has(list, 0));
	XVT_ASSERT_TRUE(!list_has(list, 2));
	XVT_ASSERT_TRUE(!list_has(list, 3));
	XVT_ASSERT_INT_EQ(list->count, 3);

	/* No working systems: the static objects only. */
	four_craft_world();
	player_flies(1, 1);
	g_test_craft[1].working_subsystems = 0;
	populate(1);
	XVT_ASSERT_INT_EQ(list->count, 2);
	XVT_ASSERT_TRUE(list_has(list, TEST_MINE));
}

/* Comment: a computer-flown starfighter adds itself to the lists of player
 * craft and of freighters, starships and platforms in other flight groups
 * (not dropping off or carried), and static objects of types 100 to 105 to
 * its own. */
static void check_populate_for_ai_fighter(void)
{
	four_craft_world();
	player_flies(1, 1);
	g_test_objects[2].genus_id = CRAFT_GENUS_STARSHIP;
	g_test_objects[3].genus_id = CRAFT_GENUS_PLATFORM;
	g_test_objects[3].flight_group_idx = 0;
	populate(0);
	/* Itself on the lists of the player's craft and the starship; not the
	 * platform of its own group. */
	XVT_ASSERT_TRUE(list_has(&g_test_mobiles[1].proximity_list, 0));
	XVT_ASSERT_TRUE(list_has(&g_test_mobiles[2].proximity_list, 0));
	XVT_ASSERT_TRUE(!list_has(&g_test_mobiles[3].proximity_list, 0));
	/* Its own: only the static object of type 100. */
	const struct mobile_object_proximity_list *list =
		&g_test_mobiles[0].proximity_list;
	XVT_ASSERT_INT_EQ(list->count, 1);
	XVT_ASSERT_TRUE(list_has(list, TEST_MINE));

	/* Types 100 to 105 are the hazards; 99 and 106 are not. */
	static const struct {
		int type;
		int added;
	} types[] = {{99, 0}, {100, 1}, {105, 1}, {106, 0}};
	for (size_t i = 0; i < sizeof types / sizeof types[0]; ++i) {
		four_craft_world();
		g_test_objects[TEST_MINE].object_type = (uint8_t)types[i].type;
		populate(0);
		XVT_ASSERT_INT_EQ(g_test_mobiles[0].proximity_list.count,
				  types[i].added);
	}

	/* A starship that is dropping off or carried is not given the craft. */
	for (int busy = 0; busy < 2; ++busy) {
		four_craft_world();
		g_test_objects[2].genus_id = CRAFT_GENUS_STARSHIP;
		if (busy == 0) {
			g_test_craft[2].ai_controller.maneuver_mode =
				AI_MANEUVER_MODE_DROPOFF;
		} else {
			g_test_craft[2].carrier_obj_idx = 1;
		}
		populate(0);
		XVT_ASSERT_TRUE(
			!list_has(&g_test_mobiles[2].proximity_list, 0));
	}

	/* Nothing in proving grounds is dead; a ship of the same group is not
	 * given the craft. */
	four_craft_world();
	g_test_objects[2].genus_id = CRAFT_GENUS_FREIGHTER;
	g_test_objects[2].flight_group_idx = 0;
	populate(0);
	XVT_ASSERT_TRUE(!list_has(&g_test_mobiles[2].proximity_list, 0));
	/* A craft without a mobile_object is not given it; other AI craft are
	 * not given it either. */
	four_craft_world();
	g_test_mobiles[1].p_craft = &g_test_craft[1];
	g_test_objects[2].genus_id = CRAFT_GENUS_STARSHIP;
	g_test_objects[2].mobj = NULL;
	populate(0);
	XVT_ASSERT_TRUE(!list_has(&g_test_mobiles[1].proximity_list, 0));
}

/* Comment: a computer-flown freighter, starship or platform not dropping
 * off or carried adds itself to player craft's lists and to other such
 * ships' lists, and adds every other AI craft except explosions to its own. */
static void check_populate_for_capital_ship(void)
{
	four_craft_world();
	player_flies(1, 1);
	g_test_objects[0].genus_id = CRAFT_GENUS_STARSHIP;
	g_test_objects[3].genus_id = CRAFT_GENUS_FREIGHTER;
	populate(0);
	const struct mobile_object_proximity_list *own =
		&g_test_mobiles[0].proximity_list;
	/* Its own list: the other AI craft (not itself, not explosions); the
	 * player's craft is given it instead. */
	XVT_ASSERT_TRUE(list_has(own, 2));
	XVT_ASSERT_TRUE(list_has(own, 3));
	XVT_ASSERT_TRUE(!list_has(own, 0));
	XVT_ASSERT_TRUE(!list_has(own, 1));
	XVT_ASSERT_INT_EQ(own->count, 2);
	XVT_ASSERT_TRUE(list_has(&g_test_mobiles[1].proximity_list, 0));
	/* The other ship is given it too. */
	XVT_ASSERT_TRUE(list_has(&g_test_mobiles[3].proximity_list, 0));
	XVT_ASSERT_TRUE(!list_has(&g_test_mobiles[2].proximity_list, 0));

	/* An explosion is left out; a dropping-off or carried ship takes
	 * nothing in either direction. */
	four_craft_world();
	g_test_objects[0].genus_id = CRAFT_GENUS_PLATFORM;
	g_test_objects[2].genus_id = CRAFT_GENUS_EXPLOSION;
	populate(0);
	XVT_ASSERT_TRUE(!list_has(own, 2));
	XVT_ASSERT_TRUE(list_has(own, 3));
	for (int busy = 0; busy < 2; ++busy) {
		four_craft_world();
		g_test_objects[0].genus_id = CRAFT_GENUS_STARSHIP;
		if (busy == 0) {
			g_test_craft[0].ai_controller.maneuver_mode =
				AI_MANEUVER_MODE_DROPOFF;
		} else {
			g_test_craft[0].carrier_obj_idx = 1;
		}
		populate(0);
		XVT_ASSERT_INT_EQ(own->count, 0);
		XVT_ASSERT_TRUE(
			!list_has(&g_test_mobiles[1].proximity_list, 0));
	}
	/* A busy ship among the others is not given this one. */
	four_craft_world();
	g_test_objects[0].genus_id = CRAFT_GENUS_STARSHIP;
	g_test_objects[3].genus_id = CRAFT_GENUS_STARSHIP;
	g_test_craft[3].carrier_obj_idx = 1;
	populate(0);
	XVT_ASSERT_TRUE(list_has(own, 3));
	XVT_ASSERT_TRUE(!list_has(&g_test_mobiles[3].proximity_list, 0));
	g_test_craft[3].carrier_obj_idx = UINT16_MAX;
	g_test_craft[3].ai_controller.maneuver_mode = AI_MANEUVER_MODE_DROPOFF;
	populate(0);
	XVT_ASSERT_TRUE(!list_has(&g_test_mobiles[3].proximity_list, 0));
}

/* Shots 2 to 4 of the usual layout: shot 2 of craft 0 is the owner; shot 3
 * (a missile of craft 1) and shot 4 (a cannon shot of craft 1) are others;
 * the mine is in the first static slot. */
static void shots_world(void)
{
	fresh_world();
	place_mine(1000, 0, 0);
	place_shot(TEST_SHOT, WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 0, 0, 0,
		   0, 0, 0, 0);
	place_shot(TEST_OTHER_SHOT, WARHEAD_OBJECT_TYPE_PROTON_TORPEDO, 1, 0, 0,
		   0, 0, 0, 0);
	place_shot(4, PROJECTILE_OBJECT_TYPE_REBEL_LASER, 1, 0, 0, 0, 0, 0, 0);
}

/* Comment: a shot gets every craft and every warhead from another source,
 * not itself, its source or an explosion, when the shot's target or
 * source belongs to a player or the candidate is its target; also every
 * static object. */
static void check_populate_for_shot(void)
{
	const struct mobile_object_proximity_list *list =
		&g_test_mobiles[TEST_SHOT].proximity_list;

	/* Neither target nor source with a player: only the static objects. */
	shots_world();
	populate(TEST_SHOT);
	XVT_ASSERT_INT_EQ(list->count, 1);
	XVT_ASSERT_TRUE(list_has(list, TEST_MINE));

	/* Aimed at craft 1: that craft, and nothing else of the craft. */
	shots_world();
	g_test_guidance[0].target_obj_idx = 1;
	populate(TEST_SHOT);
	XVT_ASSERT_TRUE(list_has(list, 1));
	XVT_ASSERT_TRUE(!list_has(list, 0));
	XVT_ASSERT_INT_EQ(list->count, 2);

	/* Aimed at a player's craft: every craft but its source, and the
	 * other missile, not the cannon shot, not itself. */
	shots_world();
	player_flies(1, 1);
	g_test_guidance[0].target_obj_idx = 1;
	populate(TEST_SHOT);
	XVT_ASSERT_TRUE(list_has(list, 1));
	XVT_ASSERT_TRUE(!list_has(list, 0));
	XVT_ASSERT_TRUE(list_has(list, TEST_OTHER_SHOT));
	XVT_ASSERT_TRUE(!list_has(list, 4));
	XVT_ASSERT_TRUE(!list_has(list, TEST_SHOT));
	XVT_ASSERT_TRUE(list_has(list, TEST_MINE));

	/* Fired by a player's craft: the same. A missile from the same source
	 * is left out, so is an explosion. */
	shots_world();
	player_flies(0, 0);
	g_test_mobiles[TEST_OTHER_SHOT].source_obj_idx = 0;
	populate(TEST_SHOT);
	XVT_ASSERT_TRUE(list_has(list, 1));
	XVT_ASSERT_TRUE(!list_has(list, 0));
	XVT_ASSERT_TRUE(!list_has(list, TEST_OTHER_SHOT));
	shots_world();
	player_flies(0, 0);
	g_test_objects[TEST_OTHER_SHOT].genus_id = CRAFT_GENUS_EXPLOSION;
	populate(TEST_SHOT);
	XVT_ASSERT_TRUE(!list_has(list, TEST_OTHER_SHOT));
	/* An impact effect left in a shot slot is not a candidate. */
	shots_world();
	player_flies(0, 0);
	g_test_objects[TEST_OTHER_SHOT].object_type = TEST_EXPLOSION_TYPE;
	populate(TEST_SHOT);
	XVT_ASSERT_TRUE(!list_has(list, TEST_OTHER_SHOT));
	/* A dead slot. */
	shots_world();
	player_flies(0, 0);
	g_test_objects[1].object_type = 0;
	populate(TEST_SHOT);
	XVT_ASSERT_TRUE(!list_has(list, 1));

	/* Other genera get nothing. */
	shots_world();
	g_test_objects[1].genus_id = CRAFT_GENUS_SATELLITE;
	populate(1);
	XVT_ASSERT_INT_EQ(g_test_mobiles[1].proximity_list.count, 0);
}

/* Comment: collide_collisions rebuilds an object's proximity list when its
 * rebuild_ticks has run out. */
static void check_collisions_rebuilds_list(void)
{
	four_craft_world();
	g_test_mobiles[0].proximity_list.rebuild_ticks = 1;
	collide_collisions();
	XVT_ASSERT_TRUE(line_holds("combat.proximity_rebuilt",
				   "object=0 genus=0 count=1"));
	XVT_ASSERT_TRUE(list_has(&g_test_mobiles[0].proximity_list, TEST_MINE));
	/* A list not yet due is left as it is. */
	four_craft_world();
	g_test_mobiles[0].proximity_list.rebuild_ticks = 2;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_mobiles[0].proximity_list.count, 0);
	XVT_ASSERT_TRUE(find_line("combat.proximity_rebuilt") == NULL);
}

/* Comment: collide_reset_object_proximity_for_slot makes a slot with a
 * mobile_object rebuild; a static slot (none) is added to the list of every
 * live player's craft. */
static void check_reset_static_slot_reaches_players(void)
{
	four_craft_world();
	player_flies(1, 1);
	g_test_objects[0].player_owner_idx = -1;
	collide_reset_object_proximity_for_slot(TEST_MINE);
	XVT_ASSERT_TRUE(list_has(&g_test_mobiles[1].proximity_list, TEST_MINE));
	XVT_ASSERT_INT_EQ(g_test_mobiles[0].proximity_list.count, 0);
	XVT_ASSERT_INT_EQ(g_test_mobiles[2].proximity_list.count, 0);
	/* A player's craft that is gone does not get it. */
	four_craft_world();
	player_flies(1, 1);
	g_test_objects[1].object_type = 0;
	collide_reset_object_proximity_for_slot(TEST_MINE);
	XVT_ASSERT_INT_EQ(g_test_mobiles[1].proximity_list.count, 0);
	/* With no craft slots, nothing. */
	four_craft_world();
	player_flies(1, 1);
	g_active_region_craft_object_slot_end = 0;
	collide_reset_object_proximity_for_slot(TEST_MINE);
	XVT_ASSERT_INT_EQ(g_test_mobiles[1].proximity_list.count, 0);
}

/* Comment: collide_reset_neighbor_proximity_lists does nothing for an object
 * without a mobile_object or with an empty list. */
static void check_reset_neighbors_skips(void)
{
	fresh_world();
	g_test_objects[0].mobj = NULL;
	collide_reset_neighbor_proximity_lists(0);
	g_test_mobiles[1].proximity_list.count = 0;
	g_test_mobiles[0].proximity_list.rebuild_ticks = 7;
	g_test_mobiles[1].proximity_list.rebuild_ticks = 7;
	collide_reset_neighbor_proximity_lists(1);
	XVT_ASSERT_INT_EQ(g_test_mobiles[0].proximity_list.rebuild_ticks, 7);
	XVT_ASSERT_INT_EQ(g_test_mobiles[1].proximity_list.rebuild_ticks, 7);
}

/* Comment: when a player's craft bounces off another craft, each spins at 100
 * times the closing speed, at most 0x7FFF: the craft's speed less the other's
 * times the cosine of their headings' difference folded into a quarter turn,
 * in size. */
static void check_collisions_player_bounce_spin(void)
{
	/* Headings (the other's, the player's being 0) that do not fold to
	 * exactly a quarter turn. */
	static const unsigned int headings[] = {
		0x0000, 0x1000, 0x2000, 0x3000, 0x5000, 0x6000, 0x7000,
		0x8000, 0x9000, 0xA000, 0xB000, 0xD000, 0xE000, 0xF000};
	static const int speeds[][2] = {{50, 30}, {10, 60}, {400, 20}, {0, 0}};
	for (size_t s = 0; s < sizeof speeds / sizeof speeds[0]; ++s) {
		for (size_t h = 0; h < sizeof headings / sizeof headings[0];
		     ++h) {
			for (int touching = 0; touching < 2; ++touching) {
				craft_meet();
				g_flight_mission_state.collisions_enabled = 0;
				g_test_mobiles[0].speed = speeds[s][0];
				g_test_mobiles[1].speed = speeds[s][1];
				g_test_objects[1].yaw = (uint16_t)headings[h];
				g_test_mobiles[0].move_vector_dirty =
					(h & 1) != 0;
				g_test_mobiles[1].move_vector_dirty =
					(h & 2) != 0;
				if (touching) {
					place(1, 5, 0, 0);
					g_test_mobiles[1].prev_world_x = 5;
				}
				collide_collisions();
				int cosine = (int)(int16_t)trig2_cosinewordmult(
					(uint16_t)speeds[s][1],
					(int16_t)fold_into_quarter(
						headings[h]));
				int closing = abs_int(speeds[s][0] - cosine);
				int spin = 100 * closing;
				if (spin > 0x7FFF) {
					spin = 0x7FFF;
				}
				XVT_ASSERT_INT_EQ(
					abs_int(g_test_mobiles[0]
							.roll_impulse_rate),
					spin);
				XVT_ASSERT_INT_EQ(
					abs_int(g_test_mobiles[1]
							.roll_impulse_rate),
					spin);
				XVT_ASSERT_INT_EQ(
					g_test_craft[0]
						.ai_flight.impact_obj_idx,
					1);
				XVT_ASSERT_INT_EQ(
					g_test_craft[1]
						.ai_flight.impact_obj_idx,
					0);
			}
		}
	}
}

/* Comment: an object whose list is walked but has no mobile_object is
 * passed over, and one of a genus the step does not test does nothing. */
static void check_collisions_other_genera_ignored(void)
{
	craft_meet();
	g_test_objects[0].player_owner_idx = -1;
	g_players[0].object_index = -1;
	g_test_objects[0].genus_id = CRAFT_GENUS_SATELLITE;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000);
	XVT_ASSERT_INT_EQ(g_test_craft[0].shield_energy[0], 1000);
	XVT_ASSERT_INT_EQ(g_test_craft[1].ai_flight.impact_obj_idx, UINT16_MAX);

	fresh_world();
	g_test_objects[4].object_type = 9;
	g_test_objects[4].genus_id = CRAFT_GENUS_OTHER_PROJECTILE;
	g_test_objects[4].mobj = NULL;
	g_test_mobiles[0].proximity_list.rebuild_ticks = 5;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_mobiles[0].proximity_list.rebuild_ticks, 4);
}

/* ------------------------------------------------------------------------ */
/* collide_damagecraft and collide_laserhitcraft: edges. */

/* Comment: a hit is refused (1, nothing dealt) when a source craft's flight
 * group has status 20; a source that is not a craft is not refused for its
 * group's status. */
static void check_damage_protected_sources(void)
{
	laser_at_craft();
	g_test_mobiles[0].damage_amount = 300;
	g_mission_flight_groups[0].fg.status1 = 20;
	XVT_ASSERT_INT_EQ(collide_damagecraft(1, -1, 0, 0), 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000);
	XVT_ASSERT_TRUE(line_holds("combat.damage_refused",
				   "reason=\"source_protected\""));
	g_mission_flight_groups[0].fg.status1 = 0;
	collide_damagecraft(1, -1, 0, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 300);

	/* A mine of a protected group still hurts. */
	laser_at_craft();
	place_mine(20000, 0, 0);
	g_test_craft[1].hull_max = 100000;
	g_mission_flight_groups[2].fg.status1 = 20;
	collide_damagecraft(1, -1, TEST_MINE, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].damage_stats.damage_received_total,
			  4 * TEST_MINE_EXTENT);
	XVT_ASSERT_TRUE(find_line("combat.damage_refused") == NULL);
	/* So does a shot slot. */
	laser_at_craft();
	g_mission_flight_groups[0].fg.status2 = 20;
	g_test_mobiles[TEST_SHOT].source_obj_idx = TEST_MINE;
	g_test_objects[TEST_SHOT].flight_group_idx = 0;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 1000 - 250);
}

/* Comment: a craft's ram deals collide_compute_craft_damage_amount; another
 * mobile object deals its damage_amount. In mission file version 14 a
 * craft with warheads deals more to a starship. */
static void check_damage_craft_source_uses_warheads(void)
{
	laser_at_craft();
	g_mission_file_version = 14;
	g_test_objects[1].genus_id = CRAFT_GENUS_STARSHIP;
	g_test_craft[1].hull_max = 100000;
	g_test_mobiles[0].damage_amount = 1200;
	g_test_craft[0].warhead_launcher_count = 1;
	g_model_defs[TEST_MODEL].warhead_launcher_first_slot[0] = 2;
	g_model_defs[TEST_MODEL].warhead_launcher_last_slot[0] = 3;
	g_test_craft[0].weapon_slots[2].ammo_count = 8;
	g_test_craft[0].weapon_slots[3].ammo_count = 8;
	g_test_craft[0].warhead_slot_type_ids[0] =
		WARHEAD_OBJECT_TYPE_PROTON_TORPEDO;
	unsigned int ram = collide_compute_craft_damage_amount(1, 0);
	XVT_ASSERT_TRUE(ram > 1200);
	collide_damagecraft(1, -1, 0, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].damage_stats.damage_received_total,
			  (int)(ram / 16));
	/* A shot of the same craft deals its own figure. */
	laser_at_craft();
	g_test_objects[1].genus_id = CRAFT_GENUS_STARSHIP;
	g_test_craft[1].hull_max = 100000;
	g_test_mobiles[TEST_SHOT].damage_amount = 1200;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].damage_stats.damage_received_total,
			  1200 / 16);
	g_mission_file_version = 0;
}

/* Comment: with difficulty above 0 a mesh hit goes to the component only
 * when both shields are down: two shields holding the same energy are not
 * down. */
static void check_damage_component_equal_shields(void)
{
	laser_at_craft();
	gun_craft_model(0, MESH_EXPLOSION_BIT);
	g_test_craft[1].hull_max = 100000;
	g_flight_mission_state.difficulty = 1;
	g_test_craft[1].shield_energy[0] = 400;
	g_test_craft[1].shield_energy[1] = 400;
	g_test_craft[1].component_hp[1] = 20;
	collide_damagecraft(1, 2, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].component_hp[1], 20);
	XVT_ASSERT_INT_EQ(g_test_craft[1].shield_energy[0], 400 - 250);
	g_flight_mission_state.difficulty = 0;
}

/* One point through the shield is damage: the hull takes it, and an ion
 * hit still knocks out a system. */
static void check_damage_one_point_through(void)
{
	laser_at_craft();
	g_test_craft[1].hull_max = 1000;
	g_test_craft[1].system_damage_hull_threshold = 1000;
	g_test_craft[1].shield_energy[0] = 249;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 1);
	XVT_ASSERT_TRUE(line_holds("combat.hull_hit", "damage=1 hull=1"));

	ion_at_craft(PROJECTILE_OBJECT_TYPE_ION_LASER, 251);
	g_test_craft[1].subsystem_damage = TEST_ION_STRENGTH - 11;
	g_test_craft[1].shield_energy[0] = 250;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].working_subsystems,
			  CRAFT_SUBSYSTEM_FLAGS_ALL ^
				  g_subsystem_id_to_flag[0]);
	XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 0);
}

/* Comment: a player's craft whose systems are already out keeps their
 * repair records when it loses all its strength; a computer-flown craft
 * with turrets and cannon groups loses the aim of each turret (projectile
 * type 2) and each group's link. */
static void check_damage_ion_spares_failed_systems(void)
{
	ion_at_craft(PROJECTILE_OBJECT_TYPE_ION_LASER, 30);
	g_test_craft[1].subsystem_damage = TEST_ION_STRENGTH - 1;
	player_flies(1, 1);
	g_test_craft[1].working_subsystems &= ~g_subsystem_id_to_flag[9];
	g_test_craft[1].system_health[9] = 33;
	g_test_craft[1].system_repair_seconds[9] = 12;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].working_subsystems, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].system_health[9], 33);
	XVT_ASSERT_INT_EQ(g_test_craft[1].system_repair_seconds[9], 12);
	XVT_ASSERT_INT_EQ(g_test_craft[1].system_health[0], 0);

	ion_at_craft(PROJECTILE_OBJECT_TYPE_ION_LASER, 2001);
	g_test_craft[1].subsystem_damage = TEST_ION_STRENGTH - 11;
	g_test_craft[1].laser_slot_count = 3;
	g_test_craft[1].cannon_group_count = 2;
	g_test_craft[1].weapon_slots[0].projectile_type_id = 2;
	g_test_craft[1].weapon_slots[1].projectile_type_id = 1;
	g_test_craft[1].weapon_slots[2].projectile_type_id = 2;
	for (int slot = 0; slot < 3; ++slot) {
		g_test_craft[1].turret_target_states[slot].target_obj_idx = 0;
	}
	g_test_craft[1].laser_state.link_mode[0] = 1;
	g_test_craft[1].laser_state.link_mode[1] = 1;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(
		g_test_craft[1].turret_target_states[0].target_obj_idx,
		UINT16_MAX);
	XVT_ASSERT_INT_EQ(
		g_test_craft[1].turret_target_states[1].target_obj_idx, 0);
	XVT_ASSERT_INT_EQ(
		g_test_craft[1].turret_target_states[2].target_obj_idx,
		UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_test_craft[1].laser_state.link_mode[1], 0);
	/* The disabled craft is announced: message 137 follows the craft. */
	XVT_ASSERT_TRUE(g_msg_arg_table[2] == 137 || g_msg_arg_table[3] == 137);
}

/* Comment: the hull hit on a player's craft knocks out a system one time in
 * 4: the craft's first draw below 0x4000. The edges: 0x3FFF fails a system,
 * 0x4000 does not; a system already out keeps its repair record. */
static void check_damage_hull_hit_seed_edges(void)
{
	for (int edge = 0; edge < 2; ++edge) {
		laser_at_craft();
		player_flies(1, 1);
		g_test_craft[1].hull_max = 1000;
		g_test_craft[1].system_damage_hull_threshold = 1000;
		g_test_craft[1].shield_energy[1] = 0;
		g_test_craft[1].ai_controller.saved_rand_seed =
			edge == 0 ? 0x3FFF : 0x4000;
		all_systems_work(&g_test_craft[1]);
		collide_damagecraft(1, -1, TEST_SHOT, 1);
		XVT_ASSERT_INT_EQ(g_test_craft[1].working_subsystems !=
					  CRAFT_SUBSYSTEM_FLAGS_ALL,
				  edge == 0);
	}
	laser_at_craft();
	player_flies(1, 1);
	g_test_craft[1].hull_max = 1000;
	g_test_craft[1].system_damage_hull_threshold = 1000;
	g_test_craft[1].shield_energy[1] = 0;
	g_test_craft[1].ai_controller.saved_rand_seed = 0;
	all_systems_work(&g_test_craft[1]);
	g_test_craft[1].working_subsystems &= ~g_subsystem_id_to_flag[0];
	g_test_craft[1].system_health[0] = 42;
	collide_damagecraft(1, -1, TEST_SHOT, 1);
	XVT_ASSERT_INT_EQ(g_test_craft[1].system_health[0], 42);
	XVT_ASSERT_TRUE(find_line("combat.system_failed") == NULL);
}

/* Comment: the kill comes when hull_damage reaches hull_max: equal is
 * enough. */
static void check_kill_exact_boundary(void)
{
	for (int spare = 0; spare < 2; ++spare) {
		killing_shot();
		g_test_craft[1].hull_max = 250 + spare;
		collide_damagecraft(1, -1, TEST_SHOT, 0);
		XVT_ASSERT_INT_EQ(
			g_mission_fg_stats[1]
				.outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED],
			spare == 0);
		XVT_ASSERT_INT_EQ(g_test_craft[1].hull_damage, 250);
		XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
				  spare == 0 ? CRAFT_OBJECT_KIND_BREAKING_UP
					     : CRAFT_OBJECT_KIND_ACTIVE);
	}
}

/* Comment: the kill clears the carried and carrier links that name the
 * victim and no others, tells the craft message 136, and leaves a computer
 * flown victim's player scan alone. */
static void check_kill_clears_only_its_links(void)
{
	killing_shot();
	g_test_craft[0].carried_object_index = 1;
	g_test_craft[0].carrier_obj_idx = 1;
	g_test_craft[1].carried_object_index = 0;
	g_test_craft[1].carrier_obj_idx = 0;
	g_flight_global_countdown_timers.mission_arrival_trigger_scan_timer =
		77;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[0].carried_object_index, UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_test_craft[0].carrier_obj_idx, UINT16_MAX);
	/* The victim's own links to craft 0 stay. */
	XVT_ASSERT_INT_EQ(g_test_craft[1].carried_object_index, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[1].carrier_obj_idx, 0);
	XVT_ASSERT_INT_EQ(g_flight_global_countdown_timers
				  .mission_arrival_trigger_scan_timer,
			  77);
	XVT_ASSERT_TRUE(g_msg_arg_table[2] == 136 || g_msg_arg_table[3] == 136);

	/* A slot that holds nothing is not read as a carrier. */
	killing_shot();
	g_test_objects[0].object_type = 0;
	g_test_craft[0].carried_object_index = 1;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[0].carried_object_index, 1);
}

/* Comment: a player's craft that is destroyed has its throttle, recharge
 * levels, shield distribution and link modes saved for the next craft
 * (player_save_craft_settings), and its loss counted against the player. */
static void check_kill_saves_player_settings(void)
{
	killing_shot();
	player_flies(1, 1);
	g_test_craft[1].throttle_speed = 1234;
	g_test_craft[1].laser_state.link_mode[0] = 1;
	g_test_craft[1].laser_state.link_mode[1] = 0;
	g_test_craft[1].warhead_launcher_flags[0] = 7;
	g_test_craft[1].shield_distrib_mode = SHIELD_DISTRIBUTION_FULLY_AFT;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_players[1].saved_craft_settings.throttle_speed,
			  1234);
	XVT_ASSERT_INT_EQ(g_players[1].saved_craft_settings.laser_link_mode[0],
			  1);
	XVT_ASSERT_INT_EQ(g_players[1].saved_craft_settings.laser_link_mode[1],
			  0);
	XVT_ASSERT_INT_EQ(
		g_players[1].saved_craft_settings.warhead_launcher_flags[0], 3);
	XVT_ASSERT_INT_EQ(g_players[1].per_mission_kills.total_craft_losses, 1);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime.team_kill_stats[3][1],
			  1);
}

/* Comment: a torn wing throws the craft back: the craft's yaw turns by half
 * the wing's deflection the other way; with no wing torn, it does not
 * turn. */
static void check_kill_wing_recoil(void)
{
	for (int seed = 0; seed < 16; ++seed) {
		killing_shot();
		winged_craft_model();
		g_test_objects[1].yaw = 0x1234;
		g_test_craft[1].ai_controller.saved_rand_seed = (uint16_t)seed;
		collide_damagecraft(1, -1, TEST_SHOT, 0);
		int debris =
			(int16_t)(g_test_objects[TEST_DEBRIS].yaw - 0x1234);
		int craft = (int16_t)(g_test_objects[1].yaw - 0x1234);
		XVT_ASSERT_TRUE(abs_int(debris) >= 2048 &&
				abs_int(debris) <= 4095);
		XVT_ASSERT_TRUE(abs_int(craft + debris / 2) <= 1);
	}
	killing_shot();
	fuselage_craft_model();
	g_test_objects[1].yaw = 0x1234;
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[1].yaw, 0x1234);
}

/* ------------------------------------------------------------------------ */
/* Contact tests: collide_checkboxcollision, collide_test_swept_pair_collision,
 * collide_would_shot_hit_target and collide_craftstarshipcollision. */

/* The source moves from (sx, sy, sz) to (px, py, pz) in the step; the
 * target's box (radius) moves from (ax, ay, az) to (bx, by, bz). */
static int box_hit(int radius, const int source[6], const int target[6])
{
	g_collision_segment_start_world_x = source[0];
	g_collision_segment_start_world_y = source[1];
	g_collision_segment_start_world_z = source[2];
	g_collision_probe_world_x = source[3];
	g_collision_probe_world_y = source[4];
	g_collision_probe_world_z = source[5];
	g_collision_sweep_start_x = target[0];
	g_collision_sweep_start_y = target[1];
	g_collision_sweep_start_z = target[2];
	g_collision_sweep_end_x = target[3];
	g_collision_sweep_end_y = target[4];
	g_collision_sweep_end_z = target[5];
	g_collision_hit_offset_x = 12345;
	return collide_checkboxcollision(radius);
}

/* Comment: the box test says whether the source, moving through the step,
 * enters the target's box of half-size radius, the target moving too, and
 * returns -1 on a hit with the source's travel up to the entry; else 0. */
static void check_box_collision_cases(void)
{
	static const int along_x[6] = {0, 0, 0, 1000, 0, 0};
	static const struct {
		const char *what;
		int target[6];
		int hit;
	} cases[] = {
		{"static, on the path", {500, 0, 0, 500, 0, 0}, 1},
		{"static, beside it in Y", {500, 150, 0, 500, 150, 0}, 0},
		{"static, beside it in Z", {500, 0, 150, 500, 0, 150}, 0},
		{"static, below it in Y", {500, -150, 0, 500, -150, 0}, 0},
		{"static, below it in Z", {500, 0, -150, 500, 0, -150}, 0},
		{"static, just inside in Y", {500, 100, 0, 500, 100, 0}, 1},
		{"static, just inside in Z", {500, 0, -100, 500, 0, -100}, 1},
		{"static, past the end", {1200, 0, 0, 1200, 0, 0}, 0},
		{"static, behind the start", {-300, 0, 0, -300, 0, 0}, 0},
		{"crossing the path in Y", {500, 300, 0, 500, -300, 0}, 1},
		{"crossing the path in Z", {500, 0, 300, 500, 0, -300}, 1},
		{"crossing from below in Y", {500, -300, 0, 500, 300, 0}, 1},
		{"crossing from below in Z", {500, 0, -300, 500, 0, 300}, 1},
		{"coming too slowly in Y", {500, 300, 0, 500, 250, 0}, 0},
		{"coming too slowly in Z", {500, 0, 300, 500, 0, 250}, 0},
		{"moving away in Y", {500, 300, 0, 500, 600, 0}, 0},
		{"moving away in Z", {500, 0, 300, 500, 0, 600}, 0},
		{"moving away from below in Y",
		 {500, -300, 0, 500, -600, 0},
		 0},
		{"moving away from below in Z",
		 {500, 0, -300, 500, 0, -600},
		 0},
		{"head on in X", {1000, 0, 0, 0, 0, 0}, 1},
		{"overtaken in X", {-500, 0, 0, 2000, 0, 0}, 1},
		{"ahead and running away in X", {1500, 0, 0, 4000, 0, 0}, 0},
		{"behind and falling back in X", {-300, 0, 0, -1300, 0, 0}, 0},
		{"too far in X to close", {5000, 0, 0, 5000, 0, 0}, 0},
		{"starting inside the box", {50, 0, 0, 50, 0, 0}, 1},
		{"crossing in Y too late", {500, 300, 0, 500, 100, 0}, 0},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		XVT_ASSERT_INT_EQ(box_hit(100, along_x, cases[i].target),
				  cases[i].hit ? -1 : 0);
	}

	/* The travel to the entry, in 256ths of the step: a static box at 500
	 * is entered at 400. */
	box_hit(100, along_x, cases[0].target);
	XVT_ASSERT_TRUE(g_collision_hit_offset_x >= 390 &&
			g_collision_hit_offset_x <= 400);
	XVT_ASSERT_INT_EQ(g_collision_hit_offset_y, 0);
	XVT_ASSERT_INT_EQ(g_collision_hit_offset_z, 0);
	/* Met head on: the gap of 1,000 closes at 2,000 per step, to the box's
	 * edge 100 out: entered at 450. */
	box_hit(100, along_x, cases[19].target);
	XVT_ASSERT_TRUE(g_collision_hit_offset_x >= 440 &&
			g_collision_hit_offset_x <= 450);
	/* Starting inside the box: no travel. */
	box_hit(100, along_x, cases[24].target);
	XVT_ASSERT_INT_EQ(g_collision_hit_offset_x, 0);
	/* A source that moves in Y and Z carries the offset along. */
	static const int diagonal[6] = {0, 0, 0, 1000, 500, -250};
	static const int still[6] = {500, 250, -125, 500, 250, -125};
	XVT_ASSERT_INT_EQ(box_hit(100, diagonal, still), -1);
	XVT_ASSERT_TRUE(g_collision_hit_offset_y * 2 >=
			g_collision_hit_offset_x - 20);
	XVT_ASSERT_TRUE(g_collision_hit_offset_y * 2 <=
			g_collision_hit_offset_x + 20);
	XVT_ASSERT_TRUE(g_collision_hit_offset_z < 0);
	/* The same cases for a source moving the other way along each axis, and
	 * for a target already inside the box on an axis and leaving it. */
	static const int along_neg_y[6] = {0, 0, 0, 0, -1000, 0};
	static const int along_neg_z[6] = {0, 0, 0, 0, 0, -1000};
	static const struct {
		const int *source;
		int target[6];
		int hit;
	} others[] = {
		{along_neg_y, {0, -500, 0, 0, -500, 0}, 1},
		{along_neg_y, {150, -500, 0, 150, -500, 0}, 0},
		{along_neg_y, {0, -500, 150, 0, -500, 150}, 0},
		{along_neg_y, {300, -500, 0, -300, -500, 0}, 1},
		{along_neg_y, {0, -500, 300, 0, -500, -300}, 1},
		{along_neg_y, {0, -500, 50, 0, -500, 120}, 1},
		{along_neg_y, {50, -500, 0, 120, -500, 0}, 1},
		{along_neg_y, {0, -1500, 0, 0, -3000, 0}, 0},
		{along_neg_z, {0, 0, -500, 0, 0, -500}, 1},
		{along_neg_z, {150, 0, -500, 150, 0, -500}, 0},
		{along_neg_z, {0, 150, -500, 0, 150, -500}, 0},
		{along_neg_z, {300, 0, -500, -300, 0, -500}, 1},
		{along_neg_z, {0, 300, -500, 0, -300, -500}, 1},
		{along_neg_z, {50, 0, -500, 120, 0, -500}, 1},
		{along_neg_z, {0, 50, -500, 0, 120, -500}, 1},
		{along_x, {500, 50, 0, 500, 120, 0}, 1},
		{along_x, {500, 0, 50, 500, 0, 120}, 1},
		{along_x, {500, -50, 0, 500, -120, 0}, 1},
		{along_x, {500, 0, -50, 500, 0, -120}, 1},
		{along_x, {500, 250, 0, 500, 120, 0}, 0},
		{along_x, {500, 0, 250, 500, 0, 120}, 0},
		{along_x, {500, 0, 0, 500, 0, 150}, 1},
	};
	for (size_t i = 0; i < sizeof others / sizeof others[0]; ++i) {
		XVT_ASSERT_INT_EQ(
			box_hit(100, others[i].source, others[i].target),
			others[i].hit ? -1 : 0);
	}
	/* A box of radius 0 is hit only head on... a still source misses a
	 * box beside it. */
	static const int rest[6] = {0, 0, 0, 0, 0, 0};
	XVT_ASSERT_INT_EQ(box_hit(100, rest, cases[1].target), 0);
}

/* The pair test: craft 0 (a starfighter, extent 400) moves from (0, 0, 0)
 * to (1,000, 0, 0); the target sits at (500, y, 0), not moving; the target
 * is object slot target of extent 400. */
static int pair_hit_at(int target, int y)
{
	g_collision_segment_start_world_x = 0;
	g_collision_segment_start_world_y = 0;
	g_collision_segment_start_world_z = 0;
	g_collision_probe_world_x = 1000;
	g_collision_probe_world_y = 0;
	g_collision_probe_world_z = 0;
	g_collision_sweep_start_x = 500;
	g_collision_sweep_start_y = y;
	g_collision_sweep_start_z = 0;
	g_collision_sweep_end_x = 500;
	g_collision_sweep_end_y = y;
	g_collision_sweep_end_z = 0;
	return collide_test_swept_pair_collision(0, (uint16_t)target);
}

/* Comment: the pair test rejects when the end points lie farther apart than
 * both extents plus 0x20000, on an axis or by rough distance; a target the
 * box of 3/8 of its extent meets is a hit (-1). */
static void check_swept_pair_rejections(void)
{
	fresh_world();
	/* Both extents 400: 0x20000 + 800 = 131,872 is the largest gap. */
	XVT_ASSERT_INT_EQ(pair_hit_at(1, 0), -1);
	XVT_ASSERT_INT_EQ(pair_hit_at(1, 149), -1);
	XVT_ASSERT_INT_EQ(pair_hit_at(1, 151), 0);
	/* The probe is at x 1,000: the target's end may be 131,872 away on an
	 * axis. */
	XVT_ASSERT_INT_EQ(g_approx_dist != 0, 1);
	g_collision_probe_world_x = 0;
	g_collision_sweep_end_x = 131872;
	g_collision_sweep_start_x = 131872;
	g_collision_sweep_end_y = 0;
	g_collision_sweep_start_y = 0;
	g_collision_sweep_end_z = 0;
	g_collision_sweep_start_z = 0;
	g_collision_segment_start_world_x = 0;
	g_collision_probe_world_x = 1000;
	XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(0, 1) == 0, 1);
	g_collision_sweep_end_x = 500;
	g_collision_sweep_start_x = 500;
	for (int axis = 0; axis < 3; ++axis) {
		fresh_world();
		pair_hit_at(1, 0);
		int far = 131873;
		if (axis == 0) {
			g_collision_sweep_end_x = 1000 + far;
		} else if (axis == 1) {
			g_collision_sweep_end_y = far;
		} else {
			g_collision_sweep_end_z = far;
		}
		XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(0, 1), 0);
		XVT_ASSERT_INT_EQ(g_approx_dist, 131872);
	}
	/* Each axis within, but the rough distance past the largest. */
	fresh_world();
	pair_hit_at(1, 0);
	g_collision_sweep_end_x = 100000 + 1000;
	g_collision_sweep_end_y = 100000;
	g_collision_sweep_end_z = 100000;
	XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(0, 1), 0);
	XVT_ASSERT_TRUE(g_approx_dist > 131872);
	/* A target the sweeps cannot reach: within range but 60,000 off. */
	fresh_world();
	pair_hit_at(1, 0);
	g_collision_sweep_start_x = 60000;
	g_collision_sweep_end_x = 60000;
	XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(0, 1), 0);
	/* A target moving at the source from there, quick enough, is hit. */
	g_collision_sweep_start_x = 3000;
	g_collision_sweep_end_x = 0;
	XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(0, 1), -1);
}

/* The pair test works the same for a source and a target moving the other
 * way along every axis. */
static void check_swept_pair_negative_directions(void)
{
	fresh_world();
	g_collision_segment_start_world_x = 0;
	g_collision_segment_start_world_y = 0;
	g_collision_segment_start_world_z = 0;
	g_collision_probe_world_x = -1000;
	g_collision_probe_world_y = -1000;
	g_collision_probe_world_z = -1000;
	g_collision_sweep_start_x = -500;
	g_collision_sweep_start_y = -500;
	g_collision_sweep_start_z = -500;
	g_collision_sweep_end_x = -600;
	g_collision_sweep_end_y = -600;
	g_collision_sweep_end_z = -600;
	XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(0, 1), -1);
	g_collision_sweep_start_x = 500;
	g_collision_sweep_end_x = 600;
	XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(0, 1), 0);
	g_collision_sweep_start_x = -500;
	g_collision_sweep_end_x = -600;
	g_collision_sweep_start_y = -500 - 140000;
	g_collision_sweep_end_y = -600 - 140000;
	XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(0, 1), 0);
	g_collision_sweep_start_y = -500;
	g_collision_sweep_end_y = -600;
	g_collision_sweep_start_z = -500 - 140000;
	g_collision_sweep_end_z = -600 - 140000;
	XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(0, 1), 0);
}

/* Comment: the target's extent is halved for a shot (not a countermeasure);
 * for a starfighter hit by a shot it is half again larger while its shields
 * are up; in internet play a player's cannon shot on a player's starfighter
 * grows it by craft type (doubled for an A-wing, TIE Interceptor or TIE
 * Advanced, 1.75 times for a TIE Fighter, unchanged for a Y-wing or B-wing,
 * else 1.5 times). The reach in Y of the hit is the box of 3/8 of it. */
static void check_swept_pair_target_extent(void)
{
	/* The craft target, hit by another craft, then by a shot with shields
	 * down and up. */
	fresh_world();
	XVT_ASSERT_INT_EQ(pair_hit_at(1, 149) != 0, 1);
	XVT_ASSERT_INT_EQ(pair_hit_at(1, 151), 0);
	place_shot(TEST_SHOT, PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0, 0, 0, 0, 0,
		   0, 0);
	g_test_craft[1].shield_energy[0] = 0;
	g_test_craft[1].shield_energy[1] = 0;
	XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(TEST_SHOT, 1), 0);
	pair_hit_at(1, 149);
	XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(TEST_SHOT, 1), -1);
	/* Shields up: 600, box 225. */
	g_test_craft[1].shield_energy[0] = 1;
	for (int y = 220; y <= 230; y += 5) {
		pair_hit_at(1, y);
		XVT_ASSERT_INT_EQ(
			collide_test_swept_pair_collision(TEST_SHOT, 1) != 0,
			y <= 225);
	}

	/* A shot as target: half the extent, 200 and a box of 75. */
	fresh_world();
	place_shot(TEST_OTHER_SHOT, PROJECTILE_OBJECT_TYPE_REBEL_LASER, 1, 0, 0,
		   0, 0, 0, 0);
	g_object_type_table[PROJECTILE_OBJECT_TYPE_REBEL_LASER]
		.max_bounds_extent = 400;
	for (int y = 70; y <= 80; y += 5) {
		pair_hit_at(TEST_OTHER_SHOT, y);
		XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(
					  0, TEST_OTHER_SHOT) != 0,
				  y <= 75);
	}
	/* A countermeasure shot is not halved: a box of 150. */
	g_test_objects[TEST_OTHER_SHOT].object_type =
		COUNTERMEASURE_PROJECTILE_OBJECT_TYPE;
	g_object_type_table[COUNTERMEASURE_PROJECTILE_OBJECT_TYPE]
		.max_bounds_extent = 400;
	pair_hit_at(TEST_OTHER_SHOT, 149);
	XVT_ASSERT_INT_EQ(
		collide_test_swept_pair_collision(0, TEST_OTHER_SHOT) != 0, 1);
	pair_hit_at(TEST_OTHER_SHOT, 151);
	XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(0, TEST_OTHER_SHOT),
			  0);
	g_object_type_table[PROJECTILE_OBJECT_TYPE_REBEL_LASER]
		.max_bounds_extent = 0;
}

/* The internet play figures: reach in Y of a cannon shot of a player on a
 * player's craft of object type type. */
static int internet_reach(int type)
{
	int reach = 0;
	for (int y = 0; y < 700; ++y) {
		fresh_world();
		g_internet_play_enabled = 1;
		g_test_objects[1].object_type = (uint8_t)type;
		g_object_type_table[type].max_bounds_extent = 400;
		g_test_objects[1].genus_id = CRAFT_GENUS_STARFIGHTER;
		player_flies(1, 1);
		place_shot(TEST_SHOT, PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0, 0,
			   0, 0, 0, 0, 0);
		g_test_objects[TEST_SHOT].genus_id =
			CRAFT_GENUS_PLAYER_PROJECTILE;
		g_test_craft[1].shield_energy[0] = 1;
		pair_hit_at(1, y);
		if (collide_test_swept_pair_collision(TEST_SHOT, 1) != 0) {
			reach = y;
		}
	}
	g_internet_play_enabled = 0;
	return reach;
}

static void check_swept_pair_internet_play(void)
{
	/* Shields up make 600; internet play grows that by craft type. The
	 * box is 3/8 of the extent: 225 for 600. */
	XVT_ASSERT_INT_EQ(internet_reach(CRAFT_SPECIES_Y_WING), 225);
	XVT_ASSERT_INT_EQ(internet_reach(CRAFT_SPECIES_B_WING), 225);
	int a_wing = internet_reach(CRAFT_SPECIES_A_WING);
	XVT_ASSERT_INT_EQ(internet_reach(CRAFT_SPECIES_TIE_INTERCEPTOR),
			  a_wing);
	XVT_ASSERT_INT_EQ(internet_reach(CRAFT_SPECIES_TIE_ADVANCED), a_wing);
	int tie = internet_reach(CRAFT_SPECIES_TIE_FIGHTER);
	int other = internet_reach(TEST_FIGHTER_TYPE);
	/* Doubled > 1.75 times > 1.5 times > unchanged. */
	XVT_ASSERT_TRUE(a_wing > tie);
	XVT_ASSERT_TRUE(tie > other);
	XVT_ASSERT_TRUE(other > 225);
	XVT_ASSERT_TRUE(a_wing >= 440 && a_wing <= 460);
	XVT_ASSERT_TRUE(tie >= 385 && tie <= 400);
	XVT_ASSERT_TRUE(other >= 330 && other <= 345);
	/* Not for a computer-flown craft, nor for a warhead. */
	fresh_world();
	g_internet_play_enabled = 1;
	place_shot(TEST_SHOT, PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0, 0, 0, 0, 0,
		   0, 0);
	g_test_objects[TEST_SHOT].genus_id = CRAFT_GENUS_PLAYER_PROJECTILE;
	g_test_craft[1].shield_energy[0] = 1;
	pair_hit_at(1, 300);
	XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(TEST_SHOT, 1), 0);
	player_flies(1, 1);
	g_test_objects[TEST_SHOT].object_type =
		WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE;
	pair_hit_at(1, 300);
	XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(TEST_SHOT, 1), 0);
	g_internet_play_enabled = 0;
}

/* Comment: a target of extent 1,095 or more (or a Container Class H) gets the
 * polygon test: a source that has not moved gets 0 from it. */
static void check_swept_pair_large_target(void)
{
	fresh_world();
	g_object_type_table[TEST_FIGHTER_TYPE].max_bounds_extent = 1095;
	pair_hit_at(1, 0);
	g_collision_probe_world_x = g_collision_segment_start_world_x;
	g_collision_probe_world_y = g_collision_segment_start_world_y;
	g_collision_probe_world_z = g_collision_segment_start_world_z;
	XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(0, 1), 0);
	/* A container of class H is tested the same way... it is tested with
	 * the polygon test whatever its extent. */
	fresh_world();
	g_test_objects[1].object_type = CRAFT_SPECIES_CONTAINER_CLASS_H;
	pair_hit_at(1, 0);
	g_collision_probe_world_x = g_collision_segment_start_world_x;
	XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(0, 1), 0);
	/* Just under: the box test, and a moving source hits. */
	fresh_world();
	g_object_type_table[TEST_FIGHTER_TYPE].max_bounds_extent = 1094;
	XVT_ASSERT_INT_EQ(pair_hit_at(1, 0), -1);
}

/* Comment: collide_would_shot_hit_target sweeps the shot from the hardpoint
 * along the source's move vector for the shot's whole life against the
 * target moving along its own; 0 for a miss, nonzero for a hit; it puts back
 * every g_collision* global and leaves g_collision_is_aim_prediction 0. */
static void aim_world(void)
{
	fresh_world();
	/* Craft 0 at the origin flying along +Y at speed 0 (the shot's speed
	 * alone carries it), its first cannon group firing laser shots. */
	g_test_mobiles[0].move_vector_dirty = 0;
	g_test_mobiles[0].move_y = 0x7FFF;
	g_test_mobiles[0].cached_side_x = FORWARD_X;
	g_test_mobiles[0].cached_fwd_y = FORWARD_X;
	g_test_mobiles[0].cached_up_z = FORWARD_X;
	g_test_craft[0].model_index = TEST_MODEL;
	g_model_defs[TEST_MODEL].laser_group_weapon_type[0] =
		PROJECTILE_OBJECT_TYPE_REBEL_LASER;
	memset(g_model_defs[TEST_MODEL].weapon_hardpoints, 0,
	       sizeof g_model_defs[TEST_MODEL].weapon_hardpoints);
	g_players[0].selected_weapon_bank = 0;
	g_players[0].selected_target_component = 0;
	g_test_mobiles[1].move_vector_dirty = 0;
	g_test_mobiles[1].cached_side_x = FORWARD_X;
	g_test_mobiles[1].cached_fwd_y = FORWARD_X;
	g_test_mobiles[1].cached_up_z = FORWARD_X;
	g_test_craft[0].weapon_slots[0].laser_charge = 0;
}

/* How far the laser flies in its life, by the figures of the game's shot
 * table: the speed times the life, as the aim code computes it. */
static int laser_reach(void)
{
	int index = PROJECTILE_OBJECT_TYPE_REBEL_LASER -
		    PROJECTILE_OBJECT_TYPE_FIRST;
	int ticks = SIMULATION_TICKS_PER_SECOND *
		    g_projectile_type_data.lifetime_seconds[index];
	ticks += (uint16_t)math2_fraction(
		SIMULATION_TICKS_PER_SECOND,
		g_projectile_type_data.lifetime_frac_q16[index]);
	return ticks *
	       ((4660 * g_projectile_type_data.speed[index] + 128) >> 8) /
	       SIMULATION_TICKS_PER_SECOND;
}

static void check_would_shot_hit_target(void)
{
	aim_world();
	int reach = laser_reach();
	XVT_ASSERT_TRUE(reach > 1000);
	/* The sentinels the call must put back. */
	g_collision_segment_start_world_x = 11;
	g_collision_probe_world_y = 22;
	g_collision_sweep_start_z = 33;
	g_collision_sweep_end_x = 44;
	g_collision_hit_offset_y = 55;

	/* A target on the line of fire, half way: a hit. */
	place(1, 0, reach / 2, 0);
	XVT_ASSERT_TRUE(collide_would_shot_hit_target(0, 1, 0) != 0);
	XVT_ASSERT_INT_EQ(g_collision_segment_start_world_x, 11);
	XVT_ASSERT_INT_EQ(g_collision_probe_world_y, 22);
	XVT_ASSERT_INT_EQ(g_collision_sweep_start_z, 33);
	XVT_ASSERT_INT_EQ(g_collision_sweep_end_x, 44);
	XVT_ASSERT_INT_EQ(g_collision_hit_offset_y, 55);
	XVT_ASSERT_INT_EQ(g_collision_is_aim_prediction, 0);
	/* Out of reach: a miss. */
	place(1, 0, reach * 3, 0);
	XVT_ASSERT_INT_EQ(collide_would_shot_hit_target(0, 1, 0), 0);
	XVT_ASSERT_INT_EQ(g_collision_is_aim_prediction, 0);
	/* Behind the craft; or off the line of fire. */
	place(1, 0, -reach / 2, 0);
	XVT_ASSERT_INT_EQ(collide_would_shot_hit_target(0, 1, 0), 0);
	place(1, 2000, reach / 2, 0);
	XVT_ASSERT_INT_EQ(collide_would_shot_hit_target(0, 1, 0), 0);

	/* A target that crosses the line by the time the shot gets there is
	 * hit; one moving away fast enough is not. */
	place(1, -3000, reach / 2, 0);
	g_test_mobiles[1].move_vector_dirty = 0;
	g_test_mobiles[1].move_x = FORWARD_X;
	g_test_mobiles[1].move_y = 0;
	g_test_mobiles[1].speed = 1;
	XVT_ASSERT_INT_EQ(collide_would_shot_hit_target(0, 1, 0), 0);
	g_test_mobiles[1].speed = 0;
	g_test_mobiles[1].move_x = 0;
	place(1, 0, reach / 2, 0);
	g_test_mobiles[1].move_y = FORWARD_X;
	g_test_mobiles[1].speed = 6000;
	XVT_ASSERT_INT_EQ(collide_would_shot_hit_target(0, 1, 0), 0);
	g_test_mobiles[1].speed = 0;

	/* The hardpoint: the shot starts there. A target 300 ahead of a
	 * hardpoint at 300 is hit; behind it, not. */
	g_model_defs[TEST_MODEL].weapon_hardpoints[1].y = 1000;
	place(1, 0, 1000, 0);
	XVT_ASSERT_TRUE(collide_would_shot_hit_target(0, 1, 1) != 0);
	place(1, 0, 500, 0);
	XVT_ASSERT_INT_EQ(collide_would_shot_hit_target(0, 1, 1), 0);
	/* On an Imperial Star Destroyer the hardpoint lies twice as far out. */
	g_test_objects[0].object_type = CRAFT_SPECIES_IMPERIAL_STAR_DESTROYER;
	g_object_type_table[CRAFT_SPECIES_IMPERIAL_STAR_DESTROYER]
		.max_bounds_extent = 400;
	place(1, 0, 1000, 0);
	XVT_ASSERT_INT_EQ(collide_would_shot_hit_target(0, 1, 1), 0);
	place(1, 0, 2000, 0);
	XVT_ASSERT_TRUE(collide_would_shot_hit_target(0, 1, 1) != 0);
}

/* A static target on the line of fire is tested with the static test (the
 * firer's own aim must be at it unless the firer is a player's). */
static void check_would_shot_hit_static(void)
{
	aim_world();
	int reach = laser_reach();
	place_mine(0, reach / 2, 0);
	g_test_craft[0].ai_controller.target_obj_idx = TEST_MINE;
	XVT_ASSERT_TRUE(collide_would_shot_hit_target(0, TEST_MINE, 0) != 0);
	XVT_ASSERT_INT_EQ(g_collision_is_aim_prediction, 0);
	place_mine(0, reach * 3, 0);
	XVT_ASSERT_INT_EQ(collide_would_shot_hit_target(0, TEST_MINE, 0), 0);
	place_mine(3000, reach / 2, 0);
	XVT_ASSERT_INT_EQ(collide_would_shot_hit_target(0, TEST_MINE, 0), 0);
	/* A computer-flown firer aimed elsewhere misses it. */
	place_mine(0, reach / 2, 0);
	g_test_craft[0].ai_controller.target_obj_idx = 1;
	XVT_ASSERT_INT_EQ(collide_would_shot_hit_target(0, TEST_MINE, 0), 0);
	/* A player's craft needs no aim at it. */
	player_flies(0, 0);
	XVT_ASSERT_TRUE(collide_would_shot_hit_target(0, TEST_MINE, 0) != 0);
}

/* Comment: a target at the center of the selected component of the local
 * player's selected target is aimed at, not the center of the craft. */
static void check_would_shot_hit_component(void)
{
	static const int types[2] = {MESH_COMPONENT_01_MAIN_HULL,
				     MESH_COMPONENT_05_LASR_GUN};
	static const int center_x[2] = {0, 600};
	static const int flags[2] = {0, 0};
	aim_world();
	int reach = laser_reach();
	craft_model(2, types, center_x, flags);
	/* The target's center is off the line of fire by 600, the gun's on
	 * it. */
	place(1, -600, reach / 2, 0);
	XVT_ASSERT_INT_EQ(collide_would_shot_hit_target(0, 1, 0), 0);
	g_players[0].selected_target_component = 1;
	XVT_ASSERT_TRUE(collide_would_shot_hit_target(0, 1, 0) != 0);
	/* And the other way about. */
	place(1, 0, reach / 2, 0);
	XVT_ASSERT_INT_EQ(collide_would_shot_hit_target(0, 1, 0), 0);
	g_players[0].selected_target_component = 0;
}

/* Comment: collide_craftstarshipcollision looks ahead from a craft along its
 * move vector for a freighter, starship or platform on its way (moving
 * itself), then for a static object in its proximity list; the first hit's
 * slot, else UINT16_MAX. */
static void check_craft_starship_collision(void)
{
	g_sim_steps_per_second = 10;
	/* Craft 0 flies along +Y at 1,000: 77 or so units a step, 20 steps in
	 * two seconds. */
	fresh_world();
	g_test_mobiles[0].move_vector_dirty = 0;
	g_test_mobiles[0].move_y = FORWARD_X;
	g_test_mobiles[0].speed = 1000;
	g_test_objects[1].genus_id = CRAFT_GENUS_STARSHIP;
	place(1, 0, 1000, 0);
	g_test_mobiles[1].move_vector_dirty = 0;
	XVT_ASSERT_INT_EQ(collide_craftstarshipcollision(0, 2), 1);
	/* Out of reach, off the path, behind. */
	place(1, 0, 9000, 0);
	XVT_ASSERT_INT_EQ(collide_craftstarshipcollision(0, 2), UINT16_MAX);
	place(1, 2000, 1000, 0);
	XVT_ASSERT_INT_EQ(collide_craftstarshipcollision(0, 2), UINT16_MAX);
	place(1, 0, -1000, 0);
	XVT_ASSERT_INT_EQ(collide_craftstarshipcollision(0, 2), UINT16_MAX);
	/* The lookahead decides: one second reaches about 770 units, a target
	 * at 1,000 only in two. */
	place(1, 0, 700, 0);
	XVT_ASSERT_INT_EQ(collide_craftstarshipcollision(0, 1), 1);
	place(1, 0, 1000, 0);
	XVT_ASSERT_INT_EQ(collide_craftstarshipcollision(0, 1), UINT16_MAX);
	place(1, 0, 3000, 0);
	XVT_ASSERT_INT_EQ(collide_craftstarshipcollision(0, 1), UINT16_MAX);
	/* Other genera are not looked for; a platform and a freighter are. */
	place(1, 0, 1000, 0);
	g_test_objects[1].genus_id = CRAFT_GENUS_STARFIGHTER;
	XVT_ASSERT_INT_EQ(collide_craftstarshipcollision(0, 2), UINT16_MAX);
	g_test_objects[1].genus_id = CRAFT_GENUS_PLATFORM;
	XVT_ASSERT_INT_EQ(collide_craftstarshipcollision(0, 2), 1);
	g_test_objects[1].genus_id = CRAFT_GENUS_FREIGHTER;
	XVT_ASSERT_INT_EQ(collide_craftstarshipcollision(0, 2), 1);
	g_test_objects[1].object_type = 0;
	XVT_ASSERT_INT_EQ(collide_craftstarshipcollision(0, 2), UINT16_MAX);
	g_test_objects[1].object_type = TEST_FIGHTER_TYPE;
	/* The ship moving too: out of the way of the craft's path. */
	g_test_mobiles[1].move_x = FORWARD_X;
	g_test_mobiles[1].speed = 20000;
	XVT_ASSERT_INT_EQ(collide_craftstarshipcollision(0, 2), UINT16_MAX);
	g_test_mobiles[1].speed = 0;

	/* A static object in the craft's list, on its way. */
	place(1, 5000, 9000, 0);
	place_mine(0, 1200, 0);
	g_test_craft[0].ai_controller.target_obj_idx = TEST_MINE;
	list_holds(0, TEST_MINE);
	XVT_ASSERT_INT_EQ(collide_craftstarshipcollision(0, 2), TEST_MINE);
	/* A starship in the way is found first. */
	place(1, 0, 1000, 0);
	g_test_objects[1].genus_id = CRAFT_GENUS_STARSHIP;
	XVT_ASSERT_INT_EQ(collide_craftstarshipcollision(0, 2), 1);
	/* A mine off the path; a slot that is not a static one. */
	g_test_objects[1].genus_id = CRAFT_GENUS_STARFIGHTER;
	place_mine(4000, 1200, 0);
	XVT_ASSERT_INT_EQ(collide_craftstarshipcollision(0, 2), UINT16_MAX);
	place_mine(0, 1200, 0);
	g_test_mobiles[0].proximity_list.obj_idx[0] = 1;
	XVT_ASSERT_INT_EQ(collide_craftstarshipcollision(0, 2), UINT16_MAX);
	g_sim_steps_per_second = 0;
}

/* ------------------------------------------------------------------------ */
/* The model walk: collide_check_swept_model_collision and what it calls,
 * on models with faces. */

enum {
	FACE_MESHES = 2,
};

/* Each mesh of a face model: a square 200 across lying flat at height z of
 * its own axes, its bounds (the last two vertices) a little larger. */
static struct opt_vector g_face_vertices[FACE_MESHES][6];
static union {
	struct opt_packed_face_data data;
	unsigned char bytes[sizeof(int) +
			    sizeof(struct opt_packed_face_record) +
			    3 * sizeof(struct opt_vector)];
} g_face_data[FACE_MESHES];
static struct opt_node g_face_vertex_nodes[FACE_MESHES];
static struct opt_node g_face_face_nodes[FACE_MESHES];
static struct opt_node g_face_root_nodes[FACE_MESHES];
static struct opt_node *g_face_children[FACE_MESHES][2];
static struct opt_node *g_face_roots[FACE_MESHES];
static struct collide_walk_rotation {
	struct opt_vector origin;
	struct opt_vector axis;
} g_face_rotation;

/* Mesh i's square at height z; its root node is of type root_type (a type the
 * walk passes through, or OPT_ROTSCALE with g_face_rotation). */
static void face_mesh(int i, float z, opt_node_type root_type)
{
	struct opt_vector *vertices = g_face_vertices[i];
	vertices[0] = (struct opt_vector){-100, -100, z};
	vertices[1] = (struct opt_vector){100, -100, z};
	vertices[2] = (struct opt_vector){100, 100, z};
	vertices[3] = (struct opt_vector){-100, 100, z};
	vertices[4] = (struct opt_vector){-120, -120, z - 5};
	vertices[5] = (struct opt_vector){120, 120, z + 5};
	memset(&g_face_data[i], 0, sizeof g_face_data[i]);
	struct opt_packed_face_record *record = &g_face_data[i].data.records[0];
	record->vertex_indices[0] = 0;
	record->vertex_indices[1] = 1;
	record->vertex_indices[2] = 2;
	record->vertex_indices[3] = 3;
	struct opt_vector *normal =
		(struct opt_vector *)&g_face_data[i].data.records[1];
	*normal = (struct opt_vector){0, 0, 1};
	memset(&g_face_vertex_nodes[i], 0, sizeof g_face_vertex_nodes[i]);
	g_face_vertex_nodes[i].node_type = OPT_MESHVERTS;
	g_face_vertex_nodes[i].payload_count = 6;
	g_face_vertex_nodes[i].payload = vertices;
	memset(&g_face_face_nodes[i], 0, sizeof g_face_face_nodes[i]);
	g_face_face_nodes[i].node_type = OPT_FACEDATA;
	g_face_face_nodes[i].payload_count = 1;
	g_face_face_nodes[i].payload = &g_face_data[i];
	g_face_children[i][0] = &g_face_vertex_nodes[i];
	g_face_children[i][1] = &g_face_face_nodes[i];
	memset(&g_face_root_nodes[i], 0, sizeof g_face_root_nodes[i]);
	g_face_root_nodes[i].node_type = root_type;
	g_face_root_nodes[i].child_count = 2;
	g_face_root_nodes[i].p_children = g_face_children[i];
	if (root_type == OPT_ROTSCALE) {
		g_face_root_nodes[i].payload = &g_face_rotation;
	}
	g_face_roots[i] = &g_face_root_nodes[i];
}

/* Gives object type type (craft 1's, or the mine's) a model of count face
 * meshes, built by face_mesh, with the boxes the model test checks first. */
static void face_model(int type, int count)
{
	struct optimized_poly_object *model =
		memory_get_handle_block(g_craft_model_handle);
	memset(model, 0, sizeof *model);
	model->self_marker = model;
	model->root_node_count = count;
	model->root_nodes = g_face_roots;
	for (int i = 0; i < count; ++i) {
		struct mesh_descriptor *descriptor =
			&g_craft_model_descriptors[i];
		memset(descriptor, 0, sizeof *descriptor);
		descriptor->mesh_type = MESH_COMPONENT_01_MAIN_HULL;
		descriptor->box_min = g_face_vertices[i][4];
		descriptor->box_max = g_face_vertices[i][5];
		g_object_type_mesh_cache[type].mesh_descriptors[i] = descriptor;
		g_object_type_mesh_cache[type].mesh_types[i] =
			MESH_COMPONENT_01_MAIN_HULL;
	}
	g_object_type_mesh_cache[type].mesh_count = count;
	g_loaded_models[type] = g_craft_model_handle;
	g_object_type_table[type].asset_flags |= 1;
}

/* Craft 1 at (20,000, 0, 0), axes along the world's (forward reversed, as the
 * walk takes it), with a one-mesh model whose square lies at its center. */
static void face_world(void)
{
	fresh_world();
	g_test_mobiles[1].cached_side_x = FORWARD_X;
	g_test_mobiles[1].cached_fwd_y = FORWARD_X;
	g_test_mobiles[1].cached_up_z = FORWARD_X;
	g_test_mobiles[1].orient_matrix_dirty = 0;
	g_collide_sweep_reject_near_start_hits = 0;
	g_turret_fire_hull_mesh_ordinal = 0;
	face_mesh(0, 0, OPT_NODEREF + 100);
	face_model(TEST_FIGHTER_TYPE, 1);
	g_test_craft[1].component_hp[0] = 20;
	g_test_craft[1].component_hp[1] = 20;
}

/* The model test of source 0 on craft 1 for a sweep from (sx, sy, sz) to
 * (ex, ey, ez), relative to craft 1's center. */
static int walk(int sx, int sy, int sz, int ex, int ey, int ez)
{
	g_collision_segment_start_world_x = 20000 + sx;
	g_collision_segment_start_world_y = sy;
	g_collision_segment_start_world_z = sz;
	g_collision_probe_world_x = 20000 + ex;
	g_collision_probe_world_y = ey;
	g_collision_probe_world_z = ez;
	return collide_check_swept_model_collision(0, 1);
}

/* Comment: the model test returns the 1-based ordinal of the mesh with the
 * nearest hit, or 0; on a hit g_collisionHitOffset* is the source's travel
 * to the hit fraction less 0.1. */
static void check_model_walk_hits_face(void)
{
	/* Straight down through the square's middle at half way: 0.4 of the
	 * travel (400 down). */
	face_world();
	XVT_ASSERT_INT_EQ(walk(0, 0, 200, 0, 0, -200), 1);
	XVT_ASSERT_INT_EQ(g_collision_hit_offset_x, 0);
	XVT_ASSERT_INT_EQ(g_collision_hit_offset_y, 0);
	XVT_ASSERT_TRUE(g_collision_hit_offset_z >= -162 &&
			g_collision_hit_offset_z <= -158);
	XVT_ASSERT_INT_EQ(g_cur_craft == &g_test_craft[1], 1);
	/* Up through it, off to one side: offsets along the whole move. */
	XVT_ASSERT_INT_EQ(walk(50, 20, -200, 50, 20, 200), 1);
	XVT_ASSERT_TRUE(g_collision_hit_offset_z >= 158 &&
			g_collision_hit_offset_z <= 162);
	/* Beside the square, but within its box: no hit. */
	XVT_ASSERT_INT_EQ(walk(110, 0, 200, 110, 0, -200), 0);
	/* Beside the box. */
	XVT_ASSERT_INT_EQ(walk(150, 0, 200, 150, 0, -200), 0);
	/* Stopping short of the plane; over the box wholly. */
	XVT_ASSERT_INT_EQ(walk(0, 0, 200, 0, 0, 50), 0);
	XVT_ASSERT_INT_EQ(walk(0, 0, 200, 0, 0, 100), 0);
	XVT_ASSERT_INT_EQ(walk(0, 0, 20, 0, 0, 40), 0);
	/* Past the plane both ends: no crossing. */
	XVT_ASSERT_INT_EQ(walk(0, 0, -50, 0, 0, -200), 0);
}

/* Comment: a destroyed mesh (component_hp 0) is skipped; when the source is
 * the target, laser turret and gun meshes are skipped; the hit nearest
 * along the sweep wins. */
static void check_model_walk_skips_and_nearest(void)
{
	face_world();
	g_test_craft[1].component_hp[0] = 0;
	XVT_ASSERT_INT_EQ(walk(0, 0, 200, 0, 0, -200), 0);

	/* Two meshes: the sweep meets the upper (2) first, then the lower
	 * (1). */
	face_world();
	face_mesh(0, 0, OPT_NODEREF + 100);
	face_mesh(1, 100, OPT_NODEREF + 100);
	face_model(TEST_FIGHTER_TYPE, 2);
	XVT_ASSERT_INT_EQ(walk(0, 0, 200, 0, 0, -200), 2);
	/* The upper one destroyed: the lower. */
	g_test_craft[1].component_hp[1] = 0;
	XVT_ASSERT_INT_EQ(walk(0, 0, 200, 0, 0, -200), 1);
	/* Coming up from below, the lower is first. */
	g_test_craft[1].component_hp[1] = 20;
	XVT_ASSERT_INT_EQ(walk(0, 0, -200, 0, 0, 200), 1);

	/* A gun or turret does not stop the target's own shot. */
	static const int gun_types[] = {MESH_COMPONENT_04_LASR_TUR,
					MESH_COMPONENT_05_LASR_GUN,
					MESH_COMPONENT_21_ROTATING_LASR_TUR,
					MESH_COMPONENT_03_FUSELAGE};
	for (size_t i = 0; i < sizeof gun_types / sizeof gun_types[0]; ++i) {
		face_world();
		g_object_type_mesh_cache[TEST_FIGHTER_TYPE].mesh_types[0] =
			gun_types[i];
		g_collision_segment_start_world_x = 20000;
		g_collision_segment_start_world_y = 0;
		g_collision_segment_start_world_z = 200;
		g_collision_probe_world_x = 20000;
		g_collision_probe_world_y = 0;
		g_collision_probe_world_z = -200;
		XVT_ASSERT_INT_EQ(collide_check_swept_model_collision(1, 1),
				  gun_types[i] == MESH_COMPONENT_03_FUSELAGE);
		XVT_ASSERT_INT_EQ(collide_check_swept_model_collision(0, 1), 1);
	}
	/* A Super Star Destroyer's own shot passes the mesh after the one
	 * named g_turret_fire_hull_mesh_ordinal. */
	face_world();
	g_test_objects[1].object_type = 54;
	face_model(54, 1);
	g_turret_fire_hull_mesh_ordinal = 0;
	g_collision_segment_start_world_x = 20000;
	g_collision_segment_start_world_y = 0;
	g_collision_segment_start_world_z = 200;
	g_collision_probe_world_x = 20000;
	g_collision_probe_world_y = 0;
	g_collision_probe_world_z = -200;
	XVT_ASSERT_INT_EQ(collide_check_swept_model_collision(1, 1), 0);
	g_turret_fire_hull_mesh_ordinal = 5;
	XVT_ASSERT_INT_EQ(collide_check_swept_model_collision(1, 1), 1);
	g_object_type_table[54].asset_flags &= ~1;
	g_loaded_models[54] = 0;
}

/* Comment: with g_collide_sweep_reject_near_start_hits set, a face the sweep
 * starts at (a fraction under 0.1) does not count. */
static void check_model_walk_near_start(void)
{
	face_world();
	XVT_ASSERT_INT_EQ(walk(0, 0, 5, 0, 0, -395), 1);
	g_collide_sweep_reject_near_start_hits = 1;
	XVT_ASSERT_INT_EQ(walk(0, 0, 5, 0, 0, -395), 0);
	XVT_ASSERT_INT_EQ(walk(0, 0, 100, 0, 0, -300), 1);
	/* The offset never goes below 0. */
	g_collide_sweep_reject_near_start_hits = 0;
	XVT_ASSERT_INT_EQ(walk(0, 0, 5, 0, 0, -395), 1);
	XVT_ASSERT_INT_EQ(g_collision_hit_offset_z, 0);
	g_collide_sweep_reject_near_start_hits = 0;
}

/* Comment: a rotating mesh is tested turned by its mesh_rotation for a source
 * no player owns, and unturned for a player's source. A quarter turn
 * about Y takes a sweep along X to one along Z. */
static void check_model_walk_rotating_mesh(void)
{
	for (int owned = 0; owned < 2; ++owned) {
		face_world();
		g_face_rotation.origin = (struct opt_vector){0, 0, 0};
		g_face_rotation.axis = (struct opt_vector){0, 32768, 0};
		face_mesh(0, 0, OPT_ROTSCALE);
		face_model(TEST_FIGHTER_TYPE, 1);
		g_test_craft[1].mesh_rotation[0] = 64;
		if (owned) {
			player_flies(0, 0);
		}
		/* In the square's own plane, along X from outside it: only a
		 * turned sweep crosses it. */
		XVT_ASSERT_INT_EQ(walk(-200, 0, 0, 200, 0, 0), owned ? 0 : 1);
		/* With no rotation, the sweep is as it was. */
		g_test_craft[1].mesh_rotation[0] = 0;
		XVT_ASSERT_INT_EQ(walk(0, 0, 200, 0, 0, -200), 1);
		XVT_ASSERT_INT_EQ(walk(-200, 0, 0, 200, 0, 0), 0);
	}
}

/* Comment: it returns 0 for an object type whose asset_flags bit 0 is clear
 * and when the model will not lock; a static object with a model is walked in
 * the axes of the view. */
static void check_model_walk_unavailable_and_static(void)
{
	face_world();
	g_object_type_table[TEST_FIGHTER_TYPE].asset_flags &= ~1;
	XVT_ASSERT_INT_EQ(walk(0, 0, 200, 0, 0, -200), 0);
	XVT_ASSERT_TRUE(
		line_holds("combat.hull_test_skipped", "reason=\"sprite\""));

	face_world();
	g_loaded_models[TEST_FIGHTER_TYPE] = 0;
	XVT_ASSERT_INT_EQ(walk(0, 0, 200, 0, 0, -200), 0);
	XVT_ASSERT_TRUE(line_holds("combat.hull_test_skipped",
				   "reason=\"model_not_loaded\""));

	/* A static object's model (type 40 here), walked in the axes of the
	 * view: a sweep straight through its center along each world axis
	 * in turn crosses its square for exactly one of them, the one
	 * across it. */
	face_world();
	place_mine(20000, 0, 0);
	g_test_objects[TEST_MINE].object_type = 40;
	face_model(40, 1);
	int crossings = 0;
	for (int axis = 0; axis < 3; ++axis) {
		g_collision_segment_start_world_x =
			20000 + (axis == 0 ? 200 : 0);
		g_collision_segment_start_world_y = axis == 1 ? 200 : 0;
		g_collision_segment_start_world_z = axis == 2 ? 200 : 0;
		g_collision_probe_world_x = 20000 - (axis == 0 ? 200 : 0);
		g_collision_probe_world_y = axis == 1 ? -200 : 0;
		g_collision_probe_world_z = axis == 2 ? -200 : 0;
		crossings +=
			collide_check_swept_model_collision(0, TEST_MINE) == 1;
	}
	XVT_ASSERT_INT_EQ(crossings, 1);
	g_loaded_models[40] = 0;
	g_object_type_table[40].asset_flags &= ~1;
	g_object_type_mesh_cache[40].mesh_count = 0;
}

/* The distance of point from the plane through vertex with the unit normal
 * normal. */
static float plane_distance(const float normal[3], const float vertex[3],
			    const float point[3])
{
	return normal[0] * (point[0] - vertex[0]) +
	       normal[1] * (point[1] - vertex[1]) +
	       normal[2] * (point[2] - vertex[2]);
}

/* Comment: the crossing of a segment with a face's plane, off the origin and
 * leaning: a distance within 10 of the plane counts as on it (the start is
 * checked first); the fraction is where the signed distances meet. */
static void check_face_plane_oblique(void)
{
	const float normal[3] = {0.6f, 0.0f, 0.8f};
	const float vertex[3] = {10.0f, -40.0f, -20.0f};
	/* Points at signed distances 50, 9, -9, -50 along the normal from the
	 * plane's point (10, 0, -20), moved along the plane by (0, 15, 0). */
	static const float distances[] = {50.0f,  9.0f,	 -9.0f,
					  -50.0f, 11.0f, -11.0f};
	float points[6][3];
	for (int i = 0; i < 6; ++i) {
		points[i][0] = 10.0f + 0.6f * distances[i];
		points[i][1] = -25.0f;
		points[i][2] = -20.0f + 0.8f * distances[i];
		XVT_ASSERT_TRUE(
			fabsf(plane_distance(normal, vertex, points[i]) -
			      distances[i]) < 0.01f);
	}
	float t = 7.0f;
	/* Opposite sides: 50 and -50 cross half way; 50 and -11 at 50/61. */
	XVT_ASSERT_INT_EQ(collide_intersect_segment_with_face_plane(
				  normal, vertex, points[0], points[3], &t),
			  1);
	XVT_ASSERT_CLOSE(t, 0.5, 1e-4, "equal distances either side");
	XVT_ASSERT_INT_EQ(collide_intersect_segment_with_face_plane(
				  normal, vertex, points[0], points[5], &t),
			  1);
	XVT_ASSERT_CLOSE(t, 50.0 / 61.0, 1e-4, "distances 50 and -11");
	XVT_ASSERT_INT_EQ(collide_intersect_segment_with_face_plane(
				  normal, vertex, points[3], points[0], &t),
			  1);
	XVT_ASSERT_CLOSE(t, 0.5, 1e-4, "the other way");
	/* Both on one side: no crossing, and t is left alone. */
	t = 7.0f;
	XVT_ASSERT_INT_EQ(collide_intersect_segment_with_face_plane(
				  normal, vertex, points[0], points[4], &t),
			  0);
	XVT_ASSERT_INT_EQ(collide_intersect_segment_with_face_plane(
				  normal, vertex, points[3], points[5], &t),
			  0);
	XVT_ASSERT_CLOSE(t, 7.0, 1e-9, "t left alone");
	/* A start within 10 of the plane is on it: t 0, whichever the end. */
	XVT_ASSERT_INT_EQ(collide_intersect_segment_with_face_plane(
				  normal, vertex, points[1], points[0], &t),
			  1);
	XVT_ASSERT_CLOSE(t, 0.0, 1e-9, "start on the plane");
	t = 7.0f;
	XVT_ASSERT_INT_EQ(collide_intersect_segment_with_face_plane(
				  normal, vertex, points[2], points[3], &t),
			  1);
	XVT_ASSERT_CLOSE(t, 0.0, 1e-9, "start just under the plane");
	/* An end within 10 is on it: t 1. */
	t = 7.0f;
	XVT_ASSERT_INT_EQ(collide_intersect_segment_with_face_plane(
				  normal, vertex, points[0], points[1], &t),
			  1);
	XVT_ASSERT_CLOSE(t, 1.0, 1e-9, "end on the plane");
	XVT_ASSERT_INT_EQ(collide_intersect_segment_with_face_plane(
				  normal, vertex, points[3], points[2], &t),
			  1);
	XVT_ASSERT_CLOSE(t, 1.0, 1e-9, "end just under the plane");
}

/* Comment: a destroyed candidate is dropped from the list and the one after
 * it is tested in the same step; objects ending the step out of the list
 * stay out. */
static void check_collisions_destroyed_candidate_dropped(void)
{
	craft_meets_mine();
	player_flies(0, 0);
	place(1, 2100, 0, 0);
	g_test_mobiles[1].prev_world_x = 2100;
	g_test_craft[1].shield_energy[0] = 1000;
	g_test_craft[1].hull_max = 100000;
	struct mobile_object_proximity_list *list =
		&g_test_mobiles[0].proximity_list;
	list->count = 2;
	list->obj_idx[0] = TEST_MINE;
	list->contact_ticks[0] = 0;
	list->obj_idx[1] = 1;
	list->contact_ticks[1] = 0;
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_MINE].object_type, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[0].ai_flight.impact_obj_idx, 1);
	XVT_ASSERT_INT_EQ(list->count, 1);
	XVT_ASSERT_INT_EQ(list->obj_idx[0], 1);
}

/* Comment: a shot that hits another shot explodes with it; the pair works the
 * same whichever slot is the owner's: here the owner is the second shot
 * slot and the other the first, the first slot past the craft's. */
static void check_collisions_shot_meets_shot_in_first_slot(void)
{
	fresh_world();
	g_object_type_table[PROJECTILE_OBJECT_TYPE_REBEL_LASER]
		.max_bounds_extent = 400;
	g_object_type_table[WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE]
		.max_bounds_extent = 400;
	place_shot(TEST_OTHER_SHOT, PROJECTILE_OBJECT_TYPE_REBEL_LASER, 1, 0, 0,
		   0, 2000, 0, 0);
	place_shot(TEST_SHOT, WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 0, 1000,
		   0, 0, 1000, 0, 0);
	list_holds(TEST_OTHER_SHOT, TEST_SHOT);
	collide_collisions();
	XVT_ASSERT_INT_EQ(g_test_craft[1].weapon_stats.laser_hits_scored, 1);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_OTHER_SHOT].object_type, 131);
	XVT_ASSERT_INT_EQ(g_test_objects[TEST_SHOT].object_type, 129);
	XVT_ASSERT_TRUE(
		line_holds("combat.shots_collided", "shot=3 type=137 other=2"));
}

/* Comment: a target far off but sweeping through the path in the step is
 * reached by the sweeps, on every axis and in either direction; the same
 * target held still at that distance is not. */
static void check_swept_pair_sweeping_targets(void)
{
	static const int sweeps[][6] = {
		{-3000, 0, 0, 3000, 0, 0},
		{3000, 0, 0, -1000, 0, 0},
		{500, 60000, 0, 500, -60000, 0},
		{500, -60000, 0, 500, 60000, 0},
		{500, 0, 60000, 500, 0, -60000},
		{500, 0, -60000, 500, 0, 60000},
	};
	for (size_t i = 0; i < sizeof sweeps / sizeof sweeps[0]; ++i) {
		fresh_world();
		pair_hit_at(1, 0);
		g_collision_sweep_start_x = sweeps[i][0];
		g_collision_sweep_start_y = sweeps[i][1];
		g_collision_sweep_start_z = sweeps[i][2];
		g_collision_sweep_end_x = sweeps[i][3];
		g_collision_sweep_end_y = sweeps[i][4];
		g_collision_sweep_end_z = sweeps[i][5];
		XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(0, 1), -1);
		/* Held at its start, it is out of the way. */
		g_collision_sweep_end_x = g_collision_sweep_start_x;
		g_collision_sweep_end_y = g_collision_sweep_start_y;
		g_collision_sweep_end_z = g_collision_sweep_start_z;
		XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(0, 1), 0);
	}
}

/* Comment: a starfighter hit by a shot has its extent raised by half while
 * its shields are up, and held to 1,094 when that passes 1,095: an extent of
 * 800 gives a box of 3/8 of 1,094 (409), not of 1,200 (450). */
static void check_swept_pair_extent_cap(void)
{
	fresh_world();
	g_object_type_table[TEST_FIGHTER_TYPE].max_bounds_extent = 800;
	place_shot(TEST_SHOT, PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0, 0, 0, 0, 0,
		   0, 0);
	g_test_craft[1].shield_energy[0] = 1;
	for (int y = 405; y <= 415; y += 5) {
		pair_hit_at(1, y);
		XVT_ASSERT_INT_EQ(
			collide_test_swept_pair_collision(TEST_SHOT, 1) != 0,
			y <= 409);
	}
	/* At 1,095 without the shields' half, the polygon test is used: a
	 * source that has not moved gets 0. */
	g_test_craft[1].shield_energy[0] = 0;
	g_object_type_table[TEST_FIGHTER_TYPE].max_bounds_extent = 1095;
	pair_hit_at(1, 0);
	g_collision_probe_world_x = g_collision_segment_start_world_x;
	XVT_ASSERT_INT_EQ(collide_test_swept_pair_collision(TEST_SHOT, 1), 0);
}

/* Comment: the wash lies behind the engine mesh, about its center: a victim
 * level with an engine that sits to one side or up takes the wash that a
 * victim level with a centered engine does, and less when it is level with
 * the ship's middle instead. */
static void check_wash_engine_off_center(void)
{
	wash_world(TEST_SHIP_TYPE);
	int centered = wash_at(0, -300, 0);
	XVT_ASSERT_TRUE(centered > 0);
	static const struct {
		float side;
		float up;
	} engines[] = {{200, 0}, {-200, 0}, {0, 150}, {0, -150}, {120, -90}};
	for (size_t i = 0; i < sizeof engines / sizeof engines[0]; ++i) {
		wash_world(TEST_SHIP_TYPE);
		mesh_box(0, -50, -100, -50, 50, 100, 50, engines[i].side,
			 engines[i].up);
		/* Level with the engine: the same wash as a centered one. */
		XVT_ASSERT_INT_EQ(
			wash_at((int)engines[i].side, -300, (int)engines[i].up),
			centered);
		/* Level with the middle of the ship: less, or none. */
		XVT_ASSERT_TRUE(wash_at(0, -300, 0) < centered);
	}
}

/* Comment: at a hangar the weapons are jammed within a sixth of the extent
 * of the entry point, which need not lie on any axis of the craft. */
static void check_jam_off_axis_entry_point(void)
{
	/* An entry point 300 to the side, 400 up and 500 along; the craft
	 * 100 or less from it jams, though it is far from the center. */
	for (int near = 0; near < 2; ++near) {
		jam_world(TEST_FIGHTER_TYPE, 600, 300 + (near ? 60 : 160), 500,
			  400);
		g_model_defs[TEST_MODEL].hangar_points.inside.side = 300;
		g_model_defs[TEST_MODEL].hangar_points.inside.up = 400;
		g_model_defs[TEST_MODEL].hangar_points.inside.forward = 500;
		collide_apply_hostile_proximity_weapon_disruption(0, 1);
		XVT_ASSERT_INT_EQ(is_jammed(), near);
	}
	/* The same along each of the other two directions. */
	for (int axis = 0; axis < 3; ++axis) {
		jam_world(TEST_FIGHTER_TYPE, 600, 300 + (axis == 0 ? 90 : 0),
			  500 + (axis == 1 ? 90 : 0),
			  400 + (axis == 2 ? 90 : 0));
		g_model_defs[TEST_MODEL].hangar_points.inside.side = 300;
		g_model_defs[TEST_MODEL].hangar_points.inside.up = 400;
		g_model_defs[TEST_MODEL].hangar_points.inside.forward = 500;
		collide_apply_hostile_proximity_weapon_disruption(0, 1);
		XVT_ASSERT_TRUE(is_jammed());
	}
}

/* ------------------------------------------------------------------------ */
/* Sharper checks: results pinned to the figure the comment's rule gives. */

/* The n-th value (counting from 1) game_rand returns from the state seed,
 * which is what the first n draws of a craft with that seed see; the shared
 * state is put back. */
static uint16_t nth_draw(uint16_t seed, int n)
{
	int16_t saved = g_game_rand_feedback_state;
	uint16_t value = 0;
	g_game_rand_feedback_state = (int16_t)seed;
	for (int i = 0; i < n; ++i) {
		value = (uint16_t)game_rand();
	}
	g_game_rand_feedback_state = saved;
	return value;
}

/* The heading a bounce leaves a craft with, from the rule in the comment of
 * collide_apply_craft_impact_bounce: it turns toward its move vector plus a
 * push away from the other along the line between them, 1,000 times the
 * closing speed over the squared distance (a push of 100 along Y when the two
 * touch, within a squared distance of 50); a push that would turn a move
 * component round is dropped. dx, dy and dz are the craft's offset from the
 * other. */
static unsigned int bounce_heading(int speed, int dx, int dy, int dz,
				   int move_x, int move_y)
{
	int distance_squared = dx * dx + dy * dy + dz * dz;
	int force_x;
	int force_y;
	if (distance_squared > 50) {
		force_x = (int16_t)((int32_t)(1000u * (uint32_t)speed *
					      (uint32_t)dx) /
				    distance_squared);
		force_y = (int16_t)((int32_t)(1000u * (uint32_t)speed *
					      (uint32_t)dy) /
				    distance_squared);
	} else {
		force_x = 0;
		force_y = 100;
	}
	force_x = (int16_t)(force_x + move_x);
	if (((force_x ^ move_x) & 0x8000) != 0) {
		force_x = move_x;
	}
	force_y = (int16_t)(force_y + move_y);
	if (((force_y ^ move_y) & 0x8000) != 0) {
		force_y = move_y;
	}
	return (uint16_t)trig2_arctan(force_x, force_y);
}

/* Comment: the craft's yaw turns toward its move vector plus a push away from
 * the other along the line between them, and a shot turns it further by 8
 * times the shot's speed, either way. The heading is the one the log line
 * records. The cases run the push for a distance apart and for touching
 * (squared distance 25, 50 and 51 around the edge of 50). */
static void check_bounce_heading(void)
{
	static const struct {
		int other;
		int x;
		int y;
		int z;
		int move_x;
		int move_y;
	} cases[] = {
		{TEST_MINE, 100, 50, 0, 2000, 3000},
		{TEST_MINE, 5, 0, 0, 2000, 3000},
		{TEST_MINE, 5, 5, 0, 2000, 3000},
		{TEST_MINE, 1, 5, 5, 2000, 3000},
		{TEST_MINE, 100, 50, 0, -300, -200},
		{TEST_MINE, 100, 50, 0, 20, 3000},
		{TEST_MINE, 100, 50, 0, 2000, 20},
		{TEST_SHOT, 100, 50, 0, 2000, 3000},
		{TEST_SHOT, 100, 50, 0, 3000, 2000},
		{TEST_SHOT, 5, 5, 0, 2000, 3000},
		{0, 100, 50, 0, 2000, 3000},
		{0, 1, 5, 5, 2000, 3000},
	};
	int turned_down = 0;
	int turned_up = 0;
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		fresh_world();
		int other = cases[i].other;
		g_test_objects[1].yaw = 0x2000;
		place(1, cases[i].x, cases[i].y, cases[i].z);
		g_test_mobiles[1].prev_world_x = cases[i].x;
		g_test_mobiles[1].prev_world_y = cases[i].y;
		g_test_mobiles[1].prev_world_z = cases[i].z;
		g_test_mobiles[1].speed = 100;
		g_test_mobiles[1].move_vector_dirty = 0;
		g_test_mobiles[1].move_x = (int16_t)cases[i].move_x;
		g_test_mobiles[1].move_y = (int16_t)cases[i].move_y;
		int speed = 100;
		if (other == TEST_MINE) {
			place_mine(0, 0, 0);
		} else if (other == TEST_SHOT) {
			place_shot(TEST_SHOT,
				   WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE, 0, 0,
				   0, 0, 0, 0, 0);
			g_test_mobiles[TEST_SHOT].speed = 40;
		} else {
			place(0, 0, 0, 0);
			g_test_objects[0].yaw = 0x2000;
			g_test_mobiles[0].speed = BOUNCE_OTHER_SPEED;
			speed = abs_int(100 -
					(int)(int16_t)trig2_cosinewordmult(
						BOUNCE_OTHER_SPEED, 0));
		}
		collide_apply_craft_impact_bounce(1, (uint16_t)other);
		unsigned int heading = bounce_heading(
			speed, cases[i].x, cases[i].y, cases[i].z,
			cases[i].move_x, cases[i].move_y);
		if (other == TEST_SHOT) {
			int16_t step = (int16_t)(8 * 40);
			if (0x2000 > heading) {
				step = (int16_t)-step;
				++turned_down;
			} else {
				++turned_up;
			}
			heading = (uint16_t)(heading + step);
		}
		XVT_ASSERT_INT_EQ(g_test_objects[1].yaw, heading);
		XVT_ASSERT_TRUE(find_line("combat.craft_bounced") != NULL);
	}
	/* Both ways of turning for a shot were run. */
	XVT_ASSERT_TRUE(turned_down > 0 && turned_up > 0);
}

/* Comment: the bounce of a player's craft off another craft turns the player's
 * craft by the same rule, with the other's move and position as they are in
 * the step: the heading the step leaves it with, as the DEBUG line of the
 * bounce records, from the push of 1,000 times the closing speed (20 here)
 * over the squared distance, with a push that would turn a move component round
 * dropped. */
static void check_collisions_player_bounce_heading(void)
{
	static const int moves[][2] = {
		{2000, 3000}, {2000, 10}, {10, 3000}, {-300, 10}, {10, -300},
	};
	for (size_t i = 0; i < sizeof moves / sizeof moves[0]; ++i) {
		craft_meet();
		g_flight_mission_state.collisions_enabled = 0;
		place(1, 100, 60, 0);
		g_test_mobiles[1].prev_world_x = 100;
		g_test_mobiles[1].prev_world_y = 60;
		g_test_mobiles[0].speed = 50;
		g_test_mobiles[1].speed = BOUNCE_OTHER_SPEED;
		g_test_mobiles[0].move_vector_dirty = 0;
		g_test_mobiles[0].move_x = (int16_t)moves[i][0];
		g_test_mobiles[0].move_y = (int16_t)moves[i][1];
		collide_collisions();
		unsigned int heading = bounce_heading(
			abs_int(50 - (int)(int16_t)trig2_cosinewordmult(
					     BOUNCE_OTHER_SPEED, 0)),
			-100, -60, 0, moves[i][0], moves[i][1]);
		XVT_ASSERT_INT_EQ(g_test_objects[0].yaw, heading);
		XVT_ASSERT_TRUE(find_line("combat.craft_bounced") != NULL);
	}
}

/* Comment: the wash lies behind the engine mesh and widens with depth; at its
 * start it is 1.5 times the engine's width across, to either side and up or
 * down: a victim 1 unit into the wash is hit 148 units to the side and not 153
 * (the engine is 100 across). */
static void check_wash_width_at_its_start(void)
{
	wash_world(TEST_SHIP_TYPE);
	for (int sign = -1; sign <= 1; sign += 2) {
		XVT_ASSERT_TRUE(wash_at(sign * 148, 99, 0) > 0);
		XVT_ASSERT_INT_EQ(wash_at(sign * 153, 99, 0), 0);
		XVT_ASSERT_TRUE(wash_at(0, 99, sign * 148) > 0);
		XVT_ASSERT_INT_EQ(wash_at(0, 99, sign * 153), 0);
	}
}

/* Comment: a Super Star Destroyer's wash is placed differently and cut to
 * 3/32. Its damage at a depth is 3/32 of the wash a craft of any other kind
 * would take there, where the wash is 8 times the engine's largest size
 * long: about the engine's 48,000 here, falling off linearly with depth. */
static void check_wash_super_destroyer_cut(void)
{
	wash_world(SUPER_TYPE);
	mesh_box(0, -3000, -3000, -3000, 3000, 3000, 3000, 0, 0);
	g_object_type_table[SUPER_TYPE].max_bounds_extent = 30000;
	int length = 8 * 6000;
	/* The wash reaches its start 3,000 up the tail's length: 687 units
	 * deep at 3,500 behind the middle. */
	int depth = (length >> 8) - 3000 + 3500;
	double wash = (length / 256.0) * (1.0 - (double)depth / length);
	int loss = wash_at(0, -3500, 0);
	XVT_ASSERT_TRUE(loss >= wash * 3.0 / 32.0 - 2.0);
	XVT_ASSERT_TRUE(loss <= wash * 3.0 / 32.0 + 2.0);
}

/* Comment: the Super Star Destroyer's engine mesh counts for a 64th of its
 * length when the wash's length is set: the wash is 8 times the largest of the
 * mesh's three sizes, here 200 (its 12,800 long, over 64) against 100 across,
 * 1,600 long, measured from the mesh's far end. */
static void check_wash_super_destroyer_length(void)
{
	wash_world(SUPER_TYPE);
	mesh_box(0, -50, -6400, -50, 50, 6400, 50, 0, 0);
	g_object_type_table[SUPER_TYPE].max_bounds_extent = 30000;
	/* 106, 1,006 and 2,006 deep. */
	XVT_ASSERT_TRUE(wash_at(0, -6500, 0) > 0);
	XVT_ASSERT_TRUE(wash_at(0, -7400, 0) > 0);
	XVT_ASSERT_INT_EQ(wash_at(0, -8400, 0), 0);
}

/* Comment: at a Super Star Destroyer the craft must be within an eighth of
 * the extent of the hangar entry point, whichever way it lies from the middle
 * (forward or aft, to either side) and level with it. */
static void check_jam_super_destroyer_entry_point(void)
{
	static const struct {
		int side;
		int forward;
		int jammed;
	} cases[] = {
		{200, 700, 1},	{200, -700, 0}, {-900, 700, 0}, {900, 700, 1},
		{-200, 700, 1}, {200, 1600, 1}, {200, 1800, 0},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		jam_world(SUPER_TYPE, 8000, cases[i].side, cases[i].forward,
			  -400);
		g_model_defs[TEST_MODEL].hangar_points.inside.side = 200;
		g_model_defs[TEST_MODEL].hangar_points.inside.up = -400;
		g_model_defs[TEST_MODEL].hangar_points.inside.forward = 700;
		collide_apply_hostile_proximity_weapon_disruption(0, 1);
		XVT_ASSERT_INT_EQ(is_jammed(), cases[i].jammed);
	}
}

/* A square in the plane z = 0, 100 on a side, its corners in order. */
static const float g_square_corners[12] = {0,	0,   0, 100, 0,	  0,
					   100, 100, 0, 0,   100, 0};

/* Comment: the polygon test takes the face's corners by their indices, from
 * any of them: the same square with each corner first, either way round, holds
 * the same points. */
static void check_point_in_face_from_any_corner(void)
{
	static const int32_t faces[8][4] = {
		{0, 1, 2, 3}, {1, 2, 3, 0}, {2, 3, 0, 1}, {3, 0, 1, 2},
		{0, 3, 2, 1}, {3, 2, 1, 0}, {2, 1, 0, 3}, {1, 0, 3, 2},
	};
	const float normal[3] = {0.0f, 0.0f, 1.0f};
	static const float points[][3] = {
		{50, 50, 0},  {10, 10, 0},  {90, 90, 0},
		{10, 90, 0},  {90, 10, 0},  {150, 50, 0},
		{-20, 50, 0}, {50, -20, 0}, {50, 120, 0},
	};
	for (size_t face = 0; face < 8; ++face) {
		for (size_t p = 0; p < sizeof points / sizeof points[0]; ++p) {
			float point[3] = {points[p][0], points[p][1],
					  points[p][2]};
			int inside = points[p][0] >= 0 && points[p][0] <= 100 &&
				     points[p][1] >= 0 && points[p][1] <= 100;
			XVT_ASSERT_INT_EQ(collide_point_in_face_polygon(
						  normal, g_square_corners,
						  faces[face], point),
					  inside);
		}
	}
}

/* Craft 1 at (20,000, 3,000, -2,000) in the face world: off every axis, so a
 * sum taken for a difference shows. */
static void face_world_off_axis(void)
{
	face_world();
	place(1, 20000, 3000, -2000);
}

static int walk_off_axis(int sx, int sy, int sz, int ex, int ey, int ez)
{
	g_collision_segment_start_world_x = 20000 + sx;
	g_collision_segment_start_world_y = 3000 + sy;
	g_collision_segment_start_world_z = -2000 + sz;
	g_collision_probe_world_x = 20000 + ex;
	g_collision_probe_world_y = 3000 + ey;
	g_collision_probe_world_z = -2000 + ez;
	return collide_check_swept_model_collision(0, 1);
}

/* Comment: the sweep is moved into the target's own axes from the target's
 * position: a model not at the origin on Y or Z is hit where it is. */
static void check_model_walk_target_off_axis(void)
{
	face_world_off_axis();
	XVT_ASSERT_INT_EQ(walk_off_axis(0, 0, 200, 0, 0, -200), 1);
	XVT_ASSERT_TRUE(g_collision_hit_offset_z >= -162 &&
			g_collision_hit_offset_z <= -158);
	XVT_ASSERT_INT_EQ(walk_off_axis(50, 30, -200, 50, 30, 200), 1);
	/* Where the model would be if the target's Y or Z were added, no
	 * hit. */
	XVT_ASSERT_INT_EQ(walk_off_axis(0, 6000, 200, 0, 6000, -200), 0);
	XVT_ASSERT_INT_EQ(walk_off_axis(0, 0, 4200, 0, 0, 3800), 0);
	XVT_ASSERT_INT_EQ(walk_off_axis(0, 0, 200, 0, 0, 100), 0);
}

/* Comment: it skips laser turret and gun meshes of the target's own ship; a
 * mesh past the ones the cache holds is read as the last one it holds: a ship
 * whose cache holds a turret and a second mesh, of which the cache knows no
 * more, skips that second mesh too. */
static void check_model_walk_mesh_past_the_cache(void)
{
	face_world();
	face_mesh(0, 0, OPT_NODEREF + 100);
	face_mesh(1, 0, OPT_NODEREF + 100);
	face_model(TEST_FIGHTER_TYPE, 2);
	g_object_type_mesh_cache[TEST_FIGHTER_TYPE].mesh_count = 1;
	g_object_type_mesh_cache[TEST_FIGHTER_TYPE].mesh_types[0] =
		MESH_COMPONENT_04_LASR_TUR;
	g_object_type_mesh_cache[TEST_FIGHTER_TYPE].mesh_types[1] =
		MESH_COMPONENT_03_FUSELAGE;
	g_test_craft[1].component_hp[0] = 20;
	g_test_craft[1].component_hp[1] = 20;
	g_collision_segment_start_world_x = 20000;
	g_collision_segment_start_world_y = 0;
	g_collision_segment_start_world_z = 200;
	g_collision_probe_world_x = 20000;
	g_collision_probe_world_y = 0;
	g_collision_probe_world_z = -200;
	XVT_ASSERT_INT_EQ(collide_check_swept_model_collision(1, 1), 0);
	XVT_ASSERT_INT_EQ(collide_check_swept_model_collision(0, 1), 1);
}

/* Comment: it walks each mesh whose box the sweep does not lie wholly beside:
 * a model whose box is smaller than its face decides the hit, at the box's
 * edges as well. */
static void check_model_walk_box_decides(void)
{
	/* The walk takes the sweep into the model's axes with a Q15 dot product
	 * that rounds down: world 51 is 50 on the side axis and -50 is -50, and
	 * the forward axis is reversed (world -50 is 50, 51 is -50). The box is
	 * 50 either way. A sweep crossing the face half way between its ends
	 * meets it inside the 100 across. */
	static const struct {
		int x0;
		int y0;
		int x1;
		int y1;
		int hit;
	} cases[] = {
		/* Both ends on one line. */
		{0, 0, 0, 0, 1},
		{51, 0, 51, 0, 1},
		{-50, 0, -50, 0, 1},
		{0, -50, 0, -50, 1},
		{0, 51, 0, 51, 1},
		{52, 0, 52, 0, 0},
		{-51, 0, -51, 0, 0},
		{0, -51, 0, -51, 0},
		{0, 52, 0, 52, 0},
		{75, 0, 75, 0, 0},
		{0, 75, 0, 75, 0},
		{-75, -75, -75, -75, 0},
		{75, 75, 75, 75, 0},
		{51, -50, 51, -50, 1},
		{-50, 51, -50, 51, 1},
		/* One end on the box's edge, the other past it: either end
		 * holds the sweep in the box. */
		{51, 0, 101, 0, 1},
		{101, 0, 51, 0, 1},
		{52, 0, 101, 0, 0},
		{101, 0, 52, 0, 0},
		{-50, 0, -100, 0, 1},
		{-100, 0, -50, 0, 1},
		{-51, 0, -100, 0, 0},
		{-100, 0, -51, 0, 0},
		{0, -50, 0, -100, 1},
		{0, -100, 0, -50, 1},
		{0, -51, 0, -100, 0},
		{0, -100, 0, -51, 0},
		{0, 51, 0, 101, 1},
		{0, 101, 0, 51, 1},
		{0, 52, 0, 101, 0},
		{0, 101, 0, 52, 0},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		face_world();
		g_craft_model_descriptors[0].box_min =
			(struct opt_vector){-50, -50, -5};
		g_craft_model_descriptors[0].box_max =
			(struct opt_vector){50, 50, 5};
		XVT_ASSERT_INT_EQ(walk(cases[i].x0, cases[i].y0, 200,
				       cases[i].x1, cases[i].y1, -200),
				  cases[i].hit);
	}
	/* The box's lower edge in height is the face's own: a sweep starting
	 * or ending on the face from below is in it, one wholly below is not.
	 * And the other way about for a box ending at the face. */
	face_world();
	g_craft_model_descriptors[0].box_min = (struct opt_vector){-50, -50, 0};
	g_craft_model_descriptors[0].box_max = (struct opt_vector){50, 50, 5};
	XVT_ASSERT_INT_EQ(walk(0, 0, 0, 0, 0, -200), 1);
	XVT_ASSERT_INT_EQ(walk(0, 0, -200, 0, 0, 0), 1);
	XVT_ASSERT_INT_EQ(walk(0, 0, -200, 0, 0, -100), 0);
	face_world();
	g_craft_model_descriptors[0].box_min =
		(struct opt_vector){-50, -50, -5};
	g_craft_model_descriptors[0].box_max = (struct opt_vector){50, 50, 0};
	XVT_ASSERT_INT_EQ(walk(0, 0, 0, 0, 0, 200), 1);
	XVT_ASSERT_INT_EQ(walk(0, 0, 200, 0, 0, 0), 1);
	XVT_ASSERT_INT_EQ(walk(0, 0, 200, 0, 0, 100), 0);
}

/* Comment: a craft with a fuselage and one wing tears that wing off, when the
 * seed's side is the wing's: the second draw of the craft's seed, odd for the
 * left wing (negative X); with the other side drawn it breaks up whole. */
static void check_kill_the_only_wing(void)
{
	static const int types[2] = {MESH_COMPONENT_03_FUSELAGE,
				     MESH_COMPONENT_02_WING};
	static const int center_x[2] = {0, -100};
	static const int flags[2] = {0, 0};
	int torn = 0;
	int whole = 0;
	for (int seed = 0; seed < 16; ++seed) {
		killing_shot();
		craft_model(2, types, center_x, flags);
		g_test_craft[1].system_damage_hull_threshold = 100000;
		g_test_craft[1].ai_controller.saved_rand_seed = (uint16_t)seed;
		collide_damagecraft(1, -1, TEST_SHOT, 0);
		int side = nth_draw((uint16_t)seed, 2) & 1;
		XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
				  CRAFT_OBJECT_KIND_BREAKING_UP);
		XVT_ASSERT_INT_EQ(g_test_craft[1].component_state[1] ==
					  TORN_OFF_STATE,
				  side != 0);
		XVT_ASSERT_INT_EQ(g_test_objects[TEST_DEBRIS].object_type ==
					  CRAFT_SPECIES_COMPONENT,
				  side != 0);
		if (side != 0) {
			++torn;
		} else {
			++whole;
		}
	}
	XVT_ASSERT_TRUE(torn > 0 && whole > 0);
}

static struct opt_vector g_two_face_vertices[10];
static union {
	struct opt_packed_face_data data;
	unsigned char bytes[sizeof(int) +
			    2 * sizeof(struct opt_packed_face_record) +
			    6 * sizeof(struct opt_vector)];
} g_two_face_data;

/* Comment: a face is hit by the sweep when it crosses its plane inside it;
 * each face has its own normal: a floor square (normal up) and a wall (normal
 * along X) in one mesh are each hit by the sweep that crosses them. */
static void check_model_walk_faces_of_one_mesh(void)
{
	face_world();
	g_two_face_vertices[0] = (struct opt_vector){-100, -100, 0};
	g_two_face_vertices[1] = (struct opt_vector){100, -100, 0};
	g_two_face_vertices[2] = (struct opt_vector){100, 100, 0};
	g_two_face_vertices[3] = (struct opt_vector){-100, 100, 0};
	g_two_face_vertices[4] = (struct opt_vector){150, -100, -100};
	g_two_face_vertices[5] = (struct opt_vector){150, 100, -100};
	g_two_face_vertices[6] = (struct opt_vector){150, 100, 100};
	g_two_face_vertices[7] = (struct opt_vector){150, -100, 100};
	g_two_face_vertices[8] = (struct opt_vector){-120, -120, -120};
	g_two_face_vertices[9] = (struct opt_vector){160, 120, 120};
	memset(&g_two_face_data, 0, sizeof g_two_face_data);
	struct opt_packed_face_record *records = g_two_face_data.data.records;
	for (int corner = 0; corner < 4; ++corner) {
		records[0].vertex_indices[corner] = corner;
		records[1].vertex_indices[corner] = 4 + corner;
	}
	struct opt_vector *normals = (struct opt_vector *)&records[2];
	normals[0] = (struct opt_vector){0, 0, 1};
	normals[1] = (struct opt_vector){1, 0, 0};
	g_face_vertex_nodes[0].payload = g_two_face_vertices;
	g_face_vertex_nodes[0].payload_count = 10;
	g_face_face_nodes[0].payload = &g_two_face_data;
	g_face_face_nodes[0].payload_count = 2;
	g_craft_model_descriptors[0].box_min =
		(struct opt_vector){-120, -120, -120};
	g_craft_model_descriptors[0].box_max =
		(struct opt_vector){160, 120, 120};
	/* Down through the floor. */
	XVT_ASSERT_INT_EQ(walk(0, 0, 200, 0, 0, -200), 1);
	/* In along X at height 50, through the wall at 150: 0.75 of the way,
	 * less 0.1, of the 200 travelled. */
	XVT_ASSERT_INT_EQ(walk(300, 0, 50, 100, 0, 50), 1);
	XVT_ASSERT_TRUE(g_collision_hit_offset_x >= -133 &&
			g_collision_hit_offset_x <= -127);
	/* Past the wall's top, or short of it. */
	XVT_ASSERT_INT_EQ(walk(300, 0, 130, 100, 0, 130), 0);
	XVT_ASSERT_INT_EQ(walk(300, 0, 50, 200, 0, 50), 0);
}

/* The reach of a shot of type fired by a craft moving at source_speed, from
 * the game's figures: its life (the seconds and the fraction of one) times
 * the speed of the shot and the craft together, as the aim code works it
 * out. */
static int shot_reach(int type, int source_speed, int with_fraction)
{
	int index = type - PROJECTILE_OBJECT_TYPE_FIRST;
	int ticks = SIMULATION_TICKS_PER_SECOND *
		    g_projectile_type_data.lifetime_seconds[index];
	if (with_fraction) {
		ticks += (uint16_t)math2_fraction(
			SIMULATION_TICKS_PER_SECOND,
			g_projectile_type_data.lifetime_frac_q16[index]);
	}
	return ticks *
	       ((4660 * (g_projectile_type_data.speed[index] + source_speed) +
		 128) >>
		8) /
	       SIMULATION_TICKS_PER_SECOND;
}

/* Comment: the shot is swept for its whole life, at the shot's speed plus the
 * source's: the Rebel turbo laser lives 1 second and a fraction, and the
 * craft's own speed carries it further. */
static void check_would_shot_hit_life_and_speed(void)
{
	aim_world();
	g_model_defs[TEST_MODEL].laser_group_weapon_type[0] =
		PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER;
	int whole = shot_reach(PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER, 0, 1);
	int bare = shot_reach(PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER, 0, 0);
	XVT_ASSERT_TRUE(whole > bare + 5000);
	place(1, 0, (bare + whole) / 2, 0);
	XVT_ASSERT_TRUE(collide_would_shot_hit_target(0, 1, 0) != 0);
	place(1, 0, whole + 600, 0);
	XVT_ASSERT_INT_EQ(collide_would_shot_hit_target(0, 1, 0), 0);
	place(1, 0, whole - 500, 0);
	XVT_ASSERT_TRUE(collide_would_shot_hit_target(0, 1, 0) != 0);

	g_test_mobiles[0].speed = 1000;
	int fast =
		shot_reach(PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER, 1000, 1);
	XVT_ASSERT_TRUE(fast > whole + 5000);
	place(1, 0, (whole + fast) / 2, 0);
	XVT_ASSERT_TRUE(collide_would_shot_hit_target(0, 1, 0) != 0);
	place(1, 0, fast + 600, 0);
	XVT_ASSERT_INT_EQ(collide_would_shot_hit_target(0, 1, 0), 0);
	place(1, 0, fast - 500, 0);
	XVT_ASSERT_TRUE(collide_would_shot_hit_target(0, 1, 0) != 0);

	/* The edge: the shot's last point reaches the target's box (150 half
	 * size, a few units less for the step's rounding of the entry time)
	 * at the reach plus 147 for the shot alone, plus 146 with the craft's
	 * speed, and not 2 units further. */
	g_test_mobiles[0].speed = 0;
	place(1, 0, whole + 147, 0);
	XVT_ASSERT_TRUE(collide_would_shot_hit_target(0, 1, 0) != 0);
	place(1, 0, whole + 149, 0);
	XVT_ASSERT_INT_EQ(collide_would_shot_hit_target(0, 1, 0), 0);
	g_test_mobiles[0].speed = 1000;
	place(1, 0, fast + 146, 0);
	XVT_ASSERT_TRUE(collide_would_shot_hit_target(0, 1, 0) != 0);
	place(1, 0, fast + 149, 0);
	XVT_ASSERT_INT_EQ(collide_would_shot_hit_target(0, 1, 0), 0);
}

/* The pair test on the face world's craft 1, made 1,100 across, for the sweep
 * of craft 0 from (sx, 0, sz) to (ex, 0, ez) relative to it, with the aim flag
 * as given and craft 1 moving at target_speed. */
static int pair_on_face_model(int sx, int sz, int ex, int ez, int aim,
			      int target_speed)
{
	g_collision_segment_start_world_x = 20000 + sx;
	g_collision_segment_start_world_y = 0;
	g_collision_segment_start_world_z = sz;
	g_collision_probe_world_x = 20000 + ex;
	g_collision_probe_world_y = 0;
	g_collision_probe_world_z = ez;
	g_collision_sweep_start_x = 20000;
	g_collision_sweep_start_y = 0;
	g_collision_sweep_start_z = 0;
	g_collision_sweep_end_x = 20000;
	g_collision_sweep_end_y = 0;
	g_collision_sweep_end_z = 0;
	g_collision_is_aim_prediction = aim;
	g_test_mobiles[1].speed = target_speed;
	return collide_test_swept_pair_collision(0, 1);
}

/* Comment: a target of extent 1,095 or more gets the polygon test; under the
 * aim flag, which the call clears, only a target slower than 40 does, else
 * the box test; otherwise a source that has not moved gets 0. */
static void check_swept_pair_polygon_test_paths(void)
{
	face_world();
	g_object_type_table[TEST_FIGHTER_TYPE].max_bounds_extent = 1100;
	/* Straight down through the face: the polygon test says mesh 1, with or
	 * without the aim flag for a slow target. */
	XVT_ASSERT_INT_EQ(pair_on_face_model(0, 200, 0, -200, 0, 0), 1);
	XVT_ASSERT_INT_EQ(pair_on_face_model(0, 200, 0, -200, 1, 0), 1);
	XVT_ASSERT_INT_EQ(g_collision_is_aim_prediction, 0);
	/* 300 off to the side: past the face, within the box test's reach.
	 * The polygon test says no; the box test says yes. */
	XVT_ASSERT_INT_EQ(pair_on_face_model(300, 200, 300, -200, 0, 0), 0);
	XVT_ASSERT_INT_EQ(pair_on_face_model(300, 200, 300, -200, 1, 0), 0);
	XVT_ASSERT_INT_EQ(pair_on_face_model(300, 200, 300, -200, 1, 39), 0);
	XVT_ASSERT_INT_EQ(pair_on_face_model(300, 200, 300, -200, 1, 40), -1);
	XVT_ASSERT_INT_EQ(g_collision_is_aim_prediction, 0);
	XVT_ASSERT_INT_EQ(pair_on_face_model(300, 200, 300, -200, 1, 100), -1);
	/* The box test's own reach is not met from further out. */
	XVT_ASSERT_INT_EQ(pair_on_face_model(500, 200, 500, -200, 1, 100), 0);
	/* A source that has not moved, on the face: no hit from the polygon
	 * test, which would find it on the plane. */
	XVT_ASSERT_INT_EQ(pair_on_face_model(0, 0, 0, 0, 0, 0), 0);
	XVT_ASSERT_INT_EQ(pair_on_face_model(0, 0, 0, 0, 1, 0), 1);
}

/* Comment: the kill is credited to the craft that fired, or that rammed: a
 * craft already at its hull maximum, hit for nothing, is credited to the
 * player behind the shot's firer, and behind the ramming craft itself. */
static void check_kill_credit_goes_to_the_firer(void)
{
	for (int ram = 0; ram < 2; ++ram) {
		player_kills_craft();
		g_test_craft[1].hull_damage = g_test_craft[1].hull_max;
		if (ram) {
			g_test_mobiles[0].damage_amount = 0;
			g_test_mobiles[0].source_obj_idx = 1;
			collide_damagecraft(1, -1, 0, 0);
		} else {
			g_test_mobiles[TEST_SHOT].damage_amount = 0;
			collide_damagecraft(1, -1, TEST_SHOT, 0);
		}
		XVT_ASSERT_INT_EQ(
			g_mission_fg_stats[1]
				.outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED],
			1);
		XVT_ASSERT_TRUE(g_players[0].mission_stats.mission_score > 0);
		XVT_ASSERT_INT_EQ(g_players[0].mission_stats.mission_score,
				  mission_compute_kill_score_for_object(1));
	}
}

/* Comment: the kill of a player's craft credits the killer once: the player's
 * loss is recorded without sharing out the kill again. */
static void check_kill_of_players_craft_credits_once(void)
{
	player_kills_craft();
	player_flies(1, 1);
	collide_damagecraft(1, -1, TEST_SHOT, 0);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[1]
			.outcome_count[FLIGHT_GROUP_OUTCOME_DESTROYED],
		1);
	XVT_ASSERT_INT_EQ(g_players[0].mission_stats.mission_score,
			  mission_compute_kill_score_for_object(1));
	XVT_ASSERT_INT_EQ(g_players[1].per_mission_kills.total_craft_losses, 1);
}

/* Comment: a moving craft hit by something small breaks up, tearing off a
 * wing when one is left, on a randomly chosen side: the craft's own seed gives
 * the side with its second draw (the first is the player's one in four), the
 * left wing (negative X) for an odd one and the right for an even one. */
static void check_kill_wing_side_follows_the_seed(void)
{
	int left = 0;
	int right = 0;
	for (int seed = 0; seed < 32; ++seed) {
		killing_shot();
		winged_craft_model();
		g_test_craft[1].system_damage_hull_threshold = 100000;
		g_test_craft[1].ai_controller.saved_rand_seed = (uint16_t)seed;
		collide_damagecraft(1, -1, TEST_SHOT, 0);
		int side = nth_draw((uint16_t)seed, 2) & 1;
		XVT_ASSERT_INT_EQ(g_test_craft[1].component_state[WING_LEFT] ==
					  TORN_OFF_STATE,
				  side != 0);
		XVT_ASSERT_INT_EQ(g_test_craft[1].component_state[WING_RIGHT] ==
					  TORN_OFF_STATE,
				  side == 0);
		if (side != 0) {
			++left;
		} else {
			++right;
		}
	}
	XVT_ASSERT_TRUE(left > 0 && right > 0);
}

/* Comment: a craft with one mesh has no wing to lose and its breakup is
 * drawn from its seed alone: the life, 236 ticks a second, is the second draw
 * after the roll's, as the DEBUG line records. */
static void check_kill_one_mesh_breakup_life(void)
{
	static const uint16_t seeds[] = {0x1234, 0x0F0F, 0x7ACE, 0x2B67,
					 0x31C5, 0x0555, 0x6E3D, 0x1111};
	for (size_t i = 0; i < sizeof seeds / sizeof seeds[0]; ++i) {
		killing_shot();
		fuselage_craft_model();
		g_test_craft[1].system_damage_hull_threshold = 100000;
		g_test_craft[1].ai_controller.saved_rand_seed = seeds[i];
		collide_damagecraft(1, -1, TEST_SHOT, 0);
		int expected = SIMULATION_TICKS_PER_SECOND *
			       ((nth_draw(seeds[i], 3) & 3) +
				(nth_draw(seeds[i], 4) & 7) + 1);
		XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
				  CRAFT_OBJECT_KIND_BREAKING_UP);
		XVT_ASSERT_INT_EQ(g_test_mobiles[1].lifetime_timer, expected);
		XVT_ASSERT_TRUE(line_holds("combat.craft_breakup", "state=3"));
	}
}

/* Comment: only wings are torn off: a ship with a mesh on each side that is
 * a turret loses none; rotating wings are wings. */
static void check_kill_which_meshes_tear_off(void)
{
	static const int center_x[3] = {0, -100, 100};
	static const int flags[3] = {0, 0, 0};
	int torn_left = 0;
	int torn_right = 0;
	for (int rotating = 0; rotating < 2; ++rotating) {
		int types[3] = {MESH_COMPONENT_03_FUSELAGE,
				rotating ? MESH_COMPONENT_20_ROTATING_WING
					 : MESH_COMPONENT_04_LASR_TUR,
				rotating ? MESH_COMPONENT_20_ROTATING_WING
					 : MESH_COMPONENT_04_LASR_TUR};
		for (int seed = 0; seed < 16; ++seed) {
			killing_shot();
			craft_model(3, types, center_x, flags);
			g_test_craft[1].system_damage_hull_threshold = 100000;
			g_test_craft[1].ai_controller.saved_rand_seed =
				(uint16_t)seed;
			collide_damagecraft(1, -1, TEST_SHOT, 0);
			XVT_ASSERT_INT_EQ(g_test_craft[1].object_kind,
					  CRAFT_OBJECT_KIND_BREAKING_UP);
			int left = g_test_craft[1].component_state[WING_LEFT] ==
				   TORN_OFF_STATE;
			int right =
				g_test_craft[1].component_state[WING_RIGHT] ==
				TORN_OFF_STATE;
			if (rotating) {
				XVT_ASSERT_TRUE(left != right);
				XVT_ASSERT_INT_EQ(
					g_test_objects[TEST_DEBRIS].object_type,
					CRAFT_SPECIES_COMPONENT);
				torn_left += left;
				torn_right += right;
			} else {
				XVT_ASSERT_TRUE(!left && !right);
				XVT_ASSERT_INT_EQ(
					g_test_objects[TEST_DEBRIS].object_type,
					0);
			}
		}
	}
	XVT_ASSERT_TRUE(torn_left > 0 && torn_right > 0);
}

int main(int argc, char **argv)
{
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	SDL_SetLogOutputFunction(catch_line, NULL);
	/* "known-failure <check>" runs one check the code is known to fail; an
	 * unknown name runs nothing. */
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		static const struct {
			const char *name;
			void (*check)(void);
		} known_failures[] = {
			{"self_reference_hangs", check_self_reference_returns},
			{"reference_pair_hangs", check_reference_pair_returns},
			{"static_hit_uncounted", check_static_hit_counted},
			{"impact_keeps_homing", check_impact_stops_homing},
			{"explosion_keeps_homing",
			 check_explosion_stops_homing},
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
	g_craft_model_handle = memory_alloc_handle_zeroed(
		sizeof(struct optimized_poly_object), 0);
	XVT_ASSERT_TRUE(g_craft_model_handle != 0);
	fail_after_seconds(20);
	check_rough_distance();
	check_proximity_speed();
	check_insert_orders_by_contact();
	check_insert_motionless_apart();
	check_insert_full_list();
	check_remove_candidate();
	check_reset_slot();
	check_reset_neighbors();
	check_convert_to_explosion();
	check_craft_damage_amount();
	check_damage_shield_takes();
	check_damage_scaled_by_genus();
	check_damage_refused();
	check_face_plane_crossing();
	check_point_in_face();
	check_model_walk();
	check_shot_hits_static();
	check_shots_collide();
	check_pulse_hit();
	check_pulse_on_large_craft();
	check_pulse_inhibit_by_genus();
	check_hit_from_itself();
	check_tally_sith_levels();
	check_tally_player_sources();
	check_tally_other_sources();
	check_tally_player_owned_victim();
	check_damage_fixed_amounts();
	check_damage_engine_wash();
	check_damage_transport_scale();
	check_damage_hull_through_shield();
	check_damage_player_shields_even();
	check_damage_hull_hit_on_player();
	check_damage_hud_feature_loss();
	check_damage_hud_loss_player_craft();
	check_damage_ion_counts_up();
	check_damage_ion_knocks_out_systems();
	check_damage_ion_disables_craft();
	check_kill_records();
	check_kill_weapon_names();
	check_kill_needs_live_craft();
	check_kill_tumbles_without_fuselage();
	check_kill_tears_off_wing();
	check_kill_without_a_wing_to_tear();
	check_kill_explodes_at_once();
	check_kill_of_player_craft();
	check_hit_attacker_named();
	check_hit_attacked_goal_once();
	check_hit_records_attacker();
	check_hit_friendly_player_counts();
	check_hit_side_by_travel();
	check_hit_chaff();
	check_hit_impact_effect_types();
	check_pulse_on_player_craft();
	check_damage_component_first();
	check_damage_component_in_hull();
	check_damage_hull_thresholds();
	check_damage_disabled_with_officer();
	check_damage_local_player_systems();
	check_kill_is_reported_to_player();
	check_kill_of_players_own_craft();
	check_bounce_only_small_craft();
	check_bounce_roll_spin();
	check_hit_bounce_conditions();
	check_wash_falls_off();
	check_wash_limits_and_player_record();
	check_wash_large_ship_cases();
	check_jam_near_hangar();
	check_jam_at_super_destroyer();
	check_jam_at_repair_yard();
	check_collisions_player_meets_craft();
	check_collisions_player_pair_left_alone();
	check_collisions_player_meets_static();
	check_collisions_ai_craft_meets_static();
	check_collisions_capital_ship_meets_craft();
	check_collisions_shot_meets_craft();
	check_collisions_shot_meets_shot();
	check_collisions_young_and_not_due();
	check_collisions_skips_hidden_objects();
	check_collisions_engine_wash_scan();
	check_collisions_inspects_target();
	check_collisions_inspection_range();
	check_collisions_inspection_goal_and_messages();
	check_collisions_hangar_offer();
	check_populate_for_player_craft();
	check_populate_for_ai_fighter();
	check_populate_for_capital_ship();
	check_populate_for_shot();
	check_collisions_rebuilds_list();
	check_reset_static_slot_reaches_players();
	check_reset_neighbors_skips();
	check_collisions_player_bounce_spin();
	check_collisions_other_genera_ignored();
	check_damage_protected_sources();
	check_damage_craft_source_uses_warheads();
	check_damage_component_equal_shields();
	check_damage_one_point_through();
	check_damage_ion_spares_failed_systems();
	check_damage_hull_hit_seed_edges();
	check_kill_exact_boundary();
	check_kill_clears_only_its_links();
	check_kill_saves_player_settings();
	check_kill_wing_recoil();
	check_box_collision_cases();
	check_swept_pair_rejections();
	check_swept_pair_negative_directions();
	check_swept_pair_target_extent();
	check_swept_pair_internet_play();
	check_swept_pair_large_target();
	check_would_shot_hit_target();
	check_would_shot_hit_static();
	check_would_shot_hit_component();
	check_craft_starship_collision();
	check_model_walk_hits_face();
	check_model_walk_skips_and_nearest();
	check_model_walk_near_start();
	check_model_walk_rotating_mesh();
	check_model_walk_unavailable_and_static();
	check_face_plane_oblique();
	check_wash_engine_off_center();
	check_jam_off_axis_entry_point();
	check_swept_pair_sweeping_targets();
	check_swept_pair_extent_cap();
	check_collisions_destroyed_candidate_dropped();
	check_collisions_shot_meets_shot_in_first_slot();
	check_bounce_heading();
	check_collisions_player_bounce_heading();
	check_wash_width_at_its_start();
	check_wash_super_destroyer_cut();
	check_wash_super_destroyer_length();
	check_jam_super_destroyer_entry_point();
	check_point_in_face_from_any_corner();
	check_model_walk_target_off_axis();
	check_model_walk_mesh_past_the_cache();
	check_model_walk_box_decides();
	check_model_walk_faces_of_one_mesh();
	check_would_shot_hit_life_and_speed();
	check_swept_pair_polygon_test_paths();
	check_kill_credit_goes_to_the_firer();
	check_kill_of_players_craft_credits_once();
	check_kill_wing_side_follows_the_seed();
	check_kill_one_mesh_breakup_life();
	check_kill_which_meshes_tear_off();
	check_kill_the_only_wing();
	return 0;
}