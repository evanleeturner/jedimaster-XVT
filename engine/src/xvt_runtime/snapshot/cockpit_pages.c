#include "xvt_runtime/snapshot/cockpit_pages.h"

#include <stdlib.h>
#include <string.h>

#include "aeron/aeron.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/snapshot/cockpit_text.h"
#include "xvt_runtime/snapshot/render_hud.h"

struct page_section {
	struct xvt_cockpit_glyph *glyphs;
	unsigned glyph_count, capacity, row_count;
	uint64_t generation;
	struct xvt_cockpit_page_row rows[XVT_HUD_ROWS_PER_SECTION];
};

struct page_content {
	struct page_section sections[XVT_COCKPIT_PAGE_SECTION_COUNT];
	struct xvt_snap_rect background_bounds, border_bounds;
	uint32_t background_argb, border_argb;
	int16_t origin_x, origin_y, first_visible_row, selected_row;
	uint16_t total_rows;
	uint16_t mode;
	uint64_t generation;
	int latched_this_frame;
	unsigned failed_sections;
};

static struct page_content g_working[MFD_PAGE_COUNT], g_pending[MFD_PAGE_COUNT];
static struct page_section g_building;
static unsigned g_page, g_section;
static int g_capturing, g_capture_failed;
static int g_pending_failed;
static uint32_t g_palette[256];
static uint64_t g_generation;

static uint32_t capture_color(unsigned index)
{
	uint32_t color = xvt_render_draw_color(index);
	return color == xvt_render_draw_color(g_flight_transparent_color_index)
		       ? 0
		       : color;
}

static struct xvt_snap_rect capture_local_bounds(unsigned page)
{
	return (struct xvt_snap_rect){
		g_flight_clip_left - g_working[page].origin_x,
		g_flight_clip_top - g_working[page].origin_y,
		g_flight_clip_right - g_flight_clip_left,
		g_flight_clip_bottom - g_flight_clip_top};
}

static int reserve_glyphs(struct page_section *section, unsigned count)
{
	if (count <= section->capacity) {
		return 1;
	}
	if (count > XVT_HUD_PAGE_GLYPH_CAPACITY) {
		return 0;
	}
	unsigned capacity = section->capacity ? section->capacity : 128;
	while (capacity < count) {
		capacity *= 2;
	}
	struct xvt_cockpit_glyph *glyphs =
		realloc(section->glyphs, capacity * sizeof *glyphs);
	if (!glyphs) {
		return 0;
	}
	section->glyphs = glyphs;
	section->capacity = capacity;
	return 1;
}

void xvt_cockpit_pages_reset(void)
{
	for (unsigned page = 0; page < MFD_PAGE_COUNT; ++page) {
		for (unsigned section = 0;
		     section < XVT_COCKPIT_PAGE_SECTION_COUNT; ++section) {
			free(g_working[page].sections[section].glyphs);
			free(g_pending[page].sections[section].glyphs);
		}
	}
	free(g_building.glyphs);
	memset(g_working, 0, sizeof g_working);
	memset(g_pending, 0, sizeof g_pending);
	memset(&g_building, 0, sizeof g_building);
	g_capturing = g_capture_failed = 0;
	g_pending_failed = 0;
}

void xvt_cockpit_pages_reset_working(void)
{
	for (unsigned page = 0; page < MFD_PAGE_COUNT; ++page) {
		for (unsigned section = 0;
		     section < XVT_COCKPIT_PAGE_SECTION_COUNT; ++section) {
			free(g_working[page].sections[section].glyphs);
		}
	}
	memset(g_working, 0, sizeof g_working);
	g_capturing = g_capture_failed = 0;
}

void xvt_cockpit_pages_begin_frame(void)
{
	g_pending_failed = 0;
	for (unsigned page = 0; page < MFD_PAGE_COUNT; ++page) {
		g_pending[page].latched_this_frame = 0;
	}
}

