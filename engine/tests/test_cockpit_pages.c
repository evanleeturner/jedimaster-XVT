/* Checks the cockpit MFD pages (xvt_runtime/snapshot/cockpit_pages.h) against the promises in its header:
 * a page captured section by section, latched and exported into the page store; which pages Export writes;
 * when generations rise; Clear; the failures that invalidate the state (a nested section, too many rows,
 * too many glyphs, a full store) and BeginFrame clearing them; background, border and scroll records;
 * out-of-range arguments; and what the two resets drop. The test sets the flight text globals and the
 * palette itself; every case starts from Reset, BeginFrame and the same text state.
 *
 * Not checked here: a failed allocation, which a test cannot cause, and how RecordMode's value appears in
 * the exported page, which the header does not say; only that a new mode raises the page's generation. */
#include "test_assert.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/mfd.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/snapshot/cockpit_pages.h"
#include "xvt_runtime/snapshot/render_hud.h"

#include <string.h>

enum { BYPASS = 9, FOREGROUND = 7, BACKGROUND = 4 };

enum { PAGE = MFD_PAGE_GOALS, OTHER = MFD_PAGE_DAMAGE };

static struct xvt_cockpit_state g_state;

/* The clip runs from (90, 40) to (400, 300); the cursor starts at (110, 60). */
static void cockpit_pages_start(void)
{
	for (unsigned index = 0; index < 256; ++index) {
		g_sw_palette[index] = (struct rgb_triplet){
			(uint8_t)(index & 63), (uint8_t)(index >> 6), 7};
	}
	g_flight_clip_left = 90;
	g_flight_clip_top = 40;
	g_flight_clip_right = 400;
	g_flight_clip_bottom = 300;
	g_flight_cursor_x = 110;
	g_flight_cursor_y = 60;
	g_flight_text_color_index = FOREGROUND;
	g_flight_text_bg_color = BACKGROUND;
	g_flight_text_shadow_enabled = 0;
	g_flight_transparent_color_index = BYPASS;
	xvt_cockpit_pages_reset();
	xvt_cockpit_pages_begin_frame();
}

static void glyph(unsigned character)
{
	xvt_cockpit_pages_record_glyph(character, 8, 10, 0);
}

/* One section holding count glyphs, the first one being first. */
static void section(unsigned page, xvt_cockpit_page_section section,
		    unsigned first, unsigned count)
{
	xvt_cockpit_pages_begin_section(page, section);
	for (unsigned index = 0; index < count; ++index) {
		glyph(first + index);
	}
	xvt_cockpit_pages_end_section();
}

/* Exports into a valid state that shows the given pages, -1 for none. */
static const struct xvt_cockpit_state *exported(int page, int other)
{
	memset(&g_state, 0, sizeof g_state);
	g_state.valid = 1;
	if (page >= 0) {
		g_state.pages[page].visible = 1;
	}
	if (other >= 0) {
		g_state.pages[other].visible = 1;
	}
	xvt_cockpit_pages_export(&g_state);
	return &g_state;
}

