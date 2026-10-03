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

static struct object_record g_test_objects[2];
static struct mobile_object g_test_mobiles[2];
static struct craft_data g_test_craft;
static struct xvt_cockpit_state g_state;
static struct hud_radar_blip_point g_fore_drawn[48], g_aft_drawn[48];

/* The local player flies a TIE Interceptor in main slot 1 with four lasers, every HUD feature installed
 * and only the two radars active. Each laser and both radar scopes have a place in the cockpit layout. */
static void cockpit_instruments_start(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(&g_test_craft, 0, sizeof g_test_craft);
	g_object_table = g_test_objects;
	g_region_main_object_slot_end = 2;
	g_test_objects[SLOT].object_type = CRAFT_SPECIES_TIE_INTERCEPTOR;
	g_test_objects[SLOT].object_signature = 0x0111;
	g_test_objects[SLOT].mobj = &g_test_mobiles[SLOT];
	g_test_mobiles[SLOT].p_craft = &g_test_craft;
	g_test_craft.model_index = 0;
	memset(&g_model_defs[0], 0, sizeof g_model_defs[0]);
	g_test_craft.system_flags = 0x15;
	g_test_craft.working_subsystems = 0x05;
	g_test_craft.laser_slot_count = LASERS;
	g_test_craft.damage_stats.installed_hud_feature_mask = 0x1FFF;
	g_test_craft.damage_stats.active_hud_feature_mask =
		XVT_COCKPIT_FEATURE_FORE_RADAR | XVT_COCKPIT_FEATURE_AFT_RADAR;

	memset(g_players, 0, sizeof g_players);
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
	}
	g_local_player = LOCAL;
	g_players[LOCAL].object_index = SLOT;
	g_players[LOCAL].current_target_object_idx = -1;
	memset(g_player_flight_transient_timers, 0,
	       sizeof g_player_flight_transient_timers);

	memset(g_hud_element_layouts, 0, sizeof g_hud_element_layouts);
	g_hud_element_layouts[0] =
		(struct hud_element_layout){.x = 200, .y = 300};
	g_hud_element_layouts[1] =
		(struct hud_element_layout){.x = 400, .y = 300};
	for (unsigned laser = 0; laser < LASERS; ++laser) {
		g_hud_element_layouts[3 + laser] = (struct hud_element_layout){
			.x = (uint16_t)(10 + laser), .y = 20};
	}

	memset(g_fore_drawn, 0, sizeof g_fore_drawn);
	memset(g_aft_drawn, 0, sizeof g_aft_drawn);
	g_radar_fore_draw_blips = g_fore_drawn;
	g_radar_aft_draw_blips = g_aft_drawn;
	g_flight_bytes_per_pixel = 1;

	xvt_cockpit_instruments_begin_update(LOCAL);
}

/* A cockpit view: the forward view with the map closed and instruments shown, unless changed. */
static struct xvt_cockpit_state *view(unsigned hud_state,
				      int instruments_visible, int map_active)
{
	memset(&g_state, 0, sizeof g_state);
	g_state.view.hud_state = (uint16_t)hud_state;
	g_state.view.instruments_visible = (uint8_t)instruments_visible;
	g_state.view.map_active = (uint8_t)map_active;
	g_state.view.instrument_base = HUD_COCKPIT_INSTRUMENT_BASE_INDEX;
	return &g_state;
}

static const struct xvt_cockpit_state *
built(unsigned hud_state, int instruments_visible, int map_active)
{
	xvt_cockpit_instruments_build(
		view(hud_state, instruments_visible, map_active));
	return &g_state;
}

static const struct xvt_cockpit_state *forward(void)
{
	return built(HUD_VIEW_FORWARD, 1, 0);
}

static int all_zero(const void *data, size_t size)
{
	const unsigned char *bytes = data;
	for (size_t index = 0; index < size; ++index) {
		if (bytes[index]) {
			return 0;
		}
	}
	return 1;
}

static int no_threats(const struct xvt_cockpit_systems *systems)
{
	return all_zero(systems->threats, sizeof systems->threats);
}

/* Builds over a state whose five parts hold junk, and reports whether all five came back cleared. */
static int build_clears(void)
{
	struct xvt_cockpit_state *state = view(HUD_VIEW_FORWARD, 1, 0);
	memset(&state->systems, 0xA5, sizeof state->systems);
	memset(&state->weapons, 0xA5, sizeof state->weapons);
	memset(&state->target, 0xA5, sizeof state->target);
	memset(&state->radar, 0xA5, sizeof state->radar);
	memset(&state->readouts, 0xA5, sizeof state->readouts);
	xvt_cockpit_instruments_build(state);
	return all_zero(&state->systems, sizeof state->systems) &&
	       all_zero(&state->weapons, sizeof state->weapons) &&
	       all_zero(&state->target, sizeof state->target) &&
	       all_zero(&state->radar, sizeof state->radar) &&
	       all_zero(&state->readouts, sizeof state->readouts);
}

