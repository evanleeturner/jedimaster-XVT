/* Tests for xvt/flight/ai_waypoints.c, a computer craft's waypoints in its
 * view, on worlds this file builds: CRAFT_SLOTS craft in flight groups 0
 * and 1, whose mission points are set by hand. No game data is read. The
 * points and their scale are those the 1997 cruise maneuver flew to
 * (paiman_advance_order_waypoint): mission points 4 to 11, through the
 * mission point resolver. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/ai_waypoints.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt_runtime/ai/ai_wire.h"

enum {
	CRAFT_SLOTS = 4,
	IN_GROUP_0 = 0,
	IN_GROUP_1 = 1,
	/* A byte no field of a fresh view holds. */
	STALE = 0x5A,
};

static struct object_record g_test_objects[CRAFT_SLOTS];

/* An empty world: craft IN_GROUP_0 in flight group 0, IN_GROUP_1 in flight
 * group 1, and no mission point on in either group. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_mission_flight_groups, 0,
	       2 * sizeof g_mission_flight_groups[0]);
	g_object_table = g_test_objects;
	g_test_objects[IN_GROUP_0].flight_group_idx = 0;
	g_test_objects[IN_GROUP_1].flight_group_idx = 1;
	g_flight_sim_side_effects_suppressed = 0;
}

/* Turns mission point point of group on at x, y and z, the mission's own
 * numbers. */
static void set_point(int group, int point, int x, int y, int z)
{
	struct mission_flight_group *flight_group =
		&g_mission_flight_groups[group];
	flight_group->fg.mission_point_enabled[point] = 1;
	flight_group->fg.mission_point_x[point] = (int16_t)x;
	flight_group->fg.mission_point_y[point] = (int16_t)y;
	flight_group->fg.mission_point_z[point] = (int16_t)z;
}

/* The view of object_index, written over a view of stale bytes. */
static struct ai_wire_craft_view view_of(uint16_t object_index)
{
	struct ai_wire_craft_view view;
	memset(&view, STALE, sizeof view);
	ai_waypoints_view_craft(object_index, &view);
	return view;
}

static void assert_point_zero(const struct ai_wire_craft_view *view, int point)
{
	XVT_ASSERT_INT_EQ(view->waypoint_x[point], 0);
	XVT_ASSERT_INT_EQ(view->waypoint_y[point], 0);
	XVT_ASSERT_INT_EQ(view->waypoint_z[point], 0);
}

/* Points 4 to 6 on give bits 0 to 2, each at 256 times the mission's
 * numbers with y negated; point 7, off, is 0 whatever the mission holds
 * there; the start point 3 and points 12 and 13, on, are not waypoints. */
static void check_view_names_points_on(void)
{
	fresh_world();
	set_point(0, 4, 10, 20, 30);
	set_point(0, 5, -1, 2, -3);
	set_point(0, 6, 0, 0, 5);
	set_point(0, 7, 99, 99, 99);
	g_mission_flight_groups[0].fg.mission_point_enabled[7] = 0;
	set_point(0, 3, 40, 40, 40);
	set_point(0, 12, 50, 50, 50);
	set_point(0, 13, 60, 60, 60);

	struct ai_wire_craft_view view = view_of(IN_GROUP_0);

	XVT_ASSERT_INT_EQ(view.waypoint_mask, 0x07);
	XVT_ASSERT_INT_EQ(view.waypoint_x[0], 2560);
	XVT_ASSERT_INT_EQ(view.waypoint_y[0], -5120);
	XVT_ASSERT_INT_EQ(view.waypoint_z[0], 7680);
	XVT_ASSERT_INT_EQ(view.waypoint_x[1], -256);
	XVT_ASSERT_INT_EQ(view.waypoint_y[1], -512);
	XVT_ASSERT_INT_EQ(view.waypoint_z[1], -768);
	XVT_ASSERT_INT_EQ(view.waypoint_x[2], 0);
	XVT_ASSERT_INT_EQ(view.waypoint_y[2], 0);
	XVT_ASSERT_INT_EQ(view.waypoint_z[2], 1280);
	for (int point = 3; point < AI_WIRE_WAYPOINTS; ++point) {
		assert_point_zero(&view, point);
	}
}

/* Point 11, the last waypoint, alone on: bit 7 and entry 7. */
static void check_view_last_point(void)
{
	fresh_world();
	set_point(0, 11, 1, -1, 2);

	struct ai_wire_craft_view view = view_of(IN_GROUP_0);

	XVT_ASSERT_INT_EQ(view.waypoint_mask, 0x80);
	XVT_ASSERT_INT_EQ(view.waypoint_x[7], 256);
	XVT_ASSERT_INT_EQ(view.waypoint_y[7], 256);
	XVT_ASSERT_INT_EQ(view.waypoint_z[7], 512);
	for (int point = 0; point < 7; ++point) {
		assert_point_zero(&view, point);
	}
}

/* Each craft gets its own group's points. */
static void check_view_reads_own_group(void)
{
	fresh_world();
	set_point(0, 4, 3, 0, 0);
	set_point(1, 4, 7, 0, 0);
	set_point(1, 5, 0, 0, 9);

	struct ai_wire_craft_view first = view_of(IN_GROUP_0);
	struct ai_wire_craft_view second = view_of(IN_GROUP_1);

	XVT_ASSERT_INT_EQ(first.waypoint_mask, 0x01);
	XVT_ASSERT_INT_EQ(first.waypoint_x[0], 768);
	assert_point_zero(&first, 1);
	XVT_ASSERT_INT_EQ(second.waypoint_mask, 0x03);
	XVT_ASSERT_INT_EQ(second.waypoint_x[0], 1792);
	XVT_ASSERT_INT_EQ(second.waypoint_z[1], 2304);
}

/* With no point on, the mask and every entry are 0, whatever the view held
 * before. */
static void check_view_no_point_on(void)
{
	fresh_world();

	struct ai_wire_craft_view view = view_of(IN_GROUP_0);

	XVT_ASSERT_INT_EQ(view.waypoint_mask, 0);
	for (int point = 0; point < AI_WIRE_WAYPOINTS; ++point) {
		assert_point_zero(&view, point);
	}
}

int main(void)
{
	check_view_names_points_on();
	check_view_last_point();
	check_view_reads_own_group();
	check_view_no_point_on();
	return 0;
}
