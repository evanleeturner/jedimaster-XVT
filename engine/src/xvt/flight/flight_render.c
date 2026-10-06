#include "xvt/flight/flight_render.h"

#include "xvt/flight/hud/flight_text.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/log/log.h"

/* Does nothing. flight_render_install_callbacks makes it the transition hook
 * (g_flight_render_transition_hook) in every mode. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40E580
void flight_render_transition_hook_stub(void) {}

/* Calls g_flight_render_transition_hook; the argument is ignored. */
// FUNCTION: XVT 0x4A9B60
void flight_render_invoke_transition_hook(int transition_flags)
{
	(void)transition_flags;
	g_flight_render_transition_hook();
}

/* Calls g_flight_reset_palette_fn; the argument is ignored. */
// FUNCTION: XVT 0x4A9B70
void flight_render_reset_palette(int transition_flags)
{
	(void)transition_flags;
	g_flight_reset_palette_fn();
}

/* Picks the software drawing mode for the resolution and installs its
 * drawing functions: g_flight_pixel_mode is 0 at 320x240 or an unknown mode,
 * 1 at 640x480 and 480x360, and 2 at 640x480 in 16 bits or whenever
 * g_flight_bytes_per_pixel is 2. Also sets g_flight_viewport_mode to 1 and
 * g_flight_graphics_detail_preset to initial_graphics_detail_preset; both callers,
 * at flight start, pass 3. */
// FUNCTION: XVT 0x411AF0
void flight_render_configure_callbacks_for_resolution(
	uint8_t initial_graphics_detail_preset)
{
	uint8_t pixel_mode;

	switch (g_flight_resolution_mode) {
	case FLIGHT_RESOLUTION_320X240:
		pixel_mode = 0;
		break;
	case FLIGHT_RESOLUTION_640X480:
	case FLIGHT_RESOLUTION_480X360:
		pixel_mode = 1;
		break;
	case FLIGHT_RESOLUTION_640X480_16BPP:
		pixel_mode = 2;
		break;
	default:
		pixel_mode = 0;
		break;
	}
	g_flight_pixel_mode = pixel_mode;
	if (g_flight_bytes_per_pixel == 2) {
		pixel_mode = 2;
	}
	g_flight_pixel_mode = pixel_mode;
	g_flight_viewport_mode = 1;
	flight_render_install_callbacks(pixel_mode);
	g_flight_graphics_detail_preset = initial_graphics_detail_preset;
	XVT_LOG_DEBUG(
		"display.drawing_mode mode=%#x pixel_mode=%d bpp=%d preset=%d",
		(unsigned)g_flight_resolution_mode, (int)g_flight_pixel_mode,
		8 * g_flight_bytes_per_pixel,
		(int)g_flight_graphics_detail_preset);
}

/* Sets the 20 software drawing function pointers, g_flight_init_line_buffer_fn
 * to g_flight_draw_line_fn: the 8-bit drawing functions for pixel_mode 0 or 1,
 * the same set for both, or the 16-bit ones for 2. The palette functions,
 * the transition hook and g_flight_init_line_buffer_fn are the same in all
 * three. Any other pixel_mode changes nothing. */
