/* Tests for xvt/flight/ai_boarding.c, the dock of a computer craft on a
 * boarding order, on worlds this file builds: no game data is read. Each rule
 * is checked against the 1997 board maneuver it was ported from (align,
 * transfer by order, the shared tail, separate, and the abort order); the
 * three 1997 faults the port does not keep (the tenth docked craft, the
 * Give cockpit restore on one machine, Destroy's countdown) are checked gone.
 * The craft's state is its maneuver mode, phase and timers, so a test puts a
 * craft in a phase by setting those, as the engine's save would. */
#include <SDL3/SDL_log.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/object_type.h"
#include "xvt/assets/opt_model.h"
#include "xvt/audio/fsfx.h"
#include "xvt/flight/ai_boarding.h"
#include "xvt/flight/ai_books.h"
#include "xvt/flight/ai_brain.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_geometry.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/laser.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/util/game_rand.h"
#include "xvt_runtime/ai/ai_wire.h"
#include "xvt_runtime/log/log.h"

enum {
	SLOT_COUNT = 14,
	GROUP_COUNT = 6,
	BOARDER = 1,
	TARGET = 2,
	BOARD_GROUP = 2,
	TARGET_GROUP = 3,
	/* Models: the boarder's and the target's, each with docking offsets
	 * that tell the branches of the offset rule apart. */
	BOARDER_MODEL = 1,
	TARGET_MODEL = 2,
	TEST_TICK = 0x2000,
	/* Docking offsets: the target's small and big, the boarder's small
	 * and big. */
	FROM_SMALL = 400,
	FROM_BIG = 1000,
	TO_SMALL = 70,
	TO_BIG = 300,
	DOCK_FORWARD = 90,
	/* A player's slot that is not this machine's, and the machine's. */
	REMOTE_PLAYER = 3,
	LOCAL_PLAYER = 0,
};

static struct object_record g_objects[SLOT_COUNT];
static struct mobile_object g_mobiles[SLOT_COUNT];
static struct craft_data g_craft[SLOT_COUNT];
static char g_test_message[] = "\003message";

/* The linker hands the cockpit redraw, and the four calls that speak, play a
 * sound and print a message, to the functions below: each is counted or
 * written down. kind 1 is a tactical officer line (id, the craft it is said
 * for), 2 a sound (id, the player), 3 a craft message (id, the craft), 4 an
 * in-flight message (id, the player, with the sender's iff). */
enum {
	EVENT_VOICE = 1,
	EVENT_SOUND = 2,
	EVENT_CRAFT_MESSAGE = 3,
	EVENT_MESSAGE = 4,
	EVENT_CAPACITY = 64,
};

struct test_event {
	int kind;
	int id;
	int object;
	int sender_iff;
};

static struct test_event g_events[EVENT_CAPACITY];
static int g_event_count;
static int g_redraws;
static int g_redraw_player;

static void note_event(int kind, int id, int object)
{
	if (g_event_count < EVENT_CAPACITY) {
		g_events[g_event_count].kind = kind;
		g_events[g_event_count].id = id;
		g_events[g_event_count].object = object;
		g_events[g_event_count].sender_iff = g_msg_sender_iff;
		++g_event_count;
	}
}

static int events_of(int kind, int id, int object)
{
	int found = 0;
	for (int event = 0; event < g_event_count; ++event) {
		found += g_events[event].kind == kind &&
			 g_events[event].id == id &&
			 (object < 0 || g_events[event].object == object);
	}
	return found;
}

void __wrap_hud_rebuild_display_for_view_state(int hud_view_state,
					       int player_idx);
int __wrap_fsfx_speak_tactical_officer_event(int voice_category, int message_id,
					     int obj_idx, uint16_t probability);
int __wrap_fsfx_play_sound(unsigned int sound_id, int emitter_obj_idx,
			   int player_idx);
void __wrap_msg_emit_craft_message(uint16_t obj_idx,
				   const struct craft_data *craft,
				   int16_t msg_template_id);
void __wrap_msg_emit_in_flight_message(in_flight_message_id message_id,
				       int player_idx);

/* Leaves g_cur_craft on the craft of the target, as the 1997 redraw could
 * leave it on another. */
void __wrap_hud_rebuild_display_for_view_state(int hud_view_state,
					       int player_idx)
{
	(void)hud_view_state;
	++g_redraws;
	g_redraw_player = player_idx;
	g_cur_craft = &g_craft[TARGET];
}

int __wrap_fsfx_speak_tactical_officer_event(int voice_category, int message_id,
					     int obj_idx, uint16_t probability)
{
	(void)voice_category;
	(void)probability;
	note_event(EVENT_VOICE, message_id, obj_idx);
	return 0;
}

int __wrap_fsfx_play_sound(unsigned int sound_id, int emitter_obj_idx,
			   int player_idx)
{
	note_event(EVENT_SOUND, (int)sound_id, player_idx);
	/* The emitter, -1 for none, goes where a message's sender iff does. */
	if (g_event_count > 0 &&
	    g_events[g_event_count - 1].kind == EVENT_SOUND) {
		g_events[g_event_count - 1].sender_iff = emitter_obj_idx;
	}
	return 0;
}

void __wrap_msg_emit_craft_message(uint16_t obj_idx,
				   const struct craft_data *craft,
				   int16_t msg_template_id)
{
	(void)craft;
	if (g_flight_sim_side_effects_suppressed != 0) {
		return;
	}
	note_event(EVENT_CRAFT_MESSAGE, msg_template_id, obj_idx);
}

void __wrap_msg_emit_in_flight_message(in_flight_message_id message_id,
				       int player_idx)
{
	if (g_flight_sim_side_effects_suppressed != 0 ||
	    player_idx != g_local_player) {
		return;
	}
	note_event(EVENT_MESSAGE, (int)message_id, player_idx);
}

enum {
	LOG_LINE_CAPACITY = 128,
	LOG_LINE_SIZE = 240,
};

static char g_log_lines[LOG_LINE_CAPACITY][LOG_LINE_SIZE];
static int g_log_line_count;

static void catch_line(void *userdata, int category, SDL_LogPriority priority,
		       const char *message)
{
	static const char prefix[] = "xvt: ";
	(void)userdata;
	(void)category;
	(void)priority;
	if (g_log_line_count >= LOG_LINE_CAPACITY) {
		return;
	}
	if (strncmp(message, prefix, sizeof prefix - 1) == 0) {
		message += sizeof prefix - 1;
	}
	snprintf(g_log_lines[g_log_line_count++], LOG_LINE_SIZE, "%s", message);
}

static int lines_starting(const char *start)
{
	int found = 0;
	for (int line = 0; line < g_log_line_count; ++line) {
		found += strncmp(g_log_lines[line], start, strlen(start)) == 0;
	}
	return found;
}

static unsigned int count_of(int group, int outcome)
{
	return g_mission_fg_stats[group].outcome_count[outcome];
}

/* Empty slots with their mobile and craft records, every slot in the active
 * region, GROUP_COUNT groups whose first order is Give with a dock of 2
 * units, the models' docking offsets, the clock at TEST_TICK and this
 * machine's player slot LOCAL_PLAYER flying nothing. */
static void fresh_world(void)
{
	memset(g_objects, 0, sizeof g_objects);
	memset(g_mobiles, 0, sizeof g_mobiles);
	memset(g_craft, 0, sizeof g_craft);
	memset(g_mission_flight_groups, 0, sizeof g_mission_flight_groups);
	memset(g_mission_fg_stats, 0,
	       GROUP_COUNT * sizeof g_mission_fg_stats[0]);
	memset(&g_flight_mission_state, 0, sizeof g_flight_mission_state);
	memset(g_players, 0, sizeof g_players);
	for (int player = 0; player < 8; ++player) {
		g_players[player].object_index = -1;
		g_players[player].current_target_object_idx = -1;
	}
	g_local_player = LOCAL_PLAYER;
	g_mission_header.num_flight_groups = GROUP_COUNT;
	g_object_table = g_objects;
	g_mobile_object_pool_base = g_mobiles;
	g_craft_data_pool_base = g_craft;
	g_craft_data_pool_capacity = SLOT_COUNT;
	g_region_main_object_slot_end = SLOT_COUNT;
	g_region_static_object_slot_count = 0;
	g_local_transient_slot_start = 0;
	g_local_debris_slot_end = 0;
	g_mobile_object_char_data_count = 0;
	g_projectile_object_slots_total = 0;
	for (int slot = 0; slot < SLOT_COUNT; ++slot) {
		g_objects[slot].mobj = &g_mobiles[slot];
		g_objects[slot].player_owner_idx = -1;
		g_mobiles[slot].p_craft = &g_craft[slot];
	}
	for (int group = 0; group < GROUP_COUNT; ++group) {
		g_mission_flight_groups[group].player_owner_idx = -1;
		g_mission_flight_groups[group].fg.orders[0].order =
			MISSION_ORDER_BOARD_AND_GIVE_CARGO;
		g_mission_flight_groups[group].fg.orders[0].variable1 = 2;
		g_mission_flight_groups[group].fg.orders[0].variable2 = 3;
		g_mission_flight_groups[group].fg.team = (uint8_t)group;
	}
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = SLOT_COUNT;
	g_game_time = TEST_TICK;
	g_flight_sim_side_effects_suppressed = 0;
	memset(&g_model_defs[BOARDER_MODEL], 0, sizeof g_model_defs[0]);
	memset(&g_model_defs[TARGET_MODEL], 0, sizeof g_model_defs[0]);
	static char name_long[] = "Craft";
	for (int model = BOARDER_MODEL; model <= TARGET_MODEL; ++model) {
		strcpy(g_model_defs[model].name, "Craft");
		g_model_defs[model].name_long = name_long;
	}
	g_model_defs[TARGET_MODEL].dock_from_up[0] = FROM_SMALL;
	g_model_defs[TARGET_MODEL].dock_from_up[1] = FROM_BIG;
	g_model_defs[TARGET_MODEL].dock_forward = DOCK_FORWARD;
	g_model_defs[BOARDER_MODEL].dock_to_up[0] = TO_SMALL;
	g_model_defs[BOARDER_MODEL].dock_to_up[1] = TO_BIG;
	for (size_t id = 0; id < sizeof g_str_in_flight_messages /
					 sizeof g_str_in_flight_messages[0];
	     ++id) {
		g_str_in_flight_messages[id] = g_test_message;
	}
	g_log_line_count = 0;
	g_event_count = 0;
	g_redraws = 0;
	ai_brain_set_exchange(NULL);
}

