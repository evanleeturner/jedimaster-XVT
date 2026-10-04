#include "xvt_runtime/runtime/frontend_task.h"

#include <stdlib.h>
#include <string.h>

#include "aeron/aeron.h"
#include "aeron/compat/dsound.h"
#include "xvt/assets/model_preview.h"
#include "xvt/assets/opt_model.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/direct_sound.h"
#include "xvt/audio/sound.h"
#include "xvt/flight/flight.h"
#include "xvt/frontend/concourse.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/credits.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_bootstrap.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_joystick.h"
#include "xvt/frontend/frontend_mission_list.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/frontend/tech_library.h"
#include "xvt/net/frontend_net.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/campaign_task.h"
#include "xvt_runtime/runtime/cd_task.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/frontend_actions.h"
#include "xvt_runtime/runtime/frontend_movies.h"
#include "xvt_runtime/runtime/launch_task.h"
#include "xvt_runtime/runtime/movie_task.h"
#include "xvt_runtime/runtime/network_task.h"
#include "xvt_runtime/runtime/presentation.h"
#include "xvt_runtime/snapshot/render_frontend.h"
#include "xvt_runtime/storage/storage.h"
#include "xvt_runtime/timing/host_clock.h"

static uint64_t g_next_frame_due_us;
static uint64_t g_next_joystick_poll_due_us;
static int g_quit;
static int g_initialized;
static int g_startup_mode;
static int g_continuation_frame;

int xvt_frontend_task_init(int skip_intro)
{
	static char command_line[] = "";
	memset(&g_front_state, 0, sizeof(g_front_state));
	g_shutdown_complete = 0;
	g_quit = 0;
	xvt_frontend_action_reset();
	xvt_frontend_movies_reset();
	xvt_campaign_task_reset();
	g_initialized = 1;
	g_cmd_line = command_line;
	g_opt_skip_intro = skip_intro;
	g_no_page_flip = 1;
	g_opt_no_fullscreen = 0;
	g_front_state.frontend_sound_buffers =
		calloc(128, sizeof(*g_front_state.frontend_sound_buffers));
	g_front_state.frontend_sound_voices =
		calloc(12, sizeof(*g_front_state.frontend_sound_voices));
	g_front_state.resource_table =
		calloc(512, sizeof(*g_front_state.resource_table));
	if (!g_front_state.frontend_sound_buffers ||
	    !g_front_state.frontend_sound_voices ||
	    !g_front_state.resource_table) {
		return 0;
	}
	g_front_state.clip_max_x = 639;
	g_front_state.clip_max_y = 479;
	g_front_state.display_bpp = 16;
	g_front_state.clear_back_buffer_after_present = 1;
	g_front_state.cd_audio_saved_aux_volume = -1;
	g_front_state.app_active = 1;
	frontend_display_set_frame_rate(24);
	file_detect_game_and_cd_paths(NULL);
	config_load();
	if (!frontend_display_init_main_window(NULL, 0)) {
		return 0;
	}
	g_front_state.screen_states[0].update_fn =
		skip_intro ? concourse_update
			   : frontend_bootstrap_play_opening_and_enter_credits;
	g_front_state.screen_states[0].exit_fn =
		skip_intro ? concourse_exit
			   : frontend_bootstrap_exit_intro_and_load_credits;
	g_startup_mode = skip_intro ? 2 : 1;
	g_next_frame_due_us = xvt_time_get_elapsed_us();
	g_next_joystick_poll_due_us = g_next_frame_due_us;
	XVT_LOG_INFO("frontend.ready");
	return 1;
}

void xvt_frontend_task_service_frame_systems(void)
{
	uint64_t now = xvt_time_get_elapsed_us();
	if (now >= g_next_joystick_poll_due_us) {
		joystick_update_state(0);
		joystick_update_state(1);
		g_next_joystick_poll_due_us = now + 100000;
	}
	if (!xvt_network_task_is_active()) {
		net_pump_incoming_packets();
	}
	xvt_cd_task_update();
}

