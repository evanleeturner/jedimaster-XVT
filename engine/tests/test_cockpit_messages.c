/* Checks the cockpit overlay text (xvt_runtime/snapshot/cockpit_messages.h)
 * against the promises in its header: which pane a message goes to, placing a
 * pane, when a pane's generation rises, the glyph limits, clearing and the two
 * resets; the alert's lines, modes and generation; the progress bar; the
 * loading text, its priority over the other captures and its fatal limit; and
 * how Export packs everything into the overlay store and when it sets the
 * screen size. The test sets the flight text globals, the three live message
 * records, the pane timers and the palette itself; every case starts from Reset
 * with the loading text cleared.
 *
 * The full-loading-text check requests a fatal error, which latches for the
 * whole process, so it runs last. Not checked here: which rows an alert line
 * from the second on fills, since the header does not say how its five rows
 * match its three lines, and the border color's value. */
#include <string.h>

#include "aeron/aeron.h"
#include "test_assert.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/player/player.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/snapshot/cockpit_messages.h"
#include "xvt_runtime/snapshot/render_hud.h"

/* The fatal-error request opens SDL's message box, and SDL keeps a small
 * allocation from it when no video device is running, as in a test. The leak
 * checker is told to ignore what that one call allocates. */
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

static struct xvt_cockpit_state g_state;

/* The clip runs from (50, 60) to (450, 300), so its corner, a message's origin,
 * is (50, 60); the cursor starts at (70, 80). The screen is 640 by 480. */
static void cockpit_messages_start(void)
{
	for (unsigned index = 0; index < 256; ++index) {
		g_sw_palette[index] = (struct rgb_triplet){
			(uint8_t)(index & 63), (uint8_t)(index >> 6), 7};
	}
	g_flight_clip_left = 50;
	g_flight_clip_top = 60;
	g_flight_clip_right = 450;
	g_flight_clip_bottom = 300;
	g_flight_cursor_x = 70;
	g_flight_cursor_y = 80;
	g_flight_text_color_index = FOREGROUND;
	g_flight_text_bg_color = BACKGROUND;
	g_flight_text_shadow_enabled = 0;
	g_flight_transparent_color_index = BYPASS;
	g_screen_width = 640;
	g_screen_height = 480;

	memset(g_ready_message_pane_queue, 0,
	       sizeof g_ready_message_pane_queue);
	memset(&g_system_message_pane, 0, sizeof g_system_message_pane);
	memset(&g_flight_group_message_pane, 0,
	       sizeof g_flight_group_message_pane);
	g_ready_message_pane_queue[0].state_or_message_id = READY_ID;
	g_ready_message_pane_queue[0].age_seconds = 3;
	g_system_message_pane.state_or_message_id = SYSTEM_ID;
	g_system_message_pane.age_seconds = 4;
	g_flight_group_message_pane.state_or_message_id = GROUP_ID;
	g_flight_group_message_pane.age_seconds = 5;
	g_local_player = 0;
	memset(g_player_flight_transient_timers, 0,
	       sizeof g_player_flight_transient_timers);
	g_player_flight_transient_timers[0].ready_message_pane_timer = 100;
	g_player_flight_transient_timers[0].system_message_pane_timer = 200;
	g_player_flight_transient_timers[0].flight_group_message_pane_timer =
		300;

	xvt_cockpit_messages_reset();
	xvt_cockpit_messages_clear_progress();
}

static void cockpit_messages_glyphs(unsigned first, unsigned count)
{
	for (unsigned index = 0; index < count; ++index) {
		xvt_cockpit_messages_record_glyph(first + index, 8, 10, 0);
	}
}

static void cockpit_messages_message(int pane_type, unsigned first,
				     unsigned count)
{
	xvt_cockpit_messages_begin_message(pane_type);
	cockpit_messages_glyphs(first, count);
	xvt_cockpit_messages_end_message();
}

/* Latches a pane with its origin as the source point, so its glyphs do not move. */
static void latch(xvt_cockpit_message_id pane)
{
	xvt_cockpit_messages_latch(pane, 50, 60, 0, 0, 100, 20);
}

