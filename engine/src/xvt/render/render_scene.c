#include "xvt/render/render_scene.h"
#ifdef XVT_MODERN
#include "aeron/compat/host.h"
#include "xvt_runtime/assets/opt_native.h"
#endif

#include <string.h>

#include "xvt/assets/model_mesh.h"
#include "xvt/assets/model_preview.h"
#include "xvt/assets/model_texture.h"
#include "xvt/assets/opt_model.h"
#include "xvt/flight/craft.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight_display.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/targeting.h"
#include "xvt/flight/transfm2.h"
#include "xvt/math/math.h"
#include "xvt/math/math3d.h"
#include "xvt/render/flight_light.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/render_clip.h"
#include "xvt/render/render_texture.h"
#include "xvt/render/renderer.h"
#include "xvt/render/scene_billboard.h"
#include "xvt/render/std3d.h"
#include "xvt/render/sw3d.h"
#include "xvt/util/debug_console.h"
#include "xvt/util/memory.h"
#ifndef XVT_MODERN
#include <float.h>
#endif

enum {
	OPT_INDEXED_SHADE_TABLE_SIZE = 4096,
	DEFAULT_WHITE_TEXTURE_DIMENSION = 8,
	DEFAULT_WHITE_TEXTURE_RGB_SIZE = DEFAULT_WHITE_TEXTURE_DIMENSION *
					 DEFAULT_WHITE_TEXTURE_DIMENSION * 3,
	B_WING_OBJECT_TYPE = 4,
	COMPONENT_OBJECT_TYPE = 89,
};

/* 1 while flight_view_render calls sw3d_draw_visible_faces_to_surface, so that
 * render_scene_flush_geometry, which that calls in the hardware path, also draws
 * the queued target markers and target boxes; else 0. Only flight_view_render
 * writes it. */
// GLOBAL: XVT 0x51A520
int g_scene_flush_draw_target_markers = 0;

/* Index of the B-wing model's bridge mesh, which render_scene_draw_object_model
 * looks up with model_mesh_find_bridge_index when it draws a craft of object type
 * 4, and keeps once found; -1 until then. Only that function writes it. */
// GLOBAL: XVT 0x5272A0
static int g_bwing_bridge_mesh_index_cache = -1;
/* 1 when render_scene_compute_vertex_lighting asks whether the object's own model
 * blocks a light from a vertex. Starts at 0; only
 * render_scene_toggle_vertex_light_occlusion changes it, and nothing calls that, so
 * the test never runs. */
// GLOBAL: XVT 0x5272A4
static int g_vertex_light_occlusion_enabled;
/* Radians per unit of a craft's mesh_rotation byte: 2 pi over 256, to float
 * precision. */
// GLOBAL: XVT 0x5181D0
static const float g_mesh_rotation_byte_to_radians_scale = 0.024543673f;
/* 1 over 32767: turns the Q15 camera and object matrices into floats. */
// GLOBAL: XVT 0x5181D8
static const float g_render_matrix_q15_to_float_scale = 0.000030518509f;
/* 1 over 32768: turns a rotate-and-scale node's axis into floats. */
// GLOBAL: XVT 0x5181DC
static const float g_opt_axis_q15_to_float_scale = 0.000030517578125f;
/* 100000: render_scene_project_distant_mesh_vertices adds it to every vertex's
 * depth and multiplies the projection by it. */
// GLOBAL: XVT 0x51807C
const float g_render_distant_depth = 100000.0f;
/* 0.0; render_scene_project_mesh_vertices compares a face's texture area with it
 * to take its magnitude. */
// GLOBAL: XVT 0x518054
const float g_render_projection_zero_float = 0.0f;
/* 1.0: the view depth of the near plane in render_scene_project_mesh_vertices and
 * render_clip_clip_poly_near, and a constant 1 elsewhere. */
// GLOBAL: XVT 0x518060
const float g_render_unit_float = 1.0f;
/* 0.5: render_scene_draw_mesh_faces scales a texture coordinate by it once for
 * each doubling that makes a texture square. */
// GLOBAL: XVT 0x518070
const float g_render_texture_uv_half_scale = 0.5f;
/* 3.0, divided by the sum of a triangle's corner scaled_inverse_depth values to
 * give the reciprocal of their mean. */
// GLOBAL: XVT 0x518074
const float g_render_triangle_corner_count = 3.0f;
/* 4.0, divided by the sum of a quad's corner scaled_inverse_depth values to give
 * the reciprocal of their mean. */
// GLOBAL: XVT 0x518078
const float g_render_quad_corner_count = 4.0f;
/* 1 over 2048: render_scene_emit_flight_vertex writes 1 / (depth * this + 1) as a
 * vertex's z-buffer value. */
// GLOBAL: XVT 0x518080
const float g_inv_depth_proj_scale = 0.00048828125f;
/* 1 over 32768: turns the Q15 light direction in g_object_light_direction_x, Y and
 * Z into floats. */
// GLOBAL: XVT 0x518098
const float g_render_light_direction_unit_scale = 0.000030517578125f;
/* 0.8: render_scene_compute_vertex_lighting multiplies the directional light's dot
 * product with the normal by it. */
// GLOBAL: XVT 0x51809C
const float g_render_directional_light_intensity_scale = 0.80000001f;
/* 0.0 for the lighting code: in render_scene_compute_vertex_lighting a point light
 * adds to a vertex only when its contribution is over it. */
// GLOBAL: XVT 0x5180A0
const float g_render_zero_float = 0.0f;
/* 0.4. Nothing reads it: render_scene_compute_vertex_lighting writes the same 0.4
 * as a literal when directional light is off. */
// GLOBAL: XVT 0x5180A4
const float g_render_ambient_light_intensity = 0.40000001f;
/* 0.2941: the lighting code's rough distance is the largest component of the
 * offset plus the other two times this, in place of a square root. */
// GLOBAL: XVT 0x5180A8
const float g_render_rough_distance_scale = 0.29409999f;
/* -0.3: in the hardware path a point light lights a vertex only when the dot
 * product of the normal and the offset to the light, over the rough distance,
 * is not under this. */
// GLOBAL: XVT 0x5180AC
const float g_render_point_light_facing_threshold = -0.30000001f;
/* 0.5: in the hardware path render_scene_compute_vertex_lighting puts half the
 * rough distance in place of the normal's dot product with the offset to a
 * light. */
// GLOBAL: XVT 0x5180B0
const double g_render_half_double = 0.5;
/* 0.5 for the lighting code: render_scene_compute_vertex_lighting halves the
 * normal's dot product with the specular half vector by it, and adds no
 * specular light when the resulting cosine is under it. */
// GLOBAL: XVT 0x5180B8
const float g_render_half_float = 0.5f;
/* 0.1936: weight of the two smaller components in the rough length of the
 * specular half vector. */
// GLOBAL: XVT 0x5180BC
const float g_render_specular_approx_other_components_scale = 0.1936f;
/* 0.4632: weight of the largest component in the rough length of the specular
 * half vector. */
// GLOBAL: XVT 0x5180C0
const float g_render_specular_approx_max_component_scale = 0.4632f;
/* The z-buffer surface attached to g_flight_back_buffer: renderer_init_d3d_device
 * gets it; std3d_detach_and_release_z_buffer_surface releases it and sets it to
 * NULL. */
// GLOBAL: XVT 0xA68748
IDirectDrawSurface *g_std3dz_buffer_surface;
/* 1 from render_scene_init_hardware_frame until the first mesh face or rotated
 * sprite of the frame is queued. While it is 1, render_scene_emit_flight_vertex
 * gives vertices alpha 0xFE instead of 0xFF, render_quad_draw_rotated_sprite gives
 * its sprite the color 0xFEFFFFFF, and render_scene_draw_mesh_faces adds the
 * alpha-blend flag to its first triangle; those two then set it to 0. */
// GLOBAL: XVT 0x51A54C
int g_cap_vertex_alpha = 1;
/* Set to 0 by render_scene_init_hardware_frame; nothing reads it. */
// GLOBAL: XVT 0x51A550
int g_d3d_vertex_alpha_state_reset_slot = 0;
/* Triangles g_tri_buffer may hold before a batch is drawn, at most 256;
 * render_scene_init_hardware_frame sets it from the span buffer's size and the
 * device's buffer size. */
// GLOBAL: XVT 0x52F868
int g_max_batch_tris = 0;
/* Vertices of the hardware path's current batch; render_scene_init_hardware_frame
 * points it at the start of the span buffer, g_scene_span_data_base. */
// GLOBAL: XVT 0x53F970
D3DTLVERTEX *g_flight_vertex_buffer = NULL;
/* Screen y of the viewport's top in the display mode: g_flight_vp_y plus half of
 * g_display_mode_height minus g_surface_height; added to every emitted vertex.
 * Only render_scene_init_hardware_frame writes it. */
// GLOBAL: XVT 0x53F974
float g_flight_vp_origin_y = 0.0f;
/* Triangles of the hardware path's current batch; render_scene_init_hardware_frame
 * points it at the second half of the span buffer. */
// GLOBAL: XVT 0x54F988
struct std3d_render_tri *g_tri_buffer = NULL;
/* The mesh's vert_base_index (0 in the hardware path) plus its projected vertex
 * count: render_scene_draw_mesh_faces starts g_clip_vert_cursor here, and emits a
 * vertex below it once per mesh but a clip vertex once per use. Only that
 * function writes it. */
// GLOBAL: XVT 0x54F98C
int g_clip_input_proj_vert_end_index = 0;
/* Vertices g_flight_vertex_buffer may hold before a batch is drawn: at most 256
 * and the device's max_vertex_count; render_scene_init_hardware_frame sets it from
 * the span buffer's size. */
// GLOBAL: XVT 0x54F990
int g_max_batch_verts = 0;
/* Vertices in g_flight_vertex_buffer for the current batch. Many functions write
 * it, chiefly render_scene_emit_flight_vertex, which adds one;
 * render_scene_init_hardware_frame and a batch drawn early in
 * render_scene_draw_mesh_hardware set it to 0. */
// GLOBAL: XVT 0x54F994
int g_d3d_vertex_count = 0;
/* Triangles in g_tri_buffer for the current batch. Many functions write it,
 * chiefly render_scene_draw_mesh_faces; render_scene_init_hardware_frame and a batch
 * drawn early in render_scene_draw_mesh_hardware set it to 0. */
// GLOBAL: XVT 0x54F9A0
int g_d3d_triangle_count = 0;
/* Screen x of the viewport's left in the display mode: g_flight_vp_x plus half of
 * g_display_mode_width minus g_surface_width; added to every emitted vertex. Only
 * render_scene_init_hardware_frame writes it. */
// GLOBAL: XVT 0x54F9A4
float g_flight_vp_origin_x = 0.0f;
/* Locked memory of g_scene_span_data_handle, g_scene_span_data_capacity spans; the
 * hardware path also keeps its vertex and triangle buffers there.
 * render_scene_initialize locks it; render_scene_unlock_buffers sets it to
 * NULL. */
// GLOBAL: XVT 0x999420
static struct scene_span *g_scene_span_data_base = NULL;
/* Nothing writes it, so it stays 0 and the distant-mesh paths that test it
 * (render_scene_project_distant_mesh_vertices, sw3d_project_mesh_vertices_distant and
 * the far eye in render_scene_cull_mesh_faces_from_view) never run. */
// GLOBAL: XVT 0x5270B4
static uint8_t g_b_backdrop_mesh_mode = 0;
/* Counter raised before each node the model walk visits;
 * render_scene_cull_mesh_faces_from_view puts it in face_and_layer_id, which nothing
 * reads. Nothing resets it. */
// GLOBAL: XVT 0x5271D4
static int g_cur_layer_id = 0;
/* Header of the built-in white texture a mesh with no texture node uses: NULL
 * until the first render_scene_draw_object_model or
 * render_scene_draw_selected_root_node fills g_default_white_texture and points it
 * there. */
// GLOBAL: XVT 0x5271D8
static struct opt_texture_data *g_default_white_texture_desc_ptr = NULL;
/* The built-in 8x8 white texture as 24-bit color, every byte 0xFF.
 * g_default_white_texture's texels are built from it, and
 * model_texture_load_rgb_or_tex_file uses it when a texture file does not open. */
// GLOBAL: XVT 0x5271E0
uint8_t g_default_white_texture_rgb24[DEFAULT_WHITE_TEXTURE_RGB_SIZE] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};
/* The texture of the last texture node the model walk met, given to a mesh with
 * none of its own; each model's draw starts it at
 * g_default_white_texture_desc_ptr. */
// GLOBAL: XVT 0x60F1EC
static struct opt_texture_data *g_cur_texture_desc = NULL;
/* The built-in white texture: an 8x8 header with 16 inline palette entries and
 * texels built from g_default_white_texture_rgb24 on first use. */
// GLOBAL: XVT 0x60F210
static struct model_texture_default_texture g_default_white_texture = {0};
/* Next row of g_scene_light_sample_data that render_scene_cull_mesh_faces_from_view
 * gives a face, 0 to 199; it stops rising at 199, so later faces share that
 * row. render_scene_initialize sets it to 0 on a reset. */
// GLOBAL: XVT 0x999446
static int g_light_sample_slot_index = 0;
/* Light samples per row: g_flight_vp_width over g_sw3d_light_sample_block_size,
 * rounded up; set by render_scene_initialize. */
// GLOBAL: XVT 0x999410
static int g_light_sample_slot_stride = 0;
/* Memory handle of the span buffer; render_scene_allocate_buffers allocates it,
 * render_scene_free_buffers frees it and sets it to 0. */
// GLOBAL: XVT 0x999424
uint16_t g_scene_span_data_handle = 0;
/* Spans the span buffer holds: 20000, set by render_scene_allocate_buffers. */
// GLOBAL: XVT 0x999426
int g_scene_span_data_capacity = 0;
/* Next free span in g_scene_span_data_base. On a reset render_scene_initialize
 * starts it at the buffer's start and takes the cockpit's spans;
 * sw3d_insert_span takes one per span and, once it reaches g_p_scene_span_data_end,
 * keeps handing out the span just before it. */
// GLOBAL: XVT 0x99942A
struct scene_span *g_p_scene_span_data_cur = NULL;
/* The buffer's last span, g_scene_span_data_capacity - 1; set by
 * render_scene_initialize on a reset. */
// GLOBAL: XVT 0x99942E
struct scene_span *g_p_scene_span_data_end = NULL;
/* Locked memory of g_scene_span_ptr_list_handle, 20000 span pointers;
 * sw3d_scan_convert_face gives each face one per row from the end, through
 * g_scene_span_ptr_avail. */
// GLOBAL: XVT 0x999432
struct scene_span **g_scene_span_ptr_list = NULL;
/* Memory handle of g_scene_span_ptr_list; allocated by
 * render_scene_allocate_buffers, freed by render_scene_free_buffers. */
// GLOBAL: XVT 0x999436
uint16_t g_scene_span_ptr_list_handle = 0;
/* Pointers g_scene_span_ptr_list holds: 20000, set by
 * render_scene_allocate_buffers. */
// GLOBAL: XVT 0x999438
static int g_scene_span_ptr_capacity = 0;
/* Pointers still free at the front of g_scene_span_ptr_list; sw3d_scan_convert_face
 * lowers it by a face's row count when the count is under it.
 * render_scene_initialize sets it to the capacity on a reset. */
// GLOBAL: XVT 0x99943C
int g_scene_span_ptr_avail = 0;
/* Locked memory of g_scene_light_sample_data_handle: 0x25800 bytes of 12-byte light
 * samples, one row per face slot. */
// GLOBAL: XVT 0x999440
static uint8_t *g_scene_light_sample_data = NULL;
/* Memory handle of g_scene_light_sample_data; allocated by
 * render_scene_allocate_buffers, freed by render_scene_free_buffers. */
// GLOBAL: XVT 0x999444
uint16_t g_scene_light_sample_data_handle = 0;
/* Locked memory of g_vis_face_list_handle: up to 5000 faces that passed the cull,
 * appended by render_scene_cull_mesh_faces_from_view. */
// GLOBAL: XVT 0x99944A
struct scene_face *g_vis_face_list = NULL;
/* 1 when the next render_scene_initialize from flight_map_draw_object_pass should
 * reset the scene: flight_map_render_view sets it, and flight_map_draw_object_pass
 * clears it after that call. */
// GLOBAL: XVT 0x55635C
int g_render_scene_reset_pending = 0;
/* Faces in g_vis_face_list. render_scene_cull_mesh_faces_from_view adds each that
 * passes; render_scene_draw_mesh_hardware puts it back after each mesh, as that
 * path draws at once; render_scene_initialize sets it to 0 on a reset. */
// GLOBAL: XVT 0x999450
int g_vis_face_count = 0;
/* First face of the current pass in g_vis_face_list, where
 * sw3d_draw_visible_faces_to_surface starts: render_scene_initialize sets it to 0 on
 * a reset, else to g_vis_face_count. */
// GLOBAL: XVT 0x999454
int g_vis_face_pass_start = 0;
/* Faces g_vis_face_list holds: 5000, set by render_scene_allocate_buffers; a mesh
 * that would pass it is not drawn. */
// GLOBAL: XVT 0x999458
static int g_scene_face_max = 0;
/* Memory handle of g_vis_face_list; allocated by render_scene_allocate_buffers,
 * freed by render_scene_free_buffers. */
// GLOBAL: XVT 0x99944E
uint16_t g_vis_face_list_handle = 0;
/* Locked memory of g_proj_vert_list_handle: the meshes' projected vertices,
 * g_proj_vert_max of them. */
// GLOBAL: XVT 0x99945C
struct proj_vertex *g_proj_vert_list = NULL;
/* Vertices in use in g_proj_vert_list. Many functions write it, chiefly the
 * projection functions, which add each mesh's count; render_scene_draw_scene_mesh
 * and render_scene_draw_mesh_hardware set it to 0 before each mesh. */
