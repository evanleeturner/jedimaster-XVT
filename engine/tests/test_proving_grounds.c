/* Tests for xvt/flight/proving_grounds.c, the training course: its gates,
 * the start of a level, the course step that turns the obstacles, counts the
 * gates crossed and ends a level, the score's digits and the local player's
 * pose history. Each check builds the world it needs in the game's own
 * tables: an object table this file owns with the twelve gates in slots 1 to
 * 12, the local player's craft in slot 20, and one model, set by hand in a
 * memory handle, for both gate types. Characters drawn go to a function of
 * this file. No game data is read.
 *
 * POSIX only, for the alarm that stops a check whose call does not return. */
#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "test_assert.h"
#include "xvt/assets/model_bounds.h"
#include "xvt/assets/model_mesh.h"
#include "xvt/assets/object_genus.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/proving_grounds.h"
#include "xvt/net/flight_net.h"
#include "xvt/render/renderer.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/timing/flight_timing.h"

enum {
	SLOT_COUNT = 64,
	CRAFT_SLOTS = 32,
	PLAYER_CRAFT = 20, /* The slot of the local player's craft. */
	GATE_A = 98,	   /* The two gate object types. */
	GATE_B = 99,
	MESHES = 5,
	Q15_ONE = 0x7FFF,
	LOCAL = 0, /* The local player. */
};

static struct object_record g_test_objects[SLOT_COUNT];
static struct mobile_object g_test_mobiles[SLOT_COUNT];
static struct craft_data g_test_craft[CRAFT_SLOTS];
static struct opt_node g_mesh_nodes[MESHES];
static struct opt_node *g_mesh_roots[MESHES];
static struct mesh_descriptor g_mesh_descriptors[MESHES];
static uint16_t g_model_handle;
static uint16_t g_message_log;
/* Every in-flight message's text, a plain line (pane type 1). */
static char g_test_message[] = "\001message";
static char g_drawn[256];
static int g_drawn_length;

/* The gates' meshes, in order: main hull, laser gun, cargo, misc hull,
 * antenna. */
static const int g_gate_meshes[MESHES] = {
	MESH_COMPONENT_01_MAIN_HULL, MESH_COMPONENT_05_LASR_GUN,
	MESH_COMPONENT_17_CARGO,     MESH_COMPONENT_18_MISC_HULL,
	MESH_COMPONENT_19_ANTENNA,
};

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

/* Keeps every character drawn, in order. */
static void catch_char(uint8_t ch)
{
	XVT_ASSERT_TRUE(g_drawn_length + 1 < (int)sizeof g_drawn);
	g_drawn[g_drawn_length] = (char)ch;
	++g_drawn_length;
	g_drawn[g_drawn_length] = '\0';
}

static void clear_drawn(void)
{
	g_drawn_length = 0;
	g_drawn[0] = '\0';
}

/* Both gate types get the model of the five gate meshes and a box 2,000
 * units deep along Y, from -1,000 to 1,000. */
static void set_gate_model(void)
{
	struct optimized_poly_object *model =
		memory_get_handle_block(g_model_handle);
	memset(model, 0, sizeof *model);
	model->self_marker = model;
	model->root_node_count = MESHES;
	model->root_nodes = g_mesh_roots;
	for (int i = 0; i < MESHES; ++i) {
		memset(&g_mesh_nodes[i], 0, sizeof g_mesh_nodes[i]);
		memset(&g_mesh_descriptors[i], 0, sizeof g_mesh_descriptors[i]);
		g_mesh_nodes[i].node_type = OPT_MESHDESC;
		g_mesh_nodes[i].payload = &g_mesh_descriptors[i];
		g_mesh_roots[i] = &g_mesh_nodes[i];
		g_mesh_descriptors[i].mesh_type = g_gate_meshes[i];
	}
	for (int type = GATE_A; type <= GATE_B; ++type) {
		g_loaded_models[type] = g_model_handle;
		g_object_type_table[type].asset_flags |= 1;
		g_model_bounds_cached[type] = 1;
		g_model_bounds_min[type].x = -500.0f;
		g_model_bounds_min[type].y = -1000.0f;
		g_model_bounds_min[type].z = -500.0f;
		g_model_bounds_max[type].x = 500.0f;
		g_model_bounds_max[type].y = 1000.0f;
		g_model_bounds_max[type].z = 500.0f;
	}
}

