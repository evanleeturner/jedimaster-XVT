/* Tests for xvt/flight/ai_mines.c, a mine's target search, on worlds this
 * file builds in the game's slot layout: CRAFT_SLOTS craft slots, then
 * STATIC_COUNT static slots, the last two of them mines. Targets are picked
 * by flight group (target type 1). No game data is read. Each rule is
 * checked against the 1997 search it was ported from
 * (paifight_find_nearest_matching_target_from_origin). */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/object_genus.h"
#include "xvt/assets/object_type.h"
#include "xvt/flight/ai_mines.h"
#include "xvt/flight/ai_state.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_geometry.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"

enum {
	CRAFT_SLOTS = 8,
	STATIC_FIRST = 8,
	STATIC_COUNT = 6,
	MINE = 12,
	MINE_2 = 13,
	SLOT_COUNT = 14,
	TEST_XWING = 1,	 /* Object type 1, a starfighter. */
	TEST_STATIC = 1, /* Object type 1 again, given behavior flag 2. */
	TARGET_GROUP = 2,
	OTHER_GROUP = 3,
	NONE = UINT16_MAX,
	RANGE_MAX = 0x10000,
};

static struct object_record g_test_objects[SLOT_COUNT];
static struct mobile_object g_test_mobiles[SLOT_COUNT];
static struct craft_data g_test_craft[CRAFT_SLOTS];

/* An empty world with the mine in slot MINE at the origin, Mine Type A, and
 * mine 2 in slot MINE_2 far away on the x axis. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_mission_flight_groups, 0,
	       4 * sizeof g_mission_flight_groups[0]);
	g_object_table = g_test_objects;
	for (int slot = 0; slot < CRAFT_SLOTS; ++slot) {
		g_test_objects[slot].player_owner_idx = -1;
		g_test_objects[slot].mobj = &g_test_mobiles[slot];
		g_test_mobiles[slot].p_craft = &g_test_craft[slot];
	}
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = CRAFT_SLOTS;
	g_region_main_object_slot_end = STATIC_FIRST;
	g_region_static_object_slot_count = STATIC_COUNT;
	g_object_type_table[TEST_STATIC].behavior_flags = 2;
	g_object_type_table[CRAFT_SPECIES_MINE_TYPE_A].behavior_flags = 0;
	g_object_type_table[CRAFT_SPECIES_MINE_TYPE_B].behavior_flags = 0;
	g_object_type_table[CRAFT_SPECIES_MINE_TYPE_C].behavior_flags = 0;
	g_test_objects[MINE].object_type = CRAFT_SPECIES_MINE_TYPE_A;
	g_test_objects[MINE].genus_id = CRAFT_GENUS_MINE;
	g_test_objects[MINE_2].object_type = CRAFT_SPECIES_MINE_TYPE_A;
	g_test_objects[MINE_2].genus_id = CRAFT_GENUS_MINE;
	g_test_objects[MINE_2].world_x = 0x50000;
	g_flight_sim_side_effects_suppressed = 0;
}

/* Puts an active computer X-wing of flight group fg at x, y, z in craft slot
 * obj, working, undamaged (hull_max 100). */
static struct craft_data *place_craft(int obj, int fg, int x, int y, int z)
{
	g_test_objects[obj].object_type = TEST_XWING;
	g_test_objects[obj].genus_id = CRAFT_GENUS_STARFIGHTER;
	g_test_objects[obj].flight_group_idx = (uint8_t)fg;
	g_test_objects[obj].world_x = x;
	g_test_objects[obj].world_y = y;
	g_test_objects[obj].world_z = z;
	struct craft_data *craft = &g_test_craft[obj];
	craft->object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	craft->working_subsystems = 1;
	craft->hull_max = 100;
	return craft;
}

