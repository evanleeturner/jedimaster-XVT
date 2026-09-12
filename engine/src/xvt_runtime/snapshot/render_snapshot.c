#include "xvt_runtime/snapshot/render_snapshot.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt_runtime/snapshot/render_capture.h"
#include "xvt_runtime/snapshot/render_frontend.h"
#include "xvt_runtime/snapshot/render_hud.h"

#include "aeron/aeron.h"
#include <string.h>

static XvtRenderSnapshot g_slots[3];
static int g_initialized;
static int g_tickOpen;
static int g_writeSlot;
static int g_currentSlot = -1;
static int g_previousSlot = -1;
static uint64_t g_tickIndex;
static XvtSceneKind g_sceneKind;
static uint32_t g_drawOrder;

void XvtRenderSnapshot_Init(void) {
	if (g_initialized)
		return;
	memset(g_slots, 0, sizeof(g_slots));
	g_writeSlot = 0;
	g_currentSlot = -1;
	g_previousSlot = -1;
	g_tickIndex = 0;
	g_tickOpen = 0;
	g_sceneKind = XVT_SCENE_NONE;
	g_initialized = 1;
	XvtRenderAssets_Init();
	XvtRenderCapture_Init();
	XvtRenderFrontend_Init();
}

void XvtRenderSnapshot_Shutdown(void) {
	XvtRenderAssets_Shutdown();
	XvtRenderCapture_Init();
	g_initialized = 0;
	g_tickOpen = 0;
	g_currentSlot = -1;
	g_previousSlot = -1;
	g_sceneKind = XVT_SCENE_NONE;
}

void XvtRenderSnapshot_BeginTick(void) {
	XvtRenderSnapshot* snapshot;
	if (!g_initialized || g_tickOpen)
		return;
	XvtRenderAssets_BeginTick();
	XvtRenderCapture_BeginTick();
	snapshot = &g_slots[g_writeSlot];
	snapshot->tick_index = g_tickIndex;
	g_drawOrder = 0;
	snapshot->scene_kind = g_sceneKind;
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
	g_tickOpen = 1;
}

void XvtRenderSnapshot_SetSceneKind(XvtSceneKind kind) {
	if (!g_initialized)
		return;
	g_sceneKind = kind;
	if (g_tickOpen)
		g_slots[g_writeSlot].scene_kind = kind;
}

void XvtRenderSnapshot_Commit(int32_t game_time_ticks, int focused, int paused) {
	XvtRenderSnapshot* snapshot;
	int slot;
	if (!g_initialized || !g_tickOpen)
		return;
	snapshot = &g_slots[g_writeSlot];
	snapshot->game_time_ticks = game_time_ticks;
	snapshot->capture_host_us = Aeron_NowUs();
	snapshot->focused = focused != 0;
	snapshot->paused = paused != 0;
	snapshot->scene_kind = g_sceneKind;
	XvtRenderCapture_Commit(snapshot, XvtRenderSnapshot_Current());
	XvtRenderFrontend_Commit(snapshot);
	g_previousSlot = g_currentSlot;
	XvtRenderAssets_Export(snapshot);
	g_currentSlot = g_writeSlot;
	/* Preserve both committed slots while the next task tick fills the writer. */
	for (slot = 0; slot < 3; ++slot) {
		if (slot != g_currentSlot && slot != g_previousSlot) {
			g_writeSlot = slot;
			break;
		}
	}
	++g_tickIndex;
	g_tickOpen = 0;
}

const XvtRenderSnapshot* XvtRenderSnapshot_Current(void) {
	return g_currentSlot >= 0 ? &g_slots[g_currentSlot] : NULL;
}

XvtRenderSnapshot* XvtRenderSnapshot_Writer(void) {
	return g_initialized && g_tickOpen ? &g_slots[g_writeSlot] : NULL;
}

uint32_t XvtRenderSnapshot_NextOrder(void) { return g_tickOpen ? g_drawOrder++ : 0; }

const XvtRenderSnapshot* XvtRenderSnapshot_Previous(void) {
	return g_previousSlot >= 0 ? &g_slots[g_previousSlot] : NULL;
}