// GLOBAL: XVT 0x999462
int g_proj_vert_count = 0;
/* Vertices g_proj_vert_list holds: twice g_vertex_remap_capacity, set by
 * render_scene_allocate_buffers; a mesh with more vertices is not drawn. */
// GLOBAL: XVT 0x999466
static int g_proj_vert_max = 0;
/* Memory handle of g_proj_vert_list; allocated by render_scene_allocate_buffers,
 * freed by render_scene_free_buffers. */
// GLOBAL: XVT 0x999460
uint16_t g_proj_vert_list_handle = 0;
/* Locked memory of g_scene_edge_list_handle: g_scene_edge_max edges the software
 * renderer writes. render_scene_draw_mesh_faces borrows it as a table of the
 * emitted index of each projected vertex. */
// GLOBAL: XVT 0x99946A
struct scene_edge *g_scene_edge_list = NULL;
/* Memory handle of g_scene_edge_list, allocated for g_scene_edge_max edges by
 * render_scene_allocate_buffers; freed by render_scene_free_buffers. */
// GLOBAL: XVT 0x99946E
uint16_t g_scene_edge_list_handle = 0;
/* Edges in use in g_scene_edge_list: sw3d_rasterize_mesh_faces adds each mesh's,
 * and render_scene_draw_scene_mesh and render_scene_draw_mesh_hardware set it to 0
 * before each mesh. */
// GLOBAL: XVT 0x999470
int g_scene_edge_cursor = 0;
/* Edges g_scene_edge_list holds: twice g_scene_edge_flags_capacity, set by
 * render_scene_allocate_buffers; a mesh with more edges is not drawn. */
// GLOBAL: XVT 0x999474
static int g_scene_edge_max = 0;
/* Locked memory of g_vertex_remap_handle: for each model vertex of the mesh being
 * projected, its index among the mesh's projected vertices, -1 until
 * projected. */
// GLOBAL: XVT 0x999478
int *g_vertex_remap = NULL;
/* Memory handle of g_vertex_remap; allocated by render_scene_allocate_buffers,
 * freed by render_scene_free_buffers. */
// GLOBAL: XVT 0x99947C
uint16_t g_vertex_remap_handle = 0;
/* Entries g_vertex_remap is allocated with: the most vertices of any mesh the
 * model loading code (opt_model.c) has measured; fe_disk_io_load_resources sets it
 * to 0 before loading. */
// GLOBAL: XVT 0x99947E
int g_vertex_remap_capacity = 0;
/* Locked memory of g_scene_edge_flags_handle: for each model edge, the index of
 * the scene edge sw3d_rasterize_mesh_faces made for it, -1 before it is set up
 * and -2 when it was rejected. */
// GLOBAL: XVT 0x999482
int *g_scene_edge_flags = NULL;
/* Memory handle of g_scene_edge_flags; allocated by render_scene_allocate_buffers,
 * freed by render_scene_free_buffers. */
// GLOBAL: XVT 0x999486
uint16_t g_scene_edge_flags_handle = 0;
/* Entries g_scene_edge_flags is allocated with: the most edges of any mesh the
 * model loading code has measured; fe_disk_io_load_resources sets it to 0 before
 * loading. */
// GLOBAL: XVT 0x999488
int g_scene_edge_flags_capacity = 0;
/* Locked memory of g_scene_scl_edge_list_handle, 768 edge pointers; nothing reads
 * it. */
// GLOBAL: XVT 0x99948C
static struct scene_edge **g_scene_scl_edge_list = NULL;
/* Memory handle of g_scene_scl_edge_list; allocated by
 * render_scene_allocate_buffers, freed by render_scene_free_buffers. */
// GLOBAL: XVT 0x999490
uint16_t g_scene_scl_edge_list_handle = 0;
/* Locked memory of g_scanline_span_heads_handle: for each viewport row, up to 768,
 * the first span of the software renderer's list. render_scene_initialize fills
 * it on a reset with the spans the cockpit covers. */
// GLOBAL: XVT 0x999492
struct scene_span **g_scanline_span_heads = NULL;
/* Memory handle of g_scanline_span_heads; allocated by
 * render_scene_allocate_buffers, freed by render_scene_free_buffers. */
// GLOBAL: XVT 0x999496
uint16_t g_scanline_span_heads_handle = 0;
/* Locked memory of g_mesh_queue_handle: 500 scene_mesh copies, where the draw
 * functions keep each mesh while its faces sit in g_vis_face_list. */
// GLOBAL: XVT 0x999498
static struct scene_mesh *g_mesh_queue = NULL;
/* Memory handle of g_mesh_queue; allocated by render_scene_allocate_buffers, freed
 * by render_scene_free_buffers. */
// GLOBAL: XVT 0x99949C
uint16_t g_mesh_queue_handle = 0;
/* Entries g_mesh_queue holds: 500, set by render_scene_allocate_buffers. */
// GLOBAL: XVT 0x99949E
static int g_mesh_queue_max = 0;
/* Next free entry in g_mesh_queue: render_scene_draw_scene_mesh's software path
 * advances it, the hardware path reuses the entry, and render_scene_initialize
 * sets it to 0 on a reset. At g_mesh_queue_max no mesh is drawn. */
// GLOBAL: XVT 0x9994A2
static int g_mesh_queue_index = 0;
/* Eye position in the current mesh's model space, set by
 * render_scene_cull_mesh_faces_from_view from the mesh; the cull and the specular
 * lighting read it. */
// GLOBAL: XVT 0x9994B0
struct opt_vector g_mesh_eye_pos = {0.0f, 0.0f, 0.0f};
/* Table from a 16-bit color to a palette index that model loading maps texture
 * palettes through; fe_disk_io_init_resources points it at
 * g_rgb565_to_palette_index_lut. */
// GLOBAL: XVT 0x9994BC
uint8_t *g_active_rgb565_to_palette_index_lut = NULL;

/* Projects the corners of the mesh's visible faces for the hardware path. Sets
 * vert_base_index to g_proj_vert_count and g_vertex_remap to -1 for the mesh's
 * vertices, and appends each corner not yet projected to g_proj_vert_list:
 * viewport x and y with g_proj_scale_int over depth, or, for a corner closer than
 * view depth 1, its view-space x and y with depth minus 1, which also sets the
 * face's near_clip_state to -1; then its light from
 * render_scene_compute_vertex_lighting and its texture coordinates. Sets each
 * face's largest and smallest scaled_inverse_depth (a corner closer than 1
 * counting as g_proj_scale_int) and its texture gradients; when the mesh has
 * texture coordinates, also turns them into the planes in viewport x and y and
 * sets texels_per_pixel_q8 from the texture's size, the planes' area and the
 * corners' depths (the reciprocal of their mean scaled_inverse_depth, times
 * g_proj_scale_int). Adds the new vertices to g_proj_vert_count. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4084E0
void render_scene_project_mesh_vertices(struct scene_mesh *mesh)
{
	struct scene_face *face = &g_vis_face_list[mesh->face_base_index];

	mesh->vert_base_index = g_proj_vert_count;
	struct proj_vertex *output = &g_proj_vert_list[mesh->vert_base_index];
	mesh->proj_vert_cursor = 0;
	for (int vertex_index = 0; vertex_index < mesh->vertex_count;
	     ++vertex_index) {
		g_vertex_remap[vertex_index] = -1;
	}
	for (int face_index = 0; face_index < mesh->vis_face_count;
	     ++face, ++face_index) {
		render_scene_transform_face_texture_gradients(
			face, &mesh->p_face_texturing[face->face_index],
			&mesh->view_pos_x);
		const struct face_record *geometry =
			&mesh->p_face_geom[face->face_index];
		face->max_scaled_inverse_depth = 0.0f;
		float total_w = 0.0f;
		face->min_scaled_inverse_depth =
			(float)(unsigned int)g_proj_scale_int;
		struct opt_vector transformed;
		for (int corner_index = 0; corner_index < 4; ++corner_index) {
			const int model_vertex_index =
				geometry->vertex_idx[corner_index];
			const int uv_index = geometry->uv_idx[corner_index];
			const int normal_index =
				geometry->normal_idx[corner_index];

			if (model_vertex_index == -1) {
				break;
			}
			int remapped_vertex =
				g_vertex_remap[model_vertex_index];
			float vertex_scaled_inverse_depth;
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
				if (transformed.z < g_render_unit_float) {
					output->scaled_inverse_depth =
						transformed.z -
						g_render_unit_float;
					output->sx = transformed.x;
					output->sy = transformed.y;
					face->near_clip_state = -1;
					vertex_scaled_inverse_depth =
						(float)(unsigned int)
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
					vertex_scaled_inverse_depth =
						output->scaled_inverse_depth;
				}
				render_scene_compute_vertex_lighting(
					mesh, output,
					&mesh->p_vert_normals[normal_index],
					&mesh->p_model_verts
						 [model_vertex_index],
					&g_mesh_eye_pos);
				output->tu = mesh->p_uvs[uv_index].u;
				output->tv = mesh->p_uvs[uv_index].v;
				++output;
			} else {
				const struct proj_vertex *projected =
					&g_proj_vert_list
						[mesh->vert_base_index +
						 remapped_vertex];
				if (projected->scaled_inverse_depth < 0.0f) {
					face->near_clip_state = -1;
					vertex_scaled_inverse_depth =
						(float)(unsigned int)
							g_proj_scale_int;
				} else {
					vertex_scaled_inverse_depth =
						projected->scaled_inverse_depth;
				}
			}
			total_w += vertex_scaled_inverse_depth;
			if (face->max_scaled_inverse_depth <
			    vertex_scaled_inverse_depth) {
				face->max_scaled_inverse_depth =
					vertex_scaled_inverse_depth;
			}
			if (face->min_scaled_inverse_depth >
			    vertex_scaled_inverse_depth) {
				face->min_scaled_inverse_depth =
					vertex_scaled_inverse_depth;
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
			float c00 = face->gradients[8] * face->gradients[4] -
				    face->gradients[5] * face->gradients[7];
			float c01 = face->gradients[5] * face->gradients[6] -
				    face->gradients[8] * face->gradients[3];
			float c02 = face->gradients[7] * face->gradients[3] -
				    face->gradients[4] * face->gradients[6];
			float c10 = face->gradients[2] * face->gradients[7] -
				    face->gradients[8] * face->gradients[1];
			float c11 = face->gradients[8] * face->gradients[0] -
				    face->gradients[2] * face->gradients[6];
			float c12 = face->gradients[6] * face->gradients[1] -
				    face->gradients[7] * face->gradients[0];
			float c20 = face->gradients[5] * face->gradients[1] -
				    face->gradients[2] * face->gradients[4];
			float c21 = face->gradients[2] * face->gradients[3] -
				    face->gradients[5] * face->gradients[0];
			float c22 = face->gradients[4] * face->gradients[0] -
				    face->gradients[1] * face->gradients[3];
			if (c20 == 0.0f && c21 == 0.0f && c22 == 0.0f) {
				c22 = 1.0f;
			}
			float inverse = g_render_unit_float /
					(c20 * face->gradients[6] +
					 c21 * face->gradients[7] +
					 c22 * face->gradients[8]);
			float scaled = inverse * g_inv_proj_scale;
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
			float area = face->gradients[0] * face->gradients[4] -
				     face->gradients[3] * face->gradients[1];
			if (area < g_render_projection_zero_float) {
				area = -area;
			}
			{
				const struct opt_texture_data *material =
					(const struct opt_texture_data *)
						mesh->p_material;

				/* total_w, the sum of the corners' scaled
				 * inverse depths, becomes the corner count over
				 * that sum: the reciprocal of their mean. */
				if (geometry->vertex_idx[3] == -1) {
					total_w =
						g_render_triangle_corner_count /
						total_w;
				} else {
					total_w = g_render_quad_corner_count /
						  total_w;
				}
				float mean_view_depth =
					(float)(unsigned int)g_proj_scale_int *
					total_w;
				face->texels_per_pixel_q8 =
					(int)((float)((material->width *
						       material->height)
						      << 8) *
					      (area * (mean_view_depth *
						       mean_view_depth)));
			}
		}
	}
	g_proj_vert_count += mesh->proj_vert_cursor;
}

/* The distant form of render_scene_project_mesh_vertices: adds
 * g_render_distant_depth to every corner's depth and scales the projection by
 * g_proj_scale_int / view_pos_z * g_render_distant_depth, with no near test and no
 * texture planes. Only render_scene_draw_mesh_hardware calls it, while
 * g_b_backdrop_mesh_mode is set, and nothing sets that. */
// FUNCTION: XVT 0x408BC0
void render_scene_project_distant_mesh_vertices(struct scene_mesh *mesh)
{
	float projection_scale =
		(float)((double)(unsigned int)g_proj_scale_int /
			mesh->view_pos_z * g_render_distant_depth);
	struct scene_face *face = &g_vis_face_list[mesh->face_base_index];
	int vertex_base_index = g_proj_vert_count;
	mesh->vert_base_index = vertex_base_index;
	struct proj_vertex *output = &g_proj_vert_list[vertex_base_index];
	mesh->proj_vert_cursor = 0;
	for (int vertex_index = 0; mesh->vertex_count > vertex_index;
	     ++vertex_index) {
		g_vertex_remap[vertex_index] = -1;
	}
	for (int face_index = 0; mesh->vis_face_count > face_index;
	     ++face, ++face_index) {
		const struct face_record *geometry =
			&mesh->p_face_geom[face->face_index];

		face->max_scaled_inverse_depth = 0.0f;
		face->min_scaled_inverse_depth = (float)g_proj_scale_int;
		for (int corner_index = 0; corner_index < 4; ++corner_index) {
			const int model_vertex_index =
				geometry->vertex_idx[corner_index];
			const int uv_index = geometry->uv_idx[corner_index];
			const int normal_index =
				geometry->normal_idx[corner_index];

			if (model_vertex_index == -1) {
				break;
			}
			float vertex_scaled_inverse_depth;
			if (g_vertex_remap[model_vertex_index] == -1) {
				g_vertex_remap[model_vertex_index] =
					mesh->proj_vert_cursor++;
				struct opt_vector transformed;
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
				transformed.z += g_render_distant_depth;
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
				vertex_scaled_inverse_depth =
					output->scaled_inverse_depth;
				render_scene_compute_vertex_lighting(
					mesh, output,
					&mesh->p_vert_normals[normal_index],
					&mesh->p_model_verts
						 [model_vertex_index],
					&g_mesh_eye_pos);
				output->tu = mesh->p_uvs[uv_index].u;
				output->tv = mesh->p_uvs[uv_index].v;
				++output;
			} else {
				vertex_scaled_inverse_depth =
					g_proj_vert_list
						[mesh->vert_base_index +
						 g_vertex_remap
							 [model_vertex_index]]
							.scaled_inverse_depth;
			}
			if (face->max_scaled_inverse_depth <
			    vertex_scaled_inverse_depth) {
				face->max_scaled_inverse_depth =
					vertex_scaled_inverse_depth;
			}
			if (face->min_scaled_inverse_depth >
			    vertex_scaled_inverse_depth) {
				face->min_scaled_inverse_depth =
					vertex_scaled_inverse_depth;
			}
		}
	}
	g_proj_vert_count += mesh->proj_vert_cursor;
}

/* Clips and queues the mesh's visible faces for the hardware path. For each
 * face it lists the 3 or 4 corners in g_clip_idx_a, adding a copy at
 * g_clip_vert_cursor of any corner whose stored texture coordinates differ from
 * this face's (scaled for a square texture when the device takes only square
 * ones); runs render_clip_clip_poly_near when near_clip_state is -1, then the top,
 * bottom, left and right clips; and emits the result with
 * render_scene_emit_flight_vertex, once per mesh for a projected vertex and on
 * every use for a clip vertex. With 3 or more corners left it picks a mip level
 * when the texture's texture_size is width times height: from texels_per_pixel_q8 *
 * g_mip_lod_scale, it shifts the value down 2 bits and takes the next level while
 * the value is over 256 and neither side is 8. When the texels changed it gets
 * the opaque texture, and a color-key texture when the palette marks a
 * transparent entry: for a projectile it clears that mark instead, and when
 * none can be made at full size it puts '_' at the start of the texture's name.
 * Then queues a fan of triangles into g_tri_buffer: first, with a color-key
 * texture, copies of the corners in white with that texture and alpha blending,
 * then the opaque ones. Returns at once when the modern build suppresses
 * classic flight rendering. Does not check the batch limits;
 * render_scene_draw_mesh_hardware does. */
