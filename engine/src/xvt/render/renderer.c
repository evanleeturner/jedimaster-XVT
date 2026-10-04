#include "xvt/render/renderer.h"
#ifdef XVT_MODERN
#include "xvt_runtime/log/log.h"
#endif

#include <string.h>

#include "xvt/flight/flight_display.h"
#include "xvt/math/math.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/render_texture.h"
#include "xvt/render/std3d.h"
#include "xvt/util/debug_console.h"

/* Level-of-detail child render_scene_draw_model_node takes in every face group, 0
 * to choose by distance (child 1 at a view depth of 0 or less). Only
 * flight_main (original build) and xvt_flight_entry_configure_lod_distance (modern)
 * write it, setting 0, so no level is ever forced. */
// GLOBAL: XVT 0x5233A4
int g_forced_lod_level = 0;
/* Factor on view depth when render_scene_draw_model_node picks a level of detail,
 * from the level-of-detail option (flight_main in the original build,
 * xvt_flight_entry_configure_lod_distance in the modern one); 1.0 at start and in
 * the model preview (model_preview_reset_view_and_render_state). */
// GLOBAL: XVT 0x5233A8
float g_lod_distance_scale = 1.0f;
/* 1 when model textures get mipmap levels, built by opt_model_build_runtime_node
 * and chosen by the drawing code, 0 not. Set from the "nomipmaps" and "mipmaps"
 * launch options and the mipmap option (flight_main in the original build,
 * xvt_flight_entry_read_launch_switches and xvt_flight_entry_configure_mipmaps in the
 * modern one); model_preview_load_model sets 1. */
// GLOBAL: XVT 0x5233AC
int g_mipmapping_enabled = 1;
/* Factor on a face's texels per pixel when the mipmap level is chosen, from the
 * mipmap option; set with g_mipmapping_enabled, 1.0 at start and in the model
 * preview. */
// GLOBAL: XVT 0x5233B0
float g_mip_lod_scale = 1.0f;
/* The dithering option, 1 on, 0 off, set at flight start (flight_main,
 * xvt_flight_entry_configure) and 1 in the model preview; nothing reads it. */
// GLOBAL: XVT 0x5233B4
int g_dithering_enabled = 1;
/* The local lights option, 1 on, 0 off, set at flight start (flight_main,
 * xvt_flight_entry_configure); the object lighting setup lights from nearby
 * sources only while it is set. model_preview_render_viewport sets 0 while it
 * draws and puts it back. */
// GLOBAL: XVT 0x5233B8
int g_local_lights_enabled = 1;
/* The specular lighting option, 1 on, 0 off, set at flight start (flight_main,
 * xvt_flight_entry_configure) and 1 in the model preview; read by the face and
 * vertex lighting. */
// GLOBAL: XVT 0x5233BC
int g_specular_enabled = 1;
/* The diffuse (directional) lighting option, 1 on, 0 off, set at flight start
 * (flight_main, xvt_flight_entry_configure) and 1 in the model preview; read by
 * render_scene_compute_vertex_lighting. */
// GLOBAL: XVT 0x5233C0
int g_dir_lighting_enabled = 1;
/* The texture resolution option, 0 to 2, set at flight start (flight_main,
 * xvt_flight_entry_configure) and 1 in the model preview; read when model
 * textures are built. */
// GLOBAL: XVT 0x5233C4
int g_texture_resolution_level = 1;
/* The DirectDraw object flight draws with, which flight_display_init takes from
 * frontend_display_get_direct_draw; renderer_get_direct_draw returns it. */
// GLOBAL: XVT 0x66DDD4
IDirectDraw *g_flight_direct_draw;
/* Palette index used as the transparent color: blits skip it, hardware textures
 * put its pixel in their transparent entry, and flight_surface_clear_to_black
 * fills with it. 0xFB at start; fe_disk_io_init_resources sets it, with
 * g_flight_background_color_index, to the palette color nearest (0, 0, 2) at
 * flight start. */
