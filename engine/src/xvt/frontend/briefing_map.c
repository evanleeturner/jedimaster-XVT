#include "xvt/frontend/briefing_map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xvt/frontend/briefing_script.h"
#include "xvt/frontend/briefing_text.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"

/* Flight group nearest the mouse on the briefing map, by its mission point
 * 14: set by briefing_map_select_nearest_mission_point14_flight_group, and to 0 by
 * frontend_mission_init_for_briefing. Nothing reads it. */
// GLOBAL: XVT 0x6691E8
int16_t g_briefing_selected_mission_point14_flight_group_idx = 0;

/* Map point drawn at the middle of the briefing map, in the units of the
 * flight groups' mission points. briefing_map_animate_view_state steps it toward
 * g_briefing_map_target_center; script opcode 6 sets it directly at time 0 or
 * when applied at once; briefing_script_reset_state and
 * frontend_mission_init_for_briefing set (0, 0). */
// GLOBAL: XVT 0x669204
struct briefing_map_s16_pair g_briefing_map_center = {0, 0};
/* Map point the center moves toward, set by script opcode 6; (0, 0) after
 * briefing_script_reset_state and frontend_mission_init_for_briefing. */
// GLOBAL: XVT 0x669208
struct briefing_map_s16_pair g_briefing_map_target_center = {0, 0};
/* Briefing map zoom on each axis: pixels per 256 map units, so the grid lines
 * at every 256 units sit this many pixels apart. briefing_map_animate_view_state
 * steps it toward g_briefing_map_target_scale; script opcode 7 sets it directly
 * at time 0 or when applied at once; 32 after briefing_script_reset_state and
 * frontend_mission_init_for_briefing. */
// GLOBAL: XVT 0x66920C
struct briefing_map_s16_pair g_briefing_map_scale = {0, 0};
/* Zoom the scale moves toward, set by script opcode 7; 32 after
 * briefing_script_reset_state and frontend_mission_init_for_briefing. */
// GLOBAL: XVT 0x669210
struct briefing_map_s16_pair g_briefing_map_target_scale = {0, 0};
/* Which of the mission file's 8 briefings is shown, 0 to 7: the last one
 * flagged for the pilot's team, set by
 * frontend_mission_load_current_with_briefing; frontend_mission_init_for_briefing
 * sets 0. Flight groups sit on the map at mission point 14 plus this. */
// GLOBAL: XVT 0x669214
int g_active_briefing_index = 0;
/* Index in g_map_icon_rects of each craft type's map icon, by craft_species
 * value 0 to 105. */
// GLOBAL: XVT 0x52C908
int g_map_icon_by_craft_type[106] = {
	0,  0,	1,  2,	3,  4,	5,  6,	7,  8,	0,  0,	9,  10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 0,	28, 29, 30, 31,
	64, 32, 33, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
	65, 48, 49, 50, 51, 52, 53, 53, 53, 53, 53, 53, 63, 61, 62, 54, 55, 55,
	55, 55, 55, 56, 57, 58, 66, 60, 59, 59, 59, 60, 60, 58, 61, 61, 0,  0,
	67, 68, 69, 0,	0,  0,	0,  0,	0,  0,	61, 61, 61, 61, 61, 61,
};
/* Where each briefing map icon lies in the "mapicon0" to "mapicon4" and
 * "greyicon" images; the code takes right - left + 1 as an icon's width and
 * bottom - top + 1 as its height. */
// GLOBAL: XVT 0x52CAB0
struct RECT g_map_icon_rects[70] = {
	{6, 9, 13, 20},	     {25, 8, 32, 20},	   {44, 10, 50, 19},
	{61, 10, 71, 19},    {82, 10, 89, 18},	   {101, 10, 108, 18},
	{118, 10, 128, 19},  {139, 9, 145, 19},	   {157, 10, 166, 19},
	{175, 9, 185, 18},   {196, 8, 202, 20},	   {215, 9, 221, 19},
	{234, 10, 241, 19},  {251, 10, 261, 18},   {270, 10, 280, 18},
	{289, 9, 299, 19},   {6, 29, 12, 42},	   {25, 30, 31, 42},
	{45, 31, 50, 41},    {63, 30, 70, 42},	   {82, 30, 89, 42},
	{103, 34, 106, 39},  {121, 33, 125, 40},   {140, 31, 145, 41},
	{158, 32, 166, 40},  {179, 32, 182, 40},   {195, 32, 203, 40},
	{216, 33, 221, 40},  {233, 29, 242, 43},   {252, 30, 260, 41},
	{271, 30, 279, 42},  {290, 29, 298, 44},   {5, 53, 14, 63},
	{24, 53, 32, 64},    {44, 51, 50, 65},	   {63, 51, 69, 65},
	{83, 50, 87, 66},    {100, 50, 108, 66},   {120, 49, 126, 66},
	{140, 51, 145, 66},  {158, 50, 164, 66},   {177, 50, 183, 66},
	{196, 50, 203, 66},  {214, 49, 222, 68},   {234, 50, 240, 66},
	{253, 51, 259, 65},  {271, 50, 279, 66},   {289, 48, 299, 68},
	{5, 77, 14, 83},     {26, 77, 31, 83},	   {43, 78, 51, 83},
	{63, 76, 69, 84},    {83, 76, 88, 84},	   {98, 74, 111, 87},
	{115, 73, 131, 87},  {138, 99, 146, 104},  {157, 98, 165, 105},
	{176, 98, 184, 105}, {197, 101, 202, 103}, {216, 100, 221, 105},
	{234, 97, 240, 106}, {253, 77, 259, 83},   {272, 77, 278, 83},
	{288, 72, 301, 88},  {46, 95, 50, 108},	   {24, 90, 32, 110},
	{65, 97, 68, 105},   {79, 95, 93, 108},	   {98, 95, 111, 108},
	{120, 94, 128, 108},
};
/* The briefing map panel, (0, 0) to (360, 236) once
 * frontend_mission_init_for_briefing sets it; its users move it by their
 * viewport's top left. The narration takes its bottom 27 pixels and the map
 * the rest less 28. */
