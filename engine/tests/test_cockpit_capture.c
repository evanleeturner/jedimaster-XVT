/* Checks the cockpit capture (xvt_runtime/snapshot/cockpit_capture.h) against the promises in its header:
 * CopyState's partial store copy; Reset; refreshing working for the local player only and its validity
 * following the cockpit layout; composition, sealing and publication across frames; the standalone overlay;
 * which parts' generations rise; placing pages, the message log, launchers and message panes; keeping the
 * presented frame; the refusals of ExportResources; and when loading assets count as ready. The test builds
 * its own cockpit: it sets the HUD layout, the MFD page states and sizes, the message records and the
 * palette, makes the cockpit layout valid by capturing those live values, and plays the host's frame
 * order. The other cockpit modules are the library's own. Every case starts from that world and Reset.
 *
 * Not checked here: ExportResources filling its output, which needs a cockpit panel image registered from
 * a file the game ships, and the mouse stick marker, which needs mouse flight running. */
#include <stddef.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/mfd.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/player/player.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/snapshot/cockpit_capture.h"
#include "xvt_runtime/snapshot/cockpit_messages.h"
#include "xvt_runtime/snapshot/cockpit_pages.h"
#include "xvt_runtime/snapshot/cockpit_readouts.h"
#include "xvt_runtime/snapshot/cockpit_text.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt_runtime/snapshot/render_cockpit_assets.h"
#include "xvt_runtime/snapshot/render_hud.h"

enum { LOCAL = 0, BYPASS = 9, BACKGROUND = 4 };

static struct xvt_cockpit_state g_out, g_expected;
static struct xvt_snap_preview g_crt;
static uint8_t g_framebuffer[16];

static void palette(void)
{
	for (unsigned index = 0; index < 256; ++index) {
		g_sw_palette[index] = (struct rgb_triplet){
			(uint8_t)(index & 63), (uint8_t)(index >> 6), 7};
	}
}

/* The local player in seat 0 has no craft and looks forward with the map closed. The goals, damage and
 * command pages have sizes; the message log pane is 100 by 50. The cockpit layout is valid. */
static void cockpit_capture_world(void)
{
	palette();
	g_screen_width = 640;
	g_screen_height = 480;
	memset(g_players, 0, sizeof g_players);
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
	}
	g_local_player = LOCAL;
	g_players[LOCAL].current_target_object_idx = -1;
	g_players[LOCAL].view_state.hud_state_live = HUD_VIEW_FORWARD;
	memset(&g_flight_mission_state, 0, sizeof g_flight_mission_state);
	memset(g_hud_cockpit_resource_descriptors, 0,
	       sizeof g_hud_cockpit_resource_descriptors);
	g_hud_cockpit_resources_loaded = 0;
	g_flight_player_count = 0;
	g_flight_font_digit_width = 4;

	memset(g_hud_element_layouts, 0, sizeof g_hud_element_layouts);
	g_hud_instrument_set_base_index = HUD_COCKPIT_INSTRUMENT_BASE_INDEX;
	g_hud_element_layouts[HUD_MFD_GOALS_ELEMENT] =
		(struct hud_element_layout){.x = 10, .y = 20};
	g_hud_element_layouts[HUD_MFD_DAMAGE_ELEMENT] =
		(struct hud_element_layout){.x = 30, .y = 40};
	g_hud_element_layouts[HUD_MFD_MAP_OR_COMMAND_ELEMENT] =
		(struct hud_element_layout){.x = 50,
					    .y = 60,
					    .clip_width = 70,
					    .clip_height_or_foreground_color =
						    80};
	g_hud_element_layouts[HUD_MFD_MESSAGE_LOG_ELEMENT] =
		(struct hud_element_layout){.x = 90, .y = 100};
	memset(g_mfd_page_states, 0, sizeof g_mfd_page_states);
	g_mfd_active_page = MFD_PAGE_NONE;
	g_mfd_goals_blit_width = 50;
	g_mfd_goals_blit_height = 40;
	g_mfd_damage_blit_width = 60;
	g_mfd_damage_blit_height = 30;
	g_mfd_map_blit_width = 70;
	g_mfd_map_blit_height = 20;
	g_mfd_mission_scoreboard_blit_width =
		g_mfd_mission_scoreboard_blit_height = 10;
	g_mfd_craft_list_blit_width = g_mfd_craft_list_blit_height = 10;
	g_ready_message_pane_left = 10;
	g_ready_message_pane_top = 20;
	g_ready_message_pane_right = 110;
	g_ready_message_pane_bottom = 70;

	g_flight_clip_left = 50;
	g_flight_clip_top = 60;
	g_flight_clip_right = 450;
	g_flight_clip_bottom = 300;
	g_flight_cursor_x = 70;
	g_flight_cursor_y = 80;
	g_flight_text_bg_color = BACKGROUND;
	g_flight_transparent_color_index = BYPASS;
	g_flight_offscreen_buffer = NULL;
	g_flight_sw_framebuffer_base = g_framebuffer;
	memset(g_ready_message_pane_queue, 0,
	       sizeof g_ready_message_pane_queue);
	g_ready_message_pane_queue[0].state_or_message_id = 11;
	g_system_message_pane.state_or_message_id = 22;

	xvt_render_cockpit_reset();
	xvt_render_assets_capture_cockpit(0);
	xvt_cockpit_reset();
	xvt_cockpit_messages_clear_progress();
	memset(&g_crt, 0, sizeof g_crt);
	g_crt.valid = 1;
	g_crt.mask_index = 2;
}

