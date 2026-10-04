#include "xvt/net/frontend_net.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/network_browser.h"
#include "xvt_runtime/runtime/network_dialogs.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/runtime/network_task.h"
#endif

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
#ifdef XVT_MODERN
	(void)frame_counter;
	return xvt_network_browser_draw_list();
#else
	if (frame_counter == 0) {
		g_frontend_net_session_list_scroll_offset = 0;
	}
	uint32_t now_ms = GetTickCount();
	struct RECT rect;
	frontend_draw_rect_assign(&rect, 88, 94, 416, 109);
	frontend_text_draw_aligned_in_rect(
		12,
		frontend_string_get(
			FRONTSTR_016_GAME_NAME_PLAYERS_NEEDED_LAST_QUERY),
		&rect, 0, 1, 0xFFFF);
	int mouse_x;
	int mouse_y;
	frontend_cursor_get_pos(&mouse_x, &mouse_y);
	if (g_frontend_net_session_count > 6) {
		frontend_draw_rect_assign(&rect, 421, 114, 430, 204);
		g_frontend_net_session_list_scroll_offset =
			frontend_scrollbar_draw(
				&rect,
				g_frontend_net_session_list_scroll_offset,
				g_frontend_net_session_count, 0, 5,
				(unsigned int)g_color_navy, 5);
	}
	frontend_draw_rect_assign(&rect, 88, 114, 419, 128);
	int clicked_index = -1;
	int row_index = g_frontend_net_session_list_scroll_offset;
	struct RECT destination;
	while (row_index < g_frontend_net_session_list_scroll_offset + 6 &&
	       row_index < g_frontend_net_session_count) {
		if (row_index == g_frontend_net_selected_session_idx) {
			frontend_draw_rect(&rect, 0, 0, g_color_navy, 1);
		}
		unsigned short text_color;
		if (g_frontend_net_session_list[row_index].version != 101u) {
			text_color = g_color_gray;
		} else if (g_frontend_net_session_list[row_index]
				   .players_needed == 0) {
			text_color =
				g_frontend_net_session_list[row_index]
							.game_in_flight != 0
					? g_color_red
					: g_color_yellow;
		} else {
			text_color = 0xFFFF;
			if (g_frontend_net_session_list[row_index]
				    .players_needed <= 8) {
				text_color = g_color_green2;
			}
		}
		if (g_frontend_net_session_list[row_index].password_required !=
		    0) {
			front_image_draw_sprite("key", rect.left - 6,
						rect.top + 1);
		}
		if (g_frontend_net_session_list[row_index].version != 101u) {
			if (g_frontend_net_session_list[row_index].version ==
			    0) {
				sprintf(g_frontend_scratch_buffer, "%s v. ???",
					g_frontend_net_session_list[row_index]
						.game_name);
			} else {
				sprintf(g_frontend_scratch_buffer,
					"%s v. %d.%d",
					g_frontend_net_session_list[row_index]
						.game_name,
					(g_frontend_net_session_list[row_index]
						 .version -
					 1) / 100 +
						1,
					(g_frontend_net_session_list[row_index]
						 .version -
					 1) % 100);
			}
			frontend_text_draw_aligned_in_rect(
				12, g_frontend_scratch_buffer, &rect, 0, 1,
				text_color);
		} else {
			frontend_text_draw_aligned_in_rect(
				12,
				g_frontend_net_session_list[row_index]
					.game_name,
				&rect, 0, 1, text_color);
		}
		if (g_frontend_net_session_list[row_index].players_needed <=
		    8) {
			frontend_draw_rect_copy(&destination, &rect);
			destination.left = 285;
			sprintf(g_frontend_scratch_buffer, "%d",
				g_frontend_net_session_list[row_index]
					.players_needed);
			frontend_text_draw_aligned_in_rect(
				12, g_frontend_scratch_buffer, &destination, 0,
				1, text_color);
			destination.left = 374;
			frontend_format_seconds_to_clock_string(
				(now_ms - g_frontend_net_session_list[row_index]
						  .last_query_ms) /
				1000);
			frontend_text_draw_aligned_in_rect(
				12, g_frontend_scratch_buffer, &destination, 0,
				1, text_color);
		}
		if (frontend_draw_point_in_rect(&rect, mouse_x, mouse_y)) {
			frontend_draw_rect_outline(&rect, 0, 0, g_color_green);
			if (frontend_mouse_get_left_click() != 0 ||
			    frontend_mouse_get_right_click() != 0) {
				if (g_game_config.sfx_datapad_enabled != 0) {
					frontend_sound_play_ui_sound(
						"jewelsound", 1, 0, 255,
						12 * g_game_config
								.sfx_datapad_volume,
						63);
				}
				clicked_index = row_index;
			}
		}
		frontend_draw_rect_offset_xy(&rect, 0, 15);
		++row_index;
	}
	return clicked_index;
#endif
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
#ifdef XVT_MODERN
	return xvt_network_browser_screen(frame_counter);
