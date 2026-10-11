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

#include <SDL3/SDL_log.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "log_lines.h"
#include "test_assert.h"
#include "xvt/assets/object_genus.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/ai/pai.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/mfd.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/transfm2.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/math/math.h"
#include "xvt/render/renderer.h"
#include "xvt/util/game_rand.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/log/log.h"

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

/* The last kept line that starts with start (an event and any of its first
 * fields), or NULL. */
static const char *find_line(const char *start)
{
	size_t length = strlen(start);
	for (int i = g_log_line_count - 1; i >= 0; --i) {
		if (strncmp(g_log_lines[i], start, length) == 0) {
			return g_log_lines[i];
		}
	}
	return NULL;
}

/* 1 when the last kept line that starts with start also holds fields. */
static int line_holds(const char *start, const char *fields)
{
	const char *line = find_line(start);
	return line != NULL && strstr(line, fields) != NULL;
}

/* 1 when a kept line starts with the text printed from format. */
__attribute__((format(printf, 1, 2))) static int logged(const char *format, ...)
{
	char start[LOG_LINES_SIZE];
	va_list args;
	va_start(args, format);
	vsnprintf(start, sizeof start, format, args);
	va_end(args);
	return find_line(start) != NULL;
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
	g_log_line_count = 0;
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

/* Presses key for player; the log lines kept are the ones it writes. */
static void press(int player, uint16_t key)
{
	g_log_line_count = 0;
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

/* ------------------------------------------------------------------------ */
/* The map view's keys. */

enum {
	MAP_TARGET = MATE_CRAFT,
	TARGET_EXTENT = 800,
	MAP_DEFAULT_DISTANCE = 512,
	CAMERA_START_X = 11,
	CAMERA_START_Y = 22,
	CAMERA_START_Z = 33,
};

/* The pilot flies with the map view open in the given camera state, a free
 * camera at (11, 22, 33), no object followed and no aim object. The craft
 * of object type 1 has a bounding extent of 800 and MATE's craft is placed
 * apart from every coordinate of the camera. */
static struct player_view_state *map_world(int camera_state)
{
	fresh_world();
	fly(PILOT, PILOT_CRAFT, 0);
	fly(MATE, MATE_CRAFT, 1);
	g_object_type_table[TEST_XWING].max_bounds_extent = TARGET_EXTENT;
	g_players[PILOT].map_camera_state = (uint8_t)camera_state;
	struct player_view_state *view = &g_players[PILOT].view_state;
	view->camera_world_x = CAMERA_START_X;
	view->camera_world_y = CAMERA_START_Y;
	view->camera_world_z = CAMERA_START_Z;
	view->camera_focus_obj_idx = UINT16_MAX;
	view->aim_target_idx = UINT16_MAX;
	view->camera_distance = 0;
	return view;
}

/* C in the map view, from a camera state above 1 with a target, puts the
 * camera over the target in x and y and leaves its height; otherwise the
 * camera stays where it was. */
static void check_map_c_centers_on_target(void)
{
	static const struct {
		int state;
		int has_target;
	} cases[] = {{1, 1}, {2, 1}, {3, 1}, {2, 0}};
	for (size_t k = 0; k < sizeof cases / sizeof cases[0]; ++k) {
		struct player_view_state *view = map_world(cases[k].state);
		g_players[PILOT].current_target_object_idx =
			cases[k].has_target ? MAP_TARGET : -1;
		press(PILOT, FLIGHT_KEY_C);
		int moved = cases[k].state > 1 && cases[k].has_target;
		XVT_ASSERT_INT_EQ(view->camera_world_x,
				  moved ? g_test_objects[MAP_TARGET].world_x
					: CAMERA_START_X);
		XVT_ASSERT_INT_EQ(view->camera_world_y,
				  moved ? g_test_objects[MAP_TARGET].world_y
					: CAMERA_START_Y);
		XVT_ASSERT_INT_EQ(view->camera_world_z, CAMERA_START_Z);
	}
}

/* Z in the map view above camera state 1 puts the camera over the target in
 * x and y, and above it in height by 16 times the target type's bounding
 * extent. With no target nothing moves. */
static void check_map_z_over_target(void)
{
	struct player_view_state *view = map_world(2);
	g_players[PILOT].current_target_object_idx = MAP_TARGET;
	press(PILOT, FLIGHT_KEY_Z);
	XVT_ASSERT_INT_EQ(view->camera_world_x,
			  g_test_objects[MAP_TARGET].world_x);
	XVT_ASSERT_INT_EQ(view->camera_world_y,
			  g_test_objects[MAP_TARGET].world_y);
	XVT_ASSERT_INT_EQ(view->camera_world_z,
			  g_test_objects[MAP_TARGET].world_z +
				  16 * TARGET_EXTENT);
	view = map_world(3);
	press(PILOT, FLIGHT_KEY_Z);
	XVT_ASSERT_INT_EQ(view->camera_world_x, CAMERA_START_X);
	XVT_ASSERT_INT_EQ(view->camera_world_y, CAMERA_START_Y);
	XVT_ASSERT_INT_EQ(view->camera_world_z, CAMERA_START_Z);
}

/* Z in camera state 1 with a followed object sets the camera's distance to
 * that object type's bounding extent plus 512 and moves nothing else. */
static void check_map_z_distance_to_followed(void)
{
	struct player_view_state *view = map_world(1);
	view->camera_focus_obj_idx = PILOT_CRAFT;
	g_players[PILOT].current_target_object_idx = MAP_TARGET;
	press(PILOT, FLIGHT_KEY_Z);
	XVT_ASSERT_INT_EQ(view->camera_distance,
			  TARGET_EXTENT + MAP_DEFAULT_DISTANCE);
	XVT_ASSERT_INT_EQ(view->camera_world_x, CAMERA_START_X);
	XVT_ASSERT_INT_EQ(view->camera_world_y, CAMERA_START_Y);
	XVT_ASSERT_INT_EQ(view->camera_world_z, CAMERA_START_Z);
}

/* Z in camera state 1 with nothing followed and a target puts the camera
 * behind the target as the view angles look at it: the target's position
 * less the distance (the extent plus 512) along the view's third axis. The
 * camera ends that distance from the target. */
static void check_map_z_behind_target(void)
{
	static const struct {
		int16_t pitch;
		int16_t yaw;
	} angles[] = {{0x1000, 0x2000}, {-0x0900, 0x5400}};
	for (size_t k = 0; k < sizeof angles / sizeof angles[0]; ++k) {
		struct player_view_state *view = map_world(1);
		view->view_pitch = angles[k].pitch;
		view->view_yaw = angles[k].yaw;
		g_players[PILOT].current_target_object_idx = MAP_TARGET;
		press(PILOT, FLIGHT_KEY_Z);
		int distance = TARGET_EXTENT + MAP_DEFAULT_DISTANCE;
		XVT_ASSERT_INT_EQ(view->camera_distance, distance);
		fview_build_camera_orient(0, angles[k].pitch, angles[k].yaw, 0,
					  0, 0, NULL);
		const struct object_record *target =
			&g_test_objects[MAP_TARGET];
		XVT_ASSERT_INT_EQ(
			view->camera_world_x,
			target->world_x -
				math_mul_q15(distance, g_cam_mat_r2_x));
		XVT_ASSERT_INT_EQ(
			view->camera_world_y,
			target->world_y -
				math_mul_q15(distance, g_cam_mat_r2_y));
		XVT_ASSERT_INT_EQ(
			view->camera_world_z,
			target->world_z -
				math_mul_q15(distance, g_cam_mat_r2_z));
		double dx = view->camera_world_x - target->world_x;
		double dy = view->camera_world_y - target->world_y;
		double dz = view->camera_world_z - target->world_z;
		double length = dx * dx + dy * dy + dz * dz;
		XVT_ASSERT_TRUE(length > (distance - 3.0) * (distance - 3.0));
		XVT_ASSERT_TRUE(length < (distance + 3.0) * (distance + 3.0));
	}
}

/* Z in camera state 1 with nothing followed and no target changes nothing. */
static void check_map_z_nothing_to_follow(void)
{
	struct player_view_state *view = map_world(1);
	press(PILOT, FLIGHT_KEY_Z);
	XVT_ASSERT_INT_EQ(view->camera_distance, 0);
	XVT_ASSERT_INT_EQ(view->camera_world_x, CAMERA_START_X);
	XVT_ASSERT_INT_EQ(view->camera_world_y, CAMERA_START_Y);
	XVT_ASSERT_INT_EQ(view->camera_world_z, CAMERA_START_Z);
}

/* The keypad minus key lets go of the object the map camera looks at, and
 * the keypad plus key aims the camera at the target: from a camera state
 * above 1 it also sets the state to 1 and the look offset to 0, and from
 * state 1 it leaves both. With no target it changes nothing. */
static void check_map_aim_keys(void)
{
	struct player_view_state *view = map_world(2);
	view->aim_target_idx = MAP_TARGET;
	press(PILOT, FLIGHT_KEY_PAD_MINUS);
	XVT_ASSERT_INT_EQ(view->aim_target_idx, UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_players[PILOT].map_camera_state, 2);

	for (int state = 1; state <= 3; ++state) {
		view = map_world(state);
		view->hud_aim_x = 77;
		g_players[PILOT].current_target_object_idx = MAP_TARGET;
		press(PILOT, FLIGHT_KEY_PAD_PLUS);
		XVT_ASSERT_INT_EQ(view->aim_target_idx, MAP_TARGET);
		XVT_ASSERT_INT_EQ(g_players[PILOT].map_camera_state,
				  state > 1 ? 1 : state);
		XVT_ASSERT_INT_EQ(view->hud_aim_x, state > 1 ? 0 : 77);
	}
	view = map_world(2);
	view->hud_aim_x = 77;
	press(PILOT, FLIGHT_KEY_PAD_PLUS);
	XVT_ASSERT_INT_EQ(view->aim_target_idx, UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_players[PILOT].map_camera_state, 2);
	XVT_ASSERT_INT_EQ(view->hud_aim_x, 77);
}

/* ------------------------------------------------------------------------ */
/* Shift+C, a call to cover the pilot. */

enum { ATTACKER = 6, WINGMAN = 8, REQUEST_COVER = 5 };

/* The pilot (team 0, in the local seat) flies with MATE, a second player of
 * team 0, and a wingman of the computer's of team 0 that no player flies.
 * A hostile craft of the computer's, in object slot 6, has the pilot's craft
 * for its target and is on the attack. */
static void cover_world(void)
{
	fresh_world();
	fly(PILOT, PILOT_CRAFT, 0);
	fly(MATE, MATE_CRAFT, 0);
	g_active_flight_player_count = 2;
	for (int obj = ATTACKER; obj <= WINGMAN; obj += 2) {
		int team = obj == ATTACKER ? 2 : 0;
		g_test_objects[obj].object_type = TEST_XWING;
		g_test_objects[obj].genus_id = 1;
		g_test_objects[obj].flight_group_idx = (uint8_t)team;
		g_test_objects[obj].world_x = 5000 + 1000 * obj;
		g_test_objects[obj].world_y = 7000;
		g_test_objects[obj].world_z = 3000;
		g_test_mobiles[obj].team = (uint8_t)team;
		g_test_craft[obj].working_subsystems = 0x03FF;
	}
	g_test_craft[ATTACKER].ai_controller.target_obj_idx = PILOT_CRAFT;
	g_test_craft[ATTACKER].ai_controller.maneuver_mode =
		AI_MANEUVER_MODE_ATTACK;
	g_test_craft[WINGMAN].ai_controller.candidate_target_idx = UINT16_MAX;
	g_mission_flight_groups[2].fg.team = 2;
	g_mission_flight_groups[2].fg.iff = 2;
	g_mission_teams[0].allies[0] = 1;
	g_mission_teams[1].allies[1] = 1;
	g_mission_teams[2].allies[2] = 1;
}

/* Shift+C asks every other player of the pilot's side to cover the pilot:
 * request 5 from the pilot, naming the craft attacking the pilot, with the
 * 1416 ticks to answer a new request gets. Nothing is posted for the pilot. */
static void check_cover_request_names_attacker(void)
{
	cover_world();
	press(PILOT, FLIGHT_KEY_SHIFT_C);
	XVT_ASSERT_INT_EQ(g_players[MATE].pending_action_id, REQUEST_COVER);
	XVT_ASSERT_INT_EQ(g_players[MATE].pending_action_issuer_player_idx,
			  PILOT);
	XVT_ASSERT_INT_EQ(g_players[MATE].pending_action_param, ATTACKER);
	XVT_ASSERT_INT_EQ(g_players[MATE].pending_action_timer, 1416);
	XVT_ASSERT_INT_EQ(g_players[PILOT].pending_action_id, 0);
}

/* The call also goes to the pilot's computer wingmen as an order to attack
 * the craft hitting the pilot: the wingman takes it as its candidate target. */
static void check_cover_orders_wingman(void)
{
	cover_world();
	press(PILOT, FLIGHT_KEY_SHIFT_C);
	XVT_ASSERT_INT_EQ(
		g_test_craft[WINGMAN].ai_controller.candidate_target_idx,
		ATTACKER);
}

/* With no craft attacking the pilot, the request names the pilot's own craft
 * and no order goes to the wingmen. */
static void check_cover_without_attacker(void)
{
	cover_world();
	g_test_craft[ATTACKER].ai_controller.maneuver_mode =
		AI_MANEUVER_MODE_NULL;
	press(PILOT, FLIGHT_KEY_SHIFT_C);
	XVT_ASSERT_INT_EQ(g_players[MATE].pending_action_id, REQUEST_COVER);
	XVT_ASSERT_INT_EQ(g_players[MATE].pending_action_param, PILOT_CRAFT);
	XVT_ASSERT_INT_EQ(
		g_test_craft[WINGMAN].ai_controller.candidate_target_idx,
		UINT16_MAX);
}

/* A player who is the pilot's ally on another team gets the request; one
 * whose team is not allied to the pilot's does not, and neither does a
 * player who already has a request pending, has no craft, has left the
 * flight, has a radio that is out, or is the attacking craft's pilot. */
static void check_cover_who_is_asked(void)
{
	static const struct {
		int ally_team;
		int allied;
		int busy;
		int craft;
		int active;
		int radio;
		int attacker;
		int asked;
	} cases[] = {
		{0, 0, 0, 1, 1, 1, 0, 1}, {1, 1, 0, 1, 1, 1, 0, 1},
		{1, 0, 0, 1, 1, 1, 0, 0}, {0, 0, 1, 1, 1, 1, 0, 0},
		{0, 0, 0, 0, 1, 1, 0, 0}, {0, 0, 0, 1, 0, 1, 0, 0},
		{0, 0, 0, 1, 1, 0, 0, 0}, {0, 0, 0, 1, 1, 1, 1, 0},
	};
	for (size_t k = 0; k < sizeof cases / sizeof cases[0]; ++k) {
		cover_world();
		g_players[MATE].team = (int16_t)cases[k].ally_team;
		g_mission_teams[1].allies[0] = (uint8_t)cases[k].allied;
		g_mission_teams[0].allies[1] = (uint8_t)cases[k].allied;
		if (cases[k].busy) {
			g_players[MATE].pending_action_id = 3;
		}
		if (!cases[k].craft) {
			g_players[MATE].object_index = -1;
		}
		if (!cases[k].active) {
			g_players[MATE].participation_state = 0;
		}
		if (!cases[k].radio) {
			g_test_craft[MATE_CRAFT].working_subsystems = 0;
		}
		if (cases[k].attacker) {
			g_players[MATE].object_index = ATTACKER;
		}
		press(PILOT, FLIGHT_KEY_SHIFT_C);
		XVT_ASSERT_INT_EQ(g_players[MATE].pending_action_id,
				  cases[k].asked ? REQUEST_COVER
						 : (cases[k].busy ? 3 : 0));
	}
}

/* When the player asked is the one in the local seat, the call is announced
 * with a message that says to hit Space to target the attacker, or the
 * caller when there is no attacker; nothing is announced to the seat of a
 * player who is not asked. */
static void check_cover_announced_to_local(void)
{
	cover_world();
	g_test_craft[ATTACKER].ai_controller.target_obj_idx = MATE_CRAFT;
	press(MATE, FLIGHT_KEY_SHIFT_C);
	XVT_ASSERT_INT_EQ(g_players[PILOT].pending_action_id, REQUEST_COVER);
	XVT_ASSERT_INT_EQ(g_players[PILOT].pending_action_param, ATTACKER);
	XVT_ASSERT_INT_EQ(
		last_message(),
		IFMSG_277_FROM_ARG_COVER_ME_HIT_SPACE_TO_TARGET_ATTACKER);
	cover_world();
	g_test_craft[ATTACKER].ai_controller.maneuver_mode =
		AI_MANEUVER_MODE_NULL;
	press(MATE, FLIGHT_KEY_SHIFT_C);
	XVT_ASSERT_INT_EQ(g_players[PILOT].pending_action_param, MATE_CRAFT);
	XVT_ASSERT_INT_EQ(last_message(),
			  IFMSG_278_FROM_ARG_COVER_ME_HIT_SPACE_TO_TARGET_ME);
}

/* With the radio out, Shift+C asks nobody and says the system is down. */
static void check_cover_radio_out(void)
{
	cover_world();
	g_test_craft[PILOT_CRAFT].working_subsystems = 0;
	press(PILOT, FLIGHT_KEY_SHIFT_C);
	XVT_ASSERT_INT_EQ(g_players[MATE].pending_action_id, 0);
	XVT_ASSERT_INT_EQ(
		g_test_craft[WINGMAN].ai_controller.candidate_target_idx,
		UINT16_MAX);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_086_ARG_SYSTEM_IS_ARG);
}

/* ------------------------------------------------------------------------ */
/* W, the weapon selection. */

enum { FIRST_WARHEAD_SLOT = 2 };

/* The pilot's craft has two cannon groups and two warhead launchers, proton
 * torpedoes and concussion missiles, six of each in either of the launchers'
 * slots, in the launcher mode 1, with cannons selected at bank 0. */
static struct craft_data *armed_world(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	xwing_model()->warhead_launcher_first_slot[0] = FIRST_WARHEAD_SLOT;
	craft->cannon_group_count = 2;
	craft->warhead_launcher_count = 2;
	craft->warhead_slot_type_ids[0] = WARHEAD_OBJECT_TYPE_PROTON_TORPEDO;
	craft->warhead_slot_type_ids[1] =
		WARHEAD_OBJECT_TYPE_CONCUSSION_MISSILE;
	craft->weapon_slots[FIRST_WARHEAD_SLOT].ammo_count = 6;
	craft->weapon_slots[FIRST_WARHEAD_SLOT + 1].ammo_count = 6;
	craft->warhead_launcher_flags[0] = 1;
	return craft;
}

/* W steps through the cannon groups and then the warhead launchers and round
 * to the first cannon group, naming each one it arms. */
static void check_weapon_cycle(void)
{
	static const struct {
		int mode;
		int bank;
		int message;
	} steps[] = {
		{0, 1, IFMSG_004_ION_CANNONS_ARMED},
		{1, 0, IFMSG_008_PROTON_TORPEDO_LAUNCHER_ARMED},
		{1, 1, IFMSG_009_CONCUSSION_MISSILE_LAUNCHER_ARMED},
		{0, 0, IFMSG_003_LASER_CANNONS_ARMED},
		{0, 1, IFMSG_004_ION_CANNONS_ARMED},
	};
	armed_world();
	for (size_t k = 0; k < sizeof steps / sizeof steps[0]; ++k) {
		press(PILOT, FLIGHT_KEY_W);
		XVT_ASSERT_INT_EQ(g_players[PILOT].selected_weapon_mode,
				  steps[k].mode);
		XVT_ASSERT_INT_EQ(g_players[PILOT].selected_weapon_bank,
				  steps[k].bank);
		XVT_ASSERT_INT_EQ(last_message(), steps[k].message);
	}
}

/* Without a launcher, or with no warheads in a launcher's slots, W goes on
 * from the last cannon group to the first; with one cannon group it stays at
 * it. */
static void check_weapon_skips_empty_launchers(void)
{
	for (int cause = 0; cause < 3; ++cause) {
		struct craft_data *craft = armed_world();
		if (cause == 0) {
			craft->warhead_launcher_count = 0;
		} else if (cause == 1) {
			craft->weapon_slots[FIRST_WARHEAD_SLOT].ammo_count = 0;
			craft->weapon_slots[FIRST_WARHEAD_SLOT + 1].ammo_count =
				0;
		} else {
			craft->cannon_group_count = 1;
			craft->warhead_launcher_count = 0;
		}
		g_players[PILOT].selected_weapon_bank = 0;
		press(PILOT, FLIGHT_KEY_W);
		XVT_ASSERT_INT_EQ(g_players[PILOT].selected_weapon_mode, 0);
		XVT_ASSERT_INT_EQ(g_players[PILOT].selected_weapon_bank,
				  cause == 2 ? 0 : 1);
		press(PILOT, FLIGHT_KEY_W);
		XVT_ASSERT_INT_EQ(g_players[PILOT].selected_weapon_mode, 0);
		XVT_ASSERT_INT_EQ(g_players[PILOT].selected_weapon_bank, 0);
		XVT_ASSERT_INT_EQ(last_message(),
				  IFMSG_003_LASER_CANNONS_ARMED);
	}
}

/* A craft with launchers and no cannon groups starts at the first launcher,
 * steps to the second and comes round to the first, never leaving the
 * launchers. */
static void check_weapon_launchers_only(void)
{
	struct craft_data *craft = armed_world();
	craft->cannon_group_count = 0;
	static const struct {
		int bank;
		int message;
	} steps[] = {
		{0, IFMSG_008_PROTON_TORPEDO_LAUNCHER_ARMED},
		{1, IFMSG_009_CONCUSSION_MISSILE_LAUNCHER_ARMED},
		{0, IFMSG_008_PROTON_TORPEDO_LAUNCHER_ARMED},
	};
	for (size_t k = 0; k < sizeof steps / sizeof steps[0]; ++k) {
		press(PILOT, FLIGHT_KEY_W);
		XVT_ASSERT_INT_EQ(g_players[PILOT].selected_weapon_mode, 1);
		XVT_ASSERT_INT_EQ(g_players[PILOT].selected_weapon_bank,
				  steps[k].bank);
		XVT_ASSERT_INT_EQ(last_message(), steps[k].message);
	}
}

/* Arming a launcher drops the warhead lock built so far, and picks the
 * launcher's first slot to fire from unless it has the mode 3: bit 7 of the
 * launcher's flags is set when the second slot holds more warheads than the
 * first and cleared when it holds fewer, and an equal count leaves it. */
static void check_weapon_launcher_flags(void)
{
	static const struct {
		int8_t flags;
		int first;
		int second;
		int8_t expected;
	} cases[] = {
		{1, 6, 9, (int8_t)0x81},
		{1, 9, 6, 1},
		{1, 6, 6, 1},
		{(int8_t)0x81, 9, 6, 1},
		{(int8_t)0x81, 6, 9, (int8_t)0x81},
		{(int8_t)0x81, 6, 6, (int8_t)0x81},
		{3, 6, 9, 3},
		{(int8_t)0x83, 9, 6, (int8_t)0x83},
	};
	for (size_t k = 0; k < sizeof cases / sizeof cases[0]; ++k) {
		struct craft_data *craft = armed_world();
		craft->warhead_launcher_flags[0] = cases[k].flags;
		craft->weapon_slots[FIRST_WARHEAD_SLOT].ammo_count =
			(uint8_t)cases[k].first;
		craft->weapon_slots[FIRST_WARHEAD_SLOT + 1].ammo_count =
			(uint8_t)cases[k].second;
		craft->warhead_lock_ticks = 77;
		g_players[PILOT].missile_lock_state = 2;
		g_players[PILOT].selected_weapon_bank = 1;
		press(PILOT, FLIGHT_KEY_W);
		XVT_ASSERT_INT_EQ(g_players[PILOT].selected_weapon_mode, 1);
		XVT_ASSERT_INT_EQ(craft->warhead_launcher_flags[0],
				  cases[k].expected);
		XVT_ASSERT_INT_EQ(craft->warhead_lock_ticks, 0);
		XVT_ASSERT_INT_EQ(g_players[PILOT].missile_lock_state, 0);
	}
}

/* When the cannons are out, W names the cannon group it has come to as down;
 * when the launchers are, it names the launchers. */
static void check_weapon_system_out(void)
{
	struct craft_data *craft = armed_world();
	craft->working_subsystems &= ~CRAFT_SUBSYSTEM_FLAG_CANNONS;
	press(PILOT, FLIGHT_KEY_W);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_086_ARG_SYSTEM_IS_ARG);
	XVT_ASSERT_INT_EQ(g_msg_arg_table[0], 93);
	XVT_ASSERT_INT_EQ(g_msg_arg_table[1], 87);
	craft = armed_world();
	craft->working_subsystems &= ~CRAFT_SUBSYSTEM_FLAG_WARHEAD_LAUNCHER;
	g_players[PILOT].selected_weapon_bank = 1;
	press(PILOT, FLIGHT_KEY_W);
	XVT_ASSERT_INT_EQ(g_players[PILOT].selected_weapon_mode, 1);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_086_ARG_SYSTEM_IS_ARG);
	XVT_ASSERT_INT_EQ(g_msg_arg_table[0], 94);
	XVT_ASSERT_INT_EQ(g_msg_arg_table[1], 87);
}

