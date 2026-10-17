/* Tests for xvt/flight/ai_dropoff.c and the Drop Off books in ai_books.c, on
 * worlds this file builds: no game data is read. Each rule is checked against
 * the 1997 drop-off maneuver and order it was ported from (the place, the push
 * raised by the carrier's lowest Z, the turn at 256, the arrival at 32, the
 * wait of 1,180 ticks, the 2,048 test, and the deliveries counted); the two 1997
 * faults the port does not keep (the formation basis 255, and a drop that
 * outlived its order) are checked gone. The drop's state is the craft's
 * maneuver mode, phase and timer, so a test puts a craft in a phase by setting
 * those, as the engine's save would. The group's arrival is counted by a
 * stand-in the linker hands the test: the real one spawns craft. */
#include <SDL3/SDL_log.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/model_bounds.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai_boarding.h"
#include "xvt/flight/ai_books.h"
#include "xvt/flight/ai_dropoff.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/math/trig2.h"
#include "xvt_runtime/ai/ai_wire.h"
#include "xvt_runtime/log/log.h"

enum {
	SLOT_COUNT = 12,
	GROUP_COUNT = 6,
	/* The carrier's slot and group, and the delivered groups: a plain
	 * group of 2 craft, and a mine group of 4 x 4. */
	CARRIER = 1,
	CARRIER_GROUP = 0,
	PLAIN_GROUP = 2,
	MINE_GROUP = 3,
	/* The carrier's object type, and the plain and mine groups'. */
	CARRIER_TYPE = 5,
	PLAIN_CRAFT_TYPE = 7,
	PLAIN_OBJECT_TYPE = 8,
	MINE_CRAFT_TYPE = 9,
	MINE_OBJECT_TYPE = 100,
	/* Where the plain group's point is, in mission units, and in world
	 * units (x 256). */
	POINT_X = 4,
	POINT_Y = 3,
	POINT_Z = 2,
	WORLD_X = POINT_X * 256,
	WORLD_Y = -POINT_Y * 256,
	WORLD_Z = POINT_Z * 256,
	DROP_RAISE = 932,
	/* The carrier's order slot for Drop Off. */
	ORDER_SLOT = 1,
	/* The carrier's lowest Z, negated: the push is raised by it. */
	LOWEST_Z = -40,
	STATIC_FLAG = 0x80,
	MINE_GENUS = 8,
};

static struct object_record g_objects[SLOT_COUNT];
static struct mobile_object g_mobiles[SLOT_COUNT];
static struct craft_data g_craft[SLOT_COUNT];

/* What the stand-in for a group's arrival was called with. */
enum { ARRIVAL_CAPACITY = 32 };
struct test_arrival {
	int group;
	int place;
	int leader;
	struct craft_data *caller;
};
static struct test_arrival g_arrivals[ARRIVAL_CAPACITY];
static int g_arrival_count;
/* A craft the stand-in leaves in g_cur_craft, as the real arrival may. */
static struct craft_data g_other_craft;

int16_t __wrap_mission_start_flight_group_arrival(uint16_t craft_ordinal);

/* Counts the arrival, adds the craft to the group's arrived count as a spawn
 * would, and leaves g_cur_craft on another craft. */
int16_t __wrap_mission_start_flight_group_arrival(uint16_t craft_ordinal)
{
	if (g_arrival_count < ARRIVAL_CAPACITY) {
		g_arrivals[g_arrival_count].group = g_current_flight_group_idx;
		g_arrivals[g_arrival_count].place = craft_ordinal;
		g_arrivals[g_arrival_count].leader = g_spawn_leader_obj_idx;
		g_arrivals[g_arrival_count].caller = g_cur_craft;
	}
	++g_arrival_count;
	g_mission_fg_stats[g_current_flight_group_idx].has_arrived = 1;
	++g_mission_fg_stats[g_current_flight_group_idx]
		  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED];
	g_cur_craft = &g_other_craft;
	return 1;
}

