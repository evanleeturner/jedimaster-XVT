/* Tests for xvt/flight/object/object.c, the object table: finding free slots,
 * copying an object into another slot, breaking pieces and fragments off an
 * object, the world's edge, the decoy beam test, and the step that counts
 * down every object's life and moves it. Each check builds the world it needs
 * in the game's own layout: an object table this file owns with 32 craft
 * slots, 160 shot slots, 16 debris and 16 explosion slots, the other mobile
 * slots and 64 static slots, a craft record for each craft slot and a guidance
 * record for each shot slot. No game data is read.
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
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/trig2.h"

enum {
	SLOT_COUNT = 552,
	MOBILE_SLOTS = 488,
	CRAFT_SLOTS = 32,
	SHOT_START = 32,
	SHOT_END = 192,
	OTHER_SHOT_START = 160,
	DEBRIS_START = 192,
	DEBRIS_END = 208,
	EXPLOSION_START = 208,
	EXPLOSION_END = 224,
	STATIC_START = 488,
	STATIC_SLOTS = 64,
	TEST_XWING = 1,
	NO_PLAYER = 7,
	WORLD_EDGE = 0x1000000,
};

static struct object_record g_test_objects[SLOT_COUNT];
static struct mobile_object g_test_mobiles[MOBILE_SLOTS];
static struct craft_data g_test_craft[CRAFT_SLOTS];
static struct warhead_guidance_state g_test_guidance[SHOT_END - SHOT_START + 1];
static struct mobile_object_char_data g_test_char_data[4];

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

/* The program fails after the given number of seconds, so a call that never
 * returns fails at once instead of at the suite's time limit. */
static void fail_after_seconds(unsigned int seconds)
{
	signal(SIGALRM, stop_on_alarm);
	alarm(seconds);
}

/* An empty world in the game's slot layout, every slot range set as a mission
 * sets it, no player flying anything (the local player is slot 7), one tick
 * per step at the shared clock, and no single object picked for the step. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_test_guidance, 0, sizeof g_test_guidance);
	memset(g_test_char_data, 0, sizeof g_test_char_data);
	memset(g_players, 0, sizeof g_players);
	memset(g_object_slot_range_by_genus, 0,
	       sizeof g_object_slot_range_by_genus);
	memset(g_mobile_object_link_indices, 0xFF,
	       sizeof g_mobile_object_link_indices);
	g_object_table = g_test_objects;
	g_projectile_guidance_states = g_test_guidance;
	g_mobile_object_pool_base = g_test_mobiles;
	g_mobile_object_char_data_pool = g_test_char_data;
	for (int i = 0; i < SLOT_COUNT; ++i) {
		g_test_objects[i].player_owner_idx = -1;
		if (i < MOBILE_SLOTS) {
			g_test_objects[i].mobj = &g_test_mobiles[i];
		}
		if (i < CRAFT_SLOTS) {
			g_test_mobiles[i].p_craft = &g_test_craft[i];
		}
		if (i >= SHOT_START && i < SHOT_END) {
			g_test_mobiles[i].p_warhead_guidance =
				&g_test_guidance[i - SHOT_START];
		}
	}
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
	}
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = CRAFT_SLOTS;
	g_projectile_object_slot_start = SHOT_START;
	g_projectile_object_slot_end = SHOT_END;
	g_region_main_object_slot_end = MOBILE_SLOTS;
	g_region_static_object_slot_count = STATIC_SLOTS;
	struct object_slot_range *range = g_object_slot_range_by_genus;
	range[CRAFT_GENUS_OTHER_PROJECTILE].start = OTHER_SHOT_START;
	range[CRAFT_GENUS_OTHER_PROJECTILE].end = SHOT_END;
	range[CRAFT_GENUS_SMALL_DEBRIS].start = DEBRIS_START;
	range[CRAFT_GENUS_SMALL_DEBRIS].end = DEBRIS_END;
	range[CRAFT_GENUS_EXPLOSION].start = EXPLOSION_START;
	range[CRAFT_GENUS_EXPLOSION].end = EXPLOSION_END;
	g_local_player = NO_PLAYER;
	g_flight_sim_side_effects_suppressed = 0;
	g_single_object_update_override_idx = -1;
	g_elapsed_ticks = 1;
	g_sim_steps_per_second = SIMULATION_TICKS_PER_SECOND;
	g_game_time = 1000;
}

/* Puts an X-wing in craft slot obj at x, y, z, carrying nothing. */
static struct craft_data *place_craft(int obj, int x, int y, int z)
{
	g_test_objects[obj].object_type = TEST_XWING;
	g_test_objects[obj].genus_id = CRAFT_GENUS_STARFIGHTER;
	g_test_objects[obj].world_x = x;
	g_test_objects[obj].world_y = y;
	g_test_objects[obj].world_z = z;
	g_test_craft[obj].ai_flight.impact_obj_idx = UINT16_MAX;
	g_test_craft[obj].carried_object_index = UINT16_MAX;
	return &g_test_craft[obj];
}

