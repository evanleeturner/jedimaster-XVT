#include "xvt/frontend/frontend_draw.h"

#include <stdlib.h>
#include <string.h>

#include "xvt/frontend/frontend_state.h"
#include "xvt_runtime/snapshot/render_frontend.h"

/* The first pixel of the surface frontend drawing writes to: the back buffer's
 * locked memory, or the offscreen surface's between
 * frontend_display_lock_offscreen_surface and its unlock. Rows are
 * g_front_state.draw_surface_pitch bytes apart. Many functions write it, chiefly
 * the callers of frontend_display_lock_back_buffer, which store its result here,
 * and frontend_display_lock_offscreen_surface and
 * frontend_display_unlock_offscreen_surface. An unlock of the back buffer leaves
 * it pointing at memory no longer locked. */
// GLOBAL: XVT 0xAA6CFC
uint8_t *g_draw_surface_ptr;

/* Sets the four edges of *rect. */
// FUNCTION: XVT 0x4D6460
void frontend_draw_rect_assign(struct RECT *rect, int32_t left, int32_t top,
			       int32_t right, int32_t bottom)
{
	rect->left = left;
	rect->top = top;
	rect->right = right;
	rect->bottom = bottom;
}

/* Copies *src into *dst. */
// FUNCTION: XVT 0x4D6480
void frontend_draw_rect_copy(struct RECT *dst, const struct RECT *src)
{
	*dst = *src;
}

/* Moves *rect right by dx and down by dy. */
// FUNCTION: XVT 0x4D64A0
void frontend_draw_rect_offset_xy(struct RECT *rect, int dx, int dy)
{
	rect->left += dx;
	rect->right += dx;
	rect->top += dy;
	rect->bottom += dy;
}

/* Shrinks *rect by dx at the left and the right and by dy at the top and the
 * bottom; negative values grow it. */
// FUNCTION: XVT 0x4D64C0
void frontend_draw_rect_inset_xy(struct RECT *rect, int dx, int dy)
{
	rect->left += dx;
	rect->right -= dx;
	rect->top += dy;
	rect->bottom -= dy;
}

/* Clips *rect in place to the clip bounds, g_front_state.clip_min_x to clip_max_x
 * and clip_min_y to clip_max_y, all inclusive. Returns the edges it moved as bits:
 * 0x1 left, 0x2 top, 0x4 right, 0x8 bottom; 0 when none. A rect wholly past a
 * bound is left with its far edge one pixel beyond that bound (for example
 * right = clip_min_x - 1), so its inclusive width or height is 0. */
// FUNCTION: XVT 0x4D64E0
int frontend_draw_rect_clip_to_bounds(struct RECT *rect)
{
	int result = 0;
	int bound = g_front_state.clip_min_x;
	if (rect->left < bound) {
		rect->left = bound;
		if (rect->right < bound) {
			rect->right = bound - 1;
		}
		result = 1;
	}

	bound = g_front_state.clip_max_x;
	if (rect->right > bound) {
		rect->right = bound;
		if (rect->left > bound) {
			rect->left = bound + 1;
		}
		result |= 4;
	}

	bound = g_front_state.clip_min_y;
	if (rect->top < bound) {
		rect->top = bound;
		if (rect->bottom < bound) {
			rect->bottom = bound - 1;
		}
		result |= 2;
	}

	bound = g_front_state.clip_max_y;
	if (rect->bottom > bound) {
		rect->bottom = bound;
		if (rect->top > bound) {
			rect->top = bound + 1;
		}
		result |= 8;
	}

	return result;
}

/* Blends color over *src moved by dx and dy, clipped to the clip bounds, both
 * edges of each axis included. At 16 bits per pixel each pixel becomes the
 * per-channel average of itself and color, in the 555 or 565 layout
 * g_front_state.pixel_format555 names: red and green as (a + b) / 2, blue as a /
 * 2 + b / 2, each division rounded down. Despite the name, at 8 bits per pixel
 * it fills solid with the color index. Draws nothing when src->right <=
 * src->left or src->top >= src->bottom, or when the moved rect has left > 640,
 * right < 0, top > 480 or bottom < 0. Writes through g_draw_surface_ptr. The
 * modern build also records the fill for its renderer. */