static const struct xvt_cockpit_state *presented(void)
{
	xvt_cockpit_export(&g_out);
	return &g_out;
}

/* The host's frame up to the composition: begin, refresh the local player, select the composition. */
static void compose(void)
{
	xvt_cockpit_begin_frame();
	xvt_cockpit_refresh_instruments(LOCAL);
	xvt_cockpit_latch_composition();
}

static const struct xvt_cockpit_state *seal_and_present(void)
{
	xvt_cockpit_seal(&g_crt);
	xvt_cockpit_presented(0);
	return presented();
}

static void check_copy_state(void)
{
	static struct xvt_cockpit_state source, destination;
	memset(&source, 0x11, sizeof source);
	memset(&destination, 0xCD, sizeof destination);
	source.page_content.row_count = 2;
	source.page_content.glyph_count = 3;
	source.overlay_content.glyph_count = 2;
	xvt_cockpit_copy_state(&destination, &source);

	XVT_ASSERT_INT_EQ(
		memcmp(&destination, &source,
		       offsetof(struct xvt_cockpit_state, page_content)),
		0);
	XVT_ASSERT_INT_EQ(destination.page_content.row_count, 2);
	XVT_ASSERT_INT_EQ(destination.page_content.glyph_count, 3);
	XVT_ASSERT_INT_EQ(destination.overlay_content.glyph_count, 2);
	const struct xvt_cockpit_page_store *from = &source.page_content;
	struct xvt_cockpit_page_store *to = &destination.page_content;
	XVT_ASSERT_INT_EQ(memcmp(to->rows, from->rows, 2 * sizeof to->rows[0]),
			  0);
	XVT_ASSERT_INT_EQ(
		memcmp(to->glyphs, from->glyphs, 3 * sizeof to->glyphs[0]), 0);
	XVT_ASSERT_INT_EQ(
		memcmp(destination.overlay_content.glyphs,
		       source.overlay_content.glyphs,
		       2 * sizeof destination.overlay_content.glyphs[0]),
		0);
	/* The unused rest of each store keeps its old contents. */
	const unsigned char *row = (const unsigned char *)&to->rows[2];
	const unsigned char *glyph = (const unsigned char *)&to->glyphs[3];
	const unsigned char *overlay =
		(const unsigned char *)&destination.overlay_content.glyphs[2];
	XVT_ASSERT_INT_EQ(row[0], 0xCD);
	XVT_ASSERT_INT_EQ(glyph[0], 0xCD);
	XVT_ASSERT_INT_EQ(overlay[0], 0xCD);
	XVT_ASSERT_INT_EQ(
		((const unsigned char *)&to
			 ->glyphs[XVT_HUD_PAGE_GLYPH_CAPACITY - 1])[0],
		0xCD);
}

