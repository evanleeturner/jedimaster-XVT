#include "xvt_runtime/runtime/flight_task.h"

#include "xvt_runtime/config/config.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/flight_checkpoint.h"
#include "xvt_runtime/runtime/flight_internal.h"
#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_network_exchange.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/runtime/resync_task.h"
#include "xvt_runtime/snapshot/cockpit_capture.h"
#include "xvt_runtime/snapshot/render_capture.h"
#include "xvt_runtime/timing/flight_integration.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/player_timing.h"
#include "xvt_runtime/timing/reference_motion.h"

typedef enum xvt_flight_phase {
	XVT_FLIGHT_IDLE,
	XVT_FLIGHT_PREPARE,
	XVT_FLIGHT_SESSION,
	XVT_FLIGHT_DEVICES,
	XVT_FLIGHT_GLOBALS,
	XVT_FLIGHT_PALETTE,
	XVT_FLIGHT_MISSION_SETUP,
	XVT_FLIGHT_MISSION,
	XVT_FLIGHT_VOICES,
	XVT_FLIGHT_RESOURCES,
	XVT_FLIGHT_LOADING_COMPLETE,
	XVT_FLIGHT_RUNTIME,
	XVT_FLIGHT_FIRST_DELTA,
	XVT_FLIGHT_OPTIONS,
	XVT_FLIGHT_WORLD,
	XVT_FLIGHT_START,
	XVT_FLIGHT_FRAMES,
	XVT_FLIGHT_CLEANUP,
	XVT_FLIGHT_FADE,
	XVT_FLIGHT_DONE
} xvt_flight_phase;

static struct {
	xvt_flight_phase phase;
	char command[1024];
	int resources_allocated;
	int commit_results;
	int options_failed;
	int result;
	int released;
	int mission_entered;
} g_flight;

int xvt_flight_task_begin(const char *command)
{
	if (xvt_flight_task_is_active() || !command) {
		return 0;
	}
	memset(&g_flight, 0, sizeof(g_flight));
	snprintf(g_flight.command, sizeof(g_flight.command), "%s", command);
	g_flight.phase = XVT_FLIGHT_PREPARE;
	xvt_flight_sim_reset();
	XVT_LOG_INFO("flight.launch");
	return 1;
}

/* The flight.end reason for a flight that left the phase from for cleanup. */
static const char *xvt_flight_task_end_reason(xvt_flight_phase from)
{
	switch (from) {
	case XVT_FLIGHT_PREPARE:
	case XVT_FLIGHT_SESSION:
	case XVT_FLIGHT_DEVICES:
		return "entry_failed";
	case XVT_FLIGHT_OPTIONS:
		return "options_failed";
	case XVT_FLIGHT_WORLD:
		return "world_failed";
	case XVT_FLIGHT_START:
		return "start_failed";
	case XVT_FLIGHT_FRAMES:
		return "frames_ended";
	default:
		return "other";
	}
}

static void xvt_flight_task_release_mission(int quitting)
{
	if (g_flight.released) {
		return;
	}
	g_flight.released = 1;
	xvt_resync_reset();
	xvt_flight_network_reset_mission();
	xvt_flight_frame_reset_replay();
	xvt_render_capture_end_mission();
	xvt_flight_timing_end_session();
	xvt_flight_integration_shutdown();
	xvt_reference_motion_shutdown();
	xvt_player_timing_reset();
	flight_free_world_state_buffers();
	if (g_unused_flight_debug_log_file) {
		file_close(g_unused_flight_debug_log_file);
		g_unused_flight_debug_log_file = NULL;
	}
	if (g_flight.commit_results) {
		g_flight_render_transition_hook();
		fe_disk_io_commit_flight_results(0, 0);
		g_flight.commit_results = 0;
	}
	g_flight_display_surfaces_active = 0;
	if (g_flight.mission_entered) {
		sound_stop_all_instances();
	}
	if (g_flight.options_failed) {
		sound_empty_stub();
	}
	if (g_flight.resources_allocated) {
		fe_disk_io_free_flight_resources();
		g_flight.resources_allocated = 0;
	}
	if (g_flight.mission_entered && !quitting &&
	    g_pre_flight_resolution_mode != g_flight_resolution_mode) {
		flight_display_apply_resolution_mode_stub(
			g_pre_flight_resolution_mode);
	}
	if (g_flight.options_failed) {
		memcpy(&g_local_player_snapshot_on_options_sync_failure,
		       &g_players[g_local_player],
		       sizeof(g_local_player_snapshot_on_options_sync_failure));
	} else if (g_flight.result) {
		pilot_save(0);
	}
	xvt_flight_sim_reset();
}