/* Fills slots first to end - 1 with laser shots. */
static void fill_slots(int first, int end)
{
	for (int i = first; i < end; ++i) {
		g_test_objects[i].object_type =
			PROJECTILE_OBJECT_TYPE_REBEL_LASER;
	}
}

/* ------------------------------------------------------------------------ */
/* Free slots. */

/* object_alloc_slot_for_genus returns the first free slot of the genus's
 * range, sets its source_obj_idx and effect_size to 0 and empties its
 * proximity list for a rebuild. */
static void check_alloc_first_free(void)
{
	fresh_world();
	fill_slots(OTHER_SHOT_START, OTHER_SHOT_START + 2);
	g_test_objects[OTHER_SHOT_START + 4].object_type = 1;
	struct mobile_object *slot = &g_test_mobiles[OTHER_SHOT_START + 2];
	slot->source_obj_idx = 9;
	slot->effect_size = 3;
	slot->proximity_list.count = 4;
	slot->proximity_list.rebuild_ticks = 50;
	XVT_ASSERT_INT_EQ(
		object_alloc_slot_for_genus(CRAFT_GENUS_OTHER_PROJECTILE),
		OTHER_SHOT_START + 2);
	XVT_ASSERT_INT_EQ(slot->source_obj_idx, 0);
	XVT_ASSERT_INT_EQ(slot->effect_size, 0);
	XVT_ASSERT_INT_EQ(slot->proximity_list.count, 0);
	XVT_ASSERT_INT_EQ(slot->proximity_list.rebuild_ticks, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[OTHER_SHOT_START + 2].object_type, 0);
}

/* A full range, or an empty one, gives UINT16_MAX. */
static void check_alloc_full_or_empty(void)
{
	fresh_world();
	fill_slots(OTHER_SHOT_START, SHOT_END);
	g_test_objects[SHOT_END].object_type = 0;
	XVT_ASSERT_INT_EQ(
		object_alloc_slot_for_genus(CRAFT_GENUS_OTHER_PROJECTILE),
		UINT16_MAX);
	g_test_objects[SHOT_END - 1].object_type = 0;
	XVT_ASSERT_INT_EQ(
		object_alloc_slot_for_genus(CRAFT_GENUS_OTHER_PROJECTILE),
		SHOT_END - 1);
	XVT_ASSERT_INT_EQ(object_alloc_slot_for_genus(CRAFT_GENUS_MINE),
			  UINT16_MAX);
}

/* object_find_free_mission_slot returns the first free static slot after the
 * mobile slots, or UINT16_MAX when all are used. */
static void check_free_mission_slot(void)
{
	fresh_world();
	g_test_objects[STATIC_START].object_type = 70;
	g_test_objects[STATIC_START + 1].object_type = 70;
	XVT_ASSERT_INT_EQ(object_find_free_mission_slot(), STATIC_START + 2);
	fill_slots(STATIC_START, SLOT_COUNT);
	XVT_ASSERT_INT_EQ(object_find_free_mission_slot(), UINT16_MAX);
	g_test_objects[SLOT_COUNT - 1].object_type = 0;
	XVT_ASSERT_INT_EQ(object_find_free_mission_slot(), SLOT_COUNT - 1);
}

/* ------------------------------------------------------------------------ */
/* Copying and relinking. */

/* object_copy_state_preserving_storage copies the craft record, the mobile
 * record and the object record of one craft slot onto another; the
 * destination keeps its own record pointers, and its proximity list is
 * emptied for a rebuild. */