static void check_compose_seal_present(void)
{
	cockpit_capture_world();
	compose();
	const struct xvt_cockpit_state *state = seal_and_present();
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_TRUE(state->presentation_serial != 0);
	/* The sealed CRT is the one handed in. */
	XVT_ASSERT_INT_EQ(state->crt.valid, 1);
	XVT_ASSERT_INT_EQ(state->crt.mask_index, 2);
	/* Working came from the live view, the cockpit definition and the palette. */
	XVT_ASSERT_INT_EQ(state->view.screen_width, 640);
	XVT_ASSERT_INT_EQ(state->view.screen_height, 480);
	XVT_ASSERT_INT_EQ(state->palette_argb[5], xvt_render_draw_color(5));
	XVT_ASSERT_INT_EQ(state->palette_argb[200], xvt_render_draw_color(200));
	xvt_render_cockpit_capture_definition(&g_expected.definition);
	XVT_ASSERT_INT_EQ(memcmp(&state->definition, &g_expected.definition,
				 sizeof state->definition),
			  0);

	/* A new frame keeps the composition selected: sealing it again publishes again. */
	uint64_t serial = state->presentation_serial;
	xvt_cockpit_begin_frame();
	state = seal_and_present();
	XVT_ASSERT_TRUE(state->presentation_serial != serial);
	XVT_ASSERT_INT_EQ(state->valid, 1);
}

static void check_nothing_published_unsealed(void)
{
	cockpit_capture_world();
	/* Without a composition, Seal does nothing and an unsealed frame is not published. */
	xvt_cockpit_seal(&g_crt);
	xvt_cockpit_presented(0);
	XVT_ASSERT_INT_EQ(presented()->presentation_serial, 0);

	/* A new frame unseals. */
	compose();
	xvt_cockpit_seal(&g_crt);
	xvt_cockpit_begin_frame();
	xvt_cockpit_presented(0);
	XVT_ASSERT_INT_EQ(presented()->presentation_serial, 0);
}

static void check_begin_frame_drops_pending_crt(void)
{
	cockpit_capture_world();
	compose();
	xvt_cockpit_seal(&g_crt);
	xvt_cockpit_begin_frame();
	/* Published as a standalone overlay, the pending frame no longer has a valid CRT. */
	xvt_cockpit_presented(1);
	XVT_ASSERT_INT_EQ(presented()->crt.valid, 0);
}

static void check_refresh_local_only(void)
{
	cockpit_capture_world();
	/* Another player's refresh leaves working invalid, so no composition is selected. */
	xvt_cockpit_begin_frame();
	xvt_cockpit_refresh_instruments(LOCAL + 1);
	xvt_cockpit_refresh_instruments(8);
	xvt_cockpit_latch_composition();
	xvt_cockpit_seal(&g_crt);
	xvt_cockpit_presented(0);
	XVT_ASSERT_INT_EQ(presented()->presentation_serial, 0);
	XVT_ASSERT_INT_EQ(xvt_cockpit_loading_assets_ready(), 0);
}

static void check_working_follows_layout(void)
{
	cockpit_capture_world();
	xvt_render_cockpit_reset();
	compose();
	xvt_cockpit_seal(&g_crt);
	xvt_cockpit_presented(0);
	XVT_ASSERT_INT_EQ(presented()->presentation_serial, 0);

	xvt_render_assets_capture_cockpit(0);
	compose();
	const struct xvt_cockpit_state *state = seal_and_present();
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_INT_EQ(state->view.screen_width, 640);

	/* Once the layout is gone again, a refresh leaves working invalid, so LatchComposition copies nothing:
	 * the frame sealed next still holds the old view, not the new screen width. */
	xvt_render_cockpit_reset();
	g_screen_width = 800;
	compose();
	XVT_ASSERT_INT_EQ(seal_and_present()->view.screen_width, 640);

	/* With the layout back, the refresh is taken again. */
	xvt_render_assets_capture_cockpit(0);
	compose();
	XVT_ASSERT_INT_EQ(seal_and_present()->view.screen_width, 800);
}

