/* Checks the cockpit overlay text (xvt_runtime/snapshot/cockpit_messages.h) against the promises in its
 * header: which pane a message goes to, placing a pane, when a pane's generation rises, the glyph limits,
 * clearing and the two resets; the alert's lines, modes and generation; the progress bar; the loading
 * text, its priority over the other captures and its fatal limit; and how Export packs everything into
 * the overlay store and when it sets the screen size. The test sets the flight text globals, the three
 * live message records, the pane timers and the palette itself; every case starts from Reset with the
 * loading text cleared.
 *
 * The full-loading-text check requests a fatal error, which latches for the whole process, so it runs
 * last. Not checked here: which rows an alert line from the second on fills, since the header does not
 * say how its five rows match its three lines, and the border color's value. */
#include "aeron/aeron.h"
#include "test_assert.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/player/player.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/snapshot/cockpit_messages.h"
#include "xvt_runtime/snapshot/render_hud.h"

#include <string.h>

/* The fatal-error request opens SDL's message box, and SDL keeps a small allocation from it when no video
 * device is running, as in a test. The leak checker is told to ignore what that one call allocates. */
#if defined(__SANITIZE_ADDRESS__)
#define XVT_TEST_LEAK_CHECKER 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define XVT_TEST_LEAK_CHECKER 1
#endif
#endif
#ifdef XVT_TEST_LEAK_CHECKER
#include <sanitizer/lsan_interface.h>
#endif

enum { BYPASS = 9, FOREGROUND = 7, BACKGROUND = 4 };

enum { READY_ID = 11, SYSTEM_ID = 22, GROUP_ID = 33 };

static XvtCockpitState g_state;

/* The clip runs from (50, 60) to (450, 300), so its corner, a message's origin, is (50, 60); the cursor
 * starts at (70, 80). The screen is 640 by 480. */
static void Start(void) {
	for (unsigned index = 0; index < 256; ++index)
		g_swPalette[index] = (RgbTriplet) { (uint8_t)(index & 63), (uint8_t)(index >> 6), 7 };
	g_flightClipLeft = 50;
	g_flightClipTop = 60;
	g_flightClipRight = 450;
	g_flightClipBottom = 300;
	g_flightCursorX = 70;
	g_flightCursorY = 80;
	g_flightTextColorIndex = FOREGROUND;
	g_flightTextBgColor = BACKGROUND;
	g_flightTextShadowEnabled = 0;
	g_flightColorEscapeBypassChar = BYPASS;
	g_screenWidth = 640;
	g_screenHeight = 480;

	memset(g_readyMessagePaneQueue, 0, sizeof g_readyMessagePaneQueue);
	memset(&g_systemMessagePane, 0, sizeof g_systemMessagePane);
	memset(&g_flightGroupMessagePane, 0, sizeof g_flightGroupMessagePane);
	g_readyMessagePaneQueue[0].stateOrMessageId = READY_ID;
	g_readyMessagePaneQueue[0].ageTicks = 3;
	g_systemMessagePane.stateOrMessageId = SYSTEM_ID;
	g_systemMessagePane.ageTicks = 4;
	g_flightGroupMessagePane.stateOrMessageId = GROUP_ID;
	g_flightGroupMessagePane.ageTicks = 5;
	g_localPlayer = 0;
	memset(g_playerFlightTransientTimers, 0, sizeof g_playerFlightTransientTimers);
	g_playerFlightTransientTimers[0].readyMessagePaneTimer = 100;
	g_playerFlightTransientTimers[0].systemMessagePaneTimer = 200;
	g_playerFlightTransientTimers[0].flightGroupMessagePaneTimer = 300;

	XvtCockpitMessages_Reset();
	XvtCockpitMessages_ClearProgress();
}

static void Glyphs(unsigned first, unsigned count) {
	for (unsigned index = 0; index < count; ++index)
		XvtCockpitMessages_RecordGlyph(first + index, 8, 10, 0);
}

