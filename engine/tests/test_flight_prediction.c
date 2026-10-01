/* Checks input prediction for remote players (xvt_runtime/runtime/flight_prediction.h) against the promises
 * in its header: which players get a predicted frame, where its controls come from, what the copy keeps,
 * what it leaves alone, and the refusals. No game data is read: the test sets the per-player input histories
 * and the player records itself. Player 0 is the local player; players 1 to 3 are connected remote players,
 * each bound to an object of its own; the others are not connected. Every check starts from empty
 * histories, no confirmed controls and no recovery request. */
#include "test_assert.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/player/player.h"
#include "xvt/net/flight_sync.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_prediction.h"
#include "xvt_runtime/runtime/flight_protocol.h"

#include <string.h>

enum { TICK = 40 };

static void World(void) {
	memset(g_players, 0, sizeof g_players);
	memset(g_inputHistory, 0, sizeof g_inputHistory);
	memset(g_inputFrameCount, 0, sizeof g_inputFrameCount);
	for (int i = 0; i < 8; ++i)
		g_players[i].objectIndex = -1;
	for (int i = 0; i < 4; ++i) {
		g_players[i].connectedFlag = 1;
		g_players[i].objectIndex = i;
		g_players[i].boundObjectSignature = 0x100 + (unsigned)i;
	}
	g_localPlayer = 0;
	XvtFlightPrediction_Reset();
	XvtFlightNetwork_BeginRecovery();
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 0);
}

static FlightInputFrameRecord Controls(int8_t axis) {
	FlightInputFrameRecord input;
	memset(&input, 0, sizeof input);
	input.key = 0x61;
	input.axisX = axis;
	input.axisY = (int8_t)(axis + 1);
	input.axisR = (int8_t)(axis - 1);
	input.keyMods = 0x0F;
	input.flags = XVT_INPUT_THROTTLE_PRESENT;
	input.throttle = 777;
	return input;
}

/* Appends a frame to player's history, which the caller keeps in tick order. */
static void AddFrame(unsigned player, int tick, int valid, int applied, int8_t axis) {
	InputFrame* frame = &g_inputHistory[player][g_inputFrameCount[player]++];
	frame->timestamp = tick;
	frame->valid = valid;
	frame->applied = applied;
	frame->input = Controls(axis);
}

/* The frame at tick in player's history, or NULL. */
static const InputFrame* FrameAt(unsigned player, int tick) {
	for (int i = 0; i < g_inputFrameCount[player]; ++i)
		if (g_inputHistory[player][i].timestamp == tick)
			return &g_inputHistory[player][i];
	return NULL;
}

/* Checks that player has a predicted, unapplied frame at tick holding source's axes and roll bit only. */
static void AssertPredicted(unsigned player, int tick, int8_t axis) {
	const InputFrame* frame = FrameAt(player, tick);
	XVT_ASSERT_TRUE(frame != NULL);
	XVT_ASSERT_INT_EQ(frame->valid, XVT_INPUT_PREDICTED);
	XVT_ASSERT_INT_EQ(frame->applied, 0);
	FlightInputFrameRecord source = Controls(axis);
	XVT_ASSERT_INT_EQ(frame->input.axisX, source.axisX);
	XVT_ASSERT_INT_EQ(frame->input.axisY, source.axisY);
	XVT_ASSERT_INT_EQ(frame->input.axisR, source.axisR);
	XVT_ASSERT_INT_EQ(frame->input.keyMods, source.keyMods & 0x02);
	XVT_ASSERT_INT_EQ(frame->input.key, 0);
	XVT_ASSERT_INT_EQ(frame->input.flags, 0);
	XVT_ASSERT_INT_EQ(frame->input.throttle, 0);
}

static void CheckInvalidTick(void) {
	World();
	AddFrame(1, 2, XVT_INPUT_REAL, 1, 10);
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(0), 0);
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(TICK + 1), 0);
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(-TICK), 0);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 0);
}

