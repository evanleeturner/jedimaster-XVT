/* Tests for xvt/flight/object/laser.c, the weapons: cannon groups, warheads,
 * countermeasures, shots fired from static objects, and the weapon step that
 * recharges and fires every craft's weapons. Each check builds the world it
 * needs in the game's own layout: an object table this file owns with 32
 * craft slots, 160 shot slots, the other mobile slots and 64 static slots, a
 * craft record for each craft slot and a guidance record for each shot slot.
 * Model boxes are set by hand; no game data is read.
 *
 * POSIX only, for the alarm that stops a check whose call does not return. */
#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "test_assert.h"
#include "xvt/assets/model_bounds.h"
#include "xvt/assets/object_genus.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/util/memory.h"

enum {
	SLOT_COUNT = 552,
	MOBILE_SLOTS = 488,
	CRAFT_SLOTS = 32,
	SHOT_START = 32,
	SHOT_END = 192,
	OTHER_SHOT_START = 160,
	STATIC_START = 488,
	STATIC_SLOTS = 64,
	TEST_XWING = 1, /* Object type 1, a starfighter. */
	NO_PLAYER = 7,	/* The local player, flying nothing. */
	IMPACT_EFFECT = 129,
	CONCUSSION_MISSILE = WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE,
	FLARE = COUNTERMEASURE_PROJECTILE_OBJECT_TYPE,
};

static struct object_record g_test_objects[SLOT_COUNT];
static struct mobile_object g_test_mobiles[MOBILE_SLOTS];
static struct craft_data g_test_craft[CRAFT_SLOTS];
static struct warhead_guidance_state g_test_guidance[SHOT_END - SHOT_START + 1];
static struct model_def g_saved_xwing_model;
static uint16_t g_message_log;

static void stop_on_alarm(int signal_number)
{
	static const char message[] =
		"check failed: the call did not return within the time allowed\n";
	(void)signal_number;
	if (write(2, message, sizeof message - 1) < 0) {
		_exit(1);
	}
	_exit(1);
}

/* The program fails after the given number of seconds, so a call that never
 * returns fails at once instead of at the suite's time limit. */
static void fail_after_seconds(unsigned int seconds)
{
	signal(SIGALRM, stop_on_alarm);
	alarm(seconds);
}

static struct model_def *xwing_model(void)
{
	return &g_model_defs[get_model_index_from_type(TEST_XWING)];
}

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

/* An empty world in the game's slot layout: no object in any slot, no player
 * in the flight (the local player is slot 7), flight groups 0 and 1 on teams
 * 0 and 1 with IFFs 0 and 1, and every timer at 0. The X-wing's model is put
 * back as the game defines it. */
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
	memset(g_object_slot_range_by_genus, 0,
	       sizeof g_object_slot_range_by_genus);
	memset(&g_flight_global_countdown_timers, 0,
	       sizeof g_flight_global_countdown_timers);
	*xwing_model() = g_saved_xwing_model;
	g_object_table = g_test_objects;
	g_projectile_guidance_states = g_test_guidance;
	for (int i = 0; i < SLOT_COUNT; ++i) {
		g_test_objects[i].player_owner_idx = -1;
		if (i < MOBILE_SLOTS) {
			g_test_objects[i].mobj = &g_test_mobiles[i];
		}
		if (i < CRAFT_SLOTS) {
			g_test_mobiles[i].p_craft = &g_test_craft[i];
		}
	}
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = CRAFT_SLOTS;
	g_projectile_object_slot_start = SHOT_START;
	g_projectile_object_slot_end = SHOT_END;
	g_region_main_object_slot_end = MOBILE_SLOTS;
	g_region_static_object_slot_count = STATIC_SLOTS;
	g_object_slot_range_by_genus[CRAFT_GENUS_PLAYER_PROJECTILE].start =
		SHOT_START;
	g_object_slot_range_by_genus[CRAFT_GENUS_PLAYER_PROJECTILE].end =
		OTHER_SHOT_START;
	g_object_slot_range_by_genus[CRAFT_GENUS_OTHER_PROJECTILE].start =
		OTHER_SHOT_START;
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
	g_message_log_write_index = 0;
	g_message_log_total_count = 0;
	g_ready_message_queue_count = 0;
	memset(g_ready_message_pane_queue, 0,
	       sizeof g_ready_message_pane_queue);
}

/* Puts an X-wing of flight group fg in craft slot obj at x, y, z. */
static struct craft_data *place_craft(int obj, int fg, int x, int y, int z)
{
	g_test_objects[obj].object_type = TEST_XWING;
	g_test_objects[obj].genus_id = CRAFT_GENUS_STARFIGHTER;
	g_test_objects[obj].flight_group_idx = (uint8_t)fg;
	g_test_objects[obj].world_x = x;
	g_test_objects[obj].world_y = y;
	g_test_objects[obj].world_z = z;
	g_test_mobiles[obj].team = (uint8_t)fg;
	g_test_mobiles[obj].iff = (uint8_t)fg;
	g_test_craft[obj].model_index =
		(uint8_t)get_model_index_from_type(TEST_XWING);
	g_test_craft[obj].ai_controller.target_obj_idx = UINT16_MAX;
	return &g_test_craft[obj];
}

/* Puts a flying shot of object type type in slot obj at x, 0, 0, aimed at
 * target. */
static void place_shot(int obj, int type, int team, int x, uint16_t target)
{
	g_test_objects[obj].object_type = (uint8_t)type;
	g_test_objects[obj].genus_id = obj < OTHER_SHOT_START
					       ? CRAFT_GENUS_PLAYER_PROJECTILE
					       : CRAFT_GENUS_OTHER_PROJECTILE;
	g_test_objects[obj].world_x = x;
	g_test_mobiles[obj].family = 1;
	g_test_mobiles[obj].team = (uint8_t)team;
	g_test_mobiles[obj].p_warhead_guidance =
		&g_test_guidance[obj - SHOT_START];
	g_test_guidance[obj - SHOT_START].target_obj_idx = target;
}

static int is_warhead(int object_type)
{
	return g_projectile_type_data
		       .warhead_class[object_type -
				      PROJECTILE_OBJECT_TYPE_FIRST] != 0;
}

/* ------------------------------------------------------------------------ */
/* Shot lifetimes. */

/* laser_get_projectile_lifetime_ticks gives 236 ticks for each whole second of
 * a shot type's life plus its share of 236 for the fraction, rounded. */
static void check_lifetime_ticks(void)
{
	for (int i = 0; i < PROJECTILE_OBJECT_TYPE_COUNT; ++i) {
		unsigned seconds = g_projectile_type_data.lifetime_seconds[i];
		unsigned frac = g_projectile_type_data.lifetime_frac_q16[i];
		unsigned expected =
			236u * seconds + (frac * 236u + 0x8000u) / 0x10000u;
		XVT_ASSERT_INT_EQ(laser_get_projectile_lifetime_ticks(
					  PROJECTILE_OBJECT_TYPE_FIRST + i),
				  expected);
	}
	/* A Rebel turbo laser lives a second and a half. */
	XVT_ASSERT_INT_EQ(laser_get_projectile_lifetime_ticks(
				  PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER),
			  354);
}

/* ------------------------------------------------------------------------ */
/* Warheads fired from static objects. */

enum { MINE = STATIC_START + 3, TARGET_CRAFT = 4 };

/* Flight group 1 owns a static object in slot MINE at 7000, 8000, 9000 and
 * carries warhead choice 3 (a proton torpedo); its team is 1, its IFF 4.
 * The target is a craft of group 0 in slot TARGET_CRAFT. */
