#define _POSIX_C_SOURCE 200809L
/* Checks the campaign task (xvt_runtime/runtime/campaign_task.h) against the promises in its header: the
 * team-assignment prefix's immediate returns, a network client waiting for the host's continuation and
 * then, past the wait, clearing the remote battle state and leaving the game when no cutscene plays; the
 * debrief prefix's cursor, keyboard, briefing text, cutscenes and network leave; the packet wait's limit
 * and its pending flag; and Reset. The installation check passes on empty files at the names it looks for,
 * in a temporary asset folder; the host clock moves only when the test advances it. Every case starts from
 * Reset, a cleared frontend and pilot, no network session, the clock at 0, and an installed game.
 *
 * Not checked here: a continuation packet actually arriving, the single-player and host continuations
 * (they read the game's mission lists), the end of the program on a missing installation, and the renaming
 * of a promoted player, which needs a DirectPlay session. */
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "test_assert.h"
#include "test_asset_folder.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt_runtime/runtime/campaign_task.h"
#include "xvt_runtime/runtime/movie_task.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/timing/host_clock.h"

static struct xvt_test_assets g_assets;
static struct cutscene_entry g_table[1];

static int placeholder(int frame) { return frame; }

static void fresh(int session_mode)
{
	xvt_campaign_task_reset();
	xvt_movie_task_shutdown();
	xvt_network_session_shutdown();
	xvt_test_close_assets(&g_assets);
	xvt_time_reset();
	memset(&g_front_state, 0, sizeof g_front_state);
	memset(&g_pilot_data, 0, sizeof g_pilot_data);
	g_front_state.screen_states[0].update_fn = placeholder;
	g_frontend_mission_session_mode = session_mode;
	g_frontend_skip_screen_entry_setup = 0;
	g_mission_setup_debrief_transition = 0;
	g_cutscene_table = NULL;
	g_cutscene_count = 0;
	free(g_mission_text);
	g_mission_text = NULL;
	g_remote_battle_continuation_active = 0;
	g_remote_battle_sequence_continuation_choice = 0;
	g_remote_battle_last_completed_mission_index = 0;
	g_remote_battle_rebel_victory_count = 0;
	g_remote_battle_imperial_victory_count = 0;
	xvt_test_open_assets(&g_assets);
	xvt_test_add_asset(&g_assets, "wave/PBC/Pb1los07.wav");
	xvt_test_add_asset(&g_assets, "ivfiles/cal.opt");
	xvt_test_add_asset(&g_assets, "train/1ta01bf.tie");
}

/* A remote battle in progress, which a failed continuation clears. */
static void remote_battle(void)
{
	g_remote_battle_continuation_active = 1;
	g_remote_battle_sequence_continuation_choice = 2;
	g_remote_battle_last_completed_mission_index = 3;
	g_remote_battle_rebel_victory_count = 4;
	g_remote_battle_imperial_victory_count = 5;
}

static void check_remote_battle_cleared(void)
{
	XVT_ASSERT_INT_EQ(g_remote_battle_continuation_active, 0);
	XVT_ASSERT_INT_EQ(g_remote_battle_sequence_continuation_choice, 0);
	XVT_ASSERT_INT_EQ(g_remote_battle_last_completed_mission_index, 0);
	XVT_ASSERT_INT_EQ(g_remote_battle_rebel_victory_count, 0);
	XVT_ASSERT_INT_EQ(g_remote_battle_imperial_victory_count, 0);
}

static frontend_screen_update_fn top_screen(void)
{
	return g_front_state.screen_states[g_front_state.screen_stack_top]
		.update_fn;
}

static void advance_ms(int ms) { xvt_time_advance_host_clock(ms * 1000); }

static void check_wait_packet_limit(void)
{
	int dummy = 0;
	int *packet = &dummy;
	fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_continues_without_focus(), 0);

	/* The wait starts with its first call, here 5 s after the clock's start. */
	advance_ms(5000);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_wait_packet(
				  NET_PACKET_CAMPAIGN_CONTINUATION, &packet),
			  XVT_CAMPAIGN_PENDING);
	XVT_ASSERT_TRUE(packet == NULL);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_continues_without_focus(), 1);

	/* Exactly 30 s later it still waits; past that it gives up with 0. */
	advance_ms(30000);
	packet = &dummy;
	XVT_ASSERT_INT_EQ(xvt_campaign_task_wait_packet(
				  NET_PACKET_CAMPAIGN_CONTINUATION, &packet),
			  XVT_CAMPAIGN_PENDING);
	XVT_ASSERT_TRUE(packet == NULL);
	advance_ms(1);
	packet = &dummy;
	XVT_ASSERT_INT_EQ(xvt_campaign_task_wait_packet(
				  NET_PACKET_CAMPAIGN_CONTINUATION, &packet),
			  0);
	XVT_ASSERT_TRUE(packet == NULL);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_continues_without_focus(), 0);

	/* The next call starts a new wait. */
	XVT_ASSERT_INT_EQ(xvt_campaign_task_wait_packet(
				  NET_PACKET_BATTLE_CONTINUATION, &packet),
			  XVT_CAMPAIGN_PENDING);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_continues_without_focus(), 1);
}