static void Message(int pane_type, unsigned first, unsigned count) {
	XvtCockpitMessages_BeginMessage(pane_type);
	Glyphs(first, count);
	XvtCockpitMessages_EndMessage();
}

/* Latches a pane with its origin as the source point, so its glyphs do not move. */
static void Latch(XvtCockpitMessageId pane) { XvtCockpitMessages_Latch(pane, 50, 60, 0, 0, 100, 20); }

static const XvtCockpitState* Exported(void) {
	memset(&g_state, 0, sizeof g_state);
	g_state.view.screen_width = 7;
	g_state.view.screen_height = 7;
	XvtCockpitMessages_Export(&g_state);
	return &g_state;
}

static void Alert(int mode, unsigned first, unsigned count) {
	XvtCockpitMessages_BeginAlertLine(mode, 100, 110, 200, 50);
	Glyphs(first, count);
	XvtCockpitMessages_EndAlertLine();
}

static void CheckPaneRouting(void) {
	static const struct {
		int pane_type;
		XvtCockpitMessageId pane;
		unsigned message_id;
	} routes[] = {
		{ 8, XVT_COCKPIT_MESSAGE_FLIGHT_GROUP, GROUP_ID }, { 3, XVT_COCKPIT_MESSAGE_SYSTEM, SYSTEM_ID },
		{ 4, XVT_COCKPIT_MESSAGE_SYSTEM, SYSTEM_ID },      { 7, XVT_COCKPIT_MESSAGE_SYSTEM, SYSTEM_ID },
		{ 0, XVT_COCKPIT_MESSAGE_READY, READY_ID },        { 5, XVT_COCKPIT_MESSAGE_READY, READY_ID },
		{ 9, XVT_COCKPIT_MESSAGE_READY, READY_ID },
	};

	for (unsigned route = 0; route < sizeof routes / sizeof routes[0]; ++route) {
		Start();
		Message(routes[route].pane_type, 'A', 1);
		for (unsigned pane = 0; pane < XVT_COCKPIT_MESSAGE_COUNT; ++pane)
			Latch((XvtCockpitMessageId)pane);
		const XvtCockpitState* state = Exported();
		for (unsigned pane = 0; pane < XVT_COCKPIT_MESSAGE_COUNT; ++pane) {
			const XvtCockpitMessage* message = &state->messages.panes[pane];
			int expected = pane == (unsigned)routes[route].pane;
			XVT_ASSERT_INT_EQ(message->visible, expected);
			XVT_ASSERT_INT_EQ(message->glyph_count, expected);
			if (expected)
				XVT_ASSERT_INT_EQ(message->message_id, routes[route].message_id);
		}
	}
}