static void static_warhead_world(void)
{
	fresh_world();
	g_mission_flight_groups[1].fg.warhead = 3;
	g_mission_flight_groups[1].fg.iff = 4;
	g_test_objects[MINE].object_type = 70;
	g_test_objects[MINE].flight_group_idx = 1;
	g_test_objects[MINE].world_x = 7000;
	g_test_objects[MINE].world_y = 8000;
	g_test_objects[MINE].world_z = 9000;
	g_test_objects[MINE].object_signature = 0x1234;
	place_craft(TARGET_CRAFT, 0, 0, 0, 0);
	g_test_objects[TARGET_CRAFT].object_signature = 0x4321;
}

/* A flight group with no warhead fires nothing. */
static void check_static_no_warhead(void)
{
	static_warhead_world();
	g_mission_flight_groups[1].fg.warhead = 0;
	XVT_ASSERT_INT_EQ(laser_createprojectilefromstatic(MINE, TARGET_CRAFT),
			  UINT16_MAX);
	for (int i = SHOT_START; i < SHOT_END; ++i) {
		XVT_ASSERT_INT_EQ(g_test_objects[i].object_type, 0);
	}
}

/* With a free other-shot slot, the warhead takes the first one: the group's
 * warhead type and IFF, all angles 0, its type's speed (also the cruise
 * speed) and damage and life, 384 world units above the static object, a
 * homing tier of 3 to 6, and the target and its signature. */
static void check_static_warhead_fields(void)
{
	static_warhead_world();
	g_test_objects[OTHER_SHOT_START].object_type =
		PROJECTILE_OBJECT_TYPE_REBEL_LASER;
	g_test_objects[MINE].yaw = 0x1000;
	uint16_t slot = laser_createprojectilefromstatic(MINE, TARGET_CRAFT);
	XVT_ASSERT_INT_EQ(slot, OTHER_SHOT_START + 1);
	int type = g_warhead_type_ids[3];
	int row = type - PROJECTILE_OBJECT_TYPE_FIRST;
	struct object_record *shot = &g_test_objects[slot];
	struct warhead_guidance_state *guidance =
		&g_test_guidance[slot - SHOT_START];
	XVT_ASSERT_INT_EQ(shot->object_type, type);
	XVT_ASSERT_INT_EQ(shot->genus_id, CRAFT_GENUS_OTHER_PROJECTILE);
	XVT_ASSERT_INT_EQ(shot->mobj->family, 1);
	XVT_ASSERT_INT_EQ(shot->mobj->iff, 4);
	XVT_ASSERT_INT_EQ(shot->yaw, 0);
	XVT_ASSERT_INT_EQ(shot->pitch, 0);
	XVT_ASSERT_INT_EQ(shot->roll, 0);
	XVT_ASSERT_INT_EQ(shot->mobj->speed, g_projectile_type_data.speed[row]);
	XVT_ASSERT_INT_EQ(guidance->cruise_speed,
			  g_projectile_type_data.speed[row]);
	XVT_ASSERT_INT_EQ(shot->mobj->damage_amount,
			  g_projectile_type_data.damage[row]);
	XVT_ASSERT_INT_EQ(shot->mobj->lifetime_timer,
			  laser_get_projectile_lifetime_ticks(type));
	XVT_ASSERT_INT_EQ(shot->mobj->source_obj_idx, MINE);
	XVT_ASSERT_INT_EQ(shot->world_x, 7000);
	XVT_ASSERT_INT_EQ(shot->world_y, 8000);
	XVT_ASSERT_INT_EQ(shot->world_z, 9000 + 384);
	XVT_ASSERT_TRUE(guidance->homing_tier >= 3);
	XVT_ASSERT_TRUE(guidance->homing_tier <= 6);
	XVT_ASSERT_INT_EQ(guidance->target_obj_idx, TARGET_CRAFT);
	XVT_ASSERT_INT_EQ(guidance->target_signature, 0x4321);
	XVT_ASSERT_INT_EQ(guidance->source_player_idx, -1);
	XVT_ASSERT_TRUE(shot->mobj->p_warhead_guidance == guidance);
}

/* A warhead aimed at a player's craft warns that player: pending action 1
 * from no player, naming the warhead's slot, for 1,416 ticks. */
static void check_static_warhead_warns_player(void)
{
	static_warhead_world();
	g_test_objects[TARGET_CRAFT].player_owner_idx = 2;
	uint16_t slot = laser_createprojectilefromstatic(MINE, TARGET_CRAFT);
	XVT_ASSERT_INT_EQ(slot, OTHER_SHOT_START);
	XVT_ASSERT_INT_EQ(g_players[2].pending_action_id, 1);
	XVT_ASSERT_INT_EQ(g_players[2].pending_action_issuer_player_idx,
			  UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_players[2].pending_action_param, slot);
	XVT_ASSERT_INT_EQ(g_players[2].pending_action_timer, 1416);
}

/* With every other-shot slot taken, the warhead takes over a cannon shot of
 * the group's team. Warheads, a cannon shot of team 0 and one of team 1
 * fill the range; the team 1 cannon shot in slot 170 is taken over. */
static void check_static_takes_team_cannon_shot(void)
{
	static_warhead_world();
	for (int i = OTHER_SHOT_START; i < SHOT_END; ++i) {
		place_shot(i, CONCUSSION_MISSILE, 1, 0, UINT16_MAX);
	}
	place_shot(165, PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0, 0, UINT16_MAX);
	place_shot(170, PROJECTILE_OBJECT_TYPE_IMPERIAL_LASER, 1, 0,
		   UINT16_MAX);
	XVT_ASSERT_INT_EQ(laser_createprojectilefromstatic(MINE, TARGET_CRAFT),
			  170);
	XVT_ASSERT_INT_EQ(g_test_objects[170].object_type,
			  g_warhead_type_ids[3]);
	XVT_ASSERT_INT_EQ(g_test_objects[165].object_type,
			  PROJECTILE_OBJECT_TYPE_REBEL_LASER);
}

/* With every other-shot slot taken and no cannon shot of the group's team,
 * nothing is fired and nothing is taken over. */
static void check_static_no_slot(void)
{
	static_warhead_world();
	for (int i = OTHER_SHOT_START; i < SHOT_END; ++i) {
		place_shot(i, CONCUSSION_MISSILE, 1, 0, UINT16_MAX);
	}
	place_shot(165, PROJECTILE_OBJECT_TYPE_REBEL_LASER, 0, 0, UINT16_MAX);
	XVT_ASSERT_INT_EQ(laser_createprojectilefromstatic(MINE, TARGET_CRAFT),
			  UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_test_objects[165].object_type,
			  PROJECTILE_OBJECT_TYPE_REBEL_LASER);
	XVT_ASSERT_INT_EQ(g_test_objects[170].object_type, CONCUSSION_MISSILE);
}

/* Known failure static_warhead_takes_impact_effect, issue #41: with every
 * other-shot slot taken, the function promises to take over a cannon shot of
 * the group's team. A shot that hit a craft stays in its slot as an impact
 * effect (object type 129), which is no shot at all; the search reads
 * warhead_class at index -8 for it, past the start of the table, and the
 * sanitizer stops the program. The function's own comment describes the
 * missing check, so this check rests on the issue. Every slot holds an
 * impact effect of team 1: nothing should be fired. */
static void check_static_skips_impact_effects(void)
{
	static_warhead_world();
	for (int i = OTHER_SHOT_START; i < SHOT_END; ++i) {
		place_shot(i, IMPACT_EFFECT, 1, 0, UINT16_MAX);
		g_test_mobiles[i].family = 5;
	}
	XVT_ASSERT_INT_EQ(laser_createprojectilefromstatic(MINE, TARGET_CRAFT),
			  UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_test_objects[OTHER_SHOT_START].object_type,
			  IMPACT_EFFECT);
}

/* ------------------------------------------------------------------------ */
/* Countermeasures. */