enum {
	LOG_LINE_CAPACITY = 256,
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

static int lines_containing(const char *start, const char *part)
{
	int found = 0;
	for (int line = 0; line < g_log_line_count; ++line) {
		found +=
			strncmp(g_log_lines[line], start, strlen(start)) == 0 &&
			strstr(g_log_lines[line], part) != NULL;
	}
	return found;
}

static struct ai_controller *carrier(void)
{
	return &g_craft[CARRIER].ai_controller;
}

/* Leaves the place the last lookup found as nonsense, so a step that did not
 * look the place up shows it. */
static void poison_world_loc(void)
{
	g_world_loc_x = 0x1234567;
	g_world_loc_y = -0x2345678;
	g_world_loc_z = 0x3456789;
}

/* Empty slots with their mobile and craft records, in the active region;
 * GROUP_COUNT groups; the carrier in slot CARRIER of group CARRIER_GROUP at
 * the origin, flying, on Drop Off of the plain group in order slot
 * ORDER_SLOT; the plain group of 2 craft with its point at (WORLD_X,
 * WORLD_Y, WORLD_Z), 2 to deliver; the mine group of 4 x 4 (16 to deliver)
 * on the mission's grid. Bounds: the carrier's lowest Z is LOWEST_Z, every
 * other 0. */
static void fresh_world(void)
{
	memset(g_objects, 0, sizeof g_objects);
	memset(g_mobiles, 0, sizeof g_mobiles);
	memset(g_craft, 0, sizeof g_craft);
	memset(&g_other_craft, 0, sizeof g_other_craft);
	memset(g_mission_flight_groups, 0, sizeof g_mission_flight_groups);
	memset(g_mission_fg_stats, 0,
	       GROUP_COUNT * sizeof g_mission_fg_stats[0]);
	g_mission_header.num_flight_groups = GROUP_COUNT;
	g_object_table = g_objects;
	for (int slot = 0; slot < SLOT_COUNT; ++slot) {
		g_objects[slot].mobj = &g_mobiles[slot];
		g_objects[slot].player_owner_idx = -1;
		g_mobiles[slot].p_craft = &g_craft[slot];
		g_craft[slot].leader_obj_idx = UINT8_MAX;
	}
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = SLOT_COUNT;
	g_game_time = 0x2000;
	g_flight_sim_side_effects_suppressed = 0;
	g_cur_craft = &g_craft[CARRIER];
	poison_world_loc();
	g_current_flight_group_idx = 0;
	g_spawn_leader_obj_idx = 0;
	g_log_line_count = 0;
	g_arrival_count = 0;
	memset(g_arrivals, 0, sizeof g_arrivals);
	g_objects[CARRIER].object_type = CARRIER_TYPE;
	g_objects[CARRIER].flight_group_idx = CARRIER_GROUP;
	g_craft[CARRIER].object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	g_craft[CARRIER].carrier_obj_idx = UINT16_MAX;
	carrier()->current_order_slot = ORDER_SLOT;
	struct mission_order *order =
		&g_mission_flight_groups[CARRIER_GROUP].fg.orders[ORDER_SLOT];
	order->order = MISSION_ORDER_DROP_OFF;
	order->variable2 = PLAIN_GROUP + 1;
	g_craft_type_to_object_type[PLAIN_CRAFT_TYPE] = PLAIN_OBJECT_TYPE;
	g_craft_type_to_object_type[MINE_CRAFT_TYPE] = MINE_OBJECT_TYPE;
	memset(&g_object_type_table[PLAIN_OBJECT_TYPE], 0,
	       sizeof g_object_type_table[0]);
	memset(&g_object_type_table[MINE_OBJECT_TYPE], 0,
	       sizeof g_object_type_table[0]);
	g_object_type_table[MINE_OBJECT_TYPE].behavior_flags = STATIC_FLAG;
	g_object_type_table[MINE_OBJECT_TYPE].genus_id = MINE_GENUS;
	for (int type = 0; type < 201; ++type) {
		g_model_bounds_cached[type] = 1;
		memset(&g_model_bounds_min[type], 0,
		       sizeof g_model_bounds_min[0]);
		memset(&g_model_bounds_max[type], 0,
		       sizeof g_model_bounds_max[0]);
	}
	g_model_bounds_min[CARRIER_TYPE].z = LOWEST_Z;
	struct xvt_flight_group *plain =
		&g_mission_flight_groups[PLAIN_GROUP].fg;
	plain->craft_type = PLAIN_CRAFT_TYPE;
	plain->number_of_craft = 2;
	plain->mission_point_x[0] = POINT_X;
	plain->mission_point_y[0] = POINT_Y;
	plain->mission_point_z[0] = POINT_Z;
	g_mission_fg_stats[PLAIN_GROUP].current_mission_point_ref = 0x8000;
	g_mission_fg_stats[PLAIN_GROUP]
		.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 2;
	struct xvt_flight_group *mines =
		&g_mission_flight_groups[MINE_GROUP].fg;
	mines->craft_type = MINE_CRAFT_TYPE;
	mines->number_of_craft = 4;
	mines->mission_point_x[0] = POINT_X;
	mines->mission_point_y[0] = POINT_Y;
	mines->mission_point_z[0] = POINT_Z;
	g_mission_fg_stats[MINE_GROUP].current_mission_point_ref = 0x8000;
	g_mission_fg_stats[MINE_GROUP]
		.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 16;
}

/* Puts the carrier where the plain group's drop point is, plus (dx, 0, 0). */
static void carrier_at_drop_point(int dx)
{
	g_objects[CARRIER].world_x = WORLD_X + dx;
	g_objects[CARRIER].world_y = WORLD_Y;
	g_objects[CARRIER].world_z = WORLD_Z + DROP_RAISE;
}

static uint32_t drop_bits(uint8_t phase)
{
	int32_t point[AI_WIRE_POINT_AXES];
	return ai_books_drop_bits(CARRIER, phase, point);
}

/* Puts the carrier in a phase of the drop as the engine's save holds it. */
static void in_drop(uint8_t phase, int place, int timer)
{
	carrier()->maneuver_mode = AI_MANEUVER_MODE_DROPOFF;
	carrier()->maneuver_phase = phase;
	carrier()->waypoint_index = (uint8_t)place;
	carrier()->maneuver_timer = timer;
	carrier()->unused0b = ORDER_SLOT;
}

static void check_phase_and_mode(void)
{
	fresh_world();
	XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), 0);
	/* The phase is the maneuver phase, and only while the mode is DROPOFF:
	 * the dock's mode with the same phase is no drop. */
	carrier()->maneuver_phase = 2;
	XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), 0);
	carrier()->maneuver_mode = AI_MANEUVER_MODE_BOARD;
	XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), 0);
	carrier()->maneuver_mode = AI_MANEUVER_MODE_DROPOFF;
	XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), 2);
	carrier()->maneuver_phase = 1;
	XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), 1);
	/* A phase past the wait is none. */
	carrier()->maneuver_phase = 3;
	XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), 0);
	carrier()->maneuver_phase = 0;
	XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), 0);
}

static void check_sync_mode(void)
{
	fresh_world();
	/* DROPOFF with no phase is cleared; with a phase it stays; another mode
	 * is left alone. */
	carrier()->maneuver_mode = AI_MANEUVER_MODE_DROPOFF;
	ai_dropoff_sync_mode(CARRIER);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_mode, AI_MANEUVER_MODE_NULL);
	in_drop(2, 0, 5);
	ai_dropoff_sync_mode(CARRIER);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_mode, AI_MANEUVER_MODE_DROPOFF);
	carrier()->maneuver_mode = AI_MANEUVER_MODE_BOARD;
	ai_dropoff_sync_mode(CARRIER);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_mode, AI_MANEUVER_MODE_BOARD);
	/* A craft that left the battle is skipped. */
	in_drop(0, 0, 0);
	g_objects[CARRIER].object_type = 0;
	ai_dropoff_sync_mode(CARRIER);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_mode, AI_MANEUVER_MODE_DROPOFF);
}