static void CheckLatchPlacesPane(void) {
	Start();
	XvtCockpitMessages_BeginMessage(0);
	Glyphs('A', 2);
	XvtCockpitMessages_RecordReveal(5);
	XvtCockpitMessages_EndMessage();

	/* The live record still holds the message: its live age is used. */
	g_readyMessagePaneQueue[0].ageTicks = 9;
	XvtCockpitMessages_Latch(XVT_COCKPIT_MESSAGE_READY, 40, 55, 300, 400, 120, 30);
	const XvtCockpitState* state = Exported();
	const XvtCockpitMessage* pane = &state->messages.panes[XVT_COCKPIT_MESSAGE_READY];
	XVT_ASSERT_INT_EQ(pane->visible, 1);
	XVT_ASSERT_INT_EQ(pane->placement.x, 300);
	XVT_ASSERT_INT_EQ(pane->placement.y, 400);
	XVT_ASSERT_INT_EQ(pane->placement.width, 120);
	XVT_ASSERT_INT_EQ(pane->placement.height, 30);
	XVT_ASSERT_INT_EQ(pane->timer_ticks, 100);
	XVT_ASSERT_INT_EQ(pane->age_ticks, 9);
	XVT_ASSERT_INT_EQ(pane->revealed_characters, 5);
	XVT_ASSERT_INT_EQ(pane->glyph_count, 2);
	/* A glyph captured relative to the origin (50, 60) moves by the origin minus the source point. */
	const XvtCockpitGlyph* glyph = &state->overlay_content.glyphs[pane->first_glyph];
	XVT_ASSERT_INT_EQ(glyph->character, 'A');
	XVT_ASSERT_INT_EQ(glyph->x, (70 - 50) + (50 - 40));
	XVT_ASSERT_INT_EQ(glyph->y, (80 - 60) + (60 - 55));

	/* Once the live record holds another message, the age captured with the message stays. */
	g_readyMessagePaneQueue[0].stateOrMessageId = READY_ID + 1;
	g_readyMessagePaneQueue[0].ageTicks = 15;
	XvtCockpitMessages_Latch(XVT_COCKPIT_MESSAGE_READY, 40, 55, 300, 400, 120, 30);
	XVT_ASSERT_INT_EQ(Exported()->messages.panes[XVT_COCKPIT_MESSAGE_READY].age_ticks, 3);

	/* Each pane takes its own live timer; an out-of-range pane is ignored. */
	Message(3, 'S', 1);
	Latch(XVT_COCKPIT_MESSAGE_SYSTEM);
	Latch(XVT_COCKPIT_MESSAGE_COUNT);
	XVT_ASSERT_INT_EQ(Exported()->messages.panes[XVT_COCKPIT_MESSAGE_SYSTEM].timer_ticks, 200);
}

static uint64_t ReadyGeneration(void) {
	Latch(XVT_COCKPIT_MESSAGE_READY);
	return Exported()->messages.panes[XVT_COCKPIT_MESSAGE_READY].generation;
}

static void CheckMessageGeneration(void) {
	Start();
	Message(0, 'A', 1);
	uint64_t first = ReadyGeneration();
	XVT_ASSERT_TRUE(first > 0);

	/* The same glyphs at the same origin: no change. */
	Message(0, 'A', 1);
	XVT_ASSERT_INT_EQ(ReadyGeneration(), first);

	/* New glyphs raise it; so does a new origin. */
	Message(0, 'B', 1);
	uint64_t second = ReadyGeneration();
	XVT_ASSERT_TRUE(second > first);
	g_flightClipLeft = 51;
	g_flightCursorX = 71;
	Message(0, 'B', 1);
	XVT_ASSERT_TRUE(ReadyGeneration() > second);
}

static void CheckMessageLimit(void) {
	Start();
	Message(0, 0, XVT_HUD_MESSAGE_GLYPHS);
	uint64_t generation = ReadyGeneration();
	XVT_ASSERT_INT_EQ(g_state.messages.panes[XVT_COCKPIT_MESSAGE_READY].glyph_count, XVT_HUD_MESSAGE_GLYPHS);

	/* One glyph too many discards the capture: the pane keeps what it had. */
	Message(0, 1, XVT_HUD_MESSAGE_GLYPHS + 1);
	XVT_ASSERT_INT_EQ(ReadyGeneration(), generation);
	const XvtCockpitMessage* pane = &g_state.messages.panes[XVT_COCKPIT_MESSAGE_READY];
	XVT_ASSERT_INT_EQ(pane->glyph_count, XVT_HUD_MESSAGE_GLYPHS);
	XVT_ASSERT_INT_EQ(g_state.overlay_content.glyphs[pane->first_glyph].character, 0);
}

static void CheckIgnoredGlyphs(void) {
	Start();
	/* With no capture open, a glyph goes nowhere. */
	Glyphs('X', 1);
	XVT_ASSERT_INT_EQ(Exported()->overlay_content.glyph_count, 0);
	/* Outside the clip, a glyph is not captured. */
	XvtCockpitMessages_BeginMessage(0);
	Glyphs('A', 1);
	g_flightCursorX = 500;
	Glyphs('Y', 1);
	XvtCockpitMessages_EndMessage();
	Latch(XVT_COCKPIT_MESSAGE_READY);
	XVT_ASSERT_INT_EQ(Exported()->messages.panes[XVT_COCKPIT_MESSAGE_READY].glyph_count, 1);
}

