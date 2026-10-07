/* Tests for xvt/flight/flight_object.c: an object's texture animation, the
 * local debris kept around the player, a player's hyperspace jump and the
 * animation step's timer and tumbling crew objects. Each check builds the
 * world it needs in the game's own tables: an object table this file owns
 * with 48 mobile slots and 16 static slots, and craft records for the first
 * eight. Every in-flight message is a plain line. No game data is read. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/assets/object_genus.h"
#include "xvt/assets/object_type.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_object.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/hud/msg.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/util/game_rand.h"
#include "xvt/util/memory.h"

enum {
	SLOT_COUNT = 64,
	MOBILE_SLOTS = 48,
	STATIC_SLOTS = 16,
	CRAFT_SLOTS = 8,
	DEBRIS_START = 30, /* The local debris slots, 30 to 37. */
	DEBRIS_END = 38,
	PLAYER_CRAFT = 5,
	LOCAL = 0,
	JUMPER_TYPE = 1, /* The X-wing. */
	BLOCKER_TYPE = 20,
	Q15_ONE = 0x7FFF,
	ALIGN = 1,  /* The jump's first stage, turning onto the jump line. */
	DEPART = 2, /* Its second stage. */
	STAGE_TICKS = 0x49C,
	FORWARD = 0x4000, /* A quarter turn of pitch. */
};

enum {
	JUMP_ABORTED_MESSAGE =
		IFMSG_112_OBJECT_DETECTED_IN_JUMP_PATH_HYPERSPACE_JUMP_ABORTED,
};

static struct object_record g_test_objects[SLOT_COUNT];
static struct mobile_object g_test_mobiles[MOBILE_SLOTS];
static struct craft_data g_test_craft[CRAFT_SLOTS];
static uint16_t g_message_log;
static char g_test_message[] = "\001message";

/* An empty world: mobile slots 0 to 47 with level axes (side X, forward Y,
 * up Z), static slots 48 to 63 without mobile data, craft records in the
 * first eight slots, local debris slots 30 to 37. Player 0, the local
 * player, flies the active X-wing in slot 5, away from the origin. Messages
 * are plain lines logged for the local player; the game's random generators
 * start from a fixed state. */
static void fresh_world(void)
{
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	memset(g_test_craft, 0, sizeof g_test_craft);
	memset(g_players, 0, sizeof g_players);
	memset(&g_flight_mission_state, 0, sizeof g_flight_mission_state);
	memset(&g_flight_global_countdown_timers, 0,
	       sizeof g_flight_global_countdown_timers);
	g_object_table = g_test_objects;
	g_craft_data_pool_base = g_test_craft;
	for (int i = 0; i < SLOT_COUNT; ++i) {
		g_test_objects[i].player_owner_idx = -1;
		if (i < MOBILE_SLOTS) {
			g_test_objects[i].mobj = &g_test_mobiles[i];
			g_test_mobiles[i].cached_side_x = Q15_ONE;
			g_test_mobiles[i].cached_fwd_y = Q15_ONE;
			g_test_mobiles[i].cached_up_z = Q15_ONE;
		}
		if (i < CRAFT_SLOTS) {
			g_test_mobiles[i].p_craft = &g_test_craft[i];
		}
	}
	g_active_region_object_slot_start = 0;
	g_active_region_craft_object_slot_end = CRAFT_SLOTS;
	g_region_main_object_slot_end = MOBILE_SLOTS;
	g_region_static_object_slot_count = STATIC_SLOTS;
	g_local_transient_slot_start = DEBRIS_START;
	g_local_debris_slot_end = DEBRIS_END;
	g_local_debris_recycle_slot_cursor = DEBRIS_START;
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
		g_players[i].current_target_object_idx = -1;
	}
	struct object_record *craft = &g_test_objects[PLAYER_CRAFT];
	craft->object_type = JUMPER_TYPE;
	craft->genus_id = CRAFT_GENUS_STARFIGHTER;
	craft->world_x = 120000;
	craft->world_y = -340000;
	craft->world_z = 56000;
	craft->pitch = FORWARD;
	g_local_player = LOCAL;
	g_players[LOCAL].object_index = PLAYER_CRAFT;
	g_test_objects[PLAYER_CRAFT].player_owner_idx = LOCAL;
	g_object_type_table[JUMPER_TYPE].max_bounds_extent = 1000;
	g_object_type_table[BLOCKER_TYPE].max_bounds_extent = 3000;
	g_elapsed_ticks = 4;
	g_flight_sim_side_effects_suppressed = 0;
	g_game_rand_value_state = 0x1234;
	g_game_rand_feedback_state = 0x5678;
	g_game_rand2_value_state = 0x2468;
	g_game_rand2_feedback_state = 0x1357;
	for (size_t id = 0; id < sizeof g_str_in_flight_messages /
					 sizeof g_str_in_flight_messages[0];
	     ++id) {
		g_str_in_flight_messages[id] = g_test_message;
	}
	g_system_message_display_enabled = 0;
	g_replay_view_mode = 0;
	g_message_log_handle = g_message_log;
	g_message_log_write_index = 0;
	g_message_log_total_count = 0;
	g_ready_message_queue_count = 0;
	memset(g_ready_message_pane_queue, 0,
	       sizeof g_ready_message_pane_queue);
	g_ready_message_pane_queue[0].state_or_message_id = 5;
	g_ready_message_pane_queue[0].pane_type = 1;
}