// FUNCTION: XVT 0x411B60
void flight_render_install_callbacks(int pixel_mode)
{
	switch (pixel_mode) {
	case 1: {
		g_flight_init_line_buffer_fn = flight_sw_init_framebuffer;
		g_flight_render_transition_hook =
			flight_render_transition_hook_stub;
		g_flight_reset_palette_fn = flight_palette_apply_to_display;
		g_flight_set_palette_range_fn = flight_palette_set_range;
		g_flight_get_palette_fn = flight_palette_get_full;
		g_flight_set_palette_fn = flight_palette_set_full;
		g_flight_compute_pixel_offset_fn =
			flight_sw_compute_pixel_offset8bpp;
		g_flight_blit_sprite_fn = flight_sw_blit_sprite_rle8bpp;
		g_flight_blit_sprite_faded_fn =
			flight_sw_blit_sprite_rle_faded8bpp;
		g_flight_draw_char_fn = flight_text_draw_wide_glyph8bpp;
		g_flight_fill_clip_rect_fn = flight_sw_fill_clip_rect8bpp;
		g_flight_fill_rect_clipped_fn = flight_sw_fill_rect_clipped8bpp;
		g_flight_save_screen_rect_fn =
			(flight_screen_rect_fn)flight_sw_save_screen_rect8bpp;
		g_flight_restore_screen_rect_fn = (flight_screen_rect_fn)
			flight_sw_restore_screen_rect8bpp;
		g_flight_draw_point_array_fn = flight_sw_draw_point_array8bpp;
		g_flight_draw_point_array_masked_fn =
			flight_sw_erase_point_array8bpp;
		g_flight_draw_pixel_fn = flight_sw_draw_pixel8bpp;
		g_flight_draw_radar_target_marker_fn =
			flight_sw_draw_radar_target_marker8bpp;
		g_flight_restore_radar_target_marker_fn =
			flight_sw_restore_radar_target_marker8bpp;
		g_flight_draw_line_fn = flight_sw_draw_line8bpp;
		flight_render_set_pixel_mode_stub(pixel_mode);
		break;
	}
	case 2: {
		g_flight_init_line_buffer_fn = flight_sw_init_framebuffer;
		g_flight_render_transition_hook =
			flight_render_transition_hook_stub;
		g_flight_reset_palette_fn = flight_palette_apply_to_display;
		g_flight_set_palette_range_fn = flight_palette_set_range;
		g_flight_get_palette_fn = flight_palette_get_full;
		g_flight_set_palette_fn = flight_palette_set_full;
		g_flight_compute_pixel_offset_fn =
			flight_sw_compute_pixel_offset;
		g_flight_blit_sprite_fn = flight_sw_blit_sprite_rle16bpp;
		g_flight_blit_sprite_faded_fn =
			flight_sw_blit_sprite_rle_faded16bpp;
		g_flight_draw_char_fn = flight_text_draw_wide_glyph;
		g_flight_fill_clip_rect_fn = flight_sw_fill_clip_rect16bpp;
		g_flight_fill_rect_clipped_fn =
			flight_sw_fill_rect_clipped16bpp;
		g_flight_save_screen_rect_fn =
			(flight_screen_rect_fn)flight_sw_save_screen_rect16bpp;
		g_flight_restore_screen_rect_fn = (flight_screen_rect_fn)
			flight_sw_restore_screen_rect16bpp;
		g_flight_draw_point_array_fn = flight_sw_draw_point_array16bpp;
		g_flight_draw_point_array_masked_fn =
			flight_sw_erase_point_array16bpp;
		g_flight_draw_pixel_fn = flight_sw_draw_pixel16bpp;
		g_flight_draw_radar_target_marker_fn =
			flight_sw_draw_radar_target_marker16bpp;
		g_flight_restore_radar_target_marker_fn =
			flight_sw_restore_radar_target_marker16bpp;
		g_flight_draw_line_fn = flight_sw_draw_line16bpp;
		flight_render_set_pixel_mode_stub(pixel_mode);
		break;
	}
	default:
		flight_render_set_pixel_mode_stub(pixel_mode);
		break;
	case 0: {
		g_flight_init_line_buffer_fn = flight_sw_init_framebuffer;
		g_flight_render_transition_hook =
			flight_render_transition_hook_stub;
		g_flight_reset_palette_fn = flight_palette_apply_to_display;
		g_flight_set_palette_range_fn = flight_palette_set_range;
		g_flight_get_palette_fn = flight_palette_get_full;
		g_flight_set_palette_fn = flight_palette_set_full;
		g_flight_compute_pixel_offset_fn =
			flight_sw_compute_pixel_offset8bpp;
		g_flight_blit_sprite_fn = flight_sw_blit_sprite_rle8bpp;
		g_flight_blit_sprite_faded_fn =
			flight_sw_blit_sprite_rle_faded8bpp;
		g_flight_draw_char_fn = flight_text_draw_wide_glyph8bpp;
		g_flight_fill_clip_rect_fn = flight_sw_fill_clip_rect8bpp;
		g_flight_fill_rect_clipped_fn = flight_sw_fill_rect_clipped8bpp;
		g_flight_save_screen_rect_fn =
			(flight_screen_rect_fn)flight_sw_save_screen_rect8bpp;
		g_flight_restore_screen_rect_fn = (flight_screen_rect_fn)
			flight_sw_restore_screen_rect8bpp;
		g_flight_draw_point_array_fn = flight_sw_draw_point_array8bpp;
		g_flight_draw_point_array_masked_fn =
			flight_sw_erase_point_array8bpp;
		g_flight_draw_pixel_fn = flight_sw_draw_pixel8bpp;
		g_flight_draw_radar_target_marker_fn =
			flight_sw_draw_radar_target_marker8bpp;
		g_flight_restore_radar_target_marker_fn =
			flight_sw_restore_radar_target_marker8bpp;
		g_flight_draw_line_fn = flight_sw_draw_line8bpp;
		flight_render_set_pixel_mode_stub(pixel_mode);
		break;
	}
	}
}

/* Does nothing; flight_render_install_callbacks calls it last in every
 * mode. */
// FUNCTION: XVT 0x426C40
void flight_render_set_pixel_mode_stub(int pixel_mode) { (void)pixel_mode; }