// GLOBAL: XVT 0x669220
struct RECT g_briefing_map_panel_rect = {0, 0, 0, 0};
/* Set to 1 by script opcode 6 and to 0 at the start of every script frame by
 * briefing_script_advance_frame; nothing reads it. */
// GLOBAL: XVT 0x6696D6
int16_t g_briefing_map_center_dirty = 0;
/* Set to 1 by script opcode 7 and to 0 at the start of every script frame by
 * briefing_script_advance_frame; nothing reads it. */
// GLOBAL: XVT 0x6696D8
int16_t g_briefing_map_scale_dirty = 0;
/* Per marker slot, 1 while it highlights a flight group on the map: set by
 * script opcodes 9 to 16, cleared by opcode 8, briefing_script_reset_state and
 * frontend_mission_init_for_briefing. */
// GLOBAL: XVT 0x6696E4
int16_t g_briefing_map_fg_marker_active[8] = {0};
/* Per marker slot, the flight group it highlights; only
 * briefing_script_advance_frame writes it. */
// GLOBAL: XVT 0x6696F4
int16_t g_briefing_map_fg_marker_flight_group_idx[8] = {0};
/* Per marker slot, frames since it appeared: 0 when shown, 80 when shown at
 * once, then raised by briefing_map_animate_view_state each frame the briefing
 * plays. briefing_map_draw_craft_icon_highlight takes it as highlight_phase. */
// GLOBAL: XVT 0x669704
int16_t g_briefing_map_fg_marker_age[8] = {0};
/* Set to 1 by script opcode 8 and to 0 at the start of every script frame by
 * briefing_script_advance_frame; nothing reads it. */
// GLOBAL: XVT 0x669714
int16_t g_briefing_map_fg_markers_changed = 0;
/* Per label slot, 1 while it shows a label on the map: set by script opcodes
 * 18 to 25, cleared by opcode 17, briefing_script_reset_state and
 * frontend_mission_init_for_briefing. */
// GLOBAL: XVT 0x669716
int16_t g_briefing_map_label_active[8] = {0};
/* Per label slot, the index in g_briefing_map_label_texts of its text; only
 * briefing_script_advance_frame writes it. */
// GLOBAL: XVT 0x669726
int16_t g_briefing_map_label_text_idx[8] = {0};
/* Per label slot, the map x its text is drawn at; only
 * briefing_script_advance_frame writes it. */
// GLOBAL: XVT 0x669736
int16_t g_briefing_map_label_x[8] = {0};
/* Per label slot, the map y its text is drawn at; only
 * briefing_script_advance_frame writes it. */
// GLOBAL: XVT 0x669746
int16_t g_briefing_map_label_y[8] = {0};
/* Per label slot, frames since it appeared, kept like g_briefing_map_fg_marker_age;
 * up to twice this many characters of the text show. */
// GLOBAL: XVT 0x669756
int16_t g_briefing_map_label_age[8] = {0};
/* Per label slot, the row of g_text_shade_ramps its text is drawn in; the
 * mission setup screen fills rows 0 to 4 with green, red, yellow, blue and
 * purple, dark to bright. Only briefing_script_advance_frame writes it. */
// GLOBAL: XVT 0x669766
int16_t g_briefing_map_label_style[8] = {0};
/* Set to 1 by script opcode 17 and to 0 at the start of every script frame by
 * briefing_script_advance_frame; nothing reads it. */
// GLOBAL: XVT 0x669776
int16_t g_briefing_map_labels_changed = 0;

/* Finds the flight group whose mission point 14, projected into viewport_rect,
 * is nearest the mouse, measuring the larger of the x and y distances in
 * pixels. Only groups with point 14 set count, and the first wins a tie.
 * Stores it in g_briefing_selected_mission_point14_flight_group_idx and returns 1;
 * returns 0, storing nothing, when no group lies under 999 pixels away on
 * both axes. */
