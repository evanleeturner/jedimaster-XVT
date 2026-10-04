/* Checks the room advertisement and its text conversion (xvt_runtime/runtime/network_metadata.h) against
 * the promises in its header: Windows-1252 to UTF-8 and back, the room built from the session (name,
 * password flag, slots, mission, the ready roster and when the room is joinable), and the flight roster
 * that keeps only players still active. The test sets the session, pilot and mission setup globals
 * itself; every case starts from a cleared session. */
#include <string.h>

#include "test_assert.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/net_session.h"
#include "xvt_runtime/runtime/network_metadata.h"

static struct xvt_network_metadata g_meta;

/* No session name, no password, no players, no mission selected, the session roster in use. */
static void clear_session(void)
{
	memset(&g_front_state, 0, sizeof g_front_state);
	memset(&g_pilot_data, 0, sizeof g_pilot_data);
	memset(&g_net_session, 0, sizeof g_net_session);
	memset(g_mp_roster, 0, sizeof g_mp_roster);
	g_mission_setup_roster_authoritative = 0;
	g_game_config.require_password = 0;
	g_pilot_data.mission_directory_id = 0;
	for (int i = 0; i < 6; ++i) {
		g_pilot_data.mission_description_ids[i] = -1;
	}
}

/* Adds a session player; rating_byte is the first byte of its playerName. */
static void add_player(DPID id, const char *name, int ready, int rating_byte)
{
	struct net_player_info *player =
		&g_front_state.net_players[g_front_state.net_player_count++];
	memset(player, 0, sizeof *player);
	player->player_id = id;
	player->ready_flag = ready;
	strncpy(player->player_name, name, sizeof player->player_name);
	player->long_name[0] = (char)rating_byte;
}

static void check_to_utf8(void)
{
	char out[16];

	/* ASCII passes through; the copy stops at size bytes or at a NUL. */
	xvt_network_metadata_to_utf8(out, sizeof out, "abc", 2);
	XVT_ASSERT_INT_EQ(strcmp(out, "ab"), 0);
	xvt_network_metadata_to_utf8(out, sizeof out, "ab\0cd", 5);
	XVT_ASSERT_INT_EQ(strcmp(out, "ab"), 0);

	/* Windows-1252: the euro sign at 0x80 and e-acute at 0xE9. */
	xvt_network_metadata_to_utf8(out, sizeof out, "\x80\xe9", 2);
	XVT_ASSERT_INT_EQ(strcmp(out, "\xe2\x82\xac\xc3\xa9"), 0);

	/* Control characters, DEL and the five undefined bytes become '?'. */
	xvt_network_metadata_to_utf8(out, sizeof out,
				     "\x01\x1f\x7f\x81\x8d\x8f\x90\x9d", 8);
	XVT_ASSERT_INT_EQ(strcmp(out, "????????"), 0);

	/* A character that would not fit is not started; out stays terminated. */
	xvt_network_metadata_to_utf8(out, 4, "a\x80", 2);
	XVT_ASSERT_INT_EQ(strcmp(out, "a"), 0);
	xvt_network_metadata_to_utf8(out, 5, "a\x80", 2);
	XVT_ASSERT_INT_EQ(strcmp(out, "a\xe2\x82\xac"), 0);
	xvt_network_metadata_to_utf8(out, 1, "abc", 3);
	XVT_ASSERT_INT_EQ(out[0], 0);

	/* A capacity of 0 writes nothing. */
	out[0] = 'x';
	xvt_network_metadata_to_utf8(out, 0, "abc", 3);
	XVT_ASSERT_INT_EQ(out[0], 'x');
}

