/* Tests for xvt/flight/flight.c: the key handler that acts on the key a player
 * pressed this step, and the step that counts the flight's timers down. Each
 * check builds the world it needs in the game's own tables: an object table
 * this file owns with 16 craft slots, a craft record for each, and the
 * players flying X-wings in some of them. A key is pressed by setting
 * g_current_action_key and calling flight_process_player_actions with the
 * player's index. Every in-flight message's text is a logged line, so the
 * local player's message log shows which messages were sent. No game data is
 * read.
 *
 * POSIX only, for the alarm that stops a check whose call does not return. */
#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "test_assert.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/mfd.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/util/game_rand.h"
#include "xvt/util/memory.h"

enum {
	SLOT_COUNT = 16,
	TEST_XWING = 1,	       /* Object type 1, an X-wing. */
	SHIELD_STRENGTH = 200, /* The X-wing's, set by the test. */
	BANK_LIMIT = 2 * SHIELD_STRENGTH,
	STEP = 4,   /* Shield energy one laser charge unit is worth. */
	FULL = 127, /* A laser slot's full charge. */
	PILOT = 0,  /* The local player. */
	MATE = 1,   /* A second player on the pilot's team. */
	PILOT_CRAFT = 2,
	MATE_CRAFT = 3,
};

static struct object_record g_test_objects[SLOT_COUNT];
static struct mobile_object g_test_mobiles[SLOT_COUNT];
static struct craft_data g_test_craft[SLOT_COUNT];
static struct model_def g_saved_xwing_model;
static uint16_t g_message_log;
/* Every in-flight message's text: a line the message log keeps. */
static char g_test_message[] = "\001message";

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

/* An empty world: no object in any slot, no player in the flight, flight
 * groups 0 to 3 cleared, every MFD page closed and none active, and a
 * mission of type 0 with no time limits. The local player is player 0, the
 * only one in the flight. The X-wing's model is put back as the game defines
 * it, with a shield strength of 200. The message log is empty and the ready
 * pane holds a message, so a new one waits behind it. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_players, 0, sizeof g_players);
	memset(g_mission_flight_groups, 0,
	       4 * sizeof g_mission_flight_groups[0]);
	memset(&g_mission_header, 0, sizeof g_mission_header);
	memset(g_mission_teams, 0, sizeof g_mission_teams);
	memset(&g_flight_mission_state, 0, sizeof g_flight_mission_state);
	*xwing_model() = g_saved_xwing_model;
	xwing_model()->shield_strength = SHIELD_STRENGTH;
	g_object_table = g_test_objects;
	for (int i = 0; i < SLOT_COUNT; ++i) {
		g_test_objects[i].player_owner_idx = -1;
		g_test_objects[i].mobj = &g_test_mobiles[i];
		g_test_mobiles[i].p_craft = &g_test_craft[i];
	}
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = SLOT_COUNT;
	g_region_main_object_slot_end = SLOT_COUNT;
	g_region_static_object_slot_count = 0;
	g_mobile_object_char_data_slot_start = 0;
	g_mobile_object_char_data_slot_end = 0;
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
		g_players[i].current_target_object_idx = -1;
		g_players[i].pending_action_param = -1;
		for (int slot = 0; slot < 4; ++slot) {
			g_players[i].target_preset_slot[slot] = -1;
		}
	}
	for (int page = 0; page < MFD_PAGE_COUNT; ++page) {
		g_mfd_page_states[page] = MFD_PAGE_STATE_CLOSED;
	}
	g_mfd_active_page = MFD_PAGE_NONE;
	g_mfd_secondary_page = MFD_PAGE_NONE;
	g_local_player = PILOT;
	g_active_flight_player_count = 1;
	g_pilot_data.num_human_players_last_mission = 0;
	g_flight_sim_side_effects_suppressed = 0;
	g_current_action_key = FLIGHT_KEY_NONE;
	g_game_rand_value_state = 0x1234;
	g_game_rand_feedback_state = 0x5678;
	for (size_t id = 0; id < sizeof g_str_in_flight_messages /
					 sizeof g_str_in_flight_messages[0];
	     ++id) {
		g_str_in_flight_messages[id] = g_test_message;
	}
	g_system_message_display_enabled = 0;
	g_message_log_handle = g_message_log;
	g_message_log_write_index = 0;
	g_message_log_total_count = 0;
	g_ready_message_queue_count = 0;
	memset(g_ready_message_pane_queue, 0,
	       sizeof g_ready_message_pane_queue);
	g_ready_message_pane_queue[0].state_or_message_id = 5;
	g_ready_message_pane_queue[0].pane_type = 1;
}

/* Puts player in flight on team and IFF team, flying an X-wing in slot obj
 * at a point off the axes. The craft has every system installed and working,
 * two laser slots at full charge, 100 energy in each shield bank, shields
 * shared evenly and every recharge setting at maintenance. */
static struct craft_data *fly(int player, int obj, int team)
{
	g_test_objects[obj].object_type = TEST_XWING;
	g_test_objects[obj].genus_id = 1;
	g_test_objects[obj].flight_group_idx = (uint8_t)team;
	g_test_objects[obj].player_owner_idx = (int8_t)player;
	g_test_objects[obj].world_x = 5000 + 300 * obj;
	g_test_objects[obj].world_y = 7000 - 200 * obj;
	g_test_objects[obj].world_z = 3000 + 100 * obj;
	g_test_mobiles[obj].team = (uint8_t)team;
	g_test_mobiles[obj].iff = (uint8_t)team;
	g_mission_flight_groups[team].fg.team = (uint8_t)team;
	g_mission_flight_groups[team].fg.iff = (uint8_t)team;
	struct craft_data *craft = &g_test_craft[obj];
	craft->model_index = (uint8_t)get_model_index_from_type(TEST_XWING);
	craft->system_flags = 0x03FF;
	craft->working_subsystems = 0x03FF;
	craft->laser_slot_count = 2;
	craft->weapon_slots[0].laser_charge = FULL;
	craft->weapon_slots[1].laser_charge = FULL;
	craft->shield_energy[0] = 100;
	craft->shield_energy[1] = 100;
	craft->shield_distrib_mode = SHIELD_DISTRIBUTION_EVEN;
	craft->laser_recharge_level = POWER_RECHARGE_MAINTENANCE;
	craft->shield_recharge_level = POWER_RECHARGE_MAINTENANCE;
	craft->beam_recharge_level = POWER_RECHARGE_MAINTENANCE;
	g_players[player].object_index = obj;
	g_players[player].team = (int16_t)team;
	g_players[player].iff = (int16_t)team;
	g_players[player].participation_state = 1;
	return craft;
}

/* Presses key for player. */
static void press(int player, uint16_t key)
{
	g_current_action_key = key;
	flight_process_player_actions(player);
}

/* The id of the last message the local player's message log took: the log
 * steps its index on, then writes the record there. */
static int last_message(void)
{
	const struct hud_in_flight_message_record *log =
		memory_get_handle_block(g_message_log);
	XVT_ASSERT_TRUE(g_message_log_total_count > 0);
	return log[g_message_log_write_index].state_or_message_id;
}

static int laser_charge(const struct craft_data *craft)
{
	return craft->weapon_slots[0].laser_charge +
	       craft->weapon_slots[1].laser_charge;
}

/* ------------------------------------------------------------------------ */
/* Hyperspace and the proving grounds. */

/* While the player lines up a jump, H aborts it and says so. */
static void check_hyperspace_abort(void)
{
	fresh_world();
	fly(PILOT, PILOT_CRAFT, 0);
	g_players[PILOT].hyperspace_phase = 1;
	press(PILOT, FLIGHT_KEY_H);
	XVT_ASSERT_INT_EQ(g_players[PILOT].hyperspace_phase, 0);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_107_HYPERSPACE_JUMP_ABORTED);
}

/* In the proving grounds F5 to F7 are dropped, so F6 does not bring back the
 * target kept in its preset; outside it does. */
static void check_proving_grounds_drops_keys(void)
{
	for (int course = 0; course < 2; ++course) {
		fresh_world();
		fly(PILOT, PILOT_CRAFT, 0);
		fly(MATE, MATE_CRAFT, 1);
		g_flight_mission_state.proving_grounds_mode_active =
			(uint8_t)course;
		g_players[PILOT].target_preset_slot[1] = MATE_CRAFT;
		press(PILOT, FLIGHT_KEY_F6);
		XVT_ASSERT_INT_EQ(g_players[PILOT].current_target_object_idx,
				  course ? -1 : MATE_CRAFT);
	}
}

/* ------------------------------------------------------------------------ */
/* The throttle keys. */

/* Backspace sets full throttle, backslash none, and the brackets a third and
 * two thirds, each saying so. */
static void check_throttle_fixed_keys(void)
{
	static const struct {
		uint16_t key;
		int throttle;
		int message;
	} keys[] = {
		{FLIGHT_KEY_BACKSPACE, 65535,
		 IFMSG_122_THROTTLE_SET_TO_FULL_POWER},
		{FLIGHT_KEY_BACKSLASH, 0, IFMSG_119_THROTTLE_SET_TO_NO_POWER},
		{FLIGHT_KEY_LEFT_BRACKET, 65535 / 3,
		 IFMSG_120_THROTTLE_SET_TO_1_3_POWER},
		{FLIGHT_KEY_RIGHT_BRACKET, 2 * 65535 / 3,
		 IFMSG_121_THROTTLE_SET_TO_2_3_POWER},
	};
	for (size_t k = 0; k < sizeof keys / sizeof keys[0]; ++k) {
		fresh_world();
		struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
		craft->throttle_speed = 12345;
		press(PILOT, keys[k].key);
		XVT_ASSERT_INT_EQ(craft->throttle_speed, keys[k].throttle);
		XVT_ASSERT_INT_EQ(last_message(), keys[k].message);
	}
}

/* A joystick throttle sends the key at entry i of g_throttle_key_table for a
 * throttle of i sixteenths; each of the throttle keys there sets the throttle
 * to i sixteenths of full. */
static void check_throttle_sixteenths(void)
{
	int tested = 0;
	for (int i = 0; i < 17; ++i) {
		uint16_t key = g_throttle_key_table[i];
		if (key < FLIGHT_KEY_THROTTLE_1 ||
		    key > FLIGHT_KEY_THROTTLE_14) {
			continue;
		}
		fresh_world();
		struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
		press(PILOT, key);
		XVT_ASSERT_INT_EQ(craft->throttle_speed, i * 4096);
		++tested;
	}
	XVT_ASSERT_INT_EQ(tested, 13);
}

