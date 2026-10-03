#include "xvt/frontend/concourse.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/frontend_actions.h"
#include "xvt_runtime/runtime/frontend_movies.h"
#include "xvt_runtime/runtime/network_task.h"
#endif
#include "xvt/assets/file.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_file_list.h"
#include "xvt/frontend/frontend_joystick.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	CONCOURSE_CHAT_LOG_BUFFER_SIZE = 0x400,
	CONCOURSE_CD_VOLUME_MAX = 0xFFFF,
	CONCOURSE_CD_VOLUME_DIVISOR = 9,
	CONCOURSE_MUSIC_TRACK = 7,
	CONCOURSE_CD_MOVIE_CHECK_LIMIT = 5,
	CONCOURSE_CURSOR_START_X = 32,
	CONCOURSE_CURSOR_START_Y = 127,
	CONCOURSE_TEXT_FADE_FRAMES = 20,
	CONCOURSE_ANIMATION_CYCLE_FRAMES = 32,
	CONCOURSE_VERSION_MAJOR = 2,
	CONCOURSE_VERSION_MINOR = 0,
};

/* Heap array of the saved pilots' names, one 14-byte entry per file in
 * g_pilot_file_list, each read from the first 12 bytes of its file by
 * pilot_record_rebuild_pilot_list. Freed and set NULL by concourse_exit and before
 * each rebuild. */
// GLOBAL: XVT 0xB69CC0
char (*g_pilot_list_display_names)[14] = NULL;
/* The tournaments directory's mission list for the pilot record pages:
 * pilot_record_draw_mission_achievements_page takes it over from
 * mission_setup_load_mission_list. Freed and set NULL by concourse_exit and
 * before each reload. */
// GLOBAL: XVT 0xB69CD4
struct mission_list_entry *g_pilot_record_tournament_mission_list = NULL;
/* Entries in g_pilot_record_melee_mission_list, set when it loads; freeing the
 * list leaves the count as it was. */
// GLOBAL: XVT 0xB69E20
int g_pilot_record_melee_mission_count = 0;
/* The saved pilot files, *.plt in the base game folder, sorted by name;
 * pilot_record_rebuild_pilot_list builds it. Freed and set NULL by
 * concourse_exit and before each rebuild. */
// GLOBAL: XVT 0xB69E28
struct frontend_file_list *g_pilot_file_list = NULL;
/* The combat engagements directory's single-player mission list for the
 * pilot record pages, loaded like g_pilot_record_tournament_mission_list. */
// GLOBAL: XVT 0xB69E2C
struct mission_list_entry *g_pilot_record_singleplayer_combat_mission_list =
	NULL;
/* The campaigns directory's multiplayer mission list for the pilot record
 * pages: loaded like g_pilot_record_tournament_mission_list, but with
 * g_frontend_mission_session_mode at the network host mode during the load. */
// GLOBAL: XVT 0xB6A240
struct mission_list_entry *g_pilot_record_multiplayer_campaign_mission_list =
	NULL;
/* The campaigns directory's single-player mission list for the pilot record
 * pages, loaded like g_pilot_record_tournament_mission_list, and also by
 * pilot_record_draw_cutscene_viewer_page and pilot_record_draw_campaign_medals_page. */
// GLOBAL: XVT 0xB6A258
struct mission_list_entry *g_pilot_record_singleplayer_campaign_mission_list =
	NULL;
/* Entries in g_pilot_record_singleplayer_campaign_mission_list, set when it
 * loads; freeing the list leaves the count as it was. */
// GLOBAL: XVT 0xB6A25C
int g_pilot_record_singleplayer_campaign_mission_count = 0;
/* Entries in g_pilot_record_singleplayer_combat_mission_list, set when it loads;
 * freeing the list leaves the count as it was. */
// GLOBAL: XVT 0xB6A254
int g_pilot_record_singleplayer_combat_mission_count = 0;
/* The combat engagements directory's multiplayer mission list for the pilot
 * record pages, loaded like g_pilot_record_multiplayer_campaign_mission_list. */
// GLOBAL: XVT 0xB6A2B8
struct mission_list_entry *g_pilot_record_multiplayer_combat_mission_list =
	NULL;
/* The training exercises directory's multiplayer mission list for the pilot
 * record pages, loaded like g_pilot_record_multiplayer_campaign_mission_list and
 * also by pilot_record_draw_campaign_medals_page. */
// GLOBAL: XVT 0xB6A2CC
struct mission_list_entry *g_pilot_record_multiplayer_training_mission_list =
	NULL;
