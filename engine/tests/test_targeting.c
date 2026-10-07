/* Tests for xvt/flight/targeting.c: the aim cone test that decides whether an
 * object lies ahead of the player's craft, and the size of a target box. Each
 * check builds the flight it needs in an object table this file owns: the
 * player's craft and one object to test. The player's craft faces down the
 * world's negative Y axis, its side axis down negative X and its up axis down
 * negative Z, each component exactly -1, so the products the test takes are
 * exact. The model sizes are set by hand; no game data is read. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/targeting.h"

enum {
	TEST_SLOTS = 8,
	PLAYER = 2,
	PLAYER_SLOT = 1,
	TARGET_SLOT = 4,
	TARGET_MODEL = 5,
	TINY_MODEL = 6,
	TARGET_TYPE = 1,
	PLAIN_TYPE = 120, /* An object type with no craft record here. */
	MINUS_ONE_Q15 = -32768,
	NO_SCORE = 0xFFFF,
	/* The player's craft sits off the axes. */
	PLAYER_X = 123456,
	PLAYER_Y = -234560,
	PLAYER_Z = 345600,
	NEAR_SCALE = 16,
	FAR_SCALE = 256,
	/* The target model's sizes, 600 each shifted left by 4: 9,600 units. */
	TARGET_SIZE = 600,
	TARGET_SHIFT = 4,
};

static struct object_record g_test_objects[TEST_SLOTS];
static struct mobile_object g_test_mobiles[TEST_SLOTS];
static struct craft_data g_test_craft[TEST_SLOTS];

/* Player 2 flies the craft in slot 1 and has no current target. Slot 4 holds
 * a craft of the target model, whose box is 9,600 units across; where the
 * check does not move it, it sits on the player's craft. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_players, 0, sizeof g_players);
	g_object_table = g_test_objects;
	for (int i = 0; i < TEST_SLOTS; ++i) {
		g_test_objects[i].player_owner_idx = -1;
		g_test_objects[i].mobj = &g_test_mobiles[i];
		g_test_mobiles[i].p_craft = &g_test_craft[i];
	}
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
		g_players[i].current_target_object_idx = -1;
	}
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = TEST_SLOTS;
	g_local_player = PLAYER;

	struct object_record *player = &g_test_objects[PLAYER_SLOT];
	player->object_type = TARGET_TYPE;
	player->player_owner_idx = PLAYER;
	player->world_x = PLAYER_X;
	player->world_y = PLAYER_Y;
	player->world_z = PLAYER_Z;
	g_test_mobiles[PLAYER_SLOT].cached_fwd_y = MINUS_ONE_Q15;
	g_test_mobiles[PLAYER_SLOT].cached_side_x = MINUS_ONE_Q15;
	g_test_mobiles[PLAYER_SLOT].cached_up_z = MINUS_ONE_Q15;
	g_players[PLAYER].object_index = PLAYER_SLOT;

	struct object_record *target = &g_test_objects[TARGET_SLOT];
	target->object_type = TARGET_TYPE;
	target->world_x = PLAYER_X;
	target->world_y = PLAYER_Y;
	target->world_z = PLAYER_Z;
	g_test_craft[TARGET_SLOT].model_index = TARGET_MODEL;
	g_model_defs[TARGET_MODEL].bound_size_x = TARGET_SIZE;
	g_model_defs[TARGET_MODEL].bound_size_y = TARGET_SIZE;
	g_model_defs[TARGET_MODEL].bound_size_z = TARGET_SIZE;
	g_model_defs[TARGET_MODEL].bound_size_shift = TARGET_SHIFT;
	g_model_defs[TINY_MODEL].bound_size_x = 3;
	g_model_defs[TINY_MODEL].bound_size_y = 3;
	g_model_defs[TINY_MODEL].bound_size_z = 3;
	g_model_defs[TINY_MODEL].bound_size_shift = 0;
}

/* Puts the target forward, side and up of the player's craft, in world units
 * at the given scale: forward is along the craft's forward axis, side and up
 * along its side and up axes. */