static void check_reset_forgets_wait(void)
{
	int *packet = NULL;
	fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_wait_packet(
				  NET_PACKET_CAMPAIGN_CONTINUATION, &packet),
			  XVT_CAMPAIGN_PENDING);
	advance_ms(20000);
	xvt_campaign_task_reset();
	XVT_ASSERT_INT_EQ(xvt_campaign_task_continues_without_focus(), 0);
	/* A wait after Reset starts its own 30 s: 20 s on, the old limit is past but the new one is not. */
	XVT_ASSERT_INT_EQ(xvt_campaign_task_wait_packet(
				  NET_PACKET_CAMPAIGN_CONTINUATION, &packet),
			  XVT_CAMPAIGN_PENDING);
	advance_ms(20000);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_wait_packet(
				  NET_PACKET_CAMPAIGN_CONTINUATION, &packet),
			  XVT_CAMPAIGN_PENDING);
}

static void check_teams_return_at_once(void)
{
	/* No mission sequence. */
	fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	g_pilot_data.mission_directory_id =
		MISSION_DIRECTORY_TRAINING_EXERCISES;
	XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_teams(), 1);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_is_pending(), 0);

	/* A sequence, but the entry movie is skipped, or the debrief chose to enter the current mission. */
	fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	g_pilot_data.mission_sequence_active = 1;
	g_frontend_skip_screen_entry_setup = 1;
	XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_teams(), 1);
	fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	g_pilot_data.mission_sequence_active = 1;
	g_mission_setup_debrief_transition =
		MISSION_SETUP_DEBRIEF_TRANSITION_ENTER_CURRENT_MISSION;
	XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_teams(), 1);

	/* A sequence in a directory other than training or combat. */
	fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	g_pilot_data.mission_sequence_active = 1;
	g_pilot_data.mission_directory_id = MISSION_DIRECTORY_MELEES;
	XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_teams(), 1);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_is_pending(), 0);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_continues_without_focus(), 0);
}

static void check_teams_campaign_client(void)
{
	fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	g_pilot_data.mission_sequence_active = 1;
	g_pilot_data.mission_directory_id =
		MISSION_DIRECTORY_TRAINING_EXERCISES;
	remote_battle();

	/* The client waits for the host's continuation. */
	XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_teams(),
			  XVT_CAMPAIGN_PENDING);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_is_pending(), 1);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_continues_without_focus(), 1);
	advance_ms(1000);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_teams(),
			  XVT_CAMPAIGN_PENDING);
	XVT_ASSERT_TRUE(top_screen() == placeholder);

	/* None comes: the continuation returns 0 and the remote battle state is cleared. With no cutscene
	 * table the cutscene result is 0, so the client leaves: session shut down, join screen, 0. */
	advance_ms(30000);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_teams(), 0);
	check_remote_battle_cleared();
	XVT_ASSERT_TRUE(top_screen() == frontend_net_join_game_screen);
	XVT_ASSERT_INT_EQ(xvt_network_session_get_status().state,
			  XVT_NETWORK_SESSION_PENDING);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_is_pending(), 0);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_continues_without_focus(), 0);
}

static void check_teams_battle_client(void)
{
	fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	g_pilot_data.mission_sequence_active = 1;
	g_pilot_data.mission_directory_id =
		MISSION_DIRECTORY_COMBAT_ENGAGEMENTS;
	remote_battle();
	XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_teams(),
			  XVT_CAMPAIGN_PENDING);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_is_pending(), 1);

	/* A battle plays no cutscenes: once the wait gives up, the state is cleared and the prefix is done. */
	advance_ms(30001);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_teams(), 1);
	check_remote_battle_cleared();
	XVT_ASSERT_TRUE(top_screen() == placeholder);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_is_pending(), 0);
}

static void check_reset_forgets_prefix(void)
{
	fresh(FRONTEND_MISSION_SESSION_NET_CLIENT);
	g_pilot_data.mission_sequence_active = 1;
	g_pilot_data.mission_directory_id =
		MISSION_DIRECTORY_COMBAT_ENGAGEMENTS;
	XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_teams(),
			  XVT_CAMPAIGN_PENDING);
	xvt_campaign_task_reset();
	XVT_ASSERT_INT_EQ(xvt_campaign_task_is_pending(), 0);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_continues_without_focus(), 0);
	/* The next call starts the prefix over, with a fresh 30 s wait. */
	advance_ms(29000);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_teams(),
			  XVT_CAMPAIGN_PENDING);
	advance_ms(29000);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_teams(),
			  XVT_CAMPAIGN_PENDING);
}

