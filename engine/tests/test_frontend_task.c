/* Checks the frontend's frame loop (xvt_runtime/runtime/frontend_task.h) against the promises in its
 * header that hold without the game's window and files: run_frame's update, frame counter, exit callback
 * and pending screen push, a dialog continuation run in place of the update, and the frame held for an
 * opened dialog or the network task; Update's pacing, the dialog that updates alone, a quit result, and when
 * a frame is presented; the wake delay; the joystick polling interval and the CD task tick of
 * ServiceFrameSystems; and Shutdown before Init. The frontend runs on a display with no window
 * (test_frontend_display.h); presented frames are counted through Aeron's classic frame serial, which rises
 * once for each frame the frontend presents. The test's own screens stand in for the game's. Init is never
 * called, so the startup frame does not run. Every case starts from a cleared frontend with a 40 ms frame
 * interval and no dialog, network task or CD fade, the clock running on from the case before; the case
 * that quits runs last, since nothing but Init clears the quit.
 *
 * Not checked here: Init and Shutdown after it, which need the main window, the game's files and the
 * config; the launch task's turn in Update, which needs a queued launch; the hold for a pending campaign
 * prefix, which takes the same path as the network task's hold checked here; drawing the cursor; and the
 * end of the program when the back buffer cannot be locked. */
#include <stdint.h>
#include <string.h>

#include "aeron/compat/host.h"
#include "test_assert.h"
#include "test_frontend_display.h"
#include "xvt/audio/music_cd.h"
#include "xvt/frontend/cutscene.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt_runtime/runtime/cd_task.h"
#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt_runtime/runtime/frontend_task.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/runtime/network_task.h"
#include "xvt_runtime/timing/host_clock.h"

enum { FRAME_MS = 40 };

static int g_screen_calls;
static int g_screen_frame;
static int g_screen_return;
static int g_screen_action;
static int g_exit_calls;
static int g_other_exit_calls;
static int g_dialog_calls;

enum { ACTION_NONE, ACTION_SWITCH, ACTION_QUEUE, ACTION_DIALOG };

static int other_screen(int frame) { return frame * 0; }

static int other_exit(int frame)
{
	(void)frame;
	++g_other_exit_calls;
	return 0;
}

static int dialog_screen(int frame)
{
	(void)frame;
	++g_dialog_calls;
	return 0;
}

/* The top screen's update: counts its calls, and on request switches screens, queues a screen push or
 * opens a dialog before returning g_screen_return. */
static int screen(int frame)
{
	static const struct RECT whole = {0, 0, 639, 479};
	++g_screen_calls;
	g_screen_frame = frame;
	if (g_screen_action == ACTION_SWITCH) {
		frontend_screen_set_callbacks(other_screen, other_exit);
	} else if (g_screen_action == ACTION_QUEUE) {
		frontend_screen_queue_push(other_screen, &whole);
	} else if (g_screen_action == ACTION_DIALOG) {
		xvt_dialog_begin(dialog_screen, NULL);
	}
	return g_screen_return;
}

static int frontend_task_exit(int frame)
{
	(void)frame;
	++g_exit_calls;
	return 0;
}

static int continuation(int result, int context)
{
	(void)result;
	(void)context;
	return 5;
}

static void fresh(void)
{
	xvt_dialog_shutdown();
	xvt_network_task_shutdown();
	xvt_network_session_shutdown();
	xvt_cd_task_cancel_fade();
	xvt_test_close_display();
	memset(&g_front_state, 0, sizeof g_front_state);
	xvt_test_open_display();
	memset(&g_pilot_data, 0, sizeof g_pilot_data);
	g_music_cd_mci_device_id = 0;
	g_front_state.frame_interval_ms = FRAME_MS;
	g_front_state.screen_states[0].update_fn = screen;
	g_front_state.screen_states[0].exit_fn = frontend_task_exit;
	g_front_state.frame_counter = 3;
	g_screen_calls = g_screen_frame = g_screen_return = 0;
	g_screen_action = ACTION_NONE;
	g_exit_calls = g_other_exit_calls = g_dialog_calls = 0;
}

static void advance_ms(int ms) { xvt_time_advance_host_clock(ms * 1000); }

/* The clock runs on across cases, and so does the frame deadline: move past any deadline an earlier case
 * left, so the next Update runs a frame. */
static void frame_due(void) { advance_ms(1000); }

static void check_run_frame_without_update(void)
{
	fresh();
	g_front_state.screen_states[0].update_fn = NULL;
	XVT_ASSERT_INT_EQ(xvt_frontend_task_run_frame(), 0);
	XVT_ASSERT_INT_EQ(g_exit_calls, 0);
}