/* The id of the newest in-flight message in the local player's log, which
 * is written at the log's write index. */
static int last_message(void)
{
	XVT_ASSERT_TRUE(g_message_log_total_count > 0);
	struct hud_in_flight_message_record *log =
		memory_get_handle_block(g_message_log_handle);
	return log[g_message_log_write_index].state_or_message_id;
}

/* ------------------------------------------------------------------------ */
/* Texture animation. */

/* Each step moves one entry along the sequence and acts on the entry it
 * lands on: -3 starts over at 0; -2 and any entry below 0xFF00 stay; any
 * other entry from 0xFF00 up jumps to the entry plus 0x100; -1 frees the
 * object, and for a craft slot its linked objects too. A NULL sequence does
 * nothing. */
static void check_texture_sequence(void)
{
	static int16_t sequence[] = {
		10, 11, -3, 12, -2, 13, (int16_t)0xFF01, -1, (int16_t)0xFF00,
	};
	fresh_world();
	g_test_objects[40].object_type = 129;
	g_billboard_texture_frame_sequence = sequence;
	g_billboard_texture_sequence_index = 0;
	flight_object_advance_texture_frame_sequence(40);
	XVT_ASSERT_INT_EQ(g_billboard_texture_sequence_index, 1);
	flight_object_advance_texture_frame_sequence(40);
	XVT_ASSERT_INT_EQ(g_billboard_texture_sequence_index, 0);
	g_billboard_texture_sequence_index = 3;
	flight_object_advance_texture_frame_sequence(40);
	XVT_ASSERT_INT_EQ(g_billboard_texture_sequence_index, 4);
	g_billboard_texture_sequence_index = 5;
	flight_object_advance_texture_frame_sequence(40);
	XVT_ASSERT_INT_EQ(g_billboard_texture_sequence_index, 1);
	XVT_ASSERT_INT_EQ(g_test_objects[40].object_type, 129);
	/* 0xFF00 plus 0x100 is index 0. */
	g_billboard_texture_sequence_index = 7;
	flight_object_advance_texture_frame_sequence(40);
	XVT_ASSERT_INT_EQ(g_billboard_texture_sequence_index, 0);

	g_billboard_texture_sequence_index = 6;
	flight_object_advance_texture_frame_sequence(40);
	XVT_ASSERT_INT_EQ(g_test_objects[40].object_type, 0);

	/* On a craft slot the craft's linked objects go too. */
	g_test_objects[3].object_type = 7;
	g_test_objects[41].object_type = 7;
	g_test_objects[42].object_type = 7;
	g_test_craft[3].effective_ai_object_link = &g_test_objects[41];
	g_test_craft[3].turret_object_links[2] = &g_test_objects[42];
	g_billboard_texture_sequence_index = 6;
	flight_object_advance_texture_frame_sequence(3);
	XVT_ASSERT_INT_EQ(g_test_objects[3].object_type, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[41].object_type, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[42].object_type, 0);
	XVT_ASSERT_TRUE(g_test_craft[3].effective_ai_object_link == NULL);
	/* Past the craft slots only the object goes. */
	g_test_objects[41].object_type = 7;
	g_test_objects[8].object_type = 7;
	g_test_craft[0].effective_ai_object_link = &g_test_objects[41];
	g_test_mobiles[8].p_craft = &g_test_craft[0];
	g_billboard_texture_sequence_index = 6;
	flight_object_advance_texture_frame_sequence(8);
	XVT_ASSERT_INT_EQ(g_test_objects[8].object_type, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[41].object_type, 7);

	g_billboard_texture_frame_sequence = NULL;
	g_billboard_texture_sequence_index = 6;
	g_test_objects[40].object_type = 129;
	flight_object_advance_texture_frame_sequence(40);
	XVT_ASSERT_INT_EQ(g_billboard_texture_sequence_index, 6);
	XVT_ASSERT_INT_EQ(g_test_objects[40].object_type, 129);
}

