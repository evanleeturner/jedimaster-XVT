#include "xvt_runtime/snapshot/cockpit_capture.h"

#include <stddef.h>
#include <string.h>

#include "xvt/flight/flight.h"
#include "xvt/flight/flight_display.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/player/player.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/input/mouse_flight.h"
#include "xvt_runtime/snapshot/cockpit_instruments.h"
#include "xvt_runtime/snapshot/cockpit_messages.h"
#include "xvt_runtime/snapshot/cockpit_pages.h"
#include "xvt_runtime/snapshot/cockpit_readouts.h"
#include "xvt_runtime/snapshot/cockpit_text.h"
#include "xvt_runtime/snapshot/render_cockpit_assets.h"
#include "xvt_runtime/snapshot/render_hud.h"

/* All capture and publication runs on the host thread. Composition selects the
 * working content before the original can refresh it for another presentation. */
static struct xvt_cockpit_state g_working;
static struct xvt_cockpit_state g_pending;
static struct xvt_cockpit_state g_completed;
static uint64_t g_presentation_serial;
static uint64_t g_prepared_resource_generation;
static int g_composition_selected;
static int g_sealed;

void xvt_cockpit_copy_state(struct xvt_cockpit_state *destination,
			    const struct xvt_cockpit_state *source)
{
	memcpy(destination, source,
	       offsetof(struct xvt_cockpit_state, page_content));
	destination->page_content.row_count = source->page_content.row_count;
	destination->page_content.glyph_count =
		source->page_content.glyph_count;
	memcpy(destination->page_content.rows, source->page_content.rows,
	       source->page_content.row_count *
		       sizeof source->page_content.rows[0]);
	memcpy(destination->page_content.glyphs, source->page_content.glyphs,
	       source->page_content.glyph_count *
		       sizeof source->page_content.glyphs[0]);
	destination->overlay_content.glyph_count =
		source->overlay_content.glyph_count;
	memcpy(destination->overlay_content.glyphs,
	       source->overlay_content.glyphs,
	       source->overlay_content.glyph_count *
		       sizeof source->overlay_content.glyphs[0]);
}

void xvt_cockpit_reset(void)
{
	g_prepared_resource_generation = 0;
	xvt_cockpit_text_reset_fields();
	xvt_cockpit_readouts_reset();
	xvt_cockpit_pages_reset();
	xvt_cockpit_messages_reset();
	memset(&g_working, 0, sizeof g_working);
	memset(&g_pending, 0, sizeof g_pending);
	memset(&g_completed, 0, sizeof g_completed);
	g_composition_selected = 0;
	g_sealed = 0;
}

static void capture_view(struct xvt_cockpit_view *view)
{
	const struct player_data *player = &g_players[g_local_player];
	memset(view, 0, sizeof *view);
	view->screen_width = (uint16_t)g_screen_width;
	view->screen_height = (uint16_t)g_screen_height;
	view->hud_state = player->view_state.hud_state_live;
	view->instrument_base = g_hud_instrument_set_base_index;
	view->panel_set = g_hud_panel_set_id;
	view->active_page = g_mfd_active_page;
	view->secondary_page = g_mfd_secondary_page;
	view->map_active = player->map_camera_state != 0;
	view->external_camera = player->view_state.external_camera_active != 0;
	view->mission_ending = g_flight_mission_state.mission_end_pending != 0;
	view->awaiting_new_craft = player->awaiting_new_craft != 0;
	view->instruments_visible =
		!view->mission_ending && !view->awaiting_new_craft;
	view->viewport =
		(struct xvt_snap_rect){g_flight_vp_x, g_flight_vp_y,
				       g_flight_vp_width, g_flight_vp_height};
	view->projection_offset_y = g_proj_offset_y;
	unsigned resource = view->hud_state;
	if (resource < 28 && resource != HUD_VIEW_FULL_SCREEN) {
		unsigned reference =
			g_hud_cockpit_resource_descriptors[resource]
				.resource_ref;
		if (reference >= 0xc0) {
			resource = reference - 0xc0;
			view->mirrored = 1;
		} else if (reference >= 0x80) {
			resource = reference - 0x80;
		}
	}
	view->resource_descriptor = (uint16_t)resource;
	view->viewport_descriptor =
		view->mirrored ? view->hud_state : (uint16_t)resource;
}

