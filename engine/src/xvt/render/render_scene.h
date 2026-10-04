#ifndef XVT_RENDER_RENDER_SCENE_H
#define XVT_RENDER_RENDER_SCENE_H

#include <stdint.h>

#include "aeron/compat/d3d.h"
#include "aeron/compat/ddraw.h"
#include "aeron/compat/win_types.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One polygon edge as the software renderer scans it, set up by
 * sw3d_setup_clipped_edge; rows are viewport rows. */
struct scene_edge {
	/* Row after the edge's last, capped at g_flight_vp_height. */
	int y_end;
	/* First row the edge covers: its top y rounded up, 0 when above the
	 * viewport. */
	int y_start;
	float x;		    /* Edge's x on row yStart. */
	float light_intensity;	    /* Light level on row yStart. */
	float dxdy;		    /* Change in x per row. */
	float d_light_intensity_dy; /* Change in light level per row. */
	/* The vertex sw3d_setup_clipped_edge made where the edge crosses view
	 * depth 1, set by sw3d_rasterize_mesh_faces; NULL when it made none. */
	struct proj_vertex *p_clip_vert;
};

extern IDirectDrawSurface *g_std3dz_buffer_surface;
extern const float g_render_distant_depth;
extern const float g_render_projection_zero_float;
extern const float g_render_unit_float;
extern const float g_render_texture_uv_half_scale;
extern const float g_inv_depth_proj_scale;
extern const float g_render_triangle_corner_count;
extern const float g_render_quad_corner_count;
extern const float g_render_light_direction_unit_scale;
extern const float g_render_directional_light_intensity_scale;
extern const float g_render_zero_float;
extern const float g_render_ambient_light_intensity;
extern const float g_render_rough_distance_scale;
extern const float g_render_point_light_facing_threshold;
extern const double g_render_half_double;
extern const float g_render_half_float;
extern const float g_render_specular_approx_other_components_scale;
extern const float g_render_specular_approx_max_component_scale;
extern int g_cap_vertex_alpha;
extern int g_d3d_vertex_alpha_state_reset_slot;
extern int g_max_batch_tris;
extern D3DTLVERTEX *g_flight_vertex_buffer;
extern float g_flight_vp_origin_y;
extern struct std3d_render_tri *g_tri_buffer;
extern int g_clip_input_proj_vert_end_index;
extern int g_max_batch_verts;
extern int g_d3d_vertex_count;
extern int g_d3d_triangle_count;
extern float g_flight_vp_origin_x;
extern uint16_t g_scene_span_data_handle;
extern uint16_t g_scene_span_ptr_list_handle;
extern uint16_t g_scene_light_sample_data_handle;
extern uint16_t g_vis_face_list_handle;
extern uint16_t g_proj_vert_list_handle;
extern struct proj_vertex *g_proj_vert_list;
extern int g_proj_vert_count;
extern uint16_t g_scene_edge_list_handle;
extern struct scene_edge *g_scene_edge_list;
extern int g_scene_edge_cursor;
extern uint16_t g_vertex_remap_handle;
extern uint16_t g_scene_edge_flags_handle;
extern int *g_scene_edge_flags;
extern int g_vertex_remap_capacity;
extern int g_scene_edge_flags_capacity;
extern uint16_t g_scene_scl_edge_list_handle;
extern uint16_t g_scanline_span_heads_handle;
extern struct scene_span **g_scanline_span_heads;
extern uint16_t g_mesh_queue_handle;
extern int g_scene_span_data_capacity;
extern struct scene_span *g_p_scene_span_data_cur;
extern struct scene_span *g_p_scene_span_data_end;
extern struct scene_span **g_scene_span_ptr_list;
extern int g_scene_span_ptr_avail;
extern struct scene_face *g_vis_face_list;
extern int g_render_scene_reset_pending;
extern int g_vis_face_count;
extern int g_vis_face_pass_start;
extern int *g_vertex_remap;
extern struct opt_vector g_mesh_eye_pos;
extern uint8_t *g_active_rgb565_to_palette_index_lut;
extern uint8_t g_default_white_texture_rgb24[8 * 8 * 3];

static inline unsigned int
render_scene_get_memory_handle(const uint16_t *handle)
{
	return *handle;
}

/* One mesh vertex carried into the viewport, as the projection functions
 * write it to g_proj_vert_list; render_clip_vertex has the same layout. */
