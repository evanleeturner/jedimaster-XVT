#include "xvt_remaster/xvt_remaster.h"

#include "aeron/aeron.h"
#include "aeron/compat/host.h"
#include "xvt_remaster/assets.h"
#include "xvt_remaster/cockpit_loading.h"
#include "xvt_remaster/component_animation.h"
#include "xvt_remaster/config.h"
#include "xvt_remaster/flight.h"
#include "xvt_remaster/flight_pipeline.h"
#include "xvt_remaster/frontend.h"
#include "xvt_remaster/hud_renderer.h"
#include "xvt_remaster/preview.h"
#include "xvt_remaster/view_mode.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt_runtime/snapshot/render_snapshot.h"

static int g_initialized;
static uint32_t g_last_scene;
static int g_frontend_surfaces_released;

int xvt_remaster_init(void)
{
	if (g_initialized) {
		return 1;
	}
	int width;
	int height;
	if (!Aeron_GetLogicalSize(&width, &height) || width <= 0 ||
	    height <= 0) {
		XVT_LOG_ERROR("remaster.host_missing");
		return 0;
	}
	if (!xvt_remaster_config_sync()) {
		return 0;
	}
	if (!xvt_remaster_assets_init()) {
		xvt_remaster_assets_shutdown();
		xvt_remaster_config_shutdown();
		return 0;
	}
	g_last_scene = XVT_SCENE_NONE;
	g_frontend_surfaces_released = 0;
	xvt_remaster_view_init();
	g_initialized = 1;
	XVT_LOG_INFO("remaster.ready");
	return 1;
}

void xvt_remaster_begin_frame(const struct AeronInputSnapshot *input)
{
	if (!g_initialized) {
		return;
	}
	if (!xvt_remaster_config_sync()) {
		Aeron_RequestFatalRendererError(
			"rendering configuration update");
		return;
	}
	xvt_remaster_view_begin_frame(input);
}

/* Runs the remaster renderer for one frame: uploads assets until they are ready, prepares the flight
 * view and the cockpit, records and submits the presentation, then presents. It stays one function
 * because each step decides from flags the earlier steps set (assets ready, world needed, frontend
 * replay, HUD dirty, direct). */
