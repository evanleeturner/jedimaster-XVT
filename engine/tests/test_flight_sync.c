/* Tests for xvt/net/flight_sync.c, the flight's input histories, the drawing
 * of remote players' craft between simulation steps, and the world checksum
 * answers. Each check sets the game state it needs itself: the player
 * records, the input histories, a small object table this file owns, the
 * network session's ids, the local world checksum and the checksum epoch. No
 * game data is read and no DirectPlay session is open.
 *
 * Not checked here: what a resync sends. With no transfer under way,
 * xvt_resync_begin_send draws an alert on the flight display, which this
 * program does not have, so the checks whose packets start a resync keep
 * another transfer under way, which it leaves alone. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/flight_sync.h"
#include "xvt/net/net_session.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/resync_task.h"

extern struct remote_player_render_sample g_remote_player_render_samples[8];
extern struct remote_player_saved_sim_pose g_remote_player_saved_sim_poses[8];

enum {
	EPOCH = 6,
	OBJECT_COUNT = 4,
	CHECKSUM_WORDS =
		35, /* Type, epoch, 16 checksums, 16 lengths, request */
	REQUEST_WORD = 34,
};

static struct object_record g_test_objects[OBJECT_COUNT];
static struct mobile_object g_test_mobiles[OBJECT_COUNT];

/* Players 0 and 1 are flying, player 0 local; player n has DirectPlay id
 * 100 + n. Every input history is empty, every checksum answer is still
 * missing, and the local world checksum's 16 words are 1 to 16 for epoch 6,
 * with world messages kept until the checksums are compared. */
static void flight_sync_world(int host)
{
	memset(g_players, 0, sizeof g_players);
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
		g_player_abort_flags[i] = 0;
		g_flight_net_world_checksum_peer_status[i] = 0;
	}
	g_players[0].participation_state = 1;
	g_players[1].participation_state = 1;
	g_local_player = 0;
	memset(&g_net_session, 0, sizeof g_net_session);
	g_net_session.local_is_host = host;
	g_net_session.host_dplay_id = 100;
	for (int i = 0; i < 8; ++i) {
		g_net_session.players[i].direct_play_id = 100 + i;
	}
	memset(g_input_history, 0, sizeof g_input_history);
	memset(g_input_frame_count, 0, sizeof g_input_frame_count);
	for (int i = 0; i < 16; ++i) {
		g_world_checksum[i] = (unsigned)i + 1;
	}
	g_flight_net_world_checksum_epoch = EPOCH;
	g_flight_net_buffer_world_messages_until_checksum = 1;
	xvt_resync_reset();
	xvt_flight_messages_reset();
}

/* Appends a frame at tick to player's history, which the caller keeps in tick
 * order. */
static void add_frame(int player, int tick, int source, int awaiting_relay)
{
	struct input_frame *frame =
		&g_input_history[player][g_input_frame_count[player]++];
	frame->timestamp = tick;
	frame->input_source = source;
	frame->awaiting_relay = awaiting_relay;
}

static int tick_at(int player, int index)
{
	return g_input_history[player][index].timestamp;
}

/* ------------------------------------------------------------------------ */
/* Input histories. */

/* Only predicted frames that do not await relay go; the rest keep their
 * order. */
static void check_discard_predicted(void)
{
	flight_sync_world(1);
	g_players[2].participation_state = 1;
	add_frame(2, 10, 1, 0);
	add_frame(2, 11, 2, 0);
	add_frame(2, 12, 2, 1);
	add_frame(2, 13, 2, 0);
	add_frame(2, 14, 0, 0);
	flight_sync_discard_predicted_input_frames(2);
	XVT_ASSERT_INT_EQ(g_input_frame_count[2], 3);
	XVT_ASSERT_INT_EQ(tick_at(2, 0), 10);
	XVT_ASSERT_INT_EQ(tick_at(2, 1), 12);
	XVT_ASSERT_INT_EQ(tick_at(2, 2), 14);
}

/* The local player's history and an inactive player's are left alone. */
static void check_discard_skips_local_and_inactive(void)
{
	flight_sync_world(1);
	add_frame(0, 10, 2, 0);
	add_frame(3, 10, 2, 0);
	flight_sync_discard_predicted_input_frames(0);
	flight_sync_discard_predicted_input_frames(3);
	XVT_ASSERT_INT_EQ(g_input_frame_count[0], 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[3], 1);
}

