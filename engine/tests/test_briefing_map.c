/* Tests for xvt/frontend/briefing_map.c, the briefing map: its view steps,
 * projection and flight group pick, and its drawing. The drawing checks run
 * on a frontend display with no window (test_frontend_display.h), where text
 * and images draw nothing, so they read the pixels the grid lines, the label
 * cursor block and the marker boxes leave in the back buffer. The shade ramps
 * hold the test's own colors. No game data is read.
 *
 * POSIX only, for the alarm that stops a check whose call does not return. */
#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "test_assert.h"
#include "test_frontend_display.h"
#include "xvt/frontend/briefing_map.h"
#include "xvt/frontend/briefing_script.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"

static const struct RECT g_viewport = {0, 0, 320, 200};
static char g_label_text[32][40];

static void stop_on_alarm(int signal_number)
{
	static const char message[] =
		"check failed: the call did not return within the time allowed\n";
	(void)signal_number;
	if (write(2, message, sizeof message - 1) < 0) {
		_exit(1);
	}
	_exit(1);
}

/* For a check whose call may never return: the program fails after the given
 * seconds. */
static void fail_after_seconds(unsigned int seconds)
{
	signal(SIGALRM, stop_on_alarm);
	alarm(seconds);
}

/* The view at rest: center (0, 0) and zoom 32, both on target; no markers or
 * labels; the label texts the test's empty strings; three flight groups with
 * no mission points; briefing 0. */
static void fresh_map(void)
{
	g_briefing_map_center.x = 0;
	g_briefing_map_center.y = 0;
	g_briefing_map_target_center = g_briefing_map_center;
	g_briefing_map_scale.x = 32;
	g_briefing_map_scale.y = 32;
	g_briefing_map_target_scale = g_briefing_map_scale;
	memset(g_briefing_map_fg_marker_active, 0,
	       sizeof g_briefing_map_fg_marker_active);
	memset(g_briefing_map_label_active, 0,
	       sizeof g_briefing_map_label_active);
	memset(g_label_text, 0, sizeof g_label_text);
	for (int i = 0; i < 32; ++i) {
		g_briefing_map_label_texts[i] = g_label_text[i];
	}
	memset(&g_frontend_mission, 0, sizeof g_frontend_mission);
	g_frontend_mission.flight_group_count = 3;
	g_active_briefing_index = 0;
	for (int row = 0; row < 5; ++row) {
		for (int shade = 0; shade < 8; ++shade) {
			g_text_shade_ramps[row][shade] =
				0x400 * (row + 1) + shade;
		}
	}
}

/* Puts flight group fg's mission point 14 at map point (x, y). */
static void place_group(int fg, int x, int y)
{
	g_frontend_mission.flight_groups[fg].mission_point_x[14] = (int16_t)x;
	g_frontend_mission.flight_groups[fg].mission_point_y[14] = (int16_t)y;
	g_frontend_mission.flight_groups[fg].mission_point_enabled[14] = 1;
}

/* Locks the back buffer for drawing, clears it, and clips to the screen. */
static void begin_drawing(void)
{
	g_draw_surface_ptr = frontend_display_lock_back_buffer();
	XVT_ASSERT_TRUE(g_draw_surface_ptr != NULL);
	memset(g_draw_surface_ptr, 0,
	       (size_t)g_front_state.back_buffer_pitch * 480);
	static const struct RECT screen = {0, 0, 639, 479};
	frontend_display_set_screen_clip_rect640x480(&screen);
}

static void end_drawing(void) { frontend_display_unlock_back_buffer(); }

static int pixel(int x, int y)
{
	const uint8_t *row = g_draw_surface_ptr +
			     (size_t)y * g_front_state.back_buffer_pitch;
	return ((const uint16_t *)row)[x];
}

/* A step moves toward the target by the step without passing it, and leaves
 * a value on its target alone. */
static void check_step_toward_target(void)
{
	XVT_ASSERT_INT_EQ(briefing_map_step_s16_toward_target(10, 20, 3), 13);
	XVT_ASSERT_INT_EQ(briefing_map_step_s16_toward_target(10, 12, 3), 12);
	XVT_ASSERT_INT_EQ(briefing_map_step_s16_toward_target(10, 0, 4), 6);
	XVT_ASSERT_INT_EQ(briefing_map_step_s16_toward_target(10, 8, 4), 8);
	XVT_ASSERT_INT_EQ(briefing_map_step_s16_toward_target(10, 10, 5), 10);
}

