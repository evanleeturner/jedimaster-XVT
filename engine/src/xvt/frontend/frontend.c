#include "xvt/frontend/frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/assets/model_preview.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/front_image.h"
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
#include "xvt/frontend/mission_briefing.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/frontend/tech_library.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt_runtime/log/log_both_builds.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/frontend_actions.h"

enum {
	FRONTEND_CHAT_LOG_CAPACITY = 1024,
	FRONTEND_CURSOR_BYTES_PER_PIXEL = 2,
	FRONTEND_DATAPAD_MUSIC_TRACK = 7,
	FRONTEND_MUSIC_VOLUME_STEPS = 9,
	AUX_VOLUME_MAX = 65535,
};

/* Controls in g_scrollable_control_ids, 0 to 32. Written by
 * frontend_register_scrollable_control, frontend_unregister_scrollable_control,
 * frontend_reset_scrollable_controls and frontend_scrollbar_restore_state. */
// GLOBAL: XVT 0x52C004
int g_scrollable_control_count;
/* Copy of g_scrollable_control_count that frontend_scrollbar_save_state keeps for
 * frontend_scrollbar_restore_state. */
// GLOBAL: XVT 0x52C008
int g_scrollable_control_count_saved;
/* Ids of the scrollbars that Tab moves the keyboard focus through, in focus
 * order: entry 0 has the focus and takes Page Up, Page Down and the up and down
 * arrows. */
// GLOBAL: XVT 0x665470
int g_scrollable_control_ids[32];
/* Copy of g_scrollable_control_ids that frontend_scrollbar_save_state keeps for
 * frontend_scrollbar_restore_state. */
// GLOBAL: XVT 0x6654F0
int g_scrollable_control_ids_saved[32];
/* Background color of editable text fields, frontend_display_pack_rgb(64, 128,
 * 64); set by frontend_load_resources and read by the config screens. */
// GLOBAL: XVT 0xB69CC4
int g_editable_field_background_color = 0;
/* Gray, frontend_display_pack_rgb(96, 96, 96), set by frontend_load_resources;
 * mission lists draw unavailable entries in it. */
// GLOBAL: XVT 0xB69CCC
int g_color_gray = 0;
/* Navy, frontend_display_pack_rgb(0, 0, 128), set by frontend_load_resources;
 * scrollbars are drawn in it. */
// GLOBAL: XVT 0xBB2810
int g_color_navy = 0;
/* Shared 256-byte text buffer of the frontend: most screens format a line here
 * just before drawing it, and file readers use it for the line being read. */
// GLOBAL: XVT 0xB69D20
char g_frontend_scratch_buffer[256] = {0};
/* Pale cyan, frontend_display_pack_rgb(196, 252, 252), set by
 * frontend_load_resources; read by the button and scrollbar drawing. */
// GLOBAL: XVT 0xB69E24
int g_color_pale_cyan = 0;
/* Green, frontend_display_pack_rgb(0, 255, 0), set by frontend_load_resources. */
// GLOBAL: XVT 0xB6A2A0
int g_color_green = 0;
/* Blue, frontend_display_pack_rgb(0, 0, 255), set by frontend_load_resources. */
// GLOBAL: XVT 0xB6A2A4
int g_color_blue = 0;
/* Red, frontend_display_pack_rgb(255, 0, 0), set by frontend_load_resources. */
// GLOBAL: XVT 0xB69E38
int g_color_red = 0;
/* Yellow, frontend_display_pack_rgb(255, 255, 0), set by frontend_load_resources;
 * the most used text color. */
// GLOBAL: XVT 0xB6A2C0
int g_color_yellow = 0;
/* Green, frontend_display_pack_rgb(0, 255, 0), the same as g_color_green; set by
 * frontend_load_resources and read by the network game lists. */
// GLOBAL: XVT 0xB6A2D8
int g_color_green2 = 0;
/* First line shown of the mission text in its scrolling box; the box's
 * scrollbar sets it, and screens set 0 when they load a new text. Many
 * functions write it, chiefly in mission_setup.c. */
// GLOBAL: XVT 0x52C208
int g_frontend_first_visible_line = 0;
/* 1 when the host CD's first training mission is found, else 0; only
 * frontend_check_host_cd_present writes it. Hosting a game needs it. */
// GLOBAL: XVT 0xB6A250
int g_host_cd_available = 0;
/* Teal, frontend_display_pack_rgb(48, 111, 123), set by frontend_load_resources;
 * read by the button and scrollbar drawing. */
// GLOBAL: XVT 0xB6A264
int g_color_teal = 0;
/* Copy of g_color_red, set by frontend_load_resources; nothing reads it. */
// GLOBAL: XVT 0xB69CF0
int g_color_red2 = 0;
/* Copy of g_color_navy, set by frontend_load_resources; nothing reads it. */
// GLOBAL: XVT 0xB69CF4
int g_color_navy2 = 0;
/* Copy of g_color_blue, set by frontend_load_resources; nothing reads it. */
// GLOBAL: XVT 0xB69CF8
int g_color_blue2 = 0;
/* Copy of g_color_yellow, set by frontend_load_resources; nothing reads it. */
// GLOBAL: XVT 0xB69CFC
int g_color_yellow2 = 0;
/* Violet, frontend_display_pack_rgb(128, 0, 255), set by frontend_load_resources;
 * nothing reads it. */
// GLOBAL: XVT 0xB69D00
int g_color_violet = 0;
/* Spring green, frontend_display_pack_rgb(0, 255, 128), set by
 * frontend_load_resources; nothing reads it. */
// GLOBAL: XVT 0xB69D04
int g_color_spring_green = 0;
/* Copy of g_editable_field_background_color, set by frontend_load_resources;
 * nothing reads it. */
// GLOBAL: XVT 0xB69D08
int g_color_muted_green2 = 0;
/* Cyan, frontend_display_pack_rgb(0, 255, 255), set by frontend_load_resources;
 * nothing reads it. */
// GLOBAL: XVT 0xB69D0C
int g_color_cyan = 0;
/* Azure, frontend_display_pack_rgb(0, 128, 255), set by frontend_load_resources;
 * nothing reads it. */
// GLOBAL: XVT 0xB69D10
int g_color_azure = 0;
/* Orange, frontend_display_pack_rgb(255, 128, 0), set by frontend_load_resources;
 * nothing reads it. */
// GLOBAL: XVT 0xB69D14
int g_color_orange = 0;
/* Twelve shades of yellow for pulsing text, from frontend_display_pack_rgb(128,
 * 128, 0) up to (255, 255, 0) at entry 6 and back down to (149, 149, 0); set by
 * frontend_load_resources. */
// GLOBAL: XVT 0xB6A270
int g_pulse_color_ramp[12] = {0};
/* 1 when the pilot record pages must reload their mission lists and totals
 * before drawing; the concourse and pilot changes set it, and the pages set 0
 * once rebuilt. Many functions write it, chiefly in pilot_record.c. */
// GLOBAL: XVT 0x664F2C
int g_pilot_record_pages_need_rebuild = 0;
/* 1 when the CD music could not be started and the concourse has not yet said
 * so. frontend_load_resources sets it when cd_audio_initialize fails, the
 * concourse's CD retry loop updates it, and the concourse sets 0 once it has
 * shown the warning. */
// GLOBAL: XVT 0x664EC4
int g_cd_audio_warning_pending = 0;
/* Passed to file_check_game_cd_present, whose original build checks the CD's movie
 * files only while this is 0. frontend_load_resources sets 0 and the concourse's
 * first frame sets 1 after its CD checks; while it is set, the original
 * concourse also skips its movie CD check. */
// GLOBAL: XVT 0xBB2818
int g_skip_movie_checks = 0;

