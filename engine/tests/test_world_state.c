/* Checks the world-state image (xvt_runtime/snapshot/world_state.h) against the promises in its header,
 * on worlds this file builds itself: no game data is read. Each case starts from an empty world (no
 * object slots, no flight groups, the offline timing profile) and sets only what it needs. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/util/game_rand.h"
#include "xvt_runtime/snapshot/records.h"
#include "xvt_runtime/snapshot/world_state.h"

static uint8_t *g_image;
static size_t g_capacity;

/* The rich world's tables: 2 main slots and 1 static slot, 1 entry in each of the other pools. */
static struct object_record g_test_objects[3];
static struct mobile_object g_test_mobiles[2];
static struct craft_data g_test_craft[1];
static struct mobile_object_char_data g_test_char_data[1];
static struct warhead_guidance_state g_test_guidance[1];

/* Gives the world-state buffer room for the world as it is now. */
static void size_image(void)
{
	free(g_image);
	g_capacity = xvt_snapshot_calculate_size();
	g_image = calloc(1, g_capacity + 64);
	XVT_ASSERT_TRUE(g_image != NULL);
}

/* Clears the counts and tables the image depends on, then sizes a fresh image buffer for that world. */
static void empty_world(void)
{
	g_region_main_object_slot_end = 0;
	g_region_static_object_slot_count = 0;
	g_local_transient_slot_start = 0;
	g_local_debris_slot_end = 0;
	g_object_table = NULL;
	memset(&g_mission_header, 0, sizeof g_mission_header);
	g_craft_data_pool_capacity = 0;
	g_mobile_object_char_data_count = 0;
	g_projectile_object_slots_total = 0;
	memset(&g_flight_mission_state, 0, sizeof g_flight_mission_state);
	/* A player's slot must be a main slot or -1; with no slots, every player has none. */
	memset(g_players, 0, sizeof g_players);
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
	}
	size_image();
}

/* Slot 0 is a flying object with a craft, a character record and warhead guidance; slot 1 is empty but
 * owns the second mobile record; slot 2 is a static object. Player 0 sits in slot 0. */
static void rich_world(void)
{
	empty_world();
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_test_char_data, 0, sizeof g_test_char_data);
	memset(g_test_guidance, 0, sizeof g_test_guidance);
	g_object_table = g_test_objects;
	g_mobile_object_pool_base = g_test_mobiles;
	g_craft_data_pool_base = g_test_craft;
	g_mobile_object_char_data_pool = g_test_char_data;
	g_projectile_guidance_states = g_test_guidance;
	g_region_main_object_slot_end = 2;
	g_region_static_object_slot_count = 1;
	g_craft_data_pool_capacity = 1;
	g_mobile_object_char_data_count = 1;
	g_projectile_object_slots_total = 1;

	g_test_objects[0].object_type = 1;
	g_test_objects[0].object_signature = 0x0101;
	g_test_objects[0].mobj = &g_test_mobiles[0];
	g_test_mobiles[0].p_craft = &g_test_craft[0];
	g_test_mobiles[0].p_char_data = &g_test_char_data[0];
	g_test_mobiles[0].p_warhead_guidance = &g_test_guidance[0];
	g_test_mobiles[0].cached_fwd_x = 5;
	g_test_objects[1].mobj = &g_test_mobiles[1];
	g_test_objects[2].object_type = 2;
	g_test_objects[2].object_signature = 0x0202;
	g_players[0].object_index = 0;
	size_image();
}

/* Offset in the rich world's image of slot 0's object record and of its mobile record. */
static const size_t k_slot0_object = 1;
static const size_t k_slot0_mobile =
	1 + sizeof(struct xvt_snapshot_object_record);

