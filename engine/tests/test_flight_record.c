/* Tests for xvt_runtime/runtime/flight_record.c, the record point and the
 * warhead track. Checks the lines a record point writes, word for word, on
 * craft this file builds itself: one record.point line, then one world.track
 * line per craft slot holding a craft with a craft record, in slot order; a
 * slot in use without a craft record counted and not tracked; free slots and
 * slots past the craft slots left out; a player's target read only for a
 * player number in the player table; and nothing written with DEBUG lines
 * off. Also the warhead track on shots this file builds: one
 * world.warhead_track line per shot slot holding a warhead (a countermeasure
 * included, a laser bolt, a free slot, a type outside the shot types and a
 * slot outside the shot slots left out); a warhead without a guidance record;
 * which steps of the 16-tick warhead beat and the 472-tick record point beat
 * write lines (xvt_flight_record_warhead_beat and xvt_flight_record_step); and
 * nothing written with DEBUG off. The lines are caught at SDL's log output,
 * where Aeron's log funnel sends them. The shot type table is the engine's
 * own; no game data is read. */
#include <SDL3/SDL_log.h>
#include <stdio.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/flight_record.h"

enum {
	SLOTS = 8,
	CRAFT_SLOTS = 6, /* Slots 0 to 5 are craft slots; 6 and 7 are not. */
	TICK = 960,
	MAX_LINES = 16,
	LINE_SIZE = 512,
};

static struct object_record g_test_objects[SLOTS];
static struct mobile_object g_test_mobiles[SLOTS];
static struct craft_data g_test_craft[SLOTS];

static char g_lines[MAX_LINES][LINE_SIZE];
static int g_line_count;

/* Keeps each line the engine writes, without Aeron's "xvt: " category
 * prefix; a line without it is kept whole, so a check on it fails. */
static void catch_line(void *userdata, int category, SDL_LogPriority priority,
		       const char *message)
{
	(void)userdata;
	(void)category;
	(void)priority;
	static const char prefix[] = "xvt: ";
	if (g_line_count >= MAX_LINES) {
		fprintf(stderr, "more than %d lines written\n", MAX_LINES);
		exit(1);
	}
	if (!strncmp(message, prefix, sizeof prefix - 1)) {
		message += sizeof prefix - 1;
	}
	snprintf(g_lines[g_line_count++], LINE_SIZE, "%s", message);
}

static void check_line(int index, const char *expected)
{
	XVT_ASSERT_TRUE(index < g_line_count);
	if (strcmp(g_lines[index], expected) != 0) {
		fprintf(stderr, "line %d:\n  got  %s\n  want %s\n", index,
			g_lines[index], expected);
		exit(1);
	}
}

/* What one craft carries into its world.track line. */
struct craft_values {
	uint16_t signature;
	uint8_t type;
	uint8_t fg;
	int player;
	uint8_t team;
	int x, y, z;
	uint16_t yaw, pitch, roll;
	uint16_t speed;
	uint16_t throttle;
	craft_object_kind state;
	unsigned hull, hull_max;
	int front, rear;
	uint16_t ai_target;
};

static void place_craft(int slot, const struct craft_values *v)
{
	struct object_record *object = &g_test_objects[slot];
	struct mobile_object *mobj = &g_test_mobiles[slot];
	struct craft_data *craft = &g_test_craft[slot];
	object->object_signature = v->signature;
	object->object_type = v->type;
	object->flight_group_idx = v->fg;
	object->player_owner_idx = v->player;
	object->world_x = v->x;
	object->world_y = v->y;
	object->world_z = v->z;
	object->yaw = v->yaw;
	object->pitch = v->pitch;
	object->roll = v->roll;
	object->mobj = mobj;
	mobj->team = v->team;
	mobj->speed = v->speed;
	mobj->p_craft = craft;
	craft->throttle_speed = v->throttle;
	craft->object_kind = v->state;
	craft->hull_damage = v->hull;
	craft->hull_max = v->hull_max;
	craft->shield_energy[0] = v->front;
	craft->shield_energy[1] = v->rear;
	craft->ai_controller.target_obj_idx = v->ai_target;
}