/* DROP_POINT, AT_DROP_POINT and the drop point: the order, the group, the
 * delivered count against the total, the leader, the 2,048 test at its
 * edge (2,048 itself is out), and an arrived group. */
static void check_books_drop_point(void)
{
	int32_t point[AI_WIRE_POINT_AXES];
	const uint32_t both =
		AI_WIRE_BOOKS_DROP_POINT | AI_WIRE_BOOKS_AT_DROP_POINT;
	fresh_world();
	carrier_at_drop_point(0);
	XVT_ASSERT_INT_EQ(ai_books_drop_bits(CARRIER, 0, point), both);
	XVT_ASSERT_INT_EQ(point[0], WORLD_X);
	XVT_ASSERT_INT_EQ(point[1], WORLD_Y);
	XVT_ASSERT_INT_EQ(point[2], WORLD_Z + DROP_RAISE);
	/* The 2,048 test: 2,047 away is at it, 2,048 is not, still the point
	 * is given. */
	carrier_at_drop_point(2047);
	XVT_ASSERT_INT_EQ(drop_bits(0), both);
	carrier_at_drop_point(2048);
	XVT_ASSERT_INT_EQ(drop_bits(0), AI_WIRE_BOOKS_DROP_POINT);
	carrier_at_drop_point(-2047);
	XVT_ASSERT_INT_EQ(drop_bits(0), both);
	carrier_at_drop_point(-2048);
	XVT_ASSERT_INT_EQ(drop_bits(0), AI_WIRE_BOOKS_DROP_POINT);
	carrier_at_drop_point(0);
	/* Another order: no bits and no point. */
	g_mission_flight_groups[CARRIER_GROUP].fg.orders[ORDER_SLOT].order =
		MISSION_ORDER_BOARD_AND_GIVE_CARGO;
	XVT_ASSERT_INT_EQ(ai_books_drop_bits(CARRIER, 0, point), 0);
	XVT_ASSERT_INT_EQ(point[0] | point[1] | point[2], 0);
	g_mission_flight_groups[CARRIER_GROUP].fg.orders[ORDER_SLOT].order =
		MISSION_ORDER_DROP_OFF;
	/* The craft works on the order slot, not on slot 0. */
	carrier()->current_order_slot = 0;
	XVT_ASSERT_INT_EQ(drop_bits(0), 0);
	carrier()->current_order_slot = ORDER_SLOT;
	/* A follower is no one's carrier. */
	g_craft[CARRIER].leader_obj_idx = 3;
	XVT_ASSERT_INT_EQ(ai_books_drop_bits(CARRIER, 0, point), 0);
	XVT_ASSERT_INT_EQ(point[2], 0);
	g_craft[CARRIER].leader_obj_idx = UINT8_MAX;
	/* One delivered of two: still to drop; 2 of 2: all dropped, no point. */
	carrier()->order_progress.goal_progress[ORDER_SLOT] = 1;
	XVT_ASSERT_INT_EQ(drop_bits(0), both);
	carrier()->order_progress.goal_progress[ORDER_SLOT] = 2;
	XVT_ASSERT_INT_EQ(ai_books_drop_bits(CARRIER, 0, point),
			  AI_WIRE_BOOKS_ALL_DROPPED);
	XVT_ASSERT_INT_EQ(point[2], 0);
	XVT_ASSERT_INT_EQ(drop_bits(1), AI_WIRE_BOOKS_ALL_DROPPED);
	carrier()->order_progress.goal_progress[ORDER_SLOT] = 0;
	/* A delivered group with an arrived craft: the point, not at it, and
	 * with no drop running all dropped. */
	g_mission_fg_stats[PLAIN_GROUP]
		.outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED] = 1;
	XVT_ASSERT_INT_EQ(drop_bits(0),
			  AI_WIRE_BOOKS_DROP_POINT | AI_WIRE_BOOKS_ALL_DROPPED);
	XVT_ASSERT_INT_EQ(drop_bits(1), AI_WIRE_BOOKS_DROP_POINT);
	XVT_ASSERT_INT_EQ(drop_bits(2), AI_WIRE_BOOKS_DROP_POINT);
}

/* ALL_DROPPED: every case, a Drop Off with nothing to drop counted as done
 * (1997 circled the drop point for good), and the group's total past what
 * the 8-bit counters reach. */
