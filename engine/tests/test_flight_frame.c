/* Checks the flight frame loop (xvt_runtime/runtime/flight_frame.h) against the promises in its header, for
 * the parts that hold without a running flight: DelayForTicks on the host and frame-delta clocks, what Begin
 * and ResetReplay clear, ReplayBuffered with an empty queue, an old message and a gap, NextWakeDelayUs in
 * the native and network125 profiles, and a native Update before a step's worth of input time has passed. No
 * game data is read: the test drives the host clock and sets the clocks and globals it reads itself. Every
 * check starts with the host clock on a whole millisecond, the frame-delta clock reset, an empty world
 * message queue and no recovery request.
 *
 * Not checked here: confirming a message (it restores the saved world and steps the recovered game's
 * simulation), and Update past that first wait (it runs the simulation, renders a frame, and on the network
 * reads and sends packets); they need a running flight of the recovered game. */
#include "test_assert.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/player/player.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/flight_sync.h"
#include "xvt/util/time.h"
#include "xvt_runtime/runtime/flight_frame.h"
#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_prediction.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/host_clock.h"

#include <stdint.h>
#include <string.h>

enum { MS_US = 1000, TICK_US = 4 * MS_US };

static XvtFlightMessage g_message;

static void Clocks(XvtFlightTimingProfile profile)
{
	XvtTime_Reset();
	XvtTime_AdvanceHostClock(5000 * MS_US);
	Time_ResetElapsedTicks();
	XvtFlightTiming_BeginSession(profile);
	XvtFlightNetwork_ResetMission();
	XvtFlightNetwork_ClearRecoveryRequest();
	memset(g_players, 0, sizeof g_players);
	memset(g_inputHistory, 0, sizeof g_inputHistory);
	memset(g_inputFrameCount, 0, sizeof g_inputFrameCount);
	for (int i = 0; i < 8; ++i) {
		g_players[i].objectIndex = -1;
	}
	g_localPlayer = 0;
	g_gameTime = 0;
	g_serverTickTime = 0;
	g_inputTimestamp = 0;
	XvtFlightFrame_Begin();
}

static void QueueReplay(unsigned target)
{
	memset(&g_message, 0, sizeof g_message);
	g_message.target_flags = target;
	g_message.participant_mask = 0x01;
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_Enqueue(&g_message, XVT_QUEUE_REPLAY), 1);
}

static void CheckDelayForTicks(void)
{
	/* With the frame-delta clock reset its last tick is now: n ticks are n times 4 ms away. */
	Clocks(XVT_FLIGHT_TIMING_NATIVE);
	XVT_ASSERT_TRUE(XvtFlightTime_DelayForTicks(0) == 0);
	XVT_ASSERT_TRUE(XvtFlightTime_DelayForTicks(1) == TICK_US);
	XVT_ASSERT_TRUE(XvtFlightTime_DelayForTicks(3) == 3 * TICK_US);
	XVT_ASSERT_TRUE(XvtFlightTime_DelayForTicks(1000) == 1000ull * TICK_US);

	/* After a tick of the frame-delta clock, time passes on the host clock and the delay shrinks by it. */
	XVT_ASSERT_INT_EQ(Time_ConsumeElapsedTicks(), 0);
	XvtTime_AdvanceHostClock(10 * MS_US);
	XVT_ASSERT_TRUE(XvtFlightTime_DelayForTicks(3) ==
			3 * TICK_US - 10 * MS_US);
	XVT_ASSERT_TRUE(XvtFlightTime_DelayForTicks(5) ==
			5 * TICK_US - 10 * MS_US);
	XvtTime_AdvanceHostClock(MS_US / 2);
	XVT_ASSERT_TRUE(XvtFlightTime_DelayForTicks(3) ==
			3 * TICK_US - 10 * MS_US - MS_US / 2);
	/* One tick more is always 4 ms more, while the ticks have not passed. */
	XVT_ASSERT_TRUE(XvtFlightTime_DelayForTicks(4) -
				XvtFlightTime_DelayForTicks(3) ==
			TICK_US);
	/* 0 once they have. */
	XVT_ASSERT_TRUE(XvtFlightTime_DelayForTicks(2) == 0);
	XVT_ASSERT_TRUE(XvtFlightTime_DelayForTicks(1) == 0);
}

