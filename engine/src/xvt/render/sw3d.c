#include "xvt/render/sw3d.h"

#include <string.h>

#include "xvt/flight/flight_surface.h"
#include "xvt/math/math3d.h"
#include "xvt/render/flight_light.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/render_clip.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"

struct software_light_sample {
	/* Lighting block row it was worked out for:
	 * g_sw3d_current_light_sample_cache_stamp at the time. */
	int stamp;
	/* Light, 0 to 1, at its block corner on that block row's top row. */
	float intensity;
	/* Light at the corner one block lower, less intensity. */
	float row_delta;
};

/* g_sw3d_light_sample_block_size - 1, 15. render_scene_allocate_buffers sets the five
 * lighting block globals. */
// GLOBAL: XVT 0x612284
int g_sw3d_light_sample_block_mask = 0;
/* Face sw3d_draw_visible_faces_to_surface is drawing; sw3d_draw_textured_span draws
 * it. */
// GLOBAL: XVT 0x612280
struct scene_face *g_sw3d_current_face = NULL;
/* Shade fraction carried from pixel to pixel, the low 8 bits of shade + carry,
 * which dithers between the 16 shade levels; each span starts it from
 * g_sw3d_shade_dither_initial_by_scanline_parity by its row's parity. */
// GLOBAL: XVT 0x612288
int g_sw3d_span_shade_dither_accum = 0;
/* Side in pixels of a lighting block, 16 (set by render_scene_allocate_buffers):
 * the software renderer works out light at block corners and interpolates
 * between them. */
// GLOBAL: XVT 0x61228C
int g_sw3d_light_sample_block_size = 0;
/* Byte offset in g_surface_pixels of the current row's first viewport pixel:
 * (row + g_flight_vp_y) * g_surface_pitch + g_flight_vp_x * g_flight_bytes_per_pixel.
 * Set by sw3d_draw_visible_faces_to_surface for each row and by
 * sw3d_blit_occluded_span. */
// GLOBAL: XVT 0x612294
int g_sw3d_span_framebuffer_row_offset = 0;
/* The current row's place in its lighting block: row within the
 * block / g_sw3d_light_sample_block_size, 0 at the block's top row.
 * sw3d_draw_visible_faces_to_surface sets it, with the two other row
 * globals, for each row with a span. */
// GLOBAL: XVT 0x61229C
static float g_sw3d_light_sample_subrow_lerp_t = 0.0f;
/* Only the modern build's render_scene_initialize writes it, clearing its 0x300
 * bits; nothing reads it. */
// GLOBAL: XVT 0x612298
uint32_t g_sw3d_fpu_control_word_scratch = 0;
/* FPU control word the original build's render_scene_initialize saves before it
 * sets single precision; nothing reads it. */
// GLOBAL: XVT 0x6122A0
uint32_t g_sw3d_initialize_scene_saved_fpu_control = 0;
/* Rows from the current one to the next lighting block's top, as a float; set
 * with g_sw3d_light_sample_subrow_lerp_t. */
// GLOBAL: XVT 0x6122B0
static float g_sw3d_light_sample_rows_to_next_block_float = 0.0f;
/* The current row's offset within its lighting block, as a float; set with
 * g_sw3d_light_sample_subrow_lerp_t. */
// GLOBAL: XVT 0x6122B4
static float g_sw3d_light_sample_subrow_float = 0.0f;
/* Texture v of the next pixel, in texels with 8 fraction bits;
 * sw3d_draw_textured_span sets it at each lighting block boundary and the pixel
 * loops step it. */
// GLOBAL: XVT 0x6122A8
int g_sw3d_span_vq8 = 0;
/* Texture u of the next pixel, in texels with 8 fraction bits; set and stepped
 * like g_sw3d_span_vq8. */
// GLOBAL: XVT 0x6122AC
int g_sw3d_span_uq8 = 0;
/* 1 / g_sw3d_light_sample_block_size, taken from g_sw3d_span_length_reciprocal[16]. */
// GLOBAL: XVT 0x6122B8
float g_sw3d_light_sample_inv_block_size = 0.0f;
/* Change in g_sw3d_span_shade_q8 per pixel. */
// GLOBAL: XVT 0x6122C4
int g_sw3d_span_shade_step_q8 = 0;
/* Viewport row sw3d_draw_visible_faces_to_surface is drawing. */
// GLOBAL: XVT 0x6122C8
int g_sw3d_current_scanline_y = 0;
/* Pixels in the piece of the span being drawn: to the next lighting block
 * boundary, a whole block, or to the span's end. */
// GLOBAL: XVT 0x6122CC
int g_sw3d_span_length = 0;
/* Viewport column of the first pixel of the piece being drawn. */
// GLOBAL: XVT 0x6122D0
int g_sw3d_span_start_x = 0;
/* Stamp of the current row's lighting block row,
 * g_sw3d_light_sample_cache_scene_stamp_base + (row >> g_sw3d_light_sample_block_shift):
 * a light sample with this stamp is current, and one stamped 1 less is from the
 * block row above. */
// GLOBAL: XVT 0x6122D4
static int g_sw3d_current_light_sample_cache_stamp = 0;
/* Mesh of the face being drawn; sw3d_draw_visible_faces_to_surface writes it and
 * nothing reads it. */
// GLOBAL: XVT 0x612B58
static struct scene_mesh *g_sw3d_span_scene_mesh = NULL;
/* Width of the texture level being drawn, as a float; nothing reads it. */
// GLOBAL: XVT 0x612B5C
static float g_sw3d_span_texture_width_float = 0.0f;
/* Height of the texture level being drawn, as a float; nothing reads it. */
// GLOBAL: XVT 0x612B60
static float g_sw3d_span_texture_height_float = 0.0f;
/* Width shift of the texture level being drawn, from
 * g_sw3d_texture_shift_by_size_div16: log2 of the width for powers of two from 8 to
 * 1024. */
// GLOBAL: XVT 0x612B64
int g_sw3d_span_texture_width_shift = 0;
/* Height shift of the texture level being drawn, found the same way. */
// GLOBAL: XVT 0x612B68
static int g_sw3d_span_texture_height_shift = 0;
/* The face's mesh palette used as shade tables: 16 levels of 256 8-bit pixels,
 * and from byte 4096 16 levels of 256 16-bit pixels. */
// GLOBAL: XVT 0x612B6C
uint8_t *g_sw3d_span_shade_table = 0;
/* Texels of the texture level being drawn: the mesh's texels past the larger
 * levels skipped. */
// GLOBAL: XVT 0x612B70
uint8_t *g_sw3d_span_texels = 0;
/* width * height - 1 of the texture level being drawn; the general path masks
 * texel indexes with it. */
// GLOBAL: XVT 0x612B74
int g_sw3d_span_texel_mask = 0;
/* Shade of the next pixel with 8 fraction bits, kept to 0 to 0xEFF at each
 * block boundary: the level drawn is ((shade + carry) >> 8) & 15. */
// GLOBAL: XVT 0x612B80
int g_sw3d_span_shade_q8 = 0;
/* Change in g_sw3d_span_vq8 per pixel. */
// GLOBAL: XVT 0x612B84
int g_sw3d_span_step_vq8 = 0;
/* Change in g_sw3d_span_uq8 per pixel. */
// GLOBAL: XVT 0x612B88
int g_sw3d_span_step_uq8 = 0;
/* Base of the light sample stamps: render_scene_initialize adds g_flight_vp_height
 * to it each frame, so samples from earlier frames no longer match. */
// GLOBAL: XVT 0x612ADC
int g_sw3d_light_sample_cache_scene_stamp_base = 0;
/* 16.0, g_sw3d_light_sample_block_size as a float. */
// GLOBAL: XVT 0x612B50
float g_sw3d_light_sample_block_size_float = 0.0f;
/* 4, log2 of g_sw3d_light_sample_block_size. */
// GLOBAL: XVT 0x612B7C
int g_sw3d_light_sample_block_shift = 0;
/* Stand-in face for the spans render_scene_initialize puts where the cockpit
 * covers the view: its depth range is 1.0e32 and its w row (0, 0, 1.0e32), so
 * no real face draws over them. */
// GLOBAL: XVT 0x612AE0
struct scene_face g_sw3d_cockpit_mask_sentinel_face = {0};
/* 1 makes sw3d_insert_span leave out odd rows, so the software renderer draws
 * every other row. The Alt+I key flips it (flight_update_player_step in the
 * original build, xvt_flight_sim_update_player_step in the modern one); flight
 * start and end set 0. */
// GLOBAL: XVT 0x523404
int g_sw3d_skip_odd_scanlines = 0;
/* Dither carry each span starts with: 0 on even rows, 128 on odd rows. */
// GLOBAL: XVT 0x527370
static int g_sw3d_shade_dither_initial_by_scanline_parity[2] = {0, 128};

/* Shift for a texture side, indexed by (side & ~12) >> 4: log2 of the side for
 * the powers of two from 8 to 1024. */
// GLOBAL: XVT 0x527378
static const int g_sw3d_texture_shift_by_size_div16[65] = {
	3, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8,	8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,	9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 10,
};

/* 1.0, the numerator of the 1 / w divisions. */
// GLOBAL: XVT 0x527480
static const float g_sw3d_span_one_float = 1.0f;
/* 15.0: light times this gives the shade level. */
// GLOBAL: XVT 0x527484
static const float g_sw3d_light_intensity_to_shade_scale = 15.0f;
/* 12582912.0, 1.5 * 2^23: added to a float, the sum's bits less these bits give
 * the float rounded to an integer. */
// GLOBAL: XVT 0x527488
static const float g_sw3d_float_to_int_round_bias = 12582912.0f;
/* By shift 0 to 11, 1.5 * 2^(15 - shift): added to a value, the sum's bits less
 * these bits give the value times 2^(8 + shift), which turns a texture
 * coordinate of 0 to 1 into texels of a side of 2^shift with 8 fraction bits,
 * and with shift 0 a shade into 8 fraction bits. */
// GLOBAL: XVT 0x5274A8
static const float g_sw3d_tex_coord_bias_by_shift[12] = {
	49152.0f, 24576.0f, 12288.0f, 6144.0f, 3072.0f, 1536.0f,
	768.0f,	  384.0f,   192.0f,   96.0f,   48.0f,	24.0f,
};

/* Near-plane vertex sw3d_setup_clipped_edge made for the edge it set up last,
 * NULL when it made none; sw3d_rasterize_mesh_faces clears it before each edge
 * and stores it in the edge's p_clip_vert. */
// GLOBAL: XVT 0x60F1C4
struct proj_vertex *g_sw3d_generated_clip_vertex = NULL;
/* Newest near-plane vertex of the face being clipped, made by
 * sw3d_setup_clipped_edge or taken from a shared edge's p_clip_vert;
 * sw3d_rasterize_mesh_faces clears it for each clipped face. */
// GLOBAL: XVT 0x60F1D0
struct proj_vertex *g_sw3d_latest_clip_vertex = NULL;
/* The near-plane vertex before g_sw3d_latest_clip_vertex; when both are set,
 * sw3d_rasterize_mesh_faces closes the face with an edge between them. */
// GLOBAL: XVT 0x60F1E0
struct proj_vertex *g_sw3d_previous_clip_vertex = NULL;

