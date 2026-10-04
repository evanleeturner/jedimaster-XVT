#include "xvt_runtime/snapshot/render_snapshot.h"

#include <string.h>

#include "aeron/aeron.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt_runtime/snapshot/render_capture.h"
#include "xvt_runtime/snapshot/render_frontend.h"
#include "xvt_runtime/snapshot/render_hud.h"

static struct xvt_render_snapshot g_slots[3];
static int g_initialized;
static int g_snapshot_open;
static int g_write_slot;
static int g_current_slot = -1;
static int g_previous_slot = -1;
static uint64_t g_snapshot_serial;
static xvt_scene_kind g_scene_kind;
static uint32_t g_draw_order;

void xvt_render_snapshot_init(void)
{
	if (g_initialized) {
		return;
	}
	memset(g_slots, 0, sizeof(g_slots));
	g_write_slot = 0;
	g_current_slot = -1;
	g_previous_slot = -1;
	g_snapshot_serial = 0;
	g_snapshot_open = 0;
	g_scene_kind = XVT_SCENE_NONE;
	g_initialized = 1;
	xvt_render_assets_init();
	xvt_render_capture_reset();
	xvt_render_frontend_init();
}

void xvt_render_snapshot_shutdown(void)
{
	xvt_render_assets_shutdown();
	xvt_render_capture_reset();
	g_initialized = 0;
	g_snapshot_open = 0;
	g_current_slot = -1;
	g_previous_slot = -1;
	g_scene_kind = XVT_SCENE_NONE;
}

void xvt_render_snapshot_begin_frame(void)
{
	struct xvt_render_snapshot *snapshot;
	if (!g_initialized || g_snapshot_open) {
		return;
	}
	xvt_render_assets_begin_frame();
	xvt_render_capture_begin_frame();
	snapshot = &g_slots[g_write_slot];
	snapshot->snapshot_serial = g_snapshot_serial;
	g_draw_order = 0;
	snapshot->scene_kind = g_scene_kind;
	snapshot->dropped_records = 0;
	snapshot->flight_valid = 0;
	snapshot->camera.valid = 0;
	snapshot->map.active = 0;
	snapshot->map.object_count = 0;
	snapshot->map.label_bytes = 0;
	snapshot->hyperspace.valid = 0;
	snapshot->hyperspace.count = 0;
	snapshot->cursor.visible = 0;
	snapshot->object_count = 0;
	snapshot->target_box_count = 0;
	snapshot->sprite_count = 0;
	snapshot->glyph_count = 0;
	snapshot->paint_count = 0;
	snapshot->copy_count = 0;
	snapshot->surface_event_count = 0;
	snapshot->preview_count = 0;
	snapshot->opt_asset_count = 0;
	snapshot->texture_asset_count = 0;
	snapshot->image_asset_count = 0;
	g_snapshot_open = 1;
}

void xvt_render_snapshot_set_scene_kind(xvt_scene_kind kind)
{
	if (!g_initialized) {
		return;
	}
	g_scene_kind = kind;
	if (g_snapshot_open) {
		g_slots[g_write_slot].scene_kind = kind;
	}
}

void xvt_render_snapshot_commit(int32_t game_time_ticks, int focused,
				int paused)
{
	struct xvt_render_snapshot *snapshot;
	int slot;
	if (!g_initialized || !g_snapshot_open) {
		return;
	}
	snapshot = &g_slots[g_write_slot];
	snapshot->game_time_ticks = game_time_ticks;
	snapshot->capture_host_us = Aeron_NowUs();
	snapshot->focused = focused != 0;
	snapshot->paused = paused != 0;
	snapshot->scene_kind = g_scene_kind;
	xvt_render_capture_commit(snapshot, xvt_render_snapshot_current());
	xvt_render_frontend_commit(snapshot);
	g_previous_slot = g_current_slot;
	xvt_render_assets_export(snapshot);
	g_current_slot = g_write_slot;
	/* Preserve both committed slots while the next task tick fills the writer. */
	for (slot = 0; slot < 3; ++slot) {
		if (slot != g_current_slot && slot != g_previous_slot) {
			g_write_slot = slot;
			break;
		}
	}
	++g_snapshot_serial;
	g_snapshot_open = 0;
}

const struct xvt_render_snapshot *xvt_render_snapshot_current(void)
{
	return g_current_slot >= 0 ? &g_slots[g_current_slot] : NULL;
}

struct xvt_render_snapshot *xvt_render_snapshot_writer(void)
{
	return g_initialized && g_snapshot_open ? &g_slots[g_write_slot] : NULL;
}

uint32_t xvt_render_snapshot_next_order(void)
{
	return g_snapshot_open ? g_draw_order++ : 0;
}

const struct xvt_render_snapshot *xvt_render_snapshot_previous(void)
{
	return g_previous_slot >= 0 ? &g_slots[g_previous_slot] : NULL;
}