// GLOBAL: XVT 0x5233C8
uint8_t g_flight_transparent_color_index = 0xfb;
/* Software drawing mode: 0 at 320x240 or an unknown resolution, 1 at 640x480
 * and 480x360, 2 in 16-bit color; only
 * flight_render_configure_callbacks_for_resolution writes it.
 * flight_palette_set_range keeps g_flight_palette16_bpp up to date in mode 2. */
// GLOBAL: XVT 0x9A7A30
uint8_t g_flight_pixel_mode = 0;
/* Graphics detail preset, 3 at start:
 * flight_render_configure_callbacks_for_resolution sets the starting one, and the
 * Alt+D key steps it, wrapping to 0 at GRAPHICS_DETAIL_PRESET_COUNT, and
 * applies it (flight_update_player_step in the original build,
 * xvt_flight_sim_update_player_step in the modern one). */
// GLOBAL: XVT 0x5233F8
uint8_t g_flight_graphics_detail_preset = 3;
/* 0 at start; only flight_render_configure_callbacks_for_resolution writes it,
 * setting 1. hud_rebuild_display_for_view_state passes it as the third argument of
 * set_flight_viewport, which ignores it. */
// GLOBAL: XVT 0xA08290
uint16_t g_flight_viewport_mode = 0;
/* 1 while flight draws through Direct3D hardware, 0 for the software renderer.
 * Set at flight start from the 3D hardware option (flight_main,
 * xvt_flight_entry_configure); renderer_init_d3d_device sets 0 when the device
 * cannot be used, flight_display_init when the color depth is not 16-bit, and
 * flight_main (original build) or xvt_flight_entry_cleanup (modern) when flight
 * ends. */
// GLOBAL: XVT 0x527E90
int g_use_hardware3d;
/* The bilinear filtering option for hardware drawing, 1 on, 0 off, set at
 * flight start (flight_main, xvt_flight_entry_configure). flight_view_render and
 * flight_hyperspace_render_transition_effect turn it off for part of a frame and
 * put it back. */
// GLOBAL: XVT 0x527E94
int g_bilinear_enabled = 1;
/* 1 while fe_disk_io_init_resources loads the flight's resources, else 0; only it
 * writes it. display_is_pixel_format555 reads it. */
// GLOBAL: XVT 0x527EC8
int g_loading_model;
/* Width in pixels of the flight's drawing surface: 640 at start; at flight
 * start 320, 480 or 640 from the window size option (flight_main,
 * xvt_flight_entry_configure_display_size). */
// GLOBAL: XVT 0x5233E0
int g_surface_width = 640;
/* Height in pixels of the flight's drawing surface: 480 at start; at flight
 * start 240, 360 or 480 with g_surface_width. */
// GLOBAL: XVT 0x5233E8
int g_surface_height = 480;
/* Address of the first pixel of the surface flight draws into, set by
 * flight_surface_lock each time it locks one; 0xA0000 at start. */
// GLOBAL: XVT 0x527ECC
void *g_surface_pixels = (void *)0xA0000;
/* Width in pixels of the display mode: 320, 512 or 640 from the screen
 * resolution option at flight start (flight_main,
 * xvt_flight_entry_configure_display_size); flight_display_init changes it when it
 * has to fall back to another mode. */
// GLOBAL: XVT 0x66DDC4
int g_display_mode_width;
/* Height in pixels of the display mode: 240, 384 or 480, written with
 * g_display_mode_width. */
// GLOBAL: XVT 0x66DDE0
int g_display_mode_height;
/* Width in pixels flight's drawing code lays the screen out for: 320, 640 or
 * 480 from g_flight_resolution_mode, 320 for any other mode; only
 * flight_display_configure_resolution_state writes it. 0 at start. */