static void check_copy_keeps_storage(void)
{
	fresh_world();
	struct craft_data *source = place_craft(3, 100, 200, 300);
	g_test_objects[3].yaw = 0x1234;
	g_test_objects[3].object_signature = 0x55;
	g_test_mobiles[3].speed = 77;
	g_test_mobiles[3].proximity_list.count = 2;
	g_test_mobiles[3].p_char_data = &g_test_char_data[0];
	g_test_char_data[0].skill_value = 12;
	source->cm_ammo_count = 9;
	g_test_mobiles[5].p_char_data = &g_test_char_data[1];
	g_test_mobiles[5].proximity_list.rebuild_ticks = 40;
	object_copy_state_preserving_storage(5, 3);
	XVT_ASSERT_INT_EQ(g_test_objects[5].object_type, TEST_XWING);
	XVT_ASSERT_INT_EQ(g_test_objects[5].world_y, 200);
	XVT_ASSERT_INT_EQ(g_test_objects[5].yaw, 0x1234);
	XVT_ASSERT_INT_EQ(g_test_objects[5].object_signature, 0x55);
	XVT_ASSERT_TRUE(g_test_objects[5].mobj == &g_test_mobiles[5]);
	XVT_ASSERT_INT_EQ(g_test_mobiles[5].speed, 77);
	XVT_ASSERT_TRUE(g_test_mobiles[5].p_craft == &g_test_craft[5]);
	XVT_ASSERT_TRUE(g_test_mobiles[5].p_char_data == &g_test_char_data[1]);
	XVT_ASSERT_INT_EQ(g_test_craft[5].cm_ammo_count, 9);
	XVT_ASSERT_INT_EQ(g_test_char_data[1].skill_value, 12);
	XVT_ASSERT_INT_EQ(g_test_mobiles[5].proximity_list.count, 0);
	XVT_ASSERT_INT_EQ(g_test_mobiles[5].proximity_list.rebuild_ticks, 0);
	/* The source is left as it was. */
	XVT_ASSERT_INT_EQ(g_test_mobiles[3].proximity_list.count, 2);
}

/* Copying a shot keeps the destination's guidance pointer and copies the
 * guidance record's contents. */
static void check_copy_shot_guidance(void)
{
	fresh_world();
	g_test_objects[40].object_type = PROJECTILE_OBJECT_TYPE_REBEL_LASER;
	g_test_guidance[40 - SHOT_START].target_obj_idx = 6;
	object_copy_state_preserving_storage(41, 40);
	XVT_ASSERT_TRUE(g_test_mobiles[41].p_warhead_guidance ==
			&g_test_guidance[41 - SHOT_START]);
	XVT_ASSERT_INT_EQ(g_test_guidance[41 - SHOT_START].target_obj_idx, 6);
	XVT_ASSERT_INT_EQ(g_test_objects[41].object_type,
			  PROJECTILE_OBJECT_TYPE_REBEL_LASER);
}

/* object_relink_mobile_object_pointers points every mobile slot's mobj at its
 * pool entry; with every link index -1 it links nothing else. A guidance or
 * character index names the pool entry the slot's record pointer takes. */
static void check_relink(void)
{
	fresh_world();
	for (int i = 0; i < MOBILE_SLOTS; ++i) {
		g_test_objects[i].mobj = NULL;
	}
	g_test_mobiles[50].p_warhead_guidance = NULL;
	object_relink_mobile_object_pointers();
	for (int i = 0; i < MOBILE_SLOTS; ++i) {
		XVT_ASSERT_TRUE(g_test_objects[i].mobj == &g_test_mobiles[i]);
	}
	XVT_ASSERT_TRUE(g_test_objects[STATIC_START].mobj == NULL);
	XVT_ASSERT_TRUE(g_test_mobiles[50].p_warhead_guidance == NULL);
	XVT_ASSERT_TRUE(g_test_mobiles[3].p_craft == &g_test_craft[3]);

	g_mobile_object_link_indices[50].warhead_guidance_idx = 7;
	g_mobile_object_link_indices[300].char_data_idx = 2;
	object_relink_mobile_object_pointers();
	XVT_ASSERT_TRUE(g_test_mobiles[50].p_warhead_guidance ==
			&g_test_guidance[7]);
	XVT_ASSERT_TRUE(g_test_mobiles[300].p_char_data ==
			&g_test_char_data[2]);
}

/* ------------------------------------------------------------------------ */
/* The decoy beam. */

/* object_has_active_decoy_beam is 1 for a player's craft whose beam system
 * works and is on, is the decoy beam and has output; 0 when any of that
 * fails, for UINT16_MAX, for a slot past the craft slots, and for a craft
 * with no craft record. */