/* Shift+9 and Shift+0 keep the throttle and the three recharge settings in
 * the player's first and second preset; 9 and 0 bring them back. */
static void check_throttle_presets(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->throttle_speed = 1111;
	craft->laser_recharge_level = POWER_RECHARGE_MAXIMUM;
	craft->shield_recharge_level = POWER_RECHARGE_INCREASED;
	craft->beam_recharge_level = POWER_RECHARGE_FULLY_REDIRECTED_TO_ENGINES;
	press(PILOT, FLIGHT_KEY_SHIFT_9);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_123_CONFIGURATION_SAVED_TO_PRESET);
	craft->throttle_speed = 2222;
	craft->laser_recharge_level = POWER_RECHARGE_INCREASED;
	craft->shield_recharge_level =
		POWER_RECHARGE_PARTIALLY_REDIRECTED_TO_ENGINES;
	craft->beam_recharge_level = POWER_RECHARGE_MAXIMUM;
	press(PILOT, FLIGHT_KEY_SHIFT_0);
	craft->throttle_speed = 0;
	craft->laser_recharge_level = POWER_RECHARGE_MAINTENANCE;
	craft->shield_recharge_level = POWER_RECHARGE_MAINTENANCE;
	craft->beam_recharge_level = POWER_RECHARGE_MAINTENANCE;

	press(PILOT, FLIGHT_KEY_9);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 1111);
	XVT_ASSERT_INT_EQ(craft->laser_recharge_level, POWER_RECHARGE_MAXIMUM);
	XVT_ASSERT_INT_EQ(craft->shield_recharge_level,
			  POWER_RECHARGE_INCREASED);
	XVT_ASSERT_INT_EQ(craft->beam_recharge_level,
			  POWER_RECHARGE_FULLY_REDIRECTED_TO_ENGINES);
	press(PILOT, FLIGHT_KEY_0);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 2222);
	XVT_ASSERT_INT_EQ(craft->laser_recharge_level,
			  POWER_RECHARGE_INCREASED);
	XVT_ASSERT_INT_EQ(craft->shield_recharge_level,
			  POWER_RECHARGE_PARTIALLY_REDIRECTED_TO_ENGINES);
	XVT_ASSERT_INT_EQ(craft->beam_recharge_level, POWER_RECHARGE_MAXIMUM);
}

/* The pilot targets MATE's craft, flying at speed; the pilot's craft has a
 * top speed of 100 with every recharge setting at its default. */
static struct craft_data *match_speed_world(int speed)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	fly(MATE, MATE_CRAFT, 1);
	craft->ai_flight.max_speed_cache = 100;
	g_test_mobiles[MATE_CRAFT].speed = (uint16_t)speed;
	g_players[PILOT].current_target_object_idx = MATE_CRAFT;
	return craft;
}

/* Enter matches the target's speed: the throttle becomes the target's speed
 * as a fraction of the craft's top speed. As flight_update_craft_steering_and
 * _speed says, the top speed rises as the recharge settings together sit
 * below their default, so a lower setting needs less throttle. A target as
 * fast as the top speed or faster gets full throttle. */
static void check_match_target_speed(void)
{
	struct craft_data *craft = match_speed_world(25);
	press(PILOT, FLIGHT_KEY_ENTER);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 65536 / 4);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_279_MATCHING_SPEED_WITH_TARGET);
	unsigned int at_default = craft->throttle_speed;

	craft->laser_recharge_level =
		POWER_RECHARGE_PARTIALLY_REDIRECTED_TO_ENGINES;
	press(PILOT, FLIGHT_KEY_PAD_ENTER);
	XVT_ASSERT_TRUE(craft->throttle_speed < at_default);

	g_test_mobiles[MATE_CRAFT].speed = 300;
	press(PILOT, FLIGHT_KEY_ENTER);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 65535);
	XVT_ASSERT_INT_EQ(
		last_message(),
		IFMSG_280_TRYING_TO_MATCH_SPEED_WITH_TARGET_THROTTLE_SET_TO_FULL);
	craft->laser_recharge_level = POWER_RECHARGE_MAINTENANCE;
	g_test_mobiles[MATE_CRAFT].speed = 100;
	press(PILOT, FLIGHT_KEY_ENTER);
	XVT_ASSERT_INT_EQ(
		last_message(),
		IFMSG_280_TRYING_TO_MATCH_SPEED_WITH_TARGET_THROTTLE_SET_TO_FULL);

	/* With no target the throttle stays. */
	g_players[PILOT].current_target_object_idx = -1;
	craft->throttle_speed = 777;
	press(PILOT, FLIGHT_KEY_ENTER);
	XVT_ASSERT_INT_EQ(craft->throttle_speed, 777);
}

/* Known failure match_speed_settings_above_default, issue #273: with the
 * recharge settings together above their default the top speed falls, so
 * matching a target's speed takes more throttle. On that path Enter shifts a
 * negative int left, behavior C leaves undefined, and the sanitizer stops
 * the program. The fix negates the margin as the unsigned 16-bit value it
 * is before the shift. */
static void check_match_speed_above_default(void)
{
	struct craft_data *craft = match_speed_world(25);
	craft->laser_recharge_level = POWER_RECHARGE_MAXIMUM;
	press(PILOT, FLIGHT_KEY_ENTER);
	XVT_ASSERT_TRUE(craft->throttle_speed > 65536 / 4);
}

/* ------------------------------------------------------------------------ */
/* The ' key: part of the cannon energy to the shields. */

/* The ' key moves laser charge to the shields, a step of 4 energy for each
 * unit, as much as the shields have room for, and clamps each bank to its
 * limit. Forward, the front bank fills first and then the rear; evenly
 * shared, each unit puts half a step in each bank. */
static void check_part_to_shields(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->shield_energy[0] = BANK_LIMIT - 40;
	craft->shield_energy[1] = BANK_LIMIT - 40;
	press(PILOT, FLIGHT_KEY_APOSTROPHE);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], BANK_LIMIT);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], BANK_LIMIT);
	XVT_ASSERT_INT_EQ(laser_charge(craft), 2 * FULL - 80 / STEP);
	XVT_ASSERT_INT_EQ(
		last_message(),
		IFMSG_132_TRANSFERRING_PARTIAL_POWER_FROM_CANNONS_TO_SHIELDS);

	fresh_world();
	craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->shield_distrib_mode = SHIELD_DISTRIBUTION_FULLY_FORWARD;
	craft->shield_energy[0] = BANK_LIMIT - 12;
	craft->shield_energy[1] = BANK_LIMIT - 28;
	press(PILOT, FLIGHT_KEY_SHIFT_F10);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], BANK_LIMIT);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], BANK_LIMIT);
	XVT_ASSERT_INT_EQ(laser_charge(craft), 2 * FULL - 40 / STEP);

	/* A step that takes the front bank past its limit is clamped. */
	fresh_world();
	craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->shield_distrib_mode = SHIELD_DISTRIBUTION_FULLY_FORWARD;
	craft->shield_energy[0] = BANK_LIMIT - 2;
	craft->shield_energy[1] = BANK_LIMIT - 30;
	press(PILOT, FLIGHT_KEY_APOSTROPHE);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], BANK_LIMIT);
	XVT_ASSERT_INT_EQ(laser_charge(craft), 2 * FULL - 32 / STEP);

	fresh_world();
	craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->shield_distrib_mode = SHIELD_DISTRIBUTION_FULLY_AFT;
	craft->shield_energy[0] = BANK_LIMIT - 20;
	craft->shield_energy[1] = BANK_LIMIT;
	press(PILOT, FLIGHT_KEY_APOSTROPHE);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], BANK_LIMIT);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], BANK_LIMIT);
	XVT_ASSERT_INT_EQ(laser_charge(craft), 2 * FULL - 20 / STEP);
}

/* One press moves at most 100 units, and only what the lasers hold. With
 * full shields, a craft without shields, or shields out, nothing moves. */
static void check_part_to_shields_limits(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->shield_energy[0] = 0;
	craft->shield_energy[1] = 0;
	press(PILOT, FLIGHT_KEY_APOSTROPHE);
	XVT_ASSERT_INT_EQ(laser_charge(craft), 2 * FULL - 100);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 100 * STEP / 2);

	fresh_world();
	craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->weapon_slots[0].laser_charge = 3;
	craft->weapon_slots[1].laser_charge = 0;
	press(PILOT, FLIGHT_KEY_APOSTROPHE);
	XVT_ASSERT_INT_EQ(laser_charge(craft), 0);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 100 + 3 * STEP / 2);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 100 + 3 * STEP / 2);

	fresh_world();
	craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->shield_energy[0] = BANK_LIMIT;
	craft->shield_energy[1] = BANK_LIMIT;
	press(PILOT, FLIGHT_KEY_APOSTROPHE);
	XVT_ASSERT_INT_EQ(laser_charge(craft), 2 * FULL);
	XVT_ASSERT_INT_EQ(g_message_log_total_count, 0);

	fresh_world();
	craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->system_flags &= ~CRAFT_SUBSYSTEM_FLAG_SHIELDS;
	press(PILOT, FLIGHT_KEY_APOSTROPHE);
	XVT_ASSERT_INT_EQ(laser_charge(craft), 2 * FULL);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_226_YOUR_CRAFT_DOES_NOT_HAVE_A_SHIELD_SYSTEM);
	craft->system_flags |= CRAFT_SUBSYSTEM_FLAG_SHIELDS;
	craft->working_subsystems &= ~CRAFT_SUBSYSTEM_FLAG_SHIELDS;
	press(PILOT, FLIGHT_KEY_APOSTROPHE);
	XVT_ASSERT_INT_EQ(laser_charge(craft), 2 * FULL);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_086_ARG_SYSTEM_IS_ARG);
}

/* ------------------------------------------------------------------------ */
/* The craft keys: shields, S-foils, beam, countermeasures, overdrive. */

/* S steps the shield distribution from even to aft to forward and back to
 * even, telling the player each one. Back to even, the energy of both banks
 * is shared out again: the front bank takes the share the model's shield
 * strength makes of the bank limit, a half, and the rear the rest. */
