/* Checks the network session (xvt_runtime/runtime/network_session.h) against the promises in its header,
 * as far as they hold without a peer or a directory: the player-name split, the refusals of Configure and
 * of the Begin calls, the status a setup reports, the lost mark, a close and its completion, Reset and
 * Shutdown. No setup is ticked past its first phase, so no DirectPlay session or directory request is ever
 * made. Every case starts from Shutdown with no settings loaded.
 *
 * Not checked here: the setup phases past the close, admission, registration, the listing Service keeps
 * and the flight marks; they need a DirectPlay peer and a multiplayer directory. */
#include "test_assert.h"
#include "xvt/net/net_reliable.h"
#include "xvt_runtime/runtime/network_session.h"

#include <string.h>

static const GUID g_room = { 1, 2, 3, { 4, 5, 6, 7, 8, 9, 10, 11 } };

static NetPlayerNameMessage g_message;

static XvtNetworkSessionState State(void) { return XvtNetworkSession_GetStatus().state; }

static AeronDplayDirectoryError Error(void) { return XvtNetworkSession_GetStatus().error; }

static void Fresh(void) {
	XvtNetworkSession_Shutdown();
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_IDLE);
}

/* Completes the close every Begin starts with; no DirectPlay session is open in these tests. */
static void FinishClose(void) { XvtNetworkSession_Service(); }

static void CheckCopyPlayerNames(void) {
	char short_name[16];
	char long_name[16];
	memset(&g_message, 0, sizeof g_message);
	memcpy(g_message.names, "Luke\0Skywalker", sizeof "Luke\0Skywalker");
	XVT_ASSERT_INT_EQ(XvtNetworkSession_CopyPlayerNames(&g_message, short_name, sizeof short_name, long_name,
														sizeof long_name),
					  1);
	XVT_ASSERT_INT_EQ(strcmp(short_name, "Luke"), 0);
	XVT_ASSERT_INT_EQ(strcmp(long_name, "Skywalker"), 0);

	/* Truncated to fit, each terminated. */
	XVT_ASSERT_INT_EQ(XvtNetworkSession_CopyPlayerNames(&g_message, short_name, 3, long_name, 4), 1);
	XVT_ASSERT_INT_EQ(strcmp(short_name, "Lu"), 0);
	XVT_ASSERT_INT_EQ(strcmp(long_name, "Sky"), 0);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_CopyPlayerNames(&g_message, short_name, 1, long_name, 1), 1);
	XVT_ASSERT_INT_EQ(short_name[0], 0);
	XVT_ASSERT_INT_EQ(long_name[0], 0);

	/* NULL and zero-capacity arguments. */
	XVT_ASSERT_INT_EQ(XvtNetworkSession_CopyPlayerNames(NULL, short_name, 16, long_name, 16), 0);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_CopyPlayerNames(&g_message, NULL, 16, long_name, 16), 0);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_CopyPlayerNames(&g_message, short_name, 16, NULL, 16), 0);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_CopyPlayerNames(&g_message, short_name, 0, long_name, 16), 0);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_CopyPlayerNames(&g_message, short_name, 16, long_name, 0), 0);

	/* A long name without its terminator, then no terminator at all. */
	memset(g_message.names, 'x', sizeof g_message.names);
	g_message.names[1] = 0;
	XVT_ASSERT_INT_EQ(XvtNetworkSession_CopyPlayerNames(&g_message, short_name, 16, long_name, 16), 0);
	memset(g_message.names, 'x', sizeof g_message.names);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_CopyPlayerNames(&g_message, short_name, 16, long_name, 16), 0);

	/* Both terminators at the very end: an empty long name in the last byte. */
	memset(g_message.names, 'x', sizeof g_message.names);
	g_message.names[sizeof g_message.names - 2] = 0;
	g_message.names[sizeof g_message.names - 1] = 0;
	XVT_ASSERT_INT_EQ(XvtNetworkSession_CopyPlayerNames(&g_message, short_name, 16, long_name, 16), 1);
	XVT_ASSERT_INT_EQ(strlen(short_name), 15);
	XVT_ASSERT_INT_EQ(long_name[0], 0);
}

static void CheckConfigureNeedsALobby(void) {
	Fresh();
	/* With no settings loaded the lobby URL is empty. */
	XVT_ASSERT_INT_EQ(XvtNetworkSession_Configure(), AERON_DPLAY_DIRECTORY_ERROR_NOT_CONFIGURED);
}

static void CheckBeginRefusals(void) {
	char too_long[64];
	memset(too_long, 'n', sizeof too_long);
	too_long[63] = 0;

	struct {
		const char* info;
		const char* player;
		const char* name;
	} hosts[] = {
		{ NULL, "Luke", "" },
		{ "\x02", NULL, "" },
		{ "\x02", "Luke", NULL },
		{ "0123456789abcdef", "Luke", "" },
		{ "\x02", "0123456789abcdef", "" },
		{ "\x02", "Luke", too_long },
	};

	for (unsigned i = 0; i < sizeof hosts / sizeof hosts[0]; ++i) {
		Fresh();
		XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginHost(hosts[i].info, hosts[i].player, hosts[i].name, 0), 0);
		XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_FAILED);
		XVT_ASSERT_INT_EQ(Error(), AERON_DPLAY_DIRECTORY_ERROR_INVALID_REQUEST);
	}

	/* A join needs a room as well. */
	Fresh();
	XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginJoin("\x02", "Luke", NULL), 0);
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_FAILED);
	XVT_ASSERT_INT_EQ(Error(), AERON_DPLAY_DIRECTORY_ERROR_INVALID_REQUEST);
	Fresh();
	XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginJoin("0123456789abcdef", "Luke", &g_room), 0);
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_FAILED);

	/* The longest arguments that fit are accepted: 15, 15 and 31 characters. */
	Fresh();
	too_long[31] = 0;
	XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginHost("0123456789abcde", "0123456789abcde", too_long, 0),
					  XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_PENDING);
}

