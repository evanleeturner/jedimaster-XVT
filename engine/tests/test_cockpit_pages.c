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

static XvtCockpitState g_state;

/* The clip runs from (90, 40) to (400, 300); the cursor starts at (110, 60). */
static void Start(void) {
	for (unsigned index = 0; index < 256; ++index)
		g_swPalette[index] = (RgbTriplet) { (uint8_t)(index & 63), (uint8_t)(index >> 6), 7 };
	g_flightClipLeft = 90;
	g_flightClipTop = 40;
	g_flightClipRight = 400;
	g_flightClipBottom = 300;
	g_flightCursorX = 110;
	g_flightCursorY = 60;
	g_flightTextColorIndex = FOREGROUND;
	g_flightTextBgColor = BACKGROUND;
	g_flightTextShadowEnabled = 0;
	g_flightColorEscapeBypassChar = BYPASS;
	XvtCockpitPages_Reset();
	XvtCockpitPages_BeginFrame();
}

static void Glyph(unsigned character) { XvtCockpitPages_RecordGlyph(character, 8, 10, 0); }

/* One section holding count glyphs, the first one being first. */
static void Section(unsigned page, XvtCockpitPageSection section, unsigned first, unsigned count) {
	XvtCockpitPages_BeginSection(page, section);
	for (unsigned index = 0; index < count; ++index)
		Glyph(first + index);
	XvtCockpitPages_EndSection();
}

/* Exports into a valid state that shows the given pages, -1 for none. */
static const XvtCockpitState* Exported(int page, int other) {
	memset(&g_state, 0, sizeof g_state);
	g_state.valid = 1;
	if (page >= 0)
		g_state.pages[page].visible = 1;
	if (other >= 0)
		g_state.pages[other].visible = 1;
	XvtCockpitPages_Export(&g_state);
	return &g_state;
}

static void CheckCaptureLatchExport(void) {
	Start();
	XvtCockpitPages_SetOrigin(PAGE, 100, 50);
	Section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'A', 1);
	XvtCockpitPages_BeginSection(PAGE, XVT_COCKPIT_PAGE_BODY);
	XvtCockpitPages_RecordRow(7, 1);
	g_flightCursorX = 120;
	g_flightCursorY = 70;
	Glyph('B');
	g_flightCursorX = 128;
	Glyph('C');
	XvtCockpitPages_EndSection();
	XvtCockpitPages_Latch(PAGE);

	const XvtCockpitState* state = Exported(PAGE, -1);
	const XvtCockpitPage* page = &state->pages[PAGE];
	const XvtCockpitPageStore* store = &state->page_content;
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
	const XvtCockpitPageRow* row = &store->rows[page->first_row];
	XVT_ASSERT_INT_EQ(row->key, 7);
	XVT_ASSERT_INT_EQ(row->selected, 1);
	XVT_ASSERT_INT_EQ(row->glyph_count, 2);
	XVT_ASSERT_INT_EQ(store->glyphs[row->first_glyph].character, 'B');
	XVT_ASSERT_INT_EQ(row->bounds.x, 90 - 100);
	XVT_ASSERT_INT_EQ(row->bounds.y, 40 - 50);
	XVT_ASSERT_INT_EQ(row->bounds.width, 310);
	XVT_ASSERT_INT_EQ(row->bounds.height, 260);
	XVT_ASSERT_INT_EQ(row->background_argb, XvtRenderDraw_Color(BACKGROUND));
}

static void CheckExportWritesLatchedShownPages(void) {
	Start();
	Section(PAGE, XVT_COCKPIT_PAGE_BODY, 'A', 2);
	Section(OTHER, XVT_COCKPIT_PAGE_BODY, 'a', 3);
	XvtCockpitPages_Latch(PAGE);
	XvtCockpitPages_Latch(OTHER);

	/* A latched page the state does not show stays hidden and unwritten. */
	const XvtCockpitState* state = Exported(PAGE, -1);
	XVT_ASSERT_INT_EQ(state->pages[PAGE].glyph_count, 2);
	XVT_ASSERT_INT_EQ(state->pages[OTHER].visible, 0);
	XVT_ASSERT_INT_EQ(state->pages[OTHER].glyph_count, 0);
	XVT_ASSERT_INT_EQ(state->page_content.glyph_count, 2);

	/* Both shown and latched: both packed into the store. */
	state = Exported(PAGE, OTHER);
	XVT_ASSERT_INT_EQ(state->pages[OTHER].visible, 1);
	XVT_ASSERT_INT_EQ(state->pages[OTHER].glyph_count, 3);
	XVT_ASSERT_INT_EQ(state->page_content.glyph_count, 5);
	XVT_ASSERT_INT_EQ(state->page_content.glyphs[state->pages[OTHER].first_glyph].character, 'a');

	/* A new frame unlatches every page: a shown page that is not latched again is hidden. */
	XvtCockpitPages_BeginFrame();
	XvtCockpitPages_Latch(PAGE);
	state = Exported(PAGE, OTHER);
	XVT_ASSERT_INT_EQ(state->pages[PAGE].visible, 1);
	XVT_ASSERT_INT_EQ(state->pages[OTHER].visible, 0);
	XVT_ASSERT_INT_EQ(state->pages[OTHER].glyph_count, 0);
}