/* Puts a static object of flight group fg at x, y, z in static slot obj. */
static void place_static(int obj, int fg, int x, int y, int z)
{
	g_test_objects[obj].object_type = TEST_STATIC;
	g_test_objects[obj].flight_group_idx = (uint8_t)fg;
	g_test_objects[obj].world_x = x;
	g_test_objects[obj].world_y = y;
	g_test_objects[obj].world_z = z;
}

/* What the mine in slot mine finds for flight group fg. */
static uint16_t find(int mine, int fg)
{
	return ai_mines_find_target((uint16_t)mine, 1, (uint16_t)fg, 1, 0, 0);
}

/* A matching craft 0x8000 away is found; none matching is not. */
static void check_find_matching_craft(void)
{
	fresh_world();
	place_craft(0, TARGET_GROUP, 0x8000, 0, 0);
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
	XVT_ASSERT_INT_EQ(g_last_rough_distance, 0x8000);
	XVT_ASSERT_INT_EQ(find(MINE, OTHER_GROUP), NONE);
}

/* On one axis the rough distance is exact: 0x10000 is found, 0x10001 not. */
static void check_range_edge(void)
{
	fresh_world();
	place_craft(0, TARGET_GROUP, 0, 0x10000, 0);
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
	g_test_objects[0].world_y = RANGE_MAX + 1;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), NONE);
}

/* The nearer of two matches wins; a tie keeps the lower slot. */
static void check_nearest_and_tie(void)
{
	fresh_world();
	place_craft(0, TARGET_GROUP, 0x9000, 0, 0);
	place_craft(1, TARGET_GROUP, 0x4000, 0, 0);
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 1);
	g_test_objects[1].world_x = -0x9000;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
	g_test_objects[0].object_type = 0;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 1);
}

/* The two targets join by OR or AND. Target type 5 is the IFF of a craft. */
static void check_joins(void)
{
	fresh_world();
	place_craft(0, TARGET_GROUP, 0x2000, 0, 0);
	g_test_mobiles[0].iff = 4;
	place_craft(1, OTHER_GROUP, 0x1000, 0, 0);
	g_test_mobiles[1].iff = 4;
	XVT_ASSERT_INT_EQ(ai_mines_find_target(MINE, 1, TARGET_GROUP, 1, 5, 4),
			  1);
	XVT_ASSERT_INT_EQ(ai_mines_find_target(MINE, 1, TARGET_GROUP, 0, 5, 4),
			  0);
	XVT_ASSERT_INT_EQ(ai_mines_find_target(MINE, 1, TARGET_GROUP, 0, 5, 5),
			  NONE);
	XVT_ASSERT_INT_EQ(ai_mines_find_target(MINE, 1, TARGET_GROUP, 1, 5, 5),
			  0);
}

/* A Mine Type B skips a craft with no working subsystems and takes a
 * static; Types A and C take that craft. */
static void check_type_b(void)
{
	fresh_world();
	place_craft(0, TARGET_GROUP, 0x1000, 0, 0)->working_subsystems = 0;
	place_static(9, TARGET_GROUP, 0x7000, 0, 0);
	g_test_objects[MINE].object_type = CRAFT_SPECIES_MINE_TYPE_B;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 9);
	g_test_objects[MINE].object_type = CRAFT_SPECIES_MINE_TYPE_A;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
	g_test_objects[MINE].object_type = CRAFT_SPECIES_MINE_TYPE_C;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
	/* A Type B takes a craft that works. */
	g_test_craft[0].working_subsystems = 1;
	g_test_objects[MINE].object_type = CRAFT_SPECIES_MINE_TYPE_B;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
	/* A mobile object with no craft counts as working. */
	g_test_mobiles[0].p_craft = NULL;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
}

/* A craft of kind 1, breaking up or exploding is skipped. */
static void check_craft_kinds(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, TARGET_GROUP, 0x1000, 0, 0);
	craft->object_kind = CRAFT_OBJECT_KIND_UNKNOWN_1;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), NONE);
	craft->object_kind = CRAFT_OBJECT_KIND_BREAKING_UP;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), NONE);
	craft->object_kind = CRAFT_OBJECT_KIND_EXPLODING;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), NONE);
	craft->object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
}