static void check_from_utf8(void)
{
	char out[16];

	xvt_network_metadata_from_utf8(out, sizeof out, "Hello");
	XVT_ASSERT_INT_EQ(strcmp(out, "Hello"), 0);
	xvt_network_metadata_from_utf8(out, sizeof out,
				       "\xe2\x82\xac\xc3\xa9\xc2\xa0");
	XVT_ASSERT_INT_EQ(strcmp(out, "\x80\xe9\xa0"), 0);

	/* A character 1252 cannot hold, a control character (C0, DEL, C1), a surrogate, a code point past
	 * U+10FFFF: one '?' each. */
	xvt_network_metadata_from_utf8(
		out, sizeof out,
		"\xc4\x80|\x01|\x7f|\xc2\x80|\xed\xa0\x80|\xf4\x90\x80\x80");
	XVT_ASSERT_INT_EQ(strcmp(out, "?|?|?|?|?|?"), 0);

	/* Overlong forms of two, three and four bytes: '?' each, the 2-byte one through its invalid lead
	 * byte 0xC0, which with its continuation byte gives one '?' per byte. */
	xvt_network_metadata_from_utf8(
		out, sizeof out, "\xe0\x80\x80|\xf0\x80\x80\x80|\xc0\x80");
	XVT_ASSERT_INT_EQ(strcmp(out, "?|?|??"), 0);

	/* Invalid lead bytes: one '?' per byte. */
	xvt_network_metadata_from_utf8(out, sizeof out, "\xff\xfe\x80");
	XVT_ASSERT_INT_EQ(strcmp(out, "???"), 0);

	/* A sequence cut short: one '?', then the byte that broke it is read again. */
	xvt_network_metadata_from_utf8(out, sizeof out,
				       "\xe2\x82"
				       "A");
	XVT_ASSERT_INT_EQ(strcmp(out, "?A"), 0);
	xvt_network_metadata_from_utf8(out, sizeof out, "\xc3");
	XVT_ASSERT_INT_EQ(strcmp(out, "?"), 0);

	/* Truncated to capacity - 1, always terminated; a capacity of 0 writes nothing. */
	xvt_network_metadata_from_utf8(out, 4, "abcdef");
	XVT_ASSERT_INT_EQ(strcmp(out, "abc"), 0);
	xvt_network_metadata_from_utf8(out, 3, "\xc3\xa9\xc3\xa9\xc3\xa9");
	XVT_ASSERT_INT_EQ(strcmp(out, "\xe9\xe9"), 0);
	out[0] = 'x';
	xvt_network_metadata_from_utf8(out, 0, "abc");
	XVT_ASSERT_INT_EQ(out[0], 'x');
}

static void check_round_trip(void)
{
	/* Every printable Windows-1252 byte survives the trip to UTF-8 and back. */
	char text[256];
	size_t count = 0;
	for (unsigned byte = 0x20; byte <= 0xff; ++byte) {
		if (byte == 0x7f || byte == 0x81 || byte == 0x8d ||
		    byte == 0x8f || byte == 0x90 || byte == 0x9d) {
			continue;
		}
		text[count++] = (char)byte;
	}
	text[count] = 0;
	char utf8[256 * 3 + 1];
	xvt_network_metadata_to_utf8(utf8, sizeof utf8, text, count);
	char back[256];
	xvt_network_metadata_from_utf8(back, sizeof back, utf8);
	XVT_ASSERT_INT_EQ(strlen(back), count);
	XVT_ASSERT_INT_EQ(memcmp(back, text, count), 0);
}

static void check_build_room(void)
{
	clear_session();
	memset(&g_meta, 0xAB, sizeof g_meta);
	xvt_network_metadata_build(&g_meta, 1);
	XVT_ASSERT_INT_EQ(strcmp(g_meta.room.name, "Internet game."), 0);
	XVT_ASSERT_INT_EQ(g_meta.room.max_players, 8);
	XVT_ASSERT_INT_EQ(g_meta.room.password_required, 0);
	XVT_ASSERT_INT_EQ(g_meta.room.mission.present, 0);
	XVT_ASSERT_INT_EQ(g_meta.room.players, 0);
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(g_meta.players[i], 0);
	}

	/* The session name in Windows-1252, the password flag, and the selected mission. */
	strcpy(g_front_state.net_session_name, "Caf\xe9");
	g_game_config.require_password = 5;
	g_pilot_data.mission_directory_id = 3;
	g_pilot_data.mission_description_ids[3] = 42;
	xvt_network_metadata_build(&g_meta, 1);
	XVT_ASSERT_INT_EQ(strcmp(g_meta.room.name, "Caf\xc3\xa9"), 0);
	XVT_ASSERT_INT_EQ(g_meta.room.password_required, 1);
	XVT_ASSERT_INT_EQ(g_meta.room.mission.present, 1);
	XVT_ASSERT_INT_EQ(g_meta.room.mission.directory, 3);
	XVT_ASSERT_INT_EQ(g_meta.room.mission.id, 42);

	/* No mission selected in that directory. */
	g_pilot_data.mission_description_ids[3] = -1;
	xvt_network_metadata_build(&g_meta, 1);
	XVT_ASSERT_INT_EQ(g_meta.room.mission.present, 0);
	g_game_config.require_password = 0;
}

