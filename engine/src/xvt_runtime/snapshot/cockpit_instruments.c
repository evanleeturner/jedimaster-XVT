#include "xvt_runtime/snapshot/cockpit_instruments.h"

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
#include <string.h>

static struct {
	uint8_t laser_lock[XVT_HUD_WEAPON_SLOTS];
	uint8_t threats[4];
	XvtCockpitRadar radar;
} g_recorded;

void XvtCockpitInstruments_BeginUpdate(int player) {
	if (player == g_localPlayer) {
		memset(&g_recorded, 0, sizeof g_recorded);
		XvtCockpitReadouts_BeginUpdate();
	}
}

void XvtCockpitInstruments_RecordLaserLock(unsigned slot, unsigned state) {
	if (slot < XVT_HUD_WEAPON_SLOTS)
		g_recorded.laser_lock[slot] = (uint8_t)state;
}

void XvtCockpitInstruments_RecordThreats(unsigned attack, unsigned laser, unsigned beam, unsigned warhead) {
	g_recorded.threats[0] = (uint8_t)attack;
	g_recorded.threats[1] = (uint8_t)laser;
	g_recorded.threats[2] = (uint8_t)beam;
	g_recorded.threats[3] = (uint8_t)warhead;
}

void XvtCockpitInstruments_RecordRadar(int object, int front, int index, int x, int y, int color) {
	if (index < 0 || index >= 48)
		return;
	unsigned side = front ? 0 : 1;
	const HudElementLayout* anchor = &g_hudElementLayouts[g_hudInstrumentSetBaseIndex + side];
	XvtSnapRadarBlip* blip = &g_recorded.radar.blips[side][index];
	memset(blip, 0, sizeof *blip);
	blip->object = (XvtSnapObjectId) { (uint16_t)object, g_objectTable[object].objectSignature };
	blip->x = (int16_t)(x - anchor->x);
	blip->y = (int16_t)(y - anchor->y);
	blip->color_index = (uint16_t)color;
	blip->targeted = object == g_players[g_localPlayer].currentTargetObjectIdx;
	g_recorded.radar.count[side] = (uint8_t)(index == 47 ? 47 : index + 1);
	if (blip->targeted) {
		g_recorded.radar.marker_visible = 1;
		g_recorded.radar.marker_side = (uint8_t)side;
		g_recorded.radar.marker_x = blip->x;
		g_recorded.radar.marker_y = blip->y;
	}
}