/* Projects a mesh's visible faces for the software renderer. For each face in
 * g_vis_face_list from the mesh's face_base_index it sets the face's texture rows
 * with render_scene_transform_face_texture_gradients, then projects each corner's
 * vertex the first time a face uses it, into g_proj_vert_list from
 * g_proj_vert_count, its slot kept in g_vertex_remap, lit with
 * render_scene_compute_vertex_lighting. A vertex is turned by view_orient and
 * moved by viewPos; nearer than g_sw3d_unit_float it keeps its view x and y with
 * scaled_inverse_depth z - g_sw3d_unit_float, a negative marker, and marks the
 * face's near_clip_state -1; otherwise scaled_inverse_depth is g_proj_scale_int / z
 * and sx and sy that times x and y plus the viewport middle, plus
 * g_proj_offset_y for y. Each face's min_scaled_inverse_depth and
 * max_scaled_inverse_depth cover its corners, a near corner counting as
 * g_proj_scale_int. For a textured mesh it turns the face's rows into the
 * screen-space rows of u, v and their divisor w (the inverse of the texture
 * frame, scaled by g_inv_proj_scale and moved to the viewport middle) and sets
 * texels_per_pixel_q8 to t * a * s^2, with t = width * height << 8,
 * a = |gradients[0] * gradients[4] - gradients[3] * gradients[1]| and
 * s = g_proj_scale_int * n / the sum of the n corners' scaled_inverse_depth. Adds
 * the vertices made to g_proj_vert_count. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x470300
void sw3d_project_mesh_vertices(struct scene_mesh *mesh)
{
	struct scene_face *face = &g_vis_face_list[mesh->face_base_index];
	struct proj_vertex *output;
	int vertex_index;
	int face_index;

	mesh->vert_base_index = g_proj_vert_count;
	output = &g_proj_vert_list[mesh->vert_base_index];
	mesh->proj_vert_cursor = 0;
	for (vertex_index = 0; vertex_index < mesh->vertex_count;
	     ++vertex_index) {
		g_vertex_remap[vertex_index] = -1;
	}
	for (face_index = 0; face_index < mesh->vis_face_count;
	     ++face_index, ++face) {
		struct opt_vector transformed;
		const struct face_record *geometry;
		float total_w;
		float c00;
		float c01;
		float c02;
		float c10;
		float c11;
		float c12;
		float c20;
		float c21;
		float c22;
		float inverse;
		float scaled;
		float area;
		int corner_index;

		render_scene_transform_face_texture_gradients(
			face, &mesh->p_face_texturing[face->face_index],
			&mesh->view_pos_x);
		geometry = &mesh->p_face_geom[face->face_index];
		face->max_scaled_inverse_depth = 0.0f;
		total_w = 0.0f;
		face->min_scaled_inverse_depth =
			(float)(unsigned int)g_proj_scale_int;
		for (corner_index = 0; corner_index < 4; ++corner_index) {
			const int model_vertex_index =
				geometry->vertex_idx[corner_index];
			const int normal_index =
				geometry->normal_idx[corner_index];
			int remapped_vertex;
			float vertex_w;

			if (model_vertex_index == -1) {
				break;
			}
			remapped_vertex = g_vertex_remap[model_vertex_index];
			if (remapped_vertex == -1) {
				g_vertex_remap[model_vertex_index] =
					mesh->proj_vert_cursor;
				++mesh->proj_vert_cursor;
				transformed.x =
					mesh->p_model_verts[model_vertex_index]
						.x;
				transformed.y =
					mesh->p_model_verts[model_vertex_index]
						.y;
				transformed.z =
					mesh->p_model_verts[model_vertex_index]
						.z;
				math3d_rotate_vec3(&transformed.x,
						   mesh->view_orient);
				transformed.x += mesh->view_pos_x;
				transformed.y += mesh->view_pos_y;
				transformed.z += mesh->view_pos_z;
				if (transformed.z < g_sw3d_unit_float) {
					output->scaled_inverse_depth =
						transformed.z -
						g_sw3d_unit_float;
					output->sx = transformed.x;
					output->sy = transformed.y;
					face->near_clip_state = -1;
					vertex_w = (float)(unsigned int)
						g_proj_scale_int;
				} else {
					output->scaled_inverse_depth =
						(float)(unsigned int)
							g_proj_scale_int /
						transformed.z;
					output->sx =
						output->scaled_inverse_depth *
						transformed.x;
					output->sy =
						output->scaled_inverse_depth *
						transformed.y;
					output->sx +=
						(float)(g_flight_vp_width >> 1);
					output->sy +=
						(float)(g_proj_offset_y +
							(g_flight_vp_height >>
							 1));
					vertex_w = output->scaled_inverse_depth;
				}
				render_scene_compute_vertex_lighting(
					mesh, output,
					&mesh->p_vert_normals[normal_index],
					&mesh->p_model_verts
						 [model_vertex_index],
					&g_mesh_eye_pos);
				++output;
			} else {
				const struct proj_vertex *projected =
					&g_proj_vert_list
						[mesh->vert_base_index +
						 remapped_vertex];
				if (projected->scaled_inverse_depth < 0.0f) {
					face->near_clip_state = -1;
					vertex_w = (float)(unsigned int)
						g_proj_scale_int;
				} else {
					vertex_w =
						projected->scaled_inverse_depth;
				}
			}
			total_w += vertex_w;
			if (face->max_scaled_inverse_depth < vertex_w) {
				face->max_scaled_inverse_depth = vertex_w;
			}
			if (face->min_scaled_inverse_depth > vertex_w) {
				face->min_scaled_inverse_depth = vertex_w;
			}
		}

		if (mesh->p_uvs != NULL) {
			const int uv_index = geometry->uv_idx[0];
			const int model_vertex_index = geometry->vertex_idx[0];

			transformed.x =
				mesh->p_model_verts[model_vertex_index].x;
			transformed.y =
				mesh->p_model_verts[model_vertex_index].y;
			transformed.z =
				mesh->p_model_verts[model_vertex_index].z;
			math3d_rotate_vec3(&transformed.x, mesh->view_orient);
			transformed.x += mesh->view_pos_x;
			transformed.y += mesh->view_pos_y;
			transformed.z += mesh->view_pos_z;
			face->gradients[6] =
				transformed.x -
				mesh->p_uvs[uv_index].v * face->gradients[3] -
				mesh->p_uvs[uv_index].u * face->gradients[0];
			face->gradients[7] =
				transformed.y -
				mesh->p_uvs[uv_index].v * face->gradients[4] -
				mesh->p_uvs[uv_index].u * face->gradients[1];
			face->gradients[8] =
				transformed.z -
				mesh->p_uvs[uv_index].v * face->gradients[5] -
				mesh->p_uvs[uv_index].u * face->gradients[2];
			c00 = face->gradients[8] * face->gradients[4] -
			      face->gradients[5] * face->gradients[7];
			c01 = face->gradients[5] * face->gradients[6] -
			      face->gradients[8] * face->gradients[3];
			c02 = face->gradients[7] * face->gradients[3] -
			      face->gradients[4] * face->gradients[6];
			c10 = face->gradients[2] * face->gradients[7] -
			      face->gradients[8] * face->gradients[1];
			c11 = face->gradients[8] * face->gradients[0] -
			      face->gradients[2] * face->gradients[6];
			c12 = face->gradients[6] * face->gradients[1] -
			      face->gradients[7] * face->gradients[0];
			c20 = face->gradients[5] * face->gradients[1] -
			      face->gradients[2] * face->gradients[4];
			c21 = face->gradients[2] * face->gradients[3] -
			      face->gradients[5] * face->gradients[0];
			c22 = face->gradients[4] * face->gradients[0] -
			      face->gradients[1] * face->gradients[3];
			if (c20 == 0.0f && c21 == 0.0f && c22 == 0.0f) {
				c22 = 1.0f;
			}
			inverse = g_sw3d_unit_float /
				  (c21 * face->gradients[7] +
				   (c20 * face->gradients[6] +
				    c22 * face->gradients[8]));
			scaled = inverse * g_inv_proj_scale;
			face->gradients[0] = scaled * c00;
			face->gradients[1] = scaled * c01;
			face->gradients[2] = inverse * c02;
			face->gradients[3] = scaled * c10;
			face->gradients[4] = scaled * c11;
			face->gradients[5] = inverse * c12;
			face->gradients[6] = scaled * c20;
			face->gradients[7] = scaled * c21;
			face->gradients[8] = inverse * c22;
			face->gradients[2] -= (float)(g_flight_vp_width >> 1) *
					      face->gradients[0];
			face->gradients[2] -=
				(float)(g_proj_offset_y +
					(g_flight_vp_height >> 1)) *
				face->gradients[1];
			face->gradients[5] -= (float)(g_flight_vp_width >> 1) *
					      face->gradients[3];
			face->gradients[5] -=
				(float)(g_proj_offset_y +
					(g_flight_vp_height >> 1)) *
				face->gradients[4];
			face->gradients[8] -= (float)(g_flight_vp_width >> 1) *
					      face->gradients[6];
			face->gradients[8] -=
				(float)(g_proj_offset_y +
					(g_flight_vp_height >> 1)) *
				face->gradients[7];
			area = face->gradients[0] * face->gradients[4] -
			       face->gradients[3] * face->gradients[1];
			if (area < g_sw3d_zero_float) {
				area = -area;
			}
			{
				const struct opt_texture_data *material =
					(const struct opt_texture_data *)
						mesh->p_material;
				float lod_scale;

				/* total_w, the sum of the corners' w values, becomes the corner count over that sum: the
				 * reciprocal of their mean. */
				if (geometry->vertex_idx[3] == -1) {
					total_w = g_sw3d_triangle_corner_count /
						  total_w;
				} else {
					total_w = g_sw3d_quad_corner_count /
						  total_w;
				}
				lod_scale =
					(float)(unsigned int)g_proj_scale_int *
					total_w;
				face->texels_per_pixel_q8 =
					(int)((float)((material->width *
						       material->height)
						      << 8) *
					      (area * (lod_scale * lod_scale)));
			}
		}
	}
	g_proj_vert_count += mesh->proj_vert_cursor;
}

/* Projects a distant mesh's visible faces the way
 * sw3d_project_mesh_vertices does, but every vertex is pushed
 * g_sw3d_distant_depth further away and projected with
 * g_proj_scale_int / view_pos_z * g_sw3d_distant_depth as its
 * scale, with no near test, and texels_per_pixel_q8 is
 * (width * height << 8) * |gradients[4] * gradients[0]| * z^2
 * plus the same with gradients[1] * gradients[3], each cut to
 * an integer, z being the first corner's pushed depth. */
// FUNCTION: XVT 0x4709C0
void sw3d_project_mesh_vertices_distant(struct scene_mesh *mesh)
{
	const float projection_scale = (float)(unsigned int)g_proj_scale_int /
				       mesh->view_pos_z * g_sw3d_distant_depth;
	struct scene_face *face = &g_vis_face_list[mesh->face_base_index];
	struct proj_vertex *output;
	int vertex_base_index;
	int vertex_index;
	int face_index;

	vertex_base_index = g_proj_vert_count;
	mesh->vert_base_index = vertex_base_index;
	output = &g_proj_vert_list[vertex_base_index];
	mesh->proj_vert_cursor = 0;
	for (vertex_index = 0; vertex_index < mesh->vertex_count;
	     ++vertex_index) {
		g_vertex_remap[vertex_index] = -1;
	}
	for (face_index = 0; face_index < mesh->vis_face_count;
	     ++face_index, ++face) {
		const struct face_record *geometry;
		int corner_index;

		render_scene_transform_face_texture_gradients(
			face, &mesh->p_face_texturing[face->face_index],
			&mesh->view_pos_x);
		geometry = &mesh->p_face_geom[face->face_index];
		face->max_scaled_inverse_depth = 0.0f;
		face->min_scaled_inverse_depth =
			(float)(unsigned int)g_proj_scale_int;
		for (corner_index = 0; corner_index < 4; ++corner_index) {
			struct opt_vector transformed;
			const int model_vertex_index =
				geometry->vertex_idx[corner_index];
			const int normal_index =
				geometry->normal_idx[corner_index];
			int remapped_vertex;
			float vertex_w;

			if (model_vertex_index == -1) {
				break;
			}
			remapped_vertex = g_vertex_remap[model_vertex_index];
			if (remapped_vertex == -1) {
				g_vertex_remap[model_vertex_index] =
					mesh->proj_vert_cursor;
				++mesh->proj_vert_cursor;
				transformed.x =
					mesh->p_model_verts[model_vertex_index]
						.x;
				transformed.y =
					mesh->p_model_verts[model_vertex_index]
						.y;
				transformed.z =
					mesh->p_model_verts[model_vertex_index]
						.z;
				math3d_rotate_vec3(&transformed.x,
						   mesh->view_orient);
				transformed.x += mesh->view_pos_x;
				transformed.y += mesh->view_pos_y;
				transformed.z += mesh->view_pos_z;
				transformed.z += g_sw3d_distant_depth;
				output->scaled_inverse_depth =
					projection_scale / transformed.z;
				output->sx = output->scaled_inverse_depth *
					     transformed.x;
				output->sy = output->scaled_inverse_depth *
					     transformed.y;
				output->sx += (float)(g_flight_vp_width >> 1);
				output->sy +=
					(float)(g_proj_offset_y +
						(g_flight_vp_height >> 1));
				vertex_w = output->scaled_inverse_depth;
				render_scene_compute_vertex_lighting(
					mesh, output,
					&mesh->p_vert_normals[normal_index],
					&mesh->p_model_verts
						 [model_vertex_index],
					&g_mesh_eye_pos);
				++output;
			} else {
				vertex_w =
					g_proj_vert_list[mesh->vert_base_index +
							 remapped_vertex]
						.scaled_inverse_depth;
			}
			if (face->max_scaled_inverse_depth < vertex_w) {
				face->max_scaled_inverse_depth = vertex_w;
			}
			if (face->min_scaled_inverse_depth > vertex_w) {
				face->min_scaled_inverse_depth = vertex_w;
			}
		}

		if (mesh->p_uvs != NULL) {
			struct opt_vector transformed;
			const int uv_index = geometry->uv_idx[0];
			const struct opt_vector *model_vertex =
				&mesh->p_model_verts[geometry->vertex_idx[0]];
			float c00;
			float c01;
			float c02;
			float c10;
			float c11;
			float c12;
			float c20;
			float c21;
			float c22;
			float inverse;
			float scaled;

			transformed.x = model_vertex->x;
			transformed.y = model_vertex->y;
			transformed.z = model_vertex->z;
			math3d_rotate_vec3(&transformed.x, mesh->view_orient);
			transformed.x += mesh->view_pos_x;
			transformed.y += mesh->view_pos_y;
			transformed.z += mesh->view_pos_z;
			transformed.z += g_sw3d_distant_depth;
			face->gradients[6] =
				transformed.x -
				mesh->p_uvs[uv_index].v * face->gradients[3] -
				mesh->p_uvs[uv_index].u * face->gradients[0];
			face->gradients[7] =
				transformed.y -
				mesh->p_uvs[uv_index].v * face->gradients[4] -
				mesh->p_uvs[uv_index].u * face->gradients[1];
			face->gradients[8] =
				transformed.z -
				mesh->p_uvs[uv_index].v * face->gradients[5] -
				mesh->p_uvs[uv_index].u * face->gradients[2];
			c00 = face->gradients[8] * face->gradients[4] -
			      face->gradients[5] * face->gradients[7];
			c01 = face->gradients[5] * face->gradients[6] -
			      face->gradients[8] * face->gradients[3];
			c02 = face->gradients[7] * face->gradients[3] -
			      face->gradients[4] * face->gradients[6];
			c10 = face->gradients[2] * face->gradients[7] -
			      face->gradients[8] * face->gradients[1];
			c11 = face->gradients[8] * face->gradients[0] -
			      face->gradients[2] * face->gradients[6];
			c12 = face->gradients[1] * face->gradients[6] -
			      face->gradients[7] * face->gradients[0];
			c20 = face->gradients[5] * face->gradients[1] -
			      face->gradients[2] * face->gradients[4];
			c21 = face->gradients[2] * face->gradients[3] -
			      face->gradients[5] * face->gradients[0];
			c22 = face->gradients[4] * face->gradients[0] -
			      face->gradients[1] * face->gradients[3];
			if (c20 == 0.0f && c21 == 0.0f && c22 == 0.0f) {
				c22 = 1.0f;
			}
			inverse =
				g_sw3d_unit_float / (c20 * face->gradients[6] +
						     c21 * face->gradients[7] +
						     c22 * face->gradients[8]);
			scaled = inverse / projection_scale;
			face->gradients[0] = scaled * c00;
			face->gradients[1] = scaled * c01;
			face->gradients[2] = inverse * c02;
			face->gradients[3] = scaled * c10;
			face->gradients[4] = scaled * c11;
			face->gradients[5] = inverse * c12;
			face->gradients[6] = scaled * c20;
			face->gradients[7] = scaled * c21;
			face->gradients[8] = inverse * c22;
			face->gradients[2] -= (float)(g_flight_vp_width >> 1) *
					      face->gradients[0];
			face->gradients[2] -=
				(float)(g_proj_offset_y +
					(g_flight_vp_height >> 1)) *
				face->gradients[1];
			face->gradients[5] -= (float)(g_flight_vp_width >> 1) *
					      face->gradients[3];
			face->gradients[5] -=
				(float)(g_proj_offset_y +
					(g_flight_vp_height >> 1)) *
				face->gradients[4];
			face->gradients[8] -= (float)(g_flight_vp_width >> 1) *
					      face->gradients[6];
			face->gradients[8] -=
				(float)(g_proj_offset_y +
					(g_flight_vp_height >> 1)) *
				face->gradients[7];
			{
				const struct opt_texture_data *material =
					(const struct opt_texture_data *)
						mesh->p_material;
				float mip_value;
				mip_value = face->gradients[4] *
					    face->gradients[0] * transformed.z *
					    transformed.z;
				if (mip_value < g_sw3d_zero_float) {
					mip_value = -mip_value;
				}
				face->texels_per_pixel_q8 =
					(int)((float)((material->width *
						       material->height)
						      << 8) *
					      mip_value);
				mip_value = face->gradients[1] *
					    face->gradients[3] * transformed.z *
					    transformed.z;
				if (mip_value < g_sw3d_zero_float) {
					mip_value = -mip_value;
				}
				face->texels_per_pixel_q8 +=
					(int)((float)((material->width *
						       material->height)
						      << 8) *
					      mip_value);
			}
		}
	}
	g_proj_vert_count += mesh->proj_vert_cursor;
}

