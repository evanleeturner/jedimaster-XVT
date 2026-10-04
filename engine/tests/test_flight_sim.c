/* Checks the per-player input history of the flight simulation (xvt_runtime/runtime/flight_sim.h) against the
 * promises in its header: Insert and InsertReal, what a world restore and a state recovery keep, and what
 * Reset clears. No game data is read: the test sets the histories, the player records and the network
 * session's local player id itself. Player 0 is the local player; players 1 and 2 are connected remote
 * players; the others are not connected. Every check starts from empty histories on a client.
 *
 * Not checked here: StepToTime, Advance and UpdateEntity step the recovered game's simulation (AI, weapons,
 * collisions, movement, mission logic, HUD and sound) and need a running flight of the recovered game. A
 * pause starts only inside UpdateEntity on the local Alt-P key, so Resume is checked only while not
 * paused. */
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/player/player.h"
#include "xvt/net/flight_sync.h"
#include "xvt/net/net_session.h"
#include "xvt_runtime/runtime/flight_prediction.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/flight_sim.h"

static void flight_sim_world(void)
{
	memset(g_players, 0, sizeof g_players);
	memset(g_input_history, 0, sizeof g_input_history);
	memset(g_input_frame_count, 0, sizeof g_input_frame_count);
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
	}
	for (int i = 0; i < 3; ++i) {
		g_players[i].participation_state = 1;
		g_players[i].object_index = i;
	}
	g_local_player = 0;
	g_net_session.local_is_host = 0;
	g_game_time = 0;
	xvt_flight_sim_reset();
}

static struct flight_input_frame_record controls(int8_t axis)
{
	struct flight_input_frame_record input;
	memset(&input, 0, sizeof input);
	input.key = 0x61;
	input.axis_x = axis;
	input.axis_y = (int8_t)-axis;
	input.axis_r = 2;
	input.key_mods = 1;
	input.flags = XVT_INPUT_THROTTLE_PRESENT;
	input.throttle = 300;
	return input;
}

/* Appends a frame to player's history, which the caller keeps in tick order. */
static void add_frame(unsigned player, int tick, int valid, int applied,
		      int8_t axis)
{
	struct input_frame *frame =
		&g_input_history[player][g_input_frame_count[player]++];
	frame->timestamp = tick;
	frame->input_source = valid;
	frame->awaiting_relay = applied;
	frame->input = controls(axis);
}

/* Checks that player's history holds exactly the frames at ticks, in that order. */
static void assert_ticks(unsigned player, const int *ticks, int count)
{
	XVT_ASSERT_INT_EQ(g_input_frame_count[player], count);
	for (int i = 0; i < count; ++i) {
		XVT_ASSERT_INT_EQ(g_input_history[player][i].timestamp,
				  ticks[i]);
	}
}

static int same_input(const struct flight_input_frame_record *a,
		      const struct flight_input_frame_record *b)
{
	return a->key == b->key && a->axis_x == b->axis_x &&
	       a->axis_y == b->axis_y && a->axis_r == b->axis_r &&
	       a->key_mods == b->key_mods && a->flags == b->flags &&
	       a->throttle == b->throttle;
}

static void check_reset_and_pause(void)
{
	flight_sim_world();
	XVT_ASSERT_INT_EQ(xvt_flight_sim_is_paused(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_sim_resume(), 1);

	/* Reset clears the prediction fallback (flight_prediction.h): confirmed controls are forgotten. */
	struct flight_input_frame_record input = controls(20);
	xvt_flight_prediction_confirm(1, 2, &input);
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(8), 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 1);
	flight_sim_world();
	xvt_flight_prediction_confirm(1, 2, &input);
	xvt_flight_sim_reset();
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(8), 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 0);
}