static void check_loading_assets_ready(void)
{
	cockpit_capture_world();
	compose();
	uint64_t generation =
		seal_and_present()->definition.resource_generation;
	XVT_ASSERT_INT_EQ(xvt_cockpit_loading_assets_ready(), 0);
	xvt_cockpit_resources_prepared(generation + 1);
	XVT_ASSERT_INT_EQ(xvt_cockpit_loading_assets_ready(), 0);
	xvt_cockpit_resources_prepared(generation);
	XVT_ASSERT_INT_EQ(xvt_cockpit_loading_assets_ready(), 1);

	/* Reset clears both working and the prepared mark. */
	xvt_cockpit_reset();
	XVT_ASSERT_INT_EQ(xvt_cockpit_loading_assets_ready(), 0);
	xvt_cockpit_resources_prepared(generation);
	XVT_ASSERT_INT_EQ(xvt_cockpit_loading_assets_ready(), 0);
}

static int all_zero(const void *data, size_t size)
{
	const unsigned char *bytes = data;
	for (size_t index = 0; index < size; ++index) {
		if (bytes[index]) {
			return 0;
		}
	}
	return 1;
}

static void check_reset(void)
{
	cockpit_capture_world();
	compose();
	xvt_cockpit_messages_record_progress(1, 0, 0, 10, 2, 5);
	XVT_ASSERT_INT_EQ(seal_and_present()->valid, 1);

	xvt_cockpit_reset();
	const struct xvt_cockpit_state *state = presented();
	XVT_ASSERT_INT_EQ(all_zero(state, offsetof(struct xvt_cockpit_state,
						   page_content)),
			  1);
	XVT_ASSERT_INT_EQ(state->page_content.glyph_count, 0);
	XVT_ASSERT_INT_EQ(state->overlay_content.glyph_count, 0);
	/* The messages were reset with it: the progress bar is gone. */
	xvt_cockpit_presented(1);
	XVT_ASSERT_INT_EQ(presented()->loading.progress_visible, 0);
}

static void check_standalone_overlay(void)
{
	cockpit_capture_world();
	xvt_render_cockpit_reset();
	/* No composition and nothing to show: published, but not valid. */
	xvt_cockpit_presented(1);
	const struct xvt_cockpit_state *state = presented();
	uint64_t serial = state->presentation_serial;
	XVT_ASSERT_TRUE(serial != 0);
	XVT_ASSERT_INT_EQ(state->valid, 0);
	/* Without a valid layout it first captured the cockpit definition. */
	xvt_render_cockpit_capture_definition(&g_expected.definition);
	XVT_ASSERT_INT_EQ(memcmp(&state->definition, &g_expected.definition,
				 sizeof state->definition),
			  0);

	/* With the loading display showing it is valid, and each publication has a new serial. */
	xvt_cockpit_messages_record_progress(1, 0, 0, 10, 2, 5);
	xvt_cockpit_presented(1);
	state = presented();
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_INT_EQ(state->loading.progress_visible, 1);
	XVT_ASSERT_TRUE(state->presentation_serial != serial);

	/* The alert counts as well. */
	cockpit_capture_world();
	xvt_cockpit_messages_begin_alert_line(1, 0, 0, 100, 20);
	xvt_cockpit_messages_end_alert_line();
	xvt_cockpit_presented(1);
	XVT_ASSERT_INT_EQ(presented()->valid, 1);
}