static void check_run_frame(void)
{
	fresh();
	g_screen_return = 7;
	XVT_ASSERT_INT_EQ(xvt_frontend_task_run_frame(), 7);
	XVT_ASSERT_INT_EQ(g_screen_calls, 1);
	XVT_ASSERT_INT_EQ(g_screen_frame, 3);
	XVT_ASSERT_INT_EQ(g_front_state.frame_counter, 4);
	/* The callbacks did not change and the result is not 1: no exit callback. */
	XVT_ASSERT_INT_EQ(g_exit_calls, 0);

	/* A result of 1 runs the exit callback. */
	g_screen_return = 1;
	XVT_ASSERT_INT_EQ(xvt_frontend_task_run_frame(), 1);
	XVT_ASSERT_INT_EQ(g_screen_frame, 4);
	XVT_ASSERT_INT_EQ(g_exit_calls, 1);
}

static void check_switch_runs_captured_exit(void)
{
	fresh();
	g_screen_action = ACTION_SWITCH;
	XVT_ASSERT_INT_EQ(xvt_frontend_task_run_frame(), 0);
	/* The exit callback captured before the update runs, not the new screen's. */
	XVT_ASSERT_INT_EQ(g_exit_calls, 1);
	XVT_ASSERT_INT_EQ(g_other_exit_calls, 0);
	XVT_ASSERT_TRUE(g_front_state.screen_states[0].update_fn ==
			other_screen);
	XVT_ASSERT_INT_EQ(g_front_state.screen_callbacks_dirty, 0);

	/* A callback change already pending when the frame starts also runs the exit callback. */
	fresh();
	g_front_state.screen_callbacks_dirty = 1;
	XVT_ASSERT_INT_EQ(xvt_frontend_task_run_frame(), 0);
	XVT_ASSERT_INT_EQ(g_exit_calls, 1);
}

static void check_pending_push(void)
{
	fresh();
	g_screen_action = ACTION_QUEUE;
	XVT_ASSERT_INT_EQ(xvt_frontend_task_run_frame(), 0);
	XVT_ASSERT_INT_EQ(g_front_state.screen_stack_top, 1);
	XVT_ASSERT_TRUE(g_front_state.screen_states[1].update_fn ==
			other_screen);
	frontend_screen_pop_state();
}

static void check_dialog_holds_frame(void)
{
	fresh();
	g_screen_action = ACTION_DIALOG;
	g_screen_return = 1;
	XVT_ASSERT_INT_EQ(xvt_frontend_task_run_frame(), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);
	XVT_ASSERT_INT_EQ(g_front_state.frame_counter, 3);
	XVT_ASSERT_INT_EQ(g_exit_calls, 0);
}

static void check_continuation_replaces_update(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_dialog_begin(dialog_screen, NULL),
			  XVT_DIALOG_PENDING);
	xvt_dialog_continue_with(continuation, 0);
	xvt_dialog_update();
	g_front_state.char_ring_buffer[g_front_state.char_write_idx++] = 27;
	xvt_dialog_update();
	XVT_ASSERT_INT_EQ(xvt_dialog_has_result(), 1);

	/* The parent's next frame runs the continuation in place of its update. */
	XVT_ASSERT_INT_EQ(xvt_frontend_task_run_frame(), 5);
	XVT_ASSERT_INT_EQ(g_screen_calls, 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_has_result(), 0);
	/* The frame after that runs the update again. */
	xvt_frontend_task_run_frame();
	XVT_ASSERT_INT_EQ(g_screen_calls, 1);
}

static void check_network_task_holds_frame(void)
{
	fresh();
	xvt_network_task_begin(XVT_NETWORK_HOST);
	XVT_ASSERT_INT_EQ(xvt_network_task_is_active(), 1);
	/* The network task is resumed in place of the update, and the frame is held. */
	XVT_ASSERT_INT_EQ(xvt_frontend_task_run_frame(), 0);
	XVT_ASSERT_INT_EQ(g_screen_calls, 0);
	XVT_ASSERT_INT_EQ(g_front_state.frame_counter, 3);
	XVT_ASSERT_INT_EQ(g_exit_calls, 0);
}

static void check_tick_pacing(void)
{
	fresh();
	frame_due();
	xvt_frontend_task_update();
	XVT_ASSERT_INT_EQ(g_screen_calls, 1);
	xvt_frontend_task_update();
	XVT_ASSERT_INT_EQ(g_screen_calls, 1);
	XVT_ASSERT_INT_EQ(xvt_frontend_task_next_wake_delay_us(),
			  FRAME_MS * 1000);
	advance_ms(FRAME_MS - 1);
	XVT_ASSERT_INT_EQ(xvt_frontend_task_next_wake_delay_us(), 1000);
	xvt_frontend_task_update();
	XVT_ASSERT_INT_EQ(g_screen_calls, 1);
	advance_ms(1);
	XVT_ASSERT_INT_EQ(xvt_frontend_task_next_wake_delay_us(), 0);
	xvt_frontend_task_update();
	XVT_ASSERT_INT_EQ(g_screen_calls, 2);
	XVT_ASSERT_INT_EQ(xvt_frontend_task_should_quit(), 0);
}

static void check_tick_presents(void)
{
	fresh();
	frame_due();
	uint64_t serial = AeronDx5_GetClassicFlightFrameSerial();
	xvt_frontend_task_update();
	XVT_ASSERT_INT_EQ(g_screen_calls, 1);
	XVT_ASSERT_INT_EQ(AeronDx5_GetClassicFlightFrameSerial(), serial + 1);
}