static void check_shield_distribution(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	press(PILOT, FLIGHT_KEY_S);
	XVT_ASSERT_INT_EQ(craft->shield_distrib_mode,
			  SHIELD_DISTRIBUTION_FULLY_AFT);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_070_SHIELDS_SET_FULLY_AFT);
	press(PILOT, FLIGHT_KEY_S);
	XVT_ASSERT_INT_EQ(craft->shield_distrib_mode,
			  SHIELD_DISTRIBUTION_FULLY_FORWARD);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_068_SHIELDS_SET_FULLY_FORWARD);
	craft->shield_energy[0] = 301;
	craft->shield_energy[1] = 51;
	press(PILOT, FLIGHT_KEY_S);
	XVT_ASSERT_INT_EQ(craft->shield_distrib_mode, SHIELD_DISTRIBUTION_EVEN);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_069_SHIELDS_SET_EVENLY_FORWARD_AND_AFT);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 176);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 352 - 176);

	/* A craft without shields, or with them out, keeps its setting. */
	craft->system_flags &= ~CRAFT_SUBSYSTEM_FLAG_SHIELDS;
	press(PILOT, FLIGHT_KEY_S);
	XVT_ASSERT_INT_EQ(craft->shield_distrib_mode, SHIELD_DISTRIBUTION_EVEN);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_226_YOUR_CRAFT_DOES_NOT_HAVE_A_SHIELD_SYSTEM);
	craft->system_flags |= CRAFT_SUBSYSTEM_FLAG_SHIELDS;
	craft->working_subsystems &= ~CRAFT_SUBSYSTEM_FLAG_SHIELDS;
	press(PILOT, FLIGHT_KEY_S);
	XVT_ASSERT_INT_EQ(craft->shield_distrib_mode, SHIELD_DISTRIBUTION_EVEN);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_086_ARG_SYSTEM_IS_ARG);
}

/* V moves an X-wing's S-foils: each press flips the closed bit and starts
 * the move, saying whether they close or open. A craft of another type has
 * none. */
static void check_s_foils(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	press(PILOT, FLIGHT_KEY_V);
	XVT_ASSERT_INT_EQ(craft->s_foil_state, 3);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_127_S_FOILS_CLOSING);
	craft->s_foil_state = 2;
	press(PILOT, FLIGHT_KEY_V);
	XVT_ASSERT_INT_EQ(craft->s_foil_state, 1);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_126_S_FOILS_OPENING);

	g_test_objects[PILOT_CRAFT].object_type = 2;
	craft->s_foil_state = 0;
	press(PILOT, FLIGHT_KEY_V);
	XVT_ASSERT_INT_EQ(craft->s_foil_state, 0);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_228_YOUR_CRAFT_DOES_NOT_HAVE_S_FOILS);
}

/* B turns a charged beam on and off, naming the beam; with a target the
 * activation message drops "select target". An empty beam does not turn on;
 * a missing or failed beam system is reported. */
static void check_beam_toggle(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->beam_type_id = BEAM_TYPE_JAMMING;
	craft->beam_charge = 50;
	press(PILOT, FLIGHT_KEY_B);
	XVT_ASSERT_INT_EQ(craft->beam_active, 1);
	XVT_ASSERT_INT_EQ(craft->beam_output, -1);
	XVT_ASSERT_INT_EQ(
		last_message(),
		IFMSG_250_JAMMING_BEAM_SYSTEM_ACTIVATED_SELECT_TARGET);
	press(PILOT, FLIGHT_KEY_B);
	XVT_ASSERT_INT_EQ(craft->beam_active, 0);
	XVT_ASSERT_INT_EQ(craft->beam_output, 0);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_244_JAMMING_BEAM_SYSTEM_DE_ACTIVATED);
	g_players[PILOT].current_target_object_idx = 5;
	press(PILOT, FLIGHT_KEY_B);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_238_JAMMING_BEAM_SYSTEM_ACTIVATED);

	craft->beam_active = 0;
	craft->beam_charge = 0;
	press(PILOT, FLIGHT_KEY_B);
	XVT_ASSERT_INT_EQ(craft->beam_active, 0);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_255_NO_ENERGY_FOR_BEAM_TO_ACTIVATE);
	craft->beam_charge = 50;
	craft->working_subsystems &= ~CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM;
	press(PILOT, FLIGHT_KEY_B);
	XVT_ASSERT_INT_EQ(craft->beam_active, 0);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_086_ARG_SYSTEM_IS_ARG);
	craft->system_flags &= ~CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM;
	press(PILOT, FLIGHT_KEY_B);
	XVT_ASSERT_INT_EQ(craft->beam_active, 0);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_227_YOUR_CRAFT_DOES_NOT_HAVE_A_BEAM_SYSTEM);
}

/* C launches chaff: 10 more seconds of chaff for one round. With no rounds
 * left the magazine is reported empty, and a craft without countermeasures
 * or with them out launches nothing. */
static void check_chaff(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->cm_type_id = COUNTERMEASURE_TYPE_CHAFF;
	craft->cm_ammo_count = 2;
	craft->chaff_active_seconds = 3;
	press(PILOT, FLIGHT_KEY_C);
	XVT_ASSERT_INT_EQ(craft->chaff_active_seconds, 13);
	XVT_ASSERT_INT_EQ(craft->cm_ammo_count, 1);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_367_CHAFF_BURST_TRIGGERED);
	craft->cm_ammo_count = 0;
	press(PILOT, FLIGHT_KEY_C);
	XVT_ASSERT_INT_EQ(craft->chaff_active_seconds, 13);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_363_CHAFF_MAGAZINE_EMPTY);
	craft->cm_type_id = COUNTERMEASURE_TYPE_NONE;
	press(PILOT, FLIGHT_KEY_C);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_362_NO_COUNTERMEASURES_LOADED);

	craft->cm_type_id = COUNTERMEASURE_TYPE_CHAFF;
	craft->cm_ammo_count = 2;
	craft->working_subsystems &= ~CRAFT_SUBSYSTEM_FLAG_COUNTERMEASURES;
	press(PILOT, FLIGHT_KEY_C);
	XVT_ASSERT_INT_EQ(craft->cm_ammo_count, 2);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_086_ARG_SYSTEM_IS_ARG);
}

/* N toggles the engine overdrive of a craft with the model of object type
 * 12: on while a gun holds charge, refused when none does, and off on the
 * next press. Other craft are told they have no such system. */
static void check_overdrive(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->engine_overdrive_off = UINT16_MAX;
	press(PILOT, FLIGHT_KEY_N);
	XVT_ASSERT_INT_EQ(craft->engine_overdrive_off, UINT16_MAX);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_229_YOUR_CRAFT_DOES_NOT_HAVE_A_SLAM_SYSTEM);

	g_test_objects[PILOT_CRAFT].object_type = 12;
	craft->weapon_slots[0].laser_charge = 0;
	press(PILOT, FLIGHT_KEY_N);
	XVT_ASSERT_INT_EQ(craft->engine_overdrive_off, 0);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_284_ENGINE_OVERDRIVE_BOOSTERS_ENGAGED);
	press(PILOT, FLIGHT_KEY_N);
	XVT_ASSERT_INT_EQ(craft->engine_overdrive_off, UINT16_MAX);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_285_ENGINE_OVERDRIVE_BOOSTERS_DISENGAGED);
	craft->weapon_slots[1].laser_charge = 0;
	press(PILOT, FLIGHT_KEY_N);
	XVT_ASSERT_INT_EQ(craft->engine_overdrive_off, UINT16_MAX);
	XVT_ASSERT_INT_EQ(
		last_message(),
		IFMSG_286_ENGINE_OVERDRIVE_BOOSTERS_CANNOT_BE_ENGAGED);
}

/* Shift+F5 to Shift+F7 keep the current target in presets 1 to 3, and F5
 * to F7 bring it back while its slot holds an object. With no target there
 * is nothing to keep. */
static void check_target_presets(void)
{
	fresh_world();
	fly(PILOT, PILOT_CRAFT, 0);
	fly(MATE, MATE_CRAFT, 1);
	press(PILOT, FLIGHT_KEY_SHIFT_F6);
	XVT_ASSERT_INT_EQ(g_players[PILOT].target_preset_slot[1], -1);
	g_players[PILOT].current_target_object_idx = MATE_CRAFT;
	press(PILOT, FLIGHT_KEY_SHIFT_F7);
	XVT_ASSERT_INT_EQ(g_players[PILOT].target_preset_slot[2], MATE_CRAFT);
	XVT_ASSERT_INT_EQ(g_players[PILOT].target_preset_slot[0], -1);
	XVT_ASSERT_INT_EQ(g_players[PILOT].target_preset_slot[1], -1);
	g_players[PILOT].current_target_object_idx = -1;
	press(PILOT, FLIGHT_KEY_F7);
	XVT_ASSERT_INT_EQ(g_players[PILOT].current_target_object_idx,
			  MATE_CRAFT);

	g_players[PILOT].current_target_object_idx = -1;
	g_test_objects[MATE_CRAFT].object_type = 0;
	press(PILOT, FLIGHT_KEY_F7);
	XVT_ASSERT_INT_EQ(g_players[PILOT].current_target_object_idx, -1);
}

/* ------------------------------------------------------------------------ */
/* The " key: all cannon energy to the shields. */

/* While the shields are below full, the " key moves the lasers' charge to the
 * shields, one unit of charge for a step of 4 energy, until both banks are
 * full; the lasers keep the charge the shields had no room for. Evenly
 * shared, each unit puts half a step in each bank. */
static void check_all_to_shields_fills_shields(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	int room = 2 * BANK_LIMIT - 200;
	press(PILOT, FLIGHT_KEY_QUOTES);
	XVT_ASSERT_TRUE(craft->shield_energy[0] >= BANK_LIMIT);
	XVT_ASSERT_TRUE(craft->shield_energy[1] >= BANK_LIMIT);
	XVT_ASSERT_INT_EQ(laser_charge(craft), 2 * FULL - room / STEP);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_360_TRANSFERRING_ALL_LASER_ENERGY_TO_SHIELDS);
}

/* With less charge than the shields have room for, the lasers give all of
 * it, and the shields gain at least a step for each unit. */
static void check_all_to_shields_empties_lasers(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->weapon_slots[0].laser_charge = 10;
	craft->weapon_slots[1].laser_charge = 7;
	press(PILOT, FLIGHT_KEY_QUOTES);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[0].laser_charge, 0);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[1].laser_charge, 0);
	XVT_ASSERT_TRUE(craft->shield_energy[0] >= 100 + 17 * STEP / 2);
	XVT_ASSERT_TRUE(craft->shield_energy[1] >= 100 + 17 * STEP / 2);
	XVT_ASSERT_TRUE(craft->shield_energy[0] < BANK_LIMIT);
}

/* A full bank takes nothing: shared aft, with the front bank full, the
 * charge goes to the rear bank alone until it is full too. */
