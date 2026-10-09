#include "xvt/net/frontend_net.h"

#include <stdio.h>
#include <string.h>

#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_scrollbar.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/frontend_net_packets.h"
#include "xvt/net/net.h"
#include "xvt/net/net_send.h"
#include "xvt/util/win32.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/network_browser.h"
#include "xvt_runtime/runtime/network_dialogs.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/runtime/network_task.h"

/* The mission description id in the host's last lobby STATE packet; -1 while no
 * session is selected or none has arrived. 2 functions write it:
 * frontend_net_on_lobby_state sets it from a STATE packet, and
 * xvt_network_dialogs_return resets it to -1. The mission setup screens read
 * it. */
// GLOBAL: XVT 0xAA6AE8
int g_frontend_net_received_mission_description_id = 0;
/* The mission directory id in the host's last lobby STATE packet. Set by
 * frontend_net_on_lobby_state. */
// GLOBAL: XVT 0xAA6CF8
int g_frontend_net_received_mission_directory_id = 0;
/* The buffer most frontend network packets are built in just before they are
 * sent. Many functions write it, most in the mission setup, briefing and
 * debrief screens; in this file frontend_net_send_chat_line, and the packet
 * handler's answers in frontend_net_packets.c and frontend_net_setup_packets.c.
 */
// GLOBAL: XVT 0xAA6AF0
struct frontend_net_packet_scratch g_frontend_net_packet_scratch = {0};
/* Index of the game selected on the join screen, -1 for none.
 * xvt_network_dialogs_return resets it to -1; nothing reads it. */
// GLOBAL: XVT 0x52BF50
int g_frontend_net_selected_session_idx = -1;
/* DirectPlay id of the sender of the last packet
 * frontend_net_process_network_packets read. The mission setup, briefing and
 * debrief screens read it, and the admission screen ignores packets not sent by
 * the host. */
// GLOBAL: XVT 0x52C188
int g_frontend_net_packet_sender_player_id = 0;
/* First payload word of the last mission choice, countdown, reservation,
 * release or PILOT_RATING packet frontend_net_process_network_packets read; the
 * mission setup and briefing screens read it. */
// GLOBAL: XVT 0x52C18C
int g_frontend_net_packet_arg0 = 0;
/* DirectPlay id of the player who made the last team or flight reservation: the
 * second payload word of that packet. frontend_net_store_reservation writes it
 * and nothing reads it. */
// GLOBAL: XVT 0x52C190
int g_frontend_net_reserving_player_id = 0;
/* 1 from the frame the host screen's game name is confirmed until the next
 * frame, which starts hosting. Only frontend_net_host_game_screen uses it, and
 * zeroes it on its first frame. */
// GLOBAL: XVT 0x665D0C
int g_host_game_start_pending = 0;
/* 1 after the mission setup's Quick Start button: the team assignment screen
 * (in single player) and the flight assignment screen then pass straight on,
 * and a single-player briefing applies the chosen preset craft. Only
 * mission_setup_update sets it to 1; concourse_update, mission_setup_update,
 * mission_debrief_update, frontend_net_host_game_screen and
 * xvt_mission_dialogs_resume reset it to 0. */
// GLOBAL: XVT 0x52C204
int g_frontend_quick_start_launch_flag = 0;
/* Protocol version in the last probe answer or game-started notice, stored by
 * frontend_net_store_game_status. Only a debug log line reads it. */
// GLOBAL: XVT 0xAA62A4
int g_frontend_net_probe_version = 0;
/* Players a game still needs: the 8 roster places minus the players in the
 * host's last lobby STATE or READY_ROSTER packet, stored by
 * frontend_net_on_lobby_state and frontend_net_on_ready_roster. Only debug log
 * lines read it. */
// GLOBAL: XVT 0xAA6A70
int g_frontend_net_probe_players_needed = 0;
/* Nonzero when the last probe answer or game-started notice says the game needs
 * a password; stored by frontend_net_store_game_status. Only a debug log line
 * reads it. */
// GLOBAL: XVT 0xAA6A74
int g_frontend_net_probe_password_required = 0;
/* Mission seconds elapsed, from the last probe answer or game-started notice.
 * frontend_net_store_game_status stores it, and the join screen code and
 * xvt_network_dialogs_return reset it to 0, but nothing reads it. */
// GLOBAL: XVT 0xAA6CF0
int g_frontend_net_probe_mission_elapsed_seconds = 0;
/* BRIEFING_ENTERED packets received, which the briefing compares with the ready
 * players. frontend_net_count_briefing_arrival counts it up;
 * mission_setup_update and mission_setup_flight_assignment_update reset it to
 * 0. */
// GLOBAL: XVT 0xA91C90
int g_frontend_briefing_entered_count = 0;
/* The chat line being typed, up to 100 bytes;
 * frontend_net_update_and_draw_chat_panel edits it, and
 * frontend_net_send_chat_line sends it on Enter or Tab and clears it. */