static const struct xvt_cockpit_state *exported(void)
{
	memset(&g_state, 0, sizeof g_state);
	g_state.view.screen_width = 7;
	g_state.view.screen_height = 7;
	xvt_cockpit_messages_export(&g_state);
	return &g_state;
}

static void cockpit_messages_alert(int mode, unsigned first, unsigned count)
{
	xvt_cockpit_messages_begin_alert_line(mode, 100, 110, 200, 50);
	cockpit_messages_glyphs(first, count);
	xvt_cockpit_messages_end_alert_line();
}

static void check_pane_routing(void)
{
	static const struct {
		int pane_type;
		xvt_cockpit_message_id pane;
		unsigned message_id;
	} routes[] = {
		{8, XVT_COCKPIT_MESSAGE_FLIGHT_GROUP, GROUP_ID},
		{3, XVT_COCKPIT_MESSAGE_SYSTEM, SYSTEM_ID},
		{4, XVT_COCKPIT_MESSAGE_SYSTEM, SYSTEM_ID},
		{7, XVT_COCKPIT_MESSAGE_SYSTEM, SYSTEM_ID},
		{0, XVT_COCKPIT_MESSAGE_READY, READY_ID},
		{5, XVT_COCKPIT_MESSAGE_READY, READY_ID},
		{9, XVT_COCKPIT_MESSAGE_READY, READY_ID},
	};

	for (unsigned route = 0; route < sizeof routes / sizeof routes[0];
	     ++route) {
		cockpit_messages_start();
		cockpit_messages_message(routes[route].pane_type, 'A', 1);
		for (unsigned pane = 0; pane < XVT_COCKPIT_MESSAGE_COUNT;
		     ++pane) {
			latch((xvt_cockpit_message_id)pane);
		}
		const struct xvt_cockpit_state *state = exported();
		for (unsigned pane = 0; pane < XVT_COCKPIT_MESSAGE_COUNT;
		     ++pane) {
			const struct xvt_cockpit_message *message =
				&state->messages.panes[pane];
			int expected = pane == (unsigned)routes[route].pane;
			XVT_ASSERT_INT_EQ(message->visible, expected);
			XVT_ASSERT_INT_EQ(message->glyph_count, expected);
			if (expected) {
				XVT_ASSERT_INT_EQ(message->message_id,
						  routes[route].message_id);
			}
		}
	}
}

static void check_latch_places_pane(void)
{
	cockpit_messages_start();
	xvt_cockpit_messages_begin_message(0);
	cockpit_messages_glyphs('A', 2);
	xvt_cockpit_messages_record_reveal(5);
	xvt_cockpit_messages_end_message();

	/* The live record still holds the message: its live age is used. */
	g_ready_message_pane_queue[0].age_seconds = 9;
	xvt_cockpit_messages_latch(XVT_COCKPIT_MESSAGE_READY, 40, 55, 300, 400,
				   120, 30);
	const struct xvt_cockpit_state *state = exported();
	const struct xvt_cockpit_message *pane =
		&state->messages.panes[XVT_COCKPIT_MESSAGE_READY];
	XVT_ASSERT_INT_EQ(pane->visible, 1);
	XVT_ASSERT_INT_EQ(pane->placement.x, 300);
	XVT_ASSERT_INT_EQ(pane->placement.y, 400);
	XVT_ASSERT_INT_EQ(pane->placement.width, 120);
	XVT_ASSERT_INT_EQ(pane->placement.height, 30);
	XVT_ASSERT_INT_EQ(pane->timer_ticks, 100);
	XVT_ASSERT_INT_EQ(pane->age_seconds, 9);
	XVT_ASSERT_INT_EQ(pane->revealed_characters, 5);
	XVT_ASSERT_INT_EQ(pane->glyph_count, 2);
	/* A glyph captured relative to the origin (50, 60) moves by the origin
	 * minus the source point. */
	const struct xvt_cockpit_glyph *glyph =
		&state->overlay_content.glyphs[pane->first_glyph];
	XVT_ASSERT_INT_EQ(glyph->character, 'A');
	XVT_ASSERT_INT_EQ(glyph->x, (70 - 50) + (50 - 40));
	XVT_ASSERT_INT_EQ(glyph->y, (80 - 60) + (60 - 55));

	/* Once the live record holds another message, the age captured with the message stays. */
	g_ready_message_pane_queue[0].state_or_message_id = READY_ID + 1;
	g_ready_message_pane_queue[0].age_seconds = 15;
	xvt_cockpit_messages_latch(XVT_COCKPIT_MESSAGE_READY, 40, 55, 300, 400,
				   120, 30);
	XVT_ASSERT_INT_EQ(exported()
				  ->messages.panes[XVT_COCKPIT_MESSAGE_READY]
				  .age_seconds,
			  3);

	/* Each pane takes its own live timer; an out-of-range pane is ignored. */
	cockpit_messages_message(3, 'S', 1);
	latch(XVT_COCKPIT_MESSAGE_SYSTEM);
	latch(XVT_COCKPIT_MESSAGE_COUNT);
	XVT_ASSERT_INT_EQ(exported()
				  ->messages.panes[XVT_COCKPIT_MESSAGE_SYSTEM]
				  .timer_ticks,
			  200);
}

