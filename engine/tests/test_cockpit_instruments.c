/* Checks the cockpit instruments (xvt_runtime/snapshot/cockpit_instruments.h) against the promises in its
 * header: Build clearing its parts and leaving them cleared without a craft, which parts it builds in which
 * view and with instruments shown or hidden, the recorded threats, laser locks and radar blips (their
 * scope, count rule, ignored indices, the target marker and positions relative to the scope's anchor),
 * CompleteRadar's coverage, and BeginUpdate forgetting the recorded values for the local player only. The
 * test builds its own world (one craft in main slot 1, flown by the local player in seat 0) and sets the
 * HUD layout and model entries it reads; every case starts from that world and BeginUpdate.
 *
 * Not checked here: the values Build derives for shields, beam, power gauges, launchers, warheads, laser
 * charge and the compact instruments, which the header does not describe beyond their source. */
#include "test_assert.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/render/flight_palette.h"
#include "xvt_runtime/snapshot/cockpit_instruments.h"
#include "xvt_runtime/snapshot/cockpit_readouts.h"

#include <string.h>

enum { LOCAL = 0, OTHER_PLAYER = 1, SLOT = 1, LASERS = 4 };

static ObjectRecord g_testObjects[2];
static MobileObject g_testMobiles[2];
static CraftData g_testCraft;
static XvtCockpitState g_state;
static HudRadarBlipPoint g_foreDrawn[48], g_aftDrawn[48];

/* The local player flies a TIE Interceptor in main slot 1 with four lasers, every HUD feature installed
 * and only the two radars active. Each laser and both radar scopes have a place in the cockpit layout. */
static void Start(void) {
	memset(g_testObjects, 0, sizeof g_testObjects);
	memset(g_testMobiles, 0, sizeof g_testMobiles);
	memset(&g_testCraft, 0, sizeof g_testCraft);
	g_objectTable = g_testObjects;
	g_regionMainObjectSlotEnd = 2;
	g_testObjects[SLOT].objectType = CRAFT_SPECIES_TIE_INTERCEPTOR;
	g_testObjects[SLOT].objectSignature = 0x0111;
	g_testObjects[SLOT].mobj = &g_testMobiles[SLOT];
	g_testMobiles[SLOT].pCraft = &g_testCraft;
	g_testCraft.modelIndex = 0;
	memset(&g_modelDefs[0], 0, sizeof g_modelDefs[0]);
	g_testCraft.systemFlags = 0x15;
	g_testCraft.workingSubsystems = 0x05;
	g_testCraft.laserSlotCount = LASERS;
	g_testCraft.damageStats.installedHudFeatureMask = 0x1FFF;
	g_testCraft.damageStats.activeHudFeatureMask =
		XVT_COCKPIT_FEATURE_FORE_RADAR | XVT_COCKPIT_FEATURE_AFT_RADAR;

	memset(g_players, 0, sizeof g_players);
	for (int i = 0; i < 8; ++i)
		g_players[i].objectIndex = -1;
	g_localPlayer = LOCAL;
	g_players[LOCAL].objectIndex = SLOT;
	g_players[LOCAL].currentTargetObjectIdx = -1;
	memset(g_playerFlightTransientTimers, 0, sizeof g_playerFlightTransientTimers);

	memset(g_hudElementLayouts, 0, sizeof g_hudElementLayouts);
	g_hudElementLayouts[0] = (HudElementLayout) { .x = 200, .y = 300 };
	g_hudElementLayouts[1] = (HudElementLayout) { .x = 400, .y = 300 };
	for (unsigned laser = 0; laser < LASERS; ++laser)
		g_hudElementLayouts[3 + laser] = (HudElementLayout) { .x = (uint16_t)(10 + laser), .y = 20 };

	memset(g_foreDrawn, 0, sizeof g_foreDrawn);
	memset(g_aftDrawn, 0, sizeof g_aftDrawn);
	g_radarForeDrawBlips = g_foreDrawn;
	g_radarAftDrawBlips = g_aftDrawn;
	g_flightBytesPerPixel = 1;

	XvtCockpitInstruments_BeginUpdate(LOCAL);
}