/* Makes slot a live computer craft of group: a fighter of the given model,
 * active, working, on team group and iff group, alone and carrying nothing. */
static struct craft_data *make_craft(int slot, int group, int model)
{
	g_objects[slot].object_type = 5;
	g_objects[slot].object_signature = (uint16_t)(0x100 + slot);
	g_objects[slot].flight_group_idx = (uint16_t)group;
	g_objects[slot].genus_id = 1;
	/* Axes the rotations turn by: side to -x, forward to -y, up to -z. */
	g_mobiles[slot].cached_side_x = -32768;
	g_mobiles[slot].cached_fwd_y = -32768;
	g_mobiles[slot].cached_up_z = -32768;
	g_mobiles[slot].team = (uint8_t)group;
	g_mobiles[slot].iff = (uint16_t)group;
	struct craft_data *craft = &g_craft[slot];
	craft->object_kind = CRAFT_OBJECT_KIND_ACTIVE;
	craft->leader_obj_idx = UINT8_MAX;
	craft->model_index = (uint16_t)model;
	craft->system_flags = CRAFT_SUBSYSTEM_FLAGS_ALL;
	craft->working_subsystems = CRAFT_SUBSYSTEM_FLAGS_ALL;
	craft->carried_object_index = UINT16_MAX;
	craft->carrier_obj_idx = UINT16_MAX;
	craft->craft_ordinal = (uint8_t)(slot + 1);
	craft->ai_controller.target_obj_idx = UINT16_MAX;
	craft->ai_controller.candidate_target_idx = UINT16_MAX;
	return craft;
}

/* The boarder in slot BOARDER and its target in slot TARGET, both at the
 * origin, the boarder aimed at the target. */
static void boarder_and_target(void)
{
	fresh_world();
	make_craft(BOARDER, BOARD_GROUP, BOARDER_MODEL);
	make_craft(TARGET, TARGET_GROUP, TARGET_MODEL);
	g_craft[BOARDER].ai_controller.target_obj_idx = TARGET;
	g_craft[BOARDER].ai_controller.target_signature =
		g_objects[TARGET].object_signature;
	g_craft[BOARDER].ai_controller.has_live_target = 1;
}

static struct ai_controller *boarder_controller(void)
{
	return &g_craft[BOARDER].ai_controller;
}

/* Puts the boarder in a phase of the dock with its timers set. */
static void in_phase(int phase, int timer, int secondary)
{
	boarder_controller()->maneuver_mode = AI_MANEUVER_MODE_BOARD;
	boarder_controller()->maneuver_phase = (uint8_t)phase;
	boarder_controller()->maneuver_timer = timer;
	boarder_controller()->secondary_maneuver_timer = (int16_t)secondary;
}

static struct ai_wire_craft_view view_of(int slot)
{
	struct ai_wire_craft_view view;
	memset(&view, 0xEE, sizeof view);
	ai_books_view_craft((uint16_t)slot, &view);
	return view;
}

/* Where the boarder docks on the target, by the align rule: the push the
 * hatch point at up on the target's axes gives, as the distance left. */
static void expected_push(int up, int push[3])
{
	flight_geometry_local_to_world_i16(&g_objects[TARGET], 0, up,
					   DOCK_FORWARD);
	push[0] = g_rotated_x + g_objects[TARGET].world_x -
		  g_objects[BOARDER].world_x;
	push[1] = g_rotated_y + g_objects[TARGET].world_y -
		  g_objects[BOARDER].world_y;
	push[2] = g_rotated_z + g_objects[TARGET].world_z -
		  g_objects[BOARDER].world_z;
}

/* Puts the boarder where the push left to the docking point for up is
 * (n, 0, 0): the push is the hatch point less the boarder's position. */
static void push_vector_left(int up, int x, int y, int z)
{
	int push[3];
	g_objects[BOARDER].world_x = 0;
	g_objects[BOARDER].world_y = 0;
	g_objects[BOARDER].world_z = 0;
	expected_push(up, push);
	g_objects[BOARDER].world_x = push[0] - x;
	g_objects[BOARDER].world_y = push[1] - y;
	g_objects[BOARDER].world_z = push[2] - z;
}

static void push_left(int up, int n) { push_vector_left(up, n, 0, 0); }

static void check_phase_and_mode(void)
{
	boarder_and_target();
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER), AI_BOARDING_PHASE_NONE);
	/* A phase counts only while the mode is BOARD. */
	boarder_controller()->maneuver_phase = 2;
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER), AI_BOARDING_PHASE_NONE);
	boarder_controller()->maneuver_mode = AI_MANEUVER_MODE_BOARD;
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER),
			  AI_BOARDING_PHASE_TRANSFER);
	boarder_controller()->maneuver_phase = 3;
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER),
			  AI_BOARDING_PHASE_SEPARATE);
	/* A phase past 3 is no dock. */
	boarder_controller()->maneuver_phase = 4;
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER), AI_BOARDING_PHASE_NONE);
}

/* The mode follows the activity on a boarding order and the dock. */
static void check_sync_mode(void)
{
	boarder_and_target();
	ai_boarding_sync_mode(BOARDER);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_mode,
			  AI_MANEUVER_MODE_NULL);
	/* BOARDING on a boarding order: mode BOARD, phase 0. */
	boarder_controller()->activity = AI_ACTIVITY_BOARDING;
	ai_boarding_sync_mode(BOARDER);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_mode,
			  AI_MANEUVER_MODE_BOARD);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_phase, 0);
	/* The activity ends: back to NULL. */
	boarder_controller()->activity = AI_ACTIVITY_NONE;
	ai_boarding_sync_mode(BOARDER);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_mode,
			  AI_MANEUVER_MODE_NULL);
	/* BOARDING on an order that boards nothing is not a dock. */
	boarder_controller()->activity = AI_ACTIVITY_BOARDING;
	g_mission_flight_groups[BOARD_GROUP].fg.orders[0].order =
		MISSION_ORDER_ATTACK;
	ai_boarding_sync_mode(BOARDER);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_mode,
			  AI_MANEUVER_MODE_NULL);
	/* A dock keeps the mode whatever the activity. */
	in_phase(2, 5, 0);
	boarder_controller()->activity = AI_ACTIVITY_NONE;
	ai_boarding_sync_mode(BOARDER);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_mode,
			  AI_MANEUVER_MODE_BOARD);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_phase, 2);
	/* Another mode is left alone. */
	boarder_and_target();
	boarder_controller()->maneuver_mode = AI_MANEUVER_MODE_ATTACK;
	ai_boarding_sync_mode(BOARDER);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_mode,
			  AI_MANEUVER_MODE_ATTACK);
	/* A craft that left the battle is not touched, though no activity or
	 * dock keeps its mode. */
	boarder_and_target();
	boarder_controller()->maneuver_mode = AI_MANEUVER_MODE_BOARD;
	g_objects[BOARDER].object_type = 0;
	ai_boarding_sync_mode(BOARDER);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_mode,
			  AI_MANEUVER_MODE_BOARD);
}

/* The board action starts a dock only at the board point, for the craft's
 * own target, on a boarding order, when it does not already dock; it sets
 * throttle 0, phase align, mode BOARD and clears the timers and the push. */
static void check_start_and_stop(void)
{
	boarder_and_target();
	g_craft[BOARDER].throttle_speed = 0x8000;
	g_craft[BOARDER].push_accum_x = 5;
	boarder_controller()->maneuver_timer = 77;
	ai_boarding_start(BOARDER, TARGET);
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER), AI_BOARDING_PHASE_ALIGN);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_mode,
			  AI_MANEUVER_MODE_BOARD);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].throttle_speed, 0);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].push_accum_x, 0);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_timer, 0);
	XVT_ASSERT_INT_EQ(
		lines_starting("ai.craft_board object=1 target=2 started=1"),
		1);
	/* Already docking: declined, the dock is not restarted. */
	in_phase(2, 55, 9);
	g_craft[BOARDER].throttle_speed = 0x4000;
	ai_boarding_start(BOARDER, TARGET);
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER),
			  AI_BOARDING_PHASE_TRANSFER);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_timer, 55);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].throttle_speed, 0x4000);
	/* Not the craft's AI target. */
	boarder_and_target();
	make_craft(5, TARGET_GROUP, TARGET_MODEL);
	ai_boarding_start(BOARDER, 5);
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER), AI_BOARDING_PHASE_NONE);
	XVT_ASSERT_INT_EQ(
		lines_starting("ai.craft_board object=1 target=5 started=0"),
		1);
	/* Not at the board point: 5,000 units from it. */
	g_objects[BOARDER].world_x = 5000;
	ai_boarding_start(BOARDER, TARGET);
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER), AI_BOARDING_PHASE_NONE);
	g_objects[BOARDER].world_x = 0;
	/* An order that boards nothing. */
	g_mission_flight_groups[BOARD_GROUP].fg.orders[0].order =
		MISSION_ORDER_ATTACK;
	ai_boarding_start(BOARDER, TARGET);
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER), AI_BOARDING_PHASE_NONE);
	/* A player's craft that moves is not docked with. */
	g_mission_flight_groups[BOARD_GROUP].fg.orders[0].order =
		MISSION_ORDER_BOARD_AND_GIVE_CARGO;
	g_objects[TARGET].player_owner_idx = REMOTE_PLAYER;
	g_mobiles[TARGET].speed = 40;
	ai_boarding_start(BOARDER, TARGET);
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER), AI_BOARDING_PHASE_NONE);
	g_mobiles[TARGET].speed = 0;
	ai_boarding_start(BOARDER, TARGET);
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER), AI_BOARDING_PHASE_ALIGN);
}