enum { OWNER = 0, FIRST_FREE = OTHER_SHOT_START + 3 };

/* An AI X-wing of group 0 in slot OWNER at the origin, flying at speed 100
 * with 5 countermeasures. */
static struct craft_data *countermeasure_world(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(OWNER, 0, 0, 0, 0);
	g_test_mobiles[OWNER].speed = 100;
	craft->cm_ammo_count = 5;
	return craft;
}

/* A flare flies backward from its owner at half its type's speed, with the
 * type's speed plus the owner's as its cruise speed and the type's damage
 * plus the owner's speed; it copies the owner's IFF and team, uses one
 * countermeasure and sets the countermeasure cooldown to 472. With two
 * warheads homing on the owner and no flare chasing either, it homes at
 * tier 6 on the nearer one. */
static void check_flare_takes_nearest_warhead(void)
{
	struct craft_data *craft = countermeasure_world();
	g_test_mobiles[OWNER].iff = 2;
	g_test_mobiles[OWNER].team = 3;
	place_shot(OTHER_SHOT_START, CONCUSSION_MISSILE, 1, 5000, OWNER);
	place_shot(OTHER_SHOT_START + 1, CONCUSSION_MISSILE, 1, 1000, OWNER);
	place_shot(OTHER_SHOT_START + 2, CONCUSSION_MISSILE, 1, 500, 9);
	g_test_objects[OTHER_SHOT_START + 1].object_signature = 0x77;
	int slot = laser_createcountermeasureprojectile(OWNER, FLARE);
	XVT_ASSERT_INT_EQ(slot, FIRST_FREE);
	int row = FLARE - PROJECTILE_OBJECT_TYPE_FIRST;
	struct object_record *flare = &g_test_objects[slot];
	struct warhead_guidance_state *guidance =
		&g_test_guidance[slot - SHOT_START];
	XVT_ASSERT_INT_EQ(flare->object_type, FLARE);
	XVT_ASSERT_INT_EQ(flare->mobj->speed,
			  g_projectile_type_data.speed[row] / 2);
	XVT_ASSERT_INT_EQ(guidance->cruise_speed,
			  g_projectile_type_data.speed[row] + 100);
	XVT_ASSERT_INT_EQ(flare->mobj->damage_amount,
			  g_projectile_type_data.damage[row] + 100);
	XVT_ASSERT_INT_EQ(flare->mobj->iff, 2);
	XVT_ASSERT_INT_EQ(flare->mobj->team, 3);
	XVT_ASSERT_INT_EQ(flare->yaw, 0x8000);
	XVT_ASSERT_INT_EQ(craft->cm_ammo_count, 4);
	XVT_ASSERT_INT_EQ(craft->cm_fire_cooldown_timer, 472);
	XVT_ASSERT_INT_EQ(guidance->target_obj_idx, OTHER_SHOT_START + 1);
	XVT_ASSERT_INT_EQ(guidance->target_signature, 0x77);
	XVT_ASSERT_INT_EQ(guidance->homing_tier, 6);
	XVT_ASSERT_INT_EQ(flare->mobj->lifetime_timer,
			  laser_get_projectile_lifetime_ticks(FLARE));
}

/* With no warhead after the owner, a flare homes on the nearest active craft
 * closer than 0x8000 whose AI targets the owner, and a shot aimed at a craft
 * lives half as long. A group with status 21 spends no countermeasure. */
static void check_flare_takes_pursuing_craft(void)
{
	struct craft_data *craft = countermeasure_world();
	g_mission_flight_groups[0].fg.status1 = 21;
	place_craft(5, 1, 0, 20000, 0)->ai_controller.target_obj_idx = OWNER;
	place_craft(6, 1, 0, 9000, 0)->ai_controller.target_obj_idx = OWNER;
	place_craft(7, 1, 0, 3000, 0)->ai_controller.target_obj_idx = 5;
	place_craft(8, 1, 0, 0x9000, 0)->ai_controller.target_obj_idx = OWNER;
	int slot = laser_createcountermeasureprojectile(OWNER, FLARE);
	XVT_ASSERT_INT_EQ(slot, OTHER_SHOT_START);
	struct warhead_guidance_state *guidance =
		&g_test_guidance[slot - SHOT_START];
	XVT_ASSERT_INT_EQ(guidance->target_obj_idx, 6);
	XVT_ASSERT_INT_EQ(guidance->homing_tier, 6);
	XVT_ASSERT_INT_EQ(g_test_objects[slot].mobj->lifetime_timer,
			  laser_get_projectile_lifetime_ticks(FLARE) >> 1);
	XVT_ASSERT_INT_EQ(craft->cm_ammo_count, 5);

	/* A pursuer in craft slot 0 counts as a craft too: the flare from an
	 * owner in slot 3 homes on it and lives half as long. */
	countermeasure_world();
	place_craft(3, 0, 0, 0, 0)->cm_ammo_count = 5;
	g_test_objects[OWNER].world_y = 4000;
	g_test_craft[OWNER].ai_controller.target_obj_idx = 3;
	slot = laser_createcountermeasureprojectile(3, FLARE);
	XVT_ASSERT_INT_EQ(g_test_guidance[slot - SHOT_START].target_obj_idx,
			  OWNER);
	XVT_ASSERT_INT_EQ(g_test_objects[slot].mobj->lifetime_timer,
			  laser_get_projectile_lifetime_ticks(FLARE) >> 1);

	/* A pursuer 0x9000 away is too far: the flare has no target and
	 * lives its whole life. */
	countermeasure_world();
	place_craft(8, 1, 0, 0x9000, 0)->ai_controller.target_obj_idx = OWNER;
	slot = laser_createcountermeasureprojectile(OWNER, FLARE);
	XVT_ASSERT_INT_EQ(g_test_guidance[slot - SHOT_START].target_obj_idx,
			  UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_test_guidance[slot - SHOT_START].homing_tier, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[slot].mobj->lifetime_timer,
			  laser_get_projectile_lifetime_ticks(FLARE));
}

/* Known failure second_flare_same_warhead, issue #182: the function promises
 * a flare goes for the nearest warhead after its owner that no flare chases
 * yet, and for a chased one only when there is none. Two warheads home on the
 * owner, the nearer one already chased by a flare; the new flare should go
 * for the farther one. The count of chasing flares tests the warhead's own
 * type instead of each shot's, so it is always 0 and the flare goes for the
 * nearer one. The function's comment describes the wrong test; this check
 * rests on the issue. */
static void check_second_flare_takes_unchased_warhead(void)
{
	countermeasure_world();
	place_shot(OTHER_SHOT_START, CONCUSSION_MISSILE, 1, 1000, OWNER);
	place_shot(OTHER_SHOT_START + 1, CONCUSSION_MISSILE, 1, 5000, OWNER);
	place_shot(OTHER_SHOT_START + 2, FLARE, 0, 200, OTHER_SHOT_START);
	int slot = laser_createcountermeasureprojectile(OWNER, FLARE);
	XVT_ASSERT_INT_EQ(slot, FIRST_FREE);
	XVT_ASSERT_INT_EQ(g_test_guidance[slot - SHOT_START].target_obj_idx,
			  OTHER_SHOT_START + 1);
}

/* ------------------------------------------------------------------------ */
/* Cannon groups. */

/* An AI X-wing in slot OWNER whose cannon group 0 holds weapon slots 0 to 3,
 * each with a weapon of type group_type and the given charge; it targets
 * craft 9. */