static void check_decoy_beam(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(4, 0, 0, 0);
	g_test_objects[4].player_owner_idx = 1;
	craft->working_subsystems = CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM;
	craft->beam_active = 1;
	craft->beam_type_id = BEAM_TYPE_DECOY;
	craft->beam_output = 5;
	XVT_ASSERT_INT_EQ(object_has_active_decoy_beam(4), 1);
	XVT_ASSERT_INT_EQ(object_has_active_decoy_beam(UINT16_MAX), 0);
	XVT_ASSERT_INT_EQ(object_has_active_decoy_beam(CRAFT_SLOTS), 0);

	craft->beam_output = 0;
	XVT_ASSERT_INT_EQ(object_has_active_decoy_beam(4), 0);
	craft->beam_output = 5;
	craft->beam_type_id = BEAM_TYPE_DECOY - 1;
	XVT_ASSERT_INT_EQ(object_has_active_decoy_beam(4), 0);
	craft->beam_type_id = BEAM_TYPE_DECOY;
	craft->beam_active = 0;
	XVT_ASSERT_INT_EQ(object_has_active_decoy_beam(4), 0);
	craft->beam_active = 1;
	craft->working_subsystems = CRAFT_SUBSYSTEM_FLAG_CANNONS;
	XVT_ASSERT_INT_EQ(object_has_active_decoy_beam(4), 0);
	craft->working_subsystems = CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM;
	g_test_objects[4].player_owner_idx = -1;
	XVT_ASSERT_INT_EQ(object_has_active_decoy_beam(4), 0);
	g_test_objects[4].player_owner_idx = 1;
	g_test_mobiles[4].p_craft = NULL;
	XVT_ASSERT_INT_EQ(object_has_active_decoy_beam(4), 0);

	/* The first slot past the craft slots gives 0 even when it holds a
	 * player's object linked to that working decoy beam. */
	g_test_objects[CRAFT_SLOTS].player_owner_idx = 1;
	g_test_mobiles[CRAFT_SLOTS].p_craft = craft;
	XVT_ASSERT_INT_EQ(object_has_active_decoy_beam(CRAFT_SLOTS), 0);
}

/* ------------------------------------------------------------------------ */
/* The world's edge. */

/* object_add_trig_move_delta_and_clamp_world_position adds the move
 * distances and keeps each axis within 0x1000000 either way of 0. It returns
 * world_z after the lower clamp only, so a z above the edge comes back
 * unclamped although the stored one is clamped. */
static void check_world_edge(void)
{
	fresh_world();
	struct object_record *object = &g_test_objects[5];
	object->world_x = 100;
	object->world_y = 200;
	object->world_z = 300;
	trig2_xmovedist = 10;
	trig2_ymovedist = -20;
	trig2_zmovedist = 30;
	XVT_ASSERT_INT_EQ(object_add_trig_move_delta_and_clamp_world_position(
				  (uint32_t *)object),
			  330);
	XVT_ASSERT_INT_EQ(object->world_x, 110);
	XVT_ASSERT_INT_EQ(object->world_y, 180);
	XVT_ASSERT_INT_EQ(object->world_z, 330);

	object->world_x = WORLD_EDGE - 5;
	object->world_y = -WORLD_EDGE + 5;
	object->world_z = WORLD_EDGE - 5;
	trig2_xmovedist = 10;
	trig2_ymovedist = -10;
	trig2_zmovedist = 10;
	XVT_ASSERT_INT_EQ(object_add_trig_move_delta_and_clamp_world_position(
				  (uint32_t *)object),
			  WORLD_EDGE + 5);
	XVT_ASSERT_INT_EQ(object->world_x, WORLD_EDGE);
	XVT_ASSERT_INT_EQ(object->world_y, -WORLD_EDGE);
	XVT_ASSERT_INT_EQ(object->world_z, WORLD_EDGE);

	object->world_x = -WORLD_EDGE + 5;
	object->world_y = WORLD_EDGE - 5;
	object->world_z = -WORLD_EDGE + 5;
	trig2_xmovedist = -10;
	trig2_ymovedist = 10;
	trig2_zmovedist = -10;
	XVT_ASSERT_INT_EQ(object_add_trig_move_delta_and_clamp_world_position(
				  (uint32_t *)object),
			  -WORLD_EDGE);
	XVT_ASSERT_INT_EQ(object->world_x, -WORLD_EDGE);
	XVT_ASSERT_INT_EQ(object->world_y, WORLD_EDGE);
	XVT_ASSERT_INT_EQ(object->world_z, -WORLD_EDGE);
}

/* ------------------------------------------------------------------------ */
/* Pieces and fragments. */

/* A craft in slot 3 at 100, 200, 300, flying at speed 40, yaw 0x1000 and
 * pitch 0x2000, flown by player 2. */
static void breakup_world(void)
{
	fresh_world();
	place_craft(3, 100, 200, 300);
	g_test_objects[3].yaw = 0x1000;
	g_test_objects[3].pitch = 0x2000;
	g_test_objects[3].player_owner_idx = 2;
	g_test_mobiles[3].speed = 40;
}

/* A detached component takes the first free debris slot and the craft's
 * position and angles; it becomes a component (family 3, small debris) with
 * no player, the craft's type as its source type, twice the mesh index as
 * its frame step, and 4 to 11 whole seconds of life. With every debris slot
 * taken nothing is made. */
