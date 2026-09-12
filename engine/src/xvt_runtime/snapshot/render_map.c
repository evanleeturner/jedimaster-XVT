#include "xvt_runtime/snapshot/render_map.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/flight_map.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/trig2.h"
#include "xvt/render/flight_sw.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt_runtime/snapshot/render_hud.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t PlanarDistance(uint32_t a, uint32_t b) {
	uint32_t max = a > b ? a : b, min = a > b ? b : a, index = 0;
	if (max == min)
		index = 256;
	else if (max) {
		uint32_t d = max, n = min;
		if (!(d & 0xff000000u)) {
			d <<= 8;
			n <<= 8;
			if (!(d & 0xff000000u)) {
				d <<= 8;
				n <<= 8;
			}
		}
		index = (uint16_t)(n / (d >> 16)) >> 8;
	}
	uint32_t scale = g_squarerootable[index];
	return max + (max >> 16) * scale + (uint16_t)((((max & 65535) * scale) + 32768) >> 16);
}

static uint32_t Magnitude(int32_t value) { return value < 0 ? 0u - (uint32_t)value : (uint32_t)value; }

static int OtherPlayerBox(const PlayerData* p, const ObjectRecord* o, unsigned slot) {
	const CraftData* c = o->mobj ? o->mobj->pCraft : NULL;
	if (!c || o->playerOwnerIdx == -1)
		return 0;
	unsigned iff = (uint16_t)p->playerIff, fg = o->flightGroupIdx;
	if (!g_flightMissionState.locatePlayersEnabled && iff < 10 && !c->iffVisibility[iff] && fg < 48) {
		unsigned team = g_missionFlightGroups[fg].fg.team;
		if (team < 10 && team != iff && !g_missionTeams[iff].allies[team])
			return 0;
	}
	return !(slot < (unsigned)g_activeRegionCraftObjectSlotEnd &&
			 (c->workingSubsystems & CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM) && c->beamActive &&
			 c->beamTypeId == BEAM_TYPE_DECOY && c->beamTimer);
}

static int Extent(const ObjectRecord* o) {
	const CraftData* c = o->mobj ? o->mobj->pCraft : NULL;
	if (!c)
		return g_modelTypeTable[o->objectType].maxBoundsExtent;
	const ModelDef* d = &g_modelDefs[c->modelIndex];
	return (int)((uint32_t)((d->boundSizeX + d->boundSizeY + d->boundSizeZ) / 3) << d->boundSizeShift);
}

static void Endpoint(XvtSnapMap* map, unsigned slot) {
	const ObjectRecord* o = &g_objectTable[slot];
	if (!o->mobj || !o->mobj->pCraft)
		return;
	const CraftData* craft = o->mobj->pCraft;
	uint16_t ref;
	if (slot < (unsigned)g_craftDataPoolCapacity)
		ref = craft->aiController.targetObjIdx;
	else
		memcpy(&ref, &craft->modelIndex, sizeof ref);
	if (ref == UINT16_MAX)
		return;
	if (ref < 0x8000) {
		if (ref >= g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount)
			return;
		map->order_endpoint[0] = g_objectTable[ref].world_x;
		map->order_endpoint[1] = g_objectTable[ref].world_y;
		map->order_endpoint[2] = g_objectTable[ref].world_z;
	} else {
		unsigned fg = o->flightGroupIdx;
		if (fg >= 48)
			return;
		if (ref == 0x8000)
			ref = g_missionFgStats[fg].currentMissionPointRef;
		unsigned i = (uint16_t)(ref - 0x8000);
		if (i >= sizeof g_missionFlightGroups[fg].fg.missionPointX /
					 sizeof g_missionFlightGroups[fg].fg.missionPointX[0])
			return;
		map->order_endpoint[0] = g_missionFlightGroups[fg].fg.missionPointX[i] * 256;
		map->order_endpoint[1] = -g_missionFlightGroups[fg].fg.missionPointY[i] * 256;
		map->order_endpoint[2] = g_missionFlightGroups[fg].fg.missionPointZ[i] * 256;
	}
	map->endpoint_valid = 1;
}

