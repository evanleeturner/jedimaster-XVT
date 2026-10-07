/* Tests for xvt/flight/player/player.c: how a player's craft is handed back to
 * the computer pilot, the searches for the nearest enemy and the nearest
 * objective, and the smaller steps on the player's craft. Each check builds
 * the flight it needs in the game's own tables: an object table this file
 * owns with eight craft slots, four shot slots and four static slots, four
 * flight groups on teams 0, 1 and 2, and plans whose bytes change nothing.
 * Player 2 flies the craft in slot 1 for team 0; team 2 is team 0's ally.
 * No game data is read. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/object_genus.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/ai/paiman.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/goals.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/math/math2.h"
#include "xvt/util/game_rand.h"

enum {
	TEST_SLOTS = 32,
	CRAFT_SLOTS = 8,
	SHOT_START = 8,
	SHOT_END = 12,
	MAIN_END = 16, /* Slots 16 to 19 hold static objects. */
	STATIC_COUNT = 4,
	PLAYER = 2,
	PLAYER_SLOT = 1,
	NO_PLAYER = 7, /* The local player, flying nothing. */
	TEST_XWING = 1,
	NULL_PLAN = 2,
	ORDER_PLAN = 3,
	OTHER_PLAN = 7,
	/* Flight groups: 0 the player's, on team 0; 1 and 3 on team 1, the
	 * enemy; 2 on team 2, team 0's ally. */
	OWN_FG = 0,
	ENEMY_FG = 1,
	ALLY_FG = 2,
	OTHER_ENEMY_FG = 3,
	GOAL_PENDING = 4,
	/* The player's craft sits off the axes. */
	PLAYER_X = 10000,
	PLAYER_Y = 20000,
	PLAYER_Z = 30000,
};

static struct object_record g_test_objects[TEST_SLOTS];
static struct mobile_object g_test_mobiles[MAIN_END];
static struct craft_data g_test_craft[CRAFT_SLOTS];
/* Every plan's bytes: keep the target, keep the maneuver. */
static uint8_t g_test_plan[4] = {255, 255, 0, 0};
/* Every in-flight message's text, a system message: with system messages
 * off, the message code drops it. */
static const char g_test_message[] = "\x03message";
static char g_test_model_name[] = "X-wing";

/* Puts a starfighter of flight group fg in slot obj, offset from the
 * player's craft by x, y and z, with the group's team and IFF. */
static struct craft_data *place_craft(int obj, int fg, int x, int y, int z)
{
	g_test_objects[obj].object_type = TEST_XWING;
	g_test_objects[obj].genus_id = CRAFT_GENUS_STARFIGHTER;
	g_test_objects[obj].object_signature = (uint16_t)(100 + obj);
	g_test_objects[obj].flight_group_idx = (uint8_t)fg;
	g_test_objects[obj].world_x = PLAYER_X + x;
	g_test_objects[obj].world_y = PLAYER_Y + y;
	g_test_objects[obj].world_z = PLAYER_Z + z;
	g_test_mobiles[obj].team = g_mission_flight_groups[fg].fg.team;
	g_test_mobiles[obj].iff = g_mission_flight_groups[fg].fg.iff;
	g_test_craft[obj].craft_ordinal = (uint8_t)obj;
	return &g_test_craft[obj];
}

/* The world described at the top of this file. Every slot has a mobile
 * object up to the static slots and a craft record in the craft slots; the
 * player's flight group is owned by player 2, and the game's random generator
 * starts from a fixed state. */
