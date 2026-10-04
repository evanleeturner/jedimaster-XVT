#include "xvt/frontend/frontend_flight.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/launch_task.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/flight/flight.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_debrief.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt/util/time.h"

/* GetTickCount, in milliseconds, when the "Prepare for launch" screen
 * opened; only flight_loading_update_ready_screen writes it. */
// GLOBAL: XVT 0x669798
int g_flight_loading_ready_screen_start_ms = 0;
/* GetTickCount at the launch screen's last time check; only
 * flight_loading_update_ready_screen uses it. */
// GLOBAL: XVT 0x669794
int g_flight_loading_ready_screen_now_ms = 0;
/* Set to 1 by flight_loading_update_ready_screen; nothing reads it. */
// GLOBAL: XVT 0xB69CDC
int g_unused_flight_loading_ready_screen_flag = 0;
/* Human players in the mission being launched: 1 in single player, else
 * net_count_ready_players, set on the launch screen's first frame. It is the
 * last number on the flight's command line. */
// GLOBAL: XVT 0xB6A2C8
int g_frontend_launch_human_player_count = 0;
/* The flight's command line: "~folder\file~ ~formal name~ ~pilot name~ host
 * ~game name~ 0 players" and then "nopageflip nofullscreen" or "pageflip
 * fullscreen". The original build builds it in frontend_flight_launch_session
 * and passes it to flight_main; the modern build's xvt_launch_task_queue copies
 * here the line it passes. */
// GLOBAL: XVT 0x66DA78
char g_frontend_flight_command_line[256] = {0};

/* Update function of the "Prepare for launch" screen shown before a flight.
 * On frame 0 it hides the cursor, sets g_mission_setup_is_host (1 in single
 * player, else net_is_host), stores the human player count in
 * g_frontend_launch_human_player_count and g_pilot_data.num_human_players_last_mission,
 * notes the start time, draws frontres\wait.bmp with the "frame" and
 * "alloff" images into the offscreen surface and stops the text fade. Every
 * frame it draws FRONTSTR_205_PREPARE_FOR_LAUNCH centered in font 15. The
 * host, more than 2000 ms after the start, sends the lobby state with
 * mission_setup_send_lobby_state(0) and switches to frontend_flight_launch_session;
 * every player switches more than 4000 ms after it. Switching frees the
 * "background" image. Returns 0. */
// FUNCTION: XVT 0x4FB350
int flight_loading_update_ready_screen(int frame_counter)
{
	struct RECT rect;
	int ready_player_count;

	if (frame_counter == 0) {
		ready_player_count = 1;
		frontend_cursor_hide();
		g_unused_flight_loading_ready_screen_flag = 1;
		g_mission_setup_is_host = net_is_host();
		if (g_frontend_mission_session_mode ==
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
			g_mission_setup_is_host = 1;
		} else {
			ready_player_count = net_count_ready_players();
		}
		g_frontend_launch_human_player_count = ready_player_count;
		g_pilot_data.num_human_players_last_mission =
			ready_player_count;
		g_flight_loading_ready_screen_start_ms = GetTickCount();
		front_image_register_resource_default("frontres\\wait.bmp",
						      "background");
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_opaque("background", 0, 0);
		front_image_draw_sprite("frame", 0, 0);
		front_image_draw_sprite("alloff", 0, 0);
		frontend_display_unlock_offscreen_surface(1);
		frontend_text_stop_text_fade();
	}

	frontend_draw_rect_assign(&rect, 0, 0, 639, 479);
	frontend_text_draw_centered(
		15, frontend_string_get(FRONTSTR_205_PREPARE_FOR_LAUNCH), &rect,
		0xFFFF);
	if (g_mission_setup_is_host != 0) {
		g_flight_loading_ready_screen_now_ms = GetTickCount();
		if (g_flight_loading_ready_screen_start_ms + 2000 <
		    g_flight_loading_ready_screen_now_ms) {
			mission_setup_send_lobby_state(0);
			g_unused_flight_loading_ready_screen_flag = 1;
			front_image_free_resource_by_name("background");
			frontend_screen_set_callbacks(
				frontend_flight_launch_session,
				frontend_flight_no_op_exit);
			return 0;
		}
	}
	g_flight_loading_ready_screen_now_ms = GetTickCount();
	if (g_flight_loading_ready_screen_start_ms + 4000 <
	    g_flight_loading_ready_screen_now_ms) {
		g_unused_flight_loading_ready_screen_flag = 1;
		front_image_free_resource_by_name("background");
		frontend_screen_set_callbacks(frontend_flight_launch_session,
					      frontend_flight_no_op_exit);
		return 0;
	}
	return 0;
}