static void Label(XvtSnapMap* map, XvtSnapMapObject* m, const ObjectRecord* o) {
	char text[96] = { 0 };
	unsigned fg = o->flightGroupIdx;
	if (fg >= 48 || (!o->mobj && o->genusId == CRAFT_GENUS_MINE))
		return;
	const CraftData* craft = o->mobj ? o->mobj->pCraft : NULL;
	if (o->mobj && (o->mobj->state != 0 || !craft))
		return;
	const XvtFlightGroup* group = &g_missionFlightGroups[fg].fg;
	unsigned number = 0;
	if (craft && group->disableWaveNumbering != 1 &&
		(group->globalUnit || group->numberOfCraft != 1 || group->numberOfWaves))
		number = (uint16_t)craft->craftIndexInGroup;
	if (number > 999)
		number = 999;
	if (number)
		snprintf(text, sizeof text, "%.*s %u", (int)sizeof group->name, group->name, number);
	else
		snprintf(text, sizeof text, "%.*s", (int)sizeof group->name, group->name);
	size_t length = strlen(text) + 1;
	if (length > XVT_SNAP_LABEL_BYTES - map->label_bytes)
		return;
	m->label_offset = map->label_bytes;
	memcpy(map->labels + map->label_bytes, text, length);
	map->label_bytes += (uint32_t)length;
	m->label_visible = text[0] != 0;
	static const unsigned colors[6] = { 62, 54, 50, 58, 54, 212 };
	m->label_color_argb = XvtRenderDraw_Color(colors[m->effective_iff < 6 ? m->effective_iff : 3]);
}

