/* Tests for xvt/flight/ai_countermeasures.c, a computer craft's
 * countermeasures, on worlds this file builds in the game's slot layout:
 * CRAFT_SLOTS craft slots, then SHOT_START to SHOT_END - 1 shot slots, each
 * with a guidance record. Model boxes are set by hand; no game data is read.
 * Each rule is checked against the end of the 1997 AI's turret order it was
 * ported from (paifight_gunnerselfdefenseorder). */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/model_bounds.h"
#include "xvt/assets/object_genus.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai_countermeasures.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt_runtime/ai/ai_wire.h"

enum {
	CRAFT_SLOTS = 8,
	SHOT_START = 8,
	SHOT_END = 24,
	SLOT_COUNT = SHOT_END,
	OWNER = 0,
	OTHER_CRAFT = 1,
	TEST_XWING = 1, /* Object type 1, a starfighter. */
	NO_PLAYER = 7,	/* The local player, flying nothing. */
	MISSILE = WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE,
	TORPEDO = WARHEAD_OBJECT_TYPE_PROTON_TORPEDO,
	ROCKET = WARHEAD_OBJECT_TYPE_HEAVY_ROCKET,
	FLARE = COUNTERMEASURE_PROJECTILE_OBJECT_TYPE,
	CHAFF_SECONDS = 10,
	COOLDOWN_TICKS = 472,
	UNLIMITED_STATUS = 21,
};

static struct object_record g_test_objects[SLOT_COUNT];
static struct mobile_object g_test_mobiles[SLOT_COUNT];
static struct craft_data g_test_craft[CRAFT_SLOTS];
static struct warhead_guidance_state g_test_guidance[SHOT_END - SHOT_START];

/* Sets an object type's model box, so the bounds getters read it. */
static void set_model_box(int object_type, float min_y, float max_y)
{
	g_model_bounds_cached[object_type] = 1;
	g_model_bounds_min[object_type].x = -10.0f;
	g_model_bounds_min[object_type].y = min_y;
	g_model_bounds_min[object_type].z = -10.0f;
	g_model_bounds_max[object_type].x = 10.0f;
	g_model_bounds_max[object_type].y = max_y;
	g_model_bounds_max[object_type].z = 10.0f;
}

/* An empty world: no object in any slot, no player flying, flight groups 0
 * and 1 on teams 0 and 1, computer shots in the shot slots. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_test_guidance, 0, sizeof g_test_guidance);
	memset(g_mission_flight_groups, 0,
	       2 * sizeof g_mission_flight_groups[0]);
	memset(g_mission_teams, 0, sizeof g_mission_teams);
	memset(g_players, 0, sizeof g_players);
	for (int player = 0; player < 8; ++player) {
		g_players[player].object_index = -1;
	}
	memset(g_object_slot_range_by_genus, 0,
	       sizeof g_object_slot_range_by_genus);
	g_object_table = g_test_objects;
	g_projectile_guidance_states = g_test_guidance;
	for (int slot = 0; slot < SLOT_COUNT; ++slot) {
		g_test_objects[slot].player_owner_idx = -1;
		g_test_objects[slot].mobj = &g_test_mobiles[slot];
		if (slot < CRAFT_SLOTS) {
			g_test_mobiles[slot].p_craft = &g_test_craft[slot];
		}
	}
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = CRAFT_SLOTS;
	g_projectile_object_slot_start = SHOT_START;
	g_projectile_object_slot_end = SHOT_END;
	g_region_main_object_slot_end = SLOT_COUNT;
	g_region_static_object_slot_count = 0;
	g_object_slot_range_by_genus[CRAFT_GENUS_OTHER_PROJECTILE].start =
		SHOT_START;
	g_object_slot_range_by_genus[CRAFT_GENUS_OTHER_PROJECTILE].end =
		SHOT_END;
	for (int fg = 0; fg < 2; ++fg) {
		g_mission_flight_groups[fg].fg.team = (uint8_t)fg;
		g_mission_flight_groups[fg].fg.iff = (uint8_t)fg;
	}
	g_local_player = NO_PLAYER;
	g_flight_sim_side_effects_suppressed = 0;
	g_elapsed_ticks = 1;
	set_model_box(TEST_XWING, -20.0f, 30.0f);
	set_model_box(FLARE, -5.0f, 5.0f);
}

/* Puts an active computer X-wing of flight group fg in craft slot obj,
 * carrying countermeasures of type cm, left of them, its countermeasure
 * system working. */