/* Removing the second of four frames moves the later two down one place. */
static void check_remove_frame(void)
{
	flight_sync_world(1);
	for (int tick = 10; tick < 14; ++tick) {
		add_frame(1, tick, 1, 0);
	}
	flight_sync_remove_input_history_frame(1, &g_input_history[1][1]);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 3);
	XVT_ASSERT_INT_EQ(tick_at(1, 0), 10);
	XVT_ASSERT_INT_EQ(tick_at(1, 1), 12);
	XVT_ASSERT_INT_EQ(tick_at(1, 2), 13);
	/* The first frame can be removed too. */
	flight_sync_remove_input_history_frame(1, &g_input_history[1][0]);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 2);
	XVT_ASSERT_INT_EQ(tick_at(1, 0), 12);
	XVT_ASSERT_INT_EQ(tick_at(1, 1), 13);
}

/* An empty history, and a frame before the player's row, remove nothing. */
static void check_remove_refusals(void)
{
	flight_sync_world(1);
	flight_sync_remove_input_history_frame(1, &g_input_history[1][0]);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 0);
	add_frame(2, 10, 1, 0);
	add_frame(1, 10, 1, 0);
	flight_sync_remove_input_history_frame(2, &g_input_history[1][0]);
	XVT_ASSERT_INT_EQ(g_input_frame_count[2], 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 1);
}

static struct flight_input_frame_record controls(uint8_t key)
{
	struct flight_input_frame_record input;
	memset(&input, 0, sizeof input);
	input.key = key;
	return input;
}

/* New ticks go in in tick order, each a real input that does not await
 * relay. */
static void check_insert_in_order(void)
{
	static const int ticks[] = {30, 10, 20};
	flight_sync_world(1);
	for (int i = 0; i < 3; ++i) {
		struct flight_input_frame_record input = controls(1);
		struct input_frame *frame =
			flight_sync_insert_input_frame(1, ticks[i], &input);
		XVT_ASSERT_TRUE(frame != NULL);
		XVT_ASSERT_INT_EQ(frame->timestamp, ticks[i]);
		XVT_ASSERT_INT_EQ(frame->input_source, 1);
		XVT_ASSERT_INT_EQ(frame->awaiting_relay, 0);
	}
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 3);
	XVT_ASSERT_INT_EQ(tick_at(1, 0), 10);
	XVT_ASSERT_INT_EQ(tick_at(1, 1), 20);
	XVT_ASSERT_INT_EQ(tick_at(1, 2), 30);
}

/* A frame at a tick already held replaces it, unless the held one came from
 * the server or awaits relay; those refuse it. */
static void check_insert_same_tick(void)
{
	struct flight_input_frame_record input = controls(7);
	flight_sync_world(1);
	add_frame(1, 10, 2, 0);
	add_frame(1, 20, 0, 0);
	add_frame(1, 30, 1, 1);
	struct input_frame *frame =
		flight_sync_insert_input_frame(1, 10, &input);
	XVT_ASSERT_TRUE(frame == &g_input_history[1][0]);
	XVT_ASSERT_INT_EQ(frame->input.key, 7);
	XVT_ASSERT_INT_EQ(frame->input_source, 1);
	XVT_ASSERT_TRUE(flight_sync_insert_input_frame(1, 20, &input) == NULL);
	XVT_ASSERT_TRUE(flight_sync_insert_input_frame(1, 30, &input) == NULL);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 3);
	XVT_ASSERT_INT_EQ(g_input_history[1][1].input.key, 0);
	XVT_ASSERT_INT_EQ(g_input_history[1][2].input.key, 0);
}

/* With all 450 frames in use a new tick is refused. */
static void check_insert_full(void)
{
	struct flight_input_frame_record input = controls(1);
	flight_sync_world(1);
	for (int tick = 0; tick < 450; ++tick) {
		add_frame(1, tick, 1, 0);
	}
	XVT_ASSERT_TRUE(flight_sync_insert_input_frame(1, 450, &input) == NULL);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 450);
}

