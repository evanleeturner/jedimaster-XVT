#include "xvt/assets/model_preview.h"

#include <math.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/math3d.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/render/sw3d.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/log/log_both_builds.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt_runtime/snapshot/render_capture.h"

/* 1 / 32767, which turns a 1.15 fixed point matrix entry into a float in
 * model_preview_render_viewport. */
// GLOBAL: XVT 0x518100
static const float g_model_preview_matrix_q15_to_float_scale = 0.000030518509f;
/* The 1.0 model_preview_set_light_direction divides by the vector's length. */
// GLOBAL: XVT 0x518110
static const double g_model_preview_inv_length_numerator = 1.0;
/* Size, in model units, that model_preview_load_model scales a model's largest
 * extent to: 500. */
// GLOBAL: XVT 0x5180F8
static const double g_model_preview_target_bounds_extent = 500.0;
/* 32767, which turns a unit light direction into 1.15 fixed point in
 * model_preview_set_light_direction. */
// GLOBAL: XVT 0x518118
static const double g_model_preview_light_direction_q15_scale = 32767.0;
/* 65,536 / 360: degrees to angle units. */
// GLOBAL: XVT 0x518120
static const double g_degrees_to_q16_angle_scale = 182.04444444444445;
/* 1600, which model_preview_get_displayed_size_meters multiplies the extent by. */
// GLOBAL: XVT 0x518128
static const double g_model_preview_meters_scale = 1600.0;
/* 1 / 65,536, which model_preview_get_displayed_size_meters multiplies the extent
 * by. */
// GLOBAL: XVT 0x518130
static const double g_model_preview_q16_scale = 0.0000152587890625;
/* Angle about the up axis, 65,536 a full circle, that
 * model_preview_render_viewport passes to fview_set_object_transform. Set by
 * model_preview_set_object_up_axis_angle_degrees and model_preview_restore_state; 0
 * after each successful load. */
// GLOBAL: XVT 0x520EC0
int16_t g_model_preview_up_axis_angle;
/* The preview model's block, locked by model_preview_load_model; NULL until the
 * first load. The modern xvt_frontend_task_shutdown sets it back to NULL. */
// GLOBAL: XVT 0x520EC4
struct optimized_poly_object *g_model_preview_model_data = NULL;
/* Nothing sets this flag, and model_preview_load_model clears it through
 * model_preview_free_resources before testing it, so every load resets the
 * preview object, view and light. */
/* Only model_preview_free_resources writes it, and it writes 0. */
// GLOBAL: XVT 0x520EC8
int g_model_preview_skip_scene_reset = 0;
/* 1 once model_preview_render_viewport has allocated the render buffers and the
 * span mask; model_preview_free_resources frees the buffers and sets it to 0. */
// GLOBAL: XVT 0x520ECC
int g_model_preview_render_resources_initialized = 0;
/* Memory handle of the preview's span mask buffer: model_preview_render_viewport
 * allocates it, regrows it when too small and points g_flight_aux_buffer at it.
 * The modern xvt_frontend_task_shutdown sets it to 0. */
// GLOBAL: XVT 0x520ED0
uint16_t g_model_preview_aux_buffer_handle = 0;
/* Bytes allocated for g_model_preview_aux_buffer_handle; written by the same two
 * functions. */
// GLOBAL: XVT 0x520ED4
unsigned int g_model_preview_aux_buffer_capacity_bytes = 0;
/* The preview model's largest extent, in model units before scaling, from
 * model_preview_compute_opt_bounds_extent; set by each load. */
// GLOBAL: XVT 0x520ED8
double g_model_preview_bounds_extent;
/* Which child of an OPT_NODESWITCH node is drawn: render_scene_draw_model_node
 * takes g_node_switch_index + 1, cut to the node's child count, as its selection.
 * render_scene_draw_object_model and render_scene_draw_selected_root_node set it from
 * the drawn object's mobj->node_switch_index (0 without a mobj);
 * model_preview_set_node_switch_index and model_preview_restore_state set it for the
 * preview, which model_preview_render_viewport copies into the preview object. */
// GLOBAL: XVT 0x5233A0
int g_node_switch_index;
/* The object the preview draws. Its objectType is 0, so it draws
 * g_loaded_models[0]; its mobj is g_model_preview_mobile_object. Each successful
 * load resets its position and angles to 0. */
// GLOBAL: XVT 0x5561E8
struct object_record g_model_preview_object;
/* Factor model_preview_load_model scaled the preview model by:
 * g_model_preview_target_bounds_extent over g_model_preview_bounds_extent. */
// GLOBAL: XVT 0x5561E0
static double g_model_preview_scale = 0.0;
/* The preview object's mobile part, cleared by each successful load, with
 * g_model_preview_craft_scratch as its craft. */
// GLOBAL: XVT 0x556218
static struct mobile_object g_model_preview_mobile_object = {0};
/* Name of the preview's OPT file, set by model_preview_load_model once the file
 * is loaded; model_preview_save_state copies it. */
// GLOBAL: XVT 0x555CC8
char g_model_preview_opt_file_name[128];
/* Preview pitch saved by model_preview_save_state, put back by
 * model_preview_restore_state. */
// GLOBAL: XVT 0x555CC0
int16_t g_saved_model_preview_pitch;
/* Preview yaw saved by model_preview_save_state, put back by
 * model_preview_restore_state. */
