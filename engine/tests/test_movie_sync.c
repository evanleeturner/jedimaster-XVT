/* Checks multiplayer movie sync (xvt_runtime/runtime/movie_sync.h) against the promises in its header: the
 * players Begin lists and what it clears, ReportFinished marking the local player and setting the host's and
 * the client's deadline once, and Update's answer and the moment it marks the deadline passed. The test sets
 * the network roster and players itself; there is no DirectPlay session, so the packets sent go nowhere and
 * no packet arrives. The host clock, which answers GetTickCount, moves only when the test advances it. Every
 * case starts from a cleared frontend, an empty roster and the clock at 0.
 *
 * Not checked here: remote players marked waiting or dropped by their packets, which need a DirectPlay
 * peer, and Draw, which needs the game's fonts. */
#include <string.h>

#include "test_assert.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/movie.h"
#include "xvt/net/net.h"
#include "xvt_runtime/runtime/movie_sync.h"
#include "xvt_runtime/timing/host_clock.h"

enum { LOCAL = 101 };

static void fresh(void)
{
	memset(&g_front_state, 0, sizeof g_front_state);
	memset(g_mp_roster, 0, sizeof g_mp_roster);
	xvt_time_reset();
}

/* Makes count players ready in the session, the first one local, listed in the same order in the roster. */
static void players(int count)
{
	for (int i = 0; i < count; ++i) {
		g_front_state.net_players[i].player_id = (DPID)(LOCAL + i);
		g_front_state.net_players[i].ready_flag = 1;
		g_mp_roster[i % 8].player_id = LOCAL + i;
	}
	g_front_state.net_player_count = count;
}

static void check_begin(void)
{
	fresh();
	players(3);
	g_mp_roster[3].player_id = 999;
	g_front_state.net_players[5].player_id = 555;
	g_front_state.net_players[5].ready_flag = 0;
	for (int i = 0; i < 8; ++i) {
		g_movie_multiplayer_sync_players[i].player_id = 7;
		g_movie_multiplayer_sync_players[i].is_waiting = 1;
	}
	g_movie_playback_completion_state = 1;
	g_movie_multiplayer_sync_deadline_ms = 1234;
	xvt_movie_sync_begin();

	/* The first three roster entries, as three players are ready; none waiting. */
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(g_movie_multiplayer_sync_players[i].player_id,
				  LOCAL + i);
		XVT_ASSERT_INT_EQ(
			g_movie_multiplayer_sync_players[i].is_waiting, 0);
	}
	for (int i = 3; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(g_movie_multiplayer_sync_players[i].player_id,
				  0);
		XVT_ASSERT_INT_EQ(
			g_movie_multiplayer_sync_players[i].is_waiting, 0);
	}
	XVT_ASSERT_INT_EQ(g_movie_playback_completion_state, 0);
	XVT_ASSERT_INT_EQ(g_movie_multiplayer_sync_deadline_ms, 0);

	/* At most 8 entries. */
	fresh();
	players(10);
	xvt_movie_sync_begin();
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(g_movie_multiplayer_sync_players[i].player_id,
				  g_mp_roster[i].player_id);
	}
}

static void check_report_finished(void)
{
	for (int host = 0; host < 2; ++host) {
		fresh();
		players(3);
		g_front_state.net_is_host = host;
		xvt_movie_sync_begin();
		xvt_time_advance_host_clock(2000 * 1000);
		xvt_movie_sync_report_finished();

		/* The local player waits; the others still watch. */
		XVT_ASSERT_INT_EQ(
			g_movie_multiplayer_sync_players[0].is_waiting, 1);
		XVT_ASSERT_INT_EQ(
			g_movie_multiplayer_sync_players[1].is_waiting, 0);
		XVT_ASSERT_INT_EQ(
			g_movie_multiplayer_sync_players[2].is_waiting, 0);
		/* 5 s out for the host, 20 s for a client. */
		XVT_ASSERT_INT_EQ(g_movie_multiplayer_sync_deadline_ms,
				  2000 + (host ? 5000 : 20000));

		/* Later calls do nothing, even with the local player's mark cleared. */
		g_movie_multiplayer_sync_players[0].is_waiting = 0;
		xvt_time_advance_host_clock(1000 * 1000);
		xvt_movie_sync_report_finished();
		XVT_ASSERT_INT_EQ(
			g_movie_multiplayer_sync_players[0].is_waiting, 0);
		XVT_ASSERT_INT_EQ(g_movie_multiplayer_sync_deadline_ms,
				  2000 + (host ? 5000 : 20000));
	}
}

static void check_tick_answer(void)
{
	fresh();
	players(2);
	xvt_movie_sync_begin();
	XVT_ASSERT_INT_EQ(xvt_movie_sync_update(), 0);
	xvt_movie_sync_report_finished();
	XVT_ASSERT_INT_EQ(xvt_movie_sync_update(), 0);
	/* Every listed player waiting. */
	g_movie_multiplayer_sync_players[1].is_waiting = 1;
	XVT_ASSERT_INT_EQ(xvt_movie_sync_update(), 1);

	/* With only the local player listed, its own ReportFinished completes the sync. */
	fresh();
	players(1);
	xvt_movie_sync_begin();
	XVT_ASSERT_INT_EQ(xvt_movie_sync_update(), 0);
	xvt_movie_sync_report_finished();
	XVT_ASSERT_INT_EQ(xvt_movie_sync_update(), 1);

	/* No listed players: nothing to wait for. */
	fresh();
	xvt_movie_sync_begin();
	XVT_ASSERT_INT_EQ(xvt_movie_sync_update(), 1);
}

static void check_deadline_passed(void)
{
	fresh();
	players(2);
	g_front_state.net_is_host = 1;
	xvt_movie_sync_begin();
	xvt_movie_sync_report_finished();
	XVT_ASSERT_INT_EQ(xvt_movie_sync_update(), 0);
	int waiting = g_movie_playback_completion_state;
	XVT_ASSERT_TRUE(waiting != 0);

	/* At the deadline nothing changes; past it the state marks the deadline passed. */
	xvt_time_advance_host_clock(5000 * 1000);
	XVT_ASSERT_INT_EQ(xvt_movie_sync_update(), 0);
	XVT_ASSERT_INT_EQ(g_movie_playback_completion_state, waiting);
	xvt_time_advance_host_clock(1000);
	XVT_ASSERT_INT_EQ(xvt_movie_sync_update(), 0);
	XVT_ASSERT_TRUE(g_movie_playback_completion_state != waiting);
	XVT_ASSERT_TRUE(g_movie_playback_completion_state != 0);

	/* Before ReportFinished there is no deadline to pass. */
	fresh();
	players(2);
	xvt_movie_sync_begin();
	xvt_time_advance_host_clock(60000 * 1000);
	xvt_movie_sync_update();
	XVT_ASSERT_INT_EQ(g_movie_playback_completion_state, 0);
}

int main(void)
{
	check_begin();
	check_report_finished();
	check_tick_answer();
	check_deadline_passed();
	return 0;
}
