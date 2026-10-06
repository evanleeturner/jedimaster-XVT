#include "xvt/frontend/frontend_cursor.h"

#include <string.h>

#include "aeron/aeron.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/presentation.h"
#include "xvt_runtime/snapshot/render_frontend.h"

/* The built-in cursor, a 10 by 10 arrow pointing up and left, one byte per
 * pixel, row by row: 0 is transparent, 1 the outline and 0xFF the fill.
 * frontend_cursor_draw draws 1 as 31 (blue) and 0xFF as 0xFFFF (white) at 16
 * bits per pixel, and the byte as the palette index at 8. frontend_cursor_init
 * copies it into g_front_state.cursor_default_mask; the modern build's renderer
 * also reads it. */
// GLOBAL: XVT 0x52C100
const uint8_t g_default_cursor_bitmap[100] = {
	1,    1,    1,	  1,	1,    1,    1,	  1,	1,    0,    1,	  0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 1,    0,	  0,	1,    0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 1,	  0,	0,    0,    1,	  0xFF, 0xFF, 0xFF, 0xFF, 1,
	0,    0,    0,	  0,	1,    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 1,	  0,
	0,    0,    1,	  0xFF, 0xFF, 1,    0xFF, 0xFF, 0xFF, 1,    0,	  0,
	1,    0xFF, 1,	  0,	1,    0xFF, 0xFF, 0xFF, 1,    0,    1,	  1,
	0,    0,    0,	  1,	0xFF, 0xFF, 0xFF, 1,	1,    0,    0,	  0,
	0,    0,    1,	  0xFF, 0xFF, 1,    0,	  0,	0,    0,    0,	  0,
	0,    1,    1,	  0,
};

/* Makes the registered image resource_name the frontend cursor, with save_buf as
 * the buffer for the pixels under it: points g_front_state.cursor_mask_pixels at
 * the image's pixels, sets g_front_state.cursor_save_buf, sets cursor_width and
 * cursor_height to the image's width and height plus 1 (the right and bottom of
 * front_image_get_resource_rect's rect, plus 1) and copies the name into
 * cursor_sprite_name, after which frontend_cursor_draw draws it with
 * front_image_draw_sprite. Returns 0, changing nothing, when no resource has the
 * name or its image is RLE-compressed. On success the modern build returns 0;
 * the original build's function ends without a return statement there. Does not
 * check save_buf's size or the name's length (63 characters fit). */
// FUNCTION: XVT 0x4B49A0
int frontend_cursor_set_image_from_resource_name(const char *resource_name,
						 void *save_buf)
{
	int resource_index = front_image_find_resource_by_name(resource_name);
	if (resource_index == -1) {
		XVT_LOG_WARN(
			"ui.cursor_image_refused image=\"%s\" compressed=%d",
			resource_name != NULL ? resource_name : "", 0);
		return 0;
	}
	if (g_front_state.resource_table[resource_index].image->is_compressed !=
	    0) {
		XVT_LOG_WARN(
			"ui.cursor_image_refused image=\"%s\" compressed=%d",
			resource_name, 1);
		return 0;
	}
	struct RECT resource_rect;
	front_image_get_resource_rect(resource_name, &resource_rect);
	g_front_state.cursor_mask_pixels =
		g_front_state.resource_table[resource_index].image->pixels;
	g_front_state.cursor_save_buf = (uint8_t *)save_buf;
	g_front_state.cursor_width =
		resource_rect.right - resource_rect.left + 1;
	g_front_state.cursor_height =
		resource_rect.bottom - resource_rect.top + 1;
	strcpy(g_front_state.cursor_sprite_name, resource_name);
	XVT_LOG_DEBUG("ui.cursor_image_set image=\"%s\" width=%d height=%d",
		      resource_name, g_front_state.cursor_width,
		      g_front_state.cursor_height);
	return 0;
}

/* Sets the frontend cursor to the built-in 10 by 10 arrow: copies
 * g_default_cursor_bitmap into g_front_state.cursor_default_mask, points
 * cursor_mask_pixels and cursor_save_buf at the state's default mask and save
 * buffer, sets cursor_width and cursor_height to 10 and clears
 * cursor_sprite_name. */
// FUNCTION: XVT 0x4DDC40
void frontend_cursor_init(void)
{
	g_front_state.cursor_width = 10;
	g_front_state.cursor_height = 10;
	g_front_state.cursor_mask_pixels = g_front_state.cursor_default_mask;
	g_front_state.cursor_save_buf = g_front_state.cursor_default_save_buf;
	memcpy(g_front_state.cursor_default_mask, g_default_cursor_bitmap,
	       sizeof(g_front_state.cursor_default_mask));
	memset(g_front_state.cursor_sprite_name, 0,
	       sizeof(g_front_state.cursor_sprite_name));
}