static void check_all_to_shields_full_bank(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->shield_distrib_mode = SHIELD_DISTRIBUTION_FULLY_AFT;
	craft->shield_energy[0] = BANK_LIMIT;
	craft->shield_energy[1] = BANK_LIMIT - 100;
	press(PILOT, FLIGHT_KEY_QUOTES);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], BANK_LIMIT);
	XVT_ASSERT_TRUE(craft->shield_energy[1] >= BANK_LIMIT);
	XVT_ASSERT_INT_EQ(laser_charge(craft), 2 * FULL - 100 / STEP);
}

/* With both banks full, the key changes nothing. */
static void check_all_to_shields_when_full(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->shield_energy[0] = BANK_LIMIT;
	craft->shield_energy[1] = BANK_LIMIT;
	press(PILOT, FLIGHT_KEY_QUOTES);
	XVT_ASSERT_INT_EQ(laser_charge(craft), 2 * FULL);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], BANK_LIMIT);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], BANK_LIMIT);
	XVT_ASSERT_INT_EQ(g_message_log_total_count, 0);
}

/* A craft without shields is told so, and a craft whose shields are out is
 * told they are damaged; neither moves any energy. */
static void check_all_to_shields_without_shields(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->system_flags &= ~CRAFT_SUBSYSTEM_FLAG_SHIELDS;
	press(PILOT, FLIGHT_KEY_QUOTES);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_226_YOUR_CRAFT_DOES_NOT_HAVE_A_SHIELD_SYSTEM);
	XVT_ASSERT_INT_EQ(laser_charge(craft), 2 * FULL);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 100);

	fresh_world();
	craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->working_subsystems &= ~CRAFT_SUBSYSTEM_FLAG_SHIELDS;
	press(PILOT, FLIGHT_KEY_QUOTES);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_086_ARG_SYSTEM_IS_ARG);
	XVT_ASSERT_INT_EQ(laser_charge(craft), 2 * FULL);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 100);
}

/* Known failure all_to_shields_empty_lasers, issue #173: the key moves laser
 * charge to the shields, so with empty lasers it has nothing to move. After
 * the click it plays for empty lasers it still adds a step of shield energy
 * that no charge paid for. The fix drops that extra step after the transfer
 * and clamps the banks to the limit as the ' key does. */
static void check_all_to_shields_empty_lasers(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->weapon_slots[0].laser_charge = 0;
	craft->weapon_slots[1].laser_charge = 0;
	press(PILOT, FLIGHT_KEY_QUOTES);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 100);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 100);
}

/* Known failure all_to_shields_bank_over_limit, issue #173: the craft
 * field's comment holds a shield bank between 0 and twice the model's shield
 * strength. With the front bank 2 below that limit and the rear at it, the
 * transfer adds a whole step to the front and then the extra step half to
 * each bank, leaving both over the limit. The fix is the one above. */
static void check_all_to_shields_within_limit(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->shield_energy[0] = BANK_LIMIT - 2;
	craft->shield_energy[1] = BANK_LIMIT;
	press(PILOT, FLIGHT_KEY_QUOTES);
	XVT_ASSERT_TRUE(craft->shield_energy[0] <= BANK_LIMIT);
	XVT_ASSERT_TRUE(craft->shield_energy[1] <= BANK_LIMIT);
}

/* ------------------------------------------------------------------------ */
/* The ; key: shield energy to the cannons. */

/* The ; key fills the lasers from the shields at a step of 4 energy for each
 * unit of charge: forward, the front bank pays and the rear keeps its
 * energy; aft, the rear pays. Both slots here have the same room. */
static void check_shields_to_lasers_one_bank(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->weapon_slots[0].laser_charge = 100;
	craft->weapon_slots[1].laser_charge = 100;
	craft->shield_energy[0] = 300;
	craft->shield_distrib_mode = SHIELD_DISTRIBUTION_FULLY_FORWARD;
	int room = 2 * FULL - 200;
	press(PILOT, FLIGHT_KEY_SEMICOLON);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[0].laser_charge, FULL);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[1].laser_charge, FULL);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 300 - room * STEP);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 100);
	XVT_ASSERT_INT_EQ(
		last_message(),
		IFMSG_131_TRANSFERRING_PARTIAL_POWER_FROM_SHIELDS_TO_CANNON_SYSTEM);

	fresh_world();
	craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->weapon_slots[0].laser_charge = 100;
	craft->weapon_slots[1].laser_charge = 100;
	craft->shield_energy[1] = 300;
	craft->shield_distrib_mode = SHIELD_DISTRIBUTION_FULLY_AFT;
	press(PILOT, FLIGHT_KEY_SHIFT_F9);
	XVT_ASSERT_INT_EQ(laser_charge(craft), 2 * FULL);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 300 - room * STEP);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 100);
}

/* Evenly shared, each bank pays half. */
static void check_shields_to_lasers_even(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->weapon_slots[0].laser_charge = 100;
	craft->weapon_slots[1].laser_charge = 100;
	craft->shield_energy[0] = 300;
	craft->shield_energy[1] = 250;
	int room = 2 * FULL - 200;
	press(PILOT, FLIGHT_KEY_SEMICOLON);
	XVT_ASSERT_INT_EQ(laser_charge(craft), 2 * FULL);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 300 - room * STEP / 2);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 250 - room * STEP / 2);
}

/* A bank pays at most what it holds, and the lasers get a unit for each step
 * it paid. One call fills at most 100 units of charge. */
static void check_shields_to_lasers_limits(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->weapon_slots[0].laser_charge = 100;
	craft->weapon_slots[1].laser_charge = 110;
	craft->shield_energy[0] = 10 * STEP;
	craft->shield_distrib_mode = SHIELD_DISTRIBUTION_FULLY_FORWARD;
	press(PILOT, FLIGHT_KEY_SEMICOLON);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 0);
	XVT_ASSERT_INT_EQ(laser_charge(craft), 210 + 10);

	fresh_world();
	craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->weapon_slots[0].laser_charge = 0;
	craft->weapon_slots[1].laser_charge = 0;
	craft->shield_energy[1] = 1000;
	craft->shield_distrib_mode = SHIELD_DISTRIBUTION_FULLY_AFT;
	press(PILOT, FLIGHT_KEY_SEMICOLON);
	XVT_ASSERT_INT_EQ(laser_charge(craft), 100);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 1000 - 100 * STEP);
}

/* A full slot stays at full charge while the other slot fills. */
static void check_shields_to_lasers_full_stays_full(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->weapon_slots[1].laser_charge = FULL - 10;
	craft->shield_energy[0] = 300;
	craft->shield_distrib_mode = SHIELD_DISTRIBUTION_FULLY_FORWARD;
	press(PILOT, FLIGHT_KEY_SEMICOLON);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[0].laser_charge, FULL);
	XVT_ASSERT_TRUE(craft->weapon_slots[1].laser_charge > FULL - 10);
}

/* With full lasers nothing moves; a craft without shields is told so. */
static void check_shields_to_lasers_nothing_moves(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	press(PILOT, FLIGHT_KEY_SEMICOLON);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 100);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 100);
	XVT_ASSERT_INT_EQ(g_message_log_total_count, 0);

	fresh_world();
	craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->weapon_slots[0].laser_charge = 0;
	craft->system_flags &= ~CRAFT_SUBSYSTEM_FLAG_SHIELDS;
	press(PILOT, FLIGHT_KEY_SEMICOLON);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[0].laser_charge, 0);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 100);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_226_YOUR_CRAFT_DOES_NOT_HAVE_A_SHIELD_SYSTEM);
}

/* Known failure shields_to_lasers_rear_half, issue #174: evenly shared, the
 * ; key takes half the amount from each bank. The rear half is compared with
 * the whole amount, so a rear bank holding more than half and less than the
 * whole gives all it holds. Here the lasers have room for 10 units, 40
 * energy: each bank should give 20, and the rear bank's 30 is emptied. The
 * fix compares the rear half with the rear bank, as the front half is. */
static void check_shields_to_lasers_rear_half(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->weapon_slots[0].laser_charge = FULL - 10;
	craft->shield_energy[0] = 300;
	craft->shield_energy[1] = 30;
	press(PILOT, FLIGHT_KEY_SEMICOLON);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 300 - 20);
	XVT_ASSERT_INT_EQ(craft->shield_energy[1], 30 - 20);
}

/* Known failure shields_to_lasers_full_slot, issue #174: the energy the ;
 * key takes off the shields buys a unit of laser charge for each step. The
 * units are dealt round the slots in turn and a unit dealt to a full slot is
 * lost, so with one slot full and the other 10 units short, the shields pay
 * for 10 units and the lasers get 5. The fix deals a unit only to a slot
 * with room. */
static void check_shields_to_lasers_full_slot(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->weapon_slots[1].laser_charge = FULL - 10;
	craft->shield_energy[0] = 300;
	craft->shield_distrib_mode = SHIELD_DISTRIBUTION_FULLY_FORWARD;
	press(PILOT, FLIGHT_KEY_SEMICOLON);
	XVT_ASSERT_INT_EQ(craft->shield_energy[0], 300 - 10 * STEP);
	XVT_ASSERT_INT_EQ(craft->weapon_slots[1].laser_charge, FULL);
}

/* ------------------------------------------------------------------------ */
/* F8, F9 and F10: the beam, laser and shield recharge settings. */

/* Each of F8, F9 and F10 steps its system's recharge setting up one of the
 * five levels, from the highest back to the lowest, and tells the player the
 * new level. */
static void check_recharge_settings_step(void)
{
	static const struct {
		uint16_t key;
		int first_message;
	} keys[] = {
		{FLIGHT_KEY_F8,
		 IFMSG_081_BEAM_RECHARGE_FULLY_REDIRECTED_TO_ENGINES},
		{FLIGHT_KEY_F9,
		 IFMSG_071_CANNON_RECHARGE_FULLY_REDIRECTED_TO_ENGINES},
		{FLIGHT_KEY_F10,
		 IFMSG_076_SHIELD_RECHARGE_FULLY_REDIRECTED_TO_ENGINES},
	};
	for (size_t k = 0; k < sizeof keys / sizeof keys[0]; ++k) {
		fresh_world();
		struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
		power_recharge_level *levels[] = {
			&craft->beam_recharge_level,
			&craft->laser_recharge_level,
			&craft->shield_recharge_level,
		};
		static const int expected[] = {3, 4, 0, 1, 2};
		for (int press_count = 0; press_count < 5; ++press_count) {
			press(PILOT, keys[k].key);
			XVT_ASSERT_INT_EQ(*levels[k], expected[press_count]);
			XVT_ASSERT_INT_EQ(last_message(),
					  keys[k].first_message +
						  expected[press_count]);
			for (size_t other = 0; other < 3; ++other) {
				if (other != k) {
					XVT_ASSERT_INT_EQ(
						*levels[other],
						POWER_RECHARGE_MAINTENANCE);
				}
			}
		}
	}
}