/* A stop returns phase 0 with no progress in phases align and transfer
 * only, and keeps the craft's target. */
static void check_stop(void)
{
	for (int phase = 0; phase <= 3; ++phase) {
		boarder_and_target();
		in_phase(phase, 600, 40);
		g_craft[BOARDER].push_accum_y = 90;
		ai_boarding_stop(BOARDER);
		int stopped = phase == 1 || phase == 2;
		XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_phase,
				  stopped ? 0 : phase);
		XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_timer,
				  stopped ? 0 : 600);
		XVT_ASSERT_INT_EQ(
			boarder_controller()->secondary_maneuver_timer,
			stopped ? 0 : 40);
		XVT_ASSERT_INT_EQ(g_craft[BOARDER].push_accum_y,
				  stopped ? 0 : 90);
		XVT_ASSERT_INT_EQ(boarder_controller()->target_obj_idx, TARGET);
	}
}

/* Align pushes the boarder onto the target's docking point: the offset along
 * the target's up axis is its small dock_from_up less the boarder's small
 * dock_to_up when the target is a small craft; its small from_up less the
 * boarder's big to_up when only the boarder is small; the big from_up less
 * the big to_up when neither is. */
static void check_align_offsets(void)
{
	static const struct {
		int target_genus;
		int boarder_genus;
		int up;
	} cases[] = {
		{1, 1, FROM_SMALL - TO_SMALL}, {0, 5, FROM_SMALL - TO_SMALL},
		{5, 1, FROM_SMALL - TO_BIG},   {5, 0, FROM_SMALL - TO_BIG},
		{5, 5, FROM_BIG - TO_BIG},     {2, 2, FROM_BIG - TO_BIG}};
	for (size_t row = 0; row < sizeof cases / sizeof cases[0]; ++row) {
		boarder_and_target();
		g_objects[TARGET].genus_id = (uint8_t)cases[row].target_genus;
		g_objects[BOARDER].genus_id = (uint8_t)cases[row].boarder_genus;
		g_objects[TARGET].world_x = 3000;
		g_objects[TARGET].world_y = -2000;
		g_objects[TARGET].world_z = 1000;
		g_objects[BOARDER].world_x = 111;
		g_objects[BOARDER].world_y = -222;
		g_objects[BOARDER].world_z = 333;
		in_phase(1, 0, 0);
		ai_boarding_step(BOARDER);
		int push[3];
		expected_push(cases[row].up, push);
		XVT_ASSERT_INT_EQ(g_craft[BOARDER].push_accum_x, push[0]);
		XVT_ASSERT_INT_EQ(g_craft[BOARDER].push_accum_y, push[1]);
		XVT_ASSERT_INT_EQ(g_craft[BOARDER].push_accum_z, push[2]);
		XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER),
				  AI_BOARDING_PHASE_ALIGN);
	}
}

/* Align turns the boarder to the target's roll, heading and pitch at half
 * rate, each only where it differs. */
static void check_align_turns(void)
{
	boarder_and_target();
	g_objects[TARGET].roll = 0x1111;
	g_objects[TARGET].yaw = 0x2222;
	g_objects[TARGET].pitch = 0x3333;
	g_objects[BOARDER].pitch = 0x1000;
	g_craft[BOARDER].pitch = 0x1000;
	g_objects[TARGET].world_x = 9000;
	g_craft[BOARDER].ai_flight.pitch_through_loop = 1;
	in_phase(1, 0, 0);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].ai_flight.roll_state, 1);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].ai_flight.roll_step, 0x8000);
	XVT_ASSERT_INT_EQ(boarder_controller()->target_roll, 0x1111);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].ai_flight.turn_state, 2);
	XVT_ASSERT_INT_EQ((uint16_t)g_craft[BOARDER].ai_flight.turn_step,
			  0x8000);
	XVT_ASSERT_INT_EQ(boarder_controller()->target_xy_angle, 0x2222);
	XVT_ASSERT_INT_EQ(boarder_controller()->target_z_angle, 0x3333);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].ai_flight.pitch_step_scale, 0x8000);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].ai_flight.pitch_state, 2);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].ai_flight.pitch_through_loop, 0);
	/* A pitch above the target's lowers. */
	g_objects[BOARDER].pitch = 0x7000;
	g_craft[BOARDER].pitch = 0x7000;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].ai_flight.pitch_state, 1);
	/* A craft record whose pitch is already the target's is not raised. */
	g_objects[BOARDER].pitch = 0x1000;
	g_craft[BOARDER].pitch = 0x3333;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].ai_flight.pitch_state, 1);
	/* Where they already agree nothing is set. */
	boarder_and_target();
	g_objects[TARGET].world_x = 9000;
	in_phase(1, 0, 0);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].ai_flight.roll_state, 0);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].ai_flight.turn_state, 0);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].ai_flight.pitch_state, 0);
}

/* The align phase ends when the three push distances left sum to less than
 * 16: at 16 it does not end, at 15 it does. Ending it zeroes the push,
 * begins the transfer with variable1 x 1,180 ticks and a reload step of 236,
 * and counts the boarder's group failed once and the target's completed once
 * (a second dock of the same two counts nothing more). */
static void check_align_end(void)
{
	const int up = FROM_SMALL - TO_SMALL;
	boarder_and_target();
	in_phase(1, 0, 0);
	push_left(up, 16);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER), AI_BOARDING_PHASE_ALIGN);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_timer, 0);
	XVT_ASSERT_INT_EQ(
		count_of(BOARD_GROUP, FLIGHT_GROUP_OUTCOME_FAILED_MISSION), 0);
	push_left(up, 15);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER),
			  AI_BOARDING_PHASE_TRANSFER);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_timer, 2 * 1180);
	XVT_ASSERT_INT_EQ(boarder_controller()->secondary_maneuver_timer, 236);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].push_accum_x, 0);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].push_accum_y, 0);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].push_accum_z, 0);
	XVT_ASSERT_INT_EQ(
		count_of(BOARD_GROUP, FLIGHT_GROUP_OUTCOME_FAILED_MISSION), 1);
	XVT_ASSERT_INT_EQ(
		count_of(TARGET_GROUP, FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION),
		1);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].ai_flight.docking_accounting_done,
			  1);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].ai_flight.boarded_accounting_done, 1);
	XVT_ASSERT_INT_EQ(lines_starting("ai.boarding_started object=1"), 1);
	XVT_ASSERT_INT_EQ(lines_starting("ai.docking_counted object=1"), 1);
	in_phase(1, 0, 0);
	push_left(up, 0);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(
		count_of(BOARD_GROUP, FLIGHT_GROUP_OUTCOME_FAILED_MISSION), 1);
	XVT_ASSERT_INT_EQ(
		count_of(TARGET_GROUP, FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION),
		1);
	/* A dock of 0 units transfers at once; one of 255 is 300,900 ticks. */
	boarder_and_target();
	g_mission_flight_groups[BOARD_GROUP].fg.orders[0].variable1 = 0;
	in_phase(1, 0, 0);
	push_left(up, 0);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_timer, 0);
	boarder_and_target();
	g_mission_flight_groups[BOARD_GROUP].fg.orders[0].variable1 = 255;
	in_phase(1, 0, 0);
	push_left(up, 0);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_timer, 255 * 1180);
}

/* The boarder in the transfer phase with its timer run out, on order, aimed
 * at the target; the next step does the order's transfer and the tail. */
static void transfer_on(int order)
{
	boarder_and_target();
	g_mission_flight_groups[BOARD_GROUP].fg.orders[0].order =
		(uint8_t)order;
	in_phase(2, 0, 0);
}

static void set_cargo(struct craft_data *craft, const char *name)
{
	memset(craft->special_cargo_name, 0, sizeof craft->special_cargo_name);
	strncpy(craft->special_cargo_name, name,
		sizeof craft->special_cargo_name - 1);
}

/* Give: the cargo name goes to the target and its state is 2; the boarder's
 * own name is cleared once the order's variable2 (3) boardings are done, that
 * is when the boardings done so far are 2, and its state is 1. */
static void check_give(void)
{
	for (int done = 0; done <= 3; ++done) {
		transfer_on(MISSION_ORDER_BOARD_AND_GIVE_CARGO);
		set_cargo(&g_craft[BOARDER], "FUEL");
		set_cargo(&g_craft[TARGET], "OLD");
		boarder_controller()->order_progress.goal_progress[0] =
			(uint8_t)done;
		ai_boarding_step(BOARDER);
		XVT_ASSERT_TRUE(strcmp(g_craft[TARGET].special_cargo_name,
				       "FUEL") == 0);
		XVT_ASSERT_INT_EQ(g_craft[TARGET].boarding_state, 2);
		XVT_ASSERT_INT_EQ(g_craft[BOARDER].boarding_state, 1);
		XVT_ASSERT_INT_EQ(g_craft[BOARDER].special_cargo_name[0],
				  done >= 2 ? 0 : 'F');
	}
}