/* Exit function of the launch screen: does nothing and returns 0. */
// FUNCTION: XVT 0x506690
int frontend_flight_no_op_exit(int frame_counter)
{
	(void)frame_counter;

	return 0;
}

/* Update function that flies the mission. The modern build hands the work to
 * xvt_launch_task_queue and returns what it returns. The original build, on
 * frame 0: writes the config and saves the pilot; asks for the game CD until
 * it is found, returning 1, which closes the game, on Cancel; without the
 * host CD and outside a network client it says so, cancels a network game,
 * and returns to the concourse. Otherwise, with datapad music on and the CD
 * track still playing, it fades the music to an eighth of its volume over
 * 1000 ms; then hands the display to the flight, builds
 * g_frontend_flight_command_line for the pilot's mission and runs flight_main,
 * whose result decides the next screen. Then it takes the display back,
 * reloads the sound list, writes the config, refreshes the pilot's rating
 * name, saves the pilot and restarts CD track 7 when datapad music is on. A
 * zero result returns to the concourse, shutting the network session; any
 * other goes to the debriefing. Returns 0 on every path but Cancel. Does not
 * check that the pilot's mission is in the list; the note inside covers the
 * case with no list. */
// FUNCTION: XVT 0x5066A0
int frontend_flight_launch_session(int frame_counter)
{
#ifdef XVT_MODERN
	(void)frame_counter;
	return xvt_launch_task_queue();
#else
	enum {
		MUSIC_VOLUME_MAX = 0xFFFF,
		MUSIC_VOLUME_MAX_LEVEL = 9,
		MUSIC_FADE_DIVISOR = 8,
		MUSIC_FADE_DURATION_MS = 1000,
		FLIGHT_WINDOW_PROC_MODE = 1,
		FRONTEND_WINDOW_PROC_MODE = 0,
		FRONTEND_MUSIC_TRACK = 7,
		PILOT_RATING_STRING_BASE = 122,
	};

	int launch_succeeded;
	unsigned int mission_index;
	int music_volume;

	launch_succeeded = 0;
	if (frame_counter == 0) {
		config_write();
		pilot_save(0);
		while (1) {
			frontend_cursor_show();
			if (file_check_game_cd_present(g_skip_movie_checks) !=
			    0) {
				break;
			}
			cd_audio_initialize();
			/* launch_succeeded first holds the answer to this dialog (nonzero means try again). It holds
			 * the flight's result only once the mission list loads, so with no mission list a retry answer
			 * is later read as a successful launch. */
			launch_succeeded = frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_706_FAILED_TO_DETECT_RETAIL_XVT_CD),
				frontend_string_get(
					FRONTSTR_707_PLEASE_INSERT_THE_X_WING_VS_TIE_FIGHTER_CD),
				frontend_string_get(
					FRONTSTR_708_INTO_YOUR_CD_ROM_DRIVE),
				frontend_string_get(FRONTSTR_523_OKAY),
				frontend_string_get(FRONTSTR_019_CANCEL));
			if (launch_succeeded == 0) {
				frontend_cursor_show();
				return 1;
			}
		}

		frontend_cursor_hide();
		frontend_check_host_cd_present();
		if (g_host_cd_available == 0 &&
		    g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_NET_CLIENT) {

			frontend_cursor_show();
			frontend_dialog_show_confirm_dialog(
				frontend_string_get(
					FRONTSTR_749_YOU_SWITCHED_TO_A_CLIENT_CD),
				frontend_string_get(
					FRONTSTR_750_YOU_CANNOT_HOST_A_NETWORK_GAME),
				frontend_string_get(
					FRONTSTR_751_OR_FLY_SOLO_WITH_THE_CLIENT_CD),
				NULL, NULL);
			if (g_frontend_mission_session_mode !=
			    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
				g_frontend_net_packet_scratch.packet_type =
					NET_PACKET_HOST_CANCELLED;
				net_send_packet_and_flush(
					0, &g_frontend_net_packet_scratch,
					sizeof(g_frontend_net_packet_scratch
						       .packet_type));
				net_shutdown_direct_play_session();
			}
			g_frontend_mission_session_mode =
				FRONTEND_MISSION_SESSION_NONE;
			frontend_screen_set_callbacks(concourse_update,
						      concourse_exit);
			return 0;
		}

		if (g_game_config.datapad_music_enabled != 0 &&
		    cd_audio_is_playback_complete() == 0) {
			music_volume = MUSIC_VOLUME_MAX *
				       g_game_config.music_volume /
				       MUSIC_VOLUME_MAX_LEVEL;
			cd_audio_fade_aux_volume((unsigned int)music_volume,
						 (unsigned int)music_volume /
							 MUSIC_FADE_DIVISOR,
						 MUSIC_FADE_DURATION_MS);
		}
		frontend_display_flip_direct_draw_to_gdi_surface();
		frontend_display_release_surfaces_for_flight();
		frontend_display_set_wnd_proc_mode(FLIGHT_WINDOW_PROC_MODE);
		mission_setup_load_mission_list(
			g_pilot_data.mission_directory_id);
		if (g_mission_list != NULL) {
			mission_index = 0;
			for (mission_index = 0; mission_index < g_mission_count;
			     ++mission_index) {
				if (g_mission_list[mission_index].mission_idx ==
				    g_pilot_data.mission_description_ids
					    [g_pilot_data
						     .mission_directory_id]) {
					break;
				}
			}
			if (g_opt_no_fullscreen != 0) {
				sprintf(g_frontend_flight_command_line,
					"~%s\\%s~ ~%s~ ~%s~ %u ~%s~ 0 %u nopageflip nofullscreen",
					g_mission_directory_names
						[g_pilot_data
							 .mission_directory_id],
					g_mission_list[mission_index].file_name,
					g_pilot_data
						.network_players
							[g_local_pilot_network_player_index]
						.formal_name,
					g_pilot_data.name,
					g_mission_setup_is_host,
					g_pilot_data.multiplayer_game_name,
					g_frontend_launch_human_player_count);
			} else {
				sprintf(g_frontend_flight_command_line,
					"~%s\\%s~ ~%s~ ~%s~ %u ~%s~ 0 %u pageflip fullscreen",
					g_mission_directory_names
						[g_pilot_data
							 .mission_directory_id],
					g_mission_list[mission_index].file_name,
					g_pilot_data
						.network_players
							[g_local_pilot_network_player_index]
						.formal_name,
					g_pilot_data.name,
					g_mission_setup_is_host,
					g_pilot_data.multiplayer_game_name,
					g_frontend_launch_human_player_count);
			}
			free(g_mission_list);
			g_mission_list = NULL;
			launch_succeeded =
				flight_main(g_frontend_flight_command_line);
		}

		frontend_display_reinit_surfaces();
		frontend_display_set_wnd_proc_mode(FRONTEND_WINDOW_PROC_MODE);
		frontend_sound_load_list("sfx\\sfx.lst");
		config_write();
		strcpy(g_pilot_data.rating_name,
		       frontend_string_get(
			       (frontend_string_id)(g_pilot_data.rating +
						    PILOT_RATING_STRING_BASE)));
		pilot_save(0);
		cd_audio_initialize();
		cd_audio_enable_loop_current_track();
		if (g_game_config.datapad_music_enabled != 0) {
			cd_audio_play_track_from_time(FRONTEND_MUSIC_TRACK, 0,
						      0);
			music_volume = MUSIC_VOLUME_MAX *
				       g_game_config.music_volume /
				       MUSIC_VOLUME_MAX_LEVEL;
			cd_audio_set_aux_volume((unsigned int)music_volume);
		}
	}

	if (launch_succeeded == 0) {
		frontend_cursor_show();
		net_shutdown_direct_play_session();
		frontend_screen_set_callbacks(concourse_update, concourse_exit);
	} else {
		frontend_screen_set_callbacks(mission_debrief_update,
					      mission_debrief_exit);
	}
	return 0;
#endif
}