// FUNCTION: XVT 0x4F7A30
int16_t briefing_map_select_nearest_mission_point14_flight_group(
	const struct RECT *viewport_rect, int16_t mouse_x, int16_t mouse_y)
{
	int16_t selected_flight_group_idx = 0;
	int16_t projected_y;
	int16_t projected_x;

	int16_t best_distance = 999;
	int16_t flight_group_idx = 0;
	for (;
	     (int16_t)g_frontend_mission.flight_group_count > flight_group_idx;
	     flight_group_idx++) {
		int16_t map_x =
			g_frontend_mission.flight_groups[flight_group_idx]
				.mission_point_x[14];
		int16_t map_y =
			g_frontend_mission.flight_groups[flight_group_idx]
				.mission_point_y[14];
		if (g_frontend_mission.flight_groups[flight_group_idx]
			    .mission_point_enabled[14] != 0) {
			briefing_map_project_point_to_viewport(
				viewport_rect, map_x, map_y, &projected_x,
				&projected_y);
			int distance_x = abs(mouse_x - projected_x);
			if (distance_x < best_distance) {
				int distance_y = abs(mouse_y - projected_y);
				if (distance_y < best_distance) {
					best_distance = (int16_t)distance_y;
					if (distance_y <= distance_x) {
						best_distance =
							(int16_t)distance_x;
					}
					selected_flight_group_idx =
						flight_group_idx;
				}
			}
		}
	}

	if (best_distance != 999) {
		g_briefing_selected_mission_point14_flight_group_idx =
			selected_flight_group_idx;
		return 1;
	}
	return 0;
}

/* Converts a map point to screen pixels: x is g_briefing_map_scale.x * (map_x -
 * g_briefing_map_center.x) / 256 plus viewport_rect's left plus (right - left)
 * >> 1, and y the same with the y values, top and bottom. Both results are
 * cut to 16 bits. */
// FUNCTION: XVT 0x4F7B10
void briefing_map_project_point_to_viewport(const struct RECT *viewport_rect,
					    int16_t map_x, int16_t map_y,
					    int16_t *out_x, int16_t *out_y)
{
	int projected_x = g_briefing_map_scale.x *
			  (map_x - g_briefing_map_center.x) / 256;
	*out_x = (int16_t)projected_x;
	*out_x = (int16_t)(projected_x + viewport_rect->left +
			   ((viewport_rect->right - viewport_rect->left) >> 1));
	int projected_y = g_briefing_map_scale.y *
			  (map_y - g_briefing_map_center.y) / 256;
	*out_y = (int16_t)projected_y;
	*out_y = (int16_t)(projected_y + viewport_rect->top +
			   ((viewport_rect->bottom - viewport_rect->top) >> 1));
}

/* Returns current moved by step toward target without passing it, or
 * current when they are equal. Does not guard against 16-bit wraparound. */
// FUNCTION: XVT 0x4F7C20
int16_t briefing_map_step_s16_toward_target(int16_t current, int16_t target,
					    int16_t step)
{
	if (target < current) {
		current = (int16_t)(current - step);
		if (target > current) {
			current = target;
		}
	}
	if (target > current) {
		current = (int16_t)(current + step);
		if (target < current) {
			current = target;
		}
	}
	return current;
}

/* Moves the briefing map one frame toward its targets and ages the markers
 * and labels. The scale steps by 8 when the larger axis gap is 12 or more,
 * else by 2, and by 1 whenever g_briefing_map_scale.x is under 10. The center
 * then steps on both axes by 2 * (256 / g_briefing_map_scale.x + 1) map units,
 * that sum taken as 1 when the scale is 0, and twice that when the larger
 * center gap divided by the sum is 16 or more. Raises
 * g_briefing_map_fg_marker_age and g_briefing_map_label_age of every active slot by
 * one. */
