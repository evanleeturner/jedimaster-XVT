/* Tests for xvt/flight/ai/paifight.c, the computer pilots' fighting orders:
 * the wingman's attack on its leader's target (order 14) and the warhead
 * defense of a craft with launchers (order 8). Each check builds the world it
 * needs in the game's own tables: six craft slots and four static slots of an
 * object table this file owns, the craft records, three flight groups and a
 * few plan names, with the computer pilots' context set field by field. No
 * game data is read.
 *
 * The object table has 65,535 slots, so slot 0xFFFF, the value that stands
 * for no object, lies just past its end, where a read of it is reported. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/ai/paifight.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"

enum {
	TEST_CRAFT_SLOTS = 6,  /* Slots 0 to 5 hold craft. */
	TEST_STATIC_SLOTS = 4, /* Slots 6 to 9 hold static objects. */
	TEST_SELF = 0,	       /* The craft the computer pilot flies. */
	TEST_LEADER = 1,       /* Its leader, in the same flight group. */
	TEST_ENEMY_FG = 1,     /* The flight group of craft 2 to 5. */
	TEST_STATIC_FG = 2,    /* The flight group of the static objects. */
	TEST_PLAN_NULL = 0,
	TEST_PLAN_DISABLE = 1,
	TEST_PLAN_CAPFREE = 2,
	TEST_LAUNCHER_SLOT = 0, /* The defending craft's one launcher. */
};

static struct object_record g_test_objects[UINT16_MAX];
static struct mobile_object g_test_mobiles[TEST_CRAFT_SLOTS];
static struct craft_data g_test_craft[TEST_CRAFT_SLOTS];

/* Craft 0 and 1 fly in flight group 0 on team 0, craft 1 leading craft 0;
 * craft 2 to 5 fly in flight group 1 on team 1, an enemy team. Static slots 6
 * to 9 hold objects of flight group 2. Every object lies within a few
 * thousand world units of craft 0, and no craft has an attacker, a threat or
 * a target. The context is craft 0's, at skill tier 0, on its order slot 0,
 * whose first target is flight group 1; its plan is nullpln. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_mission_flight_groups, 0,
	       3 * sizeof g_mission_flight_groups[0]);
	memset(g_mission_teams, 0, sizeof g_mission_teams);
	memset(g_players, 0, sizeof g_players);
	memset(g_plan_table, 0, sizeof g_plan_table);
	memset(&g_pai_context, 0, sizeof g_pai_context);
	g_object_table = g_test_objects;
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = TEST_CRAFT_SLOTS;
	g_region_main_object_slot_end = TEST_CRAFT_SLOTS;
	g_region_static_object_slot_count = TEST_STATIC_SLOTS;
	g_projectile_object_slot_start = TEST_CRAFT_SLOTS;
	g_projectile_object_slot_end = TEST_CRAFT_SLOTS;
	for (int i = 0; i < TEST_CRAFT_SLOTS + TEST_STATIC_SLOTS; ++i) {
		struct object_record *object = &g_test_objects[i];
		object->object_type = 1;
		object->object_signature = (uint16_t)(100 + i);
		object->world_x = 1000 * i;
		object->player_owner_idx = -1;
		if (i >= TEST_CRAFT_SLOTS) {
			object->flight_group_idx = TEST_STATIC_FG;
			object->type_specific_word = 1023;
			continue;
		}
		object->flight_group_idx = i < 2 ? 0 : TEST_ENEMY_FG;
		object->mobj = &g_test_mobiles[i];
		g_test_mobiles[i].team = i < 2 ? 0 : 1;
		g_test_mobiles[i].p_craft = &g_test_craft[i];
		g_test_craft[i].leader_obj_idx = UINT8_MAX;
		g_test_craft[i].working_subsystems = 1;
		g_test_craft[i].last_attacker_obj_idx = UINT16_MAX;
		g_test_craft[i].ai_flight.threat_obj_idx = UINT16_MAX;
		g_test_craft[i].player_command_avoid_target_obj_idx =
			UINT16_MAX;
		g_test_craft[i].ai_controller.target_obj_idx = UINT16_MAX;
		g_test_craft[i].ai_controller.candidate_target_idx = UINT16_MAX;
	}
	for (int fg = 0; fg < 3; ++fg) {
		g_mission_flight_groups[fg].fg.team =
			(uint8_t)(fg == 0 ? 0 : 1);
	}
	g_mission_flight_groups[0].fg.orders[0].target1_type = 1;
	g_mission_flight_groups[0].fg.orders[0].target1 = TEST_ENEMY_FG;
	g_mission_flight_groups[0].fg.orders[0].target1_or_target2 = 1;
	strcpy(g_plan_table[TEST_PLAN_NULL].name, "nullpln");
	strcpy(g_plan_table[TEST_PLAN_DISABLE].name, "disableldr1pln");
	strcpy(g_plan_table[TEST_PLAN_CAPFREE].name, "capfreeldr1pln");
	g_test_craft[TEST_SELF].leader_obj_idx = TEST_LEADER;
	g_cur_craft = &g_test_craft[TEST_SELF];
	g_pai_context.object_index = TEST_SELF;
	g_pai_context.craft = &g_test_craft[TEST_SELF];
	g_pai_context.controller = &g_test_craft[TEST_SELF].ai_controller;
	g_pai_context.leader_object_index = TEST_LEADER;
	g_pai_context.leader_or_self_craft = &g_test_craft[TEST_LEADER];
	g_pai_context.craft_flight_group_index = 0;
	g_pai_context.order_slot = 0;
}

static struct ai_controller *self_controller(void)
{
	return &g_test_craft[TEST_SELF].ai_controller;
}

/* The leader, a computer pilot, attacks the given object. */
static void leader_attacks(uint16_t target)
{
	g_test_craft[TEST_LEADER].ai_controller.maneuver_mode =
		AI_MANEUVER_MODE_ATTACK;
	g_test_craft[TEST_LEADER].ai_controller.target_obj_idx = target;
}

