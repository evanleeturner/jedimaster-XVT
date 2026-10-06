#include "xvt/render/backdrop.h"

#include "xvt/assets/object_type.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_view.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/trig2.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_quad.h"
#include "xvt/render/renderer.h"
#include "xvt/render/tex_level.h"
#include "xvt/util/game_rand.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/log/log_both_builds.h"

/* Object type of each backdrop record, whose texture block holds its image:
 * backdrop_generate_default_records fills the first 22 at random and mission_init
 * puts a mission's backdrop flight groups over them. */
// GLOBAL: XVT 0x5234F0
uint8_t g_backdrop_model_types[64] = {0};
/* Direction of each backdrop record on its cube face: low bits 0x07 and high
 * bits 0x70 index the camera step tables on the face's two other axes, and 0x08
 * and 0x80 negate them. Written by backdrop_generate_default_records and
 * mission_init. */
// GLOBAL: XVT 0x523530
uint8_t g_backdrop_packed_directions[64] = {0};
/* Records on the positive Y face, first in the record arrays;
 * backdrop_generate_default_records sets 4. The six face counts order the
 * records: +Y, -Y, +X, -X, +Z, -Z. */
// GLOBAL: XVT 0x523570
uint16_t g_backdrop_positive_y_count = 0;
/* Records on the negative Y face; backdrop_generate_default_records sets 4. */
// GLOBAL: XVT 0x523574
uint16_t g_backdrop_negative_y_count = 0;
/* Records on the positive Z face; backdrop_generate_default_records sets 3. */
// GLOBAL: XVT 0x523578
uint16_t g_backdrop_positive_z_count = 0;
/* Records on the negative Z face; backdrop_generate_default_records sets 3. */
// GLOBAL: XVT 0x52357C
uint16_t g_backdrop_negative_z_count = 0;
/* Records on the positive X face; backdrop_generate_default_records sets 4. */
// GLOBAL: XVT 0x523580
uint16_t g_backdrop_positive_x_count = 0;
/* Records on the negative X face; backdrop_generate_default_records sets 4. */
// GLOBAL: XVT 0x523584
uint16_t g_backdrop_negative_x_count = 0;
/* Entry i holds (i * g_cam_mat_r1_x) >> 5, i from 0 to 15; the nine step tables
 * are rebuilt each frame by backdrop_build_star_offsets_and_render, their only
 * writer and reader. */
// GLOBAL: XVT 0x9A8DD0
int32_t g_backdrop_cam_r1x_steps[16] = {0};
/* Entry i holds (i * g_cam_mat_r2_x) >> 5; see g_backdrop_cam_r1x_steps. */
// GLOBAL: XVT 0x9D1270
int32_t g_backdrop_cam_r2x_steps[16] = {0};
/* Entry i holds (i * g_cam_mat_r0_x) >> 5; see g_backdrop_cam_r1x_steps. */
// GLOBAL: XVT 0x9D80D0
int32_t g_backdrop_cam_r0x_steps[16] = {0};
/* Entry i holds (i * g_cam_mat_r1_y) >> 5; see g_backdrop_cam_r1x_steps. */
// GLOBAL: XVT 0x9E9600
int32_t g_backdrop_cam_r1y_steps[16] = {0};
/* Entry i holds (i * g_cam_mat_r2_y) >> 5; see g_backdrop_cam_r1x_steps. */
// GLOBAL: XVT 0x9EC480
int32_t g_backdrop_cam_r2y_steps[16] = {0};
/* Entry i holds (i * g_cam_mat_r0_y) >> 5; see g_backdrop_cam_r1x_steps. */
// GLOBAL: XVT 0x9FE740
int32_t g_backdrop_cam_r0y_steps[16] = {0};
/* Entry i holds (i * g_cam_mat_r1_z) >> 5; see g_backdrop_cam_r1x_steps. */
// GLOBAL: XVT 0xA004E0
int32_t g_backdrop_cam_r1z_steps[16] = {0};
/* Entry i holds (i * g_cam_mat_r2_z) >> 5; see g_backdrop_cam_r1x_steps. */
// GLOBAL: XVT 0xA07C80
int32_t g_backdrop_cam_r2z_steps[16] = {0};
/* Entry i holds (i * g_cam_mat_r0_z) >> 5; see g_backdrop_cam_r1x_steps. */
// GLOBAL: XVT 0xA08250
int32_t g_backdrop_cam_r0z_steps[16] = {0};