/* ------------------------------------------------------------------------ */
/* X, the fire link. */

/* Steps the cannon group's link mode of a group with slot_count slots, from
 * start: each step gives the mode and the message that says so. */
static void run_link_steps(int bank, int slot_count, int start,
			   const int *modes, size_t step_count)
{
	armed_world();
	struct craft_data *craft = &g_test_craft[PILOT_CRAFT];
	xwing_model()->laser_group_slot_count[bank] = (uint8_t)slot_count;
	xwing_model()->laser_group_first_slot[bank] = (uint8_t)(3 + bank);
	g_players[PILOT].selected_weapon_bank = (uint8_t)bank;
	craft->laser_state.link_mode[bank] = (uint8_t)start;
	for (size_t k = 0; k < step_count; ++k) {
		craft->laser_state.next_slot[bank] = 99;
		press(PILOT, FLIGHT_KEY_X);
		XVT_ASSERT_INT_EQ(craft->laser_state.link_mode[bank], modes[k]);
		XVT_ASSERT_INT_EQ(craft->laser_state.next_slot[bank], 3 + bank);
		XVT_ASSERT_INT_EQ(last_message(), modes[k] + 4);
	}
}

/* X steps the selected cannon group through single, dual and linked fire and
 * back to single, saying which, and starts it from the group's first slot. A
 * group of four slots has all three; any other group has no dual fire. */
static void check_link_cycle(void)
{
	static const int four[] = {2, 3, 1, 2};
	static const int two[] = {3, 1, 3, 1};
	for (int bank = 0; bank < 2; ++bank) {
		run_link_steps(bank, 4, 1, four, 4);
		run_link_steps(bank, 2, 1, two, 4);
		run_link_steps(bank, 3, 2, two + 2, 2);
	}
}

/* A cannon group of one slot has nothing to link: X changes nothing and says
 * nothing. */
static void check_link_single_slot_group(void)
{
	armed_world();
	struct craft_data *craft = &g_test_craft[PILOT_CRAFT];
	xwing_model()->laser_group_slot_count[0] = 1;
	craft->laser_state.link_mode[0] = 1;
	craft->laser_state.next_slot[0] = 99;
	press(PILOT, FLIGHT_KEY_X);
	XVT_ASSERT_INT_EQ(craft->laser_state.link_mode[0], 1);
	XVT_ASSERT_INT_EQ(craft->laser_state.next_slot[0], 99);
	XVT_ASSERT_INT_EQ(g_message_log_total_count, 0);
}

/* Only the player in the local seat is told of a change of link; the change
 * is made for any player. */
static void check_link_message_local_only(void)
{
	armed_world();
	struct craft_data *mate_craft = fly(MATE, MATE_CRAFT, 0);
	xwing_model()->laser_group_slot_count[0] = 4;
	mate_craft->laser_state.link_mode[0] = 1;
	press(MATE, FLIGHT_KEY_X);
	XVT_ASSERT_INT_EQ(mate_craft->laser_state.link_mode[0], 2);
	XVT_ASSERT_INT_EQ(g_message_log_total_count, 0);
}

/* With launchers selected, X flips the selected launcher between single and
 * dual fire, by bit 2 of its flags, and names the launcher's warhead. */
static void check_link_warheads(void)
{
	static const int bank_message[2][2] = {
		{IFMSG_028_PROTON_TORPEDO_LAUNCHER_SET_TO_DUAL_FIRE,
		 IFMSG_018_PROTON_TORPEDO_LAUNCHER_SET_TO_SINGLE_FIRE},
		{IFMSG_029_CONCUSSION_MISSILE_LAUNCHER_SET_TO_DUAL_FIRE,
		 IFMSG_019_CONCUSSION_MISSILE_LAUNCHER_SET_TO_SINGLE_FIRE},
	};
	for (int bank = 0; bank < 2; ++bank) {
		struct craft_data *craft = armed_world();
		g_players[PILOT].selected_weapon_mode = 1;
		g_players[PILOT].selected_weapon_bank = (uint8_t)bank;
		craft->warhead_launcher_flags[0] = 1;
		craft->warhead_launcher_flags[1] = 5;
		press(PILOT, FLIGHT_KEY_X);
		XVT_ASSERT_INT_EQ(craft->warhead_launcher_flags[bank],
				  bank == 0 ? 3 : 7);
		XVT_ASSERT_INT_EQ(craft->warhead_launcher_flags[1 - bank],
				  bank == 0 ? 5 : 1);
		XVT_ASSERT_INT_EQ(last_message(), bank_message[bank][0]);
		press(PILOT, FLIGHT_KEY_X);
		XVT_ASSERT_INT_EQ(craft->warhead_launcher_flags[bank],
				  bank == 0 ? 1 : 5);
		XVT_ASSERT_INT_EQ(last_message(), bank_message[bank][1]);
	}
}

/* ------------------------------------------------------------------------ */
/* Alt+E, the eject key. */

/* The pilot's craft has settings that differ from every default: the throttle
 * at 31000, the recharge levels, the shield distribution forward, link modes
 * 2 and 3, and launcher flags with bits above the low two set. */
static struct craft_data *settled_craft(void)
{
	fresh_world();
	struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
	craft->throttle_speed = 31000;
	craft->laser_recharge_level = POWER_RECHARGE_MAXIMUM;
	craft->shield_recharge_level = POWER_RECHARGE_INCREASED;
	craft->beam_recharge_level = POWER_RECHARGE_FULLY_REDIRECTED_TO_ENGINES;
	craft->shield_distrib_mode = SHIELD_DISTRIBUTION_FULLY_FORWARD;
	craft->laser_state.link_mode[0] = 2;
	craft->laser_state.link_mode[1] = 3;
	craft->warhead_launcher_flags[0] = (int8_t)0x87;
	craft->warhead_launcher_flags[1] = 0x1A;
	return craft;
}

static void assert_settings_saved(const struct craft_data *craft)
{
	const struct player_saved_craft_settings *saved =
		&g_players[PILOT].saved_craft_settings;
	XVT_ASSERT_INT_EQ(saved->throttle_speed, 31000);
	XVT_ASSERT_INT_EQ(saved->laser_recharge_level, POWER_RECHARGE_MAXIMUM);
	XVT_ASSERT_INT_EQ(saved->shield_recharge_level,
			  POWER_RECHARGE_INCREASED);
	XVT_ASSERT_INT_EQ(saved->beam_level,
			  POWER_RECHARGE_FULLY_REDIRECTED_TO_ENGINES);
	XVT_ASSERT_INT_EQ(saved->shield_distrib_mode,
			  SHIELD_DISTRIBUTION_FULLY_FORWARD);
	XVT_ASSERT_INT_EQ(saved->laser_link_mode[0], 2);
	XVT_ASSERT_INT_EQ(saved->laser_link_mode[1], 3);
	XVT_ASSERT_INT_EQ(saved->warhead_launcher_flags[0], 3);
	XVT_ASSERT_INT_EQ(saved->warhead_launcher_flags[1], 2);
	(void)craft;
}

/* Alt+E ejects: it keeps the craft's settings, counts the craft lost for its
 * team, sets it breaking up for 472 ticks with a roll between 0x2000 and 0x5FFF
 * (halved until it is no more than the model's greatest tumble) and leaves the
 * player waiting for a new craft with no camera on one. */