// FUNCTION: XVT 0x4F7C50
void briefing_map_animate_view_state(void)
{
	int16_t maximum_difference =
		abs(g_briefing_map_scale.x - g_briefing_map_target_scale.x);
	int16_t axis_difference =
		abs(g_briefing_map_scale.y - g_briefing_map_target_scale.y);
	if (maximum_difference < axis_difference) {
		maximum_difference = axis_difference;
	}
	int16_t scale_step = 2;
	if (maximum_difference >= 12) {
		scale_step = 8;
	}
	if (g_briefing_map_scale.x < 10) {
		scale_step = 1;
	}
	g_briefing_map_scale.x = briefing_map_step_s16_toward_target(
		g_briefing_map_scale.x, g_briefing_map_target_scale.x,
		scale_step);
	g_briefing_map_scale.y = briefing_map_step_s16_toward_target(
		g_briefing_map_scale.y, g_briefing_map_target_scale.y,
		scale_step);

	int16_t scale_divisor;
	if (g_briefing_map_scale.x != 0) {
		scale_divisor = 256 / g_briefing_map_scale.x + 1;
	} else {
		scale_divisor = 1;
	}
	int16_t center_difference =
		abs(g_briefing_map_center.x - g_briefing_map_target_center.x);
	axis_difference =
		abs(g_briefing_map_center.y - g_briefing_map_target_center.y);
	if (center_difference < axis_difference) {
		center_difference = axis_difference;
	}
	/* axis_difference now holds the center distance in screen pixels (map
	 * units over map units per pixel), which picks the faster center step
	 * below. */
	axis_difference = center_difference / (int16_t)scale_divisor;
	/* From here scale_divisor is the step for moving the map center: twice
	 * the map units per pixel, and twice that again when the center is 16
	 * or more screen pixels away. */
	scale_divisor *= 2;
	if (axis_difference >= 16) {
		scale_divisor *= 2;
	}
	g_briefing_map_center.x = briefing_map_step_s16_toward_target(
		g_briefing_map_center.x, g_briefing_map_target_center.x,
		scale_divisor);
	g_briefing_map_center.y = briefing_map_step_s16_toward_target(
		g_briefing_map_center.y, g_briefing_map_target_center.y,
		scale_divisor);

	int16_t index = 0;
	do {
		if (g_briefing_map_fg_marker_active[index] != 0) {
			++g_briefing_map_fg_marker_age[index];
		}
		++index;
	} while (index < 8);
	for (index = 0; index < 8; ++index) {
		if (g_briefing_map_label_active[index] != 0) {
			++g_briefing_map_label_age[index];
		}
	}
}

/* Plays the next script frame with its sounds while the current frame is
 * under duration_frames; otherwise starts the briefing over, with
 * g_briefing_last_narrated_text_block_idx and g_briefing_text_page_number at 0. */
// FUNCTION: XVT 0x4F7DD0
void briefing_map_update_script_playback_after_animation(void)
{
	if (g_briefing_script.current_frame <
	    g_briefing_script.duration_frames) {
		briefing_script_advance_frame(0);
	} else {
		g_briefing_last_narrated_text_block_idx = 0;
		g_briefing_text_page_number = 0;
		briefing_script_reset_state();
	}
}

/* Runs briefing_map_select_nearest_mission_point14_flight_group with
 * g_briefing_map_panel_rect as the viewport, unmoved from the screen's top left,
 * and returns 1. Ignores viewport_rect, clipRect and both button states. */
// FUNCTION: XVT 0x4F7E00
int16_t briefing_map_select_flight_group_at_cursor(
	const struct RECT *viewport_rect, const struct RECT *clip_rect,
	int left_down, int right_down, int16_t mouse_x, int16_t mouse_y)
{
	(void)viewport_rect;
	(void)clip_rect;
	(void)left_down;
	(void)right_down;

	struct RECT dst;
	frontend_draw_rect_copy(&dst, &g_briefing_map_panel_rect);
	briefing_map_select_nearest_mission_point14_flight_group(&dst, mouse_x,
								 mouse_y);
	return 1;
}

/* Draws the briefing map panel: g_briefing_map_panel_rect moved by
 * viewport_rect's top left. When text slot 1 is on, draws its block wrapped
 * in the panel's bottom 27 pixels, and counts a page whenever that block
 * differs from g_briefing_last_narrated_text_block_idx, storing it there. Then,
 * clipped to clipRect and to the panel less its bottom 28 pixels, draws the
 * grid, the overlays, and FRONTSTR_640_PAGE with g_briefing_text_page_number in
 * font 10, 60 pixels left of the map's right edge and 14 above its bottom.
 * Leaves the screen clip on the map and returns 1. Ignores highlight_phase,
 * and works out a 12-pixel title strip that it never draws. */
// FUNCTION: XVT 0x4F7E40
int16_t
briefing_map_draw_viewport_and_selection(const struct RECT *viewport_rect,
					 const struct RECT *clip_rect,
					 int16_t highlight_phase)
{
	(void)highlight_phase;

	struct RECT title_rect;
	frontend_draw_rect_copy(&title_rect, &g_briefing_map_panel_rect);
	frontend_draw_rect_offset_xy(&title_rect, viewport_rect->left,
				     viewport_rect->top);
	title_rect.bottom = title_rect.top + 12;

	struct RECT narration_rect;
	frontend_draw_rect_copy(&narration_rect, &g_briefing_map_panel_rect);
	frontend_draw_rect_offset_xy(&narration_rect, viewport_rect->left,
				     viewport_rect->top);
	narration_rect.top = narration_rect.bottom - 27;
	if (g_briefing_text_slot_active[1] != 0) {
		frontend_text_draw_formatted_wrapped_text(
			&narration_rect,
			(const uint8_t *)g_briefing_text_blocks
				[g_briefing_text_slot_block_idx[1]],
			0);
		if (g_briefing_last_narrated_text_block_idx !=
		    g_briefing_text_slot_block_idx[1]) {
			++g_briefing_text_page_number;
			g_briefing_last_narrated_text_block_idx =
				g_briefing_text_slot_block_idx[1];
		}
	}

	struct RECT map_viewport_rect;
	frontend_draw_rect_copy(&map_viewport_rect, &g_briefing_map_panel_rect);
	frontend_draw_rect_offset_xy(&map_viewport_rect, viewport_rect->left,
				     viewport_rect->top);
	map_viewport_rect.bottom -= 28;
	struct RECT clipped_rect;
	frontend_draw_rect_copy(&clipped_rect, clip_rect);
	frontend_display_set_screen_clip_rect640x480(&map_viewport_rect);
	frontend_draw_rect_clip_to_bounds(&clipped_rect);
	frontend_display_set_screen_clip_rect640x480(&clipped_rect);
	briefing_map_draw_grid(&map_viewport_rect, &clipped_rect);
	briefing_map_draw_overlays(&map_viewport_rect, &clipped_rect);

	sprintf(g_frontend_scratch_buffer, "%s %d",
		frontend_string_get(FRONTSTR_640_PAGE),
		g_briefing_text_page_number);
	frontend_text_draw(10, g_frontend_scratch_buffer,
			   map_viewport_rect.right - 60,
			   map_viewport_rect.bottom - 14, 0xFFFF);
	return 1;
}

