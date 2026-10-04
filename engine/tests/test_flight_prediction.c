/* Checks input prediction for remote players
 * (xvt_runtime/runtime/flight_prediction.h) against the promises in its header:
 * which players get a predicted frame, where its controls come from, what the
 * copy keeps, what it leaves alone, and the refusals. No game data is read: the
 * test sets the per-player input histories and the player records itself.
 * Player 0 is the local player; players 1 to 3 are connected remote players,
 * each bound to an object of its own; the others are not connected. Every check
 * starts from empty histories, no confirmed controls and no recovery
 * request. */
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/player/player.h"
#include "xvt/net/flight_sync.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_prediction.h"
#include "xvt_runtime/runtime/flight_protocol.h"

enum { TICK = 40 };

static void flight_prediction_world(void)
{
	memset(g_players, 0, sizeof g_players);
	memset(g_input_history, 0, sizeof g_input_history);
	memset(g_input_frame_count, 0, sizeof g_input_frame_count);
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
	}
	for (int i = 0; i < 4; ++i) {
		g_players[i].participation_state = 1;
		g_players[i].object_index = i;
		g_players[i].bound_object_signature = 0x100 + (unsigned)i;
	}
	g_local_player = 0;
	xvt_flight_prediction_reset();
	xvt_flight_network_clear_recovery_request();
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 0);
}

static struct flight_input_frame_record controls(int8_t axis)
{
	struct flight_input_frame_record input;
	memset(&input, 0, sizeof input);
	input.key = 0x61;
	input.axis_x = axis;
	input.axis_y = (int8_t)(axis + 1);
	input.axis_r = (int8_t)(axis - 1);
	input.key_mods = 0x0F;
	input.flags = XVT_INPUT_THROTTLE_PRESENT;
	input.throttle = 777;
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

/* The frame at tick in player's history, or NULL. */
static const struct input_frame *frame_at(unsigned player, int tick)
{
	for (int i = 0; i < g_input_frame_count[player]; ++i) {
		if (g_input_history[player][i].timestamp == tick) {
			return &g_input_history[player][i];
		}
	}
	return NULL;
}

/* Checks that player has a predicted, unapplied frame at tick holding source's
 * axes and roll bit only. */
static void assert_predicted(unsigned player, int tick, int8_t axis)
{
	const struct input_frame *frame = frame_at(player, tick);
	XVT_ASSERT_TRUE(frame != NULL);
	XVT_ASSERT_INT_EQ(frame->input_source, XVT_INPUT_PREDICTED);
	XVT_ASSERT_INT_EQ(frame->awaiting_relay, 0);
	struct flight_input_frame_record source = controls(axis);
	XVT_ASSERT_INT_EQ(frame->input.axis_x, source.axis_x);
	XVT_ASSERT_INT_EQ(frame->input.axis_y, source.axis_y);
	XVT_ASSERT_INT_EQ(frame->input.axis_r, source.axis_r);
	XVT_ASSERT_INT_EQ(frame->input.key_mods, source.key_mods & 0x02);
	XVT_ASSERT_INT_EQ(frame->input.key, 0);
	XVT_ASSERT_INT_EQ(frame->input.flags, 0);
	XVT_ASSERT_INT_EQ(frame->input.throttle, 0);
}

static void check_invalid_tick(void)
{
	flight_prediction_world();
	add_frame(1, 2, XVT_INPUT_REAL, 1, 10);
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(0), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(TICK + 1), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(-TICK), 0);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 0);
}

static void check_which_players(void)
{
	flight_prediction_world();
	/* Every player has a history frame before the tick. */
	for (unsigned player = 0; player < 8; ++player) {
		add_frame(player, 2, XVT_INPUT_REAL, 1, (int8_t)(10 * player));
	}
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(TICK), 1);
	/* The connected remote players are predicted; the local player and the
	 * unconnected ones are not. */
	for (unsigned player = 1; player < 4; ++player) {
		XVT_ASSERT_INT_EQ(g_input_frame_count[player], 2);
		assert_predicted(player, TICK, (int8_t)(10 * player));
	}
	XVT_ASSERT_INT_EQ(g_input_frame_count[0], 1);
	for (unsigned player = 4; player < 8; ++player) {
		XVT_ASSERT_INT_EQ(g_input_frame_count[player], 1);
	}

	/* The local player is whichever g_local_player names. */
	flight_prediction_world();
	g_local_player = 2;
	add_frame(0, 2, XVT_INPUT_REAL, 1, 5);
	add_frame(2, 2, XVT_INPUT_REAL, 1, 5);
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(TICK), 1);
	assert_predicted(0, TICK, 5);
	XVT_ASSERT_INT_EQ(g_input_frame_count[2], 1);
}

