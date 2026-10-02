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

#include <stddef.h>
#include <string.h>

enum { LOCAL = 0, BYPASS = 9, BACKGROUND = 4 };

static XvtCockpitState g_out, g_expected;
static XvtSnapPreview g_crt;
static uint8_t g_framebuffer[16];

static void Palette(void) {
	for (unsigned index = 0; index < 256; ++index)
		g_swPalette[index] = (RgbTriplet) { (uint8_t)(index & 63), (uint8_t)(index >> 6), 7 };
}

/* The local player in seat 0 has no craft and looks forward with the map closed. The goals, damage and
 * command pages have sizes; the message log pane is 100 by 50. The cockpit layout is valid. */
static void World(void) {
	Palette();
	g_screenWidth = 640;
	g_screenHeight = 480;
	memset(g_players, 0, sizeof g_players);
	for (int i = 0; i < 8; ++i)
		g_players[i].objectIndex = -1;
	g_localPlayer = LOCAL;
	g_players[LOCAL].currentTargetObjectIdx = -1;
	g_players[LOCAL].viewState.hudStateLive = HUD_VIEW_FORWARD;
	memset(&g_flightMissionState, 0, sizeof g_flightMissionState);
	memset(g_hudCockpitResourceDescriptors, 0, sizeof g_hudCockpitResourceDescriptors);
	g_hudCockpitResourcesLoaded = 0;
	g_flightPlayerCount = 0;
	g_flightFontDigitWidth = 4;

	memset(g_hudElementLayouts, 0, sizeof g_hudElementLayouts);
	g_hudInstrumentSetBaseIndex = HUD_COCKPIT_INSTRUMENT_BASE_INDEX;
	g_hudElementLayouts[HUD_MFD_GOALS_ELEMENT] = (HudElementLayout) { .x = 10, .y = 20 };
	g_hudElementLayouts[HUD_MFD_DAMAGE_ELEMENT] = (HudElementLayout) { .x = 30, .y = 40 };
	g_hudElementLayouts[HUD_MFD_MAP_OR_COMMAND_ELEMENT] =
		(HudElementLayout) { .x = 50, .y = 60, .clipWidth = 70, .clipHeightOrForegroundColor = 80 };
	g_hudElementLayouts[HUD_MFD_MESSAGE_LOG_ELEMENT] = (HudElementLayout) { .x = 90, .y = 100 };
	memset(g_mfdPageStates, 0, sizeof g_mfdPageStates);
	g_mfdActivePage = MFD_PAGE_NONE;
	g_mfdGoalsBlitWidth = 50;
	g_mfdGoalsBlitHeight = 40;
	g_mfdDamageBlitWidth = 60;
	g_mfdDamageBlitHeight = 30;
	g_mfdMapBlitWidth = 70;
	g_mfdMapBlitHeight = 20;
	g_mfdMissionScoreboardBlitWidth = g_mfdMissionScoreboardBlitHeight = 10;
	g_mfdCraftListBlitWidth = g_mfdCraftListBlitHeight = 10;
	g_readyMessagePaneLeft = 10;
	g_readyMessagePaneTop = 20;
	g_readyMessagePaneRight = 110;
	g_readyMessagePaneBottom = 70;

	g_flightClipLeft = 50;
	g_flightClipTop = 60;
	g_flightClipRight = 450;
	g_flightClipBottom = 300;
	g_flightCursorX = 70;
	g_flightCursorY = 80;
	g_flightTextBgColor = BACKGROUND;
	g_flightTransparentColorIndex = BYPASS;
	g_flightOffscreenBuffer = NULL;
	g_flightSwFramebufferBase = g_framebuffer;
	memset(g_readyMessagePaneQueue, 0, sizeof g_readyMessagePaneQueue);
	g_readyMessagePaneQueue[0].stateOrMessageId = 11;
	g_systemMessagePane.stateOrMessageId = 22;

	XvtRenderCockpit_Reset();
	XvtRenderAssets_CaptureCockpit(0);
	XvtCockpit_Reset();
	XvtCockpitMessages_ClearProgress();
	memset(&g_crt, 0, sizeof g_crt);
	g_crt.valid = 1;
	g_crt.mask_index = 2;
}