static void check_take_and_exchange(void)
{
	transfer_on(MISSION_ORDER_BOARD_AND_TAKE_CARGO);
	set_cargo(&g_craft[BOARDER], "MINE");
	set_cargo(&g_craft[TARGET], "THEIRS");
	ai_boarding_step(BOARDER);
	XVT_ASSERT_TRUE(strcmp(g_craft[BOARDER].special_cargo_name, "THEIRS") ==
			0);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].special_cargo_name[0], 0);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].boarding_state, 1);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].boarding_state, 2);
	transfer_on(MISSION_ORDER_BOARD_AND_EXCHANGE_CARGO);
	set_cargo(&g_craft[BOARDER], "MINE");
	set_cargo(&g_craft[TARGET], "THEIRS");
	ai_boarding_step(BOARDER);
	XVT_ASSERT_TRUE(strcmp(g_craft[BOARDER].special_cargo_name, "THEIRS") ==
			0);
	XVT_ASSERT_TRUE(strcmp(g_craft[TARGET].special_cargo_name, "MINE") ==
			0);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].boarding_state, 2);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].boarding_state, 2);
}

/* SS Board writes the target's state only; Repair restores its subsystems and
 * clears its damage and disabled kind. */
static void check_contact_and_repair(void)
{
	transfer_on(MISSION_ORDER_SS_BOARD);
	g_craft[BOARDER].boarding_state = 9;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].boarding_state, 2);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].boarding_state, 9);
	transfer_on(MISSION_ORDER_BOARD_TO_REPAIR);
	g_craft[BOARDER].boarding_state = 9;
	g_craft[TARGET].working_subsystems = 0;
	g_craft[TARGET].subsystem_damage = 40;
	g_craft[TARGET].object_kind = CRAFT_OBJECT_KIND_DISABLED;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].working_subsystems,
			  CRAFT_SUBSYSTEM_FLAGS_ALL);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].subsystem_damage, 0);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].object_kind,
			  CRAFT_OBJECT_KIND_ACTIVE);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].boarding_state, 3);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].boarding_state, 9);
}

static unsigned int pair_count(int team, int group)
{
	return g_flight_mission_state.runtime
		.team_fg_inspected_captured_counts[1][team][group];
}

/* Capture: a craft of another team joins the boarder's: its team and iff are
 * the boarder's, its capture mark is 0x80 and the capturing group, its group
 * counts one captured and one captured pair for the boarder's team, its
 * subsystems are restored and its damage cleared; its group's own team does
 * not change. The tail then inspects it (the iffs match now). A craft taken
 * back by its own group's team is uncounted and unmarked; one taken from
 * another captor moves the pair count to the new captor. */
static void check_capture(void)
{
	transfer_on(MISSION_ORDER_BOARD_AND_CAPTURE_CARGO);
	g_mission_flight_groups[TARGET_GROUP].fg.special_cargo_craft =
		g_craft[TARGET].craft_ordinal;
	g_craft[TARGET].working_subsystems = 0;
	g_craft[TARGET].subsystem_damage = 30;
	g_craft[TARGET].object_kind = CRAFT_OBJECT_KIND_DISABLED;
	g_craft[TARGET].last_attacker_obj_idx = 9;
	g_craft[TARGET].ai_controller.activity = AI_ACTIVITY_DISABLED;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(g_mobiles[TARGET].team, BOARD_GROUP);
	XVT_ASSERT_INT_EQ(g_mobiles[TARGET].iff, BOARD_GROUP);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].captured_by_flight_group,
			  0x80 | BOARD_GROUP);
	XVT_ASSERT_INT_EQ(count_of(TARGET_GROUP, FLIGHT_GROUP_OUTCOME_CAPTURED),
			  1);
	XVT_ASSERT_INT_EQ(pair_count(BOARD_GROUP, TARGET_GROUP), 1);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[TARGET_GROUP].fg.team,
			  TARGET_GROUP);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].working_subsystems,
			  CRAFT_SUBSYSTEM_FLAGS_ALL);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].subsystem_damage, 0);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].object_kind,
			  CRAFT_OBJECT_KIND_ACTIVE);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].last_attacker_obj_idx, UINT16_MAX);
	XVT_ASSERT_INT_EQ(
		count_of(TARGET_GROUP, FLIGHT_GROUP_OUTCOME_INSPECTED), 1);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].identified_order_by_team[BOARD_GROUP],
			  1);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[TARGET_GROUP]
			.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_CAPTURED],
		1);
	XVT_ASSERT_INT_EQ(events_of(EVENT_CRAFT_MESSAGE, 139, TARGET), 1);
	XVT_ASSERT_INT_EQ(lines_starting("ai.craft_captured object=1 target=2"),
			  1);
	XVT_ASSERT_INT_EQ(lines_starting("ai.craft_changed_side object=1"), 1);
	/* A third group's craft takes it from the first captor. */
	g_mission_flight_groups[4].fg.orders[0].order =
		MISSION_ORDER_BOARD_AND_CAPTURE_CARGO;
	make_craft(6, 4, BOARDER_MODEL);
	g_craft[6].ai_controller.target_obj_idx = TARGET;
	g_craft[6].ai_controller.target_signature =
		g_objects[TARGET].object_signature;
	g_craft[6].ai_controller.maneuver_mode = AI_MANEUVER_MODE_BOARD;
	g_craft[6].ai_controller.maneuver_phase = 2;
	ai_boarding_step(6);
	XVT_ASSERT_INT_EQ(g_mobiles[TARGET].team, 4);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].captured_by_flight_group, 0x80 | 4);
	XVT_ASSERT_INT_EQ(pair_count(BOARD_GROUP, TARGET_GROUP), 0);
	XVT_ASSERT_INT_EQ(pair_count(4, TARGET_GROUP), 1);
	XVT_ASSERT_INT_EQ(count_of(TARGET_GROUP, FLIGHT_GROUP_OUTCOME_CAPTURED),
			  1);
	/* The craft's own group's team takes it back. */
	make_craft(7, TARGET_GROUP, BOARDER_MODEL);
	g_craft[7].ai_controller.target_obj_idx = TARGET;
	g_craft[7].ai_controller.target_signature =
		g_objects[TARGET].object_signature;
	g_craft[7].ai_controller.maneuver_mode = AI_MANEUVER_MODE_BOARD;
	g_craft[7].ai_controller.maneuver_phase = 2;
	g_mission_flight_groups[TARGET_GROUP].fg.orders[0].order =
		MISSION_ORDER_BOARD_AND_CAPTURE_CARGO;
	ai_boarding_step(7);
	XVT_ASSERT_INT_EQ(g_mobiles[TARGET].team, TARGET_GROUP);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].captured_by_flight_group, 0);
	XVT_ASSERT_INT_EQ(count_of(TARGET_GROUP, FLIGHT_GROUP_OUTCOME_CAPTURED),
			  0);
	XVT_ASSERT_INT_EQ(g_mission_flight_groups[TARGET_GROUP].fg.team,
			  TARGET_GROUP);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[TARGET_GROUP]
			.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_CAPTURED],
		0);
	/* No capture message for a craft taken back: the two captures said it. */
	XVT_ASSERT_INT_EQ(events_of(EVENT_CRAFT_MESSAGE, 139, TARGET), 2);
	/* Retaken: still logged, with the mark 0. */
	XVT_ASSERT_INT_EQ(
		lines_starting(
			"ai.craft_captured object=7 target=2 team=3 captured=0"),
		1);
}

/* Pick Up: the capture's transfer, then the boarder carries the target with
 * its subsystems down. */
static void check_pick_up(void)
{
	transfer_on(MISSION_ORDER_PICK_UP);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].carried_object_index, TARGET);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].carrier_obj_idx, BOARDER);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].working_subsystems, 0);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].captured_by_flight_group,
			  0x80 | BOARD_GROUP);
	XVT_ASSERT_INT_EQ(g_mobiles[TARGET].team, BOARD_GROUP);
}

/* Destroy: the target's countdown is set to 15 to 30 seconds when none runs
 * (1997 set it only when one ran), a running one is kept, and the boarder is
 * credited; the limit is 55 units of 1,180 ticks, 64,900. */
static void check_destroy(void)
{
	g_game_rand_value_state = 0x1234;
	g_game_rand_feedback_state = 0x3A5C;
	unsigned int lowest = UINT16_MAX;
	unsigned int highest = 0;
	for (int round = 0; round < 400; ++round) {
		transfer_on(MISSION_ORDER_BOARD_AND_DESTROY_CARGO);
		g_mobiles[TARGET].lifetime_timer = 0;
		ai_boarding_step(BOARDER);
		unsigned int timer = g_mobiles[TARGET].lifetime_timer;
		XVT_ASSERT_TRUE(timer % 236 == 0);
		XVT_ASSERT_TRUE(timer >= 15 * 236 && timer <= 30 * 236);
		XVT_ASSERT_TRUE(timer <= 55 * 1180);
		lowest = timer < lowest ? timer : lowest;
		highest = timer > highest ? timer : highest;
	}
	/* Every second from 15 to 30 can come up; 400 draws reach both ends. */
	XVT_ASSERT_INT_EQ(lowest, 15 * 236);
	XVT_ASSERT_INT_EQ(highest, 30 * 236);
	transfer_on(MISSION_ORDER_BOARD_AND_DESTROY_CARGO);
	g_mobiles[TARGET].lifetime_timer = 1234;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(g_mobiles[TARGET].lifetime_timer, 1234);
	XVT_ASSERT_INT_EQ(
		lines_starting(
			"ai.target_sabotaged object=1 target=2 life=1234"),
		1);
	XVT_ASSERT_INT_EQ(lines_starting("battle.killing_blow victim=2"), 1);
	/* The target is not otherwise changed: it still belongs to its side. */
	XVT_ASSERT_INT_EQ(g_mobiles[TARGET].team, TARGET_GROUP);
}

