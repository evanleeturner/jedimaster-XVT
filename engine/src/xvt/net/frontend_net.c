#include "xvt/net/frontend_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/briefing_text.h"
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
#include "xvt/frontend/frontend_mouse.h"
#include "xvt/frontend/frontend_scrollbar.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/movie.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/net.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log_both_builds.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/network_browser.h"
#include "xvt_runtime/runtime/network_dialogs.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/runtime/network_task.h"

/* The game's DirectPlay application GUID as four words, used to list, join and
 * host sessions of this game (net_enumerate_app_sessions,
 * net_start_network_session). Never written; only original-build code reads
 * it. */
// GLOBAL: XVT 0x518278
const unsigned int g_frontend_net_xvt_direct_play_app_guid[4] = {
	0x09438C20, 0x11CEE06A, 0xAA008186, 0x575D6C00};
/* The mission description id in the host's last lobby STATE packet; -1 while no
 * session is selected or none has arrived. 7 functions write it:
 * frontend_net_process_network_packets sets it from a STATE packet, and the join
 * screen, the session probe and sort code and xvt_network_dialogs_return reset it
 * to -1. The mission setup screens and the join screen's drawing read it. */
// GLOBAL: XVT 0xAA6AE8
int g_frontend_net_received_mission_description_id = 0;
/* The mission directory id in the host's last lobby STATE packet. Set by
 * frontend_net_process_network_packets; reset to the training exercises directory
 * by frontend_net_join_game_screen, frontend_net_connect_to_selected_game_screen and
 * frontend_net_probe_session_by_index. */
// GLOBAL: XVT 0xAA6CF8
int g_frontend_net_received_mission_directory_id = 0;
/* The buffer most frontend network packets are built in just before they are
 * sent. 37 functions write it, most in the mission setup, briefing and debrief
 * screens, 5 in this file. */
// GLOBAL: XVT 0xAA6AF0
struct frontend_net_packet_scratch g_frontend_net_packet_scratch = {0};
/* The games the join screen lists, kept sorted by frontend_net_sort_sessions.
 * Written by frontend_net_refresh_session_list, frontend_net_probe_session_by_index
 * and frontend_net_join_game_screen, which clears it on entry unless
 * g_frontend_skip_screen_entry_setup is set. Only the original build uses it. */
// GLOBAL: XVT 0xAA62B0
struct frontend_net_session_entry g_frontend_net_session_list[32] = {{0}};
/* Entries in use in g_frontend_net_session_list; frontend_net_refresh_session_list
 * recounts it, at most 32, and frontend_net_join_game_screen zeroes it with the
 * list. Only the original build uses it. */
// GLOBAL: XVT 0xAA6CF4
int g_frontend_net_session_count = 0;
/* First session-list row shown, set by the list's scrollbar and zeroed on the
 * join screen's first frame. Only frontend_net_draw_join_game_list uses it, in the
 * original build. */
// GLOBAL: XVT 0x665438
int g_frontend_net_session_list_scroll_offset = 0;
/* Index in g_frontend_net_session_list of the game selected on the join screen, -1
 * for none; while one is selected the list is not refreshed and that game's
 * packets are read. 6 functions write it, chiefly frontend_net_join_game_screen,
 * frontend_net_sort_sessions (which follows the game as the list is sorted) and
 * xvt_network_dialogs_return. Only original-build code reads it. */
// GLOBAL: XVT 0x52BF50
int g_frontend_net_selected_session_idx = -1;
/* DirectPlay id of the sender of the last packet
 * frontend_net_process_network_packets read. The mission setup, briefing and
 * debrief screens read it, and the modern admission screen ignores packets not
 * sent by the host. */
// GLOBAL: XVT 0x52C188
int g_frontend_net_packet_sender_player_id = 0;
/* First payload word of the last mission choice, countdown, reservation,
 * release or PILOT_RATING packet frontend_net_process_network_packets read; the
 * mission setup and briefing screens read it. */
// GLOBAL: XVT 0x52C18C
int g_frontend_net_packet_arg0 = 0;
/* DirectPlay id of the player who made the last team or flight reservation: the
 * second payload word of that packet. frontend_net_process_network_packets writes
 * it and nothing reads it. */
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
 * frontend_net_process_network_packets; frontend_net_probe_session_by_index copies it
 * into the session list. Only original-build code reads it. */
// GLOBAL: XVT 0xAA62A4
int g_frontend_net_probe_version = 0;
/* Players a game still needs: the 8 roster places minus the players in the
 * host's last lobby STATE or READY_ROSTER packet, stored by
 * frontend_net_process_network_packets. frontend_net_probe_session_by_index zeroes it
 * before a probe and copies it into the session list. Only original-build code
 * reads it. */
// GLOBAL: XVT 0xAA6A70
int g_frontend_net_probe_players_needed = 0;
/* Nonzero when the last probe answer or game-started notice says the game needs
 * a password; stored by frontend_net_process_network_packets and copied into the
 * session list. Only original-build code reads it. */
// GLOBAL: XVT 0xAA6A74
int g_frontend_net_probe_password_required = 0;
/* Mission seconds elapsed, from the last probe answer or game-started notice.
 * frontend_net_process_network_packets stores it, and the join screen code and
 * xvt_network_dialogs_return reset it to 0, but nothing reads it. */
// GLOBAL: XVT 0xAA6CF0
int g_frontend_net_probe_mission_elapsed_seconds = 0;
/* BRIEFING_ENTERED packets received, which the briefing compares with the ready
 * players. frontend_net_process_network_packets counts it up; mission_setup_update
 * and mission_setup_flight_assignment_update reset it to 0. */
// GLOBAL: XVT 0xA91C90
int g_frontend_briefing_entered_count = 0;
/* The chat line being typed, up to 100 bytes;
 * frontend_net_update_and_draw_chat_panel edits it, sends it on Enter or Tab and
 * clears it. */
// GLOBAL: XVT 0xAA6A80
char g_frontend_chat_input_buffer[100] = {0};
/* The chat log text, 1,024 bytes from frontend_load_resources, freed by game_main
 * or xvt_frontend_task_shutdown; NULL when not allocated.
 * frontend_net_process_network_packets adds received lines to it; concourse_update
 * and the join, connect and host screens clear it. */
// GLOBAL: XVT 0xB69CD0
char *g_frontend_chat_log_buffer = NULL;
/* Bytes of text in g_frontend_chat_log_buffer, not counting the NUL after them;
 * zeroed whenever the log is cleared. */
// GLOBAL: XVT 0xB6A2C4
int g_frontend_chat_log_used_bytes = 0;
/* 1 while chat goes to this player's team only.
 * frontend_net_update_and_draw_chat_panel sets it from its Team and All tabs, which
 * show only once the mission setup roster is authoritative, and otherwise holds
 * it at 0. mission_setup_flight_assignment_update sets it to 1;
 * mission_setup_team_assignment_update, mission_debrief_update,
 * xvt_campaign_task_enter_teams and xvt_campaign_task_enter_debrief reset it to 0. */
// GLOBAL: XVT 0x52BF54
int g_frontend_chat_team_only = 0;
/* Lines the chat log view is scrolled back from its newest line; zeroed on the
 * panel's first frame. Only frontend_net_update_and_draw_chat_panel uses it. */
// GLOBAL: XVT 0x66543C
int g_frontend_chat_scroll_offset = 0;
/* Name of the game selected on the join screen, drawn at the top of the screen
 * and given as the session name when probing it; empty when none. It starts as
 * 32 bytes of 0xFF until the join screen's first frame clears it. 5 functions
 * write it: frontend_net_join_game_screen, frontend_net_probe_session_by_index, and
 * frontend_net_sort_sessions, frontend_net_probe_all_sessions and
 * xvt_network_dialogs_return, which clear it. */
// GLOBAL: XVT 0x665440
char g_frontend_net_selected_game_name[32] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

/* Draws the join screen's game list and returns the index of the row clicked
 * this frame, or -1. The modern build returns xvt_network_browser_draw_list's
 * result instead. The original draws up to 6 rows from
 * g_frontend_net_session_list_scroll_offset, with a scrollbar past 6 games, the
 * selected row highlighted, a key for a password, and for a game needing 8
 * players or fewer the count and the time since its last query. Row colors:
 * gray for another protocol version, shown with its number; red for a full game
 * in flight; yellow for a full game; green for one needing 1 to 8 players;
 * white otherwise. Frame 0 resets the scroll offset; a click plays a sound when
 * datapad sounds are on. */
// FUNCTION: XVT 0x4D7020
int frontend_net_draw_join_game_list(int frame_counter)
{
	(void)frame_counter;
	return xvt_network_browser_draw_list();
}