static void check_capture_latch_export(void)
{
	cockpit_pages_start();
	xvt_cockpit_pages_set_origin(PAGE, 100, 50);
	section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'A', 1);
	xvt_cockpit_pages_begin_section(PAGE, XVT_COCKPIT_PAGE_BODY);
	xvt_cockpit_pages_record_row(7, 1);
	g_flight_cursor_x = 120;
	g_flight_cursor_y = 70;
	glyph('B');
	g_flight_cursor_x = 128;
	glyph('C');
	xvt_cockpit_pages_end_section();
	xvt_cockpit_pages_latch(PAGE);

	const struct xvt_cockpit_state *state = exported(PAGE, -1);
	const struct xvt_cockpit_page *page = &state->pages[PAGE];
	const struct xvt_cockpit_page_store *store = &state->page_content;
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_INT_EQ(page->visible, 1);
	XVT_ASSERT_INT_EQ(page->glyph_count, 3);
	XVT_ASSERT_INT_EQ(page->header_glyph_count, 1);
	XVT_ASSERT_INT_EQ(store->glyph_count, 3);
	/* The header comes first, then the body. */
	XVT_ASSERT_INT_EQ(store->glyphs[page->first_glyph].character, 'A');
	XVT_ASSERT_INT_EQ(store->glyphs[page->first_glyph + 1].character, 'B');
	XVT_ASSERT_INT_EQ(store->glyphs[page->first_glyph + 2].character, 'C');
	/* Glyphs, rows and bounds are relative to the page's origin. */
	XVT_ASSERT_INT_EQ(store->glyphs[page->first_glyph].x, 110 - 100);
	XVT_ASSERT_INT_EQ(store->glyphs[page->first_glyph].y, 60 - 50);
	XVT_ASSERT_INT_EQ(store->glyphs[page->first_glyph + 2].x, 128 - 100);

	XVT_ASSERT_INT_EQ(page->row_count, 1);
	XVT_ASSERT_INT_EQ(store->row_count, 1);
	const struct xvt_cockpit_page_row *row =
		&store->rows[page->first_store_row];
	XVT_ASSERT_INT_EQ(row->key, 7);
	XVT_ASSERT_INT_EQ(row->selected, 1);
	XVT_ASSERT_INT_EQ(row->glyph_count, 2);
	XVT_ASSERT_INT_EQ(store->glyphs[row->first_glyph].character, 'B');
	XVT_ASSERT_INT_EQ(row->bounds.x, 90 - 100);
	XVT_ASSERT_INT_EQ(row->bounds.y, 40 - 50);
	XVT_ASSERT_INT_EQ(row->bounds.width, 310);
	XVT_ASSERT_INT_EQ(row->bounds.height, 260);
	XVT_ASSERT_INT_EQ(row->background_argb,
			  xvt_render_draw_color(BACKGROUND));
}

static void check_export_writes_latched_shown_pages(void)
{
	cockpit_pages_start();
	section(PAGE, XVT_COCKPIT_PAGE_BODY, 'A', 2);
	section(OTHER, XVT_COCKPIT_PAGE_BODY, 'a', 3);
	xvt_cockpit_pages_latch(PAGE);
	xvt_cockpit_pages_latch(OTHER);

	/* A latched page the state does not show stays hidden and unwritten. */
	const struct xvt_cockpit_state *state = exported(PAGE, -1);
	XVT_ASSERT_INT_EQ(state->pages[PAGE].glyph_count, 2);
	XVT_ASSERT_INT_EQ(state->pages[OTHER].visible, 0);
	XVT_ASSERT_INT_EQ(state->pages[OTHER].glyph_count, 0);
	XVT_ASSERT_INT_EQ(state->page_content.glyph_count, 2);

	/* Both shown and latched: both packed into the store. */
	state = exported(PAGE, OTHER);
	XVT_ASSERT_INT_EQ(state->pages[OTHER].visible, 1);
	XVT_ASSERT_INT_EQ(state->pages[OTHER].glyph_count, 3);
	XVT_ASSERT_INT_EQ(state->page_content.glyph_count, 5);
	XVT_ASSERT_INT_EQ(
		state->page_content.glyphs[state->pages[OTHER].first_glyph]
			.character,
		'a');

	/* A new frame unlatches every page: a shown page that is not latched again is hidden. */
	xvt_cockpit_pages_begin_frame();
	xvt_cockpit_pages_latch(PAGE);
	state = exported(PAGE, OTHER);
	XVT_ASSERT_INT_EQ(state->pages[PAGE].visible, 1);
	XVT_ASSERT_INT_EQ(state->pages[OTHER].visible, 0);
	XVT_ASSERT_INT_EQ(state->pages[OTHER].glyph_count, 0);
}

static uint64_t generation(void)
{
	xvt_cockpit_pages_latch(PAGE);
	return exported(PAGE, -1)->pages[PAGE].content_generation;
}