static struct craft_data *cannon_world(int group_type, int charge)
{
	fresh_world();
	struct craft_data *craft = place_craft(OWNER, 0, 0, 0, 0);
	struct model_def *model = xwing_model();
	model->laser_group_weapon_type[0] = (uint8_t)group_type;
	model->laser_group_first_slot[0] = 0;
	model->laser_group_last_slot[0] = 3;
	craft->laser_slot_count = 4;
	craft->cannon_group_count = 1;
	for (int slot = 0; slot < 4; ++slot) {
		craft->weapon_slots[slot].projectile_type_id =
			(uint8_t)group_type;
		craft->weapon_slots[slot].laser_charge = (int8_t)charge;
	}
	craft->ai_controller.target_obj_idx = 9;
	place_craft(9, 1, 0, 50000, 0);
	g_test_objects[9].object_signature = 0x99;
	return craft;
}

static int shots_in_other_range(void)
{
	int count = 0;
	for (int i = OTHER_SHOT_START; i < SHOT_END; ++i) {
		if (g_test_objects[i].object_type != 0) {
			++count;
		}
	}
	return count;
}

/* Link mode 1 fires the next slot alone and moves next_slot on; each shot
 * costs an AI craft 1 charge and adds 47 ticks plus 2 per call to the group's
 * cooldown and next fire time. The shot is aimed at the AI's target. */
static void check_cannon_single_fire(void)
{
	struct craft_data *craft =
		cannon_world(PROJECTILE_OBJECT_TYPE_REBEL_LASER, 20);
	craft->laser_state.link_mode[0] = 1;
	craft->laser_state.next_slot[0] = 2;
	laser_firelasersystem(OWNER, 0);
	XVT_ASSERT_INT_EQ(shots_in_other_range(), 1);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[2].laser_charge, 19);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[1].laser_charge, 20);
	XVT_ASSERT_INT_EQ(craft->laser_state.next_slot[0], 3);
	XVT_ASSERT_INT_EQ(craft->laser_state.fire_cooldown_ticks[0], 49);
	XVT_ASSERT_INT_EQ(craft->laser_state.next_fire_timestamp[0], 49);
	XVT_ASSERT_INT_EQ(craft->weapon_stats.laser_shots_fired, 1);
	struct warhead_guidance_state *guidance =
		&g_test_guidance[OTHER_SHOT_START - SHOT_START];
	XVT_ASSERT_INT_EQ(guidance->target_obj_idx, 9);
	XVT_ASSERT_INT_EQ(guidance->target_signature, 0x99);
	XVT_ASSERT_INT_EQ(guidance->source_player_idx, -1);
}

/* Link mode 3 fires every slot with a weapon and charge above 0. */
static void check_cannon_all_fire(void)
{
	struct craft_data *craft =
		cannon_world(PROJECTILE_OBJECT_TYPE_REBEL_LASER, 20);
	craft->laser_state.link_mode[0] = 3;
	craft->weapon_slots[1].laser_charge = 0;
	craft->weapon_slots[3].projectile_type_id = 0;
	laser_firelasersystem(OWNER, 0);
	XVT_ASSERT_INT_EQ(shots_in_other_range(), 2);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[0].laser_charge, 19);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[2].laser_charge, 19);
	XVT_ASSERT_INT_EQ(craft->laser_state.fire_cooldown_ticks[0],
			  47 * 2 + 2);
}

/* Link mode 2 fires half the group's slots, every other one from the next
 * slot, and the next call starts from the slot after. The group here holds
 * five slots, so half is two. */
static void check_cannon_pair_fire(void)
{
	struct craft_data *craft =
		cannon_world(PROJECTILE_OBJECT_TYPE_REBEL_LASER, 20);
	xwing_model()->laser_group_last_slot[0] = 4;
	craft->laser_slot_count = 5;
	craft->weapon_slots[4] = craft->weapon_slots[0];
	craft->laser_state.link_mode[0] = 2;
	craft->laser_state.next_slot[0] = 0;
	laser_firelasersystem(OWNER, 0);
	XVT_ASSERT_INT_EQ(shots_in_other_range(), 2);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[0].laser_charge, 19);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[1].laser_charge, 20);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[2].laser_charge, 19);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[3].laser_charge, 20);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[4].laser_charge, 20);
	laser_firelasersystem(OWNER, 0);
	XVT_ASSERT_INT_EQ(shots_in_other_range(), 4);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[1].laser_charge, 19);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[3].laser_charge, 19);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[0].laser_charge, 19);
}

/* A slot charged to 64 or more fires one type higher: a Rebel laser group
 * fires a Rebel turbo laser; below 64 it fires its own type. */
static void check_cannon_charged_type(void)
{
	struct craft_data *craft =
		cannon_world(PROJECTILE_OBJECT_TYPE_REBEL_LASER, 63);
	craft->laser_state.link_mode[0] = 1;
	laser_firelasersystem(OWNER, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[OTHER_SHOT_START].object_type,
			  PROJECTILE_OBJECT_TYPE_REBEL_LASER);
	craft = cannon_world(PROJECTILE_OBJECT_TYPE_REBEL_LASER, 64);
	craft->laser_state.link_mode[0] = 1;
	laser_firelasersystem(OWNER, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[OTHER_SHOT_START].object_type,
			  PROJECTILE_OBJECT_TYPE_REBEL_TURBO_LASER);
}

/* With the S-foils closed nothing fires. */
static void check_cannon_s_foils_closed(void)
{
	struct craft_data *craft =
		cannon_world(PROJECTILE_OBJECT_TYPE_REBEL_LASER, 20);
	craft->laser_state.link_mode[0] = 3;
	craft->s_foil_state = 1;
	laser_firelasersystem(OWNER, 0);
	XVT_ASSERT_INT_EQ(shots_in_other_range(), 0);
	XVT_ASSERT_INT_EQ(craft->laser_state.fire_cooldown_ticks[0], 0);
}

/* Known failure charged_ion_turbo_fires_torpedo, issue #183: a charged slot
 * fires "one type higher", which the comment over the function gives as the
 * intent, so this check rests on the issue. For an ion turbo laser group
 * (142) the next type is a proton torpedo (143), a warhead; a cannon group
 * should only ever fire cannon shots. */
static void check_charged_ion_turbo_laser_stays_cannon(void)
{
	struct craft_data *craft =
		cannon_world(PROJECTILE_OBJECT_TYPE_ION_TURBO_LASER, 100);
	craft->laser_state.link_mode[0] = 1;
	laser_firelasersystem(OWNER, 0);
	int type = g_test_objects[OTHER_SHOT_START].object_type;
	XVT_ASSERT_TRUE(type != 0);
	XVT_ASSERT_INT_EQ(is_warhead(type), 0);
}

/* ------------------------------------------------------------------------ */
/* Shots from a craft's hardpoints. */

/* Player 2 flies an X-wing in slot OWNER at 1000, 2000, 3000, flying at speed
 * 300 along its forward axis, X; hardpoint 1 sits at 10, 20, 30 on the
 * model. */
static struct craft_data *player_shot_world(void)
{
	fresh_world();
	struct craft_data *craft = place_craft(OWNER, 0, 1000, 2000, 3000);
	g_test_objects[OWNER].player_owner_idx = 2;
	g_test_objects[OWNER].yaw = 0x1234;
	g_test_objects[OWNER].pitch = 0x0100;
	g_test_objects[OWNER].roll = 0x0200;
	g_test_mobiles[OWNER].iff = 4;
	g_test_mobiles[OWNER].speed = 300;
	g_test_mobiles[OWNER].cached_fwd_x = 0x7FFF;
	g_players[2].object_index = OWNER;
	g_players[2].lockstep_timestamp = 4444;
	struct model_def *model = xwing_model();
	model->weapon_hardpoints[1].x = 10;
	model->weapon_hardpoints[1].y = 20;
	model->weapon_hardpoints[1].z = 30;
	return craft;
}