// FUNCTION: XVT 0x4D6550
void frontend_draw_fill_rect_translucent(const struct RECT *src, int dx, int dy,
					 unsigned int color)
{
	if (src->right <= src->left || src->top >= src->bottom) {
		return;
	}

	struct RECT clipped_rect;
	frontend_draw_rect_copy(&clipped_rect, src);
	frontend_draw_rect_offset_xy(&clipped_rect, dx, dy);
	if (clipped_rect.left > 640 || clipped_rect.right < 0 ||
	    clipped_rect.top > 480 || clipped_rect.bottom < 0) {
		return;
	}

	frontend_draw_rect_clip_to_bounds(&clipped_rect);

	xvt_render_frontend_paint(XVT_PAINT_TRANSLUCENT, clipped_rect.left,
				  clipped_rect.top, clipped_rect.right + 1,
				  clipped_rect.bottom + 1, color);
	int bottom_end = clipped_rect.bottom + 1;
	int width = clipped_rect.right - clipped_rect.left + 1;
	int draw_surface_pitch = g_front_state.draw_surface_pitch;
	uint8_t *destination;
	int remaining_rows;
	if (g_front_state.display_bpp == 8) {
		destination =
			&g_draw_surface_ptr
				[clipped_rect.top *
					 g_front_state.draw_surface_pitch +
				 clipped_rect.left];
		if (bottom_end <= clipped_rect.top) {
			return;
		}

		remaining_rows = bottom_end - clipped_rect.top;
		do {
			memset(destination, color, (size_t)width);
			destination += draw_surface_pitch;
			--remaining_rows;
		} while (remaining_rows != 0);
		return;
	}

	if (g_front_state.display_bpp == 16) {
		destination = &g_draw_surface_ptr[2 * clipped_rect.left +
						  clipped_rect.top *
							  draw_surface_pitch];
		unsigned int color_half;
		if (g_front_state.pixel_format555 != 0) {
			color_half = ((color & 0x1f) + ((color & 0x7c00) << 6) +
				      8 * (color & 0x3e0)) >>
				     1;
			if (bottom_end <= clipped_rect.top) {
				return;
			}

			remaining_rows = bottom_end - clipped_rect.top;
			draw_surface_pitch &= 0xfffffffe;
			do {
				if (width > 0) {
					uint16_t *pixel =
						(uint16_t *)destination;
					int remaining_width = width;
					do {
						unsigned int value = *pixel;
						unsigned int blended =
							((value & 0x1f) +
							 ((value & 0x7c00)
							  << 6) +
							 8 * (value & 0x3e0)) >>
							1;
						blended += color_half;
						*pixel = (uint16_t)((blended &
								     0x1f) +
								    ((blended >>
								      6) &
								     0x7c00) +
								    ((blended >>
								      3) &
								     0x3e0));
						++pixel;
						--remaining_width;
					} while (remaining_width != 0);
				}
				destination += draw_surface_pitch;
				--remaining_rows;
			} while (remaining_rows != 0);
			return;
		}

		color_half = ((color & 0x1f) + 32 * (color & 0xf800) +
			      8 * (color & 0x7e0)) >>
			     1;
		if (bottom_end <= clipped_rect.top) {
			return;
		}

		remaining_rows = bottom_end - clipped_rect.top;
		draw_surface_pitch &= 0xfffffffe;
		do {
			if (width > 0) {
				uint16_t *pixel = (uint16_t *)destination;
				int remaining_width = width;
				do {
					unsigned int value = *pixel;
					unsigned int blended =
						((value & 0x1f) +
						 32 * (value & 0xf800) +
						 8 * (value & 0x7e0)) >>
						1;
					blended += color_half;
					*pixel = (uint16_t)((blended & 0x1f) +
							    ((blended >> 5) &
							     0xf800) +
							    ((blended >> 3) &
							     0x7e0));
					++pixel;
					--remaining_width;
				} while (remaining_width != 0);
			}
			destination += draw_surface_pitch;
			--remaining_rows;
		} while (remaining_rows != 0);
	}
}

/* Draws *rect moved by dx and dy in color: with filled 0 an outline, through
 * frontend_draw_rect_outline, else a solid fill clipped to the clip bounds, both
 * edges of each axis included. The fill draws nothing when rect->right <=
 * rect->left or rect->top >= rect->bottom, or when the moved rect has left >
 * 640, right < 0, top > 480 or bottom < 0. Writes through g_draw_surface_ptr. The
 * modern build also records the fill for its renderer. */
