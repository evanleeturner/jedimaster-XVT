#include "xvt_runtime/snapshot/cockpit_instruments.h"

#include <string.h>

#include "xvt/assets/model_mesh.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_display.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/math2.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/snapshot/cockpit_readouts.h"
#include "xvt_runtime/snapshot/cockpit_text.h"

static struct {
	uint8_t laser_lock[XVT_HUD_WEAPON_SLOTS];
	uint8_t threats[4];
	struct xvt_cockpit_radar radar;
} g_recorded;

void xvt_cockpit_instruments_begin_update(int player)
{
	if (player == g_local_player) {
		memset(&g_recorded, 0, sizeof g_recorded);
		xvt_cockpit_readouts_begin_update();
	}
}

void xvt_cockpit_instruments_record_laser_lock(unsigned slot, unsigned state)
{
	if (slot < XVT_HUD_WEAPON_SLOTS) {
		g_recorded.laser_lock[slot] = (uint8_t)state;
	}
}

void xvt_cockpit_instruments_record_threats(unsigned attack, unsigned laser,
					    unsigned beam, unsigned warhead)
{
	g_recorded.threats[0] = (uint8_t)attack;
	g_recorded.threats[1] = (uint8_t)laser;
	g_recorded.threats[2] = (uint8_t)beam;
	g_recorded.threats[3] = (uint8_t)warhead;
}

void xvt_cockpit_instruments_record_radar(int object, int front, int index,
					  int x, int y, int color)
{
	if (index < 0 || index >= 48) {
		return;
	}
	unsigned side = front ? 0 : 1;
	const struct hud_element_layout *anchor =
		&g_hud_element_layouts[g_hud_instrument_set_base_index + side];
	struct xvt_snap_radar_blip *blip = &g_recorded.radar.blips[side][index];
	memset(blip, 0, sizeof *blip);
	blip->object = (struct xvt_snap_object_id){
		(uint16_t)object, g_object_table[object].object_signature};
	blip->x = (int16_t)(x - anchor->x);
	blip->y = (int16_t)(y - anchor->y);
	blip->color_index = (uint16_t)color;
	blip->targeted =
		object == g_players[g_local_player].current_target_object_idx;
	g_recorded.radar.count[side] = (uint8_t)(index == 47 ? 47 : index + 1);
	if (blip->targeted) {
		g_recorded.radar.marker_visible = 1;
		g_recorded.radar.marker_side = (uint8_t)side;
		g_recorded.radar.marker_x = blip->x;
		g_recorded.radar.marker_y = blip->y;
	}
}

static int is_rebel_fighter(const struct object_record *object)
{
	switch (object->object_type) {
	case CRAFT_SPECIES_X_WING:
	case CRAFT_SPECIES_Y_WING:
	case CRAFT_SPECIES_A_WING:
	case CRAFT_SPECIES_B_WING:
	case CRAFT_SPECIES_Z_95_HEADHUNTER:
		return 1;
	default:
		return 0;
	}
}

void xvt_cockpit_instruments_complete_radar(void)
{
	const struct hud_radar_blip_point *points[2] = {g_radar_fore_draw_blips,
							g_radar_aft_draw_blips};
	for (unsigned side = 0; side < 2; ++side) {
		for (unsigned index = 0; index < g_recorded.radar.count[side];
		     ++index) {
			g_recorded.radar.coverage[side][index] =
				g_flight_bytes_per_pixel == 1
					? (uint8_t)(points[side][index].color &
						    3)
					: points[side][index].color != 0;
		}
	}
}