/* ------------------------------------------------------------------------ */
/* Local debris. */

/* Each call looks at the slot under the cursor and moves the cursor on,
 * back to the first local slot past the last. A slot more than 0x800 units
 * from the player's craft becomes small debris of type 110 to 113 (genus 11,
 * family 3, no flight group) placed near the craft: from -512 to 511 along
 * its side and up axes plus its forward axis / 16. A slot within 0x800, the
 * edge included, is left as it was. Each slot is put far away again before
 * its turn, so every call but the near slot's makes debris; over 56 of them
 * each of the four types comes up. */
static void check_debris_recycled_near_player(void)
{
	fresh_world();
	struct object_record *craft = &g_test_objects[PLAYER_CRAFT];
	struct object_record *near = &g_test_objects[DEBRIS_START + 1];
	near->world_x = craft->world_x + 0x800;
	near->world_y = craft->world_y;
	near->world_z = craft->world_z;
	near->object_type = 77;
	int seen[4] = {0, 0, 0, 0};
	for (int call = 0; call < 8 * (DEBRIS_END - DEBRIS_START); ++call) {
		int slot = g_local_debris_recycle_slot_cursor;
		XVT_ASSERT_INT_EQ(slot, DEBRIS_START + call % (DEBRIS_END -
							       DEBRIS_START));
		struct object_record *debris = &g_test_objects[slot];
		if (debris != near) {
			debris->world_x = craft->world_x - 0x900;
			debris->world_y = craft->world_y + 0x100;
			debris->world_z = craft->world_z;
		}
		flight_object_recycle_local_debris_near_player();
		if (debris == near) {
			XVT_ASSERT_INT_EQ(debris->object_type, 77);
			XVT_ASSERT_INT_EQ(debris->world_x,
					  craft->world_x + 0x800);
			continue;
		}
		XVT_ASSERT_TRUE(debris->object_type >= 110);
		XVT_ASSERT_TRUE(debris->object_type <= 113);
		seen[debris->object_type - 110] = 1;
		XVT_ASSERT_INT_EQ(debris->genus_id, 11);
		XVT_ASSERT_INT_EQ(debris->mobj->family, 3);
		XVT_ASSERT_INT_EQ(debris->flight_group_idx, UINT8_MAX);
		XVT_ASSERT_INT_EQ(debris->type_specific_byte[0], 2);
		int side = debris->world_x - craft->world_x;
		int up = debris->world_z - craft->world_z;
		XVT_ASSERT_TRUE(side >= -512 && side <= 511);
		XVT_ASSERT_TRUE(up >= -512 && up <= 511);
		XVT_ASSERT_INT_EQ(debris->world_y - craft->world_y,
				  Q15_ONE >> 4);
	}
	for (int type = 0; type < 4; ++type) {
		XVT_ASSERT_INT_EQ(seen[type], 1);
	}
}

