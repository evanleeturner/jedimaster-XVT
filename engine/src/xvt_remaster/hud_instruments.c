#include "xvt_remaster/hud_instruments.h"

#include <math.h>

#include "xvt_remaster/ui_draw.h"

static void draw_indicator(const struct xvt_hud_draw *draw,
			   xvt_hud_sprite_role role,
			   const struct xvt_cockpit_indicator *indicator)
{
	if (indicator->visible) {
		xvt_hud_draw_part(draw, role, indicator->state, 0, 0,
				  indicator->phase);
	}
}

void xvt_hud_instruments_draw_covers(const struct xvt_hud_draw *draw)
{
	const struct xvt_cockpit_systems *systems = &draw->state->systems;
	for (unsigned index = 0; index < 13; ++index) {
		if (systems->feature_covers[index].visible) {
			xvt_hud_draw_part(
				draw, XVT_HUD_FEATURE_COVER + index,
				systems->feature_covers[index].state == 13, 0,
				0, XVT_COCKPIT_BEFORE_CRT);
		}
	}
	draw_indicator(draw, XVT_HUD_UNAVAILABLE_SHIELDS,
		       &systems->unavailable_shields);
	draw_indicator(draw, XVT_HUD_UNAVAILABLE_BEAM,
		       &systems->unavailable_beam[0]);
	draw_indicator(draw, XVT_HUD_UNAVAILABLE_BEAM_POWER,
		       &systems->unavailable_beam[1]);
	if (draw->state->target.panel_cover) {
		xvt_hud_draw_part(
			draw,
			draw->state->target.cover_binding %
						HUD_INSTRUMENTS_PER_SET ==
					108
				? XVT_HUD_TARGET_ALT_COVER
				: XVT_HUD_TARGET_COVER,
			0, 0, 0, XVT_COCKPIT_BEFORE_CRT);
	}
}

static void draw_power_gauge(const struct xvt_hud_draw *draw,
			     xvt_hud_sprite_role role,
			     const struct xvt_cockpit_power_gauge *gauge)
{
	if (!gauge->visible) {
		return;
	}
	for (unsigned segment = 0; segment < gauge->segments; ++segment) {
		xvt_hud_draw_part(draw, role, segment < gauge->filled, 0,
				  -(int)segment * gauge->step_y,
				  XVT_COCKPIT_BEFORE_CRT);
	}
}

static void draw_weapons(const struct xvt_hud_draw *draw)
{
	const struct xvt_cockpit_weapons *weapons = &draw->state->weapons;
	for (unsigned index = 0; index < weapons->slot_count; ++index) {
		const struct xvt_cockpit_weapon_slot *slot =
			&weapons->slots[index];
		if (!slot->visible) {
			continue;
		}
		if (slot->charge_visible &&
		    draw->state->view.instrument_base ==
			    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
			for (unsigned segment = 0; segment < 10; ++segment) {
				xvt_hud_draw_part(
					draw, XVT_HUD_LASER_CHARGE + index,
					segment < slot->segments
						? slot->charge_band
						: slot->empty_band,
					(int)segment *
						draw->layout
							->sprites
								[XVT_HUD_LASER_CHARGE +
								 index]
							.step_x,
					0, XVT_COCKPIT_BEFORE_CRT);
			}
		}
		if (slot->selection_visible) {
			xvt_hud_draw_part(draw, XVT_HUD_LASER_SELECTION + index,
					  slot->selection_state, 0, 0,
					  XVT_COCKPIT_BEFORE_CRT);
		}
		xvt_hud_draw_part(draw, XVT_HUD_LASER_READY + index,
				  slot->ready_state, 0, 0,
				  XVT_COCKPIT_BEFORE_CRT);
		if (slot->lock_visible) {
			xvt_hud_draw_part(draw, XVT_HUD_LASER_LOCK + index,
					  slot->locked, 0, 0,
					  XVT_COCKPIT_BEFORE_CRT);
		}
	}
	draw_indicator(draw, XVT_HUD_TARGET_LOCK, &weapons->lock_indicator);
	if (draw->state->view.instrument_base ==
	    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
		for (unsigned launcher = 0; launcher < 4; ++launcher) {
			if (weapons->launchers[launcher].visible) {
				xvt_hud_draw_part(
					draw, XVT_HUD_LAUNCHER + launcher,
					weapons->launchers[launcher].selection,
					0, 0, XVT_COCKPIT_BEFORE_CRT);
			}
		}
	}
}