static const XvtCockpitState* Presented(void) {
	XvtCockpit_Export(&g_out);
	return &g_out;
}

/* The host's frame up to the composition: begin, refresh the local player, select the composition. */
static void Compose(void) {
	XvtCockpit_BeginFrame();
	XvtCockpit_RefreshInstruments(LOCAL);
	XvtCockpit_LatchComposition();
}

static const XvtCockpitState* SealAndPresent(void) {
	XvtCockpit_Seal(&g_crt);
	XvtCockpit_Presented(0);
	return Presented();
}

static void CheckCopyState(void) {
	static XvtCockpitState source, destination;
	memset(&source, 0x11, sizeof source);
	memset(&destination, 0xCD, sizeof destination);
	source.page_content.row_count = 2;
	source.page_content.glyph_count = 3;
	source.overlay_content.glyph_count = 2;
	XvtCockpit_CopyState(&destination, &source);

	XVT_ASSERT_INT_EQ(memcmp(&destination, &source, offsetof(XvtCockpitState, page_content)), 0);
	XVT_ASSERT_INT_EQ(destination.page_content.row_count, 2);
	XVT_ASSERT_INT_EQ(destination.page_content.glyph_count, 3);
	XVT_ASSERT_INT_EQ(destination.overlay_content.glyph_count, 2);
	const XvtCockpitPageStore* from = &source.page_content;
	XvtCockpitPageStore* to = &destination.page_content;
	XVT_ASSERT_INT_EQ(memcmp(to->rows, from->rows, 2 * sizeof to->rows[0]), 0);
	XVT_ASSERT_INT_EQ(memcmp(to->glyphs, from->glyphs, 3 * sizeof to->glyphs[0]), 0);
	XVT_ASSERT_INT_EQ(memcmp(destination.overlay_content.glyphs, source.overlay_content.glyphs,
							 2 * sizeof destination.overlay_content.glyphs[0]),
					  0);
	/* The unused rest of each store keeps its old contents. */
	const unsigned char* row = (const unsigned char*)&to->rows[2];
	const unsigned char* glyph = (const unsigned char*)&to->glyphs[3];
	const unsigned char* overlay = (const unsigned char*)&destination.overlay_content.glyphs[2];
	XVT_ASSERT_INT_EQ(row[0], 0xCD);
	XVT_ASSERT_INT_EQ(glyph[0], 0xCD);
	XVT_ASSERT_INT_EQ(overlay[0], 0xCD);
	XVT_ASSERT_INT_EQ(((const unsigned char*)&to->glyphs[XVT_HUD_PAGE_GLYPH_CAPACITY - 1])[0], 0xCD);
}

static void CheckComposeSealPresent(void) {
	World();
	Compose();
	const XvtCockpitState* state = SealAndPresent();
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_TRUE(state->presentation_serial != 0);
	/* The sealed CRT is the one handed in. */
	XVT_ASSERT_INT_EQ(state->crt.valid, 1);
	XVT_ASSERT_INT_EQ(state->crt.mask_index, 2);
	/* Working came from the live view, the cockpit definition and the palette. */
	XVT_ASSERT_INT_EQ(state->view.screen_width, 640);
	XVT_ASSERT_INT_EQ(state->view.screen_height, 480);
	XVT_ASSERT_INT_EQ(state->palette_argb[5], XvtRenderDraw_Color(5));
	XVT_ASSERT_INT_EQ(state->palette_argb[200], XvtRenderDraw_Color(200));
	XvtRenderCockpit_CaptureDefinition(&g_expected.definition);
	XVT_ASSERT_INT_EQ(memcmp(&state->definition, &g_expected.definition, sizeof state->definition), 0);

	/* A new frame keeps the composition selected: sealing it again publishes again. */
	uint64_t serial = state->presentation_serial;
	XvtCockpit_BeginFrame();
	state = SealAndPresent();
	XVT_ASSERT_TRUE(state->presentation_serial != serial);
	XVT_ASSERT_INT_EQ(state->valid, 1);
}