void XvtRenderMap_Capture(XvtSnapMap* map, const XvtSnapObject* objects, unsigned count) {
	memset(map, 0, sizeof *map);
	const PlayerData* p = &g_players[g_localPlayer];
	if (!p->mapCameraState)
		return;
	map->active = 1;
	map->grid_z = -65536;
	map->font_asset_id = XvtRenderAssets_ImageId(
		g_flightResolutionMode == FLIGHT_RESOLUTION_640X480 ? g_flightFontSmallSw : g_flightFontMicroSw);
	map->target = (XvtSnapObjectId) { UINT16_MAX, 0 };
	const uint8_t* frames = g_flightIcons640FrameByObjectType;
	if (g_flightIconResourcePath == g_flightMapIcons320x240ResourcePath)
		frames = g_flightMapIcons320x240FrameByObjectType;
	else if (g_flightIconResourcePath == g_flightMapIcons480x360ResourcePath)
		frames = g_flightMapIcons480x360FrameByObjectType;
	for (unsigned i = 0; i < count; ++i) {
		const XvtSnapObject* snap = &objects[i];
		unsigned slot = snap->id.slot, genus = snap->genus;
		int box =
			genus <= CRAFT_GENUS_PLATFORM || (snap->slot_class == XVT_SLOT_STATIC &&
											  genus >= CRAFT_GENUS_MINE && genus <= CRAFT_GENUS_SATELLITE);
		int sphere = genus == CRAFT_GENUS_PLAYER_PROJECTILE || genus == CRAFT_GENUS_OTHER_PROJECTILE ||
					 genus == CRAFT_GENUS_SMALL_DEBRIS || genus == CRAFT_GENUS_EXPLOSION;
		if (snap->slot_class == XVT_SLOT_STATIC
				? !box
				: (slot >= (unsigned)g_explosionObjectSlotEnd || (!box && !sphere)))
			continue;
		const ObjectRecord* o = &g_objectTable[slot];
		XvtSnapMapObject* m = &map->objects[map->object_count++];
		m->object_index = (uint16_t)i;
		m->render_kind = (uint8_t)(box ? XVT_MAP_MODEL_OR_ICON : XVT_MAP_EFFECT);
		m->cull_kind = (uint8_t)box;
		m->box_extent = Extent(o);
		m->effective_iff =
			o->mobj ? (uint8_t)o->mobj->iff
					: (o->flightGroupIdx < 48 ? g_missionFlightGroups[o->flightGroupIdx].fg.iff : 0);
		unsigned group = m->effective_iff == 0                                                       ? 0
						 : m->effective_iff == 2                                                     ? 3
						 : (m->effective_iff == 1 || m->effective_iff == 3 || m->effective_iff == 4) ? 2
																									 : 1;
		unsigned base_frame = o->objectType < 106 ? frames[o->objectType] : 19;
		const uint8_t *widths = g_flightIcons640WidthByFrame, *heights = g_flightIcons640HeightByFrame;
		if (frames == g_flightMapIcons320x240FrameByObjectType) {
			widths = g_flightMapIcons320x240WidthByFrame;
			heights = g_flightMapIcons320x240HeightByFrame;
		} else if (frames == g_flightMapIcons480x360FrameByObjectType) {
			widths = g_flightMapIcons480x360WidthByFrame;
			heights = g_flightMapIcons480x360HeightByFrame;
		}
		m->icon_width = widths[base_frame];
		m->icon_height = heights[base_frame];
		unsigned frame = base_frame + g_flightIconFrameCount * group / 4;
		if (g_flightIconFrames && frame < (unsigned)g_flightIconFrameCount) {
			uint32_t actual;
			uint64_t id = XvtRenderAssets_MapIconFrame(frame, &actual);
			map->icon_asset_id = id;
			m->icon_frame = (uint16_t)actual;
		}
		m->movement_visible = o->mobj && o->mobj->state == 0;
		m->move_x = snap->move_q15[0];
		m->move_y = snap->move_q15[1];
		if (snap->orient_dirty) {
			int16_t b = (int16_t)(0u - snap->yaw), a = (int16_t)(0xc000u - snap->pitch);
			int16_t sn = (int16_t)(sin(b * 0.000095873722f) * 32767),
					cs = (int16_t)(cos(b * 0.000095873722f) * 32767),
					cp = (int16_t)(cos(a * 0.000095873722f) * 32767);
			m->move_x = (int16_t)-((-sn * (int)cp) >> 15);
			m->move_y = (int16_t)-((cs * (int)cp) >> 15);
		}
		m->box_visible = p->targetBoxEnabled && !g_replayViewMode &&
						 (slot == p->viewState.cameraFocusObjIdx ||
						  slot == (unsigned)p->currentTargetObjectIdx || (box && OtherPlayerBox(p, o, slot)));
		static const uint8_t line_colors[6] = { 63, 55, 51, 59, 55, 59 };
		m->box_color = slot == p->viewState.cameraFocusObjIdx ? 47
					   : slot == (unsigned)p->currentTargetObjectIdx
						   ? 59
						   : line_colors[m->effective_iff < 6 ? m->effective_iff : 3];
		m->line_color_argb = XvtRenderDraw_Color(line_colors[m->effective_iff < 6 ? m->effective_iff : 3]);
		static const unsigned dark_colors[6] = { 61, 53, 49, 57, 53, 213 };
		m->label_color_argb = XvtRenderDraw_Color(dark_colors[m->effective_iff < 6 ? m->effective_iff : 3]);
		m->overlay_visible = genus != CRAFT_GENUS_SMALL_DEBRIS && genus != CRAFT_GENUS_EXPLOSION &&
							 (slot < (unsigned)g_craftDataPoolCapacity || !o->mobj || o->mobj->pCraft);
		if (m->overlay_visible)
			Label(map, m, o);
		unsigned focus = p->viewState.cameraFocusObjIdx;
		if (focus < (unsigned)(g_regionMainObjectSlotEnd + g_regionStaticObjectSlotCount)) {
			uint32_t dx = Magnitude((int32_t)((uint32_t)o->world_x - (uint32_t)g_objectTable[focus].world_x)),
					 dy = Magnitude((int32_t)((uint32_t)o->world_y - (uint32_t)g_objectTable[focus].world_y)),
					 dz = Magnitude((int32_t)((uint32_t)o->world_z - (uint32_t)g_objectTable[focus].world_z));
			uint32_t range = (PlanarDistance(PlanarDistance(dx, dy), dz) * 161) >> 16;
			m->range_value = (uint16_t)(range > 9999 ? 9999 : range);
			m->range_visible = m->overlay_visible && genus != CRAFT_GENUS_PLAYER_PROJECTILE &&
							   genus != CRAFT_GENUS_OTHER_PROJECTILE;
		}
		if (slot == (unsigned)p->currentTargetObjectIdx) {
			map->target = snap->id;
			Endpoint(map, slot);
		}
	}
}