static void check_books_all_dropped(void)
{
	struct mission_order *order =
		&g_mission_flight_groups[CARRIER_GROUP].fg.orders[ORDER_SLOT];
	int32_t point[AI_WIRE_POINT_AXES];
	fresh_world();
	carrier_at_drop_point(0);
	/* variable2 outside 1 to num_flight_groups, with no drop running. */
	order->variable2 = 0;
	XVT_ASSERT_INT_EQ(ai_books_drop_bits(CARRIER, 0, point),
			  AI_WIRE_BOOKS_ALL_DROPPED);
	XVT_ASSERT_INT_EQ(point[2], 0);
	order->variable2 = GROUP_COUNT + 1;
	XVT_ASSERT_INT_EQ(drop_bits(0), AI_WIRE_BOOKS_ALL_DROPPED);
	order->variable2 = GROUP_COUNT;
	g_mission_fg_stats[GROUP_COUNT - 1]
		.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 3;
	XVT_ASSERT_INT_EQ((drop_bits(0) & AI_WIRE_BOOKS_ALL_DROPPED), 0);
	/* A drop that runs is not ended by the books. */
	order->variable2 = 0;
	XVT_ASSERT_INT_EQ(drop_bits(1), 0);
	order->variable2 = PLAIN_GROUP + 1;
	/* A total of 0 (the group switched off): delivered 0 is already
	 * enough. */
	g_mission_fg_stats[PLAIN_GROUP]
		.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 0;
	XVT_ASSERT_INT_EQ(drop_bits(0), AI_WIRE_BOOKS_ALL_DROPPED);
	/* A total of 255 can be reached; 256 cannot, so it counts as nothing to
	 * drop, never DROP_POINT, with one warning for the group. */
	g_mission_fg_stats[PLAIN_GROUP]
		.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 255;
	carrier()->order_progress.goal_progress[ORDER_SLOT] = 254;
	XVT_ASSERT_INT_EQ(drop_bits(0), AI_WIRE_BOOKS_DROP_POINT |
						AI_WIRE_BOOKS_AT_DROP_POINT);
	carrier()->order_progress.goal_progress[ORDER_SLOT] = 255;
	XVT_ASSERT_INT_EQ(drop_bits(0), AI_WIRE_BOOKS_ALL_DROPPED);
	XVT_ASSERT_INT_EQ(lines_starting("ai.dropoff_total_too_big"), 0);
	carrier()->order_progress.goal_progress[ORDER_SLOT] = 0;
	g_mission_fg_stats[PLAIN_GROUP]
		.outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 256;
	XVT_ASSERT_INT_EQ(drop_bits(0), AI_WIRE_BOOKS_ALL_DROPPED);
	XVT_ASSERT_INT_EQ(drop_bits(0), AI_WIRE_BOOKS_ALL_DROPPED);
	XVT_ASSERT_INT_EQ(drop_bits(1), 0);
	XVT_ASSERT_INT_EQ(lines_starting("ai.dropoff_total_too_big"), 1);
	XVT_ASSERT_INT_EQ(lines_containing("ai.dropoff_total_too_big",
					   "group=2 total=256 max=255"),
			  1);
	/* The first group of the mission (variable2 1) is a group too. */
	order->variable2 = 1;
	g_mission_fg_stats[0].outcome_count[FLIGHT_GROUP_OUTCOME_TOTAL] = 2;
	carrier()->order_progress.goal_progress[ORDER_SLOT] = 0;
	XVT_ASSERT_INT_EQ((drop_bits(0) & AI_WIRE_BOOKS_DROP_POINT),
			  AI_WIRE_BOOKS_DROP_POINT);
	order->variable2 = PLAIN_GROUP + 1;
	/* A mine group's total is 4 x 4: 15 delivered is not all, 16 is. */
	g_mission_flight_groups[CARRIER_GROUP].fg.orders[ORDER_SLOT].variable2 =
		MINE_GROUP + 1;
	g_mission_fg_stats[MINE_GROUP].current_mission_point_ref = 0x8000;
	carrier_at_drop_point(0);
	carrier()->order_progress.goal_progress[ORDER_SLOT] = 15;
	XVT_ASSERT_INT_EQ((drop_bits(0) & AI_WIRE_BOOKS_ALL_DROPPED), 0);
	XVT_ASSERT_INT_EQ((drop_bits(0) & AI_WIRE_BOOKS_DROP_POINT),
			  AI_WIRE_BOOKS_DROP_POINT);
	/* Its drop point is the first cell of the grid (slot 0, 64 apart,
	 * centered on the group's point), 932 up. */
	carrier()->order_progress.goal_progress[ORDER_SLOT] = 0;
	ai_books_drop_bits(CARRIER, 0, point);
	XVT_ASSERT_INT_EQ(point[0], (POINT_X - 96) * 256);
	XVT_ASSERT_INT_EQ(point[1], -(POINT_Y + 96) * 256);
	XVT_ASSERT_INT_EQ(point[2], POINT_Z * 256 + DROP_RAISE);
	carrier()->order_progress.goal_progress[ORDER_SLOT] = 16;
	XVT_ASSERT_INT_EQ(drop_bits(0), AI_WIRE_BOOKS_ALL_DROPPED);
	XVT_ASSERT_INT_EQ(drop_bits(2), AI_WIRE_BOOKS_ALL_DROPPED);
}

static void check_start(void)
{
	fresh_world();
	/* Far from the drop point: declined, nothing changes. */
	carrier_at_drop_point(2048);
	carrier()->waypoint_index = 9;
	ai_dropoff_start(CARRIER);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_mode, AI_MANEUVER_MODE_NULL);
	XVT_ASSERT_INT_EQ(carrier()->waypoint_index, 9);
	XVT_ASSERT_INT_EQ(lines_containing("ai.craft_drop_off", "started=0"),
			  1);
	/* At it: roll, pitch and turn stopped, throttle 0, place 0, phase 1,
	 * mode DROPOFF, the order slot kept, no push or timer. */
	carrier_at_drop_point(2047);
	g_craft[CARRIER].ai_flight.roll_state = 1;
	g_craft[CARRIER].ai_flight.pitch_state = 2;
	g_craft[CARRIER].ai_flight.turn_state = 2;
	g_craft[CARRIER].throttle_speed = 0x8000;
	g_craft[CARRIER].push_accum_x = 5;
	carrier()->maneuver_timer = 77;
	carrier()->unused0b = 0;
	ai_dropoff_start(CARRIER);
	XVT_ASSERT_INT_EQ(lines_containing("ai.craft_drop_off", "started=1"),
			  1);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_mode, AI_MANEUVER_MODE_DROPOFF);
	XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), AI_DROPOFF_PHASE_PUSH);
	XVT_ASSERT_INT_EQ(carrier()->waypoint_index, 0);
	XVT_ASSERT_INT_EQ(carrier()->unused0b, ORDER_SLOT);
	XVT_ASSERT_INT_EQ(g_craft[CARRIER].ai_flight.roll_state, 0);
	XVT_ASSERT_INT_EQ(g_craft[CARRIER].ai_flight.pitch_state, 0);
	XVT_ASSERT_INT_EQ(g_craft[CARRIER].ai_flight.turn_state, 0);
	XVT_ASSERT_INT_EQ(g_craft[CARRIER].throttle_speed, 0);
	XVT_ASSERT_INT_EQ(g_craft[CARRIER].push_accum_x, 0);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_timer, 0);
	/* Running already: declined, the place kept. */
	carrier()->waypoint_index = 1;
	ai_dropoff_start(CARRIER);
	XVT_ASSERT_INT_EQ(carrier()->waypoint_index, 1);
	XVT_ASSERT_INT_EQ(lines_containing("ai.craft_drop_off", "started=0"),
			  2);
	/* A dock running: declined, the dock's mode kept. */
	carrier()->maneuver_mode = AI_MANEUVER_MODE_BOARD;
	carrier()->maneuver_phase = 2;
	ai_dropoff_start(CARRIER);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_mode, AI_MANEUVER_MODE_BOARD);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_phase, 2);
	XVT_ASSERT_INT_EQ(carrier()->waypoint_index, 1);
	/* A group that already arrived: no AT_DROP_POINT, declined. */
	carrier()->maneuver_mode = AI_MANEUVER_MODE_NULL;
	carrier()->maneuver_phase = 0;
	g_mission_fg_stats[PLAIN_GROUP]
		.outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED] = 1;
	ai_dropoff_start(CARRIER);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_mode, AI_MANEUVER_MODE_NULL);
}