/* Loads the frontend: the mode start function when the intro is skipped, and
 * the last step of the credits otherwise. Sets g_game_main_skip_intro_relaunch_gate
 * to 1 and g_skip_movie_checks to 0, checks for the host CD, sets the display
 * options (Esc does not quit, clear color 0, cursor shown, no clearing after a
 * present, the back buffer refilled from the offscreen surface), loads fonts
 * 15, 12 and 10, the five image lists, the sound list and the cursor image,
 * clears g_pilot_data, and loads the string table, the cutscene table and the
 * campaign award images. Allocates and zeroes the 1024-byte chat log, sets the
 * frontend colors, loads the config, takes a pilot name from the command line,
 * loads the last pilot, and starts the CD music: track 7 at the music volume
 * with datapad music on, else stops the track. Returns 0, which lets the
 * frontend start; checks no load result. */
// FUNCTION: XVT 0x4BDB90
int frontend_load_resources(void)
{
	g_game_main_skip_intro_relaunch_gate = 1;
	g_skip_movie_checks = 0;
	frontend_check_host_cd_present();
	frontend_display_disable_escape_close();
	frontend_display_set_surface_clear_color(0);
	frontend_cursor_show();
	frontend_display_disable_clear_after_present();
	frontend_display_enable_offscreen_restore();
	frontend_text_load_font(15);
	frontend_text_load_font(12);
	frontend_text_load_font(10);
	front_image_load_resource_list("frontres\\top.lst");
	front_image_load_resource_list("frontres\\side.lst");
	front_image_load_resource_list("frontres\\awards.lst");
	front_image_load_resource_list("frontres\\promo.lst");
	front_image_load_resource_list("frontres\\icons.lst");
	frontend_sound_load_list("sfx\\sfx.lst");
	struct RECT cursor_rect;
	front_image_get_resource_rect("cursor", &cursor_rect);
	if (g_cursor_save_buffer != NULL) {
		free(g_cursor_save_buffer);
		g_cursor_save_buffer = NULL;
	}
	g_cursor_save_buffer =
		malloc(FRONTEND_CURSOR_BYTES_PER_PIXEL *
		       (cursor_rect.bottom + 1) * (cursor_rect.right + 1));
	if (g_cursor_save_buffer == NULL) {
		XVT_LOG_ERROR(
			"frontend.alloc_failed what=\"cursor_background\" bytes=%d",
			FRONTEND_CURSOR_BYTES_PER_PIXEL *
				(cursor_rect.bottom + 1) *
				(cursor_rect.right + 1));
	}
	frontend_cursor_set_image_from_resource_name("cursor",
						     g_cursor_save_buffer);
	sprintf(g_frontend_scratch_buffer, "%p\n", (void *)&g_pilot_data);
	memset(&g_pilot_data, 0, sizeof(g_pilot_data));
	frontend_string_load_table("fronttxt.txt");
	cutscene_load_table("movies\\cutscene.lst");
	pilot_record_load_campaign_award_sprite_table("frontres\\campawds.lst");
	g_frontend_chat_log_buffer = malloc(FRONTEND_CHAT_LOG_CAPACITY);
	g_frontend_chat_log_used_bytes = 0;
	if (g_frontend_chat_log_buffer != NULL) {
		memset(g_frontend_chat_log_buffer, 0,
		       FRONTEND_CHAT_LOG_CAPACITY);
	} else {
		XVT_LOG_ERROR(
			"frontend.alloc_failed what=\"chat_log\" bytes=%d",
			FRONTEND_CHAT_LOG_CAPACITY);
	}

	g_color_green2 = frontend_display_pack_rgb(0, 255, 0);
	g_color_navy = frontend_display_pack_rgb(0, 0, 128);
	g_editable_field_background_color =
		frontend_display_pack_rgb(64, 128, 64);
	g_color_green = frontend_display_pack_rgb(0, 255, 0);
	g_color_red = frontend_display_pack_rgb(255, 0, 0);
	g_color_blue = frontend_display_pack_rgb(0, 0, 255);
	g_color_yellow = frontend_display_pack_rgb(255, 255, 0);
	g_color_gray = frontend_display_pack_rgb(96, 96, 96);
	g_color_pale_cyan = frontend_display_pack_rgb(196, 252, 252);
	g_color_teal = frontend_display_pack_rgb(48, 111, 123);
	g_color_red2 = g_color_red;
	g_color_navy2 = g_color_navy;
	g_color_blue2 = g_color_blue;
	g_color_yellow2 = g_color_yellow;
	g_color_violet = frontend_display_pack_rgb(128, 0, 255);
	g_color_spring_green = frontend_display_pack_rgb(0, 255, 128);
	g_color_muted_green2 = g_editable_field_background_color;
	g_color_cyan = frontend_display_pack_rgb(0, 255, 255);
	g_color_azure = frontend_display_pack_rgb(0, 128, 255);
	g_color_orange = frontend_display_pack_rgb(255, 128, 0);
	g_pulse_color_ramp[0] = frontend_display_pack_rgb(128, 128, 0);
	g_pulse_color_ramp[1] = frontend_display_pack_rgb(149, 149, 0);
	g_pulse_color_ramp[2] = frontend_display_pack_rgb(170, 170, 0);
	g_pulse_color_ramp[3] = frontend_display_pack_rgb(192, 192, 0);
	g_pulse_color_ramp[4] = frontend_display_pack_rgb(213, 213, 0);
	g_pulse_color_ramp[5] = frontend_display_pack_rgb(234, 234, 0);
	g_pulse_color_ramp[6] = frontend_display_pack_rgb(255, 255, 0);
	g_pulse_color_ramp[7] = frontend_display_pack_rgb(234, 234, 0);
	g_pulse_color_ramp[8] = frontend_display_pack_rgb(213, 213, 0);
	g_pulse_color_ramp[9] = frontend_display_pack_rgb(192, 192, 0);
	g_pulse_color_ramp[10] = frontend_display_pack_rgb(170, 170, 0);
	g_pulse_color_ramp[11] = frontend_display_pack_rgb(149, 149, 0);

	config_load();
	pilot_parse_command_line(g_cmd_line);
	pilot_find_and_load_by_name(g_game_config.last_pilot_name);
	g_cd_audio_warning_pending = cd_audio_initialize() == 0;
	cd_audio_enable_loop_current_track();
	if (g_game_config.datapad_music_enabled != 0) {
		cd_audio_set_aux_volume(AUX_VOLUME_MAX *
					g_game_config.music_volume /
					FRONTEND_MUSIC_VOLUME_STEPS);
		cd_audio_play_track_from_time(FRONTEND_DATAPAD_MUSIC_TRACK, 0,
					      0);
	} else {
		cd_audio_stop_current_track();
	}
	XVT_LOG_INFO(
		"frontend.menus_loaded cutscenes=%d has_pilot=%d host_cd=%d music=%d music_failed=%d",
		g_cutscene_count, g_pilot_data.name[0] != '\0',
		g_host_cd_available, (int)g_game_config.datapad_music_enabled,
		g_cd_audio_warning_pending);
	return 0;
}

/* Handles the buttons along the top of the frontend screens and the help
 * toggle, and returns 1 only when the player quits the game. screen_context is 0
 * for the concourse and the join and host screens, 1 for the mission setup
 * screens (mission, teams, flights, battle choice and craft selection), 2 for
 * the config screen, 3 for the craft database and 4 for the debriefing; most
 * buttons show only in some contexts. Exit, or Esc, asks first (differently
 * in a network game, during a mission sequence, or plainly), then tells the
 * other players when needed,
 * writes the config and shuts DirectPlay down. Config opens the config screen,
 * or closes it from the config context, then sending the game options to the
 * players when hosting. Join, Host (with the host CD only), Fly solo and Pilots
 * ask first while a game session is in progress, need a selected pilot (except
 * Pilots), set g_frontend_mission_session_mode and switch to the join, host,
 * mission setup or concourse screen; leaving mission setup this way tells the
 * other players and shuts the session down. The craft database button pushes
 * the craft database, or closes it from that context. Returns 0 otherwise; the
 * modern build also returns 0 while one of its dialogs is up. */