/* Draws the map grid in viewport_rect: a line every 256 map units on each
 * axis, g_briefing_map_scale pixels apart. Lines at multiples of 1024 units are
 * drawn in frontend_display_pack_rgb(0x96, 0, 0). When g_briefing_map_scale.x is
 * 16 or more, the lines halfway between those are drawn too, in
 * frontend_display_pack_rgb(0x50, 0, 0), and at 32 or more every other line as
 * well. Ignores clipRect. */
// FUNCTION: XVT 0x4F7FD0
void briefing_map_draw_grid(const struct RECT *viewport_rect,
			    const struct RECT *clip_rect)
{
	(void)clip_rect;

	int16_t major_color = (int16_t)frontend_display_pack_rgb(0x96, 0, 0);
	int16_t minor_color = (int16_t)frontend_display_pack_rgb(0x50, 0, 0);
	struct RECT dst;
	frontend_draw_rect_copy(&dst, viewport_rect);

	int16_t x_grid_index = g_briefing_map_center.x / 256;
	if (g_briefing_map_center.x > 0 &&
	    (uint8_t)g_briefing_map_center.x != 0) {
		++x_grid_index;
	}
	int center_remainder = -g_briefing_map_center.x;
	center_remainder &= 0xFF;
	int16_t grid_start_x =
		(int16_t)(dst.left + ((dst.right - dst.left) >> 1) +
			  ((g_briefing_map_scale.x * center_remainder) >> 8));
	while (grid_start_x > dst.left) {
		grid_start_x = (int16_t)(grid_start_x - g_briefing_map_scale.x);
		--x_grid_index;
	}

	int16_t y_grid_index = g_briefing_map_center.y / 256;
	if (g_briefing_map_center.y > 0 &&
	    (uint8_t)g_briefing_map_center.y != 0) {
		++y_grid_index;
	}
	center_remainder = -g_briefing_map_center.y;
	center_remainder &= 0xFF;
	int16_t grid_start_y =
		(int16_t)(dst.top + ((dst.bottom - dst.top) >> 1) +
			  ((g_briefing_map_scale.y * center_remainder) >> 8));
	while (grid_start_y > dst.top) {
		grid_start_y = (int16_t)(grid_start_y - g_briefing_map_scale.y);
		--y_grid_index;
	}

	int16_t draw_x = grid_start_x;
	int16_t draw_y = grid_start_y;
	int16_t x_phase = x_grid_index;
	int16_t y_phase = y_grid_index;
	if (g_briefing_map_scale.x >= 16) {
		int16_t draw_all_minor_lines = g_briefing_map_scale.x >= 32;
		if (grid_start_x < dst.right) {
			do {
				if ((x_phase & 3) != 0 &&
				    ((x_phase & 3) == 2 ||
				     draw_all_minor_lines)) {
					frontend_draw_vertical_line_clipped(
						dst.top, dst.bottom, draw_x,
						minor_color);
				}
				draw_x = (int16_t)(draw_x +
						   g_briefing_map_scale.x);
				++x_phase;
			} while (draw_x < dst.right);
		}
		if (grid_start_y < dst.bottom) {
			do {
				if ((y_phase & 3) != 0 &&
				    ((y_phase & 3) == 2 ||
				     draw_all_minor_lines)) {
					frontend_draw_horizontal_line_clipped(
						dst.left, dst.right, draw_y,
						minor_color);
				}
				draw_y = (int16_t)(draw_y +
						   g_briefing_map_scale.y);
				++y_phase;
			} while (draw_y < dst.bottom);
		}
		draw_x = grid_start_x;
		draw_y = grid_start_y;
		x_phase = x_grid_index;
		y_phase = y_grid_index;
	}

	if (grid_start_x < dst.right) {
		do {
			if ((x_phase & 3) == 0) {
				frontend_draw_vertical_line_clipped(
					dst.top, dst.bottom, draw_x,
					major_color);
			}
			draw_x = (int16_t)(draw_x + g_briefing_map_scale.x);
			++x_phase;
		} while (draw_x < dst.right);
	}
	if (grid_start_y < dst.bottom) {
		do {
			if ((y_phase & 3) == 0) {
				frontend_draw_horizontal_line_clipped(
					dst.left, dst.right, draw_y,
					major_color);
			}
			draw_y = (int16_t)(draw_y + g_briefing_map_scale.y);
			++y_phase;
		} while (draw_y < dst.bottom);
	}
}