static uint64_t Generation(void) {
	XvtCockpitPages_Latch(PAGE);
	return Exported(PAGE, -1)->pages[PAGE].content_generation;
}

static void CheckGenerations(void) {
	Start();
	Section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'A', 1);
	uint64_t first = Generation();
	XVT_ASSERT_TRUE(first > 0);

	/* The same section captured again changes nothing. */
	Section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'A', 1);
	XVT_ASSERT_INT_EQ(Generation(), first);

	/* New glyphs, a new scroll position and a new mode each raise it. */
	Section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'B', 1);
	uint64_t second = Generation();
	XVT_ASSERT_TRUE(second > first);
	XvtCockpitPages_RecordScroll(PAGE, 1, 2, 0);
	uint64_t third = Generation();
	XVT_ASSERT_TRUE(third > second);
	XvtCockpitPages_RecordMode(PAGE, 4);
	XVT_ASSERT_TRUE(Generation() > third);
}

static void CheckClear(void) {
	Start();
	Section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'A', 1);
	Section(PAGE, XVT_COCKPIT_PAGE_BODY, 'B', 2);
	XvtCockpitPages_RecordBackground(PAGE);
	uint64_t before = Generation();

	XvtCockpitPages_Clear(PAGE);
	uint64_t after = Generation();
	const XvtCockpitPage* page = &g_state.pages[PAGE];
	XVT_ASSERT_TRUE(after > before);
	XVT_ASSERT_INT_EQ(page->glyph_count, 0);
	XVT_ASSERT_INT_EQ(page->row_count, 0);
	XVT_ASSERT_INT_EQ(page->background_argb, 0);
}

static void CheckNestedSectionFails(void) {
	Start();
	Section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'A', 1);
	XvtCockpitPages_Latch(PAGE);
	XVT_ASSERT_INT_EQ(Exported(PAGE, -1)->valid, 1);

	/* Opening a section while one is open fails the open capture. */
	XvtCockpitPages_BeginSection(PAGE, XVT_COCKPIT_PAGE_HEADER);
	Glyph('X');
	XvtCockpitPages_BeginSection(PAGE, XVT_COCKPIT_PAGE_BODY);
	Glyph('Y');
	XvtCockpitPages_EndSection();
	XvtCockpitPages_EndSection();

	/* Latching a page with a failed section invalidates this frame's export, and writes nothing. */
	XvtCockpitPages_BeginFrame();
	XvtCockpitPages_Latch(PAGE);
	const XvtCockpitState* state = Exported(PAGE, -1);
	XVT_ASSERT_INT_EQ(state->valid, 0);
	XVT_ASSERT_INT_EQ(state->pages[PAGE].glyph_count, 0);

	/* The next frame starts clear. */
	XvtCockpitPages_BeginFrame();
	XVT_ASSERT_INT_EQ(Exported(PAGE, -1)->valid, 1);

	/* Clear drops the failure mark. */
	XvtCockpitPages_Clear(PAGE);
	Section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'Z', 1);
	XvtCockpitPages_Latch(PAGE);
	state = Exported(PAGE, -1);
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_INT_EQ(state->page_content.glyphs[state->pages[PAGE].first_glyph].character, 'Z');
}

static void Rows(unsigned count) {
	XvtCockpitPages_BeginSection(PAGE, XVT_COCKPIT_PAGE_BODY);
	for (unsigned row = 0; row < count; ++row) {
		XvtCockpitPages_RecordRow(row, 0);
		Glyph('r');
	}
	XvtCockpitPages_EndSection();
	XvtCockpitPages_Latch(PAGE);
}