static void check_calculate_size(void)
{
	empty_world();
	XVT_ASSERT_TRUE(g_capacity > 0);

	g_region_main_object_slot_end = -1;
	XVT_ASSERT_INT_EQ(xvt_snapshot_calculate_size(), 0);
	g_region_main_object_slot_end = 0;
	g_region_static_object_slot_count = 65536;
	XVT_ASSERT_INT_EQ(xvt_snapshot_calculate_size(), 0);
	g_region_static_object_slot_count = 0;
	g_mission_header.num_flight_groups = -1;
	XVT_ASSERT_INT_EQ(xvt_snapshot_calculate_size(), 0);
	g_mission_header.num_flight_groups = 0;
	g_craft_data_pool_capacity = 65536;
	XVT_ASSERT_INT_EQ(xvt_snapshot_calculate_size(), 0);
	g_craft_data_pool_capacity = 0;
	XVT_ASSERT_INT_EQ(xvt_snapshot_calculate_size(), g_capacity);
}

static void check_encode_refusals(void)
{
	empty_world();
	XVT_ASSERT_INT_EQ(xvt_snapshot_encode(NULL, g_capacity), 0);
	XVT_ASSERT_INT_EQ(xvt_snapshot_encode(g_image, g_capacity - 1), 0);
	g_region_main_object_slot_end = -1;
	XVT_ASSERT_INT_EQ(xvt_snapshot_encode(g_image, g_capacity), 0);
}

static void check_validate(void)
{
	empty_world();
	size_t written = xvt_snapshot_encode(g_image, g_capacity);
	XVT_ASSERT_TRUE(written > 0 && written <= g_capacity);
	XVT_ASSERT_INT_EQ(xvt_snapshot_validate(g_image, written), 1);

	/* The length must be exact, and a NULL image is refused. */
	XVT_ASSERT_INT_EQ(xvt_snapshot_validate(NULL, written), 0);
	XVT_ASSERT_INT_EQ(xvt_snapshot_validate(g_image, written - 1), 0);
	if (written < g_capacity) {
		XVT_ASSERT_INT_EQ(xvt_snapshot_validate(g_image, written + 1),
				  0);
	}

	/* An image is only accepted by a world with the pool sizes it was written with. */
	g_mobile_object_char_data_count = 1;
	XVT_ASSERT_INT_EQ(xvt_snapshot_validate(g_image, written), 0);
}

static void check_decode_round_trip(void)
{
	empty_world();
	g_next_object_signature = 0x1234;
	g_game_rand_feedback_state = 77;
	size_t written = xvt_snapshot_encode(g_image, g_capacity);
	XVT_ASSERT_TRUE(written > 0);

	g_next_object_signature = 0;
	g_game_rand_feedback_state = 0;
	XVT_ASSERT_INT_EQ(xvt_snapshot_decode(g_image, written), 1);
	XVT_ASSERT_INT_EQ(g_next_object_signature, 0x1234);
	XVT_ASSERT_INT_EQ(g_game_rand_feedback_state, 77);

	/* Encoding the restored world gives the same bytes back. */
	uint8_t *again = calloc(1, g_capacity);
	XVT_ASSERT_TRUE(again != NULL);
	XVT_ASSERT_INT_EQ(xvt_snapshot_encode(again, g_capacity), written);
	XVT_ASSERT_INT_EQ(memcmp(again, g_image, written), 0);
	free(again);
}

static void check_decode_refusal_leaves_world(void)
{
	empty_world();
	g_next_object_signature = 0x1234;
	size_t written = xvt_snapshot_encode(g_image, g_capacity);
	g_next_object_signature = 0x4321;
	XVT_ASSERT_INT_EQ(xvt_snapshot_decode(g_image, written - 1), 0);
	XVT_ASSERT_INT_EQ(g_next_object_signature, 0x4321);
}