/* Without a craft the player gets no debris, and the cursor stays. */
static void check_debris_without_craft(void)
{
	fresh_world();
	g_players[LOCAL].object_index = -1;
	g_test_objects[DEBRIS_START].world_x = 0x100000;
	flight_object_recycle_local_debris_near_player();
	XVT_ASSERT_INT_EQ(g_local_debris_recycle_slot_cursor, DEBRIS_START);
	XVT_ASSERT_INT_EQ(g_test_objects[DEBRIS_START].object_type, 0);
}

/* ------------------------------------------------------------------------ */
/* The hyperspace jump. */

/* Runs one update of player 0's jump in stage stage, with ticks ticks into
 * it before this step's g_elapsed_ticks are added. */
static void jump_step(int stage, unsigned ticks)
{
	g_players[LOCAL].hyperspace_phase = (uint8_t)stage;
	g_players[LOCAL].hyperspace_runtime.phase_elapsed_ticks = ticks;
	flight_object_update_player_hyperspace_transition(LOCAL);
}

/* Stage 1 turns the craft toward roll 0, yaw 0 and pitch 0x4000 at 16 angle
 * units per elapsed tick, roll at twice that, each stopping where it is
 * aimed; the craft record's pitch follows. The step's ticks are added to the
 * stage's count. */
static void check_jump_turns_onto_line(void)
{
	static const struct {
		uint16_t roll, yaw, pitch;	    /* Before. */
		uint16_t roll_to, yaw_to, pitch_to; /* After 10 ticks. */
	} turns[] = {
		{0x1000, 0x0800, 0x3000, 0x0EC0, 0x0760, 0x30A0},
		{0xF000, 0xF800, 0x5000, 0xF140, 0xF8A0, 0x4F60},
		{0x0100, 0x0050, 0x3FF0, 0x0000, 0x0000, 0x4000},
		{0xFF00, 0xFFA0, 0x4050, 0x0000, 0x0000, 0x4000},
		{0x0000, 0x0000, 0xC100, 0x0000, 0x0000, 0xC1A0},
		{0x0000, 0x0000, 0x8000, 0x0000, 0x0000, 0x7F60},
		{0x0000, 0x0000, 0xC000, 0x0000, 0x0000, 0xC0A0},
	};
	for (size_t i = 0; i < sizeof turns / sizeof turns[0]; ++i) {
		fresh_world();
		struct object_record *craft = &g_test_objects[PLAYER_CRAFT];
		craft->roll = (int16_t)turns[i].roll;
		craft->yaw = (int16_t)turns[i].yaw;
		craft->pitch = (int16_t)turns[i].pitch;
		g_elapsed_ticks = 10;
		jump_step(ALIGN, 100);
		XVT_ASSERT_INT_EQ((uint16_t)craft->roll, turns[i].roll_to);
		XVT_ASSERT_INT_EQ((uint16_t)craft->yaw, turns[i].yaw_to);
		XVT_ASSERT_INT_EQ((uint16_t)craft->pitch, turns[i].pitch_to);
		XVT_ASSERT_INT_EQ(g_test_craft[PLAYER_CRAFT].pitch,
				  turns[i].pitch_to);
		XVT_ASSERT_INT_EQ(
			g_players[LOCAL].hyperspace_runtime.phase_elapsed_ticks,
			110);
		XVT_ASSERT_INT_EQ(g_players[LOCAL].hyperspace_phase, ALIGN);
	}
}

/* On the line, stage 1 moves to stage 2 once 0x49C ticks into it, with
 * IFMSG_108 and the count back at 0; before that it waits. A craft no longer
 * active ends the jump. */
