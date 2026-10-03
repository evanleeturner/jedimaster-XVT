#include "xvt_runtime/snapshot/cockpit_messages.h"

#include "aeron/aeron.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/player/player.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/snapshot/cockpit_text.h"
#include "xvt_runtime/snapshot/render_hud.h"
#include <string.h>

typedef char xvt_cockpit_overlay_capacity_check
	[XVT_HUD_OVERLAY_GLYPHS ==
			 XVT_COCKPIT_MESSAGE_COUNT * XVT_HUD_MESSAGE_GLYPHS +
				 3 * XVT_HUD_ALERT_LINE_GLYPHS +
				 XVT_HUD_LOADING_GLYPHS
		 ? 1
		 : -1];

struct message_content {
	struct xvt_cockpit_message state;
	int16_t origin_x, origin_y;
	struct xvt_cockpit_glyph glyphs[XVT_HUD_MESSAGE_GLYPHS];
};

struct alert_content {
	struct xvt_cockpit_alert state;
	struct xvt_cockpit_glyph glyphs[3][XVT_HUD_ALERT_LINE_GLYPHS];
};

static struct message_content g_working_panes[XVT_COCKPIT_MESSAGE_COUNT],
	g_pending[XVT_COCKPIT_MESSAGE_COUNT], g_message_build;
static struct alert_content g_alert, g_alert_build;
static struct xvt_cockpit_loading g_loading;
static int g_message_capture_pane = -1, g_alert_capture_line = -1,
	   g_capture_failed;
static uint32_t g_message_palette[256], g_alert_palette[256];
static uint64_t g_generation;

/* Loading text is emitted before mission initialization resets flight state. */
static struct {
	struct xvt_cockpit_glyph glyphs[XVT_HUD_LOADING_GLYPHS];
	uint32_t palette[256];
	struct xvt_snap_rect bounds;
	uint64_t generation;
	uint16_t count;
	uint8_t capturing, visible;
} g_loading_text;

static void capture_palette(uint32_t palette[256])
{
	for (unsigned index = 0; index < 256; ++index) {
		palette[index] = xvt_render_draw_color(index);
	}
}

void xvt_cockpit_messages_reset_working(void)
{
	memset(g_working_panes, 0, sizeof g_working_panes);
	g_message_capture_pane = -1;
}

void xvt_cockpit_messages_reset(void)
{
	xvt_cockpit_messages_reset_working();
	memset(g_pending, 0, sizeof g_pending);
	memset(&g_alert, 0, sizeof g_alert);
	memset(&g_loading, 0, sizeof g_loading);
	g_alert_capture_line = -1;
	g_capture_failed = 0;
}

void xvt_cockpit_messages_begin_flight_frame(void)
{
	g_loading.progress_visible = 0;
	g_loading_text.visible = 0;
}

void xvt_cockpit_messages_begin_loading_text(void)
{
	g_loading_text.count = 0;
	g_loading_text.visible = 0;
	g_loading_text.capturing = 1;
	g_loading_text.bounds = (struct xvt_snap_rect){
		0, 0, (int)g_screen_width, (int)g_screen_height};
	capture_palette(g_loading_text.palette);
}

void xvt_cockpit_messages_end_loading_text(void)
{
	g_loading_text.capturing = 0;
	g_loading_text.visible = g_loading_text.count != 0;
	g_loading_text.generation = ++g_generation;
}

void xvt_cockpit_messages_clear(xvt_cockpit_message_id pane)
{
	if ((unsigned)pane >= XVT_COCKPIT_MESSAGE_COUNT ||
	    !g_working_panes[pane].state.visible) {
		return;
	}
	memset(&g_working_panes[pane], 0, sizeof g_working_panes[pane]);
	g_working_panes[pane].state.generation = ++g_generation;
}