static void check_source_is_newest_before_tick(void)
{
	/* Real at 2, predicted at 4, authoritative at 6, real after the tick:
	 * the authoritative frame is the newest non-predicted frame before the
	 * tick. */
	flight_prediction_world();
	add_frame(1, 2, XVT_INPUT_REAL, 1, 10);
	add_frame(1, 4, XVT_INPUT_PREDICTED, 0, 50);
	add_frame(1, 6, XVT_INPUT_AUTHORITATIVE, 0, 20);
	add_frame(1, TICK + 2, XVT_INPUT_REAL, 0, 60);
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(TICK), 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 5);
	assert_predicted(1, TICK, 20);
	/* The history stays in tick order. */
	for (int i = 1; i < g_input_frame_count[1]; ++i) {
		XVT_ASSERT_TRUE(g_input_history[1][i - 1].timestamp <
				g_input_history[1][i].timestamp);
	}
	/* The frames already there are untouched. */
	XVT_ASSERT_INT_EQ(frame_at(1, 4)->input_source, XVT_INPUT_PREDICTED);
	XVT_ASSERT_INT_EQ(frame_at(1, 4)->input.axis_x, 50);

	/* Only predicted frames before the tick: no source, nothing inserted. */
	flight_prediction_world();
	add_frame(1, 2, XVT_INPUT_PREDICTED, 0, 50);
	add_frame(1, TICK + 2, XVT_INPUT_REAL, 0, 60);
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(TICK), 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 2);
	XVT_ASSERT_TRUE(frame_at(1, TICK) == NULL);

	/* No history at all: nothing inserted, and still 1. */
	flight_prediction_world();
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(TICK), 1);
	for (unsigned player = 0; player < 8; ++player) {
		XVT_ASSERT_INT_EQ(g_input_frame_count[player], 0);
	}
}

static void check_leaves_real_and_authoritative(void)
{
	flight_prediction_world();
	add_frame(1, 2, XVT_INPUT_REAL, 1, 10);
	add_frame(1, TICK, XVT_INPUT_REAL, 0, 30);
	add_frame(2, 2, XVT_INPUT_REAL, 1, 10);
	add_frame(2, TICK, XVT_INPUT_AUTHORITATIVE, 0, 40);
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(TICK), 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 2);
	XVT_ASSERT_INT_EQ(g_input_frame_count[2], 2);
	const struct input_frame *real = frame_at(1, TICK);
	XVT_ASSERT_INT_EQ(real->input_source, XVT_INPUT_REAL);
	XVT_ASSERT_INT_EQ(real->input.axis_x, 30);
	XVT_ASSERT_INT_EQ(real->input.key, 0x61);
	XVT_ASSERT_INT_EQ(real->input.throttle, 777);
	const struct input_frame *authoritative = frame_at(2, TICK);
	XVT_ASSERT_INT_EQ(authoritative->input_source, XVT_INPUT_AUTHORITATIVE);
	XVT_ASSERT_INT_EQ(authoritative->input.axis_x, 40);
	XVT_ASSERT_INT_EQ(authoritative->input.key_mods, 0x0F);
}

static void check_replaces_prediction(void)
{
	flight_prediction_world();
	add_frame(1, 2, XVT_INPUT_REAL, 1, 10);
	add_frame(1, TICK, XVT_INPUT_PREDICTED, 0, 99);
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(TICK), 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 2);
	assert_predicted(1, TICK, 10);
}

