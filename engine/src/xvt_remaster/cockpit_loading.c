#include "xvt_remaster/cockpit_loading.h"
#include "aeron/aeron.h"
#include "xvt_remaster/assets.h"
#include "xvt_remaster/config.h"
#include "xvt_remaster/crt.h"
#include "xvt_remaster/flight_map.h"
#include "xvt_remaster/hud_renderer.h"
#include "xvt_runtime/snapshot/cockpit_capture.h"
#include <string.h>

static uint64_t g_images, g_resources;
static int g_width, g_height, g_samples;

void XvtCockpitLoading_Reset(void) {
	g_images = g_resources = 0;
	g_width = g_height = g_samples = 0;
}

static int PrepareMapIcons(AeronCommandBuffer* cmd, const XvtRenderSnapshot* snapshot) {
	const XvtCockpitResources* resources = &snapshot->cockpit_resources;
	for (unsigned asset = 0; asset < snapshot->image_asset_count; ++asset) {
		const XvtSnapImageAsset* image = &snapshot->image_assets[asset];
		if (image->kind != XVT_IMAGE_ICO)
			continue;
		for (unsigned view = 0; view < 28; ++view) {
			if (!resources->definition.layout.descriptors[view].lfd_asset_id)
				continue;
			uint32_t palette[256];
			memcpy(palette, resources->palette, sizeof palette);
			memcpy(palette, resources->view_palette[view], sizeof resources->view_palette[view]);
			if (!XvtRemasterAssets_PrepareMapIcons(cmd, image->id, palette, 0) ||
				!XvtRemasterAssets_PrepareMapIcons(cmd, image->id, palette, 1))
				return 0;
		}
	}
	return 1;
}

int XvtCockpitLoading_Prepare(const XvtRenderSnapshot* snapshot, int width, int height) {
	const XvtCockpitResources* resources = &snapshot->cockpit_resources;
	int images_changed = g_images != snapshot->image_asset_generation;
	if (images_changed) {
		XvtHudRenderer_Invalidate();
		XvtHudAssets_Retire(snapshot);
	}
	if (!resources->valid) {
		g_images = snapshot->image_asset_generation;
		return 1;
	}
	uint64_t generation = resources->definition.resource_generation;
	int samples = XvtRemasterConfig_Effective()->msaa_samples;
	if (!images_changed && g_resources == generation && width == g_width && height == g_height &&
		samples == g_samples)
		return 1;
	AeronCommandBuffer* cmd = Aeron_AcquireCommandBuffer();
	if (!cmd)
		return 0;
	uint64_t started = Aeron_NowUs();
	int ok = XvtHudAssets_PrepareResources(cmd, resources) && PrepareMapIcons(cmd, snapshot) &&
			 XvtCrt_PrepareResources(cmd, resources) &&
			 XvtRemasterPreview_PrepareCrtResources(resources, width, height) &&
			 XvtRemasterFlight_PrepareResources(width, height) &&
			 XvtFlightMap_PrepareResources(width, height);
	if (!ok)
		Aeron_CancelCommandBuffer(cmd);
	else
		ok = Aeron_SubmitCommandBuffer(cmd);
	if (!ok) {
		XvtHudAssets_Abort();
		XvtRemasterAssets_Abort();
		return 0;
	}
	XvtHudAssets_Commit();
	XvtRemasterAssets_CommitImages();
	XvtCockpit_ResourcesPrepared(generation);
	g_images = snapshot->image_asset_generation;
	g_resources = generation;
	g_width = width;
	g_height = height;
	g_samples = samples;
	Aeron_LogDebug("xvt.remaster", "resident cockpit resources prepared in %.1f ms (%dx%d)",
				   (double)(Aeron_NowUs() - started) / 1000.0, width, height);
	return 1;
}