/* The map center lands in the middle of the viewport, and a point 256 map
 * units off it lands the zoom's pixels away on that axis. */
static void check_project_point(void)
{
	fresh_map();
	g_briefing_map_scale.x = 32;
	g_briefing_map_scale.y = 16;
	g_briefing_map_center.x = 100;
	g_briefing_map_center.y = -50;
	static const struct RECT viewport = {10, 20, 330, 220};
	int16_t x = 0;
	int16_t y = 0;
	briefing_map_project_point_to_viewport(&viewport, 100, -50, &x, &y);
	XVT_ASSERT_INT_EQ(x, 170);
	XVT_ASSERT_INT_EQ(y, 120);
	briefing_map_project_point_to_viewport(&viewport, 100 + 256, -50 - 512,
					       &x, &y);
	XVT_ASSERT_INT_EQ(x, 170 + 32);
	XVT_ASSERT_INT_EQ(y, 120 - 2 * 16);
}

/* The zoom steps by 8 when the larger axis gap is 12 or more, by 2 below
 * that, and by 1 when the zoom across is under 10, on both axes, never past
 * the target. */
static void check_animate_zoom(void)
{
	fresh_map();
	g_briefing_map_target_scale.x = 64;
	briefing_map_animate_view_state();
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.x, 40);
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.y, 32);

	fresh_map();
	g_briefing_map_target_scale.x = 43;
	g_briefing_map_target_scale.y = 24;
	briefing_map_animate_view_state();
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.x, 34);
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.y, 30);

	fresh_map();
	g_briefing_map_target_scale.y = 33;
	briefing_map_animate_view_state();
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.y, 33);

	fresh_map();
	g_briefing_map_target_scale.y = 44;
	briefing_map_animate_view_state();
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.y, 40);

	fresh_map();
	g_briefing_map_scale.x = 10;
	g_briefing_map_target_scale.x = 64;
	briefing_map_animate_view_state();
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.x, 18);

	fresh_map();
	g_briefing_map_scale.x = 9;
	g_briefing_map_scale.y = 9;
	g_briefing_map_target_scale.x = 64;
	g_briefing_map_target_scale.y = 64;
	briefing_map_animate_view_state();
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.x, 10);
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.y, 10);
}

/* At zoom 32 the center steps by 2 * (256 / 32 + 1) = 18 map units, and by
 * 36 when it is 16 or more of those sums away; at zoom 0 the sum is 1, so it
 * steps by 2. It never passes its target. */
static void check_animate_center(void)
{
	fresh_map();
	g_briefing_map_target_center.x = 143;
	g_briefing_map_target_center.y = -100;
	briefing_map_animate_view_state();
	XVT_ASSERT_INT_EQ(g_briefing_map_center.x, 18);
	XVT_ASSERT_INT_EQ(g_briefing_map_center.y, -18);

	fresh_map();
	g_briefing_map_target_center.x = 144;
	g_briefing_map_target_center.y = 10;
	briefing_map_animate_view_state();
	XVT_ASSERT_INT_EQ(g_briefing_map_center.x, 36);
	XVT_ASSERT_INT_EQ(g_briefing_map_center.y, 10);

	fresh_map();
	g_briefing_map_scale.x = 0;
	g_briefing_map_target_scale.x = 0;
	g_briefing_map_target_center.x = 10;
	briefing_map_animate_view_state();
	XVT_ASSERT_INT_EQ(g_briefing_map_center.x, 2);
}

/* Each frame raises the age of every active marker and label by one and
 * leaves the inactive ones alone. */
static void check_animate_ages(void)
{
	fresh_map();
	g_briefing_map_fg_marker_active[0] = 1;
	g_briefing_map_fg_marker_active[7] = 1;
	g_briefing_map_label_active[7] = 1;
	for (int i = 0; i < 8; ++i) {
		g_briefing_map_fg_marker_age[i] = (int16_t)(10 * i);
		g_briefing_map_label_age[i] = (int16_t)(10 * i + 5);
	}
	briefing_map_animate_view_state();
	XVT_ASSERT_INT_EQ(g_briefing_map_fg_marker_age[0], 1);
	XVT_ASSERT_INT_EQ(g_briefing_map_fg_marker_age[7], 71);
	XVT_ASSERT_INT_EQ(g_briefing_map_fg_marker_age[3], 30);
	XVT_ASSERT_INT_EQ(g_briefing_map_label_age[7], 76);
	XVT_ASSERT_INT_EQ(g_briefing_map_label_age[0], 5);
}

