#include "xvt/flight/flight_view.h"

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/cockpit_capture.h"
#include "xvt_runtime/snapshot/render_capture.h"
#endif
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_display.h"
#include "xvt/flight/flight_hyperspace.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/hud/flight_map.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/object/damage.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/proving_grounds.h"
#include "xvt/flight/targeting.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/math.h"
#include "xvt/math/trig2.h"
#include "xvt/net/flight_net.h"
#include "xvt/render/backdrop.h"
#include "xvt/render/flight_light.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_list.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/scene_billboard.h"
#include "xvt/render/std3d.h"
#include "xvt/render/sw3d.h"
#include "xvt/util/time.h"
#include <limits.h>
#include <string.h>

/* World Z offset, from the camera, of the point being placed, before it is
 * turned into view space. Many functions write it, chiefly
 * flight_view_compute_object_view_position, flight_view_project_and_test_sphere_visible,
 * flight_view_cull_world_sphere_to_viewport, the flight map and HUD 3D display code
 * and render_list_project_object_bounds_for_culling. */
// GLOBAL: XVT 0x9FD398
int g_cam_rel_world_z = 0;
/* World X offset from the camera, as g_cam_rel_world_z; written by the same
 * functions. */
// GLOBAL: XVT 0x9FD39C
int g_cam_rel_world_x = 0;
/* World Y offset from the camera, as g_cam_rel_world_z; written by the same
 * functions. */
// GLOBAL: XVT 0x9FD3A0
int g_cam_rel_world_y = 0;
/* Bounds extent (the type's max_bounds_extent) of the object being culled or
 * framed. Three functions write it: flight_view_render and hud_update3d_crt, per
 * object, and flight_view_update_player_camera, which halves it until it fits in
 * 32,767. */
// GLOBAL: XVT 0x9A8D9C
int g_current_object_bounds_extent = 0;
/* 1 when flight_view_render is to flush the hardware texture cache
 * (std3d_flush_texture_cache) before its next scene. Set by
 * mission_init_flight_runtime_state and hud_rebuild_display_for_view_state;
 * flight_view_render clears it. */
// GLOBAL: XVT 0x9CD264
uint16_t g_flight_initial_texture_cache_flush_pending = 0;
/* Ticks, cut to 16 bits, that the input clock advanced between two reads near
 * the end of flight_view_render, around the buffer unlock, the cockpit
 * compositing and the target inset. Only flight_view_render writes and reads
 * it. */
// GLOBAL: XVT 0x9A7BA8
uint16_t g_flight_post_scene_duration_ticks = 0;
/* Set to 0 by flight_view_render, its only writer; nothing reads it. */
// GLOBAL: XVT 0x9E9660
uint16_t g_flight_render_scratch_word = 0;