/* A player's cannon shot takes the first free slot of that player's 12
 * (player 2: slots 56 to 67), copies the firer's IFF and angles, flies at its
 * type's speed plus the firer's, does its type's damage plus the firer's
 * speed, lives its type's life, starts at the player's lockstep_timestamp,
 * and gets a guidance record with no target and its speed as cruise
 * speed. */
static void check_player_cannon_shot(void)
{
	player_shot_world();
	g_test_objects[56].object_type = PROJECTILE_OBJECT_TYPE_REBEL_LASER;
	int type = PROJECTILE_OBJECT_TYPE_REBEL_LASER;
	int row = type - PROJECTILE_OBJECT_TYPE_FIRST;
	int slot = laser_createprojectile(OWNER, 1, type);
	XVT_ASSERT_INT_EQ(slot, 57);
	struct object_record *shot = &g_test_objects[slot];
	struct warhead_guidance_state *guidance =
		&g_test_guidance[slot - SHOT_START];
	XVT_ASSERT_INT_EQ(shot->object_type, type);
	XVT_ASSERT_INT_EQ(shot->genus_id, CRAFT_GENUS_PLAYER_PROJECTILE);
	XVT_ASSERT_INT_EQ(shot->mobj->family, 1);
	XVT_ASSERT_INT_EQ(shot->mobj->iff, 4);
	XVT_ASSERT_INT_EQ(shot->yaw, 0x1234);
	XVT_ASSERT_INT_EQ(shot->pitch, 0x0100);
	XVT_ASSERT_INT_EQ(shot->roll, 0x0200);
	XVT_ASSERT_INT_EQ(shot->mobj->speed,
			  g_projectile_type_data.speed[row] + 300);
	XVT_ASSERT_INT_EQ(shot->mobj->damage_amount,
			  g_projectile_type_data.damage[row] + 300);
	XVT_ASSERT_INT_EQ(shot->mobj->lifetime_timer,
			  laser_get_projectile_lifetime_ticks(type));
	XVT_ASSERT_INT_EQ(shot->mobj->sim_state_timestamp, 4444);
	XVT_ASSERT_INT_EQ(shot->mobj->source_obj_idx, OWNER);
	XVT_ASSERT_INT_EQ(guidance->target_obj_idx, UINT16_MAX);
	XVT_ASSERT_INT_EQ(guidance->homing_tier, 0);
	XVT_ASSERT_INT_EQ(guidance->cruise_speed, shot->mobj->speed);
	XVT_ASSERT_TRUE(shot->mobj->p_warhead_guidance == guidance);
	/* The shot is moved launch_offset along the firer's forward axis from
	 * where it left the hardpoint. */
	XVT_ASSERT_TRUE(shot->world_x > shot->mobj->prev_world_x);
	XVT_ASSERT_INT_EQ(shot->world_y, shot->mobj->prev_world_y);
	XVT_ASSERT_INT_EQ(shot->world_z, shot->mobj->prev_world_z);
}

/* A player's warhead takes a free slot among the last 4 of the player's 12;
 * with all 12 taken a shot goes to the 32 shared slots from 128; with those
 * taken too there is no slot and -1 comes back. */
static void check_player_shot_slots(void)
{
	player_shot_world();
	XVT_ASSERT_INT_EQ(laser_createprojectile(OWNER, 1, CONCUSSION_MISSILE),
			  64);
	for (int i = 56; i < 68; ++i) {
		g_test_objects[i].object_type = CONCUSSION_MISSILE;
	}
	XVT_ASSERT_INT_EQ(laser_createprojectile(
				  OWNER, 1, PROJECTILE_OBJECT_TYPE_REBEL_LASER),
			  128);
	for (int i = 128; i < OTHER_SHOT_START; ++i) {
		g_test_objects[i].object_type = CONCUSSION_MISSILE;
	}
	XVT_ASSERT_INT_EQ(laser_createprojectile(
				  OWNER, 1, PROJECTILE_OBJECT_TYPE_REBEL_LASER),
			  -1);
	/* An AI craft's shot takes the other-shot range. */
	g_test_objects[OWNER].player_owner_idx = -1;
	XVT_ASSERT_INT_EQ(laser_createprojectile(
				  OWNER, 1, PROJECTILE_OBJECT_TYPE_REBEL_LASER),
			  OTHER_SHOT_START);
}

/* A warhead from a starship is moved launch_offset up, pointing level, from
 * a hardpoint at or above the middle, and down, pointing straight down,
 * from one below it. */
static void check_starship_warhead_offset(void)
{
	player_shot_world();
	g_test_objects[OWNER].player_owner_idx = -1;
	g_test_objects[OWNER].genus_id = CRAFT_GENUS_STARSHIP;
	int offset = g_projectile_type_data
			     .launch_offset[CONCUSSION_MISSILE -
					    PROJECTILE_OBJECT_TYPE_FIRST];
	int slot = laser_createprojectile(OWNER, 1, CONCUSSION_MISSILE);
	struct object_record *shot = &g_test_objects[slot];
	XVT_ASSERT_INT_EQ(shot->world_z, shot->mobj->prev_world_z + offset);
	XVT_ASSERT_INT_EQ(shot->world_x, shot->mobj->prev_world_x);
	XVT_ASSERT_INT_EQ(shot->pitch, 0);

	xwing_model()->weapon_hardpoints[1].z = -30;
	slot = laser_createprojectile(OWNER, 1, CONCUSSION_MISSILE);
	shot = &g_test_objects[slot];
	XVT_ASSERT_INT_EQ(shot->world_z, shot->mobj->prev_world_z - offset);
	XVT_ASSERT_INT_EQ(shot->pitch, 0x8000);

	/* A hardpoint at the middle counts as above it. */
	xwing_model()->weapon_hardpoints[1].z = 0;
	slot = laser_createprojectile(OWNER, 1, CONCUSSION_MISSILE);
	shot = &g_test_objects[slot];
	XVT_ASSERT_INT_EQ(shot->world_z, shot->mobj->prev_world_z + offset);
	XVT_ASSERT_INT_EQ(shot->pitch, 0);
}

/* ------------------------------------------------------------------------ */
/* Warheads from launchers. */

enum { TEAM_SCORE_START = 1000, PROTON = WARHEAD_OBJECT_TYPE_PROTON_TORPEDO };

/* An X-wing in slot OWNER whose launcher 0 holds weapon slots 4 and 5 with
 * proton torpedoes, the given rounds in each; it targets component 3 of craft
 * 9. Team 0's mission score starts at 1,000. */
static struct craft_data *launcher_world(int rounds_4, int rounds_5)
{
	fresh_world();
	struct craft_data *craft = place_craft(OWNER, 0, 0, 0, 0);
	struct model_def *model = xwing_model();
	model->warhead_launcher_first_slot[0] = 4;
	model->warhead_launcher_last_slot[0] = 5;
	craft->warhead_launcher_count = 1;
	craft->warhead_slot_type_ids[0] = PROTON;
	craft->weapon_slots[4].projectile_type_id = PROTON;
	craft->weapon_slots[5].projectile_type_id = PROTON;
	craft->weapon_slots[4].ammo_count = (uint8_t)rounds_4;
	craft->weapon_slots[5].ammo_count = (uint8_t)rounds_5;
	craft->ai_controller.target_obj_idx = 9;
	craft->ai_controller.target_component = 3;
	place_craft(9, 1, 0, 50000, 0);
	g_test_objects[9].object_signature = 0x99;
	g_flight_mission_state.runtime.team_scores[TEAM_SCORE_MISSION][0] =
		TEAM_SCORE_START;
	g_cur_craft = craft;
	return craft;
}

static int proton_points(void)
{
	return g_projectile_type_data
		.warhead_point_value[PROTON - PROJECTILE_OBJECT_TYPE_FIRST];
}