static void check_generations(void)
{
	cockpit_pages_start();
	section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'A', 1);
	uint64_t first = generation();
	XVT_ASSERT_TRUE(first > 0);

	/* The same section captured again changes nothing. */
	section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'A', 1);
	XVT_ASSERT_INT_EQ(generation(), first);

	/* New glyphs, a new scroll position and a new mode each raise it. */
	section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'B', 1);
	uint64_t second = generation();
	XVT_ASSERT_TRUE(second > first);
	xvt_cockpit_pages_record_scroll(PAGE, 1, 2, 0);
	uint64_t third = generation();
	XVT_ASSERT_TRUE(third > second);
	xvt_cockpit_pages_record_mode(PAGE, 4);
	XVT_ASSERT_TRUE(generation() > third);
}

static void check_clear(void)
{
	cockpit_pages_start();
	section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'A', 1);
	section(PAGE, XVT_COCKPIT_PAGE_BODY, 'B', 2);
	xvt_cockpit_pages_record_background(PAGE);
	uint64_t before = generation();

	xvt_cockpit_pages_clear(PAGE);
	uint64_t after = generation();
	const struct xvt_cockpit_page *page = &g_state.pages[PAGE];
	XVT_ASSERT_TRUE(after > before);
	XVT_ASSERT_INT_EQ(page->glyph_count, 0);
	XVT_ASSERT_INT_EQ(page->row_count, 0);
	XVT_ASSERT_INT_EQ(page->background_argb, 0);
}

static void check_nested_section_fails(void)
{
	cockpit_pages_start();
	section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'A', 1);
	xvt_cockpit_pages_latch(PAGE);
	XVT_ASSERT_INT_EQ(exported(PAGE, -1)->valid, 1);

	/* Opening a section while one is open fails the open capture. */
	xvt_cockpit_pages_begin_section(PAGE, XVT_COCKPIT_PAGE_HEADER);
	glyph('X');
	xvt_cockpit_pages_begin_section(PAGE, XVT_COCKPIT_PAGE_BODY);
	glyph('Y');
	xvt_cockpit_pages_end_section();
	xvt_cockpit_pages_end_section();

	/* Latching a page with a failed section invalidates this frame's export, and writes nothing. */
	xvt_cockpit_pages_begin_frame();
	xvt_cockpit_pages_latch(PAGE);
	const struct xvt_cockpit_state *state = exported(PAGE, -1);
	XVT_ASSERT_INT_EQ(state->valid, 0);
	XVT_ASSERT_INT_EQ(state->pages[PAGE].glyph_count, 0);

	/* The next frame starts clear. */
	xvt_cockpit_pages_begin_frame();
	XVT_ASSERT_INT_EQ(exported(PAGE, -1)->valid, 1);

	/* Clear drops the failure mark. */
	xvt_cockpit_pages_clear(PAGE);
	section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'Z', 1);
	xvt_cockpit_pages_latch(PAGE);
	state = exported(PAGE, -1);
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_INT_EQ(
		state->page_content.glyphs[state->pages[PAGE].first_glyph]
			.character,
		'Z');
}

static void rows(unsigned count)
{
	xvt_cockpit_pages_begin_section(PAGE, XVT_COCKPIT_PAGE_BODY);
	for (unsigned row = 0; row < count; ++row) {
		xvt_cockpit_pages_record_row(row, 0);
		glyph('r');
	}
	xvt_cockpit_pages_end_section();
	xvt_cockpit_pages_latch(PAGE);
}

static void check_row_limit(void)
{
	cockpit_pages_start();
	rows(XVT_HUD_ROWS_PER_SECTION);
	const struct xvt_cockpit_state *state = exported(PAGE, -1);
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_INT_EQ(state->pages[PAGE].row_count,
			  XVT_HUD_ROWS_PER_SECTION);

	cockpit_pages_start();
	rows(XVT_HUD_ROWS_PER_SECTION + 1);
	XVT_ASSERT_INT_EQ(exported(PAGE, -1)->valid, 0);
}