/* F8 and F10 leave the setting of a system the craft lacks, or whose system
 * is out, as it was, and say why. */
static void check_recharge_settings_refused(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->system_flags &= ~CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM;
	press(PILOT, FLIGHT_KEY_F8);
	XVT_ASSERT_INT_EQ(craft->beam_recharge_level,
			  POWER_RECHARGE_MAINTENANCE);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_227_YOUR_CRAFT_DOES_NOT_HAVE_A_BEAM_SYSTEM);
	craft->system_flags |= CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM;
	craft->working_subsystems &= ~CRAFT_SUBSYSTEM_FLAG_BEAM_SYSTEM;
	press(PILOT, FLIGHT_KEY_F8);
	XVT_ASSERT_INT_EQ(craft->beam_recharge_level,
			  POWER_RECHARGE_MAINTENANCE);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_086_ARG_SYSTEM_IS_ARG);

	craft->system_flags &= ~CRAFT_SUBSYSTEM_FLAG_SHIELDS;
	press(PILOT, FLIGHT_KEY_F10);
	XVT_ASSERT_INT_EQ(craft->shield_recharge_level,
			  POWER_RECHARGE_MAINTENANCE);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_226_YOUR_CRAFT_DOES_NOT_HAVE_A_SHIELD_SYSTEM);
	craft->system_flags |= CRAFT_SUBSYSTEM_FLAG_SHIELDS;
	craft->working_subsystems &= ~CRAFT_SUBSYSTEM_FLAG_SHIELDS;
	press(PILOT, FLIGHT_KEY_F10);
	XVT_ASSERT_INT_EQ(craft->shield_recharge_level,
			  POWER_RECHARGE_MAINTENANCE);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_086_ARG_SYSTEM_IS_ARG);
}

/* Known failure laser_recharge_damaged_cannons, issue #177: F8 and F10 leave
 * a damaged system's setting as it was, and F9's own message for damaged
 * cannons says the laser system is damaged and inoperative. F9 steps the
 * setting first, so it changes anyway. The fix checks the cannons, installed
 * and working, before stepping, as F8 and F10 do for their systems. */
static void check_laser_recharge_damaged_cannons(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->working_subsystems &= ~CRAFT_SUBSYSTEM_FLAG_CANNONS;
	press(PILOT, FLIGHT_KEY_F9);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_086_ARG_SYSTEM_IS_ARG);
	XVT_ASSERT_INT_EQ(craft->laser_recharge_level,
			  POWER_RECHARGE_MAINTENANCE);
}

/* Known failure laser_recharge_without_cannons, issue #177: F9 never checks
 * that the craft has cannons, so a craft without them still has its laser
 * setting stepped. The fix is the one above. */
static void check_laser_recharge_without_cannons(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->system_flags &= ~CRAFT_SUBSYSTEM_FLAG_CANNONS;
	craft->working_subsystems &= ~CRAFT_SUBSYSTEM_FLAG_CANNONS;
	press(PILOT, FLIGHT_KEY_F9);
	XVT_ASSERT_INT_EQ(craft->laser_recharge_level,
			  POWER_RECHARGE_MAINTENANCE);
}

/* ------------------------------------------------------------------------ */
/* Shift+S and its answer: reinforcements. */

/* Flight group 1 arrives when team caller calls reinforcements, by the
 * trigger in pair, slot. */
static void reinforcement_group(int pair, int slot, int caller)
{
	g_mission_header.num_flight_groups = 2;
	struct mission_trigger *trigger = &g_mission_flight_groups[1]
						   .fg.arrival_triggers[pair]
						   .triggers[slot];
	trigger->condition = MISSION_COND_REINFORCEMENTS_CALLED;
	trigger->variable = (uint8_t)caller;
}

/* With a flight group that arrives on the player's team's call, Shift+S asks
 * the player to confirm with Space, and Space calls the reinforcements: the
 * team is marked as having called and pays 5,000 points of its bonus score.
 * The trigger may sit in either pair and either slot. */
static void check_reinforcements_called(void)
{
	for (int place = 0; place < 4; ++place) {
		fresh_world();
		fly(PILOT, PILOT_CRAFT, 0);
		fly(MATE, MATE_CRAFT, 1);
		reinforcement_group(place / 2, place % 2, 1);
		g_flight_mission_state.runtime
			.team_scores[TEAM_SCORE_BONUS][1] = 7000;
		press(MATE, FLIGHT_KEY_SHIFT_S);
		XVT_ASSERT_INT_EQ(g_players[MATE].pending_action_id, 3);
		XVT_ASSERT_TRUE(g_players[MATE].pending_action_timer > 0);
		press(MATE, FLIGHT_KEY_SPACE);
		XVT_ASSERT_INT_EQ(g_players[MATE].pending_action_id, 0);
		XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
					  .team_reinforcements_called[1],
				  1);
		XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
					  .team_scores[TEAM_SCORE_BONUS][1],
				  2000);
		XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime
					  .team_reinforcements_called[0],
				  0);
	}
	XVT_ASSERT_INT_EQ(g_message_log_total_count, 0);

	fresh_world();
	fly(PILOT, PILOT_CRAFT, 0);
	reinforcement_group(0, 0, 0);
	press(PILOT, FLIGHT_KEY_SHIFT_S);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_233_HIT_SPACE_TO_CONFIRM_REINFORCEMENT_REQUEST);
	press(PILOT, FLIGHT_KEY_SPACE);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_231_REQUEST_FOR_REINFORCEMENTS_ACKNOWLEDGED);
}

/* Shift+S asks nothing when no group arrives on the team's call (one on
 * another team's call does not count), once the team has called, or while
 * another request waits for an answer; with the radio out it says so. */
static void check_reinforcements_refused(void)
{
	fresh_world();
	fly(PILOT, PILOT_CRAFT, 0);
	reinforcement_group(0, 0, 1);
	press(PILOT, FLIGHT_KEY_SHIFT_S);
	XVT_ASSERT_INT_EQ(g_players[PILOT].pending_action_id, 0);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_230_NO_REINFORCEMENTS_AVAILABLE);

	fresh_world();
	fly(PILOT, PILOT_CRAFT, 0);
	reinforcement_group(1, 1, 0);
	g_flight_mission_state.runtime.team_reinforcements_called[0] = 1;
	press(PILOT, FLIGHT_KEY_SHIFT_S);
	XVT_ASSERT_INT_EQ(g_players[PILOT].pending_action_id, 0);
	XVT_ASSERT_INT_EQ(
		last_message(),
		IFMSG_232_REINFORCEMENTS_ALREADY_SENT_NO_MORE_AVAILABLE);

	fresh_world();
	fly(PILOT, PILOT_CRAFT, 0);
	reinforcement_group(0, 1, 0);
	g_players[PILOT].pending_action_id = 6;
	g_players[PILOT].pending_action_timer = 50;
	press(PILOT, FLIGHT_KEY_SHIFT_S);
	XVT_ASSERT_INT_EQ(g_players[PILOT].pending_action_id, 6);
	XVT_ASSERT_INT_EQ(g_players[PILOT].pending_action_timer, 50);

	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	reinforcement_group(1, 0, 0);
	craft->working_subsystems &= ~CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS;
	press(PILOT, FLIGHT_KEY_SHIFT_S);
	XVT_ASSERT_INT_EQ(g_players[PILOT].pending_action_id, 0);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_086_ARG_SYSTEM_IS_ARG);
}

/* Known failure reinforcements_paid_once, issue #172: the team pays 5,000
 * points when it calls reinforcements, and the field comment marks the team
 * once it has called. When two players of one team both press Shift+S before
 * either confirms, both confirmations go through and the team pays twice.
 * The fix tests the team's mark again when Space confirms. */
static void check_reinforcements_paid_once(void)
{
	fresh_world();
	fly(PILOT, PILOT_CRAFT, 0);
	fly(MATE, MATE_CRAFT, 0);
	reinforcement_group(0, 0, 0);
	press(PILOT, FLIGHT_KEY_SHIFT_S);
	press(MATE, FLIGHT_KEY_SHIFT_S);
	press(PILOT, FLIGHT_KEY_SPACE);
	press(MATE, FLIGHT_KEY_SPACE);
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.runtime.team_scores[TEAM_SCORE_BONUS][0],
		-5000);
}

/* ------------------------------------------------------------------------ */
/* Shift+E to a player's craft, and the Space answer. */

enum { NEAR_ENEMY = 6, FAR_ENEMY = 7 };

/* The pilot targets the craft of MATE, a player on the same team. Two craft
 * of team 2 fly near MATE's craft, NEAR_ENEMY the nearer. */
static void evade_world(void)
{
	fresh_world();
	fly(PILOT, PILOT_CRAFT, 0);
	fly(MATE, MATE_CRAFT, 0);
	g_players[PILOT].current_target_object_idx = MATE_CRAFT;
	int x = g_test_objects[MATE_CRAFT].world_x;
	int y = g_test_objects[MATE_CRAFT].world_y;
	int z = g_test_objects[MATE_CRAFT].world_z;
	for (int obj = NEAR_ENEMY; obj <= FAR_ENEMY; ++obj) {
		int distance = obj == NEAR_ENEMY ? 3000 : 9000;
		g_test_objects[obj].object_type = TEST_XWING;
		g_test_objects[obj].genus_id = 1;
		g_test_objects[obj].flight_group_idx = 2;
		g_test_objects[obj].world_x = x + distance;
		g_test_objects[obj].world_y = y + distance / 3;
		g_test_objects[obj].world_z = z - distance / 2;
		g_test_mobiles[obj].team = 2;
		g_test_craft[obj].working_subsystems = 0x03FF;
	}
	g_mission_flight_groups[2].fg.team = 2;
	g_mission_flight_groups[2].fg.iff = 2;
}

/* Shift+E on a craft a teammate flies asks that teammate to evade: request 9
 * from the pilot, with the 1416 ticks to answer the field comment gives a new
 * request. The teammate's Space answer targets the hostile craft nearest
 * it, and ends the request. */
