#ifndef XVT_FLIGHT_AI_WAYPOINTS_H
#define XVT_FLIGHT_AI_WAYPOINTS_H

/* A computer craft's waypoints: what the question tells the pilot program
 * about its flight group's mission points 4 to 11, the points the patrol
 * orders fly in turn. The 1997 AI read them itself, one at a time, as it
 * reached each; the program now picks the next point and counts the
 * rounds, and the engine only says where the points are and which are on. */

#include <stdint.h>

#include "xvt_runtime/ai/ai_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Writes into view the waypoints of the craft in object_index, from its
 * flight group: bit n of the waypoint mask is set when mission point 4 + n
 * is on, and entry n holds that point's world position, the mission's
 * point times 256 with y negated; a point that is off is 0. Changes no
 * game state but the g_world_loc_ globals the point resolver writes. */
void ai_waypoints_view_craft(uint16_t object_index,
			     struct ai_wire_craft_view *view);

#ifdef __cplusplus
}
#endif

#endif
