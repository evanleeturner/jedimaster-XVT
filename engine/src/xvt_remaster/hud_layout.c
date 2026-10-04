#include "xvt_remaster/hud_layout.h"

#include <math.h>
#include <string.h>

static int add_part(struct xvt_hud_layout *layout,
		    const struct xvt_cockpit_definition *definition,
		    unsigned panel, unsigned key, xvt_hud_part_color mode,
		    unsigned color, int fade)
{
	if (panel >= XVT_HUD_PANEL_BINDINGS ||
	    layout->part_count == XVT_HUD_PART_CAPACITY) {
		return 0;
	}
	struct xvt_hud_part_request *part =
		&layout->parts[layout->part_count++];
	part->source = definition->panels[panel];
	part->key = (uint16_t)key;
	part->color_mode = (uint8_t)mode;
	part->color = (uint8_t)color;
	part->fade = (int16_t)fade;
	return part->source.asset_id != 0;
}

static int bind_frames(struct xvt_hud_layout *layout,
		       const struct xvt_cockpit_state *state,
		       xvt_hud_sprite_role role, unsigned index, unsigned count,
		       int fixed_key, unsigned frame_step)
{
	const struct xvt_snap_hud_element *element =
		&state->definition.layout.elements[index];
	struct xvt_hud_sprite_binding *binding = &layout->sprites[role];
	binding->x = (int16_t)element->x;
	binding->y = (int16_t)element->y;
	binding->first_part = layout->part_count;
	binding->part_count = (uint16_t)count;
	for (unsigned frame = 0; frame < count; ++frame) {
		if (!add_part(layout, &state->definition,
			      element->selector + frame * frame_step,
			      fixed_key < 0 ? element->color_index
					    : (unsigned)fixed_key,
			      XVT_HUD_PART_ORIGINAL, 0, 0)) {
			return 0;
		}
	}
	return 1;
}

static int bind_faded_frames(struct xvt_hud_layout *layout,
			     const struct xvt_cockpit_state *state,
			     xvt_hud_sprite_role role, unsigned index, int beam)
{
	const struct xvt_cockpit_definition *definition = &state->definition;
	const struct xvt_snap_hud_element *element =
		&definition->layout.elements[index];
	struct xvt_hud_sprite_binding *binding = &layout->sprites[role];
	if (!beam && element->color_index == UINT16_MAX) {
		return 1;
	}
	binding->x = (int16_t)element->x;
	binding->y = (int16_t)element->y;
	binding->first_part = layout->part_count;
	binding->part_count = beam ? 36 : 11;
	for (unsigned variant = 0; variant < binding->part_count; ++variant) {
		unsigned color =
			beam ? definition->beam_segment_colors[variant % 4]
			     : definition->shield_colors[variant];
		int fade = beam ? (color == definition->beam_segment_colors[0]
					   ? 0
					   : (int16_t)element->clip_width)
				: (variant ? (int16_t)element->clip_width : -1);
		if (!add_part(layout, definition,
			      element->selector + (beam ? variant / 4 : 0),
			      element->color_index,
			      fade > 0 ? XVT_HUD_PART_INDEXED_FADE
				       : XVT_HUD_PART_MONOCHROME,
			      color, fade)) {
			return 0;
		}
	}
	return 1;
}

static int compile_weapons(struct xvt_hud_layout *layout,
			   const struct xvt_cockpit_state *state)
{
	unsigned base = state->view.instrument_base;
	for (unsigned slot = 0; slot < state->view.laser_slots; ++slot) {
		const struct xvt_snap_hud_element *charge_layout =
			&state->definition.layout.elements[base + 3 + slot];
		const struct xvt_snap_hud_element *selection_layout =
			&state->definition.layout.elements[base + 11 + slot];
		if (!base && !charge_layout->x && !charge_layout->y) {
			continue;
		}
		if (!base) {
			if (!bind_frames(layout, state,
					 XVT_HUD_LASER_CHARGE + slot,
					 base + 3 + slot, 3, 253, 1)) {
				return 0;
			}
			struct xvt_hud_sprite_binding *charge =
				&layout->sprites[XVT_HUD_LASER_CHARGE + slot];
			charge->mirrored = state->definition.layout
						   .elements[base + 3 + slot]
						   .color_index != 0;
			charge->step_x =
				layout->source_width == 320
					? 3
					: (layout->source_width == 480 ? 4 : 6);
			if (charge->mirrored) {
				charge->step_x = -charge->step_x;
			}
		}
		if ((state->view.rebel_fighter &&
		     (selection_layout->x || selection_layout->y) &&
		     !bind_frames(layout, state, XVT_HUD_LASER_SELECTION + slot,
				  base + 11 + slot, 7, -1, 1)) ||
		    !bind_frames(layout, state, XVT_HUD_LASER_READY + slot,
				 base + 53 + slot, 4, -1, 1) ||
		    (state->view.rebel_fighter &&
		     !bind_frames(layout, state, XVT_HUD_LASER_LOCK + slot,
				  base + 61 + slot, 3, -1, 1))) {
			return 0;
		}
	}
	if (!base) {
		for (unsigned slot = 0; slot < 4; ++slot) {
			if (!bind_frames(layout, state, XVT_HUD_LAUNCHER + slot,
					 19 + slot, 5, -1, 1)) {
				return 0;
			}
		}
	}
	return bind_frames(layout, state, XVT_HUD_TARGET_LOCK, base + 52, 5, -1,
			   1);
}

