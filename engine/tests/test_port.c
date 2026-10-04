/* Checks the game runtime's host interface (xvt_runtime/runtime/port.h) against
 * the promises in its header that hold without a window: Init's refusal of a
 * logical size other than 640x480 and the exit code it sets, the port before
 * Init (not initialized, quitting, a Update that does nothing, a Shutdown that
 * only lifts the classic rendering suppression), the settings request latch,
 * and when the network requires progress. Aeron runs without a window here; the
 * test sets its logical size. Every case starts with the port shut down and the
 * frontend cleared.
 *
 * Not checked here: a successful Init and everything that runs after it
 * (Update's frames, pausing, quitting, the wake delay and Shutdown after Init),
 * which start the frontend with its main window, the config and the game's
 * files. */
#include <string.h>

#include "aeron/aeron.h"
#include "aeron/compat/host.h"
#include "test_assert.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/runtime/network_task.h"
#include "xvt_runtime/runtime/port.h"
#include "xvt_runtime/timing/host_clock.h"

static int placeholder(int frame) { return frame; }

static void fresh(void)
{
	xvt_port_shutdown();
	xvt_network_task_shutdown();
	xvt_network_session_shutdown();
	memset(&g_front_state, 0, sizeof g_front_state);
	memset(&g_pilot_data, 0, sizeof g_pilot_data);
	g_front_state.screen_states[0].update_fn = placeholder;
}

static void check_init_refuses_size(void)
{
	const int sizes[][2] = {{800, 600}, {640, 400}, {852, 480}};
	for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; ++i) {
		fresh();
		XVT_ASSERT_INT_EQ(
			Aeron_SetLogicalSize(sizes[i][0], sizes[i][1]), 1);
		XVT_ASSERT_INT_EQ(xvt_port_init(), 0);
		XVT_ASSERT_INT_EQ(xvt_port_get_exit_code(), 1);
		XVT_ASSERT_INT_EQ(xvt_port_is_initialized(), 0);
		XVT_ASSERT_INT_EQ(xvt_port_service_quit(), 1);
	}
}

static void check_before_init(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_port_is_initialized(), 0);
	XVT_ASSERT_INT_EQ(xvt_port_service_quit(), 1);

	/* Update does nothing once ShouldQuit is 1: the host clock does not move. */
	xvt_time_reset();
	xvt_port_update(16000);
	XVT_ASSERT_INT_EQ(xvt_time_get_elapsed_us(), 0);

	/* Shutdown before Init only lifts the classic rendering suppression. */
	AeronDx5_SetClassicFlightRenderingSuppressed(1);
	xvt_port_shutdown();
	XVT_ASSERT_INT_EQ(AeronDx5_IsClassicFlightRenderingSuppressed(), 0);
	XVT_ASSERT_INT_EQ(xvt_port_is_initialized(), 0);
}

static void check_settings_latch(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_port_consume_settings_request(), 0);
	xvt_port_request_settings();
	xvt_port_request_settings();
	XVT_ASSERT_INT_EQ(xvt_port_consume_settings_request(), 1);
	XVT_ASSERT_INT_EQ(xvt_port_consume_settings_request(), 0);
}

static void check_network_requires_progress(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_port_network_requires_progress(), 0);

	/* The game browser is visible: the join screen is on the stack. */
	g_front_state.screen_states[0].update_fn =
		frontend_net_join_game_screen;
	XVT_ASSERT_INT_EQ(xvt_port_network_requires_progress(), 1);
	g_front_state.screen_states[0].update_fn = placeholder;
	XVT_ASSERT_INT_EQ(xvt_port_network_requires_progress(), 0);

	/* A host attempt runs in the network task. */
	xvt_network_task_begin(XVT_NETWORK_HOST);
	XVT_ASSERT_INT_EQ(xvt_network_task_is_active(), 1);
	XVT_ASSERT_INT_EQ(xvt_port_network_requires_progress(), 1);
	xvt_network_task_shutdown();
	XVT_ASSERT_INT_EQ(xvt_port_network_requires_progress(), 0);
}

int main(void)
{
	check_init_refuses_size();
	check_before_init();
	check_settings_latch();
	check_network_requires_progress();
	xvt_port_shutdown();
	xvt_network_session_shutdown();
	return 0;
}