static void check_generations(void)
{
	cockpit_capture_world();
	xvt_cockpit_messages_record_progress(1, 0, 0, 10, 2, 5);
	xvt_cockpit_presented(1);
	xvt_cockpit_export(&g_expected);

	/* Nothing changed: no generation rises. */
	xvt_cockpit_presented(1);
	const struct xvt_cockpit_state *state = presented();
	XVT_ASSERT_INT_EQ(state->definition_generation,
			  g_expected.definition_generation);
	XVT_ASSERT_INT_EQ(state->palette_generation,
			  g_expected.palette_generation);
	XVT_ASSERT_INT_EQ(state->artwork_generation,
			  g_expected.artwork_generation);
	XVT_ASSERT_INT_EQ(state->instruments_generation,
			  g_expected.instruments_generation);
	XVT_ASSERT_INT_EQ(state->radar_generation, g_expected.radar_generation);
	XVT_ASSERT_INT_EQ(state->text_generation, g_expected.text_generation);
	XVT_ASSERT_INT_EQ(state->crt_generation, g_expected.crt_generation);

	/* The progress bar moved: the text generation rises, and only it. */
	xvt_cockpit_messages_record_progress(1, 0, 0, 10, 2, 6);
	xvt_cockpit_presented(1);
	state = presented();
	XVT_ASSERT_TRUE(state->text_generation > g_expected.text_generation);
	XVT_ASSERT_INT_EQ(state->definition_generation,
			  g_expected.definition_generation);
	XVT_ASSERT_INT_EQ(state->palette_generation,
			  g_expected.palette_generation);
	XVT_ASSERT_INT_EQ(state->artwork_generation,
			  g_expected.artwork_generation);
	XVT_ASSERT_INT_EQ(state->instruments_generation,
			  g_expected.instruments_generation);
	XVT_ASSERT_INT_EQ(state->crt_generation, g_expected.crt_generation);

	/* A composed frame brings a new palette and a sealed CRT: those generations rise. */
	xvt_cockpit_export(&g_expected);
	compose();
	state = seal_and_present();
	XVT_ASSERT_TRUE(state->palette_generation >
			g_expected.palette_generation);
	XVT_ASSERT_TRUE(state->crt_generation > g_expected.crt_generation);
}

static void check_latch_pages(void)
{
	cockpit_capture_world();
	g_mfd_page_states[MFD_PAGE_SCOREBOARD] = MFD_PAGE_STATE_CLOSED;
	g_mfd_page_states[MFD_PAGE_GOALS] = MFD_PAGE_STATE_OPEN;
	g_mfd_page_states[MFD_PAGE_DAMAGE] = MFD_PAGE_STATE_OPEN;
	g_mfd_page_states[MFD_PAGE_MAP_HELP] = MFD_PAGE_STATE_OPEN;
	g_mfd_page_states[MFD_PAGE_MESSAGE_LOG] = MFD_PAGE_STATE_OPEN;

	/* Off the map: open pages are placed except the command page; closed ones and the log are not. */
	xvt_cockpit_latch_pages();
	xvt_cockpit_presented(1);
	const struct xvt_cockpit_page *pages = presented()->pages;
	XVT_ASSERT_INT_EQ(pages[MFD_PAGE_GOALS].visible, 1);
	XVT_ASSERT_INT_EQ(pages[MFD_PAGE_DAMAGE].visible, 1);
	XVT_ASSERT_INT_EQ(pages[MFD_PAGE_MAP_HELP].visible, 0);
	XVT_ASSERT_INT_EQ(pages[MFD_PAGE_SCOREBOARD].visible, 0);
	XVT_ASSERT_INT_EQ(pages[MFD_PAGE_MESSAGE_LOG].visible, 0);

	/* On the map: the damage page is skipped and the command page placed. */
	g_players[LOCAL].map_camera_state = 1;
	xvt_cockpit_latch_pages();
	xvt_cockpit_presented(1);
	pages = presented()->pages;
	XVT_ASSERT_INT_EQ(pages[MFD_PAGE_GOALS].visible, 1);
	XVT_ASSERT_INT_EQ(pages[MFD_PAGE_DAMAGE].visible, 0);
	XVT_ASSERT_INT_EQ(pages[MFD_PAGE_MAP_HELP].visible, 1);
}

