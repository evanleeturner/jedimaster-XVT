/* Checks the modal dialog task (xvt_runtime/runtime/dialog_task.h) against the
 * promises in its header: Begin's refusals, the parent state a dialog saves and
 * its end restores, Escape after the first frame, an update that ends the
 * dialog, the held result and who may take it, the confirm and pilot-name entry
 * points, the continuation, and Shutdown. The dialogs run as pushed screens on
 * a frontend display with no window (test_frontend_display.h); the test's own
 * update function stands in for a dialog where the game's dialog would need its
 * images and fonts. Every case starts from a cleared frontend with a
 * placeholder parent screen at frame 5, no dialog and no result.
 *
 * Not checked here: the warning sound, and the program ending when the dialog
 * screen cannot be pushed. */
#include <string.h>

#include "test_assert.h"
#include "test_frontend_display.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt_runtime/runtime/dialog_task.h"

enum { PARENT_FRAME = 5 };

static int g_update_frames;
static int g_update_last_frame;
static int g_update_ends;
static int g_update_result;
static int g_continuation_calls;
static int g_continuation_result;
static int g_continuation_context;

static int parent(int frame) { return frame; }

/* A dialog's update: counts its frames, and when told to, sets the dialog result and ends. */
static int test_update(int frame)
{
	++g_update_frames;
	g_update_last_frame = frame;
	if (g_update_ends) {
		g_dialog_result = g_update_result;
	}
	return g_update_ends;
}

static int other_update(int frame) { return frame; }

static int continuation(int result, int context)
{
	++g_continuation_calls;
	g_continuation_result = result;
	g_continuation_context = context;
	return 99;
}

static void fresh(void)
{
	xvt_dialog_shutdown();
	xvt_test_close_display();
	memset(&g_front_state, 0, sizeof g_front_state);
	xvt_test_open_display();
	g_front_state.screen_states[0].update_fn = parent;
	g_front_state.frame_counter = PARENT_FRAME;
	g_game_config.sfx_datapad_enabled = 0;
	frontend_button_disable_overlay_text();
	g_dialog_result = 0;
	g_update_frames = 0;
	g_update_last_frame = 0;
	g_update_ends = 0;
	g_update_result = 0;
	g_continuation_calls = 0;
	g_continuation_result = 0;
	g_continuation_context = 0;
}

static void queue_keys(const char *keys)
{
	for (; *keys; ++keys) {
		g_front_state.char_ring_buffer[g_front_state.char_write_idx] =
			*keys;
		g_front_state.char_write_idx =
			(g_front_state.char_write_idx + 1) % 1024;
	}
}

static frontend_screen_update_fn top_screen(void)
{
	return g_front_state.screen_states[g_front_state.screen_stack_top]
		.update_fn;
}

/* Runs the open dialog's first frame, then ends it with Escape. */
static void escape_dialog(void)
{
	xvt_dialog_update();
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);
	queue_keys("\x1b");
	xvt_dialog_update();
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
}

static void check_nothing_open(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_text_prompt(), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_has_result(), 0);
	int result = 123;
	XVT_ASSERT_INT_EQ(xvt_dialog_take_result(&result), 0);
	XVT_ASSERT_INT_EQ(result, 123);
	XVT_ASSERT_INT_EQ(xvt_dialog_continue_with(continuation, 1), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_resume_continuation(&result), 0);
	XVT_ASSERT_INT_EQ(result, 123);
	XVT_ASSERT_INT_EQ(g_continuation_calls, 0);
	/* A tick with no dialog does nothing. */
	xvt_dialog_update();
	XVT_ASSERT_INT_EQ(g_front_state.screen_stack_top, 0);
	XVT_ASSERT_INT_EQ(g_front_state.frame_counter, PARENT_FRAME);
}