static void CheckClear(void) {
	Start();
	Message(0, 'A', 1);
	uint64_t shown = ReadyGeneration();

	/* A visible pane is cleared and its generation rises. */
	XvtCockpitMessages_Clear(XVT_COCKPIT_MESSAGE_READY);
	uint64_t cleared = ReadyGeneration();
	XVT_ASSERT_INT_EQ(g_state.messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 0);
	XVT_ASSERT_TRUE(cleared > shown);

	/* A pane that is not visible is left alone. */
	XvtCockpitMessages_Clear(XVT_COCKPIT_MESSAGE_READY);
	XVT_ASSERT_INT_EQ(ReadyGeneration(), cleared);
}

static void CheckBeginPlacement(void) {
	Start();
	Message(0, 'A', 1);
	Latch(XVT_COCKPIT_MESSAGE_READY);
	XVT_ASSERT_INT_EQ(Exported()->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 1);
	XvtCockpitMessages_BeginPlacement();
	const XvtCockpitState* state = Exported();
	XVT_ASSERT_INT_EQ(state->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 0);
	XVT_ASSERT_INT_EQ(state->overlay_content.glyph_count, 0);
	Latch(XVT_COCKPIT_MESSAGE_READY);
	XVT_ASSERT_INT_EQ(Exported()->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 1);
}

static void CheckResets(void) {
	/* ResetWorking clears the working panes but not the placed ones. */
	Start();
	Message(0, 'A', 1);
	Latch(XVT_COCKPIT_MESSAGE_READY);
	XvtCockpitMessages_ResetWorking();
	XVT_ASSERT_INT_EQ(Exported()->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 1);
	Latch(XVT_COCKPIT_MESSAGE_READY);
	XVT_ASSERT_INT_EQ(Exported()->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 0);

	/* ResetWorking abandons an open message capture. */
	XvtCockpitMessages_BeginMessage(0);
	XvtCockpitMessages_ResetWorking();
	Glyphs('A', 1);
	XvtCockpitMessages_EndMessage();
	Latch(XVT_COCKPIT_MESSAGE_READY);
	XVT_ASSERT_INT_EQ(Exported()->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 0);

	/* Reset clears the placed panes, the alert and the progress bar, but the loading text survives. */
	Start();
	Message(0, 'A', 1);
	Latch(XVT_COCKPIT_MESSAGE_READY);
	Alert(1, 'a', 1);
	XvtCockpitMessages_RecordProgress(1, 0, 0, 10, 2, 5);
	XvtCockpitMessages_BeginLoadingText();
	Glyphs('L', 1);
	XvtCockpitMessages_EndLoadingText();
	XvtCockpitMessages_Reset();
	const XvtCockpitState* state = Exported();
	XVT_ASSERT_INT_EQ(state->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 0);
	XVT_ASSERT_INT_EQ(state->alert.active, 0);
	XVT_ASSERT_INT_EQ(state->loading.visible, 0);
	XVT_ASSERT_INT_EQ(state->loading.text_visible, 1);
	XVT_ASSERT_INT_EQ(state->loading.glyph_count, 1);

	/* Reset abandons open message and alert captures. */
	Start();
	XvtCockpitMessages_BeginMessage(0);
	XvtCockpitMessages_Reset();
	Glyphs('A', 1);
	XvtCockpitMessages_EndMessage();
	XvtCockpitMessages_BeginAlertLine(1, 100, 110, 200, 50);
	XvtCockpitMessages_Reset();
	Glyphs('a', 1);
	XvtCockpitMessages_EndAlertLine();
	Latch(XVT_COCKPIT_MESSAGE_READY);
	state = Exported();
	XVT_ASSERT_INT_EQ(state->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 0);
	XVT_ASSERT_INT_EQ(state->alert.active, 0);
	XVT_ASSERT_INT_EQ(state->overlay_content.glyph_count, 0);
}