/* A cockpit view: the forward view with the map closed and instruments shown, unless changed. */
static XvtCockpitState* View(unsigned hud_state, int instruments_visible, int map_active) {
	memset(&g_state, 0, sizeof g_state);
	g_state.view.hud_state = (uint16_t)hud_state;
	g_state.view.instruments_visible = (uint8_t)instruments_visible;
	g_state.view.map_active = (uint8_t)map_active;
	g_state.view.instrument_base = HUD_COCKPIT_INSTRUMENT_BASE_INDEX;
	return &g_state;
}

static const XvtCockpitState* Built(unsigned hud_state, int instruments_visible, int map_active) {
	XvtCockpitInstruments_Build(View(hud_state, instruments_visible, map_active));
	return &g_state;
}

static const XvtCockpitState* Forward(void) { return Built(HUD_VIEW_FORWARD, 1, 0); }

static int AllZero(const void* data, size_t size) {
	const unsigned char* bytes = data;
	for (size_t index = 0; index < size; ++index)
		if (bytes[index])
			return 0;
	return 1;
}

static int NoThreats(const XvtCockpitSystems* systems) {
	return AllZero(systems->threats, sizeof systems->threats);
}

/* Builds over a state whose five parts hold junk, and reports whether all five came back cleared. */
static int BuildClears(void) {
	XvtCockpitState* state = View(HUD_VIEW_FORWARD, 1, 0);
	memset(&state->systems, 0xA5, sizeof state->systems);
	memset(&state->weapons, 0xA5, sizeof state->weapons);
	memset(&state->target, 0xA5, sizeof state->target);
	memset(&state->radar, 0xA5, sizeof state->radar);
	memset(&state->readouts, 0xA5, sizeof state->readouts);
	XvtCockpitInstruments_Build(state);
	return AllZero(&state->systems, sizeof state->systems) &&
		   AllZero(&state->weapons, sizeof state->weapons) && AllZero(&state->target, sizeof state->target) &&
		   AllZero(&state->radar, sizeof state->radar) && AllZero(&state->readouts, sizeof state->readouts);
}

static void CheckNoCraftLeavesCleared(void) {
	Start();
	XvtCockpitInstruments_RecordThreats(1, 2, 3, 4);
	g_players[LOCAL].objectIndex = -1;
	XVT_ASSERT_INT_EQ(BuildClears(), 1);
	/* A slot past the main slots holds no player craft. */
	g_players[LOCAL].objectIndex = 2;
	XVT_ASSERT_INT_EQ(BuildClears(), 1);
	/* An object with no mobile record, or a mobile record with no craft. */
	g_players[LOCAL].objectIndex = SLOT;
	g_testObjects[SLOT].mobj = NULL;
	XVT_ASSERT_INT_EQ(BuildClears(), 1);
	g_testObjects[SLOT].mobj = &g_testMobiles[SLOT];
	g_testMobiles[SLOT].pCraft = NULL;
	XVT_ASSERT_INT_EQ(BuildClears(), 1);
	/* No object table at all. */
	g_testMobiles[SLOT].pCraft = &g_testCraft;
	g_objectTable = NULL;
	XVT_ASSERT_INT_EQ(BuildClears(), 1);
	/* With the craft back, Build fills its parts again. */
	g_objectTable = g_testObjects;
	XVT_ASSERT_INT_EQ(BuildClears(), 0);
}