static void check_begin_saves_and_end_restores(void)
{
	fresh();
	g_front_state.screen_callbacks_dirty = 1;
	g_front_state.offscreen_restore_enabled = 1;
	g_front_state.mouse_x = 100;
	g_front_state.mouse_y = 200;
	g_front_state.cursor_visible = 1;
	frontend_button_enable_overlay_text();
	XVT_ASSERT_INT_EQ(xvt_dialog_begin(test_update, NULL),
			  XVT_DIALOG_PENDING);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_text_prompt(), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_has_result(), 0);

	/* The dialog's frames change what the parent saved. The display has no
	 * offscreen surface, so offscreen restore must be off while frames
	 * run. */
	g_front_state.offscreen_restore_enabled = 0;
	g_front_state.cursor_visible = 0;
	g_front_state.mouse_x = 5;
	g_front_state.mouse_y = 5;

	xvt_dialog_update();
	XVT_ASSERT_INT_EQ(g_update_frames, 1);
	XVT_ASSERT_INT_EQ(g_update_last_frame, 0);
	XVT_ASSERT_TRUE(top_screen() == test_update);
	xvt_dialog_update();
	XVT_ASSERT_INT_EQ(g_update_frames, 2);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);

	/* An update returning 1 ends the dialog with the dialog's result. */
	g_update_ends = 1;
	g_update_result = 7;
	queue_keys("xy");
	g_front_state.mouse_left_click_latch = 1;
	g_front_state.mouse_right_click_latch = 1;
	xvt_dialog_update();
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_has_result(), 1);

	/* The parent's state is back. */
	XVT_ASSERT_INT_EQ(g_front_state.screen_stack_top, 0);
	XVT_ASSERT_TRUE(top_screen() == parent);
	XVT_ASSERT_INT_EQ(g_front_state.frame_counter, PARENT_FRAME);
	XVT_ASSERT_INT_EQ(g_front_state.screen_callbacks_dirty, 1);
	XVT_ASSERT_INT_EQ(g_front_state.offscreen_restore_enabled, 1);
	XVT_ASSERT_INT_EQ(g_front_state.cursor_visible, 1);
	XVT_ASSERT_INT_EQ(g_front_state.mouse_x, 100);
	XVT_ASSERT_INT_EQ(g_front_state.mouse_y, 200);
	XVT_ASSERT_INT_EQ(frontend_button_is_overlay_text_enabled(), 1);
	/* The keyboard is flushed and the click latches cleared. */
	XVT_ASSERT_INT_EQ(g_front_state.char_read_idx,
			  g_front_state.char_write_idx);
	XVT_ASSERT_INT_EQ(g_front_state.mouse_left_click_latch, 0);
	XVT_ASSERT_INT_EQ(g_front_state.mouse_right_click_latch, 0);

	/* The result is held until taken, once. */
	int result = 0;
	XVT_ASSERT_INT_EQ(xvt_dialog_take_result(&result), 1);
	XVT_ASSERT_INT_EQ(result, 7);
	XVT_ASSERT_INT_EQ(xvt_dialog_has_result(), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_take_result(&result), 0);
}

static void check_overlay_off_restored(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_dialog_begin(test_update, NULL),
			  XVT_DIALOG_PENDING);
	frontend_button_enable_overlay_text();
	escape_dialog();
	XVT_ASSERT_INT_EQ(frontend_button_is_overlay_text_enabled(), 0);
}

static void check_escape(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_dialog_begin(test_update, NULL),
			  XVT_DIALOG_PENDING);
	/* Escape before the first frame does not end the dialog. */
	queue_keys("\x1b");
	xvt_dialog_update();
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);
	XVT_ASSERT_INT_EQ(g_update_frames, 1);

	/* After it, Escape ends the dialog with result 0, whatever the dialog's result says. */
	g_dialog_result = 9;
	queue_keys("\x1b");
	xvt_dialog_update();
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
	int result = -5;
	XVT_ASSERT_INT_EQ(xvt_dialog_take_result(&result), 1);
	XVT_ASSERT_INT_EQ(result, 0);
	XVT_ASSERT_INT_EQ(g_front_state.screen_stack_top, 0);
	XVT_ASSERT_INT_EQ(g_front_state.frame_counter, PARENT_FRAME);
}

static void check_begin_refusals(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_dialog_begin(test_update, NULL),
			  XVT_DIALOG_PENDING);
	/* While a dialog is open another Begin does nothing. */
	XVT_ASSERT_INT_EQ(xvt_dialog_begin(other_update, NULL),
			  XVT_DIALOG_PENDING);
	xvt_dialog_update();
	XVT_ASSERT_TRUE(top_screen() == test_update);
	queue_keys("\x1b");
	xvt_dialog_update();
	XVT_ASSERT_INT_EQ(xvt_dialog_has_result(), 1);

	/* While a result is untaken, Begin does nothing either. */
	XVT_ASSERT_INT_EQ(xvt_dialog_begin(other_update, NULL),
			  XVT_DIALOG_PENDING);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_has_result(), 1);
}

