#include "xvt_remaster/view_mode.h"

#include "aeron/compat/host.h"
#include "aeron/scene/blend_ramp.h"
#include "xvt_remaster/flight.h"
#include "xvt_remaster/flight_pipeline.h"
#include "xvt_remaster/frontend.h"
#include "xvt_remaster/hud_renderer.h"
#include "xvt_remaster/preview.h"
#include "xvt_remaster/xvt_remaster.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/input_bridge.h"
#include "xvt_runtime/input/keyboard_mapping.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/movie_task.h"
#include "xvt_runtime/runtime/presentation.h"

static AeronBlendRamp g_blend;
static int g_wait_classic;
static int g_consumed_tab;
static int g_world_ready;
static uint64_t g_classic_serial;
static uint64_t g_ready_flight_frame_serial;
static uint64_t g_ready_mission_generation;
static int g_width;
static int g_height;

enum { DISPLAY_NONE, DISPLAY_FRONTEND, DISPLAY_FLIGHT, DISPLAY_LOADING };

static unsigned g_display;

void xvt_remaster_view_init(void)
{
	Aeron_BlendRampInit(&g_blend);
	g_blend.alpha = 0;
	g_wait_classic = 0;
	g_consumed_tab = 0;
	g_world_ready = 0;
	g_ready_flight_frame_serial = 0;
	g_ready_mission_generation = 0;
	g_classic_serial = 0;
	g_width = 0;
	g_height = 0;
	g_display = DISPLAY_NONE;
	xvt_input_suppress_renderer_tab(0);
}

static int flight_ready(const struct xvt_render_snapshot *s)
{
	return s && s->flight_valid && s->scene_kind == XVT_SCENE_FLIGHT &&
	       s->presented_target == XVT_TARGET_FLIGHT_MAIN && g_world_ready &&
	       s->flight_frame_serial == g_ready_flight_frame_serial &&
	       s->mission_generation == g_ready_mission_generation;
}

void xvt_remaster_view_begin_frame(const AeronInputSnapshot *in)
{
	const struct xvt_render_snapshot *s = xvt_render_snapshot_current();
	if (in) {
		xvt_presentation_sync_to_window(in->window_width,
						in->window_height);
	}
	if (g_consumed_tab && in && !in->key_down[AERON_KEY_TAB] &&
	    !in->key_released[AERON_KEY_TAB]) {
		g_consumed_tab = 0;
	}
	if (in && !g_consumed_tab && in->has_focus &&
	    xvt_keyboard_mapping_find_shortcut_press(
		    in, XVT_KEYBOARD_SHORTCUT_RENDERER) >= 0 &&
	    s && s->scene_kind != XVT_SCENE_NONE &&
	    s->scene_kind != XVT_SCENE_MOVIE && xvt_remaster_output() &&
	    xvt_input_renderer_shortcut_allowed()) {
		g_consumed_tab = 1;
		Aeron_BlendRampToggle(&g_blend);
		if (g_blend.target > 0) {
			g_wait_classic = 0;
			g_world_ready = 0;
			xvt_remaster_flight_invalidate();
			xvt_hud_renderer_invalidate();
			xvt_remaster_preview_invalidate_crt();
		} else if (AeronDx5_IsClassicFlightRenderingSuppressed()) {
			g_wait_classic = 1;
			g_classic_serial =
				AeronDx5_GetClassicFlightFrameSerial();
		}
		XVT_LOG_INFO("remaster.renderer mode=\"%s\"",
			     g_blend.target > 0 ? "modern" : "classic");
	}
	xvt_input_suppress_renderer_tab(g_consumed_tab);
	int width = 0;
	int height = 0;
	Aeron_GetPresentationPixelSize(&width, &height);
	int suppress = in && in->has_focus && g_blend.target > 0 &&
		       g_blend.alpha >= 1 && !g_wait_classic &&
		       flight_ready(s) && width == g_width &&
		       height == g_height;
	AeronDx5_SetClassicFlightRenderingSuppressed(suppress);
}

int xvt_remaster_view_needs_world(const struct xvt_render_snapshot *s)
{
	return !(s->presented_target == XVT_TARGET_FLIGHT_MAIN &&
		 g_blend.target == 0 && g_blend.alpha == 0 && !g_wait_classic);
}

int xvt_remaster_view_try_enable_direct(const struct xvt_render_snapshot *s,
					int width, int height)
{
	return s->flight_valid && s->scene_kind == XVT_SCENE_FLIGHT &&
	       g_world_ready &&
	       s->mission_generation == g_ready_mission_generation &&
	       g_blend.target > 0 && g_blend.alpha >= 1 && !g_wait_classic &&
	       AeronDx5_IsClassicFlightRenderingSuppressed() &&
	       xvt_flight_pipeline_set_direct(1, width, height);
}

