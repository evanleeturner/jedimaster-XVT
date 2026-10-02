/* Checks the flight task (xvt_runtime/runtime/flight_task.h) against the promises in its header, for the
 * parts that run without the recovered game's files or a display: Begin and its refusals, the state queries,
 * NextWakeDelayUs while preparing and during a resync, Shutdown, and the way a lost network session takes
 * the task through cleanup and the music fade to done. No game data is read: the test sets the globals it
 * reads itself. Every check starts from a task Shutdown left idle, a fresh network session, no resync, and
 * no music CD open.
 *
 * Not checked here: the phases from entry to the frame loop load the config, open the game session and
 * the display, and load the mission, its models and sounds from the retail game files; the cleanup steps
 * that follow a started world (committing results, restoring the resolution, saving the pilot, the CD fade)
 * need that world. */
#include "test_assert.h"
#include "xvt/audio/music_cd.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/net/flight_sync.h"
#include "xvt/net/net_session.h"
#include "xvt/util/time.h"
#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_prediction.h"
#include "xvt_runtime/runtime/flight_task.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/runtime/resync_task.h"
#include "xvt_runtime/timing/flight_integration.h"
#include "xvt_runtime/timing/flight_timing.h"
#include "xvt_runtime/timing/host_clock.h"

#include <stdint.h>
#include <string.h>

static ObjectRecord g_testObjects[2];
static uint8_t g_testWorld[16], g_testWorldCopy[16];

static void World(void) {
	XvtFlightTask_Shutdown();
	XvtNetworkSession_Shutdown();
	memset(&g_netSession, 0, sizeof g_netSession);
	XvtResync_Reset();
	XvtFlightNetwork_ClearCookies();
	XvtTime_Reset();
	XvtTime_AdvanceHostClock(5000000);
	Time_ResetElapsedTicks();
	g_musicCdMciDeviceId = 0;
	g_activeFlightPlayerCount = 1;
	memset(g_players, 0, sizeof g_players);
	memset(g_inputHistory, 0, sizeof g_inputHistory);
	memset(g_inputFrameCount, 0, sizeof g_inputFrameCount);
	for (int i = 0; i < 8; ++i)
		g_players[i].objectIndex = -1;
	g_localPlayer = 0;
	memset(g_testObjects, 0, sizeof g_testObjects);
	g_objectTable = g_testObjects;
	g_regionMainObjectSlotEnd = 1;
	g_regionStaticObjectSlotCount = 1;
}