/* The last frame still awaiting relay is found; with none, NULL. */
static void check_find_last_unrelayed(void)
{
	flight_sync_world(1);
	add_frame(1, 10, 1, 0);
	XVT_ASSERT_TRUE(flight_sync_find_last_unrelayed_input_frame(1) == NULL);
	add_frame(1, 11, 1, 1);
	add_frame(1, 12, 1, 0);
	add_frame(1, 13, 1, 1);
	add_frame(1, 14, 1, 0);
	XVT_ASSERT_TRUE(flight_sync_find_last_unrelayed_input_frame(1) ==
			&g_input_history[1][3]);
}

/* ------------------------------------------------------------------------ */
/* Remote craft drawn between steps. */

/* Player 1 flies object 2, a craft at (1000, 2000, 3000) facing (100, 200,
 * 300), moving along X at speed 0, with simulation time stamp 50. Smoothing
 * is on, and every sample and saved pose is invalid. */
static void smoothing_world(void)
{
	flight_sync_world(1);
	memset(g_test_objects, 0, sizeof g_test_objects);
	memset(g_test_mobiles, 0, sizeof g_test_mobiles);
	g_object_table = g_test_objects;
	g_test_objects[2].object_type = 1;
	g_test_objects[2].mobj = &g_test_mobiles[2];
	g_test_objects[2].object_signature = 77;
	g_test_objects[2].world_x = 1000;
	g_test_objects[2].world_y = 2000;
	g_test_objects[2].world_z = 3000;
	g_test_objects[2].roll = 100;
	g_test_objects[2].pitch = 200;
	g_test_objects[2].yaw = 300;
	g_test_mobiles[2].move_x = 0x7fff;
	g_test_mobiles[2].sim_state_timestamp = 50;
	g_players[1].object_index = 2;
	g_players[1].bound_object_signature = 77;
	g_remote_player_render_smoothing_enabled = 1;
	flight_sync_reset_remote_player_render_smoothing();
}

/* Reset marks every sample and saved pose invalid. */
static void check_reset_smoothing(void)
{
	for (int i = 0; i < 8; ++i) {
		g_remote_player_render_samples[i].valid = 1;
		g_remote_player_saved_sim_poses[i].valid = 1;
	}
	flight_sync_reset_remote_player_render_smoothing();
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(g_remote_player_render_samples[i].valid, 0);
		XVT_ASSERT_INT_EQ(g_remote_player_saved_sim_poses[i].valid, 0);
	}
}

/* A first sample records the craft as drawn, with no turn yet; the local
 * player and players without a craft get no valid sample. */
static void check_capture_first_sample(void)
{
	smoothing_world();
	g_remote_player_render_samples[0].valid = 1;
	g_remote_player_render_samples[3].valid = 1;
	flight_sync_capture_samples_and_restore_poses();
	const struct remote_player_render_sample *sample =
		&g_remote_player_render_samples[1];
	XVT_ASSERT_INT_EQ(sample->valid, 1);
	XVT_ASSERT_INT_EQ(sample->object_signature, 77);
	XVT_ASSERT_INT_EQ(sample->world_x, 1000);
	XVT_ASSERT_INT_EQ(sample->world_y, 2000);
	XVT_ASSERT_INT_EQ(sample->world_z, 3000);
	XVT_ASSERT_INT_EQ(sample->roll, 100);
	XVT_ASSERT_INT_EQ(sample->pitch, 200);
	XVT_ASSERT_INT_EQ(sample->yaw, 300);
	XVT_ASSERT_INT_EQ(sample->roll_delta, 0);
	XVT_ASSERT_INT_EQ(sample->pitch_delta, 0);
	XVT_ASSERT_INT_EQ(sample->yaw_delta, 0);
	XVT_ASSERT_INT_EQ(sample->move_x, 0x7fff);
	XVT_ASSERT_INT_EQ(sample->sim_state_timestamp, 50);
	XVT_ASSERT_INT_EQ(g_remote_player_render_samples[0].valid, 0);
	XVT_ASSERT_INT_EQ(g_remote_player_render_samples[3].valid, 0);
}

