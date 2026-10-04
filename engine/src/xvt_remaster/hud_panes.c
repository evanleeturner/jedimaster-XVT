#include "xvt_remaster/hud_panes.h"

#include "xvt_remaster/hud_text.h"

static int draw_number_in_phase(const struct xvt_hud_draw *draw,
				const struct xvt_cockpit_number *number,
				unsigned phase, int signed_value)
{
	return number->phase != phase ||
	       xvt_hud_text_draw_number(draw, number, signed_value);
}

int xvt_hud_panes_draw_readouts(const struct xvt_hud_draw *draw, unsigned phase)
{
	const struct xvt_cockpit_state *state = draw->state;
	for (unsigned field = 0; field < XVT_COCKPIT_TEXT_CLOCK_SEPARATOR;
	     ++field) {
		if (phase == XVT_COCKPIT_AFTER_CRT &&
		    (field == XVT_COCKPIT_TEXT_NETWORK_PING ||
		     field == XVT_COCKPIT_TEXT_NETWORK_LAG)) {
			continue;
		}
		if (state->text_fields[field].caption.phase == phase &&
		    !xvt_hud_text_draw_field(draw,
					     &state->text_fields[field])) {
			return 0;
		}
	}
	const struct xvt_cockpit_number *numbers[] = {
		&state->readouts.speed,
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
		&state->proving_grounds.targets};
	for (unsigned index = 0; index < sizeof numbers / sizeof *numbers;
	     ++index) {
		if (!draw_number_in_phase(draw, numbers[index], phase, 0)) {
			return 0;
		}
	}
	if (!draw_number_in_phase(draw, &state->proving_grounds.score, phase,
				  1)) {
		return 0;
	}
	for (unsigned slot = 0; slot < state->weapons.slot_count; ++slot) {
		if (!draw_number_in_phase(
			    draw, &state->weapons.slots[slot].charge_percent,
			    phase, 0)) {
			return 0;
		}
	}
	if (phase == XVT_COCKPIT_BEFORE_CRT) {
		for (unsigned launcher = 0; launcher < 4; ++launcher) {
			if (!draw_number_in_phase(
				    draw,
				    &state->weapons.launchers[launcher].count,
				    phase, 0)) {
				return 0;
			}
		}
	}
	/* Numeric fields clear their bounds, which can overlap an adjacent symbol.
	 * Submit the original suffix/separator fields after those clears. */
	for (unsigned field = XVT_COCKPIT_TEXT_CLOCK_SEPARATOR;
	     field < XVT_COCKPIT_TEXT_FIELD_COUNT; ++field) {
		if (state->text_fields[field].caption.phase == phase &&
		    !xvt_hud_text_draw_field(draw,
					     &state->text_fields[field])) {
			return 0;
		}
	}
	return 1;
}

static struct xvt_snap_rect place_bounds(struct xvt_snap_rect bounds,
					 struct xvt_snap_rect placement)
{
	bounds.x += placement.x;
	bounds.y += placement.y;
	int right = bounds.x + bounds.width, bottom = bounds.y + bounds.height;
	if (right > placement.x + placement.width) {
		right = placement.x + placement.width;
	}
	if (bottom > placement.y + placement.height) {
		bottom = placement.y + placement.height;
	}
	if (bounds.x < placement.x) {
		bounds.x = placement.x;
	}
	if (bounds.y < placement.y) {
		bounds.y = placement.y;
	}
	bounds.width = right - bounds.x;
	bounds.height = bottom - bounds.y;
	return bounds;
}

static int draw_page(const struct xvt_hud_draw *draw, unsigned page_id)
{
	const struct xvt_cockpit_page *page = &draw->state->pages[page_id];
	const struct xvt_cockpit_page_store *content =
		&draw->state->page_content;
	if (!page->visible) {
		return 1;
	}
	if ((unsigned)page->first_store_row + page->row_count >
		    content->row_count ||
	    (unsigned)page->first_glyph + page->glyph_count >
		    content->glyph_count) {
		return 0;
	}
	xvt_hud_draw_fill(
		draw, XVT_COCKPIT_AFTER_CRT,
		place_bounds(page->background_bounds, page->placement),
		page->background_argb);
	struct xvt_snap_rect border = page->border_bounds;
	border.x += page->placement.x;
	border.y += page->placement.y;
	xvt_hud_draw_outline_clipped(draw, XVT_COCKPIT_AFTER_CRT, border,
				     page->placement, page->border_argb);
	for (unsigned index = 0; index < page->row_count; ++index) {
		const struct xvt_cockpit_page_row *row =
			&content->rows[page->first_store_row + index];
		xvt_hud_draw_fill(draw, XVT_COCKPIT_AFTER_CRT,
				  place_bounds(row->bounds, page->placement),
				  row->background_argb);
	}
	for (unsigned index = 0; index < page->glyph_count; ++index) {
		if (!xvt_hud_draw_glyph(
			    draw, &content->glyphs[page->first_glyph + index],
			    page->placement.x, page->placement.y,
			    page->placement, XVT_COCKPIT_AFTER_CRT)) {
			return 0;
		}
	}
	return 1;
}