#else
	enum {
		SESSION_REFRESH_INTERVAL_FRAMES = 160,
		BRIEFING_TEXT_CAPACITY = 4096,
		JOIN_REQUEST_PACKET_SIZE = 6 * sizeof(int),
		PILOT_BANNER_ANIMATION_PERIOD_FRAMES = 32,
	};

	g_config_connection_type_editable = 0;
	struct RECT screen_rect;
	if (g_game_config.network_type != NET_TRANSPORT_IPX) {
		if (g_frontend_skip_screen_entry_setup != 0) {
			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NONE;
			frontend_screen_set_callbacks(concourse_update,
						      concourse_exit);
			return 0;
		}
		if (g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_NET_CLIENT) {
			switch (g_game_config.network_type) {
			case NET_TRANSPORT_TCPIP:
				if (g_game_config.ip_address[0] == '\0') {

					if (frontend_dialog_show_confirm_dialog(
						    frontend_string_get(
							    FRONTSTR_693_YOU_HAVE_NOT_ENTERED_AN_IP_ADDRESS_FOR),
						    frontend_string_get(
							    FRONTSTR_694_THE_TCP_IP_CONNECTION_PLEASE_ENTER_ONE),
						    frontend_string_get(
							    FRONTSTR_695_IN_THE_CONFIGURATION_SCREEN),
						    frontend_string_get(
							    FRONTSTR_523_OKAY),
						    frontend_string_get(
							    FRONTSTR_019_CANCEL)) ==
					    0) {
						g_frontend_mission_session_mode =
							FRONTEND_MISSION_SESSION_NONE;
						frontend_screen_set_callbacks(
							concourse_update,
							concourse_exit);
						return 0;
					}

					frontend_draw_rect_assign(
						&screen_rect, 0, 0, 640, 480);
					frontend_screen_queue_push(
						config_options_datapad_update,
						&screen_rect);
					return 0;
				}
				break;
			case NET_TRANSPORT_MODEM:
				if (g_game_config.phone_number[0] == '\0') {

					if (frontend_dialog_show_confirm_dialog(
						    frontend_string_get(
							    FRONTSTR_696_YOU_HAVE_NOT_ENTERED_A_PHONE_NUMBER_FOR),
						    frontend_string_get(
							    FRONTSTR_697_THE_MODEM_CONNECTION_PLEASE_ENTER_ONE),
						    frontend_string_get(
							    FRONTSTR_698_IN_THE_CONFIGURATION_SCREEN),
						    frontend_string_get(
							    FRONTSTR_523_OKAY),
						    frontend_string_get(
							    FRONTSTR_019_CANCEL)) ==
					    0) {
						g_frontend_mission_session_mode =
							FRONTEND_MISSION_SESSION_NONE;
						frontend_screen_set_callbacks(
							concourse_update,
							concourse_exit);
						return 0;
					}

					frontend_draw_rect_assign(
						&screen_rect, 0, 0, 640, 480);
					frontend_screen_queue_push(
						config_options_datapad_update,
						&screen_rect);
					return 0;
				}
				break;
			}
		}
		return frontend_net_connect_to_selected_game_screen(
			frame_counter);
	} else {
		if (frame_counter == 0) {
			frontend_cursor_set_pos(415, 121);
			memset(g_mp_roster, 0, sizeof(g_mp_roster));
			net_clear_player_ready_flags();
			memset(g_frontend_net_selected_game_name, 0,
			       sizeof(g_frontend_net_selected_game_name));
			if (g_frontend_chat_log_buffer != NULL) {
				memset(g_frontend_chat_log_buffer, 0, 1024);
				g_frontend_chat_log_used_bytes = 0;
			}
			if (g_frontend_skip_screen_entry_setup == 0) {
				memset(g_frontend_net_session_list, 0,
				       sizeof(g_frontend_net_session_list));
				g_frontend_net_session_count = 0;
			}
			g_frontend_first_visible_line = 0;
			g_config_connection_type_editable = 0;
			g_frontend_game_session_in_progress = 0;
			g_frontend_net_received_mission_directory_id =
				MISSION_DIRECTORY_TRAINING_EXERCISES;
			g_frontend_net_received_mission_description_id = -1;
			g_frontend_net_selected_session_idx = -1;
			g_mission_setup_roster_authoritative = 0;
			g_frontend_skip_screen_entry_setup = 0;
			if (g_mission_text == NULL) {
				g_mission_text = malloc(BRIEFING_TEXT_CAPACITY);
			}
			front_image_register_resource_default(
				"frontres\\joinback.bmp", "background");
			frontend_display_lock_offscreen_surface();
			front_image_draw_sprite_opaque("background", 0, 0);
			front_image_draw_sprite("frame", 0, 0);
			if (g_host_cd_available != 0) {
				front_image_draw_sprite("allactive", 0, 0);
			} else {
				front_image_draw_sprite("clientactive", 0, 0);
			}
			front_image_draw_sprite_translucent("chatbox", 0, 0);
			front_image_draw_sprite_translucent("joinoverlay", 0,
							    0);
			frontend_display_unlock_offscreen_surface(1);
			frontend_text_start_text_fade_in(20);
		}

		struct RECT rect;
		frontend_draw_rect_assign(&rect, 158, 52, 491, 68);
		frontend_text_draw_centered(
			12, g_frontend_net_selected_game_name, &rect, 0xFFFF);
		if (frame_counter % SESSION_REFRESH_INTERVAL_FRAMES == 0 &&
		    g_frontend_net_selected_session_idx == -1) {
			frontend_net_refresh_session_list();
		}
		if (g_frontend_net_selected_session_idx != -1) {
			int packet_type =
				frontend_net_process_network_packets();
			if (packet_type == NET_PACKET_STATE) {
				g_frontend_net_session_list
					[g_frontend_net_selected_session_idx]
						.last_query_ms = GetTickCount();
				g_frontend_net_session_list
					[g_frontend_net_selected_session_idx]
						.players_needed =
					g_frontend_net_probe_players_needed;
				g_frontend_net_session_list
					[g_frontend_net_selected_session_idx]
						.password_required = (uint8_t)
					g_frontend_net_probe_password_required;
				mp_roster_compact_active_entries();
				g_pilot_data.mission_directory_id =
					g_frontend_net_received_mission_directory_id;
				g_pilot_data.mission_description_ids
					[g_frontend_net_received_mission_directory_id] =
					g_frontend_net_received_mission_description_id;
				mission_setup_load_mission_list(
					g_pilot_data.mission_directory_id);
				mission_setup_load_mission_desc_text(
					g_mission_text);
				if (g_mission_list != NULL) {
					for (g_selected_mission_list_index = 0;
					     (unsigned int)
						     g_selected_mission_list_index <
					     g_mission_count;
					     ++g_selected_mission_list_index) {
						if (g_mission_list
							    [g_selected_mission_list_index]
								    .mission_idx ==
						    g_pilot_data.mission_description_ids
							    [g_pilot_data
								     .mission_directory_id]) {
							break;
						}
					}
				}
				g_frontend_first_visible_line = 0;
			} else if (
				packet_type == NET_PACKET_HOST_CANCELLED ||
				packet_type ==
					NET_PACKET_FRONTEND_MISSION_START ||
				packet_type ==
					NET_PACKET_NEXT_TOURNAMENT_MISSION ||
				packet_type == NET_PACKET_NEXT_BATTLE_MISSION ||
				packet_type == NET_PACKET_REPLAY_MISSION ||
				packet_type ==
					NET_PACKET_RETURN_TO_MISSION_SELECTION ||
				packet_type ==
					NET_PACKET_REPLAY_CURRENT_MISSION) {
				net_shutdown_direct_play_session();
				frontend_net_refresh_session_list();
				g_frontend_net_probe_mission_elapsed_seconds =
					0;
				g_frontend_net_selected_session_idx = -1;
				g_frontend_net_received_mission_description_id =
					-1;
				memset(g_frontend_net_selected_game_name, 0,
				       sizeof(g_frontend_net_selected_game_name));
				memset(g_mission_text, 0,
				       BRIEFING_TEXT_CAPACITY);
			} else if (packet_type ==
				   NET_PACKET_PLAYER_UNAVAILABLE) {
				net_shutdown_direct_play_session();
				g_frontend_net_probe_mission_elapsed_seconds =
					0;
				g_frontend_net_selected_session_idx = -1;
				g_frontend_net_received_mission_description_id =
					-1;
				frontend_net_refresh_session_list();
				memset(g_frontend_net_selected_game_name, 0,
				       sizeof(g_frontend_net_selected_game_name));
				memset(g_mission_text, 0,
				       BRIEFING_TEXT_CAPACITY);
			} else if (packet_type == NET_PACKET_GAME_FULL ||
				   packet_type == NET_PACKET_VERSION_MISMATCH ||
				   packet_type ==
					   NET_PACKET_PASSWORD_REQUIRED ||
				   packet_type == NET_PACKET_ROSTER_LOCKED) {
				net_shutdown_direct_play_session();
				switch (packet_type - NET_PACKET_GAME_FULL) {
				case 0:
					frontend_dialog_show_confirm_dialog(
						frontend_string_get(
							FRONTSTR_543_THE_GAME_YOU_ARE_TRYING_TO_JOIN_IS_FULL),
						frontend_string_get(
							FRONTSTR_544_PLEASE_TRY_ANOTHER_GAME),
						frontend_string_get(
							FRONTSTR_545_SPACE_TRANSLATION_PLACEHOLDER),
						NULL, NULL);
					break;
				case NET_PACKET_VERSION_MISMATCH -
					NET_PACKET_GAME_FULL:
					frontend_dialog_show_confirm_dialog(
						frontend_string_get(
							FRONTSTR_546_YOUR_VERSION_OF_THE_PROGRAM_DOES_NOT),
						frontend_string_get(
							FRONTSTR_547_MATCH_THE_HOSTS_PLEASE_VERIFY_THAT_YOU),
						frontend_string_get(
							FRONTSTR_548_HAVE_THE_CORRECT_PROGRAM),
						NULL, NULL);
					break;
				case NET_PACKET_PASSWORD_REQUIRED -
					NET_PACKET_GAME_FULL:
					frontend_dialog_show_confirm_dialog(
						frontend_string_get(
							FRONTSTR_549_THIS_GAME_REQUIRES_A_PASSWORD),
						frontend_string_get(
							FRONTSTR_550_PLEASE_ENTER_THE_CORRECT_PASSWORD_IN),
						frontend_string_get(
							FRONTSTR_551_THE_NETWORK_CONFIGURATION_SCREEN),
						NULL, NULL);
					frontend_draw_rect_assign(
						&screen_rect, 0, 0, 640, 480);
					frontend_screen_queue_push(
						config_options_datapad_update,
						&screen_rect);
					break;
				case NET_PACKET_ROSTER_LOCKED -
					NET_PACKET_GAME_FULL:
					frontend_dialog_show_confirm_dialog(
						frontend_string_get(
							FRONTSTR_552_THIS_GAME_HAS_ALREADY_STARTED),
						frontend_string_get(
							FRONTSTR_553_PLEASE_SELECT_ANOTHER_GAME_TO_JOIN),
						frontend_string_get(
							FRONTSTR_554_SPACE_TRANSLATION_PLACEHOLDER),
						NULL, NULL);
					break;
				}
				g_frontend_net_probe_mission_elapsed_seconds =
					0;
				g_frontend_net_selected_session_idx = -1;
				g_frontend_net_received_mission_description_id =
					-1;
				frontend_net_refresh_session_list();
				memset(g_frontend_net_selected_game_name, 0,
				       sizeof(g_frontend_net_selected_game_name));
				memset(g_mission_text, 0,
				       BRIEFING_TEXT_CAPACITY);
			}
		}

		int clicked_session_index =
			frontend_net_draw_join_game_list(frame_counter);
		if (clicked_session_index != -1) {
			if (g_frontend_net_selected_session_idx ==
			    clicked_session_index) {
				g_frontend_net_selected_session_idx = -1;
				g_frontend_net_received_mission_description_id =
					-1;
				g_frontend_net_probe_mission_elapsed_seconds =
					0;
				memset(g_frontend_net_selected_game_name, 0,
				       sizeof(g_frontend_net_selected_game_name));
				memset(g_mission_text, 0,
				       BRIEFING_TEXT_CAPACITY);
				net_shutdown_direct_play_session();
			} else {
				g_frontend_net_selected_session_idx =
					clicked_session_index;
				if (frontend_net_probe_session_by_index(
					    clicked_session_index) == 0) {
					net_shutdown_direct_play_session();
					g_frontend_net_probe_mission_elapsed_seconds =
						0;
					g_frontend_net_selected_session_idx =
						-1;
					g_frontend_net_received_mission_description_id =
						-1;
					frontend_net_refresh_session_list();
					memset(g_frontend_net_selected_game_name,
					       0,
					       sizeof(g_frontend_net_selected_game_name));
					memset(g_mission_text, 0,
					       BRIEFING_TEXT_CAPACITY);
				} else {
					frontend_net_sort_sessions();
					if (g_frontend_net_session_list
						    [g_frontend_net_selected_session_idx]
							    .players_needed >
					    0) {
						frontend_mouse_clear_clicks();
						frontend_cursor_set_pos(37,
									445);
					}
				}
			}
		}
		frontend_net_draw_join_game_player_roster();
		frontend_net_draw_join_game_mission_briefing();
		if (g_frontend_net_selected_session_idx != -1) {
			frontend_net_update_and_draw_chat_panel(frame_counter);
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
			int animation_frame =
				(frame_counter %
				 PILOT_BANNER_ANIMATION_PERIOD_FRAMES) >>
				1;
			sprintf(g_frontend_scratch_buffer, "rebtiny%d",
				animation_frame);
			front_image_draw_sprite(g_frontend_scratch_buffer, 204,
						453);
			sprintf(g_frontend_scratch_buffer, "imptiny%d",
				animation_frame);
			front_image_draw_sprite(g_frontend_scratch_buffer, 420,
						453);
		}
		frontend_net_draw_join_game_sidebars_and_query_all();
		if (frontend_handle_common_screen_controls(0) == 1) {
			return 1;
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
			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NONE;
			net_shutdown_direct_play_session();
			frontend_screen_set_callbacks(concourse_update,
						      concourse_exit);
		}
		frontend_draw_rect_assign(&rect, 8, 405, 71, 470);
		if (g_frontend_net_selected_session_idx != -1 &&
		    g_frontend_net_session_list
				    [g_frontend_net_selected_session_idx]
					    .players_needed > 0 &&
		    g_frontend_net_session_list
				    [g_frontend_net_selected_session_idx]
					    .version ==
			    FRONTEND_NET_PROTOCOL_VERSION) {
			frontend_button_set_overlay_text(
				frontend_string_get(FRONTSTR_018_JOIN));
			if (frontend_button_handle_sprite_button(
				    &rect, "nextup", "nextdown",
				    frontend_string_get(FRONTSTR_018_JOIN), 12,
				    0, 7, "flysound") != 0) {
				g_mission_setup_is_host = 0;
				strcpy(g_pilot_data.multiplayer_game_name,
				       g_frontend_net_session_list
					       [g_frontend_net_selected_session_idx]
						       .game_name);
				int host_player_id = net_get_host_player_id();
				g_frontend_net_packet_scratch.packet_type =
					NET_PACKET_JOIN_REQUEST;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[0] =
					FRONTEND_NET_PROTOCOL_VERSION;
				memcpy(&g_frontend_net_packet_scratch
						.payload[4],
				       g_game_config.password,
				       sizeof(g_game_config.password));
				net_send_packet_and_flush(
					host_player_id,
					&g_frontend_net_packet_scratch,
					JOIN_REQUEST_PACKET_SIZE);
				frontend_screen_set_callbacks(
					frontend_net_await_join_admission_screen,
					NULL);
			}
		}
		frontend_button_disable_overlay_text();
		return 0;
	}

	return 0;