static struct craft_data *place_craft(int obj, int fg, int cm, int left)
{
	g_test_objects[obj].object_type = TEST_XWING;
	g_test_objects[obj].genus_id = CRAFT_GENUS_STARFIGHTER;
	g_test_objects[obj].flight_group_idx = (uint8_t)fg;
	g_test_mobiles[obj].team = (uint8_t)fg;
	g_test_mobiles[obj].iff = (uint8_t)fg;
	struct craft_data *craft = &g_test_craft[obj];
	craft->object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	craft->model_index = (uint8_t)get_model_index_from_type(TEST_XWING);
	craft->ai_controller.target_obj_idx = UINT16_MAX;
	craft->cm_type_id = (countermeasure_type)cm;
	craft->cm_ammo_count = (uint8_t)left;
	craft->working_subsystems = CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES;
	return craft;
}

/* Puts a flying computer shot of object type type in shot slot obj at x, y,
 * z, aimed at target, homing at tier tier (0 does not home). */
static void place_shot(int obj, int type, int x, int y, int z, uint16_t target,
		       int tier)
{
	g_test_objects[obj].object_type = (uint8_t)type;
	g_test_objects[obj].genus_id = CRAFT_GENUS_OTHER_PROJECTILE;
	g_test_objects[obj].world_x = x;
	g_test_objects[obj].world_y = y;
	g_test_objects[obj].world_z = z;
	g_test_mobiles[obj].family = 1;
	g_test_mobiles[obj].team = 1;
	g_test_mobiles[obj].p_warhead_guidance =
		&g_test_guidance[obj - SHOT_START];
	g_test_guidance[obj - SHOT_START].target_obj_idx = target;
	g_test_guidance[obj - SHOT_START].homing_tier = (uint8_t)tier;
}

/* The view of the craft in OWNER, every field first set to 0xEE. */
static struct ai_wire_craft_view view_of_owner(void)
{
	struct ai_wire_craft_view view;
	memset(&view, 0xEE, sizeof view);
	ai_countermeasures_view_craft(OWNER, &view);
	return view;
}

/* The view names the homing shots aimed at the craft in slot order, each
 * with its type, position and whether a countermeasure homes on it, and the
 * rest 0: not a shot that does not home, one aimed at another craft, an
 * empty slot, or a countermeasure aimed at a shot. Only a countermeasure
 * homing on a shot decoys it: not one that lost its lock, nor another
 * shot. It carries the craft's
 * countermeasure type, count, cooldown, chaff seconds and whether its
 * system works. */
