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
#include "test_assert.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/player/player.h"
#include "xvt/net/flight_sync.h"
#include "xvt/net/net_session.h"
#include "xvt_runtime/runtime/flight_prediction.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/flight_sim.h"

#include <string.h>

static void World(void)
{
	memset(g_players, 0, sizeof g_players);
	memset(g_inputHistory, 0, sizeof g_inputHistory);
	memset(g_inputFrameCount, 0, sizeof g_inputFrameCount);
	for (int i = 0; i < 8; ++i) {
		g_players[i].objectIndex = -1;
	}
	for (int i = 0; i < 3; ++i) {
		g_players[i].participationState = 1;
		g_players[i].objectIndex = i;
	}
	g_localPlayer = 0;
	g_netSession.localIsHost = 0;
	g_gameTime = 0;
	XvtFlightSim_Reset();
}

static struct FlightInputFrameRecord Controls(int8_t axis)
{
	struct FlightInputFrameRecord input;
	memset(&input, 0, sizeof input);
	input.key = 0x61;
	input.axisX = axis;
	input.axisY = (int8_t)-axis;
	input.axisR = 2;
	input.keyMods = 1;
	input.flags = XVT_INPUT_THROTTLE_PRESENT;
	input.throttle = 300;
	return input;
}

/* Appends a frame to player's history, which the caller keeps in tick order. */
static void AddFrame(unsigned player, int tick, int valid, int applied,
		     int8_t axis)
{
	struct InputFrame *frame =
		&g_inputHistory[player][g_inputFrameCount[player]++];
	frame->timestamp = tick;
	frame->inputSource = valid;
	frame->awaitingRelay = applied;
	frame->input = Controls(axis);
}

/* Checks that player's history holds exactly the frames at ticks, in that order. */
static void AssertTicks(unsigned player, const int *ticks, int count)
{
	XVT_ASSERT_INT_EQ(g_inputFrameCount[player], count);
	for (int i = 0; i < count; ++i) {
		XVT_ASSERT_INT_EQ(g_inputHistory[player][i].timestamp,
				  ticks[i]);
	}
}

static int SameInput(const struct FlightInputFrameRecord *a,
		     const struct FlightInputFrameRecord *b)
{
	return a->key == b->key && a->axisX == b->axisX &&
	       a->axisY == b->axisY && a->axisR == b->axisR &&
	       a->keyMods == b->keyMods && a->flags == b->flags &&
	       a->throttle == b->throttle;
}

static void CheckResetAndPause(void)
{
	World();
	XVT_ASSERT_INT_EQ(XvtFlightSim_IsPaused(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightSim_Resume(), 1);

	/* Reset clears the prediction fallback (flight_prediction.h): confirmed controls are forgotten. */
	struct FlightInputFrameRecord input = Controls(20);
	XvtFlightPrediction_Confirm(1, 2, &input);
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(8), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 1);
	World();
	XvtFlightPrediction_Confirm(1, 2, &input);
	XvtFlightSim_Reset();
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(8), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 0);
}

static void CheckInsertRefusals(void)
{
	World();
	struct FlightInputFrameRecord input = Controls(10), bad;
	static struct InputFrame sentinel;
	struct InputFrame *out = &sentinel;

	XVT_ASSERT_INT_EQ(
		XvtFlightHistory_Insert(XVT_FLIGHT_PLAYERS, 8, &input, &out),
		XVT_INPUT_INVALID);
	XVT_ASSERT_TRUE(out == NULL);
	XVT_ASSERT_INT_EQ(XvtFlightHistory_Insert(1, 0, &input, &out),
			  XVT_INPUT_INVALID);
	XVT_ASSERT_INT_EQ(XvtFlightHistory_Insert(1, -8, &input, &out),
			  XVT_INPUT_INVALID);
	XVT_ASSERT_INT_EQ(XvtFlightHistory_Insert(1, 8, NULL, &out),
			  XVT_INPUT_INVALID);
	bad = input;
	bad.flags = 2;
	XVT_ASSERT_INT_EQ(XvtFlightHistory_Insert(1, 8, &bad, &out),
			  XVT_INPUT_INVALID);
	bad = input;
	bad.flags = 0;
	XVT_ASSERT_INT_EQ(XvtFlightHistory_Insert(1, 8, &bad, &out),
			  XVT_INPUT_INVALID);
	/* A zero throttle without its flag is no throttle at all, and is accepted. */
	bad.throttle = 0;
	XVT_ASSERT_INT_EQ(XvtFlightHistory_Insert(1, 8, &bad, &out),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 1);

	/* A corrupt count. */
	g_inputFrameCount[2] = -1;
	XVT_ASSERT_INT_EQ(XvtFlightHistory_Insert(2, 8, &input, &out),
			  XVT_INPUT_INVALID);
	XVT_ASSERT_TRUE(out == NULL);
	g_inputFrameCount[2] = XVT_INPUT_HISTORY_CAPACITY + 1;
	XVT_ASSERT_INT_EQ(XvtFlightHistory_Insert(2, 8, &input, &out),
			  XVT_INPUT_INVALID);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[2], XVT_INPUT_HISTORY_CAPACITY + 1);
}