/* A second sample records each angle's change since the first. */
static void check_capture_turn(void)
{
	smoothing_world();
	flight_sync_capture_samples_and_restore_poses();
	g_test_objects[2].roll = 110;
	g_test_objects[2].pitch = 190;
	g_test_objects[2].yaw = 300;
	flight_sync_capture_samples_and_restore_poses();
	XVT_ASSERT_INT_EQ(g_remote_player_render_samples[1].roll_delta, 10);
	XVT_ASSERT_INT_EQ(g_remote_player_render_samples[1].pitch_delta, -10);
	XVT_ASSERT_INT_EQ(g_remote_player_render_samples[1].yaw_delta, 0);
	XVT_ASSERT_INT_EQ(g_remote_player_render_samples[1].roll, 110);
}

/* A stale move vector is recomputed from the craft's pitch and yaw before it
 * is sampled. */
static void check_capture_stale_move_vector(void)
{
	smoothing_world();
	g_test_mobiles[2].move_vector_dirty = 1;
	flight_sync_capture_samples_and_restore_poses();
	const struct remote_player_render_sample *sample =
		&g_remote_player_render_samples[1];
	XVT_ASSERT_TRUE(sample->move_x != 0x7fff);
	fview_calcrotatemove(200, 300, NULL);
	XVT_ASSERT_INT_EQ(sample->move_x, (int16_t)g_fview_move_x_q15);
	XVT_ASSERT_INT_EQ(sample->move_y, (int16_t)g_fview_move_y_q15);
	XVT_ASSERT_INT_EQ(sample->move_z, (int16_t)g_fview_move_z_q15);
}

/* After a sample, the craft moves on in the simulation with no simulation time
 * passed: it is drawn half way from the sample to its simulated position, and
 * the simulated pose, saved first, is put back after drawing. */
static void check_apply_and_restore(void)
{
	smoothing_world();
	flight_sync_capture_samples_and_restore_poses();
	g_test_objects[2].world_x = 1400;
	g_test_objects[2].world_y = 2200;
	g_test_objects[2].world_z = 3000;
	flight_sync_apply_remote_player_render_smoothing();
	const struct remote_player_saved_sim_pose *saved =
		&g_remote_player_saved_sim_poses[1];
	XVT_ASSERT_INT_EQ(saved->valid, 1);
	XVT_ASSERT_INT_EQ(saved->world_x, 1400);
	XVT_ASSERT_INT_EQ(saved->world_y, 2200);
	XVT_ASSERT_INT_EQ(saved->world_z, 3000);
	XVT_ASSERT_INT_EQ(saved->roll, 100);
	XVT_ASSERT_INT_EQ(g_test_objects[2].world_x, 1000 + (1400 - 1000) / 2);
	XVT_ASSERT_INT_EQ(g_test_objects[2].world_y, 2000 + (2200 - 2000) / 2);
	XVT_ASSERT_INT_EQ(g_test_objects[2].world_z, 3000);
	flight_sync_capture_samples_and_restore_poses();
	XVT_ASSERT_INT_EQ(g_test_objects[2].world_x, 1400);
	XVT_ASSERT_INT_EQ(g_test_objects[2].world_y, 2200);
	XVT_ASSERT_INT_EQ(g_test_objects[2].world_z, 3000);
}

/* A craft is left as simulated when its sample belongs to another object or
 * its simulation time stamp is older than the sample's, and everything is
 * left alone while smoothing is off. */
static void check_apply_leaves_craft(void)
{
	smoothing_world();
	flight_sync_capture_samples_and_restore_poses();
	g_test_objects[2].world_x = 1400;
	g_players[1].bound_object_signature = 78;
	flight_sync_apply_remote_player_render_smoothing();
	XVT_ASSERT_INT_EQ(g_test_objects[2].world_x, 1400);
	XVT_ASSERT_INT_EQ(g_remote_player_saved_sim_poses[1].valid, 0);

	g_players[1].bound_object_signature = 77;
	g_test_mobiles[2].sim_state_timestamp = 49;
	flight_sync_apply_remote_player_render_smoothing();
	XVT_ASSERT_INT_EQ(g_test_objects[2].world_x, 1400);

	g_test_mobiles[2].sim_state_timestamp = 50;
	g_remote_player_render_smoothing_enabled = 0;
	flight_sync_apply_remote_player_render_smoothing();
	XVT_ASSERT_INT_EQ(g_test_objects[2].world_x, 1400);
	g_test_objects[2].world_x = 1600;
	flight_sync_capture_samples_and_restore_poses();
	XVT_ASSERT_INT_EQ(g_remote_player_render_samples[1].world_x, 1000);
	g_remote_player_render_smoothing_enabled = 1;
}