static struct craft_data *fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_mission_flight_groups, 0,
	       4 * sizeof g_mission_flight_groups[0]);
	memset(g_mission_fg_stats, 0, 4 * sizeof g_mission_fg_stats[0]);
	memset(g_mission_teams, 0, sizeof g_mission_teams);
	memset(g_mission_global_goals, 0, sizeof g_mission_global_goals);
	memset(g_players, 0, sizeof g_players);
	memset(g_plan_table, 0, sizeof g_plan_table);
	for (int plan = 0; plan < 256; ++plan) {
		g_plan_data_ptrs[plan] = g_test_plan;
	}
	strcpy(g_plan_table[NULL_PLAN].name, "nullpln");
	strcpy(g_plan_table[ORDER_PLAN].name, "attackpln");
	strcpy(g_plan_table[OTHER_PLAN].name, "otherpln");
	for (size_t id = 0; id < sizeof g_str_in_flight_messages /
					 sizeof g_str_in_flight_messages[0];
	     ++id) {
		g_str_in_flight_messages[id] = g_test_message;
	}
	g_system_message_display_enabled = 0;
	for (size_t i = 0; i < sizeof g_model_defs / sizeof g_model_defs[0];
	     ++i) {
		g_model_defs[i].name_long = g_test_model_name;
	}
	g_object_table = g_test_objects;
	for (int i = 0; i < TEST_SLOTS; ++i) {
		g_test_objects[i].player_owner_idx = -1;
		if (i < MAIN_END) {
			g_test_objects[i].mobj = &g_test_mobiles[i];
			g_test_mobiles[i].cached_side_x = 0x7FFF;
			g_test_mobiles[i].cached_fwd_y = 0x7FFF;
			g_test_mobiles[i].cached_up_z = 0x7FFF;
		}
	}
	for (int i = 0; i < CRAFT_SLOTS; ++i) {
		g_test_mobiles[i].p_craft = &g_test_craft[i];
		g_test_craft[i].leader_obj_idx = UINT8_MAX;
		g_test_craft[i].carried_object_index = UINT16_MAX;
		g_test_craft[i].last_attacker_obj_idx = UINT16_MAX;
		g_test_craft[i].ai_controller.target_obj_idx = UINT16_MAX;
		g_test_craft[i].working_subsystems = CRAFT_SUBSYSTEM_FLAGS_ALL;
	}
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
		g_players[i].current_target_object_idx = -1;
		g_players[i].engine_wash_source_obj_idx = -1;
		for (int slot = 0; slot < 4; ++slot) {
			g_players[i].target_preset_slot[slot] = -1;
		}
	}
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = CRAFT_SLOTS;
	g_projectile_object_slot_start = SHOT_START;
	g_projectile_object_slot_end = SHOT_END;
	g_region_main_object_slot_end = MAIN_END;
	g_region_static_object_slot_count = STATIC_COUNT;
	static const uint8_t teams[4] = {0, 1, 2, 1};
	for (int fg = 0; fg < 4; ++fg) {
		g_mission_flight_groups[fg].fg.craft_type = TEST_XWING;
		g_mission_flight_groups[fg].fg.team = teams[fg];
		g_mission_flight_groups[fg].fg.iff = teams[fg];
		g_mission_flight_groups[fg].fg.special_cargo_craft = 9;
		g_mission_flight_groups[fg].player_owner_idx = -1;
	}
	g_mission_flight_groups[OWN_FG].player_owner_idx = PLAYER;
	g_mission_teams[0].allies[2] = 1;
	g_local_player = NO_PLAYER;
	g_flight_sim_side_effects_suppressed = 0;
	g_game_rand_value_state = 0x1234;
	g_game_rand_feedback_state = 0x5678;

	struct craft_data *craft = place_craft(PLAYER_SLOT, OWN_FG, 0, 0, 0);
	g_test_objects[PLAYER_SLOT].player_owner_idx = PLAYER;
	g_players[PLAYER].object_index = PLAYER_SLOT;
	g_players[PLAYER].team = 0;
	g_cur_craft = craft;
	return craft;
}

/* ------------------------------------------------------------------------ */
/* The nearest enemy. */

/* The search returns the nearest hostile craft, active or arriving: one of a
 * flight group on another team that is not allied. A craft of the player's
 * team or of an allied team does not count however near, and the excluded
 * object is passed over. */
static void check_nearest_enemy(void)
{
	fresh_world();
	place_craft(2, ENEMY_FG, 5000, 0, 0);
	place_craft(3, OTHER_ENEMY_FG, 0, 3000, 0);
	place_craft(4, OWN_FG, 100, 0, 0);
	place_craft(5, ALLY_FG, 0, 100, 0);
	XVT_ASSERT_INT_EQ(player_find_nearest_enemy_fighter(PLAYER, -1), 3);
	XVT_ASSERT_INT_EQ(player_find_nearest_enemy_fighter(PLAYER, 3), 2);
	g_test_craft[3].object_kind =
		CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE;
	XVT_ASSERT_INT_EQ(player_find_nearest_enemy_fighter(PLAYER, -1), 3);
	g_test_craft[3].object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	g_mission_teams[0].allies[1] = 1;
	XVT_ASSERT_TRUE((unsigned)player_find_nearest_enemy_fighter(
				PLAYER, -1) >= TEST_SLOTS);
}

/* Starships, freighters, platforms and explosions are passed over, as are
 * craft with no working system, craft in any state but active or arriving,
 * empty slots and the player's own craft, even when it is hostile. */
static void check_nearest_enemy_skips(void)
{
	static const uint8_t skipped_genus[] = {
		CRAFT_GENUS_STARSHIP,
		CRAFT_GENUS_FREIGHTER,
		CRAFT_GENUS_PLATFORM,
		CRAFT_GENUS_EXPLOSION,
	};
	fresh_world();
	place_craft(2, ENEMY_FG, 5000, 0, 0);
	place_craft(3, ENEMY_FG, 0, 3000, 0);
	for (size_t i = 0; i < sizeof skipped_genus; ++i) {
		g_test_objects[3].genus_id = skipped_genus[i];
		XVT_ASSERT_INT_EQ(player_find_nearest_enemy_fighter(PLAYER, -1),
				  2);
	}
	g_test_objects[3].genus_id = CRAFT_GENUS_TRANSPORT;
	XVT_ASSERT_INT_EQ(player_find_nearest_enemy_fighter(PLAYER, -1), 3);

	g_test_craft[3].working_subsystems = 0;
	XVT_ASSERT_INT_EQ(player_find_nearest_enemy_fighter(PLAYER, -1), 2);
	g_test_craft[3].working_subsystems = CRAFT_SUBSYSTEM_FLAG_ENGINES;
	g_test_craft[3].object_kind = CRAFT_OBJECT_KIND_DISABLED;
	XVT_ASSERT_INT_EQ(player_find_nearest_enemy_fighter(PLAYER, -1), 2);
	g_test_craft[3].object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	g_test_objects[3].object_type = 0;
	XVT_ASSERT_INT_EQ(player_find_nearest_enemy_fighter(PLAYER, -1), 2);

	g_test_objects[PLAYER_SLOT].flight_group_idx = ENEMY_FG;
	g_test_mobiles[PLAYER_SLOT].team = 1;
	XVT_ASSERT_INT_EQ(player_find_nearest_enemy_fighter(PLAYER, -1), 2);
}