static int compile_systems(struct xvt_hud_layout *layout,
			   const struct xvt_cockpit_state *state)
{
	unsigned base = state->view.instrument_base;
	unsigned features = state->systems.installed_hud_features;
	for (unsigned layer = 0; layer < 4; ++layer) {
		if ((features & XVT_COCKPIT_FEATURE_SHIELDS) &&
		    state->definition.layout
				    .elements[base + 35 + (layer / 2) * 2]
				    .color_index != UINT16_MAX &&
		    !bind_faded_frames(layout, state, XVT_HUD_SHIELD + layer,
				       base + 35 + layer, 0)) {
			return 0;
		}
	}
	static const unsigned power_features[] = {
		XVT_COCKPIT_FEATURE_ENGINE_POWER,
		XVT_COCKPIT_FEATURE_LASER_POWER,
		XVT_COCKPIT_FEATURE_SHIELD_POWER,
		XVT_COCKPIT_FEATURE_BEAM_POWER};
	for (unsigned gauge = 0; gauge < 4; ++gauge) {
		if ((features & power_features[gauge]) &&
		    (gauge != 3 || !state->view.rebel_fighter) &&
		    !bind_frames(layout, state, XVT_HUD_ENGINE_POWER + gauge,
				 base + 42 + gauge, 2, 253, 1)) {
			return 0;
		}
	}
	if ((features & XVT_COCKPIT_FEATURE_BEAM) &&
	    !bind_faded_frames(layout, state, XVT_HUD_BEAM, base + 51, 1)) {
		return 0;
	}
	if (((features & XVT_COCKPIT_FEATURE_BEAM) &&
	     !bind_frames(layout, state, XVT_HUD_BEAM_ENABLED, base + 116, 2,
			  -1, 1)) ||
	    ((features & XVT_COCKPIT_FEATURE_SHIELDS) &&
	     !bind_frames(layout, state, XVT_HUD_HULL, base + 39, 4, -1, 1))) {
		return 0;
	}
	if (state->view.rebel_fighter &&
	    state->definition.layout.elements[45].x &&
	    !bind_frames(layout, state, XVT_HUD_SFOILS, 45, 2, -1, 1)) {
		return 0;
	}
	if (state->view.rebel_fighter &&
	    (features & XVT_COCKPIT_FEATURE_SHIELDS) &&
	    (state->definition.layout.elements[51].x ||
	     state->definition.layout.elements[51].y) &&
	    !bind_frames(layout, state, XVT_HUD_SHIELD_DISTRIBUTION, 51, 3, -1,
			 1)) {
		return 0;
	}
	for (unsigned threat = 0; threat < 4; ++threat) {
		if (!bind_frames(layout, state, XVT_HUD_THREAT + threat,
				 base + 90 + threat, threat < 2 ? 2 : 3, -1,
				 1)) {
			return 0;
		}
	}
	for (unsigned cover = 0; cover < 13; ++cover) {
		if ((features & (1u << cover)) &&
		    !bind_frames(layout, state, XVT_HUD_FEATURE_COVER + cover,
				 base + 69 + cover,
				 state->view.rebel_fighter && !base ? 1 : 2, -1,
				 13)) {
			return 0;
		}
	}
	return bind_frames(layout, state, XVT_HUD_TARGET_COVER, base + 69, 1,
			   -1, 1) &&
	       bind_frames(layout, state, XVT_HUD_TARGET_ALT_COVER, base + 108,
			   1, -1, 1) &&
	       bind_frames(layout, state, XVT_HUD_UNAVAILABLE_SHIELDS,
			   base + 108, 1, -1, 1) &&
	       bind_frames(layout, state, XVT_HUD_UNAVAILABLE_BEAM, base + 109,
			   1, -1, 1) &&
	       bind_frames(layout, state, XVT_HUD_UNAVAILABLE_BEAM_POWER,
			   base + 110, 1, -1, 1) &&
	       bind_frames(layout, state, XVT_HUD_COUNTERMEASURE_SELECTION, 47,
			   2, -1, 1) &&
	       bind_frames(layout, state, XVT_HUD_CRITICAL_WARNING, 50, 2, -1,
			   1);
}