static void check_glyph_limit(void)
{
	cockpit_pages_start();
	section(PAGE, XVT_COCKPIT_PAGE_BODY, 0, XVT_HUD_PAGE_GLYPH_CAPACITY);
	xvt_cockpit_pages_latch(PAGE);
	const struct xvt_cockpit_state *state = exported(PAGE, -1);
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_INT_EQ(state->pages[PAGE].glyph_count,
			  XVT_HUD_PAGE_GLYPH_CAPACITY);

	cockpit_pages_start();
	section(PAGE, XVT_COCKPIT_PAGE_BODY, 0,
		XVT_HUD_PAGE_GLYPH_CAPACITY + 1);
	xvt_cockpit_pages_latch(PAGE);
	XVT_ASSERT_INT_EQ(exported(PAGE, -1)->valid, 0);
}

static void check_store_overflow(void)
{
	cockpit_pages_start();
	/* Each page fits the store alone; together they do not. */
	section(PAGE, XVT_COCKPIT_PAGE_BODY, 0, 5000);
	section(OTHER, XVT_COCKPIT_PAGE_BODY, 0, 5000);
	xvt_cockpit_pages_latch(PAGE);
	xvt_cockpit_pages_latch(OTHER);
	const struct xvt_cockpit_state *state = exported(PAGE, OTHER);
	XVT_ASSERT_INT_EQ(state->valid, 0);
	XVT_ASSERT_INT_EQ(state->page_content.glyph_count, 5000);
}

static void check_background_and_border(void)
{
	cockpit_pages_start();
	xvt_cockpit_pages_set_origin(PAGE, 100, 50);
	xvt_cockpit_pages_record_background(PAGE);
	g_flight_clip_left = 95;
	xvt_cockpit_pages_record_border(PAGE);
	uint64_t first = generation();
	const struct xvt_cockpit_page *page = &g_state.pages[PAGE];
	XVT_ASSERT_INT_EQ(page->background_bounds.x, 90 - 100);
	XVT_ASSERT_INT_EQ(page->background_bounds.y, 40 - 50);
	XVT_ASSERT_INT_EQ(page->background_bounds.width, 310);
	XVT_ASSERT_INT_EQ(page->background_bounds.height, 260);
	XVT_ASSERT_INT_EQ(page->background_argb,
			  xvt_render_draw_color(BACKGROUND));
	XVT_ASSERT_INT_EQ(page->border_bounds.x, 95 - 100);
	XVT_ASSERT_INT_EQ(page->border_bounds.width, 305);
	XVT_ASSERT_INT_EQ(page->border_argb, xvt_render_draw_color(BACKGROUND));

	/* The same record again changes nothing. */
	xvt_cockpit_pages_record_border(PAGE);
	XVT_ASSERT_INT_EQ(generation(), first);

	/* In the key color both are 0, and the change raises the generation. */
	g_flight_text_bg_color = BYPASS;
	xvt_cockpit_pages_record_background(PAGE);
	xvt_cockpit_pages_record_border(PAGE);
	XVT_ASSERT_TRUE(generation() > first);
	XVT_ASSERT_INT_EQ(page->background_argb, 0);
	XVT_ASSERT_INT_EQ(page->border_argb, 0);
}

static void check_scroll(void)
{
	cockpit_pages_start();
	xvt_cockpit_pages_record_scroll(PAGE, 3, 20, 5);
	generation();
	const struct xvt_cockpit_page *page = &g_state.pages[PAGE];
	XVT_ASSERT_INT_EQ(page->first_visible_row, 3);
	XVT_ASSERT_INT_EQ(page->total_rows, 20);
	XVT_ASSERT_INT_EQ(page->selected_row, 5);
}

