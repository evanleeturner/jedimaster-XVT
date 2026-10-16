/* Checks the network door's battle setup (xvt_runtime/runtime/door_battle.h)
 * against the promises in its header, on made-up state: a session player list
 * of four games, Ace (this machine), Rook, Wedge and a fourth that holds no
 * seat, and made-up team places and flight groups in place of a mission read
 * from the game's files. The deal and the flight groups are checked against
 * what the 1997 team and flight screens' first frames give for the same
 * places, worked out by hand below. */
#include <stdio.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/net.h"
#include "xvt_runtime/runtime/door_battle.h"

enum { ACE = 0x10, ROOK = 0x20, WEDGE = 0x30, OTHER = 0x40 };

/* Puts player id with name and a long name whose first byte is
 * rating_byte at entry index of the session's list. */
static void list_player(int index, int id, const char *name, int rating_byte)
{
	struct net_player_info *player = &g_front_state.net_players[index];
	memset(player, 0, sizeof(*player));
	player->player_id = (DPID)id;
	snprintf(player->player_name, sizeof(player->player_name), "%s", name);
	player->long_name[0] = (char)rating_byte;
}

/* The list with Ace, Rook and Wedge, and the fourth game marked ready as a
 * lobby would leave it. */
static void list_all(void)
{
	memset(&g_front_state.net_players, 0,
	       sizeof(g_front_state.net_players));
	list_player(0, ACE, "Ace", 5);
	list_player(1, ROOK, "Rook", 3);
	list_player(2, WEDGE, "Wedge", 1);
	list_player(3, OTHER, "Biggs", 2);
	g_front_state.net_players[3].ready_flag = 1;
	g_front_state.net_player_count = 4;
}

/* The start of a battle seating Ace, Rook and Wedge. */
static struct xvt_door_start three_seats(void)
{
	struct xvt_door_start start;
	memset(&start, 0, sizeof(start));
	start.count = 3;
	start.players[0] = ACE;
	start.players[1] = ROOK;
	start.players[2] = WEDGE;
	/* Not the list's ratings (4, 2 and 0), which the host read. */
	start.ratings[0] = 1;
	start.ratings[1] = 6;
	start.ratings[2] = 3;
	return start;
}

static void check_players_known(void)
{
	list_all();
	struct xvt_door_start start = three_seats();
	XVT_ASSERT_INT_EQ(xvt_door_battle_players_known(&start), 1);
	/* Wedge not yet listed: a joiner told of it later. */
	g_front_state.net_player_count = 2;
	XVT_ASSERT_INT_EQ(xvt_door_battle_players_known(&start), 0);
}