/* The tail: one more boarding done, the signature in the docked list, DOCKED
 * counted for the first docking only, the target boarded once more and its
 * group counted boarded, an inspection for the boarder's team when the iffs
 * match (the highest number so far plus one) and none when they differ, and
 * the separation (phase 3, 2,360 ticks). */
static void check_tail(void)
{
	transfer_on(MISSION_ORDER_SS_BOARD);
	g_mobiles[TARGET].iff = g_mobiles[BOARDER].iff;
	g_craft[TARGET].identified_order_by_team[7] = 4;
	g_craft[TARGET].identified_order_by_team[1] = 6;
	boarder_controller()->order_progress.goal_progress[0] = 5;
	g_craft[TARGET].ai_flight.times_boarded = 3;
	g_players[LOCAL_PLAYER].current_target_object_idx = TARGET;
	g_hud_cached_target_object_idx = 0;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(boarder_controller()->order_progress.goal_progress[0],
			  6);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].ai_flight.docked_target_count, 1);
	XVT_ASSERT_INT_EQ(
		g_craft[BOARDER].ai_flight.docked_target_signatures[0],
		g_objects[TARGET].object_signature);
	XVT_ASSERT_INT_EQ(count_of(BOARD_GROUP, FLIGHT_GROUP_OUTCOME_DOCKED),
			  1);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].ai_flight.times_boarded, 4);
	XVT_ASSERT_INT_EQ(count_of(TARGET_GROUP, FLIGHT_GROUP_OUTCOME_BOARDED),
			  1);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].identified_order_by_team[BOARD_GROUP],
			  7);
	XVT_ASSERT_INT_EQ(
		count_of(TARGET_GROUP, FLIGHT_GROUP_OUTCOME_INSPECTED), 1);
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER),
			  AI_BOARDING_PHASE_SEPARATE);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_timer, 2360);
	XVT_ASSERT_INT_EQ(g_hud_cached_target_object_idx, -3);
	XVT_ASSERT_INT_EQ(lines_starting("ai.boarding_done object=1"), 1);
	XVT_ASSERT_INT_EQ(lines_starting("ai.boarding_finished object=1"), 1);
	/* A second boarding of the same craft: not the first docking, and the
	 * team is already inspected. */
	in_phase(2, 0, 0);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].ai_flight.docked_target_count, 2);
	XVT_ASSERT_INT_EQ(count_of(BOARD_GROUP, FLIGHT_GROUP_OUTCOME_DOCKED),
			  1);
	XVT_ASSERT_INT_EQ(
		count_of(TARGET_GROUP, FLIGHT_GROUP_OUTCOME_INSPECTED), 1);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].identified_order_by_team[BOARD_GROUP],
			  7);
	/* Another iff: no inspection. */
	transfer_on(MISSION_ORDER_SS_BOARD);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].identified_order_by_team[BOARD_GROUP],
			  0);
	XVT_ASSERT_INT_EQ(
		count_of(TARGET_GROUP, FLIGHT_GROUP_OUTCOME_INSPECTED), 0);
}

/* The special cargo craft's marks: the boarder when it is its group's cargo
 * craft (failed at the start, docked), the target when it is (completed at
 * the start, boarded, inspected). */
static void check_special_cargo_marks(void)
{
	boarder_and_target();
	g_mission_flight_groups[BOARD_GROUP].fg.special_cargo_craft =
		g_craft[BOARDER].craft_ordinal;
	g_mission_flight_groups[TARGET_GROUP].fg.special_cargo_craft =
		g_craft[TARGET].craft_ordinal;
	g_mobiles[TARGET].iff = g_mobiles[BOARDER].iff;
	g_mission_flight_groups[BOARD_GROUP].fg.orders[0].order =
		MISSION_ORDER_SS_BOARD;
	g_mission_flight_groups[BOARD_GROUP].fg.orders[0].variable1 = 0;
	in_phase(1, 0, 0);
	push_left(FROM_SMALL - TO_SMALL, 0);
	ai_boarding_step(BOARDER);
	ai_boarding_step(BOARDER);
	const uint8_t *mine =
		g_mission_fg_stats[BOARD_GROUP].special_cargo_outcome;
	const uint8_t *theirs =
		g_mission_fg_stats[TARGET_GROUP].special_cargo_outcome;
	XVT_ASSERT_INT_EQ(mine[FLIGHT_GROUP_OUTCOME_FAILED_MISSION], 1);
	XVT_ASSERT_INT_EQ(mine[FLIGHT_GROUP_OUTCOME_DOCKED], 1);
	XVT_ASSERT_INT_EQ(theirs[FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION], 1);
	XVT_ASSERT_INT_EQ(theirs[FLIGHT_GROUP_OUTCOME_BOARDED], 1);
	XVT_ASSERT_INT_EQ(theirs[FLIGHT_GROUP_OUTCOME_INSPECTED], 1);
	XVT_ASSERT_INT_EQ(mine[FLIGHT_GROUP_OUTCOME_COMPLETED_MISSION], 0);
	XVT_ASSERT_INT_EQ(theirs[FLIGHT_GROUP_OUTCOME_FAILED_MISSION], 0);
	/* Another craft than the cargo craft marks nothing. */
	boarder_and_target();
	g_mission_flight_groups[BOARD_GROUP].fg.special_cargo_craft = 99;
	g_mission_flight_groups[TARGET_GROUP].fg.special_cargo_craft = 99;
	g_mission_flight_groups[BOARD_GROUP].fg.orders[0].order =
		MISSION_ORDER_SS_BOARD;
	g_mission_flight_groups[BOARD_GROUP].fg.orders[0].variable1 = 0;
	in_phase(1, 0, 0);
	push_left(FROM_SMALL - TO_SMALL, 0);
	ai_boarding_step(BOARDER);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[BOARD_GROUP]
			.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_DOCKED],
		0);
	XVT_ASSERT_INT_EQ(
		g_mission_fg_stats[TARGET_GROUP]
			.special_cargo_outcome[FLIGHT_GROUP_OUTCOME_BOARDED],
		0);
}

/* #54: a craft that docks eleven times keeps every signature it wrote: the
 * first nine, then the tenth, and the eleventh over the tenth (1997 kept
 * nine and re-wrote the ninth slot). The view reads all ten; DOCKED is
 * counted once. */
static void check_docked_list_eleven_times(void)
{
	boarder_and_target();
	g_mission_flight_groups[BOARD_GROUP].fg.orders[0].order =
		MISSION_ORDER_SS_BOARD;
	for (int dock = 0; dock < 11; ++dock) {
		g_objects[TARGET].object_signature = (uint16_t)(0x500 + dock);
		boarder_controller()->target_signature =
			g_objects[TARGET].object_signature;
		in_phase(2, 0, 0);
		ai_boarding_step(BOARDER);
		XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER),
				  AI_BOARDING_PHASE_SEPARATE);
		in_phase(3, 0, 0);
		g_craft[BOARDER].ai_controller.target_obj_idx = TARGET;
		ai_boarding_step(BOARDER);
		XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER),
				  AI_BOARDING_PHASE_NONE);
		boarder_controller()->target_obj_idx = TARGET;
	}
	struct ai_flight_state *flight = &g_craft[BOARDER].ai_flight;
	XVT_ASSERT_INT_EQ(flight->docked_target_count, 10);
	for (int slot = 0; slot < 9; ++slot) {
		XVT_ASSERT_INT_EQ(flight->docked_target_signatures[slot],
				  0x500 + slot);
	}
	XVT_ASSERT_INT_EQ(flight->docked_target_signatures[9], 0x50A);
	struct ai_wire_craft_view view = view_of(BOARDER);
	XVT_ASSERT_INT_EQ(view.boarded_count, 10);
	XVT_ASSERT_INT_EQ(view.boarded[0], 0x500);
	XVT_ASSERT_INT_EQ(view.boarded[8], 0x508);
	XVT_ASSERT_INT_EQ(view.boarded[9], 0x50A);
	XVT_ASSERT_INT_EQ(view.order_progress[0], 11);
	XVT_ASSERT_INT_EQ(count_of(BOARD_GROUP, FLIGHT_GROUP_OUTCOME_DOCKED),
			  1);
}

/* The Give reload to a player's craft: while the timer runs, each 472 ticks
 * one more warhead in each launcher slot below its goal, every weapon
 * charged to 127, the countermeasures full, the first failed subsystem
 * repaired; a change sets the timer to 1,416; the secondary timer is set to
 * 472; with the secondary timer running, or a computer target, or another
 * order, nothing happens. */