/* Draws image 0 of a model type's texture block at a screen point, rolled by
 * angle, at size 256: through render_quad_draw_rotated_sprite with
 * g_use_hardware3d, else with the software rotated-sprite functions. Sets
 * g_flight_sw_rot_sprite_span_runs_enabled to 1, g_cam_rel_world_z to 0x100000 and
 * g_view_space_depth to 0x7FFFFFFF first. It reads the image after unlocking the
 * type's resource handle. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x420110
void backdrop_draw_model_tex_quad_at_screen(int model_type, int screen_x,
					    int screen_y, int angle)
{
	g_flight_sw_rot_sprite_span_runs_enabled = 1;
	g_cam_rel_world_z = 0x100000;
	g_view_space_depth = 0x7FFFFFFF;
	if (g_object_type_table[model_type].resource_handle == 0) {
		XVT_LOG_ERROR("render.backdrop_missing type=%d", model_type);
	}
	const uint8_t *model_data = (const uint8_t *)memory_get_handle_block(
		g_object_type_table[model_type].resource_handle);
	memory_handle_block_done_stub(
		g_object_type_table[model_type].resource_handle);
	const struct tex_level_header *texture_header =
		(const struct tex_level_header *)model_data;
	struct sprite_payload *sprite =
		(struct sprite_payload
			 *)(model_data +
			    *(const uint32_t
				      *)(model_data +
					 texture_header
						 ->image_offset_table_offset));
	if (g_use_hardware3d != 0) {
		render_quad_draw_rotated_sprite(angle, screen_x, screen_y, 256,
						sprite);
	} else {
		uint16_t software_angle = (uint16_t)angle;
		flight_sw_prepare_sprite_rotation_tables(
			software_angle, FLIGHT_SW_16BPP_BYTES_PER_PIXEL);
		flight_sw_load_sprite_palette_tables(sprite);
		flight_sw_draw_rotated_sprite_quad(
			(int16_t)screen_x, (int16_t)screen_y, 256, sprite);
	}
}

/* Rebuilds the backdrop step tables and the starfield jitter, then draws the
 * backdrops. Each step table entry i is (i * camera matrix term) >> 5. The 125
 * entries of g_starfield_jitter_x, Y and Z get rows 0, 1 and 2 of the camera
 * matrix dotted with the grid point (x - 2, y - 2, z - 2), x, y and z each 0 to
 * 4, then >> 7. Unless g_backdrops_enabled is 0 it draws the records of one face
 * per axis, Y, then X, then Z: the positive face's when the camera matrix's row
 * 2 term for that axis is not negative, else the negative face's. A record's
 * view position adds the steps its packed direction names on the face's other
 * two axes (X then Z for a Y face, Y then Z for X, Y then X for Z) and the
 * camera column of the face's axis >> 2, added or taken away by the face's
 * sign; a record with a negative view depth is skipped. Each goes to
 * backdrop_project_and_draw_screen_quad with its 1-based record number and a roll
 * of -trig2_arctan(g_cam_mat_r1_x, g_cam_mat_r0_x), or of the Y terms for the X
 * faces. */
