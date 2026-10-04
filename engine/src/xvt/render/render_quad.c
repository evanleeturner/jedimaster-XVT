#include "xvt/render/render_quad.h"

#ifdef XVT_MODERN
#include "aeron/compat/host.h"
#endif

#include <string.h>

#include "xvt/assets/object_type.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_view.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/math.h"
#include "xvt/math/trig2.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_clip.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/render_texture.h"
#include "xvt/render/renderer.h"
#include "xvt/render/scene_billboard.h"
#include "xvt/render/std3d.h"
#include "xvt/render/tex_level.h"
#include "xvt/util/debug_console.h"
#include "xvt/util/memory.h"

/* Vertex color of an explosion billboard for each frame 0 to 31 of the
 * explosion (its type_specific_byte[0]): white with the alpha in the high byte,
 * rising from 0xD0 to 0xF0, falling back to 0x30 by frame 10, then 0x30. */
// GLOBAL: XVT 0x51A558
const uint32_t g_explosion_billboard_color_by_frame[32] = {
	0xd0ffffff, 0xe0ffffff, 0xf0ffffff, 0xf0ffffff, 0xe0ffffff, 0xd0ffffff,
	0xb0ffffff, 0x90ffffff, 0x70ffffff, 0x50ffffff, 0x30ffffff, 0x30ffffff,
	0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff,
	0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff,
	0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff, 0x30ffffff,
	0x30ffffff, 0x30ffffff,
};

/* Draws one queued billboard. The frame, without its 0x8000 bit, names the
 * object type whose resource holds the images (frame >> 7) and the image in its
 * offset table (the low 7 bits). Sets g_flight_sw_rot_sprite_span_runs_enabled to 1,
 * g_billboard_object_or_type_index to the record's object, g_cam_rel_world_x, Y and Z
 * to that object's offset from the local player's camera and g_view_space_depth
 * to depth_z; the size is scene_billboard_compute_projected_size with the type's
 * max_bounds_extent. With g_use_hardware3d it draws through
 * render_quad_draw_rotated_sprite, else through
 * flight_sw_prepare_sprite_rotation_tables, flight_sw_load_sprite_palette_tables and
 * flight_sw_draw_rotated_sprite_quad. It reads the image after unlocking the type's
 * resource handle. scene_billboard_render_queued_textured is its only caller. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x401450
void render_quad_draw_model_texture(
	struct scene_billboard_queue_entry *quad_record)
{
	uint16_t frame;
	uint16_t model_type;
	uint16_t screen_size;
	uint16_t handle;
	int camera_world_y;
	struct object_record *object;
	const uint8_t *model_data;
	const struct tex_level_header *texture_header;
	struct sprite_payload *sprite;

	frame = (uint16_t)quad_record->frame & 0x7FFFu;
	g_flight_sw_rot_sprite_span_runs_enabled = 1;
	model_type = frame >> 7;
	g_billboard_object_or_type_index = quad_record->object_or_type_index;
	object = &g_object_table[g_billboard_object_or_type_index];
	camera_world_y = g_players[g_local_player].view_state.camera_world_y;
	g_cam_rel_world_x = object->world_x -
			    g_players[g_local_player].view_state.camera_world_x;
	g_cam_rel_world_y = object->world_y - camera_world_y;
	g_cam_rel_world_z = object->world_z -
			    g_players[g_local_player].view_state.camera_world_z;
	g_view_space_depth = quad_record->depth_z;
	screen_size = (uint16_t)scene_billboard_compute_projected_size(
		quad_record->depth_z,
		(uint16_t)g_object_type_table[model_type].max_bounds_extent,
		(uint16_t)quad_record->screen_size);
	handle = g_object_type_table[model_type].resource_handle;
	model_data = (const uint8_t *)memory_get_handle_block(handle);
	frame &= 0x7Fu;
	memory_handle_block_done_stub(handle);
	texture_header = (const struct tex_level_header *)model_data;
	sprite = (struct sprite_payload
			  *)(model_data +
			     *(const uint32_t
				       *)(model_data +
					  texture_header
						  ->image_offset_table_offset +
					  frame * sizeof(uint32_t)));
	if (g_use_hardware3d != 0) {
		render_quad_draw_rotated_sprite(
			quad_record->rotation_angle, quad_record->screen_x,
			quad_record->screen_y, screen_size, sprite);
	} else {
		flight_sw_prepare_sprite_rotation_tables(
			quad_record->rotation_angle,
			FLIGHT_SW_16BPP_BYTES_PER_PIXEL);
		flight_sw_load_sprite_palette_tables(sprite);
		flight_sw_draw_rotated_sprite_quad(quad_record->screen_x,
						   quad_record->screen_y,
						   screen_size, sprite);
	}
}

/* Adds a textured, rotated square to the Direct3D batch: the image at
 * texture_image, centered at screen_x and screen_y (Y counted up from the
 * viewport's bottom), each half side (screen_size * image side) >> 9, turned by
 * angle. The color is white, or for an explosion in a main object slot
 * (g_billboard_object_or_type_index) g_explosion_billboard_color_by_frame at its
 * frame; 0xFEFFFFFF when g_cap_vertex_alpha is set, which it clears. Its depth
 * is 1 / (g_view_space_depth * g_inv_depth_proj_scale + 1), 1 being
 * g_render_unit_float; when g_view_space_depth, read unsigned, is over 0x1000000
 * it is the fixed value 0.00012205541 and the color is white. With
 * g_std3dz_compare_cap 2 the depth is 1 less that. The texture is the image cut
 * to 256 by 256 and rounded up to powers of two, square when the device needs
 * it, from render_texture_get_or_create_bitmap. The square is clipped to the
 * viewport (top, bottom, left, right), nothing is drawn below 3 corners, the
 * batch is flushed through std3D first when it would overflow, and the
 * triangles go in as a fan with the sprite flags, the bilinear ones when
 * g_bilinear_enabled is set. The modern build returns at once while classic
 * flight drawing is suppressed, skips a clipping pass after one that left
 * nothing, and starts the color at white; the original build leaves it unset
 * for an object past the main slots unless the depth is over 0x1000000. */