/* Lays the cockpit and HUD layer (g_flight_offscreen_surface) over the 3D frame
 * in g_flight_back_buffer, centered in the display mode: rows above and below the
 * viewport whole, and in the viewport's rows the parts left and right of it and
 * the runs the span mask marks for copying. The mask, at g_flight_aux_buffer plus
 * g_viewport_span_mask_offset, holds per viewport row a signed first byte, then
 * run lengths (a 0 byte means the next byte plus 255, or after two 0 bytes the
 * third plus 511); the runs alternate in sign from the first byte, and the
 * negative ones are copied. Returns the back buffer's unlock result, or a
 * lock's error other than still drawing, which leaves the back buffer locked
 * when the second lock fails. flight_view_render calls it with hardware 3D. The
 * modern build latches the composition (xvt_cockpit_latch_composition) after a
 * good unlock. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x40B7B0
HRESULT flight_view_composite_masked_software_surface(void)
{
	DDSURFACEDESC surface_desc;
	HRESULT lock_result;
	uint16_t *back_buffer_pixels;
	uint16_t *offscreen_pixels;
	uint16_t *destination;
	uint16_t *source;
	uint8_t *mask_cursor;
	int back_buffer_pitch;
	int offscreen_pitch;
	int8_t run_type;
	int run_length;
	int decoded_width;
	int copy_width;
	int viewport_row;
	unsigned int row;

	memset(&surface_desc, 0, sizeof(surface_desc));
	surface_desc.dwSize = sizeof(surface_desc);
	for (;;) {
		lock_result = g_flight_back_buffer->lpVtbl->Lock(
			g_flight_back_buffer, NULL, &surface_desc, 0, NULL);
		if (lock_result == 0) {
			break;
		}
		if (lock_result != DX_DDERR_WASSTILLDRAWING) {
			return lock_result;
		}
	}
	destination = surface_desc.lpSurface;
	back_buffer_pixels = destination;
	back_buffer_pitch = surface_desc.lPitch;

	memset(&surface_desc, 0, sizeof(surface_desc));
	surface_desc.dwSize = sizeof(surface_desc);
	for (;;) {
		lock_result = g_flight_offscreen_surface->lpVtbl->Lock(
			g_flight_offscreen_surface, NULL, &surface_desc, 0,
			NULL);
		if (lock_result == 0) {
			break;
		}
		if (lock_result != DX_DDERR_WASSTILLDRAWING) {
			return lock_result;
		}
	}
	source = surface_desc.lpSurface;
	offscreen_pixels = source;
	offscreen_pitch = surface_desc.lPitch;

	destination =
		(uint16_t *)((uint8_t *)destination +
			     g_flight_bytes_per_pixel *
				     ((unsigned int)(g_display_mode_width -
						     g_surface_width) >>
				      1));
	destination =
		(uint16_t *)((uint8_t *)destination +
			     back_buffer_pitch *
				     ((unsigned int)(g_display_mode_height -
						     g_surface_height) >>
				      1));

	for (row = 0; row < (unsigned int)g_flight_vp_y; ++row) {
		if ((g_surface_width & 1) != 0) {
			*destination = *source;
			if (g_surface_width != 0) {
				memcpy(destination + 1, source + 1,
				       g_flight_bytes_per_pixel *
					       (g_surface_width - 1));
			}
		} else {
			copy_width = g_surface_width;
			memcpy(destination, source,
			       copy_width * g_flight_bytes_per_pixel);
		}
		source = (uint16_t *)((uint8_t *)source + offscreen_pitch);
		destination = (uint16_t *)((uint8_t *)destination +
					   back_buffer_pitch);
	}

	mask_cursor = &g_flight_aux_buffer[g_viewport_span_mask_offset];
	for (viewport_row = 0; viewport_row < g_flight_vp_height;
	     ++viewport_row) {
		if (g_flight_vp_x != 0) {
			if ((g_flight_vp_x & 1) != 0) {
				*destination = *source;
				if (g_flight_vp_x != 0) {
					memcpy(destination + 1, source + 1,
					       g_flight_bytes_per_pixel *
						       (g_flight_vp_x - 1));
				}
			} else {
				memcpy(destination, source,
				       g_flight_bytes_per_pixel *
					       g_flight_vp_x);
			}
			source = (uint16_t *)((uint8_t *)source +
					      g_flight_bytes_per_pixel *
						      g_flight_vp_x);
			destination = (uint16_t *)((uint8_t *)destination +
						   g_flight_bytes_per_pixel *
							   g_flight_vp_x);
		}
		decoded_width = 0;
		run_type = (int8_t)*mask_cursor++;
		while (decoded_width < g_flight_vp_width) {
			run_length = *mask_cursor++;
			if (run_length == 0) {
				run_length = *mask_cursor++;
				if (run_length == 0) {
					run_length = *mask_cursor++ + 256;
				}
				run_length += 255;
			}
			if (run_type < 0) {
				if ((run_length & 1) != 0) {
					*destination = *source;
					if (run_length != 0) {
						memcpy(destination + 1,
						       source + 1,
						       g_flight_bytes_per_pixel *
							       (run_length -
								1));
					}
				} else {
					memcpy(destination, source,
					       g_flight_bytes_per_pixel *
						       run_length);
				}
			}
			source = (uint16_t *)((uint8_t *)source +
					      g_flight_bytes_per_pixel *
						      run_length);
			destination = (uint16_t *)((uint8_t *)destination +
						   g_flight_bytes_per_pixel *
							   run_length);
			decoded_width += run_length;
			run_type = -run_type;
		}

		decoded_width += g_flight_vp_x;
		if ((unsigned int)g_surface_width >
		    (unsigned int)decoded_width) {
			run_length = g_surface_width - decoded_width;
			if ((run_length & 1) != 0) {
				*destination = *source;
				if (run_length != 0) {
					memcpy(destination + 1, source + 1,
					       g_flight_bytes_per_pixel *
						       (run_length - 1));
				}
			} else {
				memcpy(destination, source,
				       g_flight_bytes_per_pixel * run_length);
			}
			source = (uint16_t *)((uint8_t *)source +
					      g_flight_bytes_per_pixel *
						      run_length);
			destination = (uint16_t *)((uint8_t *)destination +
						   g_flight_bytes_per_pixel *
							   run_length);
			decoded_width = g_surface_width;
		}
		source = (uint16_t *)((uint8_t *)source + offscreen_pitch -
				      g_flight_bytes_per_pixel * decoded_width);
		destination =
			(uint16_t *)((uint8_t *)destination +
				     back_buffer_pitch -
				     g_flight_bytes_per_pixel * decoded_width);
	}

	for (row = g_flight_vp_y + g_flight_vp_height;
	     (unsigned int)g_surface_height > row; ++row) {
		if ((g_surface_width & 1) != 0) {
			*destination = *source;
			if (g_surface_width != 0) {
				memcpy(destination + 1, source + 1,
				       g_flight_bytes_per_pixel *
					       (g_surface_width - 1));
			}
		} else {
			copy_width = g_surface_width;
			memcpy(destination, source,
			       copy_width * g_flight_bytes_per_pixel);
		}
		source = (uint16_t *)((uint8_t *)source + offscreen_pitch);
		destination = (uint16_t *)((uint8_t *)destination +
					   back_buffer_pitch);
	}

	g_flight_offscreen_surface->lpVtbl->Unlock(g_flight_offscreen_surface,
						   offscreen_pixels);
#ifdef XVT_MODERN
	lock_result = g_flight_back_buffer->lpVtbl->Unlock(g_flight_back_buffer,
							   back_buffer_pixels);
	if (lock_result == DX_DD_OK) {
		xvt_cockpit_latch_composition();
	}
	return lock_result;
#else
	return g_flight_back_buffer->lpVtbl->Unlock(g_flight_back_buffer,
						    back_buffer_pixels);
#endif
}

/* Turns a player's view by input and stores the new view angles (a full circle
 * is 65,536). It builds the camera from viewState's roll, pitch and yaw, turns
 * it by pitch_step about its side axis and, unless g_flight_key_mods selects roll
 * ((g_flight_key_mods & 0xE) == 2), by yaw_or_roll_step about its up axis, then
 * reads view_pitch and view_yaw back from its forward axis; it takes that yaw and
 * pitch back out to read view_roll, and in roll mode adds yaw_or_roll_step to it.
 * Returns the new view_roll. Writes g_cur_mat_r0_x to g_cur_mat_r2_z, and the camera
 * matrix and g_fview axis globals through fview_build_camera_orient. */