static int xvt_flight_task_start_world(void)
{
	int offline =
		atoi(g_flight_launch_args
			     .arguments[FLIGHT_LAUNCH_ARG_NUM_PLAYERS]) == 1 &&
		atoi(g_flight_launch_args
			     .arguments[FLIGHT_LAUNCH_ARG_IS_HOST]) == 1 &&
		!g_flight_in_progress_launch &&
		xvt_network_session_get_status().state !=
			XVT_NETWORK_SESSION_ESTABLISHED;
	xvt_flight_timing_profile profile =
		offline ? (xvt_config_settings()->flight_unlocked
				   ? XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED
				   : XVT_FLIGHT_TIMING_NATIVE)
			: XVT_FLIGHT_TIMING_NETWORK_125;
	int unlocked = profile != XVT_FLIGHT_TIMING_NATIVE;
	if (profile == XVT_FLIGHT_TIMING_NETWORK_125) {
		g_game_time = 0;
	}
	if (g_region_main_object_slot_end < 0 ||
	    g_region_static_object_slot_count < 0) {
		return 0;
	}
	size_t capacity = (size_t)g_region_main_object_slot_end +
			  g_region_static_object_slot_count;
	if (capacity > UINT16_MAX) {
		return 0;
	}
	xvt_player_timing_begin_world();
	if (unlocked && (!xvt_flight_integration_init(capacity) ||
			 !xvt_reference_motion_init(capacity))) {
		XVT_LOG_WARN("timing.alloc_failed outcome=\"%s\"",
			     profile == XVT_FLIGHT_TIMING_NETWORK_125
				     ? "mission_blocked"
				     : "native_flight");
		xvt_flight_integration_shutdown();
		xvt_reference_motion_shutdown();
		if (profile == XVT_FLIGHT_TIMING_NETWORK_125) {
			return 0;
		}
		unlocked = 0;
		profile = XVT_FLIGHT_TIMING_NATIVE;
	}
	xvt_flight_timing_begin_session(profile);
	xvt_flight_network_reset_mission();
	unsigned mask = 0;
	for (unsigned i = 0; i < 8; ++i) {
		if (g_players[i].participation_state) {
			mask |= 1u << i;
		}
	}
	xvt_flight_checkpoint_begin((uint8_t)mask);
	g_unused_flight_startup_object_pass_state = 0;
	for (int i = 0; i < g_region_main_object_slot_end; ++i) {
		if (g_object_table[i].mobj) {
			g_object_table[i].mobj->sim_state_timestamp = 0;
		}
	}
	flight_alloc_world_state_buffers();
	if (!g_world_state_buffer || !g_world_state_dup_buffer) {
		return 0;
	}
	flight_sync_clear_buffered_world_messages();
	flight_save_world_state();
	if (!g_world_state_size) {
		return 0;
	}
	if (xvt_flight_timing_is_network125()) {
		flight_checksum_world_state(0, 0);
		g_flight_net_world_checksum_epoch = 0;
		flight_sync_snapshot_world_state_for_replay();
		g_flight_net_buffer_world_messages_until_checksum = 1;
	}
	flight_view_render_startup_frame();
	net_session_stub_return_true();
	XVT_LOG_INFO("flight.world players=%d mask=%02x slots=%zu bytes=%u",
		     g_active_flight_player_count, mask, capacity,
		     g_world_state_size);
	return 1;
}

/* Moves the flight one step through its phases per call. A lost session first sends it to cleanup,
 * and an active resync holds it in place; otherwise the current phase runs one stage (preparation,
 * session, devices, the loading steps, options, world, start, frames, cleanup, fade) and picks the
 * next phase. Each change of phase, and the way the flight ended, is logged. */