static void CheckWhichPlayers(void) {
	World();
	/* Every player has a history frame before the tick. */
	for (unsigned player = 0; player < 8; ++player)
		AddFrame(player, 2, XVT_INPUT_REAL, 1, (int8_t)(10 * player));
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(TICK), 1);
	/* The connected remote players are predicted; the local player and the unconnected ones are not. */
	for (unsigned player = 1; player < 4; ++player) {
		XVT_ASSERT_INT_EQ(g_inputFrameCount[player], 2);
		AssertPredicted(player, TICK, (int8_t)(10 * player));
	}
	XVT_ASSERT_INT_EQ(g_inputFrameCount[0], 1);
	for (unsigned player = 4; player < 8; ++player)
		XVT_ASSERT_INT_EQ(g_inputFrameCount[player], 1);

	/* The local player is whichever g_localPlayer names. */
	World();
	g_localPlayer = 2;
	AddFrame(0, 2, XVT_INPUT_REAL, 1, 5);
	AddFrame(2, 2, XVT_INPUT_REAL, 1, 5);
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(TICK), 1);
	AssertPredicted(0, TICK, 5);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[2], 1);
}

static void CheckSourceIsNewestBeforeTick(void) {
	/* Real at 2, predicted at 4, authoritative at 6, real after the tick: the authoritative frame is the
	 * newest non-predicted frame before the tick. */
	World();
	AddFrame(1, 2, XVT_INPUT_REAL, 1, 10);
	AddFrame(1, 4, XVT_INPUT_PREDICTED, 0, 50);
	AddFrame(1, 6, XVT_INPUT_AUTHORITATIVE, 0, 20);
	AddFrame(1, TICK + 2, XVT_INPUT_REAL, 0, 60);
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(TICK), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 5);
	AssertPredicted(1, TICK, 20);
	/* The history stays in tick order. */
	for (int i = 1; i < g_inputFrameCount[1]; ++i)
		XVT_ASSERT_TRUE(g_inputHistory[1][i - 1].timestamp < g_inputHistory[1][i].timestamp);
	/* The frames already there are untouched. */
	XVT_ASSERT_INT_EQ(FrameAt(1, 4)->valid, XVT_INPUT_PREDICTED);
	XVT_ASSERT_INT_EQ(FrameAt(1, 4)->input.axisX, 50);

	/* Only predicted frames before the tick: no source, nothing inserted. */
	World();
	AddFrame(1, 2, XVT_INPUT_PREDICTED, 0, 50);
	AddFrame(1, TICK + 2, XVT_INPUT_REAL, 0, 60);
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(TICK), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 2);
	XVT_ASSERT_TRUE(FrameAt(1, TICK) == NULL);

	/* No history at all: nothing inserted, and still 1. */
	World();
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(TICK), 1);
	for (unsigned player = 0; player < 8; ++player)
		XVT_ASSERT_INT_EQ(g_inputFrameCount[player], 0);
}

static void CheckLeavesRealAndAuthoritative(void) {
	World();
	AddFrame(1, 2, XVT_INPUT_REAL, 1, 10);
	AddFrame(1, TICK, XVT_INPUT_REAL, 0, 30);
	AddFrame(2, 2, XVT_INPUT_REAL, 1, 10);
	AddFrame(2, TICK, XVT_INPUT_AUTHORITATIVE, 0, 40);
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(TICK), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 2);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[2], 2);
	const InputFrame* real = FrameAt(1, TICK);
	XVT_ASSERT_INT_EQ(real->valid, XVT_INPUT_REAL);
	XVT_ASSERT_INT_EQ(real->input.axisX, 30);
	XVT_ASSERT_INT_EQ(real->input.key, 0x61);
	XVT_ASSERT_INT_EQ(real->input.throttle, 777);
	const InputFrame* authoritative = FrameAt(2, TICK);
	XVT_ASSERT_INT_EQ(authoritative->valid, XVT_INPUT_AUTHORITATIVE);
	XVT_ASSERT_INT_EQ(authoritative->input.axisX, 40);
	XVT_ASSERT_INT_EQ(authoritative->input.keyMods, 0x0F);
}

static void CheckReplacesPrediction(void) {
	World();
	AddFrame(1, 2, XVT_INPUT_REAL, 1, 10);
	AddFrame(1, TICK, XVT_INPUT_PREDICTED, 0, 99);
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(TICK), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 2);
	AssertPredicted(1, TICK, 10);
}

