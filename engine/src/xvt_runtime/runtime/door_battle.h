#ifndef XVT_RUNTIME_DOOR_BATTLE_H
#define XVT_RUNTIME_DOOR_BATTLE_H

#include "xvt_runtime/runtime/door_admission.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The network battle the door starts, as every machine in it sets it up
 * alone from the host's start packet, in place of the 1997 lobby's team,
 * flight and craft screens, whose packets carried the host's choices to the
 * others. Each step is the first frame of one of those screens with nothing
 * chosen on it, so machines holding the same seats and the same mission reach
 * the same teams, flight groups and loadouts. */

/* 1 when the session's player list holds every player of start, else 0. A
 * joiner learns of the other joiners from the session, which may tell it after
 * the start packet comes; the battle's player state drops a player the list
 * lacks. */
int xvt_door_battle_players_known(const struct xvt_door_start *start);

/* Fills the 1997 lobby's roster (g_mp_roster) in seat order, as its lobby
 * state packet did: each seat's name from the session's player list, at most
 * 13 characters, its player id and the start's rating. Every seat flies the
 * mission's own craft and loadout, the loadout packet's values with nothing
 * chosen: craft 0, craft option -1, warhead, beam and countermeasure 0. Marks
 * exactly the seats' players ready, in the session's list and in
 * g_mp_roster_ready_flags. Returns 1, or 0 when the list lacks a seat's player,
 * having changed nothing. */
int xvt_door_battle_fill_roster(const struct xvt_door_start *start);

/* Deals the roster's first count players into the teams' player places as the
 * 1997 team screen's first frame does in a network game: it empties every
 * team's places, then goes round the teams from team 0, putting the next
 * player in the first empty place of each team that has one. The team
 * screen's deal goes on until every player has a place and never ends when
 * the places run out; this one stops after a round that places nobody. Then
 * g_pilot_data.team is the team holding the local player, as the host's team
 * packet set it on each machine. Reads g_team_count and
 * g_team_player_flight_group_count (mission_setup_update_team_counts).
 * Returns the number of players placed. */
int xvt_door_battle_deal_teams(int count);

/* Gives each placed player a player flight group of its team, as the 1997
 * flight screen's first frame does: every place of the 10 teams is emptied (-1),
 * then each of the loaded mission's player flight groups, in mission order,
 * goes to the first place of its team that holds a player and no flight group
 * yet. */
void xvt_door_battle_assign_flights(void);

#ifdef __cplusplus
}
#endif

#endif