static void CheckNothingPublishedUnsealed(void) {
	World();
	/* Without a composition, Seal does nothing and an unsealed frame is not published. */
	XvtCockpit_Seal(&g_crt);
	XvtCockpit_Presented(0);
	XVT_ASSERT_INT_EQ(Presented()->presentation_serial, 0);

	/* A new frame unseals. */
	Compose();
	XvtCockpit_Seal(&g_crt);
	XvtCockpit_BeginFrame();
	XvtCockpit_Presented(0);
	XVT_ASSERT_INT_EQ(Presented()->presentation_serial, 0);
}

static void CheckBeginFrameDropsPendingCrt(void) {
	World();
	Compose();
	XvtCockpit_Seal(&g_crt);
	XvtCockpit_BeginFrame();
	/* Published as a standalone overlay, the pending frame no longer has a valid CRT. */
	XvtCockpit_Presented(1);
	XVT_ASSERT_INT_EQ(Presented()->crt.valid, 0);
}

static void CheckRefreshLocalOnly(void) {
	World();
	/* Another player's refresh leaves working invalid, so no composition is selected. */
	XvtCockpit_BeginFrame();
	XvtCockpit_RefreshInstruments(LOCAL + 1);
	XvtCockpit_RefreshInstruments(8);
	XvtCockpit_LatchComposition();
	XvtCockpit_Seal(&g_crt);
	XvtCockpit_Presented(0);
	XVT_ASSERT_INT_EQ(Presented()->presentation_serial, 0);
	XVT_ASSERT_INT_EQ(XvtCockpit_LoadingAssetsReady(), 0);
}

static void CheckWorkingFollowsLayout(void) {
	World();
	XvtRenderCockpit_Reset();
	Compose();
	XvtCockpit_Seal(&g_crt);
	XvtCockpit_Presented(0);
	XVT_ASSERT_INT_EQ(Presented()->presentation_serial, 0);

	XvtRenderAssets_CaptureCockpit(0);
	Compose();
	const XvtCockpitState* state = SealAndPresent();
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_INT_EQ(state->view.screen_width, 640);

	/* Once the layout is gone again, a refresh leaves working invalid, so LatchComposition copies nothing:
	 * the frame sealed next still holds the old view, not the new screen width. */
	XvtRenderCockpit_Reset();
	g_screenWidth = 800;
	Compose();
	XVT_ASSERT_INT_EQ(SealAndPresent()->view.screen_width, 640);

	/* With the layout back, the refresh is taken again. */
	XvtRenderAssets_CaptureCockpit(0);
	Compose();
	XVT_ASSERT_INT_EQ(SealAndPresent()->view.screen_width, 800);
}

static void CheckLoadingAssetsReady(void) {
	World();
	Compose();
	uint64_t generation = SealAndPresent()->definition.resource_generation;
	XVT_ASSERT_INT_EQ(XvtCockpit_LoadingAssetsReady(), 0);
	XvtCockpit_ResourcesPrepared(generation + 1);
	XVT_ASSERT_INT_EQ(XvtCockpit_LoadingAssetsReady(), 0);
	XvtCockpit_ResourcesPrepared(generation);
	XVT_ASSERT_INT_EQ(XvtCockpit_LoadingAssetsReady(), 1);

	/* Reset clears both working and the prepared mark. */
	XvtCockpit_Reset();
	XVT_ASSERT_INT_EQ(XvtCockpit_LoadingAssetsReady(), 0);
	XvtCockpit_ResourcesPrepared(generation);
	XVT_ASSERT_INT_EQ(XvtCockpit_LoadingAssetsReady(), 0);
}

static int AllZero(const void* data, size_t size) {
	const unsigned char* bytes = data;
	for (size_t index = 0; index < size; ++index)
		if (bytes[index])
			return 0;
	return 1;
}

static void CheckReset(void) {
	World();
	Compose();
	XvtCockpitMessages_RecordProgress(1, 0, 0, 10, 2, 5);
	XVT_ASSERT_INT_EQ(SealAndPresent()->valid, 1);

	XvtCockpit_Reset();
	const XvtCockpitState* state = Presented();
	XVT_ASSERT_INT_EQ(AllZero(state, offsetof(XvtCockpitState, page_content)), 1);
	XVT_ASSERT_INT_EQ(state->page_content.glyph_count, 0);
	XVT_ASSERT_INT_EQ(state->overlay_content.glyph_count, 0);
	/* The messages were reset with it: the progress bar is gone. */
	XvtCockpit_Presented(1);
	XVT_ASSERT_INT_EQ(Presented()->loading.progress_visible, 0);
}