/* Gates 1 to 12 in slots 1 to 12, of types 98 and 99 in turn, obstacles with
 * craft records, each 0x10000 units further along X and with its forward axis
 * along world Y; gate 1 is current. The local player is player 0, flying the
 * craft in slot 20, far from every gate, waiting for a new craft (so the HUD
 * draws nothing) with the replay view on (so a level start does not rebuild
 * the HUD). The proving grounds is at level 4 with 1 second left. Every
 * in-flight message is a plain line, the message log has room, and drawn
 * characters are kept. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_players, 0, sizeof g_players);
	memset(&g_flight_mission_state, 0, sizeof g_flight_mission_state);
	memset(&g_mission_countdown_clock, 0, sizeof g_mission_countdown_clock);
	set_gate_model();
	g_object_table = g_test_objects;
	g_craft_data_pool_base = g_test_craft;
	for (int i = 0; i < SLOT_COUNT; ++i) {
		g_test_objects[i].player_owner_idx = -1;
		g_test_objects[i].mobj = &g_test_mobiles[i];
		g_test_mobiles[i].cached_side_x = Q15_ONE;
		g_test_mobiles[i].cached_fwd_y = Q15_ONE;
		g_test_mobiles[i].cached_up_z = Q15_ONE;
		if (i < CRAFT_SLOTS) {
			g_test_mobiles[i].p_craft = &g_test_craft[i];
		}
	}
	for (int gate = 1; gate <= 12; ++gate) {
		struct object_record *object = &g_test_objects[gate];
		object->object_type = gate % 2 == 1 ? GATE_A : GATE_B;
		object->genus_id = CRAFT_GENUS_OBSTACLE;
		object->world_x = 0x10000 * gate + 1234;
		object->world_y = -50000 + 77 * gate;
		object->world_z = 9000 + gate;
	}
	struct object_record *craft = &g_test_objects[PLAYER_CRAFT];
	craft->object_type = 1;
	craft->world_x = -0x200000;
	craft->world_y = 0x300000;
	craft->world_z = 0x100000;
	craft->mobj->prev_world_x = craft->world_x;
	craft->mobj->prev_world_y = craft->world_y;
	craft->mobj->prev_world_z = craft->world_z;
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
		g_players[i].current_target_object_idx = -1;
	}
	g_local_player = LOCAL;
	g_players[LOCAL].object_index = PLAYER_CRAFT;
	g_players[LOCAL].awaiting_new_craft = 1;
	g_replay_view_mode = 1;
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = CRAFT_SLOTS;
	g_proving_grounds_current_checkpoint_obj_idx = 1;
	g_flight_mission_state.proving_grounds_mode_active = 1;
	g_flight_mission_state.proving_grounds_level = 4;
	g_mission_countdown_clock.seconds = 1;
	g_elapsed_ticks = 0;
	g_input_timestamp = 5000;
	g_flight_sim_side_effects_suppressed = 0;
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
	g_flight_draw_char_fn = catch_char;
	clear_drawn();
}

/* The total time on the countdown clock, in seconds. */
static int clock_seconds(void)
{
	return 60 * g_mission_countdown_clock.minutes +
	       g_mission_countdown_clock.seconds;
}

/* Moves the local player's craft from one side of gate gate's plane to the
 * other, along world Y, crossing the point the plane passes through. */
static void cross_gate(int gate)
{
	struct object_record *craft = &g_test_objects[PLAYER_CRAFT];
	struct object_record *object = &g_test_objects[gate];
	craft->world_x = object->world_x + 300;
	craft->world_z = object->world_z - 200;
	craft->mobj->prev_world_x = craft->world_x;
	craft->mobj->prev_world_z = craft->world_z;
	int offset = 0;
	if (object->object_type == GATE_A) {
		offset = -1000;
	}
	offset += gate == g_proving_grounds_current_checkpoint_obj_idx ? -1024
								       : 32;
	int plane_y = object->world_y + 2 * (offset * Q15_ONE >> 15);
	craft->mobj->prev_world_y = plane_y - 400;
	craft->world_y = plane_y + 400;
}

/* ------------------------------------------------------------------------ */
/* The start of a level. */

/* A level up to 8 gets (10 - level) / 2 minutes plus 30 seconds when odd; a
 * level above 8 gets 5 * (20 - level) seconds. Gate 1 is current, no gate is
 * passed and 12 remain. */
