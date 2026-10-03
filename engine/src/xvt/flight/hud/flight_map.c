#include "xvt/flight/hud/flight_map.h"

#include "xvt/assets/object_type.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/flight_view.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/proving_grounds.h"
#include "xvt/flight/targeting.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/math.h"
#include "xvt/math/trig2.h"
#include "xvt/render/flight_light.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_list.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/scene_billboard.h"
#include "xvt/render/sw3d.h"
#include <string.h>

/* Icon frame for each object type, 0 to 105, in the 640x480 icon set
 * (RESOURCE\icons640.ico); flight_map_draw_object_icon_at_view_pos uses frame 19
 * for higher types. The modern build's map capture reads it too. */
// GLOBAL: XVT 0x521240
const uint8_t g_flight_icons640_frame_by_object_type[106] = {
	0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x00, 0x00,
	0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14,
	0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x00, 0x1C, 0x1D, 0x1E, 0x1F,
	0x41, 0x20, 0x21, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29,
	0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x40, 0x30, 0x31, 0x32, 0x33, 0x34,
	0x35, 0x35, 0x35, 0x35, 0x35, 0x35, 0x3F, 0x3D, 0x3E, 0x36, 0x37, 0x37,
	0x37, 0x37, 0x37, 0x38, 0x39, 0x3A, 0x42, 0x3C, 0x3B, 0x3B, 0x3B, 0x3C,
	0x3C, 0x3A, 0x3D, 0x3D, 0x00, 0x00, 0x43, 0x44, 0x45, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x3D, 0x3D, 0x3D, 0x3D, 0x3D, 0x3D,
};
/* Width in pixels of each frame of the 640x480 icon set. */
// GLOBAL: XVT 0x5212B0
const uint8_t g_flight_icons640_width_by_frame[72] = {
	8,  8, 7, 11, 8, 8, 11, 7, 10, 11, 7,  7,  8,  11, 11, 11, 7, 7,
	6,  8, 8, 4,  5, 6, 9,	4, 9,  6,  10, 9,  9,  9,  10, 9,  7, 7,
	5,  9, 7, 6,  7, 7, 8,	9, 7,  7,  9,  11, 10, 6,  9,  7,  6, 14,
	17, 9, 9, 9,  6, 6, 7,	7, 7,  14, 9,  5,  4,  15, 14, 9,  0, 0,
};
/* Height in pixels of each frame of the 640x480 icon set. */
// GLOBAL: XVT 0x5212F8
const uint8_t g_flight_icons640_height_by_frame[72] = {
	12, 13, 10, 10, 9,  9,	10, 11, 10, 10, 13, 11, 10, 9,	9,  11, 14, 13,
	11, 13, 13, 6,	8,  11, 9,  9,	9,  8,	15, 12, 13, 16, 11, 12, 15, 15,
	17, 17, 18, 16, 17, 17, 17, 20, 17, 15, 17, 21, 7,  7,	6,  9,	9,  14,
	15, 6,	8,  8,	3,  6,	10, 7,	7,  17, 21, 14, 9,  14, 14, 15, 0,  0,
};
/* Icon frame for each object type, 0 to 105, in the 480x360 icon set
 * (RESOURCE\mapicons.ico); the same values as the 320x240 table. */
// GLOBAL: XVT 0x521340
const uint8_t g_flight_map_icons480x360_frame_by_object_type[106] = {
	0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x00, 0x00,
	0x32, 0x33, 0x09, 0x34, 0x0A, 0x0B, 0x0C, 0x0D, 0x35, 0x0E, 0x0F, 0x36,
	0x10, 0x37, 0x11, 0x12, 0x13, 0x14, 0x15, 0x00, 0x16, 0x17, 0x18, 0x19,
	0x43, 0x38, 0x1A, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x39, 0x3A, 0x3B, 0x1F,
	0x3C, 0x20, 0x21, 0x22, 0x23, 0x24, 0x42, 0x25, 0x3D, 0x3E, 0x3F, 0x40,
	0x26, 0x26, 0x26, 0x26, 0x26, 0x26, 0x26, 0x26, 0x26, 0x41, 0x27, 0x28,
	0x28, 0x28, 0x28, 0x29, 0x2A, 0x2B, 0x44, 0x2B, 0x2C, 0x2D, 0x2E, 0x2E,
	0x2E, 0x2F, 0x30, 0x31, 0x00, 0x00, 0x45, 0x46, 0x47, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
};
/* Width in pixels of each frame of the 480x360 icon set. */
// GLOBAL: XVT 0x5213B0
const uint8_t g_flight_map_icons480x360_width_by_frame[72] = {
	5, 5, 5,  5, 5, 5, 7, 5, 5, 5, 9, 5, 5, 5, 3, 5, 5, 4, 6, 3, 5, 4, 4, 5,
	5, 5, 7,  3, 3, 3, 7, 5, 5, 5, 5, 5, 5, 6, 9, 5, 5, 7, 7, 4, 3, 4, 5, 3,
	4, 4, 12, 9, 8, 5, 8, 7, 8, 7, 3, 3, 5, 8, 8, 7, 7, 5, 5, 3, 2, 7, 7, 5,
};
/* Height in pixels of each frame of the 480x360 icon set. */
// GLOBAL: XVT 0x5213F8
const uint8_t g_flight_map_icons480x360_height_by_frame[72] = {
	5, 6, 4, 6, 5, 5, 5,  5, 5, 5, 5, 5, 5,	 6, 6, 6, 5, 6,
	4, 6, 5, 6, 7, 7, 7,  6, 6, 6, 6, 7, 7,	 7, 8, 9, 5, 7,
	7, 4, 6, 5, 5, 7, 7,  8, 7, 6, 7, 4, 4,	 4, 4, 5, 8, 6,
	4, 5, 6, 5, 8, 8, 10, 4, 4, 4, 6, 7, 10, 7, 4, 7, 7, 8,
};
/* Icon frame for each object type, 0 to 105, in the 320x240 icon set
 * (RESOURCE\mapicons.ico). */
// GLOBAL: XVT 0x521440
const uint8_t g_flight_map_icons320x240_frame_by_object_type[106] = {
	0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x00, 0x00,
	0x32, 0x33, 0x09, 0x34, 0x0A, 0x0B, 0x0C, 0x0D, 0x35, 0x0E, 0x0F, 0x36,
	0x10, 0x37, 0x11, 0x12, 0x13, 0x14, 0x15, 0x00, 0x16, 0x17, 0x18, 0x19,
	0x43, 0x38, 0x1A, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x39, 0x3A, 0x3B, 0x1F,
	0x3C, 0x20, 0x21, 0x22, 0x23, 0x24, 0x42, 0x25, 0x3D, 0x3E, 0x3F, 0x40,
	0x26, 0x26, 0x26, 0x26, 0x26, 0x26, 0x26, 0x26, 0x26, 0x41, 0x27, 0x28,
	0x28, 0x28, 0x28, 0x29, 0x2A, 0x2B, 0x44, 0x2B, 0x2C, 0x2D, 0x2E, 0x2E,
	0x2E, 0x2F, 0x30, 0x31, 0x00, 0x00, 0x45, 0x46, 0x47, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
};
/* Width in pixels of each frame of the 320x240 icon set. */
// GLOBAL: XVT 0x5214B0
const uint8_t g_flight_map_icons320x240_width_by_frame[72] = {
	5, 5, 5,  5, 5, 5, 7, 5, 5, 5, 9, 5, 5, 5, 3, 5, 5, 4, 6, 3, 5, 4, 4, 5,
	5, 5, 7,  3, 3, 3, 7, 5, 5, 5, 5, 5, 5, 6, 9, 5, 5, 7, 7, 4, 3, 4, 5, 3,
	4, 4, 12, 9, 8, 5, 8, 7, 8, 7, 3, 3, 5, 8, 8, 7, 7, 5, 5, 3, 2, 7, 7, 5,
};
/* Height in pixels of each frame of the 320x240 icon set. */
// GLOBAL: XVT 0x5214F8
const uint8_t g_flight_map_icons320x240_height_by_frame[72] = {
	5, 6, 4, 6, 5, 5, 5,  5, 5, 5, 5, 5, 5,	 6, 6, 6, 5, 6,
	4, 6, 5, 6, 7, 7, 7,  6, 6, 6, 6, 7, 7,	 7, 8, 9, 5, 7,
	7, 4, 6, 5, 5, 7, 7,  8, 7, 6, 7, 4, 4,	 4, 4, 5, 8, 6,
	4, 5, 6, 5, 8, 8, 10, 4, 4, 4, 6, 7, 10, 7, 4, 7, 7, 8,
};
/* Icon file of the 320x240 flight resolution; fe_disk_io_init_global_buffers
 * points g_flight_icon_resource_path at it. */