// GLOBAL: XVT 0xAA6A80
char g_frontend_chat_input_buffer[100] = {0};
/* The chat log text, 1,024 bytes from frontend_load_resources, freed by
 * xvt_frontend_task_shutdown; NULL when not allocated. frontend_net_on_chat
 * adds received lines to it and frontend_net_on_chat_sync_chunk rebuilds it
 * from a synced log; concourse_update and frontend_net_host_game_screen clear
 * it. */
// GLOBAL: XVT 0xB69CD0
char *g_frontend_chat_log_buffer = NULL;
/* Bytes of text in g_frontend_chat_log_buffer, not counting the NUL after them;
 * zeroed whenever the log is cleared. */
// GLOBAL: XVT 0xB6A2C4
int g_frontend_chat_log_used_bytes = 0;
/* 1 while chat goes to this player's team only. frontend_net_update_chat_tabs
 * sets it from the chat panel's Team and All tabs, which show only once the
 * mission setup roster is authoritative;
 * frontend_net_update_and_draw_chat_panel otherwise holds it at 0.
 * mission_setup_flight_assignment_update sets it to 1;
 * mission_setup_team_assignment_update, mission_debrief_update,
 * xvt_campaign_task_enter_teams and xvt_campaign_task_enter_debrief reset it to
 * 0. */
// GLOBAL: XVT 0x52BF54
int g_frontend_chat_team_only = 0;
/* Lines the chat log view is scrolled back from its newest line; zeroed on the
 * panel's first frame. Only frontend_net_update_and_draw_chat_panel uses it. */
// GLOBAL: XVT 0x66543C
int g_frontend_chat_scroll_offset = 0;
/* Name of the game selected on the join screen; empty when none. It starts as
 * 32 bytes of 0xFF, and xvt_network_dialogs_return clears it. Nothing reads
 * it. */
// GLOBAL: XVT 0x665440
char g_frontend_net_selected_game_name[32] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

/* Draws the join screen's game list and returns the index of the row clicked
 * this frame, or -1. Returns xvt_network_browser_draw_list's result. */
// FUNCTION: XVT 0x4D7020
int frontend_net_draw_join_game_list(int frame_counter)
{
	(void)frame_counter;
	return xvt_network_browser_draw_list();
}

/* The join screen, run each frame. Hands off to xvt_network_browser_screen and
 * returns its result. */
// FUNCTION: XVT 0x4D73C0
int frontend_net_join_game_screen(int frame_counter)
{
	return xvt_network_browser_screen(frame_counter);
}

/* What a part of frontend_net_await_join_admission_screen returns when it
 * did not end the frame; the screen's own returns are 0 or 1. */
enum { FRONTEND_NET_FRAME_GOES_ON = -1 };

/* Part of frontend_net_await_join_admission_screen's first frame: registers
 * joinback.bmp as "background" and draws it with the frame, "allactive"
 * or "clientactive" by g_host_cd_available and the chat box, then starts
 * the text fade-in. */
static void frontend_net_draw_admission_background(void)
{
	front_image_register_resource_default("frontres\\joinback.bmp",
					      "background");
	frontend_display_lock_offscreen_surface();
	front_image_draw_sprite_opaque("background", 0, 0);
	front_image_draw_sprite("frame", 0, 0);
	if (g_host_cd_available != 0) {
		front_image_draw_sprite("allactive", 0, 0);
	} else {
		front_image_draw_sprite("clientactive", 0, 0);
	}
	front_image_draw_sprite_translucent("chatbox", 0, 0);
	frontend_display_unlock_offscreen_surface(1);
	frontend_text_start_text_fade_in(20);
}

/* Part of frontend_net_handle_refusal for a game that has started, or whose
 * roster is locked when network_result is NET_PACKET_ROSTER_LOCKED: logs the
 * refusal and shows the dialog that says the game has already started. */
static void frontend_net_show_started_refusal(int network_result)
{
	XVT_LOG_DEBUG("network.refusal_received reason=\"%s\"",
		      network_result == NET_PACKET_ROSTER_LOCKED ? "locked"
								 : "started");
	frontend_dialog_show_confirm_dialog(
		frontend_string_get(FRONTSTR_552_THIS_GAME_HAS_ALREADY_STARTED),
		frontend_string_get(
			FRONTSTR_553_PLEASE_SELECT_ANOTHER_GAME_TO_JOIN),
		frontend_string_get(FRONTSTR_554_SPACE_TRANSLATION_PLACEHOLDER),
		NULL, NULL);
}

/* Part of frontend_net_handle_refusal for NET_PACKET_GAME_FULL: logs the
 * refusal and shows the dialog that says the game is full. */