// GLOBAL: XVT 0x9A7BAC
unsigned int g_screen_width = 0;
/* A screen-sized work buffer, locked from its memory handle by
 * fe_disk_io_init_global_buffers and fe_disk_io_lock_global_buffers; the HUD and damage
 * displays draw into it and copy from it. */
// GLOBAL: XVT 0x9A806C
void *g_flight_offscreen_buffer = 0;
/* Top row of the flight viewport, in pixels: the viewport's byte offset divided
 * by g_surface_pitch. Written by set_flight_viewport, push_flight_viewport,
 * pop_flight_viewport and model_preview_render_viewport. */
// GLOBAL: XVT 0x9A7B60
int g_flight_vp_y = 0;
/* Height of the flight viewport in pixels, written with g_flight_vp_y. */
// GLOBAL: XVT 0x9A1FE4
uint16_t g_flight_vp_height = 0;
/* Half of g_flight_vp_height, rounded down, written with g_flight_vp_y; the
 * projection centers the view on it. */
// GLOBAL: XVT 0x9A20A2
uint16_t g_flight_vp_center_y = 0;
/* Height in pixels flight's drawing code lays the screen out for: 240, 480 or
 * 360, written with g_screen_width. */
// GLOBAL: XVT 0x9A8C14
unsigned int g_screen_height = 0;
/* Left column of the flight viewport, in pixels: the remainder of its byte
 * offset by g_surface_pitch, divided by g_flight_bytes_per_pixel. Written with
 * g_flight_vp_y. */
// GLOBAL: XVT 0x9A8E3C
int g_flight_vp_x = 0;
/* Rows added to the projected screen Y, so the view's center can sit off the
 * viewport's middle: the cockpit's projection offset in
 * hud_rebuild_display_for_view_state, else 0; hud_update3d_crt sets 0 while it draws
 * and puts it back, and the model preview sets 0. */
// GLOBAL: XVT 0x9D7680
int g_proj_offset_y;
/* Projection scale: the drawing code turns a view offset into a screen offset
 * as offset * g_proj_scale_int / depth. 256 at 320x240, 512 at 640x480 and
 * 480x360 (flight_display_configure_resolution_state), 512 in the model
 * preview. */
// GLOBAL: XVT 0xA081EC
uint32_t g_proj_scale_int = 0;
/* Width of the flight viewport in pixels, written with g_flight_vp_y. */
// GLOBAL: XVT 0x9D682C
uint16_t g_flight_vp_width = 0;
/* Component of the local player's target that is selected, set by
 * flight_update_timers from selected_target_component; read by
 * damage_queue_craft_billboards_for_object_type. */
// GLOBAL: XVT 0x9D682E
uint16_t g_render_target_component_idx = 0;
/* g_flight_vp_width - 1, written with g_flight_vp_y. */
// GLOBAL: XVT 0x9CD272
uint16_t g_flight_vp_max_x = 0;
/* Object the targeting display draws for, with flags in its high bits:
 * flight_update_timers sets it to the local player's target object index OR
 * g_render_object_ref_flags OR g_target_proximity_blink_bit. hud_update3d_crt adds its
 * box and cross flags while it draws, and
 * damage_queue_craft_billboards_for_object_type and proving_grounds_draw_course_object
 * set it for one draw; each puts it back. mission_init_flight_runtime_state sets
 * 0xFFFF. */
// GLOBAL: XVT 0x9CC450
uint16_t g_render_object_ref = 0;
/* Flag bits flight_update_timers adds to g_render_object_ref: 1024 or 0, set by
 * flight_process_player_actions, and 0 from mission_init_flight_runtime_state and
 * player_validate_current_targets. */
// GLOBAL: XVT 0xA0810A
uint16_t g_render_object_ref_flags = 0;
/* g_flight_vp_height - 1, written with g_flight_vp_y. */
// GLOBAL: XVT 0x9D1260
uint16_t g_flight_vp_max_y = 0;
/* Fills the clip rectangle with the text background color:
 * flight_render_install_callbacks sets it to flight_sw_fill_clip_rect8bpp, or
 * flight_sw_fill_clip_rect16bpp in pixel mode 2. */