static void check_insert_refusals(void)
{
	flight_sim_world();
	struct flight_input_frame_record input = controls(10), bad;
	static struct input_frame sentinel;
	struct input_frame *out = &sentinel;

	XVT_ASSERT_INT_EQ(
		xvt_flight_history_insert(XVT_FLIGHT_PLAYERS, 8, &input, &out),
		XVT_INPUT_INVALID);
	XVT_ASSERT_TRUE(out == NULL);
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert(1, 0, &input, &out),
			  XVT_INPUT_INVALID);
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert(1, -8, &input, &out),
			  XVT_INPUT_INVALID);
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert(1, 8, NULL, &out),
			  XVT_INPUT_INVALID);
	bad = input;
	bad.flags = 2;
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert(1, 8, &bad, &out),
			  XVT_INPUT_INVALID);
	bad = input;
	bad.flags = 0;
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert(1, 8, &bad, &out),
			  XVT_INPUT_INVALID);
	/* A zero throttle without its flag is no throttle at all, and is accepted. */
	bad.throttle = 0;
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert(1, 8, &bad, &out),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 1);

	/* A corrupt count. */
	g_input_frame_count[2] = -1;
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert(2, 8, &input, &out),
			  XVT_INPUT_INVALID);
	XVT_ASSERT_TRUE(out == NULL);
	g_input_frame_count[2] = XVT_INPUT_HISTORY_CAPACITY + 1;
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert(2, 8, &input, &out),
			  XVT_INPUT_INVALID);
	XVT_ASSERT_INT_EQ(g_input_frame_count[2],
			  XVT_INPUT_HISTORY_CAPACITY + 1);
}

static void check_insert(void)
{
	flight_sim_world();
	struct flight_input_frame_record input = controls(10);
	struct input_frame *out = NULL;

	/* A new frame is real and unapplied, out points at it, and the history stays in tick order. */
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert(1, 10, &input, &out),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_TRUE(out == &g_input_history[1][0]);
	XVT_ASSERT_INT_EQ(out->timestamp, 10);
	XVT_ASSERT_INT_EQ(out->input_source, XVT_INPUT_REAL);
	XVT_ASSERT_INT_EQ(out->awaiting_relay, 0);
	XVT_ASSERT_TRUE(same_input(&out->input, &input));
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert(1, 4, &input, &out),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_TRUE(out == &g_input_history[1][0]);
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert(1, 8, &input, &out),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_TRUE(out == &g_input_history[1][1]);
	const int ticks[] = {4, 8, 10};
	assert_ticks(1, ticks, 3);

	/* A real unapplied frame at the tick is overwritten in place. */
	struct flight_input_frame_record other = controls(-30);
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert(1, 8, &other, &out),
			  XVT_INPUT_INSERTED);
	assert_ticks(1, ticks, 3);
	XVT_ASSERT_TRUE(same_input(&g_input_history[1][1].input, &other));

	/* So is a predicted unapplied frame, which becomes real. */
	g_input_history[1][1].input_source = XVT_INPUT_PREDICTED;
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert(1, 8, &input, &out),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_INT_EQ(g_input_history[1][1].input_source, XVT_INPUT_REAL);
	XVT_ASSERT_TRUE(same_input(&g_input_history[1][1].input, &input));

	/* An authoritative frame, or an applied one, at the tick is a duplicate and stays as it was. */
	g_input_history[1][1].input_source = XVT_INPUT_AUTHORITATIVE;
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert(1, 8, &other, &out),
			  XVT_INPUT_DUPLICATE);
	XVT_ASSERT_TRUE(out == NULL);
	XVT_ASSERT_TRUE(same_input(&g_input_history[1][1].input, &input));
	XVT_ASSERT_INT_EQ(g_input_history[1][1].input_source,
			  XVT_INPUT_AUTHORITATIVE);
	g_input_history[1][2].awaiting_relay = 1;
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert(1, 10, &other, &out),
			  XVT_INPUT_DUPLICATE);
	XVT_ASSERT_TRUE(same_input(&g_input_history[1][2].input, &input));
	XVT_ASSERT_INT_EQ(g_input_history[1][2].awaiting_relay, 1);
	assert_ticks(1, ticks, 3);
}