static void check_out_of_range_ignored(void)
{
	cockpit_pages_start();
	xvt_cockpit_pages_set_origin(MFD_PAGE_COUNT, 1, 1);
	xvt_cockpit_pages_clear(MFD_PAGE_COUNT);
	xvt_cockpit_pages_record_background(MFD_PAGE_COUNT);
	xvt_cockpit_pages_record_border(MFD_PAGE_COUNT);
	xvt_cockpit_pages_record_scroll(MFD_PAGE_COUNT, 1, 2, 3);
	xvt_cockpit_pages_record_mode(MFD_PAGE_COUNT, 1);
	xvt_cockpit_pages_latch(MFD_PAGE_COUNT);

	/* Out-of-range sections open nothing, so the next section opens without a nesting failure. */
	xvt_cockpit_pages_begin_section(MFD_PAGE_COUNT,
					XVT_COCKPIT_PAGE_HEADER);
	xvt_cockpit_pages_begin_section(PAGE, XVT_COCKPIT_PAGE_SECTION_COUNT);
	glyph('X');
	xvt_cockpit_pages_end_section();
	section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'A', 1);
	xvt_cockpit_pages_latch(PAGE);
	const struct xvt_cockpit_state *state = exported(PAGE, -1);
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_INT_EQ(state->pages[PAGE].glyph_count, 1);
	XVT_ASSERT_INT_EQ(
		state->page_content.glyphs[state->pages[PAGE].first_glyph]
			.character,
		'A');
}

static void check_ignored_glyphs_and_rows(void)
{
	cockpit_pages_start();
	/* With no capture open, glyphs and rows go nowhere. */
	glyph('X');
	xvt_cockpit_pages_record_row(1, 0);
	/* Outside the clip, a glyph is not captured. */
	xvt_cockpit_pages_begin_section(PAGE, XVT_COCKPIT_PAGE_BODY);
	glyph('A');
	g_flight_cursor_x = 500;
	glyph('Y');
	xvt_cockpit_pages_end_section();
	xvt_cockpit_pages_latch(PAGE);
	const struct xvt_cockpit_state *state = exported(PAGE, -1);
	XVT_ASSERT_INT_EQ(state->pages[PAGE].glyph_count, 1);
	XVT_ASSERT_INT_EQ(state->pages[PAGE].row_count, 0);
	XVT_ASSERT_INT_EQ(
		state->page_content.glyphs[state->pages[PAGE].first_glyph]
			.character,
		'A');
}

static void check_resets(void)
{
	/* Reset abandons an open capture: nothing more is captured and a new section opens cleanly. */
	cockpit_pages_start();
	xvt_cockpit_pages_begin_section(PAGE, XVT_COCKPIT_PAGE_HEADER);
	xvt_cockpit_pages_reset();
	glyph('X');
	xvt_cockpit_pages_end_section();
	xvt_cockpit_pages_latch(PAGE);
	XVT_ASSERT_INT_EQ(exported(PAGE, -1)->pages[PAGE].glyph_count, 0);
	section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'A', 1);
	xvt_cockpit_pages_latch(PAGE);
	XVT_ASSERT_INT_EQ(exported(PAGE, -1)->valid, 1);

	/* Reset clears the placed pages too. */
	xvt_cockpit_pages_reset();
	XVT_ASSERT_INT_EQ(exported(PAGE, -1)->pages[PAGE].visible, 0);

	/* ResetWorking keeps the placed page; the working page is empty afterwards. */
	cockpit_pages_start();
	section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'A', 1);
	xvt_cockpit_pages_latch(PAGE);
	xvt_cockpit_pages_begin_section(PAGE, XVT_COCKPIT_PAGE_BODY);
	xvt_cockpit_pages_reset_working();
	XVT_ASSERT_INT_EQ(exported(PAGE, -1)->pages[PAGE].glyph_count, 1);
	xvt_cockpit_pages_latch(PAGE);
	XVT_ASSERT_INT_EQ(exported(PAGE, -1)->pages[PAGE].glyph_count, 0);
	/* It abandoned the open capture as well. */
	section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'B', 1);
	xvt_cockpit_pages_latch(PAGE);
	const struct xvt_cockpit_state *state = exported(PAGE, -1);
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_INT_EQ(state->pages[PAGE].glyph_count, 1);
}

int main(void)
{
	check_capture_latch_export();
	check_export_writes_latched_shown_pages();
	check_generations();
	check_clear();
	check_nested_section_fails();
	check_row_limit();
	check_glyph_limit();
	check_store_overflow();
	check_background_and_border();
	check_scroll();
	check_out_of_range_ignored();
	check_ignored_glyphs_and_rows();
	check_resets();
	xvt_cockpit_pages_reset();
	return 0;
}