static void check_confirm(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(
		xvt_dialog_confirm("one", NULL, "three", "Okay", NULL, 0),
		XVT_DIALOG_PENDING);
	XVT_ASSERT_INT_EQ(strcmp(g_front_dialog_line1_or_edit, "one"), 0);
	XVT_ASSERT_INT_EQ(strcmp(g_front_dialog_line2, ""), 0);
	XVT_ASSERT_INT_EQ(strcmp(g_front_dialog_line3, "three"), 0);
	XVT_ASSERT_INT_EQ(strcmp(g_front_dialog_okay_label, "Okay"), 0);
	XVT_ASSERT_INT_EQ(strcmp(g_front_dialog_cancel_label, ""), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_text_prompt(), 0);

	/* While it is open, Confirm returns -1 and copies nothing. */
	XVT_ASSERT_INT_EQ(xvt_dialog_confirm("other", "b", "c", "d", "e", 0),
			  XVT_DIALOG_PENDING);
	XVT_ASSERT_INT_EQ(strcmp(g_front_dialog_line1_or_edit, "one"), 0);

	xvt_dialog_update();
	XVT_ASSERT_TRUE(top_screen() ==
			frontend_dialog_confirm_update_callback);
	queue_keys("\x1b");
	xvt_dialog_update();

	/* The untaken result comes back first, and no dialog opens. */
	XVT_ASSERT_INT_EQ(xvt_dialog_confirm("two", NULL, NULL, NULL, NULL, 0),
			  0);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_has_result(), 0);

	/* With network set, the network abort dialog opens instead. */
	XVT_ASSERT_INT_EQ(
		xvt_dialog_confirm("net", NULL, NULL, NULL, "Cancel", 1),
		XVT_DIALOG_PENDING);
	XVT_ASSERT_INT_EQ(strcmp(g_front_dialog_cancel_label, "Cancel"), 0);
	xvt_dialog_update();
	XVT_ASSERT_TRUE(top_screen() ==
			frontend_dialog_network_abort_error_callback);
	queue_keys("\x1b");
	xvt_dialog_update();
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
}

static void check_confirm_returns_any_result(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_dialog_begin(test_update, NULL),
			  XVT_DIALOG_PENDING);
	xvt_dialog_update();
	g_update_ends = 1;
	g_update_result = 4;
	xvt_dialog_update();
	/* The result of a dialog Confirm did not open is returned all the same. */
	XVT_ASSERT_INT_EQ(xvt_dialog_confirm("a", "b", "c", "d", "e", 0), 4);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
}

static void check_pilot_name_typed(void)
{
	fresh();
	char name[13];
	XVT_ASSERT_INT_EQ(xvt_dialog_pilot_name(name), XVT_DIALOG_PENDING);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 1);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_text_prompt(), 1);
	XVT_ASSERT_INT_EQ(xvt_dialog_pilot_name(name), XVT_DIALOG_PENDING);
	xvt_dialog_update();
	XVT_ASSERT_TRUE(top_screen() ==
			frontend_dialog_create_pilot_name_callback);

	/* The prompt takes one typed character a frame, and Enter. */
	queue_keys("Luke\r");
	for (int frame = 0; frame < 10 && xvt_dialog_is_active(); ++frame) {
		xvt_dialog_update();
	}
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_text_prompt(), 0);
	memset(name, 'Z', sizeof name);
	XVT_ASSERT_INT_EQ(xvt_dialog_pilot_name(name), 1);
	XVT_ASSERT_INT_EQ(strcmp(name, "Luke"), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_has_result(), 0);
}

static void check_pilot_name_escaped(void)
{
	fresh();
	char name[13];
	XVT_ASSERT_INT_EQ(xvt_dialog_pilot_name(name), XVT_DIALOG_PENDING);
	escape_dialog();
	memset(name, 'Z', sizeof name);
	XVT_ASSERT_INT_EQ(xvt_dialog_pilot_name(name), 0);
	XVT_ASSERT_INT_EQ(name[0], 0);
}

static void check_pilot_name_after_confirm(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_dialog_confirm("ABCDEFGHIJKLMNOP", NULL, NULL,
					     NULL, NULL, 0),
			  XVT_DIALOG_PENDING);
	escape_dialog();
	char name[13];
	/* The confirm's first line, cut to 12 characters and terminated. */
	memset(name, 'Z', sizeof name);
	XVT_ASSERT_INT_EQ(xvt_dialog_pilot_name(name), 0);
	XVT_ASSERT_INT_EQ(strcmp(name, "ABCDEFGHIJKL"), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
}