static void assert_self_targets(uint16_t target)
{
	XVT_ASSERT_INT_EQ(self_controller()->target_obj_idx, target);
	XVT_ASSERT_INT_EQ(self_controller()->target_signature,
			  g_test_objects[target].object_signature);
	XVT_ASSERT_INT_EQ(self_controller()->has_live_target, 1);
}

/* ------------------------------------------------------------------------ */
/* Order 14, paifight_followleadatkorder. */

/* A computer leader that is not attacking gives no target: the order returns
 * 0 at once, before it looks at the candidate target. */
static void check_follow_leader_not_attacking(void)
{
	fresh_world();
	g_test_craft[TEST_LEADER].ai_controller.target_obj_idx = 2;
	self_controller()->candidate_target_idx = 3;
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 0);
	XVT_ASSERT_INT_EQ(self_controller()->target_obj_idx, UINT16_MAX);
	XVT_ASSERT_INT_EQ(self_controller()->candidate_target_idx, 3);

	/* The rocket attack counts as attacking too. */
	fresh_world();
	g_test_craft[TEST_LEADER].ai_controller.maneuver_mode =
		AI_MANEUVER_MODE_ROCKET_ATTACK;
	g_test_craft[TEST_LEADER].ai_controller.target_obj_idx = 2;
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 1);
}

/* require_undisabled_target is set on disableldr1pln and cleared on any
 * other plan, whatever the order then returns. */
static void check_follow_leader_disable_plan_flag(void)
{
	fresh_world();
	self_controller()->current_plan_id = TEST_PLAN_DISABLE;
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 0);
	XVT_ASSERT_INT_EQ(g_pai_context.require_undisabled_target, 1);

	fresh_world();
	g_pai_context.require_undisabled_target = 1;
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 0);
	XVT_ASSERT_INT_EQ(g_pai_context.require_undisabled_target, 0);
}

/* Under an attacking computer leader, the search starts craft_ordinal slots
 * after the leader's target and takes the first craft of the target's flight
 * group it finds there. */
static void check_follow_leader_target_by_ordinal(void)
{
	fresh_world();
	leader_attacks(2);
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 1);
	assert_self_targets(2);

	fresh_world();
	leader_attacks(2);
	g_test_craft[TEST_SELF].craft_ordinal = 2;
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 1);
	assert_self_targets(4);

	/* Past the last craft slot the search goes on from the first, and the
	 * craft of other flight groups on the way are passed over. */
	fresh_world();
	leader_attacks(5);
	g_test_craft[TEST_SELF].craft_ordinal = 1;
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 1);
	assert_self_targets(2);
}