static uint64_t ready_generation(void)
{
	latch(XVT_COCKPIT_MESSAGE_READY);
	return exported()->messages.panes[XVT_COCKPIT_MESSAGE_READY].generation;
}

static void check_message_generation(void)
{
	cockpit_messages_start();
	cockpit_messages_message(0, 'A', 1);
	uint64_t first = ready_generation();
	XVT_ASSERT_TRUE(first > 0);

	/* The same glyphs at the same origin: no change. */
	cockpit_messages_message(0, 'A', 1);
	XVT_ASSERT_INT_EQ(ready_generation(), first);

	/* New glyphs raise it; so does a new origin. */
	cockpit_messages_message(0, 'B', 1);
	uint64_t second = ready_generation();
	XVT_ASSERT_TRUE(second > first);
	g_flight_clip_left = 51;
	g_flight_cursor_x = 71;
	cockpit_messages_message(0, 'B', 1);
	XVT_ASSERT_TRUE(ready_generation() > second);
}

static void check_message_limit(void)
{
	cockpit_messages_start();
	cockpit_messages_message(0, 0, XVT_HUD_MESSAGE_GLYPHS);
	uint64_t generation = ready_generation();
	XVT_ASSERT_INT_EQ(
		g_state.messages.panes[XVT_COCKPIT_MESSAGE_READY].glyph_count,
		XVT_HUD_MESSAGE_GLYPHS);

	/* One glyph too many discards the capture: the pane keeps what it had. */
	cockpit_messages_message(0, 1, XVT_HUD_MESSAGE_GLYPHS + 1);
	XVT_ASSERT_INT_EQ(ready_generation(), generation);
	const struct xvt_cockpit_message *pane =
		&g_state.messages.panes[XVT_COCKPIT_MESSAGE_READY];
	XVT_ASSERT_INT_EQ(pane->glyph_count, XVT_HUD_MESSAGE_GLYPHS);
	XVT_ASSERT_INT_EQ(
		g_state.overlay_content.glyphs[pane->first_glyph].character, 0);
}

static void check_ignored_glyphs(void)
{
	cockpit_messages_start();
	/* With no capture open, a glyph goes nowhere. */
	cockpit_messages_glyphs('X', 1);
	XVT_ASSERT_INT_EQ(exported()->overlay_content.glyph_count, 0);
	/* Outside the clip, a glyph is not captured. */
	xvt_cockpit_messages_begin_message(0);
	cockpit_messages_glyphs('A', 1);
	g_flight_cursor_x = 500;
	cockpit_messages_glyphs('Y', 1);
	xvt_cockpit_messages_end_message();
	latch(XVT_COCKPIT_MESSAGE_READY);
	XVT_ASSERT_INT_EQ(exported()
				  ->messages.panes[XVT_COCKPIT_MESSAGE_READY]
				  .glyph_count,
			  1);
}

