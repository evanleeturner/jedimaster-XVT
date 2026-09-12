#include "xvt_runtime/snapshot/cockpit_capture.h"

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
#include <stddef.h>
#include <string.h>

/* All capture and publication runs on the host thread. Composition selects the
 * working content before the original can refresh it for another presentation. */
static XvtCockpitState g_working, g_pending, g_completed;
static uint64_t g_presentationSerial;
static uint64_t g_preparedResources;
static int g_compositionSelected, g_sealed;

void XvtCockpit_CopyState(XvtCockpitState* destination, const XvtCockpitState* source) {
	memcpy(destination, source, offsetof(XvtCockpitState, page_content));
	destination->page_content.row_count = source->page_content.row_count;
	destination->page_content.glyph_count = source->page_content.glyph_count;
	memcpy(destination->page_content.rows, source->page_content.rows,
		   source->page_content.row_count * sizeof source->page_content.rows[0]);
	memcpy(destination->page_content.glyphs, source->page_content.glyphs,
		   source->page_content.glyph_count * sizeof source->page_content.glyphs[0]);
	destination->overlay_content.glyph_count = source->overlay_content.glyph_count;
	memcpy(destination->overlay_content.glyphs, source->overlay_content.glyphs,
		   source->overlay_content.glyph_count * sizeof source->overlay_content.glyphs[0]);
}

void XvtCockpit_Reset(void) {
	g_preparedResources = 0;
	XvtCockpitText_ResetFields();
	XvtCockpitReadouts_Reset();
	XvtCockpitPages_Reset();
	XvtCockpitMessages_Reset();
	memset(&g_working, 0, sizeof g_working);
	memset(&g_pending, 0, sizeof g_pending);
	memset(&g_completed, 0, sizeof g_completed);
	g_compositionSelected = g_sealed = 0;
}

static void CaptureView(XvtCockpitView* view) {
	const PlayerData* player = &g_players[g_localPlayer];
	memset(view, 0, sizeof *view);
	view->screen_width = (uint16_t)g_screenWidth;
	view->screen_height = (uint16_t)g_screenHeight;
	view->hud_state = player->viewState.hudStateLive;
	view->instrument_base = g_hudInstrumentSetBaseIndex;
	view->panel_set = g_hudPanelSetId;
	view->active_page = g_mfdActivePage;
	view->secondary_page = g_mfdSecondaryPage;
	view->map_active = player->mapCameraState != 0;
	view->external_camera = player->viewState.externalCameraActive != 0;
	view->mission_ending = g_flightMissionState.missionEndPending != 0;
	view->region_session = player->regionSessionId != 0;
	view->instruments_visible = !view->mission_ending && !view->region_session;
	view->viewport = (XvtSnapRect) { g_flightVpX, g_flightVpY, g_flightVpWidth, g_flightVpHeight };
	view->projection_offset_y = g_projOffsetY;
	unsigned resource = view->hud_state;
	if (resource < 28 && resource != HUD_VIEW_FULL_SCREEN) {
		unsigned reference = g_hudCockpitResourceDescriptors[resource].enabled;
		if (reference >= 0xc0) {
			resource = reference - 0xc0;
			view->mirrored = 1;
		} else if (reference >= 0x80) {
			resource = reference - 0x80;
		}
	}
	view->resource_descriptor = (uint16_t)resource;
	view->viewport_descriptor = view->mirrored ? view->hud_state : (uint16_t)resource;
}

void XvtCockpit_RefreshInstruments(int player) {
	if (player != g_localPlayer || (unsigned)player >= 8)
		return;
	CaptureView(&g_working.view);
	XvtRenderCockpit_CaptureDefinition(&g_working.definition);
	XvtCockpitInstruments_Build(&g_working);
	int mouse_x = 0, mouse_y = 0;
	g_working.mouse_stick_visible =
		XvtMouseFlight_GetHudMarker(&mouse_x, &mouse_y) && !g_working.view.map_active &&
		!g_working.view.external_camera &&
		(g_working.view.hud_state == HUD_VIEW_FORWARD || g_working.view.hud_state == HUD_VIEW_HUD_ONLY) &&
		g_working.view.instruments_visible;
	g_working.mouse_stick_x = (int8_t)mouse_x;
	g_working.mouse_stick_y = (int8_t)mouse_y;
	XvtCockpitReadouts_CopyState(&g_working);
	XvtCockpitText_CopyFields(&g_working);
	for (unsigned index = 0; index < 256; ++index)
		g_working.palette_argb[index] = XvtRenderDraw_Color(index);
	g_working.valid = g_working.definition.layout.valid;
}