static void check_no_craft_leaves_cleared(void)
{
	cockpit_instruments_start();
	xvt_cockpit_instruments_record_threats(1, 2, 3, 4);
	g_players[LOCAL].object_index = -1;
	XVT_ASSERT_INT_EQ(build_clears(), 1);
	/* A slot past the main slots holds no player craft. */
	g_players[LOCAL].object_index = 2;
	XVT_ASSERT_INT_EQ(build_clears(), 1);
	/* An object with no mobile record, or a mobile record with no craft. */
	g_players[LOCAL].object_index = SLOT;
	g_test_objects[SLOT].mobj = NULL;
	XVT_ASSERT_INT_EQ(build_clears(), 1);
	g_test_objects[SLOT].mobj = &g_test_mobiles[SLOT];
	g_test_mobiles[SLOT].p_craft = NULL;
	XVT_ASSERT_INT_EQ(build_clears(), 1);
	/* No object table at all. */
	g_test_mobiles[SLOT].p_craft = &g_test_craft;
	g_object_table = NULL;
	XVT_ASSERT_INT_EQ(build_clears(), 1);
	/* With the craft back, Build fills its parts again. */
	g_object_table = g_test_objects;
	XVT_ASSERT_INT_EQ(build_clears(), 0);
}

static void check_system_masks_whatever_visibility(void)
{
	cockpit_instruments_start();
	struct xvt_cockpit_systems shown =
		built(HUD_VIEW_FORWARD, 1, 0)->systems;
	struct xvt_cockpit_systems hidden =
		built(HUD_VIEW_FORWARD, 0, 0)->systems;
	struct xvt_cockpit_systems elsewhere =
		built(HUD_VIEW_TARGET_CAMERA, 0, 1)->systems;
	XVT_ASSERT_TRUE(shown.installed != 0 && shown.working != 0);
	XVT_ASSERT_TRUE(shown.active_hud_features != 0 &&
			shown.installed_hud_features != 0);
	XVT_ASSERT_INT_EQ(hidden.installed, shown.installed);
	XVT_ASSERT_INT_EQ(hidden.working, shown.working);
	XVT_ASSERT_INT_EQ(hidden.active_hud_features,
			  shown.active_hud_features);
	XVT_ASSERT_INT_EQ(hidden.installed_hud_features,
			  shown.installed_hud_features);
	XVT_ASSERT_INT_EQ(elsewhere.working, shown.working);
	XVT_ASSERT_INT_EQ(elsewhere.installed_hud_features,
			  shown.installed_hud_features);

	/* They come from the craft: a damaged subsystem changes the working mask. */
	g_test_craft.working_subsystems = 0x01;
	XVT_ASSERT_TRUE(built(HUD_VIEW_FORWARD, 0, 0)->systems.working !=
			shown.working);
}

static void check_feature_covers(void)
{
	cockpit_instruments_start();
	struct xvt_cockpit_indicator shown[13], hidden[13];
	memcpy(shown, built(HUD_VIEW_FORWARD, 1, 0)->systems.feature_covers,
	       sizeof shown);
	/* Every feature is installed and most are inactive, so this cockpit has covers to show. */
	XVT_ASSERT_INT_EQ(all_zero(shown, sizeof shown), 0);

	/* In the forward view they are built whether or not instruments show, map open or closed. */
	memcpy(hidden, built(HUD_VIEW_FORWARD, 0, 0)->systems.feature_covers,
	       sizeof hidden);
	XVT_ASSERT_INT_EQ(memcmp(hidden, shown, sizeof shown), 0);
	memcpy(hidden, built(HUD_VIEW_FORWARD, 0, 1)->systems.feature_covers,
	       sizeof hidden);
	XVT_ASSERT_INT_EQ(memcmp(hidden, shown, sizeof shown), 0);

	/* The same holds in the HUD-only view. */
	memcpy(shown, built(HUD_VIEW_HUD_ONLY, 1, 0)->systems.feature_covers,
	       sizeof shown);
	XVT_ASSERT_INT_EQ(all_zero(shown, sizeof shown), 0);
	memcpy(hidden, built(HUD_VIEW_HUD_ONLY, 0, 0)->systems.feature_covers,
	       sizeof hidden);
	XVT_ASSERT_INT_EQ(memcmp(hidden, shown, sizeof shown), 0);

	/* In any other view there are none. */
	const struct xvt_cockpit_state *state =
		built(HUD_VIEW_TARGET_CAMERA, 1, 0);
	XVT_ASSERT_INT_EQ(all_zero(state->systems.feature_covers,
				   sizeof state->systems.feature_covers),
			  1);
	state = built(HUD_VIEW_CRAFT_LIST, 0, 0);
	XVT_ASSERT_INT_EQ(all_zero(state->systems.feature_covers,
				   sizeof state->systems.feature_covers),
			  1);
}