/* Before the script's length a frame plays the next script frame; at its
 * length the briefing starts over, with the page count and the last
 * narrated block at 0. */
static void check_update_script_playback(void)
{
	fresh_map();
	memset(&g_briefing_script, 0, sizeof g_briefing_script);
	g_briefing_script.duration_frames = 5;
	g_briefing_script.words[0] = 9999;
	g_briefing_script.words[1] = 34;
	g_briefing_script.current_frame = 2;
	g_briefing_text_page_number = 3;
	g_briefing_last_narrated_text_block_idx = 4;
	briefing_map_update_script_playback_after_animation();
	XVT_ASSERT_INT_EQ(g_briefing_script.current_frame, 3);
	XVT_ASSERT_INT_EQ(g_briefing_text_page_number, 3);
	g_briefing_script.current_frame = 5;
	g_briefing_map_scale.x = 64;
	briefing_map_update_script_playback_after_animation();
	XVT_ASSERT_INT_EQ(g_briefing_script.current_frame, 1);
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.x, 32);
	XVT_ASSERT_INT_EQ(g_briefing_text_page_number, 0);
	XVT_ASSERT_INT_EQ(g_briefing_last_narrated_text_block_idx, 0);
}

/* The pick takes the group whose point 14 lies nearest the mouse, by the
 * larger of the two pixel distances, among groups with that point set; the
 * first wins a tie. With none under 999 pixels away it returns 0 and keeps
 * the last pick. The pick at the cursor measures in the map panel at the
 * screen's top left, whatever viewport it is given, and returns 1. */
static void check_select_nearest(void)
{
	fresh_map();
	place_group(0, 0, 0);
	place_group(1, 512, 0);
	g_frontend_mission.flight_groups[2].mission_point_x[14] = 448;
	g_briefing_selected_mission_point14_flight_group_idx = 7;
	/* Group 0 lies at (160, 100), group 1 at (224, 100). */
	XVT_ASSERT_INT_EQ(
		briefing_map_select_nearest_mission_point14_flight_group(
			&g_viewport, 214, 100),
		1);
	XVT_ASSERT_INT_EQ(g_briefing_selected_mission_point14_flight_group_idx,
			  1);
	XVT_ASSERT_INT_EQ(
		briefing_map_select_nearest_mission_point14_flight_group(
			&g_viewport, 180, 130),
		1);
	XVT_ASSERT_INT_EQ(g_briefing_selected_mission_point14_flight_group_idx,
			  0);
	XVT_ASSERT_INT_EQ(
		briefing_map_select_nearest_mission_point14_flight_group(
			&g_viewport, 192, 90),
		1);
	XVT_ASSERT_INT_EQ(g_briefing_selected_mission_point14_flight_group_idx,
			  0);
	g_briefing_selected_mission_point14_flight_group_idx = 7;
	XVT_ASSERT_INT_EQ(
		briefing_map_select_nearest_mission_point14_flight_group(
			&g_viewport, 1500, 100),
		0);
	XVT_ASSERT_INT_EQ(g_briefing_selected_mission_point14_flight_group_idx,
			  7);

	/* In the 360 by 236 panel group 0 lies at (180, 118), group 1 at
	 * (244, 118). */
	static const struct RECT panel = {0, 0, 360, 236};
	g_briefing_map_panel_rect = panel;
	XVT_ASSERT_INT_EQ(briefing_map_select_flight_group_at_cursor(
				  &g_viewport, &g_viewport, 0, 0, 240, 118),
			  1);
	XVT_ASSERT_INT_EQ(g_briefing_selected_mission_point14_flight_group_idx,
			  1);
	XVT_ASSERT_INT_EQ(briefing_map_select_flight_group_at_cursor(
				  &g_viewport, &g_viewport, 0, 0, 200, 118),
			  1);
	XVT_ASSERT_INT_EQ(g_briefing_selected_mission_point14_flight_group_idx,
			  0);
}