/* In the static slots a hostile mine counts while its type_specific_word is
 * nonzero; an allied mine and the excluded object do not. */
static void check_nearest_enemy_mine(void)
{
	fresh_world();
	place_craft(3, ENEMY_FG, 0, 3000, 0);
	struct object_record *mine = &g_test_objects[MAIN_END + 1];
	mine->object_type = 75;
	mine->genus_id = CRAFT_GENUS_MINE;
	mine->flight_group_idx = ENEMY_FG;
	mine->type_specific_word = 1;
	mine->world_x = PLAYER_X + 1000;
	mine->world_y = PLAYER_Y;
	mine->world_z = PLAYER_Z;
	XVT_ASSERT_INT_EQ(player_find_nearest_enemy_fighter(PLAYER, -1),
			  MAIN_END + 1);
	XVT_ASSERT_INT_EQ(
		player_find_nearest_enemy_fighter(PLAYER, MAIN_END + 1), 3);
	mine->type_specific_word = 0;
	XVT_ASSERT_INT_EQ(player_find_nearest_enemy_fighter(PLAYER, -1), 3);
	mine->type_specific_word = 1;
	mine->flight_group_idx = ALLY_FG;
	XVT_ASSERT_INT_EQ(player_find_nearest_enemy_fighter(PLAYER, -1), 3);
	mine->flight_group_idx = OWN_FG;
	XVT_ASSERT_INT_EQ(player_find_nearest_enemy_fighter(PLAYER, -1), 3);
	mine->flight_group_idx = ENEMY_FG;
	mine->genus_id = CRAFT_GENUS_SATELLITE;
	XVT_ASSERT_INT_EQ(player_find_nearest_enemy_fighter(PLAYER, -1), 3);
}

/* Known failure nearest_enemy_none_minus_one, issue #106: with no hostile
 * object the search answers -1, which the Space answers to requests 4 and 9
 * test for before they clear the player's target. It answers UINT16_MAX, as
 * its comment says, so those answers keep the old target; the check rests on
 * the issue. The throwaway fix starts the search at -1 in this file, which
 * serves all three callers (the R key passes the answer to player_set_target,
 * which ignores -1 as it ignores UINT16_MAX); the comment changes with it. */
static void check_nearest_enemy_none(void)
{
	fresh_world();
	place_craft(4, OWN_FG, 100, 0, 0);
	XVT_ASSERT_INT_EQ(player_find_nearest_enemy_fighter(PLAYER, -1), -1);
}

/* ------------------------------------------------------------------------ */
/* Handing the craft back. */

/* With require_multiple_craft 1 a player who owns only one craft that is not
 * breaking up, exploding or entering hyperspace keeps it, and the answer is
 * 0; with a second one in flight the craft is released, and the answer is 1.
 * A player with no craft has nothing to release. */
static void check_unbind_keeps_last_craft(void)
{
	static const uint8_t gone_kinds[] = {
		CRAFT_OBJECT_KIND_BREAKING_UP,
		CRAFT_OBJECT_KIND_EXPLODING,
		CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE,
	};
	for (size_t i = 0; i < sizeof gone_kinds; ++i) {
		fresh_world();
		place_craft(4, OWN_FG, 100, 0, 0)->object_kind = gone_kinds[i];
		XVT_ASSERT_INT_EQ(
			player_unbind_from_current_craft(PLAYER, 1, 0), 0);
		XVT_ASSERT_INT_EQ(g_players[PLAYER].object_index, PLAYER_SLOT);
		XVT_ASSERT_INT_EQ(g_test_objects[PLAYER_SLOT].player_owner_idx,
				  PLAYER);
	}
	g_test_craft[4].object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	XVT_ASSERT_INT_EQ(player_unbind_from_current_craft(PLAYER, 1, 0), 1);
	XVT_ASSERT_INT_EQ(g_players[PLAYER].object_index, -1);
	XVT_ASSERT_INT_EQ(player_unbind_from_current_craft(PLAYER, 0, 0), 0);
}

/* Released without orders, the craft keeps nothing of the player's: no owner,
 * cleared laser banks, launcher cooldowns and lock, launchers on link mode 1
 * keeping bit 7, every shield's energy in the front bank, plan nullpln at
 * order slot 0 and no steering. The player keeps a copy of the craft's
 * settings and loses the craft, hyperspace, lock, input smoothing and engine
 * wash state. g_cur_craft, already at this craft, stays there. */