static void CheckRowLimit(void) {
	Start();
	Rows(XVT_HUD_VISIBLE_ROWS_PER_PAGE);
	const XvtCockpitState* state = Exported(PAGE, -1);
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_INT_EQ(state->pages[PAGE].row_count, XVT_HUD_VISIBLE_ROWS_PER_PAGE);

	Start();
	Rows(XVT_HUD_VISIBLE_ROWS_PER_PAGE + 1);
	XVT_ASSERT_INT_EQ(Exported(PAGE, -1)->valid, 0);
}

static void CheckGlyphLimit(void) {
	Start();
	Section(PAGE, XVT_COCKPIT_PAGE_BODY, 0, XVT_HUD_PAGE_GLYPH_CAPACITY);
	XvtCockpitPages_Latch(PAGE);
	const XvtCockpitState* state = Exported(PAGE, -1);
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_INT_EQ(state->pages[PAGE].glyph_count, XVT_HUD_PAGE_GLYPH_CAPACITY);

	Start();
	Section(PAGE, XVT_COCKPIT_PAGE_BODY, 0, XVT_HUD_PAGE_GLYPH_CAPACITY + 1);
	XvtCockpitPages_Latch(PAGE);
	XVT_ASSERT_INT_EQ(Exported(PAGE, -1)->valid, 0);
}

static void CheckStoreOverflow(void) {
	Start();
	/* Each page fits the store alone; together they do not. */
	Section(PAGE, XVT_COCKPIT_PAGE_BODY, 0, 5000);
	Section(OTHER, XVT_COCKPIT_PAGE_BODY, 0, 5000);
	XvtCockpitPages_Latch(PAGE);
	XvtCockpitPages_Latch(OTHER);
	const XvtCockpitState* state = Exported(PAGE, OTHER);
	XVT_ASSERT_INT_EQ(state->valid, 0);
	XVT_ASSERT_INT_EQ(state->page_content.glyph_count, 5000);
}

static void CheckBackgroundAndBorder(void) {
	Start();
	XvtCockpitPages_SetOrigin(PAGE, 100, 50);
	XvtCockpitPages_RecordBackground(PAGE);
	g_flightClipLeft = 95;
	XvtCockpitPages_RecordBorder(PAGE);
	uint64_t first = Generation();
	const XvtCockpitPage* page = &g_state.pages[PAGE];
	XVT_ASSERT_INT_EQ(page->background_bounds.x, 90 - 100);
	XVT_ASSERT_INT_EQ(page->background_bounds.y, 40 - 50);
	XVT_ASSERT_INT_EQ(page->background_bounds.width, 310);
	XVT_ASSERT_INT_EQ(page->background_bounds.height, 260);
	XVT_ASSERT_INT_EQ(page->background_argb, XvtRenderDraw_Color(BACKGROUND));
	XVT_ASSERT_INT_EQ(page->border_bounds.x, 95 - 100);
	XVT_ASSERT_INT_EQ(page->border_bounds.width, 305);
	XVT_ASSERT_INT_EQ(page->border_argb, XvtRenderDraw_Color(BACKGROUND));

	/* The same record again changes nothing. */
	XvtCockpitPages_RecordBorder(PAGE);
	XVT_ASSERT_INT_EQ(Generation(), first);

	/* In the key color both are 0, and the change raises the generation. */
	g_flightTextBgColor = BYPASS;
	XvtCockpitPages_RecordBackground(PAGE);
	XvtCockpitPages_RecordBorder(PAGE);
	XVT_ASSERT_TRUE(Generation() > first);
	XVT_ASSERT_INT_EQ(page->background_argb, 0);
	XVT_ASSERT_INT_EQ(page->border_argb, 0);
}

static void CheckScroll(void) {
	Start();
	XvtCockpitPages_RecordScroll(PAGE, 3, 20, 5);
	Generation();
	const XvtCockpitPage* page = &g_state.pages[PAGE];
	XVT_ASSERT_INT_EQ(page->first_visible_row, 3);
	XVT_ASSERT_INT_EQ(page->total_rows, 20);
	XVT_ASSERT_INT_EQ(page->selected_row, 5);
}