// FUNCTION: XVT 0x4D67E0
void frontend_draw_rect(const struct RECT *rect, int dx, int dy, int color,
			int filled)
{
	if (filled == 0) {
		frontend_draw_rect_outline(rect, dx, dy, color);
		return;
	}
	if (rect->right <= rect->left || rect->top >= rect->bottom) {
		return;
	}

	struct RECT clipped_rect;
	frontend_draw_rect_copy(&clipped_rect, rect);
	frontend_draw_rect_offset_xy(&clipped_rect, dx, dy);
	if (clipped_rect.left > 640 || clipped_rect.right < 0 ||
	    clipped_rect.top > 480 || clipped_rect.bottom < 0) {
		return;
	}

	frontend_draw_rect_clip_to_bounds(&clipped_rect);

	xvt_render_frontend_paint(XVT_PAINT_FILL, clipped_rect.left,
				  clipped_rect.top, clipped_rect.right + 1,
				  clipped_rect.bottom + 1, color);
	int bottom_end = clipped_rect.bottom + 1;
	int width = clipped_rect.right - clipped_rect.left + 1;
	int draw_surface_pitch = g_front_state.draw_surface_pitch;
	int display_bpp = g_front_state.display_bpp;
	uint8_t *destination;
	int remaining_rows;
	switch (display_bpp) {
	case 8:
		destination =
			&g_draw_surface_ptr
				[clipped_rect.left +
				 clipped_rect.top *
					 g_front_state.draw_surface_pitch];
		if (clipped_rect.top >= bottom_end) {
			break;
		}
		remaining_rows = bottom_end - clipped_rect.top;
		do {
			memset(destination, color, (size_t)width);
			destination += draw_surface_pitch;
			--remaining_rows;
		} while (remaining_rows != 0);
		break;

	case 16:
		destination = &g_draw_surface_ptr[2 * clipped_rect.left +
						  clipped_rect.top *
							  draw_surface_pitch];
		if (clipped_rect.top >= bottom_end) {
			break;
		}
		remaining_rows = bottom_end - clipped_rect.top;
		do {
			uint8_t *word_destination = destination;
			int remaining_width = width;
			if (remaining_width > 0) {
				while (remaining_width > 0) {
					*(uint16_t *)word_destination =
						(uint16_t)color;
					word_destination += 2;
					--remaining_width;
				}
			}
			destination += draw_surface_pitch;
			--remaining_rows;
		} while (remaining_rows != 0);
		break;
	}
}

/* Draws a one-pixel outline of *rect moved by dx and dy in color, on its
 * inclusive edges, clipped to the clip bounds; an edge the clip moved is not
 * drawn. When the top edge is clipped away, the sides start on the clipped top
 * row and the bottom edge is drawn one row above the rect's bottom. Draws
 * nothing when rect->right <= rect->left or rect->top >= rect->bottom, or when
 * the moved rect has left > 640, right < 0, top > 480 or bottom < 0. Writes
 * through g_draw_surface_ptr. The modern build also records the unclipped outline
 * for its renderer. */