/* Samples player 1's craft moving along (0x7fff, 0x4000, -0x4000) at speed,
 * then draws it elapsed simulation ticks later, still at the sampled position
 * in the simulation. */
static void draw_after(int elapsed, int speed)
{
	smoothing_world();
	g_test_mobiles[2].move_y = 0x4000;
	g_test_mobiles[2].move_z = -0x4000;
	g_test_mobiles[2].speed = (uint16_t)speed;
	flight_sync_capture_samples_and_restore_poses();
	g_test_mobiles[2].sim_state_timestamp = 50 + elapsed;
	flight_sync_apply_remote_player_render_smoothing();
}

/* Once simulation time has passed since a sample with speed, even one tick,
 * the craft is drawn ahead of the sampled position along the sampled move
 * vector, further after more time or at more speed; at speed 0 it stays where
 * it is. */
static void check_apply_projects_along_move_vector(void)
{
	draw_after(1, 100);
	XVT_ASSERT_TRUE(g_test_objects[2].world_x > 1000);
	draw_after(SIMULATION_TICKS_PER_SECOND, 100);
	int ahead_x = g_test_objects[2].world_x;
	XVT_ASSERT_TRUE(ahead_x > 1000);
	XVT_ASSERT_TRUE(g_test_objects[2].world_y > 2000);
	XVT_ASSERT_TRUE(g_test_objects[2].world_z < 3000);
	draw_after(2 * SIMULATION_TICKS_PER_SECOND, 100);
	XVT_ASSERT_TRUE(g_test_objects[2].world_x > ahead_x);
	draw_after(SIMULATION_TICKS_PER_SECOND, 200);
	XVT_ASSERT_TRUE(g_test_objects[2].world_x > ahead_x);
	draw_after(SIMULATION_TICKS_PER_SECOND, 0);
	XVT_ASSERT_INT_EQ(g_test_objects[2].world_x, 1000);
	XVT_ASSERT_INT_EQ(g_test_objects[2].world_y, 2000);
	XVT_ASSERT_INT_EQ(g_test_objects[2].world_z, 3000);
}

/* With no move vector the projection stays at the sampled position. At speed
 * 100 a craft travels (4,660 * 100 + 128) / 256 = 1,820 world units in a
 * simulated second (the speed's comment in object.h), so a gap of 20,000 is
 * within 32 times the distance and the craft is moved toward its simulated
 * position by less than half the gap; a gap of 100,000 is not, and it is moved
 * by half. */
static void check_apply_smaller_share(void)
{
	static const int gaps[] = {20000, 100000};
	for (int i = 0; i < 2; ++i) {
		smoothing_world();
		g_test_mobiles[2].move_x = 0;
		g_test_mobiles[2].speed = 100;
		flight_sync_capture_samples_and_restore_poses();
		g_test_mobiles[2].sim_state_timestamp =
			50 + SIMULATION_TICKS_PER_SECOND;
		g_test_objects[2].world_y = 2000 + gaps[i];
		flight_sync_apply_remote_player_render_smoothing();
		XVT_ASSERT_INT_EQ(g_test_objects[2].world_x, 1000);
		XVT_ASSERT_INT_EQ(g_test_objects[2].world_z, 3000);
		if (i == 0) {
			XVT_ASSERT_TRUE(g_test_objects[2].world_y > 2000);
			XVT_ASSERT_TRUE(g_test_objects[2].world_y <
					2000 + gaps[0] / 2);
		} else {
			XVT_ASSERT_INT_EQ(g_test_objects[2].world_y,
					  2000 + gaps[1] / 2);
		}
	}
}

/* Samples player 1's craft twice, turned by turn_roll, turn_pitch and turn_yaw
 * between the samples, then moves each simulated angle back by a tenth of its
 * turn and draws the craft with no simulation time passed. */
