/* A computer craft's waypoints, for the pilot program: its flight group's
 * mission points 4 to 11, the points the 1997 cruise maneuver flew in turn
 * (paiman_cruisemaneuver and paiman_advance_order_waypoint in paiman.c,
 * deleted with the 1997 AI). Which point comes next, when one is reached
 * and how the rounds count moved to the program. */
#include "xvt/flight/ai_waypoints.h"

#include <stdint.h>

#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"

enum {
	/* The mission point that is a flight group's first waypoint. */
	AI_WAYPOINTS_FIRST_POINT = 4,
	/* The point resolver's reference to a mission point: this plus the
	 * point's index. */
	AI_WAYPOINTS_POINT_REF = 0x8000,
};

void ai_waypoints_view_craft(uint16_t object_index,
			     struct ai_wire_craft_view *view)
{
	int group = g_object_table[object_index].flight_group_idx;
	const struct mission_flight_group *flight_group =
		&g_mission_flight_groups[group];
	view->waypoint_mask = 0;
	for (int point = 0; point < AI_WIRE_WAYPOINTS; ++point) {
		int mission_point = AI_WAYPOINTS_FIRST_POINT + point;
		view->waypoint_x[point] = 0;
		view->waypoint_y[point] = 0;
		view->waypoint_z[point] = 0;
		if (flight_group->fg.mission_point_enabled[mission_point] ==
		    0) {
			continue;
		}
		mission_resolve_object_or_mission_point_world_loc(
			AI_WAYPOINTS_POINT_REF + (unsigned int)mission_point,
			group);
		view->waypoint_mask |= (uint8_t)(1u << point);
		view->waypoint_x[point] = g_world_loc_x;
		view->waypoint_y[point] = g_world_loc_y;
		view->waypoint_z[point] = g_world_loc_z;
	}
}