static void CheckBegin(void)
{
	Clocks(XVT_FLIGHT_TIMING_NETWORK_125);
	g_predictedFrameDelta = 99;
	g_flightLastStepTargetTimestamp = 1234;
	g_flightPacketDropScore = 7;
	for (int i = 0; i < 20; ++i) {
		g_flightUpdateDurationHistogram[i] = (unsigned)i + 1;
	}
	XvtFlightFrame_Begin();
	XVT_ASSERT_INT_EQ(g_predictedFrameDelta, XVT_NETWORK_STEP_TICKS);
	XVT_ASSERT_INT_EQ(g_flightLastStepTargetTimestamp, 0);
	XVT_ASSERT_INT_EQ(g_flightPacketDropScore, 0);
	for (int i = 0; i < 20; ++i) {
		XVT_ASSERT_INT_EQ(g_flightUpdateDurationHistogram[i], 0);
	}
}

static void CheckReplayBuffered(void)
{
	/* An empty queue is idle. */
	Clocks(XVT_FLIGHT_TIMING_NETWORK_125);
	XVT_ASSERT_INT_EQ(XvtFlightFrame_ReplayBuffered(), XVT_REPLAY_IDLE);

	/* A message at or before the last confirmed tick is dropped: the queue runs empty. */
	Clocks(XVT_FLIGHT_TIMING_NETWORK_125);
	g_serverTickTime = 64;
	QueueReplay(64);
	QueueReplay(56 | XVT_WORLD_CHECKSUM_FLAG);
	XVT_ASSERT_INT_EQ(XvtFlightFrame_ReplayBuffered(), XVT_REPLAY_IDLE);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_REPLAY), 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 0);
	XVT_ASSERT_INT_EQ(g_serverTickTime, 64);

	/* One that is not exactly a message interval past it requests recovery. */
	Clocks(XVT_FLIGHT_TIMING_NETWORK_125);
	g_serverTickTime = 64;
	QueueReplay(64 + 2 * XVT_WORLD_MESSAGE_TICKS);
	XVT_ASSERT_INT_EQ(XvtFlightFrame_ReplayBuffered(), XVT_REPLAY_PENDING);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 1);
	XVT_ASSERT_INT_EQ(g_serverTickTime, 64);
	Clocks(XVT_FLIGHT_TIMING_NETWORK_125);
	g_serverTickTime = 64;
	QueueReplay(64 + XVT_NETWORK_STEP_TICKS);
	XVT_ASSERT_INT_EQ(XvtFlightFrame_ReplayBuffered(), XVT_REPLAY_PENDING);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 1);
}

static void CheckResetReplay(void)
{
	/* ResetReplay resets the flight simulation, which forgets the prediction fallback (flight_sim.h). */
	Clocks(XVT_FLIGHT_TIMING_NETWORK_125);
	g_players[1].participationState = 1;
	FlightInputFrameRecord input;
	memset(&input, 0, sizeof input);
	input.axisX = 20;
	XvtFlightPrediction_Confirm(1, 2, &input);
	XvtFlightFrame_ResetReplay();
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(8), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 0);
	/* Without the reset the same controls are predicted. */
	XvtFlightPrediction_Confirm(1, 2, &input);
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(8), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 1);
}