static void check_detached_component(void)
{
	breakup_world();
	g_test_objects[DEBRIS_START].object_type = CRAFT_SPECIES_COMPONENT;
	uint16_t slot = object_spawn_detached_component(3, 7);
	XVT_ASSERT_INT_EQ(slot, DEBRIS_START + 1);
	struct object_record *piece = &g_test_objects[slot];
	XVT_ASSERT_INT_EQ(piece->object_type, CRAFT_SPECIES_COMPONENT);
	XVT_ASSERT_INT_EQ(piece->genus_id, CRAFT_GENUS_SMALL_DEBRIS);
	XVT_ASSERT_INT_EQ(piece->mobj->family, 3);
	XVT_ASSERT_INT_EQ(piece->player_owner_idx, -1);
	XVT_ASSERT_INT_EQ(piece->mobj->source_object_type, TEST_XWING);
	XVT_ASSERT_INT_EQ(piece->type_specific_byte[0], 14);
	XVT_ASSERT_INT_EQ(piece->type_specific_byte[1], 0);
	XVT_ASSERT_INT_EQ(piece->world_x, 100);
	XVT_ASSERT_INT_EQ(piece->world_z, 300);
	XVT_ASSERT_INT_EQ(piece->yaw, 0x1000);
	XVT_ASSERT_INT_EQ(piece->mobj->speed, 40);
	XVT_ASSERT_TRUE(piece->mobj == &g_test_mobiles[slot]);
	unsigned life = piece->mobj->lifetime_timer;
	XVT_ASSERT_INT_EQ(life % SIMULATION_TICKS_PER_SECOND, 0);
	XVT_ASSERT_TRUE(life >= 4 * SIMULATION_TICKS_PER_SECOND);
	XVT_ASSERT_TRUE(life <= 11 * SIMULATION_TICKS_PER_SECOND);

	fill_slots(DEBRIS_START, DEBRIS_END);
	XVT_ASSERT_INT_EQ(object_spawn_detached_component(3, 7), UINT16_MAX);
}

static int angle_turn(uint16_t to, uint16_t from)
{
	int16_t turn = (int16_t)(uint16_t)(to - from);
	return turn < 0 ? -turn : turn;
}

/* A fragment takes the first free explosion slot and the source's state, and
 * becomes an explosion (family 5) of type 133 or 134 with no player; its yaw
 * and pitch are each turned by 0x100 to 0x8FF either way, its speed raised by
 * 50 to 305, and it lives 1 to 4 whole seconds. With every explosion slot
 * taken nothing is made. */
static void check_effect_fragment(void)
{
	for (int round = 0; round < 20; ++round) {
		breakup_world();
		uint16_t slot = object_spawn_effect_fragment(3);
		XVT_ASSERT_INT_EQ(slot, EXPLOSION_START);
		struct object_record *fragment = &g_test_objects[slot];
		XVT_ASSERT_TRUE(fragment->object_type == 133 ||
				fragment->object_type == 134);
		XVT_ASSERT_INT_EQ(fragment->genus_id, CRAFT_GENUS_EXPLOSION);
		XVT_ASSERT_INT_EQ(fragment->mobj->family, 5);
		XVT_ASSERT_INT_EQ(fragment->player_owner_idx, -1);
		XVT_ASSERT_INT_EQ(fragment->mobj->source_object_type,
				  TEST_XWING);
		XVT_ASSERT_INT_EQ(fragment->world_y, 200);
		int yaw_turn = angle_turn(fragment->yaw, 0x1000);
		int pitch_turn = angle_turn(fragment->pitch, 0x2000);
		XVT_ASSERT_TRUE(yaw_turn >= 0x100 && yaw_turn <= 0x8FF);
		XVT_ASSERT_TRUE(pitch_turn >= 0x100 && pitch_turn <= 0x8FF);
		int speed_gain = fragment->mobj->speed - 40;
		XVT_ASSERT_TRUE(speed_gain >= 50 && speed_gain <= 305);
		unsigned life = fragment->mobj->lifetime_timer;
		XVT_ASSERT_INT_EQ(life % SIMULATION_TICKS_PER_SECOND, 0);
		XVT_ASSERT_TRUE(life >= SIMULATION_TICKS_PER_SECOND);
		XVT_ASSERT_TRUE(life <= 4 * SIMULATION_TICKS_PER_SECOND);
		XVT_ASSERT_INT_EQ(fragment->mobj->orient_matrix_dirty, 1);
		XVT_ASSERT_INT_EQ(fragment->mobj->move_vector_dirty, 1);
	}
	fill_slots(EXPLOSION_START, EXPLOSION_END);
	XVT_ASSERT_INT_EQ(object_spawn_effect_fragment(3), UINT16_MAX);
}