static void check_view_names_homing_shots(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(OWNER, 0, 2, 4);
	craft->chaff_active_seconds = 7;
	craft->cm_fire_cooldown_timer = 100;
	place_craft(OTHER_CRAFT, 1, 0, 0);
	place_shot(8, MISSILE, 1000, -2000, 3000, OWNER, 2);
	place_shot(9, TORPEDO, 1, 1, 1, OWNER, 0);
	place_shot(10, MISSILE, 2, 2, 2, OTHER_CRAFT, 3);
	place_shot(11, MISSILE, 3, 3, 3, OWNER, 4);
	g_test_objects[11].object_type = 0;
	place_shot(12, ROCKET, -5, 6, -7, OWNER, 1);
	place_shot(13, FLARE, 4, 4, 4, 12, 6);
	place_shot(14, FLARE, 5, 5, 5, 8, 0);
	place_shot(15, TORPEDO, 6, 6, 6, 8, 2);
	struct ai_wire_craft_view view = view_of_owner();
	XVT_ASSERT_INT_EQ(view.countermeasure, AI_WIRE_COUNTERMEASURE_FLARE);
	XVT_ASSERT_INT_EQ(view.countermeasures_left, 4);
	XVT_ASSERT_INT_EQ(view.countermeasure_working, 1);
	XVT_ASSERT_INT_EQ(view.chaff_seconds, 7);
	XVT_ASSERT_INT_EQ(view.countermeasure_cooldown, 100);
	XVT_ASSERT_INT_EQ(view.threat_count, 2);
	XVT_ASSERT_INT_EQ(view.threat_object[0], 8);
	XVT_ASSERT_INT_EQ(view.threat_type[0], MISSILE);
	XVT_ASSERT_INT_EQ(view.threat_decoyed[0], 0);
	XVT_ASSERT_INT_EQ(view.threat_x[0], 1000);
	XVT_ASSERT_INT_EQ(view.threat_y[0], -2000);
	XVT_ASSERT_INT_EQ(view.threat_z[0], 3000);
	XVT_ASSERT_INT_EQ(view.threat_object[1], 12);
	XVT_ASSERT_INT_EQ(view.threat_type[1], ROCKET);
	XVT_ASSERT_INT_EQ(view.threat_decoyed[1], 1);
	XVT_ASSERT_INT_EQ(view.threat_x[1], -5);
	XVT_ASSERT_INT_EQ(view.threat_y[1], 6);
	XVT_ASSERT_INT_EQ(view.threat_z[1], -7);
	for (int threat = 2; threat < AI_WIRE_THREATS_MAX; ++threat) {
		XVT_ASSERT_INT_EQ(view.threat_object[threat], 0);
		XVT_ASSERT_INT_EQ(view.threat_type[threat], 0);
		XVT_ASSERT_INT_EQ(view.threat_decoyed[threat], 0);
		XVT_ASSERT_INT_EQ(view.threat_x[threat], 0);
		XVT_ASSERT_INT_EQ(view.threat_y[threat], 0);
		XVT_ASSERT_INT_EQ(view.threat_z[threat], 0);
	}

	craft->working_subsystems = 0;
	XVT_ASSERT_INT_EQ(view_of_owner().countermeasure_working, 0);
}

/* With more homing shots aimed at the craft than the view holds, it names
 * the first AI_WIRE_THREATS_MAX in slot order. */
static void check_view_takes_first_shots(void)
{
	fresh_world();
	place_craft(OWNER, 0, 1, 2);
	for (int slot = 15; slot < 15 + AI_WIRE_THREATS_MAX + 1; ++slot) {
		place_shot(slot, MISSILE, slot, 0, 0, OWNER, 1);
	}
	struct ai_wire_craft_view view = view_of_owner();
	XVT_ASSERT_INT_EQ(view.threat_count, AI_WIRE_THREATS_MAX);
	for (int threat = 0; threat < AI_WIRE_THREATS_MAX; ++threat) {
		XVT_ASSERT_INT_EQ(view.threat_object[threat], 15 + threat);
	}
}

/* Chaff adds 10 simulated seconds when none are left and uses a
 * countermeasure, none while chaff still runs, and none under a flight
 * group status of 21 in either status. Nothing launches from a craft that
 * is not active, whose system does not work or that has none left. */
