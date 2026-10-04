#include "xvt_runtime/runtime/dialog_task.h"

#include <stdio.h>
#include <string.h>

#include "aeron/aeron.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_dialog.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/input/keyboard.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/frontend_task.h"
#include "xvt_runtime/runtime/presentation.h"
#include "xvt_runtime/storage/storage.h"

static struct {
	frontend_screen_update_fn update;
	struct RECT rect;
	int active;
	int pushed;
	int complete;
	int result;
	int parent_overlay_text_enabled;
	int cursor_x;
	int cursor_y;
	int cursor_visible;
	int parent_callbacks_dirty;
	int parent_frame;
	int parent_offscreen_restore;
	int is_pilot_name_prompt;
	xvt_dialog_continuation continuation;
	int context;
} g_dialog;

int xvt_dialog_continue_with(xvt_dialog_continuation continuation, int context)
{
	g_dialog.continuation = continuation;
	g_dialog.context = context;
	return 0;
}

int xvt_dialog_resume_continuation(int *frame_result)
{
	xvt_dialog_continuation continuation;
	int result;
	int context;
	if (!g_dialog.complete || !g_dialog.continuation) {
		return 0;
	}
	continuation = g_dialog.continuation;
	context = g_dialog.context;
	g_dialog.continuation = NULL;
	xvt_dialog_take_result(&result);
	*frame_result = continuation(result, context);
	return 1;
}

int xvt_dialog_begin(frontend_screen_update_fn update, const struct RECT *rect)
{
	if (g_dialog.active || g_dialog.complete) {
		return XVT_DIALOG_PENDING;
	}
	xvt_presentation_require_classic();
	g_dialog.update = update;
	g_dialog.rect = rect ? *rect : (struct RECT){0, 0, 639, 479};
	g_dialog.parent_overlay_text_enabled =
		frontend_button_is_overlay_text_enabled();
	g_dialog.cursor_x = g_front_state.mouse_x;
	g_dialog.cursor_y = g_front_state.mouse_y;
	g_dialog.cursor_visible = g_front_state.cursor_visible;
	g_dialog.parent_callbacks_dirty = g_front_state.screen_callbacks_dirty;
	g_dialog.parent_frame = g_front_state.frame_counter;
	g_dialog.parent_offscreen_restore =
		g_front_state.offscreen_restore_enabled;
	g_dialog.active = 1;
	xvt_input_update_mouse_capture(Aeron_InputSnapshot());
	g_dialog.pushed = 0;
	frontend_button_disable_overlay_text();
	XVT_LOG_INFO("dialog.opened");
	return XVT_DIALOG_PENDING;
}

static void xvt_dialog_end(void)
{
	frontend_display_unlock_back_buffer();
	if (g_dialog.pushed) {
		frontend_screen_pop_state();
	}
	g_front_state.frame_counter = g_dialog.parent_frame;
	g_front_state.screen_callbacks_dirty = g_dialog.parent_callbacks_dirty;
	g_front_state.offscreen_restore_enabled =
		g_dialog.parent_offscreen_restore;
	g_front_state.cursor_visible = g_dialog.cursor_visible;
	frontend_cursor_set_pos(g_dialog.cursor_x, g_dialog.cursor_y);
	frontend_text_stop_text_fade();
	if (g_dialog.parent_overlay_text_enabled) {
		frontend_button_enable_overlay_text();
	} else {
		frontend_button_disable_overlay_text();
	}
	keyboard_flush_char_buffer();
	g_front_state.mouse_left_click_latch =
		g_front_state.mouse_right_click_latch = 0;
	g_dialog.active = 0;
	g_dialog.pushed = 0;
	g_dialog.complete = 1;
	XVT_LOG_INFO("dialog.closed result=%d", g_dialog.result);
}