/* The leader's target alone does not make the craft a target: the craft must
 * be targetable near this craft. A craft breaking up is passed over, and an
 * object of the group far away too. */
static void check_follow_leader_skips_untargetable(void)
{
	fresh_world();
	leader_attacks(2);
	g_test_craft[2].object_kind = CRAFT_OBJECT_KIND_BREAKING_UP;
	g_test_objects[3].world_x = 2000000;
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 1);
	assert_self_targets(4);

	/* Passing over the last craft slot, the search goes on from the
	 * first. */
	fresh_world();
	leader_attacks(2);
	g_test_craft[TEST_SELF].craft_ordinal = 3;
	g_test_craft[5].object_kind = CRAFT_OBJECT_KIND_EXPLODING;
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 1);
	assert_self_targets(2);
}

/* On disableldr1pln a craft with no working subsystem is passed over, and the
 * candidate must match the current order's targets; a group the order does
 * not name gives none. */
static void check_follow_leader_disable_plan_search(void)
{
	fresh_world();
	leader_attacks(2);
	self_controller()->current_plan_id = TEST_PLAN_DISABLE;
	g_test_craft[2].working_subsystems = 0;
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 1);
	assert_self_targets(3);

	fresh_world();
	leader_attacks(2);
	self_controller()->current_plan_id = TEST_PLAN_CAPFREE;
	g_mission_flight_groups[0].fg.orders[0].target1 = TEST_STATIC_FG;
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 0);
	XVT_ASSERT_INT_EQ(self_controller()->target_obj_idx, UINT16_MAX);
}

/* A candidate target that can be targeted comes first, and under a computer
 * leader at any range. One that cannot is cleared, and the search goes on
 * from the leader's target. */
static void check_follow_leader_candidate_first(void)
{
	fresh_world();
	leader_attacks(2);
	self_controller()->candidate_target_idx = 4;
	g_test_objects[4].world_x = 2000000;
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 1);
	assert_self_targets(4);

	fresh_world();
	leader_attacks(2);
	self_controller()->candidate_target_idx = 4;
	g_test_objects[4].object_type = 0;
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 1);
	assert_self_targets(2);
	XVT_ASSERT_INT_EQ(self_controller()->candidate_target_idx, UINT16_MAX);

	/* AI_TARGET_ABORT stands for no candidate, and is left as it is. */
	fresh_world();
	leader_attacks(2);
	self_controller()->candidate_target_idx = AI_TARGET_ABORT;
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 1);
	assert_self_targets(2);
	XVT_ASSERT_INT_EQ(self_controller()->candidate_target_idx,
			  AI_TARGET_ABORT);
}

/* Under a player leader the candidate target must lie within a rough
 * 0x50000 of this craft; one farther away makes the order return 0. */
static void check_follow_player_leader_candidate_range(void)
{
	fresh_world();
	g_test_objects[TEST_LEADER].player_owner_idx = 0;
	self_controller()->candidate_target_idx = 4;
	g_test_objects[4].world_x = 0x50000;
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 1);
	assert_self_targets(4);

	fresh_world();
	g_test_objects[TEST_LEADER].player_owner_idx = 0;
	self_controller()->candidate_target_idx = 4;
	g_test_objects[4].world_x = 0x50001;
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 0);
	XVT_ASSERT_INT_EQ(self_controller()->target_obj_idx, UINT16_MAX);
	XVT_ASSERT_INT_EQ(self_controller()->candidate_target_idx, 4);
}

/* When the leader's target is a static object, the search takes the first
 * static object after it, of the same flight group, that is targetable near
 * this craft and matches the current order. */
static void check_follow_leader_static_target(void)
{
	fresh_world();
	g_mission_flight_groups[0].fg.orders[0].target1 = TEST_STATIC_FG;
	leader_attacks(7);
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 1);
	assert_self_targets(8);

	/* Past the last static slot the search goes on from the first. */
	fresh_world();
	g_mission_flight_groups[0].fg.orders[0].target1 = TEST_STATIC_FG;
	leader_attacks(9);
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 1);
	assert_self_targets(6);

	/* An order naming another group gives none. */
	fresh_world();
	leader_attacks(7);
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 0);
}

