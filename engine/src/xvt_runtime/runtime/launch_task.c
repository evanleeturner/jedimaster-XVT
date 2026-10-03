#include "xvt_runtime/runtime/launch_task.h"

#include "aeron/aeron.h"
#include "xvt/assets/file.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_flight.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/mission_debrief.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/input/keyboard.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/cd_task.h"
#include "xvt_runtime/snapshot/render_frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	XVT_LAUNCH_IDLE,
	XVT_LAUNCH_FADE,
	XVT_LAUNCH_PENDING,
	XVT_LAUNCH_RUNNING
};

static int g_phase;
static char g_command[sizeof(g_frontend_flight_command_line)];

int xvt_launch_task_queue(void)
{
	unsigned int index;
	int length;
	int volume;
	if (g_phase != XVT_LAUNCH_IDLE) {
		return 0;
	}
	config_write();
	if (!pilot_save(0)) {
		return 1;
	}
	if (!file_check_game_cd_present(g_skip_movie_checks)) {
		xvt_storage_fatal(
			"Required flight/voice data is missing; select a complete installation with --setup",
			1);
		return 1;
	}
	frontend_check_host_cd_present();
	if (!g_host_cd_available &&
	    g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_NET_CLIENT) {
		xvt_storage_fatal(
			"Required training mission is missing from the installation",
			1);
		return 1;
	}
	mission_setup_load_mission_list(g_pilot_data.mission_directory_id);
	for (index = 0; g_mission_list && index < g_mission_count; ++index) {
		if (g_mission_list[index].mission_idx ==
		    g_pilot_data.mission_description_ids
			    [g_pilot_data.mission_directory_id]) {
			break;
		}
	}
	if (!g_mission_list || index == g_mission_count) {
		free(g_mission_list);
		g_mission_list = NULL;
		xvt_storage_fatal(
			"Selected mission is absent from the installed mission list",
			1);
		return 1;
	}
	length = snprintf(
		g_command, sizeof(g_command),
		"~%s\\%s~ ~%s~ ~%s~ %u ~%s~ 0 %u %s",
		g_mission_directory_names[g_pilot_data.mission_directory_id],
		g_mission_list[index].file_name,
		g_pilot_data.network_players[g_local_pilot_network_player_index]
			.formal_name,
		g_pilot_data.name, g_mission_setup_is_host,
		g_pilot_data.multiplayer_game_name,
		g_frontend_launch_human_player_count,
		g_opt_no_fullscreen ? "nopageflip nofullscreen"
				    : "pageflip fullscreen");
	free(g_mission_list);
	g_mission_list = NULL;
	if (length < 0 || length >= (int)sizeof(g_command)) {
		xvt_storage_fatal(
			"Mission launch arguments exceed their supported length",
			1);
		return 1;
	}
	memcpy(g_frontend_flight_command_line, g_command, sizeof(g_command));
	frontend_cursor_hide();
	if (g_game_config.datapad_music_enabled &&
	    !cd_audio_is_playback_complete()) {
		volume = 65535 * g_game_config.music_volume / 9;
		xvt_cd_task_begin_fade(volume, volume / 8, 1000);
	}
	g_phase = XVT_LAUNCH_FADE;
	return 0;
}

void xvt_launch_task_update(void)
{
	if (g_phase == XVT_LAUNCH_FADE && !xvt_cd_task_is_fading()) {
		g_phase = XVT_LAUNCH_PENDING;
		XVT_LOG_INFO("launch.queued");
		XVT_LOG_DEBUG("launch.command command=\"%s\"", g_command);
	}
	if ((g_phase == XVT_LAUNCH_FADE || g_phase == XVT_LAUNCH_PENDING) &&
	    keyboard_peek_char() == 27) {
		keyboard_flush_char_buffer();
		xvt_launch_task_complete(0);
	}
}

int xvt_launch_task_is_active(void) { return g_phase != XVT_LAUNCH_IDLE; }

int xvt_launch_task_has_pending_launch(void)
{
	return g_phase == XVT_LAUNCH_PENDING;
}

const char *xvt_launch_task_begin_pending_launch(void)
{
	if (!xvt_launch_task_has_pending_launch()) {
		return NULL;
	}
	frontend_display_unlock_back_buffer();
	frontend_display_flip_direct_draw_to_gdi_surface();
	frontend_display_release_surfaces_for_flight();
	xvt_render_frontend_release_surfaces();
	frontend_display_set_wnd_proc_mode(1);
	g_phase = XVT_LAUNCH_RUNNING;
	return g_command;
}

void xvt_launch_task_complete(int succeeded)
{
	int launched = g_phase == XVT_LAUNCH_RUNNING;
	if (!xvt_launch_task_is_active()) {
		return;
	}
	g_phase = XVT_LAUNCH_IDLE;
	xvt_cd_task_cancel_fade();
	if (launched) {
		if (!frontend_display_reinit_surfaces()) {
			xvt_storage_fatal(
				"Cannot restore frontend surfaces after flight",
				1);
			return;
		}
		frontend_display_set_wnd_proc_mode(0);
		frontend_sound_load_list("sfx\\sfx.lst");
		config_write();
		snprintf(g_pilot_data.rating_name,
			 sizeof(g_pilot_data.rating_name), "%s",
			 frontend_string_get(
				 (frontend_string_id)(g_pilot_data.rating +
						      122)));
		pilot_save(0);
		cd_audio_initialize();
		cd_audio_enable_loop_current_track();
	}
	if (g_game_config.datapad_music_enabled) {
		cd_audio_play_track_from_time(7, 0, 0);
		cd_audio_set_aux_volume(65535 * g_game_config.music_volume / 9);
	}
	if (succeeded) {
		frontend_screen_set_callbacks(mission_debrief_update,
					      mission_debrief_exit);
	} else {
		frontend_cursor_show();
		net_shutdown_direct_play_session();
		frontend_screen_set_callbacks(concourse_update, concourse_exit);
	}
	/* Completion occurs outside a screen slice; the previous exit callback is a no-op. */
	g_front_state.screen_callbacks_dirty = 0;
	g_front_state.frame_counter = 0;
}

void xvt_launch_task_shutdown(void)
{
	g_phase = XVT_LAUNCH_IDLE;
	g_command[0] = 0;
}