static void check_confirmed_controls(void)
{
	struct flight_input_frame_record confirmed = controls(30);

	/* With no history, confirmed controls are the source. */
	flight_prediction_world();
	xvt_flight_prediction_confirm(1, 2, &confirmed);
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(TICK), 1);
	assert_predicted(1, TICK, 30);

	/* Newer than the newest history frame: the confirmed controls win... */
	flight_prediction_world();
	add_frame(1, 2, XVT_INPUT_REAL, 1, 10);
	xvt_flight_prediction_confirm(1, 6, &confirmed);
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(TICK), 1);
	assert_predicted(1, TICK, 30);
	/* ...older, the history frame does. */
	flight_prediction_world();
	add_frame(1, 6, XVT_INPUT_REAL, 1, 10);
	xvt_flight_prediction_confirm(1, 2, &confirmed);
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(TICK), 1);
	assert_predicted(1, TICK, 10);

	/* Used only while the player stays bound to the same object: another
	 * slot, or another signature. */
	flight_prediction_world();
	xvt_flight_prediction_confirm(1, 2, &confirmed);
	g_players[1].object_index = 7;
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(TICK), 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 0);
	flight_prediction_world();
	xvt_flight_prediction_confirm(1, 2, &confirmed);
	g_players[1].bound_object_signature ^= 0x8000;
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(TICK), 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 0);

	/* Ignored for an out-of-range player or NULL input: the earlier controls stay. */
	flight_prediction_world();
	struct flight_input_frame_record other = controls(70);
	xvt_flight_prediction_confirm(1, 2, &confirmed);
	xvt_flight_prediction_confirm(1, 4, NULL);
	xvt_flight_prediction_confirm(XVT_FLIGHT_PLAYERS, 4, &other);
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(TICK), 1);
	assert_predicted(1, TICK, 30);

	/* Reset forgets every player's confirmed controls. */
	flight_prediction_world();
	xvt_flight_prediction_confirm(1, 2, &confirmed);
	xvt_flight_prediction_confirm(2, 2, &confirmed);
	xvt_flight_prediction_reset();
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(TICK), 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 0);
	XVT_ASSERT_INT_EQ(g_input_frame_count[2], 0);
}

static void check_corrupt_history(void)
{
	/* Player 2's count is corrupt: recovery is requested and 0 returned;
	 * player 1's prediction stays. */
	flight_prediction_world();
	add_frame(1, 2, XVT_INPUT_REAL, 1, 10);
	add_frame(2, 2, XVT_INPUT_REAL, 1, 10);
	g_input_frame_count[2] = -1;
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(TICK), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 1);
	assert_predicted(1, TICK, 10);

	flight_prediction_world();
	add_frame(1, 2, XVT_INPUT_REAL, 1, 10);
	g_input_frame_count[1] = XVT_INPUT_HISTORY_CAPACITY + 1;
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(TICK), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 1);
}

static void check_full_history(void)
{
	/* A full history: recovery is requested and 0 returned. */
	flight_prediction_world();
	for (int i = 0; i < XVT_INPUT_HISTORY_CAPACITY; ++i) {
		add_frame(1, 2 * (i + 1), XVT_INPUT_REAL, 1, 10);
	}
	int tick = 2 * (XVT_INPUT_HISTORY_CAPACITY + 1);
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(tick), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], XVT_INPUT_HISTORY_CAPACITY);
	XVT_ASSERT_TRUE(frame_at(1, tick) == NULL);

	/* One frame fewer and the same tick is predicted. */
	flight_prediction_world();
	for (int i = 0; i < XVT_INPUT_HISTORY_CAPACITY - 1; ++i) {
		add_frame(1, 2 * (i + 1), XVT_INPUT_REAL, 1, 10);
	}
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(tick), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_network_needs_recovery(), 0);
	assert_predicted(1, tick, 10);
}

int main(void)
{
	check_invalid_tick();
	check_which_players();
	check_source_is_newest_before_tick();
	check_leaves_real_and_authoritative();
	check_replaces_prediction();
	check_confirmed_controls();
	check_corrupt_history();
	check_full_history();
	xvt_flight_network_clear_recovery_request();
	return 0;
}
