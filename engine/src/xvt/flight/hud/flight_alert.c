#include "xvt/flight/hud/flight_alert.h"

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/cockpit_messages.h"
#include "xvt_runtime/snapshot/render_capture.h"
#endif

#include <stdlib.h>

#include "xvt/flight/flight_display.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"

/* Pixels added to g_surface_height before halving it to place the alert box's
 * vertical center. Nothing writes it, so it stays 0 and the box sits at the
 * surface's vertical middle. */
// GLOBAL: XVT 0x5233E4
int g_flight_alert_box_vertical_offset = 0;
/* Heap copy of the screen under the alert box. Only
 * flight_alert_save_box_background writes it: it allocates it on first use and
 * frees and reallocates it when a box needs more bytes; nothing else frees
 * it. NULL until then, or after a failed allocation; while it is NULL,
 * flight_alert_draw_box and flight_alert_restore_box_background draw nothing. */
// GLOBAL: XVT 0x5236A0
void *g_flight_alert_box_saved_pixels = 0;
/* Bytes allocated for g_flight_alert_box_saved_pixels; only
 * flight_alert_save_box_background writes it. A failed reallocation leaves the
 * old size here while the pointer is NULL. */
// GLOBAL: XVT 0x5236A4
int g_flight_alert_box_saved_bytes = 0;

/* Saves the screen under the alert box that network waits draw over the
 * flight view, so flight_alert_restore_box_background can put it back. The box
 * is half as wide as the span from g_flight_viewport_inset_x to g_surface_width
 * and centered on it, five lines of font tier 0 tall, centered at half of
 * g_surface_height plus g_flight_alert_box_vertical_offset, and saved with a
 * one-pixel border. Sets font tier 0. Allocates g_flight_alert_box_saved_pixels,
 * or a larger one when the box needs more than g_flight_alert_box_saved_bytes,
 * and returns without saving when the allocation fails. Then calls
 * flight_display_flip, copies the box out of the frame buffer with
 * g_flight_draw_to_hud_layer at 0, sets g_flight_draw_to_hud_layer to 1 and calls
 * flight_display_flip again. The modern build first clears the alert it
 * records for its renderer. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x448DA0
void flight_alert_save_box_background(void)
{
	int box_x;
	int box_y;
	int box_width;
	int box_height;
	int pixel_count;

	flight_text_set_font_tier(0);
	box_width =
		(unsigned int)(g_surface_width - g_flight_viewport_inset_x) >>
		1;
	box_height = 5 * g_flight_font_line_height;
	box_x = ((unsigned int)(g_flight_viewport_inset_x + g_surface_width) >>
		 1) -
		box_width / 2 - 1;
	box_y = ((unsigned int)(g_flight_alert_box_vertical_offset +
				g_surface_height) >>
		 1) -
		box_height / 2 - 1;
	box_width += 2;
	box_height += 2;

	if (g_flight_alert_box_saved_pixels == 0) {
		pixel_count = box_width * box_height;
		g_flight_alert_box_saved_pixels =
			malloc(pixel_count * g_flight_bytes_per_pixel);
		if (g_flight_alert_box_saved_pixels == 0) {
			return;
		}
		g_flight_alert_box_saved_bytes =
			pixel_count * g_flight_bytes_per_pixel;
	} else {
		pixel_count = box_width * box_height;
		if ((unsigned int)(pixel_count * g_flight_bytes_per_pixel) >
		    (unsigned int)g_flight_alert_box_saved_bytes) {
			free(g_flight_alert_box_saved_pixels);
			g_flight_alert_box_saved_pixels =
				malloc(pixel_count * g_flight_bytes_per_pixel);
			if (g_flight_alert_box_saved_pixels == 0) {
				return;
			}
			g_flight_alert_box_saved_bytes =
				pixel_count * g_flight_bytes_per_pixel;
		}
	}

#ifdef XVT_MODERN
	xvt_cockpit_messages_begin_alert();
	xvt_render_capture_begin_overlay();
#endif
	flight_display_flip();
	g_flight_draw_to_hud_layer = 0;
	flight_surface_lock();
	g_flight_save_screen_rect_fn(g_flight_alert_box_saved_pixels,
				     (uint16_t)box_x, (uint16_t)box_y,
				     (uint16_t)box_width, (uint16_t)box_height);
	flight_surface_unlock();
	g_flight_draw_to_hud_layer = 1;
	flight_display_flip();

#ifdef XVT_MODERN
	xvt_render_capture_end_overlay();
#endif
}

/* Puts back the screen flight_alert_save_box_background saved under the alert
 * box, which removes the alert. Sets font tier 0 and computes the box again
 * from the same globals; when nothing was saved it stops there. Otherwise it
 * calls flight_display_flip, writes the saved pixels into the frame buffer with
 * g_flight_draw_to_hud_layer at 0, sets g_flight_draw_to_hud_layer to 1 and calls
 * flight_display_flip again; the modern build also ends its recorded alert.
 * Keeps the saved copy. Does not check that the box is still the size it
 * saved. */