static void check_eject(void)
{
	static const uint16_t limits[] = {0x7FFF, 0x1000, 0x0900};
	for (size_t k = 0; k < sizeof limits / sizeof limits[0]; ++k) {
		struct craft_data *craft = settled_craft();
		xwing_model()->max_tumble_rate = limits[k];
		craft->object_kind = CRAFT_OBJECT_KIND_ACTIVE;
		g_test_mobiles[PILOT_CRAFT].lifetime_timer = 5;
		press(PILOT, FLIGHT_KEY_ALT_E);
		g_game_rand_value_state = 0x1234;
		g_game_rand_feedback_state = 0x5678;
		int tumble = (game_rand() & 0x3FFF) + 0x2000;
		while (tumble > limits[k]) {
			tumble >>= 1;
		}
		XVT_ASSERT_INT_EQ(craft->object_kind,
				  CRAFT_OBJECT_KIND_BREAKING_UP);
		XVT_ASSERT_INT_EQ(g_test_mobiles[PILOT_CRAFT].lifetime_timer,
				  472);
		XVT_ASSERT_INT_EQ(g_test_mobiles[PILOT_CRAFT].roll_impulse_rate,
				  tumble);
		XVT_ASSERT_INT_EQ(
			g_flight_mission_state.runtime.team_kill_stats[3][0],
			1);
		XVT_ASSERT_INT_EQ(g_players[PILOT].awaiting_new_craft, 1);
		XVT_ASSERT_INT_EQ(
			g_players[PILOT].view_state.camera_focus_obj_idx,
			UINT16_MAX);
		assert_settings_saved(craft);
	}
}

/* In a replay the eject is refused with a click, but the craft's settings are
 * kept before the key is refused: the craft is not lost. */
static void check_eject_in_replay(void)
{
	struct craft_data *craft = settled_craft();
	craft->object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	g_test_mobiles[PILOT_CRAFT].lifetime_timer = 5;
	g_replay_view_mode = 1;
	press(PILOT, FLIGHT_KEY_ALT_E);
	g_replay_view_mode = 0;
	XVT_ASSERT_INT_EQ(craft->object_kind, CRAFT_OBJECT_KIND_ACTIVE);
	XVT_ASSERT_INT_EQ(g_test_mobiles[PILOT_CRAFT].lifetime_timer, 5);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.runtime.team_kill_stats[3][0],
			  0);
	XVT_ASSERT_INT_EQ(g_players[PILOT].awaiting_new_craft, 0);
	assert_settings_saved(craft);
}

/* ------------------------------------------------------------------------ */
/* The throttle steps and the view keys. */

enum { HUD_FORWARD = 0, HUD_SNAPPED = 8, HUD_CENTRE = 16, HUD_ONLY = 19 };

/* Plus, equals and keypad plus raise the throttle by 2048, minus and keypad
 * minus lower it by 2048, and neither goes past the ends of the range. */
static void check_throttle_steps(void)
{
	static const struct {
		uint16_t key;
		int step;
	} keys[] = {
		{FLIGHT_KEY_PLUS, 2048},       {FLIGHT_KEY_EQUAL, 2048},
		{FLIGHT_KEY_PAD_PLUS, 2048},   {FLIGHT_KEY_MINUS, -2048},
		{FLIGHT_KEY_PAD_MINUS, -2048},
	};
	static const int starts[] = {30000, 64000, 1000};
	for (size_t k = 0; k < sizeof keys / sizeof keys[0]; ++k) {
		for (size_t j = 0; j < sizeof starts / sizeof starts[0]; ++j) {
			fresh_world();
			struct craft_data *craft = fly(PILOT, PILOT_CRAFT, 0);
			craft->throttle_speed = (uint16_t)starts[j];
			press(PILOT, keys[k].key);
			int expected = starts[j] + keys[k].step;
			expected = expected < 0 ? 0 : expected;
			expected = expected > 65535 ? 65535 : expected;
			XVT_ASSERT_INT_EQ(craft->throttle_speed, expected);
		}
	}
}

/* The pilot flies in the cockpit: the camera follows the pilot's craft, the
 * external camera is off and the HUD shows view 0. The saved view is the
 * HUD only view. The cockpit has no pictures, so a request for a view is
 * refused and logged with the view asked for. */
static struct player_view_state *cockpit_world(void)
{
	fresh_world();
	fly(PILOT, PILOT_CRAFT, 0);
	fly(MATE, MATE_CRAFT, 1);
	struct player_view_state *view = &g_players[PILOT].view_state;
	view->camera_focus_obj_idx = PILOT_CRAFT;
	view->external_camera_active = 0;
	view->hud_state_live = HUD_FORWARD;
	view->hud_aim_x_snap_state = 0;
	view->hud_aim_x = 1234;
	view->hud_aim_y = -4321;
	g_players[PILOT].saved_hud_view_state = HUD_ONLY;
	return view;
}

/* The keypad look keys turn the view to the direction the key lies in on the
 * keypad (7 8 9 over 4 5 6 over 1 2 3, 8 straight ahead): the aim turns by
 * the direction's angle, 65,536 to a circle, the HUD shows the direction's
 * view added to the keypad 0 snap of 0 or 8, and straight ahead returns to the
 * player's saved view. */
static void check_look_keys(void)
{
	static const struct {
		uint16_t key;
		int view;
		int angle;
	} keys[] = {
		{FLIGHT_KEY_PAD_1, 3, -0x6000}, {FLIGHT_KEY_PAD_2, 4, -0x8000},
		{FLIGHT_KEY_PAD_3, 5, 0x6000},	{FLIGHT_KEY_PAD_4, 2, -0x4000},
		{FLIGHT_KEY_PAD_6, 6, 0x4000},	{FLIGHT_KEY_PAD_7, 1, -0x2000},
		{FLIGHT_KEY_PAD_8, 0, 0},	{FLIGHT_KEY_PAD_9, 7, 0x2000},
	};
	for (size_t k = 0; k < sizeof keys / sizeof keys[0]; ++k) {
		for (int snap = 0; snap <= HUD_SNAPPED; snap += HUD_SNAPPED) {
			struct player_view_state *view = cockpit_world();
			view->hud_aim_x_snap_state = (uint8_t)snap;
			press(PILOT, keys[k].key);
			int shown = snap + keys[k].view;
			XVT_ASSERT_TRUE(
				logged("hud.view_refused slot=0 view=%d ",
				       shown != 0 ? shown : HUD_ONLY));
			XVT_ASSERT_INT_EQ(view->hud_aim_x, snap << 10);
			XVT_ASSERT_INT_EQ(view->hud_aim_y,
					  (int16_t)keys[k].angle);
		}
	}
}

/* The keys turn the aim but ask for no new view when the camera is on
 * another object or outside the cockpit. */
static void check_look_keys_outside_cockpit(void)
{
	for (int cause = 0; cause < 2; ++cause) {
		struct player_view_state *view = cockpit_world();
		if (cause == 0) {
			view->camera_focus_obj_idx = MATE_CRAFT;
		} else {
			view->external_camera_active = 1;
		}
		press(PILOT, FLIGHT_KEY_PAD_6);
		XVT_ASSERT_TRUE(!logged("hud.view_refused"));
		XVT_ASSERT_INT_EQ(view->hud_aim_x, 0);
		XVT_ASSERT_INT_EQ(view->hud_aim_y, 0x4000);
		press(PILOT, FLIGHT_KEY_PAD_5);
		XVT_ASSERT_TRUE(!logged("hud.view_refused"));
		XVT_ASSERT_INT_EQ(view->hud_aim_x, 0x4000);
		XVT_ASSERT_INT_EQ(view->hud_aim_y, 0);
	}
}

/* Keypad 5 turns the aim a quarter circle in x and none in y, and asks for
 * view 16. */
static void check_look_centre_key(void)
{
	struct player_view_state *view = cockpit_world();
	press(PILOT, FLIGHT_KEY_PAD_5);
	XVT_ASSERT_INT_EQ(view->hud_aim_x, 0x4000);
	XVT_ASSERT_INT_EQ(view->hud_aim_y, 0);
	XVT_ASSERT_TRUE(logged("hud.view_refused slot=0 view=%d ", HUD_CENTRE));
}

/* Keypad 0 flips the snap between 0 and 8 and puts the aim at the snap
 * shifted left 10, asking for the live view with 8 flipped. It does nothing
 * from a view of 16 or more, unless the external camera is on, when it flips
 * the snap and asks for nothing. */
static void check_look_snap_key(void)
{
	struct player_view_state *view = cockpit_world();
	press(PILOT, FLIGHT_KEY_PAD_0);
	XVT_ASSERT_INT_EQ(view->hud_aim_x_snap_state, 8);
	XVT_ASSERT_INT_EQ(view->hud_aim_x, 8 << 10);
	XVT_ASSERT_TRUE(logged("hud.view_refused slot=0 view=%d ", 8));
	press(PILOT, FLIGHT_KEY_PAD_0);
	XVT_ASSERT_INT_EQ(view->hud_aim_x_snap_state, 0);
	XVT_ASSERT_INT_EQ(view->hud_aim_x, 0);

	view = cockpit_world();
	view->hud_state_live = 11;
	press(PILOT, FLIGHT_KEY_PAD_0);
	XVT_ASSERT_TRUE(logged("hud.view_refused slot=0 view=%d ", 3));

	view = cockpit_world();
	view->hud_state_live = HUD_CENTRE;
	press(PILOT, FLIGHT_KEY_PAD_0);
	XVT_ASSERT_INT_EQ(view->hud_aim_x_snap_state, 0);
	XVT_ASSERT_INT_EQ(view->hud_aim_x, 1234);
	XVT_ASSERT_TRUE(!logged("hud.view_refused"));

	view = cockpit_world();
	view->hud_state_live = HUD_CENTRE;
	view->external_camera_active = 1;
	press(PILOT, FLIGHT_KEY_PAD_0);
	XVT_ASSERT_INT_EQ(view->hud_aim_x_snap_state, 8);
	XVT_ASSERT_INT_EQ(view->hud_aim_x, 8 << 10);
	XVT_ASSERT_TRUE(!logged("hud.view_refused"));
}

/* The period key (and keypad period) toggles between the HUD only view and
 * the cockpit view: it remembers the one it asks for as the player's saved
 * view, and centres the aim. Outside the cockpit it only centres the aim. */
static void check_period_toggles_hud_only(void)
{
	static const uint16_t keys[] = {FLIGHT_KEY_PERIOD, FLIGHT_KEY_PAD_DOT};
	for (size_t k = 0; k < sizeof keys / sizeof keys[0]; ++k) {
		struct player_view_state *view = cockpit_world();
		g_players[PILOT].saved_hud_view_state = 5;
		press(PILOT, keys[k]);
		XVT_ASSERT_INT_EQ(g_players[PILOT].saved_hud_view_state,
				  HUD_ONLY);
		XVT_ASSERT_TRUE(
			logged("hud.view_refused slot=0 view=%d ", HUD_ONLY));
		XVT_ASSERT_INT_EQ(view->hud_aim_x, 0);
		XVT_ASSERT_INT_EQ(view->hud_aim_y, 0);

		view = cockpit_world();
		view->hud_state_live = HUD_ONLY;
		g_players[PILOT].saved_hud_view_state = 5;
		press(PILOT, keys[k]);
		XVT_ASSERT_INT_EQ(g_players[PILOT].saved_hud_view_state,
				  HUD_FORWARD);
		XVT_ASSERT_TRUE(logged("hud.view_refused slot=0 view=%d ",
				       HUD_FORWARD));

		view = cockpit_world();
		view->external_camera_active = 1;
		g_players[PILOT].saved_hud_view_state = 5;
		press(PILOT, keys[k]);
		XVT_ASSERT_INT_EQ(g_players[PILOT].saved_hud_view_state, 5);
		XVT_ASSERT_TRUE(!logged("hud.view_refused"));
		XVT_ASSERT_INT_EQ(view->hud_aim_x, 0);
		XVT_ASSERT_INT_EQ(view->hud_aim_y, 0);
	}
}

/* Z turns the target camera on: it keeps the cockpit view and aim to come
 * back to, follows the target from outside the cockpit and asks for the
 * target camera view. With no target it says so and changes nothing. Z again
 * brings the camera back to the pilot's craft, asking for the saved view. */
static void check_target_camera(void)
{
	struct player_view_state *view = cockpit_world();
	view->hud_state_live = 7;
	view->hud_aim_x = 111;
	view->hud_aim_y = 222;
	press(PILOT, FLIGHT_KEY_Z);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_223_NO_CRAFT_TARGETED);
	XVT_ASSERT_INT_EQ(view->target_camera_active, 0);
	XVT_ASSERT_INT_EQ(view->external_camera_active, 0);
	XVT_ASSERT_INT_EQ(view->camera_focus_obj_idx, PILOT_CRAFT);

	g_players[PILOT].current_target_object_idx = MATE_CRAFT;
	press(PILOT, FLIGHT_KEY_Z);
	XVT_ASSERT_INT_EQ(view->target_camera_active, 1);
	XVT_ASSERT_INT_EQ(view->external_camera_active, 1);
	XVT_ASSERT_INT_EQ(view->camera_focus_obj_idx, MATE_CRAFT);
	XVT_ASSERT_INT_EQ(view->saved_hud_state_byte, 7);
	XVT_ASSERT_INT_EQ(view->saved_hud_aim_x, 111);
	XVT_ASSERT_INT_EQ(view->saved_hud_aim_y, 222);
	XVT_ASSERT_INT_EQ(g_render_object_ref_flags, 1024);
	XVT_ASSERT_TRUE(logged("hud.view_refused slot=0 view=%d ", 20));

	view->player_input_blocked = 1;
	g_hud_cached_target_object_idx = 5;
	press(PILOT, FLIGHT_KEY_Z);
	XVT_ASSERT_INT_EQ(view->target_camera_active, 0);
	XVT_ASSERT_INT_EQ(view->external_camera_active, 0);
	XVT_ASSERT_INT_EQ(view->camera_focus_obj_idx, PILOT_CRAFT);
	XVT_ASSERT_INT_EQ(view->player_input_blocked, 0);
	XVT_ASSERT_INT_EQ(g_hud_cached_target_object_idx, -2);
	XVT_ASSERT_INT_EQ(g_render_object_ref_flags, 0);
	XVT_ASSERT_INT_EQ(view->hud_aim_x, 0);
	XVT_ASSERT_INT_EQ(view->hud_aim_y, 0);
	XVT_ASSERT_TRUE(logged("hud.view_refused slot=0 view=%d ", HUD_ONLY));

	g_players[PILOT].saved_hud_view_state = 5;
	view->target_camera_active = 1;
	press(PILOT, FLIGHT_KEY_Z);
	XVT_ASSERT_TRUE(
		logged("hud.view_refused slot=0 view=%d ", HUD_FORWARD));
}

/* ---- Wingman orders: Shift+A, I, W, H, G and the resupply call Shift+B --- */

enum {
	WING = 4,	 /* A computer craft in the pilot's own flight group. */
	WING2 = 8,	 /* A second one. */
	ENEMY = 5,	 /* A computer craft of an enemy team. */
	OTHER_CRAFT = 6, /* A player of another team. */
	SUPPLY = 7,	 /* A computer supply craft in its own group. */
	FAR = 20,	 /* Past the craft slots. */
};

/* Plans by id; the names are what the order keys look at. */
enum {
	P_NONE,
	P_NULL,
	P_WAIT,
	P_STARWAIT,
	P_HOME,
	P_STARHOME,
	P_HYPER,
	P_OUTHYPER,
	P_WORK,
	P_BOARD,
	P_BOARD3,
	P_SAVED,
	P_STILL,
	P_HANGAR,
	P_FORM,
	P_COUNT,
};

static const char *const g_plan_names[P_COUNT] = {
	"",
	"nullpln",
	"craftwaitforgopln",
	"starshipwaitforgopln",
	"flyhomeevadepln",
	"starshipintohyperpln",
	"intohyperspacepln",
	"outofhyperspacepln",
	"attackpln",
	"boardtogivepln",
	"board3pln",
	"patrolpln",
	"stationaryldrpln",
	"enterhangarpln",
	"formldr1pln",
};