/* The melees directory's mission list for the pilot record pages, loaded
 * like g_pilot_record_tournament_mission_list. */
// GLOBAL: XVT 0xB6A2D0
struct mission_list_entry *g_pilot_record_melee_mission_list = NULL;
/* The training exercises directory's single-player mission list for the
 * pilot record pages, loaded like g_pilot_record_tournament_mission_list and also
 * by pilot_record_draw_campaign_medals_page. */
// GLOBAL: XVT 0xB6A2D4
struct mission_list_entry *g_pilot_record_singleplayer_training_mission_list =
	NULL;
/* Entries in g_pilot_record_singleplayer_training_mission_list, set when it
 * loads; freeing the list leaves the count as it was. */
// GLOBAL: XVT 0xB69D18
int g_pilot_record_singleplayer_training_mission_count = 0;
/* Entries in g_pilot_record_multiplayer_combat_mission_list, set when it loads;
 * freeing the list leaves the count as it was. */
// GLOBAL: XVT 0xB6A260
int g_pilot_record_multiplayer_combat_mission_count = 0;
/* Entries in g_pilot_record_multiplayer_campaign_mission_list, set when it loads;
 * freeing the list leaves the count as it was. */
// GLOBAL: XVT 0xB6A268
int g_pilot_record_multiplayer_campaign_mission_count = 0;
/* Entries in g_pilot_record_tournament_mission_list, set when it loads; freeing
 * the list leaves the count as it was. */
// GLOBAL: XVT 0xB6A2BC
int g_pilot_record_tournament_mission_count = 0;
/* Entries in g_pilot_record_multiplayer_training_mission_list, set when it
 * loads; freeing the list leaves the count as it was. */
// GLOBAL: XVT 0xBB2814
int g_pilot_record_multiplayer_training_mission_count = 0;

/* Exit function of the concourse: frees g_pilot_file_list,
 * g_pilot_list_display_names, the eight pilot record mission lists and
 * g_battle_mission_list, setting each NULL but leaving the counts, frees the
 * "background0" and "background1" images and forgets the scrollable
 * controls. Returns 0; ignores frame_counter. */
// FUNCTION: XVT 0x4BE050
int concourse_exit(int frame_counter)
{
	(void)frame_counter;

	if (g_pilot_file_list != NULL) {
		frontend_file_list_free(g_pilot_file_list);
		g_pilot_file_list = NULL;
	}
	if (g_pilot_list_display_names != NULL) {
		free(g_pilot_list_display_names);
		g_pilot_list_display_names = NULL;
	}
	if (g_pilot_record_singleplayer_training_mission_list != NULL) {
		free(g_pilot_record_singleplayer_training_mission_list);
		g_pilot_record_singleplayer_training_mission_list = NULL;
	}
	if (g_pilot_record_multiplayer_training_mission_list != NULL) {
		free(g_pilot_record_multiplayer_training_mission_list);
		g_pilot_record_multiplayer_training_mission_list = NULL;
	}
	if (g_pilot_record_melee_mission_list != NULL) {
		free(g_pilot_record_melee_mission_list);
		g_pilot_record_melee_mission_list = NULL;
	}
	if (g_pilot_record_tournament_mission_list != NULL) {
		free(g_pilot_record_tournament_mission_list);
		g_pilot_record_tournament_mission_list = NULL;
	}
	if (g_pilot_record_singleplayer_combat_mission_list != NULL) {
		free(g_pilot_record_singleplayer_combat_mission_list);
		g_pilot_record_singleplayer_combat_mission_list = NULL;
	}
	if (g_pilot_record_multiplayer_combat_mission_list != NULL) {
		free(g_pilot_record_multiplayer_combat_mission_list);
		g_pilot_record_multiplayer_combat_mission_list = NULL;
	}
	if (g_battle_mission_list != NULL) {
		free(g_battle_mission_list);
		g_battle_mission_list = NULL;
	}
	if (g_pilot_record_singleplayer_campaign_mission_list != NULL) {
		free(g_pilot_record_singleplayer_campaign_mission_list);
		g_pilot_record_singleplayer_campaign_mission_list = NULL;
	}
	if (g_pilot_record_multiplayer_campaign_mission_list != NULL) {
		free(g_pilot_record_multiplayer_campaign_mission_list);
		g_pilot_record_multiplayer_campaign_mission_list = NULL;
	}
	front_image_free_resource_by_name("background0");
	front_image_free_resource_by_name("background1");
	frontend_reset_scrollable_controls();
	return 0;
}