/* Draws the briefing map's overlays in viewport_rect. First the highlight of
 * every active flight group marker, drawn by its age; then every active label,
 * its text with '[' made text code 2 (the second text color) and ']' code 1
 * (back to the label's color), revealed by its age at its projected point. Then
 * the icon of every flight group whose mission point 14 + g_active_briefing_index
 * is set and whose craft type is not negative, centered there, from "mapicon0"
 * to "mapicon4" by IFF: IFF 0 to 3 give 0 to 3, IFF 4 gives 1, IFF 5 gives 4,
 * and any other gives 0 in the modern build and an unset value in the original.
 * Player flight groups of the pilot's team also get a number, counting from 1,
 * in font 10 at the icon's lower right. The point it projects for each marker
 * goes unused. */
// FUNCTION: XVT 0x4F82A0
void briefing_map_draw_overlays(const struct RECT *viewport_rect,
				const struct RECT *clip_rect)
{
	struct RECT map_rect;

	frontend_draw_rect_copy(&map_rect, viewport_rect);
	int16_t index = 0;
	int16_t projected_x;
	int16_t projected_y;
	do {
		if (g_briefing_map_fg_marker_active[index] != 0) {
			int16_t flight_group_idx =
				g_briefing_map_fg_marker_flight_group_idx
					[index];
			briefing_map_project_point_to_viewport(
				&map_rect,
				g_frontend_mission
					.flight_groups[flight_group_idx]
					.mission_point_x[14],
				g_frontend_mission
					.flight_groups[flight_group_idx]
					.mission_point_y[14],
				&projected_x, &projected_y);
			briefing_map_draw_craft_icon_highlight(
				viewport_rect, clip_rect, flight_group_idx,
				g_briefing_map_fg_marker_age[index]);
		}
		++index;
	} while (index < 8);

	char text[40];
	for (index = 0; index < 8; ++index) {
		if (g_briefing_map_label_active[index] != 0) {
			int16_t text_index =
				g_briefing_map_label_text_idx[index];
			briefing_map_project_point_to_viewport(
				&map_rect, g_briefing_map_label_x[index],
				g_briefing_map_label_y[index], &projected_x,
				&projected_y);
			strcpy(text, g_briefing_map_label_texts[text_index]);
			for (int16_t character_index = 0;
			     text[character_index] != '\0'; ++character_index) {
				if (text[character_index] == '[') {
					text[character_index] = 2;
				}
				if (text[character_index] == ']') {
					text[character_index] = 1;
				}
			}
			briefing_map_draw_revealed_label_if_active(
				text, 1, projected_x, projected_y,
				g_briefing_map_label_age[index],
				g_briefing_map_label_style[index]);
		}
	}

	int player_icon_number = 1;
	for (index = 0; index < (int16_t)g_frontend_mission.flight_group_count;
	     ++index) {
		int16_t craft_type =
			g_frontend_mission.flight_groups[index].craft_type;
		int mission_point_index = g_active_briefing_index + 14;
		int16_t map_x = g_frontend_mission.flight_groups[index]
					.mission_point_x[mission_point_index];
		int16_t map_y = g_frontend_mission.flight_groups[index]
					.mission_point_y[mission_point_index];
		if (g_frontend_mission.flight_groups[index]
			    .mission_point_enabled[mission_point_index] != 0) {
			int16_t icon_color_index;

#ifdef XVT_MODERN
			icon_color_index = 0;
#endif
			switch (g_frontend_mission.flight_groups[index].iff) {
			case 0:
				icon_color_index = 0;
				break;
			case 1:
				icon_color_index = 1;
				break;
			case 2:
				icon_color_index = 2;
				break;
			case 3:
				icon_color_index = 3;
				break;
			case 4:
				icon_color_index = 1;
				break;
			case 5:
				icon_color_index = 4;
				break;
			default:
				break;
			}

			if (craft_type >= 0) {
				int icon_index =
					g_map_icon_by_craft_type[craft_type];
				struct RECT *icon_rect =
					&g_map_icon_rects[icon_index];
				int icon_width =
					icon_rect->right - icon_rect->left + 1;
				int icon_height =
					icon_rect->bottom - icon_rect->top + 1;
				briefing_map_project_point_to_viewport(
					&map_rect, map_x, map_y, &projected_x,
					&projected_y);
				projected_x = (int16_t)(projected_x -
							(icon_width >> 1));
				projected_y = (int16_t)(projected_y -
							(icon_height >> 1));
				sprintf(g_frontend_scratch_buffer, "mapicon%d",
					icon_color_index);
				front_image_draw_sprite_rect_transparent(
					g_frontend_scratch_buffer, icon_rect,
					projected_x, projected_y);

				if (g_frontend_mission.flight_groups[index]
						    .player_number != 0 &&
				    g_frontend_mission.flight_groups[index]
						    .team ==
					    g_pilot_data.team) {
					projected_x = (int16_t)(projected_x +
								icon_width);
					projected_y = (int16_t)(projected_y +
								icon_height);
					sprintf(g_frontend_scratch_buffer, "%u",
						player_icon_number);
					frontend_text_draw(
						10, g_frontend_scratch_buffer,
						projected_x, projected_y,
						0xFFFF);
					++player_icon_number;
				}
			}
		}
	}
}