static void check_debrief_outside_training(void)
{
	fresh(FRONTEND_MISSION_SESSION_SINGLEPLAYER);
	g_pilot_data.mission_directory_id = MISSION_DIRECTORY_MELEES;
	g_pilot_data.mission_sequence_active = 1;
	g_front_state.cursor_visible = 0;
	g_front_state.char_ring_buffer[0] = 'k';
	g_front_state.char_write_idx = 1;
	XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_debrief(), 1);
	XVT_ASSERT_INT_EQ(g_front_state.cursor_visible, 1);
	XVT_ASSERT_INT_EQ(g_front_state.char_read_idx,
			  g_front_state.char_write_idx);
	/* No briefing text outside a training sequence. */
	XVT_ASSERT_TRUE(g_mission_text == NULL);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_is_pending(), 0);
}

static void check_debrief_training_sequence(void)
{
	/* An active training sequence gets its briefing text, whether or not the mission was completed. */
	for (int completed = 0; completed < 2; ++completed) {
		fresh(FRONTEND_MISSION_SESSION_SINGLEPLAYER);
		g_pilot_data.mission_directory_id =
			MISSION_DIRECTORY_TRAINING_EXERCISES;
		g_pilot_data.mission_sequence_active = 1;
		g_pilot_data.campaign_sequence_state.last_mission_completed =
			completed;
		XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_debrief(), 1);
		XVT_ASSERT_TRUE(g_mission_text != NULL);
		XVT_ASSERT_TRUE(top_screen() == placeholder);
	}
}

static void check_debrief_network_leaves(void)
{
	/* A completed training mission in a network session with no cutscene table: the cutscene result is
	 * 0, so the player leaves for the concourse. */
	fresh(FRONTEND_MISSION_SESSION_NET_HOST);
	g_pilot_data.mission_directory_id =
		MISSION_DIRECTORY_TRAINING_EXERCISES;
	g_pilot_data.mission_sequence_active = 1;
	g_pilot_data.campaign_sequence_state.last_mission_completed = 1;
	XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_debrief(), 0);
	XVT_ASSERT_TRUE(top_screen() == concourse_update);
	XVT_ASSERT_INT_EQ(xvt_network_session_get_status().state,
			  XVT_NETWORK_SESSION_PENDING);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_is_pending(), 0);

	/* An incomplete mission plays no cutscenes, so the network player stays. */
	fresh(FRONTEND_MISSION_SESSION_NET_HOST);
	g_pilot_data.mission_directory_id =
		MISSION_DIRECTORY_TRAINING_EXERCISES;
	g_pilot_data.mission_sequence_active = 1;
	XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_debrief(), 1);
	XVT_ASSERT_TRUE(top_screen() == placeholder);
}

static void check_debrief_waits_for_cutscene(void)
{
	fresh(FRONTEND_MISSION_SESSION_SINGLEPLAYER);
	g_pilot_data.mission_directory_id =
		MISSION_DIRECTORY_TRAINING_EXERCISES;
	g_pilot_data.mission_sequence_active = 1;
	g_pilot_data.campaign_sequence_state.last_mission_completed = 1;
	memset(g_table, 0, sizeof g_table);
	strcpy(g_table[0].movie_name, "debrief");
	g_table[0].play_after_debriefing = 1;
	g_cutscene_table = g_table;
	g_cutscene_count = 1;
	xvt_test_add_asset(&g_assets, "movies/debrief.smk");

	XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_debrief(),
			  XVT_CAMPAIGN_PENDING);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_is_pending(), 1);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_debrief(),
			  XVT_CAMPAIGN_PENDING);

	/* The movie fails on the empty file; single-player, the debrief goes on and finishes. */
	struct timespec pause = {0, 1000000};
	for (int i = 0; i < 10000 && xvt_movie_task_is_active(); ++i) {
		xvt_movie_task_update();
		nanosleep(&pause, NULL);
	}
	xvt_movie_task_reap_finished();
	XVT_ASSERT_INT_EQ(xvt_campaign_task_enter_debrief(), 1);
	XVT_ASSERT_INT_EQ(xvt_campaign_task_is_pending(), 0);
	XVT_ASSERT_TRUE(g_mission_text != NULL);
	g_cutscene_table = NULL;
	g_cutscene_count = 0;
}

int main(void)
{
	check_wait_packet_limit();
	check_reset_forgets_wait();
	check_teams_return_at_once();
	check_teams_campaign_client();
	check_teams_battle_client();
	check_reset_forgets_prefix();
	check_debrief_outside_training();
	check_debrief_training_sequence();
	check_debrief_network_leaves();
	check_debrief_waits_for_cutscene();
	xvt_campaign_task_reset();
	xvt_movie_task_shutdown();
	xvt_network_session_shutdown();
	xvt_test_close_assets(&g_assets);
	free(g_mission_text);
	g_mission_text = NULL;
	return 0;
}