// FUNCTION: XVT 0x408E70
void render_scene_draw_mesh_faces(const struct scene_mesh *mesh)
{
	enum {
		TRIANGLE_FIRST_NEW_CORNER = 2,
		MIP_LEVEL_REDUCTION_SHIFT = 2,
		MIP_LEVEL_REDUCTION_THRESHOLD = 256,
		MIP_MINIMUM_DIMENSION = 8,
		OPAQUE_PALETTE_OFFSET = 2048,
		PALETTE_TRANSPARENT_INDEX_SLOT = 256,
		BASE_MESH_TRIANGLE_FLAGS = 0x9813,
		BILINEAR_TRIANGLE_FLAGS = STD3D_RS_TEXTURE_MAG_LINEAR |
					  STD3D_RS_TEXTURE_MIN_LINEAR,
		COLOR_KEY_TRIANGLE_FLAGS = STD3D_RS_ALPHA_BLEND
	};

#ifdef XVT_MODERN
	/* Suppress before texture lookup so hidden classic draws do not refill the cache. */
	if (AeronDx5_IsClassicFlightRenderingSuppressed()) {
		return;
	}
#endif

	int vertex_index = mesh->vert_base_index;
	const uint8_t *previous_texels = NULL;
	struct render_clip_vertex *vertices =
		(struct render_clip_vertex *)&g_proj_vert_list[vertex_index];
	g_clip_input_proj_vert_end_index =
		vertex_index + mesh->proj_vert_cursor;
	g_clip_vert_cursor = g_clip_input_proj_vert_end_index;
	struct scene_face *face = &g_vis_face_list[mesh->face_base_index];
	int *emitted_vertex_by_projection = (int *)g_scene_edge_list;
	for (vertex_index = 0; vertex_index < g_clip_input_proj_vert_end_index;
	     ++vertex_index) {
		emitted_vertex_by_projection[vertex_index] = -1;
	}
	struct std3d_tex_cache_node *opaque_texture;
	struct std3d_tex_cache_node *color_key_texture;
#ifdef XVT_MODERN
	opaque_texture = NULL;
	color_key_texture = NULL;
#endif

	int face_index = 0;
	if (mesh->vis_face_count <= 0) {
		return;
	}
	int clip_index;
	int previous_vertex_index;
	int current_vertex_index;
	int triangle_corner;
	do {
		struct scene_face *current_face = face++;
		const struct face_record *geometry =
			&mesh->p_face_geom[current_face->face_index];
		int corner_count = geometry->edge_idx[3] == -1 ? 3 : 4;
		g_clip_count_a = corner_count;
		if (g_p_std3d_cur_device->caps.b_square_only_texture != 0) {
			const struct opt_texture_data *material =
				(const struct opt_texture_data *)
					mesh->p_material;
			float uv_scale = 1.0f;
			int scaled_width = material->width;
			int scaled_height = material->height;
			if (scaled_height < scaled_width) {
				while (scaled_height < scaled_width) {
					uv_scale *=
						g_render_texture_uv_half_scale;
					scaled_height *= 2;
				}
				scaled_height = material->height;
			} else if (scaled_height > scaled_width) {
				while (scaled_width < scaled_height) {
					uv_scale *=
						g_render_texture_uv_half_scale;
					scaled_width *= 2;
				}
				scaled_width = material->width;
			}
			int *clip_output = g_clip_idx_a;
			for (vertex_index = 0; vertex_index < corner_count;
			     ++vertex_index) {
				struct opt_tex_coord uv =
					mesh->p_uvs[geometry->uv_idx
							    [vertex_index]];
				if (scaled_height < scaled_width) {
					uv.v *= uv_scale;
				} else if (scaled_height > scaled_width) {
					uv.u *= uv_scale;
				}
				int projected_vertex_index = g_vertex_remap
					[geometry->vertex_idx[vertex_index]];
				*clip_output = projected_vertex_index;
				struct render_clip_vertex *source =
					&vertices[projected_vertex_index];
				if (source->u != uv.u || source->v != uv.v) {
					struct render_clip_vertex *duplicate =
						&vertices[g_clip_vert_cursor];
					duplicate->x = source->x;
					duplicate->y = source->y;
					duplicate->light_intensity =
						source->light_intensity;
					duplicate->scaled_inverse_depth =
						source->scaled_inverse_depth;
					duplicate->u = uv.u;
					duplicate->v = uv.v;
					*clip_output = g_clip_vert_cursor++;
				}
				++clip_output;
			}
		} else {
			for (vertex_index = 0; vertex_index < corner_count;
			     ++vertex_index) {
				int projected_vertex_index = g_vertex_remap
					[geometry->vertex_idx[vertex_index]];
				g_clip_idx_a[vertex_index] =
					projected_vertex_index;
				const struct opt_tex_coord *uv =
					&mesh->p_uvs[geometry->uv_idx
							     [vertex_index]];
				struct render_clip_vertex *source =
					&vertices[projected_vertex_index];
				if (source->u != uv->u || source->v != uv->v) {
					struct render_clip_vertex *duplicate =
						&vertices[g_clip_vert_cursor];
					duplicate->x = source->x;
					duplicate->y = source->y;
					duplicate->light_intensity =
						source->light_intensity;
					duplicate->scaled_inverse_depth =
						source->scaled_inverse_depth;
					duplicate->u =
						mesh->p_uvs
							[geometry->uv_idx
								 [vertex_index]]
								.u;
					duplicate->v =
						mesh->p_uvs
							[geometry->uv_idx
								 [vertex_index]]
								.v;
					g_clip_idx_a[vertex_index] =
						g_clip_vert_cursor++;
				}
			}
		}

		if (current_face->near_clip_state == -1) {
			if (g_clip_count_a > 0) {
				memcpy(g_clip_idx_b, g_clip_idx_a,
				       (size_t)g_clip_count_a *
					       sizeof(g_clip_idx_a[0]));
			}
			g_clip_count_b = g_clip_count_a;
			g_clip_count_a = 0;
			if (g_clip_count_b > 0) {
				previous_vertex_index =
					g_clip_idx_b[g_clip_count_b - 1];
				for (clip_index = 0;
				     clip_index < g_clip_count_b;
				     ++clip_index) {
					current_vertex_index =
						g_clip_idx_b[clip_index];
					render_clip_clip_poly_near(
						previous_vertex_index,
						current_vertex_index, vertices);
					previous_vertex_index =
						current_vertex_index;
				}
			}
		}

		current_face->near_clip_state = g_flight_vp_height;
		g_clip_count_b = 0;
		if (g_clip_count_a > 0) {
			previous_vertex_index =
				g_clip_idx_a[g_clip_count_a - 1];
			for (clip_index = 0; clip_index < g_clip_count_a;
			     ++clip_index) {
				current_vertex_index = g_clip_idx_a[clip_index];
				render_clip_clip_poly_top(previous_vertex_index,
							  current_vertex_index,
							  vertices);
				previous_vertex_index = current_vertex_index;
			}
		}
		g_clip_count_a = 0;
		if (g_clip_count_b > 0) {
			previous_vertex_index =
				g_clip_idx_b[g_clip_count_b - 1];
			for (clip_index = 0; clip_index < g_clip_count_b;
			     ++clip_index) {
				current_vertex_index = g_clip_idx_b[clip_index];
				render_clip_clip_poly_bottom(
					previous_vertex_index,
					current_vertex_index, vertices);
				previous_vertex_index = current_vertex_index;
			}
		}
		g_clip_count_b = 0;
		if (g_clip_count_a > 0) {
			previous_vertex_index =
				g_clip_idx_a[g_clip_count_a - 1];
			for (clip_index = 0; clip_index < g_clip_count_a;
			     ++clip_index) {
				current_vertex_index = g_clip_idx_a[clip_index];
				render_clip_clip_poly_left(
					previous_vertex_index,
					current_vertex_index, vertices);
				previous_vertex_index = current_vertex_index;
			}
		}
		g_clip_count_a = 0;
		if (g_clip_count_b > 0) {
			previous_vertex_index =
				g_clip_idx_b[g_clip_count_b - 1];
			for (clip_index = 0; clip_index < g_clip_count_b;
			     ++clip_index) {
				current_vertex_index = g_clip_idx_b[clip_index];
				render_clip_clip_poly_right(
					previous_vertex_index,
					current_vertex_index, vertices);
				previous_vertex_index = current_vertex_index;
			}
		}

		for (clip_index = 0; clip_index < g_clip_count_a;
		     ++clip_index) {
			current_vertex_index = g_clip_idx_a[clip_index];
			int emitted_vertex_index;
			if (current_vertex_index <
			    g_clip_input_proj_vert_end_index) {
				if (emitted_vertex_by_projection
					    [current_vertex_index] == -1) {
					emitted_vertex_by_projection
						[current_vertex_index] =
							render_scene_emit_flight_vertex(
								current_vertex_index,
								vertices,
								current_face);
				}
				emitted_vertex_index =
					emitted_vertex_by_projection
						[current_vertex_index];
			} else {
				emitted_vertex_index =
					render_scene_emit_flight_vertex(
						current_vertex_index, vertices,
						current_face);
			}
			g_clip_idx_a[clip_index] = emitted_vertex_index;
		}

		if (g_clip_count_a > TRIANGLE_FIRST_NEW_CORNER) {
			const struct opt_texture_data *material =
				(const struct opt_texture_data *)
					mesh->p_material;
			int texture_width = material->width;
			int texture_height = material->height;
			int texel_offset = 0;
			if (texture_width * texture_height ==
			    material->texture_size) {
				int texels_per_pixel_q8 =
					(int)((float)current_face
						      ->texels_per_pixel_q8 *
					      g_mip_lod_scale);
				while (texels_per_pixel_q8 >
					       MIP_LEVEL_REDUCTION_THRESHOLD &&
				       texture_width != MIP_MINIMUM_DIMENSION &&
				       texture_height !=
					       MIP_MINIMUM_DIMENSION) {
					texels_per_pixel_q8 >>=
						MIP_LEVEL_REDUCTION_SHIFT;
					texel_offset +=
						texture_width * texture_height;
					texture_width >>= 1;
					texture_height >>= 1;
				}
			}
			const uint8_t *texels =
				(const uint8_t *)mesh->p_texels + texel_offset;
			if (texels != previous_texels) {
				previous_texels = texels;
				uint16_t *opaque_palette =
					mesh->p_color_key_palette +
					OPAQUE_PALETTE_OFFSET;
				opaque_texture =
					render_texture_get_or_create_opaque(
						texture_width, texture_height,
						opaque_palette, texels);
				color_key_texture = NULL;
				if (opaque_palette
					    [PALETTE_TRANSPARENT_INDEX_SLOT] !=
				    0) {
					uint8_t genus_id =
						mesh->p_object->genus_id;
					if (genus_id ==
						    CRAFT_GENUS_PLAYER_PROJECTILE ||
					    genus_id ==
						    CRAFT_GENUS_OTHER_PROJECTILE) {
						opaque_palette
							[PALETTE_TRANSPARENT_INDEX_SLOT] =
								0;
					} else if (mesh->p_texture_name !=
							   NULL &&
						   *mesh->p_texture_name !=
							   '_') {
						color_key_texture =
							render_texture_get_or_create_color_key(
								texture_width,
								texture_height,
								mesh->p_color_key_palette,
								texels);
						if (color_key_texture == NULL &&
						    material->width ==
							    texture_width &&
						    material->height ==
							    texture_height) {
							*mesh->p_texture_name =
								'_';
						}
					}
				}
			}
		}

		if (color_key_texture != NULL) {
			for (vertex_index = 0; vertex_index < g_clip_count_a;
			     ++vertex_index) {
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index] =
					g_flight_vertex_buffer
						[g_clip_idx_a[vertex_index]];
				g_flight_vertex_buffer[g_d3d_vertex_count +
						       vertex_index]
					.color = UINT32_MAX;
			}
			int color_key_vertex_base = g_d3d_vertex_count;
			triangle_corner = TRIANGLE_FIRST_NEW_CORNER;
			g_d3d_vertex_count += g_clip_count_a;
			if (g_clip_count_a > triangle_corner) {
				do {
					g_tri_buffer[g_d3d_triangle_count]
						.vertex_index0 =
						color_key_vertex_base;
					g_tri_buffer[g_d3d_triangle_count]
						.vertex_index1 =
						color_key_vertex_base +
						triangle_corner - 1;
					g_tri_buffer[g_d3d_triangle_count]
						.vertex_index2 =
						color_key_vertex_base +
						triangle_corner;
					g_tri_buffer[g_d3d_triangle_count]
						.texture = color_key_texture;
					g_tri_buffer[g_d3d_triangle_count]
						.flags =
						(std3d_render_state_flags)
							BASE_MESH_TRIANGLE_FLAGS;
					if (g_bilinear_enabled != 0) {
						g_tri_buffer
							[g_d3d_triangle_count]
								.flags +=
							BILINEAR_TRIANGLE_FLAGS;
					}
					++triangle_corner;
					g_tri_buffer[g_d3d_triangle_count]
						.flags +=
						COLOR_KEY_TRIANGLE_FLAGS;
					++g_d3d_triangle_count;
				} while (triangle_corner < g_clip_count_a);
			}
		}

		triangle_corner = TRIANGLE_FIRST_NEW_CORNER;
		if (g_clip_count_a > triangle_corner) {
			int *triangle_vertex = &g_clip_idx_a[1];

			do {
				g_tri_buffer[g_d3d_triangle_count]
					.vertex_index0 = g_clip_idx_a[0];
				g_tri_buffer[g_d3d_triangle_count]
					.vertex_index1 = *triangle_vertex++;
				g_tri_buffer[g_d3d_triangle_count]
					.vertex_index2 = *triangle_vertex;
				g_tri_buffer[g_d3d_triangle_count].texture =
					opaque_texture;
				g_tri_buffer[g_d3d_triangle_count].flags =
					(std3d_render_state_flags)
						BASE_MESH_TRIANGLE_FLAGS;
				if (g_bilinear_enabled != 0) {
					g_tri_buffer[g_d3d_triangle_count]
						.flags +=
						BILINEAR_TRIANGLE_FLAGS;
				}
				if (g_cap_vertex_alpha != 0) {
					g_tri_buffer[g_d3d_triangle_count]
						.flags +=
						COLOR_KEY_TRIANGLE_FLAGS;
					g_cap_vertex_alpha = 0;
				}
				++triangle_corner;
				++g_d3d_triangle_count;
			} while (triangle_corner < g_clip_count_a);
		}
		++face_index;
	} while (face_index < mesh->vis_face_count);
}

/* Draws one mesh through the hardware path. Sets g_proj_vert_count and
 * g_scene_edge_cursor to 0; does nothing more when g_mesh_queue is full or the
 * mesh has more faces, vertices or edges than the buffers hold. Otherwise
 * copies it into g_mesh_queue at g_mesh_queue_index, without advancing it, and
 * culls it; when faces are left, first draws the batch so far if 8 vertices and
 * 2 triangles per face would not fit, then projects the mesh (distant while
 * g_b_backdrop_mesh_mode is set), queues its faces and puts g_vis_face_count back as
 * it was. */