static void check_start_level_clock(void)
{
	static const struct {
		int level;
		int minutes;
		int seconds;
	} levels[] = {
		{1, 4, 30}, {2, 4, 0}, {4, 3, 0},  {5, 2, 30},
		{7, 1, 30}, {8, 1, 0}, {9, 0, 55}, {19, 0, 5},
	};
	for (size_t i = 0; i < sizeof levels / sizeof levels[0]; ++i) {
		fresh_world();
		g_proving_grounds_current_checkpoint_obj_idx = 7;
		g_flight_mission_state.proving_grounds_checkpoints_passed = 6;
		g_flight_mission_state.proving_grounds_checkpoints_remaining =
			6;
		proving_grounds_start_level((uint16_t)levels[i].level);
		XVT_ASSERT_INT_EQ(g_mission_countdown_clock.minutes,
				  levels[i].minutes);
		XVT_ASSERT_INT_EQ(g_mission_countdown_clock.seconds,
				  levels[i].seconds);
		XVT_ASSERT_INT_EQ(g_proving_grounds_current_checkpoint_obj_idx,
				  1);
		XVT_ASSERT_INT_EQ(g_flight_mission_state
					  .proving_grounds_checkpoints_passed,
				  0);
		XVT_ASSERT_INT_EQ(
			g_flight_mission_state
				.proving_grounds_checkpoints_remaining,
			12);
	}
}

/* Known failure level_21_clock_wraps, issue #201: above level 8 a level gets
 * 5 * (20 - level) seconds, less time the higher the level. The clock keeps
 * the seconds in 8 bits, so from level 21 the negative count wraps: level 21
 * starts with 251 seconds, more than any level above 8 before it. The comment
 * says the level is not checked; this rests on the issue. The throwaway fix
 * gives a level past 20 no seconds, as level 20 gets; capping the level
 * where it goes up would also do. */
static void check_level_clock_never_grows(void)
{
	fresh_world();
	proving_grounds_start_level(9);
	int previous = clock_seconds();
	for (int level = 10; level <= 25; ++level) {
		proving_grounds_start_level((uint16_t)level);
		XVT_ASSERT_TRUE(clock_seconds() <= previous);
		previous = clock_seconds();
	}
}

/* Gate 1 hides every component of its craft (component_state 2), with
 * component_hp 255 on the main hull and 0 elsewhere. The other gates hide
 * the main hull and give laser guns 2 * level; cargo shows from level 2 with
 * 3 * level, antennas from level 3 with 24 * level and misc hull from level 5
 * with 255; below those levels they are hidden with 0. A shown cargo, misc
 * hull or antenna mesh starts unturned. Objects that are not obstacles are
 * left alone. */
static void check_start_level_components(void)
{
	fresh_world();
	g_test_objects[PLAYER_CRAFT].object_type = GATE_A;
	g_test_craft[PLAYER_CRAFT].component_hp[1] = 77;
	for (int gate = 1; gate <= 2; ++gate) {
		for (int mesh = 0; mesh < MESHES; ++mesh) {
			g_test_craft[gate].component_hp[mesh] = 99;
			g_test_craft[gate].mesh_rotation[mesh] = 9;
		}
	}
	proving_grounds_start_level(4);
	struct craft_data *start = &g_test_craft[1];
	XVT_ASSERT_INT_EQ(start->component_hp[0], UINT8_MAX);
	for (int mesh = 0; mesh < MESHES; ++mesh) {
		XVT_ASSERT_INT_EQ(start->component_state[mesh], 2);
		if (mesh != 0) {
			XVT_ASSERT_INT_EQ(start->component_hp[mesh], 0);
		}
	}
	struct craft_data *gate = &g_test_craft[2];
	XVT_ASSERT_INT_EQ(gate->component_state[0], 2);
	XVT_ASSERT_INT_EQ(gate->component_hp[1], 8);
	XVT_ASSERT_INT_EQ(gate->component_state[1], 0);
	XVT_ASSERT_INT_EQ(gate->component_hp[2], 12);
	XVT_ASSERT_INT_EQ(gate->component_state[2], 0);
	XVT_ASSERT_INT_EQ(gate->mesh_rotation[2], 0);
	XVT_ASSERT_INT_EQ(gate->component_hp[3], 0);
	XVT_ASSERT_INT_EQ(gate->component_state[3], 2);
	XVT_ASSERT_INT_EQ(gate->component_hp[4], 96);
	XVT_ASSERT_INT_EQ(gate->component_state[4], 0);
	XVT_ASSERT_INT_EQ(gate->mesh_rotation[4], 0);
	XVT_ASSERT_INT_EQ(g_test_craft[PLAYER_CRAFT].component_hp[1], 77);
	XVT_ASSERT_INT_EQ(g_test_craft[12].component_hp[1], 8);

	/* Level 5 shows the misc hull; level 1 hides cargo and antennas. */
	proving_grounds_start_level(5);
	XVT_ASSERT_INT_EQ(gate->component_hp[3], UINT8_MAX);
	XVT_ASSERT_INT_EQ(gate->component_state[3], 0);
	XVT_ASSERT_INT_EQ(gate->mesh_rotation[3], 0);
	XVT_ASSERT_INT_EQ(gate->component_hp[1], 10);
	proving_grounds_start_level(1);
	XVT_ASSERT_INT_EQ(gate->component_hp[2], 0);
	XVT_ASSERT_INT_EQ(gate->component_state[2], 2);
	XVT_ASSERT_INT_EQ(gate->component_hp[4], 0);
	XVT_ASSERT_INT_EQ(gate->component_state[4], 2);
	XVT_ASSERT_INT_EQ(gate->component_hp[1], 2);
	proving_grounds_start_level(2);
	XVT_ASSERT_INT_EQ(gate->component_hp[2], 6);
	XVT_ASSERT_INT_EQ(gate->component_state[2], 0);
	XVT_ASSERT_INT_EQ(gate->component_state[4], 2);
	proving_grounds_start_level(3);
	XVT_ASSERT_INT_EQ(gate->component_hp[4], 72);
	XVT_ASSERT_INT_EQ(gate->component_state[4], 0);
}