static void check_jump_enters_stage_two(void)
{
	fresh_world();
	g_elapsed_ticks = 4;
	jump_step(ALIGN, STAGE_TICKS - 5);
	XVT_ASSERT_INT_EQ(g_players[LOCAL].hyperspace_phase, ALIGN);
	XVT_ASSERT_INT_EQ(g_message_log_total_count, 0);
	jump_step(ALIGN, STAGE_TICKS - 4);
	XVT_ASSERT_INT_EQ(g_players[LOCAL].hyperspace_phase, DEPART);
	XVT_ASSERT_INT_EQ(
		g_players[LOCAL].hyperspace_runtime.phase_elapsed_ticks, 0);
	XVT_ASSERT_INT_EQ(last_message(), IFMSG_108_ENTERING_HYPERSPACE);

	fresh_world();
	g_test_craft[PLAYER_CRAFT].object_kind = CRAFT_OBJECT_KIND_BREAKING_UP;
	jump_step(ALIGN, STAGE_TICKS);
	XVT_ASSERT_INT_EQ(g_players[LOCAL].hyperspace_phase, 0);
	XVT_ASSERT_INT_EQ(g_message_log_total_count, 0);
}

/* Puts an object of BLOCKER_TYPE in slot obj at the given offset from the
 * jumping craft. */
static void place_blocker(int obj, int dx, int dy, int dz)
{
	struct object_record *craft = &g_test_objects[PLAYER_CRAFT];
	g_test_objects[obj].object_type = BLOCKER_TYPE;
	g_test_objects[obj].world_x = craft->world_x + dx;
	g_test_objects[obj].world_y = craft->world_y + dy;
	g_test_objects[obj].world_z = craft->world_z + dz;
}

/* On its first update stage 2 is called off by a craft or static object
 * ahead along +Y within 0x40000 units and closer sideways, on X and on Z,
 * than the two crafts' sizes: IFMSG_112, hyperspace_phase 0, speed 10. */
static void check_jump_blocked(void)
{
	static const int blocked_at[][3] = {
		{0, 1, 0},
		{0, 0x3FFFF, 0},
		{3999, 1000, -3999},
		{-3999, 0x20000, 3999},
	};
	for (size_t i = 0; i < sizeof blocked_at / sizeof blocked_at[0]; ++i) {
		for (int obj = 2; obj <= 50; obj += 48) {
			fresh_world();
			place_blocker(obj, blocked_at[i][0], blocked_at[i][1],
				      blocked_at[i][2]);
			g_test_mobiles[PLAYER_CRAFT].speed = 500;
			jump_step(DEPART, 0);
			XVT_ASSERT_INT_EQ(g_players[LOCAL].hyperspace_phase, 0);
			XVT_ASSERT_INT_EQ(g_test_mobiles[PLAYER_CRAFT].speed,
					  10);
			XVT_ASSERT_INT_EQ(last_message(), JUMP_ABORTED_MESSAGE);
		}
	}
}

/* An object behind, level with the craft, 0x40000 or more ahead, or as far
 * sideways as the two sizes does not stop the jump; nor does one found
 * after the stage's first update. */
static void check_jump_clear(void)
{
	static const int clear_at[][3] = {
		{0, 0, 0},	{0, -1, 0},    {0, 0x40000, 0}, {4000, 10, 0},
		{-4000, 10, 0}, {0, 10, 4000}, {0, 10, -4000},
	};
	for (size_t i = 0; i < sizeof clear_at / sizeof clear_at[0]; ++i) {
		for (int obj = 2; obj <= 50; obj += 48) {
			fresh_world();
			place_blocker(obj, clear_at[i][0], clear_at[i][1],
				      clear_at[i][2]);
			jump_step(DEPART, 0);
			XVT_ASSERT_INT_EQ(g_players[LOCAL].hyperspace_phase,
					  DEPART);
		}
	}
	fresh_world();
	place_blocker(2, 0, 10, 0);
	jump_step(DEPART, 1);
	XVT_ASSERT_INT_EQ(g_players[LOCAL].hyperspace_phase, DEPART);
	XVT_ASSERT_INT_EQ(g_message_log_total_count, 0);
}