static void check_fill_roster(void)
{
	list_all();
	struct xvt_door_start start = three_seats();

	/* A list lacking a seat's player changes nothing. */
	memset(g_mp_roster, 0x5A, sizeof(g_mp_roster));
	g_mp_roster_ready_flags[0] = 7;
	g_front_state.net_player_count = 2;
	XVT_ASSERT_INT_EQ(xvt_door_battle_fill_roster(&start), 0);
	XVT_ASSERT_INT_EQ(g_mp_roster[0].player_id, 0x5A5A5A5A);
	XVT_ASSERT_INT_EQ(g_mp_roster_ready_flags[0], 7);
	XVT_ASSERT_INT_EQ(g_front_state.net_players[3].ready_flag, 1);

	g_front_state.net_player_count = 4;
	XVT_ASSERT_INT_EQ(xvt_door_battle_fill_roster(&start), 1);
	const int ids[3] = {ACE, ROOK, WEDGE};
	const char *const names[3] = {"Ace", "Rook", "Wedge"};
	for (int seat = 0; seat < 3; ++seat) {
		XVT_ASSERT_INT_EQ(g_mp_roster[seat].player_id, ids[seat]);
		XVT_ASSERT_TRUE(!strcmp(g_mp_roster[seat].name, names[seat]));
		/* The start's rating, not the list's long name. */
		XVT_ASSERT_INT_EQ(g_mp_roster[seat].pilot_rating,
				  start.ratings[seat]);
		XVT_ASSERT_INT_EQ(g_mp_roster[seat].craft_type_override, 0);
		XVT_ASSERT_INT_EQ(g_mp_roster[seat].craft_option_index, -1);
		XVT_ASSERT_INT_EQ(g_mp_roster[seat].warhead_option_index, 0);
		XVT_ASSERT_INT_EQ(g_mp_roster[seat].beam_option_index, 0);
		XVT_ASSERT_INT_EQ(g_mp_roster[seat].countermeasure_option_index,
				  0);
		XVT_ASSERT_INT_EQ(g_mp_roster_ready_flags[seat], 1);
		XVT_ASSERT_INT_EQ(g_front_state.net_players[seat].ready_flag,
				  1);
	}
	/* The entries past the seats are empty and not ready. */
	for (int entry = 3; entry < 8; ++entry) {
		XVT_ASSERT_INT_EQ(g_mp_roster[entry].player_id, 0);
		XVT_ASSERT_INT_EQ(g_mp_roster[entry].name[0], 0);
		XVT_ASSERT_INT_EQ(g_mp_roster_ready_flags[entry], 0);
	}
	/* The game without a seat is no longer ready, so the launch screen
	 * counts the seats alone. */
	XVT_ASSERT_INT_EQ(g_front_state.net_players[3].ready_flag, 0);
	XVT_ASSERT_INT_EQ(net_count_ready_players(), 3);

	/* A long name keeps 13 characters, as the 1997 lobby state copied. */
	list_player(1, ROOK, "Rook_the_Third", 3);
	XVT_ASSERT_INT_EQ(xvt_door_battle_fill_roster(&start), 1);
	XVT_ASSERT_TRUE(!strcmp(g_mp_roster[1].name, "Rook_the_Thir"));
}

/* Sets the mission's teams: count teams with places[team] player places. */
static void set_teams(int count, const int *places)
{
	memset(g_team_player_flight_group_count, 0,
	       sizeof(g_team_player_flight_group_count));
	g_team_count = count;
	for (int team = 0; team < count; ++team) {
		g_team_player_flight_group_count[team] = places[team];
	}
}

/* Fills the roster with Ace, Rook and Wedge, then deals count of them on the
 * machine whose player is local (the first entry of the session's list), the
 * assignment tables first filled with junk the deal must clear. */
static int deal(int count, int local)
{
	list_all();
	struct xvt_door_start start = three_seats();
	XVT_ASSERT_INT_EQ(xvt_door_battle_fill_roster(&start), 1);
	g_front_state.net_players[0].player_id = (DPID)local;
	memset(&g_mission_setup_player_assignments, 0x33,
	       sizeof(g_mission_setup_player_assignments));
	g_pilot_data.team = 9;
	return xvt_door_battle_deal_teams(count);
}

static int place(int team, int slot)
{
	return g_mission_setup_player_assignments.team_player_ids[team][slot];
}