static void CheckOutOfRangeIgnored(void) {
	Start();
	XvtCockpitPages_SetOrigin(MFD_PAGE_COUNT, 1, 1);
	XvtCockpitPages_Clear(MFD_PAGE_COUNT);
	XvtCockpitPages_RecordBackground(MFD_PAGE_COUNT);
	XvtCockpitPages_RecordBorder(MFD_PAGE_COUNT);
	XvtCockpitPages_RecordScroll(MFD_PAGE_COUNT, 1, 2, 3);
	XvtCockpitPages_RecordMode(MFD_PAGE_COUNT, 1);
	XvtCockpitPages_Latch(MFD_PAGE_COUNT);

	/* Out-of-range sections open nothing, so the next section opens without a nesting failure. */
	XvtCockpitPages_BeginSection(MFD_PAGE_COUNT, XVT_COCKPIT_PAGE_HEADER);
	XvtCockpitPages_BeginSection(PAGE, XVT_COCKPIT_PAGE_SECTION_COUNT);
	Glyph('X');
	XvtCockpitPages_EndSection();
	Section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'A', 1);
	XvtCockpitPages_Latch(PAGE);
	const XvtCockpitState* state = Exported(PAGE, -1);
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_INT_EQ(state->pages[PAGE].glyph_count, 1);
	XVT_ASSERT_INT_EQ(state->page_content.glyphs[state->pages[PAGE].first_glyph].character, 'A');
}

static void CheckIgnoredGlyphsAndRows(void) {
	Start();
	/* With no capture open, glyphs and rows go nowhere. */
	Glyph('X');
	XvtCockpitPages_RecordRow(1, 0);
	/* Outside the clip, a glyph is not captured. */
	XvtCockpitPages_BeginSection(PAGE, XVT_COCKPIT_PAGE_BODY);
	Glyph('A');
	g_flightCursorX = 500;
	Glyph('Y');
	XvtCockpitPages_EndSection();
	XvtCockpitPages_Latch(PAGE);
	const XvtCockpitState* state = Exported(PAGE, -1);
	XVT_ASSERT_INT_EQ(state->pages[PAGE].glyph_count, 1);
	XVT_ASSERT_INT_EQ(state->pages[PAGE].row_count, 0);
	XVT_ASSERT_INT_EQ(state->page_content.glyphs[state->pages[PAGE].first_glyph].character, 'A');
}

static void CheckResets(void) {
	/* Reset abandons an open capture: nothing more is captured and a new section opens cleanly. */
	Start();
	XvtCockpitPages_BeginSection(PAGE, XVT_COCKPIT_PAGE_HEADER);
	XvtCockpitPages_Reset();
	Glyph('X');
	XvtCockpitPages_EndSection();
	XvtCockpitPages_Latch(PAGE);
	XVT_ASSERT_INT_EQ(Exported(PAGE, -1)->pages[PAGE].glyph_count, 0);
	Section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'A', 1);
	XvtCockpitPages_Latch(PAGE);
	XVT_ASSERT_INT_EQ(Exported(PAGE, -1)->valid, 1);

	/* Reset clears the placed pages too. */
	XvtCockpitPages_Reset();
	XVT_ASSERT_INT_EQ(Exported(PAGE, -1)->pages[PAGE].visible, 0);

	/* ResetWorking keeps the placed page; the working page is empty afterwards. */
	Start();
	Section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'A', 1);
	XvtCockpitPages_Latch(PAGE);
	XvtCockpitPages_BeginSection(PAGE, XVT_COCKPIT_PAGE_BODY);
	XvtCockpitPages_ResetWorking();
	XVT_ASSERT_INT_EQ(Exported(PAGE, -1)->pages[PAGE].glyph_count, 1);
	XvtCockpitPages_Latch(PAGE);
	XVT_ASSERT_INT_EQ(Exported(PAGE, -1)->pages[PAGE].glyph_count, 0);
	/* It abandoned the open capture as well. */
	Section(PAGE, XVT_COCKPIT_PAGE_HEADER, 'B', 1);
	XvtCockpitPages_Latch(PAGE);
	const XvtCockpitState* state = Exported(PAGE, -1);
	XVT_ASSERT_INT_EQ(state->valid, 1);
	XVT_ASSERT_INT_EQ(state->pages[PAGE].glyph_count, 1);
}

int main(void) {
	CheckCaptureLatchExport();
	CheckExportWritesLatchedShownPages();
	CheckGenerations();
	CheckClear();
	CheckNestedSectionFails();
	CheckRowLimit();
	CheckGlyphLimit();
	CheckStoreOverflow();
	CheckBackgroundAndBorder();
	CheckScroll();
	CheckOutOfRangeIgnored();
	CheckIgnoredGlyphsAndRows();
	CheckResets();
	XvtCockpitPages_Reset();
	return 0;
}