static void draw_turned_back(int turn_roll, int turn_pitch, int turn_yaw)
{
	smoothing_world();
	flight_sync_capture_samples_and_restore_poses();
	g_test_objects[2].roll = (uint16_t)(100 + turn_roll);
	g_test_objects[2].pitch = (uint16_t)(200 + turn_pitch);
	g_test_objects[2].yaw = (uint16_t)(300 + turn_yaw);
	flight_sync_capture_samples_and_restore_poses();
	g_test_objects[2].roll = (uint16_t)(100 + turn_roll - turn_roll / 10);
	g_test_objects[2].pitch =
		(uint16_t)(200 + turn_pitch - turn_pitch / 10);
	g_test_objects[2].yaw = (uint16_t)(300 + turn_yaw - turn_yaw / 10);
	flight_sync_apply_remote_player_render_smoothing();
}

/* An angle that moved against the sampled turn is held at the sampled angle:
 * roll and yaw turned up, pitch down. */
static void check_apply_holds_turned_back(void)
{
	draw_turned_back(10, -10, 10);
	XVT_ASSERT_INT_EQ(g_test_objects[2].roll, 110);
	XVT_ASSERT_INT_EQ(g_test_objects[2].pitch, 190);
	XVT_ASSERT_INT_EQ(g_test_objects[2].yaw, 310);
}

/* The same with each turn the other way: roll and yaw turned down, pitch
 * up. */
static void check_apply_holds_turned_back_other_way(void)
{
	draw_turned_back(-10, 10, -10);
	XVT_ASSERT_INT_EQ(g_test_objects[2].roll, 90);
	XVT_ASSERT_INT_EQ(g_test_objects[2].pitch, 210);
	XVT_ASSERT_INT_EQ(g_test_objects[2].yaw, 290);
}

/* The step that compares an angle's change with max_angle_change changes
 * nothing: changes of 100 either way one tick after the sample, and a change
 * of 10,000 from 39,900 to 49,900 (an angle sampled as -25,636) 320 ticks
 * after it, where the angle it sets differs from the drawn one by 65,536. */
static void check_apply_angle_step_changes_nothing(void)
{
	for (int sign = -1; sign <= 1; sign += 2) {
		smoothing_world();
		flight_sync_capture_samples_and_restore_poses();
		g_test_objects[2].roll = (uint16_t)(100 + sign * 100);
		g_test_objects[2].pitch = (uint16_t)(200 - sign * 100);
		g_test_objects[2].yaw = (uint16_t)(300 + sign * 100);
		g_test_mobiles[2].sim_state_timestamp = 51;
		flight_sync_apply_remote_player_render_smoothing();
		XVT_ASSERT_INT_EQ(g_test_objects[2].roll, 100 + sign * 100);
		XVT_ASSERT_INT_EQ(g_test_objects[2].pitch, 200 - sign * 100);
		XVT_ASSERT_INT_EQ(g_test_objects[2].yaw, 300 + sign * 100);
	}

	smoothing_world();
	g_test_objects[2].yaw = 39900;
	flight_sync_capture_samples_and_restore_poses();
	XVT_ASSERT_INT_EQ(g_remote_player_render_samples[1].yaw, -25636);
	g_test_objects[2].yaw = 49900;
	g_test_mobiles[2].sim_state_timestamp = 50 + 320;
	flight_sync_apply_remote_player_render_smoothing();
	XVT_ASSERT_INT_EQ(g_test_objects[2].yaw, 49900);
}

/* ------------------------------------------------------------------------ */
/* World checksums. */

/* A player's checksum for the current epoch holding the local words. */
static void world_checksum_packet(int *packet)
{
	memset(packet, 0, CHECKSUM_WORDS * sizeof *packet);
	packet[0] = NET_PACKET_WORLD_CHECKSUM;
	packet[1] = EPOCH;
	for (int i = 0; i < 16; ++i) {
		packet[2 + i] = (int)g_world_checksum[i];
		packet[18 + i] = 100;
	}
}

/* On the host, a matching checksum from each flying player marks it matched;
 * buffering stops once both have. Only another player's words are compared:
 * the host's own checksum is recorded as matched whatever it holds. */