static void check_deal(void)
{
	/* A melee of three one-place teams: one player each, in seat order;
	 * Ace's team is 0. */
	const int melee[3] = {1, 1, 1};
	set_teams(3, melee);
	XVT_ASSERT_INT_EQ(deal(3, ACE), 3);
	XVT_ASSERT_INT_EQ(place(0, 0), ACE);
	XVT_ASSERT_INT_EQ(place(1, 0), ROOK);
	XVT_ASSERT_INT_EQ(place(2, 0), WEDGE);
	XVT_ASSERT_INT_EQ(place(0, 1), 0);
	XVT_ASSERT_INT_EQ(place(9, 7), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.team, 0);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.assigned_player_ids[2],
		WEDGE);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.assigned_player_ids[3], 0);

	/* Two teams of two places: round the teams, Ace and Wedge on team 0,
	 * Rook on team 1. On Wedge's machine the team is 0, on Rook's 1. */
	const int pairs[2] = {2, 2};
	set_teams(2, pairs);
	XVT_ASSERT_INT_EQ(deal(3, WEDGE), 3);
	XVT_ASSERT_INT_EQ(place(0, 0), ACE);
	XVT_ASSERT_INT_EQ(place(1, 0), ROOK);
	XVT_ASSERT_INT_EQ(place(0, 1), WEDGE);
	XVT_ASSERT_INT_EQ(place(1, 1), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.team, 0);
	g_front_state.net_players[0].player_id = ROOK;
	XVT_ASSERT_INT_EQ(xvt_door_battle_deal_teams(3), 3);
	XVT_ASSERT_INT_EQ(g_pilot_data.team, 1);

	/* A full team is passed over: team 0's one place, then team 1 takes
	 * the rest. */
	const int uneven[2] = {1, 3};
	set_teams(2, uneven);
	XVT_ASSERT_INT_EQ(deal(3, ACE), 3);
	XVT_ASSERT_INT_EQ(place(0, 0), ACE);
	XVT_ASSERT_INT_EQ(place(1, 0), ROOK);
	XVT_ASSERT_INT_EQ(place(1, 1), WEDGE);
	XVT_ASSERT_INT_EQ(place(0, 1), 0);

	/* Fewer places than players: the deal stops, two placed. */
	const int short_places[2] = {1, 1};
	set_teams(2, short_places);
	XVT_ASSERT_INT_EQ(deal(3, ACE), 2);
	XVT_ASSERT_INT_EQ(place(1, 0), ROOK);
	XVT_ASSERT_INT_EQ(
		g_mission_setup_player_assignments.assigned_player_ids[2], 0);

	/* A mission without player teams places nobody and leaves the team. */
	set_teams(0, short_places);
	XVT_ASSERT_INT_EQ(deal(3, ACE), 0);
	XVT_ASSERT_INT_EQ(place(0, 0), 0);
	XVT_ASSERT_INT_EQ(g_pilot_data.team, 9);
}

/* Sets flight group index of the loaded mission: its team and player
 * number. */
static void set_group(int index, int team, int player_number)
{
	g_frontend_mission.flight_groups[index].team = (uint8_t)team;
	g_frontend_mission.flight_groups[index].player_number =
		(uint8_t)player_number;
}

static int flight(int team, int slot)
{
	return g_mission_setup_player_flight_group_indices[team * 8 + slot];
}

static void check_flights(void)
{
	/* Ace and Wedge on team 0, Rook on team 1, as two teams of two places
	 * deal them. */
	const int pairs[2] = {2, 2};
	set_teams(2, pairs);
	XVT_ASSERT_INT_EQ(deal(3, ACE), 3);
	memset(&g_frontend_mission, 0, sizeof(g_frontend_mission));
	set_group(0, 0, 1);
	set_group(1, 1, 0); /* a computer group of team 1 */
	set_group(2, 1, 1);
	set_group(3, 0, 2);
	set_group(4, 0, 3); /* a third player group of team 0, no player */
	set_group(5, 1, 2); /* team 1's second place holds nobody */
	g_frontend_mission.flight_group_count = 6;
	memset(g_mission_setup_player_flight_group_indices, 0x22,
	       sizeof(g_mission_setup_player_flight_group_indices));
	xvt_door_battle_assign_flights();
	XVT_ASSERT_INT_EQ(flight(0, 0), 0);
	XVT_ASSERT_INT_EQ(flight(0, 1), 3);
	XVT_ASSERT_INT_EQ(flight(0, 2), -1);
	XVT_ASSERT_INT_EQ(flight(1, 0), 2);
	XVT_ASSERT_INT_EQ(flight(1, 1), -1);
	/* Every place of the other teams is emptied. */
	for (int slot = 0; slot < 8; ++slot) {
		XVT_ASSERT_INT_EQ(flight(2, slot), -1);
		XVT_ASSERT_INT_EQ(flight(9, slot), -1);
	}
}

int main(void)
{
	check_players_known();
	check_fill_roster();
	check_deal();
	check_flights();
	return 0;
}