/* Known failure follow_leader_attacker_none, issue #50: craft.h gives
 * last_attacker_obj_idx as UINT16_MAX for none. Under a player leader, with
 * no candidate target and no craft hit, the order looks for a craft whose
 * last attacker the player flies, and reads the object table at 0xFFFF for
 * every craft it tests, one slot past this file's table. With no craft hit
 * there is none to find: the order should return 0 without that read. */
static void check_follow_player_leader_unhit_craft(void)
{
	fresh_world();
	g_test_objects[TEST_LEADER].player_owner_idx = 0;
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 0);
	XVT_ASSERT_INT_EQ(self_controller()->target_obj_idx, UINT16_MAX);
}

/* Known failure follow_leader_avoid_target, issue #51: craft.h gives
 * player_command_avoid_target_obj_idx as the object a player's order told
 * this craft to leave alone, which the targeting checks. The leader attacks
 * craft 2 and the player told this craft to leave craft 2 alone; the search
 * compares that object with its loop count, which starts at 0, and takes
 * craft 2. It should take craft 3, the next of the group. */
static void check_follow_leader_avoided_craft(void)
{
	fresh_world();
	leader_attacks(2);
	g_test_craft[TEST_SELF].player_command_avoid_target_obj_idx = 2;
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 1);
	assert_self_targets(3);
}

/* Known failure follow_leader_static_skip, issue #52: the leader attacks
 * static object 6, the player told this craft to leave object 7 alone, and
 * object 8 of the same group qualifies. The comment on the function gives the
 * first static object after the leader's that qualifies; the search stays on
 * object 7, which it must skip, and returns 0. It should take object 8. */
static void check_follow_leader_static_after_skip(void)
{
	fresh_world();
	g_mission_flight_groups[0].fg.orders[0].target1 = TEST_STATIC_FG;
	leader_attacks(6);
	g_test_craft[TEST_SELF].player_command_avoid_target_obj_idx = 7;
	XVT_ASSERT_INT_EQ(paifight_followleadatkorder(), 1);
	assert_self_targets(8);
}

/* ------------------------------------------------------------------------ */
/* Order 8, paifight_missiledefenseorder. */

/* Craft 0 is a starship whose model has one launcher, slot 0, holding one
 * defense warhead (type 0x90) at a hardpoint at its center, ready to fire.
 * No warhead flies: the projectile slots are empty. The launcher's turret
 * target starts at 9, which no search gives. */
static void defending_starship(void)
{
	fresh_world();
	memset(&g_model_defs[0], 0, sizeof g_model_defs[0]);
	g_model_defs[0].warhead_launcher_first_slot[0] = TEST_LAUNCHER_SLOT;
	g_model_defs[0].warhead_launcher_last_slot[0] = TEST_LAUNCHER_SLOT;
	g_test_objects[TEST_SELF].genus_id = CRAFT_GENUS_STARSHIP;
	g_test_craft[TEST_SELF].model_index = 0;
	g_test_craft[TEST_SELF].warhead_launcher_count = 1;
	g_test_craft[TEST_SELF]
		.weapon_slots[TEST_LAUNCHER_SLOT]
		.projectile_type_id = 0x90;
	g_test_craft[TEST_SELF].weapon_slots[TEST_LAUNCHER_SLOT].ammo_count = 1;
	g_test_craft[TEST_SELF]
		.turret_target_states[TEST_LAUNCHER_SLOT]
		.target_obj_idx = 9;
}

static uint16_t launcher_target(void)
{
	return g_test_craft[TEST_SELF]
		.turret_target_states[TEST_LAUNCHER_SLOT]
		.target_obj_idx;
}

/* The given craft attacks craft 0: it targets it in an attack maneuver. */
static void attacks_self(int object)
{
	g_test_craft[object].ai_controller.target_obj_idx = TEST_SELF;
	g_test_craft[object].ai_controller.maneuver_mode =
		AI_MANEUVER_MODE_ATTACK;
}