void xvt_remaster_frame(int32_t delta_us)
{
	if (!g_initialized) {
		return;
	}
	const struct xvt_render_snapshot *snapshot =
		xvt_render_snapshot_current();
	if (!snapshot) {
		return;
	}
	xvt_component_animation_prepare(snapshot);
	xvt_remaster_preview_begin_frame();
	if (snapshot->scene_kind != g_last_scene) {
		xvt_remaster_flight_invalidate();
		g_last_scene = snapshot->scene_kind;
		XVT_LOG_DEBUG("remaster.scene kind=%u", g_last_scene);
	}
	int width = 0;
	int height = 0;
	if (!Aeron_GetPresentationPixelSize(&width, &height) || width <= 0 ||
	    height <= 0) {
		xvt_remaster_flight_invalidate();
		return;
	}
	int world_needed = xvt_remaster_view_needs_world(snapshot);
	int loading_assets = snapshot->cockpit_resources.valid &&
			     !xvt_hud_assets_has_resources(
				     snapshot->cockpit_resources.definition
					     .resource_generation);
	int prepare_assets = world_needed || loading_assets;
	int frontend_replay =
		xvt_frontend_needs_replay(snapshot, width, height);
	int assets_ready = 1;
	if ((prepare_assets || frontend_replay) &&
	    (xvt_remaster_assets_images_need_sync(snapshot) ||
	     (frontend_replay &&
	      xvt_frontend_assets_need_preparation(snapshot)) ||
	     (prepare_assets &&
	      (xvt_remaster_ship_assets_need_sync(snapshot) ||
	       xvt_remaster_assets_textures_need_sync(snapshot))))) {
		xvt_hud_renderer_invalidate();
		do {
			AeronCommandBuffer *cmd = Aeron_AcquireCommandBuffer();
			if (!cmd) {
				Aeron_RequestFatalRendererError(
					"asset upload command buffer");
				return;
			}
			xvt_asset_sync_result result = XVT_ASSET_SYNC_FAILED;
			if (xvt_remaster_assets_sync_images(cmd, snapshot)) {
				result =
					prepare_assets
						? xvt_remaster_ship_sync_assets(
							  cmd, snapshot,
							  64u * 1024u * 1024u,
							  4096)
						: XVT_ASSET_SYNC_COMPLETE;
				if (prepare_assets &&
				    result == XVT_ASSET_SYNC_COMPLETE &&
				    !xvt_remaster_assets_sync_textures(
					    cmd, snapshot)) {
					result = XVT_ASSET_SYNC_FAILED;
				}
				if (frontend_replay &&
				    result != XVT_ASSET_SYNC_FAILED &&
				    !xvt_frontend_prepare_assets(cmd,
								 snapshot)) {
					result = XVT_ASSET_SYNC_FAILED;
				}
			}
			if (result == XVT_ASSET_SYNC_FAILED) {
				Aeron_CancelCommandBuffer(cmd);
				xvt_remaster_assets_abort();
				Aeron_RequestFatalRendererError(
					"original asset synchronization");
				return;
			}
			if (!Aeron_SubmitCommandBuffer(cmd)) {
				xvt_remaster_assets_abort();
				Aeron_RequestFatalRendererError(
					"asset upload submission");
				return;
			}
			xvt_remaster_assets_commit_images();
			xvt_remaster_ship_commit_sync_batch();
			xvt_remaster_assets_commit_textures();
			assets_ready = result == XVT_ASSET_SYNC_COMPLETE;
			/* Frontend previews must be available before their once-only ordered replay.
			 * Submit the existing bounded upload batches before consuming that stream. */
		} while (!assets_ready && snapshot->preview_count);
	}
	int resources_ready = assets_ready;
	/* From here assets_ready also requires the flight world: it means the flight view may be prepared. */
	assets_ready = assets_ready && world_needed;
	if (assets_ready &&
	    !xvt_remaster_flight_prepare(
		    snapshot, xvt_render_snapshot_previous(), width, height)) {
		Aeron_RequestFatalRendererError("flight view preparation");
		return;
	}
	const struct xvt_prepared_flight *frame =
		assets_ready ? xvt_remaster_flight_current() : NULL;
	if (frame) {
		width = frame->view.camera.viewport.width;
		height = frame->view.camera.viewport.height;
	}
	if (resources_ready &&
	    (prepare_assets || !snapshot->cockpit_resources.valid) &&
	    !xvt_cockpit_loading_prepare(snapshot, width, height)) {
		Aeron_RequestFatalRendererError(
			"cockpit resource preparation during loading");
		return;
	}
	if (!assets_ready) {
		xvt_remaster_flight_invalidate();
	}
	xvt_flight_pipeline_set_direct(0, width, height);
	int direct = assets_ready && xvt_remaster_view_try_enable_direct(
					     snapshot, width, height);
	int standalone = world_needed && !snapshot->flight_valid &&
			 snapshot->cockpit.valid &&
			 snapshot->presented_target == XVT_TARGET_FLIGHT_MAIN;
	int crt_dirty = frame && xvt_remaster_preview_crt_needs_render(
					 snapshot, width, height);
	int crt_visible = frame && snapshot->cockpit.crt.valid &&
			  (crt_dirty || xvt_remaster_preview_crt_linear());
	int hud_dirty = (frame || standalone) &&
			xvt_hud_renderer_needs_preparation(
				&snapshot->cockpit, snapshot->world_generation,
				frame ? snapshot->target_boxes : NULL,
				frame ? snapshot->target_box_count : 0,
				frame ? &frame->view : NULL, crt_visible, width,
				height);
	if (frame && (hud_dirty || crt_dirty)) {
		xvt_remaster_flight_request_composition();
	}
	int render_flight = frame && (frame->render_needed ||
				      !xvt_remaster_flight_output());
	if (render_flight || (standalone && hud_dirty) || frontend_replay ||
	    xvt_flight_pipeline_needs_retain()) {
		AeronCommandBuffer *cmd = Aeron_AcquireCommandBuffer();
		if (!cmd) {
			Aeron_RequestFatalRendererError(
				"presentation command buffer");
			return;
		}
		int ok = 1;
		if (render_flight) {
			ok = xvt_remaster_preview_render_crt(cmd, snapshot,
							     width, height);
		}
		if (ok && (render_flight || (standalone && hud_dirty))) {
			ok = xvt_hud_renderer_prepare(
				cmd, &snapshot->cockpit,
				snapshot->world_generation,
				frame ? snapshot->target_boxes : NULL,
				frame ? snapshot->target_box_count : 0,
				frame ? &frame->view : NULL,
				frame ? xvt_remaster_preview_crt_linear()
				      : NULL,
				width, height);
		}
		if (ok && render_flight) {
			ok = xvt_remaster_flight_render(
				cmd, snapshot, xvt_render_snapshot_previous());
		}
		if (ok && standalone && hud_dirty) {
			ok = xvt_flight_pipeline_draw_standalone_hud(cmd, width,
								     height);
		}
		if (ok && !direct) {
			ok = xvt_flight_pipeline_retain(cmd);
		}
		if (ok && frontend_replay) {
			ok = xvt_remaster_preview_render(cmd, snapshot, width,
							 height) &&
			     xvt_frontend_replay(cmd, snapshot, width, height);
		}
		if (!ok) {
			Aeron_CancelCommandBuffer(cmd);
			xvt_hud_renderer_invalidate();
			xvt_remaster_preview_invalidate_crt();
			Aeron_RequestFatalRendererError(
				"presentation recording");
			return;
		}
		if (!Aeron_SubmitCommandBuffer(cmd)) {
			xvt_hud_renderer_invalidate();
			xvt_remaster_preview_invalidate_crt();
			Aeron_RequestFatalRendererError(
				"presentation submission");
			return;
		}
		xvt_remaster_assets_commit_images();
	}
	/* Finish the last ordered frontend replay before retiring its GPU sources. */
	if (snapshot->frontend_surfaces_released &&
	    !g_frontend_surfaces_released) {
		xvt_frontend_release_for_flight();
		xvt_remaster_preview_release_frontend();
	}
	g_frontend_surfaces_released = snapshot->frontend_surfaces_released;
	xvt_render_assets_consumed(snapshot->snapshot_serial);
	xvt_remaster_view_present(snapshot, delta_us,
				  assets_ready && snapshot->cockpit.valid &&
					  xvt_remaster_flight_output() != NULL,
				  direct);
}