static void CheckAlertLines(void) {
	Start();
	XvtCockpitMessages_BeginAlert();
	Alert(1, 'a', 2);
	const XvtCockpitAlert* alert = &Exported()->alert;
	XVT_ASSERT_INT_EQ(alert->active, 1);
	XVT_ASSERT_INT_EQ(alert->placement.x, 100);
	XVT_ASSERT_INT_EQ(alert->placement.y, 110);
	XVT_ASSERT_INT_EQ(alert->placement.width, 200);
	XVT_ASSERT_INT_EQ(alert->placement.height, 50);
	XVT_ASSERT_INT_EQ(alert->line_visible[0], 1);
	XVT_ASSERT_INT_EQ(alert->glyph_count[0], 2);
	XVT_ASSERT_INT_EQ(alert->line_visible[1], 0);
	/* The first line fills every row below it with the text background color. */
	for (unsigned row = 0; row < 5; ++row)
		XVT_ASSERT_INT_EQ(alert->row_background_argb[row], XvtRenderDraw_Color(BACKGROUND));
	/* Mode 1 sets the border. */
	uint32_t border = alert->border_argb;
	XVT_ASSERT_TRUE(border != 0);
	uint64_t first = alert->generation;
	XVT_ASSERT_TRUE(first > 0);

	/* Modes 2 and 3 build on the active alert, on lines 1 and 2. */
	Alert(2, 'b', 1);
	Alert(3, 'c', 3);
	alert = &Exported()->alert;
	XVT_ASSERT_INT_EQ(alert->glyph_count[0], 2);
	XVT_ASSERT_INT_EQ(alert->glyph_count[1], 1);
	XVT_ASSERT_INT_EQ(alert->glyph_count[2], 3);
	XVT_ASSERT_INT_EQ(alert->line_visible[2], 1);
	XVT_ASSERT_INT_EQ(alert->border_argb, border);
	XVT_ASSERT_TRUE(alert->generation > first);
	XVT_ASSERT_INT_EQ(g_state.overlay_content.glyphs[alert->first_glyph[1]].character, 'b');
	XVT_ASSERT_INT_EQ(g_state.overlay_content.glyphs[alert->first_glyph[2]].character, 'c');

	/* A line clears itself and the lines after it, not the ones before. */
	Alert(2, 'd', 1);
	alert = &Exported()->alert;
	XVT_ASSERT_INT_EQ(alert->glyph_count[0], 2);
	XVT_ASSERT_INT_EQ(alert->glyph_count[1], 1);
	XVT_ASSERT_INT_EQ(g_state.overlay_content.glyphs[alert->first_glyph[1]].character, 'd');
	XVT_ASSERT_INT_EQ(alert->line_visible[2], 0);
	XVT_ASSERT_INT_EQ(alert->glyph_count[2], 0);

	/* Mode 0 is line 0 too. */
	Alert(0, 'e', 1);
	alert = &Exported()->alert;
	XVT_ASSERT_INT_EQ(alert->glyph_count[0], 1);
	XVT_ASSERT_INT_EQ(g_state.overlay_content.glyphs[alert->first_glyph[0]].character, 'e');
	XVT_ASSERT_INT_EQ(alert->line_visible[1], 0);
}