/* Update function of the concourse: the main screen with the pilot list and the
 * pilot record pages. The modern build first waits out a movie viewer and
 * finishes a pending common action. On frame 0 (in the modern build, only
 * without a pending pilot action), first, unless the modern build has a
 * concourse action pending: the original build ends the game, returning 1,
 * after a message box when no joystick is found, and asks for the game CD until
 * it is found, returning 1 on Cancel; the modern build stops with a fatal error
 * when the flight data is missing. It loads the image and sound lists and
 * checks for the host CD. The original build, unless movie checks are off or
 * the game was started to host or join, checks the movie CD when
 * faction_statistics[0].cd_movie_check_counter is 0, asking for it until it is
 * there and returning 1 on Cancel, then sets the counter to 1; otherwise it
 * counts the counter up, back to 0 at 5. Then it sets g_skip_movie_checks to 1.
 * After that it shows any pending CD music warning, puts the cursor at (32,
 * 127), empties the chat log, sets g_pilot_record_page to 0 and
 * g_frontend_mission_session_mode to none, marks the pilot record pages for
 * rebuilding, and clears other session flags; with the intro skipped it also
 * copies the current faction's mission choice into the pilot. Started with
 * "ishost", it returns 1 after an error box without the host CD, else opens an
 * internet game as host and goes to mission setup, or stays here when that
 * fails (the modern build hands this to xvt_network_task_begin); started with
 * "isclient", it goes to the join screen. Otherwise it loads the two
 * backgrounds and starts a 20-frame text fade. Every frame it runs the pilot
 * selection panel and draws "v. 2.0", the pilot's rating and name between two
 * animated rebel or imperial emblems, the pilot record page g_pilot_record_page
 * picks and the navigation controls. Returns 1 when
 * frontend_handle_common_screen_controls(0) returns 1, else 0. */