void xvt_cockpit_pages_set_origin(unsigned page, int x, int y)
{
	if (page >= MFD_PAGE_COUNT) {
		return;
	}
	g_working[page].origin_x = (int16_t)x;
	g_working[page].origin_y = (int16_t)y;
}

void xvt_cockpit_pages_clear(unsigned page)
{
	if (page >= MFD_PAGE_COUNT) {
		return;
	}
	struct page_content *content = &g_working[page];
	for (unsigned section = 0; section < XVT_COCKPIT_PAGE_SECTION_COUNT;
	     ++section) {
		content->sections[section].glyph_count =
			content->sections[section].row_count = 0;
		content->sections[section].generation = ++g_generation;
	}
	content->background_argb = content->border_argb = 0;
	content->failed_sections = 0;
	content->generation = ++g_generation;
}

void xvt_cockpit_pages_begin_section(unsigned page,
				     xvt_cockpit_page_section section)
{
	if (page >= MFD_PAGE_COUNT ||
	    section >= XVT_COCKPIT_PAGE_SECTION_COUNT) {
		return;
	}
	if (g_capturing) {
		XVT_LOG_ERROR("snapshot.section_nested page=%u section=%u",
			      page, (unsigned)section);
		g_capture_failed = 1;
		return;
	}
	g_page = page;
	g_section = section;
	g_building.glyph_count = g_building.row_count = 0;
	g_capture_failed = 0;
	g_capturing = 1;
	for (unsigned color = 0; color < 256; ++color) {
		g_palette[color] = xvt_render_draw_color(color);
	}
}

static void finish_row(void)
{
	if (g_building.row_count) {
		struct xvt_cockpit_page_row *row =
			&g_building.rows[g_building.row_count - 1];
		row->glyph_count =
			(uint16_t)(g_building.glyph_count - row->first_glyph);
	}
}

void xvt_cockpit_pages_end_section(void)
{
	if (!g_capturing) {
		return;
	}
	finish_row();
	if (g_capture_failed) {
		g_working[g_page].failed_sections |= 1u << g_section;
	} else {
		g_working[g_page].failed_sections &= ~(1u << g_section);
	}
	struct page_section *previous = &g_working[g_page].sections[g_section];
	if (!g_capture_failed &&
	    (previous->glyph_count != g_building.glyph_count ||
	     previous->row_count != g_building.row_count ||
	     (previous->glyph_count &&
	      memcmp(previous->glyphs, g_building.glyphs,
		     previous->glyph_count * sizeof previous->glyphs[0])) ||
	     (previous->row_count &&
	      memcmp(previous->rows, g_building.rows,
		     previous->row_count * sizeof previous->rows[0])))) {
		/* Publish a complete section; the scratch buffer takes the prior allocation. */
		struct xvt_cockpit_glyph *old_glyphs = previous->glyphs;
		unsigned old_capacity = previous->capacity;
		previous->glyphs = g_building.glyphs;
		previous->capacity = g_building.capacity;
		previous->glyph_count = g_building.glyph_count;
		previous->row_count = g_building.row_count;
		memcpy(previous->rows, g_building.rows,
		       g_building.row_count * sizeof previous->rows[0]);
		previous->generation = g_working[g_page].generation =
			++g_generation;
		g_building.glyphs = old_glyphs;
		g_building.capacity = old_capacity;
	}
	g_capturing = 0;
}

void xvt_cockpit_pages_record_glyph(unsigned character, unsigned advance,
				    unsigned height, int narrow)
{
	if (!g_capturing || g_capture_failed) {
		return;
	}
	struct xvt_cockpit_glyph glyph;
	if (!xvt_cockpit_text_capture_glyph(&glyph, character, advance, height,
					    narrow, g_working[g_page].origin_x,
					    g_working[g_page].origin_y,
					    g_palette, 1)) {
		return;
	}
	if (!reserve_glyphs(&g_building, g_building.glyph_count + 1)) {
		XVT_LOG_ERROR("snapshot.section_overflow page=%u section=%u",
			      g_page, g_section);
		g_capture_failed = 1;
		return;
	}
	g_building.glyphs[g_building.glyph_count++] = glyph;
}