void xvt_flight_task_update(void)
{
	xvt_flight_phase previous = g_flight.phase;
	if (xvt_network_session_is_lost() &&
	    g_flight.phase < XVT_FLIGHT_CLEANUP) {
		XVT_LOG_INFO("flight.end result=0 reason=\"session_lost\"");
		xvt_resync_reset();
		g_flight.result = 0;
		g_flight.phase = XVT_FLIGHT_CLEANUP;
	}
	xvt_resync_update();
	if (xvt_resync_is_active()) {
		return;
	}
	switch (g_flight.phase) {
	case XVT_FLIGHT_PREPARE: {
		int status = xvt_flight_entry_prepare(g_flight.command);
		g_flight.phase = status == XVT_FLIGHT_NETWORK_PENDING
					 ? XVT_FLIGHT_SESSION
				 : status ? XVT_FLIGHT_DEVICES
					  : XVT_FLIGHT_CLEANUP;
		break;
	}
	case XVT_FLIGHT_SESSION: {
		int status = xvt_flight_network_exchange_roster();
		if (status != XVT_FLIGHT_NETWORK_PENDING) {
			g_flight.phase = status ? XVT_FLIGHT_DEVICES
						: XVT_FLIGHT_CLEANUP;
		}
		break;
	}
	case XVT_FLIGHT_DEVICES:
		g_flight.phase = xvt_flight_entry_create_devices()
					 ? XVT_FLIGHT_GLOBALS
					 : XVT_FLIGHT_CLEANUP;
		break;
	case XVT_FLIGHT_GLOBALS:
		g_flight.mission_entered = 1;
		xvt_flight_loading_globals();
		g_flight.phase = XVT_FLIGHT_PALETTE;
		break;
	case XVT_FLIGHT_PALETTE:
		g_flight.resources_allocated = 1;
		xvt_flight_loading_palette();
		g_flight.phase = XVT_FLIGHT_MISSION_SETUP;
		break;
	case XVT_FLIGHT_MISSION_SETUP:
		xvt_flight_loading_mission_setup();
		g_flight.phase = XVT_FLIGHT_MISSION;
		break;
	case XVT_FLIGHT_MISSION:
		XVT_LOG_INFO("flight.mission file=\"%s\"",
			     g_current_mission_file);
		flight_surface_lock();
		xvt_render_capture_begin_mission();
		mission_init(g_current_mission_file);
		flight_surface_unlock();
		g_flight.phase = XVT_FLIGHT_VOICES;
		break;
	case XVT_FLIGHT_VOICES:
		fsfx_load_mission_voice_sfx();
		g_flight.phase = XVT_FLIGHT_RESOURCES;
		break;
	case XVT_FLIGHT_RESOURCES:
		fe_disk_io_init_resources();
		g_flight.phase = XVT_FLIGHT_LOADING_COMPLETE;
		break;
	case XVT_FLIGHT_LOADING_COMPLETE:
		/* The original finishes this progress cycle without doing more loading. */
		g_flight_loading_progress_step |= 0x7f;
		flight_loading_pulse_and_draw_progress_screen();
		g_flight.phase = XVT_FLIGHT_RUNTIME;
		break;
	case XVT_FLIGHT_RUNTIME:
		xvt_flight_loading_runtime();
		g_flight.phase = XVT_FLIGHT_FIRST_DELTA;
		break;
	case XVT_FLIGHT_FIRST_DELTA:
		if (!xvt_cockpit_loading_assets_ready()) {
			break;
		}
		g_input_timestamp += time_consume_elapsed_ticks();
		if (g_input_timestamp) {
			object_relink_mobile_object_pointers();
			g_flight.phase = XVT_FLIGHT_OPTIONS;
		}
		break;
	case XVT_FLIGHT_OPTIONS: {
		int status = flight_net_sync_player_options_and_taunts();
		if (status == XVT_FLIGHT_NETWORK_PENDING) {
			break;
		}
		if (status) {
			XVT_LOG_INFO("flight.options players=%d cookie=%u",
				     g_active_flight_player_count,
				     xvt_flight_network_cookie());
			g_flight.phase = XVT_FLIGHT_WORLD;
		} else {
			g_flight.options_failed = 1;
			g_flight.phase = XVT_FLIGHT_CLEANUP;
		}
		break;
	}
	case XVT_FLIGHT_WORLD:
		g_flight.phase = xvt_flight_task_start_world()
					 ? XVT_FLIGHT_START
					 : XVT_FLIGHT_CLEANUP;
		break;
	case XVT_FLIGHT_START: {
		g_flight.commit_results = 1;
		g_flight.result = 1;
		int status = flight_net_wait_for_mission_start();
		if (status == XVT_FLIGHT_NETWORK_PENDING) {
			break;
		}
		if (status) {
			XVT_LOG_INFO("flight.start local=%d players=%d",
				     g_local_player,
				     g_active_flight_player_count);
			xvt_flight_frame_begin();
			g_flight.phase = XVT_FLIGHT_FRAMES;
		} else {
			g_flight.phase = XVT_FLIGHT_CLEANUP;
		}
		break;
	}
	case XVT_FLIGHT_FRAMES:
		if (xvt_flight_frame_update()) {
			g_flight.phase = XVT_FLIGHT_CLEANUP;
		}
		break;
	case XVT_FLIGHT_CLEANUP:
		xvt_flight_task_release_mission(0);
		if (g_music_cd_mci_device_id && g_game_config.music_enabled &&
		    g_game_config.music_volume) {
			unsigned int volume =
				UINT16_MAX * g_game_config.music_volume / 9;
			xvt_cd_task_begin_fade(volume, volume / 8, 1000);
		}
		g_flight.phase = XVT_FLIGHT_FADE;
		break;
	case XVT_FLIGHT_FADE:
		if (xvt_cd_task_is_fading()) {
			break;
		}
		music_cd_close_device();
		xvt_flight_entry_cleanup();
		g_flight.phase = XVT_FLIGHT_DONE;
		break;
	default:
		break;
	}
	if (previous != g_flight.phase) {
		XVT_LOG_INFO("flight.phase from=%d to=%d", previous,
			     g_flight.phase);
	}
	/* A lost session's cleanup ran inside this tick and is reported above;
	 * this is every other way in. */
	if (g_flight.phase == XVT_FLIGHT_CLEANUP &&
	    previous != XVT_FLIGHT_CLEANUP) {
		XVT_LOG_INFO("flight.end result=%d reason=\"%s\"",
			     g_flight.result,
			     xvt_flight_task_end_reason(previous));
	}
}