// GLOBAL: XVT 0x555CC4
int16_t g_saved_model_preview_yaw;
/* Largest z over the model's vertices, starting from 0, while
 * model_preview_compute_opt_bounds_extent runs; afterwards the z extent. */
// GLOBAL: XVT 0x555D48
static float g_model_preview_bounds_max_z = 0.0f;
/* Smallest z over the model's vertices, starting from 0, for
 * model_preview_compute_opt_bounds_extent. */
// GLOBAL: XVT 0x555D4C
static float g_model_preview_bounds_min_z = 0.0f;
/* Largest x over the model's vertices, starting from 0, while
 * model_preview_compute_opt_bounds_extent runs; afterwards the x extent. */
// GLOBAL: XVT 0x555D50
static float g_model_preview_bounds_max_x = 0.0f;
/* Smallest x over the model's vertices, starting from 0, for
 * model_preview_compute_opt_bounds_extent. */
// GLOBAL: XVT 0x555D54
static float g_model_preview_bounds_min_x = 0.0f;
/* Light direction z saved by model_preview_save_state, put back by
 * model_preview_restore_state. */
// GLOBAL: XVT 0x555D58
int16_t g_saved_model_preview_light_direction_z;
/* Preview world y saved by model_preview_save_state, put back by
 * model_preview_restore_state. */
// GLOBAL: XVT 0x555D5C
int g_saved_model_preview_world_y;
/* Preview world z saved by model_preview_save_state, put back by
 * model_preview_restore_state. */
// GLOBAL: XVT 0x555D60
int g_saved_model_preview_world_z;
/* Preview world x saved by model_preview_save_state, put back by
 * model_preview_restore_state. */
// GLOBAL: XVT 0x555D64
int g_saved_model_preview_world_x;
/* Largest y over the model's vertices, starting from 0, while
 * model_preview_compute_opt_bounds_extent runs; afterwards the y extent. */
// GLOBAL: XVT 0x555D68
static float g_model_preview_bounds_max_y = 0.0f;
/* Smallest y over the model's vertices, starting from 0, for
 * model_preview_compute_opt_bounds_extent. */
// GLOBAL: XVT 0x555D6C
static float g_model_preview_bounds_min_y = 0.0f;
/* Craft record the preview's mobile object points at, cleared by each
 * successful load. */
// GLOBAL: XVT 0x555D78
static struct craft_data g_model_preview_craft_scratch = {0};
/* Light direction y saved by model_preview_save_state, put back by
 * model_preview_restore_state. */
// GLOBAL: XVT 0x555D70
int16_t g_saved_model_preview_light_direction_y;
/* Light direction x saved by model_preview_save_state, put back by
 * model_preview_restore_state. */
// GLOBAL: XVT 0x555D74
int16_t g_saved_model_preview_light_direction_x;
/* g_node_switch_index saved by model_preview_save_state, put back by
 * model_preview_restore_state. */
// GLOBAL: XVT 0x5561DC
int g_saved_model_preview_node_switch_index;
/* g_model_preview_up_axis_angle saved by model_preview_save_state, put back by
 * model_preview_restore_state. */
// GLOBAL: XVT 0x55620C
int16_t g_saved_model_preview_up_axis_angle;
/* Preview roll saved by model_preview_save_state, put back by
 * model_preview_restore_state. */
// GLOBAL: XVT 0x556210
int16_t g_saved_model_preview_roll;
/* Preview file name saved by model_preview_save_state; model_preview_restore_state
 * loads it again. */
// GLOBAL: XVT 0x5562D0
char g_saved_model_preview_model_file_name[128];
/* X of the light direction in world axes, 1.15 fixed point. Flight start sets
 * all three to DEFAULT_MODEL_LIGHT_DIRECTION (flight_main_loop in the original
 * build, xvt_flight_loading_mission_setup in the modern one);
 * model_preview_set_light_direction and model_preview_restore_state set them for the
 * preview. fview_compute_object_view_matrix turns them into the object's light
 * direction. */
// GLOBAL: XVT 0x9D12EC
int g_world_light_direction_x;
/* Y of the light direction in world axes; written and read like
 * g_world_light_direction_x. */
// GLOBAL: XVT 0x9D12F0
int g_world_light_direction_y;
/* Z of the light direction in world axes; written and read like
 * g_world_light_direction_x. */
// GLOBAL: XVT 0x9D1304
int g_world_light_direction_z;
/* The preview object's position less the camera's, turned into camera axes;
 * model_preview_render_viewport sets it each draw and makes its z
 * g_view_space_depth. The modern build passes it to
 * xvt_render_capture_frontend_preview. */
// GLOBAL: XVT 0xA60710
static struct opt_vector g_model_preview_view_delta = {0.0f, 0.0f, 0.0f};
/* model_preview_render_viewport's float copy of a 1.15 matrix: first the camera
 * rotation, used to turn g_model_preview_view_delta, then the object-to-view
 * rotation, which the modern build passes to
 * xvt_render_capture_frontend_preview. */
// GLOBAL: XVT 0xA6071C
static float g_model_preview_matrix[9] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
					  0.0f, 0.0f, 0.0f, 0.0f};
/* Minus g_model_preview_view_delta turned by g_model_preview_object_view_matrix, set
 * each draw by model_preview_render_viewport; nothing reads it. */
// GLOBAL: XVT 0xA60740
static struct opt_vector g_model_preview_neg_view_delta = {0.0f, 0.0f, 0.0f};
/* The object-to-view rotation transposed, set each draw by
 * model_preview_render_viewport only to turn g_model_preview_neg_view_delta. */