void xvt_cockpit_refresh_instruments(int player)
{
	if (player != g_local_player || (unsigned)player >= 8) {
		return;
	}
	capture_view(&g_working.view);
	xvt_render_cockpit_capture_definition(&g_working.definition);
	xvt_cockpit_instruments_build(&g_working);
	int mouse_x = 0;
	int mouse_y = 0;
	g_working.mouse_stick_visible =
		xvt_mouse_flight_get_hud_marker(&mouse_x, &mouse_y) &&
		!g_working.view.map_active && !g_working.view.external_camera &&
		(g_working.view.hud_state == HUD_VIEW_FORWARD ||
		 g_working.view.hud_state == HUD_VIEW_HUD_ONLY) &&
		g_working.view.instruments_visible;
	g_working.mouse_stick_x = (int8_t)mouse_x;
	g_working.mouse_stick_y = (int8_t)mouse_y;
	xvt_cockpit_readouts_copy_state(&g_working);
	xvt_cockpit_text_copy_fields(&g_working);
	for (unsigned index = 0; index < 256; ++index) {
		g_working.palette_argb[index] = xvt_render_draw_color(index);
	}
	g_working.valid = g_working.definition.layout.valid;
}

void xvt_cockpit_begin_frame(void)
{
	xvt_cockpit_messages_begin_flight_frame();
	xvt_cockpit_pages_begin_frame();
	/* A software blit after the previous flip may already have selected the next
	 * frame's cockpit. Keep that selection until another composition replaces it. */
	g_pending.crt.valid = 0;
	g_sealed = 0;
}

void xvt_cockpit_latch_composition(void)
{
	if (!g_working.valid) {
		return;
	}
	xvt_cockpit_copy_state(&g_pending, &g_working);
	xvt_cockpit_messages_begin_placement();
	g_composition_selected = 1;
	g_sealed = 0;
}

static void select_page_placement(struct xvt_cockpit_page *page, unsigned index)
{
	int map = g_players[g_local_player].map_camera_state != 0;
	memset(page, 0, sizeof *page);
	page->page_id = (uint16_t)index;
	page->original_state = g_mfd_page_states[index];
	page->focused = g_mfd_active_page == index;
	page->phase = XVT_COCKPIT_AFTER_CRT;
	if (page->original_state == MFD_PAGE_STATE_CLOSED) {
		return;
	}
	unsigned binding = 0;
	int width = 0;
	int height = 0;
	switch (index) {
	case MFD_PAGE_SCOREBOARD:
		binding = HUD_MFD_SCOREBOARD_ELEMENT;
		width = g_mfd_mission_scoreboard_blit_width;
		height = g_mfd_mission_scoreboard_blit_height;
		break;
	case MFD_PAGE_GOALS:
		binding = HUD_MFD_GOALS_ELEMENT;
		width = g_mfd_goals_blit_width;
		height = g_mfd_goals_blit_height;
		break;
	case MFD_PAGE_DAMAGE:
		if (map) {
			return;
		}
		binding = HUD_MFD_DAMAGE_ELEMENT;
		width = g_mfd_damage_blit_width;
		height = g_mfd_damage_blit_height;
		break;
	case MFD_PAGE_HOSTILE_CRAFT:
	case MFD_PAGE_FRIENDLY_CRAFT:
		binding = HUD_MFD_CRAFT_LIST_ELEMENT;
		width = g_mfd_craft_list_blit_width;
		height = g_mfd_craft_list_blit_height;
		break;
	case MFD_PAGE_MAP_HELP:
		if (!map) {
			return;
		}
		binding = HUD_MFD_MAP_OR_COMMAND_ELEMENT;
		width = g_hud_element_layouts[g_hud_instrument_set_base_index +
					      binding]
				.clip_width;
		height = g_hud_element_layouts[g_hud_instrument_set_base_index +
					       binding]
				 .clip_height_or_foreground_color;
		break;
	default:
		return;
	}
	if (map && index != MFD_PAGE_FRIENDLY_CRAFT) {
		binding = HUD_MFD_MAP_OR_COMMAND_ELEMENT;
		width = g_mfd_map_blit_width;
		height = g_mfd_map_blit_height;
	}
	page->layout_id = (uint16_t)(g_hud_instrument_set_base_index + binding);
	const struct hud_element_layout *layout =
		&g_hud_element_layouts[page->layout_id];
	page->placement =
		(struct xvt_snap_rect){layout->x, layout->y, width, height};
	page->visible = width > 0 && height > 0;
}

void xvt_cockpit_latch_pages(void)
{
	if ((unsigned)g_local_player >= 8) {
		return;
	}
	for (unsigned index = 0; index < MFD_PAGE_COUNT; ++index) {
		if (index != MFD_PAGE_MESSAGE_LOG) {
			select_page_placement(&g_pending.pages[index], index);
			if (g_pending.pages[index].visible) {
				xvt_cockpit_pages_latch(index);
			}
		}
	}
}

