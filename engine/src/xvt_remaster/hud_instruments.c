#include "xvt_remaster/hud_instruments.h"
#include "xvt_remaster/ui_draw.h"
#include <math.h>

static void DrawIndicator(const XvtHudDraw* draw, XvtHudSpriteRole role,
						  const XvtCockpitIndicator* indicator) {
	if (indicator->visible)
		XvtHudDraw_Part(draw, role, indicator->state, 0, 0, indicator->phase);
}

void XvtHudInstruments_DrawCovers(const XvtHudDraw* draw) {
	const XvtCockpitSystems* systems = &draw->state->systems;
	for (unsigned index = 0; index < 13; ++index)
		if (systems->feature_covers[index].visible)
			XvtHudDraw_Part(draw, XVT_HUD_FEATURE_COVER + index, systems->feature_covers[index].state == 13,
							0, 0, XVT_COCKPIT_BEFORE_CRT);
	DrawIndicator(draw, XVT_HUD_UNAVAILABLE_SHIELDS, &systems->unavailable_shields);
	DrawIndicator(draw, XVT_HUD_UNAVAILABLE_BEAM, &systems->unavailable_beam[0]);
	DrawIndicator(draw, XVT_HUD_UNAVAILABLE_BEAM_POWER, &systems->unavailable_beam[1]);
	if (draw->state->target.panel_cover)
		XvtHudDraw_Part(draw,
						draw->state->target.cover_binding % HUD_INSTRUMENTS_PER_SET == 108
							? XVT_HUD_TARGET_ALT_COVER
							: XVT_HUD_TARGET_COVER,
						0, 0, 0, XVT_COCKPIT_BEFORE_CRT);
}

static void DrawPowerGauge(const XvtHudDraw* draw, XvtHudSpriteRole role, const XvtCockpitPowerGauge* gauge) {
	if (!gauge->visible)
		return;
	for (unsigned segment = 0; segment < gauge->segments; ++segment)
		XvtHudDraw_Part(draw, role, segment < gauge->filled, 0, -(int)segment * gauge->step_y,
						XVT_COCKPIT_BEFORE_CRT);
}

static void DrawWeapons(const XvtHudDraw* draw) {
	const XvtCockpitWeapons* weapons = &draw->state->weapons;
	for (unsigned index = 0; index < weapons->slot_count; ++index) {
		const XvtCockpitWeaponSlot* slot = &weapons->slots[index];
		if (!slot->visible)
			continue;
		if (slot->charge_visible && !draw->state->view.instrument_base)
			for (unsigned segment = 0; segment < 10; ++segment)
				XvtHudDraw_Part(draw, XVT_HUD_LASER_CHARGE + index,
								segment < slot->segments ? slot->charge_band : slot->empty_band,
								(int)segment * draw->layout->sprites[XVT_HUD_LASER_CHARGE + index].step_x, 0,
								XVT_COCKPIT_BEFORE_CRT);
		if (slot->selection_visible)
			XvtHudDraw_Part(draw, XVT_HUD_LASER_SELECTION + index, slot->selected, 0, 0,
							XVT_COCKPIT_BEFORE_CRT);
		XvtHudDraw_Part(draw, XVT_HUD_LASER_READY + index, slot->ready, 0, 0, XVT_COCKPIT_BEFORE_CRT);
		if (slot->lock_visible)
			XvtHudDraw_Part(draw, XVT_HUD_LASER_LOCK + index, slot->locked, 0, 0, XVT_COCKPIT_BEFORE_CRT);
	}
	DrawIndicator(draw, XVT_HUD_TARGET_LOCK, &weapons->lock_indicator);
	if (!draw->state->view.instrument_base)
		for (unsigned launcher = 0; launcher < 4; ++launcher)
			if (weapons->launchers[launcher].visible)
				XvtHudDraw_Part(draw, XVT_HUD_LAUNCHER + launcher, weapons->launchers[launcher].selection, 0,
								0, XVT_COCKPIT_BEFORE_CRT);
}