/* Builds the screen edges of a mesh's visible faces in g_scene_edge_list from
 * g_scene_edge_cursor and scan-converts each face. A model edge is set up once
 * and shared: g_scene_edge_flags maps it to the edge made for it, -1 before and
 * -2 when rejected. A face has 4 corners, or 3 when its last edge index is -1.
 * A face marked for near clipping (near_clip_state -1) goes through
 * sw3d_setup_clipped_edge and gets a closing edge between its last two near-plane
 * vertices; any other face goes through sw3d_setup_edge. Sets each face's
 * near_clip_state to g_flight_vp_height, its edges and edge_count, and calls
 * sw3d_scan_convert_face, or sets yTop and y_bot to 0 for a face left with no
 * edge. Records the mesh's edge_base_index and emitted_edge_count and advances
 * g_scene_edge_cursor by the edges made. */
// FUNCTION: XVT 0x471020
void sw3d_rasterize_mesh_faces(struct scene_mesh *mesh)
{
	enum {
		SW3D_INVALID_EDGE = -1,
		SW3D_REJECTED_EDGE = -2,
		SW3D_FACE_NEEDS_NEAR_CLIP = -1,
	};

	struct proj_vertex *vertices = &g_proj_vert_list[mesh->vert_base_index];
	struct scene_face *face_cursor =
		&g_vis_face_list[mesh->face_base_index];
	int scene_edge_cursor = g_scene_edge_cursor;
	struct scene_face *face;
	struct scene_edge *output_edge;
	struct scene_edge *first_edge;
	int edge_index;
	const int edge_count = mesh->edge_count;
	int face_index;
	int output_count;

	mesh->edge_base_index = scene_edge_cursor;
	mesh->emitted_edge_count = 0;
	output_edge = &g_scene_edge_list[scene_edge_cursor];
	first_edge = output_edge;
	if (edge_count > 0) {
		for (edge_index = 0; edge_index < mesh->edge_count;
		     ++edge_index) {
			g_scene_edge_flags[edge_index] = SW3D_INVALID_EDGE;
		}
	}

	for (face_index = 0; face_index < mesh->vis_face_count; ++face_index) {
		const struct face_record *record;
		int corner_count;
		int current_corner;
		int previous_corner;

		output_count = 0;
		record = &mesh->p_face_geom[face_cursor->face_index];
		face = face_cursor;
		++face_cursor;

		if (face->near_clip_state == SW3D_FACE_NEEDS_NEAR_CLIP) {
			face->near_clip_state = g_flight_vp_height;
			g_sw3d_latest_clip_vertex = NULL;
			g_sw3d_previous_clip_vertex = NULL;
			corner_count =
				record->edge_idx[(sizeof(record->edge_idx) /
						  sizeof(record->edge_idx[0])) -
						 1] != SW3D_INVALID_EDGE
					? (int)(sizeof(record->edge_idx) /
						sizeof(record->edge_idx[0]))
					: (int)(sizeof(record->edge_idx) /
						sizeof(record->edge_idx[0])) -
						  1;
			current_corner = 0;
			for (previous_corner = corner_count;;) {
				int source_edge;
				int existing_edge;

				--previous_corner;
				source_edge = record->edge_idx[previous_corner];
				existing_edge = g_scene_edge_flags[source_edge];
				g_sw3d_generated_clip_vertex = NULL;
				if (existing_edge == SW3D_INVALID_EDGE) {
					if (sw3d_setup_clipped_edge(
						    mesh, output_edge,
						    &vertices
							    [g_vertex_remap
								     [record->vertex_idx
									      [previous_corner]]],
						    &vertices
							    [g_vertex_remap
								     [record->vertex_idx
									      [current_corner]]]) >=
					    0) {
						face->edges[output_count++] =
							output_edge;
						output_edge->p_clip_vert =
							g_sw3d_generated_clip_vertex;
						g_scene_edge_flags[source_edge] =
							mesh->emitted_edge_count;
						++mesh->emitted_edge_count;
						++output_edge;
					} else if (
						g_sw3d_generated_clip_vertex ==
						NULL) {
						g_scene_edge_flags[source_edge] =
							SW3D_REJECTED_EDGE;
					}
				} else if (existing_edge !=
					   SW3D_REJECTED_EDGE) {
					struct scene_edge *edge;

					edge = &first_edge[existing_edge];
					face->edges[output_count++] = edge;
					if (edge->p_clip_vert != NULL) {
						g_sw3d_previous_clip_vertex =
							g_sw3d_latest_clip_vertex;
						g_sw3d_latest_clip_vertex =
							edge->p_clip_vert;
					}
				}
				current_corner = previous_corner;
				if (previous_corner <= 0) {
					break;
				}
			}

			if (g_sw3d_previous_clip_vertex != NULL) {
				if (sw3d_setup_clipped_edge(
					    mesh, output_edge,
					    g_sw3d_latest_clip_vertex,
					    g_sw3d_previous_clip_vertex) >= 0) {
					face->edges[output_count++] =
						output_edge;
					++mesh->emitted_edge_count;
					++output_edge;
				}
			}
		} else {
			face->near_clip_state = g_flight_vp_height;
			corner_count =
				record->edge_idx[(sizeof(record->edge_idx) /
						  sizeof(record->edge_idx[0])) -
						 1] != SW3D_INVALID_EDGE
					? (int)(sizeof(record->edge_idx) /
						sizeof(record->edge_idx[0]))
					: (int)(sizeof(record->edge_idx) /
						sizeof(record->edge_idx[0])) -
						  1;
			current_corner = 0;
			for (previous_corner = corner_count;;) {
				int source_edge;
				int existing_edge;

				--previous_corner;
				source_edge = record->edge_idx[previous_corner];
				existing_edge = g_scene_edge_flags[source_edge];
				if (existing_edge == SW3D_INVALID_EDGE) {
					if (sw3d_setup_edge(
						    output_edge,
						    &vertices
							    [g_vertex_remap
								     [record->vertex_idx
									      [previous_corner]]],
						    &vertices
							    [g_vertex_remap
								     [record->vertex_idx
									      [current_corner]]]) >=
					    0) {
						face->edges[output_count++] =
							output_edge;
						g_scene_edge_flags[source_edge] =
							mesh->emitted_edge_count;
						++mesh->emitted_edge_count;
						++output_edge;
					} else {
						g_scene_edge_flags[source_edge] =
							SW3D_REJECTED_EDGE;
					}
				} else if (existing_edge !=
					   SW3D_REJECTED_EDGE) {
					face->edges[output_count++] =
						&first_edge[existing_edge];
				}
				current_corner = previous_corner;
				if (previous_corner <= 0) {
					break;
				}
			}
		}

		face->edge_count = output_count;
		if (output_count == 0) {
			face->y_bot = 0;
			face->y_top = 0;
		} else {
			sw3d_scan_convert_face(face);
		}
	}
	g_scene_edge_cursor += mesh->emitted_edge_count;
}

/* Turns a face's edges into spans, row by row, with sw3d_insert_span. Sets yTop
 * to the smallest yStart and y_bot to the largest yEnd, and takes y_bot - yTop
 * span pointers from the end of g_scene_span_ptr_list for p_spans; when that many
 * are not left it sets y_bot to yTop and adds nothing. It walks a left and a
 * right edge down from the top, moving to the edge that starts where one ends,
 * and for each row inserts the span between them with span_light_intensity_dx, the
 * light change per pixel across it; a face with no second edge starting at its
 * top row, or with no edge starting where one ends, stops there with y_bot at
 * that row. p_scan_edge points at the left edge. The edges' x and light are put
 * back at the end. */
// FUNCTION: XVT 0x471410
void sw3d_scan_convert_face(struct scene_face *face)
{
	struct scene_edge *left;
	struct scene_edge *right;
	struct scene_edge *edge;
	struct scene_edge *swap_edge;
	int edge_index;
	int edge_count;
	int remaining_edges;
	int scan_y;
	int run_end;
	int run_rows;
	int span_count;
	float run_rows_float;
	float left_start_x;
	float right_start_x;
	float left_start_light;
	float right_start_light;

	edge_count = face->edge_count;
	remaining_edges = edge_count;
	left = face->edges[0];
	right = left;
	for (edge_index = 1; edge_index < edge_count; ++edge_index) {
		edge = face->edges[edge_index];
		if (left->y_start > edge->y_start) {
			left = edge;
		}
		if (right->y_end < edge->y_end) {
			right = edge;
		}
	}

	face->y_top = left->y_start;
	face->y_bot = right->y_end;
	span_count = face->y_bot - face->y_top;
	if (span_count < g_scene_span_ptr_avail) {
		g_scene_span_ptr_avail -= span_count;
		face->p_spans = &g_scene_span_ptr_list[g_scene_span_ptr_avail];

		for (edge_index = 0; edge_index < face->edge_count;
		     ++edge_index) {
			edge = face->edges[edge_index];
			if (left != edge && edge->y_start == left->y_start) {
				right = edge;
				break;
			}
		}
		if (edge_index == face->edge_count) {
			face->y_bot = face->y_top;
		} else {
			if (right->x < left->x ||
			    (right->x == left->x && right->dxdy < left->dxdy)) {
				swap_edge = left;
				left = right;
				right = swap_edge;
			}

			face->p_scan_edge = left;
			scan_y = left->y_start;
			run_end = right->y_end;
			if (right->y_end > left->y_end) {
				run_end = left->y_end;
			}
			run_rows = run_end - scan_y;
			left_start_x = left->x;
			right_start_x = right->x;
			left_start_light = left->light_intensity;
			right_start_light = right->light_intensity;

			if (right_start_x - left_start_x > g_sw3d_unit_float) {
				do {
					--run_rows;
					face->span_light_intensity_dx =
						(right->light_intensity -
						 left->light_intensity) /
						(right->x - left->x);
					sw3d_insert_span(left->x, right->x,
							 scan_y++, face);
					if (run_rows <= 0) {
						break;
					}
					left->x += left->dxdy;
					left->light_intensity +=
						left->d_light_intensity_dy;
					right->x += right->dxdy;
					right->light_intensity +=
						right->d_light_intensity_dy;
				} while (1);
			} else {
				run_rows_float = (float)run_rows;
				face->span_light_intensity_dx =
					(run_rows_float *
						 right->d_light_intensity_dy +
					 right->light_intensity -
					 (run_rows_float *
						  left->d_light_intensity_dy +
					  left->light_intensity)) /
					(run_rows_float * right->dxdy +
					 right->x -
					 (run_rows_float * left->dxdy +
					  left->x));
				do {
					--run_rows;
					sw3d_insert_span(left->x, right->x,
							 scan_y++, face);
					if (run_rows <= 0) {
						break;
					}
					left->x += left->dxdy;
					left->light_intensity +=
						left->d_light_intensity_dy;
					right->x += right->dxdy;
					right->light_intensity +=
						right->d_light_intensity_dy;
				} while (1);
			}

			while (remaining_edges > 0) {
				if (right->y_end != left->y_end) {
					--remaining_edges;
					if (scan_y == left->y_end) {
						left->x = left_start_x;
						left->light_intensity =
							left_start_light;
						for (edge_index = 0;
						     edge_index <
						     face->edge_count;
						     ++edge_index) {
							if (face->edges[edge_index]
								    ->y_start ==
							    scan_y) {
								break;
							}
						}
						if (edge_index ==
						    face->edge_count) {
							face->y_bot = scan_y;
							break;
						}
						left = face->edges[edge_index];
						face->p_scan_edge = left;
						left_start_x = left->x;
						left_start_light =
							left->light_intensity;
						right->x += right->dxdy;
						right->light_intensity +=
							right->d_light_intensity_dy;
					} else {
						right->x = right_start_x;
						right->light_intensity =
							right_start_light;
						for (edge_index = 0;
						     edge_index <
						     face->edge_count;
						     ++edge_index) {
							if (face->edges[edge_index]
								    ->y_start ==
							    scan_y) {
								break;
							}
						}
						if (edge_index ==
						    face->edge_count) {
							face->y_bot = scan_y;
							break;
						}
						right = face->edges[edge_index];
						right_start_x = right->x;
						right_start_light =
							right->light_intensity;
						left->x += left->dxdy;
						left->light_intensity +=
							left->d_light_intensity_dy;
					}
				} else {
					remaining_edges -= 2;
					if (remaining_edges == 0) {
						break;
					}

					left->x = left_start_x;
					right->x = right_start_x;
					left->light_intensity =
						left_start_light;
					right->light_intensity =
						right_start_light;
					for (edge_index = 0;
					     edge_index < face->edge_count;
					     ++edge_index) {
						if (face->edges[edge_index]
							    ->y_start ==
						    scan_y) {
							break;
						}
					}
					if (edge_index == face->edge_count) {
						face->y_bot = scan_y;
						break;
					}
					left = face->edges[edge_index];
					for (++edge_index;
					     edge_index < face->edge_count;
					     ++edge_index) {
						if (face->edges[edge_index]
							    ->y_start ==
						    scan_y) {
							break;
						}
					}
					if (edge_index == face->edge_count) {
						face->y_bot = scan_y;
						break;
					}
					right = face->edges[edge_index];
					if (right->x < left->x ||
					    (right->x == left->x &&
					     right->dxdy < left->dxdy)) {
						swap_edge = left;
						left = right;
						right = swap_edge;
					}
					face->p_scan_edge = left;
					left_start_x = left->x;
					right_start_x = right->x;
					left_start_light =
						left->light_intensity;
					right_start_light =
						right->light_intensity;
				}

				if (remaining_edges == 1) {
					return;
				}
				if (remaining_edges == 2) {
					if (right->y_end != left->y_end) {
						return;
					}
					run_rows = left->y_end - scan_y;
					run_rows_float = (float)run_rows;
					if (right->dxdy * run_rows_float +
						    right->x -
						    (left->dxdy *
							     run_rows_float +
						     left->x) >
					    g_sw3d_unit_float) {
						do {
							--run_rows;
							face->span_light_intensity_dx =
								(right->light_intensity -
								 left->light_intensity) /
								(right->x -
								 left->x);
							sw3d_insert_span(
								left->x,
								right->x,
								scan_y++, face);
							if (run_rows <= 0) {
								break;
							}
							left->x += left->dxdy;
							left->light_intensity +=
								left->d_light_intensity_dy;
							right->x += right->dxdy;
							right->light_intensity +=
								right->d_light_intensity_dy;
						} while (1);
					} else {
						face->span_light_intensity_dx =
							(right->light_intensity -
							 left->light_intensity) /
							(right->x - left->x);
						do {
							--run_rows;
							sw3d_insert_span(
								left->x,
								right->x,
								scan_y++, face);
							if (run_rows <= 0) {
								break;
							}
							left->x += left->dxdy;
							left->light_intensity +=
								left->d_light_intensity_dy;
							right->x += right->dxdy;
							right->light_intensity +=
								right->d_light_intensity_dy;
						} while (1);
					}
				} else {
					run_end = left->y_end < right->y_end
							  ? left->y_end
							  : right->y_end;
					run_rows = run_end - scan_y;
					do {
						--run_rows;
						face->span_light_intensity_dx =
							(right->light_intensity -
							 left->light_intensity) /
							(right->x - left->x);
						sw3d_insert_span(
							left->x, right->x,
							scan_y++, face);
						if (run_rows <= 0) {
							break;
						}
						left->x += left->dxdy;
						left->light_intensity +=
							left->d_light_intensity_dy;
						right->x += right->dxdy;
						right->light_intensity +=
							right->d_light_intensity_dy;
					} while (1);
				}
			}

			left->x = left_start_x;
			right->x = right_start_x;
			left->light_intensity = left_start_light;
			right->light_intensity = right_start_light;
		}
	} else {
		face->y_bot = face->y_top;
	}
}