/* The join screen, run each frame. The modern build hands off to
 * xvt_network_browser_screen. In the original, any transport but IPX goes to
 * frontend_net_connect_to_selected_game_screen, first sending a client with no IP
 * address or phone number to the configuration screen, or back to the concourse
 * when g_frontend_skip_screen_entry_setup is set. For IPX it lists the games found.
 * Frame 0 resets the roster, chat log, game list (unless
 * g_frontend_skip_screen_entry_setup), selection and mission state and draws the
 * background; every 160 frames with nothing selected it refreshes the list.
 * With a game selected it reads frontend packets: a lobby STATE updates the
 * entry and loads the mission list and briefing; a host cancel, or a mission
 * start, replay or return to mission selection, ends the connection and
 * refreshes; a refusal (full, version, password, started) also shows its
 * message. Clicking a row probes and selects it, or drops the selection. Then
 * it draws the roster, briefing, chat, version, player banner and sidebars.
 * Join, offered when the selected game needs players and runs this protocol
 * version, sends the host a JOIN_REQUEST with the version and password and
 * moves to frontend_net_await_join_admission_screen; the back button ends the
 * connection and returns to the concourse. Returns 1 when
 * frontend_handle_common_screen_controls returns 1, else 0. */
// FUNCTION: XVT 0x4D73C0
int frontend_net_join_game_screen(int frame_counter)
{
	return xvt_network_browser_screen(frame_counter);
}

/* Waits for the host's answer to a join request, run each frame, showing the
 * access message, version and player banner. It reads frontend packets; the
 * modern build first shows any admission failure
 * (xvt_network_dialogs_report_admission_failure) and ignores packets not sent by
 * the host. A refusal (full, version, password, roster locked, game started,
 * host cancelled) ends the connection and shows its message; the modern build
 * then continues through xvt_dialog_continue_with and returns its result, while
 * the original opens the configuration screen for a password refusal and
 * returns to the concourse, or to the join screen for IPX. PLAYER_ADMITTED
 * clears g_mp_roster and moves to the mission setup screen. The cancel button,
 * or frame 480 in the original, ends the connection and returns to the join
 * screen. Returns 1 when frontend_handle_common_screen_controls returns 1, else 0
 * unless noted. */
// FUNCTION: XVT 0x4D7E70
int frontend_net_await_join_admission_screen(int frame_counter)
{
	enum {
		ACCESS_TIMEOUT_FRAME = 480,
		PILOT_BANNER_ANIMATION_PERIOD_FRAMES = 32,
	};

	if (frame_counter == 0) {
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
		xvt_network_session_reject();
		struct RECT screen_rect;
		switch (network_result - NET_PACKET_FRONTEND_GAME_STARTED) {
		case NET_PACKET_FRONTEND_GAME_STARTED -
			NET_PACKET_FRONTEND_GAME_STARTED:
		case NET_PACKET_ROSTER_LOCKED -
			NET_PACKET_FRONTEND_GAME_STARTED:
			XVT_LOG_DEBUG("network.refusal_received reason=\"%s\"",
				      network_result == NET_PACKET_ROSTER_LOCKED
					      ? "locked"
					      : "started");
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_552_THIS_GAME_HAS_ALREADY_STARTED),
				frontend_string_get(
					FRONTSTR_553_PLEASE_SELECT_ANOTHER_GAME_TO_JOIN),
				frontend_string_get(
					FRONTSTR_554_SPACE_TRANSLATION_PLACEHOLDER),
				NULL, NULL);
			return xvt_dialog_continue_with(
				xvt_network_dialogs_resume,
				XVT_NETWORK_ACCESS_REJECTED);
			break;
		case NET_PACKET_GAME_FULL - NET_PACKET_FRONTEND_GAME_STARTED:
			XVT_LOG_DEBUG(
				"network.refusal_received reason=\"full\"");
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_543_THE_GAME_YOU_ARE_TRYING_TO_JOIN_IS_FULL),
				frontend_string_get(
					FRONTSTR_544_PLEASE_TRY_ANOTHER_GAME),
				frontend_string_get(
					FRONTSTR_545_SPACE_TRANSLATION_PLACEHOLDER),
				NULL, NULL);
			return xvt_dialog_continue_with(
				xvt_network_dialogs_resume,
				XVT_NETWORK_ACCESS_REJECTED);
			break;
		case NET_PACKET_HOST_CANCELLED -
			NET_PACKET_FRONTEND_GAME_STARTED:
			XVT_LOG_DEBUG(
				"network.refusal_received reason=\"host_cancelled\"");
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_631_THE_CURRENT_GAME_HAS_BEEN),
				frontend_string_get(
					FRONTSTR_632_CANCELLED_BY_THE_HOST),
				frontend_string_get(
					FRONTSTR_633_PLEASE_SELECT_ANOTHER_GAME),
				NULL, NULL);
			return xvt_dialog_continue_with(
				xvt_network_dialogs_resume,
				XVT_NETWORK_ACCESS_REJECTED);
			break;
		case NET_PACKET_VERSION_MISMATCH -
			NET_PACKET_FRONTEND_GAME_STARTED:
			XVT_LOG_DEBUG(
				"network.refusal_received reason=\"version\"");
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_546_YOUR_VERSION_OF_THE_PROGRAM_DOES_NOT),
				frontend_string_get(
					FRONTSTR_547_MATCH_THE_HOSTS_PLEASE_VERIFY_THAT_YOU),
				frontend_string_get(
					FRONTSTR_548_HAVE_THE_CORRECT_PROGRAM),
				NULL, NULL);
			return xvt_dialog_continue_with(
				xvt_network_dialogs_resume,
				XVT_NETWORK_ACCESS_REJECTED);
			break;
		case NET_PACKET_PASSWORD_REQUIRED -
			NET_PACKET_FRONTEND_GAME_STARTED:
			XVT_LOG_DEBUG(
				"network.refusal_received reason=\"password\"");
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_549_THIS_GAME_REQUIRES_A_PASSWORD),
				frontend_string_get(
					FRONTSTR_550_PLEASE_ENTER_THE_CORRECT_PASSWORD_IN),
				frontend_string_get(
					FRONTSTR_551_THE_NETWORK_CONFIGURATION_SCREEN),
				NULL, NULL);
			return xvt_dialog_continue_with(
				xvt_network_dialogs_resume,
				XVT_NETWORK_ACCESS_PASSWORD);
			frontend_draw_rect_assign(&screen_rect, 0, 0, 640, 480);
			frontend_screen_queue_push(
				config_options_datapad_update, &screen_rect);
			break;
		}
		if (g_game_config.network_type != 0) {
			frontend_screen_set_callbacks(concourse_update,
						      concourse_exit);
		} else {
			g_frontend_skip_screen_entry_setup = 1;
			frontend_screen_set_callbacks(
				frontend_net_join_game_screen,
				frontend_mission_list_free_screen_resources);
		}
	} else if (network_result == NET_PACKET_PLAYER_ADMITTED) {
		memset(g_mp_roster, 0, sizeof(g_mp_roster));
		frontend_screen_set_callbacks(mission_setup_update,
					      mission_setup_exit);
	}

	frontend_draw_rect_assign(&rect, 507, 452, 562, 464);
	sprintf(g_frontend_scratch_buffer, "v. %d.%d", 2, 0);
	frontend_text_draw_centered(12, g_frontend_scratch_buffer, &rect,
				    0xFFFF);
	frontend_draw_rect_assign(&rect, 200, 452, 436, 464);
	if (g_pilot_data.name[0] != '\0') {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%s", 6,
			g_pilot_data.rating_name, 1, g_pilot_data.name);
		frontend_text_draw_centered(12, g_frontend_scratch_buffer,
					    &rect, g_color_yellow);
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
	if (frontend_handle_common_screen_controls(0) == 1) {
		return 1;
	}
	if (xvt_dialog_is_active()) {
		return 0;
	}
	if (g_game_config.help_on != 0) {
		frontend_button_enable_overlay_text();
	}
	frontend_button_set_overlay_text(
		frontend_string_get(FRONTSTR_019_CANCEL));
	frontend_draw_rect_assign(&rect, 85, 447, 176, 471);
	int cancel_pressed = frontend_button_handle_sprite_button(
		&rect, "leaveup", "leavedown",
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
	return 0;
}

/* Draws the join screen's mission heading, with the mission's name once a lobby
 * STATE has named it, and then its briefing text from g_mission_text, with a
 * scrollbar past 6 lines; returns 1. The modern build returns
 * xvt_network_browser_draw_mission's result instead. */
// FUNCTION: XVT 0x4D8350
int frontend_net_draw_join_game_mission_briefing(void)
{
	return xvt_network_browser_draw_mission();
}