// GLOBAL: XVT 0x9D1300
void (*g_flight_fill_clip_rect_fn)(void) = 0;
/* Bytes from one row of the flight drawing surface to the next.
 * flight_display_configure_resolution_state sets it from the primary surface,
 * flight_display_init from its surface description, and flight_surface_lock from
 * the surface it locks. */
// GLOBAL: XVT 0x9D8C08
int g_surface_pitch = 0;
/* Byte offset of the flight viewport's first pixel in the drawing surface,
 * written with g_flight_vp_y. */
// GLOBAL: XVT 0x9ED234
unsigned int g_flight_vp_base_offset = 0;
/* Draws a run-length sprite faded: flight_render_install_callbacks sets it to
 * flight_sw_blit_sprite_rle_faded8bpp, or flight_sw_blit_sprite_rle_faded16bpp in pixel
 * mode 2. */
// GLOBAL: XVT 0x9D130C
flight_blit_sprite_faded_fn g_flight_blit_sprite_faded_fn = 0;
/* Draws a run-length sprite: flight_render_install_callbacks sets it to
 * flight_sw_blit_sprite_rle8bpp, or flight_sw_blit_sprite_rle16bpp in pixel mode
 * 2. */
// GLOBAL: XVT 0x9D80C4
flight_blit_sprite_fn g_flight_blit_sprite_fn = 0;
/* Draws one character of HUD text: flight_render_install_callbacks sets it to
 * flight_text_draw_wide_glyph8bpp, or flight_text_draw_wide_glyph in pixel mode 2. */
// GLOBAL: XVT 0x9A6FE4
flight_draw_char_fn g_flight_draw_char_fn = 0;
/* flight_render_install_callbacks sets it to flight_sw_init_framebuffer in every
 * pixel mode; nothing calls through it. */
// GLOBAL: XVT 0x9D77C0
void (*g_flight_init_line_buffer_fn)(void) = 0;
/* flight_render_install_callbacks sets it to flight_render_transition_hook_stub in
 * every pixel mode. */
// GLOBAL: XVT 0x9A8D44
void (*g_flight_render_transition_hook)(void) = 0;
/* Sends the palette to the display again: flight_render_install_callbacks sets it
 * to flight_palette_apply_to_display in every pixel mode. */
// GLOBAL: XVT 0x9A8C20
void (*g_flight_reset_palette_fn)(void) = 0;
/* Sets a range of palette colors: flight_render_install_callbacks sets it to
 * flight_palette_set_range in every pixel mode. */
// GLOBAL: XVT 0x9D77F4
flight_set_palette_range_fn g_flight_set_palette_range_fn = 0;
/* flight_render_install_callbacks sets it to flight_palette_get_full in every pixel
 * mode; nothing calls through it. */
// GLOBAL: XVT 0xA0810C
flight_palette_fn g_flight_get_palette_fn = 0;
/* flight_render_install_callbacks sets it to flight_palette_set_full in every pixel
 * mode; nothing calls through it. */
// GLOBAL: XVT 0x9FE7E0
flight_palette_fn g_flight_set_palette_fn = 0;
/* Byte offset of a pixel in the drawing surface: flight_render_install_callbacks
 * sets it to flight_sw_compute_pixel_offset8bpp, or flight_sw_compute_pixel_offset in
 * pixel mode 2. */
// GLOBAL: XVT 0x9A8C30
flight_compute_pixel_offset_fn g_flight_compute_pixel_offset_fn = 0;
/* Fills a clipped rectangle or its border: flight_render_install_callbacks sets
 * it to flight_sw_fill_rect_clipped8bpp, or flight_sw_fill_rect_clipped16bpp in pixel
 * mode 2. */