void xvt_remaster_view_present(const struct xvt_render_snapshot *s,
			       int32_t delta_us, int world_ready, int direct)
{
	if (world_ready && s->flight_valid) {
		g_world_ready = 1;
		g_ready_flight_frame_serial = s->flight_frame_serial;
		g_ready_mission_generation = s->mission_generation;
		const struct xvt_prepared_flight *frame =
			xvt_remaster_flight_current();
		if (frame) {
			g_width = frame->view.camera.viewport.width;
			g_height = frame->view.camera.viewport.height;
		}
	}
	if (g_wait_classic &&
	    AeronDx5_GetClassicFlightFrameSerial() != g_classic_serial) {
		g_wait_classic = 0;
	}
	if (s->scene_kind == XVT_SCENE_MOVIE) {
		AeronTexture *overlay = xvt_remaster_movie_overlay();
		if (g_blend.target > 0 && overlay) {
			AeronTextureLayerDesc layer = {
				.texture = overlay,
				.logical_rect = xvt_presentation_classic_rect(),
				.blend_mode = AERON_LAYER_BLEND_PREMULTIPLIED,
				.color_space = AERON_COLOR_SPACE_SRGB};
			if (!Aeron_SubmitTextureLayer(&layer)) {
				Aeron_RequestFatalRendererError(
					"movie overlay presentation");
			} else {
				xvt_movie_task_suppress_classic_subtitles();
			}
		}
		return;
	}
	int requested_flight = s->presented_target == XVT_TARGET_FLIGHT_MAIN &&
			       s->presented_scene == XVT_SCENE_FLIGHT;
	int ready = xvt_remaster_output() &&
		    (!requested_flight || flight_ready(s) || !s->flight_valid);
	if (ready) {
		g_display = requested_flight ? DISPLAY_FLIGHT
			    : s->presented_target == XVT_TARGET_FLIGHT_MAIN
				    ? DISPLAY_LOADING
				    : DISPLAY_FRONTEND;
	}
	/* Select retained owners, not borrowed pointers that resize can replace. */
	if (g_display != DISPLAY_FRONTEND) {
		xvt_frontend_release_presented();
	}
	AeronTexture *output =
		g_display == DISPLAY_FLIGHT	? xvt_flight_pipeline_output()
		: g_display == DISPLAY_FRONTEND ? xvt_frontend_output()
		: g_display == DISPLAY_LOADING	? xvt_flight_pipeline_output()
						: NULL;
	int flight = g_display == DISPLAY_FLIGHT;
	float target =
		g_wait_classic ? 1 : (g_blend.target > 0 && ready ? 1 : 0);
	if (!ready && AeronDx5_IsClassicFlightRenderingSuppressed()) {
		/* Keep the last complete modern frame opaque while classic resumes. */
		target = 1;
		g_world_ready = 0;
		AeronDx5_SetClassicFlightRenderingSuppressed(0);
	}
	Aeron_BlendRampAdvance(&g_blend, delta_us, target);
	if (g_blend.alpha <= 0) {
		xvt_frontend_release_presented();
		return;
	}
	if (!output) {
		return;
	}
	if (direct && g_blend.alpha >= 1) {
		if (!xvt_flight_pipeline_submit_direct()) {
			Aeron_RequestFatalRendererError(
				"direct flight presentation");
		}
		return;
	}
	AeronTextureLayerDesc layer = {
		.texture = output,
		.logical_rect = g_display == DISPLAY_FRONTEND
					? xvt_presentation_classic_rect()
					: xvt_presentation_logical_rect(),
		.blend_mode = AERON_LAYER_BLEND_PREMULTIPLIED,
		.color_space = flight ? AERON_COLOR_SPACE_LINEAR_SRGB
			       : g_display == DISPLAY_FRONTEND
				       ? AERON_COLOR_SPACE_SRGB
				       : AERON_COLOR_SPACE_LINEAR_DISPLAY,
		.tint_enabled = 1,
		.tint_rgba = {g_blend.alpha, g_blend.alpha, g_blend.alpha,
			      g_blend.alpha}};
	if (!Aeron_SubmitTextureLayer(&layer)) {
		Aeron_RequestFatalRendererError("modern texture presentation");
	} else if (g_display == DISPLAY_FRONTEND) {
		xvt_frontend_present_cursor(g_blend.alpha);
	}
}

void xvt_remaster_view_shutdown(void)
{
	AeronDx5_SetClassicFlightRenderingSuppressed(0);
	xvt_input_suppress_renderer_tab(0);
	g_wait_classic = 0;
	g_consumed_tab = 0;
	g_world_ready = 0;
}