// GLOBAL: XVT 0x523588
const char g_flight_map_icons320x240_resource_path[22] =
	"RESOURCE\\mapicons.ico";
/* Icon file of the 480x360 flight resolution, the same file as at 320x240;
 * fe_disk_io_init_global_buffers points g_flight_icon_resource_path at it. */
// GLOBAL: XVT 0x5235A0
const char g_flight_map_icons480x360_resource_path[22] =
	"RESOURCE\\mapicons.ico";
/* Icon file of the 640x480 flight resolution; fe_disk_io_init_global_buffers
 * points g_flight_icon_resource_path at it. */
// GLOBAL: XVT 0x5235B8
const char g_flight_icons640x480_resource_path[22] = "RESOURCE\\icons640.ico";
/* Icon frames flight_icon_load_frames loaded, in four color groups of a
 * quarter each, picked by IFF; fe_disk_io_init_global_buffers sets it. */
// GLOBAL: XVT 0x9A8E38
int g_flight_icon_frame_count = 0;
/* The icon file in use, one of the three paths above, picked by
 * fe_disk_io_init_global_buffers from g_flight_resolution_mode; NULL before.
 * flight_map_draw_object_icon_at_view_pos compares the pointer to pick its size
 * tables. */
// GLOBAL: XVT 0x9EC478
const char *g_flight_icon_resource_path = NULL;
/* The loaded icons: a table of frame pointers, then the frame data, in the
 * locked memory of g_flight_icon_frames_handle; fe_disk_io_init_global_buffers
 * sets it. */
// GLOBAL: XVT 0x9ECC4C
uint8_t **g_flight_icon_frames = NULL;

/* Draws the map view for the local player: clips text to the flight
 * viewport, builds and sorts the render list (flight_map_build_render_list,
 * render_list_sort_depth_descending), then draws the objects beyond the grid
 * plane (world z -65,536), the grid, and the objects on the camera's side;
 * the camera's height picks which side is beyond. Sets
 * g_render_scene_reset_pending. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x435BC0
void flight_map_render_view(void)
{
	enum { MAP_GRID_PLANE_Z = -65536 };

	flight_text_set_clip_rect(
		(int16_t)g_flight_vp_x, (int16_t)g_flight_vp_y,
		(int16_t)(g_flight_vp_x + g_flight_vp_width),
		(int16_t)(g_flight_vp_y + g_flight_vp_height));
	flight_map_build_render_list();
	render_list_sort_depth_descending();
	g_render_scene_reset_pending = 1;
	if (g_players[g_local_player].view_state.camera_world_z <
	    MAP_GRID_PLANE_Z) {
		flight_map_draw_object_pass(1);
		flight_map_draw_grid();
		flight_map_draw_object_pass(0);
	} else {
		flight_map_draw_object_pass(0);
		flight_map_draw_grid();
		flight_map_draw_object_pass(1);
	}
	flight_map_render_view_end_stub();
}

/* Places the map camera of player_idx in its viewState. With map_camera_state
 * above 1: a camera with a focus object takes the object's angles and
 * position and, unless g_flight_sim_side_effects_suppressed is set, eases
 * hud_aim_y toward 0 (flight_map_compute_aim_step) and hud_aim_x toward 0, or,
 * with bit 0x80 of map_camera_state set, sets roll 0, pitch 0x4000 and yaw 0
 * and eases hud_aim_x toward -16,384; with no focus it sets roll 0, pitch
 * 0x4000, yaw 0, hud_aim_y 0 and hud_aim_x to the low 7 bits of map_camera_state
 * shifted left 14 and divided by -127. With map_camera_state 1: it takes the
 * focus object's angles and position, or with none folds any hud aim into
 * the view's pitch and yaw. Either way it builds the camera orientation
 * (fview_build_camera_orient) and moves a focused camera back by
 * camera_distance along camera matrix row 2 (g_camMatR2_*); with
 * map_camera_state 1 and an aim_target_idx it then turns the view to that
 * object. Writes g_worldLoc* and the trig2 outputs. */