static void check_clear(void)
{
	cockpit_messages_start();
	cockpit_messages_message(0, 'A', 1);
	uint64_t shown = ready_generation();

	/* A visible pane is cleared and its generation rises. */
	xvt_cockpit_messages_clear(XVT_COCKPIT_MESSAGE_READY);
	uint64_t cleared = ready_generation();
	XVT_ASSERT_INT_EQ(
		g_state.messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 0);
	XVT_ASSERT_TRUE(cleared > shown);

	/* A pane that is not visible is left alone. */
	xvt_cockpit_messages_clear(XVT_COCKPIT_MESSAGE_READY);
	XVT_ASSERT_INT_EQ(ready_generation(), cleared);
}

static void check_begin_placement(void)
{
	cockpit_messages_start();
	cockpit_messages_message(0, 'A', 1);
	latch(XVT_COCKPIT_MESSAGE_READY);
	XVT_ASSERT_INT_EQ(
		exported()->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible,
		1);
	xvt_cockpit_messages_begin_placement();
	const struct xvt_cockpit_state *state = exported();
	XVT_ASSERT_INT_EQ(
		state->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 0);
	XVT_ASSERT_INT_EQ(state->overlay_content.glyph_count, 0);
	latch(XVT_COCKPIT_MESSAGE_READY);
	XVT_ASSERT_INT_EQ(
		exported()->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible,
		1);
}

static void check_resets(void)
{
	/* ResetWorking clears the working panes but not the placed ones. */
	cockpit_messages_start();
	cockpit_messages_message(0, 'A', 1);
	latch(XVT_COCKPIT_MESSAGE_READY);
	xvt_cockpit_messages_reset_working();
	XVT_ASSERT_INT_EQ(
		exported()->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible,
		1);
	latch(XVT_COCKPIT_MESSAGE_READY);
	XVT_ASSERT_INT_EQ(
		exported()->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible,
		0);

	/* ResetWorking abandons an open message capture. */
	xvt_cockpit_messages_begin_message(0);
	xvt_cockpit_messages_reset_working();
	cockpit_messages_glyphs('A', 1);
	xvt_cockpit_messages_end_message();
	latch(XVT_COCKPIT_MESSAGE_READY);
	XVT_ASSERT_INT_EQ(
		exported()->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible,
		0);

	/* Reset clears the placed panes, the alert and the progress bar, but
	 * the loading text survives. */
	cockpit_messages_start();
	cockpit_messages_message(0, 'A', 1);
	latch(XVT_COCKPIT_MESSAGE_READY);
	cockpit_messages_alert(1, 'a', 1);
	xvt_cockpit_messages_record_progress(1, 0, 0, 10, 2, 5);
	xvt_cockpit_messages_begin_loading_text();
	cockpit_messages_glyphs('L', 1);
	xvt_cockpit_messages_end_loading_text();
	xvt_cockpit_messages_reset();
	const struct xvt_cockpit_state *state = exported();
	XVT_ASSERT_INT_EQ(
		state->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 0);
	XVT_ASSERT_INT_EQ(state->alert.active, 0);
	XVT_ASSERT_INT_EQ(state->loading.progress_visible, 0);
	XVT_ASSERT_INT_EQ(state->loading.text_visible, 1);
	XVT_ASSERT_INT_EQ(state->loading.glyph_count, 1);

	/* Reset abandons open message and alert captures. */
	cockpit_messages_start();
	xvt_cockpit_messages_begin_message(0);
	xvt_cockpit_messages_reset();
	cockpit_messages_glyphs('A', 1);
	xvt_cockpit_messages_end_message();
	xvt_cockpit_messages_begin_alert_line(1, 100, 110, 200, 50);
	xvt_cockpit_messages_reset();
	cockpit_messages_glyphs('a', 1);
	xvt_cockpit_messages_end_alert_line();
	latch(XVT_COCKPIT_MESSAGE_READY);
	state = exported();
	XVT_ASSERT_INT_EQ(
		state->messages.panes[XVT_COCKPIT_MESSAGE_READY].visible, 0);
	XVT_ASSERT_INT_EQ(state->alert.active, 0);
	XVT_ASSERT_INT_EQ(state->overlay_content.glyph_count, 0);
}