/* Until 0x49C ticks stage 2 moves the craft along +Y by 224 times the step's
 * ticks times the number of 0xEC-tick steps passed. */
static void check_jump_moves_along_line(void)
{
	static const unsigned ticks[] = {0,	0xE8,  0xEC,
					 0x1D8, 0x2C4, STAGE_TICKS - 5};
	for (size_t i = 0; i < sizeof ticks / sizeof ticks[0]; ++i) {
		fresh_world();
		struct object_record *craft = &g_test_objects[PLAYER_CRAFT];
		int x = craft->world_x;
		int y = craft->world_y;
		g_elapsed_ticks = 4;
		jump_step(DEPART, ticks[i]);
		int stage = (int)((ticks[i] + 4) / 0xEC);
		XVT_ASSERT_INT_EQ(craft->world_y - y, 224 * 4 * stage);
		XVT_ASSERT_INT_EQ(craft->world_x, x);
		XVT_ASSERT_INT_EQ(g_players[LOCAL].hyperspace_phase, DEPART);
	}
}

/* ------------------------------------------------------------------------ */
/* The animation step. */

/* The step runs only when the special behavior timer has reached 0, then
 * rearms it to 29 ticks. Crew object types 100 to 105 tumble at rates set by
 * their slot: roll by 29 * (slot >> 4) / 16, pitch by 29 * (slot >> 3) / 32
 * and yaw by 29 * (4 - (slot >> 4)) / 16, slots counted past the static
 * ones. */
static void check_animation_step_tumbles_crew(void)
{
	fresh_world();
	g_region_static_object_slot_count = 0;
	g_test_objects[40].object_type = 100;
	g_test_objects[40].genus_id = CRAFT_GENUS_OBSTACLE;
	g_test_objects[40].roll = 1000;
	g_test_objects[40].pitch = 2000;
	g_test_objects[40].yaw = 3000;
	g_test_objects[20].object_type = 105;
	g_test_objects[20].genus_id = CRAFT_GENUS_OBSTACLE;
	g_test_objects[21].object_type = 106;
	g_test_objects[21].genus_id = CRAFT_GENUS_OBSTACLE;
	g_flight_global_countdown_timers.special_behavior_update_timer = 3;
	flight_object_update_special_behavior();
	XVT_ASSERT_INT_EQ(g_test_objects[40].roll, 1000);
	XVT_ASSERT_INT_EQ(
		g_flight_global_countdown_timers.special_behavior_update_timer,
		3);

	g_flight_global_countdown_timers.special_behavior_update_timer = 0;
	flight_object_update_special_behavior();
	XVT_ASSERT_INT_EQ(
		g_flight_global_countdown_timers.special_behavior_update_timer,
		29);
	XVT_ASSERT_INT_EQ(g_test_objects[40].roll, 1000 + 29 * 2 / 16);
	XVT_ASSERT_INT_EQ(g_test_objects[40].pitch, 2000 + 29 * 5 / 32);
	XVT_ASSERT_INT_EQ(g_test_objects[40].yaw, 3000 + 29 * 2 / 16);
	XVT_ASSERT_INT_EQ(g_test_objects[20].roll, 29 * 1 / 16);
	XVT_ASSERT_INT_EQ(g_test_objects[20].pitch, 29 * 2 / 32);
	XVT_ASSERT_INT_EQ(g_test_objects[20].yaw, 29 * 3 / 16);
	XVT_ASSERT_INT_EQ(g_test_objects[21].roll, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[21].yaw, 0);
}

int main(void)
{
	g_message_log = memory_alloc_handle_zeroed(
		301 * sizeof(struct hud_in_flight_message_record), 0);
	XVT_ASSERT_TRUE(g_message_log != 0);
	check_texture_sequence();
	check_debris_recycled_near_player();
	check_debris_without_craft();
	check_jump_turns_onto_line();
	check_jump_enters_stage_two();
	check_jump_blocked();
	check_jump_clear();
	check_jump_moves_along_line();
	check_animation_step_tumbles_crew();
	return 0;
}