void xvt_cockpit_messages_begin_message(int pane_type)
{
	xvt_cockpit_message_id pane =
		pane_type == 8
			? XVT_COCKPIT_MESSAGE_FLIGHT_GROUP
			: (pane_type == 3 || pane_type == 4 || pane_type == 7
				   ? XVT_COCKPIT_MESSAGE_SYSTEM
				   : XVT_COCKPIT_MESSAGE_READY);
	const struct hud_in_flight_message_record *message =
		pane == XVT_COCKPIT_MESSAGE_READY
			? &g_ready_message_pane_queue[0]
			: (pane == XVT_COCKPIT_MESSAGE_SYSTEM
				   ? &g_system_message_pane
				   : &g_flight_group_message_pane);
	memset(&g_message_build, 0, sizeof g_message_build);
	g_message_build.origin_x = g_flight_clip_left;
	g_message_build.origin_y = g_flight_clip_top;
	g_message_build.state.visible = 1;
	g_message_build.state.message_id = message->state_or_message_id;
	g_message_build.state.sender_iff = message->sender_iff;
	g_message_build.state.pane_type = (uint8_t)pane_type;
	g_message_build.state.font_tier = g_flight_font_tier;
	g_message_build.state.age_seconds = message->age_seconds;
	g_message_capture_pane = pane;
	g_capture_failed = 0;
	capture_palette(g_message_palette);
}

void xvt_cockpit_messages_record_reveal(unsigned characters)
{
	if (g_message_capture_pane >= 0) {
		g_message_build.state.revealed_characters =
			(uint16_t)characters;
	}
}

void xvt_cockpit_messages_end_message(void)
{
	if (g_message_capture_pane < 0) {
		return;
	}
	unsigned pane = (unsigned)g_message_capture_pane;
	if (!g_capture_failed) {
		g_message_build.state.generation =
			g_working_panes[pane].state.generation;
		if (!g_working_panes[pane].state.visible ||
		    g_message_build.origin_x !=
			    g_working_panes[pane].origin_x ||
		    g_message_build.origin_y !=
			    g_working_panes[pane].origin_y ||
		    g_message_build.state.glyph_count !=
			    g_working_panes[pane].state.glyph_count ||
		    memcmp(g_message_build.glyphs, g_working_panes[pane].glyphs,
			   g_message_build.state.glyph_count *
				   sizeof g_message_build.glyphs[0])) {
			g_message_build.state.generation = ++g_generation;
		}
		g_working_panes[pane] = g_message_build;
	}
	g_message_capture_pane = -1;
}

void xvt_cockpit_messages_begin_placement(void)
{
	for (unsigned pane = 0; pane < XVT_COCKPIT_MESSAGE_COUNT; ++pane) {
		g_pending[pane].state.visible = 0;
	}
}

void xvt_cockpit_messages_latch(xvt_cockpit_message_id pane, int source_x,
				int source_y, int x, int y, int width,
				int height)
{
	if ((unsigned)pane >= XVT_COCKPIT_MESSAGE_COUNT) {
		return;
	}
	struct message_content *destination = &g_pending[pane];
	const struct message_content *source = &g_working_panes[pane];
	destination->state = source->state;
	destination->state.placement =
		(struct xvt_snap_rect){x, y, width, height};
	const struct hud_in_flight_message_record *message =
		pane == XVT_COCKPIT_MESSAGE_READY
			? &g_ready_message_pane_queue[0]
			: (pane == XVT_COCKPIT_MESSAGE_SYSTEM
				   ? &g_system_message_pane
				   : &g_flight_group_message_pane);
	if (message->state_or_message_id == destination->state.message_id) {
		destination->state.age_seconds = message->age_seconds;
	}
	destination->state.timer_ticks =
		pane == XVT_COCKPIT_MESSAGE_READY
			? g_player_flight_transient_timers[g_local_player]
				  .ready_message_pane_timer
			: (pane == XVT_COCKPIT_MESSAGE_SYSTEM
				   ? g_player_flight_transient_timers
					     [g_local_player]
						     .system_message_pane_timer
				   : g_player_flight_transient_timers[g_local_player]
					     .flight_group_message_pane_timer);
	int offset_x = source->origin_x - source_x,
	    offset_y = source->origin_y - source_y;
	for (unsigned index = 0; index < source->state.glyph_count; ++index) {
		struct xvt_cockpit_glyph *glyph = &destination->glyphs[index];
		*glyph = source->glyphs[index];
		glyph->x += (int16_t)offset_x;
		glyph->y += (int16_t)offset_y;
		glyph->clip.x += offset_x;
		glyph->clip.y += offset_y;
	}
}