static void check_host_records_matches(void)
{
	int packet[CHECKSUM_WORDS];
	flight_sync_world(1);
	world_checksum_packet(packet);
	flight_sync_handle_world_checksum_packet(101, packet);
	XVT_ASSERT_INT_EQ(g_flight_net_world_checksum_peer_status[1], 1);
	XVT_ASSERT_INT_EQ(g_flight_net_buffer_world_messages_until_checksum, 1);
	packet[2 + 15] += 1;
	flight_sync_handle_world_checksum_packet(100, packet);
	XVT_ASSERT_INT_EQ(g_flight_net_world_checksum_peer_status[0], 1);
	XVT_ASSERT_INT_EQ(g_flight_net_buffer_world_messages_until_checksum, 0);
}

/* Ignored: another epoch, a sender with no slot, a request code above 1, a
 * player who left and an inactive player. */
static void check_host_ignores(void)
{
	int packet[CHECKSUM_WORDS];
	flight_sync_world(1);
	world_checksum_packet(packet);
	packet[1] = EPOCH + 1;
	flight_sync_handle_world_checksum_packet(101, packet);
	world_checksum_packet(packet);
	flight_sync_handle_world_checksum_packet(999, packet);
	packet[REQUEST_WORD] = 2;
	flight_sync_handle_world_checksum_packet(101, packet);
	packet[REQUEST_WORD] = 0;
	g_player_abort_flags[1] = 1;
	flight_sync_handle_world_checksum_packet(101, packet);
	flight_sync_handle_world_checksum_packet(102, packet);
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(g_flight_net_world_checksum_peer_status[i],
				  0);
	}
	XVT_ASSERT_INT_EQ(g_flight_net_buffer_world_messages_until_checksum, 1);
}

/* A client records nothing, even for a match. */
static void check_client_records_nothing(void)
{
	int packet[CHECKSUM_WORDS];
	flight_sync_world(0);
	world_checksum_packet(packet);
	flight_sync_handle_world_checksum_packet(101, packet);
	flight_sync_handle_world_checksum_packet(100, packet);
	XVT_ASSERT_INT_EQ(g_flight_net_world_checksum_peer_status[0], 0);
	XVT_ASSERT_INT_EQ(g_flight_net_world_checksum_peer_status[1], 0);
	XVT_ASSERT_INT_EQ(g_flight_net_buffer_world_messages_until_checksum, 1);
}

/* On the host, another player's request for the world state (request code 1)
 * is answered with a resync instead of being taken as a checksum, so nothing
 * is recorded for that player although its words match; the host's own
 * request is taken as its checksum and recorded as matched. A transfer to
 * player 2 is under way. */
static void check_host_answers_state_request(void)
{
	int packet[CHECKSUM_WORDS];
	flight_sync_world(1);
	xvt_resync_begin_apply(102, 64);
	world_checksum_packet(packet);
	packet[REQUEST_WORD] = XVT_CHECKSUM_REQUEST_STATE;
	flight_sync_handle_world_checksum_packet(101, packet);
	XVT_ASSERT_INT_EQ(g_flight_net_world_checksum_peer_status[1], 0);
	flight_sync_handle_world_checksum_packet(100, packet);
	XVT_ASSERT_INT_EQ(g_flight_net_world_checksum_peer_status[0], 1);
}

/* On the host, another player's checksums that differ from the local ones
 * start a resync and the handler returns, so the player is not recorded. The
 * function's comment says the host then records it as not matched (2); the
 * resync does that when it ends (issue #116). A transfer to player 2 is under
 * way. */
static void check_host_mismatch_returns(void)
{
	int packet[CHECKSUM_WORDS];
	flight_sync_world(1);
	xvt_resync_begin_apply(102, 64);
	world_checksum_packet(packet);
	packet[2 + 15] += 1;
	flight_sync_handle_world_checksum_packet(101, packet);
	XVT_ASSERT_INT_EQ(g_flight_net_world_checksum_peer_status[1], 0);
}

/* The server's checksum for the current epoch, holding the local words. */
static void server_checksum_packet(uint32_t *packet)
{
	memset(packet, 0, CHECKSUM_WORDS * sizeof *packet);
	packet[0] = NET_PACKET_SERVER_CHECKSUM;
	packet[1] = EPOCH;
	for (int i = 0; i < 16; ++i) {
		packet[2 + i] = g_world_checksum[i];
	}
}