static void check_unbind_without_orders(void)
{
	struct craft_data *craft = fresh_world();
	struct player_data *player = &g_players[PLAYER];
	craft->throttle_speed = 0x4000;
	craft->laser_recharge_level = 1;
	craft->shield_recharge_level = 2;
	craft->beam_recharge_level = 3;
	craft->shield_energy[0] = 100;
	craft->shield_energy[1] = 50;
	craft->shield_distrib_mode = SHIELD_DISTRIBUTION_EVEN;
	for (int bank = 0; bank < 2; ++bank) {
		craft->laser_state.link_mode[bank] = (uint8_t)(2 + bank);
		craft->laser_state.burst_remaining[bank] = 1;
		craft->laser_state.next_slot[bank] = 1;
		craft->laser_state.fire_cooldown_ticks[bank] = 5;
		craft->laser_state.next_fire_timestamp[bank] = 6;
		craft->warhead_launcher_cooldown_ticks[bank] = 7;
	}
	craft->warhead_launcher_flags[0] = (int8_t)0x83;
	craft->warhead_launcher_flags[1] = 0x02;
	craft->warhead_lock_ticks = 9;
	craft->ai_controller.running_plan_id = OTHER_PLAN;
	craft->ai_controller.current_plan_id = OTHER_PLAN;
	craft->ai_controller.current_order_slot = 2;
	craft->ai_flight.roll_state = 1;
	craft->ai_flight.pitch_state = 1;
	craft->ai_flight.turn_state = 1;
	craft->ai_flight.climb_state = 1;
	craft->ai_flight.dive_state = 1;
	player->awaiting_new_craft = 1;
	player->hyperspace_phase = 2;
	player->missile_lock_state = 1;
	player->yaw_roll_swap = 1;
	player->smoothed_input_yaw = 5;
	player->smoothed_input_pitch = 6;
	player->saved_key_mods = 1;
	player->key_mods_hold_timer = 3;
	player->engine_wash_source_obj_idx = 4;

	XVT_ASSERT_INT_EQ(player_unbind_from_current_craft(PLAYER, 0, 0), 1);
	XVT_ASSERT_INT_EQ(g_test_objects[PLAYER_SLOT].player_owner_idx, -1);
	XVT_ASSERT_INT_EQ(g_test_mobiles[PLAYER_SLOT].orient_matrix_dirty, 1);
	XVT_ASSERT_INT_EQ(g_test_mobiles[PLAYER_SLOT].move_vector_dirty, 1);
	for (int bank = 0; bank < 2; ++bank) {
		XVT_ASSERT_INT_EQ(craft->laser_state.link_mode[bank], 0);
		XVT_ASSERT_INT_EQ(craft->laser_state.burst_remaining[bank], 0);
		XVT_ASSERT_INT_EQ(craft->laser_state.next_slot[bank], 0);
		XVT_ASSERT_INT_EQ(craft->laser_state.fire_cooldown_ticks[bank],
				  0);
		XVT_ASSERT_INT_EQ(craft->laser_state.next_fire_timestamp[bank],
				  0);
		XVT_ASSERT_INT_EQ(craft->warhead_launcher_cooldown_ticks[bank],
				  0);
	}
	XVT_ASSERT_INT_EQ((uint8_t)craft->warhead_launcher_flags[0], 0x81);
	XVT_ASSERT_INT_EQ(craft->warhead_launcher_flags[1], 0x01);
	XVT_ASSERT_INT_EQ(craft->warhead_lock_ticks, 0);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 150);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 0);
	XVT_ASSERT_INT_EQ(craft->shield_distrib_mode,
			  SHIELD_DISTRIBUTION_FULLY_FORWARD);
	XVT_ASSERT_INT_EQ(craft->ai_controller.running_plan_id, NULL_PLAN);
	XVT_ASSERT_INT_EQ(craft->ai_controller.current_plan_id, NULL_PLAN);
	XVT_ASSERT_INT_EQ(craft->ai_controller.current_order_slot, 0);
	XVT_ASSERT_INT_EQ(craft->ai_flight.roll_state, 0);
	XVT_ASSERT_INT_EQ(craft->ai_flight.pitch_state, 0);
	XVT_ASSERT_INT_EQ(craft->ai_flight.turn_state, 0);
	XVT_ASSERT_INT_EQ(craft->ai_flight.climb_state, 0);
	XVT_ASSERT_INT_EQ(craft->ai_flight.dive_state, 0);
	XVT_ASSERT_TRUE(g_cur_craft == craft);

	XVT_ASSERT_INT_EQ(player->saved_craft_settings.throttle_speed, 0x4000);
	XVT_ASSERT_INT_EQ(player->saved_craft_settings.shield_distrib_mode,
			  SHIELD_DISTRIBUTION_EVEN);
	XVT_ASSERT_INT_EQ(player->saved_craft_settings.laser_link_mode[1], 3);
	XVT_ASSERT_INT_EQ(player->object_index, -1);
	XVT_ASSERT_INT_EQ(player->awaiting_new_craft, 0);
	XVT_ASSERT_INT_EQ(player->hyperspace_phase, 0);
	XVT_ASSERT_INT_EQ(player->missile_lock_state, 0);
	XVT_ASSERT_INT_EQ(player->yaw_roll_swap, 0);
	XVT_ASSERT_INT_EQ(player->smoothed_input_yaw, 0);
	XVT_ASSERT_INT_EQ(player->smoothed_input_pitch, 0);
	XVT_ASSERT_INT_EQ(player->saved_key_mods, 0);
	XVT_ASSERT_INT_EQ(player->key_mods_hold_timer, 0);
	XVT_ASSERT_INT_EQ(player->engine_wash_source_obj_idx, -1);
}

