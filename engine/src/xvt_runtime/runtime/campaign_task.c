#include "xvt_runtime/runtime/campaign_task.h"

#include <stdlib.h>
#include <string.h>

#include "aeron/aeron.h"
#include "xvt/assets/file.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/mission_debrief.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt/net/net_send.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/cutscene_task.h"
#include "xvt_runtime/runtime/movie_task.h"
#include "xvt_runtime/storage/storage.h"
#include "xvt_runtime/timing/host_clock.h"

enum {
	XVT_CAMPAIGN_IDLE,
	XVT_CAMPAIGN_SEQUENCE,
	XVT_CAMPAIGN_MOVIE,
	XVT_CAMPAIGN_DEBRIEF_MOVIE,
	XVT_CAMPAIGN_DEBRIEF_ROSTER,
	XVT_CAMPAIGN_DEBRIEF_RENAME
};

static struct {
	int phase;
	int is_campaign;
	int pending;
	int packet_type;
	uint32_t wait_start;
	char rating_long_name[16];
} g_campaign;

int xvt_campaign_task_wait_packet(int packet_type, int **packet)
{
	uint32_t now = xvt_time_get_elapsed_ms();
	*packet = NULL;
	if (!g_campaign.packet_type) {
		g_campaign.packet_type = packet_type;
		g_campaign.wait_start = now;
	}
	DPID sender;
	uint32_t size;
	/* Ingress is serviced by the frontend host tick; consume a finite packet slice. */
	for (int count = 0; count < 32; ++count) {
		int *candidate = net_get_next_app_packet(&sender, &size);
		if (candidate && size >= 2 * sizeof(int) &&
		    candidate[0] == packet_type) {
			size_t required =
				candidate[1] == 0
					? 2 * sizeof(int)
					: 3 * sizeof(int) +
						  (packet_type == NET_PACKET_BATTLE_CONTINUATION
							   ? sizeof(struct
								    battle_sequence_state)
							   : sizeof(struct
								    campaign_sequence_state));
			if (size >= required) {
				*packet = candidate;
				g_campaign.packet_type = NET_PACKET_NONE;
				return 1;
			}
		}
		/* Preserve the original expected-packet-before-timeout ordering
		 * and strict limit. */
		if ((uint32_t)(now - g_campaign.wait_start) > 30000) {
			g_campaign.packet_type = NET_PACKET_NONE;
			XVT_LOG_WARN("campaign.packet_timeout type=%d",
				     packet_type);
			return 0;
		}
		if (!candidate) {
			break;
		}
	}
	return XVT_CAMPAIGN_PENDING;
}

static int xvt_campaign_task_finish(int result)
{
	g_campaign.phase = XVT_CAMPAIGN_IDLE;
	g_campaign.pending = 0;
	return result;
}

int xvt_campaign_task_enter_teams(void)
{
	if (g_campaign.phase == XVT_CAMPAIGN_IDLE) {
		g_frontend_chat_team_only = 0;
		g_mission_setup_show_description_panel = 0;
		g_frontend_first_visible_line = 0;
		if (!file_check_game_cd_present(g_skip_movie_checks)) {
			xvt_storage_fatal(
				"Cannot load required flight/voice files", 1);
		}
		frontend_check_host_cd_present();
		if (!g_host_cd_available &&
		    g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_NET_CLIENT) {
			xvt_storage_fatal(
				"Cannot load required training mission", 1);
		}
		if (g_frontend_skip_screen_entry_setup ||
		    g_mission_setup_debrief_transition ==
			    MISSION_SETUP_DEBRIEF_TRANSITION_ENTER_CURRENT_MISSION ||
		    g_pilot_data.mission_sequence_active != 1) {
			return 1;
		}
		g_campaign.is_campaign = g_pilot_data.mission_directory_id ==
					 MISSION_DIRECTORY_TRAINING_EXERCISES;
		if (!g_campaign.is_campaign &&
		    g_pilot_data.mission_directory_id !=
			    MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
			return 1;
		}
		g_campaign.phase = XVT_CAMPAIGN_SEQUENCE;
	}
	int result;
	if (g_campaign.phase == XVT_CAMPAIGN_SEQUENCE) {
		result = g_campaign.is_campaign
				 ? mission_setup_try_continue_campaign()
				 : mission_setup_try_continue_battle();
		if (result == XVT_CAMPAIGN_PENDING) {
			g_campaign.pending = 1;
			return result;
		}
		if (result == 0) {
			g_remote_battle_continuation_active = 0;
			g_remote_battle_sequence_continuation_choice = 0;
			g_remote_battle_last_completed_mission_index = 0;
			g_remote_battle_rebel_victory_count = 0;
			g_remote_battle_imperial_victory_count = 0;
		}
		if (!g_campaign.is_campaign) {
			return xvt_campaign_task_finish(1);
		}
		g_campaign.phase = XVT_CAMPAIGN_MOVIE;
	}
	result = cutscene_play_for_current_mission_phase(0);
	if (result == XVT_MOVIE_PENDING) {
		g_campaign.pending = 1;
		return XVT_CAMPAIGN_PENDING;
	}
	if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
	    result == 0) {
		g_frontend_net_packet_scratch.packet_type =
			NET_PACKET_PLAYER_LEFT;
		net_send_packet_and_flush(net_get_host_player_id(),
					  &g_frontend_net_packet_scratch,
					  sizeof(int));
		net_shutdown_direct_play_session();
		frontend_screen_set_callbacks(
			frontend_net_join_game_screen,
			frontend_mission_list_free_screen_resources);
		return xvt_campaign_task_finish(0);
	}
	return xvt_campaign_task_finish(1);
}