static void CheckAlertModesAndLimits(void) {
	Start();
	Alert(1, 'a', 2);
	uint64_t generation = Exported()->alert.generation;

	/* Modes outside 0 to 3 are ignored: nothing opens, nothing changes. */
	Alert(4, 'x', 1);
	Alert(-1, 'x', 1);
	const XvtCockpitAlert* alert = &Exported()->alert;
	XVT_ASSERT_INT_EQ(alert->generation, generation);
	XVT_ASSERT_INT_EQ(alert->glyph_count[0], 2);
	XVT_ASSERT_INT_EQ(alert->line_visible[1], 0);

	/* A full line is kept; one glyph more discards the line. */
	Alert(2, 0, XVT_HUD_ALERT_LINE_GLYPHS);
	alert = &Exported()->alert;
	XVT_ASSERT_INT_EQ(alert->glyph_count[1], XVT_HUD_ALERT_LINE_GLYPHS);
	generation = alert->generation;
	Alert(2, 1, XVT_HUD_ALERT_LINE_GLYPHS + 1);
	alert = &Exported()->alert;
	XVT_ASSERT_INT_EQ(alert->generation, generation);
	XVT_ASSERT_INT_EQ(alert->glyph_count[1], XVT_HUD_ALERT_LINE_GLYPHS);
	XVT_ASSERT_INT_EQ(g_state.overlay_content.glyphs[alert->first_glyph[1]].character, 0);
}

static void CheckAlertBeginAndEnd(void) {
	Start();
	Alert(1, 'a', 1);
	Alert(2, 'b', 1);

	/* EndAlert deactivates and raises the generation; a second one changes nothing. */
	uint64_t active = Exported()->alert.generation;
	XvtCockpitMessages_EndAlert();
	const XvtCockpitAlert* alert = &Exported()->alert;
	XVT_ASSERT_INT_EQ(alert->active, 0);
	XVT_ASSERT_INT_EQ(alert->glyph_count[0], 0);
	uint64_t ended = alert->generation;
	XVT_ASSERT_TRUE(ended > active);
	XvtCockpitMessages_EndAlert();
	XVT_ASSERT_INT_EQ(Exported()->alert.generation, ended);

	/* A line on an inactive alert builds on an empty one. */
	Alert(3, 'c', 1);
	alert = &Exported()->alert;
	XVT_ASSERT_INT_EQ(alert->active, 1);
	XVT_ASSERT_INT_EQ(alert->line_visible[0], 0);
	XVT_ASSERT_INT_EQ(alert->line_visible[1], 0);
	XVT_ASSERT_INT_EQ(alert->glyph_count[1], 0);
	XVT_ASSERT_INT_EQ(alert->glyph_count[2], 1);

	/* BeginAlert clears the alert, generation included, and abandons an open line. */
	XvtCockpitMessages_BeginAlertLine(1, 100, 110, 200, 50);
	Glyphs('d', 1);
	XvtCockpitMessages_BeginAlert();
	Glyphs('e', 1);
	XvtCockpitMessages_EndAlertLine();
	alert = &Exported()->alert;
	XVT_ASSERT_INT_EQ(alert->active, 0);
	XVT_ASSERT_INT_EQ(alert->generation, 0);
	XVT_ASSERT_INT_EQ(g_state.overlay_content.glyph_count, 0);
}

static void CheckProgress(void) {
	Start();
	XvtCockpitMessages_RecordProgress(3, 10, 20, 300, 8, 120);
	const XvtCockpitLoading* loading = &Exported()->loading;
	XVT_ASSERT_INT_EQ(loading->visible, 1);
	XVT_ASSERT_INT_EQ(loading->placement.x, 10);
	XVT_ASSERT_INT_EQ(loading->placement.y, 20);
	XVT_ASSERT_INT_EQ(loading->placement.width, 300);
	XVT_ASSERT_INT_EQ(loading->placement.height, 8);
	XVT_ASSERT_INT_EQ(loading->progress_step, 3);
	XVT_ASSERT_INT_EQ(loading->filled_width, 120);
	uint64_t first = loading->generation;
	XVT_ASSERT_TRUE(first > 0);
	XvtCockpitMessages_RecordProgress(3, 10, 20, 300, 8, 150);
	XVT_ASSERT_TRUE(Exported()->loading.generation > first);

	/* A new flight frame hides the bar and the loading text. */
	XvtCockpitMessages_BeginLoadingText();
	Glyphs('L', 1);
	XvtCockpitMessages_EndLoadingText();
	XvtCockpitMessages_BeginFlightFrame();
	loading = &Exported()->loading;
	XVT_ASSERT_INT_EQ(loading->visible, 0);
	XVT_ASSERT_INT_EQ(loading->text_visible, 0);

	/* ClearProgress hides the bar and clears the loading text. */
	XvtCockpitMessages_RecordProgress(3, 10, 20, 300, 8, 150);
	XvtCockpitMessages_BeginLoadingText();
	Glyphs('L', 1);
	XvtCockpitMessages_EndLoadingText();
	XvtCockpitMessages_ClearProgress();
	loading = &Exported()->loading;
	XVT_ASSERT_INT_EQ(loading->visible, 0);
	XVT_ASSERT_INT_EQ(loading->text_visible, 0);
	XVT_ASSERT_INT_EQ(loading->glyph_count, 0);
}

