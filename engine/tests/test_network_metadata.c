/* Checks the room advertisement and its text conversion (xvt_runtime/runtime/network_metadata.h) against
 * the promises in its header: Windows-1252 to UTF-8 and back, the room built from the session (name,
 * password flag, slots, mission, the ready roster and when the room is joinable), and the flight roster
 * that keeps only players still active. The test sets the session, pilot and mission setup globals
 * itself; every case starts from a cleared session. */
#include "test_assert.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/net_session.h"
#include "xvt_runtime/runtime/network_metadata.h"

#include <string.h>

static XvtNetworkMetadata g_meta;

/* No session name, no password, no players, no mission selected, the session roster in use. */
static void ClearSession(void) {
	memset(&g_frontState, 0, sizeof g_frontState);
	memset(&g_pilotData, 0, sizeof g_pilotData);
	memset(&g_netSession, 0, sizeof g_netSession);
	memset(g_mpRoster, 0, sizeof g_mpRoster);
	g_missionSetupRosterAuthoritative = 0;
	g_gameConfig.requirePassword = 0;
	g_pilotData.missionDirectoryId = 0;
	for (int i = 0; i < 6; ++i)
		g_pilotData.missionDescriptionIds[i] = -1;
}

/* Adds a session player; rating_byte is the first byte of its playerName. */
static void AddPlayer(DPID id, const char* name, int ready, int rating_byte) {
	NetPlayerInfo* player = &g_frontState.netPlayers[g_frontState.netPlayerCount++];
	memset(player, 0, sizeof *player);
	player->playerId = id;
	player->readyFlag = ready;
	strncpy(player->sessionName, name, sizeof player->sessionName);
	player->playerName[0] = (char)rating_byte;
}

static void CheckToUtf8(void) {
	char out[16];

	/* ASCII passes through; the copy stops at size bytes or at a NUL. */
	XvtNetworkMetadata_ToUtf8(out, sizeof out, "abc", 2);
	XVT_ASSERT_INT_EQ(strcmp(out, "ab"), 0);
	XvtNetworkMetadata_ToUtf8(out, sizeof out, "ab\0cd", 5);
	XVT_ASSERT_INT_EQ(strcmp(out, "ab"), 0);

	/* Windows-1252: the euro sign at 0x80 and e-acute at 0xE9. */
	XvtNetworkMetadata_ToUtf8(out, sizeof out, "\x80\xe9", 2);
	XVT_ASSERT_INT_EQ(strcmp(out, "\xe2\x82\xac\xc3\xa9"), 0);

	/* Control characters, DEL and the five undefined bytes become '?'. */
	XvtNetworkMetadata_ToUtf8(out, sizeof out, "\x01\x1f\x7f\x81\x8d\x8f\x90\x9d", 8);
	XVT_ASSERT_INT_EQ(strcmp(out, "????????"), 0);

	/* A character that would not fit is not started; out stays terminated. */
	XvtNetworkMetadata_ToUtf8(out, 4, "a\x80", 2);
	XVT_ASSERT_INT_EQ(strcmp(out, "a"), 0);
	XvtNetworkMetadata_ToUtf8(out, 5, "a\x80", 2);
	XVT_ASSERT_INT_EQ(strcmp(out, "a\xe2\x82\xac"), 0);
	XvtNetworkMetadata_ToUtf8(out, 1, "abc", 3);
	XVT_ASSERT_INT_EQ(out[0], 0);

	/* A capacity of 0 writes nothing. */
	out[0] = 'x';
	XvtNetworkMetadata_ToUtf8(out, 0, "abc", 3);
	XVT_ASSERT_INT_EQ(out[0], 'x');
}

