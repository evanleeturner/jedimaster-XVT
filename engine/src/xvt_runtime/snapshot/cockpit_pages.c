#include "xvt_runtime/snapshot/cockpit_pages.h"

#include "aeron/aeron.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/snapshot/cockpit_text.h"
#include "xvt_runtime/snapshot/render_hud.h"
#include <stdlib.h>
#include <string.h>

typedef struct PageSection {
	XvtCockpitGlyph* glyphs;
	unsigned count, capacity, row_count;
	uint64_t generation;
	XvtCockpitPageRow rows[XVT_HUD_VISIBLE_ROWS_PER_PAGE];
} PageSection;

typedef struct PageContent {
	PageSection sections[XVT_COCKPIT_PAGE_SECTION_COUNT];
	XvtSnapRect background_bounds, border_bounds;
	uint32_t background_argb, border_argb;
	int16_t origin_x, origin_y, first_row, selected_row;
	uint16_t total_rows;
	uint16_t mode;
	uint64_t generation;
	int selected;
	unsigned failed_sections;
} PageContent;

static PageContent g_working[MFD_PAGE_COUNT], g_pending[MFD_PAGE_COUNT];
static PageSection g_building;
static unsigned g_page, g_section;
static int g_capturing, g_captureFailed;
static int g_pendingFailed;
static uint32_t g_palette[256];
static uint64_t g_generation;

static uint32_t CaptureColor(unsigned index) {
	uint32_t color = XvtRenderDraw_Color(index);
	return color == XvtRenderDraw_Color(g_flightColorEscapeBypassChar) ? 0 : color;
}

static XvtSnapRect CaptureLocalBounds(unsigned page) {
	return (XvtSnapRect) { g_flightClipLeft - g_working[page].origin_x,
						   g_flightClipTop - g_working[page].origin_y, g_flightClipRight - g_flightClipLeft,
						   g_flightClipBottom - g_flightClipTop };
}

static int ReserveGlyphs(PageSection* section, unsigned count) {
	if (count <= section->capacity)
		return 1;
	if (count > XVT_HUD_PAGE_GLYPH_CAPACITY)
		return 0;
	unsigned capacity = section->capacity ? section->capacity : 128;
	while (capacity < count)
		capacity *= 2;
	XvtCockpitGlyph* glyphs = realloc(section->glyphs, capacity * sizeof *glyphs);
	if (!glyphs)
		return 0;
	section->glyphs = glyphs;
	section->capacity = capacity;
	return 1;
}

void XvtCockpitPages_Reset(void) {
	for (unsigned page = 0; page < MFD_PAGE_COUNT; ++page)
		for (unsigned section = 0; section < XVT_COCKPIT_PAGE_SECTION_COUNT; ++section) {
			free(g_working[page].sections[section].glyphs);
			free(g_pending[page].sections[section].glyphs);
		}
	free(g_building.glyphs);
	memset(g_working, 0, sizeof g_working);
	memset(g_pending, 0, sizeof g_pending);
	memset(&g_building, 0, sizeof g_building);
	g_capturing = g_captureFailed = 0;
	g_pendingFailed = 0;
}

void XvtCockpitPages_ResetWorking(void) {
	for (unsigned page = 0; page < MFD_PAGE_COUNT; ++page)
		for (unsigned section = 0; section < XVT_COCKPIT_PAGE_SECTION_COUNT; ++section)
			free(g_working[page].sections[section].glyphs);
	memset(g_working, 0, sizeof g_working);
	g_capturing = g_captureFailed = 0;
}

void XvtCockpitPages_BeginFrame(void) {
	g_pendingFailed = 0;
	for (unsigned page = 0; page < MFD_PAGE_COUNT; ++page)
		g_pending[page].selected = 0;
}

void XvtCockpitPages_SetOrigin(unsigned page, int x, int y) {
	if (page >= MFD_PAGE_COUNT)
		return;
	g_working[page].origin_x = (int16_t)x;
	g_working[page].origin_y = (int16_t)y;
}

void XvtCockpitPages_Clear(unsigned page) {
	if (page >= MFD_PAGE_COUNT)
		return;
	PageContent* content = &g_working[page];
	for (unsigned section = 0; section < XVT_COCKPIT_PAGE_SECTION_COUNT; ++section) {
		content->sections[section].count = content->sections[section].row_count = 0;
		content->sections[section].generation = ++g_generation;
	}
	content->background_argb = content->border_argb = 0;
	content->failed_sections = 0;
	content->generation = ++g_generation;
}