static void CheckInsert(void)
{
	World();
	struct FlightInputFrameRecord input = Controls(10);
	struct InputFrame *out = NULL;

	/* A new frame is real and unapplied, out points at it, and the history stays in tick order. */
	XVT_ASSERT_INT_EQ(XvtFlightHistory_Insert(1, 10, &input, &out),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_TRUE(out == &g_inputHistory[1][0]);
	XVT_ASSERT_INT_EQ(out->timestamp, 10);
	XVT_ASSERT_INT_EQ(out->inputSource, XVT_INPUT_REAL);
	XVT_ASSERT_INT_EQ(out->awaitingRelay, 0);
	XVT_ASSERT_TRUE(SameInput(&out->input, &input));
	XVT_ASSERT_INT_EQ(XvtFlightHistory_Insert(1, 4, &input, &out),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_TRUE(out == &g_inputHistory[1][0]);
	XVT_ASSERT_INT_EQ(XvtFlightHistory_Insert(1, 8, &input, &out),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_TRUE(out == &g_inputHistory[1][1]);
	const int ticks[] = {4, 8, 10};
	AssertTicks(1, ticks, 3);

	/* A real unapplied frame at the tick is overwritten in place. */
	struct FlightInputFrameRecord other = Controls(-30);
	XVT_ASSERT_INT_EQ(XvtFlightHistory_Insert(1, 8, &other, &out),
			  XVT_INPUT_INSERTED);
	AssertTicks(1, ticks, 3);
	XVT_ASSERT_TRUE(SameInput(&g_inputHistory[1][1].input, &other));

	/* So is a predicted unapplied frame, which becomes real. */
	g_inputHistory[1][1].inputSource = XVT_INPUT_PREDICTED;
	XVT_ASSERT_INT_EQ(XvtFlightHistory_Insert(1, 8, &input, &out),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_INT_EQ(g_inputHistory[1][1].inputSource, XVT_INPUT_REAL);
	XVT_ASSERT_TRUE(SameInput(&g_inputHistory[1][1].input, &input));

	/* An authoritative frame, or an applied one, at the tick is a duplicate and stays as it was. */
	g_inputHistory[1][1].inputSource = XVT_INPUT_AUTHORITATIVE;
	XVT_ASSERT_INT_EQ(XvtFlightHistory_Insert(1, 8, &other, &out),
			  XVT_INPUT_DUPLICATE);
	XVT_ASSERT_TRUE(out == NULL);
	XVT_ASSERT_TRUE(SameInput(&g_inputHistory[1][1].input, &input));
	XVT_ASSERT_INT_EQ(g_inputHistory[1][1].inputSource,
			  XVT_INPUT_AUTHORITATIVE);
	g_inputHistory[1][2].awaitingRelay = 1;
	XVT_ASSERT_INT_EQ(XvtFlightHistory_Insert(1, 10, &other, &out),
			  XVT_INPUT_DUPLICATE);
	XVT_ASSERT_TRUE(SameInput(&g_inputHistory[1][2].input, &input));
	XVT_ASSERT_INT_EQ(g_inputHistory[1][2].awaitingRelay, 1);
	AssertTicks(1, ticks, 3);
}

static void CheckInsertFull(void)
{
	World();
	struct FlightInputFrameRecord input = Controls(10);
	static struct InputFrame sentinel;
	struct InputFrame *out = &sentinel;
	for (int i = 0; i < XVT_INPUT_HISTORY_CAPACITY; ++i) {
		AddFrame(1, 2 * (i + 1), XVT_INPUT_REAL, 1, 10);
	}
	XVT_ASSERT_INT_EQ(
		XvtFlightHistory_Insert(1, 2 * XVT_INPUT_HISTORY_CAPACITY + 2,
					&input, &out),
		XVT_INPUT_FULL);
	XVT_ASSERT_TRUE(out == NULL);
	XVT_ASSERT_INT_EQ(XvtFlightHistory_Insert(1, 1, &input, &out),
			  XVT_INPUT_FULL);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], XVT_INPUT_HISTORY_CAPACITY);
	XVT_ASSERT_INT_EQ(g_inputHistory[1][0].timestamp, 2);
}

static void CheckInsertReal(void)
{
	struct FlightInputFrameRecord input = Controls(10);

	/* New frames: authoritative ones are unapplied; real ones are unapplied on a client... */
	World();
	XVT_ASSERT_INT_EQ(XvtFlightHistory_InsertReal(1, 4, &input, 1),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_INT_EQ(g_inputHistory[1][0].inputSource,
			  XVT_INPUT_AUTHORITATIVE);
	XVT_ASSERT_INT_EQ(g_inputHistory[1][0].awaitingRelay, 0);
	XVT_ASSERT_INT_EQ(XvtFlightHistory_InsertReal(1, 6, &input, 0),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_INT_EQ(g_inputHistory[1][1].inputSource, XVT_INPUT_REAL);
	XVT_ASSERT_INT_EQ(g_inputHistory[1][1].awaitingRelay, 0);
	/* ...and applied on the host. */
	g_netSession.localIsHost = 1;
	XVT_ASSERT_INT_EQ(XvtFlightHistory_InsertReal(1, 8, &input, 0),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_INT_EQ(g_inputHistory[1][2].inputSource, XVT_INPUT_REAL);
	XVT_ASSERT_TRUE(g_inputHistory[1][2].awaitingRelay != 0);
	XVT_ASSERT_INT_EQ(XvtFlightHistory_InsertReal(1, 10, &input, 1),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_INT_EQ(g_inputHistory[1][3].inputSource,
			  XVT_INPUT_AUTHORITATIVE);
	XVT_ASSERT_INT_EQ(g_inputHistory[1][3].awaitingRelay, 0);

	/* Otherwise Insert's status: an invalid tick, or a real duplicate left as it was. */
	XVT_ASSERT_INT_EQ(XvtFlightHistory_InsertReal(1, 0, &input, 1),
			  XVT_INPUT_INVALID);
	struct FlightInputFrameRecord other = Controls(-30);
	XVT_ASSERT_INT_EQ(XvtFlightHistory_InsertReal(1, 8, &other, 0),
			  XVT_INPUT_DUPLICATE);
	XVT_ASSERT_TRUE(SameInput(&g_inputHistory[1][2].input, &input));
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 4);
}

static void CheckAuthoritativeDuplicate(void)
{
	struct FlightInputFrameRecord input = Controls(10),
				      other = Controls(-30);

	/* An authoritative duplicate that matches turns the frame authoritative and unapplied. */
	World();
	AddFrame(1, 8, XVT_INPUT_REAL, 1, 10);
	XVT_ASSERT_INT_EQ(XvtFlightHistory_InsertReal(1, 8, &input, 1),
			  XVT_INPUT_DUPLICATE);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 1);
	XVT_ASSERT_INT_EQ(g_inputHistory[1][0].inputSource,
			  XVT_INPUT_AUTHORITATIVE);
	XVT_ASSERT_INT_EQ(g_inputHistory[1][0].awaitingRelay, 0);

	/* One that differs is a conflict, and the frame stays as it was. */
	World();
	AddFrame(1, 8, XVT_INPUT_REAL, 1, 10);
	XVT_ASSERT_INT_EQ(XvtFlightHistory_InsertReal(1, 8, &other, 1),
			  XVT_INPUT_CONFLICT);
	XVT_ASSERT_INT_EQ(g_inputHistory[1][0].inputSource, XVT_INPUT_REAL);
	XVT_ASSERT_INT_EQ(g_inputHistory[1][0].awaitingRelay, 1);
	XVT_ASSERT_TRUE(SameInput(&g_inputHistory[1][0].input, &input));
	/* Any field counts: the throttle alone. */
	other = input;
	other.throttle = (uint16_t)(input.throttle + 1);
	XVT_ASSERT_INT_EQ(XvtFlightHistory_InsertReal(1, 8, &other, 1),
			  XVT_INPUT_CONFLICT);
}

static void CheckInsertRealFull(void)
{
	struct FlightInputFrameRecord input = Controls(10);

	/* A full history first drops its predicted frames and retries. */
	World();
	for (int i = 0; i < XVT_INPUT_HISTORY_CAPACITY; ++i) {
		AddFrame(1, 2 * (i + 1),
			 i % 3 ? XVT_INPUT_REAL : XVT_INPUT_PREDICTED, 0, 10);
	}
	int tick = 2 * XVT_INPUT_HISTORY_CAPACITY + 2;
	XVT_ASSERT_INT_EQ(XvtFlightHistory_InsertReal(1, tick, &input, 0),
			  XVT_INPUT_INSERTED);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1],
			  XVT_INPUT_HISTORY_CAPACITY -
				  XVT_INPUT_HISTORY_CAPACITY / 3 + 1);
	for (int i = 0; i < g_inputFrameCount[1]; ++i) {
		XVT_ASSERT_TRUE(g_inputHistory[1][i].inputSource !=
				XVT_INPUT_PREDICTED);
	}
	XVT_ASSERT_INT_EQ(g_inputHistory[1][g_inputFrameCount[1] - 1].timestamp,
			  tick);

	/* With nothing predicted to drop, it is still full. */
	World();
	for (int i = 0; i < XVT_INPUT_HISTORY_CAPACITY; ++i) {
		AddFrame(1, 2 * (i + 1), XVT_INPUT_REAL, 0, 10);
	}
	XVT_ASSERT_INT_EQ(XvtFlightHistory_InsertReal(1, tick, &input, 1),
			  XVT_INPUT_FULL);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], XVT_INPUT_HISTORY_CAPACITY);
}