static void build_shield_state(struct xvt_cockpit_state *state,
			       const struct craft_data *craft)
{
	struct xvt_cockpit_systems *systems = &state->systems;
	if (!(systems->active_hud_features & XVT_COCKPIT_FEATURE_SHIELDS)) {
		return;
	}
	unsigned base = state->view.instrument_base;
	unsigned maximum = g_model_defs[craft->model_index].shield_strength;
	for (unsigned side = 0; side < 2; ++side) {
		struct xvt_cockpit_shield *shield = &systems->shields[side];
		int energy = craft->shield_energy[side];
		if (energy < 0 || !(craft->working_subsystems &
				    CRAFT_SUBSYSTEM_FLAG_SHIELDS)) {
			energy = 0;
		}
		unsigned overcharged = (unsigned)energy >= maximum;
		unsigned fraction = math2_longratio_q16(
			overcharged ? (unsigned)energy - maximum
				    : (unsigned)energy,
			maximum);
		unsigned level = math2_longfraction(9, (uint16_t)fraction);
		shield->visible = 1;
		shield->text_mode =
			g_hud_element_layouts[base + 35 + side * 2]
				.color_index_or_widget_param == UINT16_MAX;
		shield->primary_level = overcharged ? 9 : (uint8_t)level;
		shield->overcharge_level = overcharged ? (uint8_t)level : 0;
		if (!shield->text_mode &&
		    g_player_flight_transient_timers[g_local_player]
			    .shield_hit_flash_timer &&
		    g_last_shield_damage_side == (int)side) {
			shield->hit_flash = 1;
			if (shield->overcharge_level) {
				shield->overcharge_level = 10;
			} else {
				shield->primary_level = 10;
			}
		}
		shield->primary_color =
			g_hud_shield_colors[shield->primary_level];
		shield->overcharge_color =
			g_hud_shield_colors[shield->overcharge_level];
	}
	unsigned hull = 2;
	if (g_player_flight_transient_timers[g_local_player]
		    .hull_hit_flash_timer) {
		hull = 3;
	} else if (craft->hull_max / 3) {
		unsigned damage = craft->hull_damage / (craft->hull_max / 3);
		hull = 2 - (damage > 2 ? 2 : damage);
	}
	systems->hull_indicator = (struct xvt_cockpit_indicator){
		1, (uint8_t)hull, 0, XVT_COCKPIT_BEFORE_CRT};
}