#endif
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
#ifdef XVT_MODERN
	if (xvt_network_dialogs_report_admission_failure()) {
		return 0;
	}
#endif
	int network_result = frontend_net_process_network_packets();
#ifdef XVT_MODERN
	if (g_frontend_net_packet_sender_player_id !=
	    net_get_host_player_id()) {
		network_result = 0;
	}
#endif
	if (network_result == NET_PACKET_GAME_FULL ||
	    network_result == NET_PACKET_VERSION_MISMATCH ||
	    network_result == NET_PACKET_PASSWORD_REQUIRED ||
	    network_result == NET_PACKET_ROSTER_LOCKED ||
	    network_result == NET_PACKET_HOST_CANCELLED ||
	    network_result == NET_PACKET_FRONTEND_GAME_STARTED) {
#ifdef XVT_MODERN
		xvt_network_session_reject();
#else
		net_shutdown_direct_play_session_no_handshake();
#endif
		struct RECT screen_rect;
		switch (network_result - NET_PACKET_FRONTEND_GAME_STARTED) {
		case NET_PACKET_FRONTEND_GAME_STARTED -
			NET_PACKET_FRONTEND_GAME_STARTED:
		case NET_PACKET_ROSTER_LOCKED -
			NET_PACKET_FRONTEND_GAME_STARTED:
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_552_THIS_GAME_HAS_ALREADY_STARTED),
				frontend_string_get(
					FRONTSTR_553_PLEASE_SELECT_ANOTHER_GAME_TO_JOIN),
				frontend_string_get(
					FRONTSTR_554_SPACE_TRANSLATION_PLACEHOLDER),
				NULL, NULL);
#ifdef XVT_MODERN
			return xvt_dialog_continue_with(
				xvt_network_dialogs_resume,
				XVT_NETWORK_ACCESS_REJECTED);
#endif
			break;
		case NET_PACKET_GAME_FULL - NET_PACKET_FRONTEND_GAME_STARTED:
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_543_THE_GAME_YOU_ARE_TRYING_TO_JOIN_IS_FULL),
				frontend_string_get(
					FRONTSTR_544_PLEASE_TRY_ANOTHER_GAME),
				frontend_string_get(
					FRONTSTR_545_SPACE_TRANSLATION_PLACEHOLDER),
				NULL, NULL);
#ifdef XVT_MODERN
			return xvt_dialog_continue_with(
				xvt_network_dialogs_resume,
				XVT_NETWORK_ACCESS_REJECTED);
#endif
			break;
		case NET_PACKET_HOST_CANCELLED -
			NET_PACKET_FRONTEND_GAME_STARTED:
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_631_THE_CURRENT_GAME_HAS_BEEN),
				frontend_string_get(
					FRONTSTR_632_CANCELLED_BY_THE_HOST),
				frontend_string_get(
					FRONTSTR_633_PLEASE_SELECT_ANOTHER_GAME),
				NULL, NULL);
#ifdef XVT_MODERN
			return xvt_dialog_continue_with(
				xvt_network_dialogs_resume,
				XVT_NETWORK_ACCESS_REJECTED);
#endif
			break;
		case NET_PACKET_VERSION_MISMATCH -
			NET_PACKET_FRONTEND_GAME_STARTED:
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_546_YOUR_VERSION_OF_THE_PROGRAM_DOES_NOT),
				frontend_string_get(
					FRONTSTR_547_MATCH_THE_HOSTS_PLEASE_VERIFY_THAT_YOU),
				frontend_string_get(
					FRONTSTR_548_HAVE_THE_CORRECT_PROGRAM),
				NULL, NULL);
#ifdef XVT_MODERN
			return xvt_dialog_continue_with(
				xvt_network_dialogs_resume,
				XVT_NETWORK_ACCESS_REJECTED);
#endif
			break;
		case NET_PACKET_PASSWORD_REQUIRED -
			NET_PACKET_FRONTEND_GAME_STARTED:
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_549_THIS_GAME_REQUIRES_A_PASSWORD),
				frontend_string_get(
					FRONTSTR_550_PLEASE_ENTER_THE_CORRECT_PASSWORD_IN),
				frontend_string_get(
					FRONTSTR_551_THE_NETWORK_CONFIGURATION_SCREEN),
				NULL, NULL);
#ifdef XVT_MODERN
			return xvt_dialog_continue_with(
				xvt_network_dialogs_resume,
				XVT_NETWORK_ACCESS_PASSWORD);
#endif
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
#ifdef XVT_MODERN
	if (xvt_dialog_is_active()) {
		return 0;
	}
#endif
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
#ifdef XVT_MODERN
	if (cancel_pressed != 0) {
		xvt_network_session_cancel();
#else
	if (cancel_pressed != 0 || frame_counter == ACCESS_TIMEOUT_FRAME) {
		net_shutdown_direct_play_session_no_handshake();
#endif
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
#ifdef XVT_MODERN
	return xvt_network_browser_draw_mission();
#else
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 88, 309, 430, 324);
	if (g_frontend_net_received_mission_description_id != -1) {
		if (g_mission_list != NULL &&
		    g_selected_mission_list_index < g_mission_count) {
			sprintf(g_frontend_scratch_buffer, "%s %c%s",
				frontend_string_get(FRONTSTR_187_MISSION), 4,
				g_mission_list[g_selected_mission_list_index]
					.description);
		} else {
			strcpy(g_frontend_scratch_buffer,
			       frontend_string_get(FRONTSTR_187_MISSION));
		}
		frontend_text_draw_aligned_in_rect(
			12, g_frontend_scratch_buffer, &rect, 0, 1, 0xFFFF);
	} else {
		frontend_text_draw_aligned_in_rect(
			12, frontend_string_get(FRONTSTR_187_MISSION), &rect, 0,
			1, 0xFFFF);
	}

	if (g_frontend_net_received_mission_description_id != -1 &&
	    g_mission_text != NULL) {
		frontend_draw_rect_assign(&rect, 88, 328, 420, 426);
		unsigned int line_count =
			frontend_text_draw_wrapped(12, g_mission_text, &rect,
						   0xFFFF, 4, 4096) +
			1;
		if (line_count > 6) {
			frontend_draw_rect_assign(&rect, 421, 328, 430, 426);
			g_frontend_first_visible_line = frontend_scrollbar_draw(
				&rect, g_frontend_first_visible_line,
				line_count, 0, 5, (unsigned int)g_color_navy,
				6);
			frontend_draw_rect_assign(&rect, 88, 328, 420, 426);
		} else {
			frontend_draw_rect_assign(&rect, 88, 328, 430, 426);
		}
		frontend_text_draw_wrapped(12, g_mission_text, &rect, 0xFFFF, 4,
					   g_frontend_first_visible_line);
	}
	return 1;
#endif
}