void xvt_cockpit_messages_record_glyph(unsigned character, unsigned advance,
				       unsigned height, int narrow)
{
	struct xvt_cockpit_glyph glyph;
	if (g_loading_text.capturing) {
		if (!xvt_cockpit_text_capture_glyph(
			    &glyph, character, advance, height, narrow, 0, 0,
			    g_loading_text.palette, 0)) {
			return;
		}
		if (g_loading_text.count == XVT_HUD_LOADING_GLYPHS) {
			Aeron_RequestFatalError(
				"Loading text capture",
				"The loading text exceeds its glyph capacity.");
			return;
		}
		g_loading_text.glyphs[g_loading_text.count++] = glyph;
		return;
	}
	if (g_message_capture_pane >= 0) {
		if (!xvt_cockpit_text_capture_glyph(
			    &glyph, character, advance, height, narrow,
			    g_message_build.origin_x, g_message_build.origin_y,
			    g_message_palette, 1)) {
			return;
		}
		if (g_message_build.state.glyph_count ==
		    XVT_HUD_MESSAGE_GLYPHS) {
			XVT_LOG_ERROR("snapshot.pane_overflow pane=%d",
				      g_message_capture_pane);
			g_capture_failed = 1;
			return;
		}
		g_message_build.glyphs[g_message_build.state.glyph_count++] =
			glyph;
	} else if (g_alert_capture_line >= 0) {
		if (!xvt_cockpit_text_capture_glyph(
			    &glyph, character, advance, height, narrow,
			    g_alert_build.state.placement.x,
			    g_alert_build.state.placement.y, g_alert_palette,
			    0)) {
			return;
		}
		uint16_t *count =
			&g_alert_build.state.glyph_count[g_alert_capture_line];
		if (*count == XVT_HUD_ALERT_LINE_GLYPHS) {
			XVT_LOG_ERROR("snapshot.alert_overflow line=%d",
				      g_alert_capture_line);
			g_capture_failed = 1;
			return;
		}
		g_alert_build.glyphs[g_alert_capture_line][(*count)++] = glyph;
	}
}

void xvt_cockpit_messages_begin_alert(void)
{
	memset(&g_alert, 0, sizeof g_alert);
	g_alert_capture_line = -1;
}

void xvt_cockpit_messages_begin_alert_line(int mode, int x, int y, int width,
					   int height)
{
	if (mode < 0 || mode > 3) {
		return;
	}
	g_alert_build = g_alert;
	if (!g_alert.state.active) {
		memset(&g_alert_build, 0, sizeof g_alert_build);
	}
	g_alert_build.state.active = 1;
	g_alert_build.state.placement =
		(struct xvt_snap_rect){x, y, width, height};
	capture_palette(g_alert_palette);
	unsigned first_row = mode <= 1 ? 0 : (unsigned)mode;
	if (mode == 1) {
		g_alert_build.state.border_argb =
			g_alert_palette[g_flight_text_bg_color];
	}
	/* The original changes the inner color immediately after its optional border fill. */
	for (unsigned row = first_row; row < 5; ++row) {
		g_alert_build.state.row_background_argb[row] = 0;
	}
	for (unsigned line = first_row ? first_row - 1 : 0; line < 3; ++line) {
		g_alert_build.state.line_visible[line] = 0;
		g_alert_build.state.glyph_count[line] = 0;
		memset(g_alert_build.glyphs[line], 0,
		       sizeof g_alert_build.glyphs[line]);
	}
	g_alert_capture_line = mode <= 1 ? 0 : mode - 1;
	g_alert_build.state.line_visible[g_alert_capture_line] = 1;
	g_capture_failed = 0;
}