// FUNCTION: XVT 0x448ED0
void flight_alert_restore_box_background(void)
{
	int box_x;
	int box_y;
	int box_width;
	int box_height;
	int viewport_inset_x;
	int surface_width;
	flight_screen_rect_fn restore_screen_rect;

	flight_text_set_font_tier(0);
	surface_width = g_surface_width;
	viewport_inset_x = g_flight_viewport_inset_x;
	box_width = (unsigned int)(surface_width - viewport_inset_x) >> 1;
	box_height = 5 * g_flight_font_line_height;
	box_x = ((unsigned int)(viewport_inset_x + surface_width) >> 1) -
		box_width / 2 - 1;
	box_y = ((unsigned int)(g_flight_alert_box_vertical_offset +
				g_surface_height) >>
		 1) -
		box_height / 2 - 1;
	box_width += 2;
	box_height += 2;
	if (g_flight_alert_box_saved_pixels != 0) {

#ifdef XVT_MODERN
		xvt_render_capture_begin_overlay();
#endif
		flight_display_flip();
		g_flight_draw_to_hud_layer = 0;
		flight_surface_lock();
		restore_screen_rect = g_flight_restore_screen_rect_fn;
		restore_screen_rect(g_flight_alert_box_saved_pixels,
				    (uint16_t)box_x, (uint16_t)box_y,
				    (uint16_t)box_width, (uint16_t)box_height);
#ifdef XVT_MODERN
		xvt_cockpit_messages_end_alert();
#endif
		flight_surface_unlock();
		g_flight_draw_to_hud_layer = 1;
		flight_display_flip();
#ifdef XVT_MODERN
		xvt_render_capture_end_overlay();
#endif
	}
}

/* Writes one line of text, centered, into the alert box on the frame buffer,
 * between two calls to flight_display_flip, with g_flight_draw_to_hud_layer at 0
 * while drawing and 1 after. Line N sits N lines of font tier 0 below the
 * box's top; text_row 1 starts a new alert: it fills the box and a one-pixel
 * border with color bg_color + 2, then the box with bg_color, and writes on
 * line 1. Row 0 does the same without the border. Any other row fills the box
 * from that line down with bg_color, which clears later lines, and writes
 * there. Colors pass through flight_text_set_background_color; the text is
 * palette index 0x2F with a 0x2C shadow. Leaves font tier 0,
 * g_flight_text_shadow_enabled at 1 and the text clip on the box. Returns
 * without drawing when flight_alert_save_box_background has saved nothing. The
 * modern build also records the line for its renderer. */
// FUNCTION: XVT 0x448F90
void flight_alert_draw_box(int text_row, const char *text, uint8_t bg_color)
{
	int box_x;
	int box_y;
	int box_width;
	int box_height;
	unsigned int background_color;

	flight_text_set_font_tier(0);
	box_width =
		(unsigned int)(g_surface_width - g_flight_viewport_inset_x) >>
		1;
	box_x = ((unsigned int)(g_flight_viewport_inset_x + g_surface_width) >>
		 1) -
		box_width / 2;
	box_height = 5 * g_flight_font_line_height;
	box_y = ((unsigned int)(g_flight_alert_box_vertical_offset +
				g_surface_height) >>
		 1) -
		box_height / 2;
	if (g_flight_alert_box_saved_pixels == 0) {
		return;
	}

	flight_text_set_color('/');
	background_color = bg_color;
	flight_text_set_background_color(background_color + 2);
	g_flight_text_shadow_enabled = 1;
	flight_text_set_shadow_color(',');

#ifdef XVT_MODERN
	xvt_render_capture_begin_overlay();
#endif
	flight_display_flip();
	g_flight_draw_to_hud_layer = 0;
	flight_surface_lock();

#ifdef XVT_MODERN
	xvt_cockpit_messages_begin_alert_line(text_row, box_x, box_y, box_width,
					      box_height);
#endif
	if (text_row == 1) {
		flight_text_set_clip_rect(box_x - 1, box_y - 1,
					  box_x + box_width + 1,
					  box_y + box_height + 1);
		g_flight_fill_clip_rect_fn();
		text_row = 0;
	}
	flight_text_set_background_color(background_color);
	flight_text_set_clip_rect(box_x,
				  box_y + text_row * g_flight_font_line_height,
				  box_x + box_width, box_y + box_height);
	g_flight_fill_clip_rect_fn();
	if (text_row == 0) {
		text_row = 1;
	}
	flight_text_set_cursor(box_x + g_flight_font_line_height,
			       box_y + text_row * g_flight_font_line_height);
	flight_text_draw_string_centered(text);
#ifdef XVT_MODERN
	xvt_cockpit_messages_end_alert_line();
#endif
	flight_surface_unlock();
	g_flight_draw_to_hud_layer = 1;
	flight_display_flip();

#ifdef XVT_MODERN
	xvt_render_capture_end_overlay();
#endif
}
