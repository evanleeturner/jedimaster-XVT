#include "xvt/frontend/frontend_screen.h"

#include "xvt_runtime/snapshot/render_frontend.h"
#include "xvt_runtime/runtime/dialog_task.h"

#include <stdlib.h>
#include <string.h>
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/input/keyboard.h"
#include "xvt_runtime/log/log_both_builds.h"

/* Gives the screen on top of the stack new update and exit functions. Sets
 * g_front_state.frame_counter to -1 and g_front_state.screen_callbacks_dirty to 1:
 * when an update function calls this, the frame loop then calls the exit
 * function it read before that update, the outgoing screen's, and the new
 * update function starts at frame 0 after the loop's increment. */
// FUNCTION: XVT 0x4DC330
void frontend_screen_set_callbacks(frontend_screen_update_fn update_fn,
				   frontend_screen_exit_fn exit_fn)
{
	XVT_LOG_DEBUG("frontend.screen_switched depth=%d frame=%d pending=%d",
		      g_front_state.screen_stack_top,
		      g_front_state.frame_counter,
		      g_front_state.screen_callbacks_dirty);
	struct frontend_screen_state *state =
		&g_front_state.screen_states[g_front_state.screen_stack_top];
	state->update_fn = update_fn;
	state = &g_front_state.screen_states[g_front_state.screen_stack_top];
	state->exit_fn = exit_fn;
	g_front_state.frame_counter = -1;
	g_front_state.screen_callbacks_dirty = 1;
}

/* Asks the main frame loop to push a screen once the current update and exit
 * functions return: stores update_fn in g_front_state.pending_screen_update_fn and
 * copies *screen_rect into g_front_state.pending_screen_rect. Returns 1. A second
 * call before the push replaces the first. The original build's
 * frontend_display_run_frame, which runs modal screens, never pushes it; its
 * frontend_display_run_main_loop and the modern build's frame loop do. */
// FUNCTION: XVT 0x4DC380
int frontend_screen_queue_push(int (*update_fn)(int),
			       const struct RECT *screen_rect)
{
	g_front_state.pending_screen_update_fn = update_fn;
	frontend_draw_rect_copy(&g_front_state.pending_screen_rect,
				screen_rect);
	return 1;
}

/* Runs a screen as a modal dialog over the current one. The original build
 * unlocks the back buffer, pushes the screen with frontend_screen_push_state
 * without checking its result, sets g_front_state.frame_counter to 0, clears both
 * joysticks' released-button flags and the two click latches, lowers
 * g_front_state.text_fade_frames_left by one when it is not 0, then calls
 * frontend_display_run_frame until it returns 1, ending the process with exit(0)
 * when it returns 2. It then flushes the keyboard's character buffer, pops the
 * screen, locks the back buffer into g_draw_surface_ptr, clears the click latches
 * and returns 1. The modern build instead returns xvt_dialog_begin's result,
 * XVT_DIALOG_PENDING (-1), and its frame loop runs the dialog. */
// FUNCTION: XVT 0x4DC3B0
int frontend_screen_run_modal(frontend_screen_update_fn update_fn,
			      struct RECT *screen_rect)
{
	return xvt_dialog_begin(update_fn, screen_rect);
}

/* Pushes a screen over the current one. In the top slot of
 * g_front_state.screen_states it saves g_front_state.frame_counter, the clip bounds
 * and a copy of the pixels under screen_rect; then it raises
 * g_front_state.screen_stack_top by one, gives the new top slot update_fn and sets
 * g_front_state.frame_counter to -1. Clamps *screen_rect in place to 0 to 639 by 0
 * to 479, copies it into the slot's saved_rect and makes it the clip. The pixels
 * come from the offscreen surface when g_front_state.offscreen_restore_enabled is
 * set, else from the back buffer; an 8-bit copy then goes through
 * front_image_compress_rle. With offscreen restore on, the unlock also copies the
 * offscreen surface to g_front_state.offscreen_backup_buffer, and
 * frontend_display_save_back_buffer then copies the back buffer to the offscreen
 * surface. With a NULL screen_rect it saves no pixels, a 0 by 0 saved_image, and
 * the clip becomes the whole screen. Returns 1. Returns 0 when
 * g_front_state.screen_stack_top is FRONTEND_SCREEN_MAX_STACK - 1 (9) or more,
 * and, after the frame counter and clip are saved, when the clamped rect has
 * left over right or bottom under top, or when the copy cannot be allocated.
 * Leaves the new slot's exit_fn as it was. The modern build also copies the rect
 * into its renderer's saved target for the slot. */
