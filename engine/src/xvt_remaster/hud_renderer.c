#include "xvt_remaster/hud_renderer.h"
#include "aeron/aeron.h"
#include "xvt_remaster/crt.h"
#include "xvt_remaster/hud_instruments.h"
#include "xvt_remaster/hud_panes.h"
#include <string.h>

typedef struct HudPreparationKey {
	uint64_t definition, palette, artwork, instruments, radar, text, crt;
	XvtRenderView view;
	unsigned marker_count;
	XvtSnapTargetBox markers[XVT_SNAP_TARGET_BOXES];
	int width, height;
	uint8_t crt_visible;
} HudPreparationKey;

static AeronDrawList2D *g_before, *g_after;
static XvtHudLayoutCache g_layout;
static HudPreparationKey g_key;
static uint64_t g_worldGeneration;
static int g_prepared, g_ready;

void XvtHudRenderer_Invalidate(void) {
	g_prepared = g_ready = 0;
	memset(&g_layout, 0, sizeof g_layout);
}

void XvtHudRenderer_Shutdown(void) {
	AeronDrawList_Destroy(g_before);
	AeronDrawList_Destroy(g_after);
	g_before = g_after = NULL;
	XvtHudAssets_Shutdown();
	XvtCrt_Shutdown();
	XvtHudRenderer_Invalidate();
}

static int SelectAssets(const XvtCockpitState* state, int has_view) {
	if (!has_view) {
		memset(&g_layout, 0, sizeof g_layout);
		g_layout.layout.source_width = state->view.screen_width;
		g_layout.layout.source_height = state->view.screen_height;
		return XvtHudAssets_Select(state, &g_layout.layout);
	}
	return XvtHudLayout_Update(&g_layout, state) && XvtHudAssets_Select(state, &g_layout.layout);
}

static HudPreparationKey MakePreparationKey(const XvtCockpitState* state, const XvtSnapTargetBox* markers,
											unsigned marker_count, const XvtRenderView* view, int width,
											int height, int crt_visible) {
	HudPreparationKey key = { 0 };
	key.definition = state->definition_generation;
	key.palette = state->palette_generation;
	key.artwork = state->artwork_generation;
	key.instruments = state->instruments_generation;
	key.radar = state->radar_generation;
	key.text = state->text_generation;
	key.crt = state->crt_generation;
	key.width = width;
	key.height = height;
	key.crt_visible = crt_visible;
	if (view)
		key.view = *view;
	key.marker_count = marker_count;
	for (unsigned index = 0; index < marker_count; ++index) {
		const XvtSnapTargetBox* marker = &markers[index];
		key.markers[index].object = marker->object;
		key.markers[index].component = marker->component;
		key.markers[index].color_index = marker->color_index;
		key.markers[index].layer = marker->layer;
		key.markers[index].extent = marker->extent;
		memcpy(key.markers[index].world_pos, marker->world_pos, sizeof marker->world_pos);
	}
	return key;
}

int XvtHudRenderer_Prepare(AeronCommandBuffer* cmd, const XvtCockpitState* state, uint64_t world_generation,
						   const XvtSnapTargetBox* markers, unsigned marker_count, const XvtRenderView* view,
						   AeronTexture* crt_color, int width, int height) {
	g_ready = 0;
	if (!cmd || !state || width <= 0 || height <= 0 || marker_count > XVT_SNAP_TARGET_BOXES ||
		(marker_count && !markers))
		return 0;
	if (!state->valid) {
		XvtHudRenderer_Invalidate();
		return 1;
	}
	if (world_generation != g_worldGeneration) {
		XvtHudRenderer_Invalidate();
		g_worldGeneration = world_generation;
	}
	/* The bounded snapshot can emit three records per glyph plus artwork,
	 * instruments and up to eight line records per world marker. */
	if (!g_before)
		g_before = AeronDrawList_Create(65536);
	if (!g_after)
		g_after = AeronDrawList_Create(65536);
	if (!g_before || !g_after || !SelectAssets(state, view != NULL))
		goto failed;
	XvtHudDraw draw = { .before = g_before,
						.after = g_after,
						.state = state,
						.layout = &g_layout.layout,
						.assets = XvtHudAssets_Current(),
						.width = width,
						.height = height };
	XvtHudLayout_Fit(draw.layout, width, height, &draw.scale, &draw.offset_x, &draw.offset_y);
	int crt_visible = state->crt.valid && crt_color;
	if (!XvtCrt_PrepareView(&state->crt, &state->definition.layout, crt_visible ? crt_color : NULL, width,
							height, draw.scale, draw.offset_x, draw.offset_y))
		goto failed;
	HudPreparationKey key =
		MakePreparationKey(state, markers, marker_count, view, width, height, crt_visible);
	if (g_prepared && !memcmp(&g_key, &key, sizeof key)) {
		g_ready = 1;
		return 1;
	}
	g_prepared = 0;
	AeronDrawList_Begin(g_before, NULL, width, height, AERON_DRAWLIST2D_LOAD, NULL);
	AeronDrawList_Begin(g_after, NULL, width, height, AERON_DRAWLIST2D_LOAD, NULL);
	XvtHudInstruments_DrawWorldMarkers(&draw, markers, marker_count, view);
	XvtHudDraw_Base(&draw);
	XvtHudInstruments_DrawCovers(&draw);
	XvtHudInstruments_DrawRadar(&draw);
	XvtHudInstruments_DrawWidgets(&draw);
	if (!XvtHudPanes_DrawReadouts(&draw, XVT_COCKPIT_BEFORE_CRT))
		goto failed;
	if (crt_visible)
		XvtHudInstruments_DrawCrtMarker(&draw);
	XvtHudInstruments_DrawMouseStick(&draw, view);
	if (!XvtHudPanes_DrawReadouts(&draw, XVT_COCKPIT_AFTER_CRT) || !XvtHudPanes_DrawMessages(&draw) ||
		!XvtHudPanes_DrawPages(&draw) || !XvtHudPanes_DrawOverlays(&draw) ||
		!AeronDrawList_Prepare(g_before, cmd) || !AeronDrawList_Prepare(g_after, cmd))
		goto failed;
	g_key = key;
	g_prepared = g_ready = 1;
	return 1;
failed:
	g_prepared = 0;
	Aeron_CommandBufferSetFailure(cmd, "semantic cockpit preparation failed");
	return 0;
}

int XvtHudRenderer_NeedsPreparation(const XvtCockpitState* state, uint64_t world_generation,
									const XvtSnapTargetBox* markers, unsigned marker_count,
									const XvtRenderView* view, int crt_visible, int width, int height) {
	if (!state || marker_count > XVT_SNAP_TARGET_BOXES)
		return 1;
	if (!state->valid)
		return g_ready;
	if (!g_prepared || world_generation != g_worldGeneration)
		return 1;
	HudPreparationKey key =
		MakePreparationKey(state, markers, marker_count, view, width, height, crt_visible);
	return memcmp(&g_key, &key, sizeof key) != 0;
}

void XvtHudRenderer_Draw(AeronCommandBuffer* cmd, AeronRenderPass* pass, AeronRenderTarget* target) {
	if (!g_ready)
		return;
	AeronDrawList_RenderIntoPass(g_before, cmd, pass, target);
	XvtCrt_Draw(pass);
	AeronDrawList_RenderIntoPass(g_after, cmd, pass, target);
}