static void frontend_net_show_full_refusal(void)
{
	XVT_LOG_DEBUG("network.refusal_received reason=\"full\"");
	frontend_dialog_show_confirm_dialog(
		frontend_string_get(
			FRONTSTR_543_THE_GAME_YOU_ARE_TRYING_TO_JOIN_IS_FULL),
		frontend_string_get(FRONTSTR_544_PLEASE_TRY_ANOTHER_GAME),
		frontend_string_get(FRONTSTR_545_SPACE_TRANSLATION_PLACEHOLDER),
		NULL, NULL);
}

/* Part of frontend_net_handle_refusal for NET_PACKET_HOST_CANCELLED: logs
 * it and shows the dialog that says the host cancelled the game. */
static void frontend_net_show_host_cancelled(void)
{
	XVT_LOG_DEBUG("network.refusal_received reason=\"host_cancelled\"");
	frontend_dialog_show_confirm_dialog(
		frontend_string_get(FRONTSTR_631_THE_CURRENT_GAME_HAS_BEEN),
		frontend_string_get(FRONTSTR_632_CANCELLED_BY_THE_HOST),
		frontend_string_get(FRONTSTR_633_PLEASE_SELECT_ANOTHER_GAME),
		NULL, NULL);
}

/* Part of frontend_net_handle_refusal for NET_PACKET_VERSION_MISMATCH: logs
 * the refusal and shows the dialog that says this program's version does
 * not match the host's. */
static void frontend_net_show_version_refusal(void)
{
	XVT_LOG_DEBUG("network.refusal_received reason=\"version\"");
	frontend_dialog_show_confirm_dialog(
		frontend_string_get(
			FRONTSTR_546_YOUR_VERSION_OF_THE_PROGRAM_DOES_NOT),
		frontend_string_get(
			FRONTSTR_547_MATCH_THE_HOSTS_PLEASE_VERIFY_THAT_YOU),
		frontend_string_get(FRONTSTR_548_HAVE_THE_CORRECT_PROGRAM),
		NULL, NULL);
}

/* Part of frontend_net_handle_refusal for NET_PACKET_PASSWORD_REQUIRED: logs
 * the refusal and shows the dialog that asks for the correct password in
 * the network configuration screen. */
static void frontend_net_show_password_refusal(void)
{
	XVT_LOG_DEBUG("network.refusal_received reason=\"password\"");
	frontend_dialog_show_confirm_dialog(
		frontend_string_get(FRONTSTR_549_THIS_GAME_REQUIRES_A_PASSWORD),
		frontend_string_get(
			FRONTSTR_550_PLEASE_ENTER_THE_CORRECT_PASSWORD_IN),
		frontend_string_get(
			FRONTSTR_551_THE_NETWORK_CONFIGURATION_SCREEN),
		NULL, NULL);
}

/* Part of frontend_net_await_join_admission_screen: the answer to the refusal
 * in network_result. Rejects the session, shows the refusal's dialog and
 * returns xvt_dialog_continue_with's result; past the switch, which no refusal
 * reaches as each case returns, it sets the concourse or the join screen as the
 * next screen and returns FRONTEND_NET_FRAME_GOES_ON. */
static int frontend_net_handle_refusal(int network_result)
{
	xvt_network_session_reject();
	struct RECT screen_rect;
	switch (network_result - NET_PACKET_FRONTEND_GAME_STARTED) {
	case NET_PACKET_FRONTEND_GAME_STARTED -
		NET_PACKET_FRONTEND_GAME_STARTED:
	case NET_PACKET_ROSTER_LOCKED - NET_PACKET_FRONTEND_GAME_STARTED:
		frontend_net_show_started_refusal(network_result);
		return xvt_dialog_continue_with(xvt_network_dialogs_resume,
						XVT_NETWORK_ACCESS_REJECTED);
		break;
	case NET_PACKET_GAME_FULL - NET_PACKET_FRONTEND_GAME_STARTED:
		frontend_net_show_full_refusal();
		return xvt_dialog_continue_with(xvt_network_dialogs_resume,
						XVT_NETWORK_ACCESS_REJECTED);
		break;
	case NET_PACKET_HOST_CANCELLED - NET_PACKET_FRONTEND_GAME_STARTED:
		frontend_net_show_host_cancelled();
		return xvt_dialog_continue_with(xvt_network_dialogs_resume,
						XVT_NETWORK_ACCESS_REJECTED);
		break;
	case NET_PACKET_VERSION_MISMATCH - NET_PACKET_FRONTEND_GAME_STARTED:
		frontend_net_show_version_refusal();
		return xvt_dialog_continue_with(xvt_network_dialogs_resume,
						XVT_NETWORK_ACCESS_REJECTED);
		break;
	case NET_PACKET_PASSWORD_REQUIRED - NET_PACKET_FRONTEND_GAME_STARTED:
		frontend_net_show_password_refusal();
		return xvt_dialog_continue_with(xvt_network_dialogs_resume,
						XVT_NETWORK_ACCESS_PASSWORD);
		frontend_draw_rect_assign(&screen_rect, 0, 0, 640, 480);
		frontend_screen_queue_push(config_options_datapad_update,
					   &screen_rect);
		break;
	}
	if (g_game_config.network_type != 0) {
		frontend_screen_set_callbacks(concourse_update, concourse_exit);
	} else {
		g_frontend_skip_screen_entry_setup = 1;
		frontend_screen_set_callbacks(
			frontend_net_join_game_screen,
			frontend_mission_list_free_screen_resources);
	}
	return FRONTEND_NET_FRAME_GOES_ON;
}