static void check_give_reload(void)
{
	boarder_and_target();
	g_objects[TARGET].player_owner_idx = REMOTE_PLAYER;
	g_mobiles[TARGET].speed = 0;
	struct craft_data *aimed = &g_craft[TARGET];
	aimed->warhead_launcher_count = 2;
	aimed->warhead_slot_type_ids[0] = 1;
	aimed->warhead_slot_type_ids[1] = 1;
	g_model_defs[TARGET_MODEL].warhead_launcher_first_slot[0] = 0;
	g_model_defs[TARGET_MODEL].warhead_launcher_last_slot[0] = 1;
	g_model_defs[TARGET_MODEL].warhead_launcher_first_slot[1] = 2;
	g_model_defs[TARGET_MODEL].warhead_launcher_last_slot[1] = 3;
	g_model_defs[TARGET_MODEL].warhead_launcher_capacity[0] = 8;
	g_model_defs[TARGET_MODEL].warhead_launcher_capacity[1] = 6;
	g_model_defs[TARGET_MODEL].countermeasure_count = 12;
	/* Group warhead 1 loads a quarter: goal 2 in launcher 0; the second
	 * launcher always loads type 5, full: goal 6. */
	g_mission_flight_groups[TARGET_GROUP].fg.warhead = 1;
	aimed->cm_type_id = COUNTERMEASURE_TYPE_CHAFF;
	aimed->cm_ammo_count = 1;
	aimed->weapon_slots[0].ammo_count = 0;
	aimed->weapon_slots[1].ammo_count = 2;
	aimed->weapon_slots[2].ammo_count = 0;
	aimed->weapon_slots[3].ammo_count = 6;
	aimed->weapon_slots[0].laser_charge = 5;
	aimed->weapon_slots[4].ammo_count = 9;
	/* Subsystem bit 10 is no subsystem; bit 1 is not one of this craft's. */
	aimed->system_flags = 0x07FD;
	aimed->working_subsystems = (craft_subsystem_flag)(0x07FD & ~0x0604);
	in_phase(2, 800, 0);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(aimed->weapon_slots[0].ammo_count, 1);
	XVT_ASSERT_INT_EQ(aimed->weapon_slots[1].ammo_count, 2);
	XVT_ASSERT_INT_EQ(aimed->weapon_slots[2].ammo_count, 1);
	XVT_ASSERT_INT_EQ(aimed->weapon_slots[3].ammo_count, 6);
	XVT_ASSERT_INT_EQ(aimed->weapon_slots[4].ammo_count, 9);
	XVT_ASSERT_INT_EQ(aimed->weapon_slots[0].laser_charge, 127);
	XVT_ASSERT_INT_EQ(aimed->cm_ammo_count, 12);
	/* The first failed subsystem of the craft's own, 0x4, comes back: not
	 * bit 1 (the craft has no such system) and not bit 10. */
	XVT_ASSERT_INT_EQ(aimed->working_subsystems, 0x07FD & ~0x0600);
	XVT_ASSERT_INT_EQ(boarder_controller()->secondary_maneuver_timer, 472);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_timer, 1416);
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER),
			  AI_BOARDING_PHASE_TRANSFER);
	XVT_ASSERT_INT_EQ(
		lines_starting(
			"ai.reload_step object=1 target=2 slot=3 reloaded=1"),
		1);
	/* With the secondary timer running, nothing happens. */
	boarder_controller()->maneuver_timer = 800;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(aimed->weapon_slots[0].ammo_count, 1);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_timer, 800);
	/* A step that only loads a warhead keeps the dock 1,416 more ticks. */
	boarder_controller()->secondary_maneuver_timer = 0;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(aimed->weapon_slots[0].ammo_count, 2);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_timer, 1416);
	/* Step until it is full: the last step changes nothing and leaves the
	 * timer where it was. */
	for (int step = 0; step < 6; ++step) {
		boarder_controller()->secondary_maneuver_timer = 0;
		boarder_controller()->maneuver_timer = 800;
		ai_boarding_step(BOARDER);
	}
	XVT_ASSERT_INT_EQ(aimed->weapon_slots[0].ammo_count, 2);
	XVT_ASSERT_INT_EQ(aimed->weapon_slots[2].ammo_count, 6);
	/* Bits 2 and 9 are back, bit 10 (no subsystem) is not touched. */
	XVT_ASSERT_INT_EQ(aimed->working_subsystems, 0x07FD & ~0x0400);
	boarder_controller()->secondary_maneuver_timer = 0;
	boarder_controller()->maneuver_timer = 800;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_timer, 800);
	XVT_ASSERT_INT_EQ(boarder_controller()->secondary_maneuver_timer, 472);
	/* A launcher whose slots run past the weapon slots stops at the last. */
	g_model_defs[TARGET_MODEL].warhead_launcher_last_slot[1] = 40;
	g_model_defs[TARGET_MODEL].warhead_launcher_first_slot[1] = 15;
	aimed->weapon_slots[15].ammo_count = 0;
	boarder_controller()->secondary_maneuver_timer = 0;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(aimed->weapon_slots[15].ammo_count, 1);
	g_model_defs[TARGET_MODEL].warhead_launcher_first_slot[1] = 2;
	g_model_defs[TARGET_MODEL].warhead_launcher_last_slot[1] = 3;
	/* A computer target, and another order, are not reloaded. */
	aimed->weapon_slots[0].ammo_count = 0;
	g_objects[TARGET].player_owner_idx = -1;
	boarder_controller()->secondary_maneuver_timer = 0;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(aimed->weapon_slots[0].ammo_count, 0);
	g_objects[TARGET].player_owner_idx = REMOTE_PLAYER;
	g_mission_flight_groups[BOARD_GROUP].fg.orders[0].order =
		MISSION_ORDER_BOARD_AND_TAKE_CARGO;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(aimed->weapon_slots[0].ammo_count, 0);
}

/* The reload goal: the capacity at the warhead's ammo fraction, at least 1,
 * doubled when the group's status is 1 and halved when 2, at least 1 again,
 * at most 99. The warhead of group 1 is a quarter (0x4000), of 0 none. */
static void check_reload_goal(void)
{
	static const struct {
		int capacity;
		int warhead;
		int status;
		int goal;
	} cases[] = {{8, 3, 0, 8},  {8, 1, 0, 2}, {8, 0, 0, 1}, {8, 3, 1, 16},
		     {8, 3, 2, 4},  {1, 3, 2, 1}, {8, 0, 2, 1}, {99, 3, 1, 99},
		     {8, 12, 0, 1}, {8, 0, 1, 2}};
	for (size_t row = 0; row < sizeof cases / sizeof cases[0]; ++row) {
		boarder_and_target();
		g_objects[TARGET].player_owner_idx = REMOTE_PLAYER;
		struct craft_data *aimed = &g_craft[TARGET];
		aimed->warhead_launcher_count = 1;
		aimed->warhead_slot_type_ids[0] = 1;
		g_model_defs[TARGET_MODEL].warhead_launcher_first_slot[0] = 0;
		g_model_defs[TARGET_MODEL].warhead_launcher_last_slot[0] = 0;
		g_model_defs[TARGET_MODEL].warhead_launcher_capacity[0] =
			(uint8_t)cases[row].capacity;
		g_mission_flight_groups[TARGET_GROUP].fg.warhead =
			(uint8_t)cases[row].warhead;
		g_mission_flight_groups[TARGET_GROUP].fg.status1 =
			(uint8_t)cases[row].status;
		aimed->weapon_slots[0].ammo_count = 0;
		for (int load = 0; load < 120; ++load) {
			in_phase(2, 800, 0);
			ai_boarding_step(BOARDER);
		}
		XVT_ASSERT_INT_EQ(aimed->weapon_slots[0].ammo_count,
				  cases[row].goal);
	}
}

/* #203: a Give to a player's craft restores its system health and its lost
 * cockpit features on every machine, whoever the local player is, only for
 * the five craft that have them; the cockpit is drawn again on the player's
 * own machine alone, and the craft the engine works on is the boarder's
 * after. Only the 13 features a craft can have are restored, and only those it
 * has installed. */
static void check_give_restore(void)
{
	static const struct {
		int type;
		int player;
		int restored;
		unsigned int installed;
		unsigned int before;
		unsigned int after;
	} cases[] = {
		{CRAFT_SPECIES_X_WING, REMOTE_PLAYER, 1, 0x3FFF, 0x0104,
		 0x1FFF},
		{CRAFT_SPECIES_X_WING, REMOTE_PLAYER, 1, 0x0FFF, 0x0104,
		 0x0FFF},
		{CRAFT_SPECIES_Y_WING, REMOTE_PLAYER, 1, 0x1FFF, 0x0104,
		 0x1FFF},
		{CRAFT_SPECIES_A_WING, REMOTE_PLAYER, 1, 0x1FFF, 0x0104,
		 0x1FFF},
		{CRAFT_SPECIES_B_WING, REMOTE_PLAYER, 1, 0x1FFF, 0x0104,
		 0x1FFF},
		{CRAFT_SPECIES_Z_95_HEADHUNTER, REMOTE_PLAYER, 1, 0x1FFF,
		 0x0104, 0x1FFF},
		{CRAFT_SPECIES_TIE_FIGHTER, REMOTE_PLAYER, 0, 0x1FFF, 0x0104,
		 0x0104},
		{CRAFT_SPECIES_X_WING, LOCAL_PLAYER, 1, 0x1FFF, 0x0104, 0x1FFF},
		{CRAFT_SPECIES_TIE_FIGHTER, LOCAL_PLAYER, 0, 0x1FFF, 0x0104,
		 0x0104}};
	for (size_t row = 0; row < sizeof cases / sizeof cases[0]; ++row) {
		transfer_on(MISSION_ORDER_BOARD_AND_GIVE_CARGO);
		g_objects[TARGET].object_type = (uint8_t)cases[row].type;
		g_objects[TARGET].player_owner_idx = cases[row].player;
		struct craft_data *aimed = &g_craft[TARGET];
		aimed->damage_stats.installed_hud_feature_mask =
			(uint16_t)cases[row].installed;
		aimed->damage_stats.active_hud_feature_mask =
			(uint16_t)cases[row].before;
		for (int system = 0; system < DAMAGE_SYSTEM_ID_COUNT;
		     ++system) {
			aimed->system_display_slot_by_system[system] = 9;
			aimed->system_health[system] = 3;
			aimed->system_repair_seconds[system] = 7;
		}
		g_cur_craft = &g_craft[BOARDER];
		g_redraws = 0;
		g_log_line_count = 0;
		ai_boarding_step(BOARDER);
		XVT_ASSERT_INT_EQ(aimed->damage_stats.active_hud_feature_mask,
				  cases[row].after);
		/* The cockpit is drawn again on the player's own machine for
		 * the five craft, and nowhere else. */
		int local = cases[row].restored &&
			    cases[row].player == LOCAL_PLAYER;
		XVT_ASSERT_INT_EQ(g_redraws, local);
		if (g_redraws != 0) {
			XVT_ASSERT_INT_EQ(g_redraw_player, LOCAL_PLAYER);
		}
		XVT_ASSERT_INT_EQ(
			lines_starting(
				local ? "ai.hud_features_restored object=1 target=2 slot=0 mask=1fff local=1"
				      : "ai.hud_features_restored object=1 target=2 slot=3"),
			cases[row].restored);
		/* The system health is restored for every player's craft, the
		 * ten systems and no more. */
		for (int system = 0; system < CRAFT_SUBSYSTEM_COUNT; ++system) {
			XVT_ASSERT_INT_EQ(aimed->system_health[system], 100);
			XVT_ASSERT_INT_EQ(aimed->system_repair_seconds[system],
					  0);
			XVT_ASSERT_INT_EQ(
				aimed->system_display_slot_by_system[system],
				system);
		}
		XVT_ASSERT_INT_EQ(aimed->system_health[CRAFT_SUBSYSTEM_COUNT],
				  3);
		XVT_ASSERT_INT_EQ(
			aimed->system_repair_seconds[CRAFT_SUBSYSTEM_COUNT], 7);
		XVT_ASSERT_TRUE(g_cur_craft == &g_craft[BOARDER]);
	}
	/* A computer's craft keeps its system health. */
	transfer_on(MISSION_ORDER_BOARD_AND_GIVE_CARGO);
	g_craft[TARGET].system_health[0] = 3;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].system_health[0], 3);
}