static void CheckSystemMasksWhateverVisibility(void) {
	Start();
	XvtCockpitSystems shown = Built(HUD_VIEW_FORWARD, 1, 0)->systems;
	XvtCockpitSystems hidden = Built(HUD_VIEW_FORWARD, 0, 0)->systems;
	XvtCockpitSystems elsewhere = Built(HUD_VIEW_TARGET_CAMERA, 0, 1)->systems;
	XVT_ASSERT_TRUE(shown.installed != 0 && shown.working != 0);
	XVT_ASSERT_TRUE(shown.hud_features != 0 && shown.installed_hud_features != 0);
	XVT_ASSERT_INT_EQ(hidden.installed, shown.installed);
	XVT_ASSERT_INT_EQ(hidden.working, shown.working);
	XVT_ASSERT_INT_EQ(hidden.hud_features, shown.hud_features);
	XVT_ASSERT_INT_EQ(hidden.installed_hud_features, shown.installed_hud_features);
	XVT_ASSERT_INT_EQ(elsewhere.working, shown.working);
	XVT_ASSERT_INT_EQ(elsewhere.installed_hud_features, shown.installed_hud_features);

	/* They come from the craft: a damaged subsystem changes the working mask. */
	g_testCraft.workingSubsystems = 0x01;
	XVT_ASSERT_TRUE(Built(HUD_VIEW_FORWARD, 0, 0)->systems.working != shown.working);
}

static void CheckFeatureCovers(void) {
	Start();
	XvtCockpitIndicator shown[13], hidden[13];
	memcpy(shown, Built(HUD_VIEW_FORWARD, 1, 0)->systems.feature_covers, sizeof shown);
	/* Every feature is installed and most are inactive, so this cockpit has covers to show. */
	XVT_ASSERT_INT_EQ(AllZero(shown, sizeof shown), 0);

	/* In the forward view they are built whether or not instruments show, map open or closed. */
	memcpy(hidden, Built(HUD_VIEW_FORWARD, 0, 0)->systems.feature_covers, sizeof hidden);
	XVT_ASSERT_INT_EQ(memcmp(hidden, shown, sizeof shown), 0);
	memcpy(hidden, Built(HUD_VIEW_FORWARD, 0, 1)->systems.feature_covers, sizeof hidden);
	XVT_ASSERT_INT_EQ(memcmp(hidden, shown, sizeof shown), 0);

	/* The same holds in the HUD-only view. */
	memcpy(shown, Built(HUD_VIEW_HUD_ONLY, 1, 0)->systems.feature_covers, sizeof shown);
	XVT_ASSERT_INT_EQ(AllZero(shown, sizeof shown), 0);
	memcpy(hidden, Built(HUD_VIEW_HUD_ONLY, 0, 0)->systems.feature_covers, sizeof hidden);
	XVT_ASSERT_INT_EQ(memcmp(hidden, shown, sizeof shown), 0);

	/* In any other view there are none. */
	const XvtCockpitState* state = Built(HUD_VIEW_TARGET_CAMERA, 1, 0);
	XVT_ASSERT_INT_EQ(AllZero(state->systems.feature_covers, sizeof state->systems.feature_covers), 1);
	state = Built(HUD_VIEW_CRAFT_LIST, 0, 0);
	XVT_ASSERT_INT_EQ(AllZero(state->systems.feature_covers, sizeof state->systems.feature_covers), 1);
}

