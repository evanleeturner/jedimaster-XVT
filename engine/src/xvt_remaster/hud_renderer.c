#include "xvt_remaster/hud_renderer.h"

#include <string.h>

#include "aeron/aeron.h"
#include "xvt_remaster/crt.h"
#include "xvt_remaster/hud_instruments.h"
#include "xvt_remaster/hud_panes.h"

struct hud_preparation_key {
	uint64_t definition;
	uint64_t palette;
	uint64_t artwork;
	uint64_t instruments;
	uint64_t radar;
	uint64_t text;
	uint64_t crt;
	struct xvt_render_view view;
	unsigned marker_count;
	struct xvt_snap_target_box markers[XVT_SNAP_TARGET_BOXES];
	int width;
	int height;
	uint8_t crt_visible;
};

static AeronDrawList2D *g_before;
static AeronDrawList2D *g_after;
static struct xvt_hud_layout_cache g_layout;
static struct hud_preparation_key g_key;
static uint64_t g_world_generation;
static int g_prepared;
static int g_ready;

void xvt_hud_renderer_invalidate(void)
{
	g_prepared = g_ready = 0;
	memset(&g_layout, 0, sizeof g_layout);
}

void xvt_hud_renderer_shutdown(void)
{
	AeronDrawList_Destroy(g_before);
	AeronDrawList_Destroy(g_after);
	g_before = g_after = NULL;
	xvt_hud_assets_shutdown();
	xvt_crt_shutdown();
	xvt_hud_renderer_invalidate();
}

static int select_assets(const struct xvt_cockpit_state *state, int has_view)
{
	if (!has_view) {
		memset(&g_layout, 0, sizeof g_layout);
		g_layout.layout.source_width = state->view.screen_width;
		g_layout.layout.source_height = state->view.screen_height;
		return xvt_hud_assets_select(state, &g_layout.layout);
	}
	return xvt_hud_layout_update(&g_layout, state) &&
	       xvt_hud_assets_select(state, &g_layout.layout);
}

static struct hud_preparation_key
make_preparation_key(const struct xvt_cockpit_state *state,
		     const struct xvt_snap_target_box *markers,
		     unsigned marker_count, const struct xvt_render_view *view,
		     int width, int height, int crt_visible)
{
	struct hud_preparation_key key = {0};
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
	if (view) {
		key.view = *view;
	}
	key.marker_count = marker_count;
	for (unsigned index = 0; index < marker_count; ++index) {
		const struct xvt_snap_target_box *marker = &markers[index];
		key.markers[index].object = marker->object;
		key.markers[index].component = marker->component;
		key.markers[index].color_index = marker->color_index;
		key.markers[index].scope = marker->scope;
		key.markers[index].extent = marker->extent;
		memcpy(key.markers[index].world_pos, marker->world_pos,
		       sizeof marker->world_pos);
	}
	return key;
}

int xvt_hud_renderer_prepare(AeronCommandBuffer *cmd,
			     const struct xvt_cockpit_state *state,
			     uint64_t world_generation,
			     const struct xvt_snap_target_box *markers,
			     unsigned marker_count,
			     const struct xvt_render_view *view,
			     AeronTexture *crt_color, int width, int height)
{
	g_ready = 0;
	if (!cmd || !state || width <= 0 || height <= 0 ||
	    marker_count > XVT_SNAP_TARGET_BOXES ||
	    (marker_count && !markers)) {
		return 0;
	}
	if (!state->valid) {
		xvt_hud_renderer_invalidate();
		return 1;
	}
	if (world_generation != g_world_generation) {
		xvt_hud_renderer_invalidate();
		g_world_generation = world_generation;
	}
	/* The bounded snapshot can emit three records per glyph plus artwork,
	 * instruments and up to eight line records per world marker. */
	if (!g_before) {
		g_before = AeronDrawList_Create(65536);
	}
	if (!g_after) {
		g_after = AeronDrawList_Create(65536);
	}
	if (!g_before || !g_after || !select_assets(state, view != NULL)) {
		goto failed;
	}
	struct xvt_hud_draw draw = {.before = g_before,
				    .after = g_after,
				    .state = state,
				    .layout = &g_layout.layout,
				    .assets = xvt_hud_assets_current(),
				    .width = width,
				    .height = height};
	xvt_hud_layout_fit(draw.layout, width, height, &draw.scale,
			   &draw.offset_x, &draw.offset_y);
	int crt_visible = state->crt.valid && crt_color;
	if (!xvt_crt_prepare_view(&state->crt, &state->definition.layout,
				  crt_visible ? crt_color : NULL, width, height,
				  draw.scale, draw.offset_x, draw.offset_y)) {
		goto failed;
	}
	struct hud_preparation_key key = make_preparation_key(
		state, markers, marker_count, view, width, height, crt_visible);
	if (g_prepared && !memcmp(&g_key, &key, sizeof key)) {
		g_ready = 1;
		return 1;
	}
	g_prepared = 0;
	AeronDrawList_Begin(g_before, NULL, width, height,
			    AERON_DRAWLIST2D_LOAD, NULL);
	AeronDrawList_Begin(g_after, NULL, width, height, AERON_DRAWLIST2D_LOAD,
			    NULL);
	xvt_hud_instruments_draw_world_markers(&draw, markers, marker_count,
					       view);
	xvt_hud_draw_base(&draw);
	xvt_hud_instruments_draw_covers(&draw);
	xvt_hud_instruments_draw_radar(&draw);
	xvt_hud_instruments_draw_widgets(&draw);
	if (!xvt_hud_panes_draw_readouts(&draw, XVT_COCKPIT_BEFORE_CRT)) {
		goto failed;
	}
	if (crt_visible) {
		xvt_hud_instruments_draw_crt_marker(&draw);
	}
	xvt_hud_instruments_draw_mouse_stick(&draw, view);
	if (!xvt_hud_panes_draw_readouts(&draw, XVT_COCKPIT_AFTER_CRT) ||
	    !xvt_hud_panes_draw_messages(&draw) ||
	    !xvt_hud_panes_draw_pages(&draw) ||
	    !xvt_hud_panes_draw_overlays(&draw) ||
	    !AeronDrawList_Prepare(g_before, cmd) ||
	    !AeronDrawList_Prepare(g_after, cmd)) {
		goto failed;
	}
	g_key = key;
	g_prepared = g_ready = 1;
	return 1;
failed:
	g_prepared = 0;
	Aeron_CommandBufferSetFailure(cmd,
				      "semantic cockpit preparation failed");
	return 0;
}

int xvt_hud_renderer_needs_preparation(
	const struct xvt_cockpit_state *state, uint64_t world_generation,
	const struct xvt_snap_target_box *markers, unsigned marker_count,
	const struct xvt_render_view *view, int crt_visible, int width,
	int height)
{
	if (!state || marker_count > XVT_SNAP_TARGET_BOXES) {
		return 1;
	}
	if (!state->valid) {
		return g_ready;
	}
	if (!g_prepared || world_generation != g_world_generation) {
		return 1;
	}
	struct hud_preparation_key key = make_preparation_key(
		state, markers, marker_count, view, width, height, crt_visible);
	return memcmp(&g_key, &key, sizeof key) != 0;
}

void xvt_hud_renderer_draw(AeronCommandBuffer *cmd, AeronRenderPass *pass,
			   AeronRenderTarget *target)
{
	if (!g_ready) {
		return;
	}
	AeronDrawList_RenderIntoPass(g_before, cmd, pass, target);
	xvt_crt_draw(pass);
	AeronDrawList_RenderIntoPass(g_after, cmd, pass, target);
}