static void check_tick_runs_only_the_dialog(void)
{
	fresh();
	frame_due();
	XVT_ASSERT_INT_EQ(xvt_dialog_begin(dialog_screen, NULL),
			  XVT_DIALOG_PENDING);
	xvt_frontend_task_update();
	XVT_ASSERT_INT_EQ(g_dialog_calls, 1);
	XVT_ASSERT_INT_EQ(g_screen_calls, 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);

	/* The tick that ends the dialog keeps its last presented frame: nothing new is presented. */
	g_front_state.char_ring_buffer[g_front_state.char_write_idx++] = 27;
	advance_ms(FRAME_MS);
	uint64_t serial = AeronDx5_GetClassicFlightFrameSerial();
	xvt_frontend_task_update();
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
	XVT_ASSERT_INT_EQ(AeronDx5_GetClassicFlightFrameSerial(), serial);
	XVT_ASSERT_INT_EQ(g_screen_calls, 0);
}

static void check_wake_delay_takes_cd_sooner(void)
{
	fresh();
	frame_due();
	xvt_frontend_task_update();
	g_music_cd_mci_device_id = 1;
	XVT_ASSERT_INT_EQ(xvt_cd_task_begin_fade(0, 512, 0), 1);
	XVT_ASSERT_INT_EQ(xvt_frontend_task_next_wake_delay_us(), 1000);
	xvt_cd_task_cancel_fade();
	XVT_ASSERT_INT_EQ(xvt_frontend_task_next_wake_delay_us(),
			  FRAME_MS * 1000);
}

static void check_service_frame_systems(void)
{
	fresh();
	/* A joystick marked present that cannot be read is dropped when it is polled. */
	xvt_frontend_task_service_frame_systems();
	advance_ms(100);
	g_front_state.joystick_present[0] = 1;
	g_front_state.joystick_present[1] = 1;
	g_front_state.joy_device_ids[0] = 77;
	g_front_state.joy_device_ids[1] = 78;
	xvt_frontend_task_service_frame_systems();
	XVT_ASSERT_INT_EQ(g_front_state.joystick_present[0], 0);
	XVT_ASSERT_INT_EQ(g_front_state.joystick_present[1], 0);

	/* Not again before 100 ms have passed. */
	g_front_state.joystick_present[0] = 1;
	advance_ms(99);
	xvt_frontend_task_service_frame_systems();
	XVT_ASSERT_INT_EQ(g_front_state.joystick_present[0], 1);
	advance_ms(1);
	xvt_frontend_task_service_frame_systems();
	XVT_ASSERT_INT_EQ(g_front_state.joystick_present[0], 0);

	/* The CD task is ticked: a due fade step moves the volume. */
	g_music_cd_mci_device_id = 1;
	XVT_ASSERT_INT_EQ(xvt_cd_task_begin_fade(0, 512, 0), 1);
	g_front_state.cd_audio_track_cache.current_aux_volume = 12345;
	advance_ms(1);
	xvt_frontend_task_service_frame_systems();
	XVT_ASSERT_INT_EQ(g_front_state.cd_audio_track_cache.current_aux_volume,
			  256);
	xvt_cd_task_cancel_fade();
}

static void check_shutdown_before_init(void)
{
	static struct cutscene_entry table[1];
	fresh();
	g_cutscene_table = table;
	g_cutscene_count = 1;
	xvt_frontend_task_shutdown();
	XVT_ASSERT_TRUE(g_cutscene_table == table);
	XVT_ASSERT_INT_EQ(g_cutscene_count, 1);
	g_cutscene_table = NULL;
	g_cutscene_count = 0;
}

static void check_quit(void)
{
	fresh();
	frame_due();
	g_screen_return = 2;
	xvt_frontend_task_update();
	XVT_ASSERT_INT_EQ(g_screen_calls, 1);
	XVT_ASSERT_INT_EQ(xvt_frontend_task_should_quit(), 1);
	/* Nothing runs once the quit is set. */
	advance_ms(FRAME_MS * 5);
	xvt_frontend_task_update();
	XVT_ASSERT_INT_EQ(g_screen_calls, 1);
	XVT_ASSERT_INT_EQ(xvt_frontend_task_should_quit(), 1);
}

int main(void)
{
	xvt_time_reset();
	check_run_frame_without_update();
	check_run_frame();
	check_switch_runs_captured_exit();
	check_pending_push();
	check_dialog_holds_frame();
	check_continuation_replaces_update();
	check_network_task_holds_frame();
	check_tick_pacing();
	check_tick_presents();
	check_tick_runs_only_the_dialog();
	check_wake_delay_takes_cd_sooner();
	check_service_frame_systems();
	check_shutdown_before_init();
	check_quit();
	xvt_dialog_shutdown();
	xvt_network_task_shutdown();
	xvt_network_session_shutdown();
	xvt_test_close_display();
	return 0;
}