/* The grid draws a line every 256 map units, the zoom's pixels apart, those
 * at multiples of 1024 in the bright red; at zoom 16 the lines halfway
 * between those in the dark red, and at 32 every line. Centered at (0, 0)
 * in a 320 by 200 viewport, the column at x 160 holds the line of map x 0.
 * Row 20 lies between horizontal lines at every zoom used here. */
static void check_draw_grid(void)
{
	int major = frontend_display_pack_rgb(0x96, 0, 0);
	int minor = frontend_display_pack_rgb(0x50, 0, 0);
	XVT_ASSERT_TRUE(major != minor);
	fresh_map();
	begin_drawing();
	briefing_map_draw_grid(&g_viewport, &g_viewport);
	XVT_ASSERT_INT_EQ(pixel(160, 20), major);
	XVT_ASSERT_INT_EQ(pixel(160 + 4 * 32, 20), major);
	XVT_ASSERT_INT_EQ(pixel(160 - 4 * 32, 20), major);
	XVT_ASSERT_INT_EQ(pixel(160 + 32, 20), minor);
	XVT_ASSERT_INT_EQ(pixel(160 + 2 * 32, 20), minor);
	XVT_ASSERT_INT_EQ(pixel(160 - 32, 20), minor);
	XVT_ASSERT_INT_EQ(pixel(160 + 16, 20), 0);
	XVT_ASSERT_INT_EQ(pixel(140, 100), major);
	XVT_ASSERT_INT_EQ(pixel(140, 100 + 4 * 32), 0);
	XVT_ASSERT_INT_EQ(pixel(140, 100 + 32), minor);
	XVT_ASSERT_INT_EQ(pixel(330, 20), 0);
	end_drawing();

	fresh_map();
	g_briefing_map_scale.x = 16;
	g_briefing_map_scale.y = 16;
	begin_drawing();
	briefing_map_draw_grid(&g_viewport, &g_viewport);
	XVT_ASSERT_INT_EQ(pixel(160 + 4 * 16, 20), major);
	XVT_ASSERT_INT_EQ(pixel(160 + 2 * 16, 20), minor);
	XVT_ASSERT_INT_EQ(pixel(160 + 16, 20), 0);
	XVT_ASSERT_INT_EQ(pixel(160 + 3 * 16, 20), 0);
	end_drawing();

	fresh_map();
	g_briefing_map_scale.x = 8;
	g_briefing_map_scale.y = 8;
	begin_drawing();
	briefing_map_draw_grid(&g_viewport, &g_viewport);
	XVT_ASSERT_INT_EQ(pixel(160 + 4 * 8, 20), major);
	XVT_ASSERT_INT_EQ(pixel(160 + 2 * 8, 20), 0);
	end_drawing();
}

/* Measures the box drawn around (160, 100): the fill runs out from there in
 * fill_color to an edge in edge_color on all four sides, with nothing drawn
 * past the edges. Returns its width and height in pixels. */
static void measure_box(int fill_color, int edge_color, int *width, int *height)
{
	XVT_ASSERT_INT_EQ(pixel(160, 100), fill_color);
	int left = 160;
	while (left > 100 && pixel(left, 100) == fill_color) {
		--left;
	}
	int right = 160;
	while (right < 220 && pixel(right, 100) == fill_color) {
		++right;
	}
	int top = 100;
	while (top > 40 && pixel(160, top) == fill_color) {
		--top;
	}
	int bottom = 100;
	while (bottom < 160 && pixel(160, bottom) == fill_color) {
		++bottom;
	}
	XVT_ASSERT_INT_EQ(pixel(left, 100), edge_color);
	XVT_ASSERT_INT_EQ(pixel(right, 100), edge_color);
	XVT_ASSERT_INT_EQ(pixel(160, top), edge_color);
	XVT_ASSERT_INT_EQ(pixel(160, bottom), edge_color);
	XVT_ASSERT_INT_EQ(pixel(left - 1, 100), 0);
	XVT_ASSERT_INT_EQ(pixel(right + 1, 100), 0);
	XVT_ASSERT_INT_EQ(pixel(160, top - 1), 0);
	XVT_ASSERT_INT_EQ(pixel(160, bottom + 1), 0);
	*width = right - left + 1;
	*height = bottom - top + 1;
}