static void check_insert_full(void)
{
	flight_sim_world();
	struct flight_input_frame_record input = controls(10);
	static struct input_frame sentinel;
	struct input_frame *out = &sentinel;
	for (int i = 0; i < XVT_INPUT_HISTORY_CAPACITY; ++i) {
		add_frame(1, 2 * (i + 1), XVT_INPUT_REAL, 1, 10);
	}
	XVT_ASSERT_INT_EQ(
		xvt_flight_history_insert(1, 2 * XVT_INPUT_HISTORY_CAPACITY + 2,
					  &input, &out),
		XVT_INPUT_FULL);
	XVT_ASSERT_TRUE(out == NULL);
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert(1, 1, &input, &out),
			  XVT_INPUT_FULL);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], XVT_INPUT_HISTORY_CAPACITY);
	XVT_ASSERT_INT_EQ(g_input_history[1][0].timestamp, 2);
}

static void check_insert_real(void)
{
	struct flight_input_frame_record input = controls(10);

	/* New frames: authoritative ones are unapplied; real ones are unapplied on a client... */
	flight_sim_world();
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert_real(1, 4, &input, 1),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_INT_EQ(g_input_history[1][0].input_source,
			  XVT_INPUT_AUTHORITATIVE);
	XVT_ASSERT_INT_EQ(g_input_history[1][0].awaiting_relay, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert_real(1, 6, &input, 0),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_INT_EQ(g_input_history[1][1].input_source, XVT_INPUT_REAL);
	XVT_ASSERT_INT_EQ(g_input_history[1][1].awaiting_relay, 0);
	/* ...and applied on the host. */
	g_net_session.local_is_host = 1;
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert_real(1, 8, &input, 0),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_INT_EQ(g_input_history[1][2].input_source, XVT_INPUT_REAL);
	XVT_ASSERT_TRUE(g_input_history[1][2].awaiting_relay != 0);
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert_real(1, 10, &input, 1),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_INT_EQ(g_input_history[1][3].input_source,
			  XVT_INPUT_AUTHORITATIVE);
	XVT_ASSERT_INT_EQ(g_input_history[1][3].awaiting_relay, 0);

	/* Otherwise Insert's status: an invalid tick, or a real duplicate left as it was. */
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert_real(1, 0, &input, 1),
			  XVT_INPUT_INVALID);
	struct flight_input_frame_record other = controls(-30);
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert_real(1, 8, &other, 0),
			  XVT_INPUT_DUPLICATE);
	XVT_ASSERT_TRUE(same_input(&g_input_history[1][2].input, &input));
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 4);
}

static void check_authoritative_duplicate(void)
{
	struct flight_input_frame_record input = controls(10),
					 other = controls(-30);

	/* An authoritative duplicate that matches turns the frame authoritative and unapplied. */
	flight_sim_world();
	add_frame(1, 8, XVT_INPUT_REAL, 1, 10);
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert_real(1, 8, &input, 1),
			  XVT_INPUT_DUPLICATE);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 1);
	XVT_ASSERT_INT_EQ(g_input_history[1][0].input_source,
			  XVT_INPUT_AUTHORITATIVE);
	XVT_ASSERT_INT_EQ(g_input_history[1][0].awaiting_relay, 0);

	/* One that differs is a conflict, and the frame stays as it was. */
	flight_sim_world();
	add_frame(1, 8, XVT_INPUT_REAL, 1, 10);
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert_real(1, 8, &other, 1),
			  XVT_INPUT_CONFLICT);
	XVT_ASSERT_INT_EQ(g_input_history[1][0].input_source, XVT_INPUT_REAL);
	XVT_ASSERT_INT_EQ(g_input_history[1][0].awaiting_relay, 1);
	XVT_ASSERT_TRUE(same_input(&g_input_history[1][0].input, &input));
	/* Any field counts: the throttle alone. */
	other = input;
	other.throttle = (uint16_t)(input.throttle + 1);
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert_real(1, 8, &other, 1),
			  XVT_INPUT_CONFLICT);
}