enum {
	ACCESS_TIMEOUT_FRAME = 480,
	PILOT_BANNER_ANIMATION_PERIOD_FRAMES = 32,
};

/* Part of each frame of frontend_net_await_join_admission_screen: draws
 * the version text in rect and, when the pilot has a name, the pilot's
 * rating and name with the "rebtiny" and "imptiny" sprites of frame
 * (frame_counter % 32) >> 1. */
static void frontend_net_draw_admission_banner(int frame_counter,
					       struct RECT *rect)
{
	frontend_draw_rect_assign(rect, 507, 452, 562, 464);
	sprintf(g_frontend_scratch_buffer, "v. %d.%d", 2, 0);
	frontend_text_draw_centered(12, g_frontend_scratch_buffer, rect,
				    0xFFFF);
	frontend_draw_rect_assign(rect, 200, 452, 436, 464);
	if (g_pilot_data.name[0] != '\0') {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%s", 6,
			g_pilot_data.rating_name, 1, g_pilot_data.name);
		frontend_text_draw_centered(12, g_frontend_scratch_buffer, rect,
					    g_color_yellow);
		int animation_frame = (frame_counter %
				       PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
				      1;
		sprintf(g_frontend_scratch_buffer, "rebtiny%d",
			animation_frame);
		front_image_draw_sprite(g_frontend_scratch_buffer, 204, 453);
		sprintf(g_frontend_scratch_buffer, "imptiny%d",
			animation_frame);
		front_image_draw_sprite(g_frontend_scratch_buffer, 420, 453);
	}
}

/* Part of each frame of frontend_net_await_join_admission_screen: draws
 * the cancel button in rect; when it is pressed, cancels the session, goes
 * back to the join screen and frees "background". */
static void frontend_net_handle_admission_cancel(struct RECT *rect)
{
	if (g_game_config.help_on != 0) {
		frontend_button_enable_overlay_text();
	}
	frontend_button_set_overlay_text(
		frontend_string_get(FRONTSTR_019_CANCEL));
	frontend_draw_rect_assign(rect, 85, 447, 176, 471);
	int cancel_pressed = frontend_button_handle_sprite_button(
		rect, "leaveup", "leavedown",
		frontend_string_get(FRONTSTR_019_CANCEL), 12, 0, 8,
		"buttonsound");
	frontend_button_disable_overlay_text();
	if (cancel_pressed != 0) {
		XVT_LOG_DEBUG("network.join_cancelled");
		xvt_network_session_cancel();
		g_frontend_skip_screen_entry_setup = 1;
		frontend_screen_set_callbacks(
			frontend_net_join_game_screen,
			frontend_mission_list_free_screen_resources);
		front_image_free_resource_by_name("background");
	}
}

/* Waits for the host's answer to a join request, run each frame, showing the
 * access message, version and player banner. It first shows any admission
 * failure (xvt_network_dialogs_report_admission_failure), then reads frontend
 * packets and ignores any not sent by the host. A refusal (full, version,
 * password, roster locked, game started, host cancelled) ends the connection
 * and shows its message, then continues through xvt_dialog_continue_with and
 * returns its result. PLAYER_ADMITTED clears g_mp_roster and moves to the
 * mission setup screen. The cancel button ends the connection and returns to
 * the join screen. Returns 1 when frontend_handle_common_screen_controls
 * returns 1, else 0 unless noted. */