void XvtCockpit_BeginFrame(void) {
	XvtCockpitMessages_BeginFlightFrame();
	XvtCockpitPages_BeginFrame();
	/* A software blit after the previous flip may already have selected the next
	 * frame's cockpit. Keep that selection until another composition replaces it. */
	g_pending.crt.valid = 0;
	g_sealed = 0;
}

void XvtCockpit_LatchComposition(void) {
	if (!g_working.valid)
		return;
	XvtCockpit_CopyState(&g_pending, &g_working);
	XvtCockpitMessages_BeginPlacement();
	g_compositionSelected = 1;
	g_sealed = 0;
}

static void SelectPagePlacement(XvtCockpitPage* page, unsigned index) {
	unsigned binding = 0;
	int width = 0, height = 0;
	int map = g_players[g_localPlayer].mapCameraState != 0;
	memset(page, 0, sizeof *page);
	page->page_id = (uint16_t)index;
	page->original_state = g_mfdPageStates[index];
	page->focused = g_mfdActivePage == index;
	page->layer = XVT_COCKPIT_AFTER_CRT;
	if (page->original_state == MFD_PAGE_STATE_CLOSED)
		return;
	switch (index) {
		case MFD_PAGE_SCOREBOARD:
			binding = HUD_MFD_SCOREBOARD_ELEMENT;
			width = g_mfdMissionScoreboardBlitWidth;
			height = g_mfdMissionScoreboardBlitHeight;
			break;
		case MFD_PAGE_GOALS:
			binding = HUD_MFD_GOALS_ELEMENT;
			width = g_mfdGoalsBlitWidth;
			height = g_mfdGoalsBlitHeight;
			break;
		case MFD_PAGE_DAMAGE:
			if (map)
				return;
			binding = HUD_MFD_DAMAGE_ELEMENT;
			width = g_mfdDamageBlitWidth;
			height = g_mfdDamageBlitHeight;
			break;
		case MFD_PAGE_FLIGHT_GROUPS:
		case MFD_PAGE_FRIENDLY_CRAFT:
			binding = HUD_MFD_CRAFT_LIST_ELEMENT;
			width = g_mfdCraftListBlitWidth;
			height = g_mfdCraftListBlitHeight;
			break;
		case MFD_PAGE_COMMAND:
			if (!map)
				return;
			binding = HUD_MFD_MAP_OR_COMMAND_ELEMENT;
			width = g_hudElementLayouts[g_hudInstrumentSetBaseIndex + binding].clipWidth;
			height = g_hudElementLayouts[g_hudInstrumentSetBaseIndex + binding].clipHeightOrForegroundColor;
			break;
		default:
			return;
	}
	if (map && index != MFD_PAGE_FRIENDLY_CRAFT) {
		binding = HUD_MFD_MAP_OR_COMMAND_ELEMENT;
		width = g_mfdMapBlitWidth;
		height = g_mfdMapBlitHeight;
	}
	page->layout_id = (uint16_t)(g_hudInstrumentSetBaseIndex + binding);
	const HudElementLayout* layout = &g_hudElementLayouts[page->layout_id];
	page->placement = (XvtSnapRect) { layout->x, layout->y, width, height };
	page->visible = width > 0 && height > 0;
}

void XvtCockpit_LatchPages(void) {
	if ((unsigned)g_localPlayer >= 8)
		return;
	for (unsigned index = 0; index < MFD_PAGE_COUNT; ++index)
		if (index != MFD_PAGE_MESSAGE_LOG) {
			SelectPagePlacement(&g_pending.pages[index], index);
			if (g_pending.pages[index].visible)
				XvtCockpitPages_Latch(index);
		}
}