/* ------------------------------------------------------------------------ */
/* Crossing a gate. */

/* The local player crossed a gate when the craft's current and previous
 * positions lie on opposite sides of the gate's plane, or on it; the plane
 * faces along the gate's forward axis through a point moved 32 forward for a
 * gate other than the current one, 1,024 back for the current one, and for
 * type 98 also by -maxY of its box. */
static void check_crossed_checkpoint(void)
{
	fresh_world();
	for (int gate = 1; gate <= 3; ++gate) {
		cross_gate(gate);
		XVT_ASSERT_INT_EQ(proving_grounds_has_player_crossed_checkpoint(
					  (uint16_t)gate),
				  1);
		/* Going back across counts too. */
		struct mobile_object *mobile = &g_test_mobiles[PLAYER_CRAFT];
		int y = g_test_objects[PLAYER_CRAFT].world_y;
		g_test_objects[PLAYER_CRAFT].world_y = mobile->prev_world_y;
		mobile->prev_world_y = y;
		XVT_ASSERT_INT_EQ(proving_grounds_has_player_crossed_checkpoint(
					  (uint16_t)gate),
				  1);
		/* Both on one side does not. */
		mobile->prev_world_y = g_test_objects[PLAYER_CRAFT].world_y;
		XVT_ASSERT_INT_EQ(proving_grounds_has_player_crossed_checkpoint(
					  (uint16_t)gate),
				  0);
	}

	/* The plane's point: 8 short of it on each side is not a crossing,
	 * the same move ending 8 past it is. */
	cross_gate(2);
	struct mobile_object *mobile = &g_test_mobiles[PLAYER_CRAFT];
	int plane_y = mobile->prev_world_y + 400;
	mobile->prev_world_y = plane_y - 300;
	g_test_objects[PLAYER_CRAFT].world_y = plane_y - 8;
	XVT_ASSERT_INT_EQ(proving_grounds_has_player_crossed_checkpoint(2), 0);
	g_test_objects[PLAYER_CRAFT].world_y = plane_y + 8;
	XVT_ASSERT_INT_EQ(proving_grounds_has_player_crossed_checkpoint(2), 1);
	mobile->prev_world_y = plane_y + 8;
	g_test_objects[PLAYER_CRAFT].world_y = plane_y + 300;
	XVT_ASSERT_INT_EQ(proving_grounds_has_player_crossed_checkpoint(2), 0);

	/* A move ending on the plane, or starting on it, crosses it. 2 units
	 * off it is a side of 1 along the axis, which does not. */
	static const int moves[][3] = {
		{-400, 0, 1}, {0, 400, 1}, {400, 0, 1},
		{0, -400, 1}, {2, 400, 0}, {400, 2, 0},
	};
	for (size_t i = 0; i < sizeof moves / sizeof moves[0]; ++i) {
		mobile->prev_world_y = plane_y + moves[i][0];
		g_test_objects[PLAYER_CRAFT].world_y = plane_y + moves[i][1];
		XVT_ASSERT_INT_EQ(
			proving_grounds_has_player_crossed_checkpoint(2),
			moves[i][2]);
	}

	/* The current gate's plane lies 1,024 back: a move ending 1 short of
	 * it has not crossed, one ending on it has. */
	cross_gate(1);
	plane_y = mobile->prev_world_y + 400;
	mobile->prev_world_y = plane_y - 3;
	g_test_objects[PLAYER_CRAFT].world_y = plane_y - 1;
	XVT_ASSERT_INT_EQ(proving_grounds_has_player_crossed_checkpoint(1), 0);
	g_test_objects[PLAYER_CRAFT].world_y = plane_y;
	XVT_ASSERT_INT_EQ(proving_grounds_has_player_crossed_checkpoint(1), 1);
}