static void check_build_session_roster(void)
{
	clear_session();
	add_player(101, "Alpha", 1, 3);
	add_player(102, "Bravo", 0, 4);
	add_player(103, "", 1, 1);
	add_player(101, "Again", 1, 5);
	add_player(104, "D\xe9lta", 1, 9);
	xvt_network_metadata_build(&g_meta, 1);

	/* Ready players only, once each, in roster order. */
	XVT_ASSERT_INT_EQ(g_meta.room.players, 3);
	XVT_ASSERT_INT_EQ(g_meta.players[0], 101);
	XVT_ASSERT_INT_EQ(g_meta.players[1], 103);
	XVT_ASSERT_INT_EQ(g_meta.players[2], 104);
	XVT_ASSERT_INT_EQ(g_meta.players[3], 0);
	XVT_ASSERT_INT_EQ(strcmp(g_meta.room.roster[0].name, "Alpha"), 0);
	XVT_ASSERT_INT_EQ(g_meta.room.roster[0].rating, 2);
	XVT_ASSERT_INT_EQ(strcmp(g_meta.room.roster[1].name, "No name"), 0);
	XVT_ASSERT_INT_EQ(g_meta.room.roster[1].rating, 0);
	XVT_ASSERT_INT_EQ(strcmp(g_meta.room.roster[2].name, "D\xc3\xa9lta"),
			  0);
	XVT_ASSERT_INT_EQ(g_meta.room.roster[2].rating, 8);

	/* Joinable when accepting and a slot is free. */
	XVT_ASSERT_INT_EQ(g_meta.room.joinable, 1);
	xvt_network_metadata_build(&g_meta, 0);
	XVT_ASSERT_INT_EQ(g_meta.room.joinable, 0);
}

static void check_build_at_most_eight(void)
{
	clear_session();
	for (DPID id = 1; id <= 10; ++id) {
		add_player(200 + id, "Pilot", 1, 2);
	}
	xvt_network_metadata_build(&g_meta, 1);
	XVT_ASSERT_INT_EQ(g_meta.room.players, 8);
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(g_meta.players[i], 201 + i);
	}
	/* No slot is free, so the room is not joinable even when accepting. */
	XVT_ASSERT_INT_EQ(g_meta.room.joinable, 0);
}

static void check_build_authoritative_roster(void)
{
	clear_session();
	add_player(101, "Alpha", 1, 3);
	add_player(102, "Bravo", 1, 4);
	add_player(103, "Charlie", 1, 5);
	add_player(104, "Delta", 0, 6);
	g_mission_setup_roster_authoritative = 1;
	g_mp_roster[0].player_id = 103;
	g_mp_roster[1].player_id = 101;
	g_mp_roster[2].player_id = 104;
	g_mp_roster[3].player_id = 999;
	xvt_network_metadata_build(&g_meta, 1);

	/* The mission roster's order, ready players only; a roster id with no session player is skipped. */
	XVT_ASSERT_INT_EQ(g_meta.room.players, 2);
	XVT_ASSERT_INT_EQ(g_meta.players[0], 103);
	XVT_ASSERT_INT_EQ(g_meta.players[1], 101);
	XVT_ASSERT_INT_EQ(strcmp(g_meta.room.roster[0].name, "Charlie"), 0);
	XVT_ASSERT_INT_EQ(g_meta.room.roster[0].rating, 4);
	/* An authoritative roster is never joinable. */
	XVT_ASSERT_INT_EQ(g_meta.room.joinable, 0);
}

static void check_flight(void)
{
	clear_session();
	add_player(101, "Alpha", 1, 3);
	add_player(102, "Bravo", 1, 4);
	add_player(103, "Charlie", 1, 5);
	xvt_network_metadata_build(&g_meta, 0);
	XVT_ASSERT_INT_EQ(g_meta.room.players, 3);

	/* 101 and 103 are still active, in the session's other order; 102 has left. */
	g_net_session.players[0].direct_play_id = 103;
	g_net_session.players[0].active_flag = 1;
	g_net_session.players[1].direct_play_id = 102;
	g_net_session.players[1].active_flag = 0;
	g_net_session.players[5].direct_play_id = 101;
	g_net_session.players[5].active_flag = 1;
	xvt_network_metadata_keep_active_players(&g_meta);
	XVT_ASSERT_INT_EQ(g_meta.room.players, 2);
	XVT_ASSERT_INT_EQ(g_meta.players[0], 101);
	XVT_ASSERT_INT_EQ(g_meta.players[1], 103);
	XVT_ASSERT_INT_EQ(strcmp(g_meta.room.roster[0].name, "Alpha"), 0);
	XVT_ASSERT_INT_EQ(g_meta.room.roster[0].rating, 2);
	XVT_ASSERT_INT_EQ(strcmp(g_meta.room.roster[1].name, "Charlie"), 0);
	XVT_ASSERT_INT_EQ(g_meta.room.roster[1].rating, 4);
	for (int i = 2; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(g_meta.players[i], 0);
		XVT_ASSERT_INT_EQ(g_meta.room.roster[i].name[0], 0);
		XVT_ASSERT_INT_EQ(g_meta.room.roster[i].rating, 0);
	}
}

int main(void)
{
	check_to_utf8();
	check_from_utf8();
	check_round_trip();
	check_build_room();
	check_build_session_roster();
	check_build_at_most_eight();
	check_build_authoritative_roster();
	check_flight();
	return 0;
}