static void check_continuation(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_dialog_begin(test_update, NULL),
			  XVT_DIALOG_PENDING);
	XVT_ASSERT_INT_EQ(xvt_dialog_continue_with(continuation, 42), 0);
	xvt_dialog_update();
	int frame_result = 0;
	XVT_ASSERT_INT_EQ(xvt_dialog_resume_continuation(&frame_result), 0);
	g_update_ends = 1;
	g_update_result = 7;
	xvt_dialog_update();
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);

	/* The continuation runs once with the result and context, and its return is the frame's. */
	XVT_ASSERT_INT_EQ(xvt_dialog_resume_continuation(&frame_result), 1);
	XVT_ASSERT_INT_EQ(g_continuation_calls, 1);
	XVT_ASSERT_INT_EQ(g_continuation_result, 7);
	XVT_ASSERT_INT_EQ(g_continuation_context, 42);
	XVT_ASSERT_INT_EQ(frame_result, 99);
	XVT_ASSERT_INT_EQ(xvt_dialog_has_result(), 0);
	frame_result = 0;
	XVT_ASSERT_INT_EQ(xvt_dialog_resume_continuation(&frame_result), 0);
	XVT_ASSERT_INT_EQ(frame_result, 0);
	XVT_ASSERT_INT_EQ(g_continuation_calls, 1);
}

static void check_shutdown_before_first_frame(void)
{
	fresh();
	g_front_state.screen_callbacks_dirty = 1;
	g_front_state.offscreen_restore_enabled = 1;
	g_front_state.cursor_visible = 1;
	XVT_ASSERT_INT_EQ(xvt_dialog_begin(test_update, NULL),
			  XVT_DIALOG_PENDING);
	/* The dialog never ran a frame, but the parent's state changed meanwhile. */
	g_front_state.frame_counter = 40;
	g_front_state.screen_callbacks_dirty = 0;
	g_front_state.offscreen_restore_enabled = 0;
	g_front_state.cursor_visible = 0;
	xvt_dialog_shutdown();
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
	XVT_ASSERT_INT_EQ(g_front_state.screen_stack_top, 0);
	XVT_ASSERT_INT_EQ(g_front_state.frame_counter, PARENT_FRAME);
	XVT_ASSERT_INT_EQ(g_front_state.screen_callbacks_dirty, 1);
	XVT_ASSERT_INT_EQ(g_front_state.offscreen_restore_enabled, 1);
	XVT_ASSERT_INT_EQ(g_front_state.cursor_visible, 1);
	XVT_ASSERT_INT_EQ(g_update_frames, 0);
}

static void check_shutdown(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(xvt_dialog_begin(test_update, NULL),
			  XVT_DIALOG_PENDING);
	xvt_dialog_continue_with(continuation, 3);
	xvt_dialog_update();
	XVT_ASSERT_INT_EQ(g_front_state.screen_stack_top, 1);

	/* Shutdown ends the open dialog, restoring its parent, and leaves no result. */
	xvt_dialog_shutdown();
	XVT_ASSERT_INT_EQ(xvt_dialog_is_active(), 0);
	XVT_ASSERT_INT_EQ(xvt_dialog_has_result(), 0);
	XVT_ASSERT_INT_EQ(g_front_state.screen_stack_top, 0);
	XVT_ASSERT_TRUE(top_screen() == parent);
	XVT_ASSERT_INT_EQ(g_front_state.frame_counter, PARENT_FRAME);

	XVT_ASSERT_INT_EQ(xvt_dialog_begin(test_update, NULL),
			  XVT_DIALOG_PENDING);
	escape_dialog();
	/* The continuation is forgotten too. */
	int frame_result = 0;
	XVT_ASSERT_INT_EQ(xvt_dialog_resume_continuation(&frame_result), 0);
	XVT_ASSERT_INT_EQ(g_continuation_calls, 0);

	/* An untaken result is forgotten. */
	XVT_ASSERT_INT_EQ(xvt_dialog_has_result(), 1);
	xvt_dialog_shutdown();
	XVT_ASSERT_INT_EQ(xvt_dialog_has_result(), 0);
}

int main(void)
{
	check_nothing_open();
	check_begin_saves_and_end_restores();
	check_overlay_off_restored();
	check_escape();
	check_begin_refusals();
	check_confirm();
	check_confirm_returns_any_result();
	check_pilot_name_typed();
	check_pilot_name_escaped();
	check_pilot_name_after_confirm();
	check_continuation();
	check_shutdown_before_first_frame();
	check_shutdown();
	xvt_dialog_shutdown();
	xvt_test_close_display();
	return 0;
}