static void place_target(int forward, int side, int up, int scale)
{
	g_test_objects[TARGET_SLOT].world_x = PLAYER_X - side * scale;
	g_test_objects[TARGET_SLOT].world_y = PLAYER_Y - forward * scale;
	g_test_objects[TARGET_SLOT].world_z = PLAYER_Z - up * scale;
}

static int aim(int narrow_cone)
{
	return targeting_test_aim_cone(TARGET_SLOT, (int16_t)narrow_cone,
				       PLAYER);
}

/* A player with no craft has nothing in the cone: the answer is 0 and the
 * score 0xFFFF. */
static void check_aim_without_craft(void)
{
	fresh_world();
	place_target(8192, 0, 0, NEAR_SCALE);
	g_players[PLAYER].object_index = -1;
	g_target_angle_score = 5;
	XVT_ASSERT_INT_EQ(aim(0), 0);
	XVT_ASSERT_INT_EQ(g_target_angle_score, NO_SCORE);
}

/* An object dead ahead is in both cones with a score of 0. Within a rough
 * distance of 655,360 the test leaves the rough distance and the object's
 * position in the globals it names. */
static void check_aim_dead_ahead(void)
{
	fresh_world();
	place_target(8192, 0, 0, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(1), 1);
	XVT_ASSERT_INT_EQ(g_target_angle_score, 0);
	XVT_ASSERT_INT_EQ(aim(0), 1);
	XVT_ASSERT_INT_EQ(g_target_angle_score, 0);
	XVT_ASSERT_INT_EQ(g_last_rough_distance, 8192 * NEAR_SCALE);
	XVT_ASSERT_INT_EQ(g_world_loc_x, PLAYER_X);
	XVT_ASSERT_INT_EQ(g_world_loc_y, PLAYER_Y - 8192 * NEAR_SCALE);
	XVT_ASSERT_INT_EQ(g_world_loc_z, PLAYER_Z);
}

/* An object that is not ahead, behind the craft or level with it, is not in
 * the cone, and the score is 0xFFFF. */
static void check_aim_not_ahead(void)
{
	fresh_world();
	place_target(-8192, 0, 0, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(0), 0);
	XVT_ASSERT_INT_EQ(g_target_angle_score, NO_SCORE);

	place_target(0, 16, 0, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(0), 0);
	XVT_ASSERT_INT_EQ(g_target_angle_score, NO_SCORE);
}

/* The side slope is 256 times the side offset over the forward distance,
 * rounded: 160 is allowed, 161 ends the test with the score at 0xFFFF. The
 * up slope ends it above 100. At 256 forward the box is far wider than either
 * slope, so an object inside the limits is in the cone. */
static void check_aim_slope_limits(void)
{
	fresh_world();
	place_target(256, 160, 0, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(0), 1);
	XVT_ASSERT_INT_EQ(g_target_angle_score, 160);
	place_target(256, 161, 0, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(0), 0);
	XVT_ASSERT_INT_EQ(g_target_angle_score, NO_SCORE);

	/* Up slope 100 scores 100 * 59578 / 65536, 90 rounded down. */
	place_target(256, 0, 100, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(0), 1);
	XVT_ASSERT_INT_EQ(g_target_angle_score, 59578 * 100 >> 16);
	place_target(256, 0, -101, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(0), 0);
	XVT_ASSERT_INT_EQ(g_target_angle_score, NO_SCORE);
}

/* The score is the up slope times 59578 / 65536 plus the side slope; an
 * offset to either side of an axis scores the same. */
static void check_aim_score(void)
{
	fresh_world();
	place_target(256, 10, 20, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(0), 1);
	XVT_ASSERT_INT_EQ(g_target_angle_score, (59578 * 20 >> 16) + 10);
	place_target(256, -10, -20, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(0), 1);
	XVT_ASSERT_INT_EQ(g_target_angle_score, (59578 * 20 >> 16) + 10);
}