static void CheckRestNeedsVisibleInstruments(void) {
	Start();
	XvtCockpitInstruments_RecordThreats(1, 2, 3, 4);
	XvtCockpitInstruments_RecordRadar(0, 1, 0, 210, 310, 5);
	XvtCockpitInstruments_RecordLaserLock(0, 2);

	/* Forward, map closed, instruments shown: weapons, radar and threats are filled. */
	const XvtCockpitState* state = Forward();
	XVT_ASSERT_INT_EQ(NoThreats(&state->systems), 0);
	XVT_ASSERT_INT_EQ(AllZero(&state->weapons, sizeof state->weapons), 0);
	XVT_ASSERT_INT_EQ(AllZero(&state->radar, sizeof state->radar), 0);

	/* Instruments hidden: none of them. */
	state = Built(HUD_VIEW_FORWARD, 0, 0);
	XVT_ASSERT_INT_EQ(NoThreats(&state->systems), 1);
	XVT_ASSERT_INT_EQ(AllZero(&state->weapons, sizeof state->weapons), 1);
	XVT_ASSERT_INT_EQ(AllZero(&state->radar, sizeof state->radar), 1);
	XVT_ASSERT_INT_EQ(AllZero(&state->readouts, sizeof state->readouts), 1);

	/* Shown, but with the map open or in another view: none of them either. */
	state = Built(HUD_VIEW_FORWARD, 1, 1);
	XVT_ASSERT_INT_EQ(NoThreats(&state->systems), 1);
	XVT_ASSERT_INT_EQ(AllZero(&state->weapons, sizeof state->weapons), 1);
	XVT_ASSERT_INT_EQ(AllZero(&state->radar, sizeof state->radar), 1);
	state = Built(HUD_VIEW_TARGET_CAMERA, 1, 0);
	XVT_ASSERT_INT_EQ(NoThreats(&state->systems), 1);
	XVT_ASSERT_INT_EQ(AllZero(&state->weapons, sizeof state->weapons), 1);
	XVT_ASSERT_INT_EQ(AllZero(&state->radar, sizeof state->radar), 1);

	/* The HUD-only view is one of the cockpit views. */
	state = Built(HUD_VIEW_HUD_ONLY, 1, 0);
	XVT_ASSERT_INT_EQ(NoThreats(&state->systems), 0);
	XVT_ASSERT_INT_EQ(AllZero(&state->radar, sizeof state->radar), 0);
}

static void CheckThreats(void) {
	Start();
	XvtCockpitInstruments_RecordThreats(1, 2, 3, 4);
	const XvtCockpitSystems* systems = &Forward()->systems;
	XVT_ASSERT_INT_EQ(systems->threats[0].state, 1);
	XVT_ASSERT_INT_EQ(systems->threats[1].state, 2);
	XVT_ASSERT_INT_EQ(systems->threats[2].state, 3);
	XVT_ASSERT_INT_EQ(systems->threats[3].state, 4);
}

static void CheckLaserLocks(void) {
	Start();
	XvtCockpitInstruments_RecordThreats(1, 2, 3, 4);
	for (unsigned slot = 0; slot < LASERS; ++slot)
		XvtCockpitInstruments_RecordLaserLock(slot, slot + 1);
	/* A slot from XVT_HUD_WEAPON_SLOTS up is ignored; nothing else recorded changes. */
	XvtCockpitInstruments_RecordLaserLock(XVT_HUD_WEAPON_SLOTS, 9);
	const XvtCockpitState* state = Forward();
	unsigned shown = 0;
	for (unsigned slot = 0; slot < LASERS; ++slot)
		if (state->weapons.slots[slot].visible) {
			XVT_ASSERT_INT_EQ(state->weapons.slots[slot].locked, slot + 1);
			++shown;
		}
	/* The craft's lasers all have a place in the layout, so they show. */
	XVT_ASSERT_TRUE(shown > 0);
	XVT_ASSERT_INT_EQ(state->systems.threats[0].state, 1);
}

static void CheckViewLaserSlots(void) {
	Start();
	XVT_ASSERT_INT_EQ(Built(HUD_VIEW_FORWARD, 1, 0)->view.laser_slots, LASERS);
	XVT_ASSERT_INT_EQ(Built(HUD_VIEW_FORWARD, 0, 0)->view.laser_slots, LASERS);
	XVT_ASSERT_INT_EQ(Built(HUD_VIEW_TARGET_CAMERA, 0, 1)->view.laser_slots, LASERS);
}

/* The scope that holds blips after recording on the fore scope only. */
static unsigned ForeSide(const XvtCockpitRadar* radar) {
	XVT_ASSERT_TRUE((radar->count[0] != 0) != (radar->count[1] != 0));
	return radar->count[0] ? 0 : 1;
}