// FUNCTION: XVT 0x435C60
void flight_map_update_camera(int player_idx)
{
	if (g_players[player_idx].map_camera_state > 1) {
		if (g_players[player_idx].view_state.camera_focus_obj_idx !=
		    UINT16_MAX) {
			g_players[player_idx].view_state.view_roll =
				g_object_table[g_players[player_idx]
						       .view_state
						       .camera_focus_obj_idx]
					.roll;
			g_players[player_idx].view_state.view_pitch =
				g_object_table[g_players[player_idx]
						       .view_state
						       .camera_focus_obj_idx]
					.pitch;
			g_players[player_idx].view_state.view_yaw =
				g_object_table[g_players[player_idx]
						       .view_state
						       .camera_focus_obj_idx]
					.yaw;
			mission_resolve_object_or_mission_point_world_loc(
				g_players[player_idx]
					.view_state.camera_focus_obj_idx,
				0);
			g_players[player_idx].view_state.camera_world_x =
				g_world_loc_x;
			g_players[player_idx].view_state.camera_world_y =
				g_world_loc_y;
			g_players[player_idx].view_state.camera_world_z =
				g_world_loc_z;
			if (g_flight_sim_side_effects_suppressed == 0) {
				g_players[player_idx].view_state.hud_aim_y +=
					flight_map_compute_aim_step(
						-g_players[player_idx]
							 .view_state.hud_aim_y);
				if ((g_players[player_idx].map_camera_state &
				     0x80) != 0) {
					g_players[player_idx]
						.view_state.view_roll = 0;
					g_players[player_idx]
						.view_state.view_pitch = 0x4000;
					g_players[player_idx]
						.view_state.view_yaw = 0;
					g_players[player_idx]
						.view_state.hud_aim_x +=
						flight_map_compute_aim_step(
							-16384 -
							g_players[player_idx]
								.view_state
								.hud_aim_x);
				} else {
					g_players[player_idx]
						.view_state.hud_aim_x +=
						flight_map_compute_aim_step(
							-g_players[player_idx]
								 .view_state
								 .hud_aim_x);
				}
			}
		} else {
			g_players[player_idx].view_state.view_roll = 0;
			g_players[player_idx].view_state.view_pitch = 0x4000;
			g_players[player_idx].view_state.view_yaw = 0;
			g_players[player_idx].view_state.hud_aim_y = 0;
			g_players[player_idx].view_state.hud_aim_x =
				(int16_t)(((g_players[player_idx]
						    .map_camera_state &
					    0x7F)
					   << 14) /
					  -127);
		}
		fview_build_camera_orient(
			g_players[player_idx].view_state.view_roll,
			g_players[player_idx].view_state.view_pitch,
			g_players[player_idx].view_state.view_yaw, 0,
			g_players[player_idx].view_state.hud_aim_x,
			g_players[player_idx].view_state.hud_aim_y, NULL);
		if (g_players[player_idx].view_state.camera_focus_obj_idx !=
		    UINT16_MAX) {
			g_players[player_idx].view_state.camera_world_x -=
				math_mul_q15(
					g_players[player_idx]
						.view_state.camera_distance,
					g_cam_mat_r2_x);
			g_players[player_idx].view_state.camera_world_y -=
				math_mul_q15(
					g_players[player_idx]
						.view_state.camera_distance,
					g_cam_mat_r2_y);
			g_players[player_idx].view_state.camera_world_z -=
				math_mul_q15(
					g_players[player_idx]
						.view_state.camera_distance,
					g_cam_mat_r2_z);
		}
		return;
	}

	if (g_players[player_idx].view_state.camera_focus_obj_idx !=
	    UINT16_MAX) {
		g_players[player_idx].view_state.view_roll =
			g_object_table[g_players[player_idx]
					       .view_state.camera_focus_obj_idx]
				.roll;
		g_players[player_idx].view_state.view_pitch =
			g_object_table[g_players[player_idx]
					       .view_state.camera_focus_obj_idx]
				.pitch;
		g_players[player_idx].view_state.view_yaw =
			g_object_table[g_players[player_idx]
					       .view_state.camera_focus_obj_idx]
				.yaw;
		mission_resolve_object_or_mission_point_world_loc(
			g_players[player_idx].view_state.camera_focus_obj_idx,
			0);
		g_players[player_idx].view_state.camera_world_x = g_world_loc_x;
		g_players[player_idx].view_state.camera_world_y = g_world_loc_y;
		g_players[player_idx].view_state.camera_world_z = g_world_loc_z;
		fview_build_camera_orient(
			g_players[player_idx].view_state.view_roll,
			g_players[player_idx].view_state.view_pitch,
			g_players[player_idx].view_state.view_yaw, 0,
			g_players[player_idx].view_state.hud_aim_x,
			g_players[player_idx].view_state.hud_aim_y, NULL);
	} else {
		if (g_players[player_idx].view_state.hud_aim_y != 0 ||
		    g_players[player_idx].view_state.hud_aim_x != 0) {
			fview_build_camera_orient(
				g_players[player_idx].view_state.view_roll,
				g_players[player_idx].view_state.view_pitch,
				g_players[player_idx].view_state.view_yaw, 0,
				g_players[player_idx].view_state.hud_aim_x,
				g_players[player_idx].view_state.hud_aim_y,
				NULL);
			trig2_ctop(g_cam_mat_r2_x, g_cam_mat_r2_y,
				   g_cam_mat_r2_z);
			g_players[player_idx].view_state.hud_aim_y = 0;
			g_players[player_idx].view_state.hud_aim_x = 0;
			g_players[player_idx].view_state.view_pitch =
				trig2_pitch;
			g_players[player_idx].view_state.view_yaw =
				trig2_xyangle;
		}
		fview_build_camera_orient(
			g_players[player_idx].view_state.view_roll,
			g_players[player_idx].view_state.view_pitch,
			g_players[player_idx].view_state.view_yaw, 0,
			g_players[player_idx].view_state.hud_aim_x,
			g_players[player_idx].view_state.hud_aim_y, NULL);
	}
	if (g_players[player_idx].view_state.camera_focus_obj_idx !=
	    UINT16_MAX) {
		g_players[player_idx].view_state.camera_world_x -= math_mul_q15(
			g_players[player_idx].view_state.camera_distance,
			g_cam_mat_r2_x);
		g_players[player_idx].view_state.camera_world_y -= math_mul_q15(
			g_players[player_idx].view_state.camera_distance,
			g_cam_mat_r2_y);
		g_players[player_idx].view_state.camera_world_z -= math_mul_q15(
			g_players[player_idx].view_state.camera_distance,
			g_cam_mat_r2_z);
	}
	if (g_players[player_idx].view_state.aim_target_idx != UINT16_MAX) {
		trig2_ctop(
			g_object_table[g_players[player_idx]
					       .view_state.aim_target_idx]
					.world_x -
				g_players[player_idx].view_state.camera_world_x,
			g_object_table[g_players[player_idx]
					       .view_state.aim_target_idx]
					.world_y -
				g_players[player_idx].view_state.camera_world_y,
			g_object_table[g_players[player_idx]
					       .view_state.aim_target_idx]
					.world_z -
				g_players[player_idx]
					.view_state.camera_world_z);
		g_players[player_idx].view_state.view_roll = 0;
		g_players[player_idx].view_state.view_pitch = trig2_pitch;
		g_players[player_idx].view_state.view_yaw = trig2_xyangle;
		fview_build_camera_orient(
			g_players[player_idx].view_state.view_roll,
			g_players[player_idx].view_state.view_pitch,
			g_players[player_idx].view_state.view_yaw, 0, 0, 0,
			NULL);
	}
}

/* Resets the render list and queues what the map shows, each at its view
 * depth: from slots 0 up to g_explosion_object_slot_end, craft (genus 0 to 5)
 * whose bounds pass render_list_project_object_bounds_for_culling for the local
 * player, and shots, small debris and explosions whose bounding sphere
 * flight_view_project_and_test_sphere_visible finds in view; then mines and
 * satellites (genus 8 and 9) in the static slots that pass the craft
 * test. */
// FUNCTION: XVT 0x436320
void flight_map_build_render_list(void)
{
	unsigned int object_idx;
	unsigned int object_type;
	unsigned int genus_id;

	object_idx = 0;
	render_list_reset();
	if (g_explosion_object_slot_end != 0) {
		do {
			object_type = g_object_table[object_idx].object_type;
			if (object_type != 0) {
				switch (g_object_table[object_idx].genus_id) {
				case CRAFT_GENUS_STARFIGHTER:
				case CRAFT_GENUS_TRANSPORT:
				case CRAFT_GENUS_UTILITY_VEHICLE:
				case CRAFT_GENUS_FREIGHTER:
				case CRAFT_GENUS_STARSHIP:
				case CRAFT_GENUS_PLATFORM:
					if (render_list_project_object_bounds_for_culling(
						    object_idx,
						    g_object_type_table[object_type]
							    .max_bounds_extent,
						    g_local_player)) {
						render_list_queue_object(
							object_idx,
							g_view_space_depth);
					}
					break;
				case CRAFT_GENUS_PLAYER_PROJECTILE:
				case CRAFT_GENUS_OTHER_PROJECTILE:
				case CRAFT_GENUS_SMALL_DEBRIS:
				case CRAFT_GENUS_EXPLOSION:
					if (flight_view_project_and_test_sphere_visible(
						    object_idx,
						    g_object_type_table[object_type]
							    .max_bounds_extent)) {
						render_list_queue_object(
							object_idx,
							g_view_space_depth);
					}
					break;
				}
			}
			++object_idx;
		} while (object_idx < g_explosion_object_slot_end);
	}

	object_idx = g_region_main_object_slot_end;
	if ((unsigned int)(g_region_main_object_slot_end +
			   g_region_static_object_slot_count) > object_idx) {
		do {
			object_type = g_object_table[object_idx].object_type;
			if (object_type != 0) {
				genus_id = g_object_table[object_idx].genus_id;
				if (genus_id >= CRAFT_GENUS_MINE &&
				    genus_id <= CRAFT_GENUS_SATELLITE &&
				    render_list_project_object_bounds_for_culling(
					    object_idx,
					    g_object_type_table[object_type]
						    .max_bounds_extent,
					    g_local_player)) {
					render_list_queue_object(
						object_idx, g_view_space_depth);
				}
			}
			++object_idx;
		} while (object_idx <
			 (unsigned int)(g_region_main_object_slot_end +
					g_region_static_object_slot_count));
	}
}

/* Draws the render-listed objects on one side of the map's grid plane
 * (world z -65,536): at or above it when draw_above_grid_plane is set, below
 * it otherwise. A craft or obstacle shows as an icon
 * (flight_map_draw_object_icon_at_view_pos) when its max_bounds_extent is under a
 * sixteenth of its sort depth, else as its model (an obstacle through
 * proving_grounds_draw_course_object; a craft lit and with its fuselage
 * billboards), and a player's craft other than the camera's focus also
 * gets flight_map_draw_other_player_object_box. Shots are drawn as models; mines
 * and satellites as icons when far, else drawn; debris and explosions
 * drawn. The camera's focus object gets a box in color 47 and the local
 * player's target one in 59 (targeting_draw_object_box), and every object
 * drawn gets flight_map_draw_object_overlay. Puts g_render_list_head back when
 * done. Writes g_camRelWorld*, g_viewSpace*, g_cur_craft and
 * g_scene_billboard_queue_count. */
