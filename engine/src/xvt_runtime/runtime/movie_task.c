#include "xvt_runtime/runtime/movie_task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/aeron.h"
#include "aeron/video.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/movie.h"
#include "xvt/input/keyboard.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/movie_sync.h"
#include "xvt_runtime/runtime/presentation.h"
#include "xvt_runtime/snapshot/render_frontend.h"
#include "xvt_runtime/storage/storage.h"

struct xvt_movie_task {
	AeronVideoPlayer *player;
	char name[256];
	char path[XVT_PATH_CAPACITY];
	char lines[3][256];
	uint16_t *overlay;
	uint64_t generation;
	unsigned int next_cue;
	int active;
	int complete;
	int result;
	int paused;
	int saved_mode;
	int saved_restore;
	int saved_lock;
	int synchronize;
};

static struct xvt_movie_task g_movie;
static AeronRenderSubmission g_subtitle_submission;

void xvt_movie_task_suppress_classic_subtitles(void)
{
	Aeron_CancelRenderSubmission(g_subtitle_submission);
	g_subtitle_submission = 0;
}

static void xvt_movie_task_close(void)
{
	if (g_movie.player) {
		Aeron_VideoClose(g_movie.player);
	}
	g_movie.player = NULL;
	if (g_movie_subtitle_file) {
		file_close(g_movie_subtitle_file);
	}
	g_movie_subtitle_file = NULL;
	free(g_movie.overlay);
	g_movie.overlay = NULL;
}

void xvt_movie_task_reap_finished(void)
{
	if (g_movie.active || !g_movie.player) {
		return;
	}
	/* Called at the next host frame, after the final decoder/overlay submission. */
	xvt_movie_task_close();
	frontend_display_set_wnd_proc_mode(g_movie.saved_mode);
	g_front_state.offscreen_restore_enabled = g_movie.saved_restore;
	if (g_movie.saved_lock) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	keyboard_flush_char_buffer();
	g_front_state.mouse_left_click_latch = 0;
	g_front_state.mouse_right_click_latch = 0;
}

int xvt_movie_task_begin(const char *name, int synchronize)
{
	AeronVideoOpenDesc desc = {0};
	char relative[XVT_PATH_CAPACITY];
	int found;
	if (!name || !*name || g_movie.active || g_movie.complete ||
	    g_movie.player) {
		return 2;
	}
	if (snprintf(g_movie.name, sizeof(g_movie.name), "%s", name) >=
	    (int)sizeof(g_movie.name)) {
		return 2;
	}
	snprintf(relative, sizeof(relative), "movies/%s.smk", g_movie.name);
	found = xvt_storage_resolve_asset(relative, g_movie.path,
					  sizeof(g_movie.path));
	if (found != 1 && synchronize &&
	    g_frontend_mission_session_mode !=
		    FRONTEND_MISSION_SESSION_SINGLEPLAYER) {
		strcpy(g_movie.name, "Flyby1a");
		found = xvt_storage_resolve_asset("movies/Flyby1a.smk",
						  g_movie.path,
						  sizeof(g_movie.path));
	}
	if (found != 1) {
		XVT_LOG_WARN("movie.open_failed name=\"%s\"", name);
		return 2;
	}
	g_movie.overlay = calloc(640 * 480, sizeof(*g_movie.overlay));
	if (!g_movie.overlay) {
		return 2;
	}
	desc.vfs = xvt_storage_vfs();
	desc.root = AERON_VFS_ROOT_ASSET;
	desc.path = g_movie.path;
	desc.autoplay = 1;
	desc.gain = g_game_config.sfx_datapad_enabled
			    ? (float)g_game_config.sfx_datapad_volume / 10.0f
			    : 0;
	g_movie.player = Aeron_VideoOpen(&desc);
	if (!g_movie.player) {
		xvt_movie_task_close();
		return 2;
	}
	snprintf(relative, sizeof(relative), "movies/%s.txt", g_movie.name);
	g_movie_subtitle_file = file_open(relative, "r");
	memset(g_movie.lines, 0, sizeof(g_movie.lines));
	g_movie.next_cue = 0;
	g_movie.saved_mode = frontend_display_get_wnd_proc_mode();
	g_movie.saved_restore = g_front_state.offscreen_restore_enabled;
	g_movie.saved_lock = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	frontend_display_disable_offscreen_restore();
	frontend_display_set_wnd_proc_mode(2);
	frontend_text_stop_text_fade();
	keyboard_flush_char_buffer();
	g_movie_skip_requested = 0;
	g_movie.synchronize =
		synchronize && g_frontend_mission_session_mode !=
				       FRONTEND_MISSION_SESSION_SINGLEPLAYER;
	g_movie_previous_wnd_proc_mode = g_movie.saved_mode;
	if (g_movie.synchronize) {
		xvt_movie_sync_begin();
	}
	xvt_presentation_require_classic();
	g_movie.active = 1;
	g_movie.paused = 0;
	g_movie.result = 0;
	XVT_LOG_INFO("movie.start path=\"%s\"", g_movie.path);
	return XVT_MOVIE_PENDING;
}