static void check_seal_exports_latched_pages(void)
{
	cockpit_capture_world();
	g_mfd_page_states[MFD_PAGE_GOALS] = MFD_PAGE_STATE_OPEN;
	xvt_cockpit_pages_begin_section(MFD_PAGE_GOALS,
					XVT_COCKPIT_PAGE_HEADER);
	xvt_cockpit_pages_record_glyph('G', 8, 10, 0);
	xvt_cockpit_pages_end_section();
	compose();
	xvt_cockpit_latch_pages();
	const struct xvt_cockpit_state *state = seal_and_present();
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_INT_EQ(state->pages[MFD_PAGE_GOALS].visible, 1);
	XVT_ASSERT_INT_EQ(state->pages[MFD_PAGE_GOALS].glyph_count, 1);
	XVT_ASSERT_INT_EQ(state->page_content.glyph_count, 1);
	XVT_ASSERT_INT_EQ(
		state->page_content
			.glyphs[state->pages[MFD_PAGE_GOALS].first_glyph]
			.character,
		'G');
}

static void check_latch_messages(void)
{
	cockpit_capture_world();
	/* A closed message log is placed hidden. */
	xvt_cockpit_latch_messages();
	xvt_cockpit_presented(1);
	XVT_ASSERT_INT_EQ(presented()->pages[MFD_PAGE_MESSAGE_LOG].visible, 0);

	/* Open, it shows; with a player count of 1 or more it is wider. */
	g_mfd_page_states[MFD_PAGE_MESSAGE_LOG] = MFD_PAGE_STATE_OPEN;
	xvt_cockpit_latch_messages();
	xvt_cockpit_presented(1);
	const struct xvt_cockpit_page *page =
		&presented()->pages[MFD_PAGE_MESSAGE_LOG];
	XVT_ASSERT_INT_EQ(page->visible, 1);
	int narrow = page->placement.width;
	g_flight_player_count = 1;
	xvt_cockpit_latch_messages();
	xvt_cockpit_presented(1);
	XVT_ASSERT_TRUE(
		presented()->pages[MFD_PAGE_MESSAGE_LOG].placement.width >
		narrow);

	/* In a composed frame the open log is latched, so the sealed frame shows it. */
	compose();
	xvt_cockpit_latch_messages();
	XVT_ASSERT_INT_EQ(
		seal_and_present()->pages[MFD_PAGE_MESSAGE_LOG].visible, 1);
}

static void check_latch_launcher(void)
{
	cockpit_capture_world();
	xvt_cockpit_readouts_record_number(
		(xvt_cockpit_number_id)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST + 2),
		7, 2, 1);
	xvt_cockpit_latch_launcher(2, 100, 110, 20, 10);
	/* Launchers from 4 up are ignored. */
	xvt_cockpit_latch_launcher(4, 1, 1, 1, 1);
	xvt_cockpit_presented(1);
	const struct xvt_cockpit_weapons *weapons = &presented()->weapons;
	const struct xvt_cockpit_number *count = &weapons->launchers[2].count;
	XVT_ASSERT_INT_EQ(weapons->launchers[2].visible, 1);
	XVT_ASSERT_INT_EQ(count->value, 7);
	XVT_ASSERT_INT_EQ(count->x, 100);
	XVT_ASSERT_INT_EQ(count->y, 110);
	XVT_ASSERT_INT_EQ(count->bounds.x, 100);
	XVT_ASSERT_INT_EQ(count->bounds.y, 110);
	XVT_ASSERT_INT_EQ(count->bounds.width, 20);
	XVT_ASSERT_INT_EQ(count->bounds.height, 10);
	XVT_ASSERT_INT_EQ(count->phase, XVT_COCKPIT_AFTER_CRT);
	XVT_ASSERT_INT_EQ(count->keyed, 1);
	XVT_ASSERT_INT_EQ(count->color_key_argb, xvt_render_draw_color(BYPASS));
	XVT_ASSERT_INT_EQ(weapons->launchers[3].visible, 0);
	XVT_ASSERT_INT_EQ(all_zero(&weapons->lock_indicator,
				   sizeof weapons->lock_indicator),
			  1);
	XVT_ASSERT_INT_EQ(all_zero(&g_out.target, sizeof g_out.target), 1);
}