/* Draws a label through briefing_map_draw_revealed_label with twice reveal_count
 * characters revealed, when reveal_count is 0 or more. */
// FUNCTION: XVT 0x4F8910
void briefing_map_draw_revealed_label_if_active(const char *text,
						int16_t color_ramp_group,
						int16_t x, int16_t y,
						int16_t reveal_count,
						int16_t shade_group)
{
	if (reveal_count >= 0) {
		briefing_map_draw_revealed_label(text, color_ramp_group, x, y,
						 (int16_t)(2 * reveal_count),
						 shade_group);
	}
}

/* Draws text at (x, y) in font 10, typing itself out, in the shades of row
 * shade_group of g_text_shade_ramps (shade 0 darkest, 7 brightest). While
 * reveal_count is under the text's length + 2, it shows the first reveal_count
 * characters, at most all of them: it draws them, then each shorter prefix,
 * one character less each time, one shade brighter, up to shade 6, so the
 * newest characters are darkest. The first of those draws uses shade 4, 2 or
 * 0 for reveal_count 1, 2, or 3 and more. A small filled block in shade 7
 * follows the text while characters are still hidden. From reveal_count at
 * length + 2 on, it draws the whole text once, in shade 7, 6 and 5 for length
 * + 2, + 3 and + 4, and shade 4 after. Ignores color_ramp_group; does not check
 * shade_group or the text's length against its 64-byte copy. */
// FUNCTION: XVT 0x4F8950
void briefing_map_draw_revealed_label(const char *text,
				      int16_t color_ramp_group, int16_t x,
				      int16_t y, int16_t reveal_count,
				      int16_t shade_group)
{
	(void)color_ramp_group;

	if (reveal_count < 0) {
		return;
	}

	int16_t text_length = (int16_t)strlen(text);
	char visible_text[64];
	strcpy(visible_text, text);
	int16_t shade_base = (int16_t)(8 * shade_group);
	if (text_length + 2 > reveal_count) {
		int16_t visible_count;
		if (text_length >= reveal_count) {
			visible_text[reveal_count] = '\0';
			visible_count = reveal_count;
		} else {
			visible_count = text_length;
		}
		int16_t reveal_phase = reveal_count;
		if (reveal_phase > 3) {
			reveal_phase = 3;
		}
		int16_t shade_index =
			(int16_t)(shade_base + 2 * (3 - reveal_phase));
		int16_t text_width =
			(int16_t)frontend_text_measure_width(visible_text, 10);
		while (shade_index <= shade_base + 6 && visible_count > 0) {
			int shade_color = g_text_shade_ramps[0][shade_index++];
			int visible_index = visible_count--;
			visible_text[visible_index] = '\0';
			frontend_text_draw(10, visible_text, x, y, shade_color);
		}
		struct RECT rect;
		frontend_draw_rect_assign(&rect, x + text_width + 2, y,
					  x + text_width + 8, y + 6);
		if (text_length > reveal_count) {
			frontend_draw_rect(
				&rect, 0, 0,
				g_text_shade_ramps[0][shade_base + 7], 1);
		}
	} else {
		strcpy(visible_text, text);
		int16_t final_shade_index;
		if (text_length + 5 > reveal_count) {
			final_shade_index =
				(int16_t)(text_length - reveal_count +
					  shade_base + 9);
		} else {
			final_shade_index = (int16_t)(shade_base + 4);
		}
		frontend_text_draw(10, visible_text, x, y,
				   g_text_shade_ramps[0][final_shade_index]);
	}
}

/* Draws the highlight around flight group flight_group_index's map icon at
 * mission point 14 + g_active_briefing_index, in a row of g_text_shade_ramps picked
 * by IFF: 0 and any IFF above 5 green, 1 and 4 red, 2 blue, 3 yellow, 5 purple.
 * While highlight_phase is under 12 it draws tinted copies of the icon from the
 * "greyicon" image at the four diagonal offsets, closing in as highlight_phase
 * rises; at 8 to 11 it also draws a filled, outlined box that grows from 3
 * pixels inside the icon's edges to them. From 12 on it draws only a filled,
 * outlined box 2 pixels outside the icon. Does nothing when the craft type is
 * negative; ignores clipRect. */