/* Slots 0 to 3: craft flown by player 0, player 7, a player number past the
 * table and no player. Slot 4: in use with no craft record. Slot 5: free,
 * still pointing at a craft record. Slot 6: a craft past the craft slots.
 * Players 0 and 7 have targeted objects 6 and 4; player 8's entry does not
 * exist. */
static void build_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	for (int slot = 0; slot < SLOTS; ++slot) {
		g_test_objects[slot].player_owner_idx = -1;
		g_test_objects[slot].mobj = &g_test_mobiles[slot];
	}
	g_object_table = g_test_objects;
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = CRAFT_SLOTS;
	memset(g_players, 0, sizeof g_players);
	g_players[0].current_target_object_idx = 6;
	g_players[7].current_target_object_idx = 4;

	static const struct craft_values player0 = {
		.signature = 4660,
		.type = 7,
		.fg = 5,
		.player = 0,
		.team = 1,
		.x = -123456,
		.y = 654321,
		.z = -65536,
		.yaw = 16384,
		.pitch = 49152,
		.roll = 1,
		.speed = 1250,
		.throttle = 32768,
		.state = 3,
		.hull = 40,
		.hull_max = 200,
		.front = 300,
		.rear = 150,
		.ai_target = 65535,
	};
	static const struct craft_values player7 = {
		.signature = 11,
		.type = 12,
		.fg = 9,
		.player = 7,
		.team = 4,
		.x = 1,
		.y = 2,
		.z = 3,
		.yaw = 65535,
		.pitch = 0,
		.roll = 32768,
		.speed = 3600,
		.throttle = 65535,
		.state = 0,
		.hull = 0,
		.hull_max = 500,
		.front = 1000,
		.rear = 999,
		.ai_target = 32769,
	};
	static const struct craft_values past_table = {
		.signature = 21,
		.type = 13,
		.fg = 2,
		.player = 8,
		.team = 2,
		.x = 70,
		.y = 80,
		.z = 90,
		.yaw = 100,
		.pitch = 110,
		.roll = 120,
		.speed = 130,
		.throttle = 140,
		.state = 6,
		.hull = 150,
		.hull_max = 160,
		.front = 170,
		.rear = 180,
		.ai_target = 190,
	};
	static const struct craft_values no_player = {
		.signature = 31,
		.type = 14,
		.fg = 3,
		.player = -1,
		.team = 0,
		.x = -7,
		.y = -8,
		.z = -9,
		.yaw = 10,
		.pitch = 20,
		.roll = 30,
		.speed = 40,
		.throttle = 50,
		.state = 4,
		.hull = 60,
		.hull_max = 70,
		.front = -80,
		.rear = 90,
		.ai_target = 2,
	};
	place_craft(0, &player0);
	place_craft(1, &player7);
	place_craft(2, &past_table);
	place_craft(3, &no_player);
	g_test_objects[4].object_type = 9;
	g_test_mobiles[5].p_craft = &g_test_craft[5];
	place_craft(6, &player0);
}

static void check_writes_nothing_with_debug_off(void)
{
	build_world();
	g_line_count = 0;
	xvt_log_set_level(AERON_LOG_INFO);
	xvt_flight_record_point(TICK);
	XVT_ASSERT_INT_EQ(g_line_count, 0);
}