// FUNCTION: XVT 0x4D6950
void frontend_draw_rect_outline(const struct RECT *rect, int dx, int dy,
				int color)
{
	int draw_top = 1;
	int draw_left = 1;
	int draw_right = 1;
	int draw_bottom = 1;
	if (rect->right <= rect->left || rect->top >= rect->bottom) {
		return;
	}

	struct RECT clipped_rect;
	frontend_draw_rect_copy(&clipped_rect, rect);
	frontend_draw_rect_offset_xy(&clipped_rect, dx, dy);
	if (clipped_rect.left > 640 || clipped_rect.right < 0 ||
	    clipped_rect.top > 480 || clipped_rect.bottom < 0) {
		return;
	}

	xvt_render_frontend_paint(XVT_PAINT_FRAME, clipped_rect.left,
				  clipped_rect.top, clipped_rect.right + 1,
				  clipped_rect.bottom + 1, color);
	struct RECT unclipped_rect;
	frontend_draw_rect_copy(&unclipped_rect, &clipped_rect);
	frontend_draw_rect_clip_to_bounds(&clipped_rect);
	if (unclipped_rect.left != clipped_rect.left) {
		draw_left = 0;
	}
	if (unclipped_rect.right != clipped_rect.right) {
		draw_right = 0;
	}
	if (unclipped_rect.top != clipped_rect.top) {
		draw_top = 0;
	}
	if (unclipped_rect.bottom != clipped_rect.bottom) {
		draw_bottom = 0;
	}

	/* bottom starts as the clipped bottom edge; each case below turns it
	 * into the number of interior rows and counts it down while drawing the
	 * side edges. */
	int bottom = clipped_rect.bottom;
	int width = clipped_rect.right - clipped_rect.left + 1;
	int draw_surface_pitch = g_front_state.draw_surface_pitch;
	int display_bpp = g_front_state.display_bpp;
	int interior_top;
	switch (display_bpp) {
	case 8: {
		uint8_t *destination =
			&g_draw_surface_ptr
				[clipped_rect.left +
				 clipped_rect.top *
					 g_front_state.draw_surface_pitch];
		if (draw_top != 0) {
			memset(destination, color, (size_t)width);
			destination += draw_surface_pitch;
		}
		interior_top = clipped_rect.top + 1;
		if (bottom > interior_top) {
			bottom -= interior_top;
			do {
				if (draw_left != 0) {
					destination[0] = (uint8_t)color;
				}
				if (draw_right != 0) {
					destination[width - 1] = (uint8_t)color;
				}
				destination += draw_surface_pitch;
				--bottom;
			} while (bottom != 0);
		}
		if (draw_bottom != 0) {
			memset(destination, color, (size_t)width);
		}
		break;
	}

	case 16: {
		uint8_t *destination =
			&g_draw_surface_ptr[clipped_rect.left * 2 +
					    clipped_rect.top *
						    draw_surface_pitch];
		if (draw_top != 0) {
			for (int x = 0; x < width; ++x) {
				((uint16_t *)destination)[x] = (uint16_t)color;
			}
			destination += draw_surface_pitch;
		}
		interior_top = clipped_rect.top + 1;
		if (bottom > interior_top) {
			bottom -= interior_top;
			do {
				if (draw_left != 0) {
					*(uint16_t *)destination =
						(uint16_t)color;
				}
				if (draw_right != 0) {
					((uint16_t *)destination)[width - 1] =
						(uint16_t)color;
				}
				destination += draw_surface_pitch;
				--bottom;
			} while (bottom != 0);
		}
		if (draw_bottom != 0) {
			for (int x = 0; x < width; ++x) {
				((uint16_t *)destination)[x] = (uint16_t)color;
			}
		}
		break;
	}
	}
}

/* Returns 1 when (x, y) lies in *rect, edges included, else 0. */
// FUNCTION: XVT 0x4D6B90
int frontend_draw_point_in_rect(const struct RECT *rect, int x, int y)
{
	if (rect->left > x || rect->right < x) {
		return 0;
	}
	if (rect->top > y || rect->bottom < y) {
		return 0;
	}
	return 1;
}

/* Draws an inclusive horizontal span clipped to the active frontend surface
 * bounds. The forward and reverse argument paths retain the original clipping
 * edge behavior. */
/* Draws x0 to x1 on row y in color, both ends included, clipped to the clip
 * bounds; nothing when y is outside them. Either order works, but with x0 over
 * x1 a span the clipping leaves one pixel long draws nothing. Writes through
 * g_draw_surface_ptr at 8 or 16 bits per pixel. The modern build also records the
 * line, unclipped, for its renderer. */