// FUNCTION: XVT 0x40B010
void render_scene_draw_mesh_hardware(const struct scene_mesh *mesh)
{
	g_proj_vert_count = 0;
	int previous_visible_face_count = g_vis_face_count;
	g_scene_edge_cursor = 0;
	if (g_mesh_queue_index == g_mesh_queue_max ||
	    g_vis_face_count + mesh->face_count > g_scene_face_max ||
	    mesh->vertex_count > g_proj_vert_max ||
	    mesh->edge_count > g_scene_edge_max) {
		return;
	}
	memcpy(&g_mesh_queue[g_mesh_queue_index], mesh,
	       sizeof(struct scene_mesh));
	struct scene_mesh *queued_mesh = &g_mesh_queue[g_mesh_queue_index];
	render_scene_cull_mesh_faces_from_view(queued_mesh);
	if (queued_mesh->vis_face_count == 0) {
		return;
	}
	if (g_d3d_vertex_count + 8 * queued_mesh->vis_face_count >
		    g_max_batch_verts ||
	    g_d3d_triangle_count + 2 * queued_mesh->vis_face_count >
		    g_max_batch_tris) {
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
	if (g_b_backdrop_mesh_mode != 0) {
		render_scene_project_distant_mesh_vertices(queued_mesh);
	} else {
		render_scene_project_mesh_vertices(queued_mesh);
	}
	render_scene_draw_mesh_faces(queued_mesh);
	g_vis_face_count = previous_visible_face_count;
}

/* Starts a frame of the hardware path: sets g_flight_vp_origin_x and
 * g_flight_vp_origin_y, sets g_d3d_vertex_count, g_d3d_triangle_count and
 * g_d3d_vertex_alpha_state_reset_slot to 0 and g_cap_vertex_alpha to 1, and points
 * g_flight_vertex_buffer at the start of the span buffer and g_tri_buffer at its
 * middle. The batch limits come from the span buffer's bytes: g_max_batch_verts
 * is bytes >> 7, then no more than the device's max_vertex_count and 256;
 * g_max_batch_tris is bytes / sizeof(std3d_render_tri) >> 2, no more than 256, then
 * no more than (maxBufferSize - 64 * g_max_batch_verts) / sizeof(scene_span). */
// FUNCTION: XVT 0x40B180
void render_scene_init_hardware_frame(void)
{
	int viewport_origin_x = g_display_mode_width - g_surface_width;
	int viewport_origin_y = g_display_mode_height - g_surface_height;
	viewport_origin_x =
		g_flight_vp_x + ((unsigned int)viewport_origin_x >> 1);
	viewport_origin_y =
		g_flight_vp_y + ((unsigned int)viewport_origin_y >> 1);
	g_flight_vp_origin_x = (float)(unsigned int)viewport_origin_x;
	g_d3d_triangle_count = 0;
	g_flight_vp_origin_y = (float)(unsigned int)viewport_origin_y;
	g_d3d_vertex_count = 0;
	g_d3d_vertex_alpha_state_reset_slot = 0;
	g_cap_vertex_alpha = 1;

	unsigned int span_bytes =
		sizeof(struct scene_span) * g_scene_span_data_capacity;
	g_max_batch_verts = span_bytes >> 7;
	g_max_batch_tris = span_bytes / sizeof(struct std3d_render_tri) >> 2;
	if (g_p_std3d_cur_device->caps.max_vertex_count <
	    (unsigned int)g_max_batch_verts) {
		g_max_batch_verts = g_p_std3d_cur_device->caps.max_vertex_count;
	}
	if (g_max_batch_verts > 256) {
		g_max_batch_verts = 256;
	}
	if (g_max_batch_tris > 256) {
		g_max_batch_tris = 256;
	}
	if ((int)((g_p_std3d_cur_device->caps.max_buffer_size -
		   ((unsigned int)g_max_batch_verts << 6)) /
		  sizeof(struct scene_span)) < g_max_batch_tris) {
		g_max_batch_tris = (g_p_std3d_cur_device->caps.max_buffer_size -
				    ((unsigned int)g_max_batch_verts << 6)) /
				   sizeof(struct scene_span);
	}
	g_flight_vertex_buffer = (D3DTLVERTEX *)g_scene_span_data_base;
	g_tri_buffer =
		(struct std3d_render_tri
			 *)&g_scene_span_data_base[g_scene_span_data_capacity /
						   2];
}

/* Draws the queued billboards with scene_billboard_render_queued_textured, with
 * target markers and targeting_draw_scene_object_boxes while
 * g_scene_flush_draw_target_markers is set, and sets g_scene_billboard_queue_count to
 * 0. Then, when g_d3d_vertex_count and g_d3d_triangle_count are both nonzero, draws
 * the hardware batch in one execute buffer in extended x87 precision, returning
 * to single after. Leaves both counts as they are. */
// FUNCTION: XVT 0x40B2C0
void render_scene_flush_geometry(void)
{
	enum {
		SKIP_TARGET_MARKERS = 0,
		DRAW_TARGET_MARKERS = 1,
	};

	if (g_scene_flush_draw_target_markers != 0) {
		scene_billboard_render_queued_textured(DRAW_TARGET_MARKERS);
		targeting_draw_scene_object_boxes();
	} else {
		scene_billboard_render_queued_textured(SKIP_TARGET_MARKERS);
	}
	g_scene_billboard_queue_count = 0;

	if (g_d3d_vertex_count == 0 || g_d3d_triangle_count == 0) {
		return;
	}
	math_set_fpu_extended_precision_mode();
	std3d_start_scene();
	std3d_lock_execute_buffer();
	std3d_add_vertices(g_flight_vertex_buffer, g_d3d_vertex_count);
	std3d_begin_instructions();
	std3d_add_triangles(g_tri_buffer, g_d3d_triangle_count);
	std3d_execute_buffer();
	std3d_end_scene();
	math_set_fpu_single_precision_mode();
}

/* Appends vertex vertex_index of vertices to g_flight_vertex_buffer and returns
 * its index there, adding 1 to g_d3d_vertex_count. Adds g_flight_vp_origin_x and
 * g_flight_vp_origin_y to x and y, takes a negative scaled_inverse_depth as
 * g_proj_scale_int, writes that as rhw and 1 / (depth * g_inv_depth_proj_scale + 1)
 * as z, or 1 minus that when g_std3dz_compare_cap is 2. The color is a gray of
 * (int)(light * 320) + 48, at most 255, with alpha 0xFE while g_cap_vertex_alpha
 * is set and 0xFF otherwise; no specular. Ignores face. Does not check
 * g_max_batch_verts. */
// FUNCTION: XVT 0x40B350
int render_scene_emit_flight_vertex(int vertex_index,
				    const struct render_clip_vertex *vertices,
				    const struct scene_face *face)
{
	(void)face;
	const struct render_clip_vertex *source = &vertices[vertex_index];
	float light_intensity = source->light_intensity;
	float x = source->x;
	float y = source->y;
	float scaled_inverse_depth = source->scaled_inverse_depth;
	float u = source->u;
	float v = source->v;
	uint32_t z_bits;
	memcpy(&z_bits, &scaled_inverse_depth, sizeof(z_bits));
	if (z_bits > 0x80000000u) {
		scaled_inverse_depth = (float)(unsigned int)g_proj_scale_int;
	}
	float depth =
		1.0f / ((float)(unsigned int)g_proj_scale_int /
				scaled_inverse_depth * g_inv_depth_proj_scale +
			1.0f);
	if (g_std3dz_compare_cap == 2) {
		depth = 1.0f - depth;
	}
	g_flight_vertex_buffer[g_d3d_vertex_count].sx =
		x + g_flight_vp_origin_x;
	g_flight_vertex_buffer[g_d3d_vertex_count].sy =
		y + g_flight_vp_origin_y;
	g_flight_vertex_buffer[g_d3d_vertex_count].sz = depth;
	g_flight_vertex_buffer[g_d3d_vertex_count].rhw = scaled_inverse_depth;
	g_flight_vertex_buffer[g_d3d_vertex_count].tu = u;
	g_flight_vertex_buffer[g_d3d_vertex_count].tv = v;
	int intensity = (int)(light_intensity * 320.0f) + 48;
	if (intensity > 255) {
		intensity = 255;
	}
	uint32_t color;
	if (g_cap_vertex_alpha != 0) {
		color = 65793 * intensity - 0x2000000;
	} else {
		color = 65793 * intensity - 0x1000000;
	}
	g_flight_vertex_buffer[g_d3d_vertex_count].color = color;
	g_flight_vertex_buffer[g_d3d_vertex_count].specular = 0;
	return g_d3d_vertex_count++;
}

/* Does nothing. flight_sw_copy_viewport_span_mask_rle and
 * flight_sw_build_full_viewport_span_mask_rle call it. */
// FUNCTION: XVT 0x40B520
void nullsub_2(void) {}

/* Copies the viewport span mask into the hardware z-buffer. Locks
 * g_std3dz_buffer_surface, trying again while it is still drawing and returning
 * on any other failure, then decodes the mask at g_viewport_span_mask_offset in
 * g_flight_aux_buffer for g_flight_vp_height rows, writing 2 bytes per pixel: one
 * value for the runs the mask marks (a negative run type) and the other for the
 * rest, 0xFF and 0 when g_std3dz_compare_cap is 16, else 0 and 0xFF. The viewport
 * starts g_flight_vp_x pixels and g_flight_vp_y rows past half the margin between
 * display mode and surface. Only hud_update3d_crt calls it. */
// FUNCTION: XVT 0x40B530
void std3d_fill_z_buffer_from_viewport_mask(void)
{
	uint8_t foreground_byte;
	uint8_t background_byte;

	if (g_std3dz_compare_cap == 16) {
		foreground_byte = 0xFF;
		background_byte = 0;
	} else {
		foreground_byte = 0;
		background_byte = 0xFF;
	}

	DDSURFACEDESC surface_desc;
	memset(&surface_desc, 0, sizeof(surface_desc));
	surface_desc.dwSize = sizeof(surface_desc);
	while (1) {
		HRESULT lock_result = g_std3dz_buffer_surface->lpVtbl->Lock(
			g_std3dz_buffer_surface, NULL, &surface_desc, 0, NULL);
		if (lock_result == 0) {
			break;
		}
		if (lock_result != DX_DDERR_WASSTILLDRAWING) {
			debug_printf(
				"ERROR!(%x) failed to lock D3D z buffer!\n",
				lock_result);
			return;
		}
	}

	uint8_t *destination_row = surface_desc.lpSurface;
	uint8_t *locked_surface = surface_desc.lpSurface;
	memset(&surface_desc, 0, sizeof(surface_desc));
	surface_desc.dwSize = sizeof(surface_desc);
	g_std3dz_buffer_surface->lpVtbl->GetSurfaceDesc(g_std3dz_buffer_surface,
							&surface_desc);
	destination_row += ((g_display_mode_width - g_surface_width) & ~1) +
			   2 * g_flight_vp_x;
	destination_row +=
		((unsigned int)(g_display_mode_height - g_surface_height) / 2 +
		 g_flight_vp_y) *
		surface_desc.lPitch;
	uint8_t *mask_cursor =
		g_flight_aux_buffer + g_viewport_span_mask_offset;

	for (unsigned int row = 0; row < g_flight_vp_height; ++row) {
		uint8_t *destination = destination_row;
		int8_t run_type = (int8_t)*mask_cursor++;
		unsigned int decoded_width = 0;
		while (decoded_width < g_flight_vp_width) {
			int run_length = *mask_cursor++;
			if (run_length == 0) {
				run_length = *mask_cursor++;
				if (run_length == 0) {
					run_length = *mask_cursor++ + 256;
				}
				run_length += 255;
			}
			if (run_type < 0) {
				memset(destination, foreground_byte,
				       2 * run_length);
			} else {
				memset(destination, background_byte,
				       2 * run_length);
			}
			destination += 2 * run_length;
			run_type = -run_type;
			decoded_width += run_length;
		}
		destination_row += surface_desc.lPitch;
	}

	g_std3dz_buffer_surface->lpVtbl->Unlock(g_std3dz_buffer_surface,
						locked_surface);
}

/* Fills g_flight_back_buffer with
 * g_flight_palette16_bpp[g_flight_transparent_color_index] by a color-fill blit and
 * returns what std3d_clear_z_buffer returns. */
// FUNCTION: XVT 0x40B750
int render_scene_clear_frame_buffers(void)
{
	DDBLTFX effects;

	memset(&effects, 0, sizeof(effects));
	effects.dwSize = sizeof(effects);
	effects.dwROP = DDROP_SRCCOPY;
	effects.dwFillColor =
		g_flight_palette16_bpp[g_flight_transparent_color_index];
	g_flight_back_buffer->lpVtbl->Blt(g_flight_back_buffer, NULL, NULL,
					  NULL, DDBLT_WAIT | DDBLT_COLORFILL,
					  &effects);
	return std3d_clear_z_buffer();
}

/* When g_std3dz_buffer_surface is set, detaches it from g_flight_back_buffer,
 * releases it and sets it to NULL. */
// FUNCTION: XVT 0x40BBC0
void std3d_detach_and_release_z_buffer_surface(void)
{
	if (g_std3dz_buffer_surface != 0) {
		g_flight_back_buffer->lpVtbl->DeleteAttachedSurface(
			g_flight_back_buffer, 0, g_std3dz_buffer_surface);
		g_std3dz_buffer_surface->lpVtbl->Release(
			g_std3dz_buffer_surface);
		g_std3dz_buffer_surface = 0;
	}
}

/* Sets out_vert's light level, 0 to 1. A projectile
 * (CRAFT_GENUS_OTHER_PROJECTILE or CRAFT_GENUS_PLAYER_PROJECTILE) gets 1.
 * Otherwise it starts, with g_dir_lighting_enabled, at 0.8 times the normal's dot
 * product with the light direction, 0 when that is negative or the model blocks
 * the light; without it, at 0.4. Each point light in g_object_point_lights the
 * model does not block then adds its intensity times diffuse plus specular,
 * when that sum is over 0. Diffuse is the normal's dot product with the offset
 * to the light over the rough distance squared. Specular, with
 * g_specular_enabled, is c to the 48th power when c is 0.5 or more, else 0,
 * where c is half the normal's dot product with the half vector (eye offset
 * plus light offset) over that vector's rough length. Stops at 1. In the
 * software path a light behind the vertex is skipped; in the hardware path one
 * is skipped when that first dot product over the rough distance is under -0.3,
 * and diffuse is 0.5 over the rough distance instead. The blocking test,
 * render_scene_is_segment_occluded_by_object_model, always says no. */
// FUNCTION: XVT 0x4201F0
void render_scene_compute_vertex_lighting(const struct scene_mesh *mesh,
					  struct proj_vertex *out_vert,
					  const struct opt_vector *normal,
					  const struct opt_vector *pos,
					  const struct opt_vector *eye_pos)
{
	int genus_id = mesh->p_object->genus_id;
	if (genus_id == CRAFT_GENUS_OTHER_PROJECTILE ||
	    genus_id == CRAFT_GENUS_PLAYER_PROJECTILE) {
		out_vert->light_intensity = 1.0f;
		return;
	}
	struct opt_vector light_position;
	if (g_dir_lighting_enabled != 0) {
		float light_direction_y = (float)g_object_light_direction_y *
					  g_render_light_direction_unit_scale;
		float light_direction_z = (float)g_object_light_direction_z *
					  g_render_light_direction_unit_scale;
		float light_direction_x = (float)g_object_light_direction_x *
					  g_render_light_direction_unit_scale;
		out_vert->light_intensity =
			(light_direction_z * normal->z +
			 (light_direction_x * normal->x +
			  light_direction_y * normal->y)) *
			g_render_directional_light_intensity_scale;
		if (out_vert->light_intensity < 0.0f) {
			out_vert->light_intensity = 0.0f;
		} else {
			light_position.x =
				(float)g_object_light_direction_x + pos->x;
			light_position.y =
				(float)g_object_light_direction_y + pos->y;
			light_position.z =
				(float)g_object_light_direction_z + pos->z;
			if (render_scene_is_segment_occluded_by_object_model(
				    mesh->p_object, pos, &light_position)) {
				out_vert->light_intensity = 0.0f;
			}
		}
	} else {
		out_vert->light_intensity = 0.40000001f;
	}

	for (int light_index = 0; g_object_point_light_count > light_index;
	     ++light_index) {
		const struct object_point_light *light =
			&g_object_point_lights[light_index];
		float dx;
		float dy;
		float dz;
		float light_dot;
		float component_x;
		float component_y;
		float component_z;

		{
			component_x = (float)light->x;
			component_y = (float)light->y;
			component_z = (float)light->z;
			dx = component_x - pos->x;
			dy = component_y - pos->y;
			dz = component_z - pos->z;
			light_dot = normal->z * dz +
				    (normal->y * dy + normal->x * dx);

			if (g_use_hardware3d == 0 && light_dot < 0.0f) {
				continue;
			}
			light_position.x = component_x;
			light_position.y = component_y;
			light_position.z = component_z;
		}
		if (render_scene_is_segment_occluded_by_object_model(
			    mesh->p_object, pos, &light_position)) {
			continue;
		}
		component_x = dx;
		component_y = dy;
		component_z = dz;
		if (dx < 0.0f) {
			component_x = -dx;
		}
		if (dy < 0.0f) {
			component_y = -dy;
		}
		if (dz < 0.0f) {
			component_z = -dz;
		}
		float distance;
		if (component_x >= component_y && component_x >= component_z) {
			distance = component_x +
				   (component_y + component_z) *
					   g_render_rough_distance_scale;
		} else if (component_y >= component_x &&
			   component_y >= component_z) {
			distance = component_y +
				   (component_x + component_z) *
					   g_render_rough_distance_scale;
		} else {
			distance = component_z +
				   (component_x + component_y) *
					   g_render_rough_distance_scale;
		}
		if (g_use_hardware3d != 0) {
			if (light_dot / distance <
			    g_render_point_light_facing_threshold) {
				continue;
			}
			/* With 3D hardware light_dot is replaced by half the
			 * distance, so the diffuse term below becomes 0.5 over
			 * the distance and the facing term is dropped. */
			light_dot = (float)(distance * g_render_half_double);
		}
		float diffuse = light_dot / (distance * distance);
		float specular;
		if (g_specular_enabled != 0) {
			float half_x = eye_pos->x - pos->x + dx;
			float half_y = eye_pos->y - pos->y + dy;
			float half_z = eye_pos->z - pos->z + dz;
			float half_dot =
				(normal->z * half_z +
				 (normal->y * half_y + normal->x * half_x)) *
				g_render_half_float;
			component_x = half_x;
			component_y = half_y;
			component_z = half_z;
			if (half_x < 0.0f) {
				component_x = -half_x;
			}
			if (half_y < 0.0f) {
				component_y = -half_y;
			}
			if (half_z < 0.0f) {
				component_z = -half_z;
			}
			if (component_x >= component_y &&
			    component_x >= component_z) {
				distance =
					component_x *
						g_render_specular_approx_max_component_scale +
					(component_y + component_z) *
						g_render_specular_approx_other_components_scale;
			} else if (component_y >= component_x &&
				   component_y >= component_z) {
				distance =
					component_y *
						g_render_specular_approx_max_component_scale +
					(component_x + component_z) *
						g_render_specular_approx_other_components_scale;
			} else {
				distance =
					component_z *
						g_render_specular_approx_max_component_scale +
					(component_x + component_y) *
						g_render_specular_approx_other_components_scale;
			}
			float cosine = half_dot / distance;
			if (cosine >= g_render_half_float) {
				specular = cosine * cosine * cosine;
				specular *= specular;
				specular *= specular;
				specular *= specular;
				specular *= specular;
			} else {
				specular = 0.0f;
			}
		} else {
			specular = 0.0f;
		}
		float contribution = diffuse + specular;
		if (contribution > g_render_zero_float) {
			out_vert->light_intensity +=
				(float)light->intensity * contribution;
			if (out_vert->light_intensity >= 1.0f) {
				out_vert->light_intensity = 1.0f;
				return;
			}
		}
	}
}

/* Copies the face's u and v axes from face_tex_gradients into gradients[0] to [2]
 * and [3] to [5], each turned by the orientation at view_pos_and_orient + 3 with
 * math3d_rotate_vec3. */
// FUNCTION: XVT 0x420DB0
void render_scene_transform_face_texture_gradients(
	struct scene_face *face,
	const struct face_texture_gradients *face_tex_gradients,
	const float *view_pos_and_orient)
{
	face->gradients[0] = face_tex_gradients->u_axis.x;
	face->gradients[1] = face_tex_gradients->u_axis.y;
	face->gradients[2] = face_tex_gradients->u_axis.z;
	math3d_rotate_vec3(&face->gradients[0], view_pos_and_orient + 3);

	face->gradients[3] = face_tex_gradients->v_axis.x;
	face->gradients[4] = face_tex_gradients->v_axis.y;
	face->gradients[5] = face_tex_gradients->v_axis.z;
	math3d_rotate_vec3(&face->gradients[3], view_pos_and_orient + 3);
}

/* Carries a model point into view space (the orientation at view_pos_and_orient +
 * 3, then the position at view_pos_and_orient) and projects it: out_projected[2] is
 * g_proj_scale_int over depth, [0] and [1] viewport x and y. Does not check for a
 * depth of 0 or less. Nothing calls this. */
// FUNCTION: XVT 0x420E10
void render_scene_transform_project_legacy_point(
	float out_projected[3], const float point[3],
	const float view_pos_and_orient[12])
{
	float view_point[3];

	view_point[0] = point[0];
	view_point[1] = point[1];
	view_point[2] = point[2];
	math3d_rotate_vec3(view_point, view_pos_and_orient + 3);
	view_point[0] += view_pos_and_orient[0];
	view_point[1] += view_pos_and_orient[1];
	view_point[2] += view_pos_and_orient[2];

	out_projected[2] = (float)((double)g_proj_scale_int / view_point[2]);
	out_projected[0] = out_projected[2] * view_point[0];
	out_projected[1] = out_projected[2] * view_point[1];
	out_projected[0] += (int)(g_flight_vp_width >> 1);
	out_projected[1] += g_proj_offset_y + (int)(g_flight_vp_height >> 1);
}

/* The distant form of render_scene_transform_project_legacy_point: adds 100000 to
 * the depth and scales the projection by g_proj_scale_int / view_pos_and_orient[2] *
 * 100000. Nothing calls this. */
// FUNCTION: XVT 0x420EE0
void render_scene_transform_project_legacy_distant_point(
	float out_projected[3], const float point[3],
	const float view_pos_and_orient[12])
{
	float view_point[3];

	float distant_project_scale = (float)g_proj_scale_int /
				      view_pos_and_orient[2] * (float)100000.0;
	view_point[0] = point[0];
	view_point[1] = point[1];
	view_point[2] = point[2];
	math3d_rotate_vec3(view_point, view_pos_and_orient + 3);
	view_point[0] += view_pos_and_orient[0];
	view_point[1] += view_pos_and_orient[1];
	view_point[2] += view_pos_and_orient[2];
	view_point[2] += (float)100000.0;

	out_projected[2] = distant_project_scale / view_point[2];
	out_projected[0] = out_projected[2] * view_point[0];
	out_projected[1] = out_projected[2] * view_point[1];
	out_projected[0] += (int)(g_flight_vp_width >> 1);
	out_projected[1] += g_proj_offset_y + (int)(g_flight_vp_height >> 1);
}

/* Appends to g_vis_face_list each face of the mesh whose normal's dot product
 * with the offset from its first corner to the eye is 0 or more, setting its
 * faceIndex, pMesh, light-sample row, face_and_layer_id and a NULL p_scan_edge, and
 * raising g_light_sample_slot_index up to 199. Sets the mesh's face_base_index and
 * vis_face_count, g_vis_face_count, and g_mesh_eye_pos: the mesh's eye, or, while
 * g_b_backdrop_mesh_mode is set, that eye 100000 back along view z. Does not check
 * g_scene_face_max; the callers do. */
// FUNCTION: XVT 0x470140
void render_scene_cull_mesh_faces_from_view(struct scene_mesh *mesh)
{
	mesh->face_base_index = g_vis_face_count;
	g_mesh_eye_pos.x = mesh->eye_model_space_x;
	g_mesh_eye_pos.y = mesh->eye_model_space_y;
	g_mesh_eye_pos.z = mesh->eye_model_space_z;
	if (g_b_backdrop_mesh_mode) {
		g_mesh_eye_pos.x = 0.0f;
		g_mesh_eye_pos.y = 0.0f;
		g_mesh_eye_pos.z = -100000.0f;
		math3d_rotate_vec3(&g_mesh_eye_pos.x,
				   mesh->view_to_model_orient);
		g_mesh_eye_pos.x += mesh->eye_model_space_x;
		g_mesh_eye_pos.y += mesh->eye_model_space_y;
		g_mesh_eye_pos.z += mesh->eye_model_space_z;
	}

	struct opt_vector *face_normal = mesh->p_face_normals;
	struct face_record *face_record = mesh->p_face_geom;
	float *model_verts = &mesh->p_model_verts->x;
	struct scene_face *out_face = &g_vis_face_list[g_vis_face_count];
	int face_index = 0;
	if (mesh->face_count > 0) {
		do {
			float view_vec[3];

			int vertex_index =
				3 * face_record[face_index].vertex_idx[0];
			view_vec[0] =
				g_mesh_eye_pos.x - model_verts[vertex_index];
			view_vec[1] = g_mesh_eye_pos.y -
				      model_verts[vertex_index + 1];
			view_vec[2] = g_mesh_eye_pos.z -
				      model_verts[vertex_index + 2];
			if (math3d_dot3(view_vec, &face_normal[face_index].x) >=
			    g_sw3d_zero_float) {
				out_face->face_index = face_index;
				out_face->p_mesh = mesh;
				int light_sample_offset =
					g_light_sample_slot_stride;
				light_sample_offset *=
					g_light_sample_slot_index;
				out_face->p_light_samples =
					g_scene_light_sample_data +
					12 * light_sample_offset;
				if (g_light_sample_slot_index < 199) {
					++g_light_sample_slot_index;
				}
				out_face->face_and_layer_id =
					face_index + (g_cur_layer_id << 16);
				out_face->p_scan_edge = NULL;
				++out_face;
				++g_vis_face_count;
			}

			++face_index;
		} while (mesh->face_count > face_index);
	}

	mesh->vis_face_count = g_vis_face_count - mesh->face_base_index;
}

/* Draws one mesh: in the hardware path through render_scene_draw_mesh_hardware.
 * Otherwise sets g_proj_vert_count and g_scene_edge_cursor to 0 and, when
 * g_mesh_queue has room and the mesh's faces, vertices and edges fit, copies it
 * into g_mesh_queue and culls it; when faces are left, projects it with sw3d
 * (distant while g_b_backdrop_mesh_mode is set), turns its faces into spans with
 * sw3d_rasterize_mesh_faces and advances g_mesh_queue_index. */
// FUNCTION: XVT 0x471E00
void render_scene_draw_scene_mesh(const struct scene_mesh *mesh)
{
	if (g_use_hardware3d != 0) {
		render_scene_draw_mesh_hardware(mesh);
		return;
	}
	g_proj_vert_count = 0;
	g_scene_edge_cursor = 0;
	if (g_mesh_queue_index != g_mesh_queue_max &&
	    g_vis_face_count + mesh->face_count <= g_scene_face_max &&
	    mesh->vertex_count <= g_proj_vert_max &&
	    mesh->edge_count <= g_scene_edge_max) {
		memcpy(&g_mesh_queue[g_mesh_queue_index], mesh,
		       sizeof(struct scene_mesh));
		struct scene_mesh *queued_mesh =
			&g_mesh_queue[g_mesh_queue_index];
		render_scene_cull_mesh_faces_from_view(queued_mesh);
		if (queued_mesh->vis_face_count != 0) {
			if (g_b_backdrop_mesh_mode != 0) {
				sw3d_project_mesh_vertices_distant(queued_mesh);
			} else {
				sw3d_project_mesh_vertices(queued_mesh);
			}
			sw3d_rasterize_mesh_faces(queued_mesh);
			++g_mesh_queue_index;
		}
	}
}

/* Turns the mesh by the B-wing bridge's mesh_rotation byte, times
 * g_mesh_rotation_byte_to_radians_scale, about the axis (0, -1, 0): multiplies
 * view_to_model_orient by that rotation, turns eyeModelSpace by it, and multiplies
 * view_orient by its transpose from the left. Ignores unused_model. */
// FUNCTION: XVT 0x472360
void render_scene_apply_bwing_bridge_rotation(
	const struct optimized_poly_object *unused_model,
	const struct object_record *obj, struct scene_mesh *mesh,
	int bridge_mesh_index)
{
	(void)unused_model;

	int bridge_rotation_byte =
		obj->mobj->p_craft->mesh_rotation[bridge_mesh_index];
	float axis_angle[4];
	axis_angle[3] =
		bridge_rotation_byte * g_mesh_rotation_byte_to_radians_scale;
	axis_angle[0] = 0.0f;
	axis_angle[2] = 0.0f;
	axis_angle[1] = -1.0f;
	float rotation_matrix[16];
	math3d_build_axis_angle_matrix(rotation_matrix, axis_angle);
	math3d_mul_matrix3x3(mesh->view_to_model_orient, rotation_matrix);
	math3d_rotate_vec3(&mesh->eye_model_space_x, rotation_matrix);
	math3d_pre_mul_transposed_matrix3x3(mesh->view_orient, rotation_matrix);
}

/* Draws an object's whole model. Locks g_loaded_models[objectType], fixing its
 * pointers when the model has moved, and sets g_node_switch_index from the object
 * (0 without a mobile record). The mesh starts at the object's offset from the
 * camera turned by the Q15 camera matrix, with orientations from the
 * g_objViewMat matrix and the eye in model space; the built-in white texture is
 * made on first use. Each root node is walked with render_scene_draw_model_node,
 * raising g_cur_layer_id. For a craft, each root other than a texture is one
 * component: a component whose component_state is set is skipped, and its
 * mesh_rotation byte gives rot_angle; for a B-wing whose bridge mesh_rotation is
 * nonzero, the bridge rotation is applied to the mesh for that root and taken
 * back after. Clears the model walk's g_cur globals first and unlocks the model
 * at the end. */
// FUNCTION: XVT 0x472400
void render_scene_draw_object_model(struct object_record *obj)
{
	uint16_t model_handle = g_loaded_models[obj->object_type];
	if (obj->mobj != NULL) {
		g_node_switch_index = obj->mobj->node_switch_index;
	} else {
		g_node_switch_index = 0;
	}
	struct optimized_poly_object *model =
		(struct optimized_poly_object *)memory_get_handle_block(
			model_handle);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	struct scene_mesh mesh;
	memset(&mesh, 0, sizeof(mesh));
	mesh.p_object = obj;
	mesh.view_pos_x =
		(float)(obj->world_x -
			g_players[g_local_player].view_state.camera_world_x);
	mesh.view_pos_y =
		(float)(obj->world_y -
			g_players[g_local_player].view_state.camera_world_y);
	mesh.view_pos_z =
		(float)(obj->world_z -
			g_players[g_local_player].view_state.camera_world_z);
	mesh.view_orient[0] =
		(float)g_cam_mat_r0_x * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[1] =
		(float)g_cam_mat_r1_x * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[2] =
		(float)g_cam_mat_r2_x * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[3] =
		(float)g_cam_mat_r0_y * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[4] =
		(float)g_cam_mat_r1_y * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[5] =
		(float)g_cam_mat_r2_y * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[6] =
		(float)g_cam_mat_r0_z * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[7] =
		(float)g_cam_mat_r1_z * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[8] =
		(float)g_cam_mat_r2_z * g_render_matrix_q15_to_float_scale;
	math3d_rotate_vec3(&mesh.view_pos_x, mesh.view_orient);

	float object_view_r0x =
		(float)g_obj_view_mat_r0_x * g_render_matrix_q15_to_float_scale;
	float object_view_r0y =
		(float)g_obj_view_mat_r0_y * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[0] = object_view_r0x;
	mesh.view_orient[1] = object_view_r0y;
	float object_view_r0z =
		(float)g_obj_view_mat_r0_z * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[2] = object_view_r0z;
	float object_view_r1x =
		(float)g_obj_view_mat_r1_x * g_render_matrix_q15_to_float_scale;
	float object_view_r1y =
		(float)g_obj_view_mat_r1_y * g_render_matrix_q15_to_float_scale;
	float object_view_r1z =
		(float)g_obj_view_mat_r1_z * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[3] = object_view_r1x;
	mesh.view_orient[4] = object_view_r1y;
	mesh.view_orient[5] = object_view_r1z;
	float object_view_r2x =
		(float)g_obj_view_mat_r2_x * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[6] = object_view_r2x;
	float object_view_r2y =
		(float)g_obj_view_mat_r2_y * g_render_matrix_q15_to_float_scale;
	float object_view_r2z =
		(float)g_obj_view_mat_r2_z * g_render_matrix_q15_to_float_scale;
	mesh.eye_model_space_x = -mesh.view_pos_x;
	mesh.eye_model_space_y = -mesh.view_pos_y;
	mesh.eye_model_space_z = -mesh.view_pos_z;
	mesh.view_to_model_orient[0] = object_view_r0x;
	mesh.view_orient[7] = object_view_r2y;
	mesh.view_to_model_orient[1] = object_view_r1x;
	mesh.view_to_model_orient[2] = object_view_r2x;
	mesh.view_orient[8] = object_view_r2z;
	mesh.view_to_model_orient[3] = object_view_r0y;
	mesh.view_to_model_orient[4] = object_view_r1y;
	mesh.view_to_model_orient[5] = object_view_r2y;
	mesh.view_to_model_orient[6] = object_view_r0z;
	mesh.view_to_model_orient[7] = object_view_r1z;
	mesh.view_to_model_orient[8] = object_view_r2z;
	math3d_rotate_vec3(&mesh.eye_model_space_x, mesh.view_to_model_orient);

	g_cur_mesh_vertices = NULL;
	if (g_default_white_texture_desc_ptr == NULL) {
		g_default_white_texture_desc_ptr =
			&g_default_white_texture.header;
		g_default_white_texture.header.height =
			DEFAULT_WHITE_TEXTURE_DIMENSION;
		g_default_white_texture_desc_ptr->width =
			DEFAULT_WHITE_TEXTURE_DIMENSION;
		g_default_white_texture_desc_ptr->inline_palette_count = 16;
		g_default_white_texture_desc_ptr->palette =
			(uint16_t *)(uintptr_t)256;
		model_texture_build_paletted_shade_table(
			g_default_white_texture.data.base_texels,
			g_default_white_texture_rgb24,
			DEFAULT_WHITE_TEXTURE_DIMENSION,
			DEFAULT_WHITE_TEXTURE_DIMENSION);
	}
	g_cur_mesh_tex_coords = NULL;
	g_cur_vert_normals = NULL;
	g_model_node_walk_unused_scratch2 = NULL;
	int restore_mesh = 0;
	g_cur_texture_desc = g_default_white_texture_desc_ptr;
	int root_index = 0;
	g_cur_mesh_materials = NULL;
	g_cur_vertex_count = 0;
	int mesh_ordinal = 0;

	struct scene_mesh saved_mesh;
	for (; root_index < model->root_node_count; ++root_index) {
		mesh.rot_angle = 0.0f;
		struct opt_node *node = model->root_nodes[root_index];
		if (node->node_type != OPT_TEXTURE) {
			++mesh_ordinal;
			if (obj->mobj != NULL && obj->mobj->p_craft != NULL) {
				struct craft_data *craft = obj->mobj->p_craft;
				if (craft->component_state[mesh_ordinal - 1] !=
				    0) {
					continue;
				}
				int rotation_byte =
					craft->mesh_rotation[mesh_ordinal - 1];
				if (obj->object_type == B_WING_OBJECT_TYPE) {
					if (g_bwing_bridge_mesh_index_cache ==
					    -1) {
						g_bwing_bridge_mesh_index_cache =
							model_mesh_find_bridge_index(
								model);
					}
					if (g_bwing_bridge_mesh_index_cache !=
						    -1 &&
					    obj->mobj->p_craft->mesh_rotation
							    [g_bwing_bridge_mesh_index_cache] !=
						    0) {
						saved_mesh = mesh;
						restore_mesh = 1;
						render_scene_apply_bwing_bridge_rotation(
							model, obj, &mesh,
							g_bwing_bridge_mesh_index_cache);
					}
				}
				mesh.rot_angle =
					rotation_byte *
					g_mesh_rotation_byte_to_radians_scale;
			}
		}
		++g_cur_layer_id;
		render_scene_draw_model_node(model, node, &mesh);
		if (restore_mesh != 0) {
			mesh = saved_mesh;
			restore_mesh = 0;
		}
	}
	memory_handle_block_done_stub(model_handle);
}

/* Draws one root node of an object's model, set up as
 * render_scene_draw_object_model sets up the whole; a component object (type 89)
 * uses its source object's model. Texture roots are walked as well, and each
 * one met raises root_node_index by 1, so texture roots ahead of the wanted one
 * do not count. No component is skipped and no rotation is applied. */
// FUNCTION: XVT 0x4728D0
void render_scene_draw_selected_root_node(struct object_record *obj,
					  int root_node_index)
{
	int object_type = obj->object_type;
	if (object_type == COMPONENT_OBJECT_TYPE && obj->mobj != NULL) {
		object_type = obj->mobj->source_object_type;
	}
	if (obj->mobj != NULL) {
		g_node_switch_index = obj->mobj->node_switch_index;
	} else {
		g_node_switch_index = 0;
	}

	uint16_t model_handle = g_loaded_models[object_type];
	struct optimized_poly_object *model =
		(struct optimized_poly_object *)memory_get_handle_block(
			model_handle);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	struct scene_mesh mesh;
	memset(&mesh, 0, sizeof(mesh));
	mesh.p_object = obj;
	mesh.view_pos_x =
		(float)(obj->world_x -
			g_players[g_local_player].view_state.camera_world_x);
	mesh.view_pos_y =
		(float)(obj->world_y -
			g_players[g_local_player].view_state.camera_world_y);
	mesh.view_pos_z =
		(float)(obj->world_z -
			g_players[g_local_player].view_state.camera_world_z);
	mesh.view_orient[0] =
		(float)g_cam_mat_r0_x * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[1] =
		(float)g_cam_mat_r1_x * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[2] =
		(float)g_cam_mat_r2_x * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[3] =
		(float)g_cam_mat_r0_y * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[4] =
		(float)g_cam_mat_r1_y * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[5] =
		(float)g_cam_mat_r2_y * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[6] =
		(float)g_cam_mat_r0_z * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[7] =
		(float)g_cam_mat_r1_z * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[8] =
		(float)g_cam_mat_r2_z * g_render_matrix_q15_to_float_scale;
	math3d_rotate_vec3(&mesh.view_pos_x, mesh.view_orient);

	float object_view_r0x =
		(float)g_obj_view_mat_r0_x * g_render_matrix_q15_to_float_scale;
	float object_view_r0y =
		(float)g_obj_view_mat_r0_y * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[0] = object_view_r0x;
	mesh.view_orient[1] = object_view_r0y;
	float object_view_r0z =
		(float)g_obj_view_mat_r0_z * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[2] = object_view_r0z;
	float object_view_r1x =
		(float)g_obj_view_mat_r1_x * g_render_matrix_q15_to_float_scale;
	float object_view_r1y =
		(float)g_obj_view_mat_r1_y * g_render_matrix_q15_to_float_scale;
	float object_view_r1z =
		(float)g_obj_view_mat_r1_z * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[3] = object_view_r1x;
	mesh.view_orient[4] = object_view_r1y;
	mesh.view_orient[5] = object_view_r1z;
	float object_view_r2x =
		(float)g_obj_view_mat_r2_x * g_render_matrix_q15_to_float_scale;
	mesh.view_orient[6] = object_view_r2x;
	float object_view_r2y =
		(float)g_obj_view_mat_r2_y * g_render_matrix_q15_to_float_scale;
	float object_view_r2z =
		(float)g_obj_view_mat_r2_z * g_render_matrix_q15_to_float_scale;
	mesh.eye_model_space_x = -mesh.view_pos_x;
	mesh.eye_model_space_y = -mesh.view_pos_y;
	mesh.eye_model_space_z = -mesh.view_pos_z;
	mesh.view_to_model_orient[0] = object_view_r0x;
	mesh.view_orient[7] = object_view_r2y;
	mesh.view_to_model_orient[1] = object_view_r1x;
	mesh.view_to_model_orient[2] = object_view_r2x;
	mesh.view_orient[8] = object_view_r2z;
	mesh.view_to_model_orient[3] = object_view_r0y;
	mesh.view_to_model_orient[4] = object_view_r1y;
	mesh.view_to_model_orient[5] = object_view_r2y;
	mesh.view_to_model_orient[6] = object_view_r0z;
	mesh.view_to_model_orient[7] = object_view_r1z;
	mesh.view_to_model_orient[8] = object_view_r2z;
	math3d_rotate_vec3(&mesh.eye_model_space_x, mesh.view_to_model_orient);

	g_cur_mesh_vertices = NULL;
	if (g_default_white_texture_desc_ptr == NULL) {
		g_default_white_texture_desc_ptr =
			&g_default_white_texture.header;
		g_default_white_texture.header.height =
			DEFAULT_WHITE_TEXTURE_DIMENSION;
		g_default_white_texture_desc_ptr->width =
			DEFAULT_WHITE_TEXTURE_DIMENSION;
		g_default_white_texture_desc_ptr->inline_palette_count = 16;
		g_default_white_texture_desc_ptr->palette =
			(uint16_t *)(uintptr_t)256;
		model_texture_build_paletted_shade_table(
			g_default_white_texture.data.base_texels,
			g_default_white_texture_rgb24,
			DEFAULT_WHITE_TEXTURE_DIMENSION,
			DEFAULT_WHITE_TEXTURE_DIMENSION);
	}
	g_cur_texture_desc = g_default_white_texture_desc_ptr;
	g_cur_mesh_tex_coords = NULL;
	g_cur_vert_normals = NULL;
	g_model_node_walk_unused_scratch2 = NULL;
	g_cur_mesh_materials = NULL;
	g_cur_vertex_count = 0;

	for (int root_index = 0; root_index < model->root_node_count;
	     ++root_index) {
		struct opt_node *root_node = model->root_nodes[root_index];

		if (root_node->node_type == OPT_TEXTURE) {
			++root_node_index;
			++g_cur_layer_id;
			render_scene_draw_model_node(model, root_node, &mesh);
			continue;
		}
		if (root_index == root_node_index) {
			++g_cur_layer_id;
			render_scene_draw_model_node(model, root_node, &mesh);
		}
	}
	memory_handle_block_done_stub(model_handle);
}

/* Walks one model node and those below it, updating mesh and drawing face data.
 * Follows node references first (in the modern build through the cached
 * resolver; in the original, while g_cache_resolved_opt_node_refs is set, through a
 * pointer cached in the node) and returns when one resolves to nothing. By
 * type: face data sets the mesh's faces, normals and texture axes, takes
 * g_cur_texture_desc when the mesh has no texture, and calls
 * render_scene_draw_scene_mesh, with the generated vertex normals when the mesh
 * has none; transform, translation, rotation and scale nodes update viewPos,
 * view_orient, view_to_model_orient and eyeModelSpace; vertex, normal and
 * texture-coordinate nodes set those arrays; a texture node sets the texture,
 * palettes and g_cur_texture_desc; a material binding copies g_cur_mesh_materials
 * into the field its payload count picks; a rotate-and-scale node turns the
 * mesh by rot_angle about its axis through its pivot; a face group picks a child
 * by distance, or by g_forced_lod_level, and a node switch picks child
 * g_node_switch_index + 1, at most the last. Then walks the picked child, or none
 * when a face group's thresholds all fail, or with no pick every child with a
 * copy of the mesh after clearing the walk's g_cur globals. Raises g_cur_layer_id
 * before each child. */
// FUNCTION: XVT 0x472C90
void render_scene_draw_model_node(struct optimized_poly_object *model,
				  struct opt_node *node,
				  struct scene_mesh *mesh)
{
	struct model_node_selection_state {
		float lod_threshold;
		int node_switch_selection;
	} selection;

	struct opt_node *current_node = node;
	if (current_node == NULL) {
		return;
	}
	int lod_child_selection = 0;
	selection.node_switch_selection = 0;
	while (current_node->node_type == OPT_NODEREF) {
		if (g_cache_resolved_opt_node_refs != 0) {
#ifdef XVT_MODERN
			current_node =
				xvt_opt_resolve_cached(model, current_node);
#else

			char **reference_name = (char **)&current_node->payload;
			if (**reference_name == '\0') {
				current_node =
					(struct opt_node *)current_node->p_name;
			} else {
				current_node->p_name =
					(char *)opt_model_resolve_node_ref(
						model, *reference_name);
				**reference_name = '\0';
				current_node =
					(struct opt_node *)current_node->p_name;
			}
#endif
		} else {
			current_node = opt_model_resolve_node_ref(
				model, (const char *)current_node->payload);
		}
		if (current_node == NULL) {
			return;
		}
	}

	void *node_data = current_node->payload;
	if (node_data != NULL) {
		struct opt_vector *parameters = (struct opt_vector *)node_data;
		switch (current_node->node_type) {
		case OPT_FACEDATA:
		case OPT_FACEDATA_QUAD_MESH:
		case OPT_FACEDATA_FACE_SET:
		case OPT_FACEDATA_TRIANGLE_STRIP_SET: {
			struct opt_packed_face_data *face_data =
				(struct opt_packed_face_data *)node_data;
			mesh->face_count = current_node->payload_count;
			mesh->edge_count = face_data->edge_count;
			struct face_record *face_geometry =
				(struct face_record *)face_data->records;
			mesh->p_face_geom = face_geometry;
			struct opt_vector *face_normals =
				(struct opt_vector *)&face_geometry
					[current_node->payload_count];
			mesh->p_face_normals = face_normals;
			struct face_texture_gradients *face_texturing =
				(struct face_texture_gradients *)&face_normals
					[current_node->payload_count];
			mesh->p_face_texturing = face_texturing;
			struct opt_vector *generated_normals =
				&face_texturing[current_node->payload_count]
					 .u_axis;
			if (mesh->p_material == NULL) {
				mesh->p_material = g_cur_texture_desc;
				mesh->p_texels = mesh->p_material;
				mesh->p_texels =
					(uint8_t *)mesh->p_texels +
					sizeof(struct opt_texture_data);
				if (g_cur_texture_desc->inline_palette_count !=
				    0) {
					mesh->p_palette = mesh->p_texels;
					int palette_offset =
						((struct opt_texture_data *)
							 mesh->p_material)
							->width *
						((struct opt_texture_data *)
							 mesh->p_material)
							->height;
					if (((struct opt_texture_data *)
						     mesh->p_material)
						    ->texture_size ==
					    palette_offset) {
						mesh->p_palette =
							(uint8_t *)
								mesh->p_texels +
							((struct
							  opt_texture_data
								  *)mesh
								 ->p_material)
								->data_size;
					} else {
						mesh->p_palette =
							(uint8_t *)
								mesh->p_texels +
							palette_offset;
					}
				} else {
					mesh->p_palette =
						g_cur_texture_desc->palette;
				}
				mesh->p_palette = (uint8_t *)mesh->p_palette +
						  OPT_INDEXED_SHADE_TABLE_SIZE;
				mesh->p_color_key_palette =
					(uint16_t *)mesh->p_palette;
				mesh->p_palette = (uint8_t *)mesh->p_palette -
						  OPT_INDEXED_SHADE_TABLE_SIZE;
			}
			if (mesh->p_vert_normals == NULL) {
				mesh->p_vert_normals = generated_normals;
				render_scene_draw_scene_mesh(mesh);
				mesh->p_vert_normals = NULL;
			} else {
				render_scene_draw_scene_mesh(mesh);
			}
			break;
		}
		case OPT_TRANSFORM:
			math3d_mul_matrix3x3(mesh->view_orient,
					     &parameters[1].x);
			math3d_rotate_vec3(&mesh->view_pos_x, &parameters[1].x);
			mesh->view_pos_x += parameters->x;
			mesh->view_pos_y += parameters->y;
			mesh->view_pos_z += parameters->z;
			math3d_pre_mul_transposed_matrix3x3(
				mesh->view_to_model_orient, &parameters[1].x);
			mesh->eye_model_space_x -= math3d_rotate_vec3x(
				&parameters->x, mesh->view_to_model_orient);
			mesh->eye_model_space_y -= math3d_rotate_vec3y(
				&parameters->x, mesh->view_to_model_orient);
			mesh->eye_model_space_z -= math3d_rotate_vec3z(
				&parameters->x, mesh->view_to_model_orient);
			break;
		case OPT_MESHVERTS:
			mesh->vertex_count = current_node->payload_count;
			mesh->p_model_verts = parameters;
			break;
		case OPT_TRANSLATION:
			mesh->view_pos_x += parameters->x;
			mesh->view_pos_y += parameters->y;
			mesh->view_pos_z += parameters->z;
			mesh->eye_model_space_x -= math3d_rotate_vec3x(
				&parameters->x, mesh->view_to_model_orient);
			mesh->eye_model_space_y -= math3d_rotate_vec3y(
				&parameters->x, mesh->view_to_model_orient);
			mesh->eye_model_space_z -= math3d_rotate_vec3z(
				&parameters->x, mesh->view_to_model_orient);
			break;
		case OPT_ROTATION:
			math3d_mul_matrix3x3(mesh->view_orient,
					     (const float *)node_data);
			math3d_rotate_vec3(&mesh->view_pos_x,
					   (const float *)node_data);
			math3d_pre_mul_transposed_matrix3x3(
				mesh->view_to_model_orient,
				(const float *)node_data);
			break;
		case OPT_SCALE: {
			float *scale_x = &parameters->x;
			float *scale_y = &parameters->y;
			float *scale_z = &parameters->z;
			mesh->view_orient[0] = mesh->view_orient[0] * *scale_x;
			mesh->view_orient[1] = mesh->view_orient[1] * *scale_y;
			mesh->view_orient[2] = mesh->view_orient[2] * *scale_z;
			mesh->view_orient[3] = mesh->view_orient[3] * *scale_x;
			mesh->view_orient[4] = mesh->view_orient[4] * *scale_y;
			mesh->view_orient[5] = mesh->view_orient[5] * *scale_z;
			mesh->view_orient[6] = mesh->view_orient[6] * *scale_x;
			mesh->view_orient[7] = mesh->view_orient[7] * *scale_y;
			mesh->view_orient[8] = mesh->view_orient[8] * *scale_z;
			mesh->view_pos_x = mesh->view_pos_x * *scale_x;
			mesh->view_pos_y = mesh->view_pos_y * *scale_y;
			mesh->view_pos_z = mesh->view_pos_z * *scale_z;

			float inverse_scale = 1.0f / *scale_x;
			mesh->view_to_model_orient[0] =
				mesh->view_to_model_orient[0] * inverse_scale;
			mesh->view_to_model_orient[1] =
				mesh->view_to_model_orient[1] * inverse_scale;
			mesh->view_to_model_orient[2] =
				mesh->view_to_model_orient[2] * inverse_scale;
			inverse_scale = 1.0f / *scale_y;
			mesh->view_to_model_orient[3] =
				mesh->view_to_model_orient[3] * inverse_scale;
			mesh->view_to_model_orient[4] =
				mesh->view_to_model_orient[4] * inverse_scale;
			mesh->view_to_model_orient[5] =
				mesh->view_to_model_orient[5] * inverse_scale;
			inverse_scale = 1.0f / *scale_z;
			mesh->view_to_model_orient[6] =
				mesh->view_to_model_orient[6] * inverse_scale;
			mesh->view_to_model_orient[7] =
				mesh->view_to_model_orient[7] * inverse_scale;
			mesh->view_to_model_orient[8] =
				mesh->view_to_model_orient[8] * inverse_scale;
			break;
		}
		case OPT_MATERIAL_BINDING:
			if (current_node->payload_count == 8 ||
			    current_node->payload_count == 7) {
				memcpy(&mesh->per_vertex_materials,
				       &g_cur_mesh_materials,
				       sizeof(mesh->per_vertex_materials));
			} else if (current_node->payload_count == 6 ||
				   current_node->payload_count == 5) {
				memcpy(&mesh->per_face_materials,
				       &g_cur_mesh_materials,
				       sizeof(mesh->per_face_materials));
			} else {
				memcpy(&mesh->base_color_and_materials[3],
				       &g_cur_mesh_materials,
				       sizeof(mesh->base_color_and_materials
						      [3]));
			}
			break;
		case OPT_VERTNORMALS:
			g_cur_vert_normals = parameters;
			mesh->p_vert_normals = parameters;
			break;
		case OPT_TEXCOORDS:
			mesh->p_uvs = (struct opt_tex_coord *)node_data;
			break;
		case OPT_BASE_COLOR:
			mesh->base_color_and_materials[0] =
				((int *)node_data)[0];
			mesh->base_color_and_materials[1] =
				((int *)node_data)[1];
			mesh->base_color_and_materials[2] =
				((int *)node_data)[2];
			break;
		case OPT_TEXTURE: {
			mesh->p_texture_name = current_node->p_name;
			mesh->p_material = current_node->payload;
			g_cur_texture_desc =
				(struct opt_texture_data *)mesh->p_material;
			mesh->p_texels = mesh->p_material;
			mesh->p_texels = (uint8_t *)mesh->p_texels +
					 sizeof(struct opt_texture_data);
			if (g_cur_texture_desc->inline_palette_count != 0) {
				mesh->p_palette = mesh->p_texels;
				int palette_offset =
					((struct opt_texture_data *)
						 mesh->p_material)
						->width *
					((struct opt_texture_data *)
						 mesh->p_material)
						->height;
				if (((struct opt_texture_data *)
					     mesh->p_material)
					    ->texture_size == palette_offset) {
					palette_offset =
						((struct opt_texture_data *)
							 mesh->p_material)
							->data_size;
				}
				mesh->p_palette = (uint8_t *)mesh->p_texels +
						  palette_offset;
			} else {
				mesh->p_palette = g_cur_texture_desc->palette;
			}
			mesh->p_palette = (uint8_t *)mesh->p_palette +
					  OPT_INDEXED_SHADE_TABLE_SIZE;
			mesh->p_color_key_palette = (uint16_t *)mesh->p_palette;
			mesh->p_palette = (uint8_t *)mesh->p_palette -
					  OPT_INDEXED_SHADE_TABLE_SIZE;
			break;
		}
		case OPT_FACEGROUP:
			if (g_view_space_depth <= 0 ||
			    g_forced_lod_level != 0) {
				lod_child_selection = g_forced_lod_level;
				if (g_forced_lod_level == 0) {
					lod_child_selection = 1;
				} else if (current_node->child_count <
					   g_forced_lod_level) {
					lod_child_selection = -1;
				}
			} else {
				selection.lod_threshold = 1.0f;
				if (g_lod_distance_scale > 0.0f) {
					selection.lod_threshold =
						g_sw3d_unit_float /
						((float)g_view_space_depth *
						 g_lod_distance_scale);
				}
				lod_child_selection = 1;
				while (lod_child_selection <=
					       current_node->child_count &&
				       ((float *)
						node_data)[lod_child_selection -
							   1] >
					       selection.lod_threshold) {
					++lod_child_selection;
				}
				if (lod_child_selection >
				    current_node->child_count) {
					lod_child_selection = -1;
				}
			}
			break;
		case OPT_ROTSCALE:
			if (mesh->rot_angle != 0.0f) {
				struct opt_vector *pivot = parameters;
				struct opt_vector *axis = pivot + 1;
				float *pivot_y = &pivot->y;
				float *pivot_z = &pivot->z;
				mesh->eye_model_space_x -= pivot->x;
				mesh->eye_model_space_y -= *pivot_y;
				mesh->eye_model_space_z -= *pivot_z;
				mesh->view_pos_x += math3d_rotate_vec3x(
					&pivot->x, mesh->view_orient);
				mesh->view_pos_y += math3d_rotate_vec3y(
					&pivot->x, mesh->view_orient);
				mesh->view_pos_z += math3d_rotate_vec3z(
					&pivot->x, mesh->view_orient);
				float axis_angle[4];
				axis_angle[0] =
					axis->x * g_opt_axis_q15_to_float_scale;
				axis_angle[1] =
					axis->y * g_opt_axis_q15_to_float_scale;
				axis_angle[2] =
					axis->z * g_opt_axis_q15_to_float_scale;
				axis_angle[3] = mesh->rot_angle;
				float rotation_matrix[16];
				math3d_build_axis_angle_matrix(rotation_matrix,
							       axis_angle);
				math3d_mul_matrix3x3(mesh->view_to_model_orient,
						     rotation_matrix);
				math3d_rotate_vec3(&mesh->eye_model_space_x,
						   rotation_matrix);
				math3d_pre_mul_transposed_matrix3x3(
					mesh->view_orient, rotation_matrix);
				mesh->eye_model_space_x += pivot->x;
				mesh->eye_model_space_y += *pivot_y;
				mesh->eye_model_space_z += *pivot_z;
				mesh->view_pos_x -= math3d_rotate_vec3x(
					&pivot->x, mesh->view_orient);
				mesh->view_pos_y -= math3d_rotate_vec3y(
					&pivot->x, mesh->view_orient);
				mesh->view_pos_z -= math3d_rotate_vec3z(
					&pivot->x, mesh->view_orient);
			}
			break;
		case OPT_NODESWITCH:
			selection.node_switch_selection =
				g_node_switch_index + 1;
			if (selection.node_switch_selection >
			    current_node->child_count) {
				selection.node_switch_selection =
					current_node->child_count;
			}
			break;
		default:
			break;
		}
	} else {
		switch (current_node->node_type) {
		case OPT_MATERIAL_BINDING:
			if (current_node->payload_count == 8 ||
			    current_node->payload_count == 7) {
				memcpy(&mesh->per_vertex_materials,
				       &g_cur_mesh_materials,
				       sizeof(mesh->per_vertex_materials));
			} else if (current_node->payload_count == 6 ||
				   current_node->payload_count == 5) {
				memcpy(&mesh->per_face_materials,
				       &g_cur_mesh_materials,
				       sizeof(mesh->per_face_materials));
			} else {
				memcpy(&mesh->base_color_and_materials[3],
				       &g_cur_mesh_materials,
				       sizeof(mesh->base_color_and_materials
						      [3]));
			}
			break;
		case OPT_TEXTURE: {
			mesh->p_texture_name = current_node->p_name;
			mesh->p_material = current_node->payload;
			g_cur_texture_desc =
				(struct opt_texture_data *)mesh->p_material;
			mesh->p_texels = mesh->p_material;
			mesh->p_texels = (uint8_t *)mesh->p_texels +
					 sizeof(struct opt_texture_data);
			if (g_cur_texture_desc->inline_palette_count != 0) {
				mesh->p_palette = mesh->p_texels;
				int palette_offset =
					((struct opt_texture_data *)
						 mesh->p_material)
						->width *
					((struct opt_texture_data *)
						 mesh->p_material)
						->height;
				if (((struct opt_texture_data *)
					     mesh->p_material)
					    ->texture_size == palette_offset) {
					palette_offset =
						((struct opt_texture_data *)
							 mesh->p_material)
							->data_size;
				}
				mesh->p_palette = (uint8_t *)mesh->p_texels +
						  palette_offset;
			} else {
				mesh->p_palette = g_cur_texture_desc->palette;
			}
			mesh->p_palette = (uint8_t *)mesh->p_palette +
					  OPT_INDEXED_SHADE_TABLE_SIZE;
			mesh->p_color_key_palette = (uint16_t *)mesh->p_palette;
			mesh->p_palette = (uint8_t *)mesh->p_palette -
					  OPT_INDEXED_SHADE_TABLE_SIZE;
			break;
		}
		case OPT_NODESWITCH:
			selection.node_switch_selection =
				g_node_switch_index + 1;
			if (selection.node_switch_selection >
			    current_node->child_count) {
				selection.node_switch_selection =
					current_node->child_count;
			}
			break;
		case OPT_TEXCOORD_BINDING:
		default:
			break;
		}
	}

	if (current_node->child_count == 0) {
		return;
	}
	if (selection.node_switch_selection != 0) {
		++g_cur_layer_id;
		render_scene_draw_model_node(
			model,
			current_node
				->p_children[selection.node_switch_selection -
					     1],
			mesh);
	} else if (lod_child_selection != 0) {
		if (lod_child_selection != -1) {
			++g_cur_layer_id;
			render_scene_draw_model_node(
				model,
				current_node
					->p_children[lod_child_selection - 1],
				mesh);
		}
	} else {
		struct scene_mesh child_mesh = *mesh;
		g_cur_mesh_vertices = NULL;
		g_cur_mesh_tex_coords = NULL;
		g_cur_vert_normals = NULL;
		g_model_node_walk_unused_scratch2 = NULL;
		g_cur_mesh_materials = NULL;
		g_cur_vertex_count = 0;
		for (int child_index = 0;
		     child_index < current_node->child_count; ++child_index) {
			++g_cur_layer_id;
			render_scene_draw_model_node(
				model, current_node->p_children[child_index],
				&child_mesh);
		}
	}
}

/* Flips g_vertex_light_occlusion_enabled between 0 and 1. Nothing calls this. */
// FUNCTION: XVT 0x473550
void render_scene_toggle_vertex_light_occlusion(void)
{
	g_vertex_light_occlusion_enabled = !g_vertex_light_occlusion_enabled;
}

/* Returns g_vertex_light_occlusion_enabled. Nothing calls this. */
// FUNCTION: XVT 0x473570
int render_scene_get_vertex_light_occlusion_enabled(void)
{
	return g_vertex_light_occlusion_enabled;
}

/* Returns 1 when the segment from segment_start to segment_end crosses a face of
 * the object's model (render_scene_test_segment_against_model_node on each root),
 * else 0. Returns 0 at once while g_vertex_light_occlusion_enabled is 0, which it
 * always is. Otherwise unlocks the model's handle and locks it again, and
 * clears the model walk's g_cur globals. */
// FUNCTION: XVT 0x473580
int render_scene_is_segment_occluded_by_object_model(
	struct object_record *object, const struct opt_vector *segment_start,
	const struct opt_vector *segment_end)
{
	if (!g_vertex_light_occlusion_enabled) {
		return 0;
	}
	uint16_t model_handle = g_loaded_models[object->object_type];
	memory_handle_block_done_stub(model_handle);
	struct optimized_poly_object *model =
		(struct optimized_poly_object *)memory_get_handle_block(
			model_handle);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}
	struct scene_mesh mesh;
	memset(&mesh, 0, sizeof(mesh));
	mesh.p_object = object;
	mesh.view_orient[0] = 1.0f;
	mesh.view_orient[1] = 0.0f;
	mesh.view_orient[2] = 0.0f;
	mesh.view_orient[3] = 0.0f;
	mesh.view_orient[4] = 1.0f;
	mesh.view_orient[5] = 0.0f;
	mesh.view_orient[6] = 0.0f;
	mesh.view_orient[7] = 0.0f;
	mesh.view_orient[8] = 1.0f;
	mesh.view_to_model_orient[0] = 1.0f;
	mesh.view_to_model_orient[1] = 0.0f;
	mesh.view_to_model_orient[2] = 0.0f;
	mesh.view_to_model_orient[3] = 0.0f;
	mesh.view_to_model_orient[4] = 1.0f;
	mesh.view_to_model_orient[5] = 0.0f;
	mesh.view_to_model_orient[6] = 0.0f;
	mesh.view_to_model_orient[7] = 0.0f;
	mesh.view_to_model_orient[8] = 1.0f;
	g_cur_mesh_vertices = NULL;
	g_cur_mesh_tex_coords = NULL;
	g_cur_vert_normals = NULL;
	g_model_node_walk_unused_scratch2 = NULL;
	g_cur_mesh_materials = NULL;
	g_cur_vertex_count = 0;
	for (int root_index = 0; root_index < model->root_node_count;
	     ++root_index) {
		if (render_scene_test_segment_against_model_node(
			    model, model->root_nodes[root_index], &mesh,
			    segment_start, segment_end)) {
			return 1;
		}
	}
	return 0;
}