static void check_stop(void)
{
	fresh_world();
	/* A running drop ends at once, in either phase, keeping the places
	 * delivered; the mode is the null maneuver again (#306). */
	for (uint8_t phase = 1; phase <= 2; ++phase) {
		in_drop(phase, 1, 500);
		g_craft[CARRIER].push_accum_x = 9;
		g_craft[CARRIER].push_accum_y = 9;
		g_craft[CARRIER].push_accum_z = 9;
		carrier()->order_progress.goal_progress[ORDER_SLOT] = 1;
		ai_dropoff_stop(CARRIER);
		XVT_ASSERT_INT_EQ(carrier()->maneuver_mode,
				  AI_MANEUVER_MODE_NULL);
		XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), 0);
		XVT_ASSERT_INT_EQ(carrier()->maneuver_phase, 0);
		XVT_ASSERT_INT_EQ(carrier()->maneuver_timer, 0);
		XVT_ASSERT_INT_EQ(g_craft[CARRIER].push_accum_x, 0);
		XVT_ASSERT_INT_EQ(g_craft[CARRIER].push_accum_y, 0);
		XVT_ASSERT_INT_EQ(g_craft[CARRIER].push_accum_z, 0);
		XVT_ASSERT_INT_EQ(carrier()->waypoint_index, 1);
		XVT_ASSERT_INT_EQ(
			carrier()->order_progress.goal_progress[ORDER_SLOT], 1);
	}
	XVT_ASSERT_INT_EQ(lines_starting("ai.dropoff_stopped"), 2);
	/* With no drop running nothing changes, a dock's mode and push
	 * included. */
	carrier()->maneuver_mode = AI_MANEUVER_MODE_BOARD;
	carrier()->maneuver_phase = 3;
	carrier()->maneuver_timer = 44;
	g_craft[CARRIER].push_accum_x = 6;
	ai_dropoff_stop(CARRIER);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_mode, AI_MANEUVER_MODE_BOARD);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_phase, 3);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_timer, 44);
	XVT_ASSERT_INT_EQ(g_craft[CARRIER].push_accum_x, 6);
}

/* The push phase: the push is the place's position less the carrier's, the
 * carrier's lowest Z negated added to Z; the turn comes when X plus Y is
 * more than 256 and the heading is not already there; the arrival at X plus
 * Y plus Z below 32 (a sum). */
static void check_push_and_turn(void)
{
	fresh_world();
	in_drop(1, 0, 0);
	carrier_at_drop_point(0);
	g_objects[CARRIER].world_x = 0;
	g_objects[CARRIER].world_y = 0;
	g_objects[CARRIER].world_z = 0;
	poison_world_loc();
	ai_dropoff_step(CARRIER);
	/* The place 0 of the plain group is its point, no raise: 1,024 east,
	 * 768 south, 512 up, and 40 more for the carrier's lowest Z. */
	XVT_ASSERT_INT_EQ(g_craft[CARRIER].push_accum_x, WORLD_X);
	XVT_ASSERT_INT_EQ(g_craft[CARRIER].push_accum_y, WORLD_Y);
	XVT_ASSERT_INT_EQ(g_craft[CARRIER].push_accum_z, WORLD_Z - LOWEST_Z);
	XVT_ASSERT_INT_EQ(g_arrival_count, 0);
	XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), AI_DROPOFF_PHASE_PUSH);
	/* The turn toward the place's heading, full step. */
	trig2_ctop(WORLD_X, WORLD_Y, WORLD_Z - LOWEST_Z);
	XVT_ASSERT_INT_EQ(g_craft[CARRIER].ai_flight.turn_state, 2);
	XVT_ASSERT_INT_EQ(g_craft[CARRIER].ai_flight.turn_step, INT16_MIN);
	XVT_ASSERT_INT_EQ(carrier()->target_xy_angle, (uint16_t)trig2_xyangle);
	/* Facing it already: no new turn. */
	g_craft[CARRIER].ai_flight.turn_state = 0;
	g_objects[CARRIER].yaw = (uint16_t)trig2_xyangle;
	ai_dropoff_step(CARRIER);
	XVT_ASSERT_INT_EQ(g_craft[CARRIER].ai_flight.turn_state, 0);
	g_objects[CARRIER].yaw = 0;
	/* The turn's edge: X plus Y of 256 turns no one, 257 does, either
	 * sign. */
	const int offsets[] = {256, 257, -256, -257};
	for (int index = 0; index < 4; ++index) {
		g_craft[CARRIER].ai_flight.turn_state = 0;
		g_objects[CARRIER].world_x = WORLD_X - offsets[index];
		g_objects[CARRIER].world_y = WORLD_Y;
		g_objects[CARRIER].yaw = 0x1234;
		g_objects[CARRIER].world_z = WORLD_Z - LOWEST_Z;
		ai_dropoff_step(CARRIER);
		XVT_ASSERT_INT_EQ(
			g_craft[CARRIER].ai_flight.turn_state,
			(offsets[index] == 256 || offsets[index] == -256) ? 0
									  : 2);
	}
	/* X and Y both count toward the 256: 128 + 129. */
	g_craft[CARRIER].ai_flight.turn_state = 0;
	g_objects[CARRIER].world_x = WORLD_X - 128;
	g_objects[CARRIER].world_y = WORLD_Y + 128;
	ai_dropoff_step(CARRIER);
	XVT_ASSERT_INT_EQ(g_craft[CARRIER].ai_flight.turn_state, 0);
	g_objects[CARRIER].world_y = WORLD_Y + 129;
	ai_dropoff_step(CARRIER);
	XVT_ASSERT_INT_EQ(g_craft[CARRIER].ai_flight.turn_state, 2);
}