void xvt_cockpit_latch_launcher(unsigned launcher, int x, int y, int width,
				int height)
{
	if (launcher >= 4) {
		return;
	}
	struct xvt_cockpit_number *number =
		&g_pending.weapons.launchers[launcher].count;
	xvt_cockpit_readouts_copy_launcher(number, launcher);
	number->x = (int16_t)x;
	number->y = (int16_t)y;
	number->bounds = (struct xvt_snap_rect){x, y, width, height};
	number->phase = XVT_COCKPIT_AFTER_CRT;
	number->keyed = 1;
	number->color_key_argb =
		xvt_render_draw_color(g_flight_transparent_color_index);
	g_pending.weapons.launchers[launcher].visible = 1;
}

void xvt_cockpit_latch_messages(void)
{
	if ((unsigned)g_local_player >= 8) {
		return;
	}
	struct xvt_cockpit_page *page = &g_pending.pages[MFD_PAGE_MESSAGE_LOG];
	memset(page, 0, sizeof *page);
	page->page_id = MFD_PAGE_MESSAGE_LOG;
	page->original_state = g_mfd_page_states[MFD_PAGE_MESSAGE_LOG];
	page->visible = page->original_state != MFD_PAGE_STATE_CLOSED;
	page->focused = g_mfd_active_page == MFD_PAGE_MESSAGE_LOG;
	page->layout_id =
		g_hud_instrument_set_base_index + HUD_MFD_MESSAGE_LOG_ELEMENT;
	const struct hud_element_layout *layout =
		&g_hud_element_layouts[page->layout_id];
	page->placement = (struct xvt_snap_rect){
		layout->x, layout->y,
		g_ready_message_pane_right - g_ready_message_pane_left,
		g_ready_message_pane_bottom - g_ready_message_pane_top};
	page->phase = XVT_COCKPIT_AFTER_CRT;
	if (g_flight_player_count >= 1) {
		page->placement.width += 4 * g_flight_font_digit_width + 1;
	}
	if (page->visible) {
		xvt_cockpit_pages_latch(MFD_PAGE_MESSAGE_LOG);
	}
}

void xvt_cockpit_begin_message_placement(void)
{
	xvt_cockpit_messages_begin_placement();
}

void xvt_cockpit_latch_message(xvt_cockpit_message_id pane, int source_x,
			       int source_y, int x, int y, int width,
			       int height)
{
	if (pane != XVT_COCKPIT_MESSAGE_READY ||
	    g_mfd_page_states[MFD_PAGE_MESSAGE_LOG] == MFD_PAGE_STATE_CLOSED) {
		xvt_cockpit_messages_latch(pane, source_x, source_y, x, y,
					   width, height);
	}
	if (pane == XVT_COCKPIT_MESSAGE_READY) {
		xvt_cockpit_text_copy_placed_field(
			&g_pending.text_fields[XVT_COCKPIT_TEXT_NETWORK_PING],
			XVT_COCKPIT_TEXT_NETWORK_PING, x - source_x,
			y - source_y);
		xvt_cockpit_text_copy_placed_field(
			&g_pending.text_fields[XVT_COCKPIT_TEXT_NETWORK_LAG],
			XVT_COCKPIT_TEXT_NETWORK_LAG, x - source_x,
			y - source_y);
	}
}

void xvt_cockpit_retain_presented_frame(void)
{
	xvt_cockpit_copy_state(&g_pending, &g_completed);
	g_sealed = 0;
}

void xvt_cockpit_seal(const struct xvt_snap_preview *crt)
{
	if (!g_composition_selected) {
		return;
	}
	g_pending.crt = *crt;
	xvt_cockpit_pages_export(&g_pending);
	/* Primitive order is unrelated to the CRT scene's visual dependencies. */
	g_sealed = 1;
}

static uint64_t update_generation(uint64_t generation, const void *current,
				  const void *previous, size_t bytes)
{
	return generation +
	       (generation == 0 || memcmp(current, previous, bytes) != 0);
}