static void CheckStandaloneOverlay(void) {
	World();
	XvtRenderCockpit_Reset();
	/* No composition and nothing to show: published, but not valid. */
	XvtCockpit_Presented(1);
	const XvtCockpitState* state = Presented();
	uint64_t serial = state->presentation_serial;
	XVT_ASSERT_TRUE(serial != 0);
	XVT_ASSERT_INT_EQ(state->valid, 0);
	/* Without a valid layout it first captured the cockpit definition. */
	XvtRenderCockpit_CaptureDefinition(&g_expected.definition);
	XVT_ASSERT_INT_EQ(memcmp(&state->definition, &g_expected.definition, sizeof state->definition), 0);

	/* With the loading display showing it is valid, and each publication has a new serial. */
	XvtCockpitMessages_RecordProgress(1, 0, 0, 10, 2, 5);
	XvtCockpit_Presented(1);
	state = Presented();
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_INT_EQ(state->loading.progress_visible, 1);
	XVT_ASSERT_TRUE(state->presentation_serial != serial);

	/* The alert counts as well. */
	World();
	XvtCockpitMessages_BeginAlertLine(1, 0, 0, 100, 20);
	XvtCockpitMessages_EndAlertLine();
	XvtCockpit_Presented(1);
	XVT_ASSERT_INT_EQ(Presented()->valid, 1);
}

static void CheckGenerations(void) {
	World();
	XvtCockpitMessages_RecordProgress(1, 0, 0, 10, 2, 5);
	XvtCockpit_Presented(1);
	XvtCockpit_Export(&g_expected);

	/* Nothing changed: no generation rises. */
	XvtCockpit_Presented(1);
	const XvtCockpitState* state = Presented();
	XVT_ASSERT_INT_EQ(state->definition_generation, g_expected.definition_generation);
	XVT_ASSERT_INT_EQ(state->palette_generation, g_expected.palette_generation);
	XVT_ASSERT_INT_EQ(state->artwork_generation, g_expected.artwork_generation);
	XVT_ASSERT_INT_EQ(state->instruments_generation, g_expected.instruments_generation);
	XVT_ASSERT_INT_EQ(state->radar_generation, g_expected.radar_generation);
	XVT_ASSERT_INT_EQ(state->text_generation, g_expected.text_generation);
	XVT_ASSERT_INT_EQ(state->crt_generation, g_expected.crt_generation);

	/* The progress bar moved: the text generation rises, and only it. */
	XvtCockpitMessages_RecordProgress(1, 0, 0, 10, 2, 6);
	XvtCockpit_Presented(1);
	state = Presented();
	XVT_ASSERT_TRUE(state->text_generation > g_expected.text_generation);
	XVT_ASSERT_INT_EQ(state->definition_generation, g_expected.definition_generation);
	XVT_ASSERT_INT_EQ(state->palette_generation, g_expected.palette_generation);
	XVT_ASSERT_INT_EQ(state->artwork_generation, g_expected.artwork_generation);
	XVT_ASSERT_INT_EQ(state->instruments_generation, g_expected.instruments_generation);
	XVT_ASSERT_INT_EQ(state->crt_generation, g_expected.crt_generation);

	/* A composed frame brings a new palette and a sealed CRT: those generations rise. */
	XvtCockpit_Export(&g_expected);
	Compose();
	state = SealAndPresent();
	XVT_ASSERT_TRUE(state->palette_generation > g_expected.palette_generation);
	XVT_ASSERT_TRUE(state->crt_generation > g_expected.crt_generation);
}