// FUNCTION: XVT 0x4D7E70
int frontend_net_await_join_admission_screen(int frame_counter)
{
	if (frame_counter == 0) {
		frontend_net_draw_admission_background();
	}

	struct RECT rect;
	frontend_draw_rect_assign(&rect, 84, 107, 434, 433);
	frontend_text_draw_centered(
		15,
		frontend_string_get(FRONTSTR_020_ACCESSING_IMPERIAL_NETWORK),
		&rect, 0xFFFF);
	if (xvt_network_dialogs_report_admission_failure()) {
		return 0;
	}
	int network_result = frontend_net_process_network_packets();
	if (g_frontend_net_packet_sender_player_id !=
	    net_get_host_player_id()) {
		if (network_result != 0) {
			XVT_LOG_DEBUG(
				"network.non_host_packet_ignored type=%d player=%u",
				network_result,
				(unsigned)
					g_frontend_net_packet_sender_player_id);
		}
		network_result = 0;
	}
	if (network_result == NET_PACKET_GAME_FULL ||
	    network_result == NET_PACKET_VERSION_MISMATCH ||
	    network_result == NET_PACKET_PASSWORD_REQUIRED ||
	    network_result == NET_PACKET_ROSTER_LOCKED ||
	    network_result == NET_PACKET_HOST_CANCELLED ||
	    network_result == NET_PACKET_FRONTEND_GAME_STARTED) {
		int refused = frontend_net_handle_refusal(network_result);
		if (refused != FRONTEND_NET_FRAME_GOES_ON) {
			return refused;
		}
	} else if (network_result == NET_PACKET_PLAYER_ADMITTED) {
		memset(g_mp_roster, 0, sizeof(g_mp_roster));
		frontend_screen_set_callbacks(mission_setup_update,
					      mission_setup_exit);
	}

	frontend_net_draw_admission_banner(frame_counter, &rect);
	if (frontend_handle_common_screen_controls(0) == 1) {
		return 1;
	}
	if (xvt_dialog_is_active()) {
		return 0;
	}
	frontend_net_handle_admission_cancel(&rect);
	return 0;
}

/* Draws the join screen's mission heading, with the mission's name once a lobby
 * STATE has named it, and then its briefing text from g_mission_text, with a
 * scrollbar past 6 lines; returns 1. Returns xvt_network_browser_draw_mission's
 * result. */
// FUNCTION: XVT 0x4D8350
int frontend_net_draw_join_game_mission_briefing(void)
{
	return xvt_network_browser_draw_mission();
}

/* Draws the join screen's player heading and, once a lobby STATE has arrived,
 * each player in g_mp_roster with rating and name, in two columns of 4; returns
 * 1. Returns xvt_network_browser_draw_roster's result. */
// FUNCTION: XVT 0x4D8540
int frontend_net_draw_join_game_player_roster(void)
{
	return xvt_network_browser_draw_roster();
}

/* Part of frontend_net_update_and_draw_chat_panel once the mission setup
 * roster is authoritative: draws the All and Team tabs in rect, and a click
 * on the tab not selected sets g_frontend_chat_team_only to match it. */
static void frontend_net_update_chat_tabs(struct RECT *rect)
{
	int cursor_x;
	int cursor_y;
	frontend_cursor_get_pos(&cursor_x, &cursor_y);
	if (g_frontend_chat_team_only == 0) {
		front_image_draw_sprite("tab2", 0, 0);
		frontend_draw_rect_assign(rect, 556, 95, 588, 105);
		frontend_text_draw_centered(
			10, frontend_string_get(FRONTSTR_217_TEAM), rect,
			g_color_gray);
		if (frontend_draw_point_in_rect(rect, cursor_x, cursor_y) &&
		    (frontend_mouse_get_left_click() != 0 ||
		     frontend_mouse_get_right_click() != 0)) {
			if (g_game_config.sfx_datapad_enabled != 0) {
				frontend_sound_play_ui_sound(
					"jewelsound", 1, 0, 255,
					12 * g_game_config.sfx_datapad_volume,
					63);
			}
			g_frontend_chat_team_only = 1;
			XVT_LOG_DEBUG("network.chat_channel team_only=%d",
				      g_frontend_chat_team_only);
		}
		front_image_draw_sprite("tab1", 0, 0);
		frontend_draw_rect_assign(rect, 520, 95, 552, 105);
		frontend_text_draw_centered(
			10, frontend_string_get(FRONTSTR_402_ALL), rect,
			0xFFFF);
	} else {
		front_image_draw_sprite("tab1", 0, 0);
		frontend_draw_rect_assign(rect, 520, 95, 552, 105);
		frontend_text_draw_centered(
			10, frontend_string_get(FRONTSTR_402_ALL), rect,
			g_color_gray);
		if (frontend_draw_point_in_rect(rect, cursor_x, cursor_y) &&
		    (frontend_mouse_get_left_click() != 0 ||
		     frontend_mouse_get_right_click() != 0)) {
			if (g_game_config.sfx_datapad_enabled != 0) {
				frontend_sound_play_ui_sound(
					"jewelsound", 1, 0, 255,
					12 * g_game_config.sfx_datapad_volume,
					63);
			}
			g_frontend_chat_team_only = 0;
			XVT_LOG_DEBUG("network.chat_channel team_only=%d",
				      g_frontend_chat_team_only);
		}
		front_image_draw_sprite("tab2", 0, 0);
		frontend_draw_rect_assign(rect, 556, 95, 588, 105);
		frontend_text_draw_centered(
			10, frontend_string_get(FRONTSTR_217_TEAM), rect,
			0xFFFF);
	}
}

/* Part of frontend_net_send_chat_line: sends the CHAT packet in
 * g_frontend_net_packet_scratch to each player on this player's team when
 * team_message is not 0 and this player has a team assignment, else to
 * everyone, and logs where it went. */