static void CheckLoadingText(void) {
	Start();
	XvtCockpitMessages_BeginLoadingText();
	Glyphs('L', 3);
	XvtCockpitMessages_EndLoadingText();
	const XvtCockpitState* state = Exported();
	XVT_ASSERT_INT_EQ(state->loading.text_visible, 1);
	XVT_ASSERT_INT_EQ(state->loading.glyph_count, 3);
	/* Relative to the screen: the glyph sits at the cursor. */
	const XvtCockpitGlyph* glyph = &state->overlay_content.glyphs[state->loading.first_glyph];
	XVT_ASSERT_INT_EQ(glyph->character, 'L');
	XVT_ASSERT_INT_EQ(glyph->x, 70);
	XVT_ASSERT_INT_EQ(glyph->y, 80);
	uint64_t first = state->loading.text_generation;
	XVT_ASSERT_TRUE(first > 0);

	/* With no glyph the text is hidden, and the generation still rises. */
	XvtCockpitMessages_BeginLoadingText();
	XvtCockpitMessages_EndLoadingText();
	state = Exported();
	XVT_ASSERT_INT_EQ(state->loading.text_visible, 0);
	XVT_ASSERT_TRUE(state->loading.text_generation > first);

	/* The loading text takes a glyph before an open message does. */
	XvtCockpitMessages_BeginLoadingText();
	XvtCockpitMessages_BeginMessage(0);
	Glyphs('M', 1);
	XvtCockpitMessages_EndMessage();
	XvtCockpitMessages_EndLoadingText();
	Latch(XVT_COCKPIT_MESSAGE_READY);
	state = Exported();
	XVT_ASSERT_INT_EQ(state->loading.glyph_count, 1);
	XVT_ASSERT_INT_EQ(state->messages.panes[XVT_COCKPIT_MESSAGE_READY].glyph_count, 0);

	/* A message takes a glyph before an open alert line does. */
	XvtCockpitMessages_BeginAlertLine(1, 100, 110, 200, 50);
	XvtCockpitMessages_BeginMessage(0);
	Glyphs('N', 1);
	XvtCockpitMessages_EndMessage();
	XvtCockpitMessages_EndAlertLine();
	Latch(XVT_COCKPIT_MESSAGE_READY);
	state = Exported();
	XVT_ASSERT_INT_EQ(state->messages.panes[XVT_COCKPIT_MESSAGE_READY].glyph_count, 1);
	XVT_ASSERT_INT_EQ(state->alert.glyph_count[0], 0);
}