// GLOBAL: XVT 0xA6074C
static float g_model_preview_object_view_matrix[9] = {
	0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

/* Loads a model for the frontend preview into g_loaded_models[0] and returns 1,
 * or 0. It frees the preview's render buffers, resets the view and render
 * settings, turns g_mipmapping_enabled on, sets g_loaded_models[0] to 0 when
 * g_model_preview_model_data is NULL, and opens the name with ".opt" in place of
 * its extension. The modern build returns 0 for a NULL name, one of 256 or more
 * characters, an extension other than ".opt", a file that does not open or a
 * load that fails. The original build cuts the name at its first '.' and, when
 * the OPT file does not open, imports the ".iv" file as opt_model_load_handle
 * does, saving it as the OPT file. An OPT file is loaded with
 * opt_model_load_file_to_handle after the old slot's handle is freed, as an import
 * frees it too, and a runtime copy built with g_flight_bytes_per_pixel set to 2,
 * which it stays, takes the slot. It locks the copy into
 * g_model_preview_model_data, scales it so its largest extent is
 * g_model_preview_target_bounds_extent (g_model_preview_bounds_extent,
 * g_model_preview_scale), sets g_transform_light_direction_to_object_space, and resets
 * the preview object and the view, and the light to (1, 1, 1). The modern build
 * also registers the copy for its renderer. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x429E70
int model_preview_load_model(const char *model_file_name)
{
	enum {
		MODEL_PREVIEW_SLOT = 0,
		FILE_NAME_CAPACITY = 256,
	};
	char *extension;

	model_preview_free_resources();
	model_preview_reset_view_and_render_state();
	g_mipmapping_enabled = 1;
	if (g_model_preview_model_data == NULL) {
		g_loaded_models[MODEL_PREVIEW_SLOT] = 0;
	}

	char file_name[FILE_NAME_CAPACITY];
	char base_name[FILE_NAME_CAPACITY];
	if (!model_file_name || strlen(model_file_name) >= sizeof(base_name)) {
		return 0;
	}
	strcpy(base_name, model_file_name);
	extension = strrchr(base_name, '.');
	if (extension) {
		if (strcasecmp(extension, ".opt") != 0) {
			XVT_LOG_WARN(
				"preview.load_failed file=\"%s\" reason=\"extension\"",
				model_file_name);
			return 0;
		}
		*extension = '\0';
	}
	if (strlen(base_name) + sizeof(".opt") > sizeof(file_name)) {
		return 0;
	}

	strcpy(file_name, base_name);
	strcat(file_name, ".opt");
	fe_disk_io_open_global_stream(file_name, g_file_mode_read_binary, 0, 0);
	xvt_file *stream = g_stream;
	if (!stream) {
		XVT_LOG_WARN("preview.load_failed file=\"%s\" reason=\"open\"",
			     file_name);
		return 0;
	}
	file_close(stream);
	g_stream = NULL;
	if (g_loaded_models[MODEL_PREVIEW_SLOT] != 0) {
		memory_free_handle(g_loaded_models[MODEL_PREVIEW_SLOT]);
	}
	g_loaded_models[MODEL_PREVIEW_SLOT] =
		opt_model_load_file_to_handle(file_name);

	if (!g_loaded_models[MODEL_PREVIEW_SLOT]) {
		return 0;
	}
	strcpy(g_model_preview_opt_file_name, base_name);
	strcat(g_model_preview_opt_file_name, ".opt");
	if (g_mipmapping_enabled != 0) {
		g_flight_bytes_per_pixel = 2;
		g_loaded_models[MODEL_PREVIEW_SLOT] =
			opt_model_create_runtime_handle(
				g_loaded_models[MODEL_PREVIEW_SLOT]);
		g_flight_bytes_per_pixel = 2;
	}
	if (!g_loaded_models[MODEL_PREVIEW_SLOT]) {
		return 0;
	}
	xvt_render_assets_register_opt(g_loaded_models[MODEL_PREVIEW_SLOT],
				       g_model_preview_opt_file_name);
	xvt_render_assets_bind_type(MODEL_PREVIEW_SLOT,
				    g_loaded_models[MODEL_PREVIEW_SLOT]);
	g_model_preview_model_data =
		(struct optimized_poly_object *)memory_get_handle_block(
			g_loaded_models[MODEL_PREVIEW_SLOT]);
	if (g_model_preview_model_data->self_marker !=
	    g_model_preview_model_data) {
		opt_model_adjust_optimized_poly_object_pointers(
			g_model_preview_model_data);
	}
	/* The second argument is an axis, not a slot: MODEL_PREVIEW_SLOT passes
	 * 0, the largest extent. */
	g_model_preview_bounds_extent = model_preview_compute_opt_bounds_extent(
		g_model_preview_model_data, MODEL_PREVIEW_SLOT);
	g_model_preview_scale = g_model_preview_target_bounds_extent /
				g_model_preview_bounds_extent;
	model_preview_scale_opt_root_nodes(g_model_preview_model_data,
					   g_model_preview_scale);
	g_transform_light_direction_to_object_space = 1;
	XVT_LOG_DEBUG(
		"preview.loaded file=\"%s\" handle=%u extent=%.3f scale=%.3f",
		g_model_preview_opt_file_name,
		(unsigned)g_loaded_models[MODEL_PREVIEW_SLOT],
		g_model_preview_bounds_extent, g_model_preview_scale);

	if (g_model_preview_skip_scene_reset == 0) {
		memset(&g_model_preview_mobile_object, 0,
		       sizeof(g_model_preview_mobile_object));
		memset(&g_model_preview_craft_scratch, 0,
		       sizeof(g_model_preview_craft_scratch));
		g_model_preview_object.object_type = 0;
		g_model_preview_object.world_x = 0;
		g_model_preview_object.pitch = 0;
		g_model_preview_object.world_y = 0;
		g_model_preview_object.yaw = 0;
		g_model_preview_object.world_z = 0;
		g_model_preview_object.mobj = &g_model_preview_mobile_object;
		g_model_preview_mobile_object.p_craft =
			&g_model_preview_craft_scratch;
		g_model_preview_object.roll = 0;
		g_model_preview_up_axis_angle = 0;
		model_preview_reset_view_and_render_state();
		model_preview_set_light_direction(1, 1, 1);
	}
	return 1;
}