static int IsRebelFighter(const ObjectRecord* object) {
	switch (object->objectType) {
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

void XvtCockpitInstruments_CompleteRadar(void) {
	const HudRadarBlipPoint* points[2] = { g_radarForeDrawBlips, g_radarAftDrawBlips };
	for (unsigned side = 0; side < 2; ++side)
		for (unsigned index = 0; index < g_recorded.radar.count[side]; ++index)
			g_recorded.radar.coverage[side][index] = g_flightBytesPerPixel == 1
														 ? (uint8_t)(points[side][index].color & 3)
														 : points[side][index].color != 0;
}

static void BuildShieldState(XvtCockpitState* state, const CraftData* craft) {
	XvtCockpitSystems* systems = &state->systems;
	if (!(systems->active_hud_features & XVT_COCKPIT_FEATURE_SHIELDS))
		return;
	unsigned base = state->view.instrument_base;
	unsigned maximum = g_modelDefs[craft->modelIndex].shieldStrength;
	for (unsigned side = 0; side < 2; ++side) {
		XvtCockpitShield* shield = &systems->shields[side];
		int energy = craft->shieldEnergy[side];
		if (energy < 0 || !(craft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_SHIELDS))
			energy = 0;
		unsigned overcharged = (unsigned)energy >= maximum;
		unsigned fraction =
			MATH2_longratioQ16(overcharged ? (unsigned)energy - maximum : (unsigned)energy, maximum);
		unsigned level = MATH2_longfraction(9, (uint16_t)fraction);
		shield->visible = 1;
		shield->text_mode = g_hudElementLayouts[base + 35 + side * 2].colorIndexOrWidgetParam == UINT16_MAX;
		shield->primary_level = overcharged ? 9 : (uint8_t)level;
		shield->overcharge_level = overcharged ? (uint8_t)level : 0;
		if (!shield->text_mode && g_playerFlightTransientTimers[g_localPlayer].shieldHitFlashTimer &&
			g_lastShieldDamageSide == (int)side) {
			shield->hit_flash = 1;
			if (shield->overcharge_level)
				shield->overcharge_level = 10;
			else
				shield->primary_level = 10;
		}
		shield->primary_color = g_hudShieldColors[shield->primary_level];
		shield->overcharge_color = g_hudShieldColors[shield->overcharge_level];
	}
	unsigned hull = 2;
	if (g_playerFlightTransientTimers[g_localPlayer].hullHitFlashTimer)
		hull = 3;
	else if (craft->hullMax / 3) {
		unsigned damage = craft->hullDamage / (craft->hullMax / 3);
		hull = 2 - (damage > 2 ? 2 : damage);
	}
	systems->hull_indicator = (XvtCockpitIndicator) { 1, (uint8_t)hull, 0, XVT_COCKPIT_BEFORE_CRT };
}

static void BuildBeamState(XvtCockpitState* state, const CraftData* craft) {
	XvtCockpitSystems* systems = &state->systems;
	if (!(systems->active_hud_features & XVT_COCKPIT_FEATURE_BEAM))
		return;
	int strength = (int16_t)craft->beamCharge;
	int working = (craft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0;
	if (strength < 0 || !working)
		strength = 0;
	systems->beam_visible = 1;
	systems->beam_enabled =
		(XvtCockpitIndicator) { 1, craft->beamActive && working, 0, XVT_COCKPIT_BEFORE_CRT };
	for (unsigned segment = 0; segment < 9; ++segment) {
		int charge = strength - 1000 * (int)segment;
		unsigned step = charge < 0 ? 0 : (unsigned)(charge > 1000 ? 1000 : charge) / 333;
		systems->beam_segments[segment] = (uint8_t)step;
	}
}

static void SetPowerGauge(XvtCockpitPowerGauge* gauge, int visible, unsigned filled, unsigned segments,
						  int step, int rebel_fighter) {
	gauge->visible = visible != 0;
	if (!visible)
		return;
	gauge->filled = (uint8_t)filled;
	gauge->segments = (uint8_t)segments;
	gauge->step_y = (int16_t)step;
	gauge->rebel_fighter = rebel_fighter != 0;
}

static void BuildPowerState(XvtCockpitState* state, const CraftData* craft, int rebel_fighter) {
	XvtCockpitSystems* systems = &state->systems;
	unsigned features = systems->active_hud_features;
	unsigned laser = (uint8_t)craft->laserRechargeLevel, shield = (uint8_t)craft->shieldRechargeLevel;
	unsigned beam = (uint8_t)craft->beamRechargeLevel;
	unsigned engine = 8 - laser;
	int has_shields = (craft->systemFlags & CRAFT_SUBSYSTEM_FLAG_SHIELDS) != 0;
	int beam_system = (craft->systemFlags & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) != 0;
	unsigned segments = 12;
	int step = g_flightResolutionMode == FLIGHT_RESOLUTION_320X240
				   ? 2
				   : (g_flightResolutionMode == FLIGHT_RESOLUTION_480X360 ? 4 : 6);
	if (rebel_fighter) {
		if (!(features & (XVT_COCKPIT_FEATURE_LASER_POWER | XVT_COCKPIT_FEATURE_ENGINE_POWER |
						  XVT_COCKPIT_FEATURE_SHIELD_POWER)))
			return;
		engine -= shield;
		segments = 4;
		step = g_flightResolutionMode == FLIGHT_RESOLUTION_320X240
				   ? 2
				   : (g_flightResolutionMode == FLIGHT_RESOLUTION_480X360 ? 3 : 5);
		if (state->view.instrument_base != HUD_COCKPIT_INSTRUMENT_BASE_INDEX) {
			segments *= 2;
			engine *= 2;
			shield *= 2;
			laser *= 2;
		}
		has_shields = 1;
	} else {
		if (has_shields)
			engine = (uint16_t)(engine - shield + 2);
		if (beam_system)
			engine = (uint16_t)(engine - beam + 2);
		laser *= 3;
		shield *= 3;
		beam *= 3;
	}
	SetPowerGauge(&systems->engine_power, features & XVT_COCKPIT_FEATURE_ENGINE_POWER, engine,
				  rebel_fighter ? 2 * segments : segments, step, rebel_fighter);
	SetPowerGauge(&systems->laser_power, features & XVT_COCKPIT_FEATURE_LASER_POWER, laser, segments,
				  rebel_fighter ? 2 * step : step, rebel_fighter);
	SetPowerGauge(&systems->shield_power, has_shields && (features & XVT_COCKPIT_FEATURE_SHIELD_POWER),
				  shield, segments, rebel_fighter ? 2 * step : step, rebel_fighter);
	SetPowerGauge(&systems->beam_power,
				  !rebel_fighter && beam_system && (features & XVT_COCKPIT_FEATURE_BEAM_POWER), beam,
				  segments, step, rebel_fighter);
}

static void BuildFeatureCovers(XvtCockpitState* state, const ObjectRecord* object, const CraftData* craft,
							   int rebel_fighter) {
	XvtCockpitSystems* systems = &state->systems;
	int forward = state->view.hud_state == HUD_VIEW_FORWARD;
	int hud_only = state->view.hud_state == HUD_VIEW_HUD_ONLY;
	if (!forward && !hud_only)
		return;
	for (unsigned index = 0; index < 13; ++index) {
		unsigned feature = 1u << index;
		if (!(feature & craft->damageStats.installedHudFeatureMask))
			continue;
		int active = (feature & craft->damageStats.activeHudFeatureMask) != 0;
		if (forward && rebel_fighter && active)
			continue;
		if (hud_only &&
			(feature == XVT_COCKPIT_FEATURE_LASER_CHARGE || feature == XVT_COCKPIT_FEATURE_LASER_SELECTION ||
			 feature == XVT_COCKPIT_FEATURE_WARHEADS))
			continue;
		if (hud_only && rebel_fighter &&
			(feature == XVT_COCKPIT_FEATURE_BEAM || feature == XVT_COCKPIT_FEATURE_BEAM_POWER))
			continue;
		systems->feature_covers[index] =
			(XvtCockpitIndicator) { 1, (uint8_t)(!active && (hud_only || !rebel_fighter) ? 13 : 0), 0,
									XVT_COCKPIT_BEFORE_CRT };
	}
	if (!state->view.map_active && !rebel_fighter && object->objectType != CRAFT_SPECIES_TIE_FIGHTER) {
		if (!(craft->systemFlags & CRAFT_SUBSYSTEM_FLAG_SHIELDS))
			systems->unavailable_shields = (XvtCockpitIndicator) { 1, 0, 0, XVT_COCKPIT_BEFORE_CRT };
		if (!(craft->systemFlags & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM))
			systems->unavailable_beam[0] = systems->unavailable_beam[1] =
				(XvtCockpitIndicator) { 1, 0, 0, XVT_COCKPIT_BEFORE_CRT };
	}
}

static void BuildBasicReadouts(XvtCockpitState* state, const CraftData* craft) {
	if (craft->damageStats.activeHudFeatureMask & XVT_COCKPIT_FEATURE_SPEED)
		state->readouts.speed.visible = state->readouts.throttle.visible = 1;
	if (state->view.hud_state == HUD_VIEW_FORWARD)
		state->readouts.clock_minutes.visible = state->readouts.clock_seconds.visible = 1;
}

static void BuildLaserSlots(XvtCockpitState* state, const CraftData* craft, int rebel_fighter) {
	const PlayerData* player = &g_players[g_localPlayer];
	const ModelDef* model = &g_modelDefs[craft->modelIndex];
	unsigned count = craft->laserSlotCount;
	if (count > XVT_HUD_WEAPON_SLOTS)
		count = XVT_HUD_WEAPON_SLOTS;
	state->weapons.slot_count = (uint8_t)count;
	state->weapons.selected_bank = player->selectedWeaponBank;
	unsigned base = state->view.instrument_base;
	int cannons = (craft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_CANNONS) != 0;
	int charge_visible = state->view.hud_state == HUD_VIEW_FORWARD &&
						 (craft->damageStats.activeHudFeatureMask & XVT_COCKPIT_FEATURE_LASER_CHARGE) &&
						 (craft->damageStats.activeHudFeatureMask & XVT_COCKPIT_FEATURE_LASER_SELECTION);
	for (unsigned index = 0; index < count; ++index) {
		XvtCockpitWeaponSlot* slot = &state->weapons.slots[index];
		const HudElementLayout* layout = &g_hudElementLayouts[base + index + 3];
		if (layout->x + layout->y == 0 && base == HUD_COCKPIT_INSTRUMENT_BASE_INDEX)
			continue;
		slot->visible = 1;
		slot->bank = index > model->laserGroupLastSlot[0];
		slot->hud_slot = (uint8_t)index;
		slot->charge_visible = charge_visible;
		int charge = craft->weaponSlots[index].laserCharge;
		if (charge_visible && charge > 0 && cannons) {
			++charge;
			slot->charge_band = charge <= 64 ? 1 : 2;
			slot->empty_band = charge <= 64 ? 0 : 1;
			if (charge > 64)
				charge -= 64;
			slot->segments = (uint8_t)(charge / 6 > 10 ? 10 : charge / 6);
		}
		slot->charge_percent.visible =
			charge_visible && base != HUD_COCKPIT_INSTRUMENT_BASE_INDEX && layout->selector;

		unsigned ready = 0, selected = 0;
		if (charge > 0 && cannons) {
			if (player->selectedWeaponMode == 0 && player->selectedWeaponBank == slot->bank) {
				unsigned next = craft->laserState.nextSlot[slot->bank];
				switch (craft->laserState.linkMode[slot->bank]) {
					case 1:
						ready = next == index ? 3 : 1;
						break;
					case 2:
						ready = next == index || (count >= 4 && (int)next - (int)index == -2) ? 3 : 1;
						break;
					case 3:
						ready = 3;
						break;
					default:
						break;
				}
				selected = ready;
				if (ready == 3 && craft->laserState.fireCooldownTicks[slot->bank]) {
					ready = 2;
					selected = 5;
				}
			} else
				selected = 1;
		}
		if (slot->charge_band == 2)
			++selected;
		slot->ready = (uint8_t)ready;
		slot->selection_state = (uint8_t)selected;
		const HudElementLayout* selection = &g_hudElementLayouts[base + index + 11];
		slot->selection_visible = rebel_fighter && charge_visible && selection->x + selection->y != 0;
		slot->lock_visible = rebel_fighter;
		slot->locked = g_recorded.laser_lock[index];
	}
	unsigned lock = player->selectedWeaponMode == 0
						? (g_targetLockActive ? 4 : 0)
						: (player->currentTargetObjectIdx == -1 ? 1 : player->missileLockState + 1);
	state->weapons.lock_indicator = (XvtCockpitIndicator) { 1, (uint8_t)lock, 0, XVT_COCKPIT_BEFORE_CRT };
}

static void BuildLauncher(XvtCockpitState* state, const CraftData* craft, unsigned weapon_slot,
						  unsigned display_slot, unsigned bank, int rebel_fighter) {
	if (weapon_slot >= XVT_HUD_WEAPON_SLOTS)
		return;
	XvtCockpitLauncher* launcher = &state->weapons.launchers[display_slot];
	const PlayerData* player = &g_players[g_localPlayer];
	unsigned count = craft->warheadLauncherCount ? craft->weaponSlots[weapon_slot].ammoCount : 0;
	unsigned selection = 0;
	if (count && (craft->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER)) {
		selection = 1;
		if (player->selectedWeaponMode && player->selectedWeaponBank == bank) {
			unsigned flags = (uint8_t)craft->warheadLauncherFlags[bank];
			selection = (flags & 127) == 3 ? 2 : ((display_slot & 1) == (flags >> 7)) + 1;
		}
	}
	unsigned base = state->view.instrument_base;
	launcher->visible = 1;
	launcher->bank = (uint8_t)bank;
	launcher->weapon_slot = (uint8_t)weapon_slot;
	launcher->selection = (uint8_t)(rebel_fighter && selection == 2 && base == 0 ? 4 : selection);

	launcher->count.visible = !base || (count && g_hudElementLayouts[base + 27 + display_slot].selector);
}

static void BuildWarheadState(XvtCockpitState* state, const ObjectRecord* object, const CraftData* craft,
							  int rebel_fighter) {
	if (!(craft->damageStats.activeHudFeatureMask & XVT_COCKPIT_FEATURE_WARHEADS))
		return;
	const ModelDef* model = &g_modelDefs[g_objectTypeTable[object->objectType].modelIndex];
	if (!model->warheadLauncherSlotCount[0] && !model->warheadLauncherSlotCount[1])
		return;
	int missile_boat = g_objectTypeTable[object->objectType].modelIndex ==
					   g_objectTypeTable[CRAFT_SPECIES_MISSILE_BOAT].modelIndex;
	for (unsigned bank = 0; bank < (missile_boat ? 2u : 1u); ++bank) {
		BuildLauncher(state, craft, model->warheadLauncherFirstSlot[bank], bank * 2, bank, rebel_fighter);
		BuildLauncher(state, craft, model->warheadLauncherLastSlot[bank], bank * 2 + 1, bank, rebel_fighter);
		state->weapons.warheads[bank].visible = 1;
		state->weapons.warheads[bank].type = craft->warheadSlotTypeIds[bank];
		state->weapons.warheads[bank].selected = g_players[g_localPlayer].selectedWeaponMode &&
												 g_players[g_localPlayer].selectedWeaponBank == bank;
	}
}

static void BuildCockpitWarnings(XvtCockpitState* state, const CraftData* craft, int rebel_fighter) {
	if (state->view.hud_state != HUD_VIEW_FORWARD)
		return;
	XvtCockpitSystems* systems = &state->systems;
	if (rebel_fighter && g_hudElementLayouts[50].x + g_hudElementLayouts[50].y) {
		unsigned shield = (uint32_t)craft->shieldEnergy[0] + (uint32_t)craft->shieldEnergy[1];
		unsigned damage = craft->hullMax / 3 ? craft->hullDamage / (craft->hullMax / 3) : 2;
		unsigned flash = shield < 100 && damage == 2 ? (g_missionElapsedClock.subsecondTicks / 59) & 1 : 0;
		systems->critical_warning = (XvtCockpitIndicator) { 1, (uint8_t)flash, 0, XVT_COCKPIT_BEFORE_CRT };
	}
	if (state->view.map_active)
		return;
	unsigned base = state->view.instrument_base;
	if (!base) {
		systems->countermeasure_count.visible = g_hudElementLayouts[48].x + g_hudElementLayouts[48].y != 0;

		systems->countermeasure_active =
			(XvtCockpitIndicator) { 1, (uint8_t)craft->chaffActiveSeconds != 0, 0, XVT_COCKPIT_BEFORE_CRT };
		systems->countermeasure_active.visible = systems->countermeasure_count.visible;
	}
	if (rebel_fighter) {
		if ((systems->active_hud_features & XVT_COCKPIT_FEATURE_SHIELDS) &&
			g_hudElementLayouts[51].x + g_hudElementLayouts[51].y)
			systems->shield_distribution =
				(XvtCockpitIndicator) { 1, (uint8_t)craft->shieldDistribMode, 0, XVT_COCKPIT_BEFORE_CRT };
		if (g_hudElementLayouts[45].x)
			systems->sfoils =
				(XvtCockpitIndicator) { 1, (craft->sFoilState & 2) == 0, 0, XVT_COCKPIT_BEFORE_CRT };
	}
}

void XvtCockpitInstruments_Build(XvtCockpitState* state) {
	memset(&state->systems, 0, sizeof state->systems);
	memset(&state->weapons, 0, sizeof state->weapons);
	memset(&state->target, 0, sizeof state->target);
	memset(&state->radar, 0, sizeof state->radar);
	memset(&state->readouts, 0, sizeof state->readouts);
	const PlayerData* player = &g_players[g_localPlayer];
	unsigned slot = (uint16_t)player->objectIndex;
	if (!g_objectTable || slot >= (unsigned)g_regionMainObjectSlotEnd)
		return;
	const ObjectRecord* object = &g_objectTable[slot];
	const MobileObject* mobile = object->mobj;
	const CraftData* craft = mobile ? mobile->pCraft : NULL;
	if (!craft)
		return;
	XvtCockpitSystems* systems = &state->systems;
	systems->installed = craft->systemFlags;
	systems->working = craft->workingSubsystems;
	systems->active_hud_features = craft->damageStats.activeHudFeatureMask;
	systems->installed_hud_features = craft->damageStats.installedHudFeatureMask;
	int rebel_fighter = IsRebelFighter(object);
	state->view.rebel_fighter = (uint8_t)rebel_fighter;
	state->view.laser_slots =
		craft->laserSlotCount > XVT_HUD_WEAPON_SLOTS ? XVT_HUD_WEAPON_SLOTS : (uint8_t)craft->laserSlotCount;
	BuildFeatureCovers(state, object, craft, rebel_fighter);
	if (!state->view.instruments_visible)
		return;
	int forward = state->view.hud_state == HUD_VIEW_FORWARD;
	int hud_only = state->view.hud_state == HUD_VIEW_HUD_ONLY;
	BuildCockpitWarnings(state, craft, rebel_fighter);
	if (!state->view.map_active && (forward || hud_only)) {
		BuildLaserSlots(state, craft, rebel_fighter);
		BuildWarheadState(state, object, craft, rebel_fighter);
		BuildShieldState(state, craft);
		BuildBeamState(state, craft);
		BuildPowerState(state, craft, rebel_fighter);
		BuildBasicReadouts(state, craft);
		state->radar = g_recorded.radar;
		int radar_visible = (systems->active_hud_features & XVT_COCKPIT_FEATURE_FORE_RADAR) &&
							(systems->active_hud_features & XVT_COCKPIT_FEATURE_AFT_RADAR);
		state->radar.visible[0] = state->radar.visible[1] = radar_visible;
		for (unsigned index = 0; index < 4; ++index)
			systems->threats[index] =
				(XvtCockpitIndicator) { 1, g_recorded.threats[index], 0, XVT_COCKPIT_BEFORE_CRT };
	}
}