// FUNCTION: XVT 0x426080
void backdrop_build_star_offsets_and_render(void)
{
	enum {
		CAMERA_STEP_COUNT = 16,
		CAMERA_STEP_SHIFT = 5,
		STAR_GRID_SIZE = 5,
		STAR_GRID_RADIUS = 2,
		STAR_JITTER_SHIFT = 7,
		CAMERA_QUARTER_SHIFT = 2,
		DIRECTION_LOW_INDEX_MASK = 0x07,
		DIRECTION_NEGATE_LOW_BIT = 0x08,
		DIRECTION_HIGH_INDEX_MASK = 0x70,
		DIRECTION_NEGATE_HIGH_BIT = 0x80
	};

	int accum_r0x = 0;
	int accum_r0y = 0;
	int accum_r0z = 0;
	int accum_r1x = 0;
	int accum_r1y = 0;
	int accum_r1z = 0;
	int accum_r2x = 0;
	int accum_r2y = 0;
	int accum_r2z = 0;
	for (int step_index = 0; step_index < CAMERA_STEP_COUNT; ++step_index) {
		g_backdrop_cam_r0x_steps[step_index] =
			accum_r0x >> CAMERA_STEP_SHIFT;
		g_backdrop_cam_r1x_steps[step_index] =
			accum_r1x >> CAMERA_STEP_SHIFT;
		g_backdrop_cam_r2x_steps[step_index] =
			accum_r2x >> CAMERA_STEP_SHIFT;
		g_backdrop_cam_r0y_steps[step_index] =
			accum_r0y >> CAMERA_STEP_SHIFT;
		g_backdrop_cam_r1y_steps[step_index] =
			accum_r1y >> CAMERA_STEP_SHIFT;
		g_backdrop_cam_r2y_steps[step_index] =
			accum_r2y >> CAMERA_STEP_SHIFT;
		g_backdrop_cam_r0z_steps[step_index] =
			accum_r0z >> CAMERA_STEP_SHIFT;
		g_backdrop_cam_r1z_steps[step_index] =
			accum_r1z >> CAMERA_STEP_SHIFT;
		g_backdrop_cam_r2z_steps[step_index] =
			accum_r2z >> CAMERA_STEP_SHIFT;
		accum_r0x += g_cam_mat_r0_x;
		accum_r1x += g_cam_mat_r1_x;
		accum_r2x += g_cam_mat_r2_x;
		accum_r0y += g_cam_mat_r0_y;
		accum_r1y += g_cam_mat_r1_y;
		accum_r2y += g_cam_mat_r2_y;
		accum_r0z += g_cam_mat_r0_z;
		accum_r1z += g_cam_mat_r1_z;
		accum_r2z += g_cam_mat_r2_z;
	}

	int grid_base_r2 = -STAR_GRID_RADIUS *
			   (g_cam_mat_r2_x + g_cam_mat_r2_y + g_cam_mat_r2_z);
	int grid_base_r1 = -STAR_GRID_RADIUS *
			   (g_cam_mat_r1_x + g_cam_mat_r1_y + g_cam_mat_r1_z);
	int grid_base_r0 = -STAR_GRID_RADIUS *
			   (g_cam_mat_r0_x + g_cam_mat_r0_y + g_cam_mat_r0_z);
	int jitter_index = 0;
	for (int grid_x = 0; grid_x < STAR_GRID_SIZE; ++grid_x) {
		int grid_row_r0 = grid_base_r0;
		int grid_row_r1 = grid_base_r1;
		int grid_row_r2 = grid_base_r2;
		for (int grid_y = 0; grid_y < STAR_GRID_SIZE; ++grid_y) {
			int grid_value_r0 = grid_row_r0;
			int grid_value_r1 = grid_row_r1;
			int grid_value_r2 = grid_row_r2;
			for (int grid_z = 0; grid_z < STAR_GRID_SIZE;
			     ++grid_z) {
				g_starfield_jitter_x[jitter_index] =
					grid_value_r0 >> STAR_JITTER_SHIFT;
				g_starfield_jitter_y[jitter_index] =
					grid_value_r1 >> STAR_JITTER_SHIFT;
				g_starfield_jitter_z[jitter_index] =
					grid_value_r2 >> STAR_JITTER_SHIFT;
				++jitter_index;
				grid_value_r0 += g_cam_mat_r0_z;
				grid_value_r2 += g_cam_mat_r2_z;
				grid_value_r1 += g_cam_mat_r1_z;
			}
			grid_row_r0 += g_cam_mat_r0_y;
			grid_row_r1 += g_cam_mat_r1_y;
			grid_row_r2 += g_cam_mat_r2_y;
		}
		grid_base_r2 += g_cam_mat_r2_x;
		grid_base_r1 += g_cam_mat_r1_x;
		grid_base_r0 += g_cam_mat_r0_x;
	}

	if (g_backdrops_enabled == 0) {
		return;
	}

	unsigned int direction_index = 0;
	int16_t angle = (int16_t)-trig2_arctan(g_cam_mat_r1_x, g_cam_mat_r0_x);
	int direction_count;
	int low_step_index;
	unsigned int high_step_index;
	int basis_x;
	int basis_y;
	int basis_z;
	int view_x;
	int view_y;
	int view_z;
	uint8_t packed_direction;
	if (g_cam_mat_r2_y >= 0) {
		for (direction_count = g_backdrop_positive_y_count;
		     direction_count-- != 0;) {
			packed_direction =
				g_backdrop_packed_directions[direction_index++];
			low_step_index = (uint8_t)packed_direction &
					 DIRECTION_LOW_INDEX_MASK;
			basis_y = g_backdrop_cam_r1x_steps[low_step_index];
			basis_z = g_backdrop_cam_r2x_steps[low_step_index];
			basis_x = g_backdrop_cam_r0x_steps[low_step_index];
			if (((uint8_t)packed_direction &
			     DIRECTION_NEGATE_LOW_BIT) != 0) {
				basis_x = -basis_x;
				basis_y = -basis_y;
				basis_z = -basis_z;
			}
			if ((packed_direction & DIRECTION_NEGATE_HIGH_BIT) !=
			    0) {
				high_step_index = (packed_direction &
						   DIRECTION_HIGH_INDEX_MASK) >>
						  4;
				view_x = basis_x - g_backdrop_cam_r0z_steps
							   [high_step_index];
				view_y = basis_y - g_backdrop_cam_r1z_steps
							   [high_step_index];
				view_z = basis_z - g_backdrop_cam_r2z_steps
							   [high_step_index];
			} else {
				high_step_index = (packed_direction &
						   DIRECTION_HIGH_INDEX_MASK) >>
						  4;
				view_x = basis_x + g_backdrop_cam_r0z_steps
							   [high_step_index];
				view_y = basis_y + g_backdrop_cam_r1z_steps
							   [high_step_index];
				view_z = basis_z + g_backdrop_cam_r2z_steps
							   [high_step_index];
			}
			view_x += g_cam_mat_r0_y >> CAMERA_QUARTER_SHIFT;
			view_y += g_cam_mat_r1_y >> CAMERA_QUARTER_SHIFT;
			view_z += g_cam_mat_r2_y >> CAMERA_QUARTER_SHIFT;
			if (view_z >= 0) {
				backdrop_project_and_draw_screen_quad(
					view_x, view_y, view_z, angle,
					direction_index);
			}
		}
		direction_index += g_backdrop_negative_y_count;
	} else {
		direction_index += g_backdrop_positive_y_count;
		for (direction_count = g_backdrop_negative_y_count;
		     direction_count-- != 0;) {
			packed_direction =
				g_backdrop_packed_directions[direction_index++];
			low_step_index = (uint8_t)packed_direction &
					 DIRECTION_LOW_INDEX_MASK;
			basis_x = g_backdrop_cam_r0x_steps[low_step_index];
			basis_y = g_backdrop_cam_r1x_steps[low_step_index];
			basis_z = g_backdrop_cam_r2x_steps[low_step_index];
			if (((uint8_t)packed_direction &
			     DIRECTION_NEGATE_LOW_BIT) != 0) {
				basis_x = -basis_x;
				basis_y = -basis_y;
				basis_z = -basis_z;
			}
			if ((packed_direction & DIRECTION_NEGATE_HIGH_BIT) !=
			    0) {
				high_step_index = (packed_direction &
						   DIRECTION_HIGH_INDEX_MASK) >>
						  4;
				view_x = basis_x - g_backdrop_cam_r0z_steps
							   [high_step_index];
				view_y = basis_y - g_backdrop_cam_r1z_steps
							   [high_step_index];
				view_z = basis_z - g_backdrop_cam_r2z_steps
							   [high_step_index];
			} else {
				high_step_index = (packed_direction &
						   DIRECTION_HIGH_INDEX_MASK) >>
						  4;
				view_x = basis_x + g_backdrop_cam_r0z_steps
							   [high_step_index];
				view_y = basis_y + g_backdrop_cam_r1z_steps
							   [high_step_index];
				view_z = basis_z + g_backdrop_cam_r2z_steps
							   [high_step_index];
			}
			view_x -= g_cam_mat_r0_y >> CAMERA_QUARTER_SHIFT;
			view_y -= g_cam_mat_r1_y >> CAMERA_QUARTER_SHIFT;
			view_z -= g_cam_mat_r2_y >> CAMERA_QUARTER_SHIFT;
			if (view_z >= 0) {
				backdrop_project_and_draw_screen_quad(
					view_x, view_y, view_z, angle,
					direction_index);
			}
		}
	}

	angle = (int16_t)-trig2_arctan(g_cam_mat_r1_y, g_cam_mat_r0_y);
	if (g_cam_mat_r2_x >= 0) {
		for (direction_count = g_backdrop_positive_x_count;
		     direction_count-- != 0;) {
			packed_direction =
				g_backdrop_packed_directions[direction_index++];
			low_step_index = (uint8_t)packed_direction &
					 DIRECTION_LOW_INDEX_MASK;
			basis_x = g_backdrop_cam_r0y_steps[low_step_index];
			basis_y = g_backdrop_cam_r1y_steps[low_step_index];
			basis_z = g_backdrop_cam_r2y_steps[low_step_index];
			if (((uint8_t)packed_direction &
			     DIRECTION_NEGATE_LOW_BIT) != 0) {
				basis_x = -basis_x;
				basis_y = -basis_y;
				basis_z = -basis_z;
			}
			if ((packed_direction & DIRECTION_NEGATE_HIGH_BIT) !=
			    0) {
				high_step_index = (packed_direction &
						   DIRECTION_HIGH_INDEX_MASK) >>
						  4;
				view_x = basis_x - g_backdrop_cam_r0z_steps
							   [high_step_index];
				view_y = basis_y - g_backdrop_cam_r1z_steps
							   [high_step_index];
				view_z = basis_z - g_backdrop_cam_r2z_steps
							   [high_step_index];
			} else {
				high_step_index = (packed_direction &
						   DIRECTION_HIGH_INDEX_MASK) >>
						  4;
				view_x = basis_x + g_backdrop_cam_r0z_steps
							   [high_step_index];
				view_y = basis_y + g_backdrop_cam_r1z_steps
							   [high_step_index];
				view_z = basis_z + g_backdrop_cam_r2z_steps
							   [high_step_index];
			}
			view_x += g_cam_mat_r0_x >> CAMERA_QUARTER_SHIFT;
			view_y += g_cam_mat_r1_x >> CAMERA_QUARTER_SHIFT;
			view_z += g_cam_mat_r2_x >> CAMERA_QUARTER_SHIFT;
			if (view_z >= 0) {
				backdrop_project_and_draw_screen_quad(
					view_x, view_y, view_z, angle,
					direction_index);
			}
		}
		direction_index += g_backdrop_negative_x_count;
	} else {
		direction_index += g_backdrop_positive_x_count;
		for (direction_count = g_backdrop_negative_x_count;
		     direction_count-- != 0;) {
			packed_direction =
				g_backdrop_packed_directions[direction_index++];
			low_step_index = (uint8_t)packed_direction &
					 DIRECTION_LOW_INDEX_MASK;
			basis_x = g_backdrop_cam_r0y_steps[low_step_index];
			basis_y = g_backdrop_cam_r1y_steps[low_step_index];
			basis_z = g_backdrop_cam_r2y_steps[low_step_index];
			if (((uint8_t)packed_direction &
			     DIRECTION_NEGATE_LOW_BIT) != 0) {
				basis_x = -basis_x;
				basis_y = -basis_y;
				basis_z = -basis_z;
			}
			if ((packed_direction & DIRECTION_NEGATE_HIGH_BIT) !=
			    0) {
				high_step_index = (packed_direction &
						   DIRECTION_HIGH_INDEX_MASK) >>
						  4;
				view_x = basis_x - g_backdrop_cam_r0z_steps
							   [high_step_index];
				view_y = basis_y - g_backdrop_cam_r1z_steps
							   [high_step_index];
				view_z = basis_z - g_backdrop_cam_r2z_steps
							   [high_step_index];
			} else {
				high_step_index = (packed_direction &
						   DIRECTION_HIGH_INDEX_MASK) >>
						  4;
				view_x = basis_x + g_backdrop_cam_r0z_steps
							   [high_step_index];
				view_y = basis_y + g_backdrop_cam_r1z_steps
							   [high_step_index];
				view_z = basis_z + g_backdrop_cam_r2z_steps
							   [high_step_index];
			}
			view_x -= g_cam_mat_r0_x >> CAMERA_QUARTER_SHIFT;
			view_y -= g_cam_mat_r1_x >> CAMERA_QUARTER_SHIFT;
			view_z -= g_cam_mat_r2_x >> CAMERA_QUARTER_SHIFT;
			if (view_z >= 0) {
				backdrop_project_and_draw_screen_quad(
					view_x, view_y, view_z, angle,
					direction_index);
			}
		}
	}

	angle = (int16_t)-trig2_arctan(g_cam_mat_r1_x, g_cam_mat_r0_x);
	if (g_cam_mat_r2_z >= 0) {
		for (direction_count = g_backdrop_positive_z_count;
		     direction_count-- != 0;) {
			packed_direction =
				g_backdrop_packed_directions[direction_index++];
			low_step_index = (uint8_t)packed_direction &
					 DIRECTION_LOW_INDEX_MASK;
			basis_x = g_backdrop_cam_r0y_steps[low_step_index];
			basis_y = g_backdrop_cam_r1y_steps[low_step_index];
			basis_z = g_backdrop_cam_r2y_steps[low_step_index];
			if (((uint8_t)packed_direction &
			     DIRECTION_NEGATE_LOW_BIT) != 0) {
				basis_x = -basis_x;
				basis_y = -basis_y;
				basis_z = -basis_z;
			}
			if ((packed_direction & DIRECTION_NEGATE_HIGH_BIT) !=
			    0) {
				high_step_index = (packed_direction &
						   DIRECTION_HIGH_INDEX_MASK) >>
						  4;
				view_x = basis_x - g_backdrop_cam_r0x_steps
							   [high_step_index];
				view_y = basis_y - g_backdrop_cam_r1x_steps
							   [high_step_index];
				view_z = basis_z - g_backdrop_cam_r2x_steps
							   [high_step_index];
			} else {
				high_step_index = (packed_direction &
						   DIRECTION_HIGH_INDEX_MASK) >>
						  4;
				view_x = basis_x + g_backdrop_cam_r0x_steps
							   [high_step_index];
				view_y = basis_y + g_backdrop_cam_r1x_steps
							   [high_step_index];
				view_z = basis_z + g_backdrop_cam_r2x_steps
							   [high_step_index];
			}
			view_x += g_cam_mat_r0_z >> CAMERA_QUARTER_SHIFT;
			view_y += g_cam_mat_r1_z >> CAMERA_QUARTER_SHIFT;
			view_z += g_cam_mat_r2_z >> CAMERA_QUARTER_SHIFT;
			if (view_z >= 0) {
				backdrop_project_and_draw_screen_quad(
					view_x, view_y, view_z, angle,
					direction_index);
			}
		}
	} else {
		direction_index += g_backdrop_positive_z_count;
		for (direction_count = g_backdrop_negative_z_count;
		     direction_count-- != 0;) {
			packed_direction =
				g_backdrop_packed_directions[direction_index++];
			low_step_index = (uint8_t)packed_direction &
					 DIRECTION_LOW_INDEX_MASK;
			basis_x = g_backdrop_cam_r0y_steps[low_step_index];
			basis_y = g_backdrop_cam_r1y_steps[low_step_index];
			basis_z = g_backdrop_cam_r2y_steps[low_step_index];
			if (((uint8_t)packed_direction &
			     DIRECTION_NEGATE_LOW_BIT) != 0) {
				basis_x = -basis_x;
				basis_y = -basis_y;
				basis_z = -basis_z;
			}
			if ((packed_direction & DIRECTION_NEGATE_HIGH_BIT) !=
			    0) {
				high_step_index = (packed_direction &
						   DIRECTION_HIGH_INDEX_MASK) >>
						  4;
				view_x = basis_x - g_backdrop_cam_r0x_steps
							   [high_step_index];
				view_y = basis_y - g_backdrop_cam_r1x_steps
							   [high_step_index];
				view_z = basis_z - g_backdrop_cam_r2x_steps
							   [high_step_index];
			} else {
				high_step_index = (packed_direction &
						   DIRECTION_HIGH_INDEX_MASK) >>
						  4;
				view_x = basis_x + g_backdrop_cam_r0x_steps
							   [high_step_index];
				view_y = basis_y + g_backdrop_cam_r1x_steps
							   [high_step_index];
				view_z = basis_z + g_backdrop_cam_r2x_steps
							   [high_step_index];
			}
			view_x -= g_cam_mat_r0_z >> CAMERA_QUARTER_SHIFT;
			view_y -= g_cam_mat_r1_z >> CAMERA_QUARTER_SHIFT;
			view_z -= g_cam_mat_r2_z >> CAMERA_QUARTER_SHIFT;
			if (view_z >= 0) {
				backdrop_project_and_draw_screen_quad(
					view_x, view_y, view_z, angle,
					direction_index);
			}
		}
	}
}