// FUNCTION: XVT 0x438330
int16_t flight_view_rotate_view_by_input(int pitch_step, int yaw_or_roll_step,
					 int player_idx)
{
	int16_t pitch;
	int16_t yaw;
	int16_t pitch_cos;
	int16_t pitch_sin;
	int16_t yaw_cos;
	int16_t yaw_sin;
	int16_t pitch_cos_yaw_cos;
	int16_t pitch_cos_yaw_sin;
	int16_t pitch_sin_yaw_cos;
	int16_t pitch_sin_yaw_sin;
	int negative_yaw_sin;
	int negative_pitch_sin;
	int16_t rotated_x;
	int16_t rotated_y;
	int16_t rotated_z;
	int16_t result;

	fview_build_camera_orient(g_players[player_idx].view_state.view_roll,
				  g_players[player_idx].view_state.view_pitch,
				  g_players[player_idx].view_state.view_yaw, 0,
				  0, 0, NULL);
	g_cur_mat_r2_x = -g_fview_forward_x_q15;
	g_cur_mat_r2_y = -g_fview_forward_y_q15;
	g_cur_mat_r1_x = g_fview_up_x_q15;
	g_cur_mat_r1_y = g_fview_up_y_q15;
	g_cur_mat_r2_z = -g_fview_forward_z_q15;
	g_cur_mat_r1_z = g_fview_up_z_q15;
	g_cur_mat_r0_x = g_fview_side_x_q15;
	g_cur_mat_r0_y = g_fview_side_y_q15;
	g_cur_mat_r0_z = g_fview_side_z_q15;
	fview_transformaxes(g_fview_side_x_q15, g_fview_side_y_q15,
			    g_fview_side_z_q15, (int16_t)pitch_step);
	if ((g_flight_key_mods & 0xE) != 2) {
		fview_transformaxes(g_cur_mat_r1_x, g_cur_mat_r1_y,
				    g_cur_mat_r1_z, (int16_t)yaw_or_roll_step);
	}

	pitch = trig2_w_arccos((int16_t)-g_cur_mat_r2_z);
	yaw = (int16_t)-trig2_arctan(g_cur_mat_r2_x, -g_cur_mat_r2_y);
	g_players[player_idx].view_state.view_yaw = yaw;
	g_players[player_idx].view_state.view_pitch = pitch;
	yaw_cos = trig2_getsignedcos(yaw);
	yaw_sin = trig2_getsignedsin(yaw);
	pitch_cos = trig2_getsignedcos(pitch);
	pitch_sin = trig2_getsignedsin(pitch);
	pitch_cos_yaw_sin = (int16_t)math_mul_q15(yaw_sin, pitch_cos);
	pitch_sin_yaw_sin = (int16_t)math_mul_q15(yaw_sin, pitch_sin);
	pitch_cos_yaw_cos = (int16_t)math_mul_q15(yaw_cos, pitch_cos);
	pitch_sin_yaw_cos = (int16_t)math_mul_q15(yaw_cos, pitch_sin);
	negative_yaw_sin = (int16_t)-yaw_sin;
	negative_pitch_sin = (int16_t)-pitch_sin;

	rotated_x = (int16_t)math_dot3q15_wrapped(
		g_cur_mat_r0_x, g_cur_mat_r0_y, g_cur_mat_r0_z, yaw_cos,
		negative_yaw_sin, 0);
	rotated_y = (int16_t)math_dot3q15_wrapped(
		g_cur_mat_r0_x, g_cur_mat_r0_y, g_cur_mat_r0_z,
		pitch_cos_yaw_sin, pitch_cos_yaw_cos, negative_pitch_sin);
	rotated_z = (int16_t)math_dot3q15_wrapped(
		g_cur_mat_r0_x, g_cur_mat_r0_y, g_cur_mat_r0_z,
		pitch_sin_yaw_sin, pitch_sin_yaw_cos, pitch_cos);
	g_cur_mat_r0_x = rotated_x;
	g_cur_mat_r0_y = rotated_y;
	g_cur_mat_r0_z = rotated_z;

	rotated_x = (int16_t)math_dot3q15_wrapped(
		g_cur_mat_r1_x, g_cur_mat_r1_y, g_cur_mat_r1_z, yaw_cos,
		negative_yaw_sin, 0);
	rotated_y = (int16_t)math_dot3q15_wrapped(
		g_cur_mat_r1_x, g_cur_mat_r1_y, g_cur_mat_r1_z,
		pitch_cos_yaw_sin, pitch_cos_yaw_cos, negative_pitch_sin);
	rotated_z = (int16_t)math_dot3q15_wrapped(
		g_cur_mat_r1_x, g_cur_mat_r1_y, g_cur_mat_r1_z,
		pitch_sin_yaw_sin, pitch_sin_yaw_cos, pitch_cos);
	g_cur_mat_r1_x = rotated_x;
	g_cur_mat_r1_y = rotated_y;
	g_cur_mat_r1_z = rotated_z;

	rotated_x = (int16_t)math_dot3q15_wrapped(
		g_cur_mat_r2_x, g_cur_mat_r2_y, g_cur_mat_r2_z, yaw_cos,
		negative_yaw_sin, 0);
	rotated_y = (int16_t)math_dot3q15_wrapped(
		g_cur_mat_r2_x, g_cur_mat_r2_y, g_cur_mat_r2_z,
		pitch_cos_yaw_sin, pitch_cos_yaw_cos, negative_pitch_sin);
	rotated_z = (int16_t)math_dot3q15_wrapped(
		g_cur_mat_r2_x, g_cur_mat_r2_y, g_cur_mat_r2_z,
		pitch_sin_yaw_sin, pitch_sin_yaw_cos, pitch_cos);
	g_cur_mat_r2_x = rotated_x;
	g_cur_mat_r2_y = rotated_y;
	g_cur_mat_r2_z = rotated_z;

	result = (int16_t)-trig2_arctan(g_cur_mat_r0_y, g_cur_mat_r0_x);
	g_players[player_idx].view_state.view_roll = result;
	if ((g_flight_key_mods & 0xE) == 2) {
		result = (int16_t)(result + yaw_or_roll_step);
		g_players[player_idx].view_state.view_roll = result;
	}
	return result;
}

/* Places a player's camera for this frame and builds the camera matrix
 * (fview_build_camera_orient), writing viewState's angles and camera_world_x to
 * camera_world_z. In the map view flight_map_update_camera does it. With no focus
 * object (camera_focus_obj_idx 0xFFFF) the camera stays where it is and looks at
 * the player's craft, roll 0; the modern build returns first when the player
 * has no craft. With the external camera on and no transition running, or a
 * transition running with input blocked, it takes the focus object's angles and
 * sits behind the focus object's position
 * (mission_resolve_object_or_mission_point_world_loc) along the view's forward axis
 * by camera_distance plus the object's bounds extent. During any other
 * transition hud_point_camera does it. Otherwise, the cockpit view, it takes the
 * focus object's angles and view_angle_d, caches the orientation in that object's
 * record, and sits at its position, plus the player's hardpoint offset when the
 * object is the player's own. Then, while the player's hyperspace_phase is 2,
 * from 531 ticks (0x213) on, it sets external_camera_active, the full-screen HUD
 * view (hud_set_hud_view_state) and a camera_focus_obj_idx of 0xFFFF on every call:
 * its test to skip that compares the 16-bit index with UINT_MAX and always
 * passes. For a player with a craft it then sets a level camera (pitch 0x4000)
 * that rolls 8 per tick past 590 ticks (0x24E), at the craft's Y less its
 * bounds extent, less the square of the ticks past 531, counted up to 236. */