int xvt_frontend_task_run_frame(void)
{
	frontend_screen_exit_fn exit_fn;
	frontend_screen_update_fn update_fn;
	int result;
	int stack_top = g_front_state.screen_stack_top;
	int dialog_was_active = xvt_dialog_is_active();
	g_continuation_frame = 0;
	g_front_state.net_ready_player_left_this_frame = 0;
	if (g_front_state.text_fade_frames_left) {
		memset(&g_front_state.text_fade_color_cache, 0,
		       sizeof(g_front_state.text_fade_color_cache));
	}
	update_fn = g_front_state.screen_states[stack_top].update_fn;
	if (!update_fn) {
		return 0;
	}
	xvt_presentation_require_classic();
	xvt_render_frontend_begin_draw();
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	if (!g_draw_surface_ptr) {
		xvt_storage_fatal("Cannot lock frontend display", 1);
		return 2;
	}
	exit_fn = g_front_state.screen_states[stack_top].exit_fn;
	g_continuation_frame = xvt_network_task_resume(&result);
	if (!g_continuation_frame) {
		g_continuation_frame = xvt_dialog_resume_continuation(&result);
	}
	if (!g_continuation_frame) {
		result = update_fn(g_front_state.frame_counter);
	}
	if (xvt_campaign_task_is_pending() || xvt_network_task_is_active()) {
		/* The entry prefix is suspended before the screen has completed frame zero. */
		frontend_display_unlock_back_buffer();
		if (g_continuation_frame && xvt_network_task_is_active()) {
			frontend_cursor_draw();
			frontend_display_present_frame();
			g_front_state.mouse_left_click_latch = 0;
			g_front_state.mouse_right_click_latch = 0;
			memset(g_front_state.joystick_button_released, 0,
			       sizeof(g_front_state.joystick_button_released));
		}
		return 0;
	}
	if (!dialog_was_active && xvt_dialog_is_active()) {
		frontend_display_unlock_back_buffer();
		return 0;
	}
	if (g_front_state.screen_callbacks_dirty || result == 1) {
		g_front_state.screen_callbacks_dirty = 0;
		if (exit_fn) {
			exit_fn(g_front_state.frame_counter);
		}
	}
	frontend_display_unlock_back_buffer();
	if (g_front_state.pending_screen_update_fn) {
		frontend_screen_push_state(
			g_front_state.pending_screen_update_fn,
			&g_front_state.pending_screen_rect);
		g_front_state.pending_screen_update_fn = NULL;
	}
	if (!xvt_movie_task_is_active() && g_front_state.cursor_visible) {
		frontend_cursor_draw();
	}
	memset(g_front_state.joystick_button_released, 0,
	       sizeof(g_front_state.joystick_button_released));
	++g_front_state.frame_counter;
	if (g_front_state.text_fade_frames_left) {
		--g_front_state.text_fade_frames_left;
	}
	g_front_state.mouse_left_click_latch = 0;
	g_front_state.mouse_right_click_latch = 0;
	return result;
}

void xvt_frontend_task_update(void)
{
	uint64_t now = xvt_time_get_elapsed_us();
	int result;
	if (g_quit || now < g_next_frame_due_us) {
		return;
	}
	g_next_frame_due_us =
		now + (uint64_t)g_front_state.frame_interval_ms * 1000;
	if (g_startup_mode) {
		int mode = g_startup_mode;
		g_startup_mode = 0;
		if (mode == 2) {
			if (frontend_load_resources()) {
				return;
			}
		} else {
			frontend_bootstrap_init_mode();
		}
	}
	if (xvt_launch_task_is_active()) {
		xvt_launch_task_update();
		return;
	}
	int credits_frame =
		!xvt_dialog_is_active() &&
		g_front_state.screen_states[g_front_state.screen_stack_top]
				.update_fn == credits_update_screen;
	if (xvt_dialog_is_active()) {
		xvt_dialog_update();
		result = 0;
	} else {
		result = xvt_frontend_task_run_frame();
	}
	if (result == 1 || result == 2) {
		g_quit = 1;
	}
	/* The original credits callback blocks in the music fade before presenting.
	 * Retain its last presentation through the fade and concourse transition. */
	if (credits_frame && g_credits_exit_pending) {
		return;
	}
	/* Keep the last presented dialog until the parent has drawn its next frame. */
	if (!xvt_movie_task_is_active() && !xvt_dialog_has_result() &&
	    !g_continuation_frame && !xvt_campaign_task_is_pending() &&
	    !xvt_network_task_is_active()) {
		frontend_display_present_frame();
	}
}