static void CheckConfirmedControls(void) {
	FlightInputFrameRecord confirmed = Controls(30);

	/* With no history, confirmed controls are the source. */
	World();
	XvtFlightPrediction_Confirm(1, 2, &confirmed);
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(TICK), 1);
	AssertPredicted(1, TICK, 30);

	/* Newer than the newest history frame: the confirmed controls win... */
	World();
	AddFrame(1, 2, XVT_INPUT_REAL, 1, 10);
	XvtFlightPrediction_Confirm(1, 6, &confirmed);
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(TICK), 1);
	AssertPredicted(1, TICK, 30);
	/* ...older, the history frame does. */
	World();
	AddFrame(1, 6, XVT_INPUT_REAL, 1, 10);
	XvtFlightPrediction_Confirm(1, 2, &confirmed);
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(TICK), 1);
	AssertPredicted(1, TICK, 10);

	/* Used only while the player stays bound to the same object: another slot, or another signature. */
	World();
	XvtFlightPrediction_Confirm(1, 2, &confirmed);
	g_players[1].objectIndex = 7;
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(TICK), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 0);
	World();
	XvtFlightPrediction_Confirm(1, 2, &confirmed);
	g_players[1].boundObjectSignature ^= 0x8000;
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(TICK), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 0);

	/* Ignored for an out-of-range player or NULL input: the earlier controls stay. */
	World();
	FlightInputFrameRecord other = Controls(70);
	XvtFlightPrediction_Confirm(1, 2, &confirmed);
	XvtFlightPrediction_Confirm(1, 4, NULL);
	XvtFlightPrediction_Confirm(XVT_FLIGHT_PLAYERS, 4, &other);
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(TICK), 1);
	AssertPredicted(1, TICK, 30);

	/* Reset forgets every player's confirmed controls. */
	World();
	XvtFlightPrediction_Confirm(1, 2, &confirmed);
	XvtFlightPrediction_Confirm(2, 2, &confirmed);
	XvtFlightPrediction_Reset();
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(TICK), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 0);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[2], 0);
}

static void CheckCorruptHistory(void) {
	/* Player 2's count is corrupt: recovery is requested and 0 returned; player 1's prediction stays. */
	World();
	AddFrame(1, 2, XVT_INPUT_REAL, 1, 10);
	AddFrame(2, 2, XVT_INPUT_REAL, 1, 10);
	g_inputFrameCount[2] = -1;
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(TICK), 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 1);
	AssertPredicted(1, TICK, 10);

	World();
	AddFrame(1, 2, XVT_INPUT_REAL, 1, 10);
	g_inputFrameCount[1] = XVT_INPUT_HISTORY_CAPACITY + 1;
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(TICK), 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 1);
}

static void CheckFullHistory(void) {
	/* A full history: recovery is requested and 0 returned. */
	World();
	for (int i = 0; i < XVT_INPUT_HISTORY_CAPACITY; ++i)
		AddFrame(1, 2 * (i + 1), XVT_INPUT_REAL, 1, 10);
	int tick = 2 * (XVT_INPUT_HISTORY_CAPACITY + 1);
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(tick), 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], XVT_INPUT_HISTORY_CAPACITY);
	XVT_ASSERT_TRUE(FrameAt(1, tick) == NULL);

	/* One frame fewer and the same tick is predicted. */
	World();
	for (int i = 0; i < XVT_INPUT_HISTORY_CAPACITY - 1; ++i)
		AddFrame(1, 2 * (i + 1), XVT_INPUT_REAL, 1, 10);
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(tick), 1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 0);
	AssertPredicted(1, tick, 10);
}

int main(void) {
	CheckInvalidTick();
	CheckWhichPlayers();
	CheckSourceIsNewestBeforeTick();
	CheckLeavesRealAndAuthoritative();
	CheckReplacesPrediction();
	CheckConfirmedControls();
	CheckCorruptHistory();
	CheckFullHistory();
	XvtFlightNetwork_BeginRecovery();
	return 0;
}