/* Draws the join screen's player heading and, once a lobby STATE has arrived,
 * each player in g_mp_roster with rating and name, in two columns of 4; returns
 * 1. Calls net_count_ready_players and ignores its result. The modern build
 * returns xvt_network_browser_draw_roster's result instead. */
// FUNCTION: XVT 0x4D8540
int frontend_net_draw_join_game_player_roster(void)
{
#ifdef XVT_MODERN
	return xvt_network_browser_draw_roster();
#else
	struct RECT rect;

	frontend_draw_rect_assign(&rect, 88, 218, 430, 233);
	frontend_text_draw_aligned_in_rect(
		12, frontend_string_get(FRONTSTR_201_PLAYERS_IN_GAME), &rect, 0,
		1, 0xFFFF);
	struct RECT previous_clip_rect;
	if (g_frontend_net_received_mission_description_id != -1) {
		int displayed_count = 0;
		net_count_ready_players();
		frontend_draw_rect_assign(&rect, 88, 238, 258, 252);
		for (int roster_index = 0; roster_index < 8; ++roster_index) {
			if (g_mp_roster[roster_index].player_id != 0) {
				frontend_display_get_screen_clip_rect(
					&previous_clip_rect);
				++displayed_count;
				frontend_display_set_screen_clip_rect640x480(
					&rect);
				sprintf(g_frontend_scratch_buffer, "%c%s %c%s",
					6,
					frontend_string_get(
						FRONTSTR_154_DRONE +
						g_mp_roster[roster_index]
							.pilot_rating),
					4, g_mp_roster[roster_index].name);
				frontend_text_draw_aligned_in_rect(
					12, g_frontend_scratch_buffer, &rect, 0,
					1, 0xFFFF);
				frontend_display_set_screen_clip_rect640x480(
					&previous_clip_rect);
				if (displayed_count == 4) {
					frontend_draw_rect_assign(
						&rect, 259, 238, 430, 252);
				} else {
					frontend_draw_rect_offset_xy(&rect, 0,
								     15);
				}
			}
		}
	}
	return 1;
#endif
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
			}
		} else {
			net_send_packet_and_flush(
				0, &g_frontend_net_packet_scratch,
				strlen(g_frontend_scratch_buffer) + 4);
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

#ifndef XVT_MODERN
/* The connecting screen for TCP/IP, modem and serial play, run each frame.
 * Frame 0 clears the ready flags, chat log, selection and mission state and
 * draws the connecting message; frames 1 to 4 add the transport's hint (the
 * TCP/IP time-out, or Alt+F4 to cancel); frame 5 ends any DirectPlay session
 * and joins one as a client at the configured IP address or phone number, named
 * after g_frontend_net_session_list[0].game_name. On success it sends the host a
 * JOIN_REQUEST with the protocol version and password and moves to
 * frontend_net_await_join_admission_screen; on failure it returns to the concourse.
 * Returns 0. Only the original build calls this. */
// FUNCTION: XVT 0x4D8C20
int frontend_net_connect_to_selected_game_screen(int frame_counter)
{
	enum {
		CONNECT_ACTION_FRAME = 5,
		CONNECT_RESTORE_DISABLE_FRAME = 4,
		CONNECT_FONT_SIZE = 12,
		CONNECT_TITLE_FONT_SIZE = 15,
		CONNECT_SCREEN_WIDTH = 640,
		CONNECT_SCREEN_HEIGHT = 480,
		CONNECT_MESSAGE_LEFT = 84,
		CONNECT_MESSAGE_RIGHT = 605,
		CONNECT_MESSAGE_TOP = 90,
		CONNECT_MESSAGE_BOTTOM = 106,
		CONNECT_MESSAGE_LOWER_TOP = 415,
		CONNECT_MESSAGE_LOWER_BOTTOM = 429,
		CONNECT_GDI_MESSAGE_BOTTOM = 429,
		BRIEFING_TEXT_CAPACITY = 4096,
		CHAT_LOG_CAPACITY = 1024,
		JOIN_REQUEST_PACKET_SIZE = 24,
	};

	struct RECT rect;

	if (frame_counter == 0) {
		net_clear_player_ready_flags();
		if (g_frontend_chat_log_buffer != NULL) {
			memset(g_frontend_chat_log_buffer, 0,
			       CHAT_LOG_CAPACITY);
			g_frontend_chat_log_used_bytes = 0;
		}
		g_frontend_net_received_mission_directory_id =
			MISSION_DIRECTORY_TRAINING_EXERCISES;
		g_frontend_net_received_mission_description_id = -1;
		g_frontend_net_selected_session_idx = -1;
		g_mission_setup_roster_authoritative = 0;
		if (g_mission_text == NULL) {
			g_mission_text = (char *)malloc(BRIEFING_TEXT_CAPACITY);
		}
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
		frontend_draw_rect_assign(&rect, 0, 0, CONNECT_SCREEN_WIDTH,
					  CONNECT_SCREEN_HEIGHT);
		frontend_text_draw_centered(
			CONNECT_TITLE_FONT_SIZE,
			frontend_string_get(FRONTSTR_645_CONNECTING), &rect,
			0xFFFF);
		frontend_display_unlock_offscreen_surface(1);
		frontend_text_stop_text_fade();
		return 0;
	}

	unsigned int network_type;
	if (frame_counter > 0 && frame_counter < CONNECT_ACTION_FRAME) {
		frontend_cursor_hide();
		network_type = g_game_config.network_type;
		switch ((network_transport_type)network_type) {
		case NET_TRANSPORT_TCPIP:
			frontend_draw_rect_assign(&rect, CONNECT_MESSAGE_LEFT,
						  CONNECT_MESSAGE_LOWER_TOP,
						  CONNECT_MESSAGE_RIGHT,
						  CONNECT_MESSAGE_LOWER_BOTTOM);
			frontend_text_draw_centered(
				CONNECT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_740_IF_ATTEMPT_TO_CONNECT_TO_THE_TCP_IP_ADDRESS_FAILS_TIMEOUT_WILL_OCCUR_IN_2_5_MINUTES),
				&rect, g_color_red);
			frontend_draw_rect_assign(&rect, CONNECT_MESSAGE_LEFT,
						  CONNECT_MESSAGE_TOP,
						  CONNECT_MESSAGE_RIGHT,
						  CONNECT_MESSAGE_BOTTOM);
			frontend_text_draw_centered(
				CONNECT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_740_IF_ATTEMPT_TO_CONNECT_TO_THE_TCP_IP_ADDRESS_FAILS_TIMEOUT_WILL_OCCUR_IN_2_5_MINUTES),
				&rect, g_color_red);
			break;
		case NET_TRANSPORT_MODEM:
		case NET_TRANSPORT_SERIAL:
			frontend_draw_rect_assign(&rect, CONNECT_MESSAGE_LEFT,
						  CONNECT_MESSAGE_LOWER_TOP,
						  CONNECT_MESSAGE_RIGHT,
						  CONNECT_MESSAGE_LOWER_BOTTOM);
			frontend_text_draw_centered(
				CONNECT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_671_PRESS_ALT_F4_TO_CANCEL_CONNECTION_ATTEMPT),
				&rect, g_color_red);
			frontend_draw_rect_assign(&rect, CONNECT_MESSAGE_LEFT,
						  CONNECT_MESSAGE_TOP,
						  CONNECT_MESSAGE_RIGHT,
						  CONNECT_MESSAGE_BOTTOM);
			frontend_text_draw_centered(
				CONNECT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_671_PRESS_ALT_F4_TO_CANCEL_CONNECTION_ATTEMPT),
				&rect, g_color_red);
			break;
		default:
			break;
		}
		if (frame_counter == CONNECT_RESTORE_DISABLE_FRAME) {
			frontend_display_disable_offscreen_restore();
			return 0;
		}
	} else if (frame_counter == CONNECT_ACTION_FRAME) {
		network_type = g_game_config.network_type;
		switch ((network_transport_type)network_type) {
		case NET_TRANSPORT_TCPIP:
			frontend_draw_rect_assign(&rect, CONNECT_MESSAGE_LEFT,
						  CONNECT_MESSAGE_LOWER_TOP,
						  CONNECT_MESSAGE_RIGHT,
						  CONNECT_MESSAGE_LOWER_BOTTOM);
			frontend_text_draw_centered(
				CONNECT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_740_IF_ATTEMPT_TO_CONNECT_TO_THE_TCP_IP_ADDRESS_FAILS_TIMEOUT_WILL_OCCUR_IN_2_5_MINUTES),
				&rect, g_color_red);
			frontend_draw_rect_assign(&rect, CONNECT_MESSAGE_LEFT,
						  CONNECT_MESSAGE_TOP,
						  CONNECT_MESSAGE_RIGHT,
						  CONNECT_MESSAGE_BOTTOM);
			frontend_text_draw_centered(
				CONNECT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_740_IF_ATTEMPT_TO_CONNECT_TO_THE_TCP_IP_ADDRESS_FAILS_TIMEOUT_WILL_OCCUR_IN_2_5_MINUTES),
				&rect, g_color_red);
			break;
		case NET_TRANSPORT_MODEM:
		case NET_TRANSPORT_SERIAL:
			frontend_draw_rect_assign(&rect, CONNECT_MESSAGE_LEFT,
						  CONNECT_MESSAGE_LOWER_TOP,
						  CONNECT_MESSAGE_RIGHT,
						  CONNECT_MESSAGE_LOWER_BOTTOM);
			frontend_text_draw_centered(
				CONNECT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_671_PRESS_ALT_F4_TO_CANCEL_CONNECTION_ATTEMPT),
				&rect, g_color_red);
			frontend_draw_rect_assign(&rect, CONNECT_MESSAGE_LEFT,
						  CONNECT_MESSAGE_TOP,
						  CONNECT_MESSAGE_RIGHT,
						  CONNECT_MESSAGE_BOTTOM);
			frontend_text_draw_centered(
				CONNECT_FONT_SIZE,
				frontend_string_get(
					FRONTSTR_671_PRESS_ALT_F4_TO_CANCEL_CONNECTION_ATTEMPT),
				&rect, g_color_red);
			break;
		default:
			break;
		}

		g_mission_setup_is_host = 0;
		net_shutdown_direct_play_session();
		network_type = g_game_config.network_type;
		switch ((network_transport_type)network_type) {
		case NET_TRANSPORT_IPX:
		case NET_TRANSPORT_SERIAL:
			g_frontend_scratch_buffer[0] = '\0';
			break;
		case NET_TRANSPORT_TCPIP:
			frontend_display_flip_direct_draw_to_gdi_surface();
			frontend_draw_rect_assign(&rect, CONNECT_MESSAGE_LEFT,
						  CONNECT_MESSAGE_TOP,
						  CONNECT_MESSAGE_RIGHT,
						  CONNECT_GDI_MESSAGE_BOTTOM);
			frontend_display_draw_gdi_text_on_desktop(
				&rect,
				frontend_string_get(FRONTSTR_645_CONNECTING),
				frontend_string_get(
					FRONTSTR_740_IF_ATTEMPT_TO_CONNECT_TO_THE_TCP_IP_ADDRESS_FAILS_TIMEOUT_WILL_OCCUR_IN_2_5_MINUTES));
			strcpy(g_frontend_scratch_buffer,
			       g_game_config.ip_address);
			break;
		case NET_TRANSPORT_MODEM:
			strcpy(g_frontend_scratch_buffer,
			       g_game_config.phone_number);
			break;
		}

		char player_info[2];
		player_info[0] = (char)(g_pilot_data.rating + 1);
		player_info[1] = '\0';
		frontend_display_unlock_back_buffer();
		frontend_cursor_show_os_cursor();
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
		if (net_start_network_session(
			    (int)g_frontend_net_xvt_direct_play_app_guid[0],
			    (int)g_frontend_net_xvt_direct_play_app_guid[1],
			    (int)g_frontend_net_xvt_direct_play_app_guid[2],
			    (int)g_frontend_net_xvt_direct_play_app_guid[3],
			    player_info, g_pilot_data.name,
			    g_mission_setup_is_host,
			    g_frontend_net_session_list[0].game_name,
			    (network_transport_type)g_game_config.network_type,
			    0, 1, g_frontend_scratch_buffer, NULL) != 0) {
			int host_player_id = net_get_host_player_id();
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_JOIN_REQUEST;
			*(int *)g_frontend_net_packet_scratch.payload =
				FRONTEND_NET_PROTOCOL_VERSION;
			memcpy(g_frontend_net_packet_scratch.payload +
				       sizeof(int),
			       g_game_config.password,
			       sizeof(g_game_config.password));
			net_send_packet_and_flush(
				host_player_id, &g_frontend_net_packet_scratch,
				JOIN_REQUEST_PACKET_SIZE);
			frontend_screen_set_callbacks(
				frontend_net_await_join_admission_screen, NULL);
		} else {
			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NONE;
			frontend_screen_set_callbacks(concourse_update,
						      concourse_exit);
		}
		frontend_display_unlock_back_buffer();
		network_type = g_game_config.network_type;
		switch ((network_transport_type)network_type) {
		case NET_TRANSPORT_TCPIP:
			frontend_draw_rect_assign(&rect, CONNECT_MESSAGE_LEFT,
						  CONNECT_MESSAGE_TOP,
						  CONNECT_MESSAGE_RIGHT,
						  CONNECT_GDI_MESSAGE_BOTTOM);
			frontend_display_clear_desktop_gdi(&rect);
			break;
		default:
			break;
		}
		frontend_cursor_hide_os_cursor();
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
		frontend_cursor_show();
		frontend_display_enable_offscreen_restore();
	}
	return 0;
}
#endif

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
	if (frontend_button_handle_sprite_button(&rect, "join1u", "join1d",
#ifdef XVT_MODERN
						 "Refresh",
#else
						 frontend_string_get(
							 FRONTSTR_734_QUERY_ALL_GAMES),
#endif
						 QUERY_BUTTON_FONT_SIZE, 0,
						 QUERY_BUTTON_HELD_SLOT,
						 "jewelsound") != 0) {
#ifdef XVT_MODERN
		xvt_network_task_refresh();
#else
		frontend_net_probe_all_sessions();
#endif
	}
	return 1;
}