static void CheckExportPacking(void) {
	Start();
	Message(0, 'r', 1);
	Message(3, 's', 2);
	for (unsigned pane = 0; pane < XVT_COCKPIT_MESSAGE_COUNT; ++pane)
		Latch((XvtCockpitMessageId)pane);
	Alert(1, 'a', 3);
	XvtCockpitMessages_BeginLoadingText();
	Glyphs('L', 1);
	XvtCockpitMessages_EndLoadingText();

	const XvtCockpitState* state = Exported();
	const XvtCockpitGlyph* glyphs = state->overlay_content.glyphs;
	XVT_ASSERT_INT_EQ(state->overlay_content.glyph_count, 7);
	const XvtCockpitMessage* ready = &state->messages.panes[XVT_COCKPIT_MESSAGE_READY];
	const XvtCockpitMessage* system = &state->messages.panes[XVT_COCKPIT_MESSAGE_SYSTEM];
	XVT_ASSERT_INT_EQ(glyphs[ready->first_glyph].character, 'r');
	XVT_ASSERT_INT_EQ(glyphs[system->first_glyph].character, 's');
	XVT_ASSERT_INT_EQ(glyphs[system->first_glyph + 1].character, 't');
	/* The panes come first, then the alert, then the loading text. */
	XVT_ASSERT_INT_EQ(state->alert.first_glyph[0], 3);
	XVT_ASSERT_INT_EQ(glyphs[3].character, 'a');
	XVT_ASSERT_INT_EQ(glyphs[5].character, 'c');
	XVT_ASSERT_INT_EQ(state->loading.first_glyph, 6);
	XVT_ASSERT_INT_EQ(glyphs[6].character, 'L');
}

static void CheckExportScreenSize(void) {
	/* Nothing shown: the view's screen size is left alone. */
	Start();
	Message(0, 'A', 1);
	Latch(XVT_COCKPIT_MESSAGE_READY);
	XVT_ASSERT_INT_EQ(Exported()->view.screen_width, 7);

	/* The alert, the progress bar or the loading text each set it. */
	Alert(1, 'a', 1);
	XVT_ASSERT_INT_EQ(Exported()->view.screen_width, 640);
	XVT_ASSERT_INT_EQ(g_state.view.screen_height, 480);
	Start();
	XvtCockpitMessages_RecordProgress(1, 0, 0, 10, 2, 5);
	XVT_ASSERT_INT_EQ(Exported()->view.screen_width, 640);
	Start();
	XvtCockpitMessages_BeginLoadingText();
	Glyphs('L', 1);
	XvtCockpitMessages_EndLoadingText();
	XVT_ASSERT_INT_EQ(Exported()->view.screen_width, 640);
}

static void CheckSharedCounter(void) {
	Start();
	Message(0, 'A', 1);
	uint64_t message = ReadyGeneration();
	Alert(1, 'a', 1);
	uint64_t alert = Exported()->alert.generation;
	XvtCockpitMessages_RecordProgress(1, 0, 0, 10, 2, 5);
	uint64_t progress = Exported()->loading.generation;
	/* Drawn in turn from one counter: each later part's generation is above the earlier ones. */
	XVT_ASSERT_TRUE(alert > message);
	XVT_ASSERT_TRUE(progress > alert);
}

static void CheckLoadingTextLimit(void) {
	Start();
	XvtCockpitMessages_BeginLoadingText();
	Glyphs(0, XVT_HUD_LOADING_GLYPHS);
	XVT_ASSERT_INT_EQ(Aeron_FatalErrorRequested(), 0);
#ifdef XVT_TEST_LEAK_CHECKER
	__lsan_disable();
#endif
	Glyphs(0, 1);
#ifdef XVT_TEST_LEAK_CHECKER
	__lsan_enable();
#endif
	XVT_ASSERT_TRUE(Aeron_FatalErrorRequested() != 0);
	XvtCockpitMessages_EndLoadingText();
}

int main(void) {
	CheckPaneRouting();
	CheckLatchPlacesPane();
	CheckMessageGeneration();
	CheckMessageLimit();
	CheckIgnoredGlyphs();
	CheckClear();
	CheckBeginPlacement();
	CheckResets();
	CheckAlertLines();
	CheckAlertModesAndLimits();
	CheckAlertBeginAndEnd();
	CheckProgress();
	CheckLoadingText();
	CheckExportPacking();
	CheckExportScreenSize();
	CheckSharedCounter();
	CheckLoadingTextLimit();
	return 0;
}
