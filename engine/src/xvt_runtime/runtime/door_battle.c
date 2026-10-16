#include "xvt_runtime/runtime/door_battle.h"

#include <string.h>

#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net_setup_packets.h"
#include "xvt/net/net.h"
#include "xvt_runtime/log/log.h"

enum {
	/* The places of a team, and the teams, in the 1997 assignment tables. */
	TEAM_PLACES = 8,
	TEAMS = 10,
	/* The flight groups a mission holds. */
	FLIGHT_GROUPS = 48
};

int xvt_door_battle_players_known(const struct xvt_door_start *start)
{
	for (int seat = 0; seat < start->count; ++seat) {
		if (!net_find_player((int)start->players[seat])) {
			return 0;
		}
	}
	return 1;
}

int xvt_door_battle_fill_roster(const struct xvt_door_start *start)
{
	if (!xvt_door_battle_players_known(start)) {
		return 0;
	}
	memset(g_mp_roster, 0, sizeof(g_mp_roster));
	memset(g_mp_roster_ready_flags, 0, sizeof(g_mp_roster_ready_flags));
	net_clear_player_ready_flags();
	for (int seat = 0; seat < start->count; ++seat) {
		const struct net_player_info *player =
			net_find_player((int)start->players[seat]);
		struct mp_roster_entry *entry = &g_mp_roster[seat];
		/* The roster was emptied, so the name ends with a zero. */
		for (int at = 0;
		     at < PLAYER_NAME_COPY_SIZE && player->player_name[at];
		     ++at) {
			entry->name[at] = player->player_name[at];
		}
		entry->player_id = (int)player->player_id;
		entry->pilot_rating = start->ratings[seat];
		entry->craft_type_override = 0;
		entry->craft_option_index = -1;
		g_mp_roster_ready_flags[seat] = 1;
		net_mark_player_ready_no_lock(entry->player_id);
		XVT_LOG_DEBUG(
			"launch.roster_seat seat=%d player=%u rating=%d name=\"%s\"",
			seat, (unsigned)entry->player_id,
			(int)entry->pilot_rating, entry->name);
	}
	return 1;
}

/* Puts the roster's player at index placed in the first empty place of team,
 * as the 1997 deal does. Returns 1, or 0 when the team has no empty place. */
static int place_player(int team, int placed)
{
	struct mission_setup_player_assignments *assignments =
		&g_mission_setup_player_assignments;
	for (int place = 0; place < g_team_player_flight_group_count[team] &&
			    place < TEAM_PLACES;
	     ++place) {
		if (assignments->team_player_ids[team][place] == 0) {
			int player = g_mp_roster[placed].player_id;
			assignments->assigned_player_ids[placed] = player;
			assignments->team_player_ids[team][place] = player;
			XVT_LOG_DEBUG(
				"launch.player_placed player=%u team=%d place=%d",
				(unsigned)player, team, place);
			return 1;
		}
	}
	return 0;
}

int xvt_door_battle_deal_teams(int count)
{
	struct mission_setup_player_assignments *assignments =
		&g_mission_setup_player_assignments;
	memset(assignments->team_player_ids, 0,
	       sizeof(assignments->team_player_ids));
	memset(assignments->assigned_player_ids, 0,
	       sizeof(assignments->assigned_player_ids));
	int teams = g_team_count < TEAMS ? g_team_count : TEAMS;
	int placed = 0;
	int placed_in_round = 0;
	for (int team = 0;
	     teams > 0 && placed < count && placed < TEAM_PLACES;) {
		if (place_player(team, placed)) {
			++placed;
			++placed_in_round;
		}
		if (++team >= teams) {
			if (!placed_in_round) {
				break;
			}
			team = 0;
			placed_in_round = 0;
		}
	}
	int local = net_get_local_player_id();
	for (int team = 0; team < teams; ++team) {
		for (int place = 0; place < TEAM_PLACES; ++place) {
			if (assignments->team_player_ids[team][place] ==
			    local) {
				g_pilot_data.team = team;
				break;
			}
		}
	}
	XVT_LOG_DEBUG("launch.teams_dealt team=%d teams=%d placed=%d count=%d",
		      g_pilot_data.team, g_team_count, placed, count);
	return placed;
}

void xvt_door_battle_assign_flights(void)
{
	for (int team = 0; team < TEAMS; ++team) {
		memset(&g_mission_setup_player_flight_group_indices
			       [team * TEAM_PLACES],
		       0xFF,
		       TEAM_PLACES *
			       sizeof(g_mission_setup_player_flight_group_indices
					      [0]));
	}
	mission_setup_prune_flight_assignments();
	int teams = g_team_count < TEAMS ? g_team_count : TEAMS;
	int group_count = (int16_t)g_frontend_mission.flight_group_count;
	if (group_count > FLIGHT_GROUPS) {
		group_count = FLIGHT_GROUPS;
	}
	for (int team = 0; team < teams; ++team) {
		const int *players = g_mission_setup_player_assignments
					     .team_player_ids[team];
		int *team_groups = &g_mission_setup_player_flight_group_indices
					   [team * TEAM_PLACES];
		int place = 0;
		for (int group = 0; group < group_count; ++group) {
			const struct xvt_flight_group *flight_group =
				&g_frontend_mission.flight_groups[group];
			if (flight_group->player_number == 0 ||
			    flight_group->team != team) {
				continue;
			}
			for (; place < TEAM_PLACES; ++place) {
				if (players[place] != 0 &&
				    team_groups[place] == -1) {
					team_groups[place] = group;
					XVT_LOG_DEBUG(
						"launch.flight_given team=%d place=%d fg=%d",
						team, place, group);
					break;
				}
			}
		}
	}
}