/* A craft more than 0x4000 units from the plane's point on any axis, now or
 * before, has not crossed, though the move crosses the plane. */
static void check_crossed_checkpoint_far(void)
{
	static const int far_axis[6][3] = {
		{0x4001, 0, 0},	 {-0x4001, 0, 0}, {0, 0x4001, 0},
		{0, -0x4001, 0}, {0, 0, 0x4001},  {0, 0, -0x4001},
	};
	fresh_world();
	for (int now = 0; now < 2; ++now) {
		for (int axis = 0; axis < 6; ++axis) {
			cross_gate(2);
			struct object_record *craft =
				&g_test_objects[PLAYER_CRAFT];
			struct mobile_object *mobile = craft->mobj;
			int plane_y = mobile->prev_world_y + 400;
			int x = g_test_objects[2].world_x + far_axis[axis][0];
			int z = g_test_objects[2].world_z + far_axis[axis][2];
			int far_y = far_axis[axis][1];
			/* The other end of the move, across the plane. */
			int other_y = far_y < 0 ? plane_y + 400 : plane_y - 400;
			if (now == 1) {
				craft->world_x = x;
				craft->world_z = z;
				if (far_y != 0) {
					craft->world_y = plane_y + far_y;
					mobile->prev_world_y = other_y;
				}
			} else {
				mobile->prev_world_x = x;
				mobile->prev_world_z = z;
				if (far_y != 0) {
					mobile->prev_world_y = plane_y + far_y;
					craft->world_y = other_y;
				}
			}
			XVT_ASSERT_INT_EQ(
				proving_grounds_has_player_crossed_checkpoint(
					2),
				0);
		}
	}
	/* Exactly 0x4000 away on each axis, either side, still counts. */
	for (int sign = -1; sign <= 1; sign += 2) {
		cross_gate(2);
		struct object_record *craft = &g_test_objects[PLAYER_CRAFT];
		struct mobile_object *mobile = craft->mobj;
		int plane_y = mobile->prev_world_y + 400;
		int edge = sign * 0x4000;
		craft->world_x = g_test_objects[2].world_x + edge;
		craft->world_z = g_test_objects[2].world_z - edge;
		mobile->prev_world_x = g_test_objects[2].world_x - edge;
		mobile->prev_world_z = g_test_objects[2].world_z + edge;
		craft->world_y = plane_y + edge;
		mobile->prev_world_y = plane_y - edge;
		XVT_ASSERT_INT_EQ(
			proving_grounds_has_player_crossed_checkpoint(2), 1);
	}
}

/* ------------------------------------------------------------------------ */
/* The course step. */

/* Crossing the gate after the current one makes it current and counts it:
 * one more passed, one fewer remaining. The craft away from it counts
 * nothing; after gate 12 the next is gate 1. */
static void check_course_counts_gates(void)
{
	fresh_world();
	g_flight_mission_state.proving_grounds_checkpoints_remaining = 12;
	proving_grounds_update_course();
	XVT_ASSERT_INT_EQ(g_proving_grounds_current_checkpoint_obj_idx, 1);
	/* This is the program's first course step, with no ticks: the course
	 * frame, which starts at 0, stays there. */
	XVT_ASSERT_INT_EQ(g_test_mobiles[1].node_switch_index, 0);
	cross_gate(2);
	proving_grounds_update_course();
	XVT_ASSERT_INT_EQ(g_proving_grounds_current_checkpoint_obj_idx, 2);
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.proving_grounds_checkpoints_passed, 1);
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.proving_grounds_checkpoints_remaining,
		11);
	/* Crossing gate 4 out of turn counts nothing. */
	cross_gate(4);
	proving_grounds_update_course();
	XVT_ASSERT_INT_EQ(g_proving_grounds_current_checkpoint_obj_idx, 2);
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.proving_grounds_checkpoints_passed, 1);

	g_proving_grounds_current_checkpoint_obj_idx = 12;
	cross_gate(1);
	g_mission_countdown_clock.seconds = 0;
	proving_grounds_update_course();
	XVT_ASSERT_INT_EQ(g_proving_grounds_current_checkpoint_obj_idx, 1);
}