// FUNCTION: XVT 0x44EB60
void flight_view_update_player_camera(int player_idx)
{
	enum {
		MAX_UNSCALED_EXTENT = INT16_MAX,
		HYPERSPACE_PHASE_TRANSITION = 2,
		HYPERSPACE_EXTERNAL_CAMERA_TICKS = 0x213,
		HYPERSPACE_CAMERA_ROLL_START_TICKS = 0x24E,
		HYPERSPACE_CAMERA_ROLL_RATE = 8,
		HYPERSPACE_CAMERA_ROLL_BIAS = 4720,
		CAMERA_PITCH_LEVEL = 0x4000,
	};

	uint16_t camera_focus_obj_idx;

	if (g_players[player_idx].map_camera_state != 0) {
		flight_map_update_camera(player_idx);
	} else {
		camera_focus_obj_idx =
			g_players[player_idx].view_state.camera_focus_obj_idx;
		if (camera_focus_obj_idx == UINT16_MAX) {
			struct object_record *player_object;

#ifdef XVT_MODERN
			/* Mission time-limit expiry marks empty slots connected; they have no craft to orbit. */
			if (g_players[player_idx].object_index == -1) {
				return;
			}
#endif
			player_object = &g_object_table[g_players[player_idx]
								.object_index];

			trig2_ctop(player_object->world_x -
					   g_players[player_idx]
						   .view_state.camera_world_x,
				   player_object->world_y -
					   g_players[player_idx]
						   .view_state.camera_world_y,
				   player_object->world_z -
					   g_players[player_idx]
						   .view_state.camera_world_z);
			g_players[player_idx].view_state.view_roll = 0;
			g_players[player_idx].view_state.view_pitch =
				trig2_pitch;
			g_players[player_idx].view_state.view_yaw =
				trig2_xyangle;
			fview_build_camera_orient(
				g_players[player_idx].view_state.view_roll,
				g_players[player_idx].view_state.view_pitch,
				g_players[player_idx].view_state.view_yaw, 0,
				g_players[player_idx].view_state.hud_aim_x,
				g_players[player_idx].view_state.hud_aim_y,
				NULL);
		} else if ((g_players[player_idx]
					    .view_state
					    .external_camera_active != 0 &&
			    g_players[player_idx]
					    .view_state.target_camera_active ==
				    0) ||
			   (g_players[player_idx]
					    .view_state.target_camera_active !=
				    0 &&
			    g_players[player_idx]
					    .view_state.player_input_blocked !=
				    0)) {
			int extent_shift;
			int camera_offset_x;
			int camera_offset_y;
			int camera_offset_z;

			g_players[player_idx].view_state.view_roll =
				g_object_table[camera_focus_obj_idx].roll;
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
			fview_build_camera_orient(
				g_players[player_idx].view_state.view_roll,
				g_players[player_idx].view_state.view_pitch,
				g_players[player_idx].view_state.view_yaw, 0,
				g_players[player_idx].view_state.hud_aim_x,
				g_players[player_idx].view_state.hud_aim_y,
				NULL);

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

			extent_shift = 0;
			g_current_object_bounds_extent =
				g_object_type_table
					[g_object_table
						 [g_players[player_idx]
							  .view_state
							  .camera_focus_obj_idx]
							 .object_type]
						.max_bounds_extent;
			while (g_current_object_bounds_extent >
			       MAX_UNSCALED_EXTENT) {
				++extent_shift;
				g_current_object_bounds_extent >>= 1;
			}

			camera_offset_x = math_mul_q15(
				g_current_object_bounds_extent, g_cam_mat_r2_x);
			camera_offset_y = math_mul_q15(
				g_current_object_bounds_extent, g_cam_mat_r2_y);
			camera_offset_z = math_mul_q15(
				g_current_object_bounds_extent, g_cam_mat_r2_z);
			if (extent_shift != 0) {
				camera_offset_x <<= extent_shift;
				camera_offset_y <<= extent_shift;
				camera_offset_z <<= extent_shift;
			}
			g_players[player_idx].view_state.camera_world_x -=
				camera_offset_x;
			g_players[player_idx].view_state.camera_world_y -=
				camera_offset_y;
			g_players[player_idx].view_state.camera_world_z -=
				camera_offset_z;
		} else if (g_players[player_idx]
				   .view_state.target_camera_active != 0) {
			hud_point_camera(camera_focus_obj_idx, 0, player_idx);
		} else {
			g_players[player_idx].view_state.view_roll =
				g_object_table[camera_focus_obj_idx].roll;
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
			fview_build_camera_orient(
				g_players[player_idx].view_state.view_roll,
				g_players[player_idx].view_state.view_pitch,
				g_players[player_idx].view_state.view_yaw,
				g_players[player_idx].view_state.view_angle_d,
				g_players[player_idx].view_state.hud_aim_x,
				g_players[player_idx].view_state.hud_aim_y,
				&g_object_table[g_players[player_idx]
							.view_state
							.camera_focus_obj_idx]);
			g_players[player_idx].view_state.camera_world_x =
				g_object_table[g_players[player_idx]
						       .view_state
						       .camera_focus_obj_idx]
					.world_x;
			g_players[player_idx].view_state.camera_world_y =
				g_object_table[g_players[player_idx]
						       .view_state
						       .camera_focus_obj_idx]
					.world_y;
			g_players[player_idx].view_state.camera_world_z =
				g_object_table[g_players[player_idx]
						       .view_state
						       .camera_focus_obj_idx]
					.world_z;
			if (g_object_table[g_players[player_idx]
						   .view_state
						   .camera_focus_obj_idx]
				    .player_owner_idx == player_idx) {
				g_players[player_idx]
					.view_state.camera_world_x +=
					g_players[player_idx].hardpoint_world_x;
				g_players[player_idx]
					.view_state.camera_world_y +=
					g_players[player_idx].hardpoint_world_y;
				g_players[player_idx]
					.view_state.camera_world_z +=
					g_players[player_idx].hardpoint_world_z;
			}
		}
	}

	if (g_players[player_idx].hyperspace_phase ==
		    HYPERSPACE_PHASE_TRANSITION &&
	    g_players[player_idx].hyperspace_runtime.phase_elapsed_ticks >=
		    HYPERSPACE_EXTERNAL_CAMERA_TICKS) {
		if ((unsigned int)g_players[player_idx]
			    .view_state.camera_focus_obj_idx != UINT_MAX) {
			g_players[player_idx]
				.view_state.external_camera_active = 1;
			hud_set_hud_view_state(HUD_VIEW_FULL_SCREEN,
					       player_idx);
			g_players[player_idx].view_state.camera_focus_obj_idx =
				UINT16_MAX;
		}
		if (g_players[player_idx].object_index != -1) {
			int16_t hyperspace_camera_roll;
			int camera_drop_ticks;
			int camera_y;

			hyperspace_camera_roll = 0;
			if (g_players[player_idx]
				    .hyperspace_runtime.phase_elapsed_ticks >=
			    HYPERSPACE_CAMERA_ROLL_START_TICKS) {
				hyperspace_camera_roll =
					(int16_t)(HYPERSPACE_CAMERA_ROLL_RATE *
							  g_players[player_idx]
								  .hyperspace_runtime
								  .phase_elapsed_ticks -
						  HYPERSPACE_CAMERA_ROLL_BIAS);
			}
			fview_build_camera_orient(hyperspace_camera_roll,
						  CAMERA_PITCH_LEVEL, 0, 0, 0,
						  0, NULL);
			camera_y =
				g_object_table[g_players[player_idx]
						       .object_index]
					.world_y -
				g_object_type_table
					[g_object_table[g_players[player_idx]
								.object_index]
						 .object_type]
						.max_bounds_extent;
			g_players[player_idx].view_state.camera_world_y =
				camera_y;
			camera_drop_ticks = (int)g_players[player_idx]
						    .hyperspace_runtime
						    .phase_elapsed_ticks -
					    HYPERSPACE_EXTERNAL_CAMERA_TICKS;
			if (camera_drop_ticks > SIMULATION_TICKS_PER_SECOND) {
				camera_drop_ticks = SIMULATION_TICKS_PER_SECOND;
			}
			g_players[player_idx].view_state.camera_world_y =
				camera_y -
				camera_drop_ticks * camera_drop_ticks;
		}
	}
}

