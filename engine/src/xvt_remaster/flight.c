#include "xvt_remaster/flight.h"
#include "aeron/aeron.h"
#include "xvt/flight/hud/hud.h"
#include "xvt_remaster/config.h"
#include <string.h>

static XvtPreparedFlight g_frame;
static uint64_t g_lastMission, g_lastWorld, g_lastOpt, g_lastTexture;
static int g_width, g_height;
static uint64_t g_poseHostUs;
static uint64_t g_lastConfig, g_resizeSince;
static int g_requestedWidth, g_requestedHeight, g_lastHdr, g_lastPaused;
static float g_lastHeadroom;

static int ViewDiscontinuity(const XvtSnapCamera* a, const XvtSnapCamera* b) {
	return a->player.slot != b->player.slot || a->player.signature != b->player.signature ||
		   a->focus.slot != b->focus.slot || a->focus.signature != b->focus.signature ||
		   a->external != b->external || a->replay_view != b->replay_view || a->map_mode != b->map_mode ||
		   a->hud_state != b->hud_state || a->hyperspace_phase != b->hyperspace_phase ||
		   a->perspective_shift != b->perspective_shift || a->aspect_y_q16 != b->aspect_y_q16 ||
		   a->screen_width != b->screen_width || a->screen_height != b->screen_height ||
		   memcmp(&a->viewport, &b->viewport, sizeof a->viewport) ||
		   a->projection_offset_y != b->projection_offset_y;
}

void XvtRemasterFlight_Invalidate(void) { g_frame.valid = 0; }

void XvtRemasterFlight_RequestComposition(void) { g_frame.render_needed = 1; }

const XvtPreparedFlight* XvtRemasterFlight_Current(void) { return g_frame.valid ? &g_frame : NULL; }