/* The abort tests of the align and transfer phases (the 1997 abort order):
 * the target gone (no object, an explosion, a slot past the region or the
 * craft's own), its signature changed, the boarder with no working subsystem,
 * and a player's target that moves; each ends the dock with no progress,
 * clears the push and the timers and makes the craft forget its target.
 * Phase 3 never aborts. */
static void check_aborts(void)
{
	for (int phase = 1; phase <= 3; ++phase) {
		for (int reason = 0; reason < 7; ++reason) {
			boarder_and_target();
			g_mission_flight_groups[BOARD_GROUP]
				.fg.orders[0]
				.order = MISSION_ORDER_SS_BOARD;
			in_phase(phase, 600, 40);
			g_craft[BOARDER].push_accum_x = 5;
			g_craft[BOARDER].push_accum_y = -6;
			g_craft[BOARDER].push_accum_z = 7;
			g_objects[TARGET].world_x = 100000;
			switch (reason) {
			case 0:
				g_objects[TARGET].object_type = 0;
				break;
			case 1:
				g_mobiles[TARGET].family = 5;
				break;
			case 2:
				g_objects[TARGET].object_signature = 0x999;
				break;
			case 3:
				g_craft[BOARDER].working_subsystems = 0;
				break;
			case 4:
				g_objects[TARGET].player_owner_idx = 4;
				g_mobiles[TARGET].speed = 12;
				break;
			case 5:
				g_active_region_craft_object_slot_end = TARGET;
				break;
			default:
				boarder_controller()->target_obj_idx =
					UINT16_MAX;
				break;
			}
			ai_boarding_step(BOARDER);
			/* Phase 3 never aborts: it goes on pushing or, with its
			 * target gone, stands. */
			if (phase == 3) {
				XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER),
						  AI_BOARDING_PHASE_SEPARATE);
				/* With its target gone it stands; else it is
				 * pushed after it. */
				int gone = reason == 0 || reason == 5 ||
					   reason == 6;
				XVT_ASSERT_INT_EQ(
					g_craft[BOARDER].push_accum_x == 0 &&
						g_craft[BOARDER].push_accum_y ==
							0 &&
						g_craft[BOARDER].push_accum_z ==
							0,
					gone);
				continue;
			}
			XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER),
					  AI_BOARDING_PHASE_NONE);
			XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_timer,
					  0);
			XVT_ASSERT_INT_EQ(
				boarder_controller()->secondary_maneuver_timer,
				0);
			XVT_ASSERT_INT_EQ(g_craft[BOARDER].push_accum_x, 0);
			XVT_ASSERT_INT_EQ(g_craft[BOARDER].push_accum_y, 0);
			XVT_ASSERT_INT_EQ(g_craft[BOARDER].push_accum_z, 0);
			XVT_ASSERT_INT_EQ(boarder_controller()->target_obj_idx,
					  UINT16_MAX);
			XVT_ASSERT_INT_EQ(
				boarder_controller()->target_signature, 0);
			XVT_ASSERT_INT_EQ(boarder_controller()->has_live_target,
					  0);
			/* No progress was made. */
			XVT_ASSERT_INT_EQ(
				boarder_controller()
					->order_progress.goal_progress[0],
				0);
			XVT_ASSERT_INT_EQ(
				g_craft[BOARDER].ai_flight.docked_target_count,
				0);
			XVT_ASSERT_INT_EQ(g_craft[TARGET].boarding_state, 0);
			XVT_ASSERT_INT_EQ(
				lines_starting(
					"ai.boarding_abandoned object=1"),
				1);
		}
	}
	/* The region's first slot is a target as any other. */
	boarder_and_target();
	g_active_region_object_slot_start = TARGET;
	in_phase(2, 600, 40);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER),
			  AI_BOARDING_PHASE_TRANSFER);
	g_objects[TARGET].world_x = 4000;
	in_phase(3, 600, 0);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_TRUE(g_craft[BOARDER].push_accum_x != 0);
	/* A moving player's craft aborts phases 1 and 2 and not phase 0's
	 * approach (the pilot program waits for it). A stopped one does not. */
	boarder_and_target();
	g_objects[TARGET].player_owner_idx = 4;
	g_objects[TARGET].world_x = 100000;
	in_phase(2, 600, 40);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER),
			  AI_BOARDING_PHASE_TRANSFER);
}

/* Separate: the boarder is pushed to the target's position plus 0x4000 along
 * its own up axis, each step, until the timer has run out; then the dock ends
 * with the target forgotten, and a player's craft's command target cleared. */
static void check_separate(void)
{
	boarder_and_target();
	g_objects[TARGET].world_x = 4000;
	g_objects[TARGET].world_y = -3000;
	g_objects[TARGET].world_z = 500;
	g_objects[BOARDER].world_x = -2000;
	g_objects[BOARDER].world_y = 700;
	g_objects[BOARDER].world_z = -300;
	in_phase(3, 900, 0);
	ai_boarding_step(BOARDER);
	flight_geometry_local_to_world_i16(&g_objects[BOARDER], 0, 0x4000, 0);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].push_accum_x,
			  g_rotated_x + 4000 - -2000);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].push_accum_y,
			  g_rotated_y + -3000 - 700);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].push_accum_z,
			  g_rotated_z + 500 - -300);
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER),
			  AI_BOARDING_PHASE_SEPARATE);
	XVT_ASSERT_INT_EQ(boarder_controller()->target_obj_idx, TARGET);
	/* A target that left the battle leaves the craft unpushed. */
	g_objects[TARGET].object_type = 0;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(g_craft[BOARDER].push_accum_x, 0);
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER),
			  AI_BOARDING_PHASE_SEPARATE);
	/* The end: a computer target is forgotten, and the command target of the
	 * craft is kept. */
	boarder_and_target();
	boarder_controller()->candidate_target_idx = 9;
	in_phase(3, 0, 0);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER), AI_BOARDING_PHASE_NONE);
	XVT_ASSERT_INT_EQ(boarder_controller()->target_obj_idx, UINT16_MAX);
	XVT_ASSERT_INT_EQ(boarder_controller()->target_signature, 0);
	XVT_ASSERT_INT_EQ(boarder_controller()->has_live_target, 0);
	XVT_ASSERT_INT_EQ(boarder_controller()->candidate_target_idx, 9);
	XVT_ASSERT_INT_EQ(lines_starting("ai.boarding_separated object=1"), 1);
	/* A player's craft: the command target is cleared too. */
	boarder_and_target();
	g_objects[TARGET].player_owner_idx = REMOTE_PLAYER;
	boarder_controller()->candidate_target_idx = TARGET;
	in_phase(3, 0, 0);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(boarder_controller()->candidate_target_idx,
			  UINT16_MAX);
	XVT_ASSERT_INT_EQ(boarder_controller()->target_obj_idx, UINT16_MAX);
	/* A craft with no dock is left alone by a step. */
	boarder_and_target();
	boarder_controller()->maneuver_timer = 5;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(boarder_controller()->maneuver_timer, 5);
	XVT_ASSERT_INT_EQ(boarder_controller()->target_obj_idx, TARGET);
}

/* In a predicted step the voices, sounds and INFO lines are not made again. */
static void check_predicted_step(void)
{
	transfer_on(MISSION_ORDER_SS_BOARD);
	g_flight_sim_side_effects_suppressed = 1;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(lines_starting("ai.boarding_finished"), 0);
	XVT_ASSERT_INT_EQ(
		lines_starting(
			"ai.boarding_done object=1 target=2 order=31 state=0 target_state=2 progress=1 docked=1 docked_count=1 boarded=1 predicted=1"),
		1);
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER),
			  AI_BOARDING_PHASE_SEPARATE);
}

/* A craft held by a capture mark that names no flight group of the mission
 * (past the 48), or one on no team, is taken without moving a count that is
 * not there. */