/* With no warhead aimed at the craft, the launcher takes the nearest
 * targetable craft that attacks it, and the order returns 0. The craft's own
 * target is put back as it was. */
static void check_defense_targets_nearest_attacker(void)
{
	defending_starship();
	attacks_self(4);
	attacks_self(3);
	self_controller()->target_obj_idx = 5;
	XVT_ASSERT_INT_EQ(paifight_missiledefenseorder(), 0);
	XVT_ASSERT_INT_EQ(launcher_target(), 3);
	XVT_ASSERT_INT_EQ(self_controller()->target_obj_idx, 5);

	/* A rocket attack counts as an attack too; a craft only targeting
	 * craft 0, or attacking another, does not. */
	defending_starship();
	attacks_self(4);
	g_test_craft[4].ai_controller.maneuver_mode =
		AI_MANEUVER_MODE_ROCKET_ATTACK;
	g_test_craft[3].ai_controller.target_obj_idx = TEST_SELF;
	attacks_self(2);
	g_test_craft[2].ai_controller.target_obj_idx = TEST_LEADER;
	XVT_ASSERT_INT_EQ(paifight_missiledefenseorder(), 0);
	XVT_ASSERT_INT_EQ(launcher_target(), 4);
}

/* The candidate must lie within 0x40000 of the hardpoint and be targetable. */
static void check_defense_target_range(void)
{
	defending_starship();
	attacks_self(3);
	g_test_objects[3].world_x = 0x3FFFF;
	XVT_ASSERT_INT_EQ(paifight_missiledefenseorder(), 0);
	XVT_ASSERT_INT_EQ(launcher_target(), 3);

	defending_starship();
	attacks_self(3);
	g_test_objects[3].world_x = 0x40000;
	XVT_ASSERT_INT_EQ(paifight_missiledefenseorder(), 0);
	XVT_ASSERT_INT_EQ(launcher_target(), UINT16_MAX);

	defending_starship();
	attacks_self(3);
	g_test_craft[3].object_kind = CRAFT_OBJECT_KIND_EXPLODING;
	XVT_ASSERT_INT_EQ(paifight_missiledefenseorder(), 0);
	XVT_ASSERT_INT_EQ(launcher_target(), UINT16_MAX);
}

/* An enemy player's craft that targets craft 0 is taken though it does not
 * attack it; a player's craft of an allied team is not. */
static void check_defense_targets_enemy_player(void)
{
	defending_starship();
	g_test_objects[4].player_owner_idx = 2;
	g_players[2].current_target_object_idx = TEST_SELF;
	XVT_ASSERT_INT_EQ(paifight_missiledefenseorder(), 0);
	XVT_ASSERT_INT_EQ(launcher_target(), 4);

	defending_starship();
	g_test_objects[4].player_owner_idx = 2;
	g_players[2].current_target_object_idx = TEST_SELF;
	g_mission_teams[0].allies[1] = 1;
	XVT_ASSERT_INT_EQ(paifight_missiledefenseorder(), 0);
	XVT_ASSERT_INT_EQ(launcher_target(), UINT16_MAX);

	defending_starship();
	g_test_objects[4].player_owner_idx = 2;
	g_players[2].current_target_object_idx = 3;
	XVT_ASSERT_INT_EQ(paifight_missiledefenseorder(), 0);
	XVT_ASSERT_INT_EQ(launcher_target(), UINT16_MAX);
}