/* From phase 12 on the highlight is a box 2 pixels outside the icon, filled in
 * shade 2 of the row the group's IFF picks and outlined in its shade 6: row 0
 * for IFF 0 and any IFF above 5. (The other rows are read as row 0's shades 8
 * and up, past the end of that row, which the test build stops on, so only row
 * 0 is checked.) At phase 11 the box lies on the icon's edges, outlined in
 * shade 5, and at phase 8, 3 pixels inside them; under phase 8 no box is drawn.
 * The group's icon, of craft type 0, is centered on its point 14, here at (160,
 * 100); an icon's size is right - left + 1 by bottom - top + 1 of its
 * rectangle. */
static void check_icon_highlight(void)
{
	const struct RECT *icon =
		&g_map_icon_rects[g_map_icon_by_craft_type[0]];
	int icon_width = icon->right - icon->left + 1;
	int icon_height = icon->bottom - icon->top + 1;
	int width = 0;
	int height = 0;
	static const int iff_of_row_0[2] = {0, 6};
	for (int i = 0; i < 2; ++i) {
		fresh_map();
		place_group(1, 0, 0);
		g_frontend_mission.flight_groups[1].iff =
			(uint8_t)iff_of_row_0[i];
		begin_drawing();
		briefing_map_draw_craft_icon_highlight(&g_viewport, &g_viewport,
						       1, 12);
		measure_box(g_text_shade_ramps[0][2], g_text_shade_ramps[0][6],
			    &width, &height);
		XVT_ASSERT_INT_EQ(width, icon_width + 4);
		XVT_ASSERT_INT_EQ(height, icon_height + 4);
		end_drawing();
	}

	fresh_map();
	place_group(1, 0, 0);
	begin_drawing();
	briefing_map_draw_craft_icon_highlight(&g_viewport, &g_viewport, 1, 11);
	measure_box(g_text_shade_ramps[0][2], g_text_shade_ramps[0][5], &width,
		    &height);
	XVT_ASSERT_INT_EQ(width, icon_width);
	XVT_ASSERT_INT_EQ(height, icon_height);
	end_drawing();
	begin_drawing();
	briefing_map_draw_craft_icon_highlight(&g_viewport, &g_viewport, 1, 8);
	XVT_ASSERT_INT_EQ(pixel(160, 100), g_text_shade_ramps[0][2]);
	XVT_ASSERT_INT_EQ(pixel(160 - icon_width / 2 + 2, 100), 0);
	end_drawing();

	fresh_map();
	place_group(1, 0, 0);
	begin_drawing();
	briefing_map_draw_craft_icon_highlight(&g_viewport, &g_viewport, 1, 7);
	XVT_ASSERT_INT_EQ(pixel(160, 100), 0);
	end_drawing();
}

/* A label types itself out: while fewer characters show than the text has, a
 * small block in shade 7 of its row, here row 0 as above, follows the text (here, with no font, at
 * the label's point plus 2 to 8 pixels across and 0 to 6 down); once all
 * show it is gone. The overlays draw an active label at its projected point
 * revealing twice its age, and a marker's box by its age. */