static void check_record_point_lines(void)
{
	build_world();
	g_line_count = 0;
	xvt_log_set_level(AERON_LOG_DEBUG);
	xvt_flight_record_point(TICK);
	XVT_ASSERT_INT_EQ(g_line_count, 5);
	check_line(0, "record.point tick=960 craft=4 unlinked=1");
	check_line(1, "world.track tick=960 object=0 signature=4660 type=7 "
		      "fg=5 player=0 team=1 x=-123456 y=654321 z=-65536 "
		      "yaw=16384 pitch=49152 roll=1 speed=1250 throttle=32768 "
		      "state=3 hull=40 hull_max=200 front=300 rear=150 "
		      "target=6 ai_target=65535");
	check_line(2, "world.track tick=960 object=1 signature=11 type=12 "
		      "fg=9 player=7 team=4 x=1 y=2 z=3 yaw=65535 pitch=0 "
		      "roll=32768 speed=3600 throttle=65535 state=0 hull=0 "
		      "hull_max=500 front=1000 rear=999 target=4 "
		      "ai_target=32769");
	check_line(3, "world.track tick=960 object=2 signature=21 type=13 "
		      "fg=2 player=8 team=2 x=70 y=80 z=90 yaw=100 pitch=110 "
		      "roll=120 speed=130 throttle=140 state=6 hull=150 "
		      "hull_max=160 front=170 rear=180 target=-1 "
		      "ai_target=190");
	check_line(4, "world.track tick=960 object=3 signature=31 type=14 "
		      "fg=3 player=-1 team=0 x=-7 y=-8 z=-9 yaw=10 pitch=20 "
		      "roll=30 speed=40 throttle=50 state=4 hull=60 "
		      "hull_max=70 front=-80 rear=90 target=-1 ai_target=2");
}

/* With every craft slot free nothing is tracked, and the record.point line
 * still marks the tick. */
static void check_empty_world(void)
{
	build_world();
	for (int slot = 0; slot < SLOTS; ++slot) {
		g_test_objects[slot].object_type = 0;
	}
	g_line_count = 0;
	xvt_log_set_level(AERON_LOG_DEBUG);
	xvt_flight_record_point(TICK);
	XVT_ASSERT_INT_EQ(g_line_count, 1);
	check_line(0, "record.point tick=960 craft=0 unlinked=0");
}

static struct warhead_guidance_state g_test_guidance[SLOTS];

/* Shot slots 2 to 6 (one past the end is 7). Slot 2: a proton torpedo (143)
 * aimed at object 5 with homing tier 4. Slot 3: a laser bolt (137). Slot 4:
 * free. Slot 5: a countermeasure (155) with no guidance record. Slot 6: a
 * type past the shot types (200). Slot 0, before the shot slots, and slot 7,
 * after them, hold concussion missiles (144); slot 1 holds type 5, below the
 * shot types. */
static void build_shots(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_guidance, 0, sizeof g_test_guidance);
	for (int slot = 0; slot < SLOTS; ++slot) {
		g_test_objects[slot].mobj = &g_test_mobiles[slot];
	}
	g_object_table = g_test_objects;
	g_projectile_object_slot_start = 2;
	g_projectile_object_slot_end = 7;
	static const unsigned char types[SLOTS] = {144, 5,   143, 137,
						   0,	155, 200, 144};
	for (int slot = 0; slot < SLOTS; ++slot) {
		g_test_objects[slot].object_type = types[slot];
		g_test_objects[slot].object_signature = (uint16_t)(100 + slot);
		g_test_objects[slot].world_x = slot;
	}
	struct object_record *torpedo = &g_test_objects[2];
	torpedo->world_x = -50000;
	torpedo->world_y = 123456;
	torpedo->world_z = -7;
	torpedo->yaw = 65535;
	torpedo->pitch = 16384;
	torpedo->roll = 2;
	g_test_mobiles[2].speed = 2750;
	g_test_mobiles[2].source_obj_idx = 1;
	g_test_mobiles[2].p_warhead_guidance = &g_test_guidance[2];
	g_test_guidance[2].target_obj_idx = 5;
	g_test_guidance[2].homing_tier = 4;
	g_test_mobiles[5].source_obj_idx = 3;
	g_test_mobiles[5].speed = 300;
	g_test_objects[5].world_x = 9;
	g_test_objects[5].world_y = 8;
	g_test_objects[5].world_z = 7;
}

static void check_warheads_off(void)
{
	build_shots();
	g_line_count = 0;
	xvt_log_set_level(AERON_LOG_INFO);
	xvt_flight_record_warheads(TICK);
	xvt_flight_record_warhead_beat(8, 16);
	xvt_flight_record_step(0, 472);
	XVT_ASSERT_INT_EQ(g_line_count, 0);
}