// FUNCTION: XVT 0x4F8B30
void briefing_map_draw_craft_icon_highlight(const struct RECT *viewport_rect,
					    const struct RECT *clip_rect,
					    int flight_group_index,
					    int highlight_phase)
{
	(void)clip_rect;

	int flight_group_idx = (int16_t)flight_group_index;
	int16_t craft_type =
		g_frontend_mission.flight_groups[flight_group_idx].craft_type;
	int16_t map_x = g_frontend_mission.flight_groups[flight_group_idx]
				.mission_point_x[g_active_briefing_index + 14];
	int16_t map_y = g_frontend_mission.flight_groups[flight_group_idx]
				.mission_point_y[g_active_briefing_index + 14];
	int16_t color_base;
	switch (g_frontend_mission.flight_groups[flight_group_idx].iff) {
	case 0:
		color_base = 0;
		break;
	case 1:
	case 4:
		color_base = 8;
		break;
	case 2:
		color_base = 24;
		break;
	case 3:
		color_base = 16;
		break;
	case 5:
		color_base = 32;
		break;
	default:
		color_base = 0;
		break;
	}

	if (craft_type < 0) {
		return;
	}

	int16_t screen_x;
	int16_t screen_y;
	briefing_map_project_point_to_viewport(viewport_rect, map_x, map_y,
					       &screen_x, &screen_y);
	int map_icon_index = g_map_icon_by_craft_type[craft_type];
	int icon_width = g_map_icon_rects[map_icon_index].right -
			 g_map_icon_rects[map_icon_index].left + 1;
	int icon_height = g_map_icon_rects[map_icon_index].bottom -
			  g_map_icon_rects[map_icon_index].top + 1;
	screen_x = (int16_t)(screen_x - (icon_width >> 1));
	screen_y = (int16_t)(screen_y - (icon_height >> 1));
	struct RECT rect;
	frontend_draw_rect_assign(&rect, screen_x, screen_y,
				  screen_x + icon_width - 1,
				  screen_y + icon_height - 1);

	if ((int16_t)highlight_phase < 12) {
		int16_t count;
		int16_t shade_index;
		int16_t offset;

		if ((int16_t)highlight_phase < 4) {
			shade_index =
				(int16_t)(color_base - 2 * highlight_phase + 7);
			offset = 16;
			count = (int16_t)(highlight_phase + 1);
		} else if ((int16_t)highlight_phase < 8) {
			shade_index = (int16_t)(color_base + 1);
			offset = (int16_t)(2 * (11 - highlight_phase));
			count = 4;
		} else {
			shade_index = (int16_t)(color_base + 1);
			offset = (int16_t)(2 * (11 - highlight_phase));
			count = (int16_t)(12 - highlight_phase);
		}

		if ((int16_t)highlight_phase >= 8) {
			int inset = 11 - (int16_t)highlight_phase;
			frontend_draw_rect_inset_xy(&rect, inset, inset);
			frontend_draw_rect(
				&rect, 0, 0,
				g_text_shade_ramps[0][color_base + 2], 1);
			frontend_draw_rect_outline(
				&rect, 0, 0,
				g_text_shade_ramps[0][color_base +
						      (int16_t)highlight_phase -
						      6]);
		}

		if (count > 0) {
			int16_t repeat_count = count;
			do {
				int tint_index = shade_index;
				int draw_offset = (int16_t)offset;
				shade_index = (int16_t)(shade_index + 2);
				int *tint_color =
					&g_text_shade_ramps[0][tint_index];
				front_image_draw_sprite_rect_tinted(
					"greyicon",
					&g_map_icon_rects[map_icon_index],
					screen_x - draw_offset,
					screen_y - draw_offset, *tint_color);
				front_image_draw_sprite_rect_tinted(
					"greyicon",
					&g_map_icon_rects[map_icon_index],
					screen_x + draw_offset,
					screen_y - draw_offset, *tint_color);
				front_image_draw_sprite_rect_tinted(
					"greyicon",
					&g_map_icon_rects[map_icon_index],
					screen_x - draw_offset,
					screen_y + draw_offset, *tint_color);
				front_image_draw_sprite_rect_tinted(
					"greyicon",
					&g_map_icon_rects[map_icon_index],
					screen_x + draw_offset,
					screen_y + draw_offset, *tint_color);
				offset = (int16_t)(offset - 2);
			} while (--repeat_count != 0);
		}
	} else {
		frontend_draw_rect_inset_xy(&rect, -2, -2);
		frontend_draw_rect(&rect, 0, 0,
				   g_text_shade_ramps[0][color_base + 2], 1);
		frontend_draw_rect_outline(
			&rect, 0, 0, g_text_shade_ramps[0][color_base + 6]);
	}
}