void XvtCockpitPages_BeginSection(unsigned page, XvtCockpitPageSection section) {
	if (page >= MFD_PAGE_COUNT || section >= XVT_COCKPIT_PAGE_SECTION_COUNT)
		return;
	if (g_capturing) {
		Aeron_LogError("xvt.snapshot", "nested cockpit page section %u/%u", page, (unsigned)section);
		g_captureFailed = 1;
		return;
	}
	g_page = page;
	g_section = section;
	g_building.count = g_building.row_count = 0;
	g_captureFailed = 0;
	g_capturing = 1;
	for (unsigned color = 0; color < 256; ++color)
		g_palette[color] = XvtRenderDraw_Color(color);
}

static void FinishRow(void) {
	if (g_building.row_count) {
		XvtCockpitPageRow* row = &g_building.rows[g_building.row_count - 1];
		row->glyph_count = (uint16_t)(g_building.count - row->first_glyph);
	}
}

void XvtCockpitPages_EndSection(void) {
	if (!g_capturing)
		return;
	FinishRow();
	if (g_captureFailed)
		g_working[g_page].failed_sections |= 1u << g_section;
	else
		g_working[g_page].failed_sections &= ~(1u << g_section);
	PageSection* previous = &g_working[g_page].sections[g_section];
	if (!g_captureFailed &&
		(previous->count != g_building.count || previous->row_count != g_building.row_count ||
		 (previous->count &&
		  memcmp(previous->glyphs, g_building.glyphs, previous->count * sizeof previous->glyphs[0])) ||
		 (previous->row_count &&
		  memcmp(previous->rows, g_building.rows, previous->row_count * sizeof previous->rows[0])))) {
		/* Publish a complete section; the scratch buffer takes the prior allocation. */
		XvtCockpitGlyph* old_glyphs = previous->glyphs;
		unsigned old_capacity = previous->capacity;
		previous->glyphs = g_building.glyphs;
		previous->capacity = g_building.capacity;
		previous->count = g_building.count;
		previous->row_count = g_building.row_count;
		memcpy(previous->rows, g_building.rows, g_building.row_count * sizeof previous->rows[0]);
		previous->generation = g_working[g_page].generation = ++g_generation;
		g_building.glyphs = old_glyphs;
		g_building.capacity = old_capacity;
	}
	g_capturing = 0;
}

void XvtCockpitPages_RecordGlyph(unsigned character, unsigned advance, unsigned height, int narrow) {
	if (!g_capturing || g_captureFailed)
		return;
	XvtCockpitGlyph glyph;
	if (!XvtCockpitText_CaptureGlyph(&glyph, character, advance, height, narrow, g_working[g_page].origin_x,
									 g_working[g_page].origin_y, g_palette, 1))
		return;
	if (!ReserveGlyphs(&g_building, g_building.count + 1)) {
		Aeron_LogError("xvt.snapshot",
					   "cockpit page %u section %u exceeds glyph capacity or allocation failed", g_page,
					   g_section);
		g_captureFailed = 1;
		return;
	}
	g_building.glyphs[g_building.count++] = glyph;
}

void XvtCockpitPages_RecordRow(uint32_t key, int selected) {
	if (!g_capturing || g_captureFailed)
		return;
	FinishRow();
	if (g_building.row_count == XVT_HUD_VISIBLE_ROWS_PER_PAGE) {
		Aeron_LogError("xvt.snapshot", "cockpit page %u exceeds visible row capacity", g_page);
		g_captureFailed = 1;
		return;
	}
	XvtCockpitPageRow* row = &g_building.rows[g_building.row_count++];
	memset(row, 0, sizeof *row);
	row->key = key;
	row->selected = selected != 0;
	row->first_glyph = (uint16_t)g_building.count;
	row->bounds = CaptureLocalBounds(g_page);
	row->background_argb = CaptureColor(g_flightTextBgColor);
}

void XvtCockpitPages_RecordBackground(unsigned page) {
	if (page >= MFD_PAGE_COUNT)
		return;
	XvtSnapRect bounds = CaptureLocalBounds(page);
	uint32_t color = CaptureColor(g_flightTextBgColor);
	PageContent* content = &g_working[page];
	if (memcmp(&content->background_bounds, &bounds, sizeof bounds) || content->background_argb != color) {
		content->background_bounds = bounds;
		content->background_argb = color;
		content->generation = ++g_generation;
	}
}

void XvtCockpitPages_RecordBorder(unsigned page) {
	if (page >= MFD_PAGE_COUNT)
		return;
	XvtSnapRect bounds = CaptureLocalBounds(page);
	uint32_t color = CaptureColor(g_flightTextBgColor);
	PageContent* content = &g_working[page];
	if (memcmp(&content->border_bounds, &bounds, sizeof bounds) || content->border_argb != color) {
		content->border_bounds = bounds;
		content->border_argb = color;
		content->generation = ++g_generation;
	}
}

void XvtCockpitPages_RecordScroll(unsigned page, int first_row, int total_rows, int selected_row) {
	if (page >= MFD_PAGE_COUNT)
		return;
	g_working[page].first_row = (int16_t)first_row;
	g_working[page].total_rows = (uint16_t)total_rows;
	g_working[page].selected_row = (int16_t)selected_row;
}