int xvt_flight_task_is_active(void)
{
	return g_flight.phase > XVT_FLIGHT_IDLE &&
	       g_flight.phase < XVT_FLIGHT_DONE;
}

int xvt_flight_task_is_loading(void)
{
	/* WORLD produces the first view and advances to START in the same tick. */
	return g_flight.phase > XVT_FLIGHT_IDLE &&
	       g_flight.phase <= XVT_FLIGHT_WORLD;
}

int xvt_flight_task_is_complete(void)
{
	return g_flight.phase == XVT_FLIGHT_DONE;
}

int xvt_flight_task_get_result(void) { return g_flight.result; }

int xvt_flight_task_continues_without_focus(void)
{
	return xvt_flight_task_is_active() && g_active_flight_player_count > 1;
}

uint64_t xvt_flight_task_next_wake_delay_us(void)
{
	if (xvt_resync_is_active() || xvt_resync_holds_input()) {
		return xvt_resync_next_wake_delay_us();
	}
	if (g_flight.phase == XVT_FLIGHT_FRAMES) {
		return xvt_flight_frame_next_wake_delay_us();
	}
	if (g_flight.phase == XVT_FLIGHT_FIRST_DELTA) {
		return xvt_flight_time_delay_for_ticks(1);
	}
	if (g_flight.phase == XVT_FLIGHT_FADE) {
		return xvt_cd_task_next_wake_delay_us();
	}
	return UINT64_MAX;
}

void xvt_flight_task_shutdown(void)
{
	Aeron_SetRelativeMouseMode(0);
	xvt_resync_reset();
	xvt_flight_network_reset();
	if (xvt_flight_task_is_active()) {
		xvt_flight_task_release_mission(1);
		xvt_cd_task_cancel_fade();
		music_cd_close_device();
		xvt_flight_entry_cleanup();
	}
	memset(&g_flight, 0, sizeof(g_flight));
}
