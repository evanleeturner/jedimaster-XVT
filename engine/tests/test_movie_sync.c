/* Checks multiplayer movie sync (xvt_runtime/runtime/movie_sync.h) against the promises in its header:
 * the players Begin lists and what it clears, Wait marking the local player and setting the host's and the
 * client's deadline once, and Update's answer and the moment it marks the deadline passed. The test sets the
 * network roster and players itself; there is no DirectPlay session, so the packets sent go nowhere and no
 * packet arrives. The host clock, which answers GetTickCount, moves only when the test advances it. Every
 * case starts from a cleared frontend, an empty roster and the clock at 0.
 *
 * Not checked here: remote players marked waiting or dropped by their packets, which need a DirectPlay
 * peer, and Draw, which needs the game's fonts. */
#include "test_assert.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/movie.h"
#include "xvt/net/net.h"
#include "xvt_runtime/runtime/movie_sync.h"
#include "xvt_runtime/timing/host_clock.h"

#include <string.h>

enum { LOCAL = 101 };

static void Fresh(void) {
	memset(&g_frontState, 0, sizeof g_frontState);
	memset(g_mpRoster, 0, sizeof g_mpRoster);
	XvtTime_Reset();
}

/* Makes count players ready in the session, the first one local, listed in the same order in the roster. */
static void Players(int count) {
	for (int i = 0; i < count; ++i) {
		g_frontState.netPlayers[i].playerId = (DPID)(LOCAL + i);
		g_frontState.netPlayers[i].readyFlag = 1;
		g_mpRoster[i % 8].playerId = LOCAL + i;
	}
	g_frontState.netPlayerCount = count;
}

static void CheckBegin(void) {
	Fresh();
	Players(3);
	g_mpRoster[3].playerId = 999;
	g_frontState.netPlayers[5].playerId = 555;
	g_frontState.netPlayers[5].readyFlag = 0;
	for (int i = 0; i < 8; ++i) {
		g_movieMultiplayerSyncPlayers[i].playerId = 7;
		g_movieMultiplayerSyncPlayers[i].isWaiting = 1;
	}
	g_moviePlaybackCompletionState = 1;
	g_movieMultiplayerSyncDeadlineMs = 1234;
	XvtMovieSync_Begin();

	/* The first three roster entries, as three players are ready; none waiting. */
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(g_movieMultiplayerSyncPlayers[i].playerId, LOCAL + i);
		XVT_ASSERT_INT_EQ(g_movieMultiplayerSyncPlayers[i].isWaiting, 0);
	}
	for (int i = 3; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(g_movieMultiplayerSyncPlayers[i].playerId, 0);
		XVT_ASSERT_INT_EQ(g_movieMultiplayerSyncPlayers[i].isWaiting, 0);
	}
	XVT_ASSERT_INT_EQ(g_moviePlaybackCompletionState, 0);
	XVT_ASSERT_INT_EQ(g_movieMultiplayerSyncDeadlineMs, 0);

	/* At most 8 entries. */
	Fresh();
	Players(10);
	XvtMovieSync_Begin();
	for (int i = 0; i < 8; ++i)
		XVT_ASSERT_INT_EQ(g_movieMultiplayerSyncPlayers[i].playerId, g_mpRoster[i].playerId);
}

static void CheckWait(void) {
	for (int host = 0; host < 2; ++host) {
		Fresh();
		Players(3);
		g_frontState.netIsHost = host;
		XvtMovieSync_Begin();
		XvtTime_AdvanceHostClock(2000 * 1000);
		XvtMovieSync_Wait();

		/* The local player waits; the others still watch. */
		XVT_ASSERT_INT_EQ(g_movieMultiplayerSyncPlayers[0].isWaiting, 1);
		XVT_ASSERT_INT_EQ(g_movieMultiplayerSyncPlayers[1].isWaiting, 0);
		XVT_ASSERT_INT_EQ(g_movieMultiplayerSyncPlayers[2].isWaiting, 0);
		/* 5 s out for the host, 20 s for a client. */
		XVT_ASSERT_INT_EQ(g_movieMultiplayerSyncDeadlineMs, 2000 + (host ? 5000 : 20000));

		/* Later calls do nothing, even with the local player's mark cleared. */
		g_movieMultiplayerSyncPlayers[0].isWaiting = 0;
		XvtTime_AdvanceHostClock(1000 * 1000);
		XvtMovieSync_Wait();
		XVT_ASSERT_INT_EQ(g_movieMultiplayerSyncPlayers[0].isWaiting, 0);
		XVT_ASSERT_INT_EQ(g_movieMultiplayerSyncDeadlineMs, 2000 + (host ? 5000 : 20000));
	}
}

static void CheckTickAnswer(void) {
	Fresh();
	Players(2);
	XvtMovieSync_Begin();
	XVT_ASSERT_INT_EQ(XvtMovieSync_Update(), 0);
	XvtMovieSync_Wait();
	XVT_ASSERT_INT_EQ(XvtMovieSync_Update(), 0);
	/* Every listed player waiting. */
	g_movieMultiplayerSyncPlayers[1].isWaiting = 1;
	XVT_ASSERT_INT_EQ(XvtMovieSync_Update(), 1);

	/* With only the local player listed, its own Wait completes the sync. */
	Fresh();
	Players(1);
	XvtMovieSync_Begin();
	XVT_ASSERT_INT_EQ(XvtMovieSync_Update(), 0);
	XvtMovieSync_Wait();
	XVT_ASSERT_INT_EQ(XvtMovieSync_Update(), 1);

	/* No listed players: nothing to wait for. */
	Fresh();
	XvtMovieSync_Begin();
	XVT_ASSERT_INT_EQ(XvtMovieSync_Update(), 1);
}

static void CheckDeadlinePassed(void) {
	Fresh();
	Players(2);
	g_frontState.netIsHost = 1;
	XvtMovieSync_Begin();
	XvtMovieSync_Wait();
	XVT_ASSERT_INT_EQ(XvtMovieSync_Update(), 0);
	int waiting = g_moviePlaybackCompletionState;
	XVT_ASSERT_TRUE(waiting != 0);

	/* At the deadline nothing changes; past it the state marks the deadline passed. */
	XvtTime_AdvanceHostClock(5000 * 1000);
	XVT_ASSERT_INT_EQ(XvtMovieSync_Update(), 0);
	XVT_ASSERT_INT_EQ(g_moviePlaybackCompletionState, waiting);
	XvtTime_AdvanceHostClock(1000);
	XVT_ASSERT_INT_EQ(XvtMovieSync_Update(), 0);
	XVT_ASSERT_TRUE(g_moviePlaybackCompletionState != waiting);
	XVT_ASSERT_TRUE(g_moviePlaybackCompletionState != 0);

	/* Before Wait there is no deadline to pass. */
	Fresh();
	Players(2);
	XvtMovieSync_Begin();
	XvtTime_AdvanceHostClock(60000 * 1000);
	XvtMovieSync_Update();
	XVT_ASSERT_INT_EQ(g_moviePlaybackCompletionState, 0);
}

int main(void) {
	CheckBegin();
	CheckWait();
	CheckTickAnswer();
	CheckDeadlinePassed();
	return 0;
}