/* An AI craft's warhead from launcher 0: counted in weapon_stats, its point
 * value off the team's mission score, one round used, a homing tier of the
 * craft's whole seconds of lock, the AI's target, component and signature,
 * no player, and flag bit 0x80 set because the other slot now has more
 * rounds. It returns the shot's guidance index. */
static void check_ai_missile(void)
{
	struct craft_data *craft = launcher_world(3, 3);
	craft->warhead_lock_ticks = 2 * SIMULATION_TICKS_PER_SECOND + 10;
	int index = laser_firemissile(OWNER, 4, PROTON, 0);
	XVT_ASSERT_INT_EQ(index, OTHER_SHOT_START - SHOT_START);
	struct warhead_guidance_state *guidance = &g_test_guidance[index];
	XVT_ASSERT_INT_EQ(g_test_objects[OTHER_SHOT_START].object_type, PROTON);
	XVT_ASSERT_INT_EQ(craft->weapon_stats.warheads_fired, 1);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
				  .team_scores[TEAM_SCORE_MISSION][0],
			  TEAM_SCORE_START - proton_points());
	XVT_ASSERT_INT_EQ(craft->weapon_slots[4].ammo_count, 2);
	XVT_ASSERT_INT_EQ(guidance->homing_tier, 2);
	XVT_ASSERT_INT_EQ(guidance->target_obj_idx, 9);
	XVT_ASSERT_INT_EQ(guidance->target_component_idx, 3);
	XVT_ASSERT_INT_EQ(guidance->target_signature, 0x99);
	XVT_ASSERT_INT_EQ(guidance->source_player_idx, -1);
	XVT_ASSERT_INT_EQ(craft->warhead_launcher_flags[0] & 0x80, 0x80);

	/* The next one from slot 5 leaves slot 4 with as many rounds, so the
	 * bit clears; a lock of more than 6 seconds homes at tier 6. */
	craft->warhead_lock_ticks = 20 * SIMULATION_TICKS_PER_SECOND;
	index = laser_firemissile(OWNER, 5, PROTON, 0);
	XVT_ASSERT_INT_EQ(g_test_guidance[index].homing_tier, 6);
	XVT_ASSERT_INT_EQ(craft->warhead_launcher_flags[0] & 0x80, 0);
}

/* A slot with no rounds, or no weapon, fires nothing and returns -1. A group
 * with status 21 uses no round. A launcher past the first two leaves the
 * homing tier at 0. */
static void check_missile_without_rounds(void)
{
	struct craft_data *craft = launcher_world(0, 3);
	XVT_ASSERT_INT_EQ(laser_firemissile(OWNER, 4, PROTON, 0), -1);
	XVT_ASSERT_INT_EQ(shots_in_other_range(), 0);
	craft->weapon_slots[5].projectile_type_id = 0;
	XVT_ASSERT_INT_EQ(laser_firemissile(OWNER, 5, PROTON, 0), -1);
	XVT_ASSERT_INT_EQ(craft->weapon_stats.warheads_fired, 0);

	craft = launcher_world(3, 3);
	g_mission_flight_groups[0].fg.status2 = 21;
	craft->warhead_lock_ticks = 3 * SIMULATION_TICKS_PER_SECOND;
	int index = laser_firemissile(OWNER, 4, PROTON, 2);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[4].ammo_count, 3);
	XVT_ASSERT_INT_EQ(g_test_guidance[index].homing_tier, 0);
}

/* A player's warhead takes the player's target and selected component, and
 * its point value off the player's mission score too; a target flown by a
 * player is warned. */
static void check_player_missile(void)
{
	launcher_world(3, 3);
	g_test_objects[OWNER].player_owner_idx = 2;
	g_players[2].object_index = OWNER;
	g_players[2].current_target_object_idx = 9;
	g_players[2].selected_target_component = 4;
	g_players[2].mission_stats.mission_score = 500;
	g_test_objects[9].player_owner_idx = 5;
	int index = laser_firemissile(OWNER, 4, PROTON, 0);
	XVT_ASSERT_INT_EQ(index, 64 - SHOT_START);
	XVT_ASSERT_INT_EQ(g_test_guidance[index].target_obj_idx, 9);
	XVT_ASSERT_INT_EQ(g_test_guidance[index].target_component_idx, 4);
	XVT_ASSERT_INT_EQ(g_test_guidance[index].target_signature, 0x99);
	XVT_ASSERT_INT_EQ(g_test_guidance[index].source_player_idx, 2);
	XVT_ASSERT_INT_EQ(g_players[2].warheads_fired, 1);
	XVT_ASSERT_INT_EQ(g_players[2].mission_stats.mission_score,
			  500 - proton_points());
	XVT_ASSERT_INT_EQ(g_players[5].pending_action_id, 1);
	XVT_ASSERT_INT_EQ(g_players[5].pending_action_param, 64);
}

/* A launcher whose flags' low 7 bits are 3 fires both its slots; otherwise
 * bit 0x80 picks the second slot. Each call adds 472 ticks to the launcher's
 * cooldown, whether or not anything fired. */
static void check_warhead_system(void)
{
	struct craft_data *craft = launcher_world(3, 3);
	craft->warhead_launcher_flags[0] = 3;
	laser_firewarheadsystem(OWNER, 0);
	XVT_ASSERT_INT_EQ(shots_in_other_range(), 2);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[4].ammo_count, 2);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[5].ammo_count, 2);
	XVT_ASSERT_INT_EQ(craft->warhead_launcher_cooldown_ticks[0], 472);

	craft = launcher_world(3, 3);
	craft->warhead_launcher_flags[0] = (int8_t)0x80;
	laser_firewarheadsystem(OWNER, 0);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[4].ammo_count, 3);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[5].ammo_count, 2);

	craft = launcher_world(0, 0);
	craft->warhead_launcher_cooldown_ticks[0] = 5;
	laser_firewarheadsystem(OWNER, 0);
	XVT_ASSERT_INT_EQ(shots_in_other_range(), 0);
	XVT_ASSERT_INT_EQ(craft->warhead_launcher_cooldown_ticks[0], 477);
}

/* ------------------------------------------------------------------------ */
/* Missile warnings. */

/* laser_warnplayer gives the player whose craft a shot targets pending
 * action 1 from no player, naming the shot's slot, for 1,416 ticks. A player
 * with an action already pending, or a target no player flies, is left
 * alone. */
static void check_warn_player(void)
{
	fresh_world();
	place_craft(5, 1, 0, 0, 0);
	g_test_objects[5].player_owner_idx = 4;
	g_test_guidance[10].target_obj_idx = 5;
	laser_warnplayer(10);
	XVT_ASSERT_INT_EQ(g_players[4].pending_action_id, 1);
	XVT_ASSERT_INT_EQ(g_players[4].pending_action_issuer_player_idx,
			  UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_players[4].pending_action_param, 10 + SHOT_START);
	XVT_ASSERT_INT_EQ(g_players[4].pending_action_timer, 1416);

	g_test_guidance[11].target_obj_idx = 5;
	g_players[4].pending_action_id = 3;
	laser_warnplayer(11);
	XVT_ASSERT_INT_EQ(g_players[4].pending_action_id, 3);
	XVT_ASSERT_INT_EQ(g_players[4].pending_action_param, 10 + SHOT_START);

	g_test_objects[5].player_owner_idx = -1;
	struct player_data before[8];
	memcpy(before, g_players, sizeof before);
	laser_warnplayer(10);
	XVT_ASSERT_INT_EQ(memcmp(before, g_players, sizeof before), 0);
}

/* ------------------------------------------------------------------------ */
/* A player's trigger. */

/* Player 2 flies the cannon world's X-wing with working cannons and
 * launchers, cannon group 0 selected. */