/* Settles the obstacle timers at the given level with a step of 25 ticks,
 * longer than any period: after a step that runs a timer below 0, each timer
 * holds from 1 to its period, so a step of k periods then turns its meshes k
 * steps. */
static void settle_timers(int level)
{
	g_flight_mission_state.proving_grounds_level = (uint8_t)level;
	g_elapsed_ticks = 25;
	proving_grounds_update_course();
}

/* The obstacles turn one step per period of ticks: cargo from level 3,
 * antennas from level 4, both at the level's period, and misc hull from level
 * 6 at the previous level's period. At level 6 the periods are 14 and 16, so
 * 224 ticks turn cargo and antennas 16 steps and misc hull 14. Main hull and
 * guns do not turn. */
static void check_course_turns_obstacles(void)
{
	fresh_world();
	settle_timers(6);
	uint8_t before[MESHES];
	memcpy(before, g_test_craft[5].mesh_rotation, sizeof before);
	g_elapsed_ticks = 224;
	proving_grounds_update_course();
	struct craft_data *gate = &g_test_craft[5];
	XVT_ASSERT_INT_EQ((uint8_t)(gate->mesh_rotation[0] - before[0]), 0);
	XVT_ASSERT_INT_EQ((uint8_t)(gate->mesh_rotation[1] - before[1]), 0);
	XVT_ASSERT_INT_EQ((uint8_t)(gate->mesh_rotation[2] - before[2]), 16);
	XVT_ASSERT_INT_EQ((uint8_t)(gate->mesh_rotation[3] - before[3]), 14);
	XVT_ASSERT_INT_EQ((uint8_t)(gate->mesh_rotation[4] - before[4]), 16);
	/* Every gate turns alike. */
	XVT_ASSERT_INT_EQ(g_test_craft[12].mesh_rotation[2],
			  gate->mesh_rotation[2]);

	/* At level 3 only cargo turns: the period is 24. */
	settle_timers(3);
	memcpy(before, gate->mesh_rotation, sizeof before);
	g_elapsed_ticks = 72;
	proving_grounds_update_course();
	XVT_ASSERT_INT_EQ((uint8_t)(gate->mesh_rotation[2] - before[2]), 3);
	XVT_ASSERT_INT_EQ((uint8_t)(gate->mesh_rotation[3] - before[3]), 0);
	XVT_ASSERT_INT_EQ((uint8_t)(gate->mesh_rotation[4] - before[4]), 0);

	/* At level 4 antennas turn too: the period is 20. */
	settle_timers(4);
	memcpy(before, gate->mesh_rotation, sizeof before);
	g_elapsed_ticks = 40;
	proving_grounds_update_course();
	XVT_ASSERT_INT_EQ((uint8_t)(gate->mesh_rotation[2] - before[2]), 2);
	XVT_ASSERT_INT_EQ((uint8_t)(gate->mesh_rotation[3] - before[3]), 0);
	XVT_ASSERT_INT_EQ((uint8_t)(gate->mesh_rotation[4] - before[4]), 2);

	/* At level 5 still not misc hull. */
	settle_timers(5);
	memcpy(before, gate->mesh_rotation, sizeof before);
	g_elapsed_ticks = 32;
	proving_grounds_update_course();
	XVT_ASSERT_INT_EQ((uint8_t)(gate->mesh_rotation[2] - before[2]), 2);
	XVT_ASSERT_INT_EQ((uint8_t)(gate->mesh_rotation[3] - before[3]), 0);
	XVT_ASSERT_INT_EQ((uint8_t)(gate->mesh_rotation[4] - before[4]), 2);
	settle_timers(2);
	memcpy(before, gate->mesh_rotation, sizeof before);
	g_elapsed_ticks = 96;
	proving_grounds_update_course();
	XVT_ASSERT_INT_EQ((uint8_t)(gate->mesh_rotation[2] - before[2]), 0);

	/* A step of no ticks turns nothing. */
	settle_timers(6);
	memcpy(before, gate->mesh_rotation, sizeof before);
	g_elapsed_ticks = 0;
	proving_grounds_update_course();
	XVT_ASSERT_INT_EQ(memcmp(before, gate->mesh_rotation, sizeof before),
			  0);
}

/* The course frame steps once in a step of 29 ticks and shows on every
 * gate's node_switch_index, frames 0 to 3 with frame 3 shown as 1. */
