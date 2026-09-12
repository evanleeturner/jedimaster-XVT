#include "xvt_remaster/hud_panes.h"
#include "xvt_remaster/hud_text.h"

static int DrawNumberInPhase(const XvtHudDraw* draw, const XvtCockpitNumber* number, unsigned phase,
							 int score) {
	return number->phase != phase || XvtHudText_DrawNumber(draw, number, score);
}

int XvtHudPanes_DrawReadouts(const XvtHudDraw* draw, unsigned phase) {
	const XvtCockpitState* state = draw->state;
	for (unsigned field = 0; field < XVT_COCKPIT_TEXT_CLOCK_SEPARATOR; ++field) {
		if (phase == XVT_COCKPIT_AFTER_CRT &&
			(field == XVT_COCKPIT_TEXT_NETWORK_PING || field == XVT_COCKPIT_TEXT_NETWORK_LAG))
			continue;
		if (state->text_fields[field].caption.phase == phase &&
			!XvtHudText_DrawField(draw, &state->text_fields[field]))
			return 0;
	}
	const XvtCockpitNumber* numbers[] = { &state->readouts.speed,
										  &state->readouts.throttle,
										  &state->readouts.clock_minutes,
										  &state->readouts.clock_seconds,
										  &state->systems.countermeasure_count,
										  &state->target.hull,
										  &state->target.shields,
										  &state->target.systems,
										  &state->target.distance,
										  &state->target.distance_fraction,
										  &state->target.order_distance,
										  &state->target.order_distance_fraction,
										  &state->target.order_minutes,
										  &state->target.order_seconds,
										  &state->proving_grounds.level,
										  &state->proving_grounds.remaining,
										  &state->proving_grounds.passed,
										  &state->proving_grounds.targets };
	for (unsigned index = 0; index < sizeof numbers / sizeof *numbers; ++index)
		if (!DrawNumberInPhase(draw, numbers[index], phase, 0))
			return 0;
	if (!DrawNumberInPhase(draw, &state->proving_grounds.score, phase, 1))
		return 0;
	for (unsigned slot = 0; slot < state->weapons.slot_count; ++slot)
		if (!DrawNumberInPhase(draw, &state->weapons.slots[slot].charge_percent, phase, 0))
			return 0;
	if (phase == XVT_COCKPIT_BEFORE_CRT)
		for (unsigned launcher = 0; launcher < 4; ++launcher)
			if (!DrawNumberInPhase(draw, &state->weapons.launchers[launcher].count, phase, 0))
				return 0;
	/* Numeric fields clear their bounds, which can overlap an adjacent symbol.
	 * Submit the original suffix/separator fields after those clears. */
	for (unsigned field = XVT_COCKPIT_TEXT_CLOCK_SEPARATOR; field < XVT_COCKPIT_TEXT_FIELD_COUNT; ++field)
		if (state->text_fields[field].caption.phase == phase &&
			!XvtHudText_DrawField(draw, &state->text_fields[field]))
			return 0;
	return 1;
}

static XvtSnapRect PlaceBounds(XvtSnapRect bounds, XvtSnapRect placement) {
	bounds.x += placement.x;
	bounds.y += placement.y;
	int right = bounds.x + bounds.width, bottom = bounds.y + bounds.height;
	if (right > placement.x + placement.width)
		right = placement.x + placement.width;
	if (bottom > placement.y + placement.height)
		bottom = placement.y + placement.height;
	if (bounds.x < placement.x)
		bounds.x = placement.x;
	if (bounds.y < placement.y)
		bounds.y = placement.y;
	bounds.width = right - bounds.x;
	bounds.height = bottom - bounds.y;
	return bounds;
}

static int DrawPage(const XvtHudDraw* draw, unsigned page_id) {
	const XvtCockpitPage* page = &draw->state->pages[page_id];
	const XvtCockpitPageStore* content = &draw->state->page_content;
	if (!page->visible)
		return 1;
	if ((unsigned)page->first_row + page->row_count > content->row_count ||
		(unsigned)page->first_glyph + page->glyph_count > content->glyph_count)
		return 0;
	XvtHudDraw_Fill(draw, XVT_COCKPIT_AFTER_CRT, PlaceBounds(page->background_bounds, page->placement),
					page->background_argb);
	XvtSnapRect border = page->border_bounds;
	border.x += page->placement.x;
	border.y += page->placement.y;
	XvtHudDraw_FrameClipped(draw, XVT_COCKPIT_AFTER_CRT, border, page->placement, page->border_argb);
	for (unsigned index = 0; index < page->row_count; ++index) {
		const XvtCockpitPageRow* row = &content->rows[page->first_row + index];
		XvtHudDraw_Fill(draw, XVT_COCKPIT_AFTER_CRT, PlaceBounds(row->bounds, page->placement),
						row->background_argb);
	}
	for (unsigned index = 0; index < page->glyph_count; ++index)
		if (!XvtHudDraw_Glyph(draw, &content->glyphs[page->first_glyph + index], page->placement.x,
							  page->placement.y, page->placement, XVT_COCKPIT_AFTER_CRT))
			return 0;
	return 1;
}