// FUNCTION: XVT 0x4DC450
int frontend_screen_push_state(frontend_screen_update_fn update_fn,
			       struct RECT *screen_rect)
{
	if (g_front_state.screen_stack_top >= FRONTEND_SCREEN_MAX_STACK - 1) {
		XVT_LOG_WARN("frontend.screen_stack_full depth=%d",
			     g_front_state.screen_stack_top);
		return 0;
	}

	int slot = g_front_state.screen_stack_top;
	g_front_state.screen_states[slot].saved_frame_counter =
		g_front_state.frame_counter;
	g_front_state.screen_states[slot].saved_clip_min_x =
		g_front_state.clip_min_x;
	g_front_state.screen_states[slot].saved_clip_max_x =
		g_front_state.clip_max_x;
	g_front_state.screen_states[slot].saved_clip_min_y =
		g_front_state.clip_min_y;
	g_front_state.screen_states[slot].saved_clip_max_y =
		g_front_state.clip_max_y;

	if (screen_rect != NULL) {
		if (screen_rect->left < 0) {
			screen_rect->left = 0;
		}
		if (screen_rect->top < 0) {
			screen_rect->top = 0;
		}
		if (screen_rect->right >= 640) {
			screen_rect->right = 639;
		}
		if (screen_rect->bottom >= 480) {
			screen_rect->bottom = 479;
		}
		if (screen_rect->left > screen_rect->right ||
		    screen_rect->bottom < screen_rect->top) {
			XVT_LOG_WARN(
				"frontend.screen_rect_empty left=%d top=%d right=%d bottom=%d",
				(int)screen_rect->left, (int)screen_rect->top,
				(int)screen_rect->right,
				(int)screen_rect->bottom);
			return 0;
		}

		frontend_draw_rect_copy(
			&g_front_state.screen_states[slot].saved_rect,
			screen_rect);
		int width = screen_rect->right - screen_rect->left + 1;
		int height = screen_rect->bottom - screen_rect->top + 1;
		int display_bpp = g_front_state.display_bpp;
		int was_back_buffer_locked;
		uint8_t *pixels;
		int y;
		switch (display_bpp) {
		case 8: {
			pixels = (uint8_t *)malloc(width * height);
			if (pixels == NULL) {
				XVT_LOG_ERROR(
					"frontend.screen_save_alloc_failed bytes=%d depth=%d",
					width * height,
					g_front_state.screen_stack_top);
				return 0;
			}

			was_back_buffer_locked =
				g_front_state.back_buffer_locked;
			if (g_front_state.offscreen_restore_enabled != 0) {
				frontend_display_lock_offscreen_surface();
			} else {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}

			uint8_t *destination = pixels;
			uint8_t *source =
				g_draw_surface_ptr + screen_rect->left +
				screen_rect->top *
					g_front_state.draw_surface_pitch;
			for (y = 0; y < height; ++y) {
				memcpy(destination, source, width);
				source += g_front_state.draw_surface_pitch;
				destination += width;
			}

			if (g_front_state.offscreen_restore_enabled != 0) {
				frontend_display_unlock_offscreen_surface(1);
			}
			if (was_back_buffer_locked == 0) {
				frontend_display_unlock_back_buffer();
			}
			break;
		}

		case 16: {
			pixels = (uint8_t *)malloc(2 * width * height);
			if (pixels == NULL) {
				XVT_LOG_ERROR(
					"frontend.screen_save_alloc_failed bytes=%d depth=%d",
					2 * width * height,
					g_front_state.screen_stack_top);
				return 0;
			}

			was_back_buffer_locked =
				g_front_state.back_buffer_locked;
			if (g_front_state.offscreen_restore_enabled != 0) {
				frontend_display_lock_offscreen_surface();
			} else {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}

			uint16_t *destination = (uint16_t *)pixels;
			uint16_t *source =
				(uint16_t
					 *)(g_draw_surface_ptr +
					    2 * screen_rect->left +
					    screen_rect->top *
						    g_front_state
							    .draw_surface_pitch);
			for (y = 0; y < height; ++y) {
				for (int x = 0; x < width; ++x) {
					destination[x] = source[x];
				}
				source += g_front_state.draw_surface_pitch >> 1;
				destination += width;
			}

			if (g_front_state.offscreen_restore_enabled != 0) {
				frontend_display_unlock_offscreen_surface(1);
			}
			if (was_back_buffer_locked == 0) {
				frontend_display_unlock_back_buffer();
			}
			break;
		}
		}

		xvt_render_frontend_screen(slot, 0);
		g_front_state.screen_states[slot].saved_image.width = width;
		g_front_state.screen_states[slot].saved_image.height = height;
		g_front_state.screen_states[slot].saved_image.pixels = pixels;
		display_bpp = g_front_state.display_bpp;
		switch (display_bpp) {
		case 8:
			g_front_state.screen_states[slot]
				.saved_image.pixel_data_bytes = width * height;
			g_front_state.screen_states[slot]
				.saved_image.is_compressed = 0;
			front_image_compress_rle(
				&g_front_state.screen_states[slot].saved_image);
			break;

		case 16:
			g_front_state.screen_states[slot]
				.saved_image.pixel_data_bytes =
				2 * width * height;
			g_front_state.screen_states[slot]
				.saved_image.is_compressed = 0;
			break;
		}

		if (g_front_state.offscreen_restore_enabled != 0) {
			frontend_display_save_back_buffer();
		}
		frontend_display_set_screen_clip_rect640x480(screen_rect);
	} else {
		g_front_state.screen_states[slot].saved_image.width = 0;
		g_front_state.screen_states[slot].saved_image.height = 0;
		g_front_state.screen_states[slot].saved_image.pixels = NULL;
		g_front_state.screen_states[slot].saved_image.pixel_data_bytes =
			0;
		struct RECT rect;
		frontend_draw_rect_assign(&rect, 0, 0, 639, 479);
		frontend_display_set_screen_clip_rect640x480(&rect);
	}