static void check_course_frames(void)
{
	fresh_world();
	g_elapsed_ticks = 29;
	int shown[8];
	for (int step = 0; step < 8; ++step) {
		proving_grounds_update_course();
		shown[step] = g_test_mobiles[1].node_switch_index;
		for (int gate = 2; gate <= 12; ++gate) {
			XVT_ASSERT_INT_EQ(
				g_test_mobiles[gate].node_switch_index,
				shown[step]);
		}
	}
	/* The four frames repeat, showing 0, 1, 2, 1 in turn. */
	int start = 0;
	while (shown[start] != 0 || shown[start + 1] != 1) {
		++start;
		XVT_ASSERT_TRUE(start < 4);
	}
	XVT_ASSERT_INT_EQ(shown[start + 2], 2);
	XVT_ASSERT_INT_EQ(shown[start + 3], 1);
	XVT_ASSERT_INT_EQ(shown[start + 4], 0);
	/* A step of no ticks keeps the frame. */
	g_elapsed_ticks = 0;
	int frame = g_test_mobiles[1].node_switch_index;
	proving_grounds_update_course();
	XVT_ASSERT_INT_EQ(g_test_mobiles[1].node_switch_index, frame);
}

/* Crossing gate 1 after gate 12 ends the level: with 1 second left the clock
 * runs down to 0, adding 10 to the time bonus and the score; the bonus
 * message gets the bonus, and the next level starts with its own clock and
 * gate 1 current. The last frame drawn shows the clock at 00:00 and the bonus
 * in five digits. */
static void check_level_end(void)
{
	fresh_world();
	g_proving_grounds_current_checkpoint_obj_idx = 12;
	g_flight_mission_state.proving_grounds_score = 1230;
	g_flight_mission_state.proving_grounds_level = 5;
	cross_gate(1);
	proving_grounds_update_course();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_time_bonus,
			  10);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_score, 1240);
	XVT_ASSERT_INT_EQ(g_msg_arg_table[0], 10);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_level, 6);
	XVT_ASSERT_INT_EQ(g_mission_countdown_clock.minutes, 2);
	XVT_ASSERT_INT_EQ(g_mission_countdown_clock.seconds, 0);
	XVT_ASSERT_INT_EQ(g_proving_grounds_current_checkpoint_obj_idx, 1);
	XVT_ASSERT_INT_EQ(
		g_flight_mission_state.proving_grounds_checkpoints_passed, 0);
	XVT_ASSERT_TRUE(g_drawn_length >= 10);
	XVT_ASSERT_INT_EQ(strcmp(&g_drawn[g_drawn_length - 10], "00:0000010"),
			  0);
}

/* Known failure level_end_hangs, issue #195: crossing gate 1 ends the level,
 * runs the clock down and starts the next level. With 2 seconds left the
 * wait after the first second never ends: it counts real ticks into
 * g_input_timestamp from 0, and the clock it reads does not move inside a
 * simulation step, so the call never returns. The throwaway fix drops the
 * wait; a wait that ends whatever the clock does would also do. */
static void check_level_end_returns(void)
{
	fresh_world();
	g_proving_grounds_current_checkpoint_obj_idx = 12;
	g_mission_countdown_clock.seconds = 2;
	cross_gate(1);
	fail_after_seconds(5);
	proving_grounds_update_course();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_time_bonus,
			  20);
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_level, 5);
}

/* Known failure level_end_restarts_game_clock, issue #196: ending a level
 * sets g_input_timestamp, the time the frame loops copy into the game clock,
 * back to 0, so the clock runs again from 0 while players and objects keep
 * times from the old clock. The comment only says the function writes it;
 * this rests on the issue: the level's end must not set the clock back. The
 * throwaway fix leaves the timestamp as the wait leaves it; waiting on a
 * count of its own would also do. */
static void check_level_end_keeps_game_clock(void)
{
	fresh_world();
	g_proving_grounds_current_checkpoint_obj_idx = 12;
	g_input_timestamp = 30000;
	cross_gate(1);
	fail_after_seconds(5);
	proving_grounds_update_course();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_level, 5);
	XVT_ASSERT_TRUE(g_input_timestamp >= 30000);
}

/* Known failure level_20_period_past_table, issue #201: the obstacle period
 * table holds levels 0 to 19, and the comment on the course step says the
 * level is not checked against it; this rests on the table's comment and the
 * issue. At level 20 the step reads the entry past the table's end. The
 * throwaway fix reads level 19's period for any level past it; stopping the
 * level from going past 19 would also do. */
static void check_level_20_period(void)
{
	fresh_world();
	g_flight_mission_state.proving_grounds_level = 20;
	g_elapsed_ticks = 400;
	proving_grounds_update_course();
	XVT_ASSERT_INT_EQ(g_flight_mission_state.proving_grounds_level, 20);
}

