#include "xvt_remaster/cockpit_loading.h"

#include <string.h>

#include "aeron/aeron.h"
#include "xvt_remaster/assets.h"
#include "xvt_remaster/config.h"
#include "xvt_remaster/crt.h"
#include "xvt_remaster/flight_map.h"
#include "xvt_remaster/hud_renderer.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/snapshot/cockpit_capture.h"

static uint64_t g_prepared_image_generation, g_prepared_resource_generation;
static int g_width, g_height, g_samples;

void xvt_cockpit_loading_reset(void)
{
	g_prepared_image_generation = g_prepared_resource_generation = 0;
	g_width = g_height = g_samples = 0;
}

static int prepare_map_icons(AeronCommandBuffer *cmd,
			     const struct xvt_render_snapshot *snapshot)
{
	const struct xvt_cockpit_resources *resources =
		&snapshot->cockpit_resources;
	for (unsigned asset = 0; asset < snapshot->image_asset_count; ++asset) {
		const struct xvt_snap_image_asset *image =
			&snapshot->image_assets[asset];
		if (image->kind != XVT_IMAGE_ICO) {
			continue;
		}
		for (unsigned view = 0; view < 28; ++view) {
			if (!resources->definition.layout.descriptors[view]
				     .lfd_asset_id) {
				continue;
			}
			uint32_t palette[256];
			memcpy(palette, resources->palette, sizeof palette);
			memcpy(palette, resources->view_palette[view],
			       sizeof resources->view_palette[view]);
			if (!xvt_remaster_assets_prepare_map_icons(
				    cmd, image->id, palette, 0) ||
			    !xvt_remaster_assets_prepare_map_icons(
				    cmd, image->id, palette, 1)) {
				return 0;
			}
		}
	}
	return 1;
}

int xvt_cockpit_loading_prepare(const struct xvt_render_snapshot *snapshot,
				int width, int height)
{
	const struct xvt_cockpit_resources *resources =
		&snapshot->cockpit_resources;
	int images_changed =
		g_prepared_image_generation != snapshot->image_asset_generation;
	if (images_changed) {
		xvt_hud_renderer_invalidate();
		xvt_hud_assets_retire(snapshot);
	}
	if (!resources->valid) {
		g_prepared_image_generation = snapshot->image_asset_generation;
		return 1;
	}
	uint64_t generation = resources->definition.resource_generation;
	int samples = xvt_remaster_config_effective()->msaa_samples;
	if (!images_changed && g_prepared_resource_generation == generation &&
	    width == g_width && height == g_height && samples == g_samples) {
		return 1;
	}
	AeronCommandBuffer *cmd = Aeron_AcquireCommandBuffer();
	if (!cmd) {
		return 0;
	}
	uint64_t started = Aeron_NowUs();
	int ok = xvt_hud_assets_prepare_resources(cmd, resources) &&
		 prepare_map_icons(cmd, snapshot) &&
		 xvt_crt_prepare_resources(cmd, resources) &&
		 xvt_remaster_preview_prepare_crt_resources(resources, width,
							    height) &&
		 xvt_remaster_flight_prepare_resources(width, height) &&
		 xvt_flight_map_prepare_resources(width, height);
	if (!ok) {
		Aeron_CancelCommandBuffer(cmd);
	} else {
		ok = Aeron_SubmitCommandBuffer(cmd);
	}
	if (!ok) {
		xvt_hud_assets_abort();
		xvt_remaster_assets_abort();
		return 0;
	}
	xvt_hud_assets_commit();
	xvt_remaster_assets_commit_images();
	xvt_cockpit_resources_prepared(generation);
	g_prepared_image_generation = snapshot->image_asset_generation;
	g_prepared_resource_generation = generation;
	g_width = width;
	g_height = height;
	g_samples = samples;
	XVT_LOG_DEBUG("remaster.cockpit_prepared ms=%.1f width=%d height=%d",
		      (double)(Aeron_NowUs() - started) / 1000.0, width,
		      height);
	return 1;
}
