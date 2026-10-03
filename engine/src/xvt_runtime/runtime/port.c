#include "xvt_runtime/runtime/port.h"

#include "aeron/aeron.h"
#include "aeron/compat/dplay.h"
#include "aeron/compat/host.h"
#include "xvt/flight/flight.h"
#include "xvt/net/net.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/input_bridge.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/campaign_task.h"
#include "xvt_runtime/runtime/cd_task.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/flight_task.h"
#include "xvt_runtime/runtime/frontend_task.h"
#include "xvt_runtime/runtime/launch_task.h"
#include "xvt_runtime/runtime/movie_task.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/runtime/network_task.h"
#include "xvt_runtime/runtime/presentation.h"
#include "xvt_runtime/snapshot/render_snapshot.h"
#include "xvt_runtime/storage/storage.h"
#include "xvt_runtime/timing/host_clock.h"

static int g_xvt_initialized;
static int g_xvt_paused;
static int g_xvt_rebase_clock;
static int g_xvt_exit_code;
static int g_skip_intro;
static int g_quitting;
static int g_settings_open, g_settings_requested;

void xvt_port_set_settings_open(int open) { g_settings_open = open != 0; }

void xvt_port_request_settings(void) { g_settings_requested = 1; }

int xvt_port_consume_settings_request(void)
{
	int requested = g_settings_requested;
	g_settings_requested = 0;
	return requested;
}

int xvt_port_network_requires_progress(void)
{
	return AeronDplay_IsActive() || xvt_network_task_is_active() ||
	       xvt_network_task_browser_visible() ||
	       xvt_flight_task_continues_without_focus();
}

void xvt_port_set_skip_intro(int skip_intro)
{
	if (!g_xvt_initialized) {
		g_skip_intro = skip_intro != 0;
	}
}

static void xvt_port_commit_snapshot(int movie_presented)
{
	const AeronInputSnapshot *input = Aeron_InputSnapshot();
	xvt_scene_kind kind = XVT_SCENE_FRONTEND;
	if (g_quitting) {
		kind = XVT_SCENE_NONE;
	} else if (movie_presented || xvt_movie_task_is_active()) {
		kind = XVT_SCENE_MOVIE;
	} else if (xvt_flight_task_is_active()) {
		if (xvt_dialog_is_active()) {
			kind = XVT_SCENE_FRONTEND_MODAL;
		} else {
			kind = xvt_flight_task_is_loading() ? XVT_SCENE_LOADING
							    : XVT_SCENE_FLIGHT;
		}
	}
	xvt_render_snapshot_set_scene_kind(kind);
	xvt_render_snapshot_commit(g_game_time, input && input->has_focus,
				   g_xvt_paused);
}

int xvt_port_init(void)
{
	int width;
	int height;
	if (g_xvt_initialized) {
		return 1;
	}
	g_xvt_exit_code = 0;
	g_quitting = 0;
	if (!Aeron_GetLogicalSize(&width, &height) ||
	    width != XVT_CLASSIC_WIDTH || height != XVT_CLASSIC_HEIGHT) {
		XVT_LOG_ERROR("port.host_invalid");
		g_xvt_exit_code = 1;
		return 0;
	}
	xvt_time_reset();
	xvt_presentation_init();
	AeronCompat_SetJoystickSource(NULL, NULL);
	AeronCompat_SetRumbleProvider(NULL);
	g_xvt_paused = 0;
	/* Startup and focus-resume intervals do not belong to virtual game time. */
	g_xvt_rebase_clock = 1;
	g_xvt_initialized = 1;
	xvt_input_init();
	xvt_render_snapshot_begin_frame();
	xvt_render_snapshot_set_scene_kind(XVT_SCENE_FRONTEND);
	if (!xvt_frontend_task_init(g_skip_intro)) {
		xvt_port_commit_snapshot(0);
		g_xvt_exit_code = 1;
		xvt_port_shutdown();
		return 0;
	}
	xvt_port_commit_snapshot(0);
	XVT_LOG_INFO("port.ready");
	return 1;
}

int xvt_port_is_initialized(void) { return g_xvt_initialized; }

void xvt_port_paused_frame(void)
{
	int movie_active;
	if (!g_xvt_initialized) {
		return;
	}
	xvt_render_snapshot_begin_frame();
	if (!g_xvt_paused) {
		g_xvt_paused = 1;
		Aeron_AudioSetPaused(1);
		XVT_LOG_INFO("port.paused");
	}
	g_xvt_rebase_clock = 1;
	AeronCompat_Update(1);
	xvt_input_update(1);
	movie_active = xvt_movie_task_is_active();
	if (movie_active) {
		xvt_movie_task_paused_frame();
	}
	xvt_presentation_end_frame(movie_active);
	xvt_port_commit_snapshot(movie_active);
}