static void check_checksum_image(void)
{
	empty_world();
	size_t written = xvt_snapshot_encode(g_image, g_capacity);
	unsigned sums[16];
	unsigned lengths[16];

	/* A refused image leaves both arrays as they were. */
	memset(sums, 0xAB, sizeof sums);
	memset(lengths, 0xAB, sizeof lengths);
	XVT_ASSERT_INT_EQ(xvt_snapshot_checksum_image(g_image, written - 1,
						      sums, lengths),
			  0);
	for (int i = 0; i < 16; ++i) {
		XVT_ASSERT_INT_EQ(sums[i], 0xABABABABu);
		XVT_ASSERT_INT_EQ(lengths[i], 0xABABABABu);
	}

	XVT_ASSERT_INT_EQ(
		xvt_snapshot_checksum_image(g_image, written, sums, lengths),
		1);
	size_t covered = 0;
	for (int i = 0; i < 16; ++i) {
		covered += lengths[i];
	}
	XVT_ASSERT_TRUE(covered > 0 && covered <= written);

	/* The sums are byte sums: one more in the image's first byte (the mission clock, which the
	 * validator does not judge) is one more in the first region, and no other region moves. */
	unsigned before[16];
	memcpy(before, sums, sizeof before);
	int delta = g_image[0] == 0xFF ? -1 : 1;
	g_image[0] = (uint8_t)(g_image[0] + delta);
	XVT_ASSERT_INT_EQ(
		xvt_snapshot_checksum_image(g_image, written, sums, lengths),
		1);
	XVT_ASSERT_INT_EQ(sums[0], before[0] + (unsigned)delta);
	for (int i = 1; i < 16; ++i) {
		XVT_ASSERT_INT_EQ(sums[i], before[i]);
	}
}

static void check_rich_round_trip(void)
{
	rich_world();
	size_t written = xvt_snapshot_encode(g_image, g_capacity);
	XVT_ASSERT_TRUE(written > 0 && written <= g_capacity);
	XVT_ASSERT_INT_EQ(xvt_snapshot_validate(g_image, written), 1);

	g_test_objects[0].object_signature = 0;
	g_test_objects[2].object_signature = 0;
	g_test_mobiles[0].p_craft = NULL;
	XVT_ASSERT_INT_EQ(xvt_snapshot_decode(g_image, written), 1);
	XVT_ASSERT_INT_EQ(g_test_objects[0].object_signature, 0x0101);
	XVT_ASSERT_INT_EQ(g_test_objects[2].object_signature, 0x0202);
	XVT_ASSERT_TRUE(g_test_objects[0].mobj == &g_test_mobiles[0]);
	XVT_ASSERT_TRUE(g_test_mobiles[0].p_craft == &g_test_craft[0]);
	XVT_ASSERT_TRUE(g_test_mobiles[0].p_char_data == &g_test_char_data[0]);
	XVT_ASSERT_TRUE(g_test_mobiles[0].p_warhead_guidance ==
			&g_test_guidance[0]);
	XVT_ASSERT_TRUE(g_test_objects[2].mobj == NULL);

	uint8_t *again = calloc(1, g_capacity);
	XVT_ASSERT_TRUE(again != NULL);
	XVT_ASSERT_INT_EQ(xvt_snapshot_encode(again, g_capacity), written);
	XVT_ASSERT_INT_EQ(memcmp(again, g_image, written), 0);
	free(again);
}

static void check_empty_slot_keeps_pool_link(void)
{
	rich_world();
	size_t written = xvt_snapshot_encode(g_image, g_capacity);
	g_test_objects[1].object_signature = 0x7777;
	XVT_ASSERT_INT_EQ(xvt_snapshot_decode(g_image, written), 1);
	XVT_ASSERT_INT_EQ(g_test_objects[1].object_type, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[1].object_signature, 0);
	XVT_ASSERT_TRUE(g_test_objects[1].mobj == &g_test_mobiles[1]);
}

