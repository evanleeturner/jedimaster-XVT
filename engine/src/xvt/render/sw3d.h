#ifndef XVT_RENDER_SW3D_H
#define XVT_RENDER_SW3D_H

#include <stdint.h>

#include "xvt/assets/opt_model.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int g_sw3d_skip_odd_scanlines;

extern struct proj_vertex *g_sw3d_generated_clip_vertex;
extern struct proj_vertex *g_sw3d_latest_clip_vertex;
extern struct proj_vertex *g_sw3d_previous_clip_vertex;
extern int g_sw3d_light_sample_block_size;
extern int g_sw3d_light_sample_block_mask;
extern float g_sw3d_light_sample_inv_block_size;
extern float g_sw3d_light_sample_block_size_float;
extern int g_sw3d_light_sample_block_shift;
extern uint32_t g_sw3d_fpu_control_word_scratch;
extern uint32_t g_sw3d_initialize_scene_saved_fpu_control;
extern int g_sw3d_light_sample_cache_scene_stamp_base;
extern struct scene_face g_sw3d_cockpit_mask_sentinel_face;
extern struct scene_face *g_sw3d_current_face;
extern int g_sw3d_current_scanline_y;

struct face_texture_gradients {
	/* The face's texture u axis in model space;
	 * render_scene_transform_face_texture_gradients turns it into view space as
	 * gradients 0 to 2. */
	struct opt_vector u_axis;
	/* The texture v axis, the same way, as gradients 3 to 5. */
	struct opt_vector v_axis;
};

extern int g_sw3d_span_framebuffer_row_offset;
extern int g_sw3d_span_shade_dither_accum;
extern int g_sw3d_span_vq8;
extern int g_sw3d_span_uq8;
extern int g_sw3d_span_shade_step_q8;
extern int g_sw3d_span_length;
extern int g_sw3d_span_start_x;
extern int g_sw3d_span_texture_width_shift;
extern uint8_t *g_sw3d_span_shade_table;
extern uint8_t *g_sw3d_span_texels;
extern int g_sw3d_span_texel_mask;
extern int g_sw3d_span_shade_q8;
extern int g_sw3d_span_step_vq8;
extern int g_sw3d_span_step_uq8;

void sw3d_project_mesh_vertices(struct scene_mesh *mesh);
void sw3d_project_mesh_vertices_distant(struct scene_mesh *mesh);
void sw3d_rasterize_mesh_faces(struct scene_mesh *mesh);
void sw3d_scan_convert_face(struct scene_face *face);
int sw3d_setup_clipped_edge(struct scene_mesh *mesh, struct scene_edge *edge,
			    const struct proj_vertex *first,
			    const struct proj_vertex *second);
int sw3d_setup_edge(struct scene_edge *edge, const struct proj_vertex *first,
		    const struct proj_vertex *second);
void sw3d_draw_visible_faces_to_surface(void);
void sw3d_draw_textured_span(int start_x, int end_x, float span_start_w);
void sw3d_insert_span(float x_left, float x_right, int scan_y,
		      struct scene_face *face);
int sw3d_draw_textured_shade_span_generic16bpp(void);
void sw3d_blit_occluded_span(const uint8_t *p_src_raster, int start_x,
			     int end_x, int scan_y, float sprite_w);
void sw3d_copy_span_to_framebuffer(const uint8_t *p_src_raster_base,
				   int start_x, int pixel_count);

#ifdef __cplusplus
}
#endif

#endif