static void check_evade_request(void)
{
	evade_world();
	press(PILOT, FLIGHT_KEY_SHIFT_E);
	XVT_ASSERT_INT_EQ(g_players[MATE].pending_action_id, 9);
	XVT_ASSERT_INT_EQ(g_players[MATE].pending_action_issuer_player_idx,
			  PILOT);
	XVT_ASSERT_INT_EQ(g_players[MATE].pending_action_timer, 1416);
	XVT_ASSERT_INT_EQ(g_players[PILOT].pending_action_id, 0);
	press(MATE, FLIGHT_KEY_SPACE);
	XVT_ASSERT_INT_EQ(g_players[MATE].current_target_object_idx,
			  NEAR_ENEMY);
	XVT_ASSERT_INT_EQ(g_players[MATE].pending_action_id, 0);
}

/* A player on another team does not take the order: no request, and the
 * pilot hears that the craft does not respond. */
static void check_evade_other_team(void)
{
	evade_world();
	g_players[MATE].team = 1;
	press(PILOT, FLIGHT_KEY_SHIFT_E);
	XVT_ASSERT_INT_EQ(g_players[MATE].pending_action_id, 0);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_146_CRAFT_NOT_RESPONDING_TO_ORDER);
}

/* Known failure evade_answer_stale_object, issue #175: the player field's
 * comment says pending_action_param is the object the pending request
 * names. Request 9 names none, but Shift+E leaves the field as an earlier
 * request set it, and the Space answer skips that object in its search. With
 * the nearer hostile craft left there, the teammate is given the farther one.
 * The fix sets the field to -1 when Shift+E posts request 9. */
static void check_evade_answer_nearest(void)
{
	evade_world();
	g_players[MATE].pending_action_param = NEAR_ENEMY;
	press(PILOT, FLIGHT_KEY_SHIFT_E);
	press(MATE, FLIGHT_KEY_SPACE);
	XVT_ASSERT_INT_EQ(g_players[MATE].current_target_object_idx,
			  NEAR_ENEMY);
}

/* ------------------------------------------------------------------------ */
/* Tab, Q, U and Alt+C. */

/* With more than one player in the flight, Tab opens a team message: an
 * empty line with the cursor, announced to the player. Alone, or with the
 * radio out, no message opens. */
static void check_tab_opens_team_message(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	g_active_flight_player_count = 2;
	press(PILOT, FLIGHT_KEY_TAB);
	XVT_ASSERT_INT_EQ(g_players[PILOT].chat_recipient_mode,
			  FLIGHT_CHAT_RECIPIENT_TEAM);
	XVT_ASSERT_INT_EQ(g_players[PILOT].msg_length, 0);
	XVT_ASSERT_INT_EQ(g_players[PILOT].msg_text[0], '_');
	XVT_ASSERT_INT_EQ(g_players[PILOT].msg_text[1], '\0');
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_375_TEAM_MESSAGE_ARG);

	fresh_world();
	craft = fly(PILOT, PILOT_CRAFT, 0);
	g_active_flight_player_count = 1;
	press(PILOT, FLIGHT_KEY_TAB);
	XVT_ASSERT_INT_EQ(g_players[PILOT].chat_recipient_mode,
			  FLIGHT_CHAT_RECIPIENT_INACTIVE);
	g_active_flight_player_count = 2;
	craft->working_subsystems &= ~CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS;
	press(PILOT, FLIGHT_KEY_TAB);
	XVT_ASSERT_INT_EQ(g_players[PILOT].chat_recipient_mode,
			  FLIGHT_CHAT_RECIPIENT_INACTIVE);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_086_ARG_SYSTEM_IS_ARG);
}

/* Q asks a flying player to confirm the end of the mission with Space:
 * request 2, naming no object. In a melee flown alone whose team has not
 * completed its goal, the player is warned of the penalty. A player out of
 * the flight is told to wait. */
static void check_quit_request(void)
{
	fresh_world();
	fly(PILOT, PILOT_CRAFT, 0);
	g_players[PILOT].pending_action_param = 4;
	press(PILOT, FLIGHT_KEY_Q);
	XVT_ASSERT_INT_EQ(g_players[PILOT].pending_action_id, 2);
	XVT_ASSERT_INT_EQ(g_players[PILOT].pending_action_param, -1);
	XVT_ASSERT_TRUE(g_players[PILOT].pending_action_timer > 0);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_215_PRESS_SPACE_TO_END_MISSION);

	fresh_world();
	fly(PILOT, PILOT_CRAFT, 0);
	g_mission_header.mission_type = MISSION_TYPE_MELEE;
	g_pilot_data.num_human_players_last_mission = 1;
	press(PILOT, FLIGHT_KEY_Q);
	XVT_ASSERT_INT_EQ(
		last_message(),
		IFMSG_216_LEAVING_NOW_IS_2000_POINT_PENALTY_PRESS_SPACE_TO_QUIT_ANYWAY);
	g_flight_mission_state.runtime.team_goal_status[0][0] = 1;
	press(PILOT, FLIGHT_KEY_Q);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_215_PRESS_SPACE_TO_END_MISSION);

	fresh_world();
	fly(PILOT, PILOT_CRAFT, 0);
	g_players[PILOT].participation_state = 2;
	press(PILOT, FLIGHT_KEY_Q);
	XVT_ASSERT_INT_EQ(g_players[PILOT].pending_action_id, 0);
	XVT_ASSERT_INT_EQ(
		last_message(),
		IFMSG_384_YOU_MUST_WAIT_UNTIL_THE_OTHER_PLAYERS_ARE_FINISHED);
}

/* Puts a craft of flight group 2 in slot obj, seconds old. */
static void place_aged_craft(int obj, int seconds)
{
	g_test_objects[obj].object_type = TEST_XWING;
	g_test_objects[obj].genus_id = 1;
	g_test_objects[obj].flight_group_idx = 2;
	g_test_objects[obj].world_x = 9000 + 100 * obj;
	g_test_mobiles[obj].seconds_alive = (uint16_t)seconds;
	g_test_craft[obj].leader_obj_idx = UINT8_MAX;
	g_test_craft[obj].working_subsystems = 0x03FF;
}

/* U targets the newest craft: the one alive the fewest seconds, not the
 * player's own, an explosion, a craft flying in a leader's formation, or one
 * neither active, disabled nor arriving. */
static void check_target_newest(void)
{
	fresh_world();
	fly(PILOT, PILOT_CRAFT, 0);
	g_test_mobiles[PILOT_CRAFT].seconds_alive = 1;
	g_test_craft[PILOT_CRAFT].leader_obj_idx = UINT8_MAX;
	place_aged_craft(5, 50);
	place_aged_craft(6, 30);
	place_aged_craft(7, 2);
	g_test_objects[7].genus_id = 13;
	place_aged_craft(8, 3);
	g_test_craft[8].leader_obj_idx = 5;
	place_aged_craft(9, 4);
	g_test_craft[9].object_kind = CRAFT_OBJECT_KIND_EXPLODING;
	place_aged_craft(10, 40);
	g_test_craft[10].object_kind = CRAFT_OBJECT_KIND_DISABLED;
	press(PILOT, FLIGHT_KEY_U);
	XVT_ASSERT_INT_EQ(g_players[PILOT].current_target_object_idx, 6);

	g_test_craft[10].object_kind =
		CRAFT_OBJECT_KIND_ARRIVING_FROM_HYPERSPACE;
	g_test_mobiles[10].seconds_alive = 20;
	press(PILOT, FLIGHT_KEY_U);
	XVT_ASSERT_INT_EQ(g_players[PILOT].current_target_object_idx, 10);
}

/* Alt+C drops the target. A player watching the target camera goes back to
 * the forward view of their own craft, with the camera's other states off. */
static void check_clear_target(void)
{
	fresh_world();
	fly(PILOT, PILOT_CRAFT, 0);
	fly(MATE, MATE_CRAFT, 1);
	g_players[MATE].current_target_object_idx = PILOT_CRAFT;
	struct player_view_state *view = &g_players[MATE].view_state;
	view->target_camera_active = 1;
	view->external_camera_active = 1;
	view->player_input_blocked = 1;
	view->camera_focus_obj_idx = PILOT_CRAFT;
	view->hud_aim_x = 9;
	press(MATE, FLIGHT_KEY_ALT_C);
	XVT_ASSERT_INT_EQ(g_players[MATE].current_target_object_idx, -1);
	XVT_ASSERT_INT_EQ(view->target_camera_active, 0);
	XVT_ASSERT_INT_EQ(view->external_camera_active, 0);
	XVT_ASSERT_INT_EQ(view->player_input_blocked, 0);
	XVT_ASSERT_INT_EQ(view->camera_focus_obj_idx, MATE_CRAFT);
	XVT_ASSERT_INT_EQ(view->hud_state_live, HUD_VIEW_FORWARD);
	XVT_ASSERT_INT_EQ(view->hud_aim_x, 0);

	/* Without the target camera, only the target goes. */
	g_players[MATE].current_target_object_idx = PILOT_CRAFT;
	view->external_camera_active = 1;
	press(MATE, FLIGHT_KEY_ALT_C);
	XVT_ASSERT_INT_EQ(g_players[MATE].current_target_object_idx, -1);
	XVT_ASSERT_INT_EQ(view->external_camera_active, 1);
}

/* ------------------------------------------------------------------------ */
/* Left and Right: paging the MFD. */

/* With no active page, Left makes the first open page active. With one,
 * Right moves on to the next open page, wrapping past the last, and the page
 * it leaves becomes the secondary one. */
static void check_mfd_paging(void)
{
	fresh_world();
	g_mfd_page_states[MFD_PAGE_DAMAGE] = MFD_PAGE_STATE_OPEN;
	g_mfd_page_states[MFD_PAGE_FRIENDLY_CRAFT] = MFD_PAGE_STATE_OPEN;
	press(PILOT, FLIGHT_KEY_LEFT);
	XVT_ASSERT_INT_EQ(g_mfd_active_page, MFD_PAGE_DAMAGE);
	XVT_ASSERT_INT_EQ(g_mfd_secondary_page, MFD_PAGE_NONE);
	press(PILOT, FLIGHT_KEY_RIGHT);
	XVT_ASSERT_INT_EQ(g_mfd_active_page, MFD_PAGE_FRIENDLY_CRAFT);
	XVT_ASSERT_INT_EQ(g_mfd_secondary_page, MFD_PAGE_DAMAGE);
	press(PILOT, FLIGHT_KEY_LEFT);
	XVT_ASSERT_INT_EQ(g_mfd_active_page, MFD_PAGE_DAMAGE);
	XVT_ASSERT_INT_EQ(g_mfd_secondary_page, MFD_PAGE_FRIENDLY_CRAFT);

	/* The only open page stays active, and nothing moves for a player who
	 * is not the local one. */
	g_mfd_page_states[MFD_PAGE_FRIENDLY_CRAFT] = MFD_PAGE_STATE_CLOSED;
	press(PILOT, FLIGHT_KEY_RIGHT);
	XVT_ASSERT_INT_EQ(g_mfd_active_page, MFD_PAGE_DAMAGE);
	XVT_ASSERT_INT_EQ(g_mfd_secondary_page, MFD_PAGE_FRIENDLY_CRAFT);
	g_mfd_page_states[MFD_PAGE_SCOREBOARD] = MFD_PAGE_STATE_OPEN;
	press(MATE, FLIGHT_KEY_RIGHT);
	XVT_ASSERT_INT_EQ(g_mfd_active_page, MFD_PAGE_DAMAGE);

	/* With no page open and none active, nothing changes. */
	fresh_world();
	press(PILOT, FLIGHT_KEY_RIGHT);
	XVT_ASSERT_INT_EQ(g_mfd_active_page, MFD_PAGE_NONE);
}