/* Returns 1 when the segment crosses a face at this node or below it, else 0.
 * Follows node references, returning 0 when one resolves to nothing; applies
 * transform, translation, rotation and scale nodes to mesh as
 * render_scene_draw_model_node does and sets vertices and normals; tests face data
 * with render_scene_test_segment_against_mesh_faces; then tests every child with a
 * copy of the mesh. It makes no face-group or node-switch choice, so every
 * child is tested, and the test reads the stored vertices, so the transforms it
 * applies do not move them. */
// FUNCTION: XVT 0x4736B0
int render_scene_test_segment_against_model_node(
	struct optimized_poly_object *model, const struct opt_node *node,
	struct scene_mesh *mesh, const struct opt_vector *segment_start,
	const struct opt_vector *segment_end)
{
	if (node == NULL) {
		return 0;
	}
	while (node->node_type == OPT_NODEREF) {
		node = opt_model_resolve_node_ref(model,
						  (const char *)node->payload);
		if (node == NULL) {
			return 0;
		}
	}
	struct opt_vector *node_payload = (struct opt_vector *)node->payload;
	if (node_payload != NULL) {
		switch (node->node_type) {
		case OPT_FACEDATA:
		case OPT_FACEDATA_QUAD_MESH:
		case OPT_FACEDATA_FACE_SET:
		case OPT_FACEDATA_TRIANGLE_STRIP_SET: {
			struct opt_packed_face_data *face_data =
				(struct opt_packed_face_data *)node_payload;

			mesh->face_count = node->payload_count;
			mesh->edge_count = face_data->edge_count;
			node_payload = (struct opt_vector *)face_data->records;
			struct face_record *face_geometry =
				(struct face_record *)node_payload;
			mesh->p_face_geom = face_geometry;
			struct opt_vector *face_normals =
				(struct opt_vector
					 *)&face_geometry[node->payload_count];
			mesh->p_face_normals = face_normals;
			struct face_texture_gradients *texturing =
				(struct face_texture_gradients
					 *)&face_normals[node->payload_count];
			mesh->p_face_texturing = texturing;
			struct opt_vector *generated_normals =
				&texturing[node->payload_count].u_axis;
			struct opt_vector **vertex_normals =
				&mesh->p_vert_normals;
			int hit;
			if (*vertex_normals == NULL) {
				mesh->p_vert_normals = generated_normals;
				hit = render_scene_test_segment_against_mesh_faces(
					mesh, segment_start, segment_end);
				if (hit) {
					return 1;
				}
				mesh->p_vert_normals = NULL;
			} else {
				hit = render_scene_test_segment_against_mesh_faces(
					mesh, segment_start, segment_end);
				if (hit) {
					return 1;
				}
			}
			break;
		}
		case OPT_TRANSFORM:
			math3d_mul_matrix3x3(mesh->view_orient,
					     &node_payload[1].x);
			math3d_rotate_vec3(&mesh->view_pos_x,
					   &node_payload[1].x);
			mesh->view_pos_x = render_scene_add_translation(
				mesh->view_pos_x, node_payload->x);
			mesh->view_pos_y += node_payload->y;
			mesh->view_pos_z += node_payload->z;
			math3d_pre_mul_transposed_matrix3x3(
				mesh->view_to_model_orient, &node_payload[1].x);
			mesh->eye_model_space_x -= math3d_rotate_vec3x(
				&node_payload->x, mesh->view_to_model_orient);
			mesh->eye_model_space_y -= math3d_rotate_vec3y(
				&node_payload->x, mesh->view_to_model_orient);
			mesh->eye_model_space_z -= math3d_rotate_vec3z(
				&node_payload->x, mesh->view_to_model_orient);
			break;
		case OPT_MESHVERTS:
			mesh->vertex_count = node->payload_count;
			mesh->p_model_verts = node_payload;
			break;
		case OPT_TRANSLATION:
			mesh->view_pos_x = render_scene_add_translation(
				mesh->view_pos_x, node_payload->x);
			mesh->view_pos_y += node_payload->y;
			mesh->view_pos_z += node_payload->z;
			mesh->eye_model_space_x -= math3d_rotate_vec3x(
				&node_payload->x, mesh->view_to_model_orient);
			mesh->eye_model_space_y -= math3d_rotate_vec3y(
				&node_payload->x, mesh->view_to_model_orient);
			mesh->eye_model_space_z -= math3d_rotate_vec3z(
				&node_payload->x, mesh->view_to_model_orient);
			break;
		case OPT_ROTATION:
			math3d_mul_matrix3x3(mesh->view_orient,
					     (const float *)node_payload);
			math3d_rotate_vec3(&mesh->view_pos_x, &node_payload->x);
			math3d_pre_mul_transposed_matrix3x3(
				mesh->view_to_model_orient, &node_payload->x);
			break;
		case OPT_SCALE: {
			float *scale_x = &node_payload->x;
			float *scale_y = &node_payload->y;
			float *scale_z = &node_payload->z;
			float *orientation = mesh->view_to_model_orient;

			mesh->view_orient[0] *= *scale_x;
			mesh->view_orient[1] *= *scale_y;
			mesh->view_orient[2] *= *scale_z;
			mesh->view_orient[3] *= *scale_x;
			mesh->view_orient[4] *= *scale_y;
			mesh->view_orient[5] *= *scale_z;
			mesh->view_orient[6] *= *scale_x;
			mesh->view_orient[7] *= *scale_y;
			mesh->view_orient[8] *= *scale_z;
			mesh->view_pos_x *= *scale_x;
			mesh->view_pos_y *= *scale_y;
			mesh->view_pos_z *= *scale_z;
			float inverse_scale = 1.0f / *scale_x;
			mesh->view_to_model_orient[0] =
				orientation[0] * inverse_scale;
			mesh->view_to_model_orient[1] =
				orientation[1] * inverse_scale;
			mesh->view_to_model_orient[2] =
				orientation[2] * inverse_scale;
			inverse_scale = 1.0f / *scale_y;
			mesh->view_to_model_orient[3] =
				orientation[3] * inverse_scale;
			mesh->view_to_model_orient[4] =
				orientation[4] * inverse_scale;
			mesh->view_to_model_orient[5] =
				orientation[5] * inverse_scale;
			inverse_scale = 1.0f / *scale_z;
			mesh->view_to_model_orient[6] =
				orientation[6] * inverse_scale;
			mesh->view_to_model_orient[7] =
				orientation[7] * inverse_scale;
			mesh->view_to_model_orient[8] =
				orientation[8] * inverse_scale;
			break;
		}
		case OPT_VERTNORMALS:
			g_cur_vert_normals = node_payload;
			mesh->p_vert_normals = node_payload;
			break;
		default:
			break;
		}
	}

	if (node->child_count != 0) {
		struct scene_mesh child_mesh = *mesh;
		g_cur_mesh_vertices = NULL;
		g_cur_mesh_tex_coords = NULL;
		g_cur_vert_normals = NULL;
		g_model_node_walk_unused_scratch2 = NULL;
		g_cur_mesh_materials = NULL;
		g_cur_vertex_count = 0;
		int child_index = 0;
		if (node->child_count > 0) {
			do {
				if (render_scene_test_segment_against_model_node(
					    model,
					    node->p_children[child_index],
					    &child_mesh, segment_start,
					    segment_end)) {
					return 1;
				}
				++child_index;
			} while (child_index < node->child_count);
		}
	}
	return 0;
}