void XvtCockpitPages_RecordMode(unsigned page, unsigned mode) {
	if (page < MFD_PAGE_COUNT)
		g_working[page].mode = (uint16_t)mode;
}

void XvtCockpitPages_Latch(unsigned page) {
	if (page >= MFD_PAGE_COUNT)
		return;
	PageContent* destination = &g_pending[page];
	const PageContent* source = &g_working[page];
	if (source->failed_sections) {
		g_pendingFailed = 1;
		return;
	}
	int changed = !destination->generation ||
				  memcmp(&destination->background_bounds, &source->background_bounds,
						 sizeof source->background_bounds) ||
				  memcmp(&destination->border_bounds, &source->border_bounds, sizeof source->border_bounds) ||
				  destination->background_argb != source->background_argb ||
				  destination->border_argb != source->border_argb ||
				  destination->first_row != source->first_row ||
				  destination->total_rows != source->total_rows ||
				  destination->selected_row != source->selected_row || destination->mode != source->mode;
	for (unsigned index = 0; index < XVT_COCKPIT_PAGE_SECTION_COUNT; ++index) {
		PageSection* to = &destination->sections[index];
		const PageSection* from = &source->sections[index];
		if (to->generation == from->generation)
			continue;
		if (to->count == from->count && to->row_count == from->row_count &&
			(!from->count || !memcmp(to->glyphs, from->glyphs, from->count * sizeof from->glyphs[0])) &&
			(!from->row_count || !memcmp(to->rows, from->rows, from->row_count * sizeof from->rows[0]))) {
			to->generation = from->generation;
			continue;
		}
		if (!ReserveGlyphs(to, from->count)) {
			Aeron_LogError("xvt.snapshot", "cannot retain cockpit page %u section %u", page, index);
			g_pendingFailed = 1;
			return;
		}
		if (from->count)
			memcpy(to->glyphs, from->glyphs, from->count * sizeof from->glyphs[0]);
		memcpy(to->rows, from->rows, from->row_count * sizeof from->rows[0]);
		to->count = from->count;
		to->row_count = from->row_count;
		to->generation = from->generation;
		changed = 1;
	}
	destination->background_bounds = source->background_bounds;
	destination->border_bounds = source->border_bounds;
	destination->background_argb = source->background_argb;
	destination->border_argb = source->border_argb;
	destination->first_row = source->first_row;
	destination->total_rows = source->total_rows;
	destination->selected_row = source->selected_row;
	destination->mode = source->mode;
	if (changed)
		destination->generation = ++g_generation;
	destination->selected = 1;
}

void XvtCockpitPages_Export(XvtCockpitState* state) {
	if (g_pendingFailed) {
		state->valid = 0;
		return;
	}
	XvtCockpitPageStore* store = &state->page_content;
	store->row_count = store->glyph_count = 0;
	for (unsigned id = 0; id < MFD_PAGE_COUNT; ++id) {
		XvtCockpitPage* page = &state->pages[id];
		const PageContent* content = &g_pending[id];
		page->glyph_count = page->row_count = page->header_glyph_count = 0;
		page->visible &= content->selected != 0;
		if (!page->visible)
			continue;
		page->content_generation = content->generation;
		page->background_bounds = content->background_bounds;
		page->border_bounds = content->border_bounds;
		page->background_argb = content->background_argb;
		page->border_argb = content->border_argb;
		page->first_visible_row = content->first_row;
		page->total_rows = content->total_rows;
		page->selected_row = content->selected_row;
		page->style = content->mode;
		page->first_glyph = store->glyph_count;
		page->first_row = store->row_count;
		for (unsigned index = 0; index < XVT_COCKPIT_PAGE_SECTION_COUNT; ++index) {
			const PageSection* section = &content->sections[index];
			if (section->count > XVT_HUD_PAGE_GLYPH_CAPACITY - store->glyph_count ||
				section->row_count > XVT_HUD_PAGE_ROW_CAPACITY - store->row_count) {
				Aeron_LogError("xvt.snapshot", "cockpit page %u exceeds snapshot text capacity", id);
				state->valid = 0;
				return;
			}
			if (section->count)
				memcpy(&store->glyphs[store->glyph_count], section->glyphs,
					   section->count * sizeof section->glyphs[0]);
			for (unsigned row = 0; row < section->row_count; ++row) {
				XvtCockpitPageRow* output = &store->rows[store->row_count++];
				*output = section->rows[row];
				output->first_glyph += store->glyph_count;
			}
			store->glyph_count += (uint16_t)section->count;
			if (index == XVT_COCKPIT_PAGE_HEADER)
				page->header_glyph_count = (uint16_t)section->count;
		}
		page->glyph_count = store->glyph_count - page->first_glyph;
		page->row_count = store->row_count - page->first_row;
	}
}