static void xvt_movie_task_submit_subtitles(uint64_t frame, int margin_height)
{
	AeronPixelLayerDesc layer = {0};
	struct RECT clip;
	struct RECT rect;
	uint8_t *saved_pixels;
	int saved_pitch;
	int line;
	int waiting = g_movie.synchronize && g_movie_playback_completion_state;
	if ((!g_movie_subtitle_file && !g_movie.synchronize) ||
	    margin_height <= 0) {
		return;
	}
	/* Each file record supplies the next boundary and the text for this interval. */
	while (!waiting && g_movie_subtitle_file &&
	       g_movie.next_cue != UINT16_MAX && g_movie.next_cue <= frame) {
		g_movie.next_cue = movie_read_subtitle_cue(
			g_movie.lines[0], g_movie.lines[1], g_movie.lines[2]);
		if (g_movie.next_cue == UINT16_MAX) {
			memset(g_movie.lines, 0, sizeof(g_movie.lines));
			break;
		}
	}
	memset(g_movie.overlay, 0, 640 * 480 * sizeof(uint16_t));
	saved_pixels = g_draw_surface_ptr;
	saved_pitch = g_front_state.draw_surface_pitch;
	frontend_display_get_screen_clip_rect(&clip);
	rect = (struct RECT){0, 0, 639, 479};
	frontend_display_set_screen_clip_rect640x480(&rect);
	g_draw_surface_ptr = (uint8_t *)g_movie.overlay;
	g_front_state.draw_surface_pitch = 640 * sizeof(uint16_t);
	rect.top = 480 - margin_height;
	for (line = 0; !waiting && line < 3; ++line) {
		rect.bottom = rect.top + margin_height / 3;
		frontend_text_draw_centered(12, g_movie.lines[line], &rect,
					    0xffff);
		rect.top = rect.bottom;
	}
	if (g_movie.synchronize) {
		xvt_movie_sync_draw(margin_height, margin_height);
	}
	g_draw_surface_ptr = saved_pixels;
	g_front_state.draw_surface_pitch = saved_pitch;
	frontend_display_set_screen_clip_rect640x480(&clip);
	layer.frame.pixels = g_movie.overlay;
	layer.frame.width = 640;
	layer.frame.height = 480;
	layer.frame.pitch = 1280;
	layer.frame.format = g_front_state.pixel_format555
				     ? AERON_PIXEL_FORMAT_RGB555
				     : AERON_PIXEL_FORMAT_RGB565;
	layer.frame.color_space = AERON_COLOR_SPACE_SRGB;
	layer.frame.generation = ++g_movie.generation;
	layer.logical_rect = xvt_presentation_classic_rect();
	layer.blend_mode = AERON_LAYER_BLEND_ALPHA;
	layer.color_key_enabled = 1;
	layer.color_key = 0;
	layer.preserve_encoded_values = 1;
	g_subtitle_submission = Aeron_SubmitPixelLayer(&layer);
	if (!g_subtitle_submission) {
		Aeron_RequestFatalRendererError("movie subtitles");
	}
}