// FUNCTION: XVT 0x4BE1B0
int concourse_update(int frame_counter)
{
	struct RECT rect;
#ifndef XVT_MODERN
	char local_player_info[2];
	int local_player_id;
#endif

#ifdef XVT_MODERN
	if (xvt_frontend_movies_resume_viewer()) {
		return 0;
	}
	if (xvt_frontend_action_pending(XVT_ACTION_OWNER_COMMON)) {
		return frontend_handle_common_screen_controls(0) == 1;
	}
	if (frame_counter == 0 &&
	    !xvt_frontend_action_pending(XVT_ACTION_OWNER_PILOT)) {
#else
	if (frame_counter == 0) {
#endif
#ifdef XVT_MODERN
		if (!xvt_frontend_action_pending(XVT_ACTION_OWNER_CONCOURSE)) {
#endif
			keyboard_flush_char_buffer();
#ifndef XVT_MODERN
			if (joystick_get_count() == 0) {
				if (error_text_load_line(
					    2, g_frontend_scratch_buffer) ==
				    0) {
					frontend_display_show_game_message_box(
						"ERROR:  Joystick not detected!\n\nThe game will not "
						"work properly\nwithout a joystick "
						"attached.\n\nPress ENTER to exit.");
				} else {
					frontend_display_show_game_message_box(
						g_frontend_scratch_buffer);
				}
				return 1;
			}

			if (file_check_game_cd_present(g_skip_movie_checks) ==
			    0) {
				int keep_retrying;

				do {
					cd_audio_initialize();
					frontend_display_clear_back_buffer();
					if (front_image_resource_exists(
						    "dialogok") != 0) {
						keep_retrying = frontend_dialog_show_confirm_dialog(
							frontend_string_get(
								FRONTSTR_706_FAILED_TO_DETECT_RETAIL_XVT_CD),
							frontend_string_get(
								FRONTSTR_707_PLEASE_INSERT_THE_X_WING_VS_TIE_FIGHTER_CD),
							frontend_string_get(
								FRONTSTR_708_INTO_YOUR_CD_ROM_DRIVE),
							frontend_string_get(
								FRONTSTR_523_OKAY),
							frontend_string_get(
								FRONTSTR_019_CANCEL));
					} else {
						keep_retrying = frontend_dialog_show_confirm_dialog(
							frontend_string_get(
								FRONTSTR_706_FAILED_TO_DETECT_RETAIL_XVT_CD),
							frontend_string_get(
								FRONTSTR_707_PLEASE_INSERT_THE_X_WING_VS_TIE_FIGHTER_CD),
							frontend_string_get(
								FRONTSTR_761_INTO_YOUR_CD_ROM_DRIVE_AND_PRESS_ENTER),
							frontend_string_get(
								FRONTSTR_523_OKAY),
							frontend_string_get(
								FRONTSTR_019_CANCEL));
					}
					if (keep_retrying == 0) {
						return 1;
					}
					file_detect_game_and_cd_paths(
						"\\wave\\PBC\\Pb1los07.wav");
					if (g_cd_audio_warning_pending != 0) {
						g_cd_audio_warning_pending =
							cd_audio_initialize() ==
							0;
					} else {
						cd_audio_initialize();
					}
					cd_audio_enable_loop_current_track();
					if (g_game_config
						    .datapad_music_enabled !=
					    0) {
						cd_audio_set_aux_volume(
							CONCOURSE_CD_VOLUME_MAX *
							g_game_config
								.music_volume /
							CONCOURSE_CD_VOLUME_DIVISOR);
						cd_audio_play_track_from_time(
							CONCOURSE_MUSIC_TRACK,
							0, 0);
						cd_audio_suspend_playback();
						cd_audio_request_resume_playback();
					} else {
						cd_audio_stop_current_track();
					}
				} while (file_check_game_cd_present(
						 g_skip_movie_checks) == 0);
			}

#else
		if (!file_check_game_cd_present(g_skip_movie_checks)) {
			xvt_storage_fatal(
				"Required flight/voice assets are missing; select a complete installation with --setup",
				1);
			return 1;
		}
#endif
			front_image_load_resource_list("frontres\\top.lst");
			front_image_load_resource_list("frontres\\side.lst");
			front_image_load_resource_list("frontres\\awards.lst");
			front_image_load_resource_list("frontres\\promo.lst");
			front_image_load_resource_list("frontres\\icons.lst");
			frontend_sound_load_list("sfx\\sfx.lst");
			frontend_display_clear_back_buffer();
			frontend_check_host_cd_present();
#ifndef XVT_MODERN
			if (g_skip_movie_checks == 0 && g_opt_is_host == 0 &&
			    g_opt_is_client == 0) {
				if (g_pilot_data.faction_statistics[0]
					    .cd_movie_check_counter == 0) {
					if (file_check_required_cd_movie_assets_present() ==
					    0) {
						do {
							cd_audio_stop_current_track();
							frontend_display_clear_back_buffer();
							front_image_resource_exists(
								"dialogok");
							if (frontend_dialog_show_confirm_dialog(
								    frontend_string_get(
									    FRONTSTR_827_PLEASE_INSERT_BALANCE_OF_POWER_CD),
								    frontend_string_get(
									    FRONTSTR_828_INTO_YOUR_CD_ROM_DRIVE),
								    frontend_string_get(
									    FRONTSTR_829_EMPTY_TRANSLATION_PLACEHOLDER),
								    frontend_string_get(
									    FRONTSTR_523_OKAY),
								    frontend_string_get(
									    FRONTSTR_019_CANCEL)) ==
							    0) {
								return 1;
							}
							cd_audio_initialize();
							cd_audio_enable_loop_current_track();
							if (g_game_config
								    .datapad_music_enabled !=
							    0) {
								cd_audio_set_aux_volume(
									CONCOURSE_CD_VOLUME_MAX *
									g_game_config
										.music_volume /
									CONCOURSE_CD_VOLUME_DIVISOR);
								cd_audio_play_track_from_time(
									CONCOURSE_MUSIC_TRACK,
									0, 0);
								cd_audio_suspend_playback();
								cd_audio_request_resume_playback();
							} else {
								cd_audio_stop_current_track();
							}
						} while (
							file_check_required_cd_movie_assets_present() ==
							0);
					}
					frontend_display_clear_back_buffer();
					g_pilot_data.faction_statistics[0]
						.cd_movie_check_counter = 1;
				} else {
					++g_pilot_data.faction_statistics[0]
						  .cd_movie_check_counter;
					if (g_pilot_data.faction_statistics[0]
						    .cd_movie_check_counter >=
					    CONCOURSE_CD_MOVIE_CHECK_LIMIT) {
						g_pilot_data
							.faction_statistics[0]
							.cd_movie_check_counter =
							0;
					}
				}
			}
#endif
			g_skip_movie_checks = 1;
#ifdef XVT_MODERN
		}
#endif
		if (g_cd_audio_warning_pending != 0) {
#ifdef XVT_MODERN
			frontend_dialog_show_confirm_dialog(
				"Music files could not be opened.",
				"Check BalanceOfPower/MUSIC/TrackNN.ogg in the selected installation.",
				NULL, frontend_string_get(FRONTSTR_523_OKAY),
				NULL);
			if (xvt_dialog_is_active()) {
				xvt_frontend_action_trigger(
					XVT_ACTION_OWNER_CONCOURSE, 1, 1);
				return 0;
			}
			xvt_frontend_action_finish(XVT_ACTION_OWNER_CONCOURSE);
#else
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_765_CD_MUSIC_NOT_AVAILABLE),
				frontend_string_get(
					FRONTSTR_766_MAKE_SURE_OTHER_CD_AUDIO_PLAYING_APPLICATIONS),
				frontend_string_get(
					FRONTSTR_767_LIKE_FLEXICD_ARE_NOT_ALREADY_RUNNING),
				frontend_string_get(FRONTSTR_523_OKAY), NULL);
#endif
			g_cd_audio_warning_pending = 0;
		}
		frontend_cursor_set_pos(CONCOURSE_CURSOR_START_X,
					CONCOURSE_CURSOR_START_Y);
		if (g_frontend_chat_log_buffer != NULL) {
			memset(g_frontend_chat_log_buffer, 0,
			       CONCOURSE_CHAT_LOG_BUFFER_SIZE);
			g_frontend_chat_log_used_bytes = 0;
		}
		g_config_connection_type_editable = 1;
		g_frontend_skip_screen_entry_setup = 0;
		g_frontend_quick_start_launch_flag = 0;
		g_frontend_game_session_in_progress = 0;
		if (g_opt_skip_intro != 0) {
			g_pilot_data.team =
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.team;
			g_pilot_data.mission_directory_id =
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.mission_directory_id;
			memcpy(g_pilot_data.mission_description_ids,
			       g_pilot_data
				       .faction_statistics
					       [g_pilot_data.current_faction_id]
				       .mission_description_ids,
			       sizeof(g_pilot_data.mission_description_ids));
			g_pilot_data.mission_sequence_active =
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.mission_sequence_active;
			g_pilot_data.saved_mission_description_id =
				g_pilot_data
					.faction_statistics
						[g_pilot_data
							 .current_faction_id]
					.saved_mission_description_id;
		}
		g_pilot_record_pages_need_rebuild = 1;
		g_frontend_mission_session_mode = FRONTEND_MISSION_SESSION_NONE;
		g_mission_setup_roster_authoritative = 0;
		g_pilot_record_page = 0;

		if (g_opt_is_host != 0) {
			if (g_host_cd_available == 0) {
				if (error_text_load_line(
					    3, g_frontend_scratch_buffer) ==
				    0) {
					frontend_display_show_game_message_box(
						"ERROR:  Not Host CD!\n\nYou cannot host an internet game with the Client CD.\nYou "
						"must insert the Host CD into your CD-ROM drive\nto host an internet game.\n\nPress "
						"ENTER "
						"to exit.");
				} else {
					frontend_display_show_game_message_box(
						g_frontend_scratch_buffer);
				}
				return 1;
			}
			g_opt_is_host = 0;
			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NET_HOST;
			memset(g_mp_roster, 0, sizeof(g_mp_roster));
			g_game_config.network_type = NET_TRANSPORT_TCPIP;
			strcpy(g_pilot_data.multiplayer_game_name,
			       "Internet game.");
			g_mission_setup_is_host = 1;
#ifndef XVT_MODERN
			switch ((network_transport_type)
					g_game_config.network_type) {
			case NET_TRANSPORT_IPX:
				g_frontend_scratch_buffer[0] = '\0';
				break;
			case NET_TRANSPORT_TCPIP:
				frontend_display_flip_direct_draw_to_gdi_surface();
				strcpy(g_frontend_scratch_buffer,
				       g_game_config.ip_address);
				break;
			case NET_TRANSPORT_MODEM:
				frontend_display_flip_direct_draw_to_gdi_surface();
				strcpy(g_frontend_scratch_buffer,
				       g_game_config.phone_number);
				break;
			case NET_TRANSPORT_SERIAL:
				break;
			}
			local_player_info[0] = (char)(g_pilot_data.rating + 1);
			local_player_info[1] = '\0';
			frontend_cursor_show_os_cursor();
#endif
#ifdef XVT_MODERN
			xvt_network_task_begin(XVT_NETWORK_AUTO_HOST);
			return 0;
#else
			if (net_start_network_session(
				    (int)g_frontend_net_xvt_direct_play_app_guid
					    [0],
				    (int)g_frontend_net_xvt_direct_play_app_guid
					    [1],
				    (int)g_frontend_net_xvt_direct_play_app_guid
					    [2],
				    (int)g_frontend_net_xvt_direct_play_app_guid
					    [3],
				    local_player_info, g_pilot_data.name,
				    g_mission_setup_is_host,
				    g_pilot_data.multiplayer_game_name,
				    (network_transport_type)
					    g_game_config.network_type,
				    0, 0, g_frontend_scratch_buffer,
				    NULL) == 0) {
				frontend_display_unlock_back_buffer();
				frontend_cursor_hide_os_cursor();
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
				g_frontend_mission_session_mode =
					FRONTEND_MISSION_SESSION_NONE;
				return 0;
			}
			frontend_display_unlock_back_buffer();
			frontend_cursor_hide_os_cursor();
			g_draw_surface_ptr =
				frontend_display_lock_back_buffer();
			local_player_id = net_get_local_player_id();
			net_set_player_ready(local_player_id);
			memset(g_mp_roster, 0, sizeof(g_mp_roster));
			strcpy(g_mp_roster[0].name, g_pilot_data.name);
			g_mp_roster[0].player_id = net_get_local_player_id();
			g_mp_roster[0].pilot_rating = g_pilot_data.rating;
			frontend_screen_set_callbacks(mission_setup_update,
						      mission_setup_exit);
			return 0;
#endif
		}
		if (g_opt_is_client != 0) {
			g_opt_is_client = 0;
			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NET_CLIENT;
			g_game_config.network_type = NET_TRANSPORT_TCPIP;
			frontend_screen_set_callbacks(
				frontend_net_join_game_screen,
				frontend_mission_list_free_screen_resources);
			return 0;
		}
		front_image_register_resource_default("frontres\\reg0.bmp",
						      "background0");
		front_image_register_resource_default("frontres\\reg1.bmp",
						      "background1");
		pilot_record_redraw_background();
		frontend_text_start_text_fade_in(CONCOURSE_TEXT_FADE_FRAMES);
	}

	pilot_record_update_pilot_selection_panel(frame_counter);