// FUNCTION: XVT 0x436780
void flight_map_draw_object_pass(int draw_above_grid_plane)
{
	enum {
		MAP_GRID_PLANE_Z = -65536,
		MAP_MODEL_ICON_DISTANCE_SHIFT = 4,
		MAP_FOCUS_BOX_COLOR = 47,
		MAP_TARGET_BOX_COLOR = 59,
	};

	struct render_object_list_entry *saved_render_list_head;

	g_scene_billboard_queue_count = 0;
	saved_render_list_head = g_render_list_head;
	while (g_render_list_head != NULL) {
		int object_idx;
		int draw_object;

		object_idx = g_render_list_head->object_idx;
		draw_object = 0;
		if (draw_above_grid_plane != 0) {
			if (g_object_table[object_idx].world_z >=
			    MAP_GRID_PLANE_Z) {
				draw_object = 1;
			}
		} else {
			if (g_object_table[object_idx].world_z <
			    MAP_GRID_PLANE_Z) {
				draw_object = 1;
			}
		}

		if (draw_object != 0) {
			int genus_id;
			int camera_world_x;
			int camera_world_y;

			render_scene_initialize(g_render_scene_reset_pending);
			g_render_scene_reset_pending = 0;
			camera_world_x = g_players[g_local_player]
						 .view_state.camera_world_x;
			camera_world_y = g_players[g_local_player]
						 .view_state.camera_world_y;
			g_cam_rel_world_x = g_object_table[object_idx].world_x -
					    camera_world_x;
			g_cam_rel_world_y = g_object_table[object_idx].world_y -
					    camera_world_y;
			g_cam_rel_world_z = g_object_table[object_idx].world_z -
					    g_players[g_local_player]
						    .view_state.camera_world_z;
			g_view_space_depth = transfm2_cam_mat_dot_row2(
				g_cam_rel_world_x, g_cam_rel_world_y,
				g_cam_rel_world_z);
			g_view_space_x = transfm2_cam_mat_dot_row0(
				g_cam_rel_world_x, g_cam_rel_world_y,
				g_cam_rel_world_z);
			g_view_space_y = transfm2_cam_mat_dot_row1(
				g_cam_rel_world_x, g_cam_rel_world_y,
				g_cam_rel_world_z);
			genus_id = g_object_table[object_idx].genus_id;

			switch (genus_id) {
			case CRAFT_GENUS_STARFIGHTER:
			case CRAFT_GENUS_TRANSPORT:
			case CRAFT_GENUS_UTILITY_VEHICLE:
			case CRAFT_GENUS_FREIGHTER:
			case CRAFT_GENUS_STARSHIP:
			case CRAFT_GENUS_PLATFORM:
			case CRAFT_GENUS_OBSTACLE:
				if (g_object_type_table
					    [g_object_table[object_idx]
						     .object_type]
						    .max_bounds_extent <
				    g_render_list_head->sort_depth >>
				    MAP_MODEL_ICON_DISTANCE_SHIFT) {
					flight_map_draw_object_icon_at_view_pos(
						object_idx, g_view_space_x,
						g_view_space_y,
						g_view_space_depth);
				} else {
					g_cur_craft = g_object_table[object_idx]
							      .mobj->p_craft;
					if (genus_id == CRAFT_GENUS_OBSTACLE) {
						g_transform_light_direction_to_object_space =
							0;
					}
					fview_set_object_transform(
						g_object_table[object_idx].roll,
						g_object_table[object_idx]
							.pitch,
						g_object_table[object_idx].yaw,
						0, &g_object_table[object_idx]);
					if (genus_id == CRAFT_GENUS_OBSTACLE) {
						proving_grounds_draw_course_object(
							object_idx);
					} else {
						flight_light_setup_object_lighting(
							&g_object_table
								[object_idx]);
						damage_queue_craft_billboards(
							object_idx);
						render_scene_draw_object_model(
							&g_object_table
								[object_idx]);
						g_object_point_light_count = 0;
					}
					g_transform_light_direction_to_object_space =
						1;
					sw3d_draw_visible_faces_to_surface();
				}
				if (g_players[g_local_player]
						    .view_state
						    .camera_focus_obj_idx !=
					    object_idx &&
				    g_object_table[object_idx]
						    .player_owner_idx != -1) {
					flight_map_draw_other_player_object_box(
						object_idx);
				}
				break;

			case CRAFT_GENUS_PLAYER_PROJECTILE:
			case CRAFT_GENUS_OTHER_PROJECTILE:
				fview_set_object_transform(
					g_object_table[object_idx].roll,
					g_object_table[object_idx].pitch,
					g_object_table[object_idx].yaw, 0,
					&g_object_table[object_idx]);
				scene_billboard_draw_roll_aligned_object_model(
					object_idx);
				sw3d_draw_visible_faces_to_surface();
				break;

			case CRAFT_GENUS_MINE:
			case CRAFT_GENUS_SATELLITE:
				if (g_object_type_table
					    [g_object_table[object_idx]
						     .object_type]
						    .max_bounds_extent <
				    g_render_list_head->sort_depth >>
				    MAP_MODEL_ICON_DISTANCE_SHIFT) {
					flight_map_draw_object_icon_at_view_pos(
						object_idx, g_view_space_x,
						g_view_space_y,
						g_view_space_depth);
					break;
				}
				g_scene_billboard_queue_count = 0;
				if (g_region_main_object_slot_end >
				    object_idx) {
					fview_set_object_transform(
						g_object_table[object_idx].roll,
						g_object_table[object_idx]
							.pitch,
						g_object_table[object_idx].yaw,
						0, &g_object_table[object_idx]);
					scene_billboard_draw_or_queue_object(
						object_idx);
				} else {
					fview_set_object_transform(
						g_object_table[object_idx].roll,
						g_object_table[object_idx]
							.pitch,
						g_object_table[object_idx].yaw,
						0, NULL);
					render_non_craft_scene_object(
						object_idx);
				}
				sw3d_draw_visible_faces_to_surface();
				scene_billboard_render_queued_textured(0);
				g_scene_billboard_queue_count = 0;
				break;

			case CRAFT_GENUS_NORMAL_DEBRIS:
			case CRAFT_GENUS_SMALL_DEBRIS:
			case CRAFT_GENUS_EXPLOSION:
				g_scene_billboard_queue_count = 0;
				if (g_region_main_object_slot_end >
				    object_idx) {
					fview_set_object_transform(
						g_object_table[object_idx].roll,
						g_object_table[object_idx]
							.pitch,
						g_object_table[object_idx].yaw,
						0, &g_object_table[object_idx]);
					scene_billboard_draw_or_queue_object(
						object_idx);
				} else {
					fview_set_object_transform(
						g_object_table[object_idx].roll,
						g_object_table[object_idx]
							.pitch,
						g_object_table[object_idx].yaw,
						0, NULL);
					render_non_craft_scene_object(
						object_idx);
				}
				sw3d_draw_visible_faces_to_surface();
				scene_billboard_render_queued_textured(0);
				g_scene_billboard_queue_count = 0;
				break;

			default:
				break;
			}

			if (g_players[g_local_player]
				    .view_state.camera_focus_obj_idx ==
			    object_idx) {
				targeting_draw_object_box(object_idx,
							  UINT16_MAX,
							  MAP_FOCUS_BOX_COLOR);
			} else if ((uint16_t)g_players[g_local_player]
					   .current_target_object_idx ==
				   object_idx) {
				targeting_draw_object_box(object_idx,
							  UINT16_MAX,
							  MAP_TARGET_BOX_COLOR);
			}
			flight_surface_lock();
			flight_map_draw_object_overlay(object_idx);
			flight_surface_unlock();
			render_scene_unlock_buffers();
		}
		g_render_list_head = g_render_list_head->next;
	}
	g_render_list_head = saved_render_list_head;
}

/* Draws a box on the map around a player's craft, object_idx, in the color
 * of its IFF (63 rebel, 55 imperial, 51 blue, else 59). Draws nothing when
 * locating players is off and the craft is hostile to the local player's
 * team and not yet inspected by it, when the craft runs a decoy beam, or
 * when it is the local player's current target. */