static void keep_one_replay_message(void)
{
	static const uint8_t message[8] = {1, 2, 3, 4, 5, 6, 7, 8};
	XVT_ASSERT_TRUE(xvt_flight_messages_push(XVT_QUEUE_REPLAY, message,
						 sizeof message) != 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_REPLAY), 1);
}

/* On a client, a matching server checksum stops buffering and empties the
 * kept messages; a mismatch keeps both. */
static void check_server_checksum(void)
{
	uint32_t packet[CHECKSUM_WORDS];
	flight_sync_world(0);
	keep_one_replay_message();
	server_checksum_packet(packet);
	packet[2 + 15] += 1;
	flight_sync_handle_server_checksum_packet((uint8_t *)packet);
	XVT_ASSERT_INT_EQ(g_flight_net_buffer_world_messages_until_checksum, 1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_REPLAY), 1);

	server_checksum_packet(packet);
	flight_sync_handle_server_checksum_packet((uint8_t *)packet);
	XVT_ASSERT_INT_EQ(g_flight_net_buffer_world_messages_until_checksum, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_REPLAY), 0);
}

/* The host, and a client given another epoch, ignore a server checksum. */
static void check_server_checksum_ignored(void)
{
	uint32_t packet[CHECKSUM_WORDS];
	flight_sync_world(1);
	server_checksum_packet(packet);
	flight_sync_handle_server_checksum_packet((uint8_t *)packet);
	XVT_ASSERT_INT_EQ(g_flight_net_buffer_world_messages_until_checksum, 1);

	flight_sync_world(0);
	keep_one_replay_message();
	server_checksum_packet(packet);
	packet[1] = EPOCH + 1;
	flight_sync_handle_server_checksum_packet((uint8_t *)packet);
	XVT_ASSERT_INT_EQ(g_flight_net_buffer_world_messages_until_checksum, 1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_REPLAY), 1);
}

/* The saved world state is copied whole into the resync copy, with its size. */
static void check_snapshot(void)
{
	static uint8_t world[64];
	static uint8_t copy[64];
	uint8_t *saved_world = g_world_state_buffer;
	uint8_t *saved_copy = g_world_state_dup_buffer;
	unsigned int saved_size = g_world_state_size;
	for (int i = 0; i < 64; ++i) {
		world[i] = (uint8_t)(i * 3 + 1);
	}
	memset(copy, 0, sizeof copy);
	g_world_state_buffer = world;
	g_world_state_dup_buffer = copy;
	g_world_state_size = 48;
	g_world_state_dup_size = 0;
	flight_sync_snapshot_world_state_for_replay();
	XVT_ASSERT_INT_EQ(g_world_state_dup_size, 48);
	XVT_ASSERT_INT_EQ(memcmp(copy, world, 48), 0);
	XVT_ASSERT_INT_EQ(copy[48], 0);
	g_world_state_buffer = saved_world;
	g_world_state_dup_buffer = saved_copy;
	g_world_state_size = saved_size;
}

/* Clearing empties the kept world messages. */
static void check_clear_buffered(void)
{
	flight_sync_world(0);
	keep_one_replay_message();
	flight_sync_clear_buffered_world_messages();
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_REPLAY), 0);
}

int main(void)
{
	check_discard_predicted();
	check_discard_skips_local_and_inactive();
	check_remove_frame();
	check_remove_refusals();
	check_insert_in_order();
	check_insert_same_tick();
	check_insert_full();
	check_find_last_unrelayed();
	check_reset_smoothing();
	check_capture_first_sample();
	check_capture_turn();
	check_capture_stale_move_vector();
	check_apply_and_restore();
	check_apply_leaves_craft();
	check_apply_projects_along_move_vector();
	check_apply_smaller_share();
	check_apply_holds_turned_back();
	check_apply_holds_turned_back_other_way();
	check_apply_angle_step_changes_nothing();
	check_host_records_matches();
	check_host_ignores();
	check_client_records_nothing();
	check_host_answers_state_request();
	check_host_mismatch_returns();
	check_server_checksum();
	check_server_checksum_ignored();
	check_snapshot();
	check_clear_buffered();
	return 0;
}