static void check_alert_lines(void)
{
	cockpit_messages_start();
	xvt_cockpit_messages_begin_alert();
	cockpit_messages_alert(1, 'a', 2);
	const struct xvt_cockpit_alert *alert = &exported()->alert;
	XVT_ASSERT_INT_EQ(alert->active, 1);
	XVT_ASSERT_INT_EQ(alert->placement.x, 100);
	XVT_ASSERT_INT_EQ(alert->placement.y, 110);
	XVT_ASSERT_INT_EQ(alert->placement.width, 200);
	XVT_ASSERT_INT_EQ(alert->placement.height, 50);
	XVT_ASSERT_INT_EQ(alert->line_visible[0], 1);
	XVT_ASSERT_INT_EQ(alert->glyph_count[0], 2);
	XVT_ASSERT_INT_EQ(alert->line_visible[1], 0);
	/* The first line fills every row below it with the text background color. */
	for (unsigned row = 0; row < 5; ++row) {
		XVT_ASSERT_INT_EQ(alert->row_background_argb[row],
				  xvt_render_draw_color(BACKGROUND));
	}
	/* Mode 1 sets the border. */
	uint32_t border = alert->border_argb;
	XVT_ASSERT_TRUE(border != 0);
	uint64_t first = alert->generation;
	XVT_ASSERT_TRUE(first > 0);

	/* Modes 2 and 3 build on the active alert, on lines 1 and 2. */
	cockpit_messages_alert(2, 'b', 1);
	cockpit_messages_alert(3, 'c', 3);
	alert = &exported()->alert;
	XVT_ASSERT_INT_EQ(alert->glyph_count[0], 2);
	XVT_ASSERT_INT_EQ(alert->glyph_count[1], 1);
	XVT_ASSERT_INT_EQ(alert->glyph_count[2], 3);
	XVT_ASSERT_INT_EQ(alert->line_visible[2], 1);
	XVT_ASSERT_INT_EQ(alert->border_argb, border);
	XVT_ASSERT_TRUE(alert->generation > first);
	XVT_ASSERT_INT_EQ(
		g_state.overlay_content.glyphs[alert->first_glyph[1]].character,
		'b');
	XVT_ASSERT_INT_EQ(
		g_state.overlay_content.glyphs[alert->first_glyph[2]].character,
		'c');

	/* A line clears itself and the lines after it, not the ones before. */
	cockpit_messages_alert(2, 'd', 1);
	alert = &exported()->alert;
	XVT_ASSERT_INT_EQ(alert->glyph_count[0], 2);
	XVT_ASSERT_INT_EQ(alert->glyph_count[1], 1);
	XVT_ASSERT_INT_EQ(
		g_state.overlay_content.glyphs[alert->first_glyph[1]].character,
		'd');
	XVT_ASSERT_INT_EQ(alert->line_visible[2], 0);
	XVT_ASSERT_INT_EQ(alert->glyph_count[2], 0);

	/* Mode 0 is line 0 too. */
	cockpit_messages_alert(0, 'e', 1);
	alert = &exported()->alert;
	XVT_ASSERT_INT_EQ(alert->glyph_count[0], 1);
	XVT_ASSERT_INT_EQ(
		g_state.overlay_content.glyphs[alert->first_glyph[0]].character,
		'e');
	XVT_ASSERT_INT_EQ(alert->line_visible[1], 0);
}