/* sw3d_setup_edge for an edge whose ends may lie in front of the near plane,
 * marked by a negative scaled_inverse_depth. Returns -1 when both ends do. When
 * one does, it makes a vertex where the edge crosses the near plane, taken from
 * the mesh's projected vertices (its proj_vert_cursor and g_proj_vert_count grow by
 * 1), with scaled_inverse_depth g_proj_scale_int and its light interpolated, makes
 * it g_sw3d_latest_clip_vertex and g_sw3d_generated_clip_vertex, the old latest
 * becoming g_sw3d_previous_clip_vertex, and sets the edge up from it to the other
 * end. The rest is as sw3d_setup_edge, a negative sy counting as row 0. */
// FUNCTION: XVT 0x471A10
int sw3d_setup_clipped_edge(struct scene_mesh *mesh, struct scene_edge *edge,
			    const struct proj_vertex *first,
			    const struct proj_vertex *second)
{
	const struct proj_vertex *inside;
	const struct proj_vertex *outside;
#ifdef XVT_MODERN
	uint32_t coordinate_bits;
#endif
	int second_y;
	int first_y;
	const struct proj_vertex *swap_vertex;
	int swap_y;
	float inverse_height;
	float first_row_offset;

	inside = second;
	outside = first;
#ifdef XVT_MODERN
	memcpy(&coordinate_bits, &second->scaled_inverse_depth,
	       sizeof(coordinate_bits));
	if (coordinate_bits > 0x80000000u) {
#else
	if (*(const uint32_t *)&second->scaled_inverse_depth > 0x80000000u) {
#endif
#ifdef XVT_MODERN
		memcpy(&coordinate_bits, &first->scaled_inverse_depth,
		       sizeof(coordinate_bits));
		if (coordinate_bits > 0x80000000u) {
#else
		if (*(const uint32_t *)&first->scaled_inverse_depth >
		    0x80000000u) {
#endif
			return -1;
		}
		outside = second;
		inside = first;
	}

#ifdef XVT_MODERN
	memcpy(&coordinate_bits, &outside->scaled_inverse_depth,
	       sizeof(coordinate_bits));
	if (coordinate_bits > 0x80000000u) {
#else
	if (*(const uint32_t *)&outside->scaled_inverse_depth > 0x80000000u) {
#endif
		float inside_inverse_w;
		float inside_x;
		float inside_y;
		float clip_fraction;
		float clipped_y;

		g_sw3d_previous_clip_vertex = g_sw3d_latest_clip_vertex;
		g_sw3d_latest_clip_vertex =
			&g_proj_vert_list[mesh->proj_vert_cursor +
					  mesh->vert_base_index];
		g_sw3d_generated_clip_vertex = g_sw3d_latest_clip_vertex;
		++mesh->proj_vert_cursor;
		++g_proj_vert_count;

		inside_inverse_w =
			g_sw3d_unit_float / inside->scaled_inverse_depth;
		inside_x = (inside->sx - (float)(g_flight_vp_width >> 1)) *
			   inside_inverse_w;
		inside_y = (inside->sy - (float)(g_proj_offset_y +
						 (g_flight_vp_height >> 1))) *
			   inside_inverse_w;
		clip_fraction = outside->scaled_inverse_depth /
				(outside->scaled_inverse_depth -
				 inside_inverse_w *
					 (float)(unsigned int)g_proj_scale_int +
				 g_sw3d_unit_float);
		clipped_y =
			(inside_y - outside->sy) * clip_fraction + outside->sy;
		g_sw3d_latest_clip_vertex->sx =
			((inside_x - outside->sx) * clip_fraction +
			 outside->sx) *
			(float)(unsigned int)g_proj_scale_int;
		g_sw3d_latest_clip_vertex->sy =
			clipped_y * (float)(unsigned int)g_proj_scale_int;
		g_sw3d_latest_clip_vertex->sx =
			(float)(g_flight_vp_width >> 1) +
			g_sw3d_latest_clip_vertex->sx;
		g_sw3d_latest_clip_vertex->sy =
			(float)(g_proj_offset_y + (g_flight_vp_height >> 1)) +
			g_sw3d_latest_clip_vertex->sy;
		g_sw3d_latest_clip_vertex->scaled_inverse_depth =
			(float)(unsigned int)g_proj_scale_int;
		g_sw3d_latest_clip_vertex->light_intensity =
			outside->light_intensity +
			(inside->light_intensity - outside->light_intensity) *
				clip_fraction;
		outside = g_sw3d_latest_clip_vertex;
	}

#ifdef XVT_MODERN
	memcpy(&coordinate_bits, &outside->sy, sizeof(coordinate_bits));
	if (coordinate_bits > 0x80000000u) {
#else
	if (*(const uint32_t *)&outside->sy > 0x80000000u) {
#endif
		first_y = 0;
	} else {
		first_y = (int)outside->sy;
		if ((float)first_y != outside->sy) {
			++first_y;
		}
	}
#ifdef XVT_MODERN
	memcpy(&coordinate_bits, &inside->sy, sizeof(coordinate_bits));
	if (coordinate_bits > 0x80000000u) {
#else
	if (*(const uint32_t *)&inside->sy > 0x80000000u) {
#endif
		second_y = 0;
	} else {
		second_y = (int)inside->sy;
		if ((float)second_y != inside->sy) {
			++second_y;
		}
	}
	if (first_y == second_y) {
		return -1;
	}
	if (first_y > second_y) {
		swap_vertex = outside;
		outside = inside;
		inside = swap_vertex;
		swap_y = first_y;
		first_y = second_y;
		second_y = swap_y;
	}
	if (second_y <= 0) {
		return -1;
	}
	if (g_flight_vp_height <= first_y) {
		return -1;
	}
	if (second_y > g_flight_vp_height) {
		second_y = g_flight_vp_height;
	}

	edge->y_end = second_y;
	inverse_height = g_sw3d_unit_float / (inside->sy - outside->sy);
	edge->dxdy = (inside->sx - outside->sx) * inverse_height;
	edge->d_light_intensity_dy =
		(inside->light_intensity - outside->light_intensity) *
		inverse_height;
	edge->p_clip_vert = NULL;
	first_row_offset = (float)first_y - outside->sy;
	edge->x = outside->sx + first_row_offset * edge->dxdy;
	edge->light_intensity = outside->light_intensity +
				first_row_offset * edge->d_light_intensity_dy;
	edge->y_start = first_y;
	return first_y;
}

/* Sets up a screen edge between two projected vertices for scan conversion. Its
 * rows run from the upper end's sy rounded up (0 when negative) to the lower
 * end's, cut at g_flight_vp_height; it stores yStart, yEnd, the x and light at
 * yStart, and their changes per row (dxdy, d_light_intensity_dy), and clears
 * p_clip_vert. Returns yStart, or -1 when the edge covers no row, ends at row 0
 * or above, or starts at g_flight_vp_height or below. */
// FUNCTION: XVT 0x471CE0
int sw3d_setup_edge(struct scene_edge *edge, const struct proj_vertex *first,
		    const struct proj_vertex *second)
{
	const struct proj_vertex *swap_vertex;
	int first_y;
	int second_y;
	int swap_y;
	float inverse_height;
	float first_row_offset;

	if (first->sy < 0.0f) {
		first_y = 0;
	} else {
		first_y = (int)first->sy;
		if ((float)first_y != first->sy) {
			++first_y;
		}
	}

	if (second->sy < 0.0f) {
		second_y = 0;
	} else {
		second_y = (int)second->sy;
		if ((float)second_y != second->sy) {
			++second_y;
		}
	}

	if (first_y == second_y) {
		return -1;
	}
	if (first_y > second_y) {
		swap_vertex = first;
		first = second;
		second = swap_vertex;
		swap_y = first_y;
		first_y = second_y;
		second_y = swap_y;
	}
	if (second_y <= 0) {
		return -1;
	}
	if (first_y >= g_flight_vp_height) {
		return -1;
	}
	if (second_y > g_flight_vp_height) {
		second_y = g_flight_vp_height;
	}

	edge->y_end = second_y;
	inverse_height = g_sw3d_unit_float / (second->sy - first->sy);
	edge->dxdy = (second->sx - first->sx) * inverse_height;
	edge->d_light_intensity_dy =
		(second->light_intensity - first->light_intensity) *
		inverse_height;
	edge->p_clip_vert = NULL;
	first_row_offset = (float)first_y - first->sy;
	edge->x = first->sx + first_row_offset * edge->dxdy;
	edge->light_intensity = first->light_intensity +
				first_row_offset * edge->d_light_intensity_dy;
	edge->y_start = first_y;
	return first_y;
}

/* Draws the frame's visible faces from the span buffer onto the flight surface,
 * from g_vis_face_pass_start to g_vis_face_count; with g_use_hardware3d it calls
 * render_scene_flush_geometry instead. Clears the face lighting cache first, and
 * locks the surface around the drawing unless g_flight_surface_already_locked is
 * set. For each face it picks the texture level: with g_mipmapping_enabled and a
 * texture whose texture_size is width * height, it halves both sides while
 * texels_per_pixel_q8 * g_mip_lod_scale, quartered at each level, is over 256 and
 * neither side is 8. It sets the span globals for that level and the mesh, then
 * for each of the face's rows with a span sets the lighting block row globals
 * and draws the span with sw3d_draw_textured_span, leaving out the columns of
 * later spans in the row's list that overlap it. */
// FUNCTION: XVT 0x4865E0
void sw3d_draw_visible_faces_to_surface(void)
{
	enum {
		SW3D_MIP_MIN_DIMENSION = 8,
		SW3D_MIP_THRESHOLD_Q8 = 256,
		SW3D_TEXTURE_SHIFT_MASK = 12,
		SW3D_TEXTURE_SHIFT_INDEX_SHIFT = 4,
	};

	struct scene_edge span_start_edge;
	int face_index;

	flight_light_reset_software_face_sample_cache();
	if (g_use_hardware3d != 0) {
		render_scene_flush_geometry();
		return;
	}

	if (g_flight_surface_already_locked == 0) {
		flight_surface_lock();
	}
	g_sw3d_span_scene_mesh = NULL;
	face_index = g_vis_face_pass_start;
	while (face_index < g_vis_face_count) {
		const struct opt_texture_data *material;
		struct scene_mesh **p_mesh;
		float row_base_w;
		int mip_texel_offset;
		int texture_width;
		int texture_height;
		int texture_height_shift_index;
		int span_index;

		g_sw3d_current_face = &g_vis_face_list[face_index];
		g_sw3d_current_scanline_y = g_sw3d_current_face->y_top;
		g_sw3d_current_face->p_scan_edge = &span_start_edge;
		mip_texel_offset = 0;
		p_mesh = &g_sw3d_current_face->p_mesh;
		material =
			(const struct opt_texture_data *)(*p_mesh)->p_material;
		texture_width = material->width;
		texture_height = material->height;
		if (g_mipmapping_enabled != mip_texel_offset &&
		    texture_width * texture_height == material->texture_size) {
			int mip_metric = (int)((float)g_sw3d_current_face
						       ->texels_per_pixel_q8 *
					       g_mip_lod_scale);

			while (mip_metric > SW3D_MIP_THRESHOLD_Q8 &&
			       texture_width != SW3D_MIP_MIN_DIMENSION &&
			       texture_height != SW3D_MIP_MIN_DIMENSION) {
				mip_metric >>= 2;
				mip_texel_offset +=
					texture_width * texture_height;
				texture_width >>= 1;
				texture_height >>= 1;
			}
		}

		g_sw3d_span_texture_width_float = (float)texture_width;
		g_sw3d_span_texture_height_float = (float)texture_height;
		texture_height_shift_index = texture_height;
		texture_height_shift_index &= ~SW3D_TEXTURE_SHIFT_MASK;
		g_sw3d_span_texture_width_shift =
			g_sw3d_texture_shift_by_size_div16
				[(texture_width & ~SW3D_TEXTURE_SHIFT_MASK) >>
				 SW3D_TEXTURE_SHIFT_INDEX_SHIFT];
		g_sw3d_span_texel_mask = texture_width * texture_height - 1;
		g_sw3d_span_texture_height_shift =
			g_sw3d_texture_shift_by_size_div16
				[texture_height_shift_index >>
				 SW3D_TEXTURE_SHIFT_INDEX_SHIFT];
		g_sw3d_span_shade_table = (*p_mesh)->p_palette;
		g_sw3d_span_texels =
			(uint8_t *)(*p_mesh)->p_texels + mip_texel_offset;
		g_sw3d_span_scene_mesh = *p_mesh;
		row_base_w = (float)(unsigned int)g_sw3d_current_scanline_y *
				     g_sw3d_current_face->gradients[7] +
			     g_sw3d_current_face->gradients[8];
		g_sw3d_span_framebuffer_row_offset =
			g_surface_pitch *
				(g_sw3d_current_scanline_y + g_flight_vp_y) +
			g_flight_bytes_per_pixel * g_flight_vp_x;

		for (span_index = 0; (unsigned int)g_sw3d_current_scanline_y <
				     (unsigned int)g_sw3d_current_face->y_bot;
		     ++span_index, ++g_sw3d_current_scanline_y) {
			struct scene_face *row_face = g_sw3d_current_face;
			struct scene_span *span = row_face->p_spans[span_index];

			if (span != NULL) {
				struct scene_span *occluder;
				int sample_subrow;
				int start_x;
				int end_x;
				float start_x_float;
				float light_intensity;

				sample_subrow = g_sw3d_current_scanline_y &
						g_sw3d_light_sample_block_mask;
				if (sample_subrow != 0) {
					g_sw3d_light_sample_subrow_float =
						(float)(unsigned int)
							sample_subrow;
					g_sw3d_light_sample_subrow_lerp_t =
						g_sw3d_light_sample_subrow_float *
						g_sw3d_span_length_reciprocal
							[g_sw3d_light_sample_block_size];
					g_sw3d_light_sample_rows_to_next_block_float =
						(float)(unsigned int)(g_sw3d_light_sample_block_size -
								      sample_subrow);
				} else {
					g_sw3d_light_sample_subrow_lerp_t =
						0.0f;
					g_sw3d_light_sample_subrow_float = 0.0f;
					g_sw3d_light_sample_rows_to_next_block_float =
						(float)(unsigned int)
							g_sw3d_light_sample_block_size;
				}
				g_sw3d_current_light_sample_cache_stamp =
					g_sw3d_light_sample_cache_scene_stamp_base +
					((unsigned int)
						 g_sw3d_current_scanline_y >>
					 g_sw3d_light_sample_block_shift);

				occluder = span->next;
				start_x = span->x_start;
				end_x = span->x_end;
				start_x_float = (float)start_x;
				light_intensity = span->light_intensity;
				span_start_edge.light_intensity =
					light_intensity;
				span_start_edge.x = start_x_float;
				g_sw3d_current_face->span_light_intensity_dx =
					span->d_light_intensity_dx;
				if (occluder != NULL) {
					do {
						const int occluder_start =
							occluder->x_start;

						if (end_x <= occluder_start) {
							break;
						}
						if (start_x >= occluder_start) {
							const int occluder_end =
								occluder->x_end;

							if (start_x <
							    occluder_end) {
								start_x =
									occluder->x_end;
								if (end_x <=
								    occluder_end) {
									break;
								}
							}
						} else {
							float span_start_w =
								(float)start_x *
									g_sw3d_current_face
										->gradients
											[6] +
								row_base_w;

							sw3d_draw_textured_span(
								start_x,
								occluder_start,
								span_start_w);
							start_x =
								occluder->x_end;
							end_x = span->x_end;
							if (end_x <= start_x) {
								break;
							}
						}
						occluder = occluder->next;
					} while (occluder != NULL);
					if (end_x > start_x) {
						float span_start_w =
							(float)start_x *
								g_sw3d_current_face
									->gradients
										[6] +
							row_base_w;

						sw3d_draw_textured_span(
							start_x, end_x,
							span_start_w);
					}
				} else {
					float span_start_w =
						g_sw3d_current_face
								->gradients[6] *
							start_x_float +
						row_base_w;

					sw3d_draw_textured_span(start_x, end_x,
								span_start_w);
				}
				row_face = g_sw3d_current_face;
			}
			row_base_w = row_face->gradients[7] + row_base_w;
			g_sw3d_span_framebuffer_row_offset += g_surface_pitch;
		}
		++face_index;
	}

	if (g_flight_surface_already_locked == 0) {
		flight_surface_unlock();
	}
}

/* Adds a face's span on row scan_y to the row's span list,
 * g_scanline_span_heads[scan_y], kept in column order, so that each pixel
 * ends with the nearest face. The span runs from xLeft to xRight, each
 * rounded up and negative ones taken as 0, its end cut at g_flight_vp_width;
 * nothing is added when it is empty, starts at g_flight_vp_width or past it,
 * or lies on an odd row while g_sw3d_skip_odd_scanlines is set. Against each
 * span it overlaps it settles depth by the two faces' depth ranges
 * (min_scaled_inverse_depth to max_scaled_inverse_depth, larger nearer) when
 * those do not overlap, else by their w along the overlap,
 * gradients[6] * x + gradients[7] * y + gradients[8], splitting at the
 * column where those cross. It cuts the new span short, or trims, moves or
 * removes the spans it hides, moving a span's light with its start and
 * clearing the p_spans entry of a span removed. The new span is the next
 * entry at g_p_scene_span_data_cur, the pool's last entry again once it runs
 * out; its light at its start comes from the face's p_scan_edge and
 * span_light_intensity_dx, 0 without a scan edge. It is stored in the face's
 * p_spans[scan_y - yTop], which the call first clears. */
// FUNCTION: XVT 0x486980
void sw3d_insert_span(float x_left, float x_right, int scan_y,
		      struct scene_face *face)
{
	struct scene_span *current;
	struct scene_span *next;
	struct scene_span *insertion_next;
	struct scene_span *previous;
	struct scene_span *span;
	float new_w;
	float current_w;
	int start_x;
	int end_x;
	int overlap_width;
	int crossing_from_right;
	int crossing_from_left;
	int current_left_width;
	int current_right_width;

	face->p_spans[scan_y - face->y_top] = NULL;
	if (x_left < 0.0f) {
		start_x = 0;
	} else {
		start_x = (int)x_left;
		if ((float)start_x != x_left) {
			++start_x;
		}
	}
	if (x_right < 0.0f) {
		end_x = 0;
	} else {
		end_x = (int)x_right;
		if ((float)end_x != x_right) {
			++end_x;
		}
	}
	if (end_x >= g_flight_vp_width) {
		end_x = g_flight_vp_width;
	}
	if (end_x <= start_x || start_x >= g_flight_vp_width ||
	    (g_sw3d_skip_odd_scanlines && (scan_y & 1))) {
		return;
	}

	previous = NULL;
	current = g_scanline_span_heads[scan_y];
	while (current != NULL) {
		if (current->x_end <= start_x) {
			previous = current;
			current = current->next;
			continue;
		}
		if (current->x_start > start_x) {
			break;
		}

		if (face->max_scaled_inverse_depth <=
		    current->face->min_scaled_inverse_depth) {
			start_x = current->x_end;
			if (start_x >= end_x) {
				return;
			}
			previous = current;
			current = current->next;
			continue;
		}
		if (face->min_scaled_inverse_depth >=
		    current->face->max_scaled_inverse_depth) {
			if (current->x_end > end_x) {
				previous = current;
				current = current->next;
				continue;
			}
			current->x_end = start_x;
			if (current->x_end == current->x_start) {
				current->face->p_spans[scan_y -
						       current->face->y_top] =
					NULL;
				if (previous == NULL) {
					g_scanline_span_heads[scan_y] =
						current->next;
				} else {
					previous->next = current->next;
				}
				current = current->next;
			} else {
				previous = current;
				current = current->next;
			}
			continue;
		}

		new_w = face->gradients[7] * (float)scan_y +
			face->gradients[8] +
			face->gradients[6] * (float)start_x;
		current_w = current->face->gradients[7] * (float)scan_y +
			    current->face->gradients[8] +
			    current->face->gradients[6] * (float)start_x;
		if (new_w <= current_w) {
			if (current->face->gradients[6] >= face->gradients[6]) {
				start_x = current->x_end;
				if (start_x >= end_x) {
					return;
				}
				previous = current;
				current = current->next;
				continue;
			}
			if (current->x_end < end_x) {
				overlap_width = current->x_end - start_x;
				new_w += (float)overlap_width *
					 face->gradients[6];
				current_w += (float)overlap_width *
					     current->face->gradients[6];
				if (new_w <= current_w) {
					start_x = current->x_end;
					previous = current;
					current = current->next;
					continue;
				}
			} else {
				overlap_width = end_x - start_x;
				new_w += (float)overlap_width *
					 face->gradients[6];
				current_w += (float)overlap_width *
					     current->face->gradients[6];
				if (new_w <= current_w) {
					return;
				}
			}
			current_left_width =
				(int)((float)overlap_width -
				      (new_w - current_w) /
					      (face->gradients[6] -
					       current->face->gradients[6])) +
				1;
			if (current_left_width < 0) {
				current_left_width = 0;
			}
			start_x += current_left_width;
			if (start_x >= end_x) {
				return;
			}
			previous = current;
			current = current->next;
			continue;
		}

		if (current->face->gradients[6] <= face->gradients[6]) {
			if (current->x_end > end_x) {
				previous = current;
				current = current->next;
				continue;
			}
			current->x_end = start_x;
			if (current->x_end == current->x_start) {
				current->face->p_spans[scan_y -
						       current->face->y_top] =
					NULL;
				if (previous == NULL) {
					g_scanline_span_heads[scan_y] =
						current->next;
				} else {
					previous->next = current->next;
				}
				current = current->next;
			} else {
				previous = current;
				current = current->next;
			}
			continue;
		}

		if (current->x_end <= end_x) {
			overlap_width = current->x_end - start_x;
			new_w += (float)overlap_width * face->gradients[6];
			current_w += (float)overlap_width *
				     current->face->gradients[6];
			if (new_w >= current_w) {
				current->x_end = start_x;
				if (current->x_end == current->x_start) {
					current->face->p_spans
						[scan_y -
						 current->face->y_top] = NULL;
					if (previous == NULL) {
						g_scanline_span_heads[scan_y] =
							current->next;
					} else {
						previous->next = current->next;
					}
					current = current->next;
				} else {
					previous = current;
					current = current->next;
				}
				continue;
			}

			crossing_from_right =
				(int)((new_w - current_w) /
				      (face->gradients[6] -
				       current->face->gradients[6]));
			if (crossing_from_right < 0) {
				crossing_from_right = 0;
			}
			if (crossing_from_right > overlap_width) {
				crossing_from_right = overlap_width;
			}
			crossing_from_left =
				overlap_width - crossing_from_right;
			current_left_width = start_x - current->x_start;
			current_right_width = end_x - current->x_end;

			if (current_left_width <= crossing_from_right &&
			    current_left_width <= crossing_from_left &&
			    current_right_width >= current_left_width) {
				current_left_width = current->x_end -
						     current->x_start -
						     crossing_from_right;
				current->x_start += current_left_width;
				current->light_intensity +=
					(float)current_left_width *
					current->d_light_intensity_dx;
				next = current->next;
				if (next == NULL ||
				    next->x_start > current->x_start) {
					break;
				}
				if (previous == NULL) {
					g_scanline_span_heads[scan_y] = next;
				} else {
					previous->next = next;
				}
				if (current->x_start < next->x_end) {
					current_left_width =
						next->x_end - current->x_start;
					current->x_start += current_left_width;
					current->light_intensity +=
						(float)current_left_width *
						current->d_light_intensity_dx;
				}
				insertion_next = next->next;
				while (insertion_next != NULL &&
				       insertion_next->x_start <
					       current->x_start) {
					if (current->x_start <
					    insertion_next->x_end) {
						current_left_width =
							insertion_next->x_end -
							current->x_start;
						current->x_start +=
							current_left_width;
						current->light_intensity +=
							(float)current_left_width *
							current->d_light_intensity_dx;
					}
					if (current->x_start >=
					    current->x_end) {
						break;
					}
					next = insertion_next;
					insertion_next = insertion_next->next;
				}
				if (current->x_start < current->x_end) {
					next->next = current;
					current->next = insertion_next;
				} else {
					current->face->p_spans
						[scan_y -
						 current->face->y_top] = NULL;
				}
				if (previous == NULL) {
					current = g_scanline_span_heads[scan_y];
				} else {
					current = previous->next;
				}
				continue;
			} else if (current_left_width >= crossing_from_right &&
				   crossing_from_left >= crossing_from_right &&
				   current_right_width >= crossing_from_right) {
				current->x_end = start_x;
				if (current->x_end == current->x_start) {
					current->face->p_spans
						[scan_y -
						 current->face->y_top] = NULL;
					if (previous == NULL) {
						g_scanline_span_heads[scan_y] =
							current->next;
					} else {
						previous->next = current->next;
					}
					current = current->next;
				} else {
					previous = current;
					current = current->next;
				}
				continue;
			}
			if (current_left_width >= crossing_from_left &&
			    crossing_from_left <= crossing_from_right &&
			    current_right_width >= crossing_from_left) {
				start_x = current->x_end;
				if (start_x >= end_x) {
					return;
				}
				previous = current;
				current = current->next;
				continue;
			}
			end_x = current->x_end - crossing_from_right;
			if (start_x >= end_x) {
				return;
			}
			previous = current;
			current = current->next;
		} else {
			overlap_width = end_x - start_x;
			new_w += (float)overlap_width * face->gradients[6];
			current_w += (float)overlap_width *
				     current->face->gradients[6];
			if (new_w < current_w) {
				current_left_width =
					(int)((float)overlap_width -
					      (new_w - current_w) /
						      (face->gradients[6] -
						       current->face->gradients
							       [6])) +
					1;
				if (current_left_width < 0) {
					current_left_width = 0;
				}
				/* Here current_left_width holds an x coordinate, the new span's new end, not a width. */
				current_left_width += start_x;
				if (current_left_width > end_x) {
					current_left_width = end_x;
				}
				end_x = current_left_width;
				if (start_x >= end_x) {
					return;
				}
			}
			previous = current;
			current = current->next;
		}
	}

	span = g_p_scene_span_data_cur++;
	if (g_p_scene_span_data_cur == g_p_scene_span_data_end) {
		--g_p_scene_span_data_cur;
	}
	span->x_start = start_x;
	span->x_end = end_x;
	span->face = face;
	if (face->p_scan_edge == NULL) {
		span->light_intensity = 0.0f;
	} else {
		span->light_intensity = (float)start_x;
		span->light_intensity -= face->p_scan_edge->x;
		span->light_intensity *= face->span_light_intensity_dx;
		span->light_intensity += face->p_scan_edge->light_intensity;
	}
	span->d_light_intensity_dx = face->span_light_intensity_dx;
	face->p_spans[scan_y - face->y_top] = span;
	if (previous == NULL) {
		g_scanline_span_heads[scan_y] = span;
	} else {
		previous->next = span;
	}
	span->next = current;

	previous = span;
	while (current != NULL) {
		if (current->x_start >= span->x_end) {
			return;
		}
		if (face->max_scaled_inverse_depth <=
		    current->face->min_scaled_inverse_depth) {
			if (current->x_end >= span->x_end) {
				span->x_end = current->x_start;
				return;
			}
			previous = current;
			current = current->next;
			continue;
		}
		if (face->min_scaled_inverse_depth >=
		    current->face->max_scaled_inverse_depth) {
			if (current->x_end <= span->x_end) {
				current->face->p_spans[scan_y -
						       current->face->y_top] =
					NULL;
				previous->next = current->next;
				current = current->next;
				continue;
			}
			current_left_width = span->x_end - current->x_start;
			current->x_start += current_left_width;
			current->light_intensity +=
				(float)current_left_width *
				current->d_light_intensity_dx;
			next = current->next;
			if (next != NULL && next->x_start < current->x_start) {
				previous->next = next;
				if (current->x_start < next->x_end) {
					current_left_width =
						next->x_end - current->x_start;
					current->x_start += current_left_width;
					current->light_intensity +=
						(float)current_left_width *
						current->d_light_intensity_dx;
				}
				insertion_next = next->next;
				while (insertion_next != NULL &&
				       insertion_next->x_start <
					       current->x_start) {
					if (current->x_start <
					    insertion_next->x_end) {
						current_left_width =
							insertion_next->x_end -
							current->x_start;
						current->x_start +=
							current_left_width;
						current->light_intensity +=
							(float)current_left_width *
							current->d_light_intensity_dx;
					}
					if (current->x_start >=
					    current->x_end) {
						break;
					}
					next = insertion_next;
					insertion_next = insertion_next->next;
				}
				if (current->x_start < current->x_end) {
					next->next = current;
					current->next = insertion_next;
				} else {
					current->face->p_spans
						[scan_y -
						 current->face->y_top] = NULL;
				}
				current = previous->next;
				continue;
			}
			previous = current;
			current = current->next;
			continue;
		}

		new_w = face->gradients[7] * (float)scan_y +
			face->gradients[8] +
			face->gradients[6] * (float)current->x_start;
		current_w =
			current->face->gradients[7] * (float)scan_y +
			current->face->gradients[8] +
			current->face->gradients[6] * (float)current->x_start;
		if (new_w <= current_w) {
			if (current->face->gradients[6] >= face->gradients[6]) {
				if (current->x_end >= span->x_end) {
					span->x_end = current->x_start;
					return;
				}
				previous = current;
				current = current->next;
				continue;
			}

			if (current->x_end < span->x_end) {
				overlap_width =
					current->x_end - current->x_start;
				new_w += (float)overlap_width *
					 face->gradients[6];
				current_w += (float)overlap_width *
					     current->face->gradients[6];
				if (new_w <= current_w) {
					previous = current;
					current = current->next;
					continue;
				}
				crossing_from_right =
					(int)((new_w - current_w) /
					      (face->gradients[6] -
					       current->face->gradients[6]));
				if (crossing_from_right < 0) {
					crossing_from_right = 0;
				}
				current->x_end -= crossing_from_right;
				if (current->x_end <= current->x_start) {
					current->face->p_spans
						[scan_y -
						 current->face->y_top] = NULL;
					previous->next = current->next;
					current = current->next;
				} else {
					previous = current;
					current = current->next;
				}
				continue;
			}

			overlap_width = span->x_end - current->x_start;
			new_w += (float)overlap_width * face->gradients[6];
			current_w += (float)overlap_width *
				     current->face->gradients[6];
			if (new_w <= current_w) {
				span->x_end = current->x_start;
				return;
			}
			crossing_from_right =
				(int)((new_w - current_w) /
				      (face->gradients[6] -
				       current->face->gradients[6]));
			if (crossing_from_right < 0) {
				crossing_from_right = 0;
			}
			if (crossing_from_right > overlap_width) {
				crossing_from_right = overlap_width;
			}
			current_left_width =
				overlap_width - crossing_from_right;
			if (current_left_width > crossing_from_right &&
			    current->x_end - span->x_end >
				    crossing_from_right) {
				span->x_end = current->x_start;
				return;
			}
			if (current->x_end - span->x_end > current_left_width) {
				current->x_start += overlap_width;
				current->light_intensity +=
					(float)overlap_width *
					current->d_light_intensity_dx;
				next = current->next;
				if (next != NULL &&
				    next->x_start < current->x_start) {
					previous->next = next;
					if (current->x_start < next->x_end) {
						current_left_width =
							next->x_end -
							current->x_start;
						current->x_start +=
							current_left_width;
						current->light_intensity +=
							(float)current_left_width *
							current->d_light_intensity_dx;
					}
					insertion_next = next->next;
					while (insertion_next != NULL &&
					       insertion_next->x_start <
						       current->x_start) {
						if (current->x_start <
						    insertion_next->x_end) {
							current_left_width =
								insertion_next
									->x_end -
								current->x_start;
							current->x_start +=
								current_left_width;
							current->light_intensity +=
								(float)current_left_width *
								current->d_light_intensity_dx;
						}
						if (current->x_start >=
						    current->x_end) {
							break;
						}
						next = insertion_next;
						insertion_next =
							insertion_next->next;
					}
					if (current->x_start < current->x_end) {
						next->next = current;
						current->next = insertion_next;
					} else {
						current->face->p_spans
							[scan_y -
							 current->face->y_top] =
							NULL;
					}
					current = previous->next;
					continue;
				}
				previous = current;
				current = current->next;
			} else {
				current->x_end =
					span->x_end - crossing_from_right;
				previous = current;
				current = current->next;
			}
			continue;
		}

		if (current->face->gradients[6] <= face->gradients[6]) {
			if (current->x_end <= span->x_end) {
				current->face->p_spans[scan_y -
						       current->face->y_top] =
					NULL;
				previous->next = current->next;
				current = current->next;
				continue;
			}
			current_left_width = span->x_end - current->x_start;
			current->x_start += current_left_width;
			current->light_intensity +=
				(float)current_left_width *
				current->d_light_intensity_dx;
			next = current->next;
			if (next != NULL && next->x_start < current->x_start) {
				previous->next = next;
				if (current->x_start < next->x_end) {
					current_left_width =
						next->x_end - current->x_start;
					current->x_start += current_left_width;
					current->light_intensity +=
						(float)current_left_width *
						current->d_light_intensity_dx;
				}
				insertion_next = next->next;
				while (insertion_next != NULL &&
				       insertion_next->x_start <
					       current->x_start) {
					if (current->x_start <
					    insertion_next->x_end) {
						current_left_width =
							insertion_next->x_end -
							current->x_start;
						current->x_start +=
							current_left_width;
						current->light_intensity +=
							(float)current_left_width *
							current->d_light_intensity_dx;
					}
					if (current->x_start >=
					    current->x_end) {
						break;
					}
					next = insertion_next;
					insertion_next = insertion_next->next;
				}
				if (current->x_start < current->x_end) {
					next->next = current;
					current->next = insertion_next;
				} else {
					current->face->p_spans
						[scan_y -
						 current->face->y_top] = NULL;
				}
				current = previous->next;
				continue;
			}
			previous = current;
			current = current->next;
			continue;
		}

		if (span->x_end >= current->x_end) {
			overlap_width = current->x_end - current->x_start;
			new_w += (float)overlap_width * face->gradients[6];
			current_w += (float)overlap_width *
				     current->face->gradients[6];
			if (new_w >= current_w) {
				current->face->p_spans[scan_y -
						       current->face->y_top] =
					NULL;
				previous->next = current->next;
				current = current->next;
				continue;
			}
			crossing_from_right =
				(int)((new_w - current_w) /
				      (face->gradients[6] -
				       current->face->gradients[6]));
			if (crossing_from_right < 0) {
				crossing_from_right = 0;
			}
			current_left_width =
				overlap_width - crossing_from_right;
			if (current_left_width < 0) {
				current_left_width = 0;
			}
			current->x_start += current_left_width;
			current->light_intensity +=
				(float)current_left_width *
				current->d_light_intensity_dx;
			if (current->x_end <= current->x_start) {
				current->face->p_spans[scan_y -
						       current->face->y_top] =
					NULL;
				previous->next = current->next;
				current = current->next;
				continue;
			}
			next = current->next;
			if (next != NULL && next->x_start < current->x_start) {
				previous->next = next;
				if (current->x_start < next->x_end) {
					current_left_width =
						next->x_end - current->x_start;
					current->x_start += current_left_width;
					current->light_intensity +=
						(float)current_left_width *
						current->d_light_intensity_dx;
				}
				insertion_next = next->next;
				while (insertion_next != NULL &&
				       insertion_next->x_start <
					       current->x_start) {
					if (current->x_start <
					    insertion_next->x_end) {
						current_left_width =
							insertion_next->x_end -
							current->x_start;
						current->x_start +=
							current_left_width;
						current->light_intensity +=
							(float)current_left_width *
							current->d_light_intensity_dx;
					}
					if (current->x_start >=
					    current->x_end) {
						break;
					}
					next = insertion_next;
					insertion_next = insertion_next->next;
				}
				if (current->x_start < current->x_end) {
					next->next = current;
					current->next = insertion_next;
				} else {
					current->face->p_spans
						[scan_y -
						 current->face->y_top] = NULL;
				}
				current = previous->next;
				continue;
			}
			previous = current;
			current = current->next;
		} else {
			overlap_width = span->x_end - current->x_start;
			new_w += (float)overlap_width * face->gradients[6];
			current_w += (float)overlap_width *
				     current->face->gradients[6];
			if (new_w >= current_w) {
				current->x_start += overlap_width;
				current->light_intensity +=
					(float)overlap_width *
					current->d_light_intensity_dx;
				next = current->next;
				if (next != NULL &&
				    next->x_start < current->x_start) {
					previous->next = next;
					if (current->x_start < next->x_end) {
						current_left_width =
							next->x_end -
							current->x_start;
						current->x_start +=
							current_left_width;
						current->light_intensity +=
							(float)current_left_width *
							current->d_light_intensity_dx;
					}
					insertion_next = next->next;
					while (insertion_next != NULL &&
					       insertion_next->x_start <
						       current->x_start) {
						if (current->x_start <
						    insertion_next->x_end) {
							current_left_width =
								insertion_next
									->x_end -
								current->x_start;
							current->x_start +=
								current_left_width;
							current->light_intensity +=
								(float)current_left_width *
								current->d_light_intensity_dx;
						}
						if (current->x_start >=
						    current->x_end) {
							break;
						}
						next = insertion_next;
						insertion_next =
							insertion_next->next;
					}
					if (current->x_start < current->x_end) {
						next->next = current;
						current->next = insertion_next;
					} else {
						current->face->p_spans
							[scan_y -
							 current->face->y_top] =
							NULL;
					}
					current = previous->next;
					continue;
				}
				previous = current;
				current = current->next;
				continue;
			}
			current_left_width =
				(int)((float)overlap_width -
				      (new_w - current_w) /
					      (face->gradients[6] -
					       current->face->gradients[6]));
			if (current_left_width < 0) {
				current_left_width = 0;
			}
			if (current_left_width > overlap_width) {
				current_left_width = overlap_width;
			}
			current->x_start += current_left_width;
			current->light_intensity +=
				(float)current_left_width *
				current->d_light_intensity_dx;
			next = current->next;
			if (next != NULL && next->x_start < current->x_start) {
				previous->next = next;
				if (current->x_start < next->x_end) {
					current_left_width =
						next->x_end - current->x_start;
					current->x_start += current_left_width;
					current->light_intensity +=
						(float)current_left_width *
						current->d_light_intensity_dx;
				}
				insertion_next = next->next;
				while (insertion_next != NULL &&
				       insertion_next->x_start <
					       current->x_start) {
					if (current->x_start <
					    insertion_next->x_end) {
						current_left_width =
							insertion_next->x_end -
							current->x_start;
						current->x_start +=
							current_left_width;
						current->light_intensity +=
							(float)current_left_width *
							current->d_light_intensity_dx;
					}
					if (current->x_start >=
					    current->x_end) {
						break;
					}
					next = insertion_next;
					insertion_next = insertion_next->next;
				}
				if (current->x_start < current->x_end) {
					next->next = current;
					current->next = insertion_next;
				} else {
					current->face->p_spans
						[scan_y -
						 current->face->y_top] = NULL;
				}
				current = previous->next;
				continue;
			}
			previous = current;
			current = current->next;
		}
	}
}

/* Draws pixels start_x to end_x - 1 of the current row of g_sw3d_current_face,
 * textured and shaded; span_start_w is the face's w at start_x. Texture u and v
 * are worked out with perspective at each lighting block boundary (every
 * g_sw3d_light_sample_block_size columns) and stepped evenly between, in texels
 * with 8 fraction bits. The light at each boundary comes from the face's light
 * samples, one per block column, kept by stamp: a sample stamped
 * g_sw3d_current_light_sample_cache_stamp is used as it is, one stamped 1 less is
 * moved on by its row_delta, any other is worked out with
 * flight_light_compute_software_face_sample_intensity at the block's top and the
 * next block's top; the row takes it at g_sw3d_light_sample_subrow_lerp_t. That
 * light plus the span's interpolated vertex light, times 15, is the shade with
 * 8 fraction bits, kept to 0 to 0xEFF at each boundary and stepped across the
 * block; a negative change gets g_sw3d_light_sample_block_size added. Each pixel is
 * the shade table entry for level ((shade + carry) >> 8) & 15 and its texel,
 * written at 8 or 16 bits. With both texture shifts 3 to 8 each coordinate
 * wraps on its own; otherwise the texel index is masked with
 * g_sw3d_span_texel_mask, through sw3d_draw_textured_shade_span_generic16bpp in 16-bit
 * color. */
// FUNCTION: XVT 0x4879D0
void sw3d_draw_textured_span(int start_x, int end_x, float span_start_w)
{
	enum {
		SW3D_MIN_SPECIALIZED_TEXTURE_SHIFT = 3,
		SW3D_MAX_SPECIALIZED_TEXTURE_SHIFT = 8,
		SW3D_FIXED_POINT_SHIFT = 8,
		SW3D_SHADE_LEVEL_MASK = 0xF,
		SW3D_SHADE_TABLE_LEVEL_STRIDE = 256,
		SW3D_TRUE_COLOR_SHADE_TABLE_OFFSET = 4096,
		SW3D_MAX_SHADE_Q8 = 0xEFF,
	};

	struct scene_face *face;
	struct software_light_sample *light_samples;
	struct software_light_sample *left_sample;
	struct software_light_sample *right_sample;
	float *u_gradient;
	float *v_gradient;
	float u_at_y;
	float v_at_y;
	float w_at_y;
	float u_numerator;
	float v_numerator;
	float inverse_w;
	float u;
	float v;
	float right_u;
	float right_v;
	float left_light;
	float right_light;
	float light_intensity;
	float light_intensity_at_end;
	float light_intensity_block_step;
	float u_numerator_block_step;
	float v_numerator_block_step;
	float w_block_step;
	float boundary_w;
	float sample_intensity;
	float fixed_point_value;
	float fixed_point_bias;
	int fixed_point_bits;
	int fixed_point_bias_bits;
	int next_uq8;
	int next_vq8;
	int end_shade_q8;
	int shade_delta_q8;
	int start_block;
	int end_block;
	int block;
	int boundary_x;
	int block_start_x;
	int block_start_y;
	int within_block_x;
	int within_block_y;
	int stamp_delta;
	int pixel_index;
	int use_specialized_texture_wrap;
	int texture_width_mask;
	int texture_height_mask;

	face = g_sw3d_current_face;
	light_samples = (struct software_light_sample *)face->p_light_samples;
	w_at_y = (float)(unsigned int)g_sw3d_current_scanline_y *
			 face->gradients[7] +
		 face->gradients[8];
	u_gradient = &face->gradients[0];
	v_gradient = &face->gradients[3];
	u_at_y = (float)g_sw3d_current_scanline_y * u_gradient[1];
	v_at_y = (float)g_sw3d_current_scanline_y * v_gradient[1];
	u_at_y += u_gradient[2];
	v_at_y += v_gradient[2];
	u_numerator = (float)start_x * u_gradient[0] + u_at_y;
	v_numerator = (float)start_x * v_gradient[0] + v_at_y;
	inverse_w = g_sw3d_span_one_float / span_start_w;
	u = inverse_w * u_numerator;
	v = inverse_w * v_numerator;
	light_intensity = ((float)start_x - face->p_scan_edge->x) *
				  face->span_light_intensity_dx +
			  face->p_scan_edge->light_intensity;

	g_sw3d_span_shade_dither_accum =
		g_sw3d_shade_dither_initial_by_scanline_parity
			[g_sw3d_current_scanline_y & 1];
	start_block = start_x >> g_sw3d_light_sample_block_shift;
	end_block = (end_x - 1) >> g_sw3d_light_sample_block_shift;
	g_sw3d_span_start_x = start_x;
	within_block_x = start_x & g_sw3d_light_sample_block_mask;
	within_block_y =
		g_sw3d_current_scanline_y & g_sw3d_light_sample_block_mask;
	block_start_x = start_x - within_block_x;
	block_start_y = g_sw3d_current_scanline_y - within_block_y;
	left_sample = &light_samples[start_block];
	stamp_delta =
		g_sw3d_current_light_sample_cache_stamp - left_sample->stamp;
	if (stamp_delta != 0) {
		if (stamp_delta != 1) {
			left_sample->stamp =
				g_sw3d_current_light_sample_cache_stamp;
			left_sample->intensity =
				flight_light_compute_software_face_sample_intensity(
					face, block_start_x, block_start_y,
					span_start_w -
						(float)within_block_x *
							face->gradients[6] -
						g_sw3d_light_sample_subrow_float *
							face->gradients[7]);
		} else {
			++left_sample->stamp;
			left_sample->intensity += left_sample->row_delta;
		}
		left_sample->row_delta =
			flight_light_compute_software_face_sample_intensity(
				face, block_start_x,
				block_start_y + g_sw3d_light_sample_block_size,
				span_start_w -
					(float)within_block_x *
						face->gradients[6] +
					g_sw3d_light_sample_rows_to_next_block_float *
						face->gradients[7]) -
			left_sample->intensity;
	}
	left_light = left_sample->intensity +
		     g_sw3d_light_sample_subrow_lerp_t * left_sample->row_delta;

	boundary_x = (start_block + 1) << g_sw3d_light_sample_block_shift;
	g_sw3d_span_length = boundary_x - g_sw3d_span_start_x;
	u_numerator = (float)boundary_x * u_gradient[0] + u_at_y;
	v_numerator = (float)boundary_x * v_gradient[0] + v_at_y;
	boundary_w = (float)boundary_x * face->gradients[6] + w_at_y;
	block = start_block + 1;
	inverse_w = g_sw3d_span_one_float / boundary_w;
	right_sample = &light_samples[block];
	stamp_delta =
		g_sw3d_current_light_sample_cache_stamp - right_sample->stamp;
	if (stamp_delta != 0) {
		right_sample->stamp = g_sw3d_current_light_sample_cache_stamp;
		if (stamp_delta != 1) {
			right_sample->intensity =
				flight_light_compute_software_face_sample_intensity(
					face, boundary_x, block_start_y,
					boundary_w);
			sample_intensity =
				flight_light_compute_software_face_sample_intensity(
					face, boundary_x,
					block_start_y +
						g_sw3d_light_sample_block_size,
					boundary_w);
		} else {
			sample_intensity =
				flight_light_compute_software_face_sample_intensity(
					face, boundary_x,
					block_start_y +
						g_sw3d_light_sample_block_size,
					boundary_w);
			right_sample->intensity += right_sample->row_delta;
		}
		right_sample->row_delta =
			sample_intensity - right_sample->intensity;
	}
	right_light =
		right_sample->intensity +
		g_sw3d_light_sample_subrow_lerp_t * right_sample->row_delta;
	left_light +=
		(right_light - left_light) *
		((float)within_block_x * g_sw3d_light_sample_inv_block_size);
	right_u = inverse_w * u_numerator;
	right_v = inverse_w * v_numerator;

	fixed_point_bias =
		g_sw3d_tex_coord_bias_by_shift[g_sw3d_span_texture_width_shift];
	memcpy(&fixed_point_bias_bits, &fixed_point_bias,
	       sizeof(fixed_point_bias_bits));
	fixed_point_value =
		(right_u - u) *
			g_sw3d_span_length_reciprocal[g_sw3d_span_length] +
		fixed_point_bias;
	memcpy(&fixed_point_bits, &fixed_point_value, sizeof(fixed_point_bits));
	g_sw3d_span_step_uq8 = fixed_point_bits - fixed_point_bias_bits;
	fixed_point_bias = g_sw3d_tex_coord_bias_by_shift
		[g_sw3d_span_texture_height_shift];
	memcpy(&fixed_point_bias_bits, &fixed_point_bias,
	       sizeof(fixed_point_bias_bits));
	fixed_point_value =
		(right_v - v) *
			g_sw3d_span_length_reciprocal[g_sw3d_span_length] +
		fixed_point_bias;
	memcpy(&fixed_point_bits, &fixed_point_value, sizeof(fixed_point_bits));
	g_sw3d_span_step_vq8 = fixed_point_bits - fixed_point_bias_bits;

	if ((unsigned int)block > (unsigned int)end_block) {
		g_sw3d_span_length = end_x - g_sw3d_span_start_x;
	} else {
		u_numerator_block_step =
			u_gradient[0] * g_sw3d_light_sample_block_size_float;
		v_numerator_block_step =
			v_gradient[0] * g_sw3d_light_sample_block_size_float;
		w_block_step = face->gradients[6] *
			       g_sw3d_light_sample_block_size_float;
	}

	light_intensity_at_end =
		(float)g_sw3d_span_length * face->span_light_intensity_dx +
		light_intensity;
	light_intensity_block_step = face->span_light_intensity_dx *
				     g_sw3d_light_sample_block_size_float;
	fixed_point_bias = g_sw3d_tex_coord_bias_by_shift[0];
	memcpy(&fixed_point_bias_bits, &fixed_point_bias,
	       sizeof(fixed_point_bias_bits));
	fixed_point_value = (left_light + light_intensity) *
				    g_sw3d_light_intensity_to_shade_scale +
			    fixed_point_bias;
	memcpy(&fixed_point_bits, &fixed_point_value, sizeof(fixed_point_bits));
	g_sw3d_span_shade_q8 = fixed_point_bits - fixed_point_bias_bits;
	if (g_sw3d_span_shade_q8 < 0) {
		g_sw3d_span_shade_q8 = 0;
	}
	if (g_sw3d_span_shade_q8 > SW3D_MAX_SHADE_Q8) {
		g_sw3d_span_shade_q8 = SW3D_MAX_SHADE_Q8;
	}
	fixed_point_value = (right_light + light_intensity_at_end) *
				    g_sw3d_light_intensity_to_shade_scale +
			    fixed_point_bias;
	memcpy(&fixed_point_bits, &fixed_point_value, sizeof(fixed_point_bits));
	end_shade_q8 = fixed_point_bits - fixed_point_bias_bits;
	if (end_shade_q8 < 0) {
		end_shade_q8 = 0;
	}
	if (end_shade_q8 > SW3D_MAX_SHADE_Q8) {
		end_shade_q8 = SW3D_MAX_SHADE_Q8;
	}
	shade_delta_q8 = end_shade_q8 - g_sw3d_span_shade_q8;
	if (shade_delta_q8 < 0) {
		shade_delta_q8 += g_sw3d_light_sample_block_size;
	}
	fixed_point_bias = g_sw3d_float_to_int_round_bias;
	memcpy(&fixed_point_bias_bits, &fixed_point_bias,
	       sizeof(fixed_point_bias_bits));
	fixed_point_value =
		(float)shade_delta_q8 *
			g_sw3d_span_length_reciprocal[g_sw3d_span_length] +
		fixed_point_bias;
	memcpy(&fixed_point_bits, &fixed_point_value, sizeof(fixed_point_bits));
	g_sw3d_span_shade_step_q8 = fixed_point_bits - fixed_point_bias_bits;

	fixed_point_bias =
		g_sw3d_tex_coord_bias_by_shift[g_sw3d_span_texture_width_shift];
	memcpy(&fixed_point_bias_bits, &fixed_point_bias,
	       sizeof(fixed_point_bias_bits));
	fixed_point_value = u + fixed_point_bias;
	memcpy(&fixed_point_bits, &fixed_point_value, sizeof(fixed_point_bits));
	g_sw3d_span_uq8 = fixed_point_bits - fixed_point_bias_bits;
	fixed_point_value = right_u + fixed_point_bias;
	memcpy(&fixed_point_bits, &fixed_point_value, sizeof(fixed_point_bits));
	next_uq8 = fixed_point_bits - fixed_point_bias_bits;
	fixed_point_bias = g_sw3d_tex_coord_bias_by_shift
		[g_sw3d_span_texture_height_shift];
	memcpy(&fixed_point_bias_bits, &fixed_point_bias,
	       sizeof(fixed_point_bias_bits));
	fixed_point_value = v + fixed_point_bias;
	memcpy(&fixed_point_bits, &fixed_point_value, sizeof(fixed_point_bits));
	g_sw3d_span_vq8 = fixed_point_bits - fixed_point_bias_bits;
	fixed_point_value = right_v + fixed_point_bias;
	memcpy(&fixed_point_bits, &fixed_point_value, sizeof(fixed_point_bits));
	next_vq8 = fixed_point_bits - fixed_point_bias_bits;
	use_specialized_texture_wrap =
		g_sw3d_span_texture_width_shift >=
			SW3D_MIN_SPECIALIZED_TEXTURE_SHIFT &&
		g_sw3d_span_texture_width_shift <=
			SW3D_MAX_SPECIALIZED_TEXTURE_SHIFT &&
		g_sw3d_span_texture_height_shift >=
			SW3D_MIN_SPECIALIZED_TEXTURE_SHIFT &&
		g_sw3d_span_texture_height_shift <=
			SW3D_MAX_SPECIALIZED_TEXTURE_SHIFT;
	if (use_specialized_texture_wrap) {
		texture_width_mask = (1 << g_sw3d_span_texture_width_shift) - 1;
		texture_height_mask =
			(1 << g_sw3d_span_texture_height_shift) - 1;
	} else {
		texture_width_mask = 0;
		texture_height_mask = 0;
	}

	for (;;) {
		if (block <= end_block) {
			boundary_w += w_block_step;
			inverse_w = g_sw3d_span_one_float / boundary_w;
		}

		if (g_flight_bytes_per_pixel == 2 &&
		    !use_specialized_texture_wrap) {
			sw3d_draw_textured_shade_span_generic16bpp();
		} else {
			for (pixel_index = 0; pixel_index < g_sw3d_span_length;
			     ++pixel_index) {
				unsigned int shade_accum;
				int texel_index;
				int texel;

				if (use_specialized_texture_wrap) {
					texel_index =
						(((g_sw3d_span_vq8 >>
						   SW3D_FIXED_POINT_SHIFT) &
						  texture_height_mask)
						 << g_sw3d_span_texture_width_shift) |
						((g_sw3d_span_uq8 >>
						  SW3D_FIXED_POINT_SHIFT) &
						 texture_width_mask);
				} else {
					texel_index =
						((g_sw3d_span_vq8 >>
						  SW3D_FIXED_POINT_SHIFT)
						 << g_sw3d_span_texture_width_shift) +
						(g_sw3d_span_uq8 >>
						 SW3D_FIXED_POINT_SHIFT);
					texel_index &= g_sw3d_span_texel_mask;
				}
				texel = g_sw3d_span_texels[texel_index];
				shade_accum =
					(unsigned int)(g_sw3d_span_shade_q8 +
						       g_sw3d_span_shade_dither_accum);
				g_sw3d_span_shade_dither_accum =
					(uint8_t)shade_accum;
				if (g_flight_bytes_per_pixel == 2) {
					uint16_t *surface16 =
						(uint16_t
							 *)((uint8_t *)
								    g_surface_pixels +
							    g_sw3d_span_framebuffer_row_offset);
					uint16_t *shade_table16 =
						(uint16_t
							 *)(g_sw3d_span_shade_table +
							    SW3D_TRUE_COLOR_SHADE_TABLE_OFFSET);
					surface16[g_sw3d_span_start_x +
						  pixel_index] = shade_table16
						[(((shade_accum >>
						    SW3D_FIXED_POINT_SHIFT) &
						   SW3D_SHADE_LEVEL_MASK)
						  << SW3D_FIXED_POINT_SHIFT) +
						 texel];
				} else {
					uint8_t *surface8 =
						(uint8_t *)g_surface_pixels +
						g_sw3d_span_framebuffer_row_offset +
						g_sw3d_span_start_x;
					surface8[pixel_index] = g_sw3d_span_shade_table
						[(((shade_accum >>
						    SW3D_FIXED_POINT_SHIFT) &
						   SW3D_SHADE_LEVEL_MASK) *
						  SW3D_SHADE_TABLE_LEVEL_STRIDE) +
						 texel];
				}
				g_sw3d_span_shade_q8 +=
					g_sw3d_span_shade_step_q8;
				g_sw3d_span_vq8 += g_sw3d_span_step_vq8;
				g_sw3d_span_uq8 += g_sw3d_span_step_uq8;
			}
		}
		if (block > end_block) {
			break;
		}

		u_numerator += u_numerator_block_step;
		v_numerator += v_numerator_block_step;
		g_sw3d_span_start_x += g_sw3d_span_length;
		boundary_x += g_sw3d_light_sample_block_size;
		if (block == end_block) {
			g_sw3d_span_length = end_x - g_sw3d_span_start_x;
		} else {
			g_sw3d_span_length = g_sw3d_light_sample_block_size;
		}
		++block;
		right_sample = &light_samples[block];
		stamp_delta = g_sw3d_current_light_sample_cache_stamp -
			      right_sample->stamp;
		if (stamp_delta != 0) {
			right_sample->stamp =
				g_sw3d_current_light_sample_cache_stamp;
			if (stamp_delta != 1) {
				right_sample->intensity =
					flight_light_compute_software_face_sample_intensity(
						face, boundary_x, block_start_y,
						boundary_w);
				sample_intensity =
					flight_light_compute_software_face_sample_intensity(
						face, boundary_x,
						block_start_y +
							g_sw3d_light_sample_block_size,
						boundary_w);
			} else {
				sample_intensity =
					flight_light_compute_software_face_sample_intensity(
						face, boundary_x,
						block_start_y +
							g_sw3d_light_sample_block_size,
						boundary_w);
				right_sample->intensity +=
					right_sample->row_delta;
			}
			right_sample->row_delta =
				sample_intensity - right_sample->intensity;
		}
		right_u = inverse_w * u_numerator;
		right_v = inverse_w * v_numerator;
		right_light = right_sample->intensity +
			      g_sw3d_light_sample_subrow_lerp_t *
				      right_sample->row_delta;
		light_intensity_at_end += light_intensity_block_step;
		g_sw3d_span_shade_q8 = end_shade_q8;
		fixed_point_bias = g_sw3d_tex_coord_bias_by_shift[0];
		memcpy(&fixed_point_bias_bits, &fixed_point_bias,
		       sizeof(fixed_point_bias_bits));
		fixed_point_value =
			(right_light + light_intensity_at_end) *
				g_sw3d_light_intensity_to_shade_scale +
			fixed_point_bias;
		memcpy(&fixed_point_bits, &fixed_point_value,
		       sizeof(fixed_point_bits));
		end_shade_q8 = fixed_point_bits - fixed_point_bias_bits;
		if (end_shade_q8 < 0) {
			end_shade_q8 = 0;
		}
		if (end_shade_q8 > SW3D_MAX_SHADE_Q8) {
			end_shade_q8 = SW3D_MAX_SHADE_Q8;
		}
		shade_delta_q8 = end_shade_q8 - g_sw3d_span_shade_q8;
		if (shade_delta_q8 < 0) {
			shade_delta_q8 += g_sw3d_light_sample_block_size;
		}
		g_sw3d_span_shade_step_q8 =
			shade_delta_q8 >> g_sw3d_light_sample_block_shift;
		g_sw3d_span_uq8 = next_uq8;
		g_sw3d_span_vq8 = next_vq8;
		fixed_point_bias = g_sw3d_tex_coord_bias_by_shift
			[g_sw3d_span_texture_width_shift];
		memcpy(&fixed_point_bias_bits, &fixed_point_bias,
		       sizeof(fixed_point_bias_bits));
		fixed_point_value = right_u + fixed_point_bias;
		memcpy(&fixed_point_bits, &fixed_point_value,
		       sizeof(fixed_point_bits));
		next_uq8 = fixed_point_bits - fixed_point_bias_bits;
		fixed_point_bias = g_sw3d_tex_coord_bias_by_shift
			[g_sw3d_span_texture_height_shift];
		memcpy(&fixed_point_bias_bits, &fixed_point_bias,
		       sizeof(fixed_point_bias_bits));
		fixed_point_value = right_v + fixed_point_bias;
		memcpy(&fixed_point_bits, &fixed_point_value,
		       sizeof(fixed_point_bits));
		next_vq8 = fixed_point_bits - fixed_point_bias_bits;
		g_sw3d_span_step_uq8 = (next_uq8 - g_sw3d_span_uq8) >>
				       g_sw3d_light_sample_block_shift;
		g_sw3d_span_step_vq8 = (next_vq8 - g_sw3d_span_vq8) >>
				       g_sw3d_light_sample_block_shift;
	}
}

/* Draws the current piece of a span in 16-bit color for the texture sizes the
 * main loop leaves out: g_sw3d_span_length pixels from g_sw3d_span_start_x, each the
 * 16-bit shade table entry for level ((shade + carry) >> 8) & 15 and the texel
 * at index i = ((v >> 8) << g_sw3d_span_texture_width_shift) + (u >> 8), masked
 * with g_sw3d_span_texel_mask, stepping the shade, v and u. Returns the last v, or
 * g_sw3d_span_start_x + g_sw3d_span_length when it draws nothing; its only caller,
 * sw3d_draw_textured_span, ignores it. */
// FUNCTION: XVT 0x497850
int sw3d_draw_textured_shade_span_generic16bpp(void)
{
	uint8_t *pixel;
	uint8_t *pixel_end;
	uint8_t *shade_table;
	int result;

	pixel = (uint8_t *)g_surface_pixels;
	shade_table = g_sw3d_span_shade_table + 4096;
	pixel += g_sw3d_span_framebuffer_row_offset;
	pixel_end = pixel;
	pixel += 2 * g_sw3d_span_start_x;
	result = g_sw3d_span_start_x + g_sw3d_span_length;
	pixel_end += 2 * result;
	while (pixel < pixel_end) {
		int texel_index;
		int texel;
		unsigned int shade_accum;

		texel_index = ((g_sw3d_span_vq8 >> 8)
			       << g_sw3d_span_texture_width_shift) +
			      (g_sw3d_span_uq8 >> 8);
		texel = g_sw3d_span_texels[texel_index &
					   g_sw3d_span_texel_mask];
		shade_accum = (unsigned int)(g_sw3d_span_shade_q8 +
					     g_sw3d_span_shade_dither_accum);
		g_sw3d_span_shade_dither_accum = (uint8_t)shade_accum;
		*(uint16_t *)pixel = ((uint16_t *)shade_table)
			[(((shade_accum >> 8) & 0xF) << 8) + texel];
		pixel += 2;
		g_sw3d_span_shade_q8 += g_sw3d_span_shade_step_q8;
		result = g_sw3d_span_vq8 + g_sw3d_span_step_vq8;
		g_sw3d_span_vq8 = result;
		g_sw3d_span_uq8 += g_sw3d_span_step_uq8;
	}
	return result;
}

/* Copies columns start_x to end_x - 1 of a row from p_src_raster, which holds that
 * row from column start_x at g_flight_bytes_per_pixel bytes a pixel, onto row scan_y
 * of the flight surface, only where depth sprite_w is nearer than the faces in
 * the row's span list (larger is nearer). A span whose face's
 * min_scaled_inverse_depth is at least sprite_w hides its columns, a face whose
 * max_scaled_inverse_depth is at most sprite_w hides none, and otherwise the column
 * where the face's w crosses sprite_w splits the span. Sets
 * g_sw3d_span_framebuffer_row_offset for the row. */
// FUNCTION: XVT 0x497940
void sw3d_blit_occluded_span(const uint8_t *p_src_raster, int start_x,
			     int end_x, int scan_y, float sprite_w)
{
	struct scene_span *span;
	int draw_x;

	draw_x = start_x;
	if (g_flight_bytes_per_pixel == 2) {
		p_src_raster -= 2 * start_x;
	} else {
		p_src_raster -= start_x;
	}
	g_sw3d_span_framebuffer_row_offset =
		g_flight_bytes_per_pixel * g_flight_vp_x +
		g_surface_pitch * (scan_y + g_flight_vp_y);

	for (span = g_scanline_span_heads[scan_y]; span != NULL;
	     span = span->next) {
		struct scene_face *face;
		int span_end;
		float span_w;
		float delta_x;
		float depth_falloff;

		span_end = span->x_end;
		if (draw_x >= span_end) {
			continue;
		}
		if (span->x_start > draw_x) {
			break;
		}
		face = span->face;
		if (sprite_w <= face->min_scaled_inverse_depth) {
			draw_x = span_end;
			if (end_x <= span_end) {
				return;
			}
		} else if (sprite_w < face->max_scaled_inverse_depth) {
			span_w = (float)scan_y * face->gradients[7] +
				 face->gradients[8];
			span_w = (float)draw_x * face->gradients[6] + span_w;
			if (sprite_w <= span_w) {
				if (face->gradients[6] >= 0.0f) {
					draw_x = span_end;
					if (end_x <= span_end) {
						return;
					}
				} else {
					if (end_x > span_end) {
						delta_x = (float)(span_end -
								  draw_x);
						span_w = delta_x *
								 face->gradients
									 [6] +
							 span_w;
						if (sprite_w <= span_w) {
							draw_x = span_end;
							continue;
						}
					} else {
						delta_x =
							(float)(end_x - draw_x);
						span_w = delta_x *
								 face->gradients
									 [6] +
							 span_w;
						if (sprite_w <= span_w) {
							return;
						}
					}
					depth_falloff = -face->gradients[6];
					draw_x += (int)(delta_x -
							(sprite_w - span_w) /
								depth_falloff);
					if (end_x <= draw_x) {
						return;
					}
				}
			} else if (face->gradients[6] > 0.0f) {
				if (end_x >= span_end) {
					delta_x = (float)(span_end - draw_x);
					span_w = delta_x * face->gradients[6] +
						 span_w;
					if (sprite_w >= span_w) {
						continue;
					}
				} else {
					delta_x = (float)(end_x - draw_x);
					span_w = delta_x * face->gradients[6] +
						 span_w;
					if (sprite_w >= span_w) {
						continue;
					}
				}
				depth_falloff = -face->gradients[6];
				end_x = draw_x +
					(int)(delta_x - (sprite_w - span_w) /
								depth_falloff);
				if (end_x <= draw_x) {
					return;
				}
			}
		}
	}

	while (span != NULL) {
		if (end_x <= span->x_start) {
			break;
		}
		if (sprite_w <= span->face->min_scaled_inverse_depth) {
			sw3d_copy_span_to_framebuffer(p_src_raster, draw_x,
						      span->x_start - draw_x);
			draw_x = span->x_end;
			if (end_x <= draw_x) {
				return;
			}
		} else if (sprite_w < span->face->max_scaled_inverse_depth) {
			float span_w;

			span_w = (float)scan_y * span->face->gradients[7] +
				 span->face->gradients[8];
			span_w = (float)span->x_start *
					 span->face->gradients[6] +
				 span_w;
			if (sprite_w <= span_w) {
				sw3d_copy_span_to_framebuffer(
					p_src_raster, draw_x,
					span->x_start - draw_x);
				draw_x = span->x_end;
				if (span->face->gradients[6] >= 0.0f) {
					if (end_x <= draw_x) {
						return;
					}
				} else if (end_x > draw_x) {
					span_w =
						(float)(draw_x -
							span->x_start) *
							span->face
								->gradients[6] +
						span_w;
					if (sprite_w > span_w) {
						float depth_falloff;

						depth_falloff =
							-span->face
								 ->gradients[6];
						draw_x -= (int)((sprite_w -
								 span_w) /
								depth_falloff);
					}
				} else {
					float depth_falloff;

					span_w =
						(float)(end_x - span->x_start) *
							span->face
								->gradients[6] +
						span_w;
					if (sprite_w <= span_w) {
						return;
					}
					depth_falloff =
						-span->face->gradients[6];
					draw_x = end_x -
						 (int)((sprite_w - span_w) /
						       depth_falloff);
					if (end_x <= draw_x) {
						return;
					}
				}
			} else if (span->face->gradients[6] > 0.0f) {
				if (end_x > span->x_end) {
					span_w =
						(float)(span->x_end -
							span->x_start) *
							span->face
								->gradients[6] +
						span_w;
					if (sprite_w < span_w) {
						float depth_falloff;

						depth_falloff =
							-span->face
								 ->gradients[6];
						sw3d_copy_span_to_framebuffer(
							p_src_raster, draw_x,
							span->x_end -
								(int)((sprite_w -
								       span_w) /
								      depth_falloff) -
								draw_x);
						draw_x = span->x_end;
					}
				} else {
					float delta_x;

					delta_x =
						(float)(end_x - span->x_start);
					span_w =
						delta_x *
							span->face
								->gradients[6] +
						span_w;
					if (sprite_w < span_w) {
						float depth_falloff;

						depth_falloff =
							-span->face
								 ->gradients[6];
						sw3d_copy_span_to_framebuffer(
							p_src_raster, draw_x,
							span->x_start +
								(int)(delta_x -
								      (sprite_w -
								       span_w) /
									      depth_falloff) -
								draw_x);
						return;
					}
				}
			}
		}
		span = span->next;
	}

	sw3d_copy_span_to_framebuffer(p_src_raster, draw_x, end_x - draw_x);
}

/* Copies pixel_count pixels from p_src_raster_base, from its pixel start_x, to
 * column start_x of the row at g_sw3d_span_framebuffer_row_offset in
 * g_surface_pixels, at g_flight_bytes_per_pixel bytes a pixel. Does nothing for a
 * count of 0 or less. Only sw3d_blit_occluded_span calls it. */
// FUNCTION: XVT 0x497D80
void sw3d_copy_span_to_framebuffer(const uint8_t *p_src_raster_base,
				   int start_x, int pixel_count)
{
	if (pixel_count > 0) {
		if (g_flight_bytes_per_pixel == 2) {
			uint8_t *dst = (uint8_t *)g_surface_pixels +
				       g_sw3d_span_framebuffer_row_offset +
				       start_x + start_x;

			p_src_raster_base += 2 * start_x;
			do {
				uint8_t lo = p_src_raster_base[0];
				uint8_t hi = p_src_raster_base[1];
				p_src_raster_base += 2;
				dst[0] = lo;
				dst[1] = hi;
				dst += 2;
			} while (--pixel_count != 0);
		} else {
			uint8_t *dst = (uint8_t *)g_surface_pixels + start_x +
				       g_sw3d_span_framebuffer_row_offset;

			p_src_raster_base += start_x;
			do {
				*dst++ = *p_src_raster_base++;
			} while (--pixel_count != 0);
		}
	}
}