static void CheckFromUtf8(void) {
	char out[16];

	XvtNetworkMetadata_FromUtf8(out, sizeof out, "Hello");
	XVT_ASSERT_INT_EQ(strcmp(out, "Hello"), 0);
	XvtNetworkMetadata_FromUtf8(out, sizeof out, "\xe2\x82\xac\xc3\xa9\xc2\xa0");
	XVT_ASSERT_INT_EQ(strcmp(out, "\x80\xe9\xa0"), 0);

	/* A character 1252 cannot hold, a control character (C0, DEL, C1), a surrogate, a code point past
	 * U+10FFFF: one '?' each. */
	XvtNetworkMetadata_FromUtf8(out, sizeof out, "\xc4\x80|\x01|\x7f|\xc2\x80|\xed\xa0\x80|\xf4\x90\x80\x80");
	XVT_ASSERT_INT_EQ(strcmp(out, "?|?|?|?|?|?"), 0);

	/* Overlong forms of two, three and four bytes: '?' each, the 2-byte one through its invalid lead
	 * byte 0xC0, which with its continuation byte gives one '?' per byte. */
	XvtNetworkMetadata_FromUtf8(out, sizeof out, "\xe0\x80\x80|\xf0\x80\x80\x80|\xc0\x80");
	XVT_ASSERT_INT_EQ(strcmp(out, "?|?|??"), 0);

	/* Invalid lead bytes: one '?' per byte. */
	XvtNetworkMetadata_FromUtf8(out, sizeof out, "\xff\xfe\x80");
	XVT_ASSERT_INT_EQ(strcmp(out, "???"), 0);

	/* A sequence cut short: one '?', then the byte that broke it is read again. */
	XvtNetworkMetadata_FromUtf8(out, sizeof out,
								"\xe2\x82"
								"A");
	XVT_ASSERT_INT_EQ(strcmp(out, "?A"), 0);
	XvtNetworkMetadata_FromUtf8(out, sizeof out, "\xc3");
	XVT_ASSERT_INT_EQ(strcmp(out, "?"), 0);

	/* Truncated to capacity - 1, always terminated; a capacity of 0 writes nothing. */
	XvtNetworkMetadata_FromUtf8(out, 4, "abcdef");
	XVT_ASSERT_INT_EQ(strcmp(out, "abc"), 0);
	XvtNetworkMetadata_FromUtf8(out, 3, "\xc3\xa9\xc3\xa9\xc3\xa9");
	XVT_ASSERT_INT_EQ(strcmp(out, "\xe9\xe9"), 0);
	out[0] = 'x';
	XvtNetworkMetadata_FromUtf8(out, 0, "abc");
	XVT_ASSERT_INT_EQ(out[0], 'x');
}

static void CheckRoundTrip(void) {
	/* Every printable Windows-1252 byte survives the trip to UTF-8 and back. */
	char text[256];
	char utf8[256 * 3 + 1];
	char back[256];
	size_t count = 0;
	for (unsigned byte = 0x20; byte <= 0xff; ++byte) {
		if (byte == 0x7f || byte == 0x81 || byte == 0x8d || byte == 0x8f || byte == 0x90 || byte == 0x9d)
			continue;
		text[count++] = (char)byte;
	}
	text[count] = 0;
	XvtNetworkMetadata_ToUtf8(utf8, sizeof utf8, text, count);
	XvtNetworkMetadata_FromUtf8(back, sizeof back, utf8);
	XVT_ASSERT_INT_EQ(strlen(back), count);
	XVT_ASSERT_INT_EQ(memcmp(back, text, count), 0);
}

static void CheckBuildRoom(void) {
	ClearSession();
	memset(&g_meta, 0xAB, sizeof g_meta);
	XvtNetworkMetadata_Build(&g_meta, 1);
	XVT_ASSERT_INT_EQ(strcmp(g_meta.room.name, "Internet game."), 0);
	XVT_ASSERT_INT_EQ(g_meta.room.max_players, 8);
	XVT_ASSERT_INT_EQ(g_meta.room.password_required, 0);
	XVT_ASSERT_INT_EQ(g_meta.room.mission.present, 0);
	XVT_ASSERT_INT_EQ(g_meta.room.players, 0);
	for (int i = 0; i < 8; ++i)
		XVT_ASSERT_INT_EQ(g_meta.players[i], 0);

	/* The session name in Windows-1252, the password flag, and the selected mission. */
	strcpy(g_frontState.netSessionName, "Caf\xe9");
	g_gameConfig.requirePassword = 5;
	g_pilotData.missionDirectoryId = 3;
	g_pilotData.missionDescriptionIds[3] = 42;
	XvtNetworkMetadata_Build(&g_meta, 1);
	XVT_ASSERT_INT_EQ(strcmp(g_meta.room.name, "Caf\xc3\xa9"), 0);
	XVT_ASSERT_INT_EQ(g_meta.room.password_required, 1);
	XVT_ASSERT_INT_EQ(g_meta.room.mission.present, 1);
	XVT_ASSERT_INT_EQ(g_meta.room.mission.directory, 3);
	XVT_ASSERT_INT_EQ(g_meta.room.mission.id, 42);

	/* No mission selected in that directory. */
	g_pilotData.missionDescriptionIds[3] = -1;
	XvtNetworkMetadata_Build(&g_meta, 1);
	XVT_ASSERT_INT_EQ(g_meta.room.mission.present, 0);
	g_gameConfig.requirePassword = 0;
}

