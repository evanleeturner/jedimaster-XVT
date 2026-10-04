/* Checks the frontend-to-flight hand-off (xvt_runtime/runtime/launch_task.h) against the promises in its
 * header that hold while no launch is queued: the idle phase reports no launch, BeginPendingLaunch refuses,
 * Complete and Update do nothing, and Shutdown leaves the task idle. The test sets the frontend state it
 * watches; every case starts from Shutdown and a cleared frontend with a placeholder screen at frame 4.
 *
 * Not checked here: Queue and every phase after it. Queue writes the settings file and the pilot file,
 * checks the installation and reads the game's mission list for the selected mission before it builds the
 * flight command, and completing a flight that ran restores the frontend's window surfaces; a test would
 * need the game's mission files, a loaded configuration and a window. */
#include <string.h>

#include "test_assert.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt_runtime/runtime/launch_task.h"
#include "xvt_runtime/runtime/network_session.h"

static int placeholder(int frame) { return frame; }

static void fresh(void)
{
	xvt_launch_task_shutdown();
	xvt_network_session_shutdown();
	memset(&g_front_state, 0, sizeof g_front_state);
	g_front_state.screen_states[0].update_fn = placeholder;
	g_front_state.frame_counter = 4;
	g_front_state.screen_callbacks_dirty = 1;
}

static void check_idle(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_launch_task_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_launch_task_has_pending_launch(), 0);
	XVT_ASSERT_TRUE(xvt_launch_task_begin_pending_launch() == NULL);
	XVT_ASSERT_INT_EQ(xvt_launch_task_is_active(), 0);
}

static void check_complete_ignored_when_idle(void)
{
	for (int succeeded = 0; succeeded < 2; ++succeeded) {
		fresh();
		g_front_state.cursor_visible = 0;
		xvt_launch_task_complete(succeeded);
		/* No screen switch, no frame reset, no cursor, no session shutdown. */
		XVT_ASSERT_TRUE(g_front_state.screen_states[0].update_fn ==
				placeholder);
		XVT_ASSERT_INT_EQ(g_front_state.frame_counter, 4);
		XVT_ASSERT_INT_EQ(g_front_state.screen_callbacks_dirty, 1);
		XVT_ASSERT_INT_EQ(g_front_state.cursor_visible, 0);
		XVT_ASSERT_INT_EQ(xvt_network_session_get_status().state,
				  XVT_NETWORK_SESSION_IDLE);
		XVT_ASSERT_INT_EQ(xvt_launch_task_is_active(), 0);
	}
}

static void check_tick_ignored_when_idle(void)
{
	fresh();
	/* Escape cancels only a fade or a pending launch; idle, it stays in the keyboard buffer. */
	g_front_state.char_ring_buffer[0] = 27;
	g_front_state.char_write_idx = 1;
	xvt_launch_task_update();
	XVT_ASSERT_INT_EQ(g_front_state.char_read_idx, 0);
	XVT_ASSERT_INT_EQ(g_front_state.char_write_idx, 1);
	XVT_ASSERT_TRUE(g_front_state.screen_states[0].update_fn ==
			placeholder);
	XVT_ASSERT_INT_EQ(xvt_launch_task_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_launch_task_has_pending_launch(), 0);
}

int main(void)
{
	check_idle();
	check_complete_ignored_when_idle();
	check_tick_ignored_when_idle();
	xvt_launch_task_shutdown();
	return 0;
}