static void check_arrival_edge(void)
{
	fresh_world();
	g_model_bounds_min[CARRIER_TYPE].z = 0;
	in_drop(1, 0, 0);
	/* Left 31 (16 + 8 + 7): not yet; 32 (16 + 8 + 8): not yet, a sum of 32
	 * is out; 31 with a sign flipped: arrives. */
	const int cases[][3] = {
		{16, 8, 8}, {16, 8, 9}, {0, 0, 32}, {-16, 8, 8}};
	for (size_t index = 0; index < 4; ++index) {
		g_objects[CARRIER].world_x = WORLD_X - cases[index][0];
		g_objects[CARRIER].world_y = WORLD_Y - cases[index][1];
		g_objects[CARRIER].world_z = WORLD_Z - cases[index][2];
		ai_dropoff_step(CARRIER);
		XVT_ASSERT_INT_EQ(g_arrival_count, 0);
		XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER),
				  AI_DROPOFF_PHASE_PUSH);
	}
	g_objects[CARRIER].world_x = WORLD_X - 16;
	g_objects[CARRIER].world_y = WORLD_Y - 8;
	g_objects[CARRIER].world_z = WORLD_Z - 7;
	ai_dropoff_step(CARRIER);
	XVT_ASSERT_INT_EQ(g_arrival_count, 1);
}

/* At the arrival: the group's arrival is started for the place with the
 * group and the leaderless craft set, the caller's craft is the current one
 * again, the wait starts at 1,180 ticks with a push of 1,500 up, and the
 * delivery is one INFO line (none in a replay of the step). */
static void check_delivery(void)
{
	fresh_world();
	g_model_bounds_min[CARRIER_TYPE].z = 0;
	in_drop(1, 0, 0);
	g_objects[CARRIER].world_x = WORLD_X;
	g_objects[CARRIER].world_y = WORLD_Y;
	g_objects[CARRIER].world_z = WORLD_Z;
	g_cur_craft = &g_craft[CARRIER];
	ai_dropoff_step(CARRIER);
	XVT_ASSERT_INT_EQ(g_arrival_count, 1);
	XVT_ASSERT_INT_EQ(g_arrivals[0].group, PLAIN_GROUP);
	XVT_ASSERT_INT_EQ(g_arrivals[0].place, 0);
	XVT_ASSERT_INT_EQ(g_arrivals[0].leader, 255);
	XVT_ASSERT_TRUE(g_arrivals[0].caller == &g_craft[CARRIER]);
	XVT_ASSERT_TRUE(g_cur_craft == &g_craft[CARRIER]);
	XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), AI_DROPOFF_PHASE_WAIT);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_timer, 1180);
	XVT_ASSERT_INT_EQ(g_craft[CARRIER].push_accum_z, 1500);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_mode, AI_MANEUVER_MODE_DROPOFF);
	XVT_ASSERT_INT_EQ(lines_starting("ai.dropoff_delivered"), 1);
	XVT_ASSERT_INT_EQ(lines_containing("ai.dropoff_delivered",
					   "fg=2 place=0 basis=-1"),
			  1);
	/* The places delivered are written each step: 0 until the wait ends. */
	XVT_ASSERT_INT_EQ(carrier()->order_progress.goal_progress[ORDER_SLOT],
			  0);
	/* A replay of the step makes the same changes and no INFO line. */
	fresh_world();
	g_model_bounds_min[CARRIER_TYPE].z = 0;
	g_flight_sim_side_effects_suppressed = 1;
	in_drop(1, 0, 0);
	g_objects[CARRIER].world_x = WORLD_X;
	g_objects[CARRIER].world_y = WORLD_Y;
	g_objects[CARRIER].world_z = WORLD_Z;
	ai_dropoff_step(CARRIER);
	XVT_ASSERT_INT_EQ(g_arrival_count, 1);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_timer, 1180);
	XVT_ASSERT_INT_EQ(lines_starting("ai.dropoff_delivered"), 0);
}

/* The wait phase: nothing happens while the timer runs (the push is left to
 * the engine's own pushing); at 0 the next place is taken and the push phase
 * begins, or at the last place the drop ends: mode NULL, phase 0, no push,
 * the places delivered equal to the total. */