/* Draws the frontend cursor on the back buffer at g_front_state.mouse_x and
 * mouse_y, first copying the pixels it covers into g_front_state.cursor_save_buf.
 * With a cursor_sprite_name set it draws that image with front_image_draw_sprite.
 * Else it draws the mask at cursor_mask_pixels, one byte per pixel, where 0 is
 * transparent: at 16 bits per pixel 1 is drawn as 31 and 0xFF as 0xFFFF and
 * other values not at all, at 8 bits each nonzero byte is drawn as the palette
 * index. The save and the mask clip only their right and bottom edges, to the
 * clip bounds. Draws and saves nothing when the cursor position is outside 0 to
 * 639 by 0 to 479. Locks the back buffer into g_draw_surface_ptr and unlocks it
 * at the end, leaving the pointer set. Records the position and the visible
 * size in g_front_state.cursor_prev_draw_x, cursor_prev_draw_y, cursor_prev_draw_width
 * and cursor_prev_draw_height. The modern build also marks the cursor for its
 * renderer. */
// FUNCTION: XVT 0x4DDC90
void frontend_cursor_draw(void)
{
	if (g_front_state.mouse_x < 0 || g_front_state.mouse_x >= 640 ||
	    g_front_state.mouse_y < 0 || g_front_state.mouse_y >= 480) {
		return;
	}

	int cursor_height = g_front_state.cursor_height;
	struct RECT clipped_rect;
	clipped_rect.left = 0;
	int cursor_width = g_front_state.cursor_width;
	clipped_rect.top = 0;
	clipped_rect.right = g_front_state.cursor_width - 1;
	clipped_rect.bottom = g_front_state.cursor_height - 1;
	frontend_draw_rect_offset_xy(&clipped_rect, g_front_state.mouse_x,
				     g_front_state.mouse_y);
	struct RECT original_rect;
	frontend_draw_rect_copy(&original_rect, &clipped_rect);
	frontend_draw_rect_clip_to_bounds(&clipped_rect);
	int visible_width =
		clipped_rect.right - original_rect.right + cursor_width;
	int visible_height =
		cursor_height + clipped_rect.bottom - original_rect.bottom;
	uint8_t *back_buffer = frontend_display_lock_back_buffer();
	uint8_t *cursor_pixels = g_front_state.cursor_mask_pixels;
	uint8_t *save_buffer = g_front_state.cursor_save_buf;
	int display_bpp = g_front_state.display_bpp;
	g_draw_surface_ptr = back_buffer;
	if (back_buffer == NULL) {
		XVT_LOG_ERROR("ui.cursor_lock_failed x=%d y=%d bpp=%d",
			      g_front_state.mouse_x, g_front_state.mouse_y,
			      display_bpp);
	}

	xvt_render_frontend_cursor(0);
	uint8_t *back_buffer_row;
	int rows_remaining;
	int mask_value;
	if (g_front_state.cursor_sprite_name[0] != '\0') {
		switch (display_bpp) {
		case 8: {
			back_buffer_row =
				&back_buffer
					[g_front_state.mouse_x +
					 g_front_state.mouse_y *
						 g_front_state
							 .draw_surface_pitch];
			if (visible_height > 0) {
				rows_remaining = visible_height;
				do {
					memcpy(save_buffer, back_buffer_row,
					       visible_width);
					save_buffer += cursor_width;
					back_buffer_row +=
						g_front_state
							.draw_surface_pitch;
					--rows_remaining;
				} while (rows_remaining != 0);
			}
			break;
		}
		case 16: {
			back_buffer_row =
				&back_buffer
					[2 * g_front_state.mouse_x +
					 g_front_state.mouse_y *
						 g_front_state
							 .draw_surface_pitch];
			if (visible_height > 0) {
				rows_remaining = visible_height;
				do {
					if (visible_width > 0) {
						uint16_t *source_pixel =
							(uint16_t *)
								back_buffer_row;
						uint16_t *saved_pixel =
							(uint16_t *)save_buffer;
						int pixels_remaining =
							visible_width;
						do {
							*saved_pixel++ =
								*source_pixel++;
							--pixels_remaining;
						} while (pixels_remaining != 0);
					}
					save_buffer += 2 * cursor_width;
					back_buffer_row +=
						g_front_state
							.draw_surface_pitch &
						~1;
					--rows_remaining;
				} while (rows_remaining != 0);
			}
			break;
		}
		default:
			break;
		}
		front_image_draw_sprite(g_front_state.cursor_sprite_name,
					g_front_state.mouse_x,
					g_front_state.mouse_y);
	} else {
		int column;
		switch (display_bpp) {
		case 8: {
			back_buffer_row =
				&back_buffer
					[g_front_state.mouse_x +
					 g_front_state.mouse_y *
						 g_front_state
							 .draw_surface_pitch];
			if (visible_height > 0) {
				rows_remaining = visible_height;
				do {
					memcpy(save_buffer, back_buffer_row,
					       visible_width);
					for (column = 0; column < visible_width;
					     ++column) {
						if (cursor_pixels[column] !=
						    0) {
							back_buffer_row[column] =
								cursor_pixels
									[column];
						}
					}
					save_buffer += cursor_width;
					cursor_pixels += cursor_width;
					back_buffer_row +=
						g_front_state
							.draw_surface_pitch;
					--rows_remaining;
				} while (rows_remaining != 0);
			}
			break;
		}
		case 16: {
			back_buffer_row =
				&back_buffer
					[2 * g_front_state.mouse_x +
					 g_front_state.mouse_y *
						 g_front_state
							 .draw_surface_pitch];
			if (visible_height > 0) {
				rows_remaining = visible_height;
				do {
					column = 0;
					if (visible_width > 0) {
						uint16_t *destination_pixel =
							(uint16_t *)
								back_buffer_row;
						uint16_t *saved_pixel =
							(uint16_t *)save_buffer;
						do {
							*saved_pixel =
								*destination_pixel;
							mask_value =
								cursor_pixels
									[column];
							switch (mask_value) {
							case 1:
								*destination_pixel =
									31;
								break;
							case 0xFF:
								*destination_pixel =
									0xFFFF;
								break;
							default:
								break;
							}
							++destination_pixel;
							++saved_pixel;
							++column;
						} while (column <
							 visible_width);
					}
					cursor_pixels += cursor_width;
					save_buffer += 2 * cursor_width;
					back_buffer_row +=
						g_front_state
							.draw_surface_pitch &
						~1;
					--rows_remaining;
				} while (rows_remaining != 0);
			}
			break;
		}
		default:
			break;
		}
	}

	xvt_render_frontend_end_cursor();
	frontend_display_unlock_back_buffer();
	g_front_state.cursor_prev_draw_x = g_front_state.mouse_x;
	g_front_state.cursor_prev_draw_y = g_front_state.mouse_y;
	g_front_state.cursor_prev_draw_width = visible_width;
	g_front_state.cursor_prev_draw_height = visible_height;
}