#ifndef XVT_MODERN
/* Returns a key for a session GUID, the sum of its parts: data2 shifted left
 * 16, data3, both words of data4 and data1, wrapping at 32 bits. Different
 * GUIDs can share a key, and a key of 0 marks an empty list entry. Only the
 * original build calls this. */
// FUNCTION: XVT 0x4D9280
int frontend_net_make_session_guid_key(struct net_session_guid guid)
{
	return (int)(((unsigned int)guid.data2 << 16) + guid.data3 +
		     guid.data4[1] + guid.data4[0] + guid.data1);
}
#endif

#ifndef XVT_MODERN
/* Updates the join screen's game list from DirectPlay's list of this game's
 * sessions; returns 0 at once while a game is selected, else the new count.
 * Entries whose GUID key is no longer listed are cleared; new sessions are
 * appended with 9 players needed (not yet probed), no query time, this build's
 * protocol version and no password, with a sound when any was added. Then it
 * closes the gaps, recounts (at most 32) and sorts the list. Does not check
 * that appended sessions fit in the 32 entries. Only the original build calls
 * this. */
// FUNCTION: XVT 0x4D92B0
int frontend_net_refresh_session_list(void)
{
	enum {
		FRONTEND_NET_SESSION_CAPACITY =
			sizeof(g_frontend_net_session_list) /
			sizeof(g_frontend_net_session_list[0]),
		FRONTEND_NET_NEW_SESSION_PLAYERS_NEEDED = 9,
		FRONTEND_NET_UI_SOUND_PRIORITY = 255,
		FRONTEND_NET_UI_SOUND_VOLUME_SCALE = 12,
		FRONTEND_NET_UI_SOUND_CENTER_PAN = 63
	};

	int added_session = 0;
	if (g_frontend_net_selected_session_idx != -1) {
		return 0;
	}

	struct net_session_enum_entry
		enumerated_sessions[FRONTEND_NET_SESSION_CAPACITY];
	unsigned int enumerated_session_count =
		(unsigned int)net_enumerate_app_sessions(
			g_frontend_net_xvt_direct_play_app_guid[0],
			g_frontend_net_xvt_direct_play_app_guid[1],
			g_frontend_net_xvt_direct_play_app_guid[2],
			g_frontend_net_xvt_direct_play_app_guid[3],
			enumerated_sessions, FRONTEND_NET_SESSION_CAPACITY,
			(network_transport_type)g_game_config.network_type);

	int existing_index = 0;
	unsigned int enumerated_index;
	if (g_frontend_net_session_count != 0) {
		do {
			int existing_guid_key =
				frontend_net_make_session_guid_key(
					g_frontend_net_session_list
						[existing_index]
							.session_guid);
			if (existing_guid_key != 0) {
				for (enumerated_index = 0;
				     enumerated_index <
				     enumerated_session_count;
				     ++enumerated_index) {
					int replacement_guid_key =
						frontend_net_make_session_guid_key(
							enumerated_sessions
								[enumerated_index]
									.session_guid);
					if (replacement_guid_key != 0 &&
					    replacement_guid_key ==
						    existing_guid_key) {
						memset(&enumerated_sessions
								[enumerated_index]
									.session_guid,
						       0,
						       sizeof(enumerated_sessions[enumerated_index]
								      .session_guid));
						break;
					}
				}
				if (enumerated_index ==
				    enumerated_session_count) {
					memset(&g_frontend_net_session_list
						       [existing_index],
					       0,
					       sizeof(g_frontend_net_session_list
							      [existing_index]));
				}
			}
			++existing_index;
		} while ((unsigned int)existing_index <
			 (unsigned int)g_frontend_net_session_count);
	}

	for (enumerated_index = 0; enumerated_index < enumerated_session_count;
	     ++enumerated_index) {
		int replacement_guid_key = frontend_net_make_session_guid_key(
			enumerated_sessions[enumerated_index].session_guid);
		if (replacement_guid_key != 0) {
			/* existing_index is reused here as the slot where the new session is appended. */
			existing_index =
				(unsigned int)g_frontend_net_session_count;
			strcpy(g_frontend_net_session_list[existing_index]
				       .game_name,
			       enumerated_sessions[enumerated_index]
				       .session_name);
			memcpy(&g_frontend_net_session_list[existing_index]
					.session_guid,
			       &enumerated_sessions[enumerated_index]
					.session_guid,
			       sizeof(g_frontend_net_session_list
					      [existing_index]
						      .session_guid));
			++g_frontend_net_session_count;
			added_session = 1;
			g_frontend_net_session_list[existing_index]
				.players_needed =
				FRONTEND_NET_NEW_SESSION_PLAYERS_NEEDED;
			g_frontend_net_session_list[existing_index]
				.last_query_ms = 0;
			g_frontend_net_session_list[existing_index].version =
				FRONTEND_NET_PROTOCOL_VERSION;
			g_frontend_net_session_list[existing_index]
				.password_required = 0;
		}
	}

	if (added_session != 0 && g_game_config.sfx_datapad_enabled != 0) {
		frontend_sound_play_ui_sound(
			"newpsound", 1, 0, FRONTEND_NET_UI_SOUND_PRIORITY,
			FRONTEND_NET_UI_SOUND_VOLUME_SCALE *
				g_game_config.sfx_datapad_volume,
			FRONTEND_NET_UI_SOUND_CENTER_PAN);
	}

	existing_index = 0;
	if (g_frontend_net_session_count != 0) {
		do {
			int existing_guid_key =
				frontend_net_make_session_guid_key(
					g_frontend_net_session_list
						[existing_index]
							.session_guid);
			if (existing_guid_key == 0) {
				for (unsigned int replacement_index =
					     existing_index + 1;
				     (unsigned int)replacement_index <
				     (unsigned int)g_frontend_net_session_count;
				     ++replacement_index) {
					int replacement_guid_key =
						frontend_net_make_session_guid_key(
							g_frontend_net_session_list
								[replacement_index]
									.session_guid);
					if (replacement_guid_key != 0) {
						memcpy(&g_frontend_net_session_list
							       [existing_index],
						       &g_frontend_net_session_list
							       [replacement_index],
						       sizeof(g_frontend_net_session_list
								      [existing_index]));
						memset(&g_frontend_net_session_list
							       [replacement_index],
						       0,
						       sizeof(g_frontend_net_session_list
								      [replacement_index]));
						break;
					}
				}
			}
			++existing_index;
		} while ((unsigned int)existing_index <
			 (unsigned int)g_frontend_net_session_count);
	}

	g_frontend_net_session_count = 0;
	while ((unsigned int)g_frontend_net_session_count <
	       FRONTEND_NET_SESSION_CAPACITY) {
		existing_index = (unsigned int)g_frontend_net_session_count;
		if (frontend_net_make_session_guid_key(
			    g_frontend_net_session_list[existing_index]
				    .session_guid) == 0) {
			break;
		}
		++g_frontend_net_session_count;
	}
	frontend_net_sort_sessions();
	return g_frontend_net_session_count;
}
#endif