// FUNCTION: XVT 0x436BC0
void flight_map_draw_other_player_object_box(int object_idx)
{
	enum {
		IFF_REBEL = 0,
		IFF_IMPERIAL = 1,
		IFF_BLUE = 2,
		IFF_IMPERIAL_2 = 4,
		COLOR_REBEL = 63,
		COLOR_IMPERIAL = 55,
		COLOR_BLUE = 51,
		COLOR_DEFAULT = 59,
	};

	struct mobile_object *mobile_object;
	struct craft_data *craft;
	uint8_t color_index;
	int player_team;
	int team;
	int is_hostile;

	mobile_object = g_object_table[object_idx].mobj;
	switch (mobile_object->iff) {
	case IFF_REBEL:
		color_index = COLOR_REBEL;
		break;
	case IFF_IMPERIAL:
	case IFF_IMPERIAL_2:
		color_index = COLOR_IMPERIAL;
		break;
	case IFF_BLUE:
		color_index = COLOR_BLUE;
		break;
	default:
		color_index = COLOR_DEFAULT;
		break;
	}

	craft = mobile_object->p_craft;
	if (g_flight_mission_state.locate_players_enabled == 0) {
		player_team = (uint16_t)g_players[g_local_player].team;
		if (craft->identified_order_by_team[player_team] == 0) {
			team = g_mission_flight_groups
				       [g_object_table[(uint16_t)object_idx]
						.flight_group_idx]
					       .fg.team;
			if (team == player_team) {
				is_hostile = 0;
			} else {
				is_hostile = g_mission_teams[player_team]
						     .allies[team] == 0;
			}
			if (is_hostile) {
				return;
			}
		}
	}

	if (object_has_active_decoy_beam((uint16_t)object_idx) != 0 ||
	    (uint16_t)g_players[g_local_player].current_target_object_idx ==
		    object_idx) {
		return;
	}
	targeting_draw_object_box(object_idx, UINT16_MAX, color_index);
}

/* Draws the map's markings for object_idx at its projected point, from the
 * g_viewSpace* the object pass left; nothing for an explosion or small
 * debris, or behind the camera. For the local player's current target it
 * draws a line from the object or mission point its AI targets: the
 * craft's ai_controller.target_obj_idx in a craft slot, or for another slot
 * with a craft record the two bytes at its model_index read as a target.
 * Then, for an object in a craft slot, with a craft record or without a
 * mobile_object: a line down to the grid plane in its IFF color, and for a
 * space craft a tick from that foot along its move vector, growing with
 * speed up to 0x400; its name above the box (none for a static mine); and,
 * except for genus 6 and 7, when the camera has a focus object, the
 * distance from it as two digits, a point and two digits
 * (trig2_polardistance times 161 / 65,536, at most 9999). Writes
 * g_viewSpace*, g_camRelWorld*, g_worldLoc* and the trig2 outputs. */
// FUNCTION: XVT 0x436D00
void flight_map_draw_object_overlay(int object_idx)
{
	int saved_view_position[3];
	int screen_x;
	int screen_y;
	unsigned int target_ref;
	int iff;
	int color;
	int draw_overlay;
	unsigned int box_extent;
	int display_extent;
	int text_x;
	int text_y;
	int grid_foot_view_position[3];
	int line_screen_x;
	int line_screen_y;
	int distance;
	int genus_id;
	uint16_t packed_target_ref;

	genus_id = g_object_table[object_idx].genus_id;
	if (genus_id == 13 || genus_id == 11) {
		return;
	}

	saved_view_position[0] = g_view_space_x;
	saved_view_position[1] = g_view_space_y;
	saved_view_position[2] = g_view_space_depth;
	if (saved_view_position[2] <= 0) {
		return;
	}
	screen_x = transfm2_project_screen_x(saved_view_position[0],
					     saved_view_position[2]);
	screen_y = transfm2_project_screen_y(saved_view_position[1],
					     saved_view_position[2]);
	screen_x += g_flight_clip_left;
	screen_y += g_flight_clip_top;

	if ((uint16_t)g_players[g_local_player].current_target_object_idx ==
	    object_idx) {
		target_ref = UINT16_MAX;
		if (object_idx < g_craft_data_pool_capacity) {
			target_ref = g_object_table[object_idx]
					     .mobj->p_craft->ai_controller
					     .target_obj_idx;
		} else {
			struct mobile_object *mobile_object =
				g_object_table[object_idx].mobj;
			if (mobile_object != NULL) {
				struct craft_data *craft =
					mobile_object->p_craft;
				if (craft != NULL) {
					memcpy(&packed_target_ref,
					       &craft->model_index,
					       sizeof(packed_target_ref));
					target_ref = packed_target_ref;
				}
			}
		}
		if (target_ref != UINT16_MAX) {
			mission_resolve_object_or_mission_point_world_loc(
				target_ref,
				g_object_table[object_idx].flight_group_idx);
			g_world_loc_x -= g_players[g_local_player]
						 .view_state.camera_world_x;
			g_world_loc_y -= g_players[g_local_player]
						 .view_state.camera_world_y;
			g_world_loc_z -= g_players[g_local_player]
						 .view_state.camera_world_z;
			g_view_space_x = transfm2_cam_mat_dot_row0(
				g_world_loc_x, g_world_loc_y, g_world_loc_z);
			g_view_space_y = transfm2_cam_mat_dot_row1(
				g_world_loc_x, g_world_loc_y, g_world_loc_z);
			g_view_space_depth = transfm2_cam_mat_dot_row2(
				g_world_loc_x, g_world_loc_y, g_world_loc_z);
			if (g_view_space_depth <= 0) {
				transfm2_clipobjecteyez(saved_view_position[0],
							saved_view_position[1],
							saved_view_position[2]);
			}
			g_flight_draw_line_fn(
				g_flight_clip_left +
					transfm2_project_screen_x(
						g_view_space_x,
						g_view_space_depth),
				g_flight_clip_top + transfm2_project_screen_y(
							    g_view_space_y,
							    g_view_space_depth),
				screen_x, screen_y, 0x36);
		}
	}

	if (g_object_table[object_idx].mobj != NULL) {
		iff = (uint8_t)g_object_table[object_idx].mobj->iff;
	} else {
		iff = g_mission_flight_groups[g_object_table[object_idx]
						      .flight_group_idx]
			      .fg.iff;
	}
	switch (iff) {
	case 0:
		color = 63;
		break;
	case 1:
	case 4:
		color = 55;
		break;
	case 2:
		color = 51;
		break;
	case 3:
		color = 59;
		break;
	case 5:
		color = 59;
		break;
	default:
		color = 59;
		break;
	}
	flight_text_set_background_color(color);
	flight_text_set_font_tier(0);

	draw_overlay = 0;
	if (object_idx < g_craft_data_pool_capacity ||
	    g_object_table[object_idx].mobj == NULL ||
	    g_object_table[object_idx].mobj->p_craft != NULL) {
		draw_overlay = 1;
	}
	if (draw_overlay == 0) {
		return;
	}

	g_view_space_x = transfm2_cam_mat_dot_row0(
		g_cam_rel_world_x, g_cam_rel_world_y,
		-65536 - g_players[g_local_player].view_state.camera_world_z);
	g_view_space_y = transfm2_cam_mat_dot_row1(
		g_cam_rel_world_x, g_cam_rel_world_y,
		-65536 - g_players[g_local_player].view_state.camera_world_z);
	g_view_space_depth = transfm2_cam_mat_dot_row2(
		g_cam_rel_world_x, g_cam_rel_world_y,
		-65536 - g_players[g_local_player].view_state.camera_world_z);
	if (g_view_space_depth <= 0) {
		transfm2_clipobjecteyez(saved_view_position[0],
					saved_view_position[1],
					saved_view_position[2]);
	}
	line_screen_x =
		transfm2_project_screen_x(g_view_space_x, g_view_space_depth);
	line_screen_y =
		transfm2_project_screen_y(g_view_space_y, g_view_space_depth);
	line_screen_x += g_flight_clip_left;
	line_screen_y += g_flight_clip_top;
	g_flight_draw_line_fn(line_screen_x, line_screen_y, screen_x, screen_y,
			      g_flight_text_bg_color);

	if (g_object_table[object_idx].mobj != NULL &&
	    g_object_table[object_idx].mobj->family == 0) {
		int move_y;

		if (g_object_table[object_idx].mobj->orient_matrix_dirty != 0) {
			fview_calcrotatemove(g_object_table[object_idx].pitch,
					     g_object_table[object_idx].yaw,
					     &g_object_table[object_idx]);
			fview_calcrotateorient(g_object_table[object_idx].roll,
					       0, &g_object_table[object_idx]);
		}
		g_cam_rel_world_x += math_mul_q15(
			256, g_object_table[object_idx].mobj->move_x);
		g_cam_rel_world_y += math_mul_q15(
			256, g_object_table[object_idx].mobj->move_y);
		if (g_object_table[object_idx].mobj->speed < 0x400) {
			g_cam_rel_world_x += math_mul_q15(
				32 * g_object_table[object_idx].mobj->speed,
				g_object_table[object_idx].mobj->move_x);
			move_y = math_mul_q15(
				32 * g_object_table[object_idx].mobj->speed,
				g_object_table[object_idx].mobj->move_y);
		} else {
			g_cam_rel_world_x +=
				g_object_table[object_idx].mobj->move_x;
			move_y = g_object_table[object_idx].mobj->move_y;
		}
		grid_foot_view_position[2] = g_view_space_depth;
		g_cam_rel_world_y += move_y;
		grid_foot_view_position[0] = g_view_space_x;
		grid_foot_view_position[1] = g_view_space_y;
		g_view_space_x = transfm2_cam_mat_dot_row0(
			g_cam_rel_world_x, g_cam_rel_world_y,
			-65536 - g_players[g_local_player]
					 .view_state.camera_world_z);
		g_view_space_y = transfm2_cam_mat_dot_row1(
			g_cam_rel_world_x, g_cam_rel_world_y,
			-65536 - g_players[g_local_player]
					 .view_state.camera_world_z);
		g_view_space_depth = transfm2_cam_mat_dot_row2(
			g_cam_rel_world_x, g_cam_rel_world_y,
			-65536 - g_players[g_local_player]
					 .view_state.camera_world_z);
		if (g_view_space_depth <= 0) {
			transfm2_clipobjecteyez(grid_foot_view_position[0],
						grid_foot_view_position[1],
						grid_foot_view_position[2]);
		}
		g_flight_draw_line_fn(
			g_flight_clip_left +
				transfm2_project_screen_x(g_view_space_x,
							  g_view_space_depth),
			g_flight_clip_top +
				transfm2_project_screen_y(g_view_space_y,
							  g_view_space_depth),
			line_screen_x, line_screen_y, g_flight_text_bg_color);
	}

	flight_text_set_background_color(0x40);
	text_x = (int16_t)screen_x;
	text_y = (int16_t)screen_y;
	box_extent = targeting_get_object_box_extent(object_idx);
	box_extent *= g_proj_scale_int;
	box_extent /= saved_view_position[2];
	if (box_extent < g_screen_width / 0x50u) {
		box_extent = g_screen_width / 0x50u;
	}
	if (box_extent > (unsigned int)g_screen_width >> 1) {
		box_extent = (unsigned int)g_screen_width >> 1;
	}
	display_extent = box_extent + 4;
	if (g_object_table[object_idx].mobj != NULL ||
	    g_object_table[object_idx].genus_id != 8) {
		hud_format_object_display_name(object_idx, 2);
	} else {
		g_flight_text_scratch_buffer[0] = 0;
	}
	if (g_flight_text_scratch_buffer[0] != 0) {
		flight_text_set_cursor(
			text_x - (flight_text_measure_string_width(
					  g_flight_text_scratch_buffer) >>
				  1),
			text_y - g_flight_font_line_height -
				display_extent / 2 - 1);
		flight_text_draw_string(g_flight_text_scratch_buffer);
	}
	if (g_object_table[object_idx].genus_id != 6 &&
	    g_object_table[object_idx].genus_id != 7) {
		text_y += display_extent >> 1;
		flight_text_set_background_color(0x40);
		if (g_players[g_local_player].view_state.camera_focus_obj_idx !=
		    UINT16_MAX) {
			pai_object_ref_direction_to_object_ref(
				g_players[g_local_player]
					.view_state.camera_focus_obj_idx,
				object_idx);
			flight_text_set_cursor(
				text_x - flight_text_measure_string_width("00"),
				text_y + 1);
			trig2_polardistance *= 161;
			distance = (trig2_polardistance >> 16) & 0xffff;
			if (distance >= 10000) {
				distance = 9999;
			}
			flight_text_draw_decimal_number(
				(uint16_t)(distance / 100), 2, 1);
			g_flight_draw_char_fn(0x2E);
			flight_text_draw_decimal_number(
				(uint16_t)(distance - distance / 100 * 100), 2,
				2);
		}
	}
}