static void check_validate_refuses_bad_records(void)
{
	rich_world();
	size_t written = xvt_snapshot_encode(g_image, g_capacity);
	uint8_t *bad = malloc(written);
	XVT_ASSERT_TRUE(bad != NULL);
	size_t mobj = k_slot0_object +
		      offsetof(struct xvt_snapshot_object_record, mobj);
	uint32_t link;

	/* The type byte must equal the record's type. */
	memcpy(bad, g_image, written);
	bad[0] = 3;
	XVT_ASSERT_INT_EQ(xvt_snapshot_validate(bad, written), 0);

	/* A pool link must land on a whole record... */
	memcpy(bad, g_image, written);
	link = 2;
	memcpy(bad + mobj, &link, sizeof link);
	XVT_ASSERT_INT_EQ(xvt_snapshot_validate(bad, written), 0);

	/* ...inside the pool: the mobile pool has one record per main slot. */
	memcpy(bad, g_image, written);
	link = 2 * sizeof(struct xvt_snapshot_mobile_object) + 1;
	memcpy(bad + mobj, &link, sizeof link);
	XVT_ASSERT_INT_EQ(xvt_snapshot_validate(bad, written), 0);

	/* The second mobile record is inside the pool; a link to it is accepted. */
	memcpy(bad, g_image, written);
	link = sizeof(struct xvt_snapshot_mobile_object) + 1;
	memcpy(bad + mobj, &link, sizeof link);
	XVT_ASSERT_INT_EQ(xvt_snapshot_validate(bad, written), 1);

	/* A player's slot must be a main slot. */
	memcpy(bad, g_image, written);
	int32_t slot = 2;
	memcpy(bad + written - 8 * sizeof(struct xvt_snapshot_player_data) +
		       offsetof(struct xvt_snapshot_player_data, object_index),
	       &slot, sizeof slot);
	XVT_ASSERT_INT_EQ(xvt_snapshot_validate(bad, written), 0);
	free(bad);
}

static void check_checksum_skips_links(void)
{
	rich_world();
	size_t written = xvt_snapshot_encode(g_image, g_capacity);
	unsigned sums[16];
	unsigned lengths[16];
	unsigned before[16];
	XVT_ASSERT_INT_EQ(
		xvt_snapshot_checksum_image(g_image, written, before, lengths),
		1);

	/* The mobile record's cached motion is not summed: changing it leaves every sum as it was. */
	int16_t fwd = 1234;
	memcpy(g_image + k_slot0_mobile +
		       offsetof(struct xvt_snapshot_mobile_object,
				cached_fwd_x),
	       &fwd, sizeof fwd);
	XVT_ASSERT_INT_EQ(
		xvt_snapshot_checksum_image(g_image, written, sums, lengths),
		1);
	XVT_ASSERT_INT_EQ(memcmp(sums, before, sizeof sums), 0);

	/* The object's signature is summed: one more in its low byte is one more in the first region. */
	size_t signature =
		k_slot0_object +
		offsetof(struct xvt_snapshot_object_record, object_signature);
	XVT_ASSERT_INT_EQ(g_image[signature], 0x01);
	g_image[signature] = 0x02;
	XVT_ASSERT_INT_EQ(
		xvt_snapshot_checksum_image(g_image, written, sums, lengths),
		1);
	XVT_ASSERT_INT_EQ(sums[0], before[0] + 1);
}

static void check_presence_map(void)
{
	rich_world();
	XVT_ASSERT_TRUE(xvt_snapshot_encode(g_image, g_capacity) > 0);
	uint8_t map[16];
	memset(map, 0xEE, sizeof map);
	/* A slot count, then slot 0's five components, a run of one empty slot, and slot 2's object alone. */
	XVT_ASSERT_INT_EQ(xvt_snapshot_build_presence_map(map, g_image),
			  (int)sizeof(int) + 3);
	int count;
	memcpy(&count, map, sizeof count);
	XVT_ASSERT_INT_EQ(count, 3);
	XVT_ASSERT_INT_EQ(map[sizeof(int)], 0x1F);
	XVT_ASSERT_INT_EQ(map[sizeof(int) + 1], 0x81);
	XVT_ASSERT_INT_EQ(map[sizeof(int) + 2], 0x01);
	XVT_ASSERT_INT_EQ(map[sizeof(int) + 3], 0xEE);
}

static void check_live_checksum(void)
{
	empty_world();
	g_next_object_signature = 1;
	int base = xvt_snapshot_live_checksum();

	/* Left out by the header: the laser-fire flag and the player count. */
	g_laser_fire_timestamp_tracking_enabled =
		!g_laser_fire_timestamp_tracking_enabled;
	g_flight_player_count += 3;
	XVT_ASSERT_INT_EQ(xvt_snapshot_live_checksum(), base);

	/* Covered: the next object signature, which the image also carries. */
	g_next_object_signature = 2;
	XVT_ASSERT_TRUE(xvt_snapshot_live_checksum() != base);
}