/* Draws the local player's view for this frame, with g_flight_draw_to_hud_layer 0,
 * leaving it 1. In the map view it draws the map (flight_map_render_view), and
 * while the local player's hyperspace_phase is 2, before 531 ticks, only the
 * streaks (flight_hyperspace_render_transition_effect); at hyperspace_phase 1 it
 * asks for new streaks first. Otherwise it flushes the texture cache when
 * g_flight_initial_texture_cache_flush_pending is set and queues the objects that
 * may be in view: craft, projectiles, small debris and explosions among the
 * main slots (flight_view_project_and_test_sphere_visible), passing over the slots
 * from g_local_transient_slot_start to g_local_debris_slot_end when debris is off, in
 * the proving grounds or in hyperspace, and over the camera's focus object in
 * the cockpit view outside replay view; and static objects of the mine to small
 * debris genera (flight_view_cull_world_sphere_to_viewport). It sorts them
 * (render_list_sort_depth_ascending) and draws each by genus: craft as models with
 * lighting and damage billboards, course obstacles by
 * proving_grounds_draw_course_object, object type 36 without bilinear filtering,
 * projectiles, debris and explosions as billboards, statics through
 * render_non_craft_scene_object. The backdrop and starfield are drawn first with
 * hardware 3D and after the objects in software, where the queued billboards
 * and the target boxes follow the faces (sw3d_draw_visible_faces_to_surface). Every
 * path then lays the cockpit layer over the frame with hardware 3D
 * (flight_view_composite_masked_software_surface), draws the target inset and blits
 * the HUD text panes and MFD pages. The full path also adds the elapsed ticks
 * to g_input_timestamp twice and sets g_flight_post_scene_duration_ticks. The modern
 * build first records the view for its renderer
 * (xvt_render_capture_capture_view). */