/* Frees the render buffers when g_model_preview_render_resources_initialized is set
 * and clears it; also clears g_model_preview_skip_scene_reset. */
// FUNCTION: XVT 0x42A350
void model_preview_free_resources(void)
{
	if (g_model_preview_render_resources_initialized != 0) {
		render_scene_free_buffers();
		g_model_preview_render_resources_initialized = 0;
	}
	g_model_preview_skip_scene_reset = 0;
}

/* Draws the preview model into the viewport at x, y, width by height and
 * returns 1; returns 0 when g_loaded_models[0] is 0, x is 1024 or more, or y is
 * 768 or more. A negative x moves the viewport's left edge to 0 and takes it
 * off the width; a negative y is taken off the height but kept. The right and
 * bottom edges are cut to 1024 by 768. It sets the flight viewport and
 * projection globals (scale 512, perspective shift 9), builds the camera from
 * the local player's view state and the object's transform with
 * g_model_preview_up_axis_angle, and sets g_model_preview_view_delta and the preview
 * matrices. The first draw after a load allocates the render buffers and the
 * span mask in g_model_preview_aux_buffer_handle: per row the byte 1, then a 0
 * while the width left is 256 or more, taking 255 the first time and 256 the
 * second (twice at most), then the width left. It draws with local lights off,
 * the object's node_switch_index from g_node_switch_index, through
 * render_scene_draw_object_model and sw3d_draw_visible_faces_to_surface. The modern
 * build also records the draw for its renderer. The arguments after height are
 * ignored. */