/* Draws the join screen's player heading and, once a lobby STATE has arrived,
 * each player in g_mp_roster with rating and name, in two columns of 4; returns
 * 1. Calls net_count_ready_players and ignores its result. The modern build
 * returns xvt_network_browser_draw_roster's result instead. */
// FUNCTION: XVT 0x4D8540
int frontend_net_draw_join_game_player_roster(void)
{
	return xvt_network_browser_draw_roster();
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
		int cursor_x;
		int cursor_y;
		frontend_cursor_get_pos(&cursor_x, &cursor_y);
		if (g_frontend_chat_team_only == 0) {
			front_image_draw_sprite("tab2", 0, 0);
			frontend_draw_rect_assign(&rect, 556, 95, 588, 105);
			frontend_text_draw_centered(
				10, frontend_string_get(FRONTSTR_217_TEAM),
				&rect, g_color_gray);
			if (frontend_draw_point_in_rect(&rect, cursor_x,
							cursor_y) &&
			    (frontend_mouse_get_left_click() != 0 ||
			     frontend_mouse_get_right_click() != 0)) {
				if (g_game_config.sfx_datapad_enabled != 0) {
					frontend_sound_play_ui_sound(
						"jewelsound", 1, 0, 255,
						12 * g_game_config
								.sfx_datapad_volume,
						63);
				}
				g_frontend_chat_team_only = 1;
				XVT_LOG_DEBUG(
					"network.chat_channel team_only=%d",
					g_frontend_chat_team_only);
			}
			front_image_draw_sprite("tab1", 0, 0);
			frontend_draw_rect_assign(&rect, 520, 95, 552, 105);
			frontend_text_draw_centered(
				10, frontend_string_get(FRONTSTR_402_ALL),
				&rect, 0xFFFF);
		} else {
			front_image_draw_sprite("tab1", 0, 0);
			frontend_draw_rect_assign(&rect, 520, 95, 552, 105);
			frontend_text_draw_centered(
				10, frontend_string_get(FRONTSTR_402_ALL),
				&rect, g_color_gray);
			if (frontend_draw_point_in_rect(&rect, cursor_x,
							cursor_y) &&
			    (frontend_mouse_get_left_click() != 0 ||
			     frontend_mouse_get_right_click() != 0)) {
				if (g_game_config.sfx_datapad_enabled != 0) {
					frontend_sound_play_ui_sound(
						"jewelsound", 1, 0, 255,
						12 * g_game_config
								.sfx_datapad_volume,
						63);
				}
				g_frontend_chat_team_only = 0;
				XVT_LOG_DEBUG(
					"network.chat_channel team_only=%d",
					g_frontend_chat_team_only);
			}
			front_image_draw_sprite("tab2", 0, 0);
			frontend_draw_rect_assign(&rect, 556, 95, 588, 105);
			frontend_text_draw_centered(
				10, frontend_string_get(FRONTSTR_217_TEAM),
				&rect, 0xFFFF);
		}
	} else {
		g_frontend_chat_team_only = 0;
	}

	frontend_draw_rect_assign(&rect, 461, 406, 595, 424);
	if (frontend_text_handle_editable_field(&rect,
						g_frontend_chat_input_buffer,
						100, 0, 12, NULL) != 0 &&
	    g_frontend_chat_input_buffer[0] != 0) {
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
			size_t message_length =
				strlen(g_frontend_scratch_buffer);
			g_frontend_scratch_buffer[message_length] = 1;
			g_frontend_scratch_buffer[message_length + 1] = 0;
		}
		strcat(g_frontend_scratch_buffer, g_frontend_chat_input_buffer);
		memcpy(g_frontend_net_packet_scratch.payload,
		       g_frontend_scratch_buffer,
		       strlen(g_frontend_scratch_buffer));
		if (team_message != 0) {
			int player_index = 0;
			while (player_index < 8 &&
			       net_get_local_player_id() !=
				       g_mission_setup_player_assignments
					       .assigned_player_ids
						       [player_index]) {
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
							.team_player_ids
								[g_pilot_data
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
			net_send_packet_and_flush(
				0, &g_frontend_net_packet_scratch,
				strlen(g_frontend_scratch_buffer) + 4);
			XVT_LOG_DEBUG(
				"network.chat_sent to=\"everyone\" team=%d ready=%d",
				g_pilot_data.team,
				g_frontend_scratch_buffer[0] != 4);
		}
		memset(g_frontend_chat_input_buffer, 0,
		       sizeof(g_frontend_chat_input_buffer));
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
 * pressed while the query button is held, and the query button; a click probes
 * every listed game (frontend_net_probe_all_sessions), or in the modern build,
 * where the button reads Refresh, calls xvt_network_task_refresh. Returns 1. */
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
 * (g_host_game_start_pending, g_frontend_quick_start_launch_flag and others), starts
 * from the last host name as the game name, clears the chat log and roster, and
 * draws the background. Until the name is confirmed, by Enter or Tab in the
 * field or the Host button, it edits the name (up to 22 characters, this
 * player's name and a suffix when left empty) and offers the back button to the
 * concourse. On the next frame it keeps the name as the next default and starts
 * hosting: the modern build hands off to xvt_network_task_begin; the original
 * starts a DirectPlay session as host on the configured transport, then marks
 * itself ready, makes itself the only roster entry and moves to the mission
 * setup screen, or on failure returns to the concourse. Returns 1 when
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

/* Reads one frontend packet (net_get_next_app_packet) and acts on it; returns its
 * type, or NET_PACKET_NONE when there is none, the type is unknown, or the
 * packet was refused. First, when a ready player left this frame, it sends
 * everyone the lobby state. Each packet's sender goes in
 * g_frontend_net_packet_sender_player_id. A PROBE_REQUEST gets the lobby state and a
 * PROBE_RESPONSE (version and password flag); a PROBE_RESPONSE or game-started
 * notice fills the g_frontendNetProbe globals. A lobby STATE or READY_ROSTER
 * rebuilds g_mp_roster and the ready flags, a STATE also
 * g_pilot_data.multiplayer_game_name and g_frontend_net_received_mission_directory_id
 * and DescriptionId. A JOIN_REQUEST is refused (roster locked, full, version,
 * password) or admitted, telling all players, resending the lobby state and
 * setting g_mission_setup_begin_button_lockout_frames to 24; the modern build drops
 * one that reaches a client or has the wrong size. Team and flight assignments,
 * ready flags, game options, loadouts, network statistics, mission starts,
 * choices, countdowns and reservations are stored in
 * g_mission_setup_player_assignments, g_mission_setup_player_flight_group_indices,
 * g_mp_roster_ready_flags, the g_missionSetupSelected globals, g_mp_roster,
 * g_game_config, g_pilot_data and g_frontend_net_packet_arg0 and
 * g_frontend_net_reserving_player_id. A CHAT line is added to
 * g_frontend_chat_log_buffer, dropping the oldest bytes past 1,022, with this
 * player's own lines marked by color code 5; a CHAT_SYNC_REQUEST gets the log
 * in 400-byte chunks, and a received chunk rebuilds it with every byte from 0
 * to 6 turned into color code 6. A PLAYER_UNAVAILABLE always returns
 * NET_PACKET_NONE. Also handles movie sync (g_movie_multiplayer_sync_players),
 * battle progress (the g_remoteBattle globals), briefing arrivals
 * (g_frontend_briefing_entered_count), RETURN_TO_SETUP
 * (g_frontend_skip_screen_entry_setup) and player departures, relays a
 * SUBMIT_MISSION_CHOICE to all as a MISSION_CHOICE, and writes
 * g_frontend_net_packet_scratch. Does not check team, slot or chunk indices or
 * chat sizes from the packet, or the chat log for NULL before a sync chunk. */
// FUNCTION: XVT 0x4E0490
int frontend_net_process_network_packets(void)
{
	enum {
		MAX_PLAYERS = 8,
		TEAM_COUNT = 10,
		PLAYER_NAME_COPY_SIZE = 13,
		ROSTER_PACKET_FIRST_PLAYER_WORD = 13,
		ROSTER_PACKET_COMPACT_FIRST_PLAYER_WORD = 2,
		ROSTER_OPTION_WORD_COUNT = 5,
		CHAT_LOG_CAPACITY = 1024,
		CHAT_LOG_CONTENT_LIMIT = 1022,
		CHAT_SYNC_CHUNK_SIZE = 400,
		FLIGHT_GROUP_ASSIGNMENT_COUNT = 80,
		MISSION_ASSIGNMENT_TEAM_BYTES = 320,
		MISSION_SETUP_PLAYER_LIMIT = 8,
		BEGIN_BUTTON_LOCKOUT_FRAMES = 24,
		MIN_PRESET_CRAFT_CATEGORY = 1,
		MAX_STANDARD_PRESET_CRAFT_CATEGORY = 2,
		SPECIAL_PRESET_CRAFT_CATEGORY = 3,
		SPECIAL_PRESET_CRAFT_OFFSET = 5,
	};

	int *packet;
	int *payload;
	uint8_t *payload_bytes;
	uint8_t *chat_chunk_bytes;
	uint32_t packet_size;
	DPID sender_player_id;
	struct net_player_info *net_player;
	int packet_type;
	int packet_word_index;
	int roster_index;
	int player_index;
	int team_index;
	int slot_index;
	int source_slot;
	int destination_slot;
	int ready_player_count;
	int bytes_to_discard;
	int chunk_index;
	int chat_offset;
	int remaining_bytes;
	int stats_count;
	int craft_option;
	int movie_player_index;
	int assignment_index;

	net_get_host_player_id();
	packet = net_get_next_app_packet(&sender_player_id, &packet_size);
	if (net_did_ready_player_leave_this_frame() != 0) {
		mission_setup_send_lobby_state(0);
		XVT_LOG_DEBUG("network.lobby_state_resent");
	}
	if (packet == NULL) {
		return 0;
	}

	packet_type = packet[0];
	g_frontend_net_packet_sender_player_id = sender_player_id;
	payload = &packet[1];
	payload_bytes = (uint8_t *)payload;
	switch (packet_type) {
	case NET_PACKET_FRONTEND_GAME_STARTED:
	case NET_PACKET_PROBE_RESPONSE:
		g_frontend_net_probe_mission_elapsed_seconds = payload[0];
		g_frontend_net_probe_version = payload[1];
		g_frontend_net_probe_password_required = payload[2];
		XVT_LOG_DEBUG(
			"network.game_status seconds=%d version=%d password=%d",
			g_frontend_net_probe_mission_elapsed_seconds,
			g_frontend_net_probe_version,
			g_frontend_net_probe_password_required);
		break;

	case NET_PACKET_PROBE_REQUEST:
		mission_setup_send_lobby_state(sender_player_id);
		g_frontend_net_packet_scratch.packet_type =
			NET_PACKET_PROBE_RESPONSE;
		*(int *)&g_frontend_net_packet_scratch.payload[0] = 0;
		*(int *)&g_frontend_net_packet_scratch.payload[4] =
			FRONTEND_NET_PROTOCOL_VERSION;
		*(int *)&g_frontend_net_packet_scratch.payload[8] =
			g_game_config.require_password;
		net_send_packet_and_flush(sender_player_id,
					  &g_frontend_net_packet_scratch,
					  4 * sizeof(int));
		XVT_LOG_DEBUG("network.status_query player=%u password=%u",
			      (unsigned)sender_player_id,
			      (unsigned)g_game_config.require_password);
		break;

	case NET_PACKET_STATE:
		net_clear_player_ready_flags();
		memcpy(g_pilot_data.multiplayer_game_name, payload,
		       sizeof(g_pilot_data.multiplayer_game_name));
		g_frontend_net_received_mission_directory_id = payload[9];
		g_frontend_net_received_mission_description_id = payload[10];
		g_frontend_net_probe_players_needed = payload[11] - payload[12];
		ready_player_count = payload[12];
		XVT_LOG_DEBUG(
			"network.lobby_state game=\"%.32s\" directory=%d mission=%d players=%d needed=%d",
			g_pilot_data.multiplayer_game_name,
			g_frontend_net_received_mission_directory_id,
			g_frontend_net_received_mission_description_id,
			ready_player_count,
			g_frontend_net_probe_players_needed);
		packet_word_index = ROSTER_PACKET_FIRST_PLAYER_WORD;
		memset(g_mp_roster, 0, sizeof(g_mp_roster));
		for (roster_index = 0; roster_index < ready_player_count;
		     ++roster_index) {
			if (payload[packet_word_index] == 0) {
				++packet_word_index;
				g_mp_roster[roster_index].player_id = 0;
				g_mp_roster[roster_index].pilot_rating =
					payload[packet_word_index++];
				++packet_word_index;
			} else {
				net_player = net_find_player(
					payload[packet_word_index]);
				if (net_player != NULL) {
					net_mark_player_ready_no_lock(
						payload[packet_word_index]);
					strncpy(g_mp_roster[roster_index].name,
						net_player->player_name,
						PLAYER_NAME_COPY_SIZE);
					g_mp_roster[roster_index].player_id =
						net_player->player_id;
					++packet_word_index;
					g_mp_roster[roster_index].pilot_rating =
						payload[packet_word_index];
					++packet_word_index;
					if (net_is_host() == 0) {
						net_set_player_latency_ms(
							g_mp_roster[roster_index]
								.player_id,
							payload[packet_word_index]);
					}
					++packet_word_index;
				} else {
					memcpy(g_mp_roster[roster_index].name,
					       "No name", sizeof("No name"));
					g_mp_roster[roster_index].player_id =
						payload[packet_word_index];
					++packet_word_index;
					g_mp_roster[roster_index].pilot_rating =
						payload[packet_word_index];
					packet_word_index += 2;
				}
			}
			XVT_LOG_DEBUG(
				"network.roster_entry index=%d player=%u rating=%d latency=%d name=\"%.14s\"",
				roster_index,
				(unsigned)g_mp_roster[roster_index].player_id,
				(int)g_mp_roster[roster_index].pilot_rating,
				payload[packet_word_index - 1],
				g_mp_roster[roster_index].name);
		}
		break;

	case NET_PACKET_JOIN_REQUEST:
		if (!net_is_host() || packet_size != 6 * sizeof(int)) {
			XVT_LOG_WARN(
				"network.join_request_dropped player=%u bytes=%u",
				(unsigned)sender_player_id,
				(unsigned)packet_size);
			packet_type = NET_PACKET_NONE;
			break;
		}
		ready_player_count = net_count_ready_players();
		if (g_mission_setup_roster_authoritative != 0) {
			XVT_LOG_WARN(
				"network.join_request_refused player=%u reason=\"locked\" version=%d",
				(unsigned)sender_player_id, payload[0]);
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_ROSTER_LOCKED;
			net_send_packet_and_flush(
				sender_player_id,
				&g_frontend_net_packet_scratch, sizeof(int));
		} else if (ready_player_count >= MAX_PLAYERS) {
			XVT_LOG_WARN(
				"network.join_request_refused player=%u reason=\"full\" version=%d",
				(unsigned)sender_player_id, payload[0]);
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_GAME_FULL;
			net_send_packet_and_flush(
				sender_player_id,
				&g_frontend_net_packet_scratch, sizeof(int));
		} else if (payload[0] != FRONTEND_NET_PROTOCOL_VERSION) {
			XVT_LOG_WARN(
				"network.join_request_refused player=%u reason=\"version\" version=%d",
				(unsigned)sender_player_id, payload[0]);
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_VERSION_MISMATCH;
			net_send_packet_and_flush(
				sender_player_id,
				&g_frontend_net_packet_scratch, sizeof(int));
		} else if (g_game_config.require_password != 0 &&
			   strncmp(g_game_config.password,
				   (const char *)&payload[1],
				   sizeof(g_game_config.password)) != 0) {
			XVT_LOG_WARN(
				"network.join_request_refused player=%u reason=\"password\" version=%d",
				(unsigned)sender_player_id, payload[0]);
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_PASSWORD_REQUIRED;
			net_send_packet_and_flush(
				sender_player_id,
				&g_frontend_net_packet_scratch, sizeof(int));
		} else if (net_set_player_ready(sender_player_id) != 0) {
			*(int *)&g_frontend_net_packet_scratch.payload[0] =
				sender_player_id;
			g_mission_setup_begin_button_lockout_frames =
				BEGIN_BUTTON_LOCKOUT_FRAMES;
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_PLAYER_ADMITTED;
			net_send_packet_and_flush(
				0, &g_frontend_net_packet_scratch,
				2 * sizeof(int));
			mission_setup_send_lobby_state(0);
			XVT_LOG_INFO(
				"network.player_admitted player=%u players=%d",
				(unsigned)sender_player_id,
				ready_player_count + 1);
		} else {
			XVT_LOG_WARN(
				"network.join_request_refused player=%u reason=\"not_listed\" version=%d",
				(unsigned)sender_player_id, payload[0]);
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_GAME_FULL;
			net_send_packet_and_flush(
				sender_player_id,
				&g_frontend_net_packet_scratch, sizeof(int));
		}
		break;

	case NET_PACKET_GAME_FULL:
	case NET_PACKET_HOST_CANCELLED:
	case NET_PACKET_PLAYER_KICKED:
	case NET_PACKET_SESSION_CANCELLED:
	case NET_PACKET_VERSION_MISMATCH:
	case NET_PACKET_PASSWORD_REQUIRED:
	case NET_PACKET_ROSTER_LOCKED:
	case NET_PACKET_FLIGHT_ASSIGNMENTS_READY:
		break;

	case NET_PACKET_PLAYER_ADMITTED:
		if (packet_size < 2 * sizeof(int) ||
		    sender_player_id != (DPID)net_get_host_player_id() ||
		    (xvt_network_session_get_status().state ==
			     XVT_NETWORK_SESSION_ADMISSION &&
		     !xvt_network_session_accept_admission(sender_player_id,
							   (DPID)payload[0]))) {
			XVT_LOG_WARN(
				"network.admission_rejected player=%u bytes=%u",
				(unsigned)sender_player_id,
				(unsigned)packet_size);
			packet_type = NET_PACKET_NONE;
			break;
		}
		for (roster_index = 0; roster_index < MAX_PLAYERS;
		     ++roster_index) {
			if (g_mp_roster[roster_index].player_id != 0 &&
			    g_mp_roster[roster_index].player_id ==
				    net_get_local_player_id()) {
				break;
			}
		}
		if (roster_index >= MAX_PLAYERS &&
		    net_get_local_player_id() != payload[0]) {
			XVT_LOG_DEBUG("network.admission_ignored player=%u",
				      (unsigned)payload[0]);
			packet_type = NET_PACKET_NONE;
			break;
		}
		if (g_game_config.sfx_datapad_enabled != 0) {
			frontend_sound_play_ui_sound(
				"newpsound", 1, 0, 255,
				12 * g_game_config.sfx_datapad_volume, 63);
		}
		net_mark_player_ready_no_lock(payload[0]);
		XVT_LOG_DEBUG("network.admission_received player=%u",
			      (unsigned)payload[0]);
		break;

	case NET_PACKET_PLAYER_LEFT:
		ready_player_count = net_count_ready_players();
		roster_index = 0;
		if (ready_player_count > 0) {
			do {
				if ((DPID)g_mp_roster[roster_index].player_id ==
				    sender_player_id) {
					if (g_game_config.sfx_datapad_enabled !=
					    0) {
						frontend_sound_play_ui_sound(
							"exitpsound", 1, 0, 255,
							12 * g_game_config
									.sfx_datapad_volume,
							63);
					}
					memset(g_mp_roster_ready_flags, 0,
					       sizeof(g_mp_roster_ready_flags));
					net_clear_player_ready_flag_with_lock_guard(
						sender_player_id);
					mission_setup_send_lobby_state(0);
					break;
				}
				++roster_index;
			} while (roster_index < ready_player_count);
		}
		g_frontend_net_packet_scratch.packet_type =
			NET_PACKET_MOVIE_SYNC;
		*(int *)&g_frontend_net_packet_scratch.payload[0] = 2;
		*(int *)&g_frontend_net_packet_scratch.payload[4] =
			sender_player_id;
		net_send_packet_and_flush(0, &g_frontend_net_packet_scratch,
					  3 * sizeof(int));
		XVT_LOG_DEBUG(
			"network.leave_notice player=%u index=%d ready=%d",
			(unsigned)sender_player_id, roster_index,
			ready_player_count);
		break;

	case NET_PACKET_TEAM_ASSIGNMENTS_READY:
		memcpy(&g_mission_setup_player_assignments, payload,
		       MISSION_ASSIGNMENT_TEAM_BYTES);
		break;

	case NET_PACKET_FRONTEND_MISSION_START:
		g_pilot_data.mission_directory_id =
			(mission_directory_id)payload[1];
		g_pilot_data.mission_sequence_active = payload[2];
		if (g_pilot_data.mission_sequence_active == 1) {
			g_pilot_data.saved_mission_description_id =
				g_pilot_data.mission_description_ids
					[g_pilot_data.mission_directory_id];
			if (g_pilot_data.mission_directory_id ==
			    MISSION_DIRECTORY_MELEES) {
				g_pilot_data.melee_tournament_sequence_state
					.mission_count = payload[3];
			} else if (g_pilot_data.mission_directory_id ==
				   MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
				g_pilot_data.battle_sequence_state
					.victories_needed = payload[3];
			} else {
				g_pilot_data.campaign_sequence_state
					.mission_count = payload[3];
			}
		}
		g_pilot_data.mission_description_ids
			[g_pilot_data.mission_directory_id] = payload[0];
		mission_setup_load_mission_list(
			g_pilot_data.mission_directory_id);
		if (g_mission_list != NULL) {
			g_selected_mission_list_index = 0;
			while ((unsigned int)g_selected_mission_list_index <
				       g_mission_count &&
			       g_mission_list[g_selected_mission_list_index]
					       .mission_idx !=
				       g_pilot_data.mission_description_ids
					       [g_pilot_data
							.mission_directory_id]) {
				++g_selected_mission_list_index;
			}
		}
		XVT_LOG_DEBUG(
			"network.mission_start directory=%d mission=%d sequence=%d count=%d listed=%d missions=%u index=%d",
			(int)g_pilot_data.mission_directory_id, payload[0],
			g_pilot_data.mission_sequence_active,
			g_pilot_data.mission_sequence_active == 1 ? payload[3]
								  : 0,
			g_mission_list != NULL, (unsigned)g_mission_count,
			g_selected_mission_list_index);
		break;

	case NET_PACKET_NEXT_TOURNAMENT_MISSION:
	case NET_PACKET_NEXT_BATTLE_MISSION:
	case NET_PACKET_REPLAY_CURRENT_MISSION:
	case NET_PACKET_NEXT_CAMPAIGN_MISSION:
	case NET_PACKET_REPLAY_CAMPAIGN_MISSION:
		g_game_config.random_seed = payload[0];
		XVT_LOG_DEBUG("network.seed_received seed=%u",
			      (unsigned)g_game_config.random_seed);
		break;

	case NET_PACKET_CHAT:
		if (g_frontend_chat_log_buffer != NULL) {
			packet_size -= sizeof(int);
			bytes_to_discard = CHAT_LOG_CONTENT_LIMIT;
			bytes_to_discard -= g_frontend_chat_log_used_bytes;
			bytes_to_discard -= (int)packet_size;
			if (bytes_to_discard < 0) {
				bytes_to_discard = -bytes_to_discard;
				memmove(g_frontend_chat_log_buffer,
					&g_frontend_chat_log_buffer
						[bytes_to_discard],
					CHAT_LOG_CAPACITY - bytes_to_discard);
				g_frontend_chat_log_used_bytes -=
					bytes_to_discard;
			}
			memcpy(&g_frontend_chat_log_buffer
				       [g_frontend_chat_log_used_bytes],
			       payload, packet_size);
			if ((DPID)net_get_local_player_id() ==
			    sender_player_id) {
				g_frontend_chat_log_buffer
					[g_frontend_chat_log_used_bytes] = 5;
			}
			g_frontend_chat_log_used_bytes += packet_size;
			g_frontend_chat_log_buffer
				[g_frontend_chat_log_used_bytes++] = '\n';
			g_frontend_chat_log_buffer
				[g_frontend_chat_log_used_bytes] = '\0';
			XVT_LOG_DEBUG(
				"network.chat_received player=%u bytes=%u used=%d",
				(unsigned)sender_player_id,
				(unsigned)packet_size,
				g_frontend_chat_log_used_bytes);
		} else {
			XVT_LOG_WARN(
				"network.chat_dropped player=%u kind=\"chat_line\"",
				(unsigned)sender_player_id);
		}
		break;

	case NET_PACKET_TEAM_ASSIGNMENT:
		XVT_LOG_DEBUG(
			"network.team_assignment player=%u team=%d place=%d",
			(unsigned)payload[0], payload[1], payload[2]);
		if (net_is_host() != 0) {
			break;
		}
		if (payload[1] == 10) {
			for (player_index = 0; player_index < MAX_PLAYERS;
			     ++player_index) {
				if (g_mission_setup_player_assignments
					    .assigned_player_ids
						    [player_index] ==
				    payload[0]) {
					g_mission_setup_player_assignments
						.assigned_player_ids
							[player_index] = 0;
					break;
				}
			}
			for (team_index = 0; team_index < TEAM_COUNT;
			     ++team_index) {
				for (slot_index = 0; slot_index < MAX_PLAYERS;
				     ++slot_index) {
					if (g_mission_setup_player_assignments
						    .team_player_ids
							    [team_index]
							    [slot_index] ==
					    payload[0]) {
						for (source_slot =
							     slot_index + 1;
						     source_slot < MAX_PLAYERS;
						     ++source_slot) {
							g_mission_setup_player_assignments
								.team_player_ids
									[team_index]
									[source_slot -
									 1] =
								g_mission_setup_player_assignments
									.team_player_ids
										[team_index]
										[source_slot];
						}
						g_mission_setup_player_assignments
							.team_player_ids
								[team_index]
								[MAX_PLAYERS -
								 1] = 0;
						break;
					}
				}
			}
			break;
		}
		for (roster_index = 0; roster_index < MAX_PLAYERS;
		     ++roster_index) {
			if (g_mp_roster[roster_index].player_id != 0 &&
			    g_mp_roster[roster_index].player_id ==
				    net_get_local_player_id()) {
				break;
			}
		}
		if (roster_index < MAX_PLAYERS &&
		    g_game_config.sfx_datapad_enabled != 0) {
			frontend_sound_play_ui_sound(
				"slotsound", 1, 0, 255,
				12 * g_game_config.sfx_datapad_volume, 63);
		}
		for (player_index = 0; player_index < MAX_PLAYERS;
		     ++player_index) {
			if (g_mission_setup_player_assignments
				    .assigned_player_ids[player_index] ==
			    payload[0]) {
				break;
			}
		}
		if (player_index == MAX_PLAYERS) {
			for (player_index = 0; player_index < MAX_PLAYERS;
			     ++player_index) {
				if (g_mission_setup_player_assignments
					    .assigned_player_ids
						    [player_index] == 0) {
					g_mission_setup_player_assignments
						.assigned_player_ids
							[player_index] =
						payload[0];
					break;
				}
			}
		}
		for (slot_index = 0; slot_index < MAX_PLAYERS; ++slot_index) {
			if (g_mission_setup_player_assignments
				    .team_player_ids[payload[1]][slot_index] ==
			    payload[0]) {
				break;
			}
		}
		if (slot_index == MAX_PLAYERS) {
			destination_slot = payload[2];
			for (source_slot = MAX_PLAYERS - 1;
			     source_slot > destination_slot; --source_slot) {
				g_mission_setup_player_assignments
					.team_player_ids[payload[1]]
							[source_slot] =
					g_mission_setup_player_assignments
						.team_player_ids[payload[1]]
								[source_slot -
								 1];
			}
			g_mission_setup_player_assignments
				.team_player_ids[payload[1]][destination_slot] =
				payload[0];
		}
		break;

	case NET_PACKET_FLIGHT_ASSIGNMENT_NOTIFY:
		if (payload[2] != -1 &&
		    g_game_config.sfx_datapad_enabled != 0) {
			for (slot_index = 0; slot_index < MAX_PLAYERS;
			     ++slot_index) {
				if (g_mission_setup_player_assignments
					    .team_player_ids[payload[0]]
							    [slot_index] ==
				    net_get_local_player_id()) {
					break;
				}
			}
			if (slot_index != MAX_PLAYERS) {
				frontend_sound_play_ui_sound(
					"slotsound", 1, 0, 255,
					12 * g_game_config.sfx_datapad_volume,
					63);
			}
		}
		/* Continue with the shared flight-group assignment. */

	case NET_PACKET_FLIGHT_ASSIGNMENT:
		XVT_LOG_DEBUG(
			"network.flight_assignment team=%d place=%d group=%d",
			payload[0], payload[1], payload[2]);
		g_mission_setup_player_flight_group_indices
			[payload[0] * MAX_PLAYERS + payload[1]] = payload[2];
		break;

	case NET_PACKET_PLAYER_READY:
		for (roster_index = 0; roster_index < MAX_PLAYERS;
		     ++roster_index) {
			if ((DPID)g_mp_roster[roster_index].player_id ==
			    sender_player_id) {
				g_mp_roster_ready_flags[roster_index] = 1;
				break;
			}
		}
		XVT_LOG_DEBUG(
			"network.ready_changed player=%u ready=1 index=%d",
			(unsigned)sender_player_id, roster_index);
		break;

	case NET_PACKET_PLAYER_UNREADY:
		for (roster_index = 0; roster_index < MAX_PLAYERS;
		     ++roster_index) {
			if ((DPID)g_mp_roster[roster_index].player_id ==
			    sender_player_id) {
				g_mp_roster_ready_flags[roster_index] = 0;
				break;
			}
		}
		XVT_LOG_DEBUG(
			"network.ready_changed player=%u ready=0 index=%d",
			(unsigned)sender_player_id, roster_index);
		break;

	case NET_PACKET_RETURN_TO_SETUP:
		g_frontend_skip_screen_entry_setup = 1;
		break;

	case NET_PACKET_CLEAR_TEAM_ASSIGNMENTS:
		memset(&g_mission_setup_player_assignments, 0,
		       MISSION_ASSIGNMENT_TEAM_BYTES);
		memset(g_mission_setup_player_assignments.assigned_player_ids,
		       0,
		       sizeof(g_mission_setup_player_assignments
				      .assigned_player_ids));
		break;

	case NET_PACKET_TEAM_ASSIGNMENTS:
		memcpy(g_mission_setup_player_assignments.team_player_ids,
		       payload,
		       sizeof(g_mission_setup_player_assignments
				      .team_player_ids));
		memcpy(g_mission_setup_player_assignments.assigned_player_ids,
		       &payload[sizeof(g_mission_setup_player_assignments
					       .team_player_ids) /
				sizeof(payload[0])],
		       sizeof(g_mission_setup_player_assignments
				      .assigned_player_ids));
		break;

	case NET_PACKET_CLEAR_FLIGHT_ASSIGNMENTS:
		memset(&g_mission_setup_player_flight_group_indices
			       [payload[0] * MAX_PLAYERS],
		       0xFF,
		       MAX_PLAYERS *
			       sizeof(g_mission_setup_player_flight_group_indices
					      [0]));
		XVT_LOG_DEBUG("network.flight_groups_cleared team=%d",
			      payload[0]);
		break;

	case NET_PACKET_FLIGHT_ASSIGNMENTS:
		memcpy(&g_mission_setup_player_flight_group_indices
			       [payload[0] * MAX_PLAYERS],
		       &payload[1],
		       MAX_PLAYERS *
			       sizeof(g_mission_setup_player_flight_group_indices
					      [0]));
		XVT_LOG_DEBUG(
			"network.flight_groups team=%d groups=\"%d,%d,%d,%d,%d,%d,%d,%d\"",
			payload[0], payload[1], payload[2], payload[3],
			payload[4], payload[5], payload[6], payload[7],
			payload[8]);
		break;

	case NET_PACKET_MISSION_CHOICE:
		g_frontend_net_packet_arg0 = payload[0];
		XVT_LOG_DEBUG(
			"network.setup_value kind=\"mission_choice\" value=%d",
			g_frontend_net_packet_arg0);
		break;

	case NET_PACKET_BRIEFING_COUNTDOWN:
	case NET_PACKET_ASSIGNMENT_COUNTDOWN:
		g_frontend_net_packet_arg0 = payload[0];
		XVT_LOG_DEBUG("network.setup_value kind=\"countdown\" value=%d",
			      g_frontend_net_packet_arg0);
		break;

	case NET_PACKET_RETURN_TO_MISSION_SELECTION:
		break;

	case NET_PACKET_RELEASE_TEAM_RESERVATION:
	case NET_PACKET_RELEASE_FLIGHT_RESERVATION:
		g_frontend_net_packet_arg0 = payload[0];
		XVT_LOG_DEBUG(
			"network.setup_value kind=\"released_reservation\" value=%d",
			g_frontend_net_packet_arg0);
		break;

	case NET_PACKET_GAME_OPTIONS:
		for (roster_index = 0; roster_index < MAX_PLAYERS;
		     ++roster_index) {
			if (g_mp_roster[roster_index].player_id != 0 &&
			    g_mp_roster[roster_index].player_id ==
				    net_get_local_player_id()) {
				break;
			}
		}
		if (roster_index < MAX_PLAYERS) {
			g_game_config.difficulty = payload_bytes[0];
			g_game_config.collisions = payload_bytes[4];
			g_game_config.craft_jumping = payload_bytes[8];
			g_game_config.random_setup = payload_bytes[12];
			g_game_config.battle_length_index = payload_bytes[16];
			g_game_config.in_progress_join = payload_bytes[24];
			g_game_config.craft_selection = payload_bytes[28];
			g_game_config.locate_players = payload_bytes[32];
			g_game_config.craft_waves = payload_bytes[36];
			g_game_config.mission_time_limit = payload_bytes[40];
			g_game_config.last_team_time_limit_minutes =
				payload_bytes[44];
			g_game_config.random_seed = payload[12];
			g_game_config.internet_play = payload_bytes[52];
			g_game_config.ai_opponents = payload_bytes[56];
			g_game_config.server_update_rate = payload_bytes[60];
			g_game_config.combat_balance = payload_bytes[64];
			g_game_config.continue_battle_or_campaign =
				payload_bytes[68];
		}
		XVT_LOG_DEBUG(
			"network.game_options applied=%d difficulty=%u collisions=%u jumping=%u random=%u length=%u join=%u craft=%u locate=%u waves=%u time_limit=%u team_limit=%u seed=%u internet=%u ai=%u rate=%u balance=%u continue=%u",
			roster_index < MAX_PLAYERS, (unsigned)payload_bytes[0],
			(unsigned)payload_bytes[4], (unsigned)payload_bytes[8],
			(unsigned)payload_bytes[12],
			(unsigned)payload_bytes[16],
			(unsigned)payload_bytes[24],
			(unsigned)payload_bytes[28],
			(unsigned)payload_bytes[32],
			(unsigned)payload_bytes[36],
			(unsigned)payload_bytes[40],
			(unsigned)payload_bytes[44], (unsigned)payload[12],
			(unsigned)payload_bytes[52],
			(unsigned)payload_bytes[56],
			(unsigned)payload_bytes[60],
			(unsigned)payload_bytes[64],
			(unsigned)payload_bytes[68]);
		break;

	case NET_PACKET_REPLAY_MISSION:
		g_game_config.random_seed = payload[0];
		XVT_LOG_DEBUG("network.seed_received seed=%u",
			      (unsigned)g_game_config.random_seed);
		break;

	case NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS:
		packet_word_index = 0;
		for (roster_index = 0; roster_index < MAX_PLAYERS;
		     ++roster_index) {
			g_mp_roster[roster_index].craft_type_override =
				payload[packet_word_index++];
			g_mp_roster[roster_index].craft_option_index =
				payload[packet_word_index++];
			g_mp_roster[roster_index].warhead_option_index =
				payload[packet_word_index++];
			g_mp_roster[roster_index].beam_option_index =
				payload[packet_word_index++];
			g_mp_roster[roster_index].countermeasure_option_index =
				payload[packet_word_index++];
			XVT_LOG_DEBUG(
				"network.loadout index=%d craft=%d option=%d warhead=%d beam=%d countermeasure=%d",
				roster_index,
				g_mp_roster[roster_index].craft_type_override,
				g_mp_roster[roster_index].craft_option_index,
				g_mp_roster[roster_index].warhead_option_index,
				g_mp_roster[roster_index].beam_option_index,
				g_mp_roster[roster_index]
					.countermeasure_option_index);
		}
		for (assignment_index = 0;
		     assignment_index < FLIGHT_GROUP_ASSIGNMENT_COUNT;
		     ++assignment_index) {
			g_mission_setup_player_flight_group_indices
				[assignment_index] =
					((uint8_t *)&payload[packet_word_index])
						[assignment_index];
		}
		break;

	case NET_PACKET_LOADOUT_ROSTER:
		packet_word_index = 0;
		for (roster_index = 0; roster_index < MAX_PLAYERS;
		     ++roster_index) {
			g_mp_roster[roster_index].craft_type_override =
				payload[packet_word_index++];
			g_mp_roster[roster_index].craft_option_index =
				payload[packet_word_index++];
			g_mp_roster[roster_index].warhead_option_index =
				payload[packet_word_index++];
			g_mp_roster[roster_index].beam_option_index =
				payload[packet_word_index++];
			g_mp_roster[roster_index].countermeasure_option_index =
				payload[packet_word_index++];
			XVT_LOG_DEBUG(
				"network.loadout index=%d craft=%d option=%d warhead=%d beam=%d countermeasure=%d",
				roster_index,
				g_mp_roster[roster_index].craft_type_override,
				g_mp_roster[roster_index].craft_option_index,
				g_mp_roster[roster_index].warhead_option_index,
				g_mp_roster[roster_index].beam_option_index,
				g_mp_roster[roster_index]
					.countermeasure_option_index);
		}
		/* Continue with the network statistics carried by this packet. */

	case NET_PACKET_LOBBY_SELECTION:
		stats_count = payload[12];
		packet_word_index = 13;
		while (stats_count > 0) {
			if (net_is_host() == 0) {
				XVT_LOG_DEBUG(
					"network.player_stats player=%u latency=%d packets=%d drops=%d retries=%d",
					(unsigned)payload[packet_word_index],
					payload[packet_word_index + 2],
					payload[packet_word_index + 3],
					payload[packet_word_index + 4],
					payload[packet_word_index + 5]);
				net_set_player_latency_ms(
					payload[packet_word_index],
					payload[packet_word_index + 2]);
				net_set_player_packet_count(
					payload[packet_word_index],
					payload[packet_word_index + 3]);
				net_set_player_packet_drop_count(
					payload[packet_word_index],
					payload[packet_word_index + 4]);
				net_set_player_packet_retry_count(
					payload[packet_word_index],
					payload[packet_word_index + 5]);
			}
			packet_word_index += 6;
			--stats_count;
		}
		break;

	case NET_PACKET_CRAFT_LOADOUT:
		XVT_LOG_DEBUG(
			"network.craft_loadout player=%u category=%d craft=%d option=%d warhead=%d beam=%d countermeasure=%d waves=%d count=%d",
			(unsigned)sender_player_id, payload[0], payload[1],
			payload[2], payload[3], payload[4], payload[5],
			payload[6], payload[7]);
		for (roster_index = 0; roster_index < MAX_PLAYERS;
		     ++roster_index) {
			if ((DPID)g_mp_roster[roster_index].player_id !=
				    sender_player_id &&
			    g_game_config.craft_selection !=
				    CRAFT_SELECTION_HOST_ONLY) {
				continue;
			}
			craft_option = payload[1];
			if (craft_option != 0) {
				if ((unsigned int)payload[0] <
				    MIN_PRESET_CRAFT_CATEGORY) {
					g_mp_roster[roster_index]
						.craft_type_override = 0;
				} else if ((unsigned int)payload[0] <=
					   MAX_STANDARD_PRESET_CRAFT_CATEGORY) {
					g_mp_roster[roster_index]
						.craft_type_override =
						g_preset_craft_types
							[craft_option];
				} else if (payload[0] ==
					   SPECIAL_PRESET_CRAFT_CATEGORY) {
					g_mp_roster[roster_index]
						.craft_type_override = g_preset_craft_types
						[craft_option +
						 SPECIAL_PRESET_CRAFT_OFFSET];
				} else {
					g_mp_roster[roster_index]
						.craft_type_override = 0;
				}
			} else {
				g_mp_roster[roster_index].craft_type_override =
					0;
			}
			g_mp_roster[roster_index].craft_option_index =
				payload[2] - 1;
			g_mp_roster[roster_index].warhead_option_index =
				payload[3];
			g_mp_roster[roster_index].beam_option_index =
				payload[4];
			g_mp_roster[roster_index].countermeasure_option_index =
				payload[5];
			if (g_game_config.craft_selection ==
			    CRAFT_SELECTION_HOST_ONLY) {
				g_mission_setup_selected_preset_craft_option_index =
					payload[1];
				g_mission_setup_selected_flight_group_craft_option_index =
					payload[2];
				g_mission_setup_selected_warhead_option_index =
					payload[3];
				g_mission_setup_selected_beam_option_index =
					payload[4];
				g_mission_setup_selected_countermeasure_option_index =
					payload[5];
				g_mission_setup_selected_wave_count_minus_one =
					payload[6];
				g_mission_setup_selected_craft_count =
					payload[7];
			}
		}
		break;

	case NET_PACKET_BRIEFING_ENTERED:
		++g_frontend_briefing_entered_count;
		XVT_LOG_DEBUG("network.briefing_entered player=%u count=%d",
			      (unsigned)sender_player_id,
			      g_frontend_briefing_entered_count);
		break;

	case NET_PACKET_TEAM_RESERVATION:
	case NET_PACKET_FLIGHT_RESERVATION:
		g_frontend_net_packet_arg0 = payload[0];
		g_frontend_net_reserving_player_id = payload[1];
		XVT_LOG_DEBUG("network.reservation player=%u holder=%u",
			      (unsigned)g_frontend_net_packet_arg0,
			      (unsigned)g_frontend_net_reserving_player_id);
		break;

	case NET_PACKET_PLAYER_UNAVAILABLE:
		if (net_is_host() != 0) {
			if (g_game_config.sfx_datapad_enabled != 0) {
				frontend_sound_play_ui_sound(
					"exitpsound", 1, 0, 255,
					12 * g_game_config.sfx_datapad_volume,
					63);
			}
			memset(g_mp_roster_ready_flags, 0,
			       sizeof(g_mp_roster_ready_flags));
			net_clear_player_ready_flag(sender_player_id);
			mission_setup_send_lobby_state(0);
		}
		XVT_LOG_DEBUG("network.player_unavailable player=%u",
			      (unsigned)sender_player_id);
		packet_type = NET_PACKET_NONE;
		break;

	case NET_PACKET_CHAT_SYNC_REQUEST:
		chunk_index = 0;
		if (g_frontend_chat_log_buffer != NULL) {
			remaining_bytes = g_frontend_chat_log_used_bytes;
			if (remaining_bytes == 0) {
				g_frontend_net_packet_scratch.packet_type =
					NET_PACKET_CHAT_SYNC_CHUNK;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[0] = 0;
				net_send_packet_and_flush(
					sender_player_id,
					&g_frontend_net_packet_scratch,
					2 * sizeof(int));
			} else {
				chat_offset = 0;
				do {
					/* packet_size is reused here as the
					 * size of each outgoing chat chunk. */
					packet_size = CHAT_SYNC_CHUNK_SIZE;
					if (remaining_bytes <
					    (int)packet_size) {
						packet_size = remaining_bytes;
					}
					g_frontend_net_packet_scratch
						.packet_type =
						NET_PACKET_CHAT_SYNC_CHUNK;
					*(int *)&g_frontend_net_packet_scratch
						 .payload[0] = chunk_index++;
					memcpy(&g_frontend_net_packet_scratch
							.payload[4],
					       &g_frontend_chat_log_buffer
						       [chat_offset],
					       packet_size);
					remaining_bytes -= packet_size;
					chat_offset += packet_size;
					net_send_packet_and_flush(
						sender_player_id,
						&g_frontend_net_packet_scratch,
						packet_size + 2 * sizeof(int));
				} while (remaining_bytes > 0);
			}
			XVT_LOG_DEBUG(
				"network.chat_sync_sent player=%u bytes=%d chunks=%d",
				(unsigned)sender_player_id,
				g_frontend_chat_log_used_bytes, chunk_index);
		} else {
			XVT_LOG_WARN(
				"network.chat_dropped player=%u kind=\"chat_log_request\"",
				(unsigned)sender_player_id);
		}
		break;

	case NET_PACKET_CHAT_SYNC_CHUNK:
		chunk_index = payload[0];
		packet_size -= 2 * sizeof(int);
		if (g_frontend_chat_log_buffer == NULL) {
			XVT_LOG_ERROR("network.chat_chunk_unheld index=%d",
				      chunk_index);
		}
		if (chunk_index == 0) {
			memset(g_frontend_chat_log_buffer, 0,
			       CHAT_LOG_CAPACITY);
			g_frontend_chat_log_used_bytes = 0;
		}
		chat_chunk_bytes = (uint8_t *)&payload[1];
		/* player_index is reused here as a byte index into the chat chunk. */
		for (player_index = 0; player_index < (int)packet_size;
		     ++player_index) {
			if (chat_chunk_bytes[player_index] <= 6) {
				chat_chunk_bytes[player_index] = 6;
			}
		}
		memcpy(&g_frontend_chat_log_buffer[CHAT_SYNC_CHUNK_SIZE *
						   chunk_index],
		       chat_chunk_bytes, packet_size);
		g_frontend_chat_log_used_bytes += packet_size;
		XVT_LOG_DEBUG("network.chat_chunk index=%d bytes=%u used=%d",
			      chunk_index, (unsigned)packet_size,
			      g_frontend_chat_log_used_bytes);
		break;

	case NET_PACKET_READY_ROSTER:
		net_clear_player_ready_flags();
		g_frontend_net_probe_players_needed = payload[0] - payload[1];
		ready_player_count = payload[1];
		XVT_LOG_DEBUG("network.ready_roster players=%d needed=%d",
			      ready_player_count,
			      g_frontend_net_probe_players_needed);
		packet_word_index = ROSTER_PACKET_COMPACT_FIRST_PLAYER_WORD;
		memset(g_mp_roster, 0, sizeof(g_mp_roster));
		for (roster_index = 0; roster_index < ready_player_count;
		     ++roster_index) {
			if (payload[packet_word_index] == 0) {
				++packet_word_index;
				g_mp_roster[roster_index].player_id = 0;
				g_mp_roster[roster_index].pilot_rating =
					payload[packet_word_index++];
				++packet_word_index;
			} else {
				net_player = net_find_player(
					payload[packet_word_index]);
				if (net_player != NULL) {
					net_mark_player_ready_no_lock(
						payload[packet_word_index]);
					strncpy(g_mp_roster[roster_index].name,
						net_player->player_name,
						PLAYER_NAME_COPY_SIZE);
					g_mp_roster[roster_index].player_id =
						net_player->player_id;
					++packet_word_index;
					g_mp_roster[roster_index].pilot_rating =
						payload[packet_word_index];
					++packet_word_index;
					if (net_is_host() == 0) {
						net_set_player_latency_ms(
							g_mp_roster[roster_index]
								.player_id,
							payload[packet_word_index]);
					}
					++packet_word_index;
				} else {
					memcpy(g_mp_roster[roster_index].name,
					       "No name", sizeof("No name"));
					g_mp_roster[roster_index].player_id =
						payload[packet_word_index];
					++packet_word_index;
					g_mp_roster[roster_index].pilot_rating =
						payload[packet_word_index];
					packet_word_index += 2;
				}
			}
			XVT_LOG_DEBUG(
				"network.roster_entry index=%d player=%u rating=%d latency=%d name=\"%.14s\"",
				roster_index,
				(unsigned)g_mp_roster[roster_index].player_id,
				(int)g_mp_roster[roster_index].pilot_rating,
				payload[packet_word_index - 1],
				g_mp_roster[roster_index].name);
		}
		break;

	case NET_PACKET_PILOT_RATING:
		g_frontend_net_packet_arg0 = payload[0];
		XVT_LOG_DEBUG(
			"network.setup_value kind=\"pilot_rating\" value=%d",
			g_frontend_net_packet_arg0);
		break;

	case NET_PACKET_BATTLE_PROGRESS:
		g_remote_battle_continuation_active = payload[0];
		g_remote_battle_sequence_continuation_choice = payload[1];
		g_remote_battle_last_completed_mission_index = payload[2];
		g_remote_battle_rebel_victory_count = payload[3];
		g_remote_battle_imperial_victory_count = payload[4];
		XVT_LOG_DEBUG(
			"network.battle_progress active=%d choice=%d last=%d rebel=%d imperial=%d",
			g_remote_battle_continuation_active,
			g_remote_battle_sequence_continuation_choice,
			g_remote_battle_last_completed_mission_index,
			g_remote_battle_rebel_victory_count,
			g_remote_battle_imperial_victory_count);
		break;

	case NET_PACKET_MOVIE_SYNC:
		XVT_LOG_DEBUG("network.movie_sync player=%u op=%d target=%u",
			      (unsigned)sender_player_id, payload[0],
			      payload[0] == 2 ? (unsigned)payload[1] : 0u);
		for (movie_player_index = 0; movie_player_index < MAX_PLAYERS;
		     ++movie_player_index) {
			switch (payload[0]) {
			case 0:
				if ((DPID)g_movie_multiplayer_sync_players
					    [movie_player_index]
						    .player_id ==
				    sender_player_id) {
					g_movie_multiplayer_sync_players
						[movie_player_index]
							.is_waiting = 1;
					movie_player_index = MAX_PLAYERS;
				}
				break;

			case 1:
				g_movie_multiplayer_sync_players
					[movie_player_index]
						.is_waiting = 1;
				break;

			case 2:
				if (payload[1] ==
				    g_movie_multiplayer_sync_players
					    [movie_player_index]
						    .player_id) {
					g_movie_multiplayer_sync_players
						[movie_player_index]
							.player_id = 0;
					movie_player_index = MAX_PLAYERS;
				}
				break;
			}
		}
		break;

	case NET_PACKET_SUBMIT_MISSION_CHOICE:
		g_frontend_net_packet_scratch.packet_type =
			NET_PACKET_MISSION_CHOICE;
		*(int *)&g_frontend_net_packet_scratch.payload[0] = payload[0];
		net_send_packet_and_flush(0, &g_frontend_net_packet_scratch,
					  2 * sizeof(int));
		XVT_LOG_DEBUG(
			"network.mission_choice_relayed player=%u choice=%d",
			(unsigned)sender_player_id, payload[0]);
		break;

	default:
		XVT_LOG_DEBUG("network.packet_unknown type=%d", packet_type);
		packet_type = NET_PACKET_NONE;
		break;
	}
	return packet_type;
}