void XvtCockpit_LatchLauncher(unsigned launcher, int x, int y, int width, int height) {
	if (launcher >= 4)
		return;
	XvtCockpitNumber* number = &g_pending.weapons.launchers[launcher].count;
	XvtCockpitReadouts_CopyLauncher(number, launcher);
	number->x = (int16_t)x;
	number->y = (int16_t)y;
	number->bounds = (XvtSnapRect) { x, y, width, height };
	number->phase = XVT_COCKPIT_AFTER_CRT;
	number->keyed = 1;
	number->color_key_argb = XvtRenderDraw_Color(g_flightColorEscapeBypassChar);
	g_pending.weapons.launchers[launcher].visible = 1;
}

void XvtCockpit_LatchMessages(void) {
	if ((unsigned)g_localPlayer >= 8)
		return;
	XvtCockpitPage* page = &g_pending.pages[MFD_PAGE_MESSAGE_LOG];
	memset(page, 0, sizeof *page);
	page->page_id = MFD_PAGE_MESSAGE_LOG;
	page->original_state = g_mfdPageStates[MFD_PAGE_MESSAGE_LOG];
	page->visible = page->original_state != MFD_PAGE_STATE_CLOSED;
	page->focused = g_mfdActivePage == MFD_PAGE_MESSAGE_LOG;
	page->layout_id = g_hudInstrumentSetBaseIndex + HUD_MFD_MESSAGE_LOG_ELEMENT;
	const HudElementLayout* layout = &g_hudElementLayouts[page->layout_id];
	page->placement = (XvtSnapRect) { layout->x, layout->y, g_readyMessagePaneRight - g_readyMessagePaneLeft,
									  g_readyMessagePaneBottom - g_readyMessagePaneTop };
	page->layer = XVT_COCKPIT_AFTER_CRT;
	if (g_flightPlayerCount >= 1)
		page->placement.width += 4 * g_flightFontHalfHeight + 1;
	if (page->visible)
		XvtCockpitPages_Latch(MFD_PAGE_MESSAGE_LOG);
}

void XvtCockpit_BeginMessagePlacement(void) { XvtCockpitMessages_BeginPlacement(); }

void XvtCockpit_LatchMessage(XvtCockpitMessageId pane, int source_x, int source_y, int x, int y, int width,
							 int height) {
	if (pane != XVT_COCKPIT_MESSAGE_READY || g_mfdPageStates[MFD_PAGE_MESSAGE_LOG] == MFD_PAGE_STATE_CLOSED)
		XvtCockpitMessages_Latch(pane, source_x, source_y, x, y, width, height);
	if (pane == XVT_COCKPIT_MESSAGE_READY) {
		XvtCockpitText_CopyPlacedField(&g_pending.text_fields[XVT_COCKPIT_TEXT_NETWORK_PING],
									   XVT_COCKPIT_TEXT_NETWORK_PING, x - source_x, y - source_y);
		XvtCockpitText_CopyPlacedField(&g_pending.text_fields[XVT_COCKPIT_TEXT_NETWORK_LAG],
									   XVT_COCKPIT_TEXT_NETWORK_LAG, x - source_x, y - source_y);
	}
}

void XvtCockpit_RetainPresentedFrame(void) {
	XvtCockpit_CopyState(&g_pending, &g_completed);
	g_sealed = 0;
}

void XvtCockpit_Seal(const XvtSnapPreview* crt) {
	if (!g_compositionSelected)
		return;
	g_pending.crt = *crt;
	XvtCockpitPages_Export(&g_pending);
	/* Primitive order is unrelated to the CRT scene's visual dependencies. */
	g_sealed = 1;
}

static uint64_t UpdateGeneration(uint64_t generation, const void* current, const void* previous,
								 size_t bytes) {
	return generation + (generation == 0 || memcmp(current, previous, bytes) != 0);
}