static void check_alert_modes_and_limits(void)
{
	cockpit_messages_start();
	cockpit_messages_alert(1, 'a', 2);
	uint64_t generation = exported()->alert.generation;

	/* Modes outside 0 to 3 are ignored: nothing opens, nothing changes. */
	cockpit_messages_alert(4, 'x', 1);
	cockpit_messages_alert(-1, 'x', 1);
	const struct xvt_cockpit_alert *alert = &exported()->alert;
	XVT_ASSERT_INT_EQ(alert->generation, generation);
	XVT_ASSERT_INT_EQ(alert->glyph_count[0], 2);
	XVT_ASSERT_INT_EQ(alert->line_visible[1], 0);

	/* A full line is kept; one glyph more discards the line. */
	cockpit_messages_alert(2, 0, XVT_HUD_ALERT_LINE_GLYPHS);
	alert = &exported()->alert;
	XVT_ASSERT_INT_EQ(alert->glyph_count[1], XVT_HUD_ALERT_LINE_GLYPHS);
	generation = alert->generation;
	cockpit_messages_alert(2, 1, XVT_HUD_ALERT_LINE_GLYPHS + 1);
	alert = &exported()->alert;
	XVT_ASSERT_INT_EQ(alert->generation, generation);
	XVT_ASSERT_INT_EQ(alert->glyph_count[1], XVT_HUD_ALERT_LINE_GLYPHS);
	XVT_ASSERT_INT_EQ(
		g_state.overlay_content.glyphs[alert->first_glyph[1]].character,
		0);
}

static void check_alert_begin_and_end(void)
{
	cockpit_messages_start();
	cockpit_messages_alert(1, 'a', 1);
	cockpit_messages_alert(2, 'b', 1);

	/* EndAlert deactivates and raises the generation; a second one changes nothing. */
	uint64_t active = exported()->alert.generation;
	xvt_cockpit_messages_end_alert();
	const struct xvt_cockpit_alert *alert = &exported()->alert;
	XVT_ASSERT_INT_EQ(alert->active, 0);
	XVT_ASSERT_INT_EQ(alert->glyph_count[0], 0);
	uint64_t ended = alert->generation;
	XVT_ASSERT_TRUE(ended > active);
	xvt_cockpit_messages_end_alert();
	XVT_ASSERT_INT_EQ(exported()->alert.generation, ended);

	/* A line on an inactive alert builds on an empty one. */
	cockpit_messages_alert(3, 'c', 1);
	alert = &exported()->alert;
	XVT_ASSERT_INT_EQ(alert->active, 1);
	XVT_ASSERT_INT_EQ(alert->line_visible[0], 0);
	XVT_ASSERT_INT_EQ(alert->line_visible[1], 0);
	XVT_ASSERT_INT_EQ(alert->glyph_count[1], 0);
	XVT_ASSERT_INT_EQ(alert->glyph_count[2], 1);

	/* BeginAlert clears the alert, generation included, and abandons an open line. */
	xvt_cockpit_messages_begin_alert_line(1, 100, 110, 200, 50);
	cockpit_messages_glyphs('d', 1);
	xvt_cockpit_messages_begin_alert();
	cockpit_messages_glyphs('e', 1);
	xvt_cockpit_messages_end_alert_line();
	alert = &exported()->alert;
	XVT_ASSERT_INT_EQ(alert->active, 0);
	XVT_ASSERT_INT_EQ(alert->generation, 0);
	XVT_ASSERT_INT_EQ(g_state.overlay_content.glyph_count, 0);
}

