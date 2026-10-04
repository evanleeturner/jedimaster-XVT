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
#include <stdint.h>
#include <string.h>

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

static struct object_record g_test_objects[2];
static uint8_t g_test_world[16], g_test_world_copy[16];

static void flight_task_world(void)
{
	xvt_flight_task_shutdown();
	xvt_network_session_shutdown();
	memset(&g_net_session, 0, sizeof g_net_session);
	xvt_resync_reset();
	xvt_flight_network_clear_cookies();
	xvt_time_reset();
	xvt_time_advance_host_clock(5000000);
	time_reset_elapsed_ticks();
	g_music_cd_mci_device_id = 0;
	g_active_flight_player_count = 1;
	memset(g_players, 0, sizeof g_players);
	memset(g_input_history, 0, sizeof g_input_history);
	memset(g_input_frame_count, 0, sizeof g_input_frame_count);
	for (int i = 0; i < 8; ++i) {
		g_players[i].object_index = -1;
	}
	g_local_player = 0;
	memset(g_test_objects, 0, sizeof g_test_objects);
	g_object_table = g_test_objects;
	g_region_main_object_slot_end = 1;
	g_region_static_object_slot_count = 1;
}

static void assert_idle(void)
{
	XVT_ASSERT_INT_EQ(xvt_flight_task_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_task_is_loading(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_task_is_complete(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_task_get_result(), 0);
	XVT_ASSERT_TRUE(xvt_flight_task_next_wake_delay_us() == UINT64_MAX);
}

static void check_begin(void)
{
	flight_task_world();
	assert_idle();
	XVT_ASSERT_INT_EQ(xvt_flight_task_begin(NULL), 0);
	assert_idle();

	XVT_ASSERT_INT_EQ(xvt_flight_task_begin("mission"), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_task_is_active(), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_task_is_loading(), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_task_is_complete(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_task_get_result(), 0);
	/* Preparing asks for no timed wake. */
	XVT_ASSERT_TRUE(xvt_flight_task_next_wake_delay_us() == UINT64_MAX);

	/* Not while a flight is active, whatever the command. */
	XVT_ASSERT_INT_EQ(xvt_flight_task_begin("another"), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_task_begin(NULL), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_task_is_active(), 1);

	/* A command longer than the copy is still a flight. */
	flight_task_world();
	static char long_command[4096];
	memset(long_command, 'x', sizeof long_command - 1);
	XVT_ASSERT_INT_EQ(xvt_flight_task_begin(long_command), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_task_is_active(), 1);
}

static void check_begin_resets_simulation(void)
{
	/* Begin resets the flight simulation, which forgets the prediction fallback (flight_sim.h). */
	flight_task_world();
	g_players[1].participation_state = 1;
	struct flight_input_frame_record input;
	memset(&input, 0, sizeof input);
	input.axis_x = 20;
	xvt_flight_prediction_confirm(1, 2, &input);
	XVT_ASSERT_INT_EQ(xvt_flight_task_begin("mission"), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(8), 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 0);
	/* A refused Begin does not. */
	xvt_flight_prediction_confirm(1, 2, &input);
	XVT_ASSERT_INT_EQ(xvt_flight_task_begin("mission"), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_prediction_queue(8), 1);
	XVT_ASSERT_INT_EQ(g_input_frame_count[1], 1);
}

static void check_continues_without_focus(void)
{
	flight_task_world();
	g_active_flight_player_count = 2;
	XVT_ASSERT_INT_EQ(xvt_flight_task_continues_without_focus(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_task_begin("mission"), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_task_continues_without_focus(), 1);
	g_active_flight_player_count = 1;
	XVT_ASSERT_INT_EQ(xvt_flight_task_continues_without_focus(), 0);
}

static void check_wake_during_resync(void)
{
	/* While a resync is active the task wakes when the resync does. The apply phase is a running send. */
	flight_task_world();
	XVT_ASSERT_INT_EQ(xvt_flight_task_begin("mission"), 1);
	g_net_session.local_is_host = 1;
	xvt_resync_begin_apply(101, 4096);
	XVT_ASSERT_INT_EQ(xvt_resync_is_active(), 1);
	XVT_ASSERT_TRUE(xvt_flight_task_next_wake_delay_us() ==
			xvt_resync_next_wake_delay_us());
	XVT_ASSERT_TRUE(xvt_flight_task_next_wake_delay_us() != UINT64_MAX);
}

static void check_shutdown(void)
{
	/* An active flight is released: the task is idle with a result of 0, the resync and the flight
	 * network state are reset. */
	flight_task_world();
	XVT_ASSERT_INT_EQ(xvt_flight_task_begin("mission"), 1);
	g_net_session.local_is_host = 1;
	XVT_ASSERT_INT_EQ(xvt_flight_network_exchange_options(), 1);
	XVT_ASSERT_TRUE(xvt_flight_network_cookie() != 0);
	xvt_resync_begin_apply(101, 4096);
	xvt_flight_task_shutdown();
	assert_idle();
	XVT_ASSERT_INT_EQ(xvt_resync_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_network_cookie(), 0);

	/* Idle, it still resets them. */
	flight_task_world();
	g_net_session.local_is_host = 1;
	XVT_ASSERT_INT_EQ(xvt_flight_network_exchange_options(), 1);
	xvt_resync_begin_apply(101, 4096);
	xvt_flight_task_shutdown();
	assert_idle();
	XVT_ASSERT_INT_EQ(xvt_resync_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_network_cookie(), 0);

	/* A new flight can begin afterwards. */
	XVT_ASSERT_INT_EQ(xvt_flight_task_begin("mission"), 1);
}

static void check_lost_session(void)
{
	flight_task_world();
	XVT_ASSERT_INT_EQ(xvt_flight_task_begin("mission"), 1);
	/* State the release tears down: a timing session with its integration table, world buffers, a
	 * queued world message. */
	xvt_flight_timing_begin_session(XVT_FLIGHT_TIMING_OFFLINE_UNLOCKED);
	XVT_ASSERT_INT_EQ(xvt_flight_integration_init(2), 1);
	g_test_objects[0].object_type = 1;
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(0, XVT_INTEGRATE_PUSH_X, 3, 1, 4),
		0);
	g_world_state_buffer = g_test_world;
	g_world_state_dup_buffer = g_test_world_copy;
	XVT_ASSERT_INT_EQ(xvt_flight_messages_push(XVT_QUEUE_PENDING, "p", 1),
			  1);

	/* A lost session sends the task to cleanup with a result of 0: the mission is released in that tick,
	 * and with no CD fade the next tick finishes the task. */
	xvt_network_session_host_lost();
	xvt_flight_task_update();
	XVT_ASSERT_INT_EQ(xvt_flight_task_get_result(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_task_is_loading(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_timing_session_profile(),
			  XVT_FLIGHT_TIMING_NATIVE);
	XVT_ASSERT_TRUE(g_world_state_buffer == NULL);
	XVT_ASSERT_TRUE(g_world_state_dup_buffer == NULL);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 0);
	/* Without the integration table, Rate returns the plain truncated result (flight_integration.h). */
	XVT_ASSERT_INT_EQ(
		xvt_flight_integration_rate(0, XVT_INTEGRATE_PUSH_X, 1, 1, 4),
		0);
	xvt_flight_task_update();
	XVT_ASSERT_INT_EQ(xvt_flight_task_is_complete(), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_task_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_task_get_result(), 0);
	/* Complete until Shutdown or the next Begin. */
	xvt_flight_task_update();
	XVT_ASSERT_INT_EQ(xvt_flight_task_is_complete(), 1);
	xvt_network_session_shutdown();
	XVT_ASSERT_INT_EQ(xvt_flight_task_begin("mission"), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_task_is_complete(), 0);
	xvt_flight_task_shutdown();
	XVT_ASSERT_INT_EQ(xvt_flight_task_is_complete(), 0);

	/* The header warns that this happens to an idle task too. */
	flight_task_world();
	xvt_network_session_host_lost();
	xvt_flight_task_update();
	xvt_flight_task_update();
	XVT_ASSERT_INT_EQ(xvt_flight_task_is_complete(), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_task_get_result(), 0);
}

int main(void)
{
	check_begin();
	check_begin_resets_simulation();
	check_continues_without_focus();
	check_wake_during_resync();
	check_shutdown();
	check_lost_session();
	flight_task_world();
	xvt_network_session_shutdown();
	return 0;
}
