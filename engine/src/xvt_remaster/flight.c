#include "xvt_remaster/flight.h"

#include <string.h>

#include "aeron/aeron.h"
#include "xvt/flight/hud/hud.h"
#include "xvt_remaster/config.h"

static struct xvt_prepared_flight g_frame;
static uint64_t g_last_mission;
static uint64_t g_last_world;
static uint64_t g_last_opt;
static uint64_t g_last_texture;
static int g_width;
static int g_height;
static uint64_t g_pose_host_us;
static uint64_t g_last_config;
static uint64_t g_resize_since;
static int g_requested_width;
static int g_requested_height;
static int g_last_hdr;
static int g_last_paused;
static float g_last_headroom;

static int view_discontinuity(const struct xvt_snap_camera *a,
			      const struct xvt_snap_camera *b)
{
	return a->player.slot != b->player.slot ||
	       a->player.signature != b->player.signature ||
	       a->focus.slot != b->focus.slot ||
	       a->focus.signature != b->focus.signature ||
	       a->external != b->external || a->replay_view != b->replay_view ||
	       a->map_mode != b->map_mode || a->hud_state != b->hud_state ||
	       a->hyperspace_phase != b->hyperspace_phase ||
	       a->perspective_shift != b->perspective_shift ||
	       a->aspect_y_q16 != b->aspect_y_q16 ||
	       a->screen_width != b->screen_width ||
	       a->screen_height != b->screen_height ||
	       memcmp(&a->viewport, &b->viewport, sizeof a->viewport) ||
	       a->projection_offset_y != b->projection_offset_y;
}

void xvt_remaster_flight_invalidate(void) { g_frame.valid = 0; }

void xvt_remaster_flight_request_composition(void)
{
	g_frame.render_needed = 1;
}

const struct xvt_prepared_flight *xvt_remaster_flight_current(void)
{
	return g_frame.valid ? &g_frame : NULL;
}

/* Turns the newest captured snapshot and the one before it into the frame the
 * renderer draws: the size, whether the motion history resets or moves on,
 * the views and layouts, each object's current and previous transform, and
 * whether the frame must be drawn again. The decisions made near the top are
 * read by most later steps, so pieces split out would each take much of this
 * state as arguments or hand several values back. */