// GLOBAL: XVT 0x9ECA20
flight_fill_rect_clipped_fn g_flight_fill_rect_clipped_fn = 0;
/* Copies a screen rectangle out to a buffer: flight_render_install_callbacks sets
 * it to flight_sw_save_screen_rect8bpp, or flight_sw_save_screen_rect16bpp in pixel
 * mode 2. */
// GLOBAL: XVT 0x9D8B6C
flight_screen_rect_fn g_flight_save_screen_rect_fn = 0;
/* Copies a saved rectangle back to the screen: flight_render_install_callbacks
 * sets it to flight_sw_restore_screen_rect8bpp, or flight_sw_restore_screen_rect16bpp
 * in pixel mode 2. */
// GLOBAL: XVT 0xA07CE0
flight_screen_rect_fn g_flight_restore_screen_rect_fn = 0;
/* Draws a list of points, the radar blips: flight_render_install_callbacks sets
 * it to flight_sw_draw_point_array8bpp, or flight_sw_draw_point_array16bpp in pixel
 * mode 2. */
// GLOBAL: XVT 0x9A20A8
flight_draw_point_array_fn g_flight_draw_point_array_fn = 0;
/* Erases a list of points: flight_render_install_callbacks sets it to
 * flight_sw_erase_point_array8bpp, or flight_sw_erase_point_array16bpp in pixel mode
 * 2. */
// GLOBAL: XVT 0x9D12E4
flight_draw_point_array_fn g_flight_draw_point_array_masked_fn = 0;
/* flight_render_install_callbacks sets it to flight_sw_draw_pixel8bpp, or
 * flight_sw_draw_pixel16bpp in pixel mode 2; nothing calls through it. */
// GLOBAL: XVT 0x9E95F4
flight_draw_pixel_fn g_flight_draw_pixel_fn = 0;
/* Draws the radar's target marker: flight_render_install_callbacks sets it to
 * flight_sw_draw_radar_target_marker8bpp, or flight_sw_draw_radar_target_marker16bpp in
 * pixel mode 2. */
// GLOBAL: XVT 0x9EC46C
void (*g_flight_draw_radar_target_marker_fn)(void) = 0;
/* Restores the screen under the radar's target marker:
 * flight_render_install_callbacks sets it to
 * flight_sw_restore_radar_target_marker8bpp, or
 * flight_sw_restore_radar_target_marker16bpp in pixel mode 2. */
// GLOBAL: XVT 0x9A1FEC
void (*g_flight_restore_radar_target_marker_fn)(void) = 0;
/* Draws a line: flight_render_install_callbacks sets it to flight_sw_draw_line8bpp,
 * or flight_sw_draw_line16bpp in pixel mode 2. */
// GLOBAL: XVT 0x9CC454
flight_draw_line_fn g_flight_draw_line_fn = 0;
/* Row 0 (the side axis), Y term, of the current object matrix, Q15; see
 * g_cur_mat_r0_x. */
// GLOBAL: XVT 0xA0048C
int g_cur_mat_r0_y = 0;
/* Object-to-view rotation, Q15: the current object matrix's row 0 dotted with
 * camera row 0. Only fview_compute_object_view_matrix writes the nine g_objViewMat
 * entries; the model drawing reads them. */