void xvt_remaster_shutdown(void)
{
	xvt_component_animation_reset();
	xvt_remaster_view_shutdown();
	xvt_frontend_shutdown();
	xvt_remaster_preview_shutdown();
	xvt_remaster_flight_shutdown();

	xvt_hud_renderer_shutdown();
	xvt_cockpit_loading_reset();
	xvt_remaster_assets_shutdown();
	xvt_remaster_config_shutdown();
	if (!g_initialized) {
		return;
	}
	AeronDx5_SetClassicFlightRenderingSuppressed(0);
	g_initialized = 0;
	g_last_scene = XVT_SCENE_NONE;
	XVT_LOG_INFO("remaster.stopped");
}

struct AeronTexture *xvt_remaster_output(void)
{
	const struct xvt_render_snapshot *s = xvt_render_snapshot_current();
	if (!s || s->scene_kind == XVT_SCENE_MOVIE) {
		return NULL;
	}
	/* The successful presentation survives task teardown and invalid cameras. */
	if (s->presented_target != XVT_TARGET_FLIGHT_MAIN) {
		return xvt_frontend_output();
	}
	return xvt_flight_pipeline_output();
}

struct AeronTexture *xvt_remaster_movie_overlay(void)
{
	const struct xvt_render_snapshot *s = xvt_render_snapshot_current();
	return s && s->scene_kind == XVT_SCENE_MOVIE
		       ? xvt_frontend_movie_overlay()
		       : NULL;
}
