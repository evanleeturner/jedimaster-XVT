#ifndef XVT_RUNTIME_LAUNCH_STATS_H
#define XVT_RUNTIME_LAUNCH_STATS_H

#include "xvt_runtime/runtime/launch_result.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Reads a finished battle's numbers into the launch result, as the game's
 * end of battle left them. No state. */

/* Fills r's players, teams and detail: every entry of the pilot record that
 * holds a player, in entry order, with the numbers of its place in the
 * flight; each of teams 0 to 7 that had a player flight group arrive or met
 * its goals, the teams the game's end of battle prints a battle.team_result
 * line for; and the local pilot's own tables from row 0 of
 * last_mission_stats. Logs one DEBUG line per player and team read, and one
 * for the tables. Call it after the game's end of battle and before the
 * next mission is loaded: both clear what it reads. The other members of r
 * are left as they are. */
void xvt_launch_stats_read(struct xvt_launch_result *r);

#ifdef __cplusplus
}
#endif

#endif