/* Copies g_front_state.mouse_x and mouse_y to *outX and *outY. Returns outX. */
// FUNCTION: XVT 0x4DE090
int *frontend_cursor_get_pos(int *out_x, int *out_y)
{
	*out_x = g_front_state.mouse_x;
	*out_y = g_front_state.mouse_y;
	return out_x;
}

/* Moves the cursor: clamps x to 0 to 640 and y to 0 to 480, stores them in
 * g_front_state.mouse_x and mouse_y and moves the system cursor there. Returns
 * SetCursorPos's result in the original build and xvt_presentation_warp_classic's
 * in the modern build. */
// FUNCTION: XVT 0x4DE0B0
int frontend_cursor_set_pos(int x, int y)
{
	if (x > 640) {
		x = 640;
	} else if (x < 0) {
		x = 0;
	}

	if (y > 480) {
		y = 480;
	} else if (y < 0) {
		y = 0;
	}

	g_front_state.mouse_x = x;
	g_front_state.mouse_y = y;
	return xvt_presentation_warp_classic(x, y);
}

/* Sets g_front_state.cursor_visible to 1: the frame loop draws the cursor after
 * each update. */
// FUNCTION: XVT 0x4DE100
void frontend_cursor_show(void) { g_front_state.cursor_visible = 1; }

/* Sets g_front_state.cursor_visible to 0: the frame loop stops drawing the
 * cursor. */
// FUNCTION: XVT 0x4DE110
void frontend_cursor_hide(void) { g_front_state.cursor_visible = 0; }

/* Copies g_front_state.cursor_width and cursor_height to *out_width and *out_height.
 * Returns 1. */
// FUNCTION: XVT 0x4DE130
int frontend_cursor_get_dimensions(int *out_width, int *out_height)
{
	*out_width = g_front_state.cursor_width;
	*out_height = g_front_state.cursor_height;
	return 1;
}

/* Hides the system's own mouse cursor. The original build calls ShowCursor(0)
 * until the display count is under 0 and returns 1; the modern build hides the
 * host cursor and returns Aeron_SetHostCursorVisible's result, 1 on success. */
// FUNCTION: XVT 0x4DE150
int frontend_cursor_hide_os_cursor(void)
{
	return Aeron_SetHostCursorVisible(0);
}