static void check_wait(void)
{
	fresh_world();
	in_drop(2, 0, 1180);
	g_craft[CARRIER].push_accum_z = 1500;
	ai_dropoff_step(CARRIER);
	XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), AI_DROPOFF_PHASE_WAIT);
	XVT_ASSERT_INT_EQ(carrier()->waypoint_index, 0);
	XVT_ASSERT_INT_EQ(g_craft[CARRIER].push_accum_z, 1500);
	carrier()->maneuver_timer = 1;
	ai_dropoff_step(CARRIER);
	XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), AI_DROPOFF_PHASE_WAIT);
	carrier()->maneuver_timer = 0;
	ai_dropoff_step(CARRIER);
	/* Place 1 of 2: back to the push phase. */
	XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), AI_DROPOFF_PHASE_PUSH);
	XVT_ASSERT_INT_EQ(carrier()->waypoint_index, 1);
	XVT_ASSERT_INT_EQ(carrier()->order_progress.goal_progress[ORDER_SLOT],
			  1);
	XVT_ASSERT_INT_EQ(lines_starting("ai.dropoff_next"), 1);
	/* The last wait ends the drop with every place delivered. */
	in_drop(2, 1, 0);
	g_craft[CARRIER].push_accum_z = 1500;
	ai_dropoff_step(CARRIER);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_mode, AI_MANEUVER_MODE_NULL);
	XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), 0);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_phase, 0);
	XVT_ASSERT_INT_EQ(g_craft[CARRIER].push_accum_z, 0);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_timer, 0);
	XVT_ASSERT_INT_EQ(carrier()->waypoint_index, 2);
	XVT_ASSERT_INT_EQ(carrier()->order_progress.goal_progress[ORDER_SLOT],
			  2);
	carrier_at_drop_point(0);
	XVT_ASSERT_INT_EQ(drop_bits(0), AI_WIRE_BOOKS_ALL_DROPPED);
	/* A step on a craft with no drop leaves it alone. */
	carrier()->waypoint_index = 9;
	ai_dropoff_step(CARRIER);
	XVT_ASSERT_INT_EQ(carrier()->waypoint_index, 9);
}

/* Moves the carrier onto place place of group and steps until its arrival
 * has come and the wait is over. */
static void deliver_place(int group, int place)
{
	int32_t point[AI_WIRE_POINT_AXES];
	mission_resolve_formation_slot_world_loc((uint16_t)group,
						 (uint16_t)place, UINT16_MAX);
	point[0] = g_world_loc_x;
	point[1] = g_world_loc_y;
	point[2] = g_world_loc_z;
	g_objects[CARRIER].world_x = point[0];
	g_objects[CARRIER].world_y = point[1];
	g_objects[CARRIER].world_z = point[2];
	poison_world_loc();
	ai_dropoff_step(CARRIER);
	XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), AI_DROPOFF_PHASE_WAIT);
	carrier()->maneuver_timer = 0;
	ai_dropoff_step(CARRIER);
}

/* A whole drop of the 2-craft group: two arrivals, places 0 and 1, the drop
 * ends after the second wait and the books then say all dropped. */
static void check_whole_drop(void)
{
	fresh_world();
	g_model_bounds_min[CARRIER_TYPE].z = 0;
	carrier_at_drop_point(0);
	ai_dropoff_start(CARRIER);
	XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), AI_DROPOFF_PHASE_PUSH);
	deliver_place(PLAIN_GROUP, 0);
	XVT_ASSERT_INT_EQ(carrier()->waypoint_index, 1);
	deliver_place(PLAIN_GROUP, 1);
	XVT_ASSERT_INT_EQ(g_arrival_count, 2);
	XVT_ASSERT_INT_EQ(g_arrivals[0].place, 0);
	XVT_ASSERT_INT_EQ(g_arrivals[1].place, 1);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_mode, AI_MANEUVER_MODE_NULL);
	XVT_ASSERT_INT_EQ(carrier()->order_progress.goal_progress[ORDER_SLOT],
			  2);
	XVT_ASSERT_INT_EQ(lines_starting("ai.dropoff_delivered"), 2);
	XVT_ASSERT_INT_EQ(g_mission_fg_stats[PLAIN_GROUP]
				  .outcome_count[FLIGHT_GROUP_OUTCOME_ARRIVED],
			  2);
	XVT_ASSERT_INT_EQ(drop_bits(0), AI_WIRE_BOOKS_ALL_DROPPED);
}

/* Bug 48: a group with no craft of its own that leads is placed with no
 * basis (0xFFFF): the places are on the group's point, and the arrival's
 * leader is the 255 of no craft. With a leaderless craft of the group in the
 * battle, the last one is the basis. */
static void check_formation_basis(void)
{
	fresh_world();
	g_model_bounds_min[CARRIER_TYPE].z = 0;
	/* Two craft of the plain group, both following a leader. */
	for (int slot = 4; slot <= 5; ++slot) {
		g_objects[slot].object_type = PLAIN_OBJECT_TYPE;
		g_objects[slot].flight_group_idx = PLAIN_GROUP;
		g_craft[slot].leader_obj_idx = 4;
	}
	in_drop(1, 1, 0);
	g_objects[CARRIER].world_x = WORLD_X;
	g_objects[CARRIER].world_y = WORLD_Y;
	g_objects[CARRIER].world_z = WORLD_Z;
	ai_dropoff_step(CARRIER);
	/* Place 1 is also on the group's point, so the carrier on it has
	 * arrived, with no basis. */
	XVT_ASSERT_INT_EQ(g_arrival_count, 1);
	XVT_ASSERT_INT_EQ(g_arrivals[0].place, 1);
	XVT_ASSERT_INT_EQ(g_arrivals[0].leader, 255);
	XVT_ASSERT_INT_EQ(lines_containing("ai.dropoff_delivered", "basis=-1"),
			  1);
	/* A leaderless craft of the group, the last of two: the basis. */
	g_craft[5].leader_obj_idx = UINT8_MAX;
	g_craft[4].leader_obj_idx = UINT8_MAX;
	in_drop(1, 0, 0);
	ai_dropoff_step(CARRIER);
	XVT_ASSERT_INT_EQ(g_arrival_count, 2);
	XVT_ASSERT_INT_EQ(g_arrivals[1].leader, 5);
	XVT_ASSERT_INT_EQ(lines_containing("ai.dropoff_delivered", "basis=5"),
			  1);
	/* A craft of the group that left the battle, or another group's, is not
	 * a basis. */
	g_objects[5].object_type = 0;
	g_objects[6].object_type = CARRIER_TYPE;
	g_objects[6].flight_group_idx = CARRIER_GROUP;
	in_drop(1, 0, 0);
	ai_dropoff_step(CARRIER);
	XVT_ASSERT_INT_EQ(g_arrivals[2].leader, 4);
}