void xvt_hud_instruments_draw_mouse_stick(const struct xvt_hud_draw *draw,
					  const struct xvt_render_view *view)
{
	if (!view || !draw->state->mouse_stick_visible) {
		return;
	}
	const AeronSceneCamera *camera = &view->camera;
	float range = draw->state->view.viewport.height * draw->scale / 6.0f;
	float x = camera->viewport.x +
		  (1 + camera->proj_x_offset) * camera->viewport.width * .5f +
		  draw->state->mouse_stick_x * range / 127.0f;
	float y = camera->viewport.y +
		  (1 - camera->proj_y_offset) * camera->viewport.height * .5f -
		  draw->state->mouse_stick_y * range / 127.0f;
	float size = 4 * draw->scale;
	float color[4];
	xvt_ui_color(draw->state->palette_argb[63], color);
	AeronRectI clip = {0, 0, draw->width, draw->height};
	AeronDrawList_AddFrame(draw->after, x - size / 2, y - size / 2, size,
			       size, draw->scale, color, AERON_BLIT2D_BLEND_PMA,
			       &clip);
}

void xvt_hud_instruments_draw_widgets(const struct xvt_hud_draw *draw)
{
	const struct xvt_cockpit_systems *systems = &draw->state->systems;
	draw_weapons(draw);
	for (unsigned side = 0; side < 2; ++side) {
		const struct xvt_cockpit_shield *shield =
			&systems->shields[side];
		if (shield->visible && !shield->text_mode) {
			xvt_hud_draw_part(draw, XVT_HUD_SHIELD + side * 2,
					  shield->primary_level, 0, 0,
					  XVT_COCKPIT_BEFORE_CRT);
			xvt_hud_draw_part(draw, XVT_HUD_SHIELD + side * 2 + 1,
					  shield->overcharge_level, 0, 0,
					  XVT_COCKPIT_BEFORE_CRT);
		}
	}
	draw_indicator(draw, XVT_HUD_HULL, &systems->hull_indicator);
	draw_indicator(draw, XVT_HUD_BEAM_ENABLED, &systems->beam_enabled);
	if (systems->beam_visible) {
		for (unsigned segment = 0; segment < 9; ++segment) {
			xvt_hud_draw_part(
				draw, XVT_HUD_BEAM,
				segment * 4 + systems->beam_segments[segment],
				draw->layout->beam_offsets[segment][0],
				draw->layout->beam_offsets[segment][1],
				XVT_COCKPIT_BEFORE_CRT);
		}
	}
	draw_power_gauge(draw, XVT_HUD_ENGINE_POWER, &systems->engine_power);
	draw_power_gauge(draw, XVT_HUD_LASER_POWER, &systems->laser_power);
	draw_power_gauge(draw, XVT_HUD_SHIELD_POWER, &systems->shield_power);
	draw_power_gauge(draw, XVT_HUD_BEAM_POWER, &systems->beam_power);
	draw_indicator(draw, XVT_HUD_SHIELD_DISTRIBUTION,
		       &systems->shield_distribution);
	draw_indicator(draw, XVT_HUD_SFOILS, &systems->sfoils);
	draw_indicator(draw, XVT_HUD_COUNTERMEASURE_SELECTION,
		       &systems->countermeasure_active);
	draw_indicator(draw, XVT_HUD_CRITICAL_WARNING,
		       &systems->critical_warning);
	for (unsigned index = 0; index < 4; ++index) {
		draw_indicator(draw, XVT_HUD_THREAT + index,
			       &systems->threats[index]);
		draw_indicator(draw, XVT_HUD_CMD_ARMAMENT + index,
			       &draw->state->target.armament[index]);
	}
}