static void compile_anchors(struct xvt_hud_layout *layout,
			    const struct xvt_cockpit_state *state)
{
	unsigned base = state->view.instrument_base;
	const struct xvt_snap_hud_element *crt =
		&state->definition.layout.elements[base + 2];
	layout->crt = (struct xvt_snap_rect){crt->x, crt->y, crt->selector,
					     crt->color_index};
	for (unsigned side = 0; side < 2; ++side) {
		const struct xvt_snap_hud_element *radar =
			&state->definition.layout.elements[base + side];
		layout->radar[side] = (struct xvt_hud_anchor){
			(int16_t)radar->x, (int16_t)radar->y};
	}
	/* Placement capture already resolves map/shared anchors and message margins. */
	for (unsigned page = 0; page < MFD_PAGE_COUNT; ++page) {
		layout->pages[page] = state->pages[page].placement;
	}
	for (unsigned pane = 0; pane < XVT_COCKPIT_MESSAGE_COUNT; ++pane) {
		layout->messages[pane] = state->messages.panes[pane].placement;
	}
}

int xvt_hud_layout_compile(const struct xvt_cockpit_state *state,
			   struct xvt_hud_layout *layout)
{
	if (!state || !layout) {
		return 0;
	}
	memset(layout, 0, sizeof *layout);
	if (!state->view.screen_width || !state->view.screen_height ||
	    state->view.instrument_base > 288 ||
	    state->view.laser_slots > XVT_HUD_WEAPON_SLOTS) {
		return 0;
	}
	layout->source_width = state->view.screen_width;
	layout->source_height = state->view.screen_height;
	layout->mirrored = state->view.mirrored;
	layout->viewport = state->view.viewport;
	layout->projection_offset_y = state->view.projection_offset_y;
	memcpy(layout->beam_offsets, state->definition.beam_offsets,
	       sizeof layout->beam_offsets);
	if (state->view.hud_state != HUD_VIEW_FULL_SCREEN &&
	    state->view.resource_descriptor < 28) {
		layout->base_asset_id =
			state->definition.layout
				.descriptors[state->view.resource_descriptor]
				.lfd_asset_id;
	}
	compile_anchors(layout, state);
	if (!state->definition.layout.valid) {
		return 1; /* Standalone loading/alert fonts do not require a cockpit definition. */
	}
	if (state->view.hud_state == HUD_VIEW_FORWARD ||
	    state->view.hud_state == HUD_VIEW_HUD_ONLY) {
		if (!compile_weapons(layout, state) ||
		    !compile_systems(layout, state)) {
			return 0;
		}
	}
	if (state->view.hud_state == HUD_VIEW_TARGET_CAMERA) {
		for (unsigned bank = 0; bank < 4; ++bank) {
			if (!bind_frames(layout, state,
					 XVT_HUD_CMD_ARMAMENT + bank, 98 + bank,
					 3, -1, 1)) {
				return 0;
			}
		}
	}
	return 1;
}

void xvt_hud_layout_fit(const struct xvt_hud_layout *layout, int width,
			int height, float *scale, float *offset_x,
			float *offset_y)
{
	*scale = layout->source_width && layout->source_height && width > 0 &&
				 height > 0
			 ? fminf((float)width / layout->source_width,
				 (float)height / layout->source_height)
			 : 0;
	*offset_x = (width - layout->source_width * *scale) * .5f;
	*offset_y = (height - layout->source_height * *scale) * .5f;
}

int xvt_hud_layout_update(struct xvt_hud_layout_cache *cache,
			  const struct xvt_cockpit_state *state)
{
	if (!cache || !state) {
		return 0;
	}
	struct xvt_cockpit_view key = {0};
	key.screen_width = state->view.screen_width;
	key.screen_height = state->view.screen_height;
	key.hud_state = state->view.hud_state;
	key.instrument_base = state->view.instrument_base;
	key.resource_descriptor = state->view.resource_descriptor;
	key.mirrored = state->view.mirrored;
	key.viewport = state->view.viewport;
	key.projection_offset_y = state->view.projection_offset_y;
	key.rebel_fighter = state->view.rebel_fighter;
	key.laser_slots = state->view.laser_slots;
	if (!cache->valid ||
	    cache->definition_generation != state->definition_generation ||
	    cache->installed_hud_features !=
		    state->systems.installed_hud_features ||
	    memcmp(&cache->view_key, &key, sizeof key)) {
		struct xvt_hud_layout replacement;
		if (!xvt_hud_layout_compile(state, &replacement)) {
			return 0;
		}
		cache->layout = replacement;
		cache->definition_generation = state->definition_generation;
		cache->view_key = key;
		cache->installed_hud_features =
			state->systems.installed_hud_features;
		cache->valid = 1;
	} else {
		compile_anchors(&cache->layout, state);
	}
	return 1;
}