	++g_front_state.screen_stack_top;
	g_front_state.screen_states[g_front_state.screen_stack_top].update_fn =
		update_fn;
	g_front_state.frame_counter = -1;
	XVT_LOG_DEBUG(
		"frontend.screen_pushed depth=%d width=%d height=%d bytes=%d",
		g_front_state.screen_stack_top,
		g_front_state.screen_states[slot].saved_image.width,
		g_front_state.screen_states[slot].saved_image.height,
		g_front_state.screen_states[slot].saved_image.pixel_data_bytes);
	return 1;
}

/* Pops the top screen and puts back what frontend_screen_push_state saved under
 * it, from slot g_front_state.screen_stack_top - 1: draws the saved pixels at
 * saved_rect's top-left corner, on the offscreen surface when
 * g_front_state.offscreen_restore_enabled is set (the unlock then copies it to
 * g_front_state.offscreen_backup_buffer), else on the back buffer, and frees them.
 * The clip is the whole screen while it draws; then it restores the saved clip
 * bounds and g_front_state.frame_counter and lowers g_front_state.screen_stack_top
 * by one. Does nothing when the stack top is 0. Leaves the freed pointer and
 * the byte count in the slot. The modern build also restores its renderer's
 * copy and, with offscreen restore on, copies its offscreen target to its
 * backup target. */