static void CheckRestoreCheckpoint(void)
{
	World();
	g_players[1].lockstepTimestamp = 10;
	AddFrame(1, 6, XVT_INPUT_REAL, 1, 1);
	AddFrame(1, 10, XVT_INPUT_AUTHORITATIVE, 0, 2);
	AddFrame(1, 12, XVT_INPUT_PREDICTED, 0, 3);
	AddFrame(1, 14, XVT_INPUT_REAL, 0, 4);
	AddFrame(1, 16, XVT_INPUT_AUTHORITATIVE, 0, 5);
	g_players[0].lockstepTimestamp = 0;
	AddFrame(0, 2, XVT_INPUT_REAL, 0, 6);
	AddFrame(0, 4, XVT_INPUT_PREDICTED, 0, 7);
	/* Player 3 is not connected. */
	AddFrame(3, 20, XVT_INPUT_REAL, 0, 8);

	XvtFlightHistory_RestoreCheckpoint();
	const int kept1[] = {14, 16};
	AssertTicks(1, kept1, 2);
	XVT_ASSERT_INT_EQ(g_inputHistory[1][0].input.axisX, 4);
	XVT_ASSERT_INT_EQ(g_inputHistory[1][1].inputSource,
			  XVT_INPUT_AUTHORITATIVE);
	const int kept0[] = {2};
	AssertTicks(0, kept0, 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[3], 0);
}