void xvt_dialog_update(void)
{
	int result;
	if (!g_dialog.active) {
		return;
	}
	if (!g_dialog.pushed) {
		if (!frontend_screen_push_state(g_dialog.update,
						&g_dialog.rect)) {
			xvt_storage_fatal("Cannot allocate dialog screen", 1);
			g_dialog.result = 0;
			xvt_dialog_end();
			return;
		}
		g_dialog.pushed = 1;
		g_front_state.screen_states[g_front_state.screen_stack_top]
			.exit_fn = NULL;
		g_front_state.frame_counter = 0;
		g_front_state.screen_callbacks_dirty = 0;
		g_dialog_result = 0;
		keyboard_flush_char_buffer();
		g_front_state.mouse_left_click_latch =
			g_front_state.mouse_right_click_latch = 0;
	}
	if (g_front_state.frame_counter > 0 && keyboard_peek_char() == 27) {
		g_dialog.result = 0;
		if (g_dialog.is_pilot_name_prompt) {
			g_front_dialog_line1_or_edit[0] = 0;
			front_image_free_resource_by_name("backname");
		}
		xvt_dialog_end();
		return;
	}
	result = xvt_frontend_task_run_frame();
	if (result == 1) {
		g_dialog.result = g_dialog.is_pilot_name_prompt
					  ? g_front_dialog_line1_or_edit[0] != 0
					  : g_dialog_result;
		xvt_dialog_end();
	}
}

int xvt_dialog_is_active(void) { return g_dialog.active; }

int xvt_dialog_is_text_prompt(void)
{
	return g_dialog.active && g_dialog.is_pilot_name_prompt;
}

int xvt_dialog_has_result(void) { return g_dialog.complete; }

int xvt_dialog_take_result(int *result)
{
	if (!g_dialog.complete) {
		return 0;
	}
	*result = g_dialog.result;
	g_dialog.complete = 0;
	return 1;
}

int xvt_dialog_confirm(const char *line1, const char *line2, const char *line3,
		       const char *okay_label, const char *cancel_label,
		       int network)
{
	int result;
	if (xvt_dialog_take_result(&result)) {
		return result;
	}
	if (g_dialog.active) {
		return XVT_DIALOG_PENDING;
	}
	snprintf(g_front_dialog_line1_or_edit,
		 sizeof(g_front_dialog_line1_or_edit), "%s",
		 line1 ? line1 : "");
	snprintf(g_front_dialog_line2, sizeof(g_front_dialog_line2), "%s",
		 line2 ? line2 : "");
	snprintf(g_front_dialog_line3, sizeof(g_front_dialog_line3), "%s",
		 line3 ? line3 : "");
	snprintf(g_front_dialog_okay_label, sizeof(g_front_dialog_okay_label),
		 "%s", okay_label ? okay_label : "");
	snprintf(g_front_dialog_cancel_label,
		 sizeof(g_front_dialog_cancel_label), "%s",
		 cancel_label ? cancel_label : "");
	frontend_cursor_get_pos(&g_front_dialog_saved_mouse_x,
				&g_front_dialog_saved_mouse_y);
	if (g_game_config.sfx_datapad_enabled) {
		frontend_sound_play_ui_sound(
			"warningsound", 1, 0, 255,
			12 * g_game_config.sfx_datapad_volume, 63);
	}
	g_dialog.is_pilot_name_prompt = 0;
	return xvt_dialog_begin(
		network ? frontend_dialog_network_abort_error_callback
			: frontend_dialog_confirm_update_callback,
		NULL);
}

int xvt_dialog_pilot_name(char *name)
{
	int result;
	if (xvt_dialog_take_result(&result)) {
		memcpy(name, g_front_dialog_line1_or_edit, 12);
		name[12] = 0;
		return result;
	}
	if (!g_dialog.active) {
		memset(g_front_dialog_line1_or_edit, 0,
		       sizeof(g_front_dialog_line1_or_edit));
		g_dialog.is_pilot_name_prompt = 1;
		xvt_dialog_begin(frontend_dialog_create_pilot_name_callback,
				 NULL);
	}
	return XVT_DIALOG_PENDING;
}

void xvt_dialog_shutdown(void)
{
	if (g_dialog.active) {
		if (g_dialog.is_pilot_name_prompt) {
			front_image_free_resource_by_name("backname");
		}
		xvt_dialog_end();
	}
	memset(&g_dialog, 0, sizeof(g_dialog));
}