/* Projects a backdrop's view position and draws it; draws nothing when the
 * size of view_x or of view_y is greater than view_z. Each offset is
 * (size * (1 << g_perspective_shift) + g_proj_scale_half_int) / view_z, worked
 * in 64 bits, with the coordinate's sign, or 0x7FFFFF00 when the quotient
 * would not fit in 32 bits. It adds g_flight_vp_center_x to X and
 * g_flight_vp_center_y and g_proj_offset_y to Y, and draws
 * g_backdrop_model_types[backdrop_number - 1] with
 * backdrop_draw_model_tex_quad_at_screen at that X and g_flight_vp_height less
 * that Y. The modern build masks the shift to 5 bits and negates without
 * signed overflow. */
// FUNCTION: XVT 0x426860
void backdrop_project_and_draw_screen_quad(int view_x, int view_y, int view_z,
					   int angle, int backdrop_number)
{
	enum { PROJECTION_WORD_BITS = 32, PROJECTION_SATURATION = 0x7FFFFF00 };

	uint32_t projection_scale;
#ifdef XVT_MODERN
	projection_scale =
		1u << (g_perspective_shift & (PROJECTION_WORD_BITS - 1));
#else
	projection_scale = 1u << g_perspective_shift;
#endif

	int projected_x;
	if (view_x < 0) {
		int projection_depth = view_z;
		int magnitude;
#ifdef XVT_MODERN
		magnitude = (int)(0u - (unsigned int)view_x);
#else
		magnitude = -view_x;
#endif
		if (projection_depth < magnitude) {
			return;
		}
		uint64_t numerator =
			(uint64_t)(uint32_t)magnitude * projection_scale +
			(uint32_t)g_proj_scale_half_int;
		uint32_t quotient;
#ifdef XVT_MODERN
		if ((uint32_t)(numerator >> PROJECTION_WORD_BITS) <
		    (uint32_t)projection_depth) {
#else
		if (((const uint32_t *)&numerator)[1] <
		    (uint32_t)projection_depth) {
#endif
			quotient = (uint32_t)(numerator /
					      (uint32_t)projection_depth);
		} else {
			quotient = PROJECTION_SATURATION;
		}
		projected_x = -(int)quotient;
	} else {
		int projection_depth = view_z;
		int magnitude = view_x;
		if (projection_depth < magnitude) {
			return;
		}
		uint64_t numerator =
			(uint64_t)(uint32_t)magnitude * projection_scale +
			(uint32_t)g_proj_scale_half_int;
#ifdef XVT_MODERN
		if ((uint32_t)(numerator >> PROJECTION_WORD_BITS) <
		    (uint32_t)projection_depth) {
#else
		if (((const uint32_t *)&numerator)[1] <
		    (uint32_t)projection_depth) {
#endif
			projected_x =
				(int)(numerator / (uint32_t)projection_depth);
		} else {
			projected_x = PROJECTION_SATURATION;
		}
	}

	int projected_y;
	if (view_y < 0) {
		int projection_depth = view_z;
		int magnitude;
#ifdef XVT_MODERN
		magnitude = (int)(0u - (unsigned int)view_y);
#else
		magnitude = -view_y;
#endif
		if (projection_depth < magnitude) {
			return;
		}
		uint64_t numerator =
			(uint64_t)(uint32_t)magnitude * projection_scale +
			(uint32_t)g_proj_scale_half_int;
		uint32_t quotient;
#ifdef XVT_MODERN
		if ((uint32_t)(numerator >> PROJECTION_WORD_BITS) <
		    (uint32_t)projection_depth) {
#else
		if (((const uint32_t *)&numerator)[1] <
		    (uint32_t)projection_depth) {
#endif
			quotient = (uint32_t)(numerator /
					      (uint32_t)projection_depth);
		} else {
			quotient = PROJECTION_SATURATION;
		}
		projected_y = -(int)quotient;
	} else {
		int projection_depth = view_z;
		int magnitude = view_y;
		if (projection_depth < magnitude) {
			return;
		}
		uint64_t numerator =
			(uint64_t)(uint32_t)magnitude * projection_scale +
			(uint32_t)g_proj_scale_half_int;
#ifdef XVT_MODERN
		if ((uint32_t)(numerator >> PROJECTION_WORD_BITS) <
		    (uint32_t)projection_depth) {
#else
		if (((const uint32_t *)&numerator)[1] <
		    (uint32_t)projection_depth) {
#endif
			projected_y =
				(int)(numerator / (uint32_t)projection_depth);
		} else {
			projected_y = PROJECTION_SATURATION;
		}
	}
	projected_x += (int)g_flight_vp_center_x;
	projected_y += (int)g_flight_vp_center_y;
	projected_y += g_proj_offset_y;
	backdrop_draw_model_tex_quad_at_screen(
		g_backdrop_model_types[backdrop_number - 1], projected_x,
		(int)g_flight_vp_height - projected_y, angle);
}

/* Fills the first 22 backdrop records at random with game_rand: 4 each on
 * the Y and X faces and 3 on each Z face. Each packed direction is
 * low + (high << 4), low and high each one of 4, 6, 8, 10 and 12, so a
 * value of 8 or more reads back as the negate bit with index value - 8.
 * Each model type comes from r = game_rand() & 31: 117 for r under 3,
 * 117 + r / 3 for r under 12, else 125 + (r & 1). mission_init is its only
 * caller, with the random state seeded from the mission's backdrop
 * value. */
// FUNCTION: XVT 0x458ED0
void backdrop_generate_default_records(void)
{
	g_backdrop_positive_y_count = 4;
	g_backdrop_negative_y_count = 4;
	uint16_t direction_record_idx = 0;
	g_backdrop_positive_x_count = 4;
	g_backdrop_negative_x_count = 4;
	g_backdrop_positive_z_count = 3;
	g_backdrop_negative_z_count = 3;
	uint16_t low_coord;
	uint16_t high_coord;
	while (direction_record_idx < 22) {
		do {
			int16_t low_coord_roll = game_rand() & 14;
			low_coord = (uint16_t)(low_coord_roll + 4);
		} while (low_coord > 12);
		do {
			high_coord = (uint16_t)((game_rand() & 14) + 4);
		} while (high_coord > 12);
		high_coord <<= 4;
		high_coord += low_coord;
		g_backdrop_packed_directions[direction_record_idx++] =
			(uint8_t)high_coord;
	}

	uint8_t model_type;
	/* low_coord is reused here as the record index for the model types. */
	for (low_coord = 0; low_coord < 22; low_coord++) {
		uint16_t model_type_roll = (uint16_t)(game_rand() & 31);
		if (model_type_roll < 3) {
			g_backdrop_model_types[low_coord] = 117;
		} else {
			if (model_type_roll < 12) {
				model_type =
					(uint8_t)(model_type_roll / 3 + 117);
			} else {
				model_type =
					(uint8_t)((model_type_roll & 1) + 125);
			}
			g_backdrop_model_types[low_coord] = model_type;
		}
	}
}