static void check_labels_and_markers(void)
{
	fresh_map();
	begin_drawing();
	briefing_map_draw_revealed_label("Convoy", 1, 50, 60, 5, 0);
	XVT_ASSERT_INT_EQ(pixel(53, 63), g_text_shade_ramps[0][7]);
	XVT_ASSERT_INT_EQ(pixel(60, 63), 0);
	end_drawing();
	begin_drawing();
	briefing_map_draw_revealed_label("Convoy", 1, 50, 60, 6, 0);
	XVT_ASSERT_INT_EQ(pixel(53, 63), 0);
	briefing_map_draw_revealed_label_if_active("Convoy", 1, 50, 60, -1, 0);
	XVT_ASSERT_INT_EQ(pixel(53, 63), 0);
	briefing_map_draw_revealed_label_if_active("Convoy", 1, 50, 60, 2, 0);
	XVT_ASSERT_INT_EQ(pixel(53, 63), g_text_shade_ramps[0][7]);
	end_drawing();
	begin_drawing();
	briefing_map_draw_revealed_label_if_active("Convoy", 1, 50, 60, 0, 0);
	XVT_ASSERT_INT_EQ(pixel(53, 63), g_text_shade_ramps[0][7]);
	end_drawing();

	fresh_map();
	strcpy(g_label_text[5], "[Red] leader");
	g_briefing_map_label_active[3] = 1;
	g_briefing_map_label_text_idx[3] = 5;
	g_briefing_map_label_x[3] = 256;
	g_briefing_map_label_y[3] = 0;
	g_briefing_map_label_age[3] = 2;
	g_briefing_map_label_style[3] = 0;
	place_group(2, 0, 0);
	g_briefing_map_fg_marker_active[6] = 1;
	g_briefing_map_fg_marker_flight_group_idx[6] = 2;
	g_briefing_map_fg_marker_age[6] = 40;
	begin_drawing();
	briefing_map_draw_overlays(&g_viewport, &g_viewport);
	XVT_ASSERT_INT_EQ(pixel(192 + 3, 100 + 3), g_text_shade_ramps[0][7]);
	XVT_ASSERT_INT_EQ(pixel(160, 100), g_text_shade_ramps[0][2]);
	end_drawing();

	g_briefing_map_label_age[3] = 6;
	g_briefing_map_fg_marker_age[6] = 5;
	begin_drawing();
	briefing_map_draw_overlays(&g_viewport, &g_viewport);
	XVT_ASSERT_INT_EQ(pixel(192 + 3, 100 + 3), 0);
	XVT_ASSERT_INT_EQ(pixel(160, 100), 0);
	end_drawing();
}

/* The panel draws the map, in the panel less its bottom 28 pixels, and
 * returns 1. With text slot 1 on it counts a
 * page each time slot 1's block differs from the last one narrated, and
 * stores that block; the same block again counts nothing. */
static void check_viewport_and_selection(void)
{
	fresh_map();
	static const struct RECT panel = {0, 0, 360, 236};
	g_briefing_map_panel_rect = panel;
	static char block[2][320] = {"First page.", "Second page."};
	g_briefing_text_blocks[2] = block[0];
	g_briefing_text_blocks[3] = block[1];
	g_briefing_text_slot_active[1] = 0;
	g_briefing_text_page_number = 0;
	g_briefing_last_narrated_text_block_idx = 0;
	static const struct RECT origin = {0, 0, 640, 480};
	begin_drawing();
	XVT_ASSERT_INT_EQ(
		briefing_map_draw_viewport_and_selection(&origin, &origin, 0),
		1);
	XVT_ASSERT_INT_EQ(g_briefing_text_page_number, 0);
	/* The map takes the panel less its bottom 28 pixels: the grid line of
	 * map x 0 runs down column 180 to row 208 and no further. */
	XVT_ASSERT_INT_EQ(pixel(180, 190),
			  frontend_display_pack_rgb(0x96, 0, 0));
	XVT_ASSERT_INT_EQ(pixel(180, 208),
			  frontend_display_pack_rgb(0x96, 0, 0));
	XVT_ASSERT_INT_EQ(pixel(180, 209), 0);
	g_briefing_text_slot_active[1] = 1;
	g_briefing_text_slot_block_idx[1] = 2;
	briefing_map_draw_viewport_and_selection(&origin, &origin, 0);
	XVT_ASSERT_INT_EQ(g_briefing_text_page_number, 1);
	XVT_ASSERT_INT_EQ(g_briefing_last_narrated_text_block_idx, 2);
	briefing_map_draw_viewport_and_selection(&origin, &origin, 0);
	XVT_ASSERT_INT_EQ(g_briefing_text_page_number, 1);
	g_briefing_text_slot_block_idx[1] = 3;
	briefing_map_draw_viewport_and_selection(&origin, &origin, 0);
	XVT_ASSERT_INT_EQ(g_briefing_text_page_number, 2);
	XVT_ASSERT_INT_EQ(g_briefing_last_narrated_text_block_idx, 3);
	end_drawing();
	g_briefing_text_slot_active[1] = 0;
	g_briefing_text_blocks[2] = NULL;
	g_briefing_text_blocks[3] = NULL;
}