/* Draws the map icon of object_idx at the view position (view_x, view_y,
 * view_z): the frame for its object type in the icon set in use (frame 19
 * for types above 105), in the quarter of the frames for its IFF's color,
 * centered on the projected point and drawn only when wholly inside the
 * clip rectangle. IFF 3 draws with flight_sw_blit_map_icon_rle, the others
 * with g_flight_blit_sprite_fn. */
// FUNCTION: XVT 0x437570
void flight_map_draw_object_icon_at_view_pos(int object_idx, int view_x,
					     int view_y, int view_z)
{
	struct object_record *object;
	struct mobile_object *mobile_object;
	int object_type;
	int use_map_icon_blitter;
	int iff;
	int color_group;
	int screen_x;
	int screen_y;
	int frame_idx;
	int icon_width;

	object = &g_object_table[object_idx];
	object_type = object->object_type;
	mobile_object = object->mobj;
	if (mobile_object != NULL) {
		iff = (uint8_t)mobile_object->iff;
	} else {
		iff = g_mission_flight_groups[object->flight_group_idx].fg.iff;
	}

	use_map_icon_blitter = 0;
	switch (iff) {
	case 0:
		color_group = 0;
		break;
	case 1:
	case 4:
		color_group = 2;
		break;
	case 2:
		color_group = 3;
		break;
	case 3:
		use_map_icon_blitter = 1;
		color_group = 2;
		break;
	case 5:
		color_group = 1;
		break;
	default:
		color_group = 1;
		break;
	}

	screen_x = transfm2_project_screen_x(view_x, view_z);
	screen_y = transfm2_project_screen_y(view_y, view_z);
	screen_x += g_flight_clip_left;
	screen_y += g_flight_clip_top;
	/* In each branch below, objectType picks the frame and is then reused as the icon height beside
	 * icon_width; the last branch leaves the object type in it. */
	if (g_flight_icon_resource_path ==
	    g_flight_icons640x480_resource_path) {
		frame_idx = 19;
		if (object_type <= 105) {
			frame_idx = g_flight_icons640_frame_by_object_type
				[object_type];
		}
		icon_width = g_flight_icons640_width_by_frame[frame_idx];
		object_type = g_flight_icons640_height_by_frame[frame_idx];
	} else if (g_flight_icon_resource_path ==
		   g_flight_map_icons320x240_resource_path) {
		frame_idx = 19;
		if (object_type <= 105) {
			frame_idx =
				g_flight_map_icons320x240_frame_by_object_type
					[object_type];
		}
		icon_width =
			g_flight_map_icons320x240_width_by_frame[frame_idx];
		object_type =
			g_flight_map_icons320x240_height_by_frame[frame_idx];
	} else if (g_flight_icon_resource_path ==
		   g_flight_map_icons480x360_resource_path) {
		frame_idx = 19;
		if (object_type <= 105) {
			frame_idx =
				g_flight_map_icons480x360_frame_by_object_type
					[object_type];
		}
		icon_width =
			g_flight_map_icons480x360_width_by_frame[frame_idx];
		object_type =
			g_flight_map_icons480x360_height_by_frame[frame_idx];
	} else {
		frame_idx = use_map_icon_blitter;
		icon_width = use_map_icon_blitter;
	}

	if (color_group > 3) {
		color_group &= 3;
	}
	frame_idx += g_flight_icon_frame_count * color_group / 4;
	screen_x -= icon_width / 2;
	screen_y -= object_type / 2;
	if ((int16_t)screen_x >= g_flight_clip_left &&
	    (int16_t)(screen_x + icon_width) < g_flight_clip_right &&
	    (int16_t)screen_y >= g_flight_clip_top &&
	    (int16_t)(screen_y + object_type) < g_flight_clip_bottom) {
		flight_surface_lock();
		if (use_map_icon_blitter != 0) {
			flight_sw_blit_map_icon_rle(
				g_flight_icon_frames[frame_idx], screen_x,
				screen_y, 0, 0);
		} else {
			g_flight_blit_sprite_fn(g_flight_icon_frames[frame_idx],
						screen_x, screen_y, 0, 0);
		}
		flight_surface_unlock();
	}
}