static void capture_ready_message(void)
{
	xvt_cockpit_messages_begin_message(0);
	xvt_cockpit_messages_record_glyph('M', 8, 10, 0);
	xvt_cockpit_messages_end_message();
}

static void check_latch_message(void)
{
	cockpit_capture_world();
	capture_ready_message();
	xvt_cockpit_text_record_field(XVT_COCKPIT_TEXT_NETWORK_PING, "12",
				      XVT_COCKPIT_ALIGN_LEFT);
	xvt_cockpit_text_record_field(XVT_COCKPIT_TEXT_NETWORK_LAG, "3",
				      XVT_COCKPIT_ALIGN_LEFT);

	/* The ready pane is latched while the log is closed, and moves the ping and lag fields with it. */
	xvt_cockpit_latch_message(XVT_COCKPIT_MESSAGE_READY, 40, 55, 300, 400,
				  120, 30);
	xvt_cockpit_presented(1);
	const struct xvt_cockpit_state *state = presented();
	XVT_ASSERT_INT_EQ(
		state->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 1);
	XVT_ASSERT_INT_EQ(
		state->messages.panes[XVT_COCKPIT_MESSAGE_READY].placement.x,
		300);
	const struct xvt_cockpit_text_field *ping =
		&state->text_fields[XVT_COCKPIT_TEXT_NETWORK_PING];
	const struct xvt_cockpit_text_field *lag =
		&state->text_fields[XVT_COCKPIT_TEXT_NETWORK_LAG];
	XVT_ASSERT_INT_EQ(strcmp(ping->caption.text, "12"), 0);
	XVT_ASSERT_INT_EQ(ping->x, 70 + (300 - 40));
	XVT_ASSERT_INT_EQ(ping->y, 80 + (400 - 55));
	XVT_ASSERT_INT_EQ(ping->bounds.x, 50 + (300 - 40));
	XVT_ASSERT_INT_EQ(ping->bounds.y, 60 + (400 - 55));
	XVT_ASSERT_INT_EQ(strcmp(lag->caption.text, "3"), 0);
	XVT_ASSERT_INT_EQ(lag->y, 80 + (400 - 55));

	/* While the log is open the ready pane is not latched, but the fields are still placed. */
	cockpit_capture_world();
	capture_ready_message();
	xvt_cockpit_text_record_field(XVT_COCKPIT_TEXT_NETWORK_PING, "12",
				      XVT_COCKPIT_ALIGN_LEFT);
	g_mfd_page_states[MFD_PAGE_MESSAGE_LOG] = MFD_PAGE_STATE_OPEN;
	xvt_cockpit_latch_message(XVT_COCKPIT_MESSAGE_READY, 40, 55, 300, 400,
				  120, 30);
	xvt_cockpit_messages_begin_message(3);
	xvt_cockpit_messages_record_glyph('S', 8, 10, 0);
	xvt_cockpit_messages_end_message();
	xvt_cockpit_latch_message(XVT_COCKPIT_MESSAGE_SYSTEM, 50, 60, 0, 0, 100,
				  20);
	xvt_cockpit_presented(1);
	state = presented();
	XVT_ASSERT_INT_EQ(
		state->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 0);
	XVT_ASSERT_INT_EQ(
		state->messages.panes[XVT_COCKPIT_MESSAGE_SYSTEM].visible, 1);
	XVT_ASSERT_INT_EQ(state->text_fields[XVT_COCKPIT_TEXT_NETWORK_PING].y,
			  80 + (400 - 55));
}

static void check_placed_panes_hidden(void)
{
	cockpit_capture_world();
	capture_ready_message();
	compose();
	xvt_cockpit_latch_message(XVT_COCKPIT_MESSAGE_READY, 50, 60, 0, 0, 100,
				  20);
	XVT_ASSERT_INT_EQ(seal_and_present()
				  ->messages.panes[XVT_COCKPIT_MESSAGE_READY]
				  .visible,
			  1);

	/* A new composition hides the placed panes until they are latched again. */
	compose();
	XVT_ASSERT_INT_EQ(seal_and_present()
				  ->messages.panes[XVT_COCKPIT_MESSAGE_READY]
				  .visible,
			  0);

	/* So does BeginMessagePlacement. */
	compose();
	xvt_cockpit_latch_message(XVT_COCKPIT_MESSAGE_READY, 50, 60, 0, 0, 100,
				  20);
	xvt_cockpit_begin_message_placement();
	XVT_ASSERT_INT_EQ(seal_and_present()
				  ->messages.panes[XVT_COCKPIT_MESSAGE_READY]
				  .visible,
			  0);
}