/* A mine group goes through the same call: 16 places, the carrier pushed to
 * each grid cell, 16 arrivals in order, then the drop ends. */
static void check_mine_group(void)
{
	fresh_world();
	g_model_bounds_min[CARRIER_TYPE].z = 0;
	g_mission_flight_groups[CARRIER_GROUP].fg.orders[ORDER_SLOT].variable2 =
		MINE_GROUP + 1;
	int32_t point[AI_WIRE_POINT_AXES];
	XVT_ASSERT_INT_EQ(ai_books_drop_bits(CARRIER, 0, point) &
				  AI_WIRE_BOOKS_DROP_POINT,
			  AI_WIRE_BOOKS_DROP_POINT);
	g_objects[CARRIER].world_x = point[0];
	g_objects[CARRIER].world_y = point[1];
	g_objects[CARRIER].world_z = point[2];
	ai_dropoff_start(CARRIER);
	XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), AI_DROPOFF_PHASE_PUSH);
	for (int place = 0; place < 16; ++place) {
		XVT_ASSERT_INT_EQ(carrier()->waypoint_index, place);
		deliver_place(MINE_GROUP, place);
	}
	XVT_ASSERT_INT_EQ(g_arrival_count, 16);
	for (int place = 0; place < 16; ++place) {
		XVT_ASSERT_INT_EQ(g_arrivals[place].group, MINE_GROUP);
		XVT_ASSERT_INT_EQ(g_arrivals[place].place, place);
	}
	XVT_ASSERT_INT_EQ(carrier()->maneuver_mode, AI_MANEUVER_MODE_NULL);
	XVT_ASSERT_INT_EQ(carrier()->order_progress.goal_progress[ORDER_SLOT],
			  16);
	/* Place 5 is its grid cell: row 1, column 1 of a 4 x 4 grid, 64
	 * units apart, centered on the group's point, as the mission lays the
	 * mines out. */
	mission_resolve_formation_slot_world_loc(MINE_GROUP, 5, UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_world_loc_x, (POINT_X - 96 + 64) * 256);
	XVT_ASSERT_INT_EQ(g_world_loc_y, (-(POINT_Y + 96) + 64) * 256);
}

/* The drop ends with its order: when the craft's current order is no longer
 * Drop Off, or its order slot is not the one the drop started under, or the
 * order's group is no group of the mission. The slot the craft moved to keeps
 * its progress, and the old slot's is not written either. */
static void check_order_change(void)
{
	struct mission_order *orders =
		g_mission_flight_groups[CARRIER_GROUP].fg.orders;
	fresh_world();
	g_model_bounds_min[CARRIER_TYPE].z = 0;
	/* To a slot with another order. */
	in_drop(2, 1, 600);
	carrier()->order_progress.goal_progress[0] = 7;
	carrier()->order_progress.goal_progress[ORDER_SLOT] = 1;
	carrier()->current_order_slot = 0;
	orders[0].order = MISSION_ORDER_CIRCLE;
	ai_dropoff_step(CARRIER);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_mode, AI_MANEUVER_MODE_NULL);
	XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER), 0);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_timer, 0);
	XVT_ASSERT_INT_EQ(carrier()->order_progress.goal_progress[0], 7);
	XVT_ASSERT_INT_EQ(carrier()->order_progress.goal_progress[ORDER_SLOT],
			  1);
	XVT_ASSERT_INT_EQ(lines_starting("ai.dropoff_ended"), 1);
	/* To another Drop Off slot: moved all the same. */
	orders[0].order = MISSION_ORDER_DROP_OFF;
	orders[0].variable2 = PLAIN_GROUP + 1;
	in_drop(1, 1, 0);
	carrier()->current_order_slot = 0;
	ai_dropoff_step(CARRIER);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_mode, AI_MANEUVER_MODE_NULL);
	XVT_ASSERT_INT_EQ(carrier()->order_progress.goal_progress[0], 7);
	XVT_ASSERT_INT_EQ(g_arrival_count, 0);
	/* The slot's order changed under it. */
	carrier()->current_order_slot = ORDER_SLOT;
	in_drop(2, 1, 30);
	orders[ORDER_SLOT].order = MISSION_ORDER_HOLD_STEADY;
	ai_dropoff_step(CARRIER);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_mode, AI_MANEUVER_MODE_NULL);
	orders[ORDER_SLOT].order = MISSION_ORDER_DROP_OFF;
	/* The group no longer a group of the mission. */
	in_drop(1, 1, 0);
	orders[ORDER_SLOT].variable2 = 0;
	ai_dropoff_step(CARRIER);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_mode, AI_MANEUVER_MODE_NULL);
	in_drop(1, 1, 0);
	orders[ORDER_SLOT].variable2 = GROUP_COUNT + 1;
	ai_dropoff_step(CARRIER);
	XVT_ASSERT_INT_EQ(carrier()->maneuver_mode, AI_MANEUVER_MODE_NULL);
	/* With the order unchanged the drop goes on, whichever group of the
	 * mission it names: the first (1) and the last (GROUP_COUNT) too. */
	const int named[] = {PLAIN_GROUP + 1, 1, GROUP_COUNT};
	for (int index = 0; index < 3; ++index) {
		orders[ORDER_SLOT].variable2 = (uint8_t)named[index];
		in_drop(2, 1, 30);
		ai_dropoff_step(CARRIER);
		XVT_ASSERT_INT_EQ(ai_dropoff_phase(CARRIER),
				  AI_DROPOFF_PHASE_WAIT);
	}
}

int main(void)
{
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	SDL_SetLogOutputFunction(catch_line, NULL);
	check_phase_and_mode();
	check_sync_mode();
	check_books_drop_point();
	check_books_all_dropped();
	check_start();
	check_stop();
	check_push_and_turn();
	check_arrival_edge();
	check_delivery();
	check_wait();
	check_whole_drop();
	check_formation_basis();
	check_mine_group();
	check_order_change();
	return 0;
}