static void check_rest_needs_visible_instruments(void)
{
	cockpit_instruments_start();
	xvt_cockpit_instruments_record_threats(1, 2, 3, 4);
	xvt_cockpit_instruments_record_radar(0, 1, 0, 210, 310, 5);
	xvt_cockpit_instruments_record_laser_lock(0, 2);

	/* Forward, map closed, instruments shown: weapons, radar and threats are filled. */
	const struct xvt_cockpit_state *state = forward();
	XVT_ASSERT_INT_EQ(no_threats(&state->systems), 0);
	XVT_ASSERT_INT_EQ(all_zero(&state->weapons, sizeof state->weapons), 0);
	XVT_ASSERT_INT_EQ(all_zero(&state->radar, sizeof state->radar), 0);

	/* Instruments hidden: none of them. */
	state = built(HUD_VIEW_FORWARD, 0, 0);
	XVT_ASSERT_INT_EQ(no_threats(&state->systems), 1);
	XVT_ASSERT_INT_EQ(all_zero(&state->weapons, sizeof state->weapons), 1);
	XVT_ASSERT_INT_EQ(all_zero(&state->radar, sizeof state->radar), 1);
	XVT_ASSERT_INT_EQ(all_zero(&state->readouts, sizeof state->readouts),
			  1);

	/* Shown, but with the map open or in another view: none of them either. */
	state = built(HUD_VIEW_FORWARD, 1, 1);
	XVT_ASSERT_INT_EQ(no_threats(&state->systems), 1);
	XVT_ASSERT_INT_EQ(all_zero(&state->weapons, sizeof state->weapons), 1);
	XVT_ASSERT_INT_EQ(all_zero(&state->radar, sizeof state->radar), 1);
	state = built(HUD_VIEW_TARGET_CAMERA, 1, 0);
	XVT_ASSERT_INT_EQ(no_threats(&state->systems), 1);
	XVT_ASSERT_INT_EQ(all_zero(&state->weapons, sizeof state->weapons), 1);
	XVT_ASSERT_INT_EQ(all_zero(&state->radar, sizeof state->radar), 1);

	/* The HUD-only view is one of the cockpit views. */
	state = built(HUD_VIEW_HUD_ONLY, 1, 0);
	XVT_ASSERT_INT_EQ(no_threats(&state->systems), 0);
	XVT_ASSERT_INT_EQ(all_zero(&state->radar, sizeof state->radar), 0);
}

static void check_threats(void)
{
	cockpit_instruments_start();
	xvt_cockpit_instruments_record_threats(1, 2, 3, 4);
	const struct xvt_cockpit_systems *systems = &forward()->systems;
	XVT_ASSERT_INT_EQ(systems->threats[0].state, 1);
	XVT_ASSERT_INT_EQ(systems->threats[1].state, 2);
	XVT_ASSERT_INT_EQ(systems->threats[2].state, 3);
	XVT_ASSERT_INT_EQ(systems->threats[3].state, 4);
}

static void check_laser_locks(void)
{
	cockpit_instruments_start();
	xvt_cockpit_instruments_record_threats(1, 2, 3, 4);
	for (unsigned slot = 0; slot < LASERS; ++slot) {
		xvt_cockpit_instruments_record_laser_lock(slot, slot + 1);
	}
	/* A slot from XVT_HUD_WEAPON_SLOTS up is ignored; nothing else recorded changes. */
	xvt_cockpit_instruments_record_laser_lock(XVT_HUD_WEAPON_SLOTS, 9);
	const struct xvt_cockpit_state *state = forward();
	unsigned shown = 0;
	for (unsigned slot = 0; slot < LASERS; ++slot) {
		if (state->weapons.slots[slot].visible) {
			XVT_ASSERT_INT_EQ(state->weapons.slots[slot].locked,
					  slot + 1);
			++shown;
		}
	}
	/* The craft's lasers all have a place in the layout, so they show. */
	XVT_ASSERT_TRUE(shown > 0);
	XVT_ASSERT_INT_EQ(state->systems.threats[0].state, 1);
}