static void CheckBeginWhileUnderWay(void) {
	Fresh();
	XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginHost("\x02", "Luke", "", 0), XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_PENDING);

	/* Another Begin while setup is under way returns -1 and does nothing: bad arguments do not fail it. */
	XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginHost(NULL, NULL, NULL, 0), XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginJoin(NULL, NULL, NULL), XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_PENDING);
	XVT_ASSERT_INT_EQ(Error(), AERON_DPLAY_DIRECTORY_ERROR_NONE);

	/* A failed session can be started again. */
	Fresh();
	XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginJoin("\x02", "Luke", NULL), 0);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginJoin("\x02", "Luke", &g_room), XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_PENDING);
}

static void CheckTick(void) {
	Fresh();
	XVT_ASSERT_INT_EQ(XvtNetworkSession_Update(), 0);

	/* A failed session ticks as 0. */
	XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginHost(NULL, "Luke", "", 0), 0);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_Update(), 0);
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_FAILED);

	/* While the previous session's close completes, setup is pending. */
	Fresh();
	XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginHost("\x02", "Luke", "", 0), XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_Update(), XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_PENDING);
}

static void CheckLost(void) {
	Fresh();
	XVT_ASSERT_INT_EQ(XvtNetworkSession_IsLost(), 0);
	XvtNetworkSession_HostLost();
	XVT_ASSERT_INT_EQ(XvtNetworkSession_IsLost(), 1);
	XvtNetworkSession_HostLost();
	XVT_ASSERT_INT_EQ(XvtNetworkSession_IsLost(), 1);

	/* Outside flight, the next Update fails a lost session. */
	Fresh();
	XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginHost("\x02", "Luke", "", 0), XVT_NETWORK_PENDING);
	XvtNetworkSession_HostLost();
	XVT_ASSERT_INT_EQ(XvtNetworkSession_Update(), 0);
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_FAILED);

	/* ...and so does the next Service. */
	Fresh();
	XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginJoin("\x02", "Luke", &g_room), XVT_NETWORK_PENDING);
	FinishClose();
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_PENDING);
	XvtNetworkSession_HostLost();
	XvtNetworkSession_Service();
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_FAILED);
}

static void CheckClose(void) {
	Fresh();
	XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginHost("\x02", "Luke", "", 0), XVT_NETWORK_PENDING);
	XvtNetworkSession_HostLost();

	/* OnClose returns to idle with a close pending, and clears the lost mark. */
	XvtNetworkSession_OnClose();
	XVT_ASSERT_INT_EQ(XvtNetworkSession_IsLost(), 0);
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_PENDING);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_Update(), 0);

	/* Service finishes the close once DirectPlay is inactive. */
	FinishClose();
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_IDLE);

	/* Leave and Cancel shut the DirectPlay session down, which closes the session. */
	XvtNetworkSession_Leave();
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_PENDING);
	FinishClose();
	XvtNetworkSession_Cancel();
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_PENDING);
	FinishClose();
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_IDLE);
}

static void CheckResetKeepsShutdownClears(void) {
	Fresh();
	XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginHost(NULL, "Luke", "", 0), 0);
	XVT_ASSERT_INT_EQ(Error(), AERON_DPLAY_DIRECTORY_ERROR_INVALID_REQUEST);

	/* Reset leaves the session but keeps its state: the last failure's error stays. */
	XvtNetworkSession_Reset();
	FinishClose();
	XVT_ASSERT_INT_EQ(Error(), AERON_DPLAY_DIRECTORY_ERROR_INVALID_REQUEST);

	/* Shutdown forgets it. */
	XvtNetworkSession_HostLost();
	XvtNetworkSession_Shutdown();
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_IDLE);
	XVT_ASSERT_INT_EQ(Error(), AERON_DPLAY_DIRECTORY_ERROR_NONE);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_IsLost(), 0);
}

static void CheckAdmissionOutsideAdmission(void) {
	Fresh();
	XVT_ASSERT_INT_EQ(XvtNetworkSession_AcceptAdmission(0, 0), 0);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_BeginJoin("\x02", "Luke", &g_room), XVT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(XvtNetworkSession_AcceptAdmission(0, 0), 0);
	XVT_ASSERT_INT_EQ(State(), XVT_NETWORK_SESSION_PENDING);
}

int main(void) {
	CheckCopyPlayerNames();
	CheckConfigureNeedsALobby();
	CheckBeginRefusals();
	CheckBeginWhileUnderWay();
	CheckTick();
	CheckLost();
	CheckClose();
	CheckResetKeepsShutdownClears();
	CheckAdmissionOutsideAdmission();
	XvtNetworkSession_Shutdown();
	return 0;
}