static void frontend_net_route_chat_packet(int team_message)
{
	if (team_message != 0) {
		int player_index = 0;
		while (player_index < 8 &&
		       net_get_local_player_id() !=
			       g_mission_setup_player_assignments
				       .assigned_player_ids[player_index]) {
			++player_index;
		}
		if (player_index == 8) {
			XVT_LOG_DEBUG(
				"network.chat_sent to=\"everyone_no_team\" team=%d ready=%d",
				g_pilot_data.team,
				g_frontend_scratch_buffer[0] != 4);
			net_send_packet_and_flush(
				0, &g_frontend_net_packet_scratch,
				strlen(g_frontend_scratch_buffer) + 4);
		} else {
			for (player_index = 0; player_index < 8;
			     ++player_index) {
				int player_id =
					g_mission_setup_player_assignments
						.team_player_ids[g_pilot_data
									 .team]
								[player_index];
				if (player_id != 0) {
					net_send_packet_and_flush(
						(DPID)player_id,
						&g_frontend_net_packet_scratch,
						strlen(g_frontend_scratch_buffer) +
							4);
				}
			}
			XVT_LOG_DEBUG(
				"network.chat_sent to=\"team\" team=%d ready=%d",
				g_pilot_data.team,
				g_frontend_scratch_buffer[0] != 4);
		}
	} else {
		net_send_packet_and_flush(0, &g_frontend_net_packet_scratch,
					  strlen(g_frontend_scratch_buffer) +
						  4);
		XVT_LOG_DEBUG(
			"network.chat_sent to=\"everyone\" team=%d ready=%d",
			g_pilot_data.team, g_frontend_scratch_buffer[0] != 4);
	}
}

/* Part of frontend_net_update_and_draw_chat_panel when Enter or Tab
 * sends the text in g_frontend_chat_input_buffer: builds the CHAT packet in
 * g_frontend_net_packet_scratch as that function's comment tells it, sends
 * it through frontend_net_route_chat_packet and clears the buffer. */
static void frontend_net_send_chat_line(void)
{
	g_frontend_net_packet_scratch.packet_type = NET_PACKET_CHAT;
	int local_player_id = net_get_local_player_id();
	int team_message;
	if (net_is_player_ready(local_player_id) != 0) {
		team_message = g_frontend_chat_team_only;
		if (g_frontend_chat_team_only == 1) {
			g_frontend_scratch_buffer[0] = 2;
		} else {
			g_frontend_scratch_buffer[0] = 3;
		}
	} else {
		g_frontend_scratch_buffer[0] = 4;
		team_message = g_frontend_chat_team_only;
	}
	g_frontend_scratch_buffer[1] = 0;
	strcat(g_frontend_scratch_buffer, g_pilot_data.name);
	strcat(g_frontend_scratch_buffer, ": ");
	{
		size_t message_length = strlen(g_frontend_scratch_buffer);
		g_frontend_scratch_buffer[message_length] = 1;
		g_frontend_scratch_buffer[message_length + 1] = 0;
	}
	strcat(g_frontend_scratch_buffer, g_frontend_chat_input_buffer);
	memcpy(g_frontend_net_packet_scratch.payload, g_frontend_scratch_buffer,
	       strlen(g_frontend_scratch_buffer));
	frontend_net_route_chat_packet(team_message);
	memset(g_frontend_chat_input_buffer, 0,
	       sizeof(g_frontend_chat_input_buffer));
}

/* Runs the chat panel each frame. Once the mission setup roster is
 * authoritative it shows All and Team tabs that set g_frontend_chat_team_only;
 * before that it holds it at 0. On Enter or Tab with text in
 * g_frontend_chat_input_buffer it sends a CHAT packet: a color code byte (2 for
 * team or 3 for all when this player is ready, 4 when not), this player's name,
 * ": ", color code 1 and the text, with no closing NUL. A team message goes to
 * each player on this player's team (g_pilot_data.team), or to all when this
 * player has no team assignment. Then it draws g_frontend_chat_log_buffer with a
 * scrollbar past 20 lines, kept in g_frontend_chat_scroll_offset, and returns
 * frontend_text_draw_wrapped's result for it. Frame 0 flushes typed keys and
 * resets the scroll. Writes g_frontend_net_packet_scratch. */
