#include "xvt_runtime/runtime/sith.h"

#include <stdint.h>

#include "xvt/assets/object_genus.h"
#include "xvt/assets/object_type.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/log/log.h"

enum { CRAFT_TYPE_COUNT = 96 };

static int g_setting = XVT_SITH_NONE;
static int g_lord_flight_group = -1;

void xvt_sith_set_setting(int setting)
{
	g_setting = setting == XVT_SITH_APPRENTICES || setting == XVT_SITH_LORD
			    ? setting
			    : XVT_SITH_NONE;
}

int xvt_sith_get_setting(void) { return g_setting; }

void xvt_sith_set_from_description(int described)
{
	xvt_sith_set_setting(described);
}

const char *xvt_sith_setting_name(int setting)
{
	switch (setting) {
	case XVT_SITH_APPRENTICES:
		return "apprentices";
	case XVT_SITH_LORD:
		return "lord";
	default:
		return "none";
	}
}

void xvt_sith_end_flight(void)
{
	g_setting = XVT_SITH_NONE;
	g_lord_flight_group = -1;
}

int xvt_sith_lord_flight_group(void) { return g_lord_flight_group; }

int xvt_sith_craft_level(int flight_group, int group_level, int craft_number)
{
	if (flight_group >= 0 && flight_group == g_lord_flight_group &&
	    craft_number == 0) {
		return XVT_SITH_LORD_LEVEL;
	}
	return group_level;
}

/* 1 when flight group index flies on a team hostile to player_team, by the
 * test the Hard difficulty uses: another team that does not count player_team
 * among its allies. */
static int is_hostile(int index, int player_team)
{
	int team = g_mission_flight_groups[index].fg.team;
	return player_team == team
		       ? 0
		       : g_mission_teams[team].allies[player_team] == 0;
}

/* 1 when the flight group's craft type is a starfighter. */
static int flies_starfighters(int index)
{
	unsigned craft_type = g_mission_flight_groups[index].fg.craft_type;
	return craft_type < CRAFT_TYPE_COUNT &&
	       g_object_type_table[g_craft_type_to_object_type[craft_type]]
			       .genus_id == CRAFT_GENUS_STARFIGHTER;
}

void xvt_sith_promote_flight_groups(void)
{
	g_lord_flight_group = -1;
	if (g_setting == XVT_SITH_NONE) {
		return;
	}
	int player_team = (uint16_t)g_players[0].team;
	int groups = g_mission_header.num_flight_groups;
	int hostile_computer = 0;
	int chosen = -1;
	for (int index = 0; index < groups; ++index) {
		if (g_mission_flight_groups[index].fg.player_number != 0 ||
		    !is_hostile(index, player_team)) {
			continue;
		}
		++hostile_computer;
		if (flies_starfighters(index) &&
		    (chosen < 0 ||
		     g_mission_flight_groups[index].fg.group_ai >
			     g_mission_flight_groups[chosen].fg.group_ai)) {
			chosen = index;
		}
	}
	if (chosen < 0) {
		XVT_LOG_DEBUG(
			"sith.none_to_promote setting=\"%s\" groups=%d hostile_computer=%d reason=\"%s\"",
			xvt_sith_setting_name(g_setting), groups,
			hostile_computer,
			hostile_computer == 0
				? "no hostile flight group that no player can fly"
				: "none of those groups flies starfighters");
		return;
	}
	struct mission_flight_group *group = &g_mission_flight_groups[chosen];
	int level_was = group->fg.group_ai;
	if (level_was < XVT_SITH_APPRENTICE_LEVEL) {
		group->fg.group_ai = XVT_SITH_APPRENTICE_LEVEL;
	}
	if (g_setting == XVT_SITH_LORD) {
		g_lord_flight_group = chosen;
	}
	XVT_LOG_DEBUG(
		"sith.promoted setting=\"%s\" fg=%d team=%d craft=%d count=%d level_was=%d level=%d lord_rank=%d",
		xvt_sith_setting_name(g_setting), chosen, (int)group->fg.team,
		(int)group->fg.craft_type, (int)group->fg.number_of_craft,
		level_was, (int)group->fg.group_ai,
		g_setting == XVT_SITH_LORD ? XVT_SITH_LORD_LEVEL : 0);
}