static void CheckRecordRadar(void) {
	Start();
	XvtCockpitInstruments_RecordRadar(0, 1, 2, 250, 360, 5);
	const XvtCockpitRadar* radar = &Forward()->radar;
	unsigned fore = ForeSide(radar), aft = 1 - fore;
	/* The count becomes index + 1; the other scope is untouched. */
	XVT_ASSERT_INT_EQ(radar->count[fore], 3);
	XVT_ASSERT_INT_EQ(radar->count[aft], 0);
	XvtSnapRadarBlip first = radar->blips[fore][2];

	/* Positions are relative to the scope's anchor: moving the point moves the blip by as much. */
	XvtCockpitInstruments_RecordRadar(0, 1, 2, 255, 363, 5);
	radar = &Forward()->radar;
	XVT_ASSERT_INT_EQ(radar->blips[fore][2].x, first.x + 5);
	XVT_ASSERT_INT_EQ(radar->blips[fore][2].y, first.y + 3);

	/* The aft scope counts its own blips. */
	XvtCockpitInstruments_RecordRadar(0, 0, 0, 410, 310, 5);
	radar = &Forward()->radar;
	XVT_ASSERT_INT_EQ(radar->count[aft], 1);
	XVT_ASSERT_INT_EQ(radar->count[fore], 3);

	/* Indices outside 0 to 47 are ignored. */
	XvtSnapRadarBlip aft_first;
	memcpy(&aft_first, &radar->blips[aft][0], sizeof aft_first);
	XvtCockpitInstruments_RecordRadar(0, 1, 48, 1, 1, 6);
	XvtCockpitInstruments_RecordRadar(0, 1, -1, 1, 1, 6);
	XvtCockpitInstruments_RecordRadar(0, 0, 48, 1, 1, 6);
	radar = &Forward()->radar;
	XVT_ASSERT_INT_EQ(radar->count[fore], 3);
	XVT_ASSERT_INT_EQ(radar->count[aft], 1);
	XVT_ASSERT_INT_EQ(memcmp(&radar->blips[aft][0], &aft_first, sizeof aft_first), 0);
}

static void CheckRadarLastIndex(void) {
	Start();
	XvtCockpitInstruments_RecordRadar(0, 1, 0, 250, 360, 5);
	XvtCockpitInstruments_RecordRadar(0, 1, 47, 250, 360, 5);
	const XvtCockpitRadar* radar = &Forward()->radar;
	unsigned fore = ForeSide(radar);
	/* Index 47 is stored but leaves the count at 47. */
	XVT_ASSERT_INT_EQ(radar->count[fore], 47);
	XVT_ASSERT_INT_EQ(radar->blips[fore][47].x, radar->blips[fore][0].x);
	XVT_ASSERT_INT_EQ(radar->blips[fore][47].y, radar->blips[fore][0].y);
	XvtCockpitInstruments_RecordRadar(0, 1, 46, 250, 360, 5);
	XVT_ASSERT_INT_EQ(Forward()->radar.count[fore], 47);
}

static void CheckRadarTargetMarker(void) {
	Start();
	g_players[LOCAL].currentTargetObjectIdx = SLOT;
	/* A blip on another object leaves the marker alone. */
	XvtCockpitInstruments_RecordRadar(0, 1, 1, 250, 360, 5);
	XVT_ASSERT_INT_EQ(Forward()->radar.marker_visible, 0);

	XvtCockpitInstruments_RecordRadar(SLOT, 1, 4, 260, 370, 5);
	const XvtCockpitRadar* radar = &Forward()->radar;
	unsigned fore = ForeSide(radar);
	XVT_ASSERT_INT_EQ(radar->marker_visible, 1);
	XVT_ASSERT_INT_EQ(radar->marker_side, fore);
	XVT_ASSERT_INT_EQ(radar->marker_x, radar->blips[fore][4].x);
	XVT_ASSERT_INT_EQ(radar->marker_y, radar->blips[fore][4].y);
}