// FUNCTION: XVT 0x44F140
void flight_view_render(void)
{
	enum {
		HYPERSPACE_PHASE_STARTING = 1,
		HYPERSPACE_PHASE_TRANSITION = 2,
		HYPERSPACE_TRANSITION_RENDER_TICKS = 0x213,
		BACKDROP_BILLBOARD_TYPE_INDEX = 0x3000,
		NO_BILINEAR_OBJECT_TYPE = 36,
	};

	int16_t main_object_index;
#ifdef XVT_MODERN
	xvt_render_capture_capture_view();
#endif

	if (g_players[g_local_player].map_camera_state != 0) {
		g_flight_draw_to_hud_layer = 0;
		g_flight_surface_already_locked = 0;
		flight_map_render_view();
		if (g_use_hardware3d != 0) {
			flight_view_composite_masked_software_surface();
		}
		hud_draw_hud_target_inset_if_enabled(g_local_player);
		flight_surface_lock();
		hud_blit_software_hud_text_panes();
		hud_blit_software_mfd_pages();
		flight_surface_unlock();
		g_flight_draw_to_hud_layer = 1;
		return;
	}

	if (g_players[g_local_player].hyperspace_phase ==
	    HYPERSPACE_PHASE_TRANSITION) {
		if (g_players[g_local_player]
			    .hyperspace_runtime.phase_elapsed_ticks <
		    HYPERSPACE_TRANSITION_RENDER_TICKS) {
			g_flight_draw_to_hud_layer = 0;
			g_flight_surface_already_locked = 0;
			render_scene_initialize(1);
			flight_hyperspace_render_transition_effect();
			sw3d_draw_visible_faces_to_surface();
			if (g_use_hardware3d != 0) {
				flight_view_composite_masked_software_surface();
			}
			hud_draw_hud_target_inset_if_enabled(g_local_player);
			flight_surface_lock();
			hud_blit_software_hud_text_panes();
			hud_blit_software_mfd_pages();
			flight_surface_unlock();
			g_flight_draw_to_hud_layer = 1;
			return;
		}
	} else if (g_players[g_local_player].hyperspace_phase ==
		   HYPERSPACE_PHASE_STARTING) {
		flight_hyperspace_request_transition_effect_initialization();
	}

	g_flight_draw_to_hud_layer = 0;
	g_flight_surface_already_locked = 0;
	if (g_flight_initial_texture_cache_flush_pending != 0) {
		if (g_use_hardware3d != 0) {
			std3d_flush_texture_cache();
		}
		g_flight_initial_texture_cache_flush_pending = 0;
	}
	render_scene_initialize(1);
	g_scene_billboard_queue_count = 0;
	if (g_use_hardware3d != 0) {
		g_billboard_object_or_type_index =
			BACKDROP_BILLBOARD_TYPE_INDEX;
		backdrop_build_star_offsets_and_render();
		flight_surface_lock();
		flight_starfield_render();
		flight_surface_unlock();
	}

	render_list_reset();
	main_object_index = 0;
	if (g_region_main_object_slot_end > 0) {
		do {
			struct object_record *object;
			uint16_t object_type;

			if (main_object_index == g_local_transient_slot_start &&
			    (g_debris_enabled == 0 ||
			     g_flight_mission_state
					     .proving_grounds_mode_active !=
				     0 ||
			     g_players[g_local_player].hyperspace_phase ==
				     HYPERSPACE_PHASE_TRANSITION)) {
				main_object_index =
					(int16_t)g_local_debris_slot_end;
				if (main_object_index ==
				    g_region_main_object_slot_end) {
					break;
				}
			}
			if (g_players[g_local_player]
					    .view_state.camera_focus_obj_idx !=
				    main_object_index ||
			    g_players[g_local_player]
					    .view_state
					    .external_camera_active != 0 ||
			    g_replay_view_mode != 0) {
				object = &g_object_table[main_object_index];
				object_type = object->object_type;
				if (object_type != 0) {
					g_current_object_bounds_extent =
						g_object_type_table[object_type]
							.max_bounds_extent;
					switch (object->genus_id) {
					case CRAFT_GENUS_STARFIGHTER:
					case CRAFT_GENUS_TRANSPORT:
					case CRAFT_GENUS_UTILITY_VEHICLE:
					case CRAFT_GENUS_FREIGHTER:
					case CRAFT_GENUS_STARSHIP:
					case CRAFT_GENUS_PLATFORM:
					case CRAFT_GENUS_OBSTACLE:
						g_cur_craft =
							object->mobj->p_craft;
						if (flight_view_project_and_test_sphere_visible(
							    main_object_index,
							    g_current_object_bounds_extent) !=
						    0) {
							render_list_queue_object(
								main_object_index,
								g_view_space_depth);
						}
						break;
					case CRAFT_GENUS_PLAYER_PROJECTILE:
					case CRAFT_GENUS_OTHER_PROJECTILE:
						if (flight_view_project_and_test_sphere_visible(
							    main_object_index,
							    g_current_object_bounds_extent) !=
						    0) {
							render_list_queue_object(
								main_object_index,
								g_view_space_depth);
						}
						break;
					case CRAFT_GENUS_SMALL_DEBRIS:
						if (flight_view_project_and_test_sphere_visible(
							    main_object_index,
							    g_current_object_bounds_extent) !=
						    0) {
							render_list_queue_object(
								main_object_index,
								g_view_space_depth);
						}
						break;
					case CRAFT_GENUS_EXPLOSION:
						if (flight_view_project_and_test_sphere_visible(
							    main_object_index,
							    g_current_object_bounds_extent) !=
						    0) {
							render_list_queue_object(
								main_object_index,
								g_view_space_depth);
						}
						break;
					default:
						break;
					}
				}
			}
			++main_object_index;
		} while (main_object_index < g_region_main_object_slot_end);
	}

	{
		int16_t static_object_index;

		for (static_object_index =
			     (int16_t)g_region_main_object_slot_end;
		     static_object_index <
		     g_region_static_object_slot_count +
			     g_region_main_object_slot_end;
		     ++static_object_index) {
			struct object_record *object =
				&g_object_table[static_object_index];
			int genus_id;
			uint16_t object_type;

			object_type = object->object_type;
			if (object_type == 0) {
				continue;
			}
			g_current_object_bounds_extent =
				g_object_type_table[object_type]
					.max_bounds_extent;
			genus_id = object->genus_id;
			if (genus_id >= CRAFT_GENUS_MINE &&
			    genus_id <= CRAFT_GENUS_SMALL_DEBRIS &&
			    flight_view_cull_world_sphere_to_viewport(
				    object->world_x, object->world_y,
				    object->world_z,
				    g_current_object_bounds_extent) != 0) {
				render_list_queue_object(static_object_index,
							 g_view_space_depth);
			}
		}
	}

	render_list_sort_depth_ascending();
	{
		struct render_object_list_entry *render_entry;
		int16_t render_object_index;
		int object_table_index;
		uint16_t genus_id;

		for (render_entry = g_render_list_head; render_entry != NULL;
		     render_entry = render_entry->next) {
			render_object_index = (int16_t)render_entry->object_idx;
			object_table_index = render_object_index;
			genus_id = g_object_table[object_table_index].genus_id;
			if (g_region_main_object_slot_end >
			    render_object_index) {
				switch ((int)genus_id) {
				case CRAFT_GENUS_STARFIGHTER:
				case CRAFT_GENUS_TRANSPORT:
				case CRAFT_GENUS_UTILITY_VEHICLE:
				case CRAFT_GENUS_FREIGHTER:
				case CRAFT_GENUS_STARSHIP:
				case CRAFT_GENUS_PLATFORM:
				case CRAFT_GENUS_OBSTACLE: {
					int saved_bilinear_enabled;

					g_cur_craft =
						g_object_table
							[object_table_index]
								.mobj->p_craft;
					flight_view_compute_object_view_position(
						render_object_index);
					if (genus_id == CRAFT_GENUS_OBSTACLE) {
						g_transform_light_direction_to_object_space =
							0;
					}
					if (g_object_table[object_table_index]
						    .object_type ==
					    NO_BILINEAR_OBJECT_TYPE) {
						saved_bilinear_enabled =
							g_bilinear_enabled;
						g_bilinear_enabled = 0;
					}
					fview_set_object_transform(
						g_object_table
							[object_table_index]
								.roll,
						g_object_table
							[object_table_index]
								.pitch,
						g_object_table
							[object_table_index]
								.yaw,
						0,
						&g_object_table
							[object_table_index]);
					if (genus_id == CRAFT_GENUS_OBSTACLE) {
						proving_grounds_draw_course_object(
							render_object_index);
					} else {
						flight_light_setup_object_lighting(
							&g_object_table
								[object_table_index]);
						damage_queue_craft_billboards(
							render_object_index);
						render_scene_draw_object_model(
							&g_object_table
								[object_table_index]);
						g_object_point_light_count = 0;
					}
					if (g_object_table[object_table_index]
						    .object_type ==
					    NO_BILINEAR_OBJECT_TYPE) {
						g_bilinear_enabled =
							saved_bilinear_enabled;
					}
					g_transform_light_direction_to_object_space =
						1;
					break;
				}
				case CRAFT_GENUS_PLAYER_PROJECTILE:
				case CRAFT_GENUS_OTHER_PROJECTILE:
					flight_view_compute_object_view_position(
						render_object_index);
					fview_set_object_transform(
						g_object_table
							[object_table_index]
								.roll,
						g_object_table
							[object_table_index]
								.pitch,
						g_object_table
							[object_table_index]
								.yaw,
						0,
						&g_object_table
							[object_table_index]);
					scene_billboard_draw_roll_aligned_object_model(
						render_object_index);
					break;
				case CRAFT_GENUS_SMALL_DEBRIS:
					flight_view_compute_object_view_position(
						render_object_index);
					fview_set_object_transform(
						g_object_table
							[object_table_index]
								.roll,
						g_object_table
							[object_table_index]
								.pitch,
						g_object_table
							[object_table_index]
								.yaw,
						0,
						&g_object_table
							[object_table_index]);
					scene_billboard_draw_or_queue_object(
						render_object_index);
					break;
				case CRAFT_GENUS_EXPLOSION:
					flight_view_compute_object_view_position(
						render_object_index);
					fview_set_object_transform(
						g_object_table
							[object_table_index]
								.roll,
						g_object_table
							[object_table_index]
								.pitch,
						g_object_table
							[object_table_index]
								.yaw,
						0,
						&g_object_table
							[object_table_index]);
					scene_billboard_draw_or_queue_object(
						render_object_index);
					break;
				default:
					break;
				}
			} else {
				int static_genus_id = genus_id;
				if (static_genus_id >= CRAFT_GENUS_MINE &&
				    static_genus_id <=
					    CRAFT_GENUS_SMALL_DEBRIS) {
					flight_view_compute_object_view_position(
						render_object_index);
					fview_set_object_transform(
						g_object_table
							[object_table_index]
								.roll,
						g_object_table
							[object_table_index]
								.pitch,
						g_object_table
							[object_table_index]
								.yaw,
						0, NULL);
					flight_light_setup_object_lighting(
						&g_object_table
							[object_table_index]);
					render_non_craft_scene_object(
						render_object_index);
					g_object_point_light_count = 0;
				}
			}
		}
	}

	if (g_use_hardware3d == 0) {
		g_billboard_object_or_type_index =
			BACKDROP_BILLBOARD_TYPE_INDEX;
		backdrop_build_star_offsets_and_render();
		flight_surface_lock();
		flight_starfield_render();
		flight_surface_unlock();
	}
	g_scene_flush_draw_target_markers = 1;
	sw3d_draw_visible_faces_to_surface();
	g_scene_flush_draw_target_markers = 0;
	if (g_use_hardware3d == 0) {
		scene_billboard_render_queued_textured(1);
		targeting_draw_scene_object_boxes();
	}
	g_flight_post_scene_duration_ticks = 0;
	g_flight_render_scratch_word = 0;
	g_input_timestamp += (int)time_consume_elapsed_ticks();
	g_flight_post_scene_duration_ticks = (uint16_t)g_input_timestamp;
	render_scene_unlock_buffers();
	if (g_use_hardware3d != 0) {
		flight_view_composite_masked_software_surface();
	}
	hud_draw_hud_target_inset_if_enabled(g_local_player);
	g_flight_background_color_index = 0;
	g_input_timestamp += (int)time_consume_elapsed_ticks();
	g_flight_background_color_index = g_flight_transparent_color_index;
	g_flight_post_scene_duration_ticks =
		(uint16_t)(g_input_timestamp -
			   g_flight_post_scene_duration_ticks);
	flight_surface_lock();
	hud_blit_software_hud_text_panes();
	hud_blit_software_mfd_pages();
	flight_surface_unlock();
	g_flight_draw_to_hud_layer = 1;
}