/* Known failure mfd_paging_no_open_page, issue #169: Left and Right page
 * through the open MFD pages. When the active page has closed and no page is
 * open, the search for the next open page never ends. The call should return
 * with the active page as it was. The fix stops the search after one round
 * of the pages. */
static void check_mfd_paging_no_open_page(void)
{
	fresh_world();
	g_mfd_active_page = MFD_PAGE_SCOREBOARD;
	g_mfd_secondary_page = MFD_PAGE_GOALS;
	fail_after_seconds(5);
	press(PILOT, FLIGHT_KEY_LEFT);
	XVT_ASSERT_INT_EQ(g_mfd_active_page, MFD_PAGE_SCOREBOARD);
}

/* ------------------------------------------------------------------------ */
/* flight_update_timers: the team victory limit. */

/* A melee mission flown by more than one player, with a 3-minute team
 * victory limit not yet started, 10 minutes left on the clock, and the
 * second about to tick. */
static void last_team_world(void)
{
	fresh_world();
	g_mission_header.mission_type = MISSION_TYPE_MELEE;
	g_flight_mission_state.max_connected_player_count_this_mission = 2;
	g_flight_mission_state.team_victory_time_limit_minutes = 3;
	g_flight_mission_state.mission_time_limit_minutes = 10;
	g_mission_countdown_clock.minutes = 10;
	g_mission_countdown_clock.seconds = 0;
	g_mission_elapsed_clock.subsecond_ticks = 1;
	g_elapsed_ticks = 4;
}

/* In a melee, the team victory limit starts on the second when one team is
 * left: the countdown is set to its minutes, which become the mission's time
 * limit. While two teams are in play it does not start. */
static void check_last_team_starts_countdown(void)
{
	last_team_world();
	fly(PILOT, PILOT_CRAFT, 0);
	fly(MATE, MATE_CRAFT, 1);
	flight_update_timers();
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.team_victory_time_limit_started, 0);
	XVT_ASSERT_INT_EQ(g_mission_countdown_clock.minutes, 9);

	last_team_world();
	fly(PILOT, PILOT_CRAFT, 0);
	fly(MATE, MATE_CRAFT, 1);
	g_players[MATE].participation_state = 2;
	flight_update_timers();
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.team_victory_time_limit_started, 1);
	XVT_ASSERT_INT_EQ(g_mission_countdown_clock.minutes, 3);
	XVT_ASSERT_INT_EQ(g_mission_countdown_clock.seconds, 0);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.mission_time_limit_minutes, 3);
}

/* In a combat mission the limit also starts when team 0 or team 1 has
 * completed its primary goal, with both teams still in play; in a melee
 * flown by one player it never starts. */
static void check_last_team_goal_and_single_player(void)
{
	last_team_world();
	g_mission_header.mission_type = MISSION_TYPE_COMBAT;
	fly(PILOT, PILOT_CRAFT, 0);
	fly(MATE, MATE_CRAFT, 1);
	g_flight_mission_state.runtime.team_goal_status[1][0] = 1;
	flight_update_timers();
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.team_victory_time_limit_started, 1);
	XVT_ASSERT_INT_EQ(g_mission_countdown_clock.minutes, 3);

	last_team_world();
	g_flight_mission_state.max_connected_player_count_this_mission = 1;
	fly(PILOT, PILOT_CRAFT, 0);
	flight_update_timers();
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.team_victory_time_limit_started, 0);
}

/* ------------------------------------------------------------------------ */
/* flight_update_timers: the countdowns of each step. */

/* A step of 10 ticks that does not end a second. */
static void timer_step_world(void)
{
	fresh_world();
	memset(&g_flight_global_countdown_timers, 0,
	       sizeof g_flight_global_countdown_timers);
	memset(g_player_flight_transient_timers, 0,
	       sizeof g_player_flight_transient_timers);
	g_elapsed_ticks = 10;
	g_mission_elapsed_clock.subsecond_ticks = 200;
}

/* Each step counts the shared countdown timers, the players' request and
 * beam timers, and the timers of the craft in the active region down by the
 * step's ticks, each stopping at 0; the AI think timer and the turret
 * cooldowns may go below 0. The transient timers of a player in the flight
 * count down with side effects on, and those of a player not in it do not.
 * An empty slot's craft record is left alone. */
static void check_timers_count_down(void)
{
	timer_step_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	g_flight_global_countdown_timers.mission_goal_evaluation_timer = 25;
	g_flight_global_countdown_timers.special_behavior_update_timer = 4;
	g_flight_global_countdown_timers.weapon_power_update_timer = 236;
	g_player_flight_transient_timers[PILOT].ready_message_pane_timer = 30;
	g_player_flight_transient_timers[PILOT].hull_hit_flash_timer = 6;
	g_player_flight_transient_timers[3].ready_message_pane_timer = 30;
	g_players[PILOT].pending_action_timer = 25;
	g_players[MATE].pending_action_timer = 4;
	g_players[PILOT].beam_fire_cooldown_timer = 12;
	g_players[MATE].beam_fire_cooldown_timer = 3;
	craft->ai_controller.think_timer = 5;
	craft->ai_controller.maneuver_timer = 7;
	craft->ai_controller.secondary_maneuver_timer = 30;
	craft->weapon_fire_inhibit_timer = 4;
	craft->cm_fire_cooldown_timer = 472;
	craft->turret_target_states[0].retarget_cooldown_timer = 5;
	craft->turret_target_states[1].retarget_cooldown_timer = 0;
	g_test_craft[5].ai_controller.maneuver_timer = 30;
	g_test_craft[5].cm_fire_cooldown_timer = 30;
	flight_update_timers();
	XVT_ASSERT_INT_EQ(
		g_flight_global_countdown_timers.mission_goal_evaluation_timer,
		15);
	XVT_ASSERT_INT_EQ(
		g_flight_global_countdown_timers.special_behavior_update_timer,
		0);
	XVT_ASSERT_INT_EQ(
		g_flight_global_countdown_timers.weapon_power_update_timer,
		226);
	XVT_ASSERT_INT_EQ(g_player_flight_transient_timers[PILOT]
				  .ready_message_pane_timer,
			  20);
	XVT_ASSERT_INT_EQ(
		g_player_flight_transient_timers[PILOT].hull_hit_flash_timer,
		0);
	XVT_ASSERT_INT_EQ(
		g_player_flight_transient_timers[3].ready_message_pane_timer,
		30);
	XVT_ASSERT_INT_EQ(g_players[PILOT].pending_action_timer, 15);
	XVT_ASSERT_INT_EQ(g_players[MATE].pending_action_timer, 0);
	XVT_ASSERT_INT_EQ(g_players[PILOT].beam_fire_cooldown_timer, 2);
	XVT_ASSERT_INT_EQ(g_players[MATE].beam_fire_cooldown_timer, 0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.think_timer, -5);
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_timer, 0);
	XVT_ASSERT_INT_EQ(craft->ai_controller.secondary_maneuver_timer, 20);
	XVT_ASSERT_INT_EQ(craft->weapon_fire_inhibit_timer, 0);
	XVT_ASSERT_INT_EQ(craft->cm_fire_cooldown_timer, 462);
	XVT_ASSERT_INT_EQ(
		craft->turret_target_states[0].retarget_cooldown_timer, -5);
	XVT_ASSERT_INT_EQ(
		craft->turret_target_states[1].retarget_cooldown_timer, 0);
	XVT_ASSERT_INT_EQ(g_test_craft[5].ai_controller.maneuver_timer, 30);
	XVT_ASSERT_INT_EQ(g_test_craft[5].cm_fire_cooldown_timer, 30);

	/* The other side of each stop at 0. */
	craft->ai_controller.maneuver_timer = 30;
	craft->ai_controller.secondary_maneuver_timer = 4;
	craft->weapon_fire_inhibit_timer = 30;
	craft->cm_fire_cooldown_timer = 7;
	flight_update_timers();
	XVT_ASSERT_INT_EQ(craft->ai_controller.maneuver_timer, 20);
	XVT_ASSERT_INT_EQ(craft->ai_controller.secondary_maneuver_timer, 0);
	XVT_ASSERT_INT_EQ(craft->weapon_fire_inhibit_timer, 20);
	XVT_ASSERT_INT_EQ(craft->cm_fire_cooldown_timer, 0);

	/* With side effects off the transient timers keep their time. */
	g_flight_sim_side_effects_suppressed = 1;
	g_player_flight_transient_timers[PILOT].ready_message_pane_timer = 30;
	flight_update_timers();
	XVT_ASSERT_INT_EQ(g_player_flight_transient_timers[PILOT]
				  .ready_message_pane_timer,
			  30);
	g_flight_sim_side_effects_suppressed = 0;
}

/* The next step, 10 ticks with 5 left in the second, ends the second: the
 * next second's 236 ticks start with the 5 overrun taken off. Both clocks
 * read 0. */
static void end_second(void)
{
	g_elapsed_ticks = 10;
	g_mission_elapsed_clock.subsecond_ticks = 5;
	memset(&g_mission_countdown_clock, 0, sizeof g_mission_countdown_clock);
	g_mission_elapsed_clock.hours = 0;
	g_mission_elapsed_clock.minutes = 0;
	g_mission_elapsed_clock.seconds = 0;
}

/* An empty world whose next step ends a second. */
static void second_world(void)
{
	timer_step_world();
	end_second();
}

/* Once a second the elapsed clock steps on, a second into a minute into an
 * hour, wrapping at 24 hours, and the countdown clock steps back. Every
 * live object is a second older. */