static void CheckLatchPages(void) {
	World();
	g_mfdPageStates[MFD_PAGE_SCOREBOARD] = MFD_PAGE_STATE_CLOSED;
	g_mfdPageStates[MFD_PAGE_GOALS] = MFD_PAGE_STATE_OPEN;
	g_mfdPageStates[MFD_PAGE_DAMAGE] = MFD_PAGE_STATE_OPEN;
	g_mfdPageStates[MFD_PAGE_MAP_HELP] = MFD_PAGE_STATE_OPEN;
	g_mfdPageStates[MFD_PAGE_MESSAGE_LOG] = MFD_PAGE_STATE_OPEN;

	/* Off the map: open pages are placed except the command page; closed ones and the log are not. */
	XvtCockpit_LatchPages();
	XvtCockpit_Presented(1);
	const XvtCockpitPage* pages = Presented()->pages;
	XVT_ASSERT_INT_EQ(pages[MFD_PAGE_GOALS].visible, 1);
	XVT_ASSERT_INT_EQ(pages[MFD_PAGE_DAMAGE].visible, 1);
	XVT_ASSERT_INT_EQ(pages[MFD_PAGE_MAP_HELP].visible, 0);
	XVT_ASSERT_INT_EQ(pages[MFD_PAGE_SCOREBOARD].visible, 0);
	XVT_ASSERT_INT_EQ(pages[MFD_PAGE_MESSAGE_LOG].visible, 0);

	/* On the map: the damage page is skipped and the command page placed. */
	g_players[LOCAL].mapCameraState = 1;
	XvtCockpit_LatchPages();
	XvtCockpit_Presented(1);
	pages = Presented()->pages;
	XVT_ASSERT_INT_EQ(pages[MFD_PAGE_GOALS].visible, 1);
	XVT_ASSERT_INT_EQ(pages[MFD_PAGE_DAMAGE].visible, 0);
	XVT_ASSERT_INT_EQ(pages[MFD_PAGE_MAP_HELP].visible, 1);
}

static void CheckSealExportsLatchedPages(void) {
	World();
	g_mfdPageStates[MFD_PAGE_GOALS] = MFD_PAGE_STATE_OPEN;
	XvtCockpitPages_BeginSection(MFD_PAGE_GOALS, XVT_COCKPIT_PAGE_HEADER);
	XvtCockpitPages_RecordGlyph('G', 8, 10, 0);
	XvtCockpitPages_EndSection();
	Compose();
	XvtCockpit_LatchPages();
	const XvtCockpitState* state = SealAndPresent();
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_INT_EQ(state->pages[MFD_PAGE_GOALS].visible, 1);
	XVT_ASSERT_INT_EQ(state->pages[MFD_PAGE_GOALS].glyph_count, 1);
	XVT_ASSERT_INT_EQ(state->page_content.glyph_count, 1);
	XVT_ASSERT_INT_EQ(state->page_content.glyphs[state->pages[MFD_PAGE_GOALS].first_glyph].character, 'G');
}

static void CheckLatchMessages(void) {
	World();
	/* A closed message log is placed hidden. */
	XvtCockpit_LatchMessages();
	XvtCockpit_Presented(1);
	XVT_ASSERT_INT_EQ(Presented()->pages[MFD_PAGE_MESSAGE_LOG].visible, 0);

	/* Open, it shows; with a player count of 1 or more it is wider. */
	g_mfdPageStates[MFD_PAGE_MESSAGE_LOG] = MFD_PAGE_STATE_OPEN;
	XvtCockpit_LatchMessages();
	XvtCockpit_Presented(1);
	const XvtCockpitPage* page = &Presented()->pages[MFD_PAGE_MESSAGE_LOG];
	XVT_ASSERT_INT_EQ(page->visible, 1);
	int narrow = page->placement.width;
	g_flightPlayerCount = 1;
	XvtCockpit_LatchMessages();
	XvtCockpit_Presented(1);
	XVT_ASSERT_TRUE(Presented()->pages[MFD_PAGE_MESSAGE_LOG].placement.width > narrow);

	/* In a composed frame the open log is latched, so the sealed frame shows it. */
	Compose();
	XvtCockpit_LatchMessages();
	XVT_ASSERT_INT_EQ(SealAndPresent()->pages[MFD_PAGE_MESSAGE_LOG].visible, 1);
}