static void check_progress(void)
{
	cockpit_messages_start();
	xvt_cockpit_messages_record_progress(3, 10, 20, 300, 8, 120);
	const struct xvt_cockpit_loading *loading = &exported()->loading;
	XVT_ASSERT_INT_EQ(loading->progress_visible, 1);
	XVT_ASSERT_INT_EQ(loading->progress_placement.x, 10);
	XVT_ASSERT_INT_EQ(loading->progress_placement.y, 20);
	XVT_ASSERT_INT_EQ(loading->progress_placement.width, 300);
	XVT_ASSERT_INT_EQ(loading->progress_placement.height, 8);
	XVT_ASSERT_INT_EQ(loading->progress_step, 3);
	XVT_ASSERT_INT_EQ(loading->filled_width, 120);
	uint64_t first = loading->generation;
	XVT_ASSERT_TRUE(first > 0);
	xvt_cockpit_messages_record_progress(3, 10, 20, 300, 8, 150);
	XVT_ASSERT_TRUE(exported()->loading.generation > first);

	/* A new flight frame hides the bar and the loading text. */
	xvt_cockpit_messages_begin_loading_text();
	cockpit_messages_glyphs('L', 1);
	xvt_cockpit_messages_end_loading_text();
	xvt_cockpit_messages_begin_flight_frame();
	loading = &exported()->loading;
	XVT_ASSERT_INT_EQ(loading->progress_visible, 0);
	XVT_ASSERT_INT_EQ(loading->text_visible, 0);

	/* ClearProgress hides the bar and clears the loading text. */
	xvt_cockpit_messages_record_progress(3, 10, 20, 300, 8, 150);
	xvt_cockpit_messages_begin_loading_text();
	cockpit_messages_glyphs('L', 1);
	xvt_cockpit_messages_end_loading_text();
	xvt_cockpit_messages_clear_progress();
	loading = &exported()->loading;
	XVT_ASSERT_INT_EQ(loading->progress_visible, 0);
	XVT_ASSERT_INT_EQ(loading->text_visible, 0);
	XVT_ASSERT_INT_EQ(loading->glyph_count, 0);
}

static void check_loading_text(void)
{
	cockpit_messages_start();
	xvt_cockpit_messages_begin_loading_text();
	cockpit_messages_glyphs('L', 3);
	xvt_cockpit_messages_end_loading_text();
	const struct xvt_cockpit_state *state = exported();
	XVT_ASSERT_INT_EQ(state->loading.text_visible, 1);
	XVT_ASSERT_INT_EQ(state->loading.glyph_count, 3);
	/* Relative to the screen: the glyph sits at the cursor. */
	const struct xvt_cockpit_glyph *glyph =
		&state->overlay_content.glyphs[state->loading.first_glyph];
	XVT_ASSERT_INT_EQ(glyph->character, 'L');
	XVT_ASSERT_INT_EQ(glyph->x, 70);
	XVT_ASSERT_INT_EQ(glyph->y, 80);
	uint64_t first = state->loading.text_generation;
	XVT_ASSERT_TRUE(first > 0);

	/* With no glyph the text is hidden, and the generation still rises. */
	xvt_cockpit_messages_begin_loading_text();
	xvt_cockpit_messages_end_loading_text();
	state = exported();
	XVT_ASSERT_INT_EQ(state->loading.text_visible, 0);
	XVT_ASSERT_TRUE(state->loading.text_generation > first);

	/* The loading text takes a glyph before an open message does. */
	xvt_cockpit_messages_begin_loading_text();
	xvt_cockpit_messages_begin_message(0);
	cockpit_messages_glyphs('M', 1);
	xvt_cockpit_messages_end_message();
	xvt_cockpit_messages_end_loading_text();
	latch(XVT_COCKPIT_MESSAGE_READY);
	state = exported();
	XVT_ASSERT_INT_EQ(state->loading.glyph_count, 1);
	XVT_ASSERT_INT_EQ(
		state->messages.panes[XVT_COCKPIT_MESSAGE_READY].glyph_count,
		0);

	/* A message takes a glyph before an open alert line does. */
	xvt_cockpit_messages_begin_alert_line(1, 100, 110, 200, 50);
	xvt_cockpit_messages_begin_message(0);
	cockpit_messages_glyphs('N', 1);
	xvt_cockpit_messages_end_message();
	xvt_cockpit_messages_end_alert_line();
	latch(XVT_COCKPIT_MESSAGE_READY);
	state = exported();
	XVT_ASSERT_INT_EQ(
		state->messages.panes[XVT_COCKPIT_MESSAGE_READY].glyph_count,
		1);
	XVT_ASSERT_INT_EQ(state->alert.glyph_count[0], 0);
}