// FUNCTION: XVT 0x4DC7E0
void frontend_screen_pop_state(void)
{
	if (g_front_state.screen_stack_top == 0) {
		XVT_LOG_WARN("frontend.screen_pop_empty");
		return;
	}

	int slot = g_front_state.screen_stack_top - 1;
	struct RECT rect;
	frontend_draw_rect_assign(&rect, 0, 0, 640, 480);
	frontend_display_set_screen_clip_rect640x480(&rect);
	if (g_front_state.screen_states[slot].saved_image.pixel_data_bytes >
	    0) {

		xvt_render_frontend_screen(slot, 1);
		xvt_render_frontend_suppress(1);
		int display_bpp = g_front_state.display_bpp;
		int was_back_buffer_locked;
		switch (display_bpp) {
		case 8: {
			was_back_buffer_locked =
				g_front_state.back_buffer_locked;
			if (g_front_state.offscreen_restore_enabled != 0) {
				frontend_display_lock_offscreen_surface();
			} else {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}

			front_image_blit_opaque(
				&g_front_state.screen_states[slot].saved_image,
				g_front_state.screen_states[slot]
					.saved_rect.left,
				g_front_state.screen_states[slot]
					.saved_rect.top);
			if (g_front_state.offscreen_restore_enabled != 0) {
				frontend_display_unlock_offscreen_surface(1);
			}
			if (was_back_buffer_locked == 0) {
				frontend_display_unlock_back_buffer();
			}
			break;
		}
		case 16: {
			was_back_buffer_locked =
				g_front_state.back_buffer_locked;
			if (g_front_state.offscreen_restore_enabled != 0) {
				frontend_display_lock_offscreen_surface();
			} else {
				g_draw_surface_ptr =
					frontend_display_lock_back_buffer();
			}

			int width = g_front_state.screen_states[slot]
					    .saved_image.width;
			int height = g_front_state.screen_states[slot]
					     .saved_image.height;
			uint8_t *pixels = g_front_state.screen_states[slot]
						  .saved_image.pixels;
			uint8_t *row_destination =
				&g_draw_surface_ptr
					[2 * g_front_state.screen_states[slot]
							 .saved_rect.left +
					 g_front_state.draw_surface_pitch *
						 g_front_state
							 .screen_states[slot]
							 .saved_rect.top];
			if (height > 0) {
				int row_bytes = width + width;
				do {
					if (width > 0) {
						uint8_t *source = pixels;
						uint8_t *destination =
							row_destination;
						for (int column = width;
						     column != 0; --column) {
							*(uint16_t *)
								destination =
								*(uint16_t *)
									source;
							source += 2;
							destination += 2;
						}
					}
					pixels += row_bytes;
					row_destination +=
						g_front_state
							.draw_surface_pitch &
						~1;
					--height;
				} while (height != 0);
			}
			if (g_front_state.offscreen_restore_enabled != 0) {
				frontend_display_unlock_offscreen_surface(1);
			}
			if (was_back_buffer_locked == 0) {
				frontend_display_unlock_back_buffer();
			}
			break;
		}
		}

		xvt_render_frontend_suppress(0);
		if (g_front_state.offscreen_restore_enabled) {
			xvt_render_frontend_copy(XVT_TARGET_FRONT_OFFSCREEN,
						 XVT_TARGET_FRONT_BACKUP);
		}
		xvt_render_frontend_select(XVT_TARGET_FRONT_BACK);
		free(g_front_state.screen_states[slot].saved_image.pixels);
	}

	frontend_display_set_screen_clip_rect640x480(
		(const struct RECT *)&g_front_state.screen_states[slot]
			.saved_clip_min_x);
	g_front_state.screen_stack_top = slot;
	g_front_state.frame_counter =
		g_front_state.screen_states[slot].saved_frame_counter;
	XVT_LOG_DEBUG(
		"frontend.screen_popped depth=%d restored=%d",
		g_front_state.screen_stack_top,
		g_front_state.screen_states[slot].saved_image.pixel_data_bytes >
			0);
}