// FUNCTION: XVT 0x42A380
int model_preview_render_viewport(int x, int y, int width, int height, ...)
{
	enum {
		MODEL_PREVIEW_SLOT = 0,
		RENDER_SURFACE_MAX_WIDTH = 1024,
		RENDER_SURFACE_MAX_HEIGHT = 768,
		MODEL_PREVIEW_PROJECTION_SCALE = 512,
		MODEL_PREVIEW_PROJECTION_HALF_SCALE =
			MODEL_PREVIEW_PROJECTION_SCALE / 2,
		MODEL_PREVIEW_PERSPECTIVE_SHIFT = 9,
		SPAN_MASK_LONG_RUN_LENGTH = 255,
		SPAN_MASK_LONG_RUN_THRESHOLD = SPAN_MASK_LONG_RUN_LENGTH + 1,
	};

	if (g_loaded_models[MODEL_PREVIEW_SLOT] == 0) {
		return 0;
	}

	if (x < 0) {
		width += x;
		x = 0;
	}
	if (y < 0) {
		height += y;
	}
	if (x >= RENDER_SURFACE_MAX_WIDTH) {
		return 0;
	}
	if (y >= RENDER_SURFACE_MAX_HEIGHT) {
		return 0;
	}
	if (y + height > RENDER_SURFACE_MAX_HEIGHT) {
		height = RENDER_SURFACE_MAX_HEIGHT - y;
	}
	if (x + width > RENDER_SURFACE_MAX_WIDTH) {
		width = RENDER_SURFACE_MAX_WIDTH - x;
	}

	g_flight_vp_width = (uint16_t)width;
	g_flight_vp_max_x = (uint16_t)(width - 1);
	g_flight_vp_center_x = (uint16_t)(width / 2);
	g_flight_vp_height = (uint16_t)height;
	g_flight_vp_max_y = (uint16_t)(height - 1);
	g_flight_vp_center_y = (uint16_t)(height / 2);
	g_flight_vp_y = y;
	g_flight_vp_x = x;
	g_flight_vp_base_offset = (unsigned int)(y * g_surface_pitch + x);
	g_proj_scale_int = MODEL_PREVIEW_PROJECTION_SCALE;
	g_proj_scale_half_int = MODEL_PREVIEW_PROJECTION_HALF_SCALE;
	g_perspective_shift = MODEL_PREVIEW_PERSPECTIVE_SHIFT;
	g_proj_aspect_y = 0;

	fview_build_camera_orient(
		g_players[g_local_player].view_state.view_roll,
		g_players[g_local_player].view_state.view_pitch,
		g_players[g_local_player].view_state.view_yaw, 0, 0, 0, NULL);
	fview_set_object_transform(g_model_preview_object.roll,
				   g_model_preview_object.pitch,
				   g_model_preview_object.yaw,
				   g_model_preview_up_axis_angle, NULL);

	g_model_preview_view_delta.x =
		(float)(g_model_preview_object.world_x -
			g_players[g_local_player].view_state.camera_world_x);
	g_model_preview_view_delta.y =
		(float)(g_model_preview_object.world_y -
			g_players[g_local_player].view_state.camera_world_y);
	g_model_preview_view_delta.z =
		(float)(g_model_preview_object.world_z -
			g_players[g_local_player].view_state.camera_world_z);
	g_model_preview_matrix[0] = (float)g_cam_mat_r0_x *
				    g_model_preview_matrix_q15_to_float_scale;
	g_model_preview_matrix[1] = (float)g_cam_mat_r1_x *
				    g_model_preview_matrix_q15_to_float_scale;
	g_model_preview_matrix[2] = (float)g_cam_mat_r2_x *
				    g_model_preview_matrix_q15_to_float_scale;
	g_model_preview_matrix[3] = (float)g_cam_mat_r0_y *
				    g_model_preview_matrix_q15_to_float_scale;
	g_model_preview_matrix[4] = (float)g_cam_mat_r1_y *
				    g_model_preview_matrix_q15_to_float_scale;
	g_model_preview_matrix[5] = (float)g_cam_mat_r2_y *
				    g_model_preview_matrix_q15_to_float_scale;
	g_model_preview_matrix[6] = (float)g_cam_mat_r0_z *
				    g_model_preview_matrix_q15_to_float_scale;
	g_model_preview_matrix[7] = (float)g_cam_mat_r1_z *
				    g_model_preview_matrix_q15_to_float_scale;
	g_model_preview_matrix[8] = (float)g_cam_mat_r2_z *
				    g_model_preview_matrix_q15_to_float_scale;
	math3d_rotate_vec3(&g_model_preview_view_delta.x,
			   g_model_preview_matrix);

	float object_row0x = (float)g_obj_view_mat_r0_x *
			     g_model_preview_matrix_q15_to_float_scale;
	g_model_preview_matrix[0] = object_row0x;
	float object_row0y = (float)g_obj_view_mat_r0_y *
			     g_model_preview_matrix_q15_to_float_scale;
	g_model_preview_matrix[1] = object_row0y;
	float object_row0z = (float)g_obj_view_mat_r0_z *
			     g_model_preview_matrix_q15_to_float_scale;
	g_model_preview_matrix[2] = object_row0z;
	float object_row1x = (float)g_obj_view_mat_r1_x *
			     g_model_preview_matrix_q15_to_float_scale;
	g_model_preview_matrix[3] = object_row1x;
	float object_row1y = (float)g_obj_view_mat_r1_y *
			     g_model_preview_matrix_q15_to_float_scale;
	g_model_preview_matrix[4] = object_row1y;
	float object_row1z = (float)g_obj_view_mat_r1_z *
			     g_model_preview_matrix_q15_to_float_scale;
	g_model_preview_matrix[5] = object_row1z;
	float object_row2x = (float)g_obj_view_mat_r2_x *
			     g_model_preview_matrix_q15_to_float_scale;
	g_model_preview_matrix[6] = object_row2x;
	float object_row2y = (float)g_obj_view_mat_r2_y *
			     g_model_preview_matrix_q15_to_float_scale;
	g_model_preview_matrix[7] = object_row2y;
	float object_row2z = (float)g_obj_view_mat_r2_z *
			     g_model_preview_matrix_q15_to_float_scale;
	g_model_preview_matrix[8] = object_row2z;

	g_model_preview_neg_view_delta.x = -g_model_preview_view_delta.x;
	g_model_preview_neg_view_delta.y = -g_model_preview_view_delta.y;
	g_model_preview_neg_view_delta.z = -g_model_preview_view_delta.z;
	g_model_preview_object_view_matrix[0] = object_row0x;
	g_model_preview_object_view_matrix[1] = object_row1x;
	g_model_preview_object_view_matrix[2] = object_row2x;
	g_model_preview_object_view_matrix[3] = object_row0y;
	g_model_preview_object_view_matrix[4] = object_row1y;
	g_model_preview_object_view_matrix[5] = object_row2y;
	g_model_preview_object_view_matrix[6] = object_row0z;
	g_model_preview_object_view_matrix[7] = object_row1z;
	g_model_preview_object_view_matrix[8] = object_row2z;
	math3d_rotate_vec3(&g_model_preview_neg_view_delta.x,
			   g_model_preview_object_view_matrix);

	if (g_model_preview_render_resources_initialized == 0) {
		render_scene_allocate_buffers();
		if (g_viewport_span_mask_offset +
			    g_flight_vp_height *
				    ((g_flight_vp_width >> 7) + 2) >
		    (int)g_model_preview_aux_buffer_capacity_bytes) {
			if (g_model_preview_aux_buffer_handle != 0) {
				memory_free_handle(
					g_model_preview_aux_buffer_handle);
			}
			g_model_preview_aux_buffer_handle = memory_alloc_handle(
				g_viewport_span_mask_offset +
					g_flight_vp_height *
						((g_flight_vp_width >> 7) + 2),
				0);
			g_model_preview_aux_buffer_capacity_bytes =
				g_viewport_span_mask_offset +
				g_flight_vp_height *
					((g_flight_vp_width >> 7) + 2);
			if (g_model_preview_aux_buffer_handle == 0) {
				XVT_LOG_ERROR(
					"preview.buffer_alloc_failed bytes=%u",
					g_model_preview_aux_buffer_capacity_bytes);
			}
		}
		uint8_t *aux_buffer = (uint8_t *)memory_get_handle_block(
			g_model_preview_aux_buffer_handle);
		g_flight_aux_buffer = aux_buffer;
		uint8_t *mask_cursor = &aux_buffer[g_viewport_span_mask_offset];
		for (unsigned int row = 0; row < g_flight_vp_height; ++row) {
			*mask_cursor++ = 1;
			unsigned int remaining_width = g_flight_vp_width;
			if (remaining_width >= SPAN_MASK_LONG_RUN_THRESHOLD) {
				*mask_cursor++ = 0;
				remaining_width -= SPAN_MASK_LONG_RUN_LENGTH;
				if (remaining_width >=
				    SPAN_MASK_LONG_RUN_THRESHOLD) {
					*mask_cursor++ = 0;
					remaining_width -=
						SPAN_MASK_LONG_RUN_THRESHOLD;
				}
			}
			*mask_cursor++ = (uint8_t)remaining_width;
		}
		g_model_preview_render_resources_initialized = 1;
		XVT_LOG_DEBUG(
			"preview.buffers_ready width=%d height=%d bytes=%u",
			(int)g_flight_vp_width, (int)g_flight_vp_height,
			g_model_preview_aux_buffer_capacity_bytes);
	}

	g_view_space_depth = (int)g_model_preview_view_delta.z;
	g_model_preview_object.mobj->node_switch_index =
		(uint8_t)g_node_switch_index;
	int saved_local_lights_enabled = g_local_lights_enabled;
	g_local_lights_enabled = 0;
	render_scene_initialize(1);
	xvt_render_capture_frontend_preview(
		g_loaded_models[0], &g_model_preview_view_delta.x,
		g_model_preview_matrix, (float)g_model_preview_scale,
		(uint16_t)g_node_switch_index, x, y, width, height);
	render_scene_draw_object_model(&g_model_preview_object);
	sw3d_draw_visible_faces_to_surface();
	render_scene_unlock_buffers();
	g_local_lights_enabled = saved_local_lights_enabled;
	return 1;
}