/* Gives the player's flight group a first order whose leader plan is the
 * plan in slot 3, named plan_name, at throttle setting 5, then hands the
 * craft back with orders. */
static struct craft_data *release_to_order_plan(const char *plan_name)
{
	struct craft_data *craft = fresh_world();
	struct mission_order *order =
		&g_mission_flight_groups[OWN_FG].fg.orders[0];
	order->order = 2;
	order->throttle = 5;
	g_builtin_plan_id_by_name_index
		[g_order_leader_builtin_plan_name_index[2]] = ORDER_PLAN;
	strcpy(g_plan_table[ORDER_PLAN].name, plan_name);
	g_model_defs[craft->model_index].max_speed = 100;
	craft->ai_controller.running_plan_id = NULL_PLAN;
	craft->ai_controller.current_order_slot = 1;
	g_test_mobiles[PLAYER_SLOT].speed_remainder = 7;
	XVT_ASSERT_INT_EQ(player_unbind_from_current_craft(PLAYER, 0, 1), 1);
	return craft;
}

/* Released with orders, the craft takes its flight group's first order: its
 * plan as current and running plan at order slot 0, and the throttle the
 * order's setting names, which with the model's top speed sets the craft's
 * speed. A plan that stands still gets throttle 0, escortldr1pln 0x8000. */
static void check_unbind_takes_first_order(void)
{
	static const char *const still_plans[] = {
		"nullpln",
		"stationaryldrpln",
		"stationaryflwpln",
		"disabledpln",
	};
	struct craft_data *craft = release_to_order_plan("attackpln");
	uint16_t throttle = g_order_throttle_to_craft_throttle_speed[5];
	XVT_ASSERT_INT_EQ(craft->ai_controller.running_plan_id, ORDER_PLAN);
	XVT_ASSERT_INT_EQ(craft->ai_controller.current_plan_id, ORDER_PLAN);
	XVT_ASSERT_INT_EQ(craft->ai_controller.current_order_slot, 0);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, throttle);
	XVT_ASSERT_INT_EQ(g_test_mobiles[PLAYER_SLOT].speed,
			  (uint16_t)math2_fraction(100, throttle));
	XVT_ASSERT_INT_EQ(g_test_mobiles[PLAYER_SLOT].speed_remainder, 0);
	XVT_ASSERT_TRUE(g_cur_craft == craft);

	for (size_t i = 0; i < sizeof still_plans / sizeof still_plans[0];
	     ++i) {
		craft = release_to_order_plan(still_plans[i]);
		XVT_ASSERT_INT_EQ(craft->throttle_speed, 0);
		XVT_ASSERT_INT_EQ(g_test_mobiles[PLAYER_SLOT].speed, 0);
	}
	craft = release_to_order_plan("escortldr1pln");
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 0x8000);
}

/* Known failure unbind_plan_to_released_craft, issue #170: a craft handed
 * back to the computer pilot takes its flight group's first order, the order
 * slot and plan included, whichever craft g_cur_craft names when the call is
 * made; no other craft's plan changes. The function's comment describes the
 * writes through g_cur_craft as intended, so the check rests on the issue.
 * The throwaway fix writes the order slot and the plans through the released
 * craft, as the rest of the function does; the comment changes with it. */
static void check_unbind_plan_to_released_craft(void)
{
	struct craft_data *craft = fresh_world();
	struct craft_data *other = place_craft(5, ENEMY_FG, 4000, 0, 0);
	other->ai_controller.running_plan_id = OTHER_PLAN;
	other->ai_controller.current_plan_id = OTHER_PLAN;
	other->ai_controller.current_order_slot = 2;
	g_cur_craft = other;
	g_mission_flight_groups[OWN_FG].fg.orders[0].order = 2;
	g_builtin_plan_id_by_name_index
		[g_order_leader_builtin_plan_name_index[2]] = ORDER_PLAN;
	craft->ai_controller.running_plan_id = NULL_PLAN;
	craft->ai_controller.current_order_slot = 1;
	XVT_ASSERT_INT_EQ(player_unbind_from_current_craft(PLAYER, 0, 1), 1);
	XVT_ASSERT_INT_EQ(other->ai_controller.running_plan_id, OTHER_PLAN);
	XVT_ASSERT_INT_EQ(other->ai_controller.current_plan_id, OTHER_PLAN);
	XVT_ASSERT_INT_EQ(other->ai_controller.current_order_slot, 2);
	XVT_ASSERT_INT_EQ(craft->ai_controller.running_plan_id, ORDER_PLAN);
	XVT_ASSERT_INT_EQ(craft->ai_controller.current_order_slot, 0);
}

/* ------------------------------------------------------------------------ */
/* The nearest objective. */

/* Gives flight group fg a pending primary goal for team 0, worth 0 points. */
static struct flight_group_goal *pending_goal(int fg)
{
	struct flight_group_goal *goal =
		&g_mission_flight_groups[fg].fg.goals[3];
	goal->enabled_teams[0] = 1;
	goal->goal_kind = 0;
	goal->points = 0;
	g_mission_fg_stats[fg].goal_state[3] = GOAL_PENDING;
	return goal;
}