// FUNCTION: XVT 0x4D8690
int frontend_net_update_and_draw_chat_panel(int frame_counter)
{
	if (frame_counter == 0) {
		keyboard_flush_char_buffer();
		g_frontend_chat_scroll_offset = 0;
	}

	struct RECT rect;
	if (g_mission_setup_roster_authoritative != 0) {
		frontend_net_update_chat_tabs(&rect);
	} else {
		g_frontend_chat_team_only = 0;
	}

	frontend_draw_rect_assign(&rect, 461, 406, 595, 424);
	if (frontend_text_handle_editable_field(&rect,
						g_frontend_chat_input_buffer,
						100, 0, 12, NULL) != 0 &&
	    g_frontend_chat_input_buffer[0] != 0) {
		frontend_net_send_chat_line();
	}

	frontend_draw_rect_assign(&rect, 461, 117, 595, 407);
	int line_count = frontend_text_draw_wrapped(
		12, g_frontend_chat_log_buffer, &rect, 0xFFFF, 2, 1024);
	int max_scroll = line_count - 20;
	if (line_count <= 20) {
		max_scroll = 0;
	}
	if (max_scroll > 0) {
		++max_scroll;
		if (g_frontend_chat_scroll_offset == max_scroll) {
			--g_frontend_chat_scroll_offset;
		}
		frontend_draw_rect_assign(&rect, 596, 117, 605, 407);
		g_frontend_chat_scroll_offset =
			max_scroll -
			frontend_scrollbar_draw(
				&rect,
				max_scroll - g_frontend_chat_scroll_offset - 1,
				max_scroll, 0, 5, (unsigned int)g_color_navy,
				7) -
			1;
	}
	frontend_draw_rect_assign(&rect, 461, 117, 595, 398);
	return frontend_text_draw_wrapped(
		12, g_frontend_chat_log_buffer, &rect, 0xFFFF, 2,
		max_scroll - g_frontend_chat_scroll_offset - 1);
}

/* Draws the join screen's side navigation, its first slot active and shown
 * pressed while the query button is held, and the query button; the button
 * reads Refresh, and a click calls xvt_network_task_refresh. Returns 1. */
// FUNCTION: XVT 0x4D9170
int frontend_net_draw_join_game_sidebars_and_query_all(void)
{
	enum {
		QUERY_BUTTON_LEFT = 22,
		QUERY_BUTTON_TOP = 114,
		QUERY_BUTTON_RIGHT = 42,
		QUERY_BUTTON_BOTTOM = 138,
		QUERY_BUTTON_FONT_SIZE = 12,
		QUERY_BUTTON_HELD_SLOT = 11,
	};

	frontend_navigation_slot_state slot_states[8];

	slot_states[1] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slot_states[2] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slot_states[3] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slot_states[4] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slot_states[5] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slot_states[6] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slot_states[7] = FRONTEND_NAVIGATION_SLOT_INACTIVE;
	slot_states[0] = FRONTEND_NAVIGATION_SLOT_ACTIVE;
	int cursor_x;
	int cursor_y;
	frontend_cursor_get_pos(&cursor_x, &cursor_y);
	struct RECT rect;
	frontend_draw_rect_assign(&rect, QUERY_BUTTON_LEFT, QUERY_BUTTON_TOP,
				  QUERY_BUTTON_RIGHT, QUERY_BUTTON_BOTTOM);
	if (frontend_draw_point_in_rect(&rect, cursor_x, cursor_y) &&
	    (frontend_mouse_get_left_down() != 0 ||
	     frontend_mouse_get_right_down() != 0 ||
	     frontend_mouse_get_left_click() != 0 ||
	     frontend_mouse_get_right_click() != 0)) {
		slot_states[0] = FRONTEND_NAVIGATION_SLOT_SELECTED;
	}
	frontend_button_draw_eight_slot_navigation_state(slot_states);
	frontend_draw_rect_assign(&rect, QUERY_BUTTON_LEFT, QUERY_BUTTON_TOP,
				  QUERY_BUTTON_RIGHT, QUERY_BUTTON_BOTTOM);
	if (frontend_button_handle_sprite_button(
		    &rect, "join1u", "join1d", "Refresh",
		    QUERY_BUTTON_FONT_SIZE, 0, QUERY_BUTTON_HELD_SLOT,
		    "jewelsound") != 0) {
		XVT_LOG_DEBUG("network.refresh_requested");
		xvt_network_task_refresh();
	}
	return 1;
}

/* Leaves the host screen: frees its background image and resets the scrollable
 * controls; returns 0. */
// FUNCTION: XVT 0x4DFD10
int frontend_net_host_game_exit(int frame_counter)
{
	(void)frame_counter;

	front_image_free_resource_by_name("background");
	frontend_reset_scrollable_controls();
	return 0;
}

/* The host screen, run each frame. Frame 0 resets the host state
 * (g_host_game_start_pending, g_frontend_quick_start_launch_flag and others),
 * starts from the last host name as the game name, clears the chat log and
 * roster, and draws the background. Until the name is confirmed, by Enter or
 * Tab in the field or the Host button, it edits the name (up to 22 characters,
 * this player's name and a suffix when left empty) and offers the back button
 * to the concourse. On the next frame it keeps the name as the next default and
 * starts hosting by handing off to xvt_network_task_begin. Returns 1 when
 * frontend_handle_common_screen_controls returns 1, else 0. */