#ifndef XVT_MODERN
/* qsort order for the game list: games on protocol version 101 first, then
 * games not in flight, then games needing players, then those needing more than
 * 8 (not yet probed), then by name. Returns a negative number, 0 or a positive
 * number. Only the original build calls this. */
// FUNCTION: XVT 0x4D95D0
int frontend_net_compare_session_list_entries(
	const struct frontend_net_session_entry *lhs,
	const struct frontend_net_session_entry *rhs)
{
	int result = (rhs->version == 101u) - (lhs->version == 101u);
	if (result == 0) {
		result =
			(rhs->game_in_flight == 0) - (lhs->game_in_flight == 0);
		if (result == 0) {
			unsigned int rhs_players_needed =
				(unsigned int)rhs->players_needed;
			unsigned int lhs_players_needed =
				(unsigned int)lhs->players_needed;
			result = (rhs_players_needed > 0) -
				 (lhs_players_needed > 0);
			if (result == 0) {
				result = (rhs_players_needed > 8u) -
					 (lhs_players_needed > 8u);
				if (result == 0) {
					return strcmp(lhs->game_name,
						      rhs->game_name);
				}
			}
		}
	}

	return result;
}
#endif

#ifndef XVT_MODERN
/* Sorts g_frontend_net_session_list and keeps the selection on the same game by
 * its GUID key; when the selected game is gone it drops the selection, its name
 * and mission text and ends the DirectPlay session. Returns the selected index,
 * or -1 for none or an empty list. Only the original build calls this. */
// FUNCTION: XVT 0x4D9680
int frontend_net_sort_sessions(void)
{
	enum { FRONTEND_NET_BRIEFING_TEXT_CAPACITY = 4096 };

	if (g_frontend_net_session_count == 0) {
		return -1;
	}
	int selected_session_guid_key;
	if (g_frontend_net_selected_session_idx != -1) {
		selected_session_guid_key = frontend_net_make_session_guid_key(
			g_frontend_net_session_list
				[g_frontend_net_selected_session_idx]
					.session_guid);
	}

	qsort(g_frontend_net_session_list, (size_t)g_frontend_net_session_count,
	      sizeof(g_frontend_net_session_list[0]),
	      (int (*)(const void *,
		       const void *))frontend_net_compare_session_list_entries);
	if (g_frontend_net_selected_session_idx != -1) {
		int session_index;
		for (session_index = 0;
		     (unsigned int)session_index <
		     (unsigned int)g_frontend_net_session_count;
		     ++session_index) {
			if (frontend_net_make_session_guid_key(
				    g_frontend_net_session_list[session_index]
					    .session_guid) ==
			    selected_session_guid_key) {
				g_frontend_net_selected_session_idx =
					session_index;
				break;
			}
		}
		if (session_index == g_frontend_net_session_count) {
			g_frontend_net_selected_session_idx = -1;
			g_frontend_net_received_mission_description_id = -1;
			memset(g_frontend_net_selected_game_name, 0,
			       sizeof(g_frontend_net_selected_game_name));
			memset(g_mission_text, 0,
			       FRONTEND_NET_BRIEFING_TEXT_CAPACITY);
			net_shutdown_direct_play_session();
		}
	}

	return g_frontend_net_selected_session_idx;
}
#endif

#ifndef XVT_MODERN
/* Drops the selection, refreshes the game list, probes every listed game in
 * turn, ends the DirectPlay session and sorts the list; returns the game count.
 * Only the original build calls this. */
// FUNCTION: XVT 0x4D9780
int frontend_net_probe_all_sessions(void)
{
	enum { BRIEFING_TEXT_CAPACITY = 4096 };

	g_frontend_net_selected_session_idx = -1;
	g_frontend_net_probe_mission_elapsed_seconds = 0;
	g_frontend_net_received_mission_description_id = -1;
	memset(g_frontend_net_selected_game_name, 0,
	       sizeof(g_frontend_net_selected_game_name));
	memset(g_mission_text, 0, BRIEFING_TEXT_CAPACITY);
	net_shutdown_direct_play_session();
	frontend_net_refresh_session_list();
	for (int session_index = 0;
	     session_index < g_frontend_net_session_count; ++session_index) {
		frontend_net_probe_session_by_index(session_index);
	}
	net_shutdown_direct_play_session();
	frontend_net_sort_sessions();
	return g_frontend_net_session_count;
}
#endif

#ifndef XVT_MODERN
/* Joins one listed game briefly to ask about it: ends any DirectPlay session,
 * names the game in g_frontend_net_selected_game_name, joins as a client, and sends
 * the host a PROBE_REQUEST, plus a CHAT_SYNC_REQUEST when it is the selected
 * game. It then reads packets for up to 2,000 ms until a probe answer or a
 * flight-session status arrives, loading the mission list and briefing from a
 * lobby STATE for the selected game. The answer's players needed, version and
 * password flag go into the entry, with game_in_flight 1 for a flight-session
 * status; with no answer the entry gets 0 players and version 0. Returns 1, or
 * 0 when the join failed, which also marks the entry and drops any selection.
 * Leaves the DirectPlay session open. Only the original build calls this. */