/* A craft is an objective of a goal kind when its flight group has a goal of
 * that kind, enabled for the player's team, pending, with points of 0 or
 * more. Of two objectives the nearer is returned; with none, -1. */
static void check_objective_group_goal(void)
{
	fresh_world();
	place_craft(3, ENEMY_FG, 0, 3000, 0);
	place_craft(2, OTHER_ENEMY_FG, 5000, 0, 0);
	struct flight_group_goal *goal = pending_goal(OTHER_ENEMY_FG);
	XVT_ASSERT_INT_EQ(player_find_nearest_objective(0, PLAYER), 2);
	XVT_ASSERT_INT_EQ(player_find_nearest_objective(2, PLAYER), -1);

	g_mission_fg_stats[OTHER_ENEMY_FG].goal_state[3] = GOAL_PENDING - 1;
	XVT_ASSERT_INT_EQ(player_find_nearest_objective(0, PLAYER), -1);
	g_mission_fg_stats[OTHER_ENEMY_FG].goal_state[3] = GOAL_PENDING;
	goal->points = -1;
	XVT_ASSERT_INT_EQ(player_find_nearest_objective(0, PLAYER), -1);
	goal->points = 0;
	goal->enabled_teams[0] = 0;
	goal->enabled_teams[1] = 1;
	XVT_ASSERT_INT_EQ(player_find_nearest_objective(0, PLAYER), -1);
	goal->enabled_teams[0] = 1;
	goal->goal_kind = 2;
	XVT_ASSERT_INT_EQ(player_find_nearest_objective(2, PLAYER), 2);

	pending_goal(OTHER_ENEMY_FG);
	pending_goal(ENEMY_FG);
	XVT_ASSERT_INT_EQ(player_find_nearest_objective(0, PLAYER), 3);
	g_test_craft[3].object_kind = CRAFT_OBJECT_KIND_DISABLED;
	XVT_ASSERT_INT_EQ(player_find_nearest_objective(0, PLAYER), 2);
	g_test_craft[3].object_kind =
		CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE;
	XVT_ASSERT_INT_EQ(player_find_nearest_objective(0, PLAYER), 3);
}

/* A craft of the player's own team is never an objective, nor the player's
 * own craft, nor an explosion, nor an empty slot. */
static void check_objective_skips(void)
{
	fresh_world();
	place_craft(4, OWN_FG, 100, 0, 0);
	pending_goal(OWN_FG);
	XVT_ASSERT_INT_EQ(player_find_nearest_objective(0, PLAYER), -1);

	place_craft(3, ENEMY_FG, 0, 3000, 0);
	pending_goal(ENEMY_FG);
	g_test_objects[PLAYER_SLOT].flight_group_idx = ENEMY_FG;
	XVT_ASSERT_INT_EQ(player_find_nearest_objective(0, PLAYER), 3);
	g_test_objects[3].genus_id = CRAFT_GENUS_EXPLOSION;
	XVT_ASSERT_INT_EQ(player_find_nearest_objective(0, PLAYER), -1);
	g_test_objects[3].genus_id = CRAFT_GENUS_STARFIGHTER;
	g_test_objects[3].object_type = 0;
	XVT_ASSERT_INT_EQ(player_find_nearest_objective(0, PLAYER), -1);
}

/* A craft is also an objective when it matches a trigger of the team's
 * global goal of that kind whose condition is neither "always" nor "never":
 * the first pair's two triggers and the second pair's second. */
static void check_objective_global_triggers(void)
{
	static const struct {
		int pair;
		int trigger;
	} triggers[] = {{0, 0}, {0, 1}, {1, 1}};
	for (size_t i = 0; i < sizeof triggers / sizeof triggers[0]; ++i) {
		fresh_world();
		place_craft(3, ENEMY_FG, 0, 3000, 0);
		place_craft(2, OTHER_ENEMY_FG, 5000, 0, 0);
		struct mission_trigger *trigger =
			&g_mission_global_goals[0][2]
				 .trigger_pairs[triggers[i].pair]
				 .triggers[triggers[i].trigger];
		trigger->condition = MISSION_COND_DESTROYED;
		trigger->variable_type = 1; /* A flight group. */
		trigger->variable = OTHER_ENEMY_FG;
		XVT_ASSERT_INT_EQ(player_find_nearest_objective(2, PLAYER), 2);
		XVT_ASSERT_INT_EQ(player_find_nearest_objective(0, PLAYER), -1);
		trigger->condition = MISSION_COND_NEVER;
		XVT_ASSERT_INT_EQ(player_find_nearest_objective(2, PLAYER), -1);
		trigger->condition = MISSION_COND_ALWAYS_TRUE;
		XVT_ASSERT_INT_EQ(player_find_nearest_objective(2, PLAYER), -1);
	}
}

/* A static object with such a pending flight group goal is an objective, and
 * counts as one the target description marks actionable: it comes before a
 * nearer craft whose goal is not actionable, but not before a nearer craft
 * whose goal is an inspection, which always is. */