/* A launcher still cooling down waits one think less and does not search. */
static void check_defense_cooldown(void)
{
	defending_starship();
	attacks_self(3);
	g_test_craft[TEST_SELF]
		.weapon_slots[TEST_LAUNCHER_SLOT]
		.missile_defense_cooldown = 2;
	XVT_ASSERT_INT_EQ(paifight_missiledefenseorder(), 0);
	XVT_ASSERT_INT_EQ(g_test_craft[TEST_SELF]
				  .weapon_slots[TEST_LAUNCHER_SLOT]
				  .missile_defense_cooldown,
			  1);
	XVT_ASSERT_INT_EQ(launcher_target(), 9);

	/* A slot with no round, or with another warhead, does not search. */
	defending_starship();
	attacks_self(3);
	g_test_craft[TEST_SELF].weapon_slots[TEST_LAUNCHER_SLOT].ammo_count = 0;
	XVT_ASSERT_INT_EQ(paifight_missiledefenseorder(), 0);
	XVT_ASSERT_INT_EQ(launcher_target(), 9);

	defending_starship();
	attacks_self(3);
	g_test_craft[TEST_SELF]
		.weapon_slots[TEST_LAUNCHER_SLOT]
		.projectile_type_id = 0x91;
	XVT_ASSERT_INT_EQ(paifight_missiledefenseorder(), 0);
	XVT_ASSERT_INT_EQ(launcher_target(), 9);

	/* Warhead type 0x95 defends too. */
	defending_starship();
	attacks_self(3);
	g_test_craft[TEST_SELF]
		.weapon_slots[TEST_LAUNCHER_SLOT]
		.projectile_type_id = 0x95;
	XVT_ASSERT_INT_EQ(paifight_missiledefenseorder(), 0);
	XVT_ASSERT_INT_EQ(launcher_target(), 3);
}

/* Not for a starfighter, a craft breaking up, with no subsystem working or
 * with weapons inhibited: the launcher is left alone. */
static void check_defense_refusals(void)
{
	defending_starship();
	attacks_self(3);
	g_test_objects[TEST_SELF].genus_id = CRAFT_GENUS_STARFIGHTER;
	XVT_ASSERT_INT_EQ(paifight_missiledefenseorder(), 0);
	XVT_ASSERT_INT_EQ(launcher_target(), 9);

	defending_starship();
	attacks_self(3);
	g_test_craft[TEST_SELF].object_kind = CRAFT_OBJECT_KIND_BREAKING_UP;
	XVT_ASSERT_INT_EQ(paifight_missiledefenseorder(), 0);
	XVT_ASSERT_INT_EQ(launcher_target(), 9);

	defending_starship();
	attacks_self(3);
	g_test_craft[TEST_SELF].working_subsystems = 0;
	XVT_ASSERT_INT_EQ(paifight_missiledefenseorder(), 0);
	XVT_ASSERT_INT_EQ(launcher_target(), 9);

	defending_starship();
	attacks_self(3);
	g_test_craft[TEST_SELF].weapon_fire_inhibit_timer = 5;
	XVT_ASSERT_INT_EQ(paifight_missiledefenseorder(), 0);
	XVT_ASSERT_INT_EQ(launcher_target(), 9);
}

/* Known failure defense_last_attacker, issue #49: the comment on the order
 * takes a craft that attacks the craft, and craft.h gives the craft's
 * last_attacker_obj_idx as the attacker it answers. Craft 3 hit craft 0 and
 * is its last attacker, but flies no attack maneuver; the order compares
 * craft 3's own last attacker with craft 3 and takes no target. It should
 * take craft 3. */
static void check_defense_answers_last_attacker(void)
{
	defending_starship();
	g_test_craft[TEST_SELF].last_attacker_obj_idx = 3;
	XVT_ASSERT_INT_EQ(paifight_missiledefenseorder(), 0);
	XVT_ASSERT_INT_EQ(launcher_target(), 3);
}

int main(int argc, char **argv)
{
	/* "known-failure <check>" runs one check the code is known to fail; an
	 * unknown name runs nothing. */
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		static const struct {
			const char *name;
			void (*check)(void);
		} known_failures[] = {
			{"follow_leader_attacker_none",
			 check_follow_player_leader_unhit_craft},
			{"follow_leader_avoid_target",
			 check_follow_leader_avoided_craft},
			{"follow_leader_static_skip",
			 check_follow_leader_static_after_skip},
			{"defense_last_attacker",
			 check_defense_answers_last_attacker},
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
	check_follow_leader_not_attacking();
	check_follow_leader_disable_plan_flag();
	check_follow_leader_target_by_ordinal();
	check_follow_leader_skips_untargetable();
	check_follow_leader_disable_plan_search();
	check_follow_leader_candidate_first();
	check_follow_player_leader_candidate_range();
	check_follow_leader_static_target();
	check_defense_targets_nearest_attacker();
	check_defense_target_range();
	check_defense_targets_enemy_player();
	check_defense_cooldown();
	check_defense_refusals();
	return 0;
}