struct proj_vertex {
	/* Viewport x in pixels; view-space x while scaled_inverse_depth is
	 * negative. */
	float sx;
	/* Viewport y in pixels; view-space y while that is negative. */
	float sy;
	/* g_proj_scale_int over the view depth; for a vertex closer than depth 1,
	 * the depth minus 1, which is negative. */
	float scaled_inverse_depth;
	/* Light level, 0 to 1, from render_scene_compute_vertex_lighting. */
	float light_intensity;
	float tu; /* Horizontal texture coordinate. */
	float tv; /* Vertical texture coordinate. */
};

/* One mesh of an object's model on its way to the screen: the walk of the
 * model's nodes fills it, and the draw functions queue a copy. */
struct scene_mesh {
	struct object_record *p_object; /* Object the model belongs to. */
	/* Radians the next rotate-and-scale node turns this part by, from the
	 * craft's mesh_rotation byte; 0 for none. */
	float rot_angle;
	/* Position of the model's origin in view space; with view_orient,
	 * math3d_rotate_vec3 then adding this carries a model point to view
	 * space. */
	float view_pos_x;
	float view_pos_y; /* y of that position. */
	float view_pos_z; /* z of that position: depth ahead of the eye. */
	/* Model-to-view rotation, scaled by scale nodes. */
	float view_orient[9];
	/* Eye position in model space, used to cull faces turned away. */
	float eye_model_space_x;
	float eye_model_space_y; /* y of that position. */
	float eye_model_space_z; /* z of that position. */
	/* View-to-model rotation, the inverse of view_orient. */
	float view_to_model_orient[9];
	/* Nothing reads it. */
	int base_color_and_materials
		[4]; ///< Elements 0-2 come from the OPT_BASE_COLOR payload; element 3 receives
	///< g_cur_mesh_materials from OPT_MATERIAL_BINDING for selectors outside 5-8.
	int vertex_count;		  /* Vertices in p_model_verts. */
	struct opt_vector *p_model_verts; /* Vertex positions in model space. */
	struct opt_tex_coord
		*p_uvs; /* Texture coordinates, by face uv index. */
	/* Vertex normals: the model's own, else those that follow the face
	 * data. */
	struct opt_vector *p_vert_normals;
	/* Nothing reads it. */
	int per_vertex_materials; ///< Receives g_cur_mesh_materials when OPT_MATERIAL_BINDING payload_count is 7 or 8;
				  ///< no downstream consumer is identified.
	int face_count;		  /* Faces in p_face_geom. */
	int edge_count;		  /* Edges the faces share. */
	struct opt_vector *p_face_normals; /* One normal per face. */
	struct face_texture_gradients
		*p_face_texturing; /* Texture axes per face. */
	/* Nothing reads it. */
	int per_face_materials; ///< Receives g_cur_mesh_materials when OPT_MATERIAL_BINDING payload_count is 5 or 6;
	///< no downstream consumer is identified.
	/* Corner, uv, normal and edge indices per face. */
	struct face_record *p_face_geom;
	/* Name of the texture node; render_scene_draw_mesh_faces sets its first
	 * character to '_' when no color-key texture could be made for it. */
	char *p_texture_name;
	void *p_material; /* The texture's opt_texture_data header. */
	void *p_texels;	  /* Texels, just after that header. */
	void *p_palette;  /* The texture's palette and shade tables. */
	/* pPalette plus 4096 bytes, read as 16-bit colors. */
	uint16_t *p_color_key_palette;
	/* Index of the mesh's first face in g_vis_face_list. */
	int face_base_index;
	/* Index of its first vertex in g_proj_vert_list. */
	int vert_base_index;
	/* g_scene_edge_cursor when sw3d_rasterize_mesh_faces began; nothing reads
	 * it. */
	int edge_base_index;
	int vis_face_count; /* Faces that passed the cull, from face_base_index. */
	/* Vertices projected so far, from vert_base_index, near-clip vertices
	 * of the software renderer included. */
	int proj_vert_cursor;
	/* Edges sw3d_rasterize_mesh_faces has written for the mesh. */
	int emitted_edge_count;
};