/* Returns 1 when the segment crosses one of the mesh's faces, else 0. Skips a
 * face whose corners all lie at or beyond both ends on one side on x, y or z.
 * Then needs the start 40 or more from the face's plane, measured along its
 * normal, and the end strictly on the other side. Puts the crossing at start +
 * (end - start) * (-ds / de), ds and de being the ends' distances from the
 * plane; drops one axis, picked by comparing the normal's components as signed
 * values (z when x and y are both under z, else y when x is under y and y is
 * over z, else x); and counts the point inside when the cross products with all
 * the edges have the same sign, 0 counting as positive. A face whose
 * vertex_idx[3] is -1 is a triangle. */
// FUNCTION: XVT 0x473AD0
int render_scene_test_segment_against_mesh_faces(
	const struct scene_mesh *mesh, const struct opt_vector *segment_start,
	const struct opt_vector *segment_end)
{
	/* A face with vertex_idx[3] == -1 is a triangle, so its scaled base index is -3. */
	const struct face_record *faces = mesh->p_face_geom;
	const struct opt_vector *normals = mesh->p_face_normals;
	const float *coordinates = &mesh->p_model_verts[0].x;
	struct opt_vector start;

	start.x = segment_start->x;
	start.y = segment_start->y;
	start.z = segment_start->z;
	struct opt_vector end;
	end.x = segment_end->x;
	end.y = segment_end->y;
	end.z = segment_end->z;

	for (int face_index = 0; face_index < mesh->face_count;
	     ++face_index, ++faces) {
		int base0 = faces->vertex_idx[0] * 3;
		int base1 = faces->vertex_idx[1] * 3;
		int base2 = faces->vertex_idx[2] * 3;
		int base3 = faces->vertex_idx[3] * 3;
		const struct opt_vector *normal = normals++;

		if (coordinates[base0] <= start.x &&
		    coordinates[base0] <= end.x) {
			if (coordinates[base1] <= start.x &&
			    coordinates[base1] <= end.x &&
			    coordinates[base2] <= start.x &&
			    coordinates[base2] <= end.x &&
			    (base3 == -3 || (coordinates[base3] <= start.x &&
					     coordinates[base3] <= end.x))) {
				continue;
			}
		} else if (coordinates[base0] >= start.x &&
			   coordinates[base0] >= end.x &&
			   coordinates[base1] >= start.x &&
			   coordinates[base1] >= end.x &&
			   coordinates[base2] >= start.x &&
			   coordinates[base2] >= end.x &&
			   (base3 == -3 || (coordinates[base3] >= start.x &&
					    coordinates[base3] >= end.x))) {
			continue;
		}
		++base0;
		++base1;
		++base2;
		++base3;
		if (coordinates[base0] <= start.y &&
		    coordinates[base0] <= end.y) {
			if (coordinates[base1] <= start.y &&
			    coordinates[base1] <= end.y &&
			    coordinates[base2] <= start.y &&
			    coordinates[base2] <= end.y &&
			    (base3 == -2 || (coordinates[base3] <= start.y &&
					     coordinates[base3] <= end.y))) {
				continue;
			}
		} else if (coordinates[base0] >= start.y &&
			   coordinates[base0] >= end.y &&
			   coordinates[base1] >= start.y &&
			   coordinates[base1] >= end.y &&
			   coordinates[base2] >= start.y &&
			   coordinates[base2] >= end.y &&
			   (base3 == -2 || (coordinates[base3] >= start.y &&
					    coordinates[base3] >= end.y))) {
			continue;
		}
		++base0;
		++base1;
		++base2;
		++base3;
		if (coordinates[base0] <= start.z &&
		    coordinates[base0] <= end.z) {
			if (coordinates[base1] <= start.z &&
			    coordinates[base1] <= end.z &&
			    coordinates[base2] <= start.z &&
			    coordinates[base2] <= end.z &&
			    (base3 == -1 || (coordinates[base3] <= start.z &&
					     coordinates[base3] <= end.z))) {
				continue;
			}
		} else if (coordinates[base0] >= start.z &&
			   coordinates[base0] >= end.z &&
			   coordinates[base1] >= start.z &&
			   coordinates[base1] >= end.z &&
			   coordinates[base2] >= start.z &&
			   coordinates[base2] >= end.z &&
			   (base3 == -1 || (coordinates[base3] >= start.z &&
					    coordinates[base3] >= end.z))) {
			continue;
		}

		float distance_start =
			(start.x - coordinates[base0 - 2]) * normal->x +
			normal->z * (start.z - coordinates[base0]) +
			normal->y * (start.y - coordinates[base0 - 1]);
		float distance_end =
			(end.x - coordinates[base0 - 2]) * normal->x +
			normal->z * (end.z - coordinates[base0]) +
			normal->y * (end.y - coordinates[base0 - 1]);
		if (distance_start >= 0.0f) {
			if (distance_start < 40.0f || distance_end >= 0.0f) {
				continue;
			}
		} else if (distance_start > -40.0f || distance_end <= 0.0f) {
			continue;
		}

		/* distance_start becomes the factor used below to place the hit
		 * point between start and end. */
		distance_start = (-distance_start) / distance_end;

		int v_index0;
		int v_index1;
		int v_index2;
		int v_index3;
		float hit_u;
		float hit_v;
		if (normal->x < normal->z && normal->y < normal->z) {
			hit_u = (end.x - start.x) * distance_start + start.x;
			hit_v = (end.y - start.y) * distance_start + start.y;
			base0 -= 2;
			base1 -= 2;
			base2 -= 2;
			base3 -= 2;
			v_index0 = base0 + 1;
			v_index1 = base1 + 1;
			v_index2 = base2 + 1;
			v_index3 = base3 + 1;
		} else if (normal->x < normal->y && normal->y > normal->z) {
			hit_u = (end.x - start.x) * distance_start + start.x;
			hit_v = (end.z - start.z) * distance_start + start.z;
			base0 -= 2;
			base1 -= 2;
			base2 -= 2;
			base3 -= 2;
			v_index0 = base0 + 2;
			v_index1 = base1 + 2;
			v_index2 = base2 + 2;
			v_index3 = base3 + 2;
		} else {
			hit_u = (end.y - start.y) * distance_start + start.y;
			hit_v = (end.z - start.z) * distance_start + start.z;
			base0 -= 1;
			base1 -= 1;
			base2 -= 1;
			base3 -= 1;
			v_index0 = base0 + 1;
			v_index1 = base1 + 1;
			v_index2 = base2 + 1;
			v_index3 = base3 + 1;
		}

		float cross0 =
			(hit_u - coordinates[base0]) * (coordinates[v_index1] -
							coordinates[v_index0]) -
			(coordinates[base1] - coordinates[base0]) *
				(hit_v - coordinates[v_index0]);
		float cross1 =
			(hit_u - coordinates[base1]) * (coordinates[v_index2] -
							coordinates[v_index1]) -
			(coordinates[base2] - coordinates[base1]) *
				(hit_v - coordinates[v_index1]);
		if (cross0 < 0.0f) {
			if (cross1 >= 0.0f) {
				continue;
			}
		} else if (cross1 < 0.0f) {
			continue;
		}
		float cross2;
		if (base3 < 0) {
			cross2 = (hit_u - coordinates[base2]) *
					 (coordinates[v_index0] -
					  coordinates[v_index2]) -
				 (coordinates[base0] - coordinates[base2]) *
					 (hit_v - coordinates[v_index2]);
			if (cross0 < 0.0f) {
				if (cross2 >= 0.0f) {
					continue;
				}
			} else if (cross2 < 0.0f) {
				continue;
			}
		} else {
			cross2 = (hit_u - coordinates[base2]) *
					 (coordinates[v_index3] -
					  coordinates[v_index2]) -
				 (coordinates[base3] - coordinates[base2]) *
					 (hit_v - coordinates[v_index2]);
			if (cross0 < 0.0f) {
				if (cross2 >= 0.0f) {
					continue;
				}
			} else if (cross2 < 0.0f) {
				continue;
			}
			float cross3 =
				(hit_u - coordinates[base3]) *
					(coordinates[v_index0] -
					 coordinates[v_index3]) -
				(coordinates[base0] - coordinates[base3]) *
					(hit_v - coordinates[v_index3]);
			if (cross0 < 0.0f) {
				if (cross3 >= 0.0f) {
					continue;
				}
			} else if (cross3 < 0.0f) {
				continue;
			}
		}
		return 1;
	}
	return 0;
}