void xvt_port_update(int32_t delta_us)
{
	const AeronInputSnapshot *input;
	int movie_active;
	if (xvt_port_service_quit()) {
		return;
	}
	xvt_render_snapshot_begin_frame();
	input = Aeron_InputSnapshot();
	xvt_input_update_mouse_capture(input);
	AeronDplay_Update();
	if (g_quitting) {
		xvt_network_session_service();
		xvt_port_commit_snapshot(0);
		return;
	}
	xvt_network_task_service_browser();
	xvt_movie_task_reap_finished();
	if (!xvt_port_network_requires_progress() &&
	    (g_settings_open ||
	     ((!input || !input->has_focus) &&
	      !xvt_movie_task_continues_without_focus() &&
	      !xvt_campaign_task_continues_without_focus()))) {
		xvt_network_session_service();
		xvt_port_paused_frame();
		return;
	}
	if (g_xvt_paused) {
		g_xvt_paused = 0;
		Aeron_AudioSetPaused(0);
		XVT_LOG_INFO("port.resumed");
	}
	/* Capture once per host frame; discard stale edges on startup and resume. */
	AeronCompat_Update(g_xvt_rebase_clock || xvt_input_is_captured() ||
			   !input || !input->has_focus);
	if (xvt_flight_task_is_active()) {
		xvt_input_update_flight(g_xvt_rebase_clock);
	} else {
		xvt_input_update(g_xvt_rebase_clock || xvt_dialog_has_result());
	}
	if (g_xvt_rebase_clock) {
		g_xvt_rebase_clock = 0;
	} else {
		xvt_time_advance_host_clock(delta_us);
	}
	if (xvt_flight_task_is_active()) {
		xvt_cd_task_update();
	} else {
		xvt_frontend_task_service_frame_systems();
	}
	movie_active = xvt_movie_task_is_active();
	if (movie_active) {
		xvt_movie_task_update();
	} else if (xvt_flight_task_is_active()) {
		xvt_flight_task_update();
	} else {
		xvt_frontend_task_update();
	}
	if (xvt_flight_task_is_complete()) {
		int result = xvt_flight_task_get_result();
		xvt_flight_task_shutdown();
		xvt_launch_task_complete(result);
		g_xvt_rebase_clock = 1;
	}
	if (xvt_launch_task_has_pending_launch()) {
		xvt_network_session_begin_flight();
		const char *command = xvt_launch_task_begin_pending_launch();
		if (!xvt_flight_task_begin(command)) {
			xvt_launch_task_complete(0);
		}
		g_xvt_rebase_clock = 1;
	}
	xvt_network_session_service();
	xvt_presentation_end_frame(movie_active);
	xvt_port_commit_snapshot(movie_active);
}

int xvt_port_service_quit(void)
{
	if (!g_xvt_initialized || Aeron_FatalErrorRequested()) {
		return 1;
	}
	if (!g_quitting &&
	    (Aeron_QuitRequested() || xvt_frontend_task_should_quit())) {
		g_quitting = 1;
		xvt_network_task_shutdown();
		net_shutdown_direct_play_session_for_quit();
	}
	return g_quitting && !AeronDplay_IsActive();
}

int xvt_port_get_exit_code(void)
{
	return Aeron_FatalErrorRequested() ? 1 : g_xvt_exit_code;
}

uint64_t xvt_port_next_wake_delay_us(void)
{
	uint64_t task;
	uint64_t delay;
	if (g_quitting) {
		return AeronDplay_NextWakeDelayUs();
	}
	if (g_xvt_paused) {
		return UINT64_MAX;
	}
	task = xvt_movie_task_is_active() ? xvt_movie_task_next_wake_delay_us()
	       : xvt_flight_task_is_active()
		       ? xvt_flight_task_next_wake_delay_us()
		       : xvt_frontend_task_next_wake_delay_us();
	delay = xvt_cd_task_next_wake_delay_us();
	if (delay < task) {
		task = delay;
	}
	delay = AeronDplay_NextWakeDelayUs();
	return delay < task ? delay : task;
}

void xvt_port_shutdown(void)
{
	xvt_presentation_require_classic();
	if (!g_xvt_initialized) {
		return;
	}
	xvt_render_snapshot_begin_frame();
	xvt_render_snapshot_set_scene_kind(XVT_SCENE_NONE);
	xvt_movie_task_shutdown();
	xvt_flight_task_shutdown();
	xvt_frontend_task_shutdown();
	AeronDplay_Shutdown();
	xvt_network_session_shutdown();
	xvt_input_shutdown();
	AeronCompat_SetJoystickSource(NULL, NULL);
	AeronCompat_SetRumbleProvider(NULL);
	AeronWinmm_Shutdown();
	xvt_presentation_shutdown();
	Aeron_SetRelativeMouseMode(0);
	Aeron_SetHostCursorVisible(1);
	Aeron_AudioSetPaused(0);
	xvt_render_snapshot_commit(g_game_time, 0, 0);
	xvt_time_reset();
	g_xvt_initialized = 0;
	g_xvt_paused = 0;
	g_xvt_rebase_clock = 0;
	g_settings_open = g_settings_requested = 0;
	XVT_LOG_INFO("port.stopped");
}
