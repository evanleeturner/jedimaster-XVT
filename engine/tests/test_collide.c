/* Tests for xvt/flight/object/collide.c, the collision tests and what a hit
 * does. Each check builds the flight it needs in the game's own tables: a small
 * object table this file owns, with two craft slots, three shot slots and two
 * static slots, the craft and guidance records behind them, and the flight
 * group, object type and model figures the code reads. The model walk checks
 * build small model trees in memory. No game data is read.
 *
 * POSIX only, for the alarm that stops a check whose call does not return. */
#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "test_assert.h"
#include "xvt/assets/object_genus.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/collide.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/config.h"
#include "xvt/util/game_rand.h"

enum {
	TEST_SLOTS = 8,
	TEST_CRAFT_END = 2,  /* Slots 0 and 1 hold craft. */
	TEST_SHOT_START = 2, /* Slots 2 to 4 hold shots and effects. */
	TEST_MAIN_END = 5,   /* Slots 5 and 6 hold static objects. */
	TEST_STATIC_COUNT = 2,
	TEST_SHOT = 2,
	TEST_OTHER_SHOT = 3,
	TEST_MINE = 5,
	TEST_FIGHTER_TYPE = 1,
	TEST_MINE_TYPE = 75,
	TEST_FIGHTER_EXTENT = 400,
	TEST_MINE_EXTENT = 800,
	TEST_MODEL = 1,
	TEST_MODEL_MAX_SPEED = 100,
	TEST_EXPLOSION_TYPE = 129,
};

static struct object_record g_test_objects[TEST_SLOTS];
static struct mobile_object g_test_mobiles[TEST_MAIN_END];
static struct craft_data g_test_craft[TEST_CRAFT_END];
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

static void place(int slot, int x, int y, int z)
{
	g_test_objects[slot].world_x = x;
	g_test_objects[slot].world_y = y;
	g_test_objects[slot].world_z = z;
}

/* Two computer-flown starfighters in flight groups 0 and 1 (teams 0 and 1),
 * at the origin and 20,000 units along X, neither moving; empty shot and
 * static slots; every proximity list empty and not due for a rebuild. Sounds
 * are off, so nothing is queued. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
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
	g_flight_conf_sfx_enabled = 0;
	g_flight_sfx_side_effect_gate = 0;
	g_flight_sim_side_effects_suppressed = 0;
	g_flight_mission_state.proving_grounds_mode_active = 0;
	g_flight_mission_state.craft_impact_bounce_enabled = 0;
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

int main(int argc, char **argv)
{
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
	check_hit_from_itself();
	return 0;
}