static void check_warhead_lines(void)
{
	build_shots();
	g_line_count = 0;
	xvt_log_set_level(AERON_LOG_DEBUG);
	xvt_flight_record_warheads(TICK);
	XVT_ASSERT_INT_EQ(g_line_count, 2);
	check_line(0, "world.warhead_track tick=960 object=2 signature=102 "
		      "type=143 source=1 target=5 homing=4 x=-50000 "
		      "y=123456 z=-7 yaw=65535 pitch=16384 roll=2 "
		      "speed=2750");
	check_line(1, "world.warhead_track tick=960 object=5 signature=105 "
		      "type=155 source=3 target=-1 homing=-1 x=9 y=8 z=7 "
		      "yaw=0 pitch=0 roll=0 speed=300");
}

/* A beat is crossed or landed on: 8 to 16 and 10 to 17 write, 16 to 24 and
 * 1-tick steps within a beat do not, 15 to 16 writes, 0 to 8 does not. */
static void check_warhead_beat(void)
{
	static const int steps[][2] = {{8, 16},	 {16, 24}, {10, 17},
				       {15, 16}, {16, 17}, {0, 8},
				       {17, 16}, {-8, 8},  {0, 40}};
	static const int writes[] = {1, 0, 1, 1, 0, 0, 0, 0, 1};
	for (unsigned i = 0; i < sizeof writes / sizeof writes[0]; ++i) {
		build_shots();
		g_line_count = 0;
		xvt_log_set_level(AERON_LOG_DEBUG);
		xvt_flight_record_warhead_beat(steps[i][0], steps[i][1]);
		if (g_line_count != 2 * writes[i]) {
			fprintf(stderr, "step %d to %d wrote %d lines\n",
				steps[i][0], steps[i][1], g_line_count);
			exit(1);
		}
	}
	build_shots();
	g_line_count = 0;
	xvt_flight_record_warhead_beat(8, 16);
	check_line(0, "world.warhead_track tick=16 object=2 signature=102 "
		      "type=143 source=1 target=5 homing=4 x=-50000 "
		      "y=123456 z=-7 yaw=65535 pitch=16384 roll=2 "
		      "speed=2750");
}

/* The step record: warheads on the 16 beat, the record point (with the craft
 * world of build_world) on the 472 beat. */
static void check_step(void)
{
	build_world();
	g_projectile_object_slot_start = 2;
	g_projectile_object_slot_end = 2;
	xvt_log_set_level(AERON_LOG_DEBUG);
	g_line_count = 0;
	xvt_flight_record_step(470, 478);
	XVT_ASSERT_INT_EQ(g_line_count, 5);
	check_line(0, "record.point tick=478 craft=4 unlinked=1");
	g_line_count = 0;
	xvt_flight_record_step(471, 472);
	XVT_ASSERT_INT_EQ(g_line_count, 5);
	g_line_count = 0;
	xvt_flight_record_step(472, 473);
	XVT_ASSERT_INT_EQ(g_line_count, 0);
	g_line_count = 0;
	xvt_flight_record_step(0, 8);
	XVT_ASSERT_INT_EQ(g_line_count, 0);
	build_shots();
	g_line_count = 0;
	xvt_flight_record_step(8, 16);
	XVT_ASSERT_INT_EQ(g_line_count, 2);
	check_line(0, "world.warhead_track tick=16 object=2 signature=102 "
		      "type=143 source=1 target=5 homing=4 x=-50000 "
		      "y=123456 z=-7 yaw=65535 pitch=16384 roll=2 "
		      "speed=2750");
}

int main(void)
{
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	SDL_SetLogOutputFunction(catch_line, NULL);
	check_writes_nothing_with_debug_off();
	check_record_point_lines();
	check_empty_world();
	check_warheads_off();
	check_warhead_lines();
	check_warhead_beat();
	check_step();
	return 0;
}