/* A local fragment is object type 157 with effect size 2 and frame step 2;
 * it faces back along the source, flies at speed 35 to 50 for 39 to 42
 * ticks, and is moved at once from where the source was. */
static void check_local_effect_fragment(void)
{
	for (int round = 0; round < 20; ++round) {
		breakup_world();
		struct mobile_object *source = &g_test_mobiles[3];
		fview_calcrotatemove(g_test_objects[3].pitch,
				     g_test_objects[3].yaw, &g_test_objects[3]);
		uint16_t slot = object_spawn_local_effect_fragment(3);
		XVT_ASSERT_INT_EQ(slot, EXPLOSION_START);
		struct object_record *fragment = &g_test_objects[slot];
		XVT_ASSERT_INT_EQ(fragment->object_type, 157);
		XVT_ASSERT_INT_EQ(fragment->genus_id, CRAFT_GENUS_EXPLOSION);
		XVT_ASSERT_INT_EQ(fragment->mobj->family, 5);
		XVT_ASSERT_INT_EQ(fragment->mobj->effect_size, 2);
		XVT_ASSERT_INT_EQ(fragment->type_specific_byte[0], 2);
		XVT_ASSERT_INT_EQ(fragment->player_owner_idx, -1);
		XVT_ASSERT_TRUE(fragment->mobj->speed >= 35);
		XVT_ASSERT_TRUE(fragment->mobj->speed <= 50);
		XVT_ASSERT_TRUE(fragment->mobj->lifetime_timer >= 39);
		XVT_ASSERT_TRUE(fragment->mobj->lifetime_timer <= 42);
		/* Facing back: its direction of travel is against the
		 * source's. */
		struct mobile_object *moved = fragment->mobj;
		XVT_ASSERT_TRUE(source->move_x * moved->move_x +
					source->move_y * moved->move_y +
					source->move_z * moved->move_z <
				0);
		XVT_ASSERT_INT_EQ(fragment->mobj->prev_world_x, 100);
		XVT_ASSERT_INT_EQ(fragment->mobj->prev_world_y, 200);
		XVT_ASSERT_INT_EQ(fragment->mobj->prev_world_z, 300);
		XVT_ASSERT_TRUE(fragment->world_x != 100 ||
				fragment->world_y != 200 ||
				fragment->world_z != 300);
	}
	fill_slots(EXPLOSION_START, EXPLOSION_END);
	XVT_ASSERT_INT_EQ(object_spawn_local_effect_fragment(3), UINT16_MAX);
}

/* ------------------------------------------------------------------------ */
/* The step. */

/* An explosion in slot obj at 1000, 2000, 3000, flying along -X (move_x is
 * exactly -1 in Q15) at speed. */
static struct object_record *place_explosion(int obj, int speed, int life)
{
	struct object_record *object = &g_test_objects[obj];
	object->object_type = 133;
	object->genus_id = CRAFT_GENUS_EXPLOSION;
	object->world_x = 1000;
	object->world_y = 2000;
	object->world_z = 3000;
	object->mobj->family = 5;
	object->mobj->speed = (uint16_t)speed;
	object->mobj->move_x = INT16_MIN;
	object->mobj->lifetime_timer = (uint16_t)life;
	return object;
}

/* World units moved in ticks at speed: (4,660 times speed + 128) / 256 per
 * simulated second, as mobile_object.speed's comment gives it. */
static int distance_moved(int speed, int ticks)
{
	return ticks * ((4660 * speed + 128) / 256) /
	       SIMULATION_TICKS_PER_SECOND;
}

/* Each object's life counts down by the step's ticks, its previous position
 * takes its current one, and it moves along its move vector by its speed's
 * distance. */
static void check_step_moves_and_counts_down(void)
{
	fresh_world();
	g_elapsed_ticks = 59;
	struct object_record *object =
		place_explosion(EXPLOSION_START, 300, 100);
	object_update_lifetime_and_movement();
	XVT_ASSERT_INT_EQ(object->mobj->lifetime_timer, 41);
	XVT_ASSERT_INT_EQ(object->mobj->prev_world_x, 1000);
	XVT_ASSERT_INT_EQ(object->world_x, 1000 - distance_moved(300, 59));
	XVT_ASSERT_INT_EQ(object->world_y, 2000);
	XVT_ASSERT_INT_EQ(g_elapsed_ticks, 59);

	/* A whole second at speed 3 covers (13,980 + 128) / 256, 55 world
	 * units, rounded down. */
	g_elapsed_ticks = SIMULATION_TICKS_PER_SECOND;
	object = place_explosion(EXPLOSION_START + 1, 3, 0);
	object_update_lifetime_and_movement();
	XVT_ASSERT_INT_EQ(object->world_x, 1000 - 55);
	XVT_ASSERT_INT_EQ(object->mobj->lifetime_timer, 0);
	XVT_ASSERT_INT_EQ(object->object_type, 133);
}