// FUNCTION: XVT 0x5063B0
void frontend_draw_horizontal_line_clipped(int x0, int x1, int y, int color)
{
	xvt_render_frontend_paint(XVT_PAINT_LINE, x0, y, x1, y, color);

	int pixel_shift = g_front_state.display_bpp >> 4;
	if (y > g_front_state.clip_max_y || y < g_front_state.clip_min_y) {
		return;
	}

	uint8_t *destination;
	uint16_t *word_destination;
	int count;
	if (x0 <= x1) {
		if (g_front_state.clip_min_x > x0) {
			x0 = g_front_state.clip_min_x;
			if (x1 < x0) {
				return;
			}
		}
		if (g_front_state.clip_max_x < x1) {
			x1 = g_front_state.clip_max_x;
			if (x0 > x1) {
				return;
			}
		}

		destination =
			&g_draw_surface_ptr[(x0 << pixel_shift) +
					    g_front_state.draw_surface_pitch *
						    y];
		switch (pixel_shift) {
		case 0:
			count = x1 - x0 + 1;
			memset(destination, (uint16_t)color, (size_t)count);
			return;

		case 1:
			count = x1 - x0 + 1;
			color = (uint16_t)color;
			if (count <= 0) {
				return;
			}
			word_destination = (uint16_t *)destination;
			do {
				*word_destination = (uint16_t)color;
				++word_destination;
				--count;
			} while (count != 0);
			return;
		}
		return;
	}

	if (g_front_state.clip_max_x < x0) {
		x0 = g_front_state.clip_max_x;
		if (x1 >= x0) {
			return;
		}
	}
	if (g_front_state.clip_min_x > x1) {
		x1 = g_front_state.clip_min_x;
	}
	if (x1 >= x0) {
		return;
	}

	destination = &g_draw_surface_ptr[(x1 << pixel_shift) +
					  g_front_state.draw_surface_pitch * y];
	switch (pixel_shift) {
	case 0:
		count = x0 - x1 + 1;
		memset(destination, (uint16_t)color, (size_t)count);
		return;

	case 1:
		count = x0 - x1 + 1;
		color = (uint16_t)color;
		if (count <= 0) {
			return;
		}
		word_destination = (uint16_t *)destination;
		do {
			*word_destination = (uint16_t)color;
			++word_destination;
			--count;
		} while (count != 0);
	}
}

/* Draws an inclusive vertical span clipped to the active frontend surface
 * bounds. Handles both argument orders with the draw block duplicated per
 * direction, as in the original. */
/* Draws rows y0 to y1 of column x in color, both ends included, clipped to the
 * clip bounds; nothing when x is outside them. Either order works, but with y0
 * over y1 a span the clipping leaves one pixel long draws nothing. Writes
 * through g_draw_surface_ptr at 8 or 16 bits per pixel. The modern build also
 * records the line, unclipped, for its renderer. */
// FUNCTION: XVT 0x506550
void frontend_draw_vertical_line_clipped(int y0, int y1, int x, int color)
{
	xvt_render_frontend_paint(XVT_PAINT_LINE, x, y0, x, y1, color);

	if (g_front_state.clip_min_x > x || g_front_state.clip_max_x < x) {
		return;
	}
	int shift = g_front_state.display_bpp >> 4;
	int start;
	int end;
	int y;
	uint8_t *p;
	if (y1 >= y0) {
		end = y1;
		start = y0;
		if (g_front_state.clip_max_y < end) {
			end = g_front_state.clip_max_y;
			if (end < y0) {
				return;
			}
		}
		if (g_front_state.clip_min_y > start) {
			start = g_front_state.clip_min_y;
			if (start > end) {
				return;
			}
		}
		p = &g_draw_surface_ptr[g_front_state.draw_surface_pitch *
						start +
					(x << shift)];
		if (shift != 0) {
			if (shift != 1) {
				return;
			}
			for (y = start; y <= end; ++y) {
				*(uint16_t *)p = (uint16_t)color;
				p += g_front_state.draw_surface_pitch &
				     0xFFFFFFFE;
			}
		} else {
			for (y = start; y <= end; ++y) {
				*p = (uint8_t)color;
				p += g_front_state.draw_surface_pitch;
			}
		}
	} else {
		start = y1;
		end = y0;
		if (g_front_state.clip_min_y > start) {
			start = g_front_state.clip_min_y;
			if (start >= y0) {
				return;
			}
		}
		if (g_front_state.clip_max_y < end) {
			end = g_front_state.clip_max_y;
			if (end <= start) {
				return;
			}
		}
		p = &g_draw_surface_ptr[g_front_state.draw_surface_pitch *
						start +
					(x << shift)];
		if (shift != 0) {
			if (shift != 1) {
				return;
			}
			for (y = start; y <= end; ++y) {
				*(uint16_t *)p = (uint16_t)color;
				p += g_front_state.draw_surface_pitch &
				     0xFFFFFFFE;
			}
		} else {
			for (y = start; y <= end; ++y) {
				*p = (uint8_t)color;
				p += g_front_state.draw_surface_pitch;
			}
		}
	}
}