static void check_export_packing(void)
{
	cockpit_messages_start();
	cockpit_messages_message(0, 'r', 1);
	cockpit_messages_message(3, 's', 2);
	for (unsigned pane = 0; pane < XVT_COCKPIT_MESSAGE_COUNT; ++pane) {
		latch((xvt_cockpit_message_id)pane);
	}
	cockpit_messages_alert(1, 'a', 3);
	xvt_cockpit_messages_begin_loading_text();
	cockpit_messages_glyphs('L', 1);
	xvt_cockpit_messages_end_loading_text();

	const struct xvt_cockpit_state *state = exported();
	const struct xvt_cockpit_glyph *glyphs = state->overlay_content.glyphs;
	XVT_ASSERT_INT_EQ(state->overlay_content.glyph_count, 7);
	const struct xvt_cockpit_message *ready =
		&state->messages.panes[XVT_COCKPIT_MESSAGE_READY];
	const struct xvt_cockpit_message *system =
		&state->messages.panes[XVT_COCKPIT_MESSAGE_SYSTEM];
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

static void check_export_screen_size(void)
{
	/* Nothing shown: the view's screen size is left alone. */
	cockpit_messages_start();
	cockpit_messages_message(0, 'A', 1);
	latch(XVT_COCKPIT_MESSAGE_READY);
	XVT_ASSERT_INT_EQ(exported()->view.screen_width, 7);

	/* The alert, the progress bar or the loading text each set it. */
	cockpit_messages_alert(1, 'a', 1);
	XVT_ASSERT_INT_EQ(exported()->view.screen_width, 640);
	XVT_ASSERT_INT_EQ(g_state.view.screen_height, 480);
	cockpit_messages_start();
	xvt_cockpit_messages_record_progress(1, 0, 0, 10, 2, 5);
	XVT_ASSERT_INT_EQ(exported()->view.screen_width, 640);
	cockpit_messages_start();
	xvt_cockpit_messages_begin_loading_text();
	cockpit_messages_glyphs('L', 1);
	xvt_cockpit_messages_end_loading_text();
	XVT_ASSERT_INT_EQ(exported()->view.screen_width, 640);
}

static void check_shared_counter(void)
{
	cockpit_messages_start();
	cockpit_messages_message(0, 'A', 1);
	uint64_t message = ready_generation();
	cockpit_messages_alert(1, 'a', 1);
	uint64_t alert = exported()->alert.generation;
	xvt_cockpit_messages_record_progress(1, 0, 0, 10, 2, 5);
	uint64_t progress = exported()->loading.generation;
	/* Drawn in turn from one counter: each later part's generation is above
	 * the earlier ones. */
	XVT_ASSERT_TRUE(alert > message);
	XVT_ASSERT_TRUE(progress > alert);
}

static void check_loading_text_limit(void)
{
	cockpit_messages_start();
	xvt_cockpit_messages_begin_loading_text();
	cockpit_messages_glyphs(0, XVT_HUD_LOADING_GLYPHS);
	XVT_ASSERT_INT_EQ(Aeron_FatalErrorRequested(), 0);
#ifdef XVT_TEST_LEAK_CHECKER
	__lsan_disable();
#endif
	cockpit_messages_glyphs(0, 1);
#ifdef XVT_TEST_LEAK_CHECKER
	__lsan_enable();
#endif
	XVT_ASSERT_TRUE(Aeron_FatalErrorRequested() != 0);
	xvt_cockpit_messages_end_loading_text();
}

int main(void)
{
	check_pane_routing();
	check_latch_places_pane();
	check_message_generation();
	check_message_limit();
	check_ignored_glyphs();
	check_clear();
	check_begin_placement();
	check_resets();
	check_alert_lines();
	check_alert_modes_and_limits();
	check_alert_begin_and_end();
	check_progress();
	check_loading_text();
	check_export_packing();
	check_export_screen_size();
	check_shared_counter();
	check_loading_text_limit();
	return 0;
}