void xvt_hud_instruments_draw_radar(const struct xvt_hud_draw *draw)
{
	const struct xvt_cockpit_radar *radar = &draw->state->radar;
	for (unsigned side = 0; side < 2; ++side) {
		if (!radar->visible[side]) {
			continue;
		}
		for (unsigned index = 0; index < radar->count[side]; ++index) {
			const struct xvt_snap_radar_blip *blip =
				&radar->blips[side][index];
			for (unsigned row = 0; row < 2; ++row) {
				if (radar->coverage[side][index] &
				    (1u << row)) {
					xvt_hud_draw_fill(
						draw, XVT_COCKPIT_BEFORE_CRT,
						(struct xvt_snap_rect){
							draw->layout->radar[side]
									.x +
								blip->x,
							draw->layout->radar[side]
									.y +
								blip->y +
								(int)row,
							1, 1},
						draw->state->palette_argb
							[blip->color_index &
							 255]);
				}
			}
		}
	}
	if (radar->marker_visible && radar->marker_side < 2) {
		static const int offsets[10][2] = {
			{-1, 1}, {-2, 1}, {-2, 0}, {-2, -1}, {-1, -1},
			{1, -1}, {2, -1}, {2, 0},  {2, 1},   {1, 1}};
		for (unsigned index = 0; index < 10; ++index) {
			xvt_hud_draw_fill(
				draw, XVT_COCKPIT_BEFORE_CRT,
				(struct xvt_snap_rect){
					draw->layout->radar[radar->marker_side]
							.x +
						radar->marker_x +
						offsets[index][0],
					draw->layout->radar[radar->marker_side]
							.y +
						radar->marker_y +
						offsets[index][1],
					1, 1},
				draw->state->palette_argb[206]);
		}
	}
}

void xvt_hud_instruments_draw_world_markers(
	const struct xvt_hud_draw *draw,
	const struct xvt_snap_target_box *markers, unsigned count,
	const struct xvt_render_view *view)
{
	if (!view) {
		return;
	}
	for (unsigned index = 0; index < count; ++index) {
		const struct xvt_snap_target_box *marker = &markers[index];
		float x;
		float y;
		float depth;
		if (marker->scope != XVT_SCOPE_COCKPIT ||
		    !xvt_render_math_project_world(view, marker->world_pos, &x,
						   &y, &depth) ||
		    depth <= 0) {
			continue;
		}
		float focal = view->camera.viewport.width /
			      (2 * tanf(view->camera.h_half_rad));
		float size =
			fminf(draw->layout->source_width * .75f * draw->scale,
			      fmaxf((draw->layout->source_width == 320 ? 4
								       : 8) *
					    draw->scale,
				    marker->extent * focal / depth)) +
			4 * draw->scale;
		float corner = fmaxf(3 * draw->scale, size / 8);
		float color[4];
		xvt_ui_color(
			draw->state->palette_argb[marker->color_index & 255],
			color);
		AeronRectI clip = {0, 0, draw->width, draw->height};
		for (unsigned side = 0; side < 2; ++side) {
			for (unsigned end = 0; end < 2; ++end) {
				float xx = x - size / 2 + side * size;
				float yy = y - size / 2 + end * size;
				AeronDrawList_AddLine(
					draw->before, xx, yy,
					xx + (side ? -corner : corner), yy,
					draw->scale, color,
					AERON_BLIT2D_BLEND_PMA, &clip);
				AeronDrawList_AddLine(
					draw->before, xx, yy, xx,
					yy + (end ? -corner : corner),
					draw->scale, color,
					AERON_BLIT2D_BLEND_PMA, &clip);
			}
		}
	}
}

void xvt_hud_instruments_draw_crt_marker(const struct xvt_hud_draw *draw)
{
	const struct xvt_snap_preview *crt = &draw->state->crt;
	struct xvt_render_view view;
	float x;
	float y;
	float depth;
	if (!crt->valid || !crt->component_marker_valid ||
	    !xvt_render_math_build_view(&crt->camera, crt->camera.world_pos,
					crt->destination.width,
					crt->destination.height, &view) ||
	    !xvt_render_math_project_world(&view, crt->component_marker_world,
					   &x, &y, &depth) ||
	    depth <= 0) {
		return;
	}
	struct xvt_snap_rect rect = {crt->destination.x + (int)floorf(x) - 2,
				     crt->destination.y + (int)floorf(y) - 2, 4,
				     4};
	xvt_hud_draw_outline_clipped(draw, XVT_COCKPIT_AFTER_CRT, rect,
				     crt->destination,
				     draw->state->palette_argb[206]);
}