static void CheckLatchLauncher(void) {
	World();
	XvtCockpitReadouts_RecordNumber((XvtCockpitNumberId)(XVT_COCKPIT_NUMBER_LAUNCHER_FIRST + 2), 7, 2, 1);
	XvtCockpit_LatchLauncher(2, 100, 110, 20, 10);
	/* Launchers from 4 up are ignored. */
	XvtCockpit_LatchLauncher(4, 1, 1, 1, 1);
	XvtCockpit_Presented(1);
	const XvtCockpitWeapons* weapons = &Presented()->weapons;
	const XvtCockpitNumber* count = &weapons->launchers[2].count;
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
	XVT_ASSERT_INT_EQ(count->color_key_argb, XvtRenderDraw_Color(BYPASS));
	XVT_ASSERT_INT_EQ(weapons->launchers[3].visible, 0);
	XVT_ASSERT_INT_EQ(AllZero(&weapons->lock_indicator, sizeof weapons->lock_indicator), 1);
	XVT_ASSERT_INT_EQ(AllZero(&g_out.target, sizeof g_out.target), 1);
}

static void CaptureReadyMessage(void) {
	XvtCockpitMessages_BeginMessage(0);
	XvtCockpitMessages_RecordGlyph('M', 8, 10, 0);
	XvtCockpitMessages_EndMessage();
}

static void CheckLatchMessage(void) {
	World();
	CaptureReadyMessage();
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_NETWORK_PING, "12", XVT_COCKPIT_ALIGN_LEFT);
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_NETWORK_LAG, "3", XVT_COCKPIT_ALIGN_LEFT);

	/* The ready pane is latched while the log is closed, and moves the ping and lag fields with it. */
	XvtCockpit_LatchMessage(XVT_COCKPIT_MESSAGE_READY, 40, 55, 300, 400, 120, 30);
	XvtCockpit_Presented(1);
	const XvtCockpitState* state = Presented();
	XVT_ASSERT_INT_EQ(state->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 1);
	XVT_ASSERT_INT_EQ(state->messages.panes[XVT_COCKPIT_MESSAGE_READY].placement.x, 300);
	const XvtCockpitTextField* ping = &state->text_fields[XVT_COCKPIT_TEXT_NETWORK_PING];
	const XvtCockpitTextField* lag = &state->text_fields[XVT_COCKPIT_TEXT_NETWORK_LAG];
	XVT_ASSERT_INT_EQ(strcmp(ping->caption.text, "12"), 0);
	XVT_ASSERT_INT_EQ(ping->x, 70 + (300 - 40));
	XVT_ASSERT_INT_EQ(ping->y, 80 + (400 - 55));
	XVT_ASSERT_INT_EQ(ping->bounds.x, 50 + (300 - 40));
	XVT_ASSERT_INT_EQ(ping->bounds.y, 60 + (400 - 55));
	XVT_ASSERT_INT_EQ(strcmp(lag->caption.text, "3"), 0);
	XVT_ASSERT_INT_EQ(lag->y, 80 + (400 - 55));

	/* While the log is open the ready pane is not latched, but the fields are still placed. */
	World();
	CaptureReadyMessage();
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_NETWORK_PING, "12", XVT_COCKPIT_ALIGN_LEFT);
	g_mfdPageStates[MFD_PAGE_MESSAGE_LOG] = MFD_PAGE_STATE_OPEN;
	XvtCockpit_LatchMessage(XVT_COCKPIT_MESSAGE_READY, 40, 55, 300, 400, 120, 30);
	XvtCockpitMessages_BeginMessage(3);
	XvtCockpitMessages_RecordGlyph('S', 8, 10, 0);
	XvtCockpitMessages_EndMessage();
	XvtCockpit_LatchMessage(XVT_COCKPIT_MESSAGE_SYSTEM, 50, 60, 0, 0, 100, 20);
	XvtCockpit_Presented(1);
	state = Presented();
	XVT_ASSERT_INT_EQ(state->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 0);
	XVT_ASSERT_INT_EQ(state->messages.panes[XVT_COCKPIT_MESSAGE_SYSTEM].visible, 1);
	XVT_ASSERT_INT_EQ(state->text_fields[XVT_COCKPIT_TEXT_NETWORK_PING].y, 80 + (400 - 55));
}