static void check_view_laser_slots(void)
{
	cockpit_instruments_start();
	XVT_ASSERT_INT_EQ(built(HUD_VIEW_FORWARD, 1, 0)->view.laser_slots,
			  LASERS);
	XVT_ASSERT_INT_EQ(built(HUD_VIEW_FORWARD, 0, 0)->view.laser_slots,
			  LASERS);
	XVT_ASSERT_INT_EQ(built(HUD_VIEW_TARGET_CAMERA, 0, 1)->view.laser_slots,
			  LASERS);
}

/* The scope that holds blips after recording on the fore scope only. */
static unsigned fore_side(const struct xvt_cockpit_radar *radar)
{
	XVT_ASSERT_TRUE((radar->count[0] != 0) != (radar->count[1] != 0));
	return radar->count[0] ? 0 : 1;
}

static void check_record_radar(void)
{
	cockpit_instruments_start();
	xvt_cockpit_instruments_record_radar(0, 1, 2, 250, 360, 5);
	const struct xvt_cockpit_radar *radar = &forward()->radar;
	unsigned fore = fore_side(radar), aft = 1 - fore;
	/* The count becomes index + 1; the other scope is untouched. */
	XVT_ASSERT_INT_EQ(radar->count[fore], 3);
	XVT_ASSERT_INT_EQ(radar->count[aft], 0);
	struct xvt_snap_radar_blip first = radar->blips[fore][2];

	/* Positions are relative to the scope's anchor: moving the point moves the blip by as much. */
	xvt_cockpit_instruments_record_radar(0, 1, 2, 255, 363, 5);
	radar = &forward()->radar;
	XVT_ASSERT_INT_EQ(radar->blips[fore][2].x, first.x + 5);
	XVT_ASSERT_INT_EQ(radar->blips[fore][2].y, first.y + 3);

	/* The aft scope counts its own blips. */
	xvt_cockpit_instruments_record_radar(0, 0, 0, 410, 310, 5);
	radar = &forward()->radar;
	XVT_ASSERT_INT_EQ(radar->count[aft], 1);
	XVT_ASSERT_INT_EQ(radar->count[fore], 3);

	/* Indices outside 0 to 47 are ignored. */
	struct xvt_snap_radar_blip aft_first;
	memcpy(&aft_first, &radar->blips[aft][0], sizeof aft_first);
	xvt_cockpit_instruments_record_radar(0, 1, 48, 1, 1, 6);
	xvt_cockpit_instruments_record_radar(0, 1, -1, 1, 1, 6);
	xvt_cockpit_instruments_record_radar(0, 0, 48, 1, 1, 6);
	radar = &forward()->radar;
	XVT_ASSERT_INT_EQ(radar->count[fore], 3);
	XVT_ASSERT_INT_EQ(radar->count[aft], 1);
	XVT_ASSERT_INT_EQ(
		memcmp(&radar->blips[aft][0], &aft_first, sizeof aft_first), 0);
}

static void check_radar_last_index(void)
{
	cockpit_instruments_start();
	xvt_cockpit_instruments_record_radar(0, 1, 0, 250, 360, 5);
	xvt_cockpit_instruments_record_radar(0, 1, 47, 250, 360, 5);
	const struct xvt_cockpit_radar *radar = &forward()->radar;
	unsigned fore = fore_side(radar);
	/* Index 47 is stored but leaves the count at 47. */
	XVT_ASSERT_INT_EQ(radar->count[fore], 47);
	XVT_ASSERT_INT_EQ(radar->blips[fore][47].x, radar->blips[fore][0].x);
	XVT_ASSERT_INT_EQ(radar->blips[fore][47].y, radar->blips[fore][0].y);
	xvt_cockpit_instruments_record_radar(0, 1, 46, 250, 360, 5);
	XVT_ASSERT_INT_EQ(forward()->radar.count[fore], 47);
}