// FUNCTION: XVT 0x4D97F0
int frontend_net_probe_session_by_index(int session_idx)
{
	enum {
		SESSION_PROBE_PACKET_SIZE = sizeof(int),
		SESSION_PROBE_TIMEOUT_MS = 2000,
		BRIEFING_TEXT_CAPACITY = 4096,
	};

	net_shutdown_direct_play_session();
	strcpy(g_frontend_net_selected_game_name,
	       g_frontend_net_session_list[session_idx].game_name);
	switch ((network_transport_type)g_game_config.network_type) {
	case NET_TRANSPORT_IPX:
		g_frontend_scratch_buffer[0] = '\0';
		break;
	case NET_TRANSPORT_TCPIP:
		strcpy(g_frontend_scratch_buffer, g_game_config.ip_address);
		break;
	case NET_TRANSPORT_MODEM:
		strcpy(g_frontend_scratch_buffer, g_game_config.phone_number);
		break;
	default:
		break;
	}

	GUID session_guid;
	memcpy(&session_guid,
	       &g_frontend_net_session_list[session_idx].session_guid,
	       sizeof(session_guid));
	char player_info[2];
	player_info[0] = (char)(g_pilot_data.rating + 1);
	player_info[1] = '\0';
	if (net_start_network_session(
		    (int)g_frontend_net_xvt_direct_play_app_guid[0],
		    (int)g_frontend_net_xvt_direct_play_app_guid[1],
		    (int)g_frontend_net_xvt_direct_play_app_guid[2],
		    (int)g_frontend_net_xvt_direct_play_app_guid[3],
		    player_info, g_pilot_data.name, 0,
		    g_frontend_net_selected_game_name,
		    (network_transport_type)g_game_config.network_type, 0, 1,
		    g_frontend_scratch_buffer, &session_guid) == 0) {
		if (g_frontend_net_selected_session_idx != -1) {
			g_frontend_net_selected_session_idx = -1;
			g_frontend_net_received_mission_description_id = -1;
			g_frontend_net_probe_mission_elapsed_seconds = 0;
			memset(g_frontend_net_selected_game_name, 0,
			       sizeof(g_frontend_net_selected_game_name));
			memset(g_mission_text, 0, BRIEFING_TEXT_CAPACITY);
			frontend_net_refresh_session_list();
		}
		g_frontend_net_session_list[session_idx].players_needed = 0;
		g_frontend_net_session_list[session_idx].last_query_ms =
			GetTickCount();
		g_frontend_net_session_list[session_idx].version = 0;
		g_frontend_net_session_list[session_idx].password_required = 0;
		g_frontend_net_session_list[session_idx].game_in_flight = 0;
		return 0;
	}

	if (g_frontend_net_selected_session_idx != -1 &&
	    session_idx == g_frontend_net_selected_session_idx) {
		g_frontend_net_received_mission_directory_id =
			MISSION_DIRECTORY_TRAINING_EXERCISES;
		g_frontend_net_received_mission_description_id = -1;
	}
	int host_player_id = net_get_host_player_id();
	g_frontend_net_packet_scratch.packet_type = NET_PACKET_PROBE_REQUEST;
	net_send_packet_and_flush(host_player_id,
				  &g_frontend_net_packet_scratch,
				  SESSION_PROBE_PACKET_SIZE);
	if (g_frontend_net_selected_session_idx != -1 &&
	    session_idx == g_frontend_net_selected_session_idx) {
		g_frontend_net_packet_scratch.packet_type =
			NET_PACKET_CHAT_SYNC_REQUEST;
		net_send_packet_and_flush(host_player_id,
					  &g_frontend_net_packet_scratch,
					  SESSION_PROBE_PACKET_SIZE);
	}

	g_frontend_net_probe_players_needed = 0;
	g_frontend_net_probe_mission_elapsed_seconds = 0;
	unsigned int probe_start_ms = GetTickCount();
	unsigned int current_time_ms;
	int packet_type;
	do {
		packet_type = frontend_net_process_network_packets();
		current_time_ms = GetTickCount();
		if (packet_type == NET_PACKET_FLIGHT_SESSION_STATUS ||
		    packet_type == NET_PACKET_PROBE_RESPONSE) {
			break;
		}
		if (packet_type == NET_PACKET_STATE) {
			if (g_frontend_net_selected_session_idx != -1 &&
			    session_idx ==
				    g_frontend_net_selected_session_idx) {
				g_pilot_data.mission_directory_id =
					g_frontend_net_received_mission_directory_id;
				g_pilot_data.mission_description_ids
					[g_pilot_data.mission_directory_id] =
					g_frontend_net_received_mission_description_id;
				mission_setup_load_mission_list(
					g_pilot_data.mission_directory_id);
				mission_setup_load_mission_desc_text(
					g_mission_text);
				if (g_mission_list != NULL) {
					g_selected_mission_list_index = 0;
					while ((unsigned int)g_selected_mission_list_index <
						       (unsigned int)
							       g_mission_count &&
					       g_mission_list[g_selected_mission_list_index]
							       .mission_idx !=
						       g_pilot_data.mission_description_ids
							       [g_pilot_data
									.mission_directory_id]) {
						++g_selected_mission_list_index;
					}
				}
				g_frontend_first_visible_line = 0;
			} else {
				g_frontend_net_received_mission_description_id =
					-1;
			}
		}
	} while (current_time_ms - probe_start_ms < SESSION_PROBE_TIMEOUT_MS);

	if (packet_type == NET_PACKET_FLIGHT_SESSION_STATUS) {
		g_frontend_net_session_list[session_idx].last_query_ms =
			GetTickCount();
		g_frontend_net_session_list[session_idx].players_needed =
			g_frontend_net_probe_players_needed;
		g_frontend_net_session_list[session_idx].version =
			g_frontend_net_probe_version;
		g_frontend_net_session_list[session_idx].password_required =
			(uint8_t)g_frontend_net_probe_password_required;
		g_frontend_net_session_list[session_idx].game_in_flight = 1;
	} else if (packet_type == NET_PACKET_PROBE_RESPONSE) {
		g_frontend_net_session_list[session_idx].last_query_ms =
			GetTickCount();
		g_frontend_net_session_list[session_idx].players_needed =
			g_frontend_net_probe_players_needed;
		g_frontend_net_session_list[session_idx].version =
			g_frontend_net_probe_version;
		g_frontend_net_session_list[session_idx].password_required =
			(uint8_t)g_frontend_net_probe_password_required;
		g_frontend_net_session_list[session_idx].game_in_flight = 0;
	} else {
		g_frontend_net_session_list[session_idx].last_query_ms =
			GetTickCount();
		g_frontend_net_session_list[session_idx].players_needed = 0;
		g_frontend_net_session_list[session_idx].version = 0;
		g_frontend_net_session_list[session_idx].password_required = 0;
		g_frontend_net_session_list[session_idx].game_in_flight = 0;
	}
	return 1;
}
#endif

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

#ifndef XVT_MODERN
	char player_info[2];
	int local_player_id;
	int network_type;
#endif
	if (frame_counter == 0) {
		g_host_game_start_pending = 0;
		g_frontend_skip_screen_entry_setup = 0;
		g_frontend_quick_start_launch_flag = 0;
		g_config_connection_type_editable = 1;
		g_frontend_game_session_in_progress = 0;
		strcpy(g_pilot_data.multiplayer_game_name,
		       g_pilot_data.multiplayer_host_name);
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
			if (g_pilot_data.multiplayer_game_name[0] == '\0') {
				sprintf(g_pilot_data.multiplayer_game_name,
					"%s%s", g_pilot_data.name,
					frontend_string_get(
						FRONTSTR_470_S_GAME));
			}
#ifndef XVT_MODERN
			network_type = g_game_config.network_type;
			switch ((network_transport_type)network_type) {
			case NET_TRANSPORT_TCPIP:
				frontend_cursor_hide();
				break;
			case NET_TRANSPORT_MODEM:
			case NET_TRANSPORT_SERIAL:
				frontend_cursor_hide();
				frontend_draw_rect_assign(&rect, 84, 415, 605,
							  429);
				frontend_text_draw_centered(
					12,
					frontend_string_get(
						FRONTSTR_671_PRESS_ALT_F4_TO_CANCEL_CONNECTION_ATTEMPT),
					&rect, g_color_red);
				frontend_draw_rect_assign(&rect, 84, 90, 605,
							  106);
				frontend_text_draw_centered(
					12,
					frontend_string_get(
						FRONTSTR_671_PRESS_ALT_F4_TO_CANCEL_CONNECTION_ATTEMPT),
					&rect, g_color_red);
				break;
			default:
				break;
			}
#endif
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
#ifdef XVT_MODERN
		if (xvt_dialog_is_active()) {
			return 0;
		}
#endif
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
			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NONE;
			frontend_screen_set_callbacks(concourse_update,
						      concourse_exit);
		}
		frontend_button_disable_overlay_text();
		return 0;
	}

#ifndef XVT_MODERN
	network_type = g_game_config.network_type;
	if (network_type >= NET_TRANSPORT_MODEM &&
	    network_type <= NET_TRANSPORT_SERIAL) {
		frontend_draw_rect_assign(&rect, 84, 415, 605, 429);
		frontend_text_draw_centered(
			12,
			frontend_string_get(
				FRONTSTR_671_PRESS_ALT_F4_TO_CANCEL_CONNECTION_ATTEMPT),
			&rect, g_color_red);
		frontend_draw_rect_assign(&rect, 84, 90, 605, 106);
		frontend_text_draw_centered(
			12,
			frontend_string_get(
				FRONTSTR_671_PRESS_ALT_F4_TO_CANCEL_CONNECTION_ATTEMPT),
			&rect, g_color_red);
	}