static struct craft_data *trigger_world(void)
{
	struct craft_data *craft =
		cannon_world(PROJECTILE_OBJECT_TYPE_REBEL_LASER, 20);
	g_test_objects[OWNER].player_owner_idx = 2;
	g_players[2].object_index = OWNER;
	g_players[2].current_target_object_idx = -1;
	craft->working_subsystems = CRAFT_SUBSYSTEM_FLAG_CANNONS |
				    CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER;
	craft->laser_state.link_mode[0] = 1;
	g_elapsed_ticks = 4;
	return craft;
}

/* In cannon mode the selected group fires once its cooldown is below 1.5
 * times the step's ticks; at or above that nothing fires. A player with no
 * craft fires nothing. */
static void check_trigger_cannon_cooldown(void)
{
	struct craft_data *craft = trigger_world();
	craft->laser_state.fire_cooldown_ticks[0] = 5;
	laser_fireplayerweapon(2);
	XVT_ASSERT_INT_EQ(g_test_objects[56].object_type,
			  PROJECTILE_OBJECT_TYPE_REBEL_LASER);
	XVT_ASSERT_INT_EQ(craft->laser_state.fire_cooldown_ticks[0], 5 + 49);
	/* A player's shot costs a craft other than a TIE Fighter or TIE
	 * Bomber 4 charge. */
	XVT_ASSERT_INT_EQ(craft->weapon_slots[0].laser_charge, 16);
	XVT_ASSERT_INT_EQ(craft->weapon_stats.laser_shots_fired, 1);
	XVT_ASSERT_INT_EQ(g_players[2].mission_stats.laser_shots_fired, 1);
	XVT_ASSERT_INT_EQ(g_test_guidance[56 - SHOT_START].source_player_idx,
			  2);

	craft = trigger_world();
	craft->laser_state.fire_cooldown_ticks[0] = 6;
	laser_fireplayerweapon(2);
	XVT_ASSERT_INT_EQ(g_test_objects[56].object_type, 0);
	XVT_ASSERT_INT_EQ(craft->laser_state.fire_cooldown_ticks[0], 6);

	trigger_world();
	g_players[2].object_index = -1;
	laser_fireplayerweapon(2);
	XVT_ASSERT_INT_EQ(g_test_objects[56].object_type, 0);
}

/* A jamming beam on the craft, with no active chaff, stops it firing. */
static void check_trigger_jammed(void)
{
	struct craft_data *craft = trigger_world();
	craft->beam_effect_accum[2] = 1;
	laser_fireplayerweapon(2);
	XVT_ASSERT_INT_EQ(g_test_objects[56].object_type, 0);

	craft = trigger_world();
	craft->beam_effect_accum[2] = 1;
	craft->cm_type_id = COUNTERMEASURE_TYPE_CHAFF;
	craft->chaff_active_seconds = 2;
	laser_fireplayerweapon(2);
	XVT_ASSERT_INT_EQ(g_test_objects[56].object_type,
			  PROJECTILE_OBJECT_TYPE_REBEL_LASER);
}

/* In warhead mode the selected launcher fires; once both its slots are empty
 * the player goes back to cannon group 0 with a 118-tick cooldown. */
static void check_trigger_warhead_mode(void)
{
	struct craft_data *craft = trigger_world();
	struct model_def *model = xwing_model();
	model->warhead_launcher_first_slot[0] = 4;
	craft->warhead_launcher_count = 1;
	craft->warhead_slot_type_ids[0] = PROTON;
	craft->weapon_slots[4].projectile_type_id = PROTON;
	craft->weapon_slots[5].projectile_type_id = PROTON;
	craft->weapon_slots[4].ammo_count = 1;
	g_players[2].selected_weapon_mode = 1;
	g_players[2].lockstep_timestamp = 1000;
	laser_fireplayerweapon(2);
	XVT_ASSERT_INT_EQ(g_test_objects[64].object_type, PROTON);
	XVT_ASSERT_INT_EQ(g_players[2].selected_weapon_mode, 0);
	XVT_ASSERT_INT_EQ(g_players[2].selected_weapon_bank, 0);
	XVT_ASSERT_INT_EQ(craft->laser_state.fire_cooldown_ticks[0], 118);
	XVT_ASSERT_INT_EQ(craft->laser_state.next_fire_timestamp[0], 1118);

	/* With rounds left in the other slot the player stays in warhead
	 * mode. */
	craft = trigger_world();
	xwing_model()->warhead_launcher_first_slot[0] = 4;
	craft->warhead_launcher_count = 1;
	craft->warhead_slot_type_ids[0] = PROTON;
	craft->weapon_slots[4].projectile_type_id = PROTON;
	craft->weapon_slots[5].projectile_type_id = PROTON;
	craft->weapon_slots[4].ammo_count = 1;
	craft->weapon_slots[5].ammo_count = 2;
	g_players[2].selected_weapon_mode = 1;
	laser_fireplayerweapon(2);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[4].ammo_count, 0);
	XVT_ASSERT_INT_EQ(g_players[2].selected_weapon_mode, 1);

	/* The launcher fires only once its cooldown is below 1.5 times the
	 * step's 4 ticks. */
	craft->warhead_launcher_cooldown_ticks[0] = 6;
	laser_fireplayerweapon(2);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[5].ammo_count, 2);
	craft->warhead_launcher_cooldown_ticks[0] = 5;
	laser_fireplayerweapon(2);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[5].ammo_count, 1);
}

/* ------------------------------------------------------------------------ */
/* The weapon step. */

enum { OVERDRIVE_ON = 0, OVERDRIVE_OFF = UINT16_MAX };

/* Player 3 flies an X-wing in slot OWNER with engine overdrive on; the local
 * player is player 0, flying nothing. Overdrive's message is pane type 1,
 * which logs it and, behind the waiting message in the ready pane, queues
 * it. */
static struct craft_data *overdrive_world(void)
{
	static char overdrive_text[] =
		"\001Engine overdrive boosters disengaged";
	fresh_world();
	struct craft_data *craft = place_craft(OWNER, 0, 0, 0, 0);
	g_test_objects[OWNER].player_owner_idx = 3;
	g_players[3].object_index = OWNER;
	g_players[3].current_target_object_idx = -1;
	g_local_player = 0;
	g_players[0].object_index = -1;
	craft->engine_overdrive_off = OVERDRIVE_ON;
	craft->laser_slot_count = 2;
	craft->weapon_fire_inhibit_timer = 1;
	g_str_in_flight_messages
		[IFMSG_285_ENGINE_OVERDRIVE_BOOSTERS_DISENGAGED] =
			overdrive_text;
	g_message_log_handle = g_message_log;
	g_ready_message_pane_queue[0].state_or_message_id = 5;
	g_ready_message_pane_queue[0].pane_type = 1;
	return craft;
}

/* On a power step a craft whose cannons hold no charge drops engine
 * overdrive; one with charge left keeps it. The power step resets its timer
 * to a second. */
static void check_overdrive_dropped_when_dry(void)
{
	struct craft_data *craft = overdrive_world();
	laser_weaponsfire();
	XVT_ASSERT_INT_EQ(craft->engine_overdrive_off, OVERDRIVE_OFF);
	XVT_ASSERT_INT_EQ(
		g_flight_global_countdown_timers.weapon_power_update_timer,
		SIMULATION_TICKS_PER_SECOND);

	craft = overdrive_world();
	craft->weapon_slots[1].laser_charge = 1;
	laser_weaponsfire();
	XVT_ASSERT_INT_EQ(craft->engine_overdrive_off, OVERDRIVE_ON);

	/* Between power steps nothing is dropped. */
	craft = overdrive_world();
	g_flight_global_countdown_timers.weapon_power_update_timer = 10;
	laser_weaponsfire();
	XVT_ASSERT_INT_EQ(craft->engine_overdrive_off, OVERDRIVE_ON);
}

