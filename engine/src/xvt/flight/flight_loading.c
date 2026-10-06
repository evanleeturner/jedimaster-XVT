#include "xvt/flight/flight_loading.h"

#include "xvt_runtime/snapshot/cockpit_messages.h"
#include "xvt_runtime/snapshot/render_capture.h"
#include "xvt/flight/flight_display.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/flight_net.h"
#include "xvt/render/renderer.h"
#include "xvt/util/time.h"

/* Calls made to flight_loading_pulse_and_draw_progress_screen since the last
 * reset; its low 7 bits are the bar's fill. Three functions write it:
 * flight_loading_reset_progress_state sets 0 at flight start,
 * flight_loading_pulse_and_draw_progress_screen adds 1 per call, and, in the
 * modern build, xvt_flight_task_update sets its low 7 bits to fill the bar. */
// GLOBAL: XVT 0x5236A8
uint32_t g_flight_loading_progress_step;
/* timeGetTime, in ms, when the loading bar was last drawn. Written only by
 * flight_loading_reset_progress_state and
 * flight_loading_pulse_and_draw_progress_screen. */
// GLOBAL: XVT 0x5236AC
uint32_t g_flight_loading_progress_last_draw_ms;

/* Sets g_flight_loading_progress_step to 0 and
 * g_flight_loading_progress_last_draw_ms to now; the modern build also clears its
 * record of the bar (xvt_cockpit_messages_clear_progress). */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x449110
void flight_loading_reset_progress_state(void)
{
	xvt_cockpit_messages_clear_progress();
	g_flight_loading_progress_step = 0;
	g_flight_loading_progress_last_draw_ms = timeGetTime();
}

/* Called between loading steps: adds 1 to g_flight_loading_progress_step and,
 * when 200 ms have passed since the last draw or the step's low 6 bits were
 * 63, draws the loading bar and shows it. On the 63 case it first sends the
 * other players a still-loading packet (flight_net_broadcast_still_loading_pulse).
 * The bar sits at mid-height from a quarter to three quarters of the screen
 * width, filled in 128 steps that start again from empty, its color index
 * 48 plus the step divided by 128. To draw, it unlocks the surface fully,
 * locks it once, then blits and flips (flight_display_blit_render_surface,
 * flight_display_flip) and locks it back to the count it found. The text
 * cursor, clip rectangle and text colors are saved and put back. The modern
 * build also marks the drawing as an overlay and records the bar for its own
 * renderer. */