/* The big world: 8 main slots holding flying objects, the first `crafts` of them with a craft record, then
 * `statics` static slots, so the object section is longer than one checksum region; and `flight_groups`
 * flight groups. */
static struct object_record g_big_objects[24];
static struct mobile_object g_big_mobiles[8];
static struct craft_data g_big_craft[8];

static void big_world(int flight_groups, int crafts, int statics)
{
	empty_world();
	memset(g_big_objects, 0, sizeof g_big_objects);
	memset(g_big_mobiles, 0, sizeof g_big_mobiles);
	memset(g_big_craft, 0, sizeof g_big_craft);
	g_object_table = g_big_objects;
	g_mobile_object_pool_base = g_big_mobiles;
	g_craft_data_pool_base = g_big_craft;
	g_region_main_object_slot_end = 8;
	g_region_static_object_slot_count = statics;
	g_craft_data_pool_capacity = 8;
	for (int i = 0; i < 8 + statics; ++i) {
		g_big_objects[i].object_type = (uint8_t)(1 + i % 3);
		g_big_objects[i].object_signature = (uint16_t)(0x100 + i);
		g_big_objects[i].world_x = 1000 * i;
	}
	for (int i = 0; i < 8; ++i) {
		g_big_objects[i].mobj = &g_big_mobiles[i];
		g_big_mobiles[i].p_craft = i < crafts ? &g_big_craft[i] : NULL;
		g_big_mobiles[i].speed = (int16_t)(10 + i);
	}
	g_mission_header.num_flight_groups = (int16_t)flight_groups;
	for (int i = 0; i < flight_groups; ++i) {
		memset(&g_mission_fg_stats[i], 0x11 + i,
		       sizeof g_mission_fg_stats[i]);
		memset(&g_mission_flight_groups[i], 0x31 + i,
		       sizeof g_mission_flight_groups[i]);
	}
	g_players[0].object_index = 0;
	size_image();
}

/* Region sums and lengths of three big worlds, recorded from the code at fork commit 03e9d80. They pin
 * where today's regions close: inside the object section, after the flight-group tables, after the fixed
 * trailer tables, and (in the third world, whose 3,400-byte region shows it) right after the two short
 * tables that follow the 3,376-byte one. A change to the image layout or to the region rule must update
 * them. */
static const unsigned k_big4_sums[16] = {0x000001ef, 0x0000024c, 0x000056ee,
					 0x0004427c, 0x00000015, 0x00001c34};
static const unsigned k_big4_lengths[16] = {4005, 4005,	 4096,
					    5528, 25509, 11760};
static const unsigned k_big12_sums[16] = {0x000002c4, 0x000002a4, 0x000f0175,
					  0x00000015, 0x00001c34};
static const unsigned k_big_small_lengths[16] = {4005, 4005,  8790,
						 3400, 22109, 11760};
static const unsigned k_big_small_sums[16] = {
	0x000001ef, 0x0000024c, 0x0004a070, 0x00000000, 0x0000001d, 0x00001c34};
static const unsigned k_big12_lengths[16] = {5340, 5340, 20362, 25509, 11760};

static void check_checksum_regions_in_big_worlds(void)
{
	static const struct {
		int flight_groups, crafts, statics;
		const unsigned *sums;
		const unsigned *lengths;
	} cases[] = {{4, 8, 2, k_big4_sums, k_big4_lengths},
		     {12, 8, 2, k_big12_sums, k_big12_lengths},
		     {4, 7, 10, k_big_small_sums, k_big_small_lengths}};

	for (size_t c = 0; c < sizeof cases / sizeof cases[0]; ++c) {
		big_world(cases[c].flight_groups, cases[c].crafts,
			  cases[c].statics);
		size_t written = xvt_snapshot_encode(g_image, g_capacity);
		XVT_ASSERT_TRUE(written > 0);
		unsigned sums[16];
		unsigned lengths[16];
		XVT_ASSERT_INT_EQ(xvt_snapshot_checksum_image(g_image, written,
							      sums, lengths),
				  1);
		/* More than one region closes, and the regions tile the world part from its start. */
		size_t covered = 0;
		int used = 0;
		for (int i = 0; i < 16; ++i) {
			covered += lengths[i];
			used += lengths[i] != 0;
		}
		XVT_ASSERT_TRUE(used > 2 && covered <= written);
		for (int i = 0; i < 16; ++i) {
			XVT_ASSERT_INT_EQ(sums[i], cases[c].sums[i]);
			XVT_ASSERT_INT_EQ(lengths[i], cases[c].lengths[i]);
		}
	}
	g_mission_header.num_flight_groups = 0;
}