int xvt_frontend_task_should_quit(void) { return g_quit; }

uint64_t xvt_frontend_task_next_wake_delay_us(void)
{
	uint64_t now = xvt_time_get_elapsed_us();
	uint64_t delay =
		g_next_frame_due_us > now ? g_next_frame_due_us - now : 0;
	uint64_t cd = xvt_cd_task_next_wake_delay_us();
	return cd < delay ? cd : delay;
}

void xvt_frontend_task_shutdown(void)
{
	int index;
	if (!g_initialized) {
		return;
	}
	xvt_campaign_task_reset();
	xvt_network_task_shutdown();
	net_shutdown_direct_play_session_for_quit();
	xvt_frontend_movies_reset();
	xvt_cd_task_cancel_fade();
	xvt_launch_task_shutdown();
	xvt_dialog_shutdown();
	cd_audio_close_device();
	if (g_frontend_credits_file) {
		file_close(g_frontend_credits_file);
	}
	g_frontend_credits_file = NULL;
	frontend_display_unlock_back_buffer();
	if (g_front_state.ui_string_count) {
		if (g_pilot_data.name[0] && !pilot_save(0)) {
			xvt_storage_fatal("Cannot save the selected pilot", 1);
		}
		config_write();
	}
	concourse_exit(0);
	free(g_ship_list);
	g_ship_list = NULL;
	g_ship_count = 0;
	free(g_mission_list);
	g_mission_list = NULL;
	free(g_tech_library_spec_text_table);
	g_tech_library_spec_text_table = NULL;
	model_preview_free_resources();
	for (index = 0; index < 32768; ++index) {
		if (g_handle_tables.ptr_table[index]) {
			memory_free_handle((unsigned int)index + 1);
		}
	}
	memset(g_loaded_models, 0, sizeof(g_loaded_models));
	g_model_preview_model_data = NULL;
	g_model_preview_aux_buffer_handle = 0;
	g_model_preview_aux_buffer_capacity_bytes = 0;
	if (g_front_state.frontend_sound_voices) {
		for (index = 0; index < 12; ++index) {
			IDirectSoundBuffer *buffer =
				g_front_state.frontend_sound_voices[index]
					.buffer;
			if (buffer) {
				buffer->lpVtbl->Stop(buffer);
				buffer->lpVtbl->Release(buffer);
				g_front_state.frontend_sound_voices[index]
					.buffer = NULL;
			}
		}
	}
	if (g_front_state.frontend_sound_buffers) {
		for (index = 0; index < 128; ++index) {
			IDirectSoundBuffer *buffer =
				g_front_state.frontend_sound_buffers[index]
					.buffer;
			if (buffer) {
				buffer->lpVtbl->Release(buffer);
				g_front_state.frontend_sound_buffers[index]
					.buffer = NULL;
			}
		}
	}
	if (g_front_state.frontend_primary_sound_buffer) {
		g_front_state.frontend_primary_sound_buffer->lpVtbl->Release(
			g_front_state.frontend_primary_sound_buffer);
		g_front_state.frontend_primary_sound_buffer = NULL;
	}
	frontend_display_shutdown(0);
	free(g_cursor_save_buffer);
	g_cursor_save_buffer = NULL;
	free(g_frontend_chat_log_buffer);
	g_frontend_chat_log_buffer = NULL;
	free(g_cutscene_table);
	g_cutscene_table = NULL;
	g_cutscene_count = 0;
	free(g_campaign_award_sprites);
	g_campaign_award_sprites = NULL;
	g_campaign_award_sprite_count = 0;
	g_initialized = 0;
}