// FUNCTION: XVT 0x449130
void flight_loading_pulse_and_draw_progress_screen(void)
{
	/* Advance the progress pulse and redraw the loading bar when due. */
	uint32_t now = timeGetTime();
	uint32_t step_phase = g_flight_loading_progress_step & 0x3fu;
	if (step_phase != 63u &&
	    (int32_t)(now - g_flight_loading_progress_last_draw_ms) < 200) {
		++g_flight_loading_progress_step;
		return;
	}

	xvt_render_capture_begin_overlay();
	g_flight_loading_progress_last_draw_ms = now;
	if (step_phase == 63u) {
		flight_net_broadcast_still_loading_pulse();
	}

	int16_t saved_cursor_x = g_flight_cursor_x;
	int16_t saved_cursor_y = g_flight_cursor_y;
	int16_t saved_clip_left = g_flight_clip_left;
	int16_t saved_clip_top = g_flight_clip_top;
	int16_t saved_clip_right = g_flight_clip_right;
	int16_t saved_clip_bottom = g_flight_clip_bottom;
	int16_t saved_word_wrap = g_flight_word_wrap_enabled;
	int16_t saved_unused_state = g_flight_text_unused_state;
	uint8_t saved_text_color = g_flight_text_color_index;
	int16_t saved_clear_line_background = g_flight_clear_line_bg_enabled;
	uint8_t saved_background_color = g_flight_text_bg_color;
	uint8_t saved_shadow_color = g_flight_text_shadow_color;
	uint8_t saved_shadow_enabled = g_flight_text_shadow_enabled;

	int lock_count = flight_surface_get_lock_count();
	int unlock_count = lock_count;
	while (unlock_count > 0) {
		flight_surface_unlock();
		--unlock_count;
	}
	flight_surface_lock();

	unsigned int bar_left = g_screen_width >> 2;
	unsigned int bar_top = g_screen_height >> 1;
	uint8_t line_height = g_flight_font_line_height;
	uint32_t bar_step = (g_flight_loading_progress_step & 0x7fu) + 1u;
	++g_flight_loading_progress_step;
	unsigned int bar_width = (g_screen_width * bar_step) >> 8;

	flight_text_set_clip_rect((int16_t)bar_left - 2, (int16_t)bar_top - 2,
				  (int16_t)(g_screen_width - bar_left + 2),
				  (int16_t)(bar_top + line_height + 2));
	g_flight_text_bg_color =
		(uint8_t)(g_flight_loading_progress_step / 128u + 48u);
	g_flight_fill_clip_rect_fn();
	flight_text_set_clip_rect((int16_t)bar_left - 1, (int16_t)bar_top - 1,
				  (int16_t)(g_screen_width - bar_left + 1),
				  (int16_t)(bar_top + line_height + 1));
	g_flight_text_bg_color = 0;
	g_flight_fill_clip_rect_fn();
	flight_text_set_clip_rect((int16_t)bar_left, (int16_t)bar_top,
				  (int16_t)(bar_left + bar_width),
				  (int16_t)(bar_top + line_height));
	g_flight_text_bg_color =
		(uint8_t)(g_flight_loading_progress_step / 128u + 48u);
	g_flight_fill_clip_rect_fn();

	xvt_cockpit_messages_record_progress(bar_step, bar_left, bar_top,
					     g_screen_width - 2 * bar_left,
					     line_height, bar_width);
	flight_surface_unlock();
	flight_display_blit_render_surface();
	flight_display_flip();
	while (lock_count > 0) {
		flight_surface_lock();
		--lock_count;
	}

	g_flight_cursor_x = saved_cursor_x;
	g_flight_cursor_y = saved_cursor_y;
	g_flight_clip_left = saved_clip_left;
	g_flight_clip_top = saved_clip_top;
	g_flight_clip_right = saved_clip_right;
	g_flight_clip_bottom = saved_clip_bottom;
	g_flight_word_wrap_enabled = saved_word_wrap;
	g_flight_text_unused_state = saved_unused_state;
	g_flight_text_color_index = saved_text_color;
	g_flight_clear_line_bg_enabled = saved_clear_line_background;
	g_flight_text_bg_color = saved_background_color;
	g_flight_text_shadow_color = saved_shadow_color;
	g_flight_text_shadow_enabled = saved_shadow_enabled;

	xvt_render_capture_end_overlay();
}

/* Pulses the loading bar until its low 7 bits reach 127, then once more,
 * which always draws it full. Only the original build calls this; the modern
 * build sets the bits itself and pulses once. */
// FUNCTION: XVT 0x4493C0
void flight_loading_draw_progress_to_completion(void)
{
	while ((g_flight_loading_progress_step & 0x7fu) != 0x7fu) {
		flight_loading_pulse_and_draw_progress_screen();
	}
	flight_loading_pulse_and_draw_progress_screen();
}

/* Returns 1 when dpid is nonzero and matches the DirectPlay id of one of the
 * 8 entries of g_pilot_data.network_players, else 0. */
// FUNCTION: XVT 0x4493E0
int pilot_data_has_network_player_dpid(int dpid)
{
	for (int player_index = 0; player_index < 8; ++player_index) {
		if (g_pilot_data.network_players[player_index].direct_play_id !=
			    0 &&
		    g_pilot_data.network_players[player_index].direct_play_id ==
			    dpid) {
			return 1;
		}
	}
	return 0;
}