/* What a case changes in the world before the key is pressed. */
enum {
	F_COMMS_OUT = 1,	 /* The presser's radio is out. */
	F_MAP = 2,		 /* The presser is in the map view. */
	F_STARSHIP = 4,		 /* The commanded craft is a starship. */
	F_TWO_WINGS = 8,	 /* A second wingman. */
	F_ABORTED = 16,		 /* The commanded craft is already aborted. */
	F_SPECIAL = 32,		 /* It carries its group's special cargo. */
	F_WING_DEAD = 64,	 /* It has no working systems. */
	F_OTHER_BUSY = 128,	 /* The other player has a request pending. */
	F_OTHER_AWAY = 256,	 /* The other player is not in flight. */
	F_OTHER_NORADIO = 512,	 /* The other player's radio is out. */
	F_OTHER_FOREIGN = 1024,	 /* The other player is on another team. */
	F_OTHER_NOBODY = 2048,	 /* The other player has no craft. */
	F_OTHER_ISTARGET = 4096, /* The other player flies the target. */
	F_OTHER_ELSE = 8192,	 /* The other player targets something else. */
	F_AVOIDING = 16384,	 /* The presser already avoids the target. */
};

/* The order of the supply craft names the pilot's group in different ways. */
enum {
	MATCH_FIRST,
	MATCH_NONE,
	MATCH_AND_ONE,
	MATCH_AND_BOTH,
	MATCH_SECOND_PAIR,
};

struct order_case {
	const char *name;
	const char *const *lines;
	uint16_t key;
	int presser; /* PILOT or MATE; the other one is the second player. */
	int target;  /* Object slot the presser has targeted, or -1. */
	int plan;    /* Plan the commanded craft is running. */
	int flags;
	int match;
};

static void install_plans(void)
{
	static uint8_t plan_bytes[4] = {0xFF, 0xFF, 0xFF, 0xFF};
	memset(g_plan_table, 0, sizeof g_plan_table);
	for (int id = 0; id < P_COUNT; ++id) {
		strcpy(g_plan_table[id].name, g_plan_names[id]);
		g_plan_data_ptrs[id] = plan_bytes;
	}
}

static void computer_craft(int obj, int group, int team, int plan)
{
	g_test_objects[obj].object_type = TEST_XWING;
	g_test_objects[obj].genus_id = 1;
	g_test_objects[obj].flight_group_idx = (uint8_t)group;
	g_test_objects[obj].world_x = 5000 + 300 * obj;
	g_test_objects[obj].world_y = 7000 - 200 * obj;
	g_test_objects[obj].world_z = 3000 + 100 * obj;
	g_test_mobiles[obj].team = (uint8_t)team;
	g_test_mobiles[obj].iff = (uint8_t)team;
	g_mission_flight_groups[group].fg.team = (uint8_t)team;
	g_mission_flight_groups[group].fg.iff = (uint8_t)team;
	struct craft_data *craft = &g_test_craft[obj];
	craft->model_index = (uint8_t)get_model_index_from_type(TEST_XWING);
	craft->working_subsystems = 0x03FF;
	craft->craft_ordinal = 1;
	craft->leader_obj_idx = 0xFF;
	craft->player_command_avoid_target_obj_idx = UINT16_MAX;
	craft->ai_controller.candidate_target_idx = UINT16_MAX;
	craft->ai_controller.running_plan_id = (uint8_t)plan;
	craft->ai_controller.saved_plan_id = P_SAVED;
}

/* The pilot (player 0, the local seat) and a mate (player 1) on team 0 in
 * flight group 0 with a computer wingman; a player of team 1; an enemy
 * craft; and a supply craft whose order slot 0 names the pilot's group as
 * the case says. The key's presser has targeted the case's target. */
static void order_world(const struct order_case *c)
{
	int flags = c->flags;
	int other = c->presser == PILOT ? MATE : PILOT;
	fresh_world();
	install_plans();
	fly(PILOT, PILOT_CRAFT, 0);
	fly(MATE, MATE_CRAFT, 0);
	fly(2, OTHER_CRAFT, 1);
	g_active_flight_player_count = 3;
	g_mission_flight_groups[0].fg.player_number = 1;
	memset(&g_mission_fg_stats[0], 0, sizeof g_mission_fg_stats[0]);
	computer_craft(WING, 0, 0, c->plan);
	if (flags & F_TWO_WINGS) {
		computer_craft(WING2, 0, 0, c->plan);
	}
	if (flags & F_STARSHIP) {
		g_test_objects[WING].genus_id = CRAFT_GENUS_STARSHIP;
	}
	if (flags & F_WING_DEAD) {
		g_test_craft[WING].working_subsystems = 0;
	}
	if (flags & F_ABORTED) {
		g_test_craft[WING].ai_flight.mission_aborted_flag = 1;
	}
	if (flags & F_SPECIAL) {
		g_mission_flight_groups[0].fg.special_cargo_craft = 1;
	}
	computer_craft(ENEMY, 2, 2, P_WORK);
	computer_craft(SUPPLY, 3, 0, c->plan);
	struct mission_order *order = &g_mission_flight_groups[3].fg.orders[0];
	order->target1_type = 1;
	order->target1 =
		c->match == MATCH_NONE || c->match == MATCH_SECOND_PAIR ? 2 : 0;
	order->target1_or_target2 =
		c->match <= MATCH_NONE || c->match == MATCH_SECOND_PAIR;
	if (c->match == MATCH_AND_BOTH) {
		order->target2_type = 1;
	}
	if (c->match == MATCH_SECOND_PAIR) {
		order->secondary_target_types[0] = 1;
		order->target3_or_target4 = 1;
	}
	struct craft_data *presser =
		&g_test_craft[c->presser == PILOT ? PILOT_CRAFT : MATE_CRAFT];
	if (flags & F_COMMS_OUT) {
		presser->working_subsystems &=
			~CRAFT_SUBSYSTEM_FLAG_COMMUNICATIONS;
	}
	if (flags & F_MAP) {
		g_players[c->presser].map_camera_state = 1;
	}
	if (flags & F_AVOIDING) {
		presser->player_command_avoid_target_obj_idx =
			(uint16_t)c->target;
	}
	g_players[c->presser].current_target_object_idx = (int16_t)c->target;
	g_players[other].current_target_object_idx =
		flags & F_OTHER_ELSE ? -1 : (int16_t)c->target;
	if (flags & F_OTHER_BUSY) {
		g_players[other].pending_action_id = 3;
	}
	if (flags & F_OTHER_AWAY) {
		g_players[other].participation_state = 0;
	}
	if (flags & F_OTHER_NORADIO) {
		g_test_craft[other == PILOT ? PILOT_CRAFT : MATE_CRAFT]
			.working_subsystems = 0;
	}
	if (flags & F_OTHER_FOREIGN) {
		g_players[other].team = 1;
	}
	if (flags & F_OTHER_NOBODY) {
		g_players[other].object_index = -1;
	}
	if (flags & F_OTHER_ISTARGET) {
		g_test_objects[g_players[other].object_index].flight_group_idx =
			2;
	}
}

static void run_order_case(const struct order_case *c)
{
	order_world(c);
	scene_open();
	press(c->presser, c->key);
	scene_close(c->name, c->lines);
}

static const char *const k_gate_a[] = {
	/* recorded gate_a */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	"msg.message_built message=86 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=86 type=1 length=7 args=\"101,87,150,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_gate_i[] = {
	/* recorded gate_i */
	"flight.action_key slot=0 key=73 object=2 map=0 hyper=0 request=0 predicted=0",
	"msg.message_built message=86 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=86 type=1 length=7 args=\"101,87,150,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_gate_w[] = {
	/* recorded gate_w */
	"flight.action_key slot=0 key=87 object=2 map=0 hyper=0 request=0 predicted=0",
	"msg.message_built message=86 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=86 type=1 length=7 args=\"101,87,150,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_gate_h[] = {
	/* recorded gate_h */
	"flight.action_key slot=0 key=72 object=2 map=0 hyper=0 request=0 predicted=0",
	"msg.message_built message=86 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=86 type=1 length=7 args=\"101,87,150,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_gate_g[] = {
	/* recorded gate_g */
	"flight.action_key slot=0 key=71 object=2 map=0 hyper=0 request=0 predicted=0",
	"msg.message_built message=86 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=86 type=1 length=7 args=\"101,87,150,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_gate_map_w[] = {
	/* recorded gate_map_w */
	"flight.action_key slot=0 key=87 object=2 map=1 hyper=0 request=0 predicted=0",
	"ai.plan_started object=4 fg=0 plan=2 target_code=255 target=0 x=0 y=0 z=0 maneuver=0 tier=0 timer=0 same=1 predicted=0",
	"flight.craft_ordered slot=0 command=\"wait\" target=4 plan=2 saved=8 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,152,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_gate_map_a[] = {
	/* recorded gate_map_a */
	"flight.action_key slot=0 key=65 object=2 map=1 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"attack\" target=5 predicted=0",
	"player.order_passed slot=0 command=154 target=5 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,154,0\" waiting=1 tick=0",
	"flight.request_posted slot=1 request=1 by=0 param=5 timer=1416 predicted=0",
	NULL,
};

static const struct order_case g_gate_cases[] = {
	{"gate_a", k_gate_a, FLIGHT_KEY_SHIFT_A, PILOT, ENEMY, P_WORK,
	 F_COMMS_OUT, 0},
	{"gate_i", k_gate_i, FLIGHT_KEY_SHIFT_I, PILOT, ENEMY, P_WORK,
	 F_COMMS_OUT, 0},
	{"gate_w", k_gate_w, FLIGHT_KEY_SHIFT_W, PILOT, WING, P_WORK,
	 F_COMMS_OUT, 0},
	{"gate_h", k_gate_h, FLIGHT_KEY_SHIFT_H, PILOT, WING, P_WORK,
	 F_COMMS_OUT, 0},
	{"gate_g", k_gate_g, FLIGHT_KEY_SHIFT_G, PILOT, WING, P_WAIT,
	 F_COMMS_OUT, 0},
	{"gate_map_w", k_gate_map_w, FLIGHT_KEY_SHIFT_W, PILOT, WING, P_WORK,
	 F_COMMS_OUT | F_MAP, 0},
	{"gate_map_a", k_gate_map_a, FLIGHT_KEY_SHIFT_A, PILOT, ENEMY, P_WORK,
	 F_COMMS_OUT | F_MAP, 0},
};

/* With the communications system out, each order key says the system is down
 * and does nothing else. In the map view the radio is not asked, so the order
 * goes out anyway. */
static void check_order_radio_gate(void)
{
	for (size_t k = 0; k < sizeof g_gate_cases / sizeof g_gate_cases[0];
	     ++k) {
		run_order_case(&g_gate_cases[k]);
	}
}