static void check_chaff_launch(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(OWNER, 0, 1, 3);
	ai_countermeasures_launch(OWNER);
	XVT_ASSERT_INT_EQ(craft->chaff_active_seconds, CHAFF_SECONDS);
	XVT_ASSERT_INT_EQ(craft->cm_ammo_count, 2);
	ai_countermeasures_launch(OWNER);
	XVT_ASSERT_INT_EQ(craft->chaff_active_seconds, CHAFF_SECONDS);
	XVT_ASSERT_INT_EQ(craft->cm_ammo_count, 2);
	XVT_ASSERT_INT_EQ(craft->cm_fire_cooldown_timer, 0);

	craft->chaff_active_seconds = 0;
	g_mission_flight_groups[0].fg.status1 = UNLIMITED_STATUS;
	ai_countermeasures_launch(OWNER);
	XVT_ASSERT_INT_EQ(craft->chaff_active_seconds, CHAFF_SECONDS);
	XVT_ASSERT_INT_EQ(craft->cm_ammo_count, 2);
	craft->chaff_active_seconds = 0;
	g_mission_flight_groups[0].fg.status1 = 0;
	g_mission_flight_groups[0].fg.status2 = UNLIMITED_STATUS;
	ai_countermeasures_launch(OWNER);
	XVT_ASSERT_INT_EQ(craft->chaff_active_seconds, CHAFF_SECONDS);
	XVT_ASSERT_INT_EQ(craft->cm_ammo_count, 2);
	g_mission_flight_groups[0].fg.status2 = 0;

	craft->chaff_active_seconds = 0;
	craft->object_kind = CRAFT_OBJECT_KIND_BREAKING_UP;
	ai_countermeasures_launch(OWNER);
	craft->object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	craft->working_subsystems = 0;
	ai_countermeasures_launch(OWNER);
	craft->working_subsystems = CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES;
	XVT_ASSERT_INT_EQ(craft->chaff_active_seconds, 0);
	XVT_ASSERT_INT_EQ(craft->cm_ammo_count, 2);
	craft->cm_ammo_count = 0;
	ai_countermeasures_launch(OWNER);
	XVT_ASSERT_INT_EQ(craft->chaff_active_seconds, 0);
	XVT_ASSERT_INT_EQ(craft->cm_ammo_count, 0);
}

/* The number of flares in the shot slots. */
static int flares_flying(void)
{
	int count = 0;
	for (int slot = SHOT_START; slot < SHOT_END; ++slot) {
		count += g_test_objects[slot].object_type == FLARE;
	}
	return count;
}

/* A flare launches through laser_createcountermeasureprojectile, which
 * homes it on the missile aimed at the craft, uses a countermeasure and
 * starts the 472-tick cooldown; none launches while the cooldown runs, nor
 * from a craft that is not active. */
static void check_flare_launch(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(OWNER, 0, 2, 3);
	place_shot(8, MISSILE, 900, 0, 0, OWNER, 3);
	ai_countermeasures_launch(OWNER);
	XVT_ASSERT_INT_EQ(flares_flying(), 1);
	XVT_ASSERT_INT_EQ(g_test_objects[9].object_type, FLARE);
	XVT_ASSERT_INT_EQ(g_test_guidance[9 - SHOT_START].target_obj_idx, 8);
	XVT_ASSERT_INT_EQ(g_test_guidance[9 - SHOT_START].homing_tier, 6);
	XVT_ASSERT_INT_EQ(craft->cm_ammo_count, 2);
	XVT_ASSERT_INT_EQ(craft->cm_fire_cooldown_timer, COOLDOWN_TICKS);
	XVT_ASSERT_INT_EQ(craft->chaff_active_seconds, 0);

	ai_countermeasures_launch(OWNER);
	XVT_ASSERT_INT_EQ(flares_flying(), 1);
	XVT_ASSERT_INT_EQ(craft->cm_ammo_count, 2);

	craft->cm_fire_cooldown_timer = 0;
	craft->object_kind = CRAFT_OBJECT_KIND_EXPLODING;
	ai_countermeasures_launch(OWNER);
	XVT_ASSERT_INT_EQ(flares_flying(), 1);
	XVT_ASSERT_INT_EQ(craft->cm_ammo_count, 2);

	/* A craft with no countermeasure type launches nothing. */
	craft->object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	craft->cm_type_id = COUNTERMEASURE_TYPE_NONE;
	ai_countermeasures_launch(OWNER);
	XVT_ASSERT_INT_EQ(flares_flying(), 1);
	XVT_ASSERT_INT_EQ(craft->cm_ammo_count, 2);
	XVT_ASSERT_INT_EQ(craft->chaff_active_seconds, 0);
}

int main(void)
{
	check_view_names_homing_shots();
	check_view_takes_first_shots();
	check_chaff_launch();
	check_flare_launch();
	return 0;
}