static void xvt_movie_task_submit(void)
{
	AeronVideoPresentDesc desc = {0};
	AeronVideoInfo info;
	int width;
	int height;
	if (!Aeron_VideoGetInfo(g_movie.player, &info) || info.width <= 0 ||
	    info.height <= 0) {
		return;
	}
	width = info.width;
	height = info.height;
	/* Original Smacker playback centers the native frame in the 640x480 display. */
	if (width > 640 || height > 480) {
		g_movie.result = 3;
		g_movie.active = 0;
		g_movie.complete = 1;
		return;
	}
	desc.bounds = xvt_presentation_from_classic((AeronRectI){
		(640 - width) / 2, (480 - height) / 2, width, height});
	desc.scale_mode = AERON_VIDEO_SCALE_CONTAIN;
	desc.blend_mode = AERON_LAYER_BLEND_OPAQUE;
	/* A skipped movie has no video frame, but its synchronization UI remains active. */
	if (Aeron_VideoSubmit(g_movie.player, &desc) ||
	    (g_movie.synchronize && g_movie_playback_completion_state)) {
		xvt_render_frontend_movie(1);
		xvt_movie_task_submit_subtitles(
			Aeron_VideoGetPresentedFrameIndex(g_movie.player),
			(480 - height) / 2);
		xvt_render_frontend_movie(0);
	}
}

void xvt_movie_task_stop(void)
{
	if (!g_movie.active) {
		return;
	}
	/* Skipping finishes local playback successfully; multiplayer still waits for peers. */
	Aeron_VideoStop(g_movie.player);
	if (g_movie.synchronize) {
		xvt_movie_sync_report_finished();
	}
}

void xvt_movie_task_update(void)
{
	AeronVideoState state;
	int key;
	int synchronized = 0;
	if (!g_movie.active) {
		return;
	}
	if (g_movie.paused) {
		Aeron_VideoPlay(g_movie.player);
		g_movie.paused = 0;
	}
	Aeron_VideoUpdate(g_movie.player);
	key = (unsigned char)keyboard_dequeue_char();
	if (g_movie.synchronize) {
		uint32_t playing = 1;
		if (key) {
			movie_multiplayer_input_callback(0, 0x102, key, 0, 0,
							 &playing);
		}
		if (g_front_state.mouse_left_click_latch ||
		    g_front_state.mouse_right_click_latch) {
			movie_multiplayer_input_callback(0, 0x202, 0, 0, 0,
							 &playing);
		}
		if (!playing) {
			xvt_movie_task_stop();
		}
		synchronized = xvt_movie_sync_update();
	} else if (key || g_front_state.mouse_left_click_latch ||
		   g_front_state.mouse_right_click_latch) {
		xvt_movie_task_stop();
	}
	g_front_state.mouse_left_click_latch = 0;
	g_front_state.mouse_right_click_latch = 0;
	state = Aeron_VideoGetState(g_movie.player);
	if (state == AERON_VIDEO_ERROR) {
		if (g_movie.result != 2) {
			XVT_LOG_ERROR("movie.failed path=\"%s\" error=\"%s\"",
				      g_movie.path,
				      Aeron_VideoGetError(g_movie.player));
		}
		g_movie.result = 2;
	} else {
		xvt_movie_task_submit();
	}
	if (g_movie.synchronize &&
	    (state == AERON_VIDEO_ENDED || state == AERON_VIDEO_ERROR)) {
		xvt_movie_sync_report_finished();
	}
	if ((!g_movie.synchronize &&
	     (state == AERON_VIDEO_ENDED || state == AERON_VIDEO_ERROR)) ||
	    (g_movie.synchronize &&
	     (synchronized || g_movie_skip_requested == -1))) {
		if (g_movie_skip_requested == -1) {
			g_movie.result = 5;
		}
		g_movie.active = 0;
		g_movie.complete = 1;
		XVT_LOG_INFO("movie.stop name=\"%s\" result=%d", g_movie.name,
			     g_movie.result);
	}
}

void xvt_movie_task_paused_frame(void)
{
	if (!g_movie.active) {
		return;
	}
	if (!g_movie.paused) {
		Aeron_VideoPause(g_movie.player);
		g_movie.paused = 1;
	}
	xvt_movie_task_submit();
}

int xvt_movie_task_is_active(void) { return g_movie.active; }

int xvt_movie_task_continues_without_focus(void)
{
	return g_movie.active && g_movie.synchronize;
}

int xvt_movie_task_take_result(int *result)
{
	if (!g_movie.complete || g_movie.player) {
		return 0;
	}
	*result = g_movie.result;
	g_movie.complete = 0;
	return 1;
}

uint64_t xvt_movie_task_next_wake_delay_us(void)
{
	uint64_t delay;
	return g_movie.active && Aeron_VideoGetNextWakeDelayUs(g_movie.player,
							       &delay)
		       ? delay
		       : 10000;
}

void xvt_movie_task_shutdown(void)
{
	xvt_movie_task_close();
	memset(&g_movie, 0, sizeof(g_movie));
}