/* Sets g_cam_rel_world_x to g_cam_rel_world_z to an object's world position less the
 * local player's camera position, and g_view_space_x, g_view_space_y and
 * g_view_space_depth to that offset in view space; returns the depth. Does not
 * check object_idx. */
// FUNCTION: XVT 0x44FE40
int flight_view_compute_object_view_position(uint16_t object_idx)
{
	struct object_record *object;
	int camera_world_x;
	int camera_world_y;
	int camera_world_z;

	object = &g_object_table[object_idx];
	camera_world_x = g_players[g_local_player].view_state.camera_world_x;
	camera_world_y = g_players[g_local_player].view_state.camera_world_y;
	g_cam_rel_world_x = object->world_x - camera_world_x;
	camera_world_z = g_players[g_local_player].view_state.camera_world_z;
	g_cam_rel_world_y = object->world_y - camera_world_y;
	g_cam_rel_world_z = object->world_z - camera_world_z;
	g_view_space_x = transfm2_cam_mat_dot_row0(
		g_cam_rel_world_x, g_cam_rel_world_y, g_cam_rel_world_z);
	g_view_space_y = transfm2_cam_mat_dot_row1(
		g_cam_rel_world_x, g_cam_rel_world_y, g_cam_rel_world_z);
	g_view_space_depth = transfm2_cam_mat_dot_row2(
		g_cam_rel_world_x, g_cam_rel_world_y, g_cam_rel_world_z);
	return g_view_space_depth;
}

/* Returns 1 when a sphere of radius sphere_radius around an object may be in the
 * local player's view, else 0. It is out when depth plus radius is below 0
 * (wholly behind the eye), when that sum shifted right 8 exceeds the radius
 * (more than about 256 radii away), or when the size of its view X, or then of
 * its view Y, less the radius exceeds that sum. Writes g_cam_rel_world_x to
 * g_cam_rel_world_z, g_view_space_depth, and g_view_space_x and g_view_space_y as far as
 * the tests get. */
// FUNCTION: XVT 0x44FF10
int flight_view_project_and_test_sphere_visible(int object_idx,
						unsigned int sphere_radius)
{
	int depth_with_radius;
	int transformed_x;
	int transformed_y;
	int absolute_y;

	g_cam_rel_world_x = g_object_table[object_idx].world_x -
			    g_players[g_local_player].view_state.camera_world_x;
	g_cam_rel_world_y = g_object_table[object_idx].world_y -
			    g_players[g_local_player].view_state.camera_world_y;
	g_cam_rel_world_z = g_object_table[object_idx].world_z -
			    g_players[g_local_player].view_state.camera_world_z;
	g_view_space_depth = transfm2_cam_mat_dot_row2(
		g_cam_rel_world_x, g_cam_rel_world_y, g_cam_rel_world_z);
	depth_with_radius = g_view_space_depth + sphere_radius;
	if (depth_with_radius < 0) {
		return 0;
	}
	if ((unsigned int)(depth_with_radius >> 8) > sphere_radius) {
		return 0;
	}

	transformed_x = transfm2_cam_mat_dot_row0(
		g_cam_rel_world_x, g_cam_rel_world_y, g_cam_rel_world_z);
	g_view_space_x = transformed_x;
	if (transformed_x < 0) {
#ifdef XVT_MODERN
		transformed_x =
			transformed_x == INT32_MIN ? INT32_MAX : -transformed_x;
#else
		transformed_x = -transformed_x;
#endif
	}
	if ((int)((unsigned int)transformed_x - sphere_radius) >
	    depth_with_radius) {
		return 0;
	}

	transformed_y = transfm2_cam_mat_dot_row1(
		g_cam_rel_world_x, g_cam_rel_world_y, g_cam_rel_world_z);
	absolute_y = transformed_y;
	g_view_space_y = transformed_y;
	if (absolute_y < 0) {
#ifdef XVT_MODERN
		absolute_y = absolute_y == INT32_MIN ? INT32_MAX : -absolute_y;
#else
		absolute_y = -absolute_y;
#endif
	}
	return (int)((unsigned int)absolute_y - sphere_radius) <=
	       depth_with_radius;
}