/* A craft going into hyperspace is skipped at phase 1, kept at phase 0. */
static void check_hyperspace(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, TARGET_GROUP, 0x1000, 0, 0);
	craft->ai_controller.maneuver_mode = AI_MANEUVER_MODE_INTO_HYPERSPACE;
	craft->ai_controller.maneuver_phase = 1;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), NONE);
	craft->ai_controller.maneuver_phase = 0;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
}

/* A decoy craft 0xC000 away on one axis is found (the large range, from the
 * mine). At (0xC000, 0xA000, 0) the flight geometry reads 0x11000, past the
 * decoy range though inside the search range: refused, and found with its
 * decoy off. */
static void check_decoy(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, TARGET_GROUP, 0xC000, 0, 0);
	craft->beam_type_id = BEAM_TYPE_DECOY;
	craft->beam_active = 1;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
	/* Exactly the decoy range on one axis is kept. */
	g_test_objects[0].world_x = 0x10000;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
	g_test_objects[0].world_x = 0xC000;
	g_test_objects[0].world_y = 0xA000;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), NONE);
	XVT_ASSERT_INT_EQ(g_last_rough_distance, 0x11000);
	craft->beam_active = 0;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
}

/* An AI craft with hull damage past its maximum is skipped, at its maximum
 * kept, and a player's craft is kept past it. */
static void check_hull(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(0, TARGET_GROUP, 0x1000, 0, 0);
	craft->hull_damage = 101;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), NONE);
	craft->hull_damage = 100;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
	craft->hull_damage = 101;
	g_test_objects[0].player_owner_idx = 0;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
}

/* A static with flag 2 is found when no craft matches; one without never. A
 * nearer static beats a craft and a nearer craft beats a static. */
static void check_statics(void)
{
	fresh_world();
	place_static(9, TARGET_GROUP, 0x3000, 0, 0);
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 9);
	g_object_type_table[TEST_STATIC].behavior_flags = 0;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), NONE);
	g_object_type_table[TEST_STATIC].behavior_flags = 2;
	place_craft(0, TARGET_GROUP, 0x5000, 0, 0);
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 9);
	g_test_objects[0].world_x = 0x1000;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
	/* A static that does not match is never taken. */
	place_static(10, OTHER_GROUP, 0x500, 0, 0);
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
	/* A tie goes to the craft, found first. */
	g_test_objects[0].world_x = 0x3000;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
}

/* The origin is the mine: two mines pick different targets. */
static void check_origin_is_the_mine(void)
{
	fresh_world();
	place_craft(0, TARGET_GROUP, 0x8000, 0x3000, 0x5000);
	place_craft(1, TARGET_GROUP, 0x50000 + 0x6000, 0x3000, 0x5000);
	g_test_objects[MINE].world_y = 0x3000;
	g_test_objects[MINE].world_z = 0x5000;
	g_test_objects[MINE_2].world_y = 0x3000;
	g_test_objects[MINE_2].world_z = 0x5000;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
	XVT_ASSERT_INT_EQ(find(MINE_2, TARGET_GROUP), 1);
	/* The mine's own y and z count: a target level with it at exactly
	 * the search range is found. */
	g_test_objects[0].world_x = RANGE_MAX;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
	g_test_objects[0].world_y = 0x3000 + 1;
	g_test_objects[0].world_x = RANGE_MAX - 4;
	XVT_ASSERT_INT_EQ(find(MINE, TARGET_GROUP), 0);
}

int main(void)
{
	check_find_matching_craft();
	check_range_edge();
	check_nearest_and_tie();
	check_joins();
	check_type_b();
	check_craft_kinds();
	check_hyperspace();
	check_decoy();
	check_hull();
	check_statics();
	check_origin_is_the_mine();
	return 0;
}