static void check_validate_refuses_other_ranges(void)
{
	rich_world();
	size_t written = xvt_snapshot_encode(g_image, g_capacity);
	XVT_ASSERT_INT_EQ(xvt_snapshot_validate(g_image, written), 1);

	/* The slot-range bounds and the reserved dword must equal the live ones. */
	g_debris_object_slot_start += 1;
	XVT_ASSERT_INT_EQ(xvt_snapshot_validate(g_image, written), 0);
	g_debris_object_slot_start -= 1;
	g_world_state_debris_slot_count += 1;
	XVT_ASSERT_INT_EQ(xvt_snapshot_validate(g_image, written), 0);
	g_world_state_debris_slot_count -= 1;
	g_explosion_object_slot_end += 1;
	XVT_ASSERT_INT_EQ(xvt_snapshot_validate(g_image, written), 0);
	g_explosion_object_slot_end -= 1;
	XVT_ASSERT_INT_EQ(xvt_snapshot_validate(g_image, written), 1);
}

/* Gives every value the live checksum mixes in without walking a pool a distinct nonzero value (seed 1),
 * or zero (seed 0), so a dropped, repeated or reordered field changes the result. */
static void set_mixed_only_values(int seed)
{
	int v = seed ? 0x31 : 0;
	g_world_state_reserved_byte = (uint8_t)v;
	g_world_state_debris_slot_count = v + 1;
	g_active_region_object_slot_start = v + 2;
	g_active_region_craft_object_slot_end = v + 3;
	g_mobile_object_char_data_slot_start = v + 4;
	g_mobile_object_char_data_slot_end = v + 5;
	g_projectile_object_slot_start = v + 6;
	g_projectile_object_slot_end = v + 7;
	g_debris_object_slot_start = v + 8;
	g_debris_object_slot_end = v + 9;
	g_explosion_object_slot_start = v + 10;
	g_explosion_object_slot_end = v + 11;
	g_debris_object_slots_total = (unsigned)(v + 12);
	g_plan_count = v + 13;
	g_unused_world_state_serialized_dword = v + 14;
	g_game_rand_feedback_state = (int16_t)(v + 15);
	g_flight_conf_new_net = v + 16;
	g_active_flight_player_count = v + 17;
	g_mission_file_version = (uint16_t)(v + 18);
}

/* The live checksum of the rich world with 3 flight groups and player 0 connected, recorded from the
 * code at fork commit 03e9d80, with set_mixed_only_values(1). Every pool loop, both flight-group loops and
 * the player loop run. */
static const unsigned k_rich_live_checksum = 0xdb657f64u;

static void check_live_checksum_of_rich_world(void)
{
	rich_world();
	g_mission_header.num_flight_groups = 3;
	for (int i = 0; i < 3; ++i) {
		memset(&g_mission_fg_stats[i], 0x21 + i,
		       sizeof g_mission_fg_stats[i]);
		memset(&g_mission_flight_groups[i], 0x41 + i,
		       sizeof g_mission_flight_groups[i]);
	}
	g_players[0].participation_state = 1;
	g_next_object_signature = 9;
	/* Every pool record carries a nonzero field, so dropping any pool's loop changes the result. */
	g_test_craft[0].craft_index_in_group = 3;
	g_test_char_data[0].skill_value = 4;
	g_test_guidance[0].cruise_speed = 5;
	g_test_mobiles[0].speed = 6;
	g_test_objects[2].world_x = 7;
	set_mixed_only_values(1);
	int live = xvt_snapshot_live_checksum();
	XVT_ASSERT_INT_EQ((unsigned)live, k_rich_live_checksum);

	/* Covered: a pool entry's record, a flight group's stats, a connected player. */
	g_test_objects[0].world_x += 1;
	XVT_ASSERT_TRUE(xvt_snapshot_live_checksum() != live);
	g_test_objects[0].world_x -= 1;
	g_mission_fg_stats[2].spawned_craft_count += 1;
	XVT_ASSERT_TRUE(xvt_snapshot_live_checksum() != live);
	g_mission_fg_stats[2].spawned_craft_count -= 1;
	XVT_ASSERT_INT_EQ(xvt_snapshot_live_checksum(), live);
	g_players[0].participation_state = 0;
	XVT_ASSERT_TRUE(xvt_snapshot_live_checksum() != live);
	set_mixed_only_values(0);
	g_mission_header.num_flight_groups = 0;
}