/* Known failure grid_zoom_zero, issue #149: the grid draws its lines the
 * zoom's pixels apart. With a zoom of 0 across, the search for the first line
 * steps by 0 and never ends; the grid should still return. */
static void check_grid_zoom_zero(void)
{
	fresh_map();
	g_briefing_map_scale.x = 0;
	begin_drawing();
	briefing_map_draw_grid(&g_viewport, &g_viewport);
	end_drawing();
}

/* Known failure zoom_divides_by_zero, issue #149: the center's step is
 * worked out from 256 / zoom + 1, which is 0 for a zoom across of -129 to
 * -256, and the center's distance is then divided by it. A frame at zoom
 * -200 should leave the view where it is. */
static void check_zoom_divides_by_zero(void)
{
	fresh_map();
	g_briefing_map_scale.x = -200;
	g_briefing_map_target_scale.x = -200;
	briefing_map_animate_view_state();
	XVT_ASSERT_INT_EQ(g_briefing_map_scale.x, -200);
	XVT_ASSERT_INT_EQ(g_briefing_map_center.x, 0);
}

/* Known failure overlay_craft_type_past_icon_table, issue #150: the icon
 * table has an entry for craft types 0 to 105. A shown flight group of craft
 * type 106 reads its icon from past the table. */
static void check_overlay_craft_type_past_icon_table(void)
{
	fresh_map();
	place_group(0, 0, 0);
	g_frontend_mission.flight_groups[0].craft_type = 106;
	begin_drawing();
	briefing_map_draw_overlays(&g_viewport, &g_viewport);
	end_drawing();
}

/* Known failure highlight_craft_type_past_icon_table, issue #150: the same
 * for a marked flight group's highlight. */
static void check_highlight_craft_type_past_icon_table(void)
{
	fresh_map();
	place_group(0, 0, 0);
	g_frontend_mission.flight_groups[0].craft_type = 106;
	begin_drawing();
	briefing_map_draw_craft_icon_highlight(&g_viewport, &g_viewport, 0, 12);
	end_drawing();
}

/* Known failure marker_age_past_32767, issue #152: a marker's age counts
 * the frames since it appeared, and from phase 12 on its highlight is a box.
 * A marker 32767 frames old ages one more frame and its 16-bit age turns
 * negative, so the box is no longer drawn. */
static void check_marker_age_past_32767(void)
{
	fresh_map();
	place_group(0, 0, 0);
	g_briefing_map_fg_marker_active[0] = 1;
	g_briefing_map_fg_marker_flight_group_idx[0] = 0;
	g_briefing_map_fg_marker_age[0] = 32767;
	briefing_map_animate_view_state();
	begin_drawing();
	briefing_map_draw_overlays(&g_viewport, &g_viewport);
	XVT_ASSERT_INT_EQ(pixel(160, 100), g_text_shade_ramps[0][2]);
	end_drawing();
}

int main(int argc, char **argv)
{
	memset(&g_front_state, 0, sizeof g_front_state);
	xvt_test_open_display();
	fail_after_seconds(30);
	/* "known-failure <check>" runs one check the code is known to fail; an
	 * unknown name runs nothing. */
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		static const struct {
			const char *name;
			void (*check)(void);
		} known_failures[] = {
			{"grid_zoom_zero", check_grid_zoom_zero},
			{"zoom_divides_by_zero", check_zoom_divides_by_zero},
			{"overlay_craft_type_past_icon_table",
			 check_overlay_craft_type_past_icon_table},
			{"highlight_craft_type_past_icon_table",
			 check_highlight_craft_type_past_icon_table},
			{"marker_age_past_32767", check_marker_age_past_32767},
		};
		for (size_t i = 0;
		     i < sizeof known_failures / sizeof known_failures[0];
		     ++i) {
			if (strcmp(argv[2], known_failures[i].name) == 0) {
				known_failures[i].check();
			}
		}
		xvt_test_close_display();
		return 0;
	}
	check_step_toward_target();
	check_project_point();
	check_animate_zoom();
	check_animate_center();
	check_animate_ages();
	check_update_script_playback();
	check_select_nearest();
	check_draw_grid();
	check_icon_highlight();
	check_labels_and_markers();
	check_viewport_and_selection();
	xvt_test_close_display();
	return 0;
}