int XvtHudPanes_DrawPages(const XvtHudDraw* draw) {
	for (unsigned page = 0; page < MFD_PAGE_COUNT; ++page)
		if (page != MFD_PAGE_MESSAGE_LOG && !DrawPage(draw, page))
			return 0;
	/* Original map launcher blits follow the MFD enumeration. */
	for (unsigned launcher = 0; launcher < 4; ++launcher)
		if (!DrawNumberInPhase(draw, &draw->state->weapons.launchers[launcher].count, XVT_COCKPIT_AFTER_CRT,
							   0))
			return 0;
	return 1;
}

static int DrawOverlayGlyphs(const XvtHudDraw* draw, unsigned first, unsigned count, XvtSnapRect placement,
							 unsigned phase) {
	if (first + count > draw->state->overlay_content.glyph_count)
		return 0;
	for (unsigned index = 0; index < count; ++index)
		if (!XvtHudDraw_Glyph(draw, &draw->state->overlay_content.glyphs[first + index], placement.x,
							  placement.y, placement, phase))
			return 0;
	return 1;
}

int XvtHudPanes_DrawMessages(const XvtHudDraw* draw) {
	if (!DrawPage(draw, MFD_PAGE_MESSAGE_LOG))
		return 0;
	for (unsigned pane = 0; pane < XVT_COCKPIT_MESSAGE_COUNT; ++pane) {
		const XvtCockpitMessage* message = &draw->state->messages.panes[pane];
		if (message->visible && !DrawOverlayGlyphs(draw, message->first_glyph, message->glyph_count,
												   message->placement, XVT_COCKPIT_AFTER_CRT))
			return 0;
		if (pane == XVT_COCKPIT_MESSAGE_READY) {
			for (unsigned field = XVT_COCKPIT_TEXT_NETWORK_PING; field <= XVT_COCKPIT_TEXT_NETWORK_LAG;
				 ++field)
				if (draw->state->text_fields[field].caption.phase == XVT_COCKPIT_AFTER_CRT &&
					!XvtHudText_DrawField(draw, &draw->state->text_fields[field]))
					return 0;
		}
	}
	return 1;
}

int XvtHudPanes_DrawOverlays(const XvtHudDraw* draw) {
	const XvtCockpitLoading* loading = &draw->state->loading;
	if (loading->text_visible && !DrawOverlayGlyphs(draw, loading->first_glyph, loading->glyph_count,
													loading->text_bounds, XVT_COCKPIT_ALERT))
		return 0;
	if (loading->visible) {
		XvtSnapRect bounds = loading->placement;
		XvtHudDraw_Fill(draw, XVT_COCKPIT_ALERT,
						(XvtSnapRect) { bounds.x - 2, bounds.y - 2, bounds.width + 4, bounds.height + 4 },
						loading->foreground_argb);
		XvtHudDraw_Fill(draw, XVT_COCKPIT_ALERT,
						(XvtSnapRect) { bounds.x - 1, bounds.y - 1, bounds.width + 2, bounds.height + 2 },
						loading->background_argb);
		bounds.width = loading->filled_width < bounds.width ? loading->filled_width : bounds.width;
		XvtHudDraw_Fill(draw, XVT_COCKPIT_ALERT, bounds, loading->foreground_argb);
	}
	const XvtCockpitAlert* alert = &draw->state->alert;
	if (alert->active) {
		XvtSnapRect bounds = alert->placement;
		XvtHudDraw_Frame(draw, XVT_COCKPIT_ALERT,
						 (XvtSnapRect) { bounds.x - 1, bounds.y - 1, bounds.width + 2, bounds.height + 2 },
						 alert->border_argb);
		for (unsigned row = 0; row < 5; ++row)
			XvtHudDraw_Fill(draw, XVT_COCKPIT_ALERT,
							(XvtSnapRect) { bounds.x, bounds.y + (int)row * bounds.height / 5, bounds.width,
											bounds.height / 5 },
							alert->row_background_argb[row]);
		for (unsigned line = 0; line < 3; ++line)
			if (alert->line_visible[line] &&
				!DrawOverlayGlyphs(draw, alert->first_glyph[line], alert->glyph_count[line], bounds,
								   XVT_COCKPIT_ALERT))
				return 0;
	}
	return 1;
}