/* Multiplies by scale every vertex of each OPT_MESHVERTS node at or below node
 * and the two texture gradient vectors of each face of each OPT_FACEDATA node,
 * and divides by scale the first child_count floats of each OPT_FACEGROUP node.
 * Follows OPT_NODEREF links and stops at one that does not resolve; a node
 * reached through two links is scaled twice. Other face node types keep their
 * gradients. */
// FUNCTION: XVT 0x42A920
void model_preview_scale_opt_node_tree(struct opt_node *node,
				       struct optimized_poly_object *opt,
				       double scale)
{
	struct opt_node *resolved_node = node;
	if (resolved_node == NULL) {
		return;
	}
	while (resolved_node->node_type == OPT_NODEREF) {
		resolved_node = opt_model_resolve_node_ref(
			opt, (const char *)resolved_node->payload);
		if (resolved_node == NULL) {
			return;
		}
	}

	switch (resolved_node->node_type) {
	case OPT_FACEDATA: {
		int count = resolved_node->payload_count;
		struct opt_packed_face_data *face_data =
			(struct opt_packed_face_data *)resolved_node->payload;
		struct opt_vector *face_normals =
			(struct opt_vector *)&face_data->records[count];
		struct face_texture_gradients *gradients =
			(struct face_texture_gradients *)&face_normals[count];
		float *points = (float *)gradients;
		if (count > 0) {
			do {
				points[0] = (float)(points[0] * scale);
				points[1] = (float)(points[1] * scale);
				points[2] = (float)(points[2] * scale);
				points += 3;
				points[0] = (float)(points[0] * scale);
				points[1] = (float)(points[1] * scale);
				points[2] = (float)(points[2] * scale);
				points += 3;
				--count;
			} while (count != 0);
		}
		break;
	}
	case OPT_MESHVERTS: {
		int count = resolved_node->payload_count;
		float *vertices = (float *)resolved_node->payload;
		if (count > 0) {
			do {
				vertices[0] = (float)(vertices[0] * scale);
				vertices[1] = (float)(vertices[1] * scale);
				vertices[2] = (float)(vertices[2] * scale);
				vertices += 3;
				--count;
			} while (count != 0);
		}
		break;
	}
	case OPT_FACEGROUP: {
		int count = resolved_node->child_count;
		float *lod_thresholds = (float *)resolved_node->payload;
		if (count > 0) {
			do {
				*lod_thresholds =
					(float)(*lod_thresholds / scale);
				++lod_thresholds;
				--count;
			} while (count != 0);
		}
		break;
	}
	default:
		break;
	}

	for (int child_index = 0; child_index < resolved_node->child_count;
	     ++child_index) {
		model_preview_scale_opt_node_tree(
			resolved_node->p_children[child_index], opt, scale);
	}
}

/* Runs model_preview_scale_opt_node_tree on each root of opt. */
// FUNCTION: XVT 0x42AC50
void model_preview_scale_opt_root_nodes(struct optimized_poly_object *opt,
					double scale)
{
	for (int root_index = 0; root_index < opt->root_node_count;
	     ++root_index) {
		model_preview_scale_opt_node_tree(opt->root_nodes[root_index],
						  opt, scale);
	}
}