static void check_objective_static_actionable(void)
{
	fresh_world();
	place_craft(3, ENEMY_FG, 0, 3000, 0);
	struct flight_group_goal *goal = pending_goal(ENEMY_FG);
	goal->event_condition = MISSION_COND_DESTROYED;
	pending_goal(OTHER_ENEMY_FG);
	struct object_record *buoy = &g_test_objects[MAIN_END + 2];
	buoy->object_type = 80;
	buoy->genus_id = CRAFT_GENUS_SATELLITE;
	buoy->flight_group_idx = OTHER_ENEMY_FG;
	buoy->world_x = PLAYER_X + 8000;
	buoy->world_y = PLAYER_Y;
	buoy->world_z = PLAYER_Z;
	XVT_ASSERT_INT_EQ(player_find_nearest_objective(0, PLAYER),
			  MAIN_END + 2);
	goal->event_condition = MISSION_COND_INSPECTED;
	XVT_ASSERT_INT_EQ(player_find_nearest_objective(0, PLAYER), 3);
	g_mission_fg_stats[OTHER_ENEMY_FG].goal_state[3] = GOAL_PENDING - 1;
	goal->event_condition = MISSION_COND_DESTROYED;
	XVT_ASSERT_INT_EQ(player_find_nearest_objective(0, PLAYER), 3);
}

/* Known failure objective_second_pair_first_trigger, issue #176: a craft that
 * matches a trigger of the team's global goal counts as an objective. For the
 * second pair's first trigger the search takes the condition from that
 * trigger but the object from the first pair's first trigger, so a craft only
 * that trigger names is not found and the first pair's unused object is.
 * The comment above those lines describes the mix as the original's, so the
 * check rests on the function's comment and the issue. The throwaway fix
 * reads the object from the second pair's first trigger. */
static void check_objective_second_pair_first_trigger(void)
{
	fresh_world();
	place_craft(3, ENEMY_FG, 0, 3000, 0);
	place_craft(2, OTHER_ENEMY_FG, 5000, 0, 0);
	struct mission_trigger_pair *pairs =
		g_mission_global_goals[0][0].trigger_pairs;
	pairs[0].triggers[0].condition = MISSION_COND_NEVER;
	pairs[0].triggers[0].variable_type = 1;
	pairs[0].triggers[0].variable = ENEMY_FG;
	pairs[1].triggers[0].condition = MISSION_COND_DESTROYED;
	pairs[1].triggers[0].variable_type = 1;
	pairs[1].triggers[0].variable = OTHER_ENEMY_FG;
	XVT_ASSERT_INT_EQ(player_find_nearest_objective(0, PLAYER), 2);
}

/* ------------------------------------------------------------------------ */
/* Smaller steps on the player's craft. */

/* The saved settings take the craft's throttle, recharge levels, shield
 * distribution, laser link modes and the low two bits of each launcher's
 * flags. */
static void check_save_craft_settings(void)
{
	struct craft_data *craft = fresh_world();
	craft->throttle_speed = 0x1234;
	craft->laser_recharge_level = 1;
	craft->shield_recharge_level = 2;
	craft->beam_recharge_level = 3;
	craft->shield_distrib_mode = SHIELD_DISTRIBUTION_FULLY_AFT;
	craft->laser_state.link_mode[0] = 2;
	craft->laser_state.link_mode[1] = 3;
	craft->warhead_launcher_flags[0] = (int8_t)0x87;
	craft->warhead_launcher_flags[1] = 0x06;
	player_save_craft_settings(PLAYER);
	XVT_ASSERT_INT_EQ(g_players[PLAYER].saved_craft_settings.throttle_speed,
			  0x1234);
	XVT_ASSERT_INT_EQ(
		g_players[PLAYER].saved_craft_settings.laser_recharge_level, 1);
	XVT_ASSERT_INT_EQ(
		g_players[PLAYER].saved_craft_settings.shield_recharge_level,
		2);
	XVT_ASSERT_INT_EQ(g_players[PLAYER].saved_craft_settings.beam_level, 3);
	XVT_ASSERT_INT_EQ(
		g_players[PLAYER].saved_craft_settings.shield_distrib_mode,
		SHIELD_DISTRIBUTION_FULLY_AFT);
	XVT_ASSERT_INT_EQ(
		g_players[PLAYER].saved_craft_settings.laser_link_mode[0], 2);
	XVT_ASSERT_INT_EQ(
		g_players[PLAYER].saved_craft_settings.laser_link_mode[1], 3);
	XVT_ASSERT_INT_EQ(
		g_players[PLAYER]
			.saved_craft_settings.warhead_launcher_flags[0],
		3);
	XVT_ASSERT_INT_EQ(
		g_players[PLAYER]
			.saved_craft_settings.warhead_launcher_flags[1],
		2);
}

/* A rate per 236 ticks becomes this update's share: step times the elapsed
 * ticks over 236. */
static void check_scale_control_step(void)
{
	g_elapsed_ticks = 4;
	XVT_ASSERT_INT_EQ(player_scale_control_step_by_elapsed_ticks(590), 10);
	XVT_ASSERT_INT_EQ(player_scale_control_step_by_elapsed_ticks(236), 4);
	g_elapsed_ticks = 236;
	XVT_ASSERT_INT_EQ(player_scale_control_step_by_elapsed_ticks(-77), -77);
}