static void check_clock_second(void)
{
	second_world();
	fly(PILOT, PILOT_CRAFT, 0);
	g_test_mobiles[PILOT_CRAFT].seconds_alive = 40;
	g_test_mobiles[5].seconds_alive = 40;
	g_mission_elapsed_clock.hours = 23;
	g_mission_elapsed_clock.minutes = 59;
	g_mission_elapsed_clock.seconds = 59;
	g_mission_countdown_clock.minutes = 5;
	flight_update_timers();
	XVT_ASSERT_INT_EQ(g_mission_elapsed_clock.subsecond_ticks, 231);
	XVT_ASSERT_INT_EQ(g_mission_elapsed_clock.seconds, 0);
	XVT_ASSERT_INT_EQ(g_mission_elapsed_clock.minutes, 0);
	XVT_ASSERT_INT_EQ(g_mission_elapsed_clock.hours, 0);
	XVT_ASSERT_INT_EQ(g_mission_countdown_clock.minutes, 4);
	XVT_ASSERT_INT_EQ(g_mission_countdown_clock.seconds, 59);
	XVT_ASSERT_INT_EQ(g_test_mobiles[PILOT_CRAFT].seconds_alive, 41);
	XVT_ASSERT_INT_EQ(g_test_mobiles[5].seconds_alive, 40);

	second_world();
	g_mission_elapsed_clock.minutes = 7;
	g_mission_elapsed_clock.seconds = 58;
	flight_update_timers();
	XVT_ASSERT_INT_EQ(g_mission_elapsed_clock.seconds, 59);
	XVT_ASSERT_INT_EQ(g_mission_elapsed_clock.minutes, 7);
	XVT_ASSERT_INT_EQ(g_mission_countdown_clock.minutes, 0);
	XVT_ASSERT_INT_EQ(g_mission_countdown_clock.seconds, 0);

	second_world();
	g_mission_elapsed_clock.hours = 3;
	g_mission_elapsed_clock.minutes = 59;
	g_mission_elapsed_clock.seconds = 59;
	flight_update_timers();
	XVT_ASSERT_INT_EQ(g_mission_elapsed_clock.hours, 4);
	XVT_ASSERT_INT_EQ(g_mission_elapsed_clock.minutes, 0);

	/* A step that uses up exactly the ticks left ends the second; one that
	 * leaves a tick does not. */
	second_world();
	g_mission_elapsed_clock.subsecond_ticks = 10;
	flight_update_timers();
	XVT_ASSERT_INT_EQ(g_mission_elapsed_clock.seconds, 1);
	XVT_ASSERT_INT_EQ(g_mission_elapsed_clock.subsecond_ticks, 236);
	g_mission_elapsed_clock.subsecond_ticks = 11;
	flight_update_timers();
	XVT_ASSERT_INT_EQ(g_mission_elapsed_clock.seconds, 1);
	XVT_ASSERT_INT_EQ(g_mission_elapsed_clock.subsecond_ticks, 1);
}

/* With a time limit, the local player is warned when 2 minutes, 1 minute
 * and 2 seconds are left; when the limit runs out every player leaves the
 * mission and it is to end. Without side effects it does not end. */
static void check_time_limit(void)
{
	static const struct {
		int minutes;
		int seconds;
		int message;
	} warnings[] = {
		{2, 1, IFMSG_201_MISSION_ENDS_IN_2_MINUTES},
		{1, 1, IFMSG_202_MISSION_ENDS_IN_1_MINUTE},
		{0, 3, IFMSG_203_MISSION_TIME_EXPIRING},
	};
	for (size_t w = 0; w < sizeof warnings / sizeof warnings[0]; ++w) {
		second_world();
		g_flight_mission_state.mission_time_limit_minutes = 10;
		g_mission_countdown_clock.minutes =
			(uint8_t)warnings[w].minutes;
		g_mission_countdown_clock.seconds =
			(uint8_t)warnings[w].seconds;
		flight_update_timers();
		XVT_ASSERT_INT_EQ(last_message(), warnings[w].message);
		XVT_ASSERT_INT_EQ(g_flight_mission_state.mission_end_pending,
				  0);
	}
	second_world();
	g_flight_mission_state.mission_time_limit_minutes = 10;
	g_mission_countdown_clock.minutes = 1;
	g_mission_countdown_clock.seconds = 2;
	flight_update_timers();
	XVT_ASSERT_INT_EQ(g_message_log_total_count, 0);

	second_world();
	fly(PILOT, PILOT_CRAFT, 0);
	g_flight_mission_state.mission_time_limit_minutes = 10;
	g_mission_countdown_clock.seconds = 1;
	flight_update_timers();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.mission_end_pending, 1);
	for (int player = 0; player < 8; ++player) {
		XVT_ASSERT_INT_EQ(g_players[player].participation_state, 2);
	}

	second_world();
	fly(PILOT, PILOT_CRAFT, 0);
	g_flight_mission_state.mission_time_limit_minutes = 10;
	g_mission_countdown_clock.seconds = 1;
	g_flight_sim_side_effects_suppressed = 1;
	flight_update_timers();
	g_flight_sim_side_effects_suppressed = 0;
	XVT_ASSERT_INT_EQ(g_flight_mission_state.mission_end_pending, 0);
	XVT_ASSERT_INT_EQ(g_players[PILOT].participation_state, 1);
}

/* Once a second a player's craft with any working system counts down the
 * repair of the failed system with the lowest display row; when its time is
 * up the system is whole and working again, and the player is told. A
 * craft with no working system repairs nothing. */
static void check_repairs(void)
{
	enum { SHIELDS = 2, CANNONS = 3 };
	second_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->working_subsystems &=
		~(CRAFT_SUBSYSTEM_FLAG_SHIELDS | CRAFT_SUBSYSTEM_FLAG_CANNONS);
	for (int system = 0; system < 10; ++system) {
		craft->system_health[system] = 100;
		craft->system_display_slot_by_system[system] =
			(uint8_t)(system + 1);
	}
	craft->system_health[SHIELDS] = 0;
	craft->system_health[CANNONS] = 0;
	craft->system_display_slot_by_system[SHIELDS] = 6;
	craft->system_display_slot_by_system[CANNONS] = 5;
	craft->system_repair_seconds[SHIELDS] = 3;
	craft->system_repair_seconds[CANNONS] = 1;
	flight_update_timers();
	XVT_ASSERT_INT_EQ(craft->system_repair_seconds[CANNONS], 0);
	XVT_ASSERT_INT_EQ(craft->system_repair_seconds[SHIELDS], 3);
	XVT_ASSERT_INT_EQ(craft->system_health[CANNONS], 0);
	XVT_ASSERT_INT_EQ(g_message_log_total_count, 0);

	end_second();
	flight_update_timers();
	XVT_ASSERT_INT_EQ(craft->system_health[CANNONS], 100);
	XVT_ASSERT_TRUE((craft->working_subsystems &
			 CRAFT_SUBSYSTEM_FLAG_CANNONS) != 0);
	XVT_ASSERT_TRUE((craft->working_subsystems &
			 CRAFT_SUBSYSTEM_FLAG_SHIELDS) == 0);
	XVT_ASSERT_INT_EQ(craft->system_repair_seconds[SHIELDS], 3);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_086_ARG_SYSTEM_IS_ARG);

	end_second();
	flight_update_timers();
	XVT_ASSERT_INT_EQ(craft->system_repair_seconds[SHIELDS], 2);

	craft->working_subsystems = 0;
	end_second();
	flight_update_timers();
	XVT_ASSERT_INT_EQ(craft->system_repair_seconds[SHIELDS], 2);
}

/* Known failure last_team_past_team_table, issue #171: the check for the
 * last team marks each team in play in a local table of the ten teams 0 to
 * 9, indexed by the player's team unchecked. A player on team 10, which a
 * flight group's team byte allows, writes past the table. With players on
 * teams 0 and 10 both flying, two teams are in play and the limit should not
 * start. The fix widens the table to every team a byte names and counts
 * them all. */
static void check_last_team_team_ten(void)
{
	last_team_world();
	fly(PILOT, PILOT_CRAFT, 0);
	fly(MATE, MATE_CRAFT, 10);
	flight_update_timers();
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.team_victory_time_limit_started, 0);
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
			{"mfd_paging_no_open_page",
			 check_mfd_paging_no_open_page},
			{"last_team_past_team_table", check_last_team_team_ten},
			{"reinforcements_paid_once",
			 check_reinforcements_paid_once},
			{"all_to_shields_empty_lasers",
			 check_all_to_shields_empty_lasers},
			{"all_to_shields_bank_over_limit",
			 check_all_to_shields_within_limit},
			{"shields_to_lasers_rear_half",
			 check_shields_to_lasers_rear_half},
			{"shields_to_lasers_full_slot",
			 check_shields_to_lasers_full_slot},
			{"evade_answer_stale_object",
			 check_evade_answer_nearest},
			{"laser_recharge_damaged_cannons",
			 check_laser_recharge_damaged_cannons},
			{"laser_recharge_without_cannons",
			 check_laser_recharge_without_cannons},
			{"match_speed_settings_above_default",
			 check_match_speed_above_default},
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
	check_hyperspace_abort();
	check_proving_grounds_drops_keys();
	check_throttle_fixed_keys();
	check_throttle_sixteenths();
	check_throttle_presets();
	check_match_target_speed();
	check_part_to_shields();
	check_part_to_shields_limits();
	check_shield_distribution();
	check_s_foils();
	check_beam_toggle();
	check_chaff();
	check_overdrive();
	check_target_presets();
	check_all_to_shields_fills_shields();
	check_all_to_shields_empties_lasers();
	check_all_to_shields_full_bank();
	check_all_to_shields_when_full();
	check_all_to_shields_without_shields();
	check_shields_to_lasers_one_bank();
	check_shields_to_lasers_even();
	check_shields_to_lasers_limits();
	check_shields_to_lasers_full_stays_full();
	check_shields_to_lasers_nothing_moves();
	check_recharge_settings_step();
	check_recharge_settings_refused();
	check_reinforcements_called();
	check_reinforcements_refused();
	check_evade_request();
	check_evade_other_team();
	check_tab_opens_team_message();
	check_quit_request();
	check_target_newest();
	check_clear_target();
	check_mfd_paging();
	check_last_team_starts_countdown();
	check_last_team_goal_and_single_player();
	check_timers_count_down();
	check_clock_second();
	check_time_limit();
	check_repairs();
	return 0;
}