static void CheckRecover(void)
{
	World();
	g_gameTime = 20;
	AddFrame(0, 18, XVT_INPUT_REAL, 1, 1);
	AddFrame(0, 20, XVT_INPUT_REAL, 0, 2);
	AddFrame(0, 22, XVT_INPUT_PREDICTED, 0, 3);
	AddFrame(0, 24, XVT_INPUT_AUTHORITATIVE, 0, 4);
	AddFrame(0, 26, XVT_INPUT_REAL, 0, 5);
	AddFrame(1, 30, XVT_INPUT_REAL, 0, 6);
	AddFrame(2, 30, XVT_INPUT_AUTHORITATIVE, 0, 7);
	struct FlightInputFrameRecord input = Controls(40);
	XvtFlightPrediction_Confirm(1, 2, &input);

	XvtFlightHistory_Recover();
	const int kept[] = {24, 26};
	AssertTicks(0, kept, 2);
	XVT_ASSERT_INT_EQ(g_inputHistory[0][0].input.axisX, 4);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 0);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[2], 0);
	/* The prediction fallback is reset: player 1 gets no prediction from the confirmed controls. */
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(40), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 0);

	/* A local player that is not connected keeps nothing. */
	World();
	g_players[0].participationState = 0;
	AddFrame(0, 26, XVT_INPUT_REAL, 0, 5);
	XvtFlightHistory_Recover();
	XVT_ASSERT_INT_EQ(g_inputFrameCount[0], 0);
}

int main(void)
{
	CheckResetAndPause();
	CheckInsertRefusals();
	CheckInsert();
	CheckInsertFull();
	CheckInsertReal();
	CheckAuthoritativeDuplicate();
	CheckInsertRealFull();
	CheckRestoreCheckpoint();
	CheckRecover();
	g_netSession.localIsHost = 0;
	return 0;
}