/* One face that passed the cull, in g_vis_face_list. */
struct scene_face {
	int face_index;		   /* Index of the face in its mesh. */
	struct scene_mesh *p_mesh; /* The queued mesh it belongs to. */
	/* faceIndex plus g_cur_layer_id << 16; nothing reads it. */
	int face_and_layer_id;
	/* -1 when a corner is closer than view depth 1 and the face needs the
	 * near clip; the draw then sets it to g_flight_vp_height. */
	int near_clip_state;
	/* After projection, three planes in viewport x and y, each as the x
	 * factor, the y factor and the constant: u over view depth, v over
	 * view depth, and 1 over view depth. Before that, the face's u and v
	 * axes in view space and its texture origin. */
	float gradients[9];
	float span_light_intensity_dx; /* Change in light level per pixel. */
	/* The left edge sw3d_scan_convert_face is filling spans from; NULL from
	 * the cull until then. */
	struct scene_edge *p_scan_edge;
	/* The face's row of 12-byte light samples in g_scene_light_sample_data. */
	void *p_light_samples;
	int y_top; /* First row of the face's spans. */
	int y_bot; /* Row after its last. */
	/* Largest scaled_inverse_depth of its corners; a corner closer than depth
	 * 1 counts as g_proj_scale_int. */
	float max_scaled_inverse_depth;
	float min_scaled_inverse_depth; /* Smallest, counted the same way. */
	/* Its edges as sw3d_rasterize_mesh_faces set them. */
	struct scene_edge *edges[5];
	int edge_count; /* Edges in edges. */
	/* One span pointer per row from yTop, taken from g_scene_span_ptr_list. */
	struct scene_span **p_spans;
	/* Estimated texels per viewport pixel, times 256, for choosing a mip
	 * level. */
	int texels_per_pixel_q8;
};

/* One run of pixels on a viewport row that a single face covers, in the
 * software renderer's per-row lists. */
struct scene_span {
	struct scene_span *next;    /* Next span on the row; NULL at the end. */
	int x_start;		    /* First pixel. */
	int x_end;		    /* Pixel after the last. */
	float light_intensity;	    /* Light level at xStart. */
	float d_light_intensity_dx; /* Change in light level per pixel. */
	/* The face drawn there; g_sw3d_cockpit_mask_sentinel_face for a run the
	 * cockpit covers. */
	struct scene_face *face;
};

static __inline float render_scene_add_translation(float position,
						   float translation)
{
	return position + translation;
}

void render_scene_project_mesh_vertices(struct scene_mesh *mesh);
void render_scene_project_distant_mesh_vertices(struct scene_mesh *mesh);
void render_scene_draw_mesh_faces(const struct scene_mesh *mesh);
void render_scene_draw_mesh_hardware(const struct scene_mesh *mesh);
void render_scene_init_hardware_frame(void);
extern int g_scene_flush_draw_target_markers;
void render_scene_flush_geometry(void);
int render_scene_emit_flight_vertex(int vertex_index,
				    const struct render_clip_vertex *vertices,
				    const struct scene_face *face);
void nullsub_2(void);
void std3d_fill_z_buffer_from_viewport_mask(void);
int render_scene_clear_frame_buffers(void);
void std3d_detach_and_release_z_buffer_surface(void);
void render_scene_compute_vertex_lighting(const struct scene_mesh *mesh,
					  struct proj_vertex *out_vert,
					  const struct opt_vector *normal,
					  const struct opt_vector *pos,
					  const struct opt_vector *eye_pos);
void render_scene_transform_face_texture_gradients(
	struct scene_face *face,
	const struct face_texture_gradients *face_tex_gradients,
	const float *view_pos_and_orient);
void render_scene_transform_project_legacy_point(
	float out_projected[3], const float point[3],
	const float view_pos_and_orient[12]);
void render_scene_transform_project_legacy_distant_point(
	float out_projected[3], const float point[3],
	const float view_pos_and_orient[12]);
void render_scene_cull_mesh_faces_from_view(struct scene_mesh *mesh);
void render_scene_draw_scene_mesh(const struct scene_mesh *mesh);
void render_scene_apply_bwing_bridge_rotation(
	const struct optimized_poly_object *unused_model,
	const struct object_record *obj, struct scene_mesh *mesh,
	int bridge_mesh_index);
void render_scene_draw_object_model(struct object_record *obj);
void render_scene_draw_selected_root_node(struct object_record *obj,
					  int root_node_index);
void render_scene_draw_model_node(struct optimized_poly_object *model,
				  struct opt_node *node,
				  struct scene_mesh *mesh);
void render_scene_toggle_vertex_light_occlusion(void);
int render_scene_get_vertex_light_occlusion_enabled(void);
int render_scene_is_segment_occluded_by_object_model(
	struct object_record *object, const struct opt_vector *segment_start,
	const struct opt_vector *segment_end);
int render_scene_test_segment_against_model_node(
	struct optimized_poly_object *model, const struct opt_node *node,
	struct scene_mesh *mesh, const struct opt_vector *segment_start,
	const struct opt_vector *segment_end);
int render_scene_test_segment_against_mesh_faces(
	const struct scene_mesh *mesh, const struct opt_vector *segment_start,
	const struct opt_vector *segment_end);
void render_scene_allocate_buffers(void);
void render_scene_initialize(int reset_scene_state);
int render_scene_unlock_buffers(void);
void render_scene_free_buffers(void);

#ifdef __cplusplus
}
#endif

#endif