/* At 8,192 forward the box's slope is 18 (9,600 units at 1/16, times 256 over
 * the distance). The narrow cone takes slopes below it; the wide cone below
 * three times it, 54. */
static void check_aim_cone_bound(void)
{
	fresh_world();
	/* A side offset of 32 per unit of slope at this distance. */
	place_target(8192, 32 * 17, 0, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(1), 1);
	place_target(8192, 32 * 18, 0, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(1), 0);
	XVT_ASSERT_INT_EQ(g_target_angle_score, 18);
	XVT_ASSERT_INT_EQ(aim(0), 1);
	place_target(8192, 32 * 53, 0, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(0), 1);
	place_target(8192, 32 * 54, 0, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(0), 0);
	XVT_ASSERT_INT_EQ(g_target_angle_score, 54);

	/* The up slope is held to the same bound, after its scaling: 20
	 * scores 18. */
	place_target(8192, 0, 32 * 19, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(1), 1);
	place_target(8192, 0, 32 * 20, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(1), 0);
}

/* Closer in than 0x2000 forward the narrow cone's bound is halved: at 4,096
 * forward the box's slope is 37 at 1/16 and 18 at 1/32, so a side slope of
 * 20 is in the wide cone but not the narrow one. */
static void check_aim_narrow_close_in(void)
{
	fresh_world();
	/* A side offset of 16 per unit of slope at this distance. */
	place_target(4096, 16 * 17, 0, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(1), 1);
	place_target(4096, 16 * 20, 0, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(1), 0);
	XVT_ASSERT_INT_EQ(aim(0), 1);
}

/* The bound is at least the object's slope of 1; the wide cone's is at least
 * 9. A model 3 units across, at 2,048 forward, has a slope of 0. */
static void check_aim_least_bound(void)
{
	fresh_world();
	g_test_craft[TARGET_SLOT].model_index = TINY_MODEL;
	place_target(2048, 0, 0, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(1), 1);
	place_target(2048, 8, 0, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(1), 0);
	XVT_ASSERT_INT_EQ(g_target_angle_score, 1);
	/* A side offset of 8 per unit of slope at this distance. */
	place_target(2048, 8 * 8, 0, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(0), 1);
	XVT_ASSERT_INT_EQ(g_target_angle_score, 8);
	place_target(2048, 8 * 9, 0, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(0), 0);
	XVT_ASSERT_INT_EQ(g_target_angle_score, 9);
}

/* An object without a craft record takes its size from its type's
 * max_bounds_extent: 9,600 units gives the same bound as the target model. */
static void check_aim_plain_object(void)
{
	fresh_world();
	g_test_objects[TARGET_SLOT].object_type = PLAIN_TYPE;
	g_test_mobiles[TARGET_SLOT].p_craft = NULL;
	int saved_extent = g_object_type_table[PLAIN_TYPE].max_bounds_extent;
	g_object_type_table[PLAIN_TYPE].max_bounds_extent = TARGET_SIZE
							    << TARGET_SHIFT;
	place_target(8192, 32 * 17, 0, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(1), 1);
	place_target(8192, 32 * 18, 0, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(1), 0);
	g_object_type_table[PLAIN_TYPE].max_bounds_extent = saved_extent;
}

/* Beyond a rough distance of 655,360 the test measures at 1/256: an object
 * dead ahead two million units away is in the cone, and one a side slope of
 * 161 off it is not. */