int XvtRemasterFlight_Prepare(const XvtRenderSnapshot* s, const XvtRenderSnapshot* p, int width, int height) {
	if (!s || !s->flight_valid || !s->camera.valid) {
		XvtRemasterFlight_Invalidate();
		return 1;
	}
	uint64_t now = Aeron_NowUs();
	if (width != g_requestedWidth || height != g_requestedHeight) {
		g_requestedWidth = width;
		g_requestedHeight = height;
		g_resizeSince = now;
	}
	/* Keep the complete target during interactive resize, as in XWA. */
	if (g_frame.valid && (width != g_width || height != g_height) && now - g_resizeSince < 150000) {
		width = g_width;
		height = g_height;
	}
	uint64_t config = XvtRemasterConfig_Generation();
	int new_tick = !g_frame.valid || s->tick_index != g_frame.tick_index;
	int previous_valid = p && p->flight_valid && p->camera.valid &&
						 p->mission_generation == s->mission_generation &&
						 p->world_generation == s->world_generation;
	int reset = !g_frame.valid || (new_tick && !previous_valid) || g_width != width || g_height != height ||
				g_lastMission != s->mission_generation || g_lastWorld != s->world_generation ||
				g_lastOpt != s->opt_asset_generation || g_lastTexture != s->texture_asset_generation ||
				g_lastConfig != config;
	if (new_tick && previous_valid &&
		(s->view_time_ticks < p->view_time_ticks || ViewDiscontinuity(&s->camera, &p->camera)))
		reset = 1;
	int changed = !previous_valid || XvtRenderMath_PoseChanged(s, p);
	if (new_tick && previous_valid && s->hyperspace.phase == XVT_SNAP_HYPERSPACE_TRANSITION &&
		((s->hyperspace.elapsed_ticks < XVT_SNAP_HYPERSPACE_STREAK_END) !=
		 (p->hyperspace.elapsed_ticks < XVT_SNAP_HYPERSPACE_STREAK_END)))
		reset = 1;
	int advance =
		reset || (new_tick && (changed || (previous_valid && s->view_time_ticks != p->view_time_ticks)));
	int scene_changed =
		!advance && previous_valid &&
		(memcmp(&s->lighting, &p->lighting, sizeof s->lighting) || memcmp(&s->sky, &p->sky, sizeof s->sky) ||
		 memcmp(s->types, p->types, sizeof s->types) ||
		 memcmp(s->fuselage_sequence, p->fuselage_sequence, sizeof s->fuselage_sequence) ||
		 memcmp(&s->hyperspace, &p->hyperspace, sizeof s->hyperspace) ||
		 (s->camera.map_mode && memcmp(&s->map, &p->map, sizeof s->map)));
	if (!XvtRenderMath_BuildMainView(&s->camera, s->camera.world_pos, width, height, &g_frame.view) ||
		!XvtRenderMath_Layout(s->camera.screen_width, s->camera.screen_height, (float)width, (float)height,
							  &g_frame.cockpit_layout) ||
		!XvtRenderMath_Layout(640, 480, (float)width, (float)height, &g_frame.frontend_layout)) {
		XvtRemasterFlight_Invalidate();
		return 0;
	}
	g_frame.content_rect = (AeronRectI) { 0, 0, width, height };
	if (s->camera.map_mode ||
		(s->camera.hud_state != HUD_VIEW_HUD_ONLY && s->camera.hud_state != HUD_VIEW_FULL_SCREEN)) {
		/* Match the centered bitmap frame without changing the scene projection. */
		const XvtLayoutTransform* layout = &g_frame.cockpit_layout;
		int w = (int)(layout->source_width * layout->scale + .5f);
		int h = (int)(layout->source_height * layout->scale + .5f);
		g_frame.content_rect = (AeronRectI) { (width - w) / 2, (height - h) / 2, w, h };
	}
	if (advance) {
		g_frame.velocity_span_us =
			!reset && s->capture_host_us > g_poseHostUs ? s->capture_host_us - g_poseHostUs : 0;
		g_poseHostUs = s->capture_host_us;
		if (reset)
			g_frame.previous_view = g_frame.view;
		else if (!XvtRenderMath_BuildMainView(&p->camera, s->camera.world_pos, width, height,
											  &g_frame.previous_view)) {
			XvtRemasterFlight_Invalidate();
			return 0;
		}
		unsigned previous_index = 0;
		for (unsigned i = 0; i < s->object_count; ++i) {
			const XvtSnapObject* object = &s->objects[i];
			XvtPreparedObject* out = &g_frame.objects[i];
			XvtRenderMath_ObjectMatrix(object, s->camera.world_pos, out->transform);
			out->previous_index = -1;
			out->zero_velocity = 1;
			memcpy(out->previous_transform, out->transform, sizeof out->transform);
			if (reset)
				continue;
			/* Capture order is ascending slot, so matching needs a single linear walk. */
			while (previous_index < p->object_count && p->objects[previous_index].id.slot < object->id.slot)
				++previous_index;
			if (previous_index == p->object_count)
				continue;
			const XvtSnapObject* old = &p->objects[previous_index];
			if (old->id.slot != object->id.slot || old->id.signature != object->id.signature ||
				old->object_type != object->object_type)
				continue;
			out->previous_index = (int32_t)previous_index;
			out->zero_velocity = 0;
			XvtRenderMath_ObjectMatrix(old, s->camera.world_pos, out->previous_transform);
		}
		int64_t ticks = previous_valid ? (int64_t)s->view_time_ticks - p->view_time_ticks : 0;
		g_frame.delta_seconds = !reset && ticks > 0 ? (float)ticks / 236.0f : 0;
	}
	g_frame.object_count = s->object_count;
	g_frame.tick_index = s->tick_index;
	g_frame.flight_frame_serial = s->flight_frame_serial;
	g_frame.reset_history = reset;
	int motion_changed = g_frame.regenerate_motion != advance;
	g_frame.regenerate_motion = advance;
	int hdr = Aeron_OutputHdrEnabled();
	float headroom = Aeron_OutputHdrHeadroom();
	g_frame.render_needed = advance || scene_changed ||
							XvtRemasterConfig_Effective()->temporal_mode != AERON_TEMPORAL_OFF ||
							g_lastHdr != hdr || g_lastHeadroom != headroom || g_lastPaused != s->paused ||
							(!XvtRemasterConfig_Effective()->motion_blur.pause_keep_blur && motion_changed);
	g_frame.valid = 1;
	g_width = width;
	g_height = height;
	g_lastMission = s->mission_generation;
	g_lastWorld = s->world_generation;
	g_lastOpt = s->opt_asset_generation;
	g_lastTexture = s->texture_asset_generation;
	g_lastConfig = config;
	g_lastHdr = hdr;
	g_lastHeadroom = headroom;
	g_lastPaused = s->paused;
	return 1;
}