/* Allocates the scene's memory handles, calling
 * fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY) when one fails: 20000
 * spans, 20000 span pointers, 5000 faces, twice g_vertex_remap_capacity projected
 * vertices, twice g_scene_edge_flags_capacity edges, g_vertex_remap_capacity remap
 * entries, g_scene_edge_flags_capacity edge flags, 768 edge pointers, 768 row
 * heads, 0x25800 bytes of light samples and 500 queued meshes. Sets those
 * capacities and the sw3d light-sample block, 16 pixels. In the original build
 * it then makes 0x80000 bytes from 0x217 bytes past its own start readable,
 * writable and executable; the modern build's memory_set_region_execute_read_write
 * does nothing. */
// FUNCTION: XVT 0x485CD0
void render_scene_allocate_buffers(void)
{
#ifndef XVT_MODERN
	int saved_edge_max;
#endif

	g_scene_span_data_capacity = 20000;
	g_scene_span_data_handle = memory_alloc_handle(
		sizeof(struct scene_span) * g_scene_span_data_capacity, 0);
	if (g_scene_span_data_handle == 0) {
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	g_scene_span_ptr_capacity = 20000;
	g_scene_span_ptr_list_handle = memory_alloc_handle(
		sizeof(struct scene_span *) * g_scene_span_ptr_capacity, 0);
	if (g_scene_span_ptr_list_handle == 0) {
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	g_scene_face_max = 5000;
	g_vis_face_list_handle = memory_alloc_handle(
		sizeof(struct scene_face) * g_scene_face_max, 0);
	if (g_vis_face_list_handle == 0) {
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	g_proj_vert_max = 2 * g_vertex_remap_capacity;
	g_proj_vert_list_handle = memory_alloc_handle(
		sizeof(struct proj_vertex) * g_proj_vert_max, 0);
	if (g_proj_vert_list_handle == 0) {
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	int edge_max = 2 * g_scene_edge_flags_capacity;
#ifdef XVT_MODERN
	g_scene_edge_max = edge_max;
	g_scene_edge_list_handle = memory_alloc_handle(
		sizeof(*g_scene_edge_list) * g_scene_edge_max, 0);
#else
	saved_edge_max = edge_max;
	g_scene_edge_max = edge_max;
	/* From here edge_max holds the list's size in bytes, 28 per edge, not a count. */
	edge_max <<= 3;
	edge_max -= saved_edge_max;
	edge_max <<= 2;
	g_scene_edge_list_handle = memory_alloc_handle(edge_max, 0);
#endif
	if (g_scene_edge_list_handle == 0) {
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	g_vertex_remap_handle = memory_alloc_handle(
		sizeof(*g_vertex_remap) * g_vertex_remap_capacity, 0);
	if (g_vertex_remap_handle == 0) {
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	g_scene_edge_flags_handle = memory_alloc_handle(
		sizeof(*g_scene_edge_flags) * g_scene_edge_flags_capacity, 0);
	if (g_scene_edge_flags_handle == 0) {
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	g_scene_scl_edge_list_handle =
		memory_alloc_handle(sizeof(*g_scene_scl_edge_list) * 768, 0);
	if (g_scene_scl_edge_list_handle == 0) {
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	g_scanline_span_heads_handle =
		memory_alloc_handle(sizeof(*g_scanline_span_heads) * 768, 0);
	if (g_scanline_span_heads_handle == 0) {
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	g_sw3d_light_sample_block_size = 16;
	g_sw3d_light_sample_inv_block_size = g_sw3d_span_length_reciprocal[16];
	g_sw3d_light_sample_block_shift = 4;
	g_sw3d_light_sample_block_mask = 15;
	g_sw3d_light_sample_block_size_float = 16.0f;
	g_scene_light_sample_data_handle = memory_alloc_handle(0x25800, 0);
	if (g_scene_light_sample_data_handle == 0) {
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	g_mesh_queue_max = 500;
	g_mesh_queue_handle = memory_alloc_handle(
		sizeof(struct scene_mesh) * g_mesh_queue_max, 0);
	if (g_mesh_queue_handle == 0) {
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
	}

	/* The original inline assembly captured the address of the following code label. */
	void *code_address_value =
		(uint8_t *)(void *)render_scene_allocate_buffers + 0x217;
	void *code_address[1];
	memcpy(code_address, &code_address_value, sizeof(code_address_value));
	memory_set_region_execute_read_write(code_address[0], 0x80000);
}

/* Starts a pass of scene drawing. The original build saves the x87 control word
 * in g_sw3d_initialize_scene_saved_fpu_control and sets extended precision; the
 * modern build clears the 0x300 bits of g_sw3d_fpu_control_word_scratch. Gives
 * g_sw3d_cockpit_mask_sentinel_face a scaled_inverse_depth and a 1-over-depth plane
 * of 1e32, and locks every scene buffer. With reset_scene_state nonzero it
 * empties the face list, span pointers, light-sample rows and mesh queue, and
 * rebuilds g_scanline_span_heads from the viewport span mask: one span with the
 * sentinel face for each run the mask marks. With 0 it starts the new pass at
 * g_vis_face_count. Then sets g_light_sample_slot_stride, adds g_flight_vp_height to
 * g_sw3d_light_sample_cache_scene_stamp_base, sets g_inv_proj_scale, and in the
 * hardware path calls render_scene_init_hardware_frame. */
// FUNCTION: XVT 0x485F00
void render_scene_initialize(int reset_scene_state)
{
#ifndef XVT_MODERN
	g_sw3d_initialize_scene_saved_fpu_control = _control87(0, 0);
	_control87(0, 0x30000);
#else
	g_sw3d_fpu_control_word_scratch &= 0xFFFFFCFF;
#endif
	g_sw3d_cockpit_mask_sentinel_face.max_scaled_inverse_depth = 1.0e32f;
	g_sw3d_cockpit_mask_sentinel_face.min_scaled_inverse_depth = 1.0e32f;
	g_sw3d_cockpit_mask_sentinel_face.gradients[8] = 1.0e32f;
	g_sw3d_cockpit_mask_sentinel_face.gradients[6] = 0.0f;
	g_sw3d_cockpit_mask_sentinel_face.gradients[7] = 0.0f;
	g_scene_span_data_base =
		memory_get_handle_block(g_scene_span_data_handle);
	g_scene_span_ptr_list =
		memory_get_handle_block(g_scene_span_ptr_list_handle);
	g_vis_face_list = memory_get_handle_block(g_vis_face_list_handle);
	g_proj_vert_list = memory_get_handle_block(g_proj_vert_list_handle);
	g_scene_edge_list = memory_get_handle_block(g_scene_edge_list_handle);
	g_vertex_remap = memory_get_handle_block(g_vertex_remap_handle);
	g_scene_edge_flags = memory_get_handle_block(g_scene_edge_flags_handle);
	g_scene_scl_edge_list =
		memory_get_handle_block(g_scene_scl_edge_list_handle);
	g_scanline_span_heads =
		memory_get_handle_block(g_scanline_span_heads_handle);
	g_scene_light_sample_data =
		memory_get_handle_block(g_scene_light_sample_data_handle);
	g_mesh_queue = memory_get_handle_block(g_mesh_queue_handle);
	if (reset_scene_state != 0) {
		g_vis_face_pass_start = 0;
		g_scene_span_ptr_avail = g_scene_span_ptr_capacity;
		g_p_scene_span_data_cur = g_scene_span_data_base;
		g_vis_face_count = 0;
		g_light_sample_slot_index = 0;
		g_mesh_queue_index = 0;
		uint8_t *mask =
			&g_flight_aux_buffer[g_viewport_span_mask_offset];
		unsigned int scan_y = 0;
		g_p_scene_span_data_end =
			&g_scene_span_data_base[g_scene_span_data_capacity - 1];
		if (g_flight_vp_height != 0) {
			int scanline = 0;
			do {
				unsigned int scan_x = 0;
				g_scanline_span_heads[scanline] = NULL;
				int8_t run_type = (int8_t)*mask++;
				struct scene_span *previous_span =
					g_scanline_span_heads[scanline];
				if (g_flight_vp_width != 0) {
					do {
						int run_length = *mask++;
						if (run_length == 0) {
							run_length = *mask++;
							if (run_length == 0) {
								run_length =
									*mask++ +
									256;
							}
							run_length += 255;
						}
						if (run_type < 0) {
							if (previous_span !=
							    NULL) {
								previous_span
									->next =
									g_p_scene_span_data_cur;
							} else {
								g_scanline_span_heads
									[scanline] =
										g_p_scene_span_data_cur;
							}
							previous_span =
								g_p_scene_span_data_cur++;
							previous_span->x_start =
								scan_x;
							previous_span->x_end =
								scan_x +
								run_length;
							previous_span->face =
								&g_sw3d_cockpit_mask_sentinel_face;
							previous_span->next =
								NULL;
						}
						run_type = -run_type;
						scan_x += run_length;
					} while (scan_x < g_flight_vp_width);
				}
				++scanline;
				++scan_y;
			} while (scan_y < g_flight_vp_height);
		}
	} else {
		g_vis_face_pass_start = g_vis_face_count;
	}
	g_light_sample_slot_stride = ((unsigned int)g_flight_vp_width +
				      g_sw3d_light_sample_block_size - 1) /
				     g_sw3d_light_sample_block_size;
	g_sw3d_light_sample_cache_scene_stamp_base += g_flight_vp_height;
	g_inv_proj_scale = 1.0f / (float)(unsigned int)g_proj_scale_int;
	if (g_use_hardware3d != 0) {
		render_scene_init_hardware_frame();
	}
}

/* Unlocks the eleven scene buffers and sets their pointers to NULL. Returns
 * 0. */
// FUNCTION: XVT 0x486200
int render_scene_unlock_buffers(void)
{
	memory_handle_block_done_stub(g_scene_span_data_handle);
	memory_handle_block_done_stub(g_scene_span_ptr_list_handle);
	memory_handle_block_done_stub(g_vis_face_list_handle);
	memory_handle_block_done_stub(g_proj_vert_list_handle);
	memory_handle_block_done_stub(g_scene_edge_list_handle);
	memory_handle_block_done_stub(g_vertex_remap_handle);
	memory_handle_block_done_stub(g_scene_edge_flags_handle);
	memory_handle_block_done_stub(g_scene_scl_edge_list_handle);
	memory_handle_block_done_stub(g_scanline_span_heads_handle);
	memory_handle_block_done_stub(g_scene_light_sample_data_handle);
	memory_handle_block_done_stub(g_mesh_queue_handle);
	g_scene_span_data_base = NULL;
	g_scene_span_ptr_list = NULL;
	g_vis_face_list = NULL;
	g_proj_vert_list = NULL;
	g_scene_edge_list = NULL;
	g_vertex_remap = NULL;
	g_scene_edge_flags = NULL;
	g_scene_scl_edge_list = NULL;
	g_scanline_span_heads = NULL;
	g_scene_light_sample_data = NULL;
	g_mesh_queue = NULL;
	return 0;
}

/* Frees each of the eleven scene memory handles that is nonzero and sets all
 * eleven to 0. */
// FUNCTION: XVT 0x4862E0
void render_scene_free_buffers(void)
{
	if (g_scene_span_data_handle != 0) {
		memory_free_handle(render_scene_get_memory_handle(
			&g_scene_span_data_handle));
	}
	g_scene_span_data_handle = 0;
	if (g_scene_span_ptr_list_handle != 0) {
		memory_free_handle(render_scene_get_memory_handle(
			&g_scene_span_ptr_list_handle));
	}
	g_scene_span_ptr_list_handle = 0;
	if (g_vis_face_list_handle != 0) {
		memory_free_handle(render_scene_get_memory_handle(
			&g_vis_face_list_handle));
	}
	g_vis_face_list_handle = 0;
	if (g_proj_vert_list_handle != 0) {
		memory_free_handle(render_scene_get_memory_handle(
			&g_proj_vert_list_handle));
	}
	g_proj_vert_list_handle = 0;
	if (g_scene_edge_list_handle != 0) {
		memory_free_handle(render_scene_get_memory_handle(
			&g_scene_edge_list_handle));
	}
	g_scene_edge_list_handle = 0;
	if (g_vertex_remap_handle != 0) {
		memory_free_handle(
			render_scene_get_memory_handle(&g_vertex_remap_handle));
	}
	g_vertex_remap_handle = 0;
	if (g_scene_edge_flags_handle != 0) {
		memory_free_handle(render_scene_get_memory_handle(
			&g_scene_edge_flags_handle));
	}
	g_scene_edge_flags_handle = 0;
	if (g_scene_scl_edge_list_handle != 0) {
		memory_free_handle(render_scene_get_memory_handle(
			&g_scene_scl_edge_list_handle));
	}
	g_scene_scl_edge_list_handle = 0;
	if (g_scanline_span_heads_handle != 0) {
		memory_free_handle(render_scene_get_memory_handle(
			&g_scanline_span_heads_handle));
	}
	g_scanline_span_heads_handle = 0;
	if (g_scene_light_sample_data_handle != 0) {
		memory_free_handle(render_scene_get_memory_handle(
			&g_scene_light_sample_data_handle));
	}
	g_scene_light_sample_data_handle = 0;
	if (g_mesh_queue_handle != 0) {
		memory_free_handle(
			render_scene_get_memory_handle(&g_mesh_queue_handle));
	}
	g_mesh_queue_handle = 0;
}