static void check_aim_far(void)
{
	fresh_world();
	place_target(7812, 0, 0, FAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(0), 1);
	XVT_ASSERT_INT_EQ(g_last_rough_distance, 7812 * FAR_SCALE);
	XVT_ASSERT_INT_EQ(g_target_angle_score, 0);
	/* At 7,812 forward a side offset of 4,913 is a slope of 161 and one of
	 * 4,882 a slope of 160, which the box of 9 does not take. */
	place_target(7812, 4913, 0, FAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(0), 0);
	XVT_ASSERT_INT_EQ(g_target_angle_score, NO_SCORE);
	place_target(7812, 4882, 0, FAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(0), 0);
	XVT_ASSERT_INT_EQ(g_target_angle_score, 160);
	/* An up offset of 3,000 at this distance is an up slope of 98. */
	place_target(7812, 0, 3000, FAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(0), 0);
	XVT_ASSERT_INT_EQ(g_target_angle_score, 59578 * 98 >> 16);
}

/* A craft's box is the mean of its model's three sizes shifted left by
 * bound_size_shift; any other object's is its type's max_bounds_extent. */
static void check_box_extent(void)
{
	fresh_world();
	g_model_defs[TARGET_MODEL].bound_size_x = 100;
	g_model_defs[TARGET_MODEL].bound_size_y = 200;
	g_model_defs[TARGET_MODEL].bound_size_z = 301;
	g_model_defs[TARGET_MODEL].bound_size_shift = 2;
	XVT_ASSERT_INT_EQ(targeting_get_object_box_extent(TARGET_SLOT),
			  (601 / 3) << 2);

	int saved_extent = g_object_type_table[PLAIN_TYPE].max_bounds_extent;
	g_object_type_table[PLAIN_TYPE].max_bounds_extent = 4321;
	g_test_objects[TARGET_SLOT].object_type = PLAIN_TYPE;
	g_test_mobiles[TARGET_SLOT].p_craft = NULL;
	XVT_ASSERT_INT_EQ(targeting_get_object_box_extent(TARGET_SLOT), 4321);
	g_test_objects[TARGET_SLOT].mobj = NULL;
	XVT_ASSERT_INT_EQ(targeting_get_object_box_extent(TARGET_SLOT), 4321);
	g_object_type_table[PLAIN_TYPE].max_bounds_extent = saved_extent;
}

/* Known failure aim_ahead_past_half_million, issue #197: an object dead ahead
 * is in the cone. Within a rough distance of 655,360 each offset is measured
 * at 1/16 in 16 bits, so one of 524,288 or more along an axis wraps: an
 * object 600,000 units dead ahead is measured behind the craft. The throwaway
 * fix moves the change to 1/256 down to 524,288, the first distance whose
 * offset no longer fits, so the near branch only sees offsets that fit; the
 * comment's 655,360 changes with it. */
static void check_aim_ahead_past_half_million(void)
{
	fresh_world();
	place_target(600000 / NEAR_SCALE, 0, 0, NEAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(0), 1);
}

/* Known failure aim_ahead_past_eight_million, no issue filed: an object dead
 * ahead is in the cone. Beyond a rough distance of 655,360 each offset is
 * measured at 1/256 in 16 bits, so one of 8,388,608 or more along an axis
 * wraps as the near branch's does: an object nine million units dead ahead
 * is measured behind the craft. The throwaway fix measures at 1/65536 from a
 * rough distance of 8,388,608, so every offset fits. */
static void check_aim_ahead_past_eight_million(void)
{
	fresh_world();
	place_target(35156, 0, 0, FAR_SCALE);
	XVT_ASSERT_INT_EQ(aim(0), 1);
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
			{"aim_ahead_past_half_million",
			 check_aim_ahead_past_half_million},
			{"aim_ahead_past_eight_million",
			 check_aim_ahead_past_eight_million},
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
	check_aim_without_craft();
	check_aim_dead_ahead();
	check_aim_not_ahead();
	check_aim_slope_limits();
	check_aim_score();
	check_aim_cone_bound();
	check_aim_narrow_close_in();
	check_aim_least_bound();
	check_aim_plain_object();
	check_aim_far();
	check_box_extent();
	return 0;
}