/* Block sizes in an image, and the rich world's layout: slot 0 holds all five blocks, slot 1 is empty,
 * slot 2 holds an object alone. */
#define OBJ_SIZE sizeof(struct xvt_snapshot_object_record)
#define MOB_SIZE sizeof(struct xvt_snapshot_mobile_object)
#define CRAFT_SIZE sizeof(struct xvt_snapshot_craft_data)
#define GUIDE_SIZE sizeof(struct warhead_guidance_state)
#define CHAR_SIZE sizeof(struct xvt_snapshot_mobile_object_char_data)

static uint8_t *g_dup;

/* Writes a 3-slot presence map: the slot count, then one flag byte per slot. */
static void map3(uint8_t map[7], int count, uint8_t slot0, uint8_t slot1,
		 uint8_t slot2)
{
	memcpy(map, &count, sizeof count);
	map[4] = slot0;
	map[5] = slot1;
	map[6] = slot2;
}

/* Copies the first `written` bytes of g_image into the duplicate buffer, with room to grow, applies map,
 * and returns the new size. */
static size_t apply(const uint8_t *map, size_t written)
{
	free(g_dup);
	g_dup = calloc(1, written + 4096);
	XVT_ASSERT_TRUE(g_dup != NULL);
	memcpy(g_dup, g_image, written);
	g_world_state_dup_buffer = g_dup;
	g_world_state_dup_size = (int)written;
	xvt_snapshot_apply_presence_map(map);
	return (size_t)g_world_state_dup_size;
}

/* True when the duplicate buffer is g_image with `length` bytes at `at` removed. */
static int removed(size_t written, size_t at, size_t length)
{
	return (size_t)g_world_state_dup_size == written - length &&
	       memcmp(g_dup, g_image, at) == 0 &&
	       memcmp(g_dup + at, g_image + at + length,
		      written - at - length) == 0;
}

/* True when the duplicate buffer is g_image with `length` zero bytes inserted at `at`. */
static int inserted(size_t written, size_t at, size_t length)
{
	for (size_t i = 0; i < length; ++i) {
		if (g_dup[at + i] != 0) {
			return 0;
		}
	}
	return (size_t)g_world_state_dup_size == written + length &&
	       memcmp(g_dup, g_image, at) == 0 &&
	       memcmp(g_dup + at + length, g_image + at, written - at) == 0;
}

static void check_apply_presence_map_keeps_matching_image(void)
{
	rich_world();
	size_t written = xvt_snapshot_encode(g_image, g_capacity);
	uint8_t map[16];
	xvt_snapshot_build_presence_map(map, g_image);
	XVT_ASSERT_INT_EQ(apply(map, written), written);
	XVT_ASSERT_INT_EQ(memcmp(g_dup, g_image, written), 0);

	/* With slot 0 emptied too, the map packs slots 0 and 1 as one run of two before slot 2's object. */
	g_test_objects[0].object_type = 0;
	g_players[0].object_index = -1;
	size_image();
	written = xvt_snapshot_encode(g_image, g_capacity);
	XVT_ASSERT_INT_EQ(xvt_snapshot_build_presence_map(map, g_image),
			  (int)sizeof(int) + 2);
	XVT_ASSERT_INT_EQ(map[sizeof(int)], 0x82);
	XVT_ASSERT_INT_EQ(map[sizeof(int) + 1], 0x01);
	XVT_ASSERT_INT_EQ(apply(map, written), written);
	XVT_ASSERT_INT_EQ(memcmp(g_dup, g_image, written), 0);
}