static void CheckCompleteRadar(void) {
	Start();
	for (unsigned index = 0; index < 48; ++index) {
		g_foreDrawn[index].color = (uint16_t)(index + 5);
		g_aftDrawn[index].color = (uint16_t)(index * 4);
	}
	for (int index = 0; index < 3; ++index)
		XvtCockpitInstruments_RecordRadar(0, 1, index, 250, 360, 5);

	/* At one byte per pixel, the low two color bits of each counted blip's drawn point. */
	XvtCockpitInstruments_CompleteRadar();
	const XvtCockpitRadar* radar = &Forward()->radar;
	unsigned fore = ForeSide(radar), aft = 1 - fore;
	for (unsigned index = 0; index < 3; ++index)
		XVT_ASSERT_INT_EQ(radar->coverage[fore][index], g_foreDrawn[index].color & 3);
	XVT_ASSERT_INT_EQ(radar->coverage[fore][3], 0);
	XVT_ASSERT_INT_EQ(AllZero(radar->coverage[aft], sizeof radar->coverage[aft]), 1);

	/* Otherwise, whether the point was drawn. */
	g_flightBytesPerPixel = 2;
	g_foreDrawn[1].color = 0;
	XvtCockpitInstruments_CompleteRadar();
	radar = &Forward()->radar;
	XVT_ASSERT_INT_EQ(radar->coverage[fore][0], 1);
	XVT_ASSERT_INT_EQ(radar->coverage[fore][1], 0);
	XVT_ASSERT_INT_EQ(radar->coverage[fore][2], 1);
}

static void CheckBeginUpdate(void) {
	Start();
	XvtCockpitInstruments_RecordThreats(1, 2, 3, 4);
	XvtCockpitInstruments_RecordRadar(0, 1, 0, 250, 360, 5);
	XvtCockpitInstruments_RecordLaserLock(0, 2);
	XvtCockpitReadouts_BeginTarget(0);

	/* Another player's update is ignored. */
	XvtCockpitInstruments_BeginUpdate(OTHER_PLAYER);
	const XvtCockpitState* state = Forward();
	XVT_ASSERT_INT_EQ(state->systems.threats[3].state, 4);
	XVT_ASSERT_INT_EQ(AllZero(state->radar.count, sizeof state->radar.count), 0);
	XVT_ASSERT_INT_EQ(state->weapons.slots[0].locked, 2);
	static XvtCockpitState readouts;
	memset(&readouts, 0, sizeof readouts);
	readouts.view.instruments_visible = 1;
	XvtCockpitReadouts_CopyState(&readouts);
	XVT_ASSERT_INT_EQ(readouts.target.visible, 1);

	/* The local player's update forgets the locks, threats and radar, and begins a readouts update. */
	XvtCockpitInstruments_BeginUpdate(LOCAL);
	state = Forward();
	for (unsigned threat = 0; threat < 4; ++threat)
		XVT_ASSERT_INT_EQ(state->systems.threats[threat].state, 0);
	XVT_ASSERT_INT_EQ(AllZero(&state->radar.count, sizeof state->radar.count), 1);
	XVT_ASSERT_INT_EQ(state->weapons.slots[0].locked, 0);
	memset(&readouts, 0, sizeof readouts);
	readouts.view.instruments_visible = 1;
	XvtCockpitReadouts_CopyState(&readouts);
	XVT_ASSERT_INT_EQ(readouts.target.visible, 0);
}

int main(void) {
	XvtCockpitReadouts_Reset();
	CheckNoCraftLeavesCleared();
	CheckSystemMasksWhateverVisibility();
	CheckFeatureCovers();
	CheckRestNeedsVisibleInstruments();
	CheckThreats();
	CheckLaserLocks();
	CheckViewLaserSlots();
	CheckRecordRadar();
	CheckRadarLastIndex();
	CheckRadarTargetMarker();
	CheckCompleteRadar();
	CheckBeginUpdate();
	return 0;
}