static void check_retain_presented_frame(void)
{
	cockpit_capture_world();
	xvt_cockpit_latch_launcher(1, 10, 10, 5, 5);
	xvt_cockpit_presented(1);
	/* A change to pending is dropped when pending restarts from the presented frame. */
	xvt_cockpit_latch_launcher(3, 20, 20, 5, 5);
	xvt_cockpit_retain_presented_frame();
	xvt_cockpit_presented(1);
	const struct xvt_cockpit_state *state = presented();
	XVT_ASSERT_INT_EQ(state->weapons.launchers[1].visible, 1);
	XVT_ASSERT_INT_EQ(state->weapons.launchers[3].visible, 0);

	/* It unseals: a sealed composition is not published after it. */
	uint64_t serial = state->presentation_serial;
	compose();
	xvt_cockpit_seal(&g_crt);
	xvt_cockpit_retain_presented_frame();
	xvt_cockpit_presented(0);
	XVT_ASSERT_INT_EQ(presented()->presentation_serial, serial);
}

static void check_export_resources_refusals(void)
{
	static struct xvt_cockpit_resources resources;
	cockpit_capture_world();
	/* Working is invalid. */
	g_hud_cockpit_resources_loaded = 1;
	memset(&resources, 0xAB, sizeof resources);
	xvt_cockpit_export_resources(&resources);
	XVT_ASSERT_INT_EQ(all_zero(&resources, sizeof resources), 1);

	/* Working is valid, but the cockpit resources are not loaded. */
	compose();
	g_hud_cockpit_resources_loaded = 0;
	memset(&resources, 0xAB, sizeof resources);
	xvt_cockpit_export_resources(&resources);
	XVT_ASSERT_INT_EQ(all_zero(&resources, sizeof resources), 1);

	/* Loaded, but panel 0 has no asset: invalid. That it is also cleared is a known failure below. */
	g_hud_cockpit_resources_loaded = 1;
	memset(&resources, 0xAB, sizeof resources);
	xvt_cockpit_export_resources(&resources);
	XVT_ASSERT_INT_EQ(resources.valid, 0);
}

/* Known failure: with panel 0 unbound the output is left invalid but not cleared. ExportResources captures
 * the cockpit definition into it before it looks at panel 0, and returns with that definition in place. */
static void check_export_resources_cleared_without_panel(void)
{
	static struct xvt_cockpit_resources resources;
	cockpit_capture_world();
	compose();
	g_hud_cockpit_resources_loaded = 1;
	memset(&resources, 0xAB, sizeof resources);
	xvt_cockpit_export_resources(&resources);
	XVT_ASSERT_INT_EQ(all_zero(&resources, sizeof resources), 1);
}

int main(int argc, char **argv)
{
	/* "known-failure <check>" runs one check the code is known to fail; an unknown name runs nothing. */
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		if (strcmp(argv[2], "export_resources_without_panel") == 0) {
			check_export_resources_cleared_without_panel();
		}
		return 0;
	}
	check_copy_state();
	check_compose_seal_present();
	check_nothing_published_unsealed();
	check_begin_frame_drops_pending_crt();
	check_refresh_local_only();
	check_working_follows_layout();
	check_loading_assets_ready();
	check_reset();
	check_standalone_overlay();
	check_generations();
	check_latch_pages();
	check_seal_exports_latched_pages();
	check_latch_messages();
	check_latch_launcher();
	check_latch_message();
	check_placed_panes_hidden();
	check_retain_presented_frame();
	check_export_resources_refusals();
	xvt_cockpit_reset();
	return 0;
}