static void CheckPlacedPanesHidden(void) {
	World();
	CaptureReadyMessage();
	Compose();
	XvtCockpit_LatchMessage(XVT_COCKPIT_MESSAGE_READY, 50, 60, 0, 0, 100, 20);
	XVT_ASSERT_INT_EQ(SealAndPresent()->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 1);

	/* A new composition hides the placed panes until they are latched again. */
	Compose();
	XVT_ASSERT_INT_EQ(SealAndPresent()->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 0);

	/* So does BeginMessagePlacement. */
	Compose();
	XvtCockpit_LatchMessage(XVT_COCKPIT_MESSAGE_READY, 50, 60, 0, 0, 100, 20);
	XvtCockpit_BeginMessagePlacement();
	XVT_ASSERT_INT_EQ(SealAndPresent()->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 0);
}

static void CheckRetainPresentedFrame(void) {
	World();
	XvtCockpit_LatchLauncher(1, 10, 10, 5, 5);
	XvtCockpit_Presented(1);
	/* A change to pending is dropped when pending restarts from the presented frame. */
	XvtCockpit_LatchLauncher(3, 20, 20, 5, 5);
	XvtCockpit_RetainPresentedFrame();
	XvtCockpit_Presented(1);
	const XvtCockpitState* state = Presented();
	XVT_ASSERT_INT_EQ(state->weapons.launchers[1].visible, 1);
	XVT_ASSERT_INT_EQ(state->weapons.launchers[3].visible, 0);

	/* It unseals: a sealed composition is not published after it. */
	uint64_t serial = state->presentation_serial;
	Compose();
	XvtCockpit_Seal(&g_crt);
	XvtCockpit_RetainPresentedFrame();
	XvtCockpit_Presented(0);
	XVT_ASSERT_INT_EQ(Presented()->presentation_serial, serial);
}

static void CheckExportResourcesRefusals(void) {
	static XvtCockpitResources resources;
	World();
	/* Working is invalid. */
	g_hudCockpitResourcesLoaded = 1;
	memset(&resources, 0xAB, sizeof resources);
	XvtCockpit_ExportResources(&resources);
	XVT_ASSERT_INT_EQ(AllZero(&resources, sizeof resources), 1);

	/* Working is valid, but the cockpit resources are not loaded. */
	Compose();
	g_hudCockpitResourcesLoaded = 0;
	memset(&resources, 0xAB, sizeof resources);
	XvtCockpit_ExportResources(&resources);
	XVT_ASSERT_INT_EQ(AllZero(&resources, sizeof resources), 1);

	/* Loaded, but panel 0 has no asset: invalid. That it is also cleared is a known failure below. */
	g_hudCockpitResourcesLoaded = 1;
	memset(&resources, 0xAB, sizeof resources);
	XvtCockpit_ExportResources(&resources);
	XVT_ASSERT_INT_EQ(resources.valid, 0);
}

/* Known failure: with panel 0 unbound the output is left invalid but not cleared. ExportResources captures
 * the cockpit definition into it before it looks at panel 0, and returns with that definition in place. */
static void CheckExportResourcesClearedWithoutPanel(void) {
	static XvtCockpitResources resources;
	World();
	Compose();
	g_hudCockpitResourcesLoaded = 1;
	memset(&resources, 0xAB, sizeof resources);
	XvtCockpit_ExportResources(&resources);
	XVT_ASSERT_INT_EQ(AllZero(&resources, sizeof resources), 1);
}

int main(int argc, char** argv) {
	/* "known-failure <check>" runs one check the code is known to fail; an unknown name runs nothing. */
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		if (strcmp(argv[2], "export_resources_without_panel") == 0)
			CheckExportResourcesClearedWithoutPanel();
		return 0;
	}
	CheckCopyState();
	CheckComposeSealPresent();
	CheckNothingPublishedUnsealed();
	CheckBeginFrameDropsPendingCrt();
	CheckRefreshLocalOnly();
	CheckWorkingFollowsLayout();
	CheckLoadingAssetsReady();
	CheckReset();
	CheckStandaloneOverlay();
	CheckGenerations();
	CheckLatchPages();
	CheckSealExportsLatchedPages();
	CheckLatchMessages();
	CheckLatchLauncher();
	CheckLatchMessage();
	CheckPlacedPanesHidden();
	CheckRetainPresentedFrame();
	CheckExportResourcesRefusals();
	XvtCockpit_Reset();
	return 0;
}
