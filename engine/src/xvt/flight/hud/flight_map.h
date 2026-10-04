#ifndef XVT_FLIGHT_HUD_FLIGHT_MAP_H
#define XVT_FLIGHT_HUD_FLIGHT_MAP_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t g_flight_icons640_frame_by_object_type[106];
extern const uint8_t g_flight_icons640_width_by_frame[72];
extern const uint8_t g_flight_icons640_height_by_frame[72];
extern const uint8_t g_flight_map_icons480x360_frame_by_object_type[106];
extern const uint8_t g_flight_map_icons480x360_width_by_frame[72];
extern const uint8_t g_flight_map_icons480x360_height_by_frame[72];
extern const uint8_t g_flight_map_icons320x240_frame_by_object_type[106];
extern const uint8_t g_flight_map_icons320x240_width_by_frame[72];
extern const uint8_t g_flight_map_icons320x240_height_by_frame[72];
extern const char g_flight_map_icons320x240_resource_path[22];
extern const char g_flight_map_icons480x360_resource_path[22];
extern const char g_flight_icons640x480_resource_path[22];
extern int g_flight_icon_frame_count;
extern const char *g_flight_icon_resource_path;
extern uint8_t **g_flight_icon_frames;

/* Returns one easing step toward delta for flight_map_update_camera: half of
 * delta, but at least 16 and at most 4,096 in size, and never past
 * delta. */
static __inline int flight_map_compute_aim_step(int delta)
{
	int step;

	step = delta / 2;
	if (step > 0) {
		if (step > 4096) {
			step = 4096;
		}
		if (step < 16) {
			step = 16;
		}
		if (step > delta) {
			step = delta;
		}
	} else {
		if (step < -4096) {
			step = -4096;
		}
		if (step > -16) {
			step = -16;
		}
		if (step < delta) {
			step = delta;
		}
	}
	return step;
}

typedef enum map_room_string_id {
	MAP_ROOM_STR_HELP_KEY = 0x0,
	MAP_ROOM_STR_SLASH = 0x1,
	MAP_ROOM_STR_FOLLOW_MARKER = 0x2,
	MAP_ROOM_STR_PLUS = 0x3,
	MAP_ROOM_STR_MINUS = 0x4,
	MAP_ROOM_STR_ZOOM_KEY = 0x5,
	MAP_ROOM_STR_MODE_KEY = 0x6,
	MAP_ROOM_STR_CENTER_KEY = 0x7,
	MAP_ROOM_STR_SPACEBAR = 0x8,
	MAP_ROOM_STR_HELP_DESCRIPTION = 0x9,
	MAP_ROOM_STR_FOLLOW_TARGET_ON = 0xA,
	MAP_ROOM_STR_FOLLOW_TARGET_OFF = 0xB,
	MAP_ROOM_STR_TARGET_TRACKING_ON = 0xC,
	MAP_ROOM_STR_TARGET_TRACKING_OFF = 0xD,
	MAP_ROOM_STR_INSTANT_ZOOM = 0xE,
	MAP_ROOM_STR_RETURN_TO_COMBAT = 0xF,
	MAP_ROOM_STR_CENTER_TARGET_DESCRIPTION = 0x10,
	MAP_ROOM_STR_TOGGLE_MODE_DESCRIPTION = 0x11,
	MAP_ROOM_STR_FOLLOWING = 0x12,
	MAP_ROOM_STR_TRACKING = 0x13,
} map_room_string_id;

void flight_map_render_view(void);
void flight_map_update_camera(int player_idx);
void flight_map_build_render_list(void);
void flight_map_draw_object_pass(int draw_above_grid_plane);
void flight_map_draw_other_player_object_box(int object_idx);
void flight_map_draw_object_overlay(int object_idx);
void flight_map_draw_object_icon_at_view_pos(int object_idx, int view_x,
					     int view_y, int view_z);
void flight_map_draw_object_box_corners(int x, int y, int width, int height,
					unsigned int color_index);
void flight_map_draw_grid(void);
void flight_map_render_view_end_stub(void);
int flight_map_pick_object_nearest_screen_center(int player_idx);

#ifdef __cplusplus
}
#endif

#endif