/* ------------------------------------------------------------------------ */
/* Drawing the score. */

static void draw_score(int score, unsigned width, unsigned min_digits)
{
	clear_drawn();
	proving_grounds_draw_score_decimal(score, width, min_digits);
}

/* The score is drawn right-aligned in width places, leading zeros as spaces
 * until min_digits places remain; a place whose digit comes out above 9
 * shows 9; width 0 draws nothing. */
static void check_score_digits(void)
{
	fresh_world();
	draw_score(1234, 6, 1);
	XVT_ASSERT_INT_EQ(strcmp(g_drawn, "  1234"), 0);
	draw_score(1234, 6, 6);
	XVT_ASSERT_INT_EQ(strcmp(g_drawn, "001234"), 0);
	draw_score(5, 3, 2);
	XVT_ASSERT_INT_EQ(strcmp(g_drawn, " 05"), 0);
	draw_score(0, 3, 1);
	XVT_ASSERT_INT_EQ(strcmp(g_drawn, "  0"), 0);
	draw_score(0, 3, 0);
	XVT_ASSERT_INT_EQ(strcmp(g_drawn, "   "), 0);
	draw_score(1005, 4, 1);
	XVT_ASSERT_INT_EQ(strcmp(g_drawn, "1005"), 0);
	draw_score(98765432, 8, 1);
	XVT_ASSERT_INT_EQ(strcmp(g_drawn, "98765432"), 0);
	draw_score(12345, 4, 1);
	XVT_ASSERT_INT_EQ(strcmp(g_drawn, "9345"), 0);
	draw_score(10345, 4, 1);
	XVT_ASSERT_INT_EQ(strcmp(g_drawn, "9345"), 0);
	draw_score(77, 0, 1);
	XVT_ASSERT_INT_EQ(g_drawn_length, 0);
}

/* ------------------------------------------------------------------------ */
/* The pose history. */

/* With locked timing, each record shifts the history one place older,
 * dropping the oldest, and records the craft's previous position and current
 * roll, pitch and yaw as the newest. */
static void check_pose_history(void)
{
	fresh_world();
	XVT_ASSERT_INT_EQ(xvt_flight_timing_is_unlocked(), 0);
	struct object_record *craft = &g_test_objects[PLAYER_CRAFT];
	for (int record = 0; record < 6; ++record) {
		craft->mobj->prev_world_x = 1000 + record;
		craft->mobj->prev_world_y = -2000 - record;
		craft->mobj->prev_world_z = 3000 + 10 * record;
		craft->world_x = 7;
		craft->roll = (int16_t)(100 + record);
		craft->pitch = (int16_t)(200 + record);
		craft->yaw = (int16_t)(300 + record);
		proving_grounds_record_local_player_pose_history();
	}
	for (int age = 0; age < 4; ++age) {
		int record = 5 - age;
		XVT_ASSERT_INT_EQ(
			g_proving_grounds_local_player_world_x_history[age],
			1000 + record);
		XVT_ASSERT_INT_EQ(
			g_proving_grounds_local_player_world_y_history[age],
			-2000 - record);
		XVT_ASSERT_INT_EQ(
			g_proving_grounds_local_player_world_z_history[age],
			3000 + 10 * record);
		XVT_ASSERT_INT_EQ(
			g_proving_grounds_local_player_roll_history[age],
			100 + record);
		XVT_ASSERT_INT_EQ(
			g_proving_grounds_local_player_pitch_history[age],
			200 + record);
		XVT_ASSERT_INT_EQ(
			g_proving_grounds_local_player_yaw_history[age],
			300 + record);
	}
}

int main(int argc, char **argv)
{
	fail_after_seconds(30);
	g_model_handle = memory_alloc_handle_zeroed(
		sizeof(struct optimized_poly_object), 0);
	XVT_ASSERT_TRUE(g_model_handle != 0);
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
			{"level_21_clock_wraps", check_level_clock_never_grows},
			{"level_end_hangs", check_level_end_returns},
			{"level_end_restarts_game_clock",
			 check_level_end_keeps_game_clock},
			{"level_20_period_past_table", check_level_20_period},
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
	check_start_level_clock();
	check_start_level_components();
	check_crossed_checkpoint();
	check_crossed_checkpoint_far();
	check_course_counts_gates();
	check_course_frames();
	check_course_turns_obstacles();
	check_level_end();
	check_score_digits();
	check_pose_history();
	return 0;
}