/* Shield energy moves from one bank to the other, as much as the receiving
 * bank lacks of twice the model's shield strength, or all there is when that
 * is less; nothing moves from an empty bank or into a full one. */
static void check_transfer_shield_energy(void)
{
	struct craft_data *craft = fresh_world();
	g_model_defs[craft->model_index].shield_strength = 100;
	craft->shield_energy[0] = 150;
	craft->shield_energy[1] = 80;
	player_transfer_shield_bank_energy(0, 1, PLAYER);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 200);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 30);
	player_transfer_shield_bank_energy(0, 1, PLAYER);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 200);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 30);

	craft->shield_energy[0] = 180;
	craft->shield_energy[1] = 10;
	player_transfer_shield_bank_energy(0, 1, PLAYER);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 190);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 0);
	player_transfer_shield_bank_energy(1, 0, PLAYER);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 0);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 190);
	player_transfer_shield_bank_energy(1, 0, PLAYER);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 0);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 190);

	/* One unit moves, and one unit of room is filled. */
	craft->shield_energy[0] = 150;
	craft->shield_energy[1] = 1;
	player_transfer_shield_bank_energy(0, 1, PLAYER);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 151);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 0);
	craft->shield_energy[0] = 199;
	craft->shield_energy[1] = 30;
	player_transfer_shield_bank_energy(0, 1, PLAYER);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 200);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 29);
}

/* A player has a craft available while one of their flight groups' craft is
 * not breaking up, exploding or entering hyperspace. */
static void check_has_available_owned_craft(void)
{
	static const uint8_t gone_kinds[] = {
		CRAFT_OBJECT_KIND_BREAKING_UP,
		CRAFT_OBJECT_KIND_EXPLODING,
		CRAFT_OBJECT_KIND_ENTERING_HYPERSPACE,
	};
	struct craft_data *craft = fresh_world();
	place_craft(3, ENEMY_FG, 0, 3000, 0);
	XVT_ASSERT_INT_EQ(player_has_available_owned_craft(PLAYER), 1);
	XVT_ASSERT_INT_EQ(player_has_available_owned_craft(PLAYER + 1), 0);
	for (size_t i = 0; i < sizeof gone_kinds; ++i) {
		craft->object_kind = gone_kinds[i];
		XVT_ASSERT_INT_EQ(player_has_available_owned_craft(PLAYER), 0);
	}
	place_craft(6, OWN_FG, 0, 0, 500)->object_kind =
		CRAFT_OBJECT_KIND_DISABLED;
	XVT_ASSERT_INT_EQ(player_has_available_owned_craft(PLAYER), 1);
}

/* While the roll modifier key is held (g_flight_key_mods & 0xE is 2) the yaw
 * step is ignored: the turn is the one a yaw step of 0 gives. Otherwise the
 * yaw step turns the craft. The craft's pitch follows the object's, and the
 * new roll is returned. */
static void check_pitch_yaw_steps(void)
{
	struct craft_data *craft = fresh_world();
	struct object_record *object = &g_test_objects[PLAYER_SLOT];
	int results[3][3];
	static const struct {
		uint16_t key_mods;
		int16_t yaw_step;
	} cases[3] = {{3, 0x200}, {0, 0}, {6, 0x200}};
	for (int i = 0; i < 3; ++i) {
		object->yaw = 0x1000;
		object->pitch = 0x0800;
		object->roll = 0x0400;
		g_flight_key_mods = cases[i].key_mods;
		int16_t roll = player_apply_pitch_yaw_steps(
			0x100, cases[i].yaw_step, PLAYER_SLOT, craft);
		XVT_ASSERT_INT_EQ(roll, (int16_t)object->roll);
		XVT_ASSERT_INT_EQ(craft->pitch, object->pitch);
		results[i][0] = object->yaw;
		results[i][1] = object->pitch;
		results[i][2] = object->roll;
	}
	g_flight_key_mods = 0;
	XVT_ASSERT_INT_EQ(results[0][0], results[1][0]);
	XVT_ASSERT_INT_EQ(results[0][1], results[1][1]);
	XVT_ASSERT_INT_EQ(results[0][2], results[1][2]);
	XVT_ASSERT_TRUE(results[2][0] != results[1][0]);
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
			{"nearest_enemy_none_minus_one",
			 check_nearest_enemy_none},
			{"unbind_plan_to_released_craft",
			 check_unbind_plan_to_released_craft},
			{"objective_second_pair_first_trigger",
			 check_objective_second_pair_first_trigger},
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
	check_nearest_enemy();
	check_nearest_enemy_skips();
	check_nearest_enemy_mine();
	check_unbind_keeps_last_craft();
	check_unbind_without_orders();
	check_unbind_takes_first_order();
	check_objective_group_goal();
	check_objective_skips();
	check_objective_global_triggers();
	check_objective_static_actionable();
	check_save_craft_settings();
	check_scale_control_step();
	check_transfer_shield_energy();
	check_has_available_owned_craft();
	check_pitch_yaw_steps();
	return 0;
}