// GLOBAL: XVT 0x9D77AC
int g_obj_view_mat_r0_x = 0;
/* Object-to-view rotation: object row 0 dotted with camera row 1. */
// GLOBAL: XVT 0x9D77B8
int g_obj_view_mat_r0_y = 0;
/* Object-to-view rotation: object row 0 dotted with camera row 2. */
// GLOBAL: XVT 0x9D77B4
int g_obj_view_mat_r0_z = 0;
/* Object-to-view rotation: object row 2 dotted with camera row 0. */
// GLOBAL: XVT 0x9D77B0
int g_obj_view_mat_r1_x = 0;
/* Object-to-view rotation: object row 2 dotted with camera row 1. */
// GLOBAL: XVT 0x9D7690
int g_obj_view_mat_r1_y = 0;
/* Object-to-view rotation: object row 2 dotted with camera row 2. */
// GLOBAL: XVT 0x9D768C
int g_obj_view_mat_r1_z = 0;
/* Object-to-view rotation: object row 1 dotted with camera row 0. */
// GLOBAL: XVT 0x9D7688
int g_obj_view_mat_r2_x = 0;
/* Object-to-view rotation: object row 1 dotted with camera row 1. */
// GLOBAL: XVT 0x9D77A4
int g_obj_view_mat_r2_y = 0;
/* Object-to-view rotation: object row 1 dotted with camera row 2. */
// GLOBAL: XVT 0x9D77A0
int g_obj_view_mat_r2_z = 0;
/* Light direction for the object being drawn, X term, Q15: the world light
 * direction dotted with object row 0 while
 * g_transform_light_direction_to_object_space is set, else g_world_light_direction_x.
 * Only fview_compute_object_view_matrix writes the three terms. */
// GLOBAL: XVT 0xA081E0
int g_object_light_direction_x = 0;
/* Light direction, Y term: dotted with object row 2, else
 * g_world_light_direction_y. */
// GLOBAL: XVT 0xA081E4
int g_object_light_direction_y = 0;
/* Light direction, Z term: dotted with object row 1, else
 * g_world_light_direction_z. */
// GLOBAL: XVT 0xA08150
int g_object_light_direction_z = 0;
/* Row 0, Z term, of the current object matrix, Q15; see g_cur_mat_r0_x. */
// GLOBAL: XVT 0xA00494
int g_cur_mat_r0_z = 0;
/* Row 0 (the side axis), X term, of the current object matrix, Q15. The
 * matrix's nine terms are written by fview_set_object_transform,
 * fview_calcrotatemove, fview_transformaxes and flight_view_rotate_view_by_input,
 * and in the original build player_apply_pitch_yaw_steps; fview_build_camera_orient
 * also negates rows 1 and 2. */
// GLOBAL: XVT 0xA0049C
int g_cur_mat_r0_x = 0;
/* Row 1 (the up axis), Y term, of the current object matrix, Q15; see
 * g_cur_mat_r0_x. */
// GLOBAL: XVT 0xA004AC
int g_cur_mat_r1_y = 0;
/* Row 1, Z term, of the current object matrix, Q15; see g_cur_mat_r0_x. */
// GLOBAL: XVT 0xA004B4
int g_cur_mat_r1_z = 0;
/* Row 1 (the up axis), X term, of the current object matrix, Q15; see
 * g_cur_mat_r0_x. */
// GLOBAL: XVT 0xA004D0
int g_cur_mat_r1_x = 0;
/* Row 2, X term, of the current object matrix, Q15: the negated forward axis
 * after fview_calcrotatemove; see g_cur_mat_r0_x. */
// GLOBAL: XVT 0xA07C68
int g_cur_mat_r2_x = 0;
/* Row 2, Y term, of the current object matrix, Q15; see g_cur_mat_r0_x. */
// GLOBAL: XVT 0xA07C6C
int g_cur_mat_r2_y = 0;
/* Row 2, Z term, of the current object matrix, Q15; see g_cur_mat_r0_x. */
// GLOBAL: XVT 0xA07CC4
int g_cur_mat_r2_z = 0;
/* Forward direction, Y term, Q15: -g_cur_mat_r2_y as fview_calcrotatemove, its
 * only writer, leaves it. */
// GLOBAL: XVT 0x9A8D84
int g_fview_move_y_q15 = 0;
/* Forward direction, Z term, Q15: -g_cur_mat_r2_z as fview_calcrotatemove, its
 * only writer, leaves it. */
// GLOBAL: XVT 0x9A8D88
int g_fview_move_z_q15 = 0;
/* Forward direction, X term, Q15: -g_cur_mat_r2_x as fview_calcrotatemove, its
 * only writer, leaves it. */