/* Draws the four corners of the box at (x, y), width by height, in
 * color_index: ticks an eighth of the width and of the height long, at
 * least 3, clipped to the flight viewport; a tick on an edge outside the
 * viewport is left out. Draws nothing when the box is empty or wholly
 * outside. targeting_draw_object_box calls it while the local player is in
 * the map view. */
// FUNCTION: XVT 0x437860
void flight_map_draw_object_box_corners(int x, int y, int width, int height,
					unsigned int color_index)
{
	int corner_width;
	int corner_height;
	int span_start;
	int span_end;
	int screen_y;
	int row;

	if (y + height > 0 && x + width > 0 && g_flight_vp_width > x &&
	    g_flight_vp_height > y && height > 0 && width > 0) {
		corner_width = width >> 3;
		corner_height = height >> 3;
		if (corner_width < 3) {
			corner_width = 3;
		}
		if (corner_height < 3) {
			corner_height = 3;
		}
		if (corner_width > width) {
			corner_width = width;
		}
		if (corner_height > height) {
			corner_height = height;
		}

		flight_surface_lock();
		if (y >= 0) {
			span_start = x;
			span_end = x + corner_width;
			if (span_end > 0 && x < g_flight_vp_width) {
				if (span_start < 0) {
					span_start = 0;
				}
				if (span_end > g_flight_vp_width) {
					span_end = g_flight_vp_width;
				}
				flight_sw_draw_horizontal_color_span(
					span_start, span_end, y, color_index);
			}

			span_start = x + width - corner_width;
			span_end = x + width;
			if (span_end > 0 && span_start < g_flight_vp_width) {
				if (span_start < 0) {
					span_start = 0;
				}
				if (span_end > g_flight_vp_width) {
					span_end = g_flight_vp_width;
				}
				flight_sw_draw_horizontal_color_span(
					span_start, span_end, y, color_index);
			}
		}

		if (g_flight_vp_height >= y + height) {
			span_start = x;
			span_end = x + corner_width;
			if (span_end > 0 && x < g_flight_vp_width) {
				if (span_start < 0) {
					span_start = 0;
				}
				if (span_end > g_flight_vp_width) {
					span_end = g_flight_vp_width;
				}
				flight_sw_draw_horizontal_color_span(
					span_start, span_end, y + height - 1,
					color_index);
			}

			span_start = x + width - corner_width;
			span_end = x + width;
			if (span_end > 0 && span_start < g_flight_vp_width) {
				if (span_start < 0) {
					span_start = 0;
				}
				if (span_end > g_flight_vp_width) {
					span_end = g_flight_vp_width;
				}
				flight_sw_draw_horizontal_color_span(
					span_start, span_end, y + height - 1,
					color_index);
			}
		}

		for (row = 1; row < corner_height; ++row) {
			screen_y = y + row;
			if (screen_y >= 0 && screen_y < g_flight_vp_height) {
				if (x >= 0) {
					flight_sw_draw_horizontal_color_span(
						x, x + 1, screen_y,
						color_index);
				}
				if (g_flight_vp_width >= x + width) {
					flight_sw_draw_horizontal_color_span(
						x + width - 1, x + width,
						screen_y, color_index);
				}
			}
		}

		for (row = height - corner_height; row < height - 1; ++row) {
			if (row >= corner_height) {
				screen_y = y + row;
				if (screen_y >= 0 &&
				    screen_y < g_flight_vp_height) {
					if (x >= 0) {
						flight_sw_draw_horizontal_color_span(
							x, x + 1, screen_y,
							color_index);
					}
					if (g_flight_vp_width >= x + width) {
						flight_sw_draw_horizontal_color_span(
							x + width - 1,
							x + width, screen_y,
							color_index);
					}
				}
			}
		}
		flight_surface_unlock();
	}
}

/* Draws the map grid in color 0x31: 33 lines each way, 0x10000 world units
 * apart, from -0x100000 to 0x100000 in X and Y, at world z -65,536,
 * clipped at the camera plane. Sets g_worldLoc* to 0 and writes
 * g_camRelWorld* and g_viewSpace*. */
// FUNCTION: XVT 0x437B20
void flight_map_draw_grid(void)
{
	int line_count;
	int grid_x;
	int grid_y;
	int other_view_x;
	int other_view_y;
	int other_depth_z;
	int swap_value;
	int camera_world_x;
	int camera_world_y;
	int camera_world_z;

	g_world_loc_z = 0;
	g_world_loc_y = 0;
	g_world_loc_x = 0;
	grid_y = -0x100000;
	line_count = 33;
	do {
		camera_world_x =
			g_players[g_local_player].view_state.camera_world_x;
		camera_world_y =
			g_players[g_local_player].view_state.camera_world_y;
		camera_world_z =
			g_players[g_local_player].view_state.camera_world_z;
		g_cam_rel_world_x = -0x100000 - camera_world_x;
		g_cam_rel_world_y = grid_y - camera_world_y;
		g_cam_rel_world_z = -0x10000 - camera_world_z;
		grid_y += 0x10000;
		g_view_space_depth = transfm2_cam_mat_dot_row2(
			g_cam_rel_world_x, g_cam_rel_world_y,
			g_cam_rel_world_z);
		other_depth_z = (int)((uint32_t)g_view_space_depth +
				      ((uint32_t)g_cam_mat_r2_x << 6));
		if (other_depth_z > 0 || g_view_space_depth > 0) {
			g_view_space_x = transfm2_cam_mat_dot_row0(
				g_cam_rel_world_x, g_cam_rel_world_y,
				g_cam_rel_world_z);
			g_view_space_y = transfm2_cam_mat_dot_row1(
				g_cam_rel_world_x, g_cam_rel_world_y,
				g_cam_rel_world_z);
			other_view_x = (int)((uint32_t)g_view_space_x +
					     ((uint32_t)g_cam_mat_r0_x << 6));
			other_view_y = (int)((uint32_t)g_view_space_y +
					     ((uint32_t)g_cam_mat_r1_x << 6));
			if (other_depth_z <= 0) {
				swap_value = other_depth_z;
				other_depth_z = g_view_space_depth;
				g_view_space_depth = swap_value;
				swap_value = other_view_y;
				other_view_y = g_view_space_y;
				g_view_space_y = swap_value;
				swap_value = other_view_x;
				other_view_x = g_view_space_x;
				g_view_space_x = swap_value;
			}
			if (g_view_space_depth <= 0) {
				transfm2_clipobjecteyez(other_view_x,
							other_view_y,
							other_depth_z);
			}
			flight_surface_lock();
			g_flight_draw_line_fn(
				g_flight_clip_left +
					transfm2_project_screen_x(
						g_view_space_x,
						g_view_space_depth),
				g_flight_clip_top + transfm2_project_screen_y(
							    g_view_space_y,
							    g_view_space_depth),
				g_flight_clip_left +
					transfm2_project_screen_x(
						other_view_x, other_depth_z),
				g_flight_clip_top +
					transfm2_project_screen_y(
						other_view_y, other_depth_z),
				0x31);
			flight_surface_unlock();
		}
		--line_count;
	} while (line_count != 0);

	line_count = 33;
	grid_x = g_world_loc_x - 0x100000;
	grid_y = g_world_loc_y - 0x100000;
	do {
		camera_world_x =
			g_players[g_local_player].view_state.camera_world_x;
		camera_world_y =
			g_players[g_local_player].view_state.camera_world_y;
		camera_world_z =
			g_players[g_local_player].view_state.camera_world_z;
		g_cam_rel_world_x = grid_x - camera_world_x;
		g_cam_rel_world_y = grid_y - camera_world_y;
		g_cam_rel_world_z = -0x10000 - camera_world_z;
		grid_x += 0x10000;
		g_view_space_depth = transfm2_cam_mat_dot_row2(
			g_cam_rel_world_x, g_cam_rel_world_y,
			g_cam_rel_world_z);
		other_depth_z = (int)((uint32_t)g_view_space_depth +
				      ((uint32_t)g_cam_mat_r2_y << 6));
		if (other_depth_z > 0 || g_view_space_depth > 0) {
			g_view_space_x = transfm2_cam_mat_dot_row0(
				g_cam_rel_world_x, g_cam_rel_world_y,
				g_cam_rel_world_z);
			g_view_space_y = transfm2_cam_mat_dot_row1(
				g_cam_rel_world_x, g_cam_rel_world_y,
				g_cam_rel_world_z);
			other_view_x = (int)((uint32_t)g_view_space_x +
					     ((uint32_t)g_cam_mat_r0_y << 6));
			other_view_y = (int)((uint32_t)g_view_space_y +
					     ((uint32_t)g_cam_mat_r1_y << 6));
			if (other_depth_z <= 0) {
				swap_value = other_depth_z;
				other_depth_z = g_view_space_depth;
				g_view_space_depth = swap_value;
				swap_value = other_view_y;
				other_view_y = g_view_space_y;
				g_view_space_y = swap_value;
				swap_value = other_view_x;
				other_view_x = g_view_space_x;
				g_view_space_x = swap_value;
			}
			if (g_view_space_depth <= 0) {
				transfm2_clipobjecteyez(other_view_x,
							other_view_y,
							other_depth_z);
			}
			flight_surface_lock();
			g_flight_draw_line_fn(
				g_flight_clip_left +
					transfm2_project_screen_x(
						g_view_space_x,
						g_view_space_depth),
				g_flight_clip_top + transfm2_project_screen_y(
							    g_view_space_y,
							    g_view_space_depth),
				g_flight_clip_left +
					transfm2_project_screen_x(
						other_view_x, other_depth_z),
				g_flight_clip_top +
					transfm2_project_screen_y(
						other_view_y, other_depth_z),
				0x31);
			flight_surface_unlock();
		}
		--line_count;
	} while (line_count != 0);
}