static void check_radar_target_marker(void)
{
	cockpit_instruments_start();
	g_players[LOCAL].current_target_object_idx = SLOT;
	/* A blip on another object leaves the marker alone. */
	xvt_cockpit_instruments_record_radar(0, 1, 1, 250, 360, 5);
	XVT_ASSERT_INT_EQ(forward()->radar.marker_visible, 0);

	xvt_cockpit_instruments_record_radar(SLOT, 1, 4, 260, 370, 5);
	const struct xvt_cockpit_radar *radar = &forward()->radar;
	unsigned fore = fore_side(radar);
	XVT_ASSERT_INT_EQ(radar->marker_visible, 1);
	XVT_ASSERT_INT_EQ(radar->marker_side, fore);
	XVT_ASSERT_INT_EQ(radar->marker_x, radar->blips[fore][4].x);
	XVT_ASSERT_INT_EQ(radar->marker_y, radar->blips[fore][4].y);
}

static void check_complete_radar(void)
{
	cockpit_instruments_start();
	for (unsigned index = 0; index < 48; ++index) {
		g_fore_drawn[index].color = (uint16_t)(index + 5);
		g_aft_drawn[index].color = (uint16_t)(index * 4);
	}
	for (int index = 0; index < 3; ++index) {
		xvt_cockpit_instruments_record_radar(0, 1, index, 250, 360, 5);
	}

	/* At one byte per pixel, the low two color bits of each counted blip's drawn point. */
	xvt_cockpit_instruments_complete_radar();
	const struct xvt_cockpit_radar *radar = &forward()->radar;
	unsigned fore = fore_side(radar), aft = 1 - fore;
	for (unsigned index = 0; index < 3; ++index) {
		XVT_ASSERT_INT_EQ(radar->coverage[fore][index],
				  g_fore_drawn[index].color & 3);
	}
	XVT_ASSERT_INT_EQ(radar->coverage[fore][3], 0);
	XVT_ASSERT_INT_EQ(
		all_zero(radar->coverage[aft], sizeof radar->coverage[aft]), 1);

	/* Otherwise, whether the point was drawn. */
	g_flight_bytes_per_pixel = 2;
	g_fore_drawn[1].color = 0;
	xvt_cockpit_instruments_complete_radar();
	radar = &forward()->radar;
	XVT_ASSERT_INT_EQ(radar->coverage[fore][0], 1);
	XVT_ASSERT_INT_EQ(radar->coverage[fore][1], 0);
	XVT_ASSERT_INT_EQ(radar->coverage[fore][2], 1);
}

static void check_begin_update(void)
{
	cockpit_instruments_start();
	xvt_cockpit_instruments_record_threats(1, 2, 3, 4);
	xvt_cockpit_instruments_record_radar(0, 1, 0, 250, 360, 5);
	xvt_cockpit_instruments_record_laser_lock(0, 2);
	xvt_cockpit_readouts_begin_target(0);

	/* Another player's update is ignored. */
	xvt_cockpit_instruments_begin_update(OTHER_PLAYER);
	const struct xvt_cockpit_state *state = forward();
	XVT_ASSERT_INT_EQ(state->systems.threats[3].state, 4);
	XVT_ASSERT_INT_EQ(
		all_zero(state->radar.count, sizeof state->radar.count), 0);
	XVT_ASSERT_INT_EQ(state->weapons.slots[0].locked, 2);
	static struct xvt_cockpit_state readouts;
	memset(&readouts, 0, sizeof readouts);
	readouts.view.instruments_visible = 1;
	xvt_cockpit_readouts_copy_state(&readouts);
	XVT_ASSERT_INT_EQ(readouts.target.visible, 1);

	/* The local player's update forgets the locks, threats and radar, and begins a readouts update. */
	xvt_cockpit_instruments_begin_update(LOCAL);
	state = forward();
	for (unsigned threat = 0; threat < 4; ++threat) {
		XVT_ASSERT_INT_EQ(state->systems.threats[threat].state, 0);
	}
	XVT_ASSERT_INT_EQ(
		all_zero(&state->radar.count, sizeof state->radar.count), 1);
	XVT_ASSERT_INT_EQ(state->weapons.slots[0].locked, 0);
	memset(&readouts, 0, sizeof readouts);
	readouts.view.instruments_visible = 1;
	xvt_cockpit_readouts_copy_state(&readouts);
	XVT_ASSERT_INT_EQ(readouts.target.visible, 0);
}

int main(void)
{
	xvt_cockpit_readouts_reset();
	check_no_craft_leaves_cleared();
	check_system_masks_whatever_visibility();
	check_feature_covers();
	check_rest_needs_visible_instruments();
	check_threats();
	check_laser_locks();
	check_view_laser_slots();
	check_record_radar();
	check_radar_last_index();
	check_radar_target_marker();
	check_complete_radar();
	check_begin_update();
	return 0;
}