// FUNCTION: XVT 0x4DFD30
int frontend_net_host_game_screen(int frame_counter)
{
	enum {
		HOST_NAME_MAX_CHARS = 22,
		HOST_TEXT_FIELD_ID = 0,
		HOST_BUTTON_HELD_SLOT = 20,
	};

	if (frame_counter == 0) {
		g_host_game_start_pending = 0;
		g_frontend_skip_screen_entry_setup = 0;
		g_frontend_quick_start_launch_flag = 0;
		g_config_connection_type_editable = 1;
		g_frontend_game_session_in_progress = 0;
		strcpy(g_pilot_data.multiplayer_game_name,
		       g_pilot_data.multiplayer_host_name);
		XVT_LOG_DEBUG("network.host_screen_opened game=\"%.32s\"",
			      g_pilot_data.multiplayer_game_name);
		if (g_frontend_chat_log_buffer != NULL) {
			memset(g_frontend_chat_log_buffer, 0, 1024);
			g_frontend_chat_log_used_bytes = 0;
		}
		frontend_cursor_set_pos(417, 291);
		memset(g_mp_roster, 0, sizeof(g_mp_roster));
		keyboard_flush_char_buffer();
		front_image_register_resource_default("frontres\\create.bmp",
						      "background");
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_opaque("background", 0, 0);
		front_image_draw_sprite("frame", 0, 0);
		if (g_host_cd_available != 0) {
			front_image_draw_sprite("allactive", 0, 0);
		} else {
			front_image_draw_sprite("clientactive", 0, 0);
		}
		front_image_draw_sprite_translucent("createoverlay", 0, 0);
		frontend_display_unlock_offscreen_surface(1);
		frontend_text_start_text_fade_in(20);
	}

	struct RECT rect;
	if (g_host_game_start_pending == 0) {
		frontend_draw_rect_assign(&rect, 245, 225, 445, 245);
		frontend_text_draw_centered(
			15,
			frontend_string_get(FRONTSTR_017_NAME_THE_GAME_SESSION),
			&rect, 0xFFFF);
		frontend_draw_rect_assign(&rect, 250, 245, 440, 265);
		int host_requested = frontend_text_handle_editable_field(
			&rect, g_pilot_data.multiplayer_game_name,
			HOST_NAME_MAX_CHARS, HOST_TEXT_FIELD_ID, 12, NULL);
		frontend_draw_rect_assign(&rect, 250, 275, 440, 295);
		host_requested |= frontend_button_handle_text_button(
			&rect, frontend_string_get(FRONTSTR_004_HOST_GAME), 15,
			0xFFFF, HOST_BUTTON_HELD_SLOT, "buttonsound");
		if (host_requested != 0) {
			XVT_LOG_DEBUG(
				"network.host_name_confirmed game=\"%.32s\"",
				g_pilot_data.multiplayer_game_name);
			if (g_pilot_data.multiplayer_game_name[0] == '\0') {
				sprintf(g_pilot_data.multiplayer_game_name,
					"%s%s", g_pilot_data.name,
					frontend_string_get(
						FRONTSTR_470_S_GAME));
			}
			g_host_game_start_pending = 1;
			frontend_display_disable_offscreen_restore();
		}

		frontend_draw_rect_assign(&rect, 507, 452, 562, 464);
		sprintf(g_frontend_scratch_buffer, "v. %d.%d", 2, 0);
		frontend_text_draw_centered(12, g_frontend_scratch_buffer,
					    &rect, 0xFFFF);
		frontend_draw_rect_assign(&rect, 200, 452, 436, 464);
		if (g_pilot_data.name[0] != '\0') {
			sprintf(g_frontend_scratch_buffer, "%c%s %c%s", 6,
				g_pilot_data.rating_name, 1, g_pilot_data.name);
			frontend_text_draw_centered(12,
						    g_frontend_scratch_buffer,
						    &rect, g_color_yellow);
		}
		if (frontend_handle_common_screen_controls(0) == 1) {
			return 1;
		}
		if (xvt_dialog_is_active()) {
			return 0;
		}
		frontend_draw_rect_assign(&rect, 85, 447, 176, 471);
		if (g_game_config.help_on != 0) {
			frontend_button_enable_overlay_text();
		}
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_569_PREVIOUS));
		if (frontend_button_handle_sprite_button(
			    &rect, "leaveup", "leavedown",
			    frontend_string_get(
				    FRONTSTR_258_RETURN_TO_PILOT_RECORDS),
			    12, 0, 8, "buttonsound") != 0) {
			XVT_LOG_DEBUG("network.host_screen_left");
			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NONE;
			frontend_screen_set_callbacks(concourse_update,
						      concourse_exit);
		}
		frontend_button_disable_overlay_text();
		return 0;
	}

	strcpy(g_pilot_data.multiplayer_host_name,
	       g_pilot_data.multiplayer_game_name);
	g_mission_setup_is_host = 1;
	g_host_game_start_pending = 0;
	frontend_cursor_show();
	xvt_network_task_begin(XVT_NETWORK_HOST);
	return 0;
}