static void CheckNextWakeNative(void)
{
	/* The time until a step's worth of input time, 0 once it has passed. */
	Clocks(XVT_FLIGHT_TIMING_NATIVE);
	unsigned step = XvtFlightTiming_StepTicks();
	g_gameTime = 100;
	g_inputTimestamp = 100;
	XVT_ASSERT_TRUE(XvtFlightFrame_NextWakeDelayUs() ==
			(uint64_t)step * TICK_US);
	g_inputTimestamp = 100 + 3;
	XVT_ASSERT_TRUE(XvtFlightFrame_NextWakeDelayUs() ==
			(uint64_t)(step - 3) * TICK_US);
	g_inputTimestamp = 100 + (int)step;
	XVT_ASSERT_TRUE(XvtFlightFrame_NextWakeDelayUs() == 0);
	g_inputTimestamp = 100 + 10 * (int)step;
	XVT_ASSERT_TRUE(XvtFlightFrame_NextWakeDelayUs() == 0);
}

static void CheckNextWakeNetwork(void)
{
	/* Prediction work remains while the game time trails the input clock. */
	Clocks(XVT_FLIGHT_TIMING_NETWORK_125);
	g_serverTickTime = g_gameTime = 100;
	g_inputTimestamp = 100 + 4 * XVT_NETWORK_STEP_TICKS;
	XVT_ASSERT_TRUE(XvtFlightFrame_NextWakeDelayUs() == 0);

	/* Caught up: the sooner of the network's next event and the next simulation step. */
	g_inputTimestamp = 100;
	uint64_t network = XvtFlightNetwork_NextWakeDelayUs(g_inputTimestamp);
	uint64_t simulation =
		XvtFlightTime_DelayForTicks(XVT_NETWORK_STEP_TICKS);
	XVT_ASSERT_TRUE(network > 0 && simulation > 0);
	XVT_ASSERT_TRUE(XvtFlightFrame_NextWakeDelayUs() ==
			(network < simulation ? network : simulation));
	/* A pending world message is network work now. */
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Push(XVT_QUEUE_PENDING, "p", 1), 1);
	XVT_ASSERT_TRUE(XvtFlightFrame_NextWakeDelayUs() == 0);
	XvtFlightMessages_Clear(XVT_QUEUE_PENDING);

	/* Prediction stops XVT_PREDICTION_LEAD_TICKS past the last confirmed tick, however far ahead the input
	 * clock is: there is no work then. */
	g_inputTimestamp = 100 + 10 * XVT_PREDICTION_LEAD_TICKS;
	g_gameTime = 100 + XVT_PREDICTION_LEAD_TICKS;
	XVT_ASSERT_TRUE(XvtFlightFrame_NextWakeDelayUs() != 0);
	XVT_ASSERT_TRUE(XvtFlightFrame_NextWakeDelayUs() ==
			XvtFlightNetwork_NextWakeDelayUs(g_inputTimestamp));
	g_gameTime = 100 + XVT_PREDICTION_LEAD_TICKS - XVT_NETWORK_STEP_TICKS;
	XVT_ASSERT_TRUE(XvtFlightFrame_NextWakeDelayUs() == 0);
}

static void CheckNativeTickWaits(void)
{
	/* Before a step's worth of input time has passed, a native Update only moves the input clock on. */
	Clocks(XVT_FLIGHT_TIMING_NATIVE);
	g_gameTime = 100;
	g_inputTimestamp = 100;
	XVT_ASSERT_INT_EQ(Time_ConsumeElapsedTicks(), 0);
	XvtTime_AdvanceHostClock(TICK_US);
	XVT_ASSERT_INT_EQ(XvtFlightFrame_Update(), 0);
	XVT_ASSERT_INT_EQ(g_gameTime, 100);
	XVT_ASSERT_INT_EQ(g_inputTimestamp, 101);
	XVT_ASSERT_TRUE(XvtFlightFrame_NextWakeDelayUs() ==
			(uint64_t)(XvtFlightTiming_StepTicks() - 1) * TICK_US);
}

int main(void)
{
	CheckDelayForTicks();
	CheckBegin();
	CheckReplayBuffered();
	CheckResetReplay();
	CheckNextWakeNative();
	CheckNextWakeNetwork();
	CheckNativeTickWaits();
	XvtFlightMessages_Reset();
	XvtFlightTiming_EndSession();
	return 0;
}