static void check_apply_presence_map_removes_blocks(void)
{
	rich_world();
	size_t written = xvt_snapshot_encode(g_image, g_capacity);
	const size_t craft = k_slot0_mobile + MOB_SIZE;
	uint8_t map[7];

	map3(map, 3, 0x1F & ~0x04, 0, 0x01);
	apply(map, written);
	XVT_ASSERT_TRUE(removed(written, craft, CRAFT_SIZE));
	map3(map, 3, 0x1F & ~0x08, 0, 0x01);
	apply(map, written);
	XVT_ASSERT_TRUE(removed(written, craft + CRAFT_SIZE, GUIDE_SIZE));
	map3(map, 3, 0x1F & ~0x10, 0, 0x01);
	apply(map, written);
	XVT_ASSERT_TRUE(
		removed(written, craft + CRAFT_SIZE + GUIDE_SIZE, CHAR_SIZE));

	/* Removing an object or mobile record leaves the blocks nested under it. The walk then reads the
	 * next slot inside those blocks, so these maps cover slot 0 alone. */
	map3(map, 1, 0x01, 0, 0x00);
	apply(map, written);
	XVT_ASSERT_TRUE(removed(written, k_slot0_mobile, MOB_SIZE));
	map3(map, 1, 0x00, 0, 0x00);
	apply(map, written);
	XVT_ASSERT_TRUE(removed(written, k_slot0_object, OBJ_SIZE));

	/* Slots from the map's slot count onward are left alone: slot 2 keeps its object. */
	map3(map, 1, 0x1F & ~0x04, 0, 0x00);
	apply(map, written);
	XVT_ASSERT_TRUE(removed(written, craft, CRAFT_SIZE));
}

static void check_apply_presence_map_inserts_zeroed_blocks(void)
{
	rich_world();
	g_test_mobiles[0].p_craft = NULL;
	g_test_mobiles[0].p_char_data = NULL;
	g_test_mobiles[0].p_warhead_guidance = NULL;
	size_t written = xvt_snapshot_encode(g_image, g_capacity);
	const size_t slot1 = k_slot0_mobile + MOB_SIZE;
	uint8_t map[7];

	/* Slot 0's mobile record gains a craft, guidance and character block, in image order. */
	map3(map, 3, 0x1F, 0, 0x01);
	apply(map, written);
	XVT_ASSERT_TRUE(
		inserted(written, slot1, CRAFT_SIZE + GUIDE_SIZE + CHAR_SIZE));

	/* Inserting a record adds that record alone: slot 2 gains a mobile record, slot 1 an object record
	 * (after its type byte, which stays 0). */
	map3(map, 3, 0x03, 0, 0x03);
	apply(map, written);
	XVT_ASSERT_TRUE(inserted(written, slot1 + 1 + 1 + OBJ_SIZE, MOB_SIZE));
	map3(map, 3, 0x03, 0x01, 0x01);
	apply(map, written);
	XVT_ASSERT_TRUE(inserted(written, slot1 + 1, OBJ_SIZE));
}

int main(void)
{
	check_calculate_size();
	check_encode_refusals();
	check_validate();
	check_decode_round_trip();
	check_decode_refusal_leaves_world();
	check_checksum_image();
	check_rich_round_trip();
	check_empty_slot_keeps_pool_link();
	check_validate_refuses_bad_records();
	check_checksum_skips_links();
	check_presence_map();
	check_live_checksum();
	check_checksum_regions_in_big_worlds();
	check_validate_refuses_other_ranges();
	check_live_checksum_of_rich_world();
	check_apply_presence_map_keeps_matching_image();
	check_apply_presence_map_removes_blocks();
	check_apply_presence_map_inserts_zeroed_blocks();
	free(g_dup);
	free(g_image);
	return 0;
}