/* Does nothing; flight_map_render_view calls it last. */
// FUNCTION: XVT 0x437ED0
void flight_map_render_view_end_stub(void) {}

/* Returns the slot of the object drawn nearest the center of player_idx's
 * map view, or UINT16_MAX when none comes closer than an eighth of the
 * squared screen diagonal. Candidates are craft (genus 0 to 5) from slots 0
 * up to g_explosion_object_slot_end and mines and satellites in the static
 * slots that pass render_list_project_object_bounds_for_culling, scored by
 * their squared projected distance from the center; the current aim
 * target and the camera's focus object score worse by (width / 16) squared
 * plus (height / 16) squared of the screen. First rebuilds the map camera's
 * orientation from map_camera_state or the view angles (turned to
 * aim_target_idx when set). Writes the camera matrices, g_viewSpace* and the
 * trig2 outputs. */
// FUNCTION: XVT 0x438010
int flight_map_pick_object_nearest_screen_center(int player_idx)
{
	int best_score;
	int best_object = UINT16_MAX;
	int object_idx;
	int object_slot;

	if (g_players[player_idx].map_camera_state > 1) {
		int hud_pitch = ((g_players[player_idx].map_camera_state & 0x7F)
				 << 14) /
				-127;
		fview_build_camera_orient(0, 0x4000, 0, 0, (int16_t)hud_pitch,
					  0, NULL);
	} else {
		fview_build_camera_orient(
			g_players[player_idx].view_state.view_roll,
			g_players[player_idx].view_state.view_pitch,
			g_players[player_idx].view_state.view_yaw, 0,
			g_players[player_idx].view_state.hud_aim_x,
			g_players[player_idx].view_state.hud_aim_y, NULL);
		if (g_players[player_idx].view_state.aim_target_idx !=
		    UINT16_MAX) {
			trig2_ctop(g_object_table[g_players[player_idx]
							  .view_state
							  .aim_target_idx]
						   .world_x -
					   g_players[player_idx]
						   .view_state.camera_world_x,
				   g_object_table[g_players[player_idx]
							  .view_state
							  .aim_target_idx]
						   .world_y -
					   g_players[player_idx]
						   .view_state.camera_world_y,
				   g_object_table[g_players[player_idx]
							  .view_state
							  .aim_target_idx]
						   .world_z -
					   g_players[player_idx]
						   .view_state.camera_world_z);
			fview_build_camera_orient(0, trig2_pitch, trig2_xyangle,
						  0, 0, 0, NULL);
		}
	}

	best_score = (g_screen_height * g_screen_height +
		      g_screen_width * g_screen_width) >>
		     3;
	/* object_idx and object_slot always hold the same slot: each loop starts both at the same value and steps
	 * both once per pass. */
	object_idx = 0;
	for (object_slot = 0; object_slot < (int)g_explosion_object_slot_end;
	     ++object_slot) {
		if (g_object_table[object_idx].object_type != 0) {
			switch (g_object_table[object_idx].genus_id) {
			case 0:
			case 1:
			case 2:
			case 3:
			case 4:
			case 5: {
				int projected_x;
				int projected_y;
				int score;

				if (!render_list_project_object_bounds_for_culling(
					    object_slot,
					    g_object_type_table
						    [g_object_table[object_idx]
							     .object_type]
							    .max_bounds_extent,
					    player_idx)) {
					break;
				}
				projected_x = (g_view_space_x
					       << g_perspective_shift) /
					      g_view_space_depth;
				projected_y = (g_view_space_y
					       << g_perspective_shift) /
					      g_view_space_depth;
				score = projected_x * projected_x +
					projected_y * projected_y;
				if (g_players[player_idx]
						    .view_state
						    .aim_target_idx ==
					    object_slot ||
				    g_players[player_idx]
						    .view_state
						    .camera_focus_obj_idx ==
					    object_slot) {
					score += (g_screen_width >> 4) *
							 (g_screen_width >> 4) +
						 (g_screen_height >> 4) *
							 (g_screen_height >> 4);
				}
				if (score < best_score) {
					best_score = score;
					best_object = object_slot;
				}
				break;
			}
			case 6:
			case 7:
			case 8:
			case 9:
			case 10:
			case 11:
			case 12:
			case 13:
			default:
				break;
			}
		}
		++object_idx;
	}
	object_idx = g_region_main_object_slot_end;
	for (object_slot = g_region_main_object_slot_end;
	     object_slot <
	     g_region_main_object_slot_end + g_region_static_object_slot_count;
	     ++object_slot) {
		if (g_object_table[object_idx].object_type != 0 &&
		    g_object_table[object_idx].genus_id >= 8 &&
		    g_object_table[object_idx].genus_id <= 9 &&
		    render_list_project_object_bounds_for_culling(
			    object_slot,
			    g_object_type_table[g_object_table[object_idx]
							.object_type]
				    .max_bounds_extent,
			    player_idx)) {
			int projected_x =
				(g_view_space_x << g_perspective_shift) /
				g_view_space_depth;
			int projected_y =
				(g_view_space_y << g_perspective_shift) /
				g_view_space_depth;
			int score = projected_x * projected_x +
				    projected_y * projected_y;
			if (g_players[player_idx].view_state.aim_target_idx ==
				    object_slot ||
			    g_players[player_idx]
					    .view_state.camera_focus_obj_idx ==
				    object_slot) {
				score += (g_screen_width >> 4) *
						 (g_screen_width >> 4) +
					 (g_screen_height >> 4) *
						 (g_screen_height >> 4);
			}
			if (score < best_score) {
				best_score = score;
				best_object = object_slot;
			}
		}
		++object_idx;
	}
	return best_object;
}