static void check_insert_real_full(void)
{
	struct flight_input_frame_record input = controls(10);

	/* A full history first drops its predicted frames and retries. */
	flight_sim_world();
	for (int i = 0; i < XVT_INPUT_HISTORY_CAPACITY; ++i) {
		add_frame(1, 2 * (i + 1),
			  i % 3 ? XVT_INPUT_REAL : XVT_INPUT_PREDICTED, 0, 10);
	}
	int tick = 2 * XVT_INPUT_HISTORY_CAPACITY + 2;
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert_real(1, tick, &input, 0),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1],
			  XVT_INPUT_HISTORY_CAPACITY -
				  XVT_INPUT_HISTORY_CAPACITY / 3 + 1);
	for (int i = 0; i < g_input_frame_count[1]; ++i) {
		XVT_ASSERT_TRUE(g_input_history[1][i].input_source !=
				XVT_INPUT_PREDICTED);
	}
	XVT_ASSERT_INT_EQ(
		g_input_history[1][g_input_frame_count[1] - 1].timestamp, tick);

	/* With nothing predicted to drop, it is still full. */
	flight_sim_world();
	for (int i = 0; i < XVT_INPUT_HISTORY_CAPACITY; ++i) {
		add_frame(1, 2 * (i + 1), XVT_INPUT_REAL, 0, 10);
	}
	XVT_ASSERT_INT_EQ(xvt_flight_history_insert_real(1, tick, &input, 1),
			  XVT_INPUT_FULL);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], XVT_INPUT_HISTORY_CAPACITY);
}

static void check_restore_checkpoint(void)
{
	flight_sim_world();
	g_players[1].lockstep_timestamp = 10;
	add_frame(1, 6, XVT_INPUT_REAL, 1, 1);
	add_frame(1, 10, XVT_INPUT_AUTHORITATIVE, 0, 2);
	add_frame(1, 12, XVT_INPUT_PREDICTED, 0, 3);
	add_frame(1, 14, XVT_INPUT_REAL, 0, 4);
	add_frame(1, 16, XVT_INPUT_AUTHORITATIVE, 0, 5);
	g_players[0].lockstep_timestamp = 0;
	add_frame(0, 2, XVT_INPUT_REAL, 0, 6);
	add_frame(0, 4, XVT_INPUT_PREDICTED, 0, 7);
	/* Player 3 is not connected. */
	add_frame(3, 20, XVT_INPUT_REAL, 0, 8);

	xvt_flight_history_restore_checkpoint();
	const int kept1[] = {14, 16};
	assert_ticks(1, kept1, 2);
	XVT_ASSERT_INT_EQ(g_input_history[1][0].input.axis_x, 4);
	XVT_ASSERT_INT_EQ(g_input_history[1][1].input_source,
			  XVT_INPUT_AUTHORITATIVE);
	const int kept0[] = {2};
	assert_ticks(0, kept0, 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[3], 0);
}

static void check_recover(void)
{
	flight_sim_world();
	g_game_time = 20;
	add_frame(0, 18, XVT_INPUT_REAL, 1, 1);
	add_frame(0, 20, XVT_INPUT_REAL, 0, 2);
	add_frame(0, 22, XVT_INPUT_PREDICTED, 0, 3);
	add_frame(0, 24, XVT_INPUT_AUTHORITATIVE, 0, 4);
	add_frame(0, 26, XVT_INPUT_REAL, 0, 5);
	add_frame(1, 30, XVT_INPUT_REAL, 0, 6);
	add_frame(2, 30, XVT_INPUT_AUTHORITATIVE, 0, 7);
	struct flight_input_frame_record input = controls(40);
	xvt_flight_prediction_confirm(1, 2, &input);

	xvt_flight_history_recover();
	const int kept[] = {24, 26};
	assert_ticks(0, kept, 2);
	XVT_ASSERT_INT_EQ(g_input_history[0][0].input.axis_x, 4);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 0);
	XVT_ASSERT_INT_EQ(g_input_frame_count[2], 0);
	/* The prediction fallback is reset: player 1 gets no prediction from the confirmed controls. */
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(40), 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 0);

	/* A local player that is not connected keeps nothing. */
	flight_sim_world();
	g_players[0].participation_state = 0;
	add_frame(0, 26, XVT_INPUT_REAL, 0, 5);
	xvt_flight_history_recover();
	XVT_ASSERT_INT_EQ(g_input_frame_count[0], 0);
}

int main(void)
{
	check_reset_and_pause();
	check_insert_refusals();
	check_insert();
	check_insert_full();
	check_insert_real();
	check_authoritative_duplicate();
	check_insert_real_full();
	check_restore_checkpoint();
	check_recover();
	g_net_session.local_is_host = 0;
	return 0;
}