void XvtHudInstruments_DrawMouseStick(const XvtHudDraw* draw, const XvtRenderView* view) {
	if (!view || !draw->state->mouse_stick_visible)
		return;
	const AeronSceneCamera* camera = &view->camera;
	float range = draw->state->view.viewport.height * draw->scale / 6.0f;
	float x = camera->viewport.x + (1 + camera->proj_x_offset) * camera->viewport.width * .5f +
			  draw->state->mouse_stick_x * range / 127.0f;
	float y = camera->viewport.y + (1 - camera->proj_y_offset) * camera->viewport.height * .5f -
			  draw->state->mouse_stick_y * range / 127.0f;
	float size = 4 * draw->scale, color[4];
	XvtUi_Color(draw->state->palette_argb[63], color);
	AeronRectI clip = { 0, 0, draw->width, draw->height };
	AeronDrawList_AddFrame(draw->after, x - size / 2, y - size / 2, size, size, draw->scale, color,
						   AERON_BLIT2D_BLEND_PMA, &clip);
}

void XvtHudInstruments_DrawWidgets(const XvtHudDraw* draw) {
	const XvtCockpitSystems* systems = &draw->state->systems;
	DrawWeapons(draw);
	for (unsigned side = 0; side < 2; ++side) {
		const XvtCockpitShield* shield = &systems->shields[side];
		if (shield->visible && !shield->text_mode) {
			XvtHudDraw_Part(draw, XVT_HUD_SHIELD + side * 2, shield->primary_level, 0, 0,
							XVT_COCKPIT_BEFORE_CRT);
			XvtHudDraw_Part(draw, XVT_HUD_SHIELD + side * 2 + 1, shield->overcharge_level, 0, 0,
							XVT_COCKPIT_BEFORE_CRT);
		}
	}
	DrawIndicator(draw, XVT_HUD_HULL, &systems->hull_indicator);
	DrawIndicator(draw, XVT_HUD_BEAM_ENABLED, &systems->beam_enabled);
	if (systems->beam_visible)
		for (unsigned segment = 0; segment < 9; ++segment)
			XvtHudDraw_Part(draw, XVT_HUD_BEAM, segment * 4 + systems->beam_segments[segment],
							draw->layout->beam_offsets[segment][0], draw->layout->beam_offsets[segment][1],
							XVT_COCKPIT_BEFORE_CRT);
	DrawPowerGauge(draw, XVT_HUD_ENGINE_POWER, &systems->engine_power);
	DrawPowerGauge(draw, XVT_HUD_LASER_POWER, &systems->laser_power);
	DrawPowerGauge(draw, XVT_HUD_SHIELD_POWER, &systems->shield_power);
	DrawPowerGauge(draw, XVT_HUD_BEAM_POWER, &systems->beam_power);
	DrawIndicator(draw, XVT_HUD_SHIELD_DISTRIBUTION, &systems->shield_distribution);
	DrawIndicator(draw, XVT_HUD_SFOILS, &systems->sfoils);
	DrawIndicator(draw, XVT_HUD_COUNTERMEASURE_SELECTION, &systems->countermeasure_selected);
	DrawIndicator(draw, XVT_HUD_CRITICAL_WARNING, &systems->critical_warning);
	for (unsigned index = 0; index < 4; ++index) {
		DrawIndicator(draw, XVT_HUD_THREAT + index, &systems->threats[index]);
		DrawIndicator(draw, XVT_HUD_CMD_ARMAMENT + index, &draw->state->target.armament[index]);
	}
}