void XvtCockpit_Presented(int standalone_overlay) {
	if (!g_sealed && !standalone_overlay)
		return;
	if (standalone_overlay && !g_pending.definition.layout.valid)
		XvtRenderCockpit_CaptureDefinition(&g_pending.definition);
	XvtCockpitMessages_Export(&g_pending);
	if (!g_compositionSelected &&
		(g_pending.alert.active || g_pending.loading.visible || g_pending.loading.text_visible))
		g_pending.valid = 1;
	g_pending.definition_generation =
		UpdateGeneration(g_completed.definition_generation, &g_pending.definition, &g_completed.definition,
						 sizeof g_pending.definition);
	g_pending.palette_generation = UpdateGeneration(g_completed.palette_generation, g_pending.palette_argb,
													g_completed.palette_argb, sizeof g_pending.palette_argb);
	g_pending.artwork_generation = UpdateGeneration(g_completed.artwork_generation, &g_pending.view,
													&g_completed.view, sizeof g_pending.view);
	g_pending.instruments_generation =
		UpdateGeneration(g_completed.instruments_generation, &g_pending.systems, &g_completed.systems,
						 offsetof(XvtCockpitState, radar) - offsetof(XvtCockpitState, systems));
	g_pending.radar_generation = UpdateGeneration(g_completed.radar_generation, &g_pending.radar,
												  &g_completed.radar, sizeof g_pending.radar);
	g_pending.text_generation =
		g_completed.text_generation +
		(!g_completed.text_generation ||
		 memcmp(g_pending.text_fields, g_completed.text_fields, sizeof g_pending.text_fields) ||
		 memcmp(&g_pending.readouts, &g_completed.readouts, sizeof g_pending.readouts) ||
		 memcmp(&g_pending.proving_grounds, &g_completed.proving_grounds, sizeof g_pending.proving_grounds) ||
		 memcmp(g_pending.pages, g_completed.pages, sizeof g_pending.pages));
	for (unsigned pane = 0; pane < XVT_COCKPIT_MESSAGE_COUNT; ++pane) {
		const XvtCockpitMessage* current = &g_pending.messages.panes[pane];
		const XvtCockpitMessage* previous = &g_completed.messages.panes[pane];
		if (current->generation != previous->generation || current->visible != previous->visible ||
			memcmp(&current->placement, &previous->placement, sizeof current->placement)) {
			g_pending.text_generation = g_completed.text_generation + 1;
			break;
		}
	}
	if (memcmp(&g_pending.alert, &g_completed.alert, sizeof g_pending.alert) ||
		memcmp(&g_pending.loading, &g_completed.loading, sizeof g_pending.loading))
		g_pending.text_generation = g_completed.text_generation + 1;
	g_pending.crt_generation =
		UpdateGeneration(g_completed.crt_generation, &g_pending.crt, &g_completed.crt, sizeof g_pending.crt);
	g_pending.presentation_serial = ++g_presentationSerial;
	XvtCockpit_CopyState(&g_completed, &g_pending);
	g_sealed = 0;
}

void XvtCockpit_Export(XvtCockpitState* destination) { XvtCockpit_CopyState(destination, &g_completed); }

void XvtCockpit_ResourcesPrepared(uint64_t generation) { g_preparedResources = generation; }

int XvtCockpit_LoadingAssetsReady(void) {
	return g_working.valid && g_preparedResources == g_working.definition.resource_generation;
}

void XvtCockpit_ExportResources(XvtCockpitResources* out) {
	memset(out, 0, sizeof *out);
	if (!g_working.valid || !g_hudCockpitResourcesLoaded)
		return;
	XvtRenderCockpit_CaptureDefinition(&out->definition);
	if (!out->definition.panels[0].asset_id)
		return;
	out->view.screen_width = g_working.view.screen_width;
	out->view.screen_height = g_working.view.screen_height;
	out->view.compact_instruments = g_working.view.compact_instruments;
	out->view.laser_slots = g_working.view.laser_slots;
	out->installed_hud_features = g_working.systems.installed_hud_features;
	for (unsigned color = 0; color < 256; ++color)
		out->palette[color] = XvtRenderDraw_Color(color);
	for (unsigned view = 0; view < 28; ++view) {
		if (!out->definition.layout.descriptors[view].lfd_asset_id)
			continue;
		RgbTriplet* rgb = (RgbTriplet*)g_hudCockpitResources[view].entries[2];
		if (!rgb)
			continue;
		for (unsigned color = 0; color < 64; ++color) {
			uint32_t r = rgb[color].r & 63, g = rgb[color].g & 63, b = rgb[color].b & 63;
			r = (r << 2) | (r >> 4);
			g = (g << 2) | (g >> 4);
			b = (b << 2) | (b >> 4);
			out->view_palette[view][color] = 0xff000000u | r << 16 | g << 8 | b;
		}
	}
	out->valid = 1;
}