// FUNCTION: XVT 0x4BF9B0
int frontend_handle_common_screen_controls(int screen_context)
{
	enum {
		SCREEN_CONTEXT_MISSION = 1,
		SCREEN_CONTEXT_CONFIG = 2,
		SCREEN_CONTEXT_TECH_LIBRARY = 3,
		SCREEN_CONTEXT_DEBRIEF = 4,
		NEVER_STORED_SESSION_MODE = 5,
		BUTTON_FONT_SIZE = 12,
		UI_SOUND_PRIORITY = 255,
		UI_SOUND_PAN_CENTER = 63,
		EXIT_HELD_SLOT = 6,
		CONFIG_HELD_SLOT = 5,
		HOST_HELD_SLOT = 4,
		JOIN_HELD_SLOT = 3,
		SOLO_HELD_SLOT = 2,
		CRAFT_HELD_SLOT = 1,
		PILOT_HELD_SLOT = 0,
		CONFIG_PACKET_SIZE = 19 * sizeof(int),
	};

	int transition_needs_session_shutdown = 0;
	int mouse_x;
	int mouse_y;
	frontend_cursor_get_pos(&mouse_x, &mouse_y);
	struct RECT rect;
	frontend_draw_rect_assign(&rect, 610, 445, 634, 469);
	int action_triggered;
	if (g_game_config.help_on != 0) {
		front_image_draw_sprite("helpdown", 610, 445);
		action_triggered = frontend_button_handle_sprite_button(
			&rect, NULL, NULL,
			frontend_string_get(FRONTSTR_704_HELP_TEXT_OFF),
			BUTTON_FONT_SIZE, 0, 35, "buttonsound");
	} else {
		front_image_draw_sprite("helpup", 610, 445);
		action_triggered = frontend_button_handle_sprite_button(
			&rect, NULL, NULL,
			frontend_string_get(FRONTSTR_703_HELP_TEXT_ON),
			BUTTON_FONT_SIZE, 0, 35, "buttonsound");
	}
	if (action_triggered != 0) {
		g_game_config.help_on ^= 1;
		XVT_LOG_DEBUG("frontend.help_toggled help=%d context=%d",
			      (int)g_game_config.help_on, screen_context);
	}
	if (g_game_config.help_on != 0) {
		frontend_button_enable_overlay_text();
	}

	frontend_draw_rect_assign(&rect, 586, 4, 633, 71);
	frontend_button_set_overlay_text(
		frontend_string_get(FRONTSTR_568_EXIT));
	if (screen_context < SCREEN_CONTEXT_CONFIG ||
	    screen_context > SCREEN_CONTEXT_TECH_LIBRARY) {
		if (screen_context != SCREEN_CONTEXT_DEBRIEF) {
			action_triggered = frontend_button_handle_sprite_button(
				&rect, NULL, "exitdown",
				frontend_string_get(
					FRONTSTR_006_EXIT_TO_WINDOWS),
				BUTTON_FONT_SIZE, 0, EXIT_HELD_SLOT,
				"buttonsound");
		} else {
			front_image_draw_sprite("configup", 0, 0);
			action_triggered = frontend_button_handle_sprite_button(
				&rect, "exitup", "exitdown",
				frontend_string_get(
					FRONTSTR_006_EXIT_TO_WINDOWS),
				BUTTON_FONT_SIZE, 0, EXIT_HELD_SLOT,
				"buttonsound");
		}
		if (keyboard_peek_char() == 27) {
			action_triggered = 1;
			keyboard_discard_char();
		}
		action_triggered = xvt_frontend_action_trigger(
			XVT_ACTION_OWNER_COMMON, 1, action_triggered);
		if (action_triggered != 0) {
			if (g_frontend_game_session_in_progress != 0) {
				if (g_frontend_mission_session_mode !=
				    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
					if (net_is_host() != 0) {
						action_triggered = frontend_dialog_show_confirm_dialog(
							frontend_string_get(
								FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
							frontend_string_get(
								FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
							frontend_string_get(
								FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
							frontend_string_get(
								FRONTSTR_523_OKAY),
							frontend_string_get(
								FRONTSTR_019_CANCEL));
						if (xvt_dialog_is_active()) {
							return 0;
						}
					} else {
						action_triggered = frontend_dialog_show_confirm_dialog(
							frontend_string_get(
								FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
							frontend_string_get(
								FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
							frontend_string_get(
								FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
							frontend_string_get(
								FRONTSTR_523_OKAY),
							frontend_string_get(
								FRONTSTR_019_CANCEL));
						if (xvt_dialog_is_active()) {
							return 0;
						}
					}
				} else if (g_pilot_data
						   .mission_sequence_active ==
					   1) {
					if (g_pilot_data.mission_directory_id ==
					    MISSION_DIRECTORY_MELEES) {
						action_triggered = frontend_dialog_show_confirm_dialog(
							frontend_string_get(
								FRONTSTR_678_YOU_ARE_CURRENTLY_PLAYING_A_TOURNAMENT),
							frontend_string_get(
								FRONTSTR_679_ARE_YOU_SURE_YOU_WANT_TO),
							frontend_string_get(
								FRONTSTR_680_TERMINATE_THIS_TOURNAMENT),
							frontend_string_get(
								FRONTSTR_523_OKAY),
							frontend_string_get(
								FRONTSTR_019_CANCEL));
						if (xvt_dialog_is_active()) {
							return 0;
						}
					} else if (
						g_pilot_data
							.mission_directory_id ==
						MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
						action_triggered = frontend_dialog_show_confirm_dialog(
							frontend_string_get(
								FRONTSTR_681_YOU_ARE_CURRENTLY_PLAYING_A_BATTLE),
							frontend_string_get(
								FRONTSTR_682_ARE_YOU_SURE_YOU_WANT_TO),
							frontend_string_get(
								FRONTSTR_683_TERMINATE_THIS_BATTLE),
							frontend_string_get(
								FRONTSTR_523_OKAY),
							frontend_string_get(
								FRONTSTR_019_CANCEL));
						if (xvt_dialog_is_active()) {
							return 0;
						}
					} else {
						action_triggered = frontend_dialog_show_confirm_dialog(
							frontend_string_get(
								FRONTSTR_779_YOU_ARE_CURRENTLY_PLAYING_A_CAMPAIGN),
							frontend_string_get(
								FRONTSTR_780_ARE_YOU_SURE_YOU_WANT_TO),
							frontend_string_get(
								FRONTSTR_781_TERMINATE_THIS_CAMPAIGN),
							frontend_string_get(
								FRONTSTR_523_OKAY),
							frontend_string_get(
								FRONTSTR_019_CANCEL));
						if (xvt_dialog_is_active()) {
							return 0;
						}
					}
				} else {
					action_triggered = frontend_dialog_show_confirm_dialog(
						frontend_string_get(
							FRONTSTR_634_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
						frontend_string_get(
							FRONTSTR_635_EMPTY_TRANSLATION_PLACEHOLDER),
						frontend_string_get(
							FRONTSTR_636_EMPTY_TRANSLATION_PLACEHOLDER),
						frontend_string_get(
							FRONTSTR_523_OKAY),
						frontend_string_get(
							FRONTSTR_019_CANCEL));
					if (xvt_dialog_is_active()) {
						return 0;
					}
				}
			} else {
				action_triggered = frontend_dialog_show_confirm_dialog(
					frontend_string_get(
						FRONTSTR_634_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
					frontend_string_get(
						FRONTSTR_635_EMPTY_TRANSLATION_PLACEHOLDER),
					frontend_string_get(
						FRONTSTR_636_EMPTY_TRANSLATION_PLACEHOLDER),
					frontend_string_get(FRONTSTR_523_OKAY),
					frontend_string_get(
						FRONTSTR_019_CANCEL));
				if (xvt_dialog_is_active()) {
					return 0;
				}
			}
			if (action_triggered != 0) {
				XVT_LOG_INFO(
					"frontend.quit_confirmed context=%d mode=%d session=%d",
					screen_context,
					(int)g_frontend_mission_session_mode,
					g_frontend_game_session_in_progress);
				switch (screen_context) {
				default:
					config_write();
					net_shutdown_direct_play_session_for_quit();
					frontend_button_disable_overlay_text();
					return 1;
				case SCREEN_CONTEXT_MISSION:
					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (net_is_host() != 0) {
							g_frontend_net_packet_scratch
								.packet_type =
								NET_PACKET_HOST_CANCELLED;
							net_send_packet_and_flush(
								0,
								&g_frontend_net_packet_scratch,
								sizeof(int));
						} else {
							g_frontend_net_packet_scratch
								.packet_type =
								NET_PACKET_PLAYER_LEFT;
							net_send_packet_and_flush(
								net_get_host_player_id(),
								&g_frontend_net_packet_scratch,
								sizeof(int));
						}
					}
					config_write();
					net_shutdown_direct_play_session_for_quit();
					frontend_button_disable_overlay_text();
					return 1;
				case SCREEN_CONTEXT_DEBRIEF:
					if (g_pilot_data
						    .mission_sequence_active ==
					    0) {
						if (g_frontend_mission_session_mode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
							if (net_is_host() !=
							    0) {
								g_frontend_net_packet_scratch
									.packet_type =
									NET_PACKET_SESSION_CANCELLED;
								net_send_packet_and_flush(
									0,
									&g_frontend_net_packet_scratch,
									sizeof(int));
							} else {
								g_frontend_net_packet_scratch
									.packet_type =
									NET_PACKET_PLAYER_LEFT;
								net_send_packet_and_flush(
									net_get_host_player_id(),
									&g_frontend_net_packet_scratch,
									sizeof(int));
							}
						}
					} else if (
						net_is_host() == 0 &&
						g_frontend_mission_session_mode !=
							FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_PLAYER_LEFT;
						net_send_packet_and_flush(
							net_get_host_player_id(),
							&g_frontend_net_packet_scratch,
							sizeof(int));
					} else if (
						g_frontend_mission_session_mode !=
						FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						g_frontend_net_packet_scratch
							.packet_type =
							NET_PACKET_HOST_CANCELLED;
						net_send_packet_and_flush(
							0,
							&g_frontend_net_packet_scratch,
							sizeof(int));
					}
					config_write();
					net_shutdown_direct_play_session_for_quit();
					frontend_button_disable_overlay_text();
					return 1;
				}
			}
		}
	}

	frontend_draw_rect_assign(&rect, 503, 4, 581, 56);
	frontend_button_set_overlay_text(
		frontend_string_get(FRONTSTR_567_CONFIG));
	struct RECT screen_rect;
	if (g_frontend_mission_session_mode == NEVER_STORED_SESSION_MODE ||
	    screen_context == SCREEN_CONTEXT_CONFIG) {
		frontend_button_use_pressed_overlay_style();
		frontend_button_draw_sprite_and_tooltip(
			&rect, "configdown",
			frontend_string_get(FRONTSTR_570_EXIT_CONFIGURATION),
			BUTTON_FONT_SIZE, 0);
		if (frontend_draw_point_in_rect(&rect, mouse_x, mouse_y) != 0 &&
		    (frontend_mouse_get_left_click() != 0 ||
		     frontend_mouse_get_right_click() != 0)) {
			XVT_LOG_DEBUG(
				"frontend.options_closed context=%d mode=%d",
				screen_context,
				(int)g_frontend_mission_session_mode);
			if (g_game_config.sfx_datapad_enabled != 0) {
				frontend_sound_play_ui_sound(
					"buttonsound", 1, 0, UI_SOUND_PRIORITY,
					12 * g_game_config.sfx_datapad_volume,
					UI_SOUND_PAN_CENTER);
			}
			config_write();
			g_active_text_field_id = 0;
			keyboard_flush_char_buffer();
			frontend_screen_pop_state();
			frontend_mouse_clear_input_gate();
			front_image_free_resource_by_name("backconfig");
			frontend_text_stop_text_fade();
			frontend_scrollbar_restore_state();
			if (g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_NET_HOST) {
				g_frontend_net_packet_scratch.packet_type =
					NET_PACKET_GAME_OPTIONS;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[0] = g_game_config.difficulty;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[4] = g_game_config.collisions;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[8] =
					g_game_config.craft_jumping;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[12] =
					g_game_config.random_setup;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[16] =
					g_game_config.battle_length_index;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[20] =
					g_game_config.require_password;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[24] =
					g_game_config.in_progress_join;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[28] =
					g_game_config.craft_selection;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[32] =
					g_game_config.locate_players;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[36] =
					g_game_config.craft_waves;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[40] =
					g_game_config.mission_time_limit;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[44] =
					g_game_config
						.last_team_time_limit_minutes;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[48] = rand();
				*(int *)&g_frontend_net_packet_scratch
					 .payload[52] =
					g_game_config.internet_play;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[56] =
					g_game_config.ai_opponents;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[60] =
					g_game_config.server_update_rate;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[64] =
					(uint8_t)g_game_config.combat_balance;
				*(int *)&g_frontend_net_packet_scratch
					 .payload[68] =
					(uint8_t)g_game_config
						.continue_battle_or_campaign;
				XVT_LOG_DEBUG(
					"frontend.options_sent difficulty=%d collisions=%d jumping=%d random=%d length=%d password=%d join=%d craft=%d locate=%d waves=%d time_limit=%d team_limit=%d seed=%d internet=%d ai=%d rate=%d balance=%d continue=%d",
					*(int *)&g_frontend_net_packet_scratch
						 .payload[0],
					*(int *)&g_frontend_net_packet_scratch
						 .payload[4],
					*(int *)&g_frontend_net_packet_scratch
						 .payload[8],
					*(int *)&g_frontend_net_packet_scratch
						 .payload[12],
					*(int *)&g_frontend_net_packet_scratch
						 .payload[16],
					*(int *)&g_frontend_net_packet_scratch
						 .payload[20],
					*(int *)&g_frontend_net_packet_scratch
						 .payload[24],
					*(int *)&g_frontend_net_packet_scratch
						 .payload[28],
					*(int *)&g_frontend_net_packet_scratch
						 .payload[32],
					*(int *)&g_frontend_net_packet_scratch
						 .payload[36],
					*(int *)&g_frontend_net_packet_scratch
						 .payload[40],
					*(int *)&g_frontend_net_packet_scratch
						 .payload[44],
					*(int *)&g_frontend_net_packet_scratch
						 .payload[48],
					*(int *)&g_frontend_net_packet_scratch
						 .payload[52],
					*(int *)&g_frontend_net_packet_scratch
						 .payload[56],
					*(int *)&g_frontend_net_packet_scratch
						 .payload[60],
					*(int *)&g_frontend_net_packet_scratch
						 .payload[64],
					*(int *)&g_frontend_net_packet_scratch
						 .payload[68]);
				net_send_packet_and_flush(
					0, &g_frontend_net_packet_scratch,
					CONFIG_PACKET_SIZE);
			}
		}
	} else if (screen_context < SCREEN_CONTEXT_TECH_LIBRARY ||
		   screen_context > SCREEN_CONTEXT_TECH_LIBRARY) {
		if (frontend_button_handle_sprite_button(
			    &rect, NULL, "configdown",
			    frontend_string_get(FRONTSTR_005_CONFIGURATION),
			    BUTTON_FONT_SIZE, 0, CONFIG_HELD_SLOT,
			    "buttonsound") != 0 &&
		    (screen_context >= 0 &&
		     (screen_context <= SCREEN_CONTEXT_MISSION ||
		      screen_context == SCREEN_CONTEXT_DEBRIEF))) {
			XVT_LOG_DEBUG(
				"frontend.screen_requested screen=\"options\" context=%d",
				screen_context);
			frontend_draw_rect_assign(&screen_rect, 0, 0, 640, 480);
			frontend_screen_queue_push(
				config_options_datapad_update, &screen_rect);
		}
	}

	if (screen_context < SCREEN_CONTEXT_CONFIG) {
		frontend_draw_rect_assign(&rect, 390, 4, 496, 43);
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_003_JOIN_GAME));
		const char *tooltip_text;
		switch (g_game_config.network_type) {
		case NET_TRANSPORT_IPX:
			if (g_game_config.internet_play != 0) {
				tooltip_text = frontend_string_get(
					FRONTSTR_727_JOIN_INTERNET_IPX_GAME);
			} else {
				tooltip_text = frontend_string_get(
					FRONTSTR_728_JOIN_LOCAL_IPX_GAME);
			}
			break;
		case NET_TRANSPORT_TCPIP:
			if (g_game_config.internet_play != 0) {
				tooltip_text = frontend_string_get(
					FRONTSTR_729_JOIN_INTERNET_TCP_IP_GAME);
			} else {
				tooltip_text = frontend_string_get(
					FRONTSTR_730_JOIN_LOCAL_TCP_IP_GAME);
			}
			break;
		case NET_TRANSPORT_MODEM:
			tooltip_text = frontend_string_get(
				FRONTSTR_731_JOIN_DIRECT_MODEM_GAME);
			break;
		case NET_TRANSPORT_SERIAL:
			tooltip_text = frontend_string_get(
				FRONTSTR_732_JOIN_DIRECT_SERIAL_GAME);
			break;
		default:
			tooltip_text =
				frontend_string_get(FRONTSTR_003_JOIN_GAME);
			break;
		}
		strcpy(g_frontend_scratch_buffer, tooltip_text);
		if (g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_NET_CLIENT) {
			frontend_button_use_pressed_overlay_style();
			frontend_button_draw_sprite_and_tooltip(
				&rect, "joinindown", g_frontend_scratch_buffer,
				BUTTON_FONT_SIZE, 0);
		} else {
			action_triggered = frontend_button_handle_sprite_button(
				&rect, NULL, "joinindown",
				g_frontend_scratch_buffer, BUTTON_FONT_SIZE, 0,
				JOIN_HELD_SLOT, "buttonsound");
			action_triggered = xvt_frontend_action_trigger(
				XVT_ACTION_OWNER_COMMON, 2, action_triggered);
			if (action_triggered != 0) {
				if (g_frontend_game_session_in_progress != 0) {
					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (net_is_host() != 0) {
							action_triggered = frontend_dialog_show_confirm_dialog(
								frontend_string_get(
									FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
								frontend_string_get(
									FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
								frontend_string_get(
									FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								frontend_string_get(
									FRONTSTR_523_OKAY),
								frontend_string_get(
									FRONTSTR_019_CANCEL));
							if (xvt_dialog_is_active()) {
								return 0;
							}
						} else {
							action_triggered = frontend_dialog_show_confirm_dialog(
								frontend_string_get(
									FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
								frontend_string_get(
									FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								frontend_string_get(
									FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
								frontend_string_get(
									FRONTSTR_523_OKAY),
								frontend_string_get(
									FRONTSTR_019_CANCEL));
							if (xvt_dialog_is_active()) {
								return 0;
							}
						}
					} else if (
						g_pilot_data
							.mission_sequence_active ==
						1) {
						if (g_pilot_data
							    .mission_directory_id ==
						    MISSION_DIRECTORY_MELEES) {
							action_triggered = frontend_dialog_show_confirm_dialog(
								frontend_string_get(
									FRONTSTR_678_YOU_ARE_CURRENTLY_PLAYING_A_TOURNAMENT),
								frontend_string_get(
									FRONTSTR_679_ARE_YOU_SURE_YOU_WANT_TO),
								frontend_string_get(
									FRONTSTR_680_TERMINATE_THIS_TOURNAMENT),
								frontend_string_get(
									FRONTSTR_523_OKAY),
								frontend_string_get(
									FRONTSTR_019_CANCEL));
							if (xvt_dialog_is_active()) {
								return 0;
							}
						} else if (
							g_pilot_data
								.mission_directory_id ==
							MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
							action_triggered = frontend_dialog_show_confirm_dialog(
								frontend_string_get(
									FRONTSTR_681_YOU_ARE_CURRENTLY_PLAYING_A_BATTLE),
								frontend_string_get(
									FRONTSTR_682_ARE_YOU_SURE_YOU_WANT_TO),
								frontend_string_get(
									FRONTSTR_683_TERMINATE_THIS_BATTLE),
								frontend_string_get(
									FRONTSTR_523_OKAY),
								frontend_string_get(
									FRONTSTR_019_CANCEL));
							if (xvt_dialog_is_active()) {
								return 0;
							}
						} else {
							action_triggered = frontend_dialog_show_confirm_dialog(
								frontend_string_get(
									FRONTSTR_779_YOU_ARE_CURRENTLY_PLAYING_A_CAMPAIGN),
								frontend_string_get(
									FRONTSTR_780_ARE_YOU_SURE_YOU_WANT_TO),
								frontend_string_get(
									FRONTSTR_781_TERMINATE_THIS_CAMPAIGN),
								frontend_string_get(
									FRONTSTR_523_OKAY),
								frontend_string_get(
									FRONTSTR_019_CANCEL));
							if (xvt_dialog_is_active()) {
								return 0;
							}
						}
					}
				}
				if (action_triggered != 0) {
					if (g_pilot_data.name[0] != '\0') {
						XVT_LOG_INFO(
							"frontend.mode_chosen choice=\"join\" context=%d previous_mode=%d",
							screen_context,
							(int)g_frontend_mission_session_mode);
						g_mission_setup_is_host = 0;
						g_mission_setup_roster_authoritative =
							0;
						if (screen_context !=
						    SCREEN_CONTEXT_MISSION) {
							net_shutdown_direct_play_session();
							g_frontend_mission_session_mode =
								FRONTEND_MISSION_SESSION_NET_CLIENT;
							frontend_screen_set_callbacks(
								frontend_net_join_game_screen,
								frontend_mission_list_free_screen_resources);
						} else {
							transition_needs_session_shutdown =
								1;
							g_frontend_mission_session_mode =
								FRONTEND_MISSION_SESSION_NET_CLIENT;
							frontend_screen_set_callbacks(
								frontend_net_join_game_screen,
								frontend_mission_list_free_screen_resources);
						}
					} else {
						XVT_LOG_DEBUG(
							"frontend.pilot_required choice=\"join\" context=%d",
							screen_context);
						frontend_dialog_show_confirm_dialog(
							frontend_string_get(
								FRONTSTR_524_YOU_MUST_SELECT_A_PILOT_FROM),
							frontend_string_get(
								FRONTSTR_525_THE_PILOT_ROSTER_OR_CREATE),
							frontend_string_get(
								FRONTSTR_526_A_NEW_ONE_BEFORE_CONTINUING),
							NULL, NULL);
						if (xvt_dialog_is_active()) {
							return 0;
						}
					}
				}
			}
		}

		if (g_host_cd_available != 0) {
			frontend_draw_rect_assign(&rect, 257, 4, 389, 42);
			frontend_button_set_overlay_text(
				frontend_string_get(FRONTSTR_004_HOST_GAME));
			switch (g_game_config.network_type) {
			case NET_TRANSPORT_IPX:
				if (g_game_config.internet_play != 0) {
					tooltip_text = frontend_string_get(
						FRONTSTR_721_HOST_INTERNET_IPX_GAME);
				} else {
					tooltip_text = frontend_string_get(
						FRONTSTR_722_HOST_LOCAL_IPX_GAME);
				}
				break;
			case NET_TRANSPORT_TCPIP:
				if (g_game_config.internet_play != 0) {
					tooltip_text = frontend_string_get(
						FRONTSTR_723_HOST_INTERNET_TCP_IP_GAME);
				} else {
					tooltip_text = frontend_string_get(
						FRONTSTR_724_HOST_LOCAL_TCP_IP_GAME);
				}
				break;
			case NET_TRANSPORT_MODEM:
				tooltip_text = frontend_string_get(
					FRONTSTR_725_HOST_DIRECT_MODEM_GAME);
				break;
			case NET_TRANSPORT_SERIAL:
				tooltip_text = frontend_string_get(
					FRONTSTR_726_HOST_DIRECT_SERIAL_GAME);
				break;
			default:
				tooltip_text = frontend_string_get(
					FRONTSTR_004_HOST_GAME);
				break;
			}
			strcpy(g_frontend_scratch_buffer, tooltip_text);
			if (g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_NET_HOST) {
				frontend_button_use_pressed_overlay_style();
				frontend_button_draw_sprite_and_tooltip(
					&rect, "creategamedown",
					g_frontend_scratch_buffer,
					BUTTON_FONT_SIZE, 0);
			} else {
				action_triggered =
					frontend_button_handle_sprite_button(
						&rect, NULL, "creategamedown",
						g_frontend_scratch_buffer,
						BUTTON_FONT_SIZE, 0,
						HOST_HELD_SLOT, "buttonsound");
				action_triggered = xvt_frontend_action_trigger(
					XVT_ACTION_OWNER_COMMON, 3,
					action_triggered);
				if (action_triggered != 0) {
					if (g_frontend_game_session_in_progress !=
					    0) {
						if (g_frontend_mission_session_mode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
							if (net_is_host() !=
							    0) {
								action_triggered = frontend_dialog_show_confirm_dialog(
									frontend_string_get(
										FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
									frontend_string_get(
										FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
									frontend_string_get(
										FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
									frontend_string_get(
										FRONTSTR_523_OKAY),
									frontend_string_get(
										FRONTSTR_019_CANCEL));
								if (xvt_dialog_is_active()) {
									return 0;
								}
							} else {
								action_triggered = frontend_dialog_show_confirm_dialog(
									frontend_string_get(
										FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
									frontend_string_get(
										FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
									frontend_string_get(
										FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
									frontend_string_get(
										FRONTSTR_523_OKAY),
									frontend_string_get(
										FRONTSTR_019_CANCEL));
								if (xvt_dialog_is_active()) {
									return 0;
								}
							}
						} else if (
							g_pilot_data
								.mission_sequence_active ==
							1) {
							if (g_pilot_data
								    .mission_directory_id ==
							    MISSION_DIRECTORY_MELEES) {
								action_triggered = frontend_dialog_show_confirm_dialog(
									frontend_string_get(
										FRONTSTR_678_YOU_ARE_CURRENTLY_PLAYING_A_TOURNAMENT),
									frontend_string_get(
										FRONTSTR_679_ARE_YOU_SURE_YOU_WANT_TO),
									frontend_string_get(
										FRONTSTR_680_TERMINATE_THIS_TOURNAMENT),
									frontend_string_get(
										FRONTSTR_523_OKAY),
									frontend_string_get(
										FRONTSTR_019_CANCEL));
								if (xvt_dialog_is_active()) {
									return 0;
								}
							} else if (
								g_pilot_data
									.mission_directory_id ==
								MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
								action_triggered = frontend_dialog_show_confirm_dialog(
									frontend_string_get(
										FRONTSTR_681_YOU_ARE_CURRENTLY_PLAYING_A_BATTLE),
									frontend_string_get(
										FRONTSTR_682_ARE_YOU_SURE_YOU_WANT_TO),
									frontend_string_get(
										FRONTSTR_683_TERMINATE_THIS_BATTLE),
									frontend_string_get(
										FRONTSTR_523_OKAY),
									frontend_string_get(
										FRONTSTR_019_CANCEL));
								if (xvt_dialog_is_active()) {
									return 0;
								}
							} else {
								action_triggered = frontend_dialog_show_confirm_dialog(
									frontend_string_get(
										FRONTSTR_779_YOU_ARE_CURRENTLY_PLAYING_A_CAMPAIGN),
									frontend_string_get(
										FRONTSTR_780_ARE_YOU_SURE_YOU_WANT_TO),
									frontend_string_get(
										FRONTSTR_781_TERMINATE_THIS_CAMPAIGN),
									frontend_string_get(
										FRONTSTR_523_OKAY),
									frontend_string_get(
										FRONTSTR_019_CANCEL));
								if (xvt_dialog_is_active()) {
									return 0;
								}
							}
						}
					}
					if (action_triggered != 0) {
						if (g_pilot_data.name[0] !=
						    '\0') {
							XVT_LOG_INFO(
								"frontend.mode_chosen choice=\"host\" context=%d previous_mode=%d",
								screen_context,
								(int)g_frontend_mission_session_mode);
							if (screen_context !=
							    SCREEN_CONTEXT_MISSION) {
								net_shutdown_direct_play_session();
								g_frontend_mission_session_mode =
									FRONTEND_MISSION_SESSION_NET_HOST;
								frontend_screen_set_callbacks(
									frontend_net_host_game_screen,
									frontend_net_host_game_exit);
							} else {
								transition_needs_session_shutdown =
									1;
								g_frontend_mission_session_mode =
									FRONTEND_MISSION_SESSION_NET_HOST;
								frontend_screen_set_callbacks(
									frontend_net_host_game_screen,
									frontend_net_host_game_exit);
							}
						} else {
							XVT_LOG_DEBUG(
								"frontend.pilot_required choice=\"host\" context=%d",
								screen_context);
							frontend_dialog_show_confirm_dialog(
								frontend_string_get(
									FRONTSTR_524_YOU_MUST_SELECT_A_PILOT_FROM),
								frontend_string_get(
									FRONTSTR_525_THE_PILOT_ROSTER_OR_CREATE),
								frontend_string_get(
									FRONTSTR_526_A_NEW_ONE_BEFORE_CONTINUING),
								NULL, NULL);
							if (xvt_dialog_is_active()) {
								return 0;
							}
						}
					}
				}
			}

			frontend_draw_rect_assign(&rect, 153, 4, 252, 43);
			frontend_button_set_overlay_text(
				frontend_string_get(FRONTSTR_002_FLY_SOLO));
			if (g_frontend_mission_session_mode ==
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				frontend_button_use_pressed_overlay_style();
				frontend_button_draw_sprite_and_tooltip(
					&rect, "flysolodown",
					frontend_string_get(
						FRONTSTR_002_FLY_SOLO),
					BUTTON_FONT_SIZE, 0);
			} else {
				action_triggered =
					frontend_button_handle_sprite_button(
						&rect, "NULL", "flysolodown",
						frontend_string_get(
							FRONTSTR_002_FLY_SOLO),
						BUTTON_FONT_SIZE, 0,
						SOLO_HELD_SLOT, "buttonsound");
				action_triggered = xvt_frontend_action_trigger(
					XVT_ACTION_OWNER_COMMON, 4,
					action_triggered);
				if (action_triggered != 0) {
					if (g_frontend_game_session_in_progress !=
						    0 &&
					    g_frontend_mission_session_mode !=
						    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (net_is_host() != 0) {
							action_triggered = frontend_dialog_show_confirm_dialog(
								frontend_string_get(
									FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
								frontend_string_get(
									FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
								frontend_string_get(
									FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								frontend_string_get(
									FRONTSTR_523_OKAY),
								frontend_string_get(
									FRONTSTR_019_CANCEL));
							if (xvt_dialog_is_active()) {
								return 0;
							}
						} else {
							action_triggered = frontend_dialog_show_confirm_dialog(
								frontend_string_get(
									FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
								frontend_string_get(
									FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								frontend_string_get(
									FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
								frontend_string_get(
									FRONTSTR_523_OKAY),
								frontend_string_get(
									FRONTSTR_019_CANCEL));
							if (xvt_dialog_is_active()) {
								return 0;
							}
						}
					}
					if (action_triggered != 0) {
						if (g_pilot_data.name[0] !=
						    '\0') {
							XVT_LOG_INFO(
								"frontend.mode_chosen choice=\"solo\" context=%d previous_mode=%d",
								screen_context,
								(int)g_frontend_mission_session_mode);
							g_mission_setup_is_host =
								0;
							g_mission_setup_roster_authoritative =
								0;
							if (screen_context !=
							    SCREEN_CONTEXT_MISSION) {
								net_shutdown_direct_play_session();
								memset(g_mp_roster,
								       0,
								       sizeof(g_mp_roster));
								g_frontend_mission_session_mode =
									FRONTEND_MISSION_SESSION_SINGLEPLAYER;
								frontend_screen_set_callbacks(
									mission_setup_update,
									mission_setup_exit);
							} else {
								transition_needs_session_shutdown =
									1;
								g_frontend_mission_session_mode =
									FRONTEND_MISSION_SESSION_SINGLEPLAYER;
								frontend_screen_set_callbacks(
									mission_setup_update,
									mission_setup_exit);
							}
						} else {
							XVT_LOG_DEBUG(
								"frontend.pilot_required choice=\"solo\" context=%d",
								screen_context);
							frontend_dialog_show_confirm_dialog(
								frontend_string_get(
									FRONTSTR_524_YOU_MUST_SELECT_A_PILOT_FROM),
								frontend_string_get(
									FRONTSTR_525_THE_PILOT_ROSTER_OR_CREATE),
								frontend_string_get(
									FRONTSTR_526_A_NEW_ONE_BEFORE_CONTINUING),
								NULL, NULL);
							if (xvt_dialog_is_active()) {
								return 0;
							}
						}
					}
				}
			}
		}
	}

	frontend_draw_rect_assign(&rect, 63, 4, 147, 57);
	frontend_button_set_overlay_text(
		frontend_string_get(FRONTSTR_566_CRAFT));
	if (screen_context == SCREEN_CONTEXT_TECH_LIBRARY) {
		frontend_button_use_pressed_overlay_style();
		frontend_button_draw_sprite_and_tooltip(
			&rect, "reviewcraftdown",
			frontend_string_get(FRONTSTR_571_EXIT_CRAFT_DATABASE),
			BUTTON_FONT_SIZE, 0);
		if (frontend_draw_point_in_rect(&rect, mouse_x, mouse_y) != 0 &&
		    (frontend_mouse_get_left_click() != 0 ||
		     frontend_mouse_get_right_click() != 0)) {
			XVT_LOG_DEBUG(
				"tech.closed by=\"top_button\" from_briefing=%d",
				g_mission_briefing_craft_selection_active);
			if (g_game_config.sfx_datapad_enabled != 0) {
				frontend_sound_play_ui_sound(
					"buttonsound", 1, 0, UI_SOUND_PRIORITY,
					12 * g_game_config.sfx_datapad_volume,
					UI_SOUND_PAN_CENTER);
			}
			g_active_text_field_id = 0;
			keyboard_flush_char_buffer();
			frontend_screen_pop_state();
			frontend_mouse_clear_input_gate();
			front_image_free_resource_by_name("backreview");
			if (g_mission_briefing_craft_selection_active == 0) {
				if (g_ship_list != NULL) {
					free(g_ship_list);
					g_ship_list = NULL;
				}
			} else {
				model_preview_restore_state();
			}
			if (g_tech_library_spec_text_table != NULL) {
				free(g_tech_library_spec_text_table);
				g_tech_library_spec_text_table = NULL;
			}
			frontend_text_stop_text_fade();
		}
	} else if (screen_context < SCREEN_CONTEXT_CONFIG &&
		   frontend_button_handle_sprite_button(
			   &rect, NULL, "reviewcraftdown",
			   frontend_string_get(FRONTSTR_001_CRAFT_DATABASE),
			   BUTTON_FONT_SIZE, 0, CRAFT_HELD_SLOT,
			   "buttonsound") != 0) {
		XVT_LOG_DEBUG(
			"frontend.screen_requested screen=\"craft_database\" context=%d",
			screen_context);
		frontend_draw_rect_assign(&screen_rect, 0, 0, 640, 480);
		frontend_screen_queue_push(tech_library_update, &screen_rect);
	}

	if (screen_context < SCREEN_CONTEXT_CONFIG) {
		frontend_draw_rect_assign(&rect, 9, 4, 62, 71);
		frontend_button_set_overlay_text(
			frontend_string_get(FRONTSTR_565_PILOTS));
		if (g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_NONE) {
			frontend_button_use_pressed_overlay_style();
			frontend_button_draw_sprite_and_tooltip(
				&rect, "pilotregdown",
				frontend_string_get(FRONTSTR_000_PILOT_RECORDS),
				BUTTON_FONT_SIZE, 0);
		} else {
			action_triggered = frontend_button_handle_sprite_button(
				&rect, NULL, "pilotregdown",
				frontend_string_get(FRONTSTR_000_PILOT_RECORDS),
				BUTTON_FONT_SIZE, 0, PILOT_HELD_SLOT,
				"buttonsound");
			action_triggered = xvt_frontend_action_trigger(
				XVT_ACTION_OWNER_COMMON, 5, action_triggered);
			if (action_triggered != 0) {
				if (g_frontend_game_session_in_progress != 0) {
					if (g_frontend_mission_session_mode !=
					    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
						if (net_is_host() != 0) {
							action_triggered = frontend_dialog_show_confirm_dialog(
								frontend_string_get(
									FRONTSTR_558_YOU_ARE_CURRENTLY_HOSTING_A_GAME_SESSION),
								frontend_string_get(
									FRONTSTR_559_IF_YOU_QUIT_THE_GAME_WILL_BE_ABORTED),
								frontend_string_get(
									FRONTSTR_560_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								frontend_string_get(
									FRONTSTR_523_OKAY),
								frontend_string_get(
									FRONTSTR_019_CANCEL));
							if (xvt_dialog_is_active()) {
								return 0;
							}
						} else {
							action_triggered = frontend_dialog_show_confirm_dialog(
								frontend_string_get(
									FRONTSTR_555_YOU_ARE_CURRENTLY_IN_A_GAME_SESSION),
								frontend_string_get(
									FRONTSTR_556_ARE_YOU_SURE_YOU_WANT_TO_QUIT),
								frontend_string_get(
									FRONTSTR_557_SPACE_TRANSLATION_PLACEHOLDER),
								frontend_string_get(
									FRONTSTR_523_OKAY),
								frontend_string_get(
									FRONTSTR_019_CANCEL));
							if (xvt_dialog_is_active()) {
								return 0;
							}
						}
					} else if (
						g_pilot_data
							.mission_sequence_active ==
						1) {
						if (g_pilot_data
							    .mission_directory_id ==
						    MISSION_DIRECTORY_MELEES) {
							action_triggered = frontend_dialog_show_confirm_dialog(
								frontend_string_get(
									FRONTSTR_678_YOU_ARE_CURRENTLY_PLAYING_A_TOURNAMENT),
								frontend_string_get(
									FRONTSTR_679_ARE_YOU_SURE_YOU_WANT_TO),
								frontend_string_get(
									FRONTSTR_680_TERMINATE_THIS_TOURNAMENT),
								frontend_string_get(
									FRONTSTR_523_OKAY),
								frontend_string_get(
									FRONTSTR_019_CANCEL));
							if (xvt_dialog_is_active()) {
								return 0;
							}
						} else if (
							g_pilot_data
								.mission_directory_id ==
							MISSION_DIRECTORY_COMBAT_ENGAGEMENTS) {
							action_triggered = frontend_dialog_show_confirm_dialog(
								frontend_string_get(
									FRONTSTR_681_YOU_ARE_CURRENTLY_PLAYING_A_BATTLE),
								frontend_string_get(
									FRONTSTR_682_ARE_YOU_SURE_YOU_WANT_TO),
								frontend_string_get(
									FRONTSTR_683_TERMINATE_THIS_BATTLE),
								frontend_string_get(
									FRONTSTR_523_OKAY),
								frontend_string_get(
									FRONTSTR_019_CANCEL));
							if (xvt_dialog_is_active()) {
								return 0;
							}
						} else {
							action_triggered = frontend_dialog_show_confirm_dialog(
								frontend_string_get(
									FRONTSTR_779_YOU_ARE_CURRENTLY_PLAYING_A_CAMPAIGN),
								frontend_string_get(
									FRONTSTR_780_ARE_YOU_SURE_YOU_WANT_TO),
								frontend_string_get(
									FRONTSTR_781_TERMINATE_THIS_CAMPAIGN),
								frontend_string_get(
									FRONTSTR_523_OKAY),
								frontend_string_get(
									FRONTSTR_019_CANCEL));
							if (xvt_dialog_is_active()) {
								return 0;
							}
						}
					}
				}
				if (action_triggered != 0) {
					XVT_LOG_INFO(
						"frontend.mode_chosen choice=\"pilots\" context=%d previous_mode=%d",
						screen_context,
						(int)g_frontend_mission_session_mode);
					g_mission_setup_is_host = 0;
					g_mission_setup_roster_authoritative =
						0;
					if (screen_context !=
					    SCREEN_CONTEXT_MISSION) {
						net_shutdown_direct_play_session();
						g_frontend_mission_session_mode =
							FRONTEND_MISSION_SESSION_NONE;
						frontend_screen_set_callbacks(
							concourse_update,
							concourse_exit);
					} else {
						g_frontend_mission_session_mode =
							FRONTEND_MISSION_SESSION_NONE;
						transition_needs_session_shutdown =
							1;
						frontend_screen_set_callbacks(
							concourse_update,
							concourse_exit);
					}
				}
			}
		}

		if (screen_context == SCREEN_CONTEXT_MISSION &&
		    transition_needs_session_shutdown != 0) {
			if (net_is_host() != 0) {
				g_frontend_net_packet_scratch.packet_type =
					NET_PACKET_HOST_CANCELLED;
				net_send_packet_and_flush(
					0, &g_frontend_net_packet_scratch,
					sizeof(int));
			} else {
				g_frontend_net_packet_scratch.packet_type =
					NET_PACKET_PLAYER_LEFT;
				net_send_packet_and_flush(
					net_get_host_player_id(),
					&g_frontend_net_packet_scratch,
					sizeof(int));
			}
			net_shutdown_direct_play_session();
			memset(g_mp_roster, 0, sizeof(g_mp_roster));
		}
	}

	frontend_button_disable_overlay_text();
	xvt_frontend_action_finish(XVT_ACTION_OWNER_COMMON);
	return 0;
}

/* Writes seconds as "MM:SS", or as "HH:MM:SS" from one hour up, each part at
 * least two digits, into g_frontend_scratch_buffer, and returns sprintf's
 * result. */
// FUNCTION: XVT 0x4C9BF0
int frontend_format_seconds_to_clock_string(unsigned int seconds)
{
	unsigned int seconds_remainder = seconds % 60u;
	unsigned int minutes = seconds / 60u % 60u;
	unsigned int hours = seconds / 3600u;
	if (hours == 0) {
		return sprintf(g_frontend_scratch_buffer, "%02d:%02d", minutes,
			       seconds_remainder);
	}
	return sprintf(g_frontend_scratch_buffer, "%02d:%02d:%02d", hours,
		       minutes, seconds_remainder);
}

/* Copies line line_index (0 for the first) of xvterr.txt into out_text, turning
 * each two-character \n into a newline, and returns 1. Returns 0 when the file
 * does not open or that line is missing. A negative line_index returns 0 in the
 * modern build; the original build then reads a pointer from the unset line
 * buffer. It always copies the first 255 bytes of the line buffer, newline and
 * whatever follows the line included, adding no terminator of its own. The
 * original build reads each line with a 512-byte limit into a 256-byte
 * buffer. */
// FUNCTION: XVT 0x4C9E30
int error_text_load_line(int line_index, char *out_text)
{
	char buffer[512];

	xvt_file *stream;
	stream = file_open("xvterr.txt", "r");
	if (stream == 0) {
		return 0;
	}
	char *line;
	if (line_index >= 0) {
		int lines_remaining = line_index + 1;
		do {
			line = FILE_GETS(buffer, 512, stream);
			--lines_remaining;
		} while (lines_remaining != 0);
	} else {
		line = 0;
	}
	file_close(stream);
	if (line == 0) {
		return 0;
	}
	for (int i = 0; i < 255; ++i) {
		char value = buffer[i];
		if (value == '\\' && buffer[i + 1] == 'n') {
			*out_text++ = '\n';
			++i;
		} else {
			*out_text++ = value;
		}
	}
	return 1;
}

/* Sets g_host_cd_available to whether the host CD's first training mission is
 * there, and returns it. The modern build looks for train/1ta01bf.tie among the
 * assets. The original build opens the drive letter followed by
 * \train\1TA01BF.TIE, with no colon after the letter; with no CD drive it
 * returns 0 and leaves g_host_cd_available as it was. */
// FUNCTION: XVT 0x4C9EE0
int frontend_check_host_cd_present(void)
{
	char path[XVT_PATH_CAPACITY];
	g_host_cd_available =
		xvt_storage_resolve_asset("train/1ta01bf.tie", path,
					  sizeof(path)) == 1;
	XVT_LOG_DEBUG("frontend.host_cd_checked found=%d", g_host_cd_available);
	return g_host_cd_available;
}

/* Returns 1 when control_id is entry 0 of g_scrollable_control_ids, the focused
 * control, else 0. Does not check that any control is registered. */
// FUNCTION: XVT 0x4D9B80
int frontend_is_scrollable_control_focused(int control_id)
{
	control_id -= g_scrollable_control_ids[0];
	return !control_id;
}

/* Adds control_id at the end of g_scrollable_control_ids unless it is there
 * already. Returns 1, or 0 when 32 are registered. */
// FUNCTION: XVT 0x4D9BA0
int frontend_register_scrollable_control(int control_id)
{
	int count = g_scrollable_control_count;
	if ((unsigned int)count >= 32u) {
		XVT_LOG_DEBUG("frontend.scroll_table_full control=%d count=%d",
			      control_id, count);
		return 0;
	}

	unsigned int index = 0;
	while (index < (unsigned int)count) {
		if (g_scrollable_control_ids[index] == control_id) {
			return 1;
		}
		++index;
	}

	g_scrollable_control_ids[count] = control_id;
	g_scrollable_control_count = count + 1;
	return 1;
}

/* Removes the first entry equal to control_id from g_scrollable_control_ids,
 * moving later entries down one, and returns 1; returns 0 when it is not there.
 * The original build moves them with memcpy over overlapping memory; the modern
 * build uses memmove. */
// FUNCTION: XVT 0x4D9BF0
int frontend_unregister_scrollable_control(int control_id)
{
	unsigned int index = 0;
	if (index != (unsigned int)g_scrollable_control_count) {
		int count = g_scrollable_control_count;
		do {
			if (g_scrollable_control_ids[index] == control_id) {
				memmove(&g_scrollable_control_ids[index],
					&g_scrollable_control_ids[index + 1],
					(size_t)(count - index - 1) *
						sizeof(g_scrollable_control_ids
							       [0]));
				--g_scrollable_control_count;
				return 1;
			}
			++index;
		} while (index < (unsigned int)count);
	}
	return 0;
}

/* Moves keyboard focus to the next scrollbar: entry 0 of g_scrollable_control_ids
 * goes to the end and the rest move down one. Returns 0 when none is
 * registered, else 1. The original build moves them with memcpy over
 * overlapping memory. */
// FUNCTION: XVT 0x4D9C50
int frontend_cycle_scrollable_focus(void)
{
	if (g_scrollable_control_count == 0) {
		return 0;
	}

	int first_control_id = g_scrollable_control_ids[0];
	/* The source and destination overlap, so modern builds require memmove. */
	memmove(g_scrollable_control_ids, &g_scrollable_control_ids[1],
		(size_t)(g_scrollable_control_count - 1) *
			sizeof(g_scrollable_control_ids[0]));
	g_scrollable_control_ids[g_scrollable_control_count - 1] =
		first_control_id;
	return 1;
}

/* Forgets every registered scrollbar: sets g_scrollable_control_count to 0.
 * Returns 1. */
// FUNCTION: XVT 0x4D9CA0
int frontend_reset_scrollable_controls(void)
{
	g_scrollable_control_count = 0;
	return 1;
}