void xvt_cockpit_pages_record_row(uint32_t key, int selected)
{
	if (!g_capturing || g_capture_failed) {
		return;
	}
	finish_row();
	if (g_building.row_count == XVT_HUD_ROWS_PER_SECTION) {
		XVT_LOG_ERROR("snapshot.page_rows_overflow page=%u", g_page);
		g_capture_failed = 1;
		return;
	}
	struct xvt_cockpit_page_row *row =
		&g_building.rows[g_building.row_count++];
	memset(row, 0, sizeof *row);
	row->key = key;
	row->selected = selected != 0;
	row->first_glyph = (uint16_t)g_building.glyph_count;
	row->bounds = capture_local_bounds(g_page);
	row->background_argb = capture_color(g_flight_text_bg_color);
}

void xvt_cockpit_pages_record_background(unsigned page)
{
	if (page >= MFD_PAGE_COUNT) {
		return;
	}
	struct xvt_snap_rect bounds = capture_local_bounds(page);
	uint32_t color = capture_color(g_flight_text_bg_color);
	struct page_content *content = &g_working[page];
	if (memcmp(&content->background_bounds, &bounds, sizeof bounds) ||
	    content->background_argb != color) {
		content->background_bounds = bounds;
		content->background_argb = color;
		content->generation = ++g_generation;
	}
}

void xvt_cockpit_pages_record_border(unsigned page)
{
	if (page >= MFD_PAGE_COUNT) {
		return;
	}
	struct xvt_snap_rect bounds = capture_local_bounds(page);
	uint32_t color = capture_color(g_flight_text_bg_color);
	struct page_content *content = &g_working[page];
	if (memcmp(&content->border_bounds, &bounds, sizeof bounds) ||
	    content->border_argb != color) {
		content->border_bounds = bounds;
		content->border_argb = color;
		content->generation = ++g_generation;
	}
}

void xvt_cockpit_pages_record_scroll(unsigned page, int first_visible_row,
				     int total_rows, int selected_row)
{
	if (page >= MFD_PAGE_COUNT) {
		return;
	}
	g_working[page].first_visible_row = (int16_t)first_visible_row;
	g_working[page].total_rows = (uint16_t)total_rows;
	g_working[page].selected_row = (int16_t)selected_row;
}

void xvt_cockpit_pages_record_mode(unsigned page, unsigned mode)
{
	if (page < MFD_PAGE_COUNT) {
		g_working[page].mode = (uint16_t)mode;
	}
}