/* At the end of its life a laser shot or anything not a craft, warhead or
 * small debris is removed; a warhead explodes in place. */
static void check_step_end_of_life(void)
{
	fresh_world();
	g_elapsed_ticks = 5;
	struct object_record *explosion =
		place_explosion(EXPLOSION_START, 0, 5);
	struct object_record *laser = &g_test_objects[OTHER_SHOT_START];
	laser->object_type = PROJECTILE_OBJECT_TYPE_REBEL_LASER;
	laser->genus_id = CRAFT_GENUS_OTHER_PROJECTILE;
	laser->mobj->lifetime_timer = 3;
	struct object_record *warhead = &g_test_objects[OTHER_SHOT_START + 1];
	warhead->object_type = WARHEAD_OBJECT_TYPE_PROTON_TORPEDO;
	warhead->genus_id = CRAFT_GENUS_OTHER_PROJECTILE;
	warhead->mobj->lifetime_timer = 5;
	g_test_guidance[OTHER_SHOT_START + 1 - SHOT_START].target_obj_idx =
		UINT16_MAX;
	struct object_record *lasting = &g_test_objects[OTHER_SHOT_START + 2];
	lasting->object_type = PROJECTILE_OBJECT_TYPE_REBEL_LASER;
	lasting->genus_id = CRAFT_GENUS_OTHER_PROJECTILE;
	lasting->mobj->lifetime_timer = 6;
	object_update_lifetime_and_movement();
	XVT_ASSERT_INT_EQ(explosion->object_type, 0);
	XVT_ASSERT_INT_EQ(laser->object_type, 0);
	XVT_ASSERT_INT_EQ(warhead->genus_id, CRAFT_GENUS_EXPLOSION);
	XVT_ASSERT_TRUE(warhead->object_type != 0);
	XVT_ASSERT_INT_EQ(lasting->object_type,
			  PROJECTILE_OBJECT_TYPE_REBEL_LASER);
	XVT_ASSERT_INT_EQ(lasting->mobj->lifetime_timer, 1);
}

/* A homing warhead explodes when its target's slot no longer holds the object
 * it was fired at (another signature) or holds nothing. */
static void check_step_homing_target_gone(void)
{
	fresh_world();
	place_craft(4, 0, 0, 0);
	g_test_objects[4].object_signature = 0x42;
	for (int i = 0; i < 2; ++i) {
		struct object_record *warhead =
			&g_test_objects[OTHER_SHOT_START + i];
		warhead->object_type = WARHEAD_OBJECT_TYPE_PROTON_TORPEDO;
		warhead->genus_id = CRAFT_GENUS_OTHER_PROJECTILE;
		warhead->mobj->lifetime_timer = 500;
		struct warhead_guidance_state *guidance =
			&g_test_guidance[OTHER_SHOT_START + i - SHOT_START];
		guidance->homing_tier = 3;
		guidance->target_obj_idx = (uint16_t)(4 + i);
		guidance->target_signature = 0x41;
	}
	object_update_lifetime_and_movement();
	XVT_ASSERT_INT_EQ(g_test_objects[OTHER_SHOT_START].genus_id,
			  CRAFT_GENUS_EXPLOSION);
	XVT_ASSERT_INT_EQ(g_test_objects[OTHER_SHOT_START + 1].genus_id,
			  CRAFT_GENUS_EXPLOSION);
}

/* An object with its own sim_state_timestamp is stepped from there to the
 * game time plus the step's ticks, and its timestamp moves on; when that
 * comes to 0 ticks it is skipped. g_elapsed_ticks is put back after. */
static void check_step_own_timestamp(void)
{
	fresh_world();
	g_elapsed_ticks = 2;
	struct object_record *behind = place_explosion(EXPLOSION_START, 0, 100);
	behind->mobj->sim_state_timestamp = 990;
	struct object_record *current =
		place_explosion(EXPLOSION_START + 1, 0, 100);
	current->mobj->sim_state_timestamp = 1002;
	object_update_lifetime_and_movement();
	XVT_ASSERT_INT_EQ(behind->mobj->lifetime_timer, 88);
	XVT_ASSERT_INT_EQ(behind->mobj->sim_state_timestamp, 1002);
	XVT_ASSERT_INT_EQ(current->mobj->lifetime_timer, 100);
	XVT_ASSERT_INT_EQ(current->mobj->sim_state_timestamp, 1002);
	XVT_ASSERT_INT_EQ(g_elapsed_ticks, 2);
}