/* An AI craft with a burst under way fires its group when the group's
 * cooldown is below the step's ticks, and the burst's end sets link_mode back
 * to 0; it counts its cooldowns down by the step's ticks, to no less than 0.
 * A craft whose weapon_fire_inhibit_timer is set fires nothing and counts
 * nothing down. */
static void check_step_ai_burst(void)
{
	struct craft_data *craft =
		cannon_world(PROJECTILE_OBJECT_TYPE_REBEL_LASER, 20);
	craft->working_subsystems = CRAFT_SUBSYSTEM_FLAG_CANNONS;
	craft->laser_state.link_mode[0] = 1;
	craft->laser_state.burst_remaining[0] = 2;
	craft->laser_state.fire_cooldown_ticks[0] = 3;
	craft->warhead_launcher_count = 2;
	craft->warhead_launcher_cooldown_ticks[0] = 10;
	craft->warhead_launcher_cooldown_ticks[1] = 2;
	g_elapsed_ticks = 4;
	laser_weaponsfire();
	XVT_ASSERT_INT_EQ(shots_in_other_range(), 1);
	XVT_ASSERT_INT_EQ(craft->laser_state.burst_remaining[0], 1);
	XVT_ASSERT_INT_EQ(craft->laser_state.link_mode[0], 1);
	XVT_ASSERT_INT_EQ(craft->warhead_launcher_cooldown_ticks[0], 6);
	XVT_ASSERT_INT_EQ(craft->warhead_launcher_cooldown_ticks[1], 0);

	/* The group's cooldown is now 49 plus 2 steps: no shot until it has
	 * counted down below 4. */
	XVT_ASSERT_INT_EQ(craft->laser_state.fire_cooldown_ticks[0], 49 + 8);
	XVT_ASSERT_INT_EQ(craft->laser_state.next_fire_timestamp[0], 49 + 8);
	laser_weaponsfire();
	XVT_ASSERT_INT_EQ(shots_in_other_range(), 1);
	XVT_ASSERT_INT_EQ(craft->laser_state.fire_cooldown_ticks[0], 53);

	craft->laser_state.fire_cooldown_ticks[0] = 0;
	laser_weaponsfire();
	XVT_ASSERT_INT_EQ(shots_in_other_range(), 2);
	XVT_ASSERT_INT_EQ(craft->laser_state.burst_remaining[0], 0);
	XVT_ASSERT_INT_EQ(craft->laser_state.link_mode[0], 0);

	craft = cannon_world(PROJECTILE_OBJECT_TYPE_REBEL_LASER, 20);
	craft->working_subsystems = CRAFT_SUBSYSTEM_FLAG_CANNONS;
	craft->laser_state.link_mode[0] = 1;
	craft->laser_state.burst_remaining[0] = 2;
	craft->laser_state.fire_cooldown_ticks[0] = 3;
	craft->weapon_fire_inhibit_timer = 1;
	laser_weaponsfire();
	XVT_ASSERT_INT_EQ(shots_in_other_range(), 0);
	XVT_ASSERT_INT_EQ(craft->laser_state.fire_cooldown_ticks[0], 3);
}

/* On a power step active chaff counts down a second; between power steps it
 * does not. */
static void check_step_chaff_countdown(void)
{
	struct craft_data *craft = overdrive_world();
	craft->engine_overdrive_off = OVERDRIVE_OFF;
	craft->chaff_active_seconds = 2;
	laser_weaponsfire();
	XVT_ASSERT_INT_EQ(craft->chaff_active_seconds, 1);
	g_flight_global_countdown_timers.weapon_power_update_timer = 3;
	laser_weaponsfire();
	XVT_ASSERT_INT_EQ(craft->chaff_active_seconds, 1);
}

/* A working mine takes half the step's ticks off its countdown; when the
 * countdown runs out it is reset to 236 and the mine looks for a target.
 * A mine whose working-systems word is 0 does nothing. The weapon step
 * runs every mine in the static slots. */
static void check_mine_countdown(void)
{
	fresh_world();
	struct object_record *mine = &g_test_objects[MINE];
	mine->object_type = CRAFT_SPECIES_MINE_TYPE_A;
	mine->genus_id = CRAFT_GENUS_MINE;
	mine->type_specific_word = 1023;
	mine->type_specific_byte[1] = 10;
	g_elapsed_ticks = 6;
	laser_update_mine_weapon_fire(MINE);
	XVT_ASSERT_INT_EQ(mine->type_specific_byte[1], 7);
	laser_weaponsfire();
	XVT_ASSERT_INT_EQ(mine->type_specific_byte[1], 4);
	mine->type_specific_byte[1] = 3;
	laser_update_mine_weapon_fire(MINE);
	XVT_ASSERT_INT_EQ(mine->type_specific_byte[1], 236);
	XVT_ASSERT_INT_EQ(shots_in_other_range(), 0);

	mine->type_specific_word = 0;
	mine->type_specific_byte[1] = 10;
	laser_update_mine_weapon_fire(MINE);
	XVT_ASSERT_INT_EQ(mine->type_specific_byte[1], 10);
}

/* Known failure overdrive_message_to_local_player, issue #181: messages and
 * sounds go to the player a call names, and the overdrive message about a
 * craft belongs to the player flying it, as the beam and chaff messages
 * beside it do. Player 3's craft runs dry; the call names the local player
 * instead, so the local player, flying nothing, is told their boosters
 * disengaged. Nothing should reach the local player's message log. */
static void check_overdrive_message_to_pilot(void)
{
	overdrive_world();
	laser_weaponsfire();
	XVT_ASSERT_INT_EQ(g_message_log_total_count, 0);
	XVT_ASSERT_INT_EQ(g_ready_message_queue_count, 0);
}

int main(int argc, char **argv)
{
	fail_after_seconds(20);
	g_saved_xwing_model = *xwing_model();
	g_message_log = memory_alloc_handle_zeroed(
		301 * sizeof(struct hud_in_flight_message_record), 0);
	XVT_ASSERT_TRUE(g_message_log != 0);
	/* "known-failure <check>" runs one check the code is known to fail; an
	 * unknown name runs nothing. */
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		static const struct {
			const char *name;
			void (*check)(void);
		} known_failures[] = {
			{"static_warhead_takes_impact_effect",
			 check_static_skips_impact_effects},
			{"second_flare_same_warhead",
			 check_second_flare_takes_unchased_warhead},
			{"charged_ion_turbo_fires_torpedo",
			 check_charged_ion_turbo_laser_stays_cannon},
			{"overdrive_message_to_local_player",
			 check_overdrive_message_to_pilot},
		};
		for (size_t i = 0;
		     i < sizeof known_failures / sizeof known_failures[0];
		     ++i) {
			if (strcmp(argv[2], known_failures[i].name) == 0) {
				known_failures[i].check();
			}
		}
		return 0;
	}
	check_lifetime_ticks();
	check_static_no_warhead();
	check_static_warhead_fields();
	check_static_warhead_warns_player();
	check_static_takes_team_cannon_shot();
	check_static_no_slot();
	check_flare_takes_nearest_warhead();
	check_flare_takes_pursuing_craft();
	check_cannon_single_fire();
	check_cannon_all_fire();
	check_cannon_pair_fire();
	check_cannon_charged_type();
	check_cannon_s_foils_closed();
	check_player_cannon_shot();
	check_player_shot_slots();
	check_starship_warhead_offset();
	check_ai_missile();
	check_missile_without_rounds();
	check_player_missile();
	check_warhead_system();
	check_warn_player();
	check_trigger_cannon_cooldown();
	check_trigger_jammed();
	check_trigger_warhead_mode();
	check_overdrive_dropped_when_dry();
	check_step_ai_burst();
	check_step_chaff_countdown();
	check_mine_countdown();
	return 0;
}