static void CheckBuildSessionRoster(void) {
	ClearSession();
	AddPlayer(101, "Alpha", 1, 3);
	AddPlayer(102, "Bravo", 0, 4);
	AddPlayer(103, "", 1, 1);
	AddPlayer(101, "Again", 1, 5);
	AddPlayer(104, "D\xe9lta", 1, 9);
	XvtNetworkMetadata_Build(&g_meta, 1);

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
	XVT_ASSERT_INT_EQ(strcmp(g_meta.room.roster[2].name, "D\xc3\xa9lta"), 0);
	XVT_ASSERT_INT_EQ(g_meta.room.roster[2].rating, 8);

	/* Joinable when accepting and a slot is free. */
	XVT_ASSERT_INT_EQ(g_meta.room.joinable, 1);
	XvtNetworkMetadata_Build(&g_meta, 0);
	XVT_ASSERT_INT_EQ(g_meta.room.joinable, 0);
}

static void CheckBuildAtMostEight(void) {
	ClearSession();
	for (DPID id = 1; id <= 10; ++id)
		AddPlayer(200 + id, "Pilot", 1, 2);
	XvtNetworkMetadata_Build(&g_meta, 1);
	XVT_ASSERT_INT_EQ(g_meta.room.players, 8);
	for (int i = 0; i < 8; ++i)
		XVT_ASSERT_INT_EQ(g_meta.players[i], 201 + i);
	/* No slot is free, so the room is not joinable even when accepting. */
	XVT_ASSERT_INT_EQ(g_meta.room.joinable, 0);
}

static void CheckBuildAuthoritativeRoster(void) {
	ClearSession();
	AddPlayer(101, "Alpha", 1, 3);
	AddPlayer(102, "Bravo", 1, 4);
	AddPlayer(103, "Charlie", 1, 5);
	AddPlayer(104, "Delta", 0, 6);
	g_missionSetupRosterAuthoritative = 1;
	g_mpRoster[0].playerId = 103;
	g_mpRoster[1].playerId = 101;
	g_mpRoster[2].playerId = 104;
	g_mpRoster[3].playerId = 999;
	XvtNetworkMetadata_Build(&g_meta, 1);

	/* The mission roster's order, ready players only; a roster id with no session player is skipped. */
	XVT_ASSERT_INT_EQ(g_meta.room.players, 2);
	XVT_ASSERT_INT_EQ(g_meta.players[0], 103);
	XVT_ASSERT_INT_EQ(g_meta.players[1], 101);
	XVT_ASSERT_INT_EQ(strcmp(g_meta.room.roster[0].name, "Charlie"), 0);
	XVT_ASSERT_INT_EQ(g_meta.room.roster[0].rating, 4);
	/* An authoritative roster is never joinable. */
	XVT_ASSERT_INT_EQ(g_meta.room.joinable, 0);
}

static void CheckFlight(void) {
	ClearSession();
	AddPlayer(101, "Alpha", 1, 3);
	AddPlayer(102, "Bravo", 1, 4);
	AddPlayer(103, "Charlie", 1, 5);
	XvtNetworkMetadata_Build(&g_meta, 0);
	XVT_ASSERT_INT_EQ(g_meta.room.players, 3);

	/* 101 and 103 are still active, in the session's other order; 102 has left. */
	g_netSession.players[0].directPlayId = 103;
	g_netSession.players[0].activeFlag = 1;
	g_netSession.players[1].directPlayId = 102;
	g_netSession.players[1].activeFlag = 0;
	g_netSession.players[5].directPlayId = 101;
	g_netSession.players[5].activeFlag = 1;
	XvtNetworkMetadata_Flight(&g_meta);
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

int main(void) {
	CheckToUtf8();
	CheckFromUtf8();
	CheckRoundTrip();
	CheckBuildRoom();
	CheckBuildSessionRoster();
	CheckBuildAtMostEight();
	CheckBuildAuthoritativeRoster();
	CheckFlight();
	return 0;
}