int xvt_hud_panes_draw_pages(const struct xvt_hud_draw *draw)
{
	for (unsigned page = 0; page < MFD_PAGE_COUNT; ++page) {
		if (page != MFD_PAGE_MESSAGE_LOG && !draw_page(draw, page)) {
			return 0;
		}
	}
	/* Original map launcher blits follow the MFD enumeration. */
	for (unsigned launcher = 0; launcher < 4; ++launcher) {
		if (!draw_number_in_phase(
			    draw,
			    &draw->state->weapons.launchers[launcher].count,
			    XVT_COCKPIT_AFTER_CRT, 0)) {
			return 0;
		}
	}
	return 1;
}

static int draw_overlay_glyphs(const struct xvt_hud_draw *draw, unsigned first,
			       unsigned count, struct xvt_snap_rect placement,
			       unsigned phase)
{
	if (first + count > draw->state->overlay_content.glyph_count) {
		return 0;
	}
	for (unsigned index = 0; index < count; ++index) {
		if (!xvt_hud_draw_glyph(
			    draw,
			    &draw->state->overlay_content.glyphs[first + index],
			    placement.x, placement.y, placement, phase)) {
			return 0;
		}
	}
	return 1;
}

int xvt_hud_panes_draw_messages(const struct xvt_hud_draw *draw)
{
	if (!draw_page(draw, MFD_PAGE_MESSAGE_LOG)) {
		return 0;
	}
	for (unsigned pane = 0; pane < XVT_COCKPIT_MESSAGE_COUNT; ++pane) {
		const struct xvt_cockpit_message *message =
			&draw->state->messages.panes[pane];
		if (message->visible &&
		    !draw_overlay_glyphs(
			    draw, message->first_glyph, message->glyph_count,
			    message->placement, XVT_COCKPIT_AFTER_CRT)) {
			return 0;
		}
		if (pane == XVT_COCKPIT_MESSAGE_READY) {
			for (unsigned field = XVT_COCKPIT_TEXT_NETWORK_PING;
			     field <= XVT_COCKPIT_TEXT_NETWORK_LAG; ++field) {
				if (draw->state->text_fields[field]
						    .caption.phase ==
					    XVT_COCKPIT_AFTER_CRT &&
				    !xvt_hud_text_draw_field(
					    draw,
					    &draw->state->text_fields[field])) {
					return 0;
				}
			}
		}
	}
	return 1;
}

int xvt_hud_panes_draw_overlays(const struct xvt_hud_draw *draw)
{
	const struct xvt_cockpit_loading *loading = &draw->state->loading;
	if (loading->text_visible &&
	    !draw_overlay_glyphs(draw, loading->first_glyph,
				 loading->glyph_count, loading->text_bounds,
				 XVT_COCKPIT_ALERT)) {
		return 0;
	}
	if (loading->progress_visible) {
		struct xvt_snap_rect bounds = loading->progress_placement;
		xvt_hud_draw_fill(draw, XVT_COCKPIT_ALERT,
				  (struct xvt_snap_rect){
					  bounds.x - 2, bounds.y - 2,
					  bounds.width + 4, bounds.height + 4},
				  loading->foreground_argb);
		xvt_hud_draw_fill(draw, XVT_COCKPIT_ALERT,
				  (struct xvt_snap_rect){
					  bounds.x - 1, bounds.y - 1,
					  bounds.width + 2, bounds.height + 2},
				  loading->background_argb);
		bounds.width = loading->filled_width < bounds.width
				       ? loading->filled_width
				       : bounds.width;
		xvt_hud_draw_fill(draw, XVT_COCKPIT_ALERT, bounds,
				  loading->foreground_argb);
	}
	const struct xvt_cockpit_alert *alert = &draw->state->alert;
	if (alert->active) {
		struct xvt_snap_rect bounds = alert->placement;
		xvt_hud_draw_outline(draw, XVT_COCKPIT_ALERT,
				     (struct xvt_snap_rect){bounds.x - 1,
							    bounds.y - 1,
							    bounds.width + 2,
							    bounds.height + 2},
				     alert->border_argb);
		for (unsigned row = 0; row < 5; ++row) {
			xvt_hud_draw_fill(
				draw, XVT_COCKPIT_ALERT,
				(struct xvt_snap_rect){
					bounds.x,
					bounds.y + (int)row * bounds.height / 5,
					bounds.width, bounds.height / 5},
				alert->row_background_argb[row]);
		}
		for (unsigned line = 0; line < 3; ++line) {
			if (alert->line_visible[line] &&
			    !draw_overlay_glyphs(draw, alert->first_glyph[line],
						 alert->glyph_count[line],
						 bounds, XVT_COCKPIT_ALERT)) {
				return 0;
			}
		}
	}
	return 1;
}