/* flight_view_project_and_test_sphere_visible for a sphere at a world position
 * rather than an object's, with the same tests, here compared as signed:
 * returns 1 when it may be in view, else 0, and writes the same globals. */
// FUNCTION: XVT 0x450020
int flight_view_cull_world_sphere_to_viewport(int world_x, int world_y,
					      int world_z, int sphere_radius)
{
	int camera_world_y;
	int camera_world_z;
	int depth_with_radius;
	int transformed_x;
	int transformed_y;
	int absolute_y;

	camera_world_y = g_players[g_local_player].view_state.camera_world_y;
	g_cam_rel_world_x =
		world_x - g_players[g_local_player].view_state.camera_world_x;
	camera_world_z = g_players[g_local_player].view_state.camera_world_z;
	g_cam_rel_world_y = world_y - camera_world_y;
	g_cam_rel_world_z = world_z - camera_world_z;
	g_view_space_depth = transfm2_cam_mat_dot_row2(
		g_cam_rel_world_x, g_cam_rel_world_y, g_cam_rel_world_z);
	depth_with_radius = g_view_space_depth + sphere_radius;
	if (depth_with_radius < 0) {
		return 0;
	}
	if ((depth_with_radius >> 8) > sphere_radius) {
		return 0;
	}

	transformed_x = transfm2_cam_mat_dot_row0(
		g_cam_rel_world_x, g_cam_rel_world_y, g_cam_rel_world_z);
	g_view_space_x = transformed_x;
	if (transformed_x < 0) {
#ifdef XVT_MODERN
		transformed_x =
			transformed_x == INT32_MIN ? INT32_MAX : -transformed_x;
#else
		transformed_x = -transformed_x;
#endif
	}
	if (transformed_x - sphere_radius > depth_with_radius) {
		return 0;
	}

	transformed_y = transfm2_cam_mat_dot_row1(
		g_cam_rel_world_x, g_cam_rel_world_y, g_cam_rel_world_z);
	g_view_space_y = transformed_y;
	absolute_y = transformed_y;
	if (absolute_y < 0) {
#ifdef XVT_MODERN
		absolute_y = absolute_y == INT32_MIN ? INT32_MAX : -absolute_y;
#else
		absolute_y = -absolute_y;
#endif
	}
	return absolute_y - sphere_radius <= depth_with_radius;
}

/* Draws the first frame of a flight: places every active player's camera (the
 * other players' first, then the local one's), draws the HUD into the cockpit
 * layer (hud_render_hud) and lays that over the frame
 * (flight_display_blit_render_surface), places the cameras again, draws the view
 * (flight_view_render), flips (flight_display_flip) and lays the cockpit layer
 * over the new frame. The modern build brackets it for its frame capture. */
// FUNCTION: XVT 0x450110
void flight_view_render_startup_frame(void)
{
	int player_index;
#ifdef XVT_MODERN
	xvt_render_capture_begin_classic_frame();
#endif

	for (player_index = 0;
	     player_index < (int)(sizeof(g_players) / sizeof(g_players[0]));
	     ++player_index) {
		if (g_players[player_index].participation_state != 0 &&
		    player_index != g_local_player) {
			flight_view_update_player_camera(player_index);
		}
	}
	flight_view_update_player_camera(g_local_player);
	flight_surface_lock();
	hud_render_hud(g_local_player);
	flight_surface_unlock();
	flight_display_blit_render_surface();
	for (player_index = 0;
	     player_index < (int)(sizeof(g_players) / sizeof(g_players[0]));
	     ++player_index) {
		if (g_players[player_index].participation_state != 0 &&
		    player_index != g_local_player) {
			flight_view_update_player_camera(player_index);
		}
	}
	flight_view_update_player_camera(g_local_player);
	flight_view_render();
#ifdef XVT_MODERN
	xvt_render_capture_seal_view();
#endif
	flight_display_flip();
#ifdef XVT_MODERN
	xvt_render_capture_end_presentation();
#endif
	flight_display_blit_render_surface();
}

/* Draws one frame: places the cameras (other active players first, then the
 * local one), draws the view (flight_view_render), applies and latches the local
 * player's replay record (flight_input_read with the local player's index,
 * flight_input_latch_flight_controls), draws the HUD (hud_render_hud) and flips.
 * Then, with hardware 3D, it clears the frame buffers
 * (render_scene_clear_frame_buffers); in software it lays the cockpit layer over
 * the next frame (flight_display_blit_render_surface). The modern build brackets
 * it for its frame capture. */
// FUNCTION: XVT 0x4501C0
void flight_view_render_frame(void)
{
	int player_index;
#ifdef XVT_MODERN
	xvt_render_capture_begin_classic_frame();
#endif

	for (player_index = 0;
	     player_index < (int)(sizeof(g_players) / sizeof(g_players[0]));
	     ++player_index) {
		if (g_players[player_index].participation_state != 0 &&
		    player_index != g_local_player) {
			flight_view_update_player_camera(player_index);
		}
	}
	flight_view_update_player_camera(g_local_player);
	flight_view_render();
	flight_input_read(g_local_player);
	flight_input_latch_flight_controls();
	flight_surface_lock();
	hud_render_hud(g_local_player);
	flight_surface_unlock();
#ifdef XVT_MODERN
	xvt_render_capture_seal_view();
#endif
	flight_display_flip();
#ifdef XVT_MODERN
	xvt_render_capture_end_presentation();
#endif
	nullsub_11();
	if (g_use_hardware3d != 0) {
		render_scene_clear_frame_buffers();
	} else {
		flight_display_blit_render_surface();
	}
}