/* Widens the preview bounds globals (g_model_preview_bounds_min_x to
 * g_model_preview_bounds_max_z) to hold every vertex of each OPT_MESHVERTS node at
 * or below node. Follows OPT_NODEREF links and stops at one that does not
 * resolve. */
// FUNCTION: XVT 0x42AD10
void model_preview_accumulate_opt_node_bounds(
	struct opt_node *node, struct optimized_poly_object *object)
{
	struct opt_node *current_node = node;
	if (current_node != NULL) {
		while (current_node->node_type == OPT_NODEREF) {
			current_node = opt_model_resolve_node_ref(
				object, (const char *)current_node->payload);
			if (current_node == NULL) {
				return;
			}
		}

		if (current_node->node_type == OPT_MESHVERTS) {
			int vertex_count = current_node->payload_count;
			float *vertex = current_node->payload;
			if (vertex_count > 0) {
				do {
					if (vertex[0] >
					    g_model_preview_bounds_max_x) {
						g_model_preview_bounds_max_x =
							vertex[0];
					}
					if (vertex[0] <
					    g_model_preview_bounds_min_x) {
						g_model_preview_bounds_min_x =
							vertex[0];
					}
					if (vertex[1] >
					    g_model_preview_bounds_max_y) {
						g_model_preview_bounds_max_y =
							vertex[1];
					}
					if (vertex[1] <
					    g_model_preview_bounds_min_y) {
						g_model_preview_bounds_min_y =
							vertex[1];
					}
					if (vertex[2] >
					    g_model_preview_bounds_max_z) {
						g_model_preview_bounds_max_z =
							vertex[2];
					}
					if (vertex[2] <
					    g_model_preview_bounds_min_z) {
						g_model_preview_bounds_min_z =
							vertex[2];
					}
					vertex += 3;
					--vertex_count;
				} while (vertex_count != 0);
			}
		}

		int child_index = 0;
		while (current_node->child_count > child_index) {
			model_preview_accumulate_opt_node_bounds(
				current_node->p_children[child_index], object);
			++child_index;
		}
	}
}

/* Returns the extent (max - min) of the object's vertices and the origin on one axis: axis 1 is X,
 * 2 is Y and 3 is Z, while axis 0 returns the largest of the three extents. */
/* Writes the six preview bounds globals, starting each at 0. For axis 0, when
 * the x and y extents are equal and both larger than the z extent, it returns
 * the z extent. Any axis other than 0 to 3 returns an uninitialized value. */
// FUNCTION: XVT 0x42AE30
double
model_preview_compute_opt_bounds_extent(struct optimized_poly_object *object,
					int axis)
{
	g_model_preview_bounds_max_x = 0.0f;
	g_model_preview_bounds_min_x = 0.0f;
	g_model_preview_bounds_max_y = 0.0f;
	g_model_preview_bounds_min_y = 0.0f;
	g_model_preview_bounds_max_z = 0.0f;
	g_model_preview_bounds_min_z = 0.0f;
	for (int root_node_index = 0; root_node_index < object->root_node_count;
	     ++root_node_index) {
		model_preview_accumulate_opt_node_bounds(
			object->root_nodes[root_node_index], object);
	}

	/* From here the Max globals hold the extents (max - min), not the maxima. */
	g_model_preview_bounds_max_x -= g_model_preview_bounds_min_x;
	g_model_preview_bounds_max_y -= g_model_preview_bounds_min_y;
	g_model_preview_bounds_max_z -= g_model_preview_bounds_min_z;
	double result;
	if (axis == 0) {
		if (g_model_preview_bounds_max_y >=
			    g_model_preview_bounds_max_x ||
		    g_model_preview_bounds_max_z >=
			    g_model_preview_bounds_max_x) {
			if (g_model_preview_bounds_max_y <=
				    g_model_preview_bounds_max_x ||
			    g_model_preview_bounds_max_z >=
				    g_model_preview_bounds_max_y) {
				result = g_model_preview_bounds_max_z;
			} else {
				result = g_model_preview_bounds_max_y;
			}
		} else {
			result = g_model_preview_bounds_max_x;
		}
	} else {
		if (axis == 1) {
			result = g_model_preview_bounds_max_x;
		}
		if (axis == 2) {
			result = g_model_preview_bounds_max_y;
		}
		if (axis == 3) {
			result = g_model_preview_bounds_max_z;
		}
	}
	return result;
}

/* Points the local player's camera at the preview: position (0, -1280, 0),
 * pitch 0x4000, roll and yaw 0, g_proj_offset_y 0. Sets g_lod_distance_scale and
 * g_mip_lod_scale to 1.0, g_texture_resolution_level to 1, and turns on local
 * lights, specular, directional lighting and dithering. Returns 1. */
// FUNCTION: XVT 0x42AF90
int model_preview_reset_view_and_render_state(void)
{
	struct player_data *player = &g_players[g_local_player];

	g_proj_offset_y = 0;
	player->view_state.camera_world_x = 0;
	player->view_state.camera_world_y = -1280;
	player->view_state.camera_world_z = 0;
	player->view_state.view_roll = 0;
	player->view_state.view_pitch = 0x4000;
	player->view_state.view_yaw = 0;
	g_lod_distance_scale = 1.0f;
	g_mip_lod_scale = 1.0f;
	g_local_lights_enabled = 1;
	g_specular_enabled = 1;
	g_texture_resolution_level = 1;
	g_dir_lighting_enabled = 1;
	g_dithering_enabled = 1;
	return 1;
}