static void build_beam_state(struct xvt_cockpit_state *state,
			     const struct craft_data *craft)
{
	struct xvt_cockpit_systems *systems = &state->systems;
	if (!(systems->active_hud_features & XVT_COCKPIT_FEATURE_BEAM)) {
		return;
	}
	int strength = (int16_t)craft->beam_charge;
	int working = (craft->working_subsystems &
		       CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0;
	if (strength < 0 || !working) {
		strength = 0;
	}
	systems->beam_visible = 1;
	systems->beam_enabled = (struct xvt_cockpit_indicator){
		1, craft->beam_active && working, 0, XVT_COCKPIT_BEFORE_CRT};
	for (unsigned segment = 0; segment < 9; ++segment) {
		int charge = strength - 1000 * (int)segment;
		unsigned step =
			charge < 0 ? 0
				   : (unsigned)(charge > 1000 ? 1000 : charge) /
					     333;
		systems->beam_segments[segment] = (uint8_t)step;
	}
}

static void set_power_gauge(struct xvt_cockpit_power_gauge *gauge, int visible,
			    unsigned filled, unsigned segments, int step,
			    int rebel_fighter)
{
	gauge->visible = visible != 0;
	if (!visible) {
		return;
	}
	gauge->filled = (uint8_t)filled;
	gauge->segments = (uint8_t)segments;
	gauge->step_y = (int16_t)step;
	gauge->rebel_fighter = rebel_fighter != 0;
}

static void build_power_state(struct xvt_cockpit_state *state,
			      const struct craft_data *craft, int rebel_fighter)
{
	struct xvt_cockpit_systems *systems = &state->systems;
	unsigned features = systems->active_hud_features;
	unsigned laser = (uint8_t)craft->laser_recharge_level;
	unsigned shield = (uint8_t)craft->shield_recharge_level;
	unsigned beam = (uint8_t)craft->beam_recharge_level;
	unsigned engine = 8 - laser;
	int has_shields =
		(craft->system_flags & CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0;
	int beam_system =
		(craft->system_flags & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0;
	unsigned segments = 12;
	int step =
		g_flight_resolution_mode == FLIGHT_RESOLUTION_320X240
			? 2
			: (g_flight_resolution_mode == FLIGHT_RESOLUTION_480X360
				   ? 4
				   : 6);
	if (rebel_fighter) {
		if (!(features & (XVT_COCKPIT_FEATURE_LASER_POWER |
				  XVT_COCKPIT_FEATURE_ENGINE_POWER |
				  XVT_COCKPIT_FEATURE_SHIELD_POWER))) {
			return;
		}
		engine -= shield;
		segments = 4;
		step = g_flight_resolution_mode == FLIGHT_RESOLUTION_320X240
			       ? 2
			       : (g_flight_resolution_mode ==
						  FLIGHT_RESOLUTION_480X360
					  ? 3
					  : 5);
		if (state->view.instrument_base !=
		    HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
			segments *= 2;
			engine *= 2;
			shield *= 2;
			laser *= 2;
		}
		has_shields = 1;
	} else {
		if (has_shields) {
			engine = (uint16_t)(engine - shield + 2);
		}
		if (beam_system) {
			engine = (uint16_t)(engine - beam + 2);
		}
		laser *= 3;
		shield *= 3;
		beam *= 3;
	}
	set_power_gauge(&systems->engine_power,
			features & XVT_COCKPIT_FEATURE_ENGINE_POWER, engine,
			rebel_fighter ? 2 * segments : segments, step,
			rebel_fighter);
	set_power_gauge(&systems->laser_power,
			features & XVT_COCKPIT_FEATURE_LASER_POWER, laser,
			segments, rebel_fighter ? 2 * step : step,
			rebel_fighter);
	set_power_gauge(&systems->shield_power,
			has_shields &&
				(features & XVT_COCKPIT_FEATURE_SHIELD_POWER),
			shield, segments, rebel_fighter ? 2 * step : step,
			rebel_fighter);
	set_power_gauge(&systems->beam_power,
			!rebel_fighter && beam_system &&
				(features & XVT_COCKPIT_FEATURE_BEAM_POWER),
			beam, segments, step, rebel_fighter);
}

static void build_feature_covers(struct xvt_cockpit_state *state,
				 const struct object_record *object,
				 const struct craft_data *craft,
				 int rebel_fighter)
{
	struct xvt_cockpit_systems *systems = &state->systems;
	int forward = state->view.hud_state == HUD_VIEW_FORWARD;
	int hud_only = state->view.hud_state == HUD_VIEW_HUD_ONLY;
	if (!forward && !hud_only) {
		return;
	}
	for (unsigned index = 0; index < 13; ++index) {
		unsigned feature = 1u << index;
		if (!(feature &
		      craft->damage_stats.installed_hud_feature_mask)) {
			continue;
		}
		int active = (feature &
			      craft->damage_stats.active_hud_feature_mask) != 0;
		if (forward && rebel_fighter && active) {
			continue;
		}
		if (hud_only &&
		    (feature == XVT_COCKPIT_FEATURE_LASER_CHARGE ||
		     feature == XVT_COCKPIT_FEATURE_LASER_SELECTION ||
		     feature == XVT_COCKPIT_FEATURE_WARHEADS)) {
			continue;
		}
		if (hud_only && rebel_fighter &&
		    (feature == XVT_COCKPIT_FEATURE_BEAM ||
		     feature == XVT_COCKPIT_FEATURE_BEAM_POWER)) {
			continue;
		}
		systems->feature_covers[index] = (struct xvt_cockpit_indicator){
			1,
			(uint8_t)(!active && (hud_only || !rebel_fighter) ? 13
									  : 0),
			0, XVT_COCKPIT_BEFORE_CRT};
	}
	if (!state->view.map_active && !rebel_fighter &&
	    object->object_type != CRAFT_SPECIES_TIE_FIGHTER) {
		if (!(craft->system_flags & CRAFT_SUBSYSTEM_FLAG_SHIELDS)) {
			systems->unavailable_shields =
				(struct xvt_cockpit_indicator){
					1, 0, 0, XVT_COCKPIT_BEFORE_CRT};
		}
		if (!(craft->system_flags & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM)) {
			systems->unavailable_beam[0] =
				systems->unavailable_beam[1] =
					(struct xvt_cockpit_indicator){
						1, 0, 0,
						XVT_COCKPIT_BEFORE_CRT};
		}
	}
}

static void build_basic_readouts(struct xvt_cockpit_state *state,
				 const struct craft_data *craft)
{
	if (craft->damage_stats.active_hud_feature_mask &
	    XVT_COCKPIT_FEATURE_SPEED) {
		state->readouts.speed.visible =
			state->readouts.throttle.visible = 1;
	}
	if (state->view.hud_state == HUD_VIEW_FORWARD) {
		state->readouts.clock_minutes.visible =
			state->readouts.clock_seconds.visible = 1;
	}
}

static void build_laser_slots(struct xvt_cockpit_state *state,
			      const struct craft_data *craft, int rebel_fighter)
{
	const struct player_data *player = &g_players[g_local_player];
	const struct model_def *model = &g_model_defs[craft->model_index];
	unsigned count = craft->laser_slot_count;
	if (count > XVT_HUD_WEAPON_SLOTS) {
		count = XVT_HUD_WEAPON_SLOTS;
	}
	state->weapons.slot_count = (uint8_t)count;
	state->weapons.selected_bank = player->selected_weapon_bank;
	unsigned base = state->view.instrument_base;
	int cannons_working =
		(craft->working_subsystems & CRAFT_SUBSYSTEM_FLAG_CANNONS) != 0;
	int charge_visible = state->view.hud_state == HUD_VIEW_FORWARD &&
			     (craft->damage_stats.active_hud_feature_mask &
			      XVT_COCKPIT_FEATURE_LASER_CHARGE) &&
			     (craft->damage_stats.active_hud_feature_mask &
			      XVT_COCKPIT_FEATURE_LASER_SELECTION);
	for (unsigned index = 0; index < count; ++index) {
		struct xvt_cockpit_weapon_slot *slot =
			&state->weapons.slots[index];
		const struct hud_element_layout *layout =
			&g_hud_element_layouts[base + index + 3];
		if (layout->x + layout->y == 0 &&
		    base == HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
			continue;
		}
		slot->visible = 1;
		slot->bank = index > model->laser_group_last_slot[0];
		slot->hud_slot = (uint8_t)index;
		slot->charge_visible = charge_visible;
		int charge = craft->weapon_slots[index].laser_charge;
		if (charge_visible && charge > 0 && cannons_working) {
			++charge;
			slot->charge_band = charge <= 64 ? 1 : 2;
			slot->empty_band = charge <= 64 ? 0 : 1;
			if (charge > 64) {
				charge -= 64;
			}
			slot->segments =
				(uint8_t)(charge / 6 > 10 ? 10 : charge / 6);
		}
		slot->charge_percent.visible =
			charge_visible &&
			base != HUD_COCKPIT_INSTRUMENT_BASE_INDEX &&
			layout->selector;

		unsigned ready = 0;
		unsigned selected = 0;
		if (charge > 0 && cannons_working) {
			if (player->selected_weapon_mode == 0 &&
			    player->selected_weapon_bank == slot->bank) {
				unsigned next = craft->laser_state
							.next_slot[slot->bank];
				switch (craft->laser_state
						.link_mode[slot->bank]) {
				case 1:
					ready = next == index ? 3 : 1;
					break;
				case 2:
					ready = next == index ||
								(count >= 4 &&
								 (int)next - (int)index ==
									 -2)
							? 3
							: 1;
					break;
				case 3:
					ready = 3;
					break;
				default:
					break;
				}
				selected = ready;
				if (ready == 3 &&
				    craft->laser_state
					    .fire_cooldown_ticks[slot->bank]) {
					ready = 2;
					selected = 5;
				}
			} else {
				selected = 1;
			}
		}
		if (slot->charge_band == 2) {
			++selected;
		}
		slot->ready_state = (uint8_t)ready;
		slot->selection_state = (uint8_t)selected;
		const struct hud_element_layout *selection =
			&g_hud_element_layouts[base + index + 11];
		slot->selection_visible = rebel_fighter && charge_visible &&
					  selection->x + selection->y != 0;
		slot->lock_visible = rebel_fighter;
		slot->locked = g_recorded.laser_lock[index];
	}
	unsigned lock = player->selected_weapon_mode == 0
				? (g_target_lock_active ? 4 : 0)
				: (player->current_target_object_idx == -1
					   ? 1
					   : player->missile_lock_state + 1);
	state->weapons.lock_indicator = (struct xvt_cockpit_indicator){
		1, (uint8_t)lock, 0, XVT_COCKPIT_BEFORE_CRT};
}

static void build_launcher(struct xvt_cockpit_state *state,
			   const struct craft_data *craft, unsigned weapon_slot,
			   unsigned display_slot, unsigned bank,
			   int rebel_fighter)
{
	if (weapon_slot >= XVT_HUD_WEAPON_SLOTS) {
		return;
	}
	struct xvt_cockpit_launcher *launcher =
		&state->weapons.launchers[display_slot];
	const struct player_data *player = &g_players[g_local_player];
	unsigned count = craft->warhead_launcher_count
				 ? craft->weapon_slots[weapon_slot].ammo_count
				 : 0;
	unsigned selection = 0;
	if (count && (craft->working_subsystems &
		      CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER)) {
		selection = 1;
		if (player->selected_weapon_mode &&
		    player->selected_weapon_bank == bank) {
			unsigned flags =
				(uint8_t)craft->warhead_launcher_flags[bank];
			selection =
				(flags & 127) == 3
					? 2
					: ((display_slot & 1) == (flags >> 7)) +
						  1;
		}
	}
	unsigned base = state->view.instrument_base;
	launcher->visible = 1;
	launcher->bank = (uint8_t)bank;
	launcher->weapon_slot = (uint8_t)weapon_slot;
	launcher->selection =
		(uint8_t)(rebel_fighter && selection == 2 && base == 0
				  ? 4
				  : selection);

	launcher->count.visible =
		!base ||
		(count &&
		 g_hud_element_layouts[base + 27 + display_slot].selector);
}

static void build_warhead_state(struct xvt_cockpit_state *state,
				const struct object_record *object,
				const struct craft_data *craft,
				int rebel_fighter)
{
	if (!(craft->damage_stats.active_hud_feature_mask &
	      XVT_COCKPIT_FEATURE_WARHEADS)) {
		return;
	}
	const struct model_def *model =
		&g_model_defs[g_object_type_table[object->object_type]
				      .model_index];
	if (!model->warhead_launcher_slot_count[0] &&
	    !model->warhead_launcher_slot_count[1]) {
		return;
	}
	int missile_boat =
		g_object_type_table[object->object_type].model_index ==
		g_object_type_table[CRAFT_SPECIES_MISSILE_BOAT].model_index;
	for (unsigned bank = 0; bank < (missile_boat ? 2u : 1u); ++bank) {
		build_launcher(state, craft,
			       model->warhead_launcher_first_slot[bank],
			       bank * 2, bank, rebel_fighter);
		build_launcher(state, craft,
			       model->warhead_launcher_last_slot[bank],
			       bank * 2 + 1, bank, rebel_fighter);
		state->weapons.warheads[bank].visible = 1;
		state->weapons.warheads[bank].type =
			craft->warhead_slot_type_ids[bank];
		state->weapons.warheads[bank].selected =
			g_players[g_local_player].selected_weapon_mode &&
			g_players[g_local_player].selected_weapon_bank == bank;
	}
}

static void build_cockpit_warnings(struct xvt_cockpit_state *state,
				   const struct craft_data *craft,
				   int rebel_fighter)
{
	if (state->view.hud_state != HUD_VIEW_FORWARD) {
		return;
	}
	struct xvt_cockpit_systems *systems = &state->systems;
	if (rebel_fighter &&
	    g_hud_element_layouts[50].x + g_hud_element_layouts[50].y) {
		unsigned shield = (uint32_t)craft->shield_energy[0] +
				  (uint32_t)craft->shield_energy[1];
		unsigned damage =
			craft->hull_max / 3
				? craft->hull_damage / (craft->hull_max / 3)
				: 2;
		unsigned flash =
			shield < 100 && damage == 2
				? (g_mission_elapsed_clock.subsecond_ticks /
				   59) & 1
				: 0;
		systems->critical_warning = (struct xvt_cockpit_indicator){
			1, (uint8_t)flash, 0, XVT_COCKPIT_BEFORE_CRT};
	}
	if (state->view.map_active) {
		return;
	}
	unsigned base = state->view.instrument_base;
	if (!base) {
		systems->countermeasure_count.visible =
			g_hud_element_layouts[48].x +
				g_hud_element_layouts[48].y !=
			0;

		systems->countermeasure_active = (struct xvt_cockpit_indicator){
			1, (uint8_t)craft->chaff_active_seconds != 0, 0,
			XVT_COCKPIT_BEFORE_CRT};
		systems->countermeasure_active.visible =
			systems->countermeasure_count.visible;
	}
	if (rebel_fighter) {
		if ((systems->active_hud_features &
		     XVT_COCKPIT_FEATURE_SHIELDS) &&
		    g_hud_element_layouts[51].x + g_hud_element_layouts[51].y) {
			systems->shield_distribution =
				(struct xvt_cockpit_indicator){
					1, (uint8_t)craft->shield_distrib_mode,
					0, XVT_COCKPIT_BEFORE_CRT};
		}
		if (g_hud_element_layouts[45].x) {
			systems->sfoils = (struct xvt_cockpit_indicator){
				1, (craft->s_foil_state & 2) == 0, 0,
				XVT_COCKPIT_BEFORE_CRT};
		}
	}
}

void xvt_cockpit_instruments_build(struct xvt_cockpit_state *state)
{
	memset(&state->systems, 0, sizeof state->systems);
	memset(&state->weapons, 0, sizeof state->weapons);
	memset(&state->target, 0, sizeof state->target);
	memset(&state->radar, 0, sizeof state->radar);
	memset(&state->readouts, 0, sizeof state->readouts);
	const struct player_data *player = &g_players[g_local_player];
	unsigned slot = (uint16_t)player->object_index;
	if (!g_object_table ||
	    slot >= (unsigned)g_region_main_object_slot_end) {
		return;
	}
	const struct object_record *object = &g_object_table[slot];
	const struct mobile_object *mobile = object->mobj;
	const struct craft_data *craft = mobile ? mobile->p_craft : NULL;
	if (!craft) {
		return;
	}
	struct xvt_cockpit_systems *systems = &state->systems;
	systems->installed = craft->system_flags;
	systems->working = craft->working_subsystems;
	systems->active_hud_features =
		craft->damage_stats.active_hud_feature_mask;
	systems->installed_hud_features =
		craft->damage_stats.installed_hud_feature_mask;
	int rebel_fighter = is_rebel_fighter(object);
	state->view.rebel_fighter = (uint8_t)rebel_fighter;
	state->view.laser_slots = craft->laser_slot_count > XVT_HUD_WEAPON_SLOTS
					  ? XVT_HUD_WEAPON_SLOTS
					  : (uint8_t)craft->laser_slot_count;
	build_feature_covers(state, object, craft, rebel_fighter);
	if (!state->view.instruments_visible) {
		return;
	}
	int forward = state->view.hud_state == HUD_VIEW_FORWARD;
	int hud_only = state->view.hud_state == HUD_VIEW_HUD_ONLY;
	build_cockpit_warnings(state, craft, rebel_fighter);
	if (!state->view.map_active && (forward || hud_only)) {
		build_laser_slots(state, craft, rebel_fighter);
		build_warhead_state(state, object, craft, rebel_fighter);
		build_shield_state(state, craft);
		build_beam_state(state, craft);
		build_power_state(state, craft, rebel_fighter);
		build_basic_readouts(state, craft);
		state->radar = g_recorded.radar;
		int radar_visible = (systems->active_hud_features &
				     XVT_COCKPIT_FEATURE_FORE_RADAR) &&
				    (systems->active_hud_features &
				     XVT_COCKPIT_FEATURE_AFT_RADAR);
		state->radar.visible[0] = state->radar.visible[1] =
			radar_visible;
		for (unsigned index = 0; index < 4; ++index) {
			systems->threats[index] =
				(struct xvt_cockpit_indicator){
					1, g_recorded.threats[index], 0,
					XVT_COCKPIT_BEFORE_CRT};
		}
	}
}