void xvt_cockpit_pages_latch(unsigned page)
{
	if (page >= MFD_PAGE_COUNT) {
		return;
	}
	struct page_content *destination = &g_pending[page];
	const struct page_content *source = &g_working[page];
	if (source->failed_sections) {
		g_pending_failed = 1;
		return;
	}
	int changed =
		!destination->generation ||
		memcmp(&destination->background_bounds,
		       &source->background_bounds,
		       sizeof source->background_bounds) ||
		memcmp(&destination->border_bounds, &source->border_bounds,
		       sizeof source->border_bounds) ||
		destination->background_argb != source->background_argb ||
		destination->border_argb != source->border_argb ||
		destination->first_visible_row != source->first_visible_row ||
		destination->total_rows != source->total_rows ||
		destination->selected_row != source->selected_row ||
		destination->mode != source->mode;
	for (unsigned index = 0; index < XVT_COCKPIT_PAGE_SECTION_COUNT;
	     ++index) {
		struct page_section *to = &destination->sections[index];
		const struct page_section *from = &source->sections[index];
		if (to->generation == from->generation) {
			continue;
		}
		if (to->glyph_count == from->glyph_count &&
		    to->row_count == from->row_count &&
		    (!from->glyph_count ||
		     !memcmp(to->glyphs, from->glyphs,
			     from->glyph_count * sizeof from->glyphs[0])) &&
		    (!from->row_count ||
		     !memcmp(to->rows, from->rows,
			     from->row_count * sizeof from->rows[0]))) {
			to->generation = from->generation;
			continue;
		}
		if (!reserve_glyphs(to, from->glyph_count)) {
			XVT_LOG_ERROR(
				"snapshot.section_retain_failed page=%u section=%u",
				page, index);
			g_pending_failed = 1;
			return;
		}
		if (from->glyph_count) {
			memcpy(to->glyphs, from->glyphs,
			       from->glyph_count * sizeof from->glyphs[0]);
		}
		memcpy(to->rows, from->rows,
		       from->row_count * sizeof from->rows[0]);
		to->glyph_count = from->glyph_count;
		to->row_count = from->row_count;
		to->generation = from->generation;
		changed = 1;
	}
	destination->background_bounds = source->background_bounds;
	destination->border_bounds = source->border_bounds;
	destination->background_argb = source->background_argb;
	destination->border_argb = source->border_argb;
	destination->first_visible_row = source->first_visible_row;
	destination->total_rows = source->total_rows;
	destination->selected_row = source->selected_row;
	destination->mode = source->mode;
	if (changed) {
		destination->generation = ++g_generation;
	}
	destination->latched_this_frame = 1;
}

void xvt_cockpit_pages_export(struct xvt_cockpit_state *state)
{
	if (g_pending_failed) {
		state->valid = 0;
		return;
	}
	struct xvt_cockpit_page_store *store = &state->page_content;
	store->row_count = store->glyph_count = 0;
	for (unsigned id = 0; id < MFD_PAGE_COUNT; ++id) {
		struct xvt_cockpit_page *page = &state->pages[id];
		const struct page_content *content = &g_pending[id];
		page->glyph_count = page->row_count = page->header_glyph_count =
			0;
		page->visible &= content->latched_this_frame != 0;
		if (!page->visible) {
			continue;
		}
		page->content_generation = content->generation;
		page->background_bounds = content->background_bounds;
		page->border_bounds = content->border_bounds;
		page->background_argb = content->background_argb;
		page->border_argb = content->border_argb;
		page->first_visible_row = content->first_visible_row;
		page->total_rows = content->total_rows;
		page->selected_row = content->selected_row;
		page->command_text_mode = content->mode;
		page->first_glyph = store->glyph_count;
		page->first_store_row = store->row_count;
		for (unsigned index = 0; index < XVT_COCKPIT_PAGE_SECTION_COUNT;
		     ++index) {
			const struct page_section *section =
				&content->sections[index];
			if (section->glyph_count > XVT_HUD_PAGE_GLYPH_CAPACITY -
							   store->glyph_count ||
			    section->row_count > XVT_HUD_PAGE_ROW_CAPACITY -
							 store->row_count) {
				XVT_LOG_ERROR(
					"snapshot.page_text_overflow page=%u",
					id);
				state->valid = 0;
				return;
			}
			if (section->glyph_count) {
				memcpy(&store->glyphs[store->glyph_count],
				       section->glyphs,
				       section->glyph_count *
					       sizeof section->glyphs[0]);
			}
			for (unsigned row = 0; row < section->row_count;
			     ++row) {
				struct xvt_cockpit_page_row *output =
					&store->rows[store->row_count++];
				*output = section->rows[row];
				output->first_glyph += store->glyph_count;
			}
			store->glyph_count += (uint16_t)section->glyph_count;
			if (index == XVT_COCKPIT_PAGE_HEADER) {
				page->header_glyph_count =
					(uint16_t)section->glyph_count;
			}
		}
		page->glyph_count = store->glyph_count - page->first_glyph;
		page->row_count = store->row_count - page->first_store_row;
	}
}