static void check_capture_from_unknown_captor(void)
{
	transfer_on(MISSION_ORDER_BOARD_AND_CAPTURE_CARGO);
	g_mobiles[TARGET].team = 5;
	g_craft[TARGET].captured_by_flight_group = 0x80 | 48;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].captured_by_flight_group,
			  0x80 | BOARD_GROUP);
	XVT_ASSERT_INT_EQ(pair_count(BOARD_GROUP, TARGET_GROUP), 1);
	transfer_on(MISSION_ORDER_BOARD_AND_CAPTURE_CARGO);
	g_mobiles[TARGET].team = 5;
	g_craft[TARGET].captured_by_flight_group = 0x81;
	g_mission_flight_groups[1].fg.team = 10;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].captured_by_flight_group,
			  0x80 | BOARD_GROUP);
	/* A boarder on a team past the ten counts no pair and inspects
	 * nothing: the target's boarding state, the byte after the ten
	 * inspections, stays 0. */
	transfer_on(MISSION_ORDER_BOARD_AND_CAPTURE_CARGO);
	g_mobiles[BOARDER].team = 10;
	g_mobiles[BOARDER].iff = g_mobiles[TARGET].iff;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(g_craft[TARGET].boarding_state, 0);
	XVT_ASSERT_INT_EQ(
		count_of(TARGET_GROUP, FLIGHT_GROUP_OUTCOME_INSPECTED), 0);
}

/* The inspection numbers the team one past the highest of the ten teams'
 * numbers, team 0's and team 9's included. */
static void check_inspection_number(void)
{
	static const struct {
		int number_at;
		int number;
		int expected;
	} cases[] = {{0, 9, 10}, {9, 5, 6}, {1, 3, 4}, {-1, 0, 1}};
	for (size_t row = 0; row < sizeof cases / sizeof cases[0]; ++row) {
		transfer_on(MISSION_ORDER_SS_BOARD);
		g_mobiles[TARGET].iff = g_mobiles[BOARDER].iff;
		if (cases[row].number_at >= 0) {
			g_craft[TARGET].identified_order_by_team
				[cases[row].number_at] =
				(uint8_t)cases[row].number;
		}
		ai_boarding_step(BOARDER);
		XVT_ASSERT_INT_EQ(
			g_craft[TARGET].identified_order_by_team[BOARD_GROUP],
			cases[row].expected);
	}
}

/* Another iff than the target's sounds the warning, on this machine, once;
 * the same iff does not; a predicted step makes no sound. */
static void check_iff_warning(void)
{
	transfer_on(MISSION_ORDER_SS_BOARD);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(events_of(EVENT_SOUND, FLIGHT_SOUND_GENERAL_WARNING,
				    LOCAL_PLAYER),
			  1);
	XVT_ASSERT_INT_EQ(g_events[g_event_count - 1].sender_iff, -1);
	transfer_on(MISSION_ORDER_SS_BOARD);
	g_mobiles[TARGET].iff = g_mobiles[BOARDER].iff;
	g_event_count = 0;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(
		events_of(EVENT_SOUND, FLIGHT_SOUND_GENERAL_WARNING, -1), 0);
	transfer_on(MISSION_ORDER_SS_BOARD);
	g_flight_sim_side_effects_suppressed = 1;
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(
		events_of(EVENT_SOUND, FLIGHT_SOUND_GENERAL_WARNING, -1), 0);
}

/* What each order says and plays when its transfer is done: the boarder's
 * "docking operation complete" message (156) for Give, Take, Exchange, SS
 * Board and Repair; the tactical officer's line for the boarder (38 for Give
 * and Pick Up, 40 for Take, Capture, Destroy, SS Board and Repair), and for
 * the target (31 captured, 29 repaired). Exchange says no line. */
static void check_transfer_messages(void)
{
	static const struct {
		int order;
		int complete_message;
		int boarder_voice;
		int target_voice;
	} cases[] = {{MISSION_ORDER_BOARD_AND_GIVE_CARGO, 1, 38, 0},
		     {MISSION_ORDER_BOARD_AND_TAKE_CARGO, 1, 40, 0},
		     {MISSION_ORDER_BOARD_AND_EXCHANGE_CARGO, 1, 0, 0},
		     {MISSION_ORDER_BOARD_AND_CAPTURE_CARGO, 0, 40, 31},
		     {MISSION_ORDER_BOARD_AND_DESTROY_CARGO, 0, 40, 0},
		     {MISSION_ORDER_PICK_UP, 0, 38, 0},
		     {MISSION_ORDER_SS_BOARD, 1, 40, 0},
		     {MISSION_ORDER_BOARD_TO_REPAIR, 1, 40, 29}};
	for (size_t row = 0; row < sizeof cases / sizeof cases[0]; ++row) {
		transfer_on(cases[row].order);
		ai_boarding_step(BOARDER);
		XVT_ASSERT_INT_EQ(events_of(EVENT_CRAFT_MESSAGE, 156, BOARDER),
				  cases[row].complete_message);
		int voices = 0;
		for (int event = 0; event < g_event_count; ++event) {
			voices += g_events[event].kind == EVENT_VOICE;
		}
		XVT_ASSERT_INT_EQ(voices, (cases[row].boarder_voice !=
					   0) + (cases[row].target_voice != 0));
		if (cases[row].boarder_voice != 0) {
			XVT_ASSERT_INT_EQ(events_of(EVENT_VOICE,
						    cases[row].boarder_voice,
						    BOARDER),
					  1);
		}
		if (cases[row].target_voice != 0) {
			XVT_ASSERT_INT_EQ(events_of(EVENT_VOICE,
						    cases[row].target_voice,
						    TARGET),
					  1);
		}
	}
}

/* The dock's start says "has docked with" to this machine's player with the
 * boarder's iff, and speaks to the target always and to the boarder only for
 * a dock of two units or more: 37 for Give, Exchange, Pick Up, SS Board and
 * Repair, 39 for Take, Capture and Destroy. A predicted step is silent. */
static void check_dock_start_voices(void)
{
	static const int orders[] = {12, 13, 14, 15, 16, 17, 31, 32};
	static const int hostile[] = {0, 1, 0, 1, 1, 0, 0, 0};
	for (int units = 0; units <= 3; ++units) {
		for (size_t row = 0; row < sizeof orders / sizeof orders[0];
		     ++row) {
			boarder_and_target();
			g_mission_flight_groups[BOARD_GROUP]
				.fg.orders[0]
				.order = (uint8_t)orders[row];
			g_mission_flight_groups[BOARD_GROUP]
				.fg.orders[0]
				.variable1 = (uint8_t)units;
			in_phase(1, 0, 0);
			push_left(FROM_SMALL - TO_SMALL, 0);
			ai_boarding_step(BOARDER);
			XVT_ASSERT_INT_EQ(events_of(EVENT_VOICE, 37, BOARDER),
					  !hostile[row] && units >= 2);
			XVT_ASSERT_INT_EQ(events_of(EVENT_VOICE, 39, BOARDER),
					  hostile[row] && units >= 2);
			XVT_ASSERT_INT_EQ(events_of(EVENT_VOICE, 30, TARGET),
					  1);
			XVT_ASSERT_INT_EQ(
				events_of(EVENT_MESSAGE, 234, LOCAL_PLAYER), 1);
			XVT_ASSERT_INT_EQ(g_events[0].kind, EVENT_MESSAGE);
			XVT_ASSERT_INT_EQ(g_events[0].sender_iff,
					  g_mobiles[BOARDER].iff);
		}
	}
	boarder_and_target();
	g_flight_sim_side_effects_suppressed = 1;
	in_phase(1, 0, 0);
	push_left(FROM_SMALL - TO_SMALL, 0);
	ai_boarding_step(BOARDER);
	XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER),
			  AI_BOARDING_PHASE_TRANSFER);
	XVT_ASSERT_INT_EQ(g_event_count, 0);
}

/* The align phase sums the push left by its absolute value on each axis:
 * 15 ends it, 16 does not, whichever axes and signs. */
static void check_align_distance_signs(void)
{
	static const int vectors[][3] = {{-15, 0, 0}, {0, -15, 0}, {0, 0, -15},
					 {-5, 5, -5}, {15, 0, 0},  {0, 15, 0},
					 {0, 0, 15},  {5, -5, 5}};
	const int up = FROM_SMALL - TO_SMALL;
	for (size_t row = 0; row < sizeof vectors / sizeof vectors[0]; ++row) {
		for (int extra = 0; extra <= 1; ++extra) {
			/* One more unit along the last nonzero axis. */
			int vector[3] = {vectors[row][0], vectors[row][1],
					 vectors[row][2]};
			int axis = vector[2] != 0 ? 2 : vector[1] != 0 ? 1 : 0;
			vector[axis] += vector[axis] < 0 ? -extra : extra;
			boarder_and_target();
			in_phase(1, 0, 0);
			push_vector_left(up, vector[0], vector[1], vector[2]);
			ai_boarding_step(BOARDER);
			XVT_ASSERT_INT_EQ(ai_boarding_phase(BOARDER),
					  extra ? AI_BOARDING_PHASE_ALIGN
						: AI_BOARDING_PHASE_TRANSFER);
		}
	}
}

int main(void)
{
	xvt_log_set_level(AERON_LOG_DEBUG);
	SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
	SDL_SetLogOutputFunction(catch_line, NULL);
	check_phase_and_mode();
	check_sync_mode();
	check_start_and_stop();
	check_stop();
	check_align_offsets();
	check_align_turns();
	check_align_end();
	check_give();
	check_take_and_exchange();
	check_contact_and_repair();
	check_capture();
	check_pick_up();
	check_destroy();
	check_tail();
	check_special_cargo_marks();
	check_docked_list_eleven_times();
	check_give_reload();
	check_reload_goal();
	check_give_restore();
	check_aborts();
	check_separate();
	check_predicted_step();
	check_capture_from_unknown_captor();
	check_inspection_number();
	check_iff_warning();
	check_transfer_messages();
	check_dock_start_voices();
	check_align_distance_signs();
	return 0;
}