/* Sets g_world_light_direction_x, Y and Z to the direction (x, -y, z) scaled to
 * length 32767, each cut to an int16_t. Does not check for a zero vector. */
// FUNCTION: XVT 0x42B010
void model_preview_set_light_direction(int x, int y, int z)
{
	y = -y;
	double light_x = x;
	double light_y = y;
	double light_z = z;
	double inv_length =
		g_model_preview_inv_length_numerator /
		sqrt(light_x * light_x + light_y * light_y + light_z * light_z);
	light_x *= inv_length;
	light_y *= inv_length;
	light_z *= inv_length;
	g_world_light_direction_x =
		(int16_t)(int)(light_x *
			       g_model_preview_light_direction_q15_scale);
	g_world_light_direction_y =
		(int16_t)(int)(light_y *
			       g_model_preview_light_direction_q15_scale);
	g_world_light_direction_z =
		(int16_t)(int)(light_z *
			       g_model_preview_light_direction_q15_scale);
}

/* Sets the preview object's pitch, yaw and roll from degrees, times 65,536 /
 * 360, each cut to an int16_t. */
// FUNCTION: XVT 0x42B090
void model_preview_set_object_euler_degrees(float pitch_deg, float yaw_deg,
					    float roll_deg)
{
	double angle = pitch_deg;
	g_model_preview_object.pitch =
		(int16_t)(int)(angle * g_degrees_to_q16_angle_scale);
	angle = yaw_deg;
	g_model_preview_object.yaw =
		(int16_t)(int)(angle * g_degrees_to_q16_angle_scale);
	angle = roll_deg;
	g_model_preview_object.roll =
		(int16_t)(int)(angle * g_degrees_to_q16_angle_scale);
}

/* Sets g_node_switch_index. */
// FUNCTION: XVT 0x42B0D0
void model_preview_set_node_switch_index(int node_switch_index)
{
	g_node_switch_index = node_switch_index;
}

/* Sets the preview object's world position. */
// FUNCTION: XVT 0x42B0E0
void model_preview_set_object_world_position(int x, int y, int z)
{
	g_model_preview_object.world_x = x;
	g_model_preview_object.world_y = y;
	g_model_preview_object.world_z = z;
}

/* Saves the preview's file name, position, angles, light direction,
 * g_node_switch_index and g_model_preview_up_axis_angle in the g_savedModelPreview
 * globals. */
// FUNCTION: XVT 0x42B100
void model_preview_save_state(void)
{
	strcpy(g_saved_model_preview_model_file_name,
	       g_model_preview_opt_file_name);
	g_saved_model_preview_world_x = g_model_preview_object.world_x;
	g_saved_model_preview_world_y = g_model_preview_object.world_y;
	g_saved_model_preview_world_z = g_model_preview_object.world_z;
	g_saved_model_preview_node_switch_index = g_node_switch_index;
	g_saved_model_preview_pitch = g_model_preview_object.pitch;
	g_saved_model_preview_yaw = g_model_preview_object.yaw;
	g_saved_model_preview_roll = g_model_preview_object.roll;
	g_saved_model_preview_light_direction_x =
		(int16_t)g_world_light_direction_x;
	g_saved_model_preview_light_direction_y =
		(int16_t)g_world_light_direction_y;
	g_saved_model_preview_light_direction_z =
		(int16_t)g_world_light_direction_z;
	g_saved_model_preview_up_axis_angle = g_model_preview_up_axis_angle;
}

/* Loads the saved file name again with model_preview_load_model, ignoring a
 * failure, then puts back what model_preview_save_state saved. */
// FUNCTION: XVT 0x42B1B0
void model_preview_restore_state(void)
{
	model_preview_load_model(g_saved_model_preview_model_file_name);
	g_model_preview_object.world_x = g_saved_model_preview_world_x;
	g_model_preview_object.world_y = g_saved_model_preview_world_y;
	g_model_preview_object.world_z = g_saved_model_preview_world_z;
	g_model_preview_object.pitch = g_saved_model_preview_pitch;
	g_model_preview_object.yaw = g_saved_model_preview_yaw;
	g_model_preview_object.roll = g_saved_model_preview_roll;
	g_node_switch_index = g_saved_model_preview_node_switch_index;
	g_world_light_direction_x = g_saved_model_preview_light_direction_x;
	g_world_light_direction_y = g_saved_model_preview_light_direction_y;
	g_world_light_direction_z = g_saved_model_preview_light_direction_z;
	g_model_preview_up_axis_angle = g_saved_model_preview_up_axis_angle;
}

/* Sets g_model_preview_up_axis_angle from degrees, times 65,536 / 360, cut to an
 * int16_t. */
// FUNCTION: XVT 0x42B250
void model_preview_set_object_up_axis_angle_degrees(float angle_deg)
{
	double angle = angle_deg;

	g_model_preview_up_axis_angle =
		(int16_t)(int)(angle * g_degrees_to_q16_angle_scale);
}

/* Returns g_model_preview_bounds_extent times 1600 times 1 / 65,536, cut to an
 * int. */
// FUNCTION: XVT 0x42B270
int model_preview_get_displayed_size_meters(void)
{
	double displayed_size = g_model_preview_bounds_extent;

	displayed_size *= g_model_preview_meters_scale;
	displayed_size *= g_model_preview_q16_scale;
	return (int)displayed_size;
}