void xvt_cockpit_messages_end_alert_line(void)
{
	if (g_alert_capture_line < 0) {
		return;
	}
	unsigned first_row = g_alert_capture_line == 0
				     ? 0
				     : (unsigned)g_alert_capture_line + 1;
	for (unsigned row = first_row; row < 5; ++row) {
		g_alert_build.state.row_background_argb[row] =
			g_alert_palette[g_flight_text_bg_color];
	}
	if (!g_capture_failed) {
		g_alert_build.state.generation = g_alert.state.generation;
		if (memcmp(&g_alert_build, &g_alert, sizeof g_alert_build)) {
			g_alert_build.state.generation = ++g_generation;
		}
		g_alert = g_alert_build;
	}
	g_alert_capture_line = -1;
}

void xvt_cockpit_messages_end_alert(void)
{
	if (g_alert.state.active) {
		g_alert.state.active = 0;
		g_alert.state.generation = ++g_generation;
	}
}

void xvt_cockpit_messages_record_progress(unsigned step, int x, int y,
					  int width, int height,
					  int filled_width)
{
	struct xvt_cockpit_loading next;
	memset(&next, 0, sizeof next);
	next.progress_visible = 1;
	next.progress_placement = (struct xvt_snap_rect){x, y, width, height};
	next.progress_step = (uint16_t)step;
	next.filled_width = (uint16_t)filled_width;
	next.foreground_argb = xvt_render_draw_color(g_flight_text_bg_color);
	next.background_argb = xvt_render_draw_color(0);
	next.generation = g_loading.generation;
	if (memcmp(&next, &g_loading, sizeof next)) {
		next.generation = ++g_generation;
	}
	g_loading = next;
}

void xvt_cockpit_messages_clear_progress(void)
{
	g_loading.progress_visible = 0;
	memset(&g_loading_text, 0, sizeof g_loading_text);
}

void xvt_cockpit_messages_export(struct xvt_cockpit_state *state)
{
	struct xvt_cockpit_overlay_store *store = &state->overlay_content;
	store->glyph_count = 0;
	for (unsigned pane = 0; pane < XVT_COCKPIT_MESSAGE_COUNT; ++pane) {
		struct xvt_cockpit_message *message =
			&state->messages.panes[pane];
		*message = g_pending[pane].state;
		message->first_glyph = store->glyph_count;
		if (!message->visible) {
			message->glyph_count = 0;
			continue;
		}
		memcpy(&store->glyphs[store->glyph_count],
		       g_pending[pane].glyphs,
		       message->glyph_count * sizeof store->glyphs[0]);
		store->glyph_count += message->glyph_count;
	}
	state->alert = g_alert.state;
	for (unsigned line = 0; line < 3; ++line) {
		state->alert.first_glyph[line] = store->glyph_count;
		if (!state->alert.active || !state->alert.line_visible[line]) {
			state->alert.glyph_count[line] = 0;
			continue;
		}
		memcpy(&store->glyphs[store->glyph_count], g_alert.glyphs[line],
		       state->alert.glyph_count[line] *
			       sizeof store->glyphs[0]);
		store->glyph_count += state->alert.glyph_count[line];
	}
	state->loading = g_loading;
	state->loading.text_generation = g_loading_text.generation;
	state->loading.text_visible = g_loading_text.visible;
	state->loading.text_bounds = g_loading_text.bounds;
	state->loading.first_glyph = store->glyph_count;
	if (g_loading_text.visible) {
		state->loading.glyph_count = g_loading_text.count;
		memcpy(&store->glyphs[store->glyph_count],
		       g_loading_text.glyphs,
		       g_loading_text.count * sizeof *store->glyphs);
		store->glyph_count += g_loading_text.count;
	}
	if (state->alert.active || state->loading.progress_visible ||
	    state->loading.text_visible) {
		state->view.screen_width = (uint16_t)g_screen_width;
		state->view.screen_height = (uint16_t)g_screen_height;
	}
}