static const char *const k_attack_no_target[] = {
	/* recorded attack_no_target */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_attack_hostile[] = {
	/* recorded attack_hostile */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"attack\" target=5 predicted=0",
	"player.order_passed slot=0 command=154 target=5 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,154,0\" waiting=1 tick=0",
	"flight.request_posted slot=1 request=1 by=0 param=5 timer=1416 predicted=0",
	NULL,
};
static const char *const k_attack_two_wings[] = {
	/* recorded attack_two_wings */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"attack\" target=5 predicted=0",
	"player.order_passed slot=0 command=154 target=5 wingmen=2 last=8 predicted=0",
	"msg.message_built message=269 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=269 type=1 length=7 args=\"32768,154,154,0\" waiting=1 tick=0",
	"flight.request_posted slot=1 request=1 by=0 param=5 timer=1416 predicted=0",
	NULL,
};
static const char *const k_attack_friendly[] = {
	/* recorded attack_friendly */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"attack\" target=4 predicted=0",
	"player.order_ignored slot=0 command=154 target=4 predicted=0",
	"flight.request_posted slot=1 request=1 by=0 param=4 timer=1416 predicted=0",
	NULL,
};
static const char *const k_attack_skips_still[] = {
	/* recorded attack_skips_still */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"attack\" target=5 predicted=0",
	"player.order_passed slot=0 command=154 target=5 wingmen=0 last=-1 predicted=0",
	"flight.request_posted slot=1 request=1 by=0 param=5 timer=1416 predicted=0",
	NULL,
};
static const char *const k_attack_skips_hangar[] = {
	/* recorded attack_skips_hangar */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"attack\" target=5 predicted=0",
	"player.order_passed slot=0 command=154 target=5 wingmen=0 last=-1 predicted=0",
	"flight.request_posted slot=1 request=1 by=0 param=5 timer=1416 predicted=0",
	NULL,
};
static const char *const k_attack_skips_null[] = {
	/* recorded attack_skips_null */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"attack\" target=5 predicted=0",
	"player.order_passed slot=0 command=154 target=5 wingmen=0 last=-1 predicted=0",
	"flight.request_posted slot=1 request=1 by=0 param=5 timer=1416 predicted=0",
	NULL,
};
static const char *const k_attack_skips_form[] = {
	/* recorded attack_skips_form */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"attack\" target=5 predicted=0",
	"player.order_passed slot=0 command=154 target=5 wingmen=0 last=-1 predicted=0",
	"flight.request_posted slot=1 request=1 by=0 param=5 timer=1416 predicted=0",
	NULL,
};
static const char *const k_attack_skips_home[] = {
	/* recorded attack_skips_home */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"attack\" target=5 predicted=0",
	"player.order_passed slot=0 command=154 target=5 wingmen=0 last=-1 predicted=0",
	"flight.request_posted slot=1 request=1 by=0 param=5 timer=1416 predicted=0",
	NULL,
};
static const char *const k_attack_skips_hyper[] = {
	/* recorded attack_skips_hyper */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"attack\" target=5 predicted=0",
	"player.order_passed slot=0 command=154 target=5 wingmen=0 last=-1 predicted=0",
	"flight.request_posted slot=1 request=1 by=0 param=5 timer=1416 predicted=0",
	NULL,
};
static const char *const k_attack_releases[] = {
	/* recorded attack_releases */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"attack\" target=5 predicted=0",
	"ai.plan_started object=4 fg=0 plan=11 target_code=255 target=0 x=0 y=0 z=0 maneuver=0 tier=0 timer=0 same=1 predicted=0",
	"player.wingman_released slot=0 object=4 plan=11 predicted=0",
	"player.order_passed slot=0 command=154 target=5 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,154,0\" waiting=1 tick=0",
	"flight.request_posted slot=1 request=1 by=0 param=5 timer=1416 predicted=0",
	NULL,
};
static const char *const k_attack_clears_avoid[] = {
	/* recorded attack_clears_avoid */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"attack\" target=5 predicted=0",
	"player.order_passed slot=0 command=154 target=5 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,154,0\" waiting=1 tick=0",
	"flight.request_posted slot=1 request=1 by=0 param=5 timer=1416 predicted=0",
	NULL,
};
static const char *const k_attack_mate_busy[] = {
	/* recorded attack_mate_busy */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"attack\" target=5 predicted=0",
	"player.order_passed slot=0 command=154 target=5 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,154,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_attack_mate_away[] = {
	/* recorded attack_mate_away */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"attack\" target=5 predicted=0",
	"player.order_passed slot=0 command=154 target=5 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,154,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_attack_mate_no_radio[] = {
	/* recorded attack_mate_no_radio */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"attack\" target=5 predicted=0",
	"player.order_passed slot=0 command=154 target=5 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,154,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_attack_mate_foreign[] = {
	/* recorded attack_mate_foreign */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"attack\" target=5 predicted=0",
	"player.order_passed slot=0 command=154 target=5 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,154,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_attack_mate_no_craft[] = {
	/* recorded attack_mate_no_craft */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"attack\" target=5 predicted=0",
	"player.order_passed slot=0 command=154 target=5 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,154,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_attack_mate_is_target[] = {
	/* recorded attack_mate_is_target */
	"flight.action_key slot=0 key=65 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"attack\" target=3 predicted=0",
	"player.order_passed slot=0 command=154 target=3 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,154,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_attack_asks_local[] = {
	/* recorded attack_asks_local */
	"flight.action_key slot=1 key=65 object=3 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=1 command=\"attack\" target=5 predicted=0",
	"player.order_passed slot=1 command=154 target=5 wingmen=1 last=4 predicted=0",
	"flight.request_posted slot=0 request=1 by=1 param=5 timer=1416 predicted=0",
	"network.player_name_missing slot=1 players=0",
	"msg.message_built message=270 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=270 type=1 length=7 args=\"32768,32769,154,0\" waiting=1 tick=0",
	NULL,
};

static const struct order_case g_attack_cases[] = {
	{"attack_no_target", k_attack_no_target, FLIGHT_KEY_SHIFT_A, PILOT, -1,
	 P_WORK, 0, 0},
	{"attack_hostile", k_attack_hostile, FLIGHT_KEY_SHIFT_A, PILOT, ENEMY,
	 P_WORK, 0, 0},
	{"attack_two_wings", k_attack_two_wings, FLIGHT_KEY_SHIFT_A, PILOT,
	 ENEMY, P_WORK, F_TWO_WINGS, 0},
	{"attack_friendly", k_attack_friendly, FLIGHT_KEY_SHIFT_A, PILOT, WING,
	 P_WORK, 0, 0},
	{"attack_skips_still", k_attack_skips_still, FLIGHT_KEY_SHIFT_A, PILOT,
	 ENEMY, P_STILL, 0, 0},
	{"attack_skips_hangar", k_attack_skips_hangar, FLIGHT_KEY_SHIFT_A,
	 PILOT, ENEMY, P_HANGAR, 0, 0},
	{"attack_skips_null", k_attack_skips_null, FLIGHT_KEY_SHIFT_A, PILOT,
	 ENEMY, P_NULL, 0, 0},
	{"attack_skips_form", k_attack_skips_form, FLIGHT_KEY_SHIFT_A, PILOT,
	 ENEMY, P_FORM, 0, 0},
	{"attack_skips_home", k_attack_skips_home, FLIGHT_KEY_SHIFT_A, PILOT,
	 ENEMY, P_HOME, 0, 0},
	{"attack_skips_hyper", k_attack_skips_hyper, FLIGHT_KEY_SHIFT_A, PILOT,
	 ENEMY, P_HYPER, 0, 0},
	{"attack_releases", k_attack_releases, FLIGHT_KEY_SHIFT_A, PILOT, ENEMY,
	 P_WAIT, 0, 0},
	{"attack_clears_avoid", k_attack_clears_avoid, FLIGHT_KEY_SHIFT_A,
	 PILOT, ENEMY, P_WORK, F_AVOIDING, 0},
	{"attack_mate_busy", k_attack_mate_busy, FLIGHT_KEY_SHIFT_A, PILOT,
	 ENEMY, P_WORK, F_OTHER_BUSY, 0},
	{"attack_mate_away", k_attack_mate_away, FLIGHT_KEY_SHIFT_A, PILOT,
	 ENEMY, P_WORK, F_OTHER_AWAY, 0},
	{"attack_mate_no_radio", k_attack_mate_no_radio, FLIGHT_KEY_SHIFT_A,
	 PILOT, ENEMY, P_WORK, F_OTHER_NORADIO, 0},
	{"attack_mate_foreign", k_attack_mate_foreign, FLIGHT_KEY_SHIFT_A,
	 PILOT, ENEMY, P_WORK, F_OTHER_FOREIGN, 0},
	{"attack_mate_no_craft", k_attack_mate_no_craft, FLIGHT_KEY_SHIFT_A,
	 PILOT, ENEMY, P_WORK, F_OTHER_NOBODY, 0},
	{"attack_mate_is_target", k_attack_mate_is_target, FLIGHT_KEY_SHIFT_A,
	 PILOT, MATE_CRAFT, P_WORK, F_OTHER_ISTARGET, 0},
	{"attack_asks_local", k_attack_asks_local, FLIGHT_KEY_SHIFT_A, MATE,
	 ENEMY, P_WORK, 0, 0},
};

/* Shift+A sends the pilot's wingmen after the targeted enemy and asks the
 * teammate players to attack it. A friendly target is ignored, wingmen on
 * still, formation, home, hyperspace or hangar plans are left alone, a waiting
 * wingman is let go first, and a teammate who is busy, away, has no radio, is
 * on another team, has no craft or flies the target is not asked. A teammate
 * at the local seat is told with a message. */
static void check_order_attack(void)
{
	for (size_t k = 0; k < sizeof g_attack_cases / sizeof g_attack_cases[0];
	     ++k) {
		run_order_case(&g_attack_cases[k]);
	}
}

static const char *const k_ignore_no_target[] = {
	/* recorded ignore_no_target */
	"flight.action_key slot=0 key=73 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_ignore_hostile[] = {
	/* recorded ignore_hostile */
	"flight.action_key slot=0 key=73 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"ignore\" target=5 predicted=0",
	"player.order_passed slot=0 command=155 target=5 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,155,0\" waiting=1 tick=0",
	"flight.request_posted slot=1 request=4 by=0 param=5 timer=1416 predicted=0",
	NULL,
};
static const char *const k_ignore_two_wings[] = {
	/* recorded ignore_two_wings */
	"flight.action_key slot=0 key=73 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"ignore\" target=5 predicted=0",
	"player.order_passed slot=0 command=155 target=5 wingmen=2 last=8 predicted=0",
	"msg.message_built message=269 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=269 type=1 length=7 args=\"32768,155,155,0\" waiting=1 tick=0",
	"flight.request_posted slot=1 request=4 by=0 param=5 timer=1416 predicted=0",
	NULL,
};
static const char *const k_ignore_friendly[] = {
	/* recorded ignore_friendly */
	"flight.action_key slot=0 key=73 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"ignore\" target=4 predicted=0",
	"player.order_ignored slot=0 command=155 target=4 predicted=0",
	"flight.request_posted slot=1 request=4 by=0 param=4 timer=1416 predicted=0",
	NULL,
};
static const char *const k_ignore_waiting[] = {
	/* recorded ignore_waiting */
	"flight.action_key slot=0 key=73 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"ignore\" target=5 predicted=0",
	"player.order_passed slot=0 command=155 target=5 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,155,0\" waiting=1 tick=0",
	"flight.request_posted slot=1 request=4 by=0 param=5 timer=1416 predicted=0",
	NULL,
};
static const char *const k_ignore_mate_elsewhere[] = {
	/* recorded ignore_mate_elsewhere */
	"flight.action_key slot=0 key=73 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"ignore\" target=5 predicted=0",
	"player.order_passed slot=0 command=155 target=5 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,155,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_ignore_mate_busy[] = {
	/* recorded ignore_mate_busy */
	"flight.action_key slot=0 key=73 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"ignore\" target=5 predicted=0",
	"player.order_passed slot=0 command=155 target=5 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,155,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_ignore_mate_away[] = {
	/* recorded ignore_mate_away */
	"flight.action_key slot=0 key=73 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"ignore\" target=5 predicted=0",
	"player.order_passed slot=0 command=155 target=5 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,155,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_ignore_mate_no_radio[] = {
	/* recorded ignore_mate_no_radio */
	"flight.action_key slot=0 key=73 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"ignore\" target=5 predicted=0",
	"player.order_passed slot=0 command=155 target=5 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,155,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_ignore_mate_foreign[] = {
	/* recorded ignore_mate_foreign */
	"flight.action_key slot=0 key=73 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"ignore\" target=5 predicted=0",
	"player.order_passed slot=0 command=155 target=5 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,155,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_ignore_mate_no_craft[] = {
	/* recorded ignore_mate_no_craft */
	"flight.action_key slot=0 key=73 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"ignore\" target=5 predicted=0",
	"player.order_passed slot=0 command=155 target=5 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,155,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_ignore_mate_is_target[] = {
	/* recorded ignore_mate_is_target */
	"flight.action_key slot=0 key=73 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=0 command=\"ignore\" target=3 predicted=0",
	"player.order_passed slot=0 command=155 target=3 wingmen=1 last=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,155,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_ignore_asks_local[] = {
	/* recorded ignore_asks_local */
	"flight.action_key slot=1 key=73 object=3 map=0 hyper=0 request=0 predicted=0",
	"flight.order_given slot=1 command=\"ignore\" target=5 predicted=0",
	"player.order_passed slot=1 command=155 target=5 wingmen=1 last=4 predicted=0",
	"flight.request_posted slot=0 request=4 by=1 param=5 timer=1416 predicted=0",
	"network.player_name_missing slot=1 players=0",
	"msg.message_built message=271 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=271 type=1 length=7 args=\"32768,32769,155,0\" waiting=1 tick=0",
	NULL,
};

static const struct order_case g_ignore_cases[] = {
	{"ignore_no_target", k_ignore_no_target, FLIGHT_KEY_SHIFT_I, PILOT, -1,
	 P_WORK, 0, 0},
	{"ignore_hostile", k_ignore_hostile, FLIGHT_KEY_SHIFT_I, PILOT, ENEMY,
	 P_WORK, 0, 0},
	{"ignore_two_wings", k_ignore_two_wings, FLIGHT_KEY_SHIFT_I, PILOT,
	 ENEMY, P_WORK, F_TWO_WINGS, 0},
	{"ignore_friendly", k_ignore_friendly, FLIGHT_KEY_SHIFT_I, PILOT, WING,
	 P_WORK, 0, 0},
	{"ignore_waiting", k_ignore_waiting, FLIGHT_KEY_SHIFT_I, PILOT, ENEMY,
	 P_WAIT, 0, 0},
	{"ignore_mate_elsewhere", k_ignore_mate_elsewhere, FLIGHT_KEY_SHIFT_I,
	 PILOT, ENEMY, P_WORK, F_OTHER_ELSE, 0},
	{"ignore_mate_busy", k_ignore_mate_busy, FLIGHT_KEY_SHIFT_I, PILOT,
	 ENEMY, P_WORK, F_OTHER_BUSY, 0},
	{"ignore_mate_away", k_ignore_mate_away, FLIGHT_KEY_SHIFT_I, PILOT,
	 ENEMY, P_WORK, F_OTHER_AWAY, 0},
	{"ignore_mate_no_radio", k_ignore_mate_no_radio, FLIGHT_KEY_SHIFT_I,
	 PILOT, ENEMY, P_WORK, F_OTHER_NORADIO, 0},
	{"ignore_mate_foreign", k_ignore_mate_foreign, FLIGHT_KEY_SHIFT_I,
	 PILOT, ENEMY, P_WORK, F_OTHER_FOREIGN, 0},
	{"ignore_mate_no_craft", k_ignore_mate_no_craft, FLIGHT_KEY_SHIFT_I,
	 PILOT, ENEMY, P_WORK, F_OTHER_NOBODY, 0},
	{"ignore_mate_is_target", k_ignore_mate_is_target, FLIGHT_KEY_SHIFT_I,
	 PILOT, MATE_CRAFT, P_WORK, F_OTHER_ISTARGET, 0},
	{"ignore_asks_local", k_ignore_asks_local, FLIGHT_KEY_SHIFT_I, MATE,
	 ENEMY, P_WORK, 0, 0},
};

/* Shift+I makes the pilot's wingmen avoid the targeted enemy and asks the
 * teammate players who already target it to ignore it too. A friendly target
 * is ignored; a teammate who targets something else, is busy, away, has no
 * radio, is on another team, has no craft or flies the target is not asked.
 * A teammate at the local seat is told with a message. */
static void check_order_ignore(void)
{
	for (size_t k = 0; k < sizeof g_ignore_cases / sizeof g_ignore_cases[0];
	     ++k) {
		run_order_case(&g_ignore_cases[k]);
	}
}

static const char *const k_wait_ai[] = {
	/* recorded wait_ai */
	"flight.action_key slot=0 key=87 object=2 map=0 hyper=0 request=0 predicted=0",
	"ai.plan_started object=4 fg=0 plan=2 target_code=255 target=0 x=0 y=0 z=0 maneuver=0 tier=0 timer=0 same=1 predicted=0",
	"flight.craft_ordered slot=0 command=\"wait\" target=4 plan=2 saved=8 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,152,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_wait_starship[] = {
	/* recorded wait_starship */
	"flight.action_key slot=0 key=87 object=2 map=0 hyper=0 request=0 predicted=0",
	"ai.plan_started object=4 fg=0 plan=3 target_code=255 target=0 x=0 y=0 z=0 maneuver=0 tier=0 timer=0 same=1 predicted=0",
	"flight.craft_ordered slot=0 command=\"wait\" target=4 plan=3 saved=8 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,152,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_wait_already[] = {
	/* recorded wait_already */
	"flight.action_key slot=0 key=87 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_wait_into_hyper[] = {
	/* recorded wait_into_hyper */
	"flight.action_key slot=0 key=87 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_wait_out_of_hyper[] = {
	/* recorded wait_out_of_hyper */
	"flight.action_key slot=0 key=87 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_wait_craft_dead[] = {
	/* recorded wait_craft_dead */
	"flight.action_key slot=0 key=87 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_wait_no_target[] = {
	/* recorded wait_no_target */
	"flight.action_key slot=0 key=87 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_wait_enemy[] = {
	/* recorded wait_enemy */
	"flight.action_key slot=0 key=87 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_wait_cancels_home[] = {
	/* recorded wait_cancels_home */
	"flight.action_key slot=0 key=87 object=2 map=0 hyper=0 request=0 predicted=0",
	"ai.plan_started object=4 fg=0 plan=2 target_code=255 target=0 x=0 y=0 z=0 maneuver=0 tier=0 timer=0 same=1 predicted=0",
	"flight.craft_ordered slot=0 command=\"wait\" target=4 plan=2 saved=4 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,152,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_wait_asks_mate[] = {
	/* recorded wait_asks_mate */
	"flight.action_key slot=0 key=87 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.request_posted slot=1 request=7 by=0 param=-1 timer=1416 predicted=0",
	NULL,
};
static const char *const k_wait_asks_local[] = {
	/* recorded wait_asks_local */
	"flight.action_key slot=1 key=87 object=3 map=0 hyper=0 request=0 predicted=0",
	"flight.request_posted slot=0 request=7 by=1 param=-1 timer=1416 predicted=0",
	"network.player_name_missing slot=1 players=0",
	"msg.message_built message=273 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=273 type=1 length=7 args=\"32768,32769,152,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_wait_other_team[] = {
	/* recorded wait_other_team */
	"flight.action_key slot=0 key=87 object=2 map=0 hyper=0 request=0 predicted=0",
	"msg.message_built message=146 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=146 type=1 length=7 args=\"32768,32769,152,0\" waiting=1 tick=0",
	NULL,
};

static const struct order_case g_wait_cases[] = {
	{"wait_ai", k_wait_ai, FLIGHT_KEY_SHIFT_W, PILOT, WING, P_WORK, 0, 0},
	{"wait_starship", k_wait_starship, FLIGHT_KEY_SHIFT_W, PILOT, WING,
	 P_WORK, F_STARSHIP, 0},
	{"wait_already", k_wait_already, FLIGHT_KEY_SHIFT_W, PILOT, WING,
	 P_WAIT, 0, 0},
	{"wait_into_hyper", k_wait_into_hyper, FLIGHT_KEY_SHIFT_W, PILOT, WING,
	 P_HYPER, 0, 0},
	{"wait_out_of_hyper", k_wait_out_of_hyper, FLIGHT_KEY_SHIFT_W, PILOT,
	 WING, P_OUTHYPER, 0, 0},
	{"wait_craft_dead", k_wait_craft_dead, FLIGHT_KEY_SHIFT_W, PILOT, WING,
	 P_WORK, F_WING_DEAD, 0},
	{"wait_no_target", k_wait_no_target, FLIGHT_KEY_SHIFT_W, PILOT, -1,
	 P_WORK, 0, 0},
	{"wait_enemy", k_wait_enemy, FLIGHT_KEY_SHIFT_W, PILOT, ENEMY, P_WORK,
	 0, 0},
	{"wait_asks_local", k_wait_asks_local, FLIGHT_KEY_SHIFT_W, MATE,
	 PILOT_CRAFT, P_WORK, 0, 0},
	{"wait_other_team", k_wait_other_team, FLIGHT_KEY_SHIFT_W, PILOT,
	 OTHER_CRAFT, P_WORK, 0, 0},
};

/* Shift+W puts the targeted computer craft on the wait plan (a starship on
 * the starship wait plan) and the radio answers. A craft already waiting or
 * jumping gets no answer, nor does a craft that cannot be commanded. Aimed at
 * a teammate player it posts a request, with a message for the local seat;
 * aimed at a player of another team it says the craft is not responding. */
static void check_order_wait(void)
{
	for (size_t k = 0; k < sizeof g_wait_cases / sizeof g_wait_cases[0];
	     ++k) {
		run_order_case(&g_wait_cases[k]);
	}
}

static const char *const k_home_ai[] = {
	/* recorded home_ai */
	"flight.action_key slot=0 key=72 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.craft_aborted slot=0 target=4 fg=0 aborted=1 special=0 predicted=0",
	"ai.plan_started object=4 fg=0 plan=4 target_code=255 target=0 x=0 y=0 z=0 maneuver=0 tier=0 timer=0 same=1 predicted=0",
	"flight.craft_ordered slot=0 command=\"home\" target=4 plan=4 saved=11 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,149,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_home_starship[] = {
	/* recorded home_starship */
	"flight.action_key slot=0 key=72 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.craft_aborted slot=0 target=4 fg=0 aborted=1 special=0 predicted=0",
	"ai.plan_started object=4 fg=0 plan=5 target_code=255 target=0 x=0 y=0 z=0 maneuver=0 tier=0 timer=0 same=1 predicted=0",
	"flight.craft_ordered slot=0 command=\"home\" target=4 plan=5 saved=11 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,149,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_home_special[] = {
	/* recorded home_special */
	"flight.action_key slot=0 key=72 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.craft_aborted slot=0 target=4 fg=0 aborted=1 special=1 predicted=0",
	"ai.plan_started object=4 fg=0 plan=4 target_code=255 target=0 x=0 y=0 z=0 maneuver=0 tier=0 timer=0 same=1 predicted=0",
	"flight.craft_ordered slot=0 command=\"home\" target=4 plan=4 saved=11 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,149,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_home_aborted[] = {
	/* recorded home_aborted */
	"flight.action_key slot=0 key=72 object=2 map=0 hyper=0 request=0 predicted=0",
	"ai.plan_started object=4 fg=0 plan=4 target_code=255 target=0 x=0 y=0 z=0 maneuver=0 tier=0 timer=0 same=1 predicted=0",
	"flight.craft_ordered slot=0 command=\"home\" target=4 plan=4 saved=11 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,149,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_home_going[] = {
	/* recorded home_going */
	"flight.action_key slot=0 key=72 object=2 map=0 hyper=0 request=0 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,149,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_home_starship_going[] = {
	/* recorded home_starship_going */
	"flight.action_key slot=0 key=72 object=2 map=0 hyper=0 request=0 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,149,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_home_asks_mate[] = {
	/* recorded home_asks_mate */
	"flight.action_key slot=0 key=72 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.request_posted slot=1 request=6 by=0 param=-1 timer=1416 predicted=0",
	NULL,
};
static const char *const k_home_asks_local[] = {
	/* recorded home_asks_local */
	"flight.action_key slot=1 key=72 object=3 map=0 hyper=0 request=0 predicted=0",
	"flight.request_posted slot=0 request=6 by=1 param=-1 timer=1416 predicted=0",
	"network.player_name_missing slot=1 players=0",
	"msg.message_built message=272 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=272 type=1 length=7 args=\"32768,32769,149,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_home_other_team[] = {
	/* recorded home_other_team */
	"flight.action_key slot=0 key=72 object=2 map=0 hyper=0 request=0 predicted=0",
	"msg.message_built message=146 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=146 type=1 length=7 args=\"32768,32769,149,0\" waiting=1 tick=0",
	NULL,
};

static const struct order_case g_home_cases[] = {
	{"home_ai", k_home_ai, FLIGHT_KEY_SHIFT_H, PILOT, WING, P_WORK, 0, 0},
	{"home_starship", k_home_starship, FLIGHT_KEY_SHIFT_H, PILOT, WING,
	 P_WORK, F_STARSHIP, 0},
	{"home_special", k_home_special, FLIGHT_KEY_SHIFT_H, PILOT, WING,
	 P_WORK, F_SPECIAL, 0},
	{"home_aborted", k_home_aborted, FLIGHT_KEY_SHIFT_H, PILOT, WING,
	 P_WORK, F_ABORTED, 0},
	{"home_asks_local", k_home_asks_local, FLIGHT_KEY_SHIFT_H, MATE,
	 PILOT_CRAFT, P_WORK, 0, 0},
	{"home_other_team", k_home_other_team, FLIGHT_KEY_SHIFT_H, PILOT,
	 OTHER_CRAFT, P_WORK, 0, 0},
};

/* Shift+H sends the targeted computer craft home (a starship into
 * hyperspace) and the radio answers. The first time, its flight group is
 * counted as aborted, and its special cargo is marked aborted when it carries
 * it. A craft already going home still gets the answer. Aimed at a teammate
 * player it posts a request; at another team's player it says the craft is not
 * responding. */
static void check_order_home(void)
{
	for (size_t k = 0; k < sizeof g_home_cases / sizeof g_home_cases[0];
	     ++k) {
		run_order_case(&g_home_cases[k]);
	}
}

static const char *const k_go_waiting[] = {
	/* recorded go_waiting */
	"flight.action_key slot=0 key=71 object=2 map=0 hyper=0 request=0 predicted=0",
	"ai.plan_started object=4 fg=0 plan=11 target_code=255 target=0 x=0 y=0 z=0 maneuver=0 tier=0 timer=0 same=1 predicted=0",
	"flight.craft_ordered slot=0 command=\"go\" target=4 plan=11 saved=11 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,153,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_go_other_plan[] = {
	/* recorded go_other_plan */
	"flight.action_key slot=0 key=71 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_go_starship[] = {
	/* recorded go_starship */
	"flight.action_key slot=0 key=71 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_go_asks_mate[] = {
	/* recorded go_asks_mate */
	"flight.action_key slot=0 key=71 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.request_posted slot=1 request=8 by=0 param=-1 timer=1416 predicted=0",
	NULL,
};
static const char *const k_go_asks_local[] = {
	/* recorded go_asks_local */
	"flight.action_key slot=1 key=71 object=3 map=0 hyper=0 request=0 predicted=0",
	"flight.request_posted slot=0 request=8 by=1 param=-1 timer=1416 predicted=0",
	"network.player_name_missing slot=1 players=0",
	"msg.message_built message=274 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=274 type=1 length=7 args=\"32768,32769,153,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_go_other_team[] = {
	/* recorded go_other_team */
	"flight.action_key slot=0 key=71 object=2 map=0 hyper=0 request=0 predicted=0",
	"msg.message_built message=146 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=146 type=1 length=7 args=\"32768,32769,153,0\" waiting=1 tick=0",
	NULL,
};

static const struct order_case g_go_cases[] = {
	{"go_waiting", k_go_waiting, FLIGHT_KEY_SHIFT_G, PILOT, WING, P_WAIT, 0,
	 0},
	{"go_other_plan", k_go_other_plan, FLIGHT_KEY_SHIFT_G, PILOT, WING,
	 P_WORK, 0, 0},
	{"go_asks_local", k_go_asks_local, FLIGHT_KEY_SHIFT_G, MATE,
	 PILOT_CRAFT, P_WORK, 0, 0},
	{"go_other_team", k_go_other_team, FLIGHT_KEY_SHIFT_G, PILOT,
	 OTHER_CRAFT, P_WORK, 0, 0},
};

/* Shift+G sends a waiting computer craft back to the plan it had before and
 * the radio answers. A craft on any other plan, a waiting starship included,
 * gets nothing. Aimed at a teammate player it posts a request; at another
 * team's player it says the craft is not responding. */
static void check_order_go(void)
{
	for (size_t k = 0; k < sizeof g_go_cases / sizeof g_go_cases[0]; ++k) {
		run_order_case(&g_go_cases[k]);
	}
}

static const char *const k_resupply_radio_out[] = {
	/* recorded resupply_radio_out */
	"flight.action_key slot=0 key=66 object=2 map=0 hyper=0 request=0 predicted=0",
	"msg.message_built message=86 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=86 type=1 length=7 args=\"101,87,153,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_resupply_no_target[] = {
	/* recorded resupply_no_target */
	"flight.action_key slot=0 key=66 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_resupply_far[] = {
	/* recorded resupply_far */
	"flight.action_key slot=0 key=66 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_resupply_boardtogive[] = {
	/* recorded resupply_boardtogive */
	"flight.action_key slot=0 key=66 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.resupply_called slot=0 target=7 plan=9 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,258,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_resupply_board3[] = {
	/* recorded resupply_board3 */
	"flight.action_key slot=0 key=66 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.resupply_called slot=0 target=7 plan=10 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,258,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_resupply_other_plan[] = {
	/* recorded resupply_other_plan */
	"flight.action_key slot=0 key=66 object=2 map=0 hyper=0 request=0 predicted=0",
	"msg.message_built message=146 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=146 type=1 length=7 args=\"32768,32769,258,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_resupply_not_named[] = {
	/* recorded resupply_not_named */
	"flight.action_key slot=0 key=66 object=2 map=0 hyper=0 request=0 predicted=0",
	"msg.message_built message=146 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=146 type=1 length=7 args=\"32768,32769,258,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_resupply_and_one[] = {
	/* recorded resupply_and_one */
	"flight.action_key slot=0 key=66 object=2 map=0 hyper=0 request=0 predicted=0",
	"msg.message_built message=146 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=146 type=1 length=7 args=\"32768,32769,258,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_resupply_and_both[] = {
	/* recorded resupply_and_both */
	"flight.action_key slot=0 key=66 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.resupply_called slot=0 target=7 plan=9 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,258,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_resupply_second_pair[] = {
	/* recorded resupply_second_pair */
	"flight.action_key slot=0 key=66 object=2 map=0 hyper=0 request=0 predicted=0",
	"flight.resupply_called slot=0 target=7 plan=9 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,258,0\" waiting=1 tick=0",
	NULL,
};

static const struct order_case g_resupply_cases[] = {
	{"resupply_radio_out", k_resupply_radio_out, FLIGHT_KEY_SHIFT_B, PILOT,
	 SUPPLY, P_BOARD, F_COMMS_OUT, MATCH_FIRST},
	{"resupply_no_target", k_resupply_no_target, FLIGHT_KEY_SHIFT_B, PILOT,
	 -1, P_BOARD, 0, MATCH_FIRST},
	{"resupply_far", k_resupply_far, FLIGHT_KEY_SHIFT_B, PILOT, FAR,
	 P_BOARD, 0, MATCH_FIRST},
	{"resupply_boardtogive", k_resupply_boardtogive, FLIGHT_KEY_SHIFT_B,
	 PILOT, SUPPLY, P_BOARD, 0, MATCH_FIRST},
	{"resupply_board3", k_resupply_board3, FLIGHT_KEY_SHIFT_B, PILOT,
	 SUPPLY, P_BOARD3, 0, MATCH_FIRST},
	{"resupply_other_plan", k_resupply_other_plan, FLIGHT_KEY_SHIFT_B,
	 PILOT, SUPPLY, P_WORK, 0, MATCH_FIRST},
	{"resupply_not_named", k_resupply_not_named, FLIGHT_KEY_SHIFT_B, PILOT,
	 SUPPLY, P_BOARD, 0, MATCH_NONE},
	{"resupply_and_one", k_resupply_and_one, FLIGHT_KEY_SHIFT_B, PILOT,
	 SUPPLY, P_BOARD, 0, MATCH_AND_ONE},
	{"resupply_and_both", k_resupply_and_both, FLIGHT_KEY_SHIFT_B, PILOT,
	 SUPPLY, P_BOARD, 0, MATCH_AND_BOTH},
	{"resupply_second_pair", k_resupply_second_pair, FLIGHT_KEY_SHIFT_B,
	 PILOT, SUPPLY, P_BOARD, 0, MATCH_SECOND_PAIR},
};

/* Shift+B calls the targeted supply craft when it is on a boarding plan whose
 * order names the pilot's craft: the radio and the tactical officer answer.
 * Any other plan, or an order that does not name the pilot, gets the
 * not-responding message. With the radio out it says so; with no target, or
 * one past the craft slots, nothing happens. */
static void check_order_resupply(void)
{
	for (size_t k = 0;
	     k < sizeof g_resupply_cases / sizeof g_resupply_cases[0]; ++k) {
		run_order_case(&g_resupply_cases[k]);
	}
}

static const char *const k_wait_starship_again[] = {
	/* recorded wait_starship_again */
	"flight.action_key slot=0 key=87 object=2 map=0 hyper=0 request=0 predicted=0",
	"ai.plan_started object=4 fg=0 plan=3 target_code=255 target=0 x=0 y=0 z=0 maneuver=0 tier=0 timer=0 same=1 predicted=0",
	"flight.craft_ordered slot=0 command=\"wait\" target=4 plan=3 saved=3 predicted=0",
	"msg.message_built message=148 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=148 type=1 length=7 args=\"32768,32769,152,0\" waiting=1 tick=0",
	NULL,
};

static const struct order_case g_wait_findings[] = {
	{"wait_cancels_home", k_wait_cancels_home, FLIGHT_KEY_SHIFT_W, PILOT,
	 WING, P_HOME, 0, 0},
	{"wait_starship_again", k_wait_starship_again, FLIGHT_KEY_SHIFT_W,
	 PILOT, WING, P_STARWAIT, F_STARSHIP, 0},
};

/* Recorded as the code does it: Shift+W on a craft that is flying home puts
 * it on the wait plan and forgets that it was going home, so the order to go
 * home is cancelled; and a second Shift+W on a waiting starship (its guard
 * only knows the craft wait plan) waits it again and keeps the wait plan as
 * the plan to resume, losing the real one. */
static void check_order_wait_overrides(void)
{
	for (size_t k = 0;
	     k < sizeof g_wait_findings / sizeof g_wait_findings[0]; ++k) {
		run_order_case(&g_wait_findings[k]);
	}
}

static const struct order_case g_go_finding = {
	"go_starship", k_go_starship, FLIGHT_KEY_SHIFT_G, PILOT,
	WING,	       P_STARWAIT,    F_STARSHIP,	  0};

/* Recorded as the code does it: Shift+G on a starship that Shift+W put on
 * the starship wait plan does nothing and gets no answer, because it only
 * resumes craft on the craft wait plan, so a starship told to wait never
 * resumes. */
static void check_order_go_starship(void) { run_order_case(&g_go_finding); }

static const struct order_case g_home_findings[] = {
	{"home_going", k_home_going, FLIGHT_KEY_SHIFT_H, PILOT, WING, P_HOME, 0,
	 0},
	{"home_starship_going", k_home_starship_going, FLIGHT_KEY_SHIFT_H,
	 PILOT, WING, P_STARHOME, F_STARSHIP, 0},
};

/* Recorded as the code does it: Shift+H on a craft that is already going home
 * (or a starship already jumping out) changes nothing but the radio still
 * answers, while Shift+W and Shift+G stay silent when they change nothing. */
static void check_order_home_already_going(void)
{
	for (size_t k = 0;
	     k < sizeof g_home_findings / sizeof g_home_findings[0]; ++k) {
		run_order_case(&g_home_findings[k]);
	}
}

static const struct order_case g_ask_findings[] = {
	{"wait_asks_mate", k_wait_asks_mate, FLIGHT_KEY_SHIFT_W, PILOT,
	 MATE_CRAFT, P_WORK, 0, 0},
	{"home_asks_mate", k_home_asks_mate, FLIGHT_KEY_SHIFT_H, PILOT,
	 MATE_CRAFT, P_WORK, 0, 0},
	{"go_asks_mate", k_go_asks_mate, FLIGHT_KEY_SHIFT_G, PILOT, MATE_CRAFT,
	 P_WORK, 0, 0},
};

/* Recorded as the code does it: Shift+W, H and G aimed at a teammate player
 * post the request but never set what it is about, so the request line shows
 * the old value (-1) where Shift+A and Shift+I name the target. */
static void check_order_ask_param(void)
{
	for (size_t k = 0; k < sizeof g_ask_findings / sizeof g_ask_findings[0];
	     ++k) {
		run_order_case(&g_ask_findings[k]);
	}
}

/* What the keys change that no line carries: Shift+A stops the pilot
 * avoiding the target and makes the wingman's candidate target the enemy;
 * Shift+I makes both the pilot and the wingman avoid it; Shift+B makes the
 * supply craft's candidate target the pilot's craft. */
static void check_order_state_changes(void)
{
	struct order_case c = {"state", NULL,	FLIGHT_KEY_SHIFT_A, PILOT,
			       ENEMY,	P_WORK, F_AVOIDING,	    0};
	order_world(&c);
	press(PILOT, c.key);
	XVT_ASSERT_INT_EQ(
		g_test_craft[PILOT_CRAFT].player_command_avoid_target_obj_idx,
		UINT16_MAX);
	XVT_ASSERT_INT_EQ(g_test_craft[WING].ai_controller.candidate_target_idx,
			  ENEMY);

	c.key = FLIGHT_KEY_SHIFT_I;
	c.flags = 0;
	order_world(&c);
	press(PILOT, c.key);
	XVT_ASSERT_INT_EQ(
		g_test_craft[PILOT_CRAFT].player_command_avoid_target_obj_idx,
		ENEMY);
	XVT_ASSERT_INT_EQ(
		g_test_craft[WING].player_command_avoid_target_obj_idx, ENEMY);

	c.key = FLIGHT_KEY_SHIFT_B;
	c.target = SUPPLY;
	c.plan = P_BOARD;
	order_world(&c);
	press(PILOT, c.key);
	XVT_ASSERT_INT_EQ(
		g_test_craft[SUPPLY].ai_controller.candidate_target_idx,
		PILOT_CRAFT);
}

/* ---- Targeting keys: I, Shift+P, P and E ------------------------------- */

enum {
	T_LOCATE = 1,		 /* The mission allows locating players. */
	T_COMP_OUT = 2,		 /* The presser's targeting computer is out. */
	T_NO_MATE = 4,		 /* The teammate is not in the flight. */
	T_NO_HOSTILE = 8,	 /* The enemy players are not in the flight. */
	T_DECOY6 = 16,		 /* The near enemy player's decoy beam is on. */
	T_BREAKING6 = 32,	 /* That craft is breaking up. */
	T_EXPLOSION6 = 64,	 /* That object is an explosion. */
	T_FAMILY6 = 128,	 /* That record has a nonzero family. */
	T_MAP = 256,		 /* The presser is in the map view. */
	T_HUD = 512,		 /* The HUD target readout is on. */
	T_NOCRAFT = 1024,	 /* The presser has no craft. */
	T_ALLY_THEIRS = 2048,	 /* Team 2 lists the pilot's team as allied. */
	T_ALLY_MINE = 4096,	 /* The pilot's team lists team 2 as allied. */
	T_MATE_FAMILY = 8192,	 /* The teammate's record has a family. */
	T_MATE_NOMOBJ = 16384,	 /* The teammate's object has no mobile. */
	T_AI_DEAD = 32768,	 /* The computer attacker has no systems. */
	T_AI_KIND = 65536,	 /* The computer attacker is breaking up. */
	T_RECENT6 = 131072,	 /* The near enemy player just hit the pilot. */
	T_STALE = 262144,	 /* The hit was over five seconds ago. */
	T_RECENT_MATE = 524288,	 /* The teammate just hit the pilot. */
	T_AIMS_AT_ME6 = 1048576, /* The near enemy player targets the pilot. */
	T_AIMS_AT_ME_MATE = 2097152, /* The teammate targets the pilot. */
};

enum {
	FOE_NEAR = 6,	    /* The near enemy player's craft. */
	FOE_FAR = 9,	    /* The far enemy player's craft. */
	CRAFT_END = 12,	    /* The craft slots end here; shots follow. */
	SHOT_WARHEAD = 144, /* A warhead's object type. */
	SHOT_CANNON = 137,  /* A cannon shot's object type. */
	NO_GUIDANCE = -2,
};

/* A shot in flight: its object type and genus, what its guidance aims at
 * and how far past the pilot it flies. */
struct shot {
	int type;
	int genus;
	int aim;
	int distance;
};

struct target_case {
	const char *name;
	const char *const *lines;
	uint16_t key;
	int flags;
	int target;		  /* Object slot the presser targets, or -1. */
	int ai_aim;		  /* Whom the computer craft is aimed at. */
	int maneuver;		  /* The computer craft's maneuver mode. */
	const struct shot *shots; /* Ended by a zero type and genus. */
};

static struct warhead_guidance_state g_test_guidance[SLOT_COUNT];

static void target_world(const struct target_case *c)
{
	int flags = c->flags;
	fresh_world();
	g_active_region_craft_object_slot_end = CRAFT_END;
	g_projectile_object_slot_start = CRAFT_END;
	g_projectile_object_slot_end = SLOT_COUNT;
	memset(g_test_guidance, 0, sizeof g_test_guidance);
	fly(PILOT, PILOT_CRAFT, 0);
	if (!(flags & T_NO_MATE)) {
		fly(MATE, MATE_CRAFT, 0);
	}
	if (!(flags & T_NO_HOSTILE)) {
		fly(2, FOE_NEAR, 1);
		fly(3, FOE_FAR, 1);
	}
	g_active_flight_player_count = 4;
	g_flight_mission_state.locate_players_enabled =
		flags & T_LOCATE ? 1 : 0;
	computer_craft(ENEMY, 2, 2, P_WORK);
	g_test_craft[ENEMY].ai_controller.target_obj_idx = (uint16_t)c->ai_aim;
	g_test_craft[ENEMY].ai_controller.maneuver_mode = (uint8_t)c->maneuver;
	struct craft_data *own = &g_test_craft[PILOT_CRAFT];
	if (flags & T_COMP_OUT) {
		own->working_subsystems &=
			~CRAFT_SUBSYSTEM_FLAG_TARGETING_COMPUTER;
	}
	if (flags & T_MAP) {
		g_players[PILOT].map_camera_state = 1;
	}
	if (flags & T_HUD) {
		g_players[PILOT].view_state.hud_state_live = HUD_VIEW_FORWARD;
		own->damage_stats.active_hud_feature_mask = 1;
	}
	if (flags & T_DECOY6) {
		g_test_craft[FOE_NEAR].beam_active = 1;
		g_test_craft[FOE_NEAR].beam_type_id = BEAM_TYPE_DECOY;
		g_test_craft[FOE_NEAR].beam_output = 1;
	}
	if (flags & T_BREAKING6) {
		g_test_craft[FOE_NEAR].object_kind =
			CRAFT_OBJECT_KIND_BREAKING_UP;
	}
	if (flags & T_EXPLOSION6) {
		g_test_objects[FOE_NEAR].genus_id = CRAFT_GENUS_EXPLOSION;
	}
	if (flags & T_FAMILY6) {
		g_test_mobiles[FOE_NEAR].family = 1;
	}
	if (flags & T_MATE_FAMILY) {
		g_test_mobiles[MATE_CRAFT].family = 1;
	}
	if (flags & T_MATE_NOMOBJ) {
		g_test_objects[MATE_CRAFT].mobj = NULL;
	}
	if (flags & T_ALLY_THEIRS) {
		g_mission_teams[2].allies[0] = 1;
	}
	if (flags & T_ALLY_MINE) {
		g_mission_teams[0].allies[2] = 1;
	}
	if (flags & T_AI_DEAD) {
		g_test_craft[ENEMY].working_subsystems = 0;
	}
	if (flags & T_AI_KIND) {
		g_test_craft[ENEMY].object_kind = CRAFT_OBJECT_KIND_BREAKING_UP;
	}
	if (flags & (T_RECENT6 | T_RECENT_MATE)) {
		own->last_attacker_obj_idx =
			flags & T_RECENT6 ? FOE_NEAR : MATE_CRAFT;
		own->last_hit_mission_second = 0;
	}
	if (flags & T_STALE) {
		g_mission_elapsed_clock.seconds = 10;
	}
	if (flags & T_AIMS_AT_ME6) {
		g_players[2].current_target_object_idx = PILOT_CRAFT;
	}
	if (flags & T_AIMS_AT_ME_MATE) {
		g_players[MATE].current_target_object_idx = PILOT_CRAFT;
	}
	g_players[PILOT].current_target_object_idx = (int16_t)c->target;
	if (flags & T_NOCRAFT) {
		g_players[PILOT].object_index = -1;
	}
	for (int n = 0; c->shots != NULL &&
			(c->shots[n].type != 0 || c->shots[n].genus != 0);
	     ++n) {
		int slot = CRAFT_END + n;
		const struct shot *shot = &c->shots[n];
		struct object_record *pilot = &g_test_objects[PILOT_CRAFT];
		g_test_objects[slot].object_type = (uint16_t)shot->type;
		g_test_objects[slot].genus_id = (uint8_t)shot->genus;
		g_test_objects[slot].world_x = pilot->world_x + shot->distance;
		g_test_objects[slot].world_y = pilot->world_y;
		g_test_objects[slot].world_z = pilot->world_z;
		if (shot->aim != NO_GUIDANCE) {
			g_test_guidance[slot].target_obj_idx =
				(uint16_t)shot->aim;
			g_test_mobiles[slot].p_warhead_guidance =
				&g_test_guidance[slot];
		}
	}
}

static void run_target_case(const struct target_case *c)
{
	target_world(c);
	scene_open();
	press(PILOT, c->key);
	scene_close(c->name, c->lines);
}

#define SP_ROW(n, f, t) {#n, k_##n, FLIGHT_KEY_SHIFT_P, f, t, 0, 0, NULL}
#define P_ROW(n, f, t) {#n, k_##n, FLIGHT_KEY_P, f, t, 0, 0, NULL}
#define E_ROW(n, f, t, a, m) {#n, k_##n, FLIGHT_KEY_E, f, t, a, m, NULL}
#define I_ROW(n, f, ...)                                                       \
	{                                                                      \
		#n, k_##n, FLIGHT_KEY_I, f, -1, 0, 0, (const struct shot[])    \
		{                                                              \
			__VA_ARGS__, {0, 0, 0, 0}                              \
		}                                                              \
	}

static const char *const k_sp_first[] = {
	/* recorded sp_first */
	"flight.action_key slot=0 key=80 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=3 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=3 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=3 ai_target=0",
	NULL,
};
static const char *const k_sp_next[] = {
	/* recorded sp_next */
	"flight.action_key slot=0 key=80 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=6 previous=3 map=0 predicted=0",
	"player.target_component slot=0 target=6 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=6 ai_target=0",
	NULL,
};
static const char *const k_sp_wrap[] = {
	/* recorded sp_wrap */
	"flight.action_key slot=0 key=80 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=3 previous=9 map=0 predicted=0",
	"player.target_component slot=0 target=3 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=3 ai_target=0",
	NULL,
};
static const char *const k_sp_off_friend[] = {
	/* recorded sp_off_friend */
	"flight.action_key slot=0 key=80 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=3 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=3 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=3 ai_target=0",
	NULL,
};
static const char *const k_sp_off_none[] = {
	/* recorded sp_off_none */
	"flight.action_key slot=0 key=80 object=2 map=0 hyper=0 request=0 predicted=0",
	"msg.message_built message=225 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=225 type=1 length=7 args=\"32768,32769,258,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_sp_on_none[] = {
	/* recorded sp_on_none */
	"flight.action_key slot=0 key=80 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_sp_decoy[] = {
	/* recorded sp_decoy */
	"flight.action_key slot=0 key=80 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=9 previous=3 map=0 predicted=0",
	"player.target_component slot=0 target=9 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=9 ai_target=0",
	NULL,
};
static const char *const k_sp_breaking[] = {
	/* recorded sp_breaking */
	"flight.action_key slot=0 key=80 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=9 previous=3 map=0 predicted=0",
	"player.target_component slot=0 target=9 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=9 ai_target=0",
	NULL,
};
static const char *const k_sp_family[] = {
	/* recorded sp_family */
	"flight.action_key slot=0 key=80 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=6 previous=3 map=0 predicted=0",
	"player.target_component slot=0 target=6 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=6 ai_target=0",
	NULL,
};
static const char *const k_sp_explosion[] = {
	/* recorded sp_explosion */
	"flight.action_key slot=0 key=80 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=9 previous=3 map=0 predicted=0",
	"player.target_component slot=0 target=9 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=9 ai_target=0",
	NULL,
};
static const char *const k_sp_comp_out[] = {
	/* recorded sp_comp_out */
	"flight.action_key slot=0 key=80 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_refused slot=0 target=3 predicted=0",
	"msg.message_built message=86 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=86 type=1 length=7 args=\"96,87,258,0\" waiting=1 tick=0",
	NULL,
};
static const char *const k_sp_same[] = {
	/* recorded sp_same */
	"flight.action_key slot=0 key=80 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_sp_map[] = {
	/* recorded sp_map */
	"flight.action_key slot=0 key=80 object=2 map=1 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=3 previous=-1 map=1 predicted=0",
	"player.target_component slot=0 target=3 component=0 predicted=0",
	"msg.message_built message=309 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=309 type=1 length=7 args=\"32768,331,323,331\" waiting=1 tick=0",
	"msg.target_described slot=0 target=3 relation=331 designation=323 phrase=331 actionable=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=3 ai_target=0",
	NULL,
};
static const char *const k_sp_hud[] = {
	/* recorded sp_hud */
	"flight.action_key slot=0 key=80 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=3 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=3 component=0 predicted=0",
	"msg.message_built message=309 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=309 type=1 length=7 args=\"32768,331,323,331\" waiting=1 tick=0",
	"msg.target_described slot=0 target=3 relation=331 designation=323 phrase=331 actionable=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=3 ai_target=0",
	NULL,
};

/* Shift+P cycles through the other players' craft in slot order, from the
 * one after the current target round to the current one, and takes the first
 * that qualifies, a teammate before a nearer enemy. Enemy players only count
 * when the mission allows locating players; with none to take it says
 * auto locating is off (only when it is). Decoys, explosions and craft
 * breaking up are skipped; the targeting computer being out refuses the new
 * target; coming round to the current target changes nothing. The map view
 * and the HUD readout add a target description. */
static const struct target_case g_shift_p_cases[] = {
	SP_ROW(sp_first, T_LOCATE, -1),
	SP_ROW(sp_next, T_LOCATE, MATE_CRAFT),
	SP_ROW(sp_wrap, T_LOCATE, FOE_FAR),
	SP_ROW(sp_off_friend, 0, -1),
	SP_ROW(sp_off_none, T_NO_MATE, -1),
	SP_ROW(sp_on_none, T_LOCATE | T_NO_MATE | T_NO_HOSTILE, -1),
	SP_ROW(sp_decoy, T_LOCATE | T_DECOY6, MATE_CRAFT),
	SP_ROW(sp_breaking, T_LOCATE | T_BREAKING6, MATE_CRAFT),
	SP_ROW(sp_family, T_LOCATE | T_BREAKING6 | T_FAMILY6, MATE_CRAFT),
	SP_ROW(sp_explosion, T_LOCATE | T_EXPLOSION6, MATE_CRAFT),
	SP_ROW(sp_comp_out, T_LOCATE | T_COMP_OUT, -1),
	SP_ROW(sp_same, T_LOCATE | T_NO_HOSTILE, MATE_CRAFT),
	SP_ROW(sp_map, T_LOCATE | T_MAP, -1),
	SP_ROW(sp_hud, T_LOCATE | T_HUD, -1),
};

static void check_target_next_player(void)
{
	for (size_t k = 0;
	     k < sizeof g_shift_p_cases / sizeof g_shift_p_cases[0]; ++k) {
		run_target_case(&g_shift_p_cases[k]);
	}
}

static const char *const k_p_off[] = {
	/* recorded p_off */
	"flight.action_key slot=0 key=112 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_p_nearest[] = {
	/* recorded p_nearest */
	"flight.action_key slot=0 key=112 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=6 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=6 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=6 ai_target=0",
	NULL,
};
static const char *const k_p_same[] = {
	/* recorded p_same */
	"flight.action_key slot=0 key=112 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_p_decoy[] = {
	/* recorded p_decoy */
	"flight.action_key slot=0 key=112 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=9 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=9 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=9 ai_target=0",
	NULL,
};
static const char *const k_p_breaking[] = {
	/* recorded p_breaking */
	"flight.action_key slot=0 key=112 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=9 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=9 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=9 ai_target=0",
	NULL,
};
static const char *const k_p_family[] = {
	/* recorded p_family */
	"flight.action_key slot=0 key=112 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=6 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=6 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=6 ai_target=0",
	NULL,
};
static const char *const k_p_explosion[] = {
	/* recorded p_explosion */
	"flight.action_key slot=0 key=112 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=9 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=9 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=9 ai_target=0",
	NULL,
};
static const char *const k_p_no_hostile[] = {
	/* recorded p_no_hostile */
	"flight.action_key slot=0 key=112 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_p_comp_out[] = {
	/* recorded p_comp_out */
	"flight.action_key slot=0 key=112 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_refused slot=0 target=6 predicted=0",
	"msg.message_built message=86 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=86 type=1 length=7 args=\"96,87,323,331\" waiting=1 tick=0",
	NULL,
};
static const char *const k_p_map[] = {
	/* recorded p_map */
	"flight.action_key slot=0 key=112 object=2 map=1 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=6 previous=-1 map=1 predicted=0",
	"player.target_component slot=0 target=6 component=0 predicted=0",
	"msg.message_built message=309 type=1 length=7 sender=0 voice=0 clock=\"0:00:01\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=309 type=1 length=7 args=\"32768,332,324,331\" waiting=1 tick=0",
	"msg.target_described slot=0 target=6 relation=332 designation=324 phrase=331 actionable=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=6 ai_target=0",
	NULL,
};

/* P targets the nearest hostile player's craft (never a teammate), and only
 * when the mission allows locating players; otherwise it does nothing. It
 * skips decoys, explosions and craft breaking up, does nothing when the
 * nearest is already the target or no enemy player is in the flight, and
 * the targeting computer being out refuses it. */
static const struct target_case g_p_cases[] = {
	P_ROW(p_off, 0, -1),
	P_ROW(p_nearest, T_LOCATE, -1),
	P_ROW(p_same, T_LOCATE, FOE_NEAR),
	P_ROW(p_decoy, T_LOCATE | T_DECOY6, -1),
	P_ROW(p_breaking, T_LOCATE | T_BREAKING6, -1),
	P_ROW(p_family, T_LOCATE | T_BREAKING6 | T_FAMILY6, -1),
	P_ROW(p_explosion, T_LOCATE | T_EXPLOSION6, -1),
	P_ROW(p_no_hostile, T_LOCATE | T_NO_HOSTILE, -1),
	P_ROW(p_comp_out, T_LOCATE | T_COMP_OUT, -1),
	P_ROW(p_map, T_LOCATE | T_MAP, -1),
};

static void check_target_nearest_hostile(void)
{
	for (size_t k = 0; k < sizeof g_p_cases / sizeof g_p_cases[0]; ++k) {
		run_target_case(&g_p_cases[k]);
	}
}

static const char *const k_e_no_craft[] = {
	/* recorded e_no_craft */
	"flight.action_key slot=0 key=101 object=-1 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_e_nobody[] = {
	/* recorded e_nobody */
	"flight.action_key slot=0 key=101 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_e_ai_12[] = {
	/* recorded e_ai_12 */
	"flight.action_key slot=0 key=101 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=5 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=5 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=5 ai_target=0",
	NULL,
};
static const char *const k_e_ai_23[] = {
	/* recorded e_ai_23 */
	"flight.action_key slot=0 key=101 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=5 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=5 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=5 ai_target=0",
	NULL,
};
static const char *const k_e_ai_mode[] = {
	/* recorded e_ai_mode */
	"flight.action_key slot=0 key=101 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_e_ai_aim[] = {
	/* recorded e_ai_aim */
	"flight.action_key slot=0 key=101 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_e_ai_dead[] = {
	/* recorded e_ai_dead */
	"flight.action_key slot=0 key=101 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_e_ai_inactive[] = {
	/* recorded e_ai_inactive */
	"flight.action_key slot=0 key=101 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_e_recent[] = {
	/* recorded e_recent */
	"flight.action_key slot=0 key=101 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=6 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=6 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=6 ai_target=0",
	NULL,
};
static const char *const k_e_stale[] = {
	/* recorded e_stale */
	"flight.action_key slot=0 key=101 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_e_recent_mate[] = {
	/* recorded e_recent_mate */
	"flight.action_key slot=0 key=101 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_e_aimed[] = {
	/* recorded e_aimed */
	"flight.action_key slot=0 key=101 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=6 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=6 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=6 ai_target=0",
	NULL,
};
static const char *const k_e_aimed_mate[] = {
	/* recorded e_aimed_mate */
	"flight.action_key slot=0 key=101 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_e_decoy[] = {
	/* recorded e_decoy */
	"flight.action_key slot=0 key=101 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_e_explosion[] = {
	/* recorded e_explosion */
	"flight.action_key slot=0 key=101 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_e_order[] = {
	/* recorded e_order */
	"flight.action_key slot=0 key=101 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=5 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=5 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=5 ai_target=0",
	NULL,
};
static const char *const k_e_wrap[] = {
	/* recorded e_wrap */
	"flight.action_key slot=0 key=101 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=5 previous=6 map=0 predicted=0",
	"player.target_component slot=0 target=5 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=5 ai_target=0",
	NULL,
};
static const char *const k_e_comp_out[] = {
	/* recorded e_comp_out */
	"flight.action_key slot=0 key=101 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_refused slot=0 target=5 predicted=0",
	"msg.message_built message=86 type=1 length=7 sender=0 voice=0 clock=\"0:00:10\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=86 type=1 length=7 args=\"96,87,324,331\" waiting=1 tick=0",
	NULL,
};
static const char *const k_e_map[] = {
	/* recorded e_map */
	"flight.action_key slot=0 key=101 object=2 map=1 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=5 previous=-1 map=1 predicted=0",
	"player.target_component slot=0 target=5 component=0 predicted=0",
	"msg.message_built message=309 type=1 length=7 sender=0 voice=0 clock=\"0:00:10\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=309 type=1 length=7 args=\"32768,332,324,331\" waiting=1 tick=0",
	"msg.target_described slot=0 target=5 relation=332 designation=324 phrase=331 actionable=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=5 ai_target=0",
	NULL,
};

/* Recorded as the code does it: a teammate who hit the pilot in the last five
 * seconds is not targeted by E (e_recent_mate records no target), though an
 * enemy who did is. The product lines that decide it are flight.c 5827-5855.
 *
 * E targets whoever is attacking the pilot: the first craft in slot order,
 * after the current target, that is a computer craft aimed at the pilot in
 * attack mode 12 or 23, a player who hit the pilot in the last five seconds,
 * or a hostile player who has the pilot targeted. Craft
 * with no working systems, not active, with a decoy beam on, and explosions
 * are passed over. With no attacker, or a pilot without a craft, nothing
 * happens; the targeting computer being out refuses the target. */
static const struct target_case g_e_cases[] = {
	E_ROW(e_no_craft, T_NOCRAFT, -1, 0, 0),
	E_ROW(e_nobody, 0, -1, 0, 0),
	E_ROW(e_ai_12, 0, -1, PILOT_CRAFT, 12),
	E_ROW(e_ai_23, 0, -1, PILOT_CRAFT, 23),
	E_ROW(e_ai_mode, 0, -1, PILOT_CRAFT, 5),
	E_ROW(e_ai_aim, 0, -1, MATE_CRAFT, 12),
	E_ROW(e_ai_dead, T_AI_DEAD, -1, PILOT_CRAFT, 12),
	E_ROW(e_ai_inactive, T_AI_KIND, -1, PILOT_CRAFT, 12),
	E_ROW(e_recent, T_RECENT6, -1, 0, 0),
	E_ROW(e_stale, T_RECENT6 | T_STALE, -1, 0, 0),
	E_ROW(e_recent_mate, T_RECENT_MATE, -1, 0, 0),
	E_ROW(e_aimed, T_AIMS_AT_ME6, -1, 0, 0),
	E_ROW(e_aimed_mate, T_AIMS_AT_ME_MATE, -1, 0, 0),
	E_ROW(e_decoy, T_RECENT6 | T_DECOY6, -1, 0, 0),
	E_ROW(e_explosion, T_RECENT6 | T_EXPLOSION6, -1, 0, 0),
	E_ROW(e_order, T_RECENT6, -1, PILOT_CRAFT, 12),
	E_ROW(e_wrap, 0, FOE_NEAR, PILOT_CRAFT, 12),
	E_ROW(e_comp_out, T_COMP_OUT, -1, PILOT_CRAFT, 12),
	E_ROW(e_map, T_MAP, -1, PILOT_CRAFT, 12),
};

static void check_target_attacker(void)
{
	for (size_t k = 0; k < sizeof g_e_cases / sizeof g_e_cases[0]; ++k) {
		run_target_case(&g_e_cases[k]);
	}
}

static const char *const k_i_none[] = {
	/* recorded i_none */
	"flight.action_key slot=0 key=105 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_i_nearest[] = {
	/* recorded i_nearest */
	"flight.action_key slot=0 key=105 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=13 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=13 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=13 ai_target=0",
	NULL,
};
static const char *const k_i_tie[] = {
	/* recorded i_tie */
	"flight.action_key slot=0 key=105 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=12 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=12 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=12 ai_target=0",
	NULL,
};
static const char *const k_i_skips[] = {
	/* recorded i_skips */
	"flight.action_key slot=0 key=105 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=15 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=15 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=15 ai_target=0",
	NULL,
};
static const char *const k_i_friend[] = {
	/* recorded i_friend */
	"flight.action_key slot=0 key=105 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=13 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=13 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=13 ai_target=0",
	NULL,
};
static const char *const k_i_you_first[] = {
	/* recorded i_you_first */
	"flight.action_key slot=0 key=105 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=13 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=13 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=13 ai_target=0",
	NULL,
};
static const char *const k_i_foe[] = {
	/* recorded i_foe */
	"flight.action_key slot=0 key=105 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_i_unaimed[] = {
	/* recorded i_unaimed */
	"flight.action_key slot=0 key=105 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_i_friend_family[] = {
	/* recorded i_friend_family */
	"flight.action_key slot=0 key=105 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_i_friend_nomobj[] = {
	/* recorded i_friend_nomobj */
	"flight.action_key slot=0 key=105 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_i_friend_skips[] = {
	/* recorded i_friend_skips */
	"flight.action_key slot=0 key=105 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=15 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=15 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=15 ai_target=0",
	NULL,
};
static const char *const k_i_ally_theirs[] = {
	/* recorded i_ally_theirs */
	"flight.action_key slot=0 key=105 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=12 previous=-1 map=0 predicted=0",
	"player.target_component slot=0 target=12 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=12 ai_target=0",
	NULL,
};
static const char *const k_i_ally_mine[] = {
	/* recorded i_ally_mine */
	"flight.action_key slot=0 key=105 object=2 map=0 hyper=0 request=0 predicted=0",
	NULL,
};
static const char *const k_i_comp_out[] = {
	/* recorded i_comp_out */
	"flight.action_key slot=0 key=105 object=2 map=0 hyper=0 request=0 predicted=0",
	"player.target_refused slot=0 target=12 predicted=0",
	"msg.message_built message=86 type=1 length=7 sender=0 voice=0 clock=\"0:00:10\" logged=1 index=1 total=1 wrapped=0 predicted=0",
	"msg.message_queued message=86 type=1 length=7 args=\"96,87,324,331\" waiting=1 tick=0",
	NULL,
};
static const char *const k_i_map[] = {
	/* recorded i_map */
	"flight.action_key slot=0 key=105 object=2 map=1 hyper=0 request=0 predicted=0",
	"player.target_changed slot=0 target=12 previous=-1 map=1 predicted=0",
	"player.target_component slot=0 target=12 component=0 predicted=0",
	"world.track tick=0 object=2 signature=0 type=1 fg=0 player=0 team=0 x=5600 y=6600 z=3200 yaw=0 pitch=0 roll=0 speed=0 throttle=0 state=0 hull=0 hull_max=0 front=100 rear=100 target=12 ai_target=0",
	NULL,
};

#define WH(genus, aim, dist) {SHOT_WARHEAD, genus, aim, dist}
#define PLAYER_SHOT CRAFT_GENUS_PLAYER_PROJECTILE
#define OTHER_SHOT CRAFT_GENUS_OTHER_PROJECTILE

/* Recorded as the code does it: i_ally_mine, a warhead aimed at a team the
 * pilot's team lists as allied, is not targeted, while i_ally_theirs, a team
 * that lists the pilot's team, is: I reads the alliance the other way round
 * from Shift+P, P and E (flight.c 5996-5998).
 *
 * I targets the nearest warhead aimed at the pilot (the lower slot on a tie,
 * either shot genus); empty slots, craft, cannon shots and unguided shots are
 * not warheads. When none is aimed at the pilot it takes the nearest aimed at
 * a teammate or at a craft of an allied team, and ignores one aimed at an
 * enemy, at nothing, or at something that is not a craft. A warhead aimed
 * at the pilot wins over a nearer one aimed at a friend. The targeting
 * computer being out refuses the target. */
static const struct target_case g_i_cases[] = {
	I_ROW(i_none, 0, {0, 0, 0, 0}),
	I_ROW(i_nearest, 0, WH(PLAYER_SHOT, PILOT_CRAFT, 800),
	      WH(PLAYER_SHOT, PILOT_CRAFT, 100)),
	I_ROW(i_tie, 0, WH(PLAYER_SHOT, PILOT_CRAFT, 100),
	      WH(PLAYER_SHOT, PILOT_CRAFT, 100)),
	I_ROW(i_skips, 0, WH(CRAFT_GENUS_STARSHIP, PILOT_CRAFT, 10),
	      {SHOT_CANNON, PLAYER_SHOT, PILOT_CRAFT, 10},
	      WH(PLAYER_SHOT, NO_GUIDANCE, 10),
	      WH(OTHER_SHOT, PILOT_CRAFT, 500)),
	I_ROW(i_friend, 0, WH(PLAYER_SHOT, MATE_CRAFT, 800),
	      WH(PLAYER_SHOT, MATE_CRAFT, 100)),
	I_ROW(i_you_first, 0, WH(PLAYER_SHOT, MATE_CRAFT, 10),
	      WH(PLAYER_SHOT, PILOT_CRAFT, 900)),
	I_ROW(i_foe, 0, WH(PLAYER_SHOT, ENEMY, 100)),
	I_ROW(i_unaimed, 0, WH(PLAYER_SHOT, UINT16_MAX, 100)),
	I_ROW(i_friend_family, T_MATE_FAMILY, WH(PLAYER_SHOT, MATE_CRAFT, 100)),
	I_ROW(i_friend_nomobj, T_MATE_NOMOBJ, WH(PLAYER_SHOT, MATE_CRAFT, 100)),
	I_ROW(i_friend_skips, 0, {0, PLAYER_SHOT, MATE_CRAFT, 10},
	      {SHOT_CANNON, PLAYER_SHOT, MATE_CRAFT, 10},
	      WH(PLAYER_SHOT, NO_GUIDANCE, 10),
	      WH(OTHER_SHOT, MATE_CRAFT, 500)),
	I_ROW(i_ally_theirs, T_ALLY_THEIRS, WH(PLAYER_SHOT, ENEMY, 100)),
	I_ROW(i_ally_mine, T_ALLY_MINE, WH(PLAYER_SHOT, ENEMY, 100)),
	I_ROW(i_comp_out, T_COMP_OUT, WH(PLAYER_SHOT, PILOT_CRAFT, 100)),
	I_ROW(i_map, T_MAP, WH(PLAYER_SHOT, PILOT_CRAFT, 100)),
};

static void check_target_warhead(void)
{
	for (size_t k = 0; k < sizeof g_i_cases / sizeof g_i_cases[0]; ++k) {
		run_target_case(&g_i_cases[k]);
	}
}

int main(int argc, char **argv)
{
	fail_after_seconds(20);
	log_lines_start();
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
	check_map_c_centers_on_target();
	check_map_z_over_target();
	check_map_z_distance_to_followed();
	check_map_z_behind_target();
	check_map_z_nothing_to_follow();
	check_map_aim_keys();
	check_cover_request_names_attacker();
	check_cover_orders_wingman();
	check_cover_without_attacker();
	check_cover_who_is_asked();
	check_cover_announced_to_local();
	check_cover_radio_out();
	check_weapon_cycle();
	check_weapon_skips_empty_launchers();
	check_weapon_launchers_only();
	check_weapon_launcher_flags();
	check_weapon_system_out();
	check_link_cycle();
	check_link_single_slot_group();
	check_link_message_local_only();
	check_link_warheads();
	check_eject();
	check_eject_in_replay();
	check_throttle_steps();
	check_look_keys();
	check_look_keys_outside_cockpit();
	check_look_centre_key();
	check_look_snap_key();
	check_period_toggles_hud_only();
	check_target_camera();
	check_order_radio_gate();
	check_order_attack();
	check_order_ignore();
	check_order_wait();
	check_order_home();
	check_order_go();
	check_order_resupply();
	check_order_wait_overrides();
	check_order_go_starship();
	check_order_home_already_going();
	check_order_ask_param();
	check_order_state_changes();
	check_target_next_player();
	check_target_nearest_hostile();
	check_target_attacker();
	check_target_warhead();
	return 0;
}