/* With g_single_object_update_override_idx set, only that object is
 * stepped, and its previous position is left as it was. */
static void check_step_single_object(void)
{
	fresh_world();
	g_elapsed_ticks = 4;
	struct object_record *first = place_explosion(EXPLOSION_START, 0, 100);
	struct object_record *picked =
		place_explosion(EXPLOSION_START + 1, 0, 100);
	picked->mobj->prev_world_x = 7;
	g_single_object_update_override_idx = EXPLOSION_START + 1;
	object_update_lifetime_and_movement();
	XVT_ASSERT_INT_EQ(first->mobj->lifetime_timer, 100);
	XVT_ASSERT_INT_EQ(picked->mobj->lifetime_timer, 96);
	XVT_ASSERT_INT_EQ(picked->mobj->prev_world_x, 7);
}

/* A spinning object turns its roll by 4 times its roll_impulse_rate per
 * simulated second. A craft hit by an impact also brings the rate toward 0
 * by 4,096 per simulated second, and clears impact_obj_idx when it gets
 * there. */
static void check_step_roll_impulse(void)
{
	fresh_world();
	g_elapsed_ticks = 59;
	struct object_record *spinning =
		place_explosion(EXPLOSION_START, 0, 100);
	spinning->mobj->roll_impulse_rate = 2000;
	spinning->roll = 100;
	object_update_lifetime_and_movement();
	XVT_ASSERT_INT_EQ(spinning->roll, 100 + 4 * (59 * 2000 / 236));
	XVT_ASSERT_INT_EQ(spinning->mobj->orient_matrix_dirty, 1);

	fresh_world();
	g_elapsed_ticks = 59;
	struct craft_data *craft = place_craft(4, 0, 0, 0);
	craft->ai_flight.impact_obj_idx = 9;
	g_test_mobiles[4].roll_impulse_rate = 2000;
	object_update_lifetime_and_movement();
	XVT_ASSERT_INT_EQ(g_test_mobiles[4].roll_impulse_rate, 2000 - 1024);
	XVT_ASSERT_INT_EQ(craft->ai_flight.impact_obj_idx, 9);
	object_update_lifetime_and_movement();
	XVT_ASSERT_INT_EQ(g_test_mobiles[4].roll_impulse_rate, 0);
	XVT_ASSERT_INT_EQ(craft->ai_flight.impact_obj_idx, UINT16_MAX);

	fresh_world();
	g_elapsed_ticks = 59;
	craft = place_craft(4, 0, 0, 0);
	craft->ai_flight.impact_obj_idx = 9;
	g_test_mobiles[4].roll_impulse_rate = -1500;
	object_update_lifetime_and_movement();
	XVT_ASSERT_INT_EQ(g_test_mobiles[4].roll_impulse_rate, -1500 + 1024);
	object_update_lifetime_and_movement();
	XVT_ASSERT_INT_EQ(g_test_mobiles[4].roll_impulse_rate, 0);
	XVT_ASSERT_INT_EQ(craft->ai_flight.impact_obj_idx, UINT16_MAX);

	/* A rate that lands exactly on 0 clears impact_obj_idx too, from
	 * either side. */
	for (int sign = -1; sign <= 1; sign += 2) {
		fresh_world();
		g_elapsed_ticks = 59;
		craft = place_craft(4, 0, 0, 0);
		craft->ai_flight.impact_obj_idx = 9;
		g_test_mobiles[4].roll_impulse_rate = (int16_t)(sign * 1024);
		object_update_lifetime_and_movement();
		XVT_ASSERT_INT_EQ(g_test_mobiles[4].roll_impulse_rate, 0);
		XVT_ASSERT_INT_EQ(craft->ai_flight.impact_obj_idx, UINT16_MAX);
	}
}

int main(void)
{
	fail_after_seconds(20);
	check_alloc_first_free();
	check_alloc_full_or_empty();
	check_free_mission_slot();
	check_copy_keeps_storage();
	check_copy_shot_guidance();
	check_relink();
	check_decoy_beam();
	check_world_edge();
	check_detached_component();
	check_effect_fragment();
	check_local_effect_fragment();
	check_step_moves_and_counts_down();
	check_step_end_of_life();
	check_step_homing_target_gone();
	check_step_own_timestamp();
	check_step_single_object();
	check_step_roll_impulse();
	return 0;
}