// FUNCTION: XVT 0x40BBF0
void render_quad_draw_rotated_sprite(int angle, int screen_x, int screen_y,
				     uint16_t screen_size,
				     const void *texture_image)
{
	enum {
		EXPLOSION_FRAME_COUNT = 32,
		CLIP_VERTEX_CAPACITY = 40,
		MAX_TEXTURE_DIMENSION = 256,
		TEXTURE_DIMENSION_STEPS = 8,
		TEXTURE_SCALE_SHIFT = 9,
		INITIAL_QUAD_VERTEX_COUNT = 4,
		MIN_TRIANGLE_VERTEX_COUNT = 3,
		TRIANGLE_FAN_FIRST_INDEX = 2,
		SPRITE_BASE_RENDER_FLAGS = 2066,
		BILINEAR_RENDER_FLAGS = 384,
		SPRITE_ALPHA_RENDER_FLAGS = 512
	};

	const uint8_t *texture_bytes;
	const struct tex_level_image_header *image_header;
	uint32_t color;
	float computed_depth;
	float depth;
	int source_width;
	int source_height;
	int power_of_two_width;
	int power_of_two_height;
	int width;
	int height;
	float max_u;
	float max_v;
	int half_width;
	int half_height;
	int negative_half_height;
	int negative_half_width;
	int x_offset;
	int y_offset;
	struct render_clip_vertex vertices[CLIP_VERTEX_CAPACITY];
	int previous_index;
	int vertex_index;
	uint32_t vertex_color;
	uint32_t vertex_specular;
	uint16_t *palette;
	const uint8_t *pixels;
	int rle_format;
	struct std3d_tex_cache_node *texture;

#ifdef XVT_MODERN
	/* Suppress before texture lookup so hidden classic draws do not refill the cache. */
	if (AeronDx5_IsClassicFlightRenderingSuppressed()) {
		return;
	}
#endif

	texture_bytes = (const uint8_t *)texture_image;
	image_header = (const struct tex_level_image_header *)texture_image;
	screen_y = g_flight_vp_height - screen_y;
#ifdef XVT_MODERN
	color = UINT32_MAX;
#endif
	if (g_billboard_object_or_type_index >= 0 &&
	    (unsigned int)g_region_main_object_slot_end >
		    (unsigned int)g_billboard_object_or_type_index) {
		struct object_record *object;
		int frame;

		object = &g_object_table[g_billboard_object_or_type_index];
		if (object->genus_id == CRAFT_GENUS_EXPLOSION) {
			frame = object->type_specific_byte[0];
			if (frame >= 0 && frame < EXPLOSION_FRAME_COUNT) {
				color = g_explosion_billboard_color_by_frame
					[frame];
			} else {
				color = UINT32_MAX;
			}
		} else {
			color = UINT32_MAX;
		}
	}

	if ((unsigned int)g_view_space_depth > 0x1000000u) {
		color = UINT32_MAX;
		computed_depth = 0.00012205541f;
		if (g_std3dz_compare_cap == 2) {
			computed_depth = 0.99987793f;
		}
	} else {
		computed_depth =
			g_render_unit_float /
			((float)g_view_space_depth * g_inv_depth_proj_scale +
			 g_render_unit_float);
		if (g_std3dz_compare_cap == 2) {
			computed_depth = g_render_unit_float - computed_depth;
		}
	}
	depth = computed_depth;
	source_width = (int32_t)image_header->width;
	source_height = (int32_t)image_header->height;
	if (source_width > MAX_TEXTURE_DIMENSION) {
		source_width = MAX_TEXTURE_DIMENSION;
		debug_printf("TRUNCATING BITMAP TO 256 WIDE!!!\n");
	}
	if (source_height > MAX_TEXTURE_DIMENSION) {
		source_height = MAX_TEXTURE_DIMENSION;
		debug_printf("TRUNCATING BITMAP TO 256 HIGH!!!\n");
	}
	max_u = (float)source_width;
	max_v = (float)source_height;

	/* Until the clip loops below, vertex_index counts the doubling steps of these two loops, not vertices. */
	power_of_two_width = 1;
	vertex_index = 0;
	do {
		power_of_two_width *= 2;
		if (power_of_two_width >= source_width) {
			break;
		}
		++vertex_index;
	} while (vertex_index < TEXTURE_DIMENSION_STEPS);
	power_of_two_height = 1;
	for (vertex_index = 0; vertex_index < TEXTURE_DIMENSION_STEPS;
	     ++vertex_index) {
		power_of_two_height *= 2;
		if (power_of_two_height >= source_height) {
			break;
		}
	}
	if (g_p_std3d_cur_device->caps.b_square_only_texture != 0) {
		if (power_of_two_width > power_of_two_height) {
			power_of_two_height = power_of_two_width;
		} else if (power_of_two_height > power_of_two_width) {
			power_of_two_width = power_of_two_height;
		}
	}
	width = power_of_two_width;
	height = power_of_two_height;

	max_u /= (float)width;
	max_v /= (float)height;
	half_width = (screen_size * (int32_t)image_header->width) >>
		     TEXTURE_SCALE_SHIFT;
	half_height = (screen_size * (int32_t)image_header->height) >>
		      TEXTURE_SCALE_SHIFT;
	angle = (uint16_t)angle;
	x_offset = trig2_cosinedwordmult(half_width, angle) +
		   trig2_sinedwordmult(half_height, angle);
	y_offset = trig2_cosinedwordmult(half_height, angle) -
		   trig2_sinedwordmult(half_width, angle);

	g_clip_count_a = INITIAL_QUAD_VERTEX_COUNT;
	g_clip_vert_cursor = INITIAL_QUAD_VERTEX_COUNT;
	g_clip_idx_a[0] = 0;
	g_clip_idx_a[1] = 1;
	g_clip_idx_a[2] = 2;
	g_clip_idx_a[3] = 3;

	vertices[0].x = (float)(screen_x + x_offset);
	vertices[0].y = (float)(screen_y + y_offset);
	vertices[0].scaled_inverse_depth = depth;
	memset(&vertices[0].light_intensity, 0, sizeof(float) * 3);
	negative_half_width = -half_width;
	x_offset = trig2_cosinedwordmult(negative_half_width, angle) +
		   trig2_sinedwordmult(half_height, angle);
	y_offset = trig2_cosinedwordmult(half_height, angle) -
		   trig2_sinedwordmult(negative_half_width, angle);
	vertices[1].x = (float)(screen_x + x_offset);
	vertices[1].y = (float)(screen_y + y_offset);
	vertices[1].scaled_inverse_depth = depth;
	vertices[1].light_intensity = 0.0f;
	vertices[1].u = max_u;
	vertices[1].v = 0.0f;
	negative_half_height = -half_height;
	x_offset = trig2_cosinedwordmult(negative_half_width, angle) +
		   trig2_sinedwordmult(negative_half_height, angle);
	y_offset = trig2_cosinedwordmult(negative_half_height, angle) -
		   trig2_sinedwordmult(negative_half_width, angle);
	vertices[2].x = (float)(screen_x + x_offset);
	vertices[2].y = (float)(screen_y + y_offset);
	vertices[2].scaled_inverse_depth = depth;
	vertices[2].light_intensity = 0.0f;
	vertices[2].u = max_u;
	vertices[2].v = max_v;
	x_offset = trig2_cosinedwordmult(half_width, angle) +
		   trig2_sinedwordmult(negative_half_height, angle);
	y_offset = trig2_cosinedwordmult(negative_half_height, angle) -
		   trig2_sinedwordmult(half_width, angle);
	vertices[3].x = (float)(screen_x + x_offset);
	vertices[3].y = (float)(screen_y + y_offset);
	vertices[3].scaled_inverse_depth = depth;
	vertices[3].light_intensity = 0.0f;
	vertices[3].u = 0.0f;
	vertices[3].v = max_v;

	g_clip_count_b = 0;
	previous_index = g_clip_idx_a[g_clip_count_a - 1];
	for (vertex_index = 0; vertex_index < g_clip_count_a; ++vertex_index) {
		int current_index;

		current_index = g_clip_idx_a[vertex_index];
		render_clip_clip_poly_top(previous_index, current_index,
					  vertices);
		previous_index = current_index;
	}
	g_clip_count_a = 0;
	/* A clipping pass can discard every vertex before the next pass. */
#ifdef XVT_MODERN
	if (g_clip_count_b > 0) {
#endif
		previous_index = g_clip_idx_b[g_clip_count_b - 1];
		for (vertex_index = 0; vertex_index < g_clip_count_b;
		     ++vertex_index) {
			int current_index;

			current_index = g_clip_idx_b[vertex_index];
			render_clip_clip_poly_bottom(previous_index,
						     current_index, vertices);
			previous_index = current_index;
		}
#ifdef XVT_MODERN
	}
#endif
	g_clip_count_b = 0;
#ifdef XVT_MODERN
	if (g_clip_count_a > 0) {
#endif
		previous_index = g_clip_idx_a[g_clip_count_a - 1];
		for (vertex_index = 0; vertex_index < g_clip_count_a;
		     ++vertex_index) {
			int current_index;

			current_index = g_clip_idx_a[vertex_index];
			render_clip_clip_poly_left(previous_index,
						   current_index, vertices);
			previous_index = current_index;
		}
#ifdef XVT_MODERN
	}
#endif
	g_clip_count_a = 0;
#ifdef XVT_MODERN
	if (g_clip_count_b > 0) {
#endif
		previous_index = g_clip_idx_b[g_clip_count_b - 1];
		for (vertex_index = 0; vertex_index < g_clip_count_b;
		     ++vertex_index) {
			int current_index;

			current_index = g_clip_idx_b[vertex_index];
			render_clip_clip_poly_right(previous_index,
						    current_index, vertices);
			previous_index = current_index;
		}
#ifdef XVT_MODERN
	}
#endif
	if (g_clip_count_a < MIN_TRIANGLE_VERTEX_COUNT) {
		return;
	}

	if (g_clip_count_a + g_d3d_vertex_count > g_max_batch_verts ||
	    g_clip_count_a + g_d3d_triangle_count > g_max_batch_tris) {
		math_set_fpu_extended_precision_mode();
		std3d_start_scene();
		std3d_lock_execute_buffer();
		std3d_add_vertices(g_flight_vertex_buffer, g_d3d_vertex_count);
		std3d_begin_instructions();
		std3d_add_triangles(g_tri_buffer,
				    (unsigned int)g_d3d_triangle_count);
		std3d_execute_buffer();
		std3d_end_scene();
		math_set_fpu_single_precision_mode();
		g_d3d_triangle_count = 0;
		g_d3d_vertex_count = 0;
	}
	if (g_cap_vertex_alpha != 0) {
		color = 0xfeffffff;
		g_cap_vertex_alpha = 0;
	}
	vertex_color = color;
	vertex_specular = 0;
	for (vertex_index = 0; vertex_index < g_clip_count_a; ++vertex_index) {
		int source_index;
		float source_y;
		float source_depth;
		float source_u;
		float source_v;

		source_index = g_clip_idx_a[vertex_index];
		source_y = vertices[source_index].y;
		source_u = vertices[source_index].u;
		source_v = vertices[source_index].v;
		source_depth = vertices[source_index].scaled_inverse_depth;
		g_flight_vertex_buffer[g_d3d_vertex_count].sx =
			vertices[source_index].x + g_flight_vp_origin_x;
		g_flight_vertex_buffer[g_d3d_vertex_count].sy =
			source_y + g_flight_vp_origin_y;
		g_flight_vertex_buffer[g_d3d_vertex_count].sz = source_depth;
		g_flight_vertex_buffer[g_d3d_vertex_count].rhw = source_depth;
		g_flight_vertex_buffer[g_d3d_vertex_count].tu = source_u;
		g_flight_vertex_buffer[g_d3d_vertex_count].tv = source_v;
		g_flight_vertex_buffer[g_d3d_vertex_count].color = vertex_color;
		g_flight_vertex_buffer[g_d3d_vertex_count].specular =
			vertex_specular;
		g_clip_idx_a[vertex_index] = g_d3d_vertex_count;
		++g_d3d_vertex_count;
	}
	palette = (uint16_t *)(texture_bytes +
			       image_header->converted_palette_offset);
	pixels = texture_bytes + image_header->encoded_image_offset + 16;
	rle_format = (int32_t)image_header->packing_mode;
	texture = render_texture_get_or_create_bitmap(width, height, palette,
						      pixels, rle_format);
	for (vertex_index = TRIANGLE_FAN_FIRST_INDEX;
	     vertex_index < g_clip_count_a; ++vertex_index) {
		g_tri_buffer[g_d3d_triangle_count].vertex_index0 =
			g_clip_idx_a[0];
		g_tri_buffer[g_d3d_triangle_count].vertex_index1 =
			g_clip_idx_a[vertex_index - 1];
		g_tri_buffer[g_d3d_triangle_count].vertex_index2 =
			g_clip_idx_a[vertex_index];
		g_tri_buffer[g_d3d_triangle_count].texture = texture;
		g_tri_buffer[g_d3d_triangle_count].flags =
			(std3d_render_state_flags)SPRITE_BASE_RENDER_FLAGS;
		if (g_bilinear_enabled != 0) {
			g_tri_buffer[g_d3d_triangle_count].flags +=
				BILINEAR_RENDER_FLAGS;
		}
		g_tri_buffer[g_d3d_triangle_count++].flags +=
			SPRITE_ALPHA_RENDER_FLAGS;
	}
}