void XvtHudInstruments_DrawRadar(const XvtHudDraw* draw) {
	const XvtCockpitRadar* radar = &draw->state->radar;
	for (unsigned side = 0; side < 2; ++side) {
		if (!radar->visible[side])
			continue;
		for (unsigned index = 0; index < radar->count[side]; ++index) {
			const XvtSnapRadarBlip* blip = &radar->blips[side][index];
			for (unsigned row = 0; row < 2; ++row)
				if (radar->coverage[side][index] & (1u << row))
					XvtHudDraw_Fill(draw, XVT_COCKPIT_BEFORE_CRT,
									(XvtSnapRect) { draw->layout->radar[side].x + blip->x,
													draw->layout->radar[side].y + blip->y + (int)row, 1, 1 },
									draw->state->palette_argb[blip->color_index & 255]);
		}
	}
	if (radar->marker_visible && radar->marker_side < 2) {
		static const int offsets[10][2] = { { -1, 1 }, { -2, 1 }, { -2, 0 }, { -2, -1 }, { -1, -1 },
											{ 1, -1 }, { 2, -1 }, { 2, 0 },  { 2, 1 },   { 1, 1 } };
		for (unsigned index = 0; index < 10; ++index)
			XvtHudDraw_Fill(
				draw, XVT_COCKPIT_BEFORE_CRT,
				(XvtSnapRect) {
					draw->layout->radar[radar->marker_side].x + radar->marker_x + offsets[index][0],
					draw->layout->radar[radar->marker_side].y + radar->marker_y + offsets[index][1], 1, 1 },
				draw->state->palette_argb[206]);
	}
}

void XvtHudInstruments_DrawWorldMarkers(const XvtHudDraw* draw, const XvtSnapTargetBox* markers,
										unsigned count, const XvtRenderView* view) {
	if (!view)
		return;
	for (unsigned index = 0; index < count; ++index) {
		const XvtSnapTargetBox* marker = &markers[index];
		float x, y, depth;
		if (marker->layer != XVT_SCOPE_COCKPIT ||
			!XvtRenderMath_ProjectWorld(view, marker->world_pos, &x, &y, &depth) || depth <= 0)
			continue;
		float focal = view->camera.viewport.width / (2 * tanf(view->camera.h_half_rad));
		float size = fminf(draw->layout->source_width * .75f * draw->scale,
						   fmaxf((draw->layout->source_width == 320 ? 4 : 8) * draw->scale,
								 marker->extent * focal / depth)) +
					 4 * draw->scale;
		float corner = fmaxf(3 * draw->scale, size / 8), color[4];
		XvtUi_Color(draw->state->palette_argb[marker->color_index & 255], color);
		AeronRectI clip = { 0, 0, draw->width, draw->height };
		for (unsigned side = 0; side < 2; ++side)
			for (unsigned end = 0; end < 2; ++end) {
				float xx = x - size / 2 + side * size, yy = y - size / 2 + end * size;
				AeronDrawList_AddLine(draw->before, xx, yy, xx + (side ? -corner : corner), yy, draw->scale,
									  color, AERON_BLIT2D_BLEND_PMA, &clip);
				AeronDrawList_AddLine(draw->before, xx, yy, xx, yy + (end ? -corner : corner), draw->scale,
									  color, AERON_BLIT2D_BLEND_PMA, &clip);
			}
	}
}

void XvtHudInstruments_DrawCrtMarker(const XvtHudDraw* draw) {
	const XvtSnapPreview* crt = &draw->state->crt;
	XvtRenderView view;
	float x, y, depth;
	if (!crt->valid || !crt->component_marker_valid ||
		!XvtRenderMath_BuildView(&crt->camera, crt->camera.world_pos, crt->destination.width,
								 crt->destination.height, &view) ||
		!XvtRenderMath_ProjectWorld(&view, crt->component_marker_world, &x, &y, &depth) || depth <= 0)
		return;
	XvtSnapRect rect = { crt->destination.x + (int)floorf(x) - 2, crt->destination.y + (int)floorf(y) - 2, 4,
						 4 };
	XvtHudDraw_FrameClipped(draw, XVT_COCKPIT_AFTER_CRT, rect, crt->destination,
							draw->state->palette_argb[206]);
}