static void AssertIdle(void) {
	XVT_ASSERT_INT_EQ(XvtFlightTask_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTask_IsLoading(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTask_IsComplete(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTask_GetResult(), 0);
	XVT_ASSERT_TRUE(XvtFlightTask_NextWakeDelayUs() == UINT64_MAX);
}

static void CheckBegin(void) {
	World();
	AssertIdle();
	XVT_ASSERT_INT_EQ(XvtFlightTask_Begin(NULL), 0);
	AssertIdle();

	XVT_ASSERT_INT_EQ(XvtFlightTask_Begin("mission"), 1);
	XVT_ASSERT_INT_EQ(XvtFlightTask_IsActive(), 1);
	XVT_ASSERT_INT_EQ(XvtFlightTask_IsLoading(), 1);
	XVT_ASSERT_INT_EQ(XvtFlightTask_IsComplete(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTask_GetResult(), 0);
	/* Preparing asks for no timed wake. */
	XVT_ASSERT_TRUE(XvtFlightTask_NextWakeDelayUs() == UINT64_MAX);

	/* Not while a flight is active, whatever the command. */
	XVT_ASSERT_INT_EQ(XvtFlightTask_Begin("another"), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTask_Begin(NULL), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTask_IsActive(), 1);

	/* A command longer than the copy is still a flight. */
	World();
	static char longCommand[4096];
	memset(longCommand, 'x', sizeof longCommand - 1);
	XVT_ASSERT_INT_EQ(XvtFlightTask_Begin(longCommand), 1);
	XVT_ASSERT_INT_EQ(XvtFlightTask_IsActive(), 1);
}

static void CheckBeginResetsSimulation(void) {
	/* Begin resets the flight simulation, which forgets the prediction fallback (flight_sim.h). */
	World();
	g_players[1].participationState = 1;
	FlightInputFrameRecord input;
	memset(&input, 0, sizeof input);
	input.axisX = 20;
	XvtFlightPrediction_Confirm(1, 2, &input);
	XVT_ASSERT_INT_EQ(XvtFlightTask_Begin("mission"), 1);
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(8), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 0);
	/* A refused Begin does not. */
	XvtFlightPrediction_Confirm(1, 2, &input);
	XVT_ASSERT_INT_EQ(XvtFlightTask_Begin("mission"), 0);
	XVT_ASSERT_INT_EQ(XvtFlightPrediction_Queue(8), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 1);
}

static void CheckContinuesWithoutFocus(void) {
	World();
	g_activeFlightPlayerCount = 2;
	XVT_ASSERT_INT_EQ(XvtFlightTask_ContinuesWithoutFocus(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTask_Begin("mission"), 1);
	XVT_ASSERT_INT_EQ(XvtFlightTask_ContinuesWithoutFocus(), 1);
	g_activeFlightPlayerCount = 1;
	XVT_ASSERT_INT_EQ(XvtFlightTask_ContinuesWithoutFocus(), 0);
}

static void CheckWakeDuringResync(void) {
	/* While a resync is active the task wakes when the resync does. The apply phase is a running send. */
	World();
	XVT_ASSERT_INT_EQ(XvtFlightTask_Begin("mission"), 1);
	g_netSession.localIsHost = 1;
	XvtResync_BeginApply(101, 4096);
	XVT_ASSERT_INT_EQ(XvtResync_IsActive(), 1);
	XVT_ASSERT_TRUE(XvtFlightTask_NextWakeDelayUs() == XvtResync_NextWakeDelayUs());
	XVT_ASSERT_TRUE(XvtFlightTask_NextWakeDelayUs() != UINT64_MAX);
}

static void CheckShutdown(void) {
	/* An active flight is released: the task is idle with a result of 0, the resync and the flight
	 * network state are reset. */
	World();
	XVT_ASSERT_INT_EQ(XvtFlightTask_Begin("mission"), 1);
	g_netSession.localIsHost = 1;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_ExchangeOptions(), 1);
	XVT_ASSERT_TRUE(XvtFlightNetwork_Cookie() != 0);
	XvtResync_BeginApply(101, 4096);
	XvtFlightTask_Shutdown();
	AssertIdle();
	XVT_ASSERT_INT_EQ(XvtResync_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Cookie(), 0);

	/* Idle, it still resets them. */
	World();
	g_netSession.localIsHost = 1;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_ExchangeOptions(), 1);
	XvtResync_BeginApply(101, 4096);
	XvtFlightTask_Shutdown();
	AssertIdle();
	XVT_ASSERT_INT_EQ(XvtResync_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Cookie(), 0);

	/* A new flight can begin afterwards. */
	XVT_ASSERT_INT_EQ(XvtFlightTask_Begin("mission"), 1);
}

static void CheckLostSession(void) {
	World();
	XVT_ASSERT_INT_EQ(XvtFlightTask_Begin("mission"), 1);
	/* State the release tears down: a timing session with its integration table, world buffers, a
	 * queued world message. */
	XvtFlightTiming_BeginSession(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Init(2), 1);
	g_testObjects[0].objectType = 1;
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(0, XVT_INTEGRATE_PUSH_X, 3, 1, 4), 0);
	g_worldStateBuffer = g_testWorld;
	g_worldStateDupBuffer = g_testWorldCopy;
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Push(XVT_QUEUE_PENDING, "p", 1), 1);

	/* A lost session sends the task to cleanup with a result of 0: the mission is released in that tick,
	 * and with no CD fade the next tick finishes the task. */
	XvtNetworkSession_HostLost();
	XvtFlightTask_Update();
	XVT_ASSERT_INT_EQ(XvtFlightTask_GetResult(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTask_IsLoading(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTiming_Profile(), XVT_FLIGHT_TIMING_NATIVE);
	XVT_ASSERT_TRUE(g_worldStateBuffer == NULL);
	XVT_ASSERT_TRUE(g_worldStateDupBuffer == NULL);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 0);
	/* Without the integration table, Rate returns the plain truncated result (flight_integration.h). */
	XVT_ASSERT_INT_EQ(XvtFlightIntegration_Rate(0, XVT_INTEGRATE_PUSH_X, 1, 1, 4), 0);
	XvtFlightTask_Update();
	XVT_ASSERT_INT_EQ(XvtFlightTask_IsComplete(), 1);
	XVT_ASSERT_INT_EQ(XvtFlightTask_IsActive(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightTask_GetResult(), 0);
	/* Complete until Shutdown or the next Begin. */
	XvtFlightTask_Update();
	XVT_ASSERT_INT_EQ(XvtFlightTask_IsComplete(), 1);
	XvtNetworkSession_Shutdown();
	XVT_ASSERT_INT_EQ(XvtFlightTask_Begin("mission"), 1);
	XVT_ASSERT_INT_EQ(XvtFlightTask_IsComplete(), 0);
	XvtFlightTask_Shutdown();
	XVT_ASSERT_INT_EQ(XvtFlightTask_IsComplete(), 0);

	/* The header warns that this happens to an idle task too. */
	World();
	XvtNetworkSession_HostLost();
	XvtFlightTask_Update();
	XvtFlightTask_Update();
	XVT_ASSERT_INT_EQ(XvtFlightTask_IsComplete(), 1);
	XVT_ASSERT_INT_EQ(XvtFlightTask_GetResult(), 0);
}

int main(void) {
	CheckBegin();
	CheckBeginResetsSimulation();
	CheckContinuesWithoutFocus();
	CheckWakeDuringResync();
	CheckShutdown();
	CheckLostSession();
	World();
	XvtNetworkSession_Shutdown();
	return 0;
}