void xvt_cockpit_presented(int standalone_overlay)
{
	if (!g_sealed && !standalone_overlay) {
		return;
	}
	if (standalone_overlay && !g_pending.definition.layout.valid) {
		xvt_render_cockpit_capture_definition(&g_pending.definition);
	}
	xvt_cockpit_messages_export(&g_pending);
	if (!g_composition_selected &&
	    (g_pending.alert.active || g_pending.loading.progress_visible ||
	     g_pending.loading.text_visible)) {
		g_pending.valid = 1;
	}
	g_pending.definition_generation = update_generation(
		g_completed.definition_generation, &g_pending.definition,
		&g_completed.definition, sizeof g_pending.definition);
	g_pending.palette_generation = update_generation(
		g_completed.palette_generation, g_pending.palette_argb,
		g_completed.palette_argb, sizeof g_pending.palette_argb);
	g_pending.artwork_generation = update_generation(
		g_completed.artwork_generation, &g_pending.view,
		&g_completed.view, sizeof g_pending.view);
	g_pending.instruments_generation = update_generation(
		g_completed.instruments_generation, &g_pending.systems,
		&g_completed.systems,
		offsetof(struct xvt_cockpit_state, radar) -
			offsetof(struct xvt_cockpit_state, systems));
	g_pending.radar_generation = update_generation(
		g_completed.radar_generation, &g_pending.radar,
		&g_completed.radar, sizeof g_pending.radar);
	g_pending.text_generation =
		g_completed.text_generation +
		(!g_completed.text_generation ||
		 memcmp(g_pending.text_fields, g_completed.text_fields,
			sizeof g_pending.text_fields) ||
		 memcmp(&g_pending.readouts, &g_completed.readouts,
			sizeof g_pending.readouts) ||
		 memcmp(&g_pending.proving_grounds,
			&g_completed.proving_grounds,
			sizeof g_pending.proving_grounds) ||
		 memcmp(g_pending.pages, g_completed.pages,
			sizeof g_pending.pages));
	for (unsigned pane = 0; pane < XVT_COCKPIT_MESSAGE_COUNT; ++pane) {
		const struct xvt_cockpit_message *current =
			&g_pending.messages.panes[pane];
		const struct xvt_cockpit_message *previous =
			&g_completed.messages.panes[pane];
		if (current->generation != previous->generation ||
		    current->visible != previous->visible ||
		    memcmp(&current->placement, &previous->placement,
			   sizeof current->placement)) {
			g_pending.text_generation =
				g_completed.text_generation + 1;
			break;
		}
	}
	if (memcmp(&g_pending.alert, &g_completed.alert,
		   sizeof g_pending.alert) ||
	    memcmp(&g_pending.loading, &g_completed.loading,
		   sizeof g_pending.loading)) {
		g_pending.text_generation = g_completed.text_generation + 1;
	}
	g_pending.crt_generation =
		update_generation(g_completed.crt_generation, &g_pending.crt,
				  &g_completed.crt, sizeof g_pending.crt);
	g_pending.presentation_serial = ++g_presentation_serial;
	xvt_cockpit_copy_state(&g_completed, &g_pending);
	g_sealed = 0;
}

void xvt_cockpit_export(struct xvt_cockpit_state *destination)
{
	xvt_cockpit_copy_state(destination, &g_completed);
}

void xvt_cockpit_resources_prepared(uint64_t generation)
{
	g_prepared_resource_generation = generation;
}

int xvt_cockpit_loading_assets_ready(void)
{
	return g_working.valid &&
	       g_prepared_resource_generation ==
		       g_working.definition.resource_generation;
}

void xvt_cockpit_export_resources(struct xvt_cockpit_resources *out)
{
	memset(out, 0, sizeof *out);
	if (!g_working.valid || !g_hud_cockpit_resources_loaded) {
		return;
	}
	xvt_render_cockpit_capture_definition(&out->definition);
	if (!out->definition.panels[0].asset_id) {
		return;
	}
	out->view.screen_width = g_working.view.screen_width;
	out->view.screen_height = g_working.view.screen_height;
	out->view.rebel_fighter = g_working.view.rebel_fighter;
	out->view.laser_slots = g_working.view.laser_slots;
	out->installed_hud_features = g_working.systems.installed_hud_features;
	for (unsigned color = 0; color < 256; ++color) {
		out->palette[color] = xvt_render_draw_color(color);
	}
	for (unsigned view = 0; view < 28; ++view) {
		if (!out->definition.layout.descriptors[view].lfd_asset_id) {
			continue;
		}
		struct rgb_triplet *rgb =
			(struct rgb_triplet *)g_hud_cockpit_resources[view]
				.entries[2];
		if (!rgb) {
			continue;
		}
		for (unsigned color = 0; color < 64; ++color) {
			uint32_t r = rgb[color].r & 63;
			uint32_t g = rgb[color].g & 63;
			uint32_t b = rgb[color].b & 63;
			r = (r << 2) | (r >> 4);
			g = (g << 2) | (g >> 4);
			b = (b << 2) | (b >> 4);
			out->view_palette[view][color] =
				0xff000000u | r << 16 | g << 8 | b;
		}
	}
	out->valid = 1;
}
