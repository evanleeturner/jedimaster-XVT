#include "xvt_runtime/runtime/mission_dialogs.h"

#include <string.h>

#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/frontend_cleanup.h"

/* Sends a packet holding only its type to every player and flushes it at
 * once. */
static void xvt_mission_dialogs_send_to_every_player(int packet_type)
{
	g_frontend_net_packet_scratch.packet_type = packet_type;
	net_send_packet_and_flush(0, &g_frontend_net_packet_scratch,
				  sizeof(int));
}

/* Sends a packet holding only its type to the host and flushes it at once. */
static void xvt_mission_dialogs_send_to_host(int packet_type)
{
	g_frontend_net_packet_scratch.packet_type = packet_type;
	net_send_packet_and_flush(net_get_host_player_id(),
				  &g_frontend_net_packet_scratch, sizeof(int));
}

/* Returns to the join-game screen; leaving it frees the mission list's
 * resources. */
static void xvt_mission_dialogs_return_to_join_screen(void)
{
	frontend_screen_set_callbacks(
		frontend_net_join_game_screen,
		frontend_mission_list_free_screen_resources);
}

/* These tails execute in the suspended screen's callback slot, before its exit callback. */
int xvt_mission_dialogs_resume(int result, int action)
{
	/* In xvt_mission_dialog_action order. */
	static const char *const action_names[] = {
		"notice",
		"setup_cancelled",
		"setup_booted",
		"team_cancelled",
		"assignment_cancelled",
		"briefing_cancelled",
		"setup_host_leave",
		"client_leave",
		"team_client_leave",
		"team_previous",
		"host_restart",
		"solo_back_to_setup",
		"solo_back_to_teams",
		"debrief_client_leave",
		"debrief_host_abort",
		"debrief_solo_abort",
		"debrief_solo_abort_clear_roster",
	};
	XVT_LOG_INFO("mission.dialog_answered action=\"%s\" result=%d",
		     (unsigned int)action < sizeof(action_names) /
						    sizeof(action_names[0])
			     ? action_names[action]
			     : "unknown",
		     result);
	switch ((xvt_mission_dialog_action)action) {
	case XVT_MISSION_NOTICE:
		break;
	case XVT_MISSION_SETUP_CANCELLED:
		g_frontend_mission_session_mode =
			FRONTEND_MISSION_SESSION_NET_CLIENT;
		xvt_mission_dialogs_return_to_join_screen();
		break;
	case XVT_MISSION_SETUP_BOOTED:
		xvt_mission_dialogs_return_to_join_screen();
		break;
	case XVT_MISSION_TEAM_CANCELLED:
		g_frontend_mission_session_mode =
			FRONTEND_MISSION_SESSION_NET_CLIENT;
		g_frontend_skip_screen_entry_setup = 1;
		xvt_mission_dialogs_return_to_join_screen();
		net_shutdown_direct_play_session();
		break;
	case XVT_MISSION_ASSIGNMENT_CANCELLED:
		g_frontend_skip_screen_entry_setup = 1;
		g_frontend_mission_session_mode =
			FRONTEND_MISSION_SESSION_NET_CLIENT;
		xvt_mission_dialogs_return_to_join_screen();
		break;
	case XVT_MISSION_BRIEFING_CANCELLED:
		if (net_is_host()) {
			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NONE;
			frontend_screen_set_callbacks(concourse_update,
						      concourse_exit);
		} else {
			g_frontend_skip_screen_entry_setup = 1;
			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NET_CLIENT;
			xvt_mission_dialogs_return_to_join_screen();
		}
		break;
	case XVT_MISSION_SETUP_HOST_LEAVE:
		if (result) {
			g_frontend_skip_screen_entry_setup = 1;
			xvt_mission_dialogs_send_to_every_player(
				NET_PACKET_HOST_CANCELLED);
			net_shutdown_direct_play_session();
			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NONE;
			frontend_screen_set_callbacks(concourse_update,
						      concourse_exit);
		}
		break;
	case XVT_MISSION_CLIENT_LEAVE:
	case XVT_MISSION_TEAM_CLIENT_LEAVE:
		if (result) {
			if (action == XVT_MISSION_CLIENT_LEAVE) {
				g_frontend_skip_screen_entry_setup = 1;
			}
			xvt_mission_dialogs_send_to_host(
				NET_PACKET_PLAYER_LEFT);
			net_shutdown_direct_play_session();
			if (action == XVT_MISSION_TEAM_CLIENT_LEAVE) {
				g_frontend_skip_screen_entry_setup = 1;
			}
			xvt_mission_dialogs_return_to_join_screen();
		}
		break;
	case XVT_MISSION_TEAM_PREVIOUS:
		/* The recovered team-screen caller does not inspect the answer. */
		if (g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			xvt_mission_dialogs_send_to_every_player(
				NET_PACKET_RETURN_TO_SETUP);
		} else {
			frontend_screen_set_callbacks(mission_setup_update,
						      mission_setup_exit);
		}
		break;
	case XVT_MISSION_HOST_RESTART:
		if (result) {
			xvt_mission_dialogs_send_to_every_player(
				NET_PACKET_RETURN_TO_SETUP);
		}
		break;
	case XVT_MISSION_SOLO_BACK_TO_SETUP:
		if (result) {
			g_frontend_skip_screen_entry_setup = 1;
			frontend_screen_set_callbacks(mission_setup_update,
						      mission_setup_exit);
		}
		break;
	case XVT_MISSION_SOLO_BACK_TO_TEAMS:
		if (result) {
			g_frontend_skip_screen_entry_setup = 1;
			frontend_screen_set_callbacks(
				mission_setup_team_assignment_update,
				xvt_frontend_cleanup_mission_resources);
		}
		break;
	case XVT_MISSION_DEBRIEF_CLIENT_LEAVE:
		if (result) {
			xvt_mission_dialogs_send_to_host(
				NET_PACKET_PLAYER_LEFT);
			net_shutdown_direct_play_session();
			frontend_button_disable_overlay_text();
			frontend_screen_set_callbacks(concourse_update,
						      concourse_exit);
		}
		break;
	case XVT_MISSION_DEBRIEF_HOST_ABORT:
		if (result) {
			xvt_mission_dialogs_send_to_every_player(
				NET_PACKET_RETURN_TO_MISSION_SELECTION);
		}
		break;
	case XVT_MISSION_DEBRIEF_SOLO_ABORT:
	case XVT_MISSION_DEBRIEF_SOLO_ABORT_CLEAR_ROSTER:
		if (result) {
			frontend_button_disable_overlay_text();
			g_frontend_skip_screen_entry_setup = 0;
			g_frontend_quick_start_launch_flag = 0;
			g_frontend_game_session_in_progress = 0;
			g_mission_setup_roster_authoritative = 0;
			if (action ==
			    XVT_MISSION_DEBRIEF_SOLO_ABORT_CLEAR_ROSTER) {
				memset(g_mp_roster, 0, sizeof(g_mp_roster));
			}
			frontend_screen_set_callbacks(mission_setup_update,
						      mission_setup_exit);
		}
		break;
	}
	frontend_button_disable_overlay_text();
	return 0;
}