int xvt_campaign_task_enter_debrief(void)
{
	if (g_campaign.phase == XVT_CAMPAIGN_IDLE) {
		frontend_cursor_show();
		keyboard_flush_char_buffer();
		if (g_pilot_data.mission_directory_id !=
			    MISSION_DIRECTORY_TRAINING_EXERCISES ||
		    g_pilot_data.mission_sequence_active != 1 ||
		    !g_pilot_data.campaign_sequence_state
			     .last_mission_completed) {
			g_campaign.phase = XVT_CAMPAIGN_DEBRIEF_ROSTER;
		} else {
			g_campaign.phase = XVT_CAMPAIGN_DEBRIEF_MOVIE;
		}
	}
	int result;
	if (g_campaign.phase == XVT_CAMPAIGN_DEBRIEF_MOVIE) {
		result = cutscene_play_for_current_mission_phase(1);
		if (result == XVT_MOVIE_PENDING) {
			g_campaign.pending = 1;
			return XVT_CAMPAIGN_PENDING;
		}
		if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER &&
		    result == 0) {
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_PLAYER_LEFT;
			net_send_packet_and_flush(
				net_get_host_player_id(),
				&g_frontend_net_packet_scratch, sizeof(int));
			net_shutdown_direct_play_session();
			frontend_screen_set_callbacks(concourse_update,
						      concourse_exit);
			return xvt_campaign_task_finish(0);
		}
		g_campaign.phase = XVT_CAMPAIGN_DEBRIEF_ROSTER;
	}
	if (g_campaign.phase == XVT_CAMPAIGN_DEBRIEF_ROSTER) {
		if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_TRAINING_EXERCISES &&
		    g_pilot_data.mission_sequence_active == 1) {
			g_mission_text = malloc(4096);
		}
		g_frontend_chat_team_only = 0;
		g_debrief_disconnected_from_net_game = 0;
		g_frontend_first_visible_line = 0;
		for (int i = 0; i < 8; ++i) {
			if (g_pilot_data.network_players[i].direct_play_id &&
			    g_pilot_data.network_players[i].has_left) {
				net_clear_player_ready_flag_with_lock_guard(
					g_pilot_data.network_players[i]
						.direct_play_id);
				if (net_get_local_player_id() ==
				    g_pilot_data.network_players[i]
					    .direct_play_id) {
					g_debrief_disconnected_from_net_game =
						1;
				}
			}
		}
		net_refresh_player_roster_with_lock_guard();
		if (g_pilot_data.promotion_delta == PILOT_PROMOTION_NONE ||
		    g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			return xvt_campaign_task_finish(1);
		}
		g_campaign.rating_long_name[0] =
			(char)(g_pilot_data.rating + 1);
		g_campaign.rating_long_name[1] = 0;
		g_campaign.phase = XVT_CAMPAIGN_DEBRIEF_RENAME;
	}
	result = net_set_player_name_with_lock_guard(
		net_get_local_player_id(), g_campaign.rating_long_name,
		g_pilot_data.name);
	if (result == XVT_CAMPAIGN_PENDING) {
		g_campaign.pending = 1;
		return result;
	}
	return xvt_campaign_task_finish(1);
}

int xvt_campaign_task_is_pending(void) { return g_campaign.pending; }

int xvt_campaign_task_continues_without_focus(void)
{
	return g_campaign.packet_type != 0;
}

void xvt_campaign_task_reset(void)
{
	memset(&g_campaign, 0, sizeof(g_campaign));
	xvt_cutscene_task_reset();
}