int xvt_remaster_flight_prepare(const struct xvt_render_snapshot *s,
				const struct xvt_render_snapshot *p, int width,
				int height)
{
	if (!s || !s->flight_valid || !s->camera.valid) {
		xvt_remaster_flight_invalidate();
		return 1;
	}
	uint64_t now = Aeron_NowUs();
	if (width != g_requested_width || height != g_requested_height) {
		g_requested_width = width;
		g_requested_height = height;
		g_resize_since = now;
	}
	/* Keep the complete target during interactive resize, as in XWA. */
	if (g_frame.valid && (width != g_width || height != g_height) &&
	    now - g_resize_since < 150000) {
		width = g_width;
		height = g_height;
	}
	uint64_t config = xvt_remaster_config_generation();
	int new_snapshot =
		!g_frame.valid || s->snapshot_serial != g_frame.snapshot_serial;
	int previous_valid = p && p->flight_valid && p->camera.valid &&
			     p->mission_generation == s->mission_generation &&
			     p->world_generation == s->world_generation;
	int reset = !g_frame.valid || (new_snapshot && !previous_valid) ||
		    g_width != width || g_height != height ||
		    g_last_mission != s->mission_generation ||
		    g_last_world != s->world_generation ||
		    g_last_opt != s->opt_asset_generation ||
		    g_last_texture != s->texture_asset_generation ||
		    g_last_config != config;
	if (new_snapshot && previous_valid &&
	    (s->view_time_ticks < p->view_time_ticks ||
	     view_discontinuity(&s->camera, &p->camera))) {
		reset = 1;
	}
	int changed = !previous_valid || xvt_render_math_pose_changed(s, p);
	if (new_snapshot && previous_valid &&
	    s->hyperspace.phase == XVT_SNAP_HYPERSPACE_TRANSITION &&
	    ((s->hyperspace.elapsed_ticks < XVT_SNAP_HYPERSPACE_STREAK_END) !=
	     (p->hyperspace.elapsed_ticks < XVT_SNAP_HYPERSPACE_STREAK_END))) {
		reset = 1;
	}
	int advance = reset ||
		      (new_snapshot &&
		       (changed || (previous_valid &&
				    s->view_time_ticks != p->view_time_ticks)));
	int scene_changed =
		!advance && previous_valid &&
		(memcmp(&s->lighting, &p->lighting, sizeof s->lighting) ||
		 memcmp(&s->sky, &p->sky, sizeof s->sky) ||
		 memcmp(s->types, p->types, sizeof s->types) ||
		 memcmp(s->fuselage_sequence, p->fuselage_sequence,
			sizeof s->fuselage_sequence) ||
		 memcmp(&s->hyperspace, &p->hyperspace, sizeof s->hyperspace) ||
		 (s->camera.map_mode &&
		  memcmp(&s->map, &p->map, sizeof s->map)));
	if (!xvt_render_math_build_main_view(&s->camera, s->camera.world_pos,
					     width, height, &g_frame.view) ||
	    !xvt_render_math_layout(s->camera.screen_width,
				    s->camera.screen_height, (float)width,
				    (float)height, &g_frame.cockpit_layout) ||
	    !xvt_render_math_layout(640, 480, (float)width, (float)height,
				    &g_frame.frontend_layout)) {
		xvt_remaster_flight_invalidate();
		return 0;
	}
	g_frame.content_rect = (AeronRectI){0, 0, width, height};
	if (s->camera.map_mode ||
	    (s->camera.hud_state != HUD_VIEW_HUD_ONLY &&
	     s->camera.hud_state != HUD_VIEW_FULL_SCREEN)) {
		/* Match the centered bitmap frame without changing the scene projection. */
		const struct xvt_layout_transform *layout =
			&g_frame.cockpit_layout;
		int w = (int)(layout->source_width * layout->scale + .5f);
		int h = (int)(layout->source_height * layout->scale + .5f);
		g_frame.content_rect =
			(AeronRectI){(width - w) / 2, (height - h) / 2, w, h};
	}
	if (advance) {
		g_frame.velocity_span_us =
			!reset && s->capture_host_us > g_pose_host_us
				? s->capture_host_us - g_pose_host_us
				: 0;
		g_pose_host_us = s->capture_host_us;
		if (reset) {
			g_frame.previous_view = g_frame.view;
		} else if (!xvt_render_math_build_main_view(
				   &p->camera, s->camera.world_pos, width,
				   height, &g_frame.previous_view)) {
			xvt_remaster_flight_invalidate();
			return 0;
		}
		unsigned previous_index = 0;
		for (unsigned i = 0; i < s->object_count; ++i) {
			const struct xvt_snap_object *object = &s->objects[i];
			struct xvt_prepared_object *out = &g_frame.objects[i];
			xvt_render_math_object_matrix(
				object, s->camera.world_pos, out->transform);
			out->previous_index = -1;
			out->zero_velocity = 1;
			memcpy(out->previous_transform, out->transform,
			       sizeof out->transform);
			if (reset) {
				continue;
			}
			/* Capture order is ascending slot, so matching needs a single linear walk. */
			while (previous_index < p->object_count &&
			       p->objects[previous_index].id.slot <
				       object->id.slot) {
				++previous_index;
			}
			if (previous_index == p->object_count) {
				continue;
			}
			const struct xvt_snap_object *old =
				&p->objects[previous_index];
			if (old->id.slot != object->id.slot ||
			    old->id.signature != object->id.signature ||
			    old->object_type != object->object_type) {
				continue;
			}
			out->previous_index = (int32_t)previous_index;
			out->zero_velocity = 0;
			xvt_render_math_object_matrix(old, s->camera.world_pos,
						      out->previous_transform);
		}
		int64_t ticks = previous_valid ? (int64_t)s->view_time_ticks -
							 p->view_time_ticks
					       : 0;
		g_frame.delta_sim_seconds =
			!reset && ticks > 0 ? (float)ticks / 236.0f : 0;
	}
	g_frame.object_count = s->object_count;
	g_frame.snapshot_serial = s->snapshot_serial;
	g_frame.flight_frame_serial = s->flight_frame_serial;
	g_frame.reset_history = reset;
	int motion_changed = g_frame.regenerate_motion != advance;
	g_frame.regenerate_motion = advance;
	int hdr = Aeron_OutputHdrEnabled();
	float headroom = Aeron_OutputHdrHeadroom();
	g_frame.render_needed =
		advance || scene_changed ||
		xvt_remaster_config_effective()->temporal_mode !=
			AERON_TEMPORAL_OFF ||
		g_last_hdr != hdr || g_last_headroom != headroom ||
		g_last_paused != s->paused ||
		(!xvt_remaster_config_effective()
			  ->motion_blur.pause_keep_blur &&
		 motion_changed);
	g_frame.valid = 1;
	g_width = width;
	g_height = height;
	g_last_mission = s->mission_generation;
	g_last_world = s->world_generation;
	g_last_opt = s->opt_asset_generation;
	g_last_texture = s->texture_asset_generation;
	g_last_config = config;
	g_last_hdr = hdr;
	g_last_headroom = headroom;
	g_last_paused = s->paused;
	return 1;
}