#ifdef XVT_MODERN
	if (xvt_dialog_is_active()) {
		return 0;
	}
#endif
	frontend_draw_rect_assign(&rect, 507, 452, 562, 464);
	sprintf(g_frontend_scratch_buffer, "v. %d.%d", CONCOURSE_VERSION_MAJOR,
		CONCOURSE_VERSION_MINOR);
	frontend_text_draw_centered(12, g_frontend_scratch_buffer, &rect,
				    0xFFFF);
	frontend_draw_rect_assign(&rect, 200, 452, 436, 464);
	if (g_pilot_data.name[0] != '\0') {
		sprintf(g_frontend_scratch_buffer, "%c%s %c%s", 6,
			g_pilot_data.rating_name, 1, g_pilot_data.name);
		frontend_text_draw_centered(12, g_frontend_scratch_buffer,
					    &rect, g_color_yellow);
		if (g_pilot_data.current_faction_id == 0) {
			sprintf(g_frontend_scratch_buffer, "rebtiny%d",
				(frame_counter %
				 CONCOURSE_ANIMATION_CYCLE_FRAMES) >>
					1);
		} else {
			sprintf(g_frontend_scratch_buffer, "imptiny%d",
				(frame_counter %
				 CONCOURSE_ANIMATION_CYCLE_FRAMES) >>
					1);
		}
		front_image_draw_sprite(g_frontend_scratch_buffer, 204, 453);
		front_image_draw_sprite(g_frontend_scratch_buffer, 420, 453);
	}
	switch (g_pilot_record_page) {
	case 0:
		pilot_record_draw_pilot_statistics_page();
		break;
	case 1:
		pilot_record_draw_pilot_awards_page();
		break;
	case 2:
		pilot_record_draw_pilot_rating_page();
		break;
	case 3:
		pilot_record_draw_mission_achievements_page();
		break;
	case 4:
		pilot_record_draw_campaign_medals_page();
		break;
	case 5:
		pilot_record_draw_cutscene_viewer_page();
		break;
	default:
		break;
	}
	pilot_record_update_navigation_controls();
	return frontend_handle_common_screen_controls(0) == 1;
}