#endif
	strcpy(g_pilot_data.multiplayer_host_name,
	       g_pilot_data.multiplayer_game_name);
	g_mission_setup_is_host = 1;
	g_host_game_start_pending = 0;
	frontend_cursor_show();
#ifndef XVT_MODERN
	network_type = g_game_config.network_type;
	switch ((network_transport_type)network_type) {
	case NET_TRANSPORT_IPX:
		frontend_cursor_show_os_cursor();
		g_frontend_scratch_buffer[0] = '\0';
		break;
	case NET_TRANSPORT_TCPIP:
		frontend_display_flip_direct_draw_to_gdi_surface();
		frontend_draw_rect_assign(&rect, 84, 90, 605, 429);
		frontend_display_draw_gdi_text_on_desktop(
			&rect,
			frontend_string_get(
				FRONTSTR_020_ACCESSING_IMPERIAL_NETWORK),
			NULL);
		frontend_cursor_show_os_cursor();
		strcpy(g_frontend_scratch_buffer, g_game_config.ip_address);
		break;
	case NET_TRANSPORT_MODEM:
		frontend_display_flip_direct_draw_to_gdi_surface();
		frontend_cursor_show_os_cursor();
		strcpy(g_frontend_scratch_buffer, g_game_config.phone_number);
		break;
	case NET_TRANSPORT_SERIAL:
		frontend_display_flip_direct_draw_to_gdi_surface();
		frontend_cursor_show_os_cursor();
		break;
	}
	player_info[0] = (char)(g_pilot_data.rating + 1);
	player_info[1] = '\0';
#endif
#ifdef XVT_MODERN
	xvt_network_task_begin(XVT_NETWORK_HOST);
	return 0;
#else
	if (net_start_network_session(
		    (int)g_frontend_net_xvt_direct_play_app_guid[0],
		    (int)g_frontend_net_xvt_direct_play_app_guid[1],
		    (int)g_frontend_net_xvt_direct_play_app_guid[2],
		    (int)g_frontend_net_xvt_direct_play_app_guid[3],
		    player_info, g_pilot_data.name, g_mission_setup_is_host,
		    g_pilot_data.multiplayer_game_name,
		    (network_transport_type)g_game_config.network_type, 0, 0,
		    g_frontend_scratch_buffer, NULL) == 0) {
		frontend_display_unlock_back_buffer();
		frontend_cursor_hide_os_cursor();
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
		g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NONE;
		frontend_screen_set_callbacks(concourse_update, concourse_exit);
		frontend_display_enable_offscreen_restore();
		return 0;
	}
	frontend_display_unlock_back_buffer();
	switch ((network_transport_type)g_game_config.network_type) {
	case NET_TRANSPORT_TCPIP:
		frontend_draw_rect_assign(&rect, 84, 90, 605, 429);
		frontend_display_clear_desktop_gdi(&rect);
		break;
	default:
		break;
	}
	frontend_cursor_hide_os_cursor();
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	local_player_id = net_get_local_player_id();
	net_set_player_ready(local_player_id);
	memset(g_mp_roster, 0, sizeof(g_mp_roster));
	strcpy(g_mp_roster[0].name, g_pilot_data.name);
	g_mp_roster[0].player_id = net_get_local_player_id();
	g_mp_roster[0].pilot_rating = g_pilot_data.rating;
	frontend_display_clear_offscreen_surface();
	frontend_screen_set_callbacks(mission_setup_update, mission_setup_exit);
	frontend_display_enable_offscreen_restore();
	return 0;
#endif
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
		break;

	case NET_PACKET_STATE:
		net_clear_player_ready_flags();
		memcpy(g_pilot_data.multiplayer_game_name, payload,
		       sizeof(g_pilot_data.multiplayer_game_name));
		g_frontend_net_received_mission_directory_id = payload[9];
		g_frontend_net_received_mission_description_id = payload[10];
		g_frontend_net_probe_players_needed = payload[11] - payload[12];
		ready_player_count = payload[12];
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
		}
		break;

	case NET_PACKET_JOIN_REQUEST:
#ifdef XVT_MODERN
		if (!net_is_host() || packet_size != 6 * sizeof(int)) {
			packet_type = NET_PACKET_NONE;
			break;
		}
#endif
		ready_player_count = net_count_ready_players();
		if (g_mission_setup_roster_authoritative != 0) {
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_ROSTER_LOCKED;
			net_send_packet_and_flush(
				sender_player_id,
				&g_frontend_net_packet_scratch, sizeof(int));
		} else if (ready_player_count >= MAX_PLAYERS) {
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_GAME_FULL;
			net_send_packet_and_flush(
				sender_player_id,
				&g_frontend_net_packet_scratch, sizeof(int));
		} else if (payload[0] != FRONTEND_NET_PROTOCOL_VERSION) {
			g_frontend_net_packet_scratch.packet_type =
				NET_PACKET_VERSION_MISMATCH;
			net_send_packet_and_flush(
				sender_player_id,
				&g_frontend_net_packet_scratch, sizeof(int));
		} else if (g_game_config.require_password != 0 &&
			   strncmp(g_game_config.password,
				   (const char *)&payload[1],
				   sizeof(g_game_config.password)) != 0) {
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
		} else {
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
#ifdef XVT_MODERN
		if (packet_size < 2 * sizeof(int) ||
		    sender_player_id != (DPID)net_get_host_player_id() ||
		    (xvt_network_session_get_status().state ==
			     XVT_NETWORK_SESSION_ADMISSION &&
		     !xvt_network_session_accept_admission(sender_player_id,
							   (DPID)payload[0]))) {
			packet_type = NET_PACKET_NONE;
			break;
		}
#endif
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
			packet_type = NET_PACKET_NONE;
			break;
		}
		if (g_game_config.sfx_datapad_enabled != 0) {
			frontend_sound_play_ui_sound(
				"newpsound", 1, 0, 255,
				12 * g_game_config.sfx_datapad_volume, 63);
		}
		net_mark_player_ready_no_lock(payload[0]);
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
		break;

	case NET_PACKET_NEXT_TOURNAMENT_MISSION:
	case NET_PACKET_NEXT_BATTLE_MISSION:
	case NET_PACKET_REPLAY_CURRENT_MISSION:
	case NET_PACKET_NEXT_CAMPAIGN_MISSION:
	case NET_PACKET_REPLAY_CAMPAIGN_MISSION:
		g_game_config.random_seed = payload[0];
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
		}
		break;

	case NET_PACKET_TEAM_ASSIGNMENT:
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
		break;

	case NET_PACKET_FLIGHT_ASSIGNMENTS:
		memcpy(&g_mission_setup_player_flight_group_indices
			       [payload[0] * MAX_PLAYERS],
		       &payload[1],
		       MAX_PLAYERS *
			       sizeof(g_mission_setup_player_flight_group_indices
					      [0]));
		break;

	case NET_PACKET_MISSION_CHOICE:
		g_frontend_net_packet_arg0 = payload[0];
		break;

	case NET_PACKET_BRIEFING_COUNTDOWN:
	case NET_PACKET_ASSIGNMENT_COUNTDOWN:
		g_frontend_net_packet_arg0 = payload[0];
		break;

	case NET_PACKET_RETURN_TO_MISSION_SELECTION:
		break;

	case NET_PACKET_RELEASE_TEAM_RESERVATION:
	case NET_PACKET_RELEASE_FLIGHT_RESERVATION:
		g_frontend_net_packet_arg0 = payload[0];
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
		break;

	case NET_PACKET_REPLAY_MISSION:
		g_game_config.random_seed = payload[0];
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
		}
		/* Continue with the network statistics carried by this packet. */

	case NET_PACKET_LOBBY_SELECTION:
		stats_count = payload[12];
		packet_word_index = 13;
		while (stats_count > 0) {
			if (net_is_host() == 0) {
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
		break;

	case NET_PACKET_TEAM_RESERVATION:
	case NET_PACKET_FLIGHT_RESERVATION:
		g_frontend_net_packet_arg0 = payload[0];
		g_frontend_net_reserving_player_id = payload[1];
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
					/* packet_size is reused here as the size of each outgoing chat chunk. */
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
		}
		break;

	case NET_PACKET_CHAT_SYNC_CHUNK:
		chunk_index = payload[0];
		packet_size -= 2 * sizeof(int);
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
		break;

	case NET_PACKET_READY_ROSTER:
		net_clear_player_ready_flags();
		g_frontend_net_probe_players_needed = payload[0] - payload[1];
		ready_player_count = payload[1];
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
		}
		break;

	case NET_PACKET_PILOT_RATING:
		g_frontend_net_packet_arg0 = payload[0];
		break;

	case NET_PACKET_BATTLE_PROGRESS:
		g_remote_battle_continuation_active = payload[0];
		g_remote_battle_sequence_continuation_choice = payload[1];
		g_remote_battle_last_completed_mission_index = payload[2];
		g_remote_battle_rebel_victory_count = payload[3];
		g_remote_battle_imperial_victory_count = payload[4];
		break;

	case NET_PACKET_MOVIE_SYNC:
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
		break;

	default:
		packet_type = NET_PACKET_NONE;
		break;
	}
	return packet_type;
}