// GLOBAL: XVT 0x9A8DA4
int g_fview_move_x_q15 = 0;

/* Returns g_flight_direct_draw. std3d_startup is its only caller. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4079D0
IDirectDraw *renderer_get_direct_draw(void) { return g_flight_direct_draw; }

/* Sets up Direct3D for hardware drawing. Sets g_render_texture_cache_cursor to -1,
 * gives std3D the render target (g_display_mode_width by g_display_mode_height,
 * pitch g_surface_pitch, surface g_flight_back_buffer), clears the color overlay
 * and starts std3D, then picks the best device asking for hardware, perspective
 * texturing, a z-buffer and color model 2. When that device has all three it
 * creates it and gets the back buffer's attached z-buffer into
 * g_std3dz_buffer_surface; when that fails, or when the device lacks one of the
 * three, it shuts std3D down (closing it first on the z-buffer failure) and
 * sets g_use_hardware3d to 0. Every path ends with
 * math_set_fpu_single_precision_mode. Its error lines go to debug_printf, which
 * prints nothing. flight_display_init calls it while g_use_hardware3d is set. */
// FUNCTION: XVT 0x408170
void renderer_init_d3d_device(void)
{
	g_render_texture_cache_cursor = -1;
	std3d_init_render_target_desc((unsigned int)g_display_mode_width,
				      (unsigned int)g_display_mode_height,
				      g_surface_pitch);
	std3d_set_render_surface(g_flight_back_buffer);
	std3d_set_color_overlay_params(0.0f, 0.0f, 0.0f, 0);
	std3d_startup();

	struct std3d_device_caps device_caps;
	memset(&device_caps, 0, sizeof(device_caps));
	device_caps.b_hardware = 1;
	device_caps.b_texture_perspective = 1;
	device_caps.b_has_z_buffer = 1;
	device_caps.color_model_flags = 2;
	unsigned int device_index = std3d_select_best_device(&device_caps);
	memcpy(&device_caps, &g_std3d_devices[device_index].caps,
	       sizeof(device_caps));

	if (device_caps.b_has_z_buffer != 0 &&
	    device_caps.b_texture_perspective != 0 &&
	    device_caps.b_hardware != 0) {
		std3d_create_device(device_index, 1);
		DDSURFACEDESC z_buffer_desc;
		memset(&z_buffer_desc, 0, sizeof(z_buffer_desc));
		z_buffer_desc.dwSize = sizeof(z_buffer_desc);
		z_buffer_desc.dwFlags = DDSD_CAPS;
		z_buffer_desc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER;
		HRESULT result =
			g_flight_back_buffer->lpVtbl->GetAttachedSurface(
				g_flight_back_buffer, &z_buffer_desc.ddsCaps,
				&g_std3dz_buffer_surface);
		if (result != 0) {
#ifdef XVT_MODERN
			XVT_LOG_ERROR("render.z_buffer_missing result=%#x",
				      (unsigned int)result);
#else
			debug_printf("ERROR(%x)! Failed to get HW Zbuffer\n",
				     result);
#endif
			std3d_close();
			std3d_shutdown();
			g_use_hardware3d = 0;
			math_set_fpu_single_precision_mode();
			return;
		}
		math_set_fpu_single_precision_mode();
	} else {
#ifdef XVT_MODERN
		XVT_LOG_ERROR(
			"render.hardware_unsupported z_buffer=%d perspective=%d hardware=%d",
			device_caps.b_has_z_buffer,
			device_caps.b_texture_perspective,
			device_caps.b_hardware);
#else
		debug_printf(
			"Essential Hardware Feature NOT Supported: Z:%d Tex:%d HW:%d\n",
			device_caps.b_has_z_buffer,
			device_caps.b_texture_perspective,
			device_caps.b_hardware);
#endif
		std3d_shutdown();
		g_use_hardware3d = 0;
		math_set_fpu_single_precision_mode();
	}
}
