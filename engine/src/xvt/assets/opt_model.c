#include "xvt/assets/opt_model.h"

#include "xvt_runtime/log/log.h"
#include "xvt_runtime/snapshot/render_assets.h"
#include "xvt/assets/file.h"
#include "xvt_runtime/assets/opt_native.h"
#include "xvt/assets/model_texture.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_loading.h"
#include "xvt/math/math3d.h"
#include "xvt/render/color.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/image_quantizer.h"
#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"
#include "xvt/util/debug_console.h"
#include "xvt/util/memory.h"
#include "xvt_runtime/log/log_both_builds.h"
int access(const char *filename, int mode);

/* One row per model, indexed by model_index: names, flight and combat figures,
 * weapon groups and points; the starting values come from model_defs_data.inc.
 * fe_disk_io_build_model_def fills the bound sizes and the dock, hangar, primary
 * and weapon points from each loaded OPT model, and string_table_load_game_strings
 * sets each name_long. */
// GLOBAL: XVT 0x51C560
struct model_def g_model_defs[73] = {
/* drift-ok: include-midfile -- the table's rows, not a header */
#include "xvt/assets/model_defs_data.inc"
};
/* The float 1.0; the software renderer and this file's normal builders read
 * it. */
// GLOBAL: XVT 0x5181C0
const float g_sw3d_unit_float = 1.0f;
/* The float 3.0, the corner count sw3d_project_mesh_vertices divides by a
 * triangle's summed corner w values. */
// GLOBAL: XVT 0x5181C4
const float g_sw3d_triangle_corner_count = 3.0f;
/* The float 4.0, the corner count sw3d_project_mesh_vertices divides by a quad's
 * summed corner w values. */
// GLOBAL: XVT 0x5181C8
const float g_sw3d_quad_corner_count = 4.0f;
/* The float 0.0; the software renderer, render_scene_cull_mesh_faces_from_view and
 * this file's normal builders compare with it. */
// GLOBAL: XVT 0x5181B8
const float g_sw3d_zero_float = 0.0f;
/* The float 100000.0: sw3d_project_mesh_vertices_distant multiplies its projection
 * scale by it and adds it to each transformed vertex's z. */
// GLOBAL: XVT 0x5181CC
const float g_sw3d_distant_depth = 100000.0f;
/* 1 while the model walkers keep an OPT_NODEREF node's target in the node after
 * the first lookup: the original build in its pName, blanking the name, the
 * modern build through xvt_opt_resolve_cached. Nothing writes it, so it stays
 * 1. */
// GLOBAL: XVT 0x5270B0
int g_cache_resolved_opt_node_refs = 1;
/* 1 while the model being loaded came from a version 0 file, whose face records
 * carry no normal indices: 48 bytes each where version 1 has 64. Only
 * opt_model_load_file_to_handle writes it; the legacy converter reads it, and
 * opt_model_save_handle_to_file picks its version marker from it. */
// GLOBAL: XVT 0x5272B0
int g_opt_source_is_version0 = 0;
/* 1 / n at index n, from 1 to 69, with 1.0 at index 0; read by the software
 * renderer, flight_starfield_render, render_scene_allocate_buffers and
 * opt_model_build_vertex_normals_from_faces. */
// GLOBAL: XVT 0x5270B8
const float g_sw3d_span_length_reciprocal[70] = {
	1.0f,	      1.0f,	    1.0f / 2.0f,  1.0f / 3.0f,	1.0f / 4.0f,
	1.0f / 5.0f,  1.0f / 6.0f,  1.0f / 7.0f,  1.0f / 8.0f,	1.0f / 9.0f,
	1.0f / 10.0f, 1.0f / 11.0f, 1.0f / 12.0f, 1.0f / 13.0f, 1.0f / 14.0f,
	1.0f / 15.0f, 1.0f / 16.0f, 1.0f / 17.0f, 1.0f / 18.0f, 1.0f / 19.0f,
	1.0f / 20.0f, 1.0f / 21.0f, 1.0f / 22.0f, 1.0f / 23.0f, 1.0f / 24.0f,
	1.0f / 25.0f, 1.0f / 26.0f, 1.0f / 27.0f, 1.0f / 28.0f, 1.0f / 29.0f,
	1.0f / 30.0f, 1.0f / 31.0f, 1.0f / 32.0f, 1.0f / 33.0f, 1.0f / 34.0f,
	1.0f / 35.0f, 1.0f / 36.0f, 1.0f / 37.0f, 1.0f / 38.0f, 1.0f / 39.0f,
	1.0f / 40.0f, 1.0f / 41.0f, 1.0f / 42.0f, 1.0f / 43.0f, 1.0f / 44.0f,
	1.0f / 45.0f, 1.0f / 46.0f, 1.0f / 47.0f, 1.0f / 48.0f, 1.0f / 49.0f,
	1.0f / 50.0f, 1.0f / 51.0f, 1.0f / 52.0f, 1.0f / 53.0f, 1.0f / 54.0f,
	1.0f / 55.0f, 1.0f / 56.0f, 1.0f / 57.0f, 1.0f / 58.0f, 1.0f / 59.0f,
	1.0f / 60.0f, 1.0f / 61.0f, 1.0f / 62.0f, 1.0f / 63.0f, 1.0f / 64.0f,
	1.0f / 65.0f, 1.0f / 66.0f, 1.0f / 67.0f, 1.0f / 68.0f, 1.0f / 69.0f,
};
/* Per object type, the Memory handle of its flight resource, a runtime model
 * from opt_model_load_handle or a texture block; 0 for none.
 * fe_disk_io_load_resources fills it, fe_disk_io_free_flight_resources clears it,
 * model_preview_load_model keeps the preview model in slot 0, and the modern
 * xvt_frontend_task_shutdown clears it. */
// GLOBAL: XVT 0x9A7ED0
uint16_t g_loaded_models[201] = {0};
/* Where opt_model_remap_vector_index starts its next search: the index it last
 * found, or the list length after a miss. Only that function writes it, and
 * nothing resets it between models. */
// GLOBAL: XVT 0x60F1E4
static int g_opt_convert_vector_search_cursor = 0;
/* Where opt_model_remap_tex_coord_index starts its next search: the index it last
 * found, or the list length after a miss. Only that function writes it, and
 * nothing resets it between models. */
// GLOBAL: XVT 0x60F1FC
static int g_opt_convert_tex_coord_search_cursor = 0;
/* Handle of the shared block a model file is loaded and converted in; kept and
 * regrown from one load to the next, 0 before the first. Only
 * opt_model_load_file_to_handle and opt_model_convert_legacy_model_to_optimized write
 * it. */
// GLOBAL: XVT 0x5272A8
static uint16_t g_load_opt_buf_handle = 0;
/* Bytes allocated for g_load_opt_buf_handle (the decoded size in the modern
 * build); written by the same two functions. */
// GLOBAL: XVT 0x5272AC
static int g_load_opt_buf_size = 0;
/* Handle of the copy opt_model_convert_legacy_model_to_optimized converts from; it
 * alone writes it, keeps it between calls, regrows it and never frees it. */
// GLOBAL: XVT 0x5272B4
static uint16_t g_opt_convert_source_handle = 0;
/* Bytes allocated for g_opt_convert_source_handle. */
// GLOBAL: XVT 0x5272B8
static unsigned int g_opt_convert_source_buf_size = 0;
/* Vertex count of the OPT_MESHVERTS node the model walkers last passed. Many
 * functions write it, chiefly this file's walkers and RenderScene's, which set
 * it to 0 before a walk. */
// GLOBAL: XVT 0x60F204
int g_cur_vertex_count = 0;
/* Vertex list of the mesh node the model walkers last passed; the converters
 * and the normal builders read it. Set to NULL before each walk, here and in
 * RenderScene's walkers. */
// GLOBAL: XVT 0x60F1C0
void *g_cur_mesh_vertices = NULL;
/* Material records of the OPT_MATERIAL node the model walkers last passed. Set
 * to NULL before each walk, here and in RenderScene's walkers. */
// GLOBAL: XVT 0x60F1D8
void *g_cur_mesh_materials = NULL;
/* Texture coordinates of the OPT_TEXCOORDS node the model walkers last passed.
 * Set to NULL before each walk, here and in RenderScene's walkers. */
// GLOBAL: XVT 0x60F1E8
void *g_cur_mesh_tex_coords = NULL;
/* Every model walker sets it to NULL before it walks; nothing sets it to
 * anything else or reads it. */
// GLOBAL: XVT 0x60F1F0
void *g_model_node_walk_unused_scratch2 = NULL;
/* Vertex normals of the OPT_VERTNORMALS node the model walkers last passed. Set
 * to NULL before each walk, here and in RenderScene's walkers. */
// GLOBAL: XVT 0x60F208
struct opt_vector *g_cur_vert_normals = NULL;
/* 1 once opt_model_append_converted_faces_for_node has reached the face node it
 * merges faces into; opt_model_append_converted_faces_for_current_mesh sets it to 0
 * first. */
// GLOBAL: XVT 0x5272BC
static int g_opt_convert_target_face_found = 0;
/* The OPT_FACEGROUP node the legacy converter last passed, whose children
 * opt_model_append_converted_faces_for_current_mesh searches for faces to merge. */
// GLOBAL: XVT 0x60F1C8
static struct opt_node *g_opt_convert_source_mesh_node = NULL;
/* The merged OPT_VERTNORMALS node the legacy converter built for the current
 * root; NULL before it. opt_model_convert_legacy_model_to_optimized clears it before
 * each root. */
// GLOBAL: XVT 0x60F1CC
static struct opt_node *g_opt_convert_vertex_normal_node = NULL;
/* The texture the legacy converter last passed, directly or through an
 * OPT_NODEREF; faces merge only with faces under the same texture. */
// GLOBAL: XVT 0x60F1DC
static struct opt_node *g_opt_convert_source_texture_node = NULL;
/* The merged OPT_TEXCOORDS node the legacy converter built for the current
 * root; NULL before it. opt_model_convert_legacy_model_to_optimized clears it before
 * each root. */
// GLOBAL: XVT 0x60F1F4
static struct opt_node *g_opt_convert_tex_coord_node = NULL;
/* The texture opt_model_append_converted_faces_for_node last passed while it
 * searches for faces to merge. */
// GLOBAL: XVT 0x60F1F8
static struct opt_node *g_opt_convert_face_texture_node = NULL;
/* The merged OPT_MESHVERTS node the legacy converter built for the current
 * root; NULL before it, and while NULL the next node with children builds the
 * merged nodes. opt_model_convert_legacy_model_to_optimized clears it before each
 * root. */
// GLOBAL: XVT 0x60F200
static struct opt_node *g_opt_convert_vertex_node = NULL;

/* Loads a model file and returns the Memory handle of its runtime copy
 * (opt_model_create_runtime_handle), or 0. The modern build returns 0 for a NULL
 * name or one of 257 or more characters, and when opt_model_load_file_to_handle
 * fails; it registers the copy with xvt_render_assets_register_opt. The original
 * build loads the OPT file when it opens. When it does not, it opens the same
 * name with ".iv" in place of everything from the first '.', checks the
 * "#Inventor" header and its format word, imports an "ascii" file with
 * opt_model_load_inventor_ascii_to_handle (a "binary" one with
 * opt_model_load_inventor_binary_to_handle, which gives 0, and packing then locks
 * handle 0), packs it, saves the packed model under the OPT name and returns
 * its runtime copy. It returns 0 when the .iv file does not open or its header
 * is wrong; only a wrong format word closes the file first. Both builds pulse
 * the loading screen before and after building the runtime copy of an OPT
 * file. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x411E00
uint16_t opt_model_load_handle(const char *model_filename)
{
	char file_name[257];

	if (!model_filename || strlen(model_filename) >= sizeof(file_name)) {
		return 0;
	}
	strcpy(file_name, model_filename);
	uint16_t file_handle = opt_model_load_file_to_handle(file_name);
	if (!file_handle) {
		return 0;
	}
	flight_loading_pulse_and_draw_progress_screen();
	uint16_t runtime_handle = opt_model_create_runtime_handle(file_handle);
	xvt_render_assets_register_opt(runtime_handle, model_filename);
	flight_loading_pulse_and_draw_progress_screen();
	return runtime_handle;
}

/* Adds translation, three floats, to every vertex of each OPT_MESHVERTS node at
 * or below node, following OPT_NODEREF links and stopping at one that does not
 * resolve; a node reached through two links moves twice. Only
 * opt_model_translate_vertices calls this, and nothing calls that. */
// FUNCTION: XVT 0x42ABA0
void opt_model_translate_node_vertices_recursive(
	const struct opt_node *node, struct optimized_poly_object *model,
	const float *translation)
{
	if (node != NULL) {
		while (node->node_type == OPT_NODEREF) {
			node = opt_model_resolve_node_ref(
				model, (const char *)node->payload);
			if (node == NULL) {
				return;
			}
		}

		const float *delta;
		if (node->node_type == OPT_MESHVERTS) {
			int vertex_count = node->payload_count;
			float *vertex = node->payload;
			delta = translation;
			if (vertex_count > 0) {
				do {
					vertex[0] += delta[0];
					vertex[1] += delta[1];
					vertex[2] += delta[2];
					vertex += 3;
					--vertex_count;
				} while (vertex_count != 0);
			}
		} else {
			delta = translation;
		}

		int child_index = 0;
		if (node->child_count > 0) {
			do {
				opt_model_translate_node_vertices_recursive(
					node->p_children[child_index], model,
					delta);
				++child_index;
			} while (node->child_count > child_index);
		}
	}
}

/* Runs opt_model_translate_node_vertices_recursive on each root of model. Nothing
 * calls this. */
// FUNCTION: XVT 0x42ACD0
void opt_model_translate_vertices(struct optimized_poly_object *model,
				  const float *translation)
{
	int root_index = 0;
	if (model->root_node_count > 0) {
		do {
			opt_model_translate_node_vertices_recursive(
				model->root_nodes[root_index], model,
				translation);
			++root_index;
		} while (model->root_node_count > root_index);
	}
}

/* Moves every pointer inside a packed model by the distance its block moved
 * since self_marker was recorded, and records the new address. The modern build
 * calls xvt_opt_relocate. The original build moves self_marker, the root table
 * and each root, then runs opt_model_adjust_optimized_node_pointers on each root.
 * Most callers call it only when self_marker is not the block's address. */
// FUNCTION: XVT 0x471FF0
void opt_model_adjust_optimized_poly_object_pointers(
	struct optimized_poly_object *model)
{
	xvt_opt_relocate(model);
}

/* Adds relocation_delta to node's name, payload and child table pointers and to
 * each child pointer, and to the palette pointer of an OPT_TEXTURE node whose
 * inline_palette_count is 0, then does the same below each child. NULL pointers
 * stay NULL. The modern arm calls xvt_opt_relocate_node, but only the original
 * build calls this. */
// FUNCTION: XVT 0x472050
void opt_model_adjust_optimized_node_pointers(struct opt_node *node,
					      xvt_opt_value relocation_delta)
{
	xvt_opt_relocate_node(node, relocation_delta);
}

/* Loads an OPT file into g_load_opt_buf_handle, converts a version 0 or 1 model to
 * version 2 with opt_model_convert_legacy_model_to_optimized, and returns that
 * handle; it is reused by the next load. Then it measures each root with
 * opt_model_measure_node_and_raise_capacities, which raises the renderer's capacity
 * counts, after setting g_cur_mesh_vertices, g_cur_mesh_tex_coords,
 * g_cur_vert_normals, g_model_node_walk_unused_scratch2 and g_cur_mesh_materials to
 * NULL and g_cur_vertex_count to 0. The file starts with a 4-byte word: a
 * positive one is the body size of version 0; -1 or -2 (versions 1 and 2) is
 * followed by the size. Sets g_opt_source_is_version0. The modern build decodes
 * the file with xvt_opt_load, keeps the result as g_load_opt_buf_handle in place of
 * the one before, and ends the program through xvt_storage_fatal when that
 * fails. The original build returns 0 when the file does not open and regrows
 * g_load_opt_buf_handle when the body is bigger; running out of memory ends the
 * program through fe_disk_io_fatal_error. For a version 0 or 1 file it also writes
 * the body as read to the name with its last character changed to '0' or '1',
 * and, when the original name is writable, writes the converted model over it
 * with the marker -1 (from version 0) or -2 (from version 1). */
// FUNCTION: XVT 0x4742A0
uint16_t opt_model_load_file_to_handle(char *filename)
{
	unsigned int native_size = 0;
	int version = 0;
	uint16_t handle = xvt_opt_load(filename, &version, &native_size);
	if (!handle) {
		xvt_storage_fatal("Invalid required OPT model", 1);
		return 0;
	}
	if (g_load_opt_buf_handle) {
		memory_free_handle(g_load_opt_buf_handle);
	}
	g_load_opt_buf_handle = handle;
	g_load_opt_buf_size = (int)native_size;
	g_opt_source_is_version0 = version == 0;
	if (version < 2) {
		native_size = opt_model_convert_legacy_model_to_optimized(
			native_size);
		if (!native_size) {
			return 0;
		}
		handle = g_load_opt_buf_handle;
	}
	struct optimized_poly_object *model = memory_get_handle_block(handle);
	struct scene_mesh mesh_state;
	memset(&mesh_state, 0, sizeof(mesh_state));
	g_cur_mesh_vertices = NULL;
	g_cur_mesh_tex_coords = NULL;
	g_cur_vert_normals = NULL;
	g_model_node_walk_unused_scratch2 = NULL;
	g_cur_mesh_materials = NULL;
	g_cur_vertex_count = 0;
	for (int root_index = 0; root_index < model->root_node_count;
	     ++root_index) {
		opt_model_measure_node_and_raise_capacities(
			model->root_nodes[root_index], &mesh_state);
	}
	XVT_LOG_DEBUG(
		"models.file_loaded file=\"%s\" version=%d bytes=%u roots=%d edges=%d vertices=%d",
		filename, version, native_size, model->root_node_count,
		g_scene_edge_flags_capacity, g_vertex_remap_capacity);
	if (model->root_node_count == 0) {
		XVT_LOG_ERROR("models.no_parts file=\"%s\"", filename);
	}
	if (model->root_node_count > 51 ||
	    (model->root_node_count == 51 &&
	     model->root_nodes[0]->node_type != OPT_TEXTURE)) {
		XVT_LOG_WARN("models.parts_capped file=\"%s\" roots=%d",
			     filename, model->root_node_count);
	}
	memory_handle_block_done_stub(handle);
	return handle;
}

/* Converts the version 0 or 1 model of source_size bytes in g_load_opt_buf_handle
 * to version 2 in the same handle and returns the new size in bytes. It copies
 * the source into g_opt_convert_source_handle, regrowing that when smaller, makes
 * g_load_opt_buf_handle hold at least twice source_size, and rebuilds each root
 * there with opt_model_convert_legacy_node_to_optimized, setting
 * g_opt_convert_vertex_node, g_opt_convert_tex_coord_node and
 * g_opt_convert_vertex_normal_node to NULL before each. Sets g_cur_mesh_vertices,
 * g_cur_mesh_tex_coords, g_cur_vert_normals, g_model_node_walk_unused_scratch2 and
 * g_cur_mesh_materials to NULL and g_cur_vertex_count to 0 first. Running out of
 * memory ends the program through fe_disk_io_fatal_error. Does not check that the
 * result fits in twice the source size. */
// FUNCTION: XVT 0x474610
unsigned int
opt_model_convert_legacy_model_to_optimized(unsigned int source_size)
{
	if ((int)g_opt_convert_source_buf_size < (int)source_size &&
	    g_opt_convert_source_handle != 0) {
		memory_free_handle(g_opt_convert_source_handle);
		g_opt_convert_source_handle = 0;
		g_opt_convert_source_buf_size = 0;
	}
	if (g_opt_convert_source_handle == 0) {
		g_opt_convert_source_handle =
			memory_alloc_handle(source_size, 0);
		if (g_opt_convert_source_handle == 0) {
			fe_disk_io_fatal_error(
				FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
		}
		g_opt_convert_source_buf_size = source_size;
	}
	/* Convert a legacy model stream into the optimized runtime representation. */
	struct optimized_poly_object *source_model =
		(struct optimized_poly_object *)memory_get_handle_block(
			g_opt_convert_source_handle);
	memcpy(source_model, memory_get_handle_block(g_load_opt_buf_handle),
	       source_size);
	if (source_model->self_marker != source_model) {
		opt_model_adjust_optimized_poly_object_pointers(source_model);
	}
	memory_handle_block_done_stub(g_load_opt_buf_handle);
	int destination_capacity = (int)(source_size * 2u);
	if (destination_capacity > g_load_opt_buf_size &&
	    g_load_opt_buf_handle != 0) {
		memory_free_handle(g_load_opt_buf_handle);
		g_load_opt_buf_handle = 0;
		g_load_opt_buf_size = 0;
	}
	if (g_load_opt_buf_handle == 0) {
		g_load_opt_buf_handle = memory_alloc_handle(
			(unsigned int)destination_capacity, 0);
		if (g_load_opt_buf_handle == 0) {
			fe_disk_io_fatal_error(
				FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
		}
		g_load_opt_buf_size = destination_capacity;
	}
	struct optimized_poly_object *destination_model =
		(struct optimized_poly_object *)memory_get_handle_block(
			g_load_opt_buf_handle);
	memcpy(destination_model, source_model, sizeof(*destination_model));
	destination_model->self_marker = destination_model;
	destination_model->root_nodes =
		(struct opt_node **)((uint8_t *)destination_model +
				     sizeof(*destination_model));
	int root_node_count = source_model->root_node_count;
	uint8_t *destination_node =
		(uint8_t *)(destination_model->root_nodes + root_node_count);
	unsigned int serialized_size =
		(unsigned int)(sizeof(*destination_model) +
			       sizeof(struct opt_node *) *
				       (unsigned int)root_node_count);
	struct scene_mesh mesh_state;
	memset(&mesh_state, 0, sizeof(mesh_state));
	g_cur_mesh_vertices = NULL;
	g_cur_mesh_tex_coords = NULL;
	g_cur_vert_normals = NULL;
	g_model_node_walk_unused_scratch2 = NULL;
	g_cur_mesh_materials = NULL;
	g_cur_vertex_count = 0;
	destination_model->root_node_count = 0;
	int root_index = 0;
	if (root_node_count > 0) {
		do {
			++root_index;
			++destination_model->root_node_count;
			destination_model->root_nodes[root_index - 1] =
				(struct opt_node *)destination_node;
			g_opt_convert_vertex_node = NULL;
			g_opt_convert_tex_coord_node = NULL;
			g_opt_convert_vertex_normal_node = NULL;
			unsigned int node_size =
				opt_model_convert_legacy_node_to_optimized(
					destination_node,
					source_model
						->root_nodes[root_index - 1],
					source_model, destination_model,
					&mesh_state);
			destination_node += node_size;
			serialized_size += node_size;
		} while (root_node_count > root_index);
	}
	XVT_LOG_DEBUG(
		"models.converted roots=%d bytes=%u converted=%u capacity=%d",
		root_node_count, source_size, serialized_size,
		destination_capacity);
	if (serialized_size > (unsigned int)destination_capacity) {
		XVT_LOG_ERROR("models.convert_overflow bytes=%u capacity=%d",
			      serialized_size, destination_capacity);
	}
	return serialized_size;
}

/* Searches node and the nodes below it, depth first, for a texture whose own
 * palette holds the same 8192 bytes of RGB565 colors as texture_data does after
 * its first 4096 bytes, and returns the start of that palette, or NULL. When
 * stop_node is among a node's children, the search of that node ends there, but
 * the levels above go on to their later children. A texture with
 * inline_palette_count 0 counts only when its palette pointer is its own embedded
 * palette. A texture compared without a match passes texture_data 4096 bytes on
 * to its own children. */
// FUNCTION: XVT 0x474830
void *opt_model_find_shared_texture_data_in_node_before_target(
	const void *texture_data, const struct opt_node *node,
	const struct opt_node *stop_node)
{
	if (node == NULL) {
		return NULL;
	}

	if (node->node_type == OPT_TEXTURE) {
		struct opt_texture_data *node_texture = node->payload;
		uint8_t *node_texture_data =
			(uint8_t *)node_texture + sizeof(*node_texture);
		int texture_byte_count = node_texture->height;
		texture_byte_count *= node_texture->width;
		if (node_texture->texture_size == texture_byte_count) {
			texture_byte_count = node_texture->data_size;
		}
		node_texture_data += texture_byte_count;
		if (node_texture->inline_palette_count == 0) {
			if (node_texture->palette ==
			    (uint16_t *)node_texture_data) {
				texture_data =
					(const uint8_t *)texture_data + 4096;
				node_texture_data += 4096;
				if (memcmp(texture_data, node_texture_data,
					   8192) == 0) {
					node_texture_data -= 4096;
					return node_texture_data;
				}
			}
		} else {
			texture_data = (const uint8_t *)texture_data + 4096;
			node_texture_data += 4096;
			if (memcmp(texture_data, node_texture_data, 8192) ==
			    0) {
				node_texture_data -= 4096;
				return node_texture_data;
			}
		}
	}

	int child_offset = 0;
	int child_index = 0;
	if (node->child_count > 0) {
		do {
			struct opt_node *child =
				*(struct opt_node **)((uint8_t *)
							      node->p_children +
						      child_offset);
			if (stop_node == child) {
				return NULL;
			}
			void *result =
				opt_model_find_shared_texture_data_in_node_before_target(
					texture_data, child, stop_node);
			if (result != NULL) {
				return result;
			}
			child_offset += sizeof(*node->p_children);
			++child_index;
		} while (node->child_count > child_index);
	}

	return NULL;
}

/* Runs opt_model_find_shared_texture_data_in_node_before_target on each root of model
 * in order and returns its first match, or NULL; stops, returning NULL, at a
 * root that is stop_node. */
// FUNCTION: XVT 0x474910
void *
opt_model_find_earlier_shared_texture_data(const void *texture_data,
					   struct optimized_poly_object *model,
					   const struct opt_node *stop_node)
{
	struct optimized_poly_object *object = model;
	int root_index = 0;
	unsigned int root_offset = 0;
	if (object->root_node_count > 0) {
		do {
			struct opt_node *root_node = *(
				struct opt_node **)((uint8_t *)
							    object->root_nodes +
						    root_offset);
			if (stop_node == root_node) {
				return NULL;
			}
			void *result =
				opt_model_find_shared_texture_data_in_node_before_target(
					texture_data, root_node, stop_node);
			if (result != NULL) {
				return result;
			}
			root_offset += sizeof(*object->root_nodes);
			++root_index;
		} while (object->root_node_count > root_index);
	}

	return NULL;
}

/* Writes at dst the version 2 form of src_node and everything below it, and
 * returns the bytes written: 0 for a NULL node, and for a node the conversion
 * drops that has no children, whose slot in the parent's child table becomes
 * NULL. In the modern build every node starts aligned and the size is rounded
 * up.
 *
 * While g_opt_convert_vertex_node is NULL, as it is at each root, a node with
 * children gets three new first children: an OPT_MESHVERTS, an OPT_TEXCOORDS
 * and an OPT_VERTNORMALS node holding each distinct vertex, texture coordinate
 * and vertex normal found below it. When the merged vertices' last two are not
 * the bounding box's minimum and maximum corners, those two corners are
 * appended. These nodes become g_opt_convert_vertex_node, g_opt_convert_tex_coord_node
 * and g_opt_convert_vertex_normal_node. An OPT_FACEGROUP in that state gets them
 * through a new OPT_GROUP above it. From then on the source's own vertex,
 * texture coordinate and normal nodes are dropped, and each face node takes,
 * through opt_model_append_converted_faces_for_current_mesh, the faces of itself and
 * of the face nodes after it under the same texture, within the same child of
 * the last OPT_FACEGROUP passed, renumbered into the merged lists. Face nodes
 * whose faces were taken this way (edge count -1) are dropped.
 *
 * An OPT_FACEGROUP keeps its list of one float per child, padded with zeros or
 * cut to its child count. A texture with inline_palette_count 0 whose palette
 * lies outside its own data is pointed at an earlier texture with the same
 * colors (opt_model_find_earlier_shared_texture_data) or given a copy of the
 * palette. A face node met before any merged lists exist is copied: its edge
 * count and records, 16 more bytes per face taken again from the start of its
 * payload, its 36 bytes per face of normal and gradients, and, while mesh_state
 * has no vertex normal list, g_cur_vertex_count vertex normals. A dropped node
 * that has children becomes an OPT_GROUP. Other nodes are copied with their
 * payloads. Writes g_cur_mesh_vertices, g_cur_vertex_count, g_cur_mesh_materials,
 * g_cur_vert_normals and g_cur_mesh_tex_coords as it passes those nodes, and
 * g_opt_convert_source_texture_node and g_opt_convert_source_mesh_node. */
// FUNCTION: XVT 0x474960
unsigned int opt_model_convert_legacy_node_to_optimized(
	uint8_t *dst, struct opt_node *src_node,
	struct optimized_poly_object *src_model,
	struct optimized_poly_object *dst_model, struct scene_mesh *mesh_state)
{
	struct opt_node *destination_node;
	uint8_t *cursor;
	struct opt_node **source_node;
	struct scene_mesh child_mesh;
	struct opt_vector minimum;
	struct opt_vector maximum;
	unsigned int payload_size;
	int emit_node;
	int first_child_index;
	int child_index;

	if (src_node == NULL) {
		return 0;
	}

	cursor = dst;
	source_node = &src_node;
	emit_node = 1;
	payload_size = 0;
	destination_node = NULL;

	switch ((*source_node)->node_type) {
	case OPT_FACEDATA:
	case OPT_FACEDATA_QUAD_MESH:
	case OPT_FACEDATA_FACE_SET:
	case OPT_FACEDATA_TRIANGLE_STRIP_SET:
		if (*(int *)(*source_node)->payload < 0) {
			emit_node = 0;
			break;
		}
		cursor = xvt_opt_align_pointer(cursor);
		destination_node = (struct opt_node *)cursor;
		if (g_opt_convert_vertex_node != NULL) {
			cursor += sizeof(*destination_node);
			destination_node->node_type = (*source_node)->node_type;
			if ((*source_node)->p_name != NULL) {
				destination_node->p_name = (char *)cursor;
				strcpy((char *)cursor, (*source_node)->p_name);
				cursor += strlen((*source_node)->p_name) + 1;
			} else {
				destination_node->p_name = NULL;
			}
			destination_node->payload_count = 0;
			cursor = xvt_opt_align_pointer(cursor);
			destination_node->payload = cursor;
			*(int *)cursor = 0;
			opt_model_append_converted_faces_for_current_mesh(
				destination_node, (*source_node), src_model,
				mesh_state);
			emit_node = 0;
			cursor += sizeof(int) +
				  100 * destination_node->payload_count;
		} else {
			unsigned int face_record_size;
			unsigned int trailing_size;
			uint8_t *source_trailing_data;

			cursor += sizeof(*destination_node);
			destination_node->node_type = (*source_node)->node_type;
			if ((*source_node)->p_name != NULL) {
				destination_node->p_name = (char *)cursor;
				strcpy((char *)cursor, (*source_node)->p_name);
				cursor += strlen((*source_node)->p_name) + 1;
			} else {
				destination_node->p_name = NULL;
			}
			destination_node->payload_count =
				(*source_node)->payload_count;
			cursor = xvt_opt_align_pointer(cursor);
			destination_node->payload = cursor;
			if (g_opt_source_is_version0) {
				face_record_size =
					sizeof(int) +
					48 * (*source_node)->payload_count;
			} else {
				face_record_size =
					sizeof(int) +
					64 * (*source_node)->payload_count;
			}
			memcpy(cursor, (*source_node)->payload,
			       face_record_size);
			cursor += face_record_size;
			trailing_size = 16 * (*source_node)->payload_count;
			memcpy(cursor, (*source_node)->payload, trailing_size);
			cursor += trailing_size;
			payload_size = 36 * (*source_node)->payload_count;
			if (g_opt_source_is_version0) {
				source_trailing_data =
					(uint8_t *)(*source_node)->payload +
					sizeof(int) +
					48 * (*source_node)->payload_count;
			} else {
				source_trailing_data =
					(uint8_t *)(*source_node)->payload +
					sizeof(int) +
					64 * (*source_node)->payload_count;
			}
			memcpy(cursor, source_trailing_data, payload_size);
			cursor += payload_size;
			source_trailing_data += payload_size;
			if (mesh_state->p_vert_normals == NULL) {
				payload_size = sizeof(struct opt_vector) *
					       g_cur_vertex_count;
				memcpy(cursor, source_trailing_data,
				       payload_size);
				cursor += payload_size;
			}
			emit_node = 0;
		}
		break;

	case OPT_TRANSFORM:
		payload_size = 48;
		break;

	case OPT_MESHVERTS:
		g_cur_mesh_vertices = (*source_node)->payload;
		g_cur_vertex_count = (*source_node)->payload_count;
		if (g_opt_convert_vertex_node != NULL) {
			emit_node = 0;
		} else {
			payload_size = sizeof(struct opt_vector) *
				       (*source_node)->payload_count;
		}
		break;

	case OPT_TRANSLATION:
		payload_size = 12;
		break;

	case OPT_ROTATION:
		payload_size = 36;
		break;

	case OPT_SCALE:
		payload_size = 12;
		break;

	case OPT_NODEREF:
		destination_node = (*source_node);
		payload_size = (unsigned int)strlen(
				       (const char *)(*source_node)->payload) +
			       1;
		while (destination_node->node_type == OPT_NODEREF) {
			destination_node = opt_model_resolve_node_ref(
				src_model,
				(const char *)destination_node->payload);
			if (destination_node == NULL) {
				break;
			}
		}
		if (destination_node != NULL &&
		    destination_node->node_type == OPT_TEXTURE) {
			g_opt_convert_source_texture_node = destination_node;
		}
		break;

	case OPT_MATERIAL:
		payload_size = 56 * (*source_node)->payload_count;
		g_cur_mesh_materials = (*source_node)->payload;
		break;

	case OPT_VERTNORMALS:
		g_cur_vert_normals =
			(struct opt_vector *)(*source_node)->payload;
		mesh_state->p_vert_normals = g_cur_vert_normals;
		if (g_opt_convert_vertex_normal_node != NULL) {
			emit_node = 0;
		} else {
			payload_size = sizeof(struct opt_vector) *
				       (*source_node)->payload_count;
		}
		break;

	case OPT_TEXCOORDS:
		g_cur_mesh_tex_coords = (*source_node)->payload;
		if (g_opt_convert_tex_coord_node != NULL) {
			emit_node = 0;
		} else {
			payload_size = sizeof(struct opt_tex_coord) *
				       (*source_node)->payload_count;
		}
		break;

	case OPT_BASE_COLOR:
		payload_size = 12;
		break;

	case OPT_TEXTURE: {
		struct opt_texture_data *texture;
		uint16_t *embedded_palette;
		int texture_data_size;

		g_opt_convert_source_texture_node = (*source_node);
		texture = (struct opt_texture_data *)(*source_node)->payload;
		texture_data_size = texture->width * texture->height;
		if (texture_data_size == texture->texture_size) {
			payload_size = sizeof(*texture) + texture->data_size;
		} else {
			payload_size = sizeof(*texture) + texture_data_size;
		}
		if (texture->inline_palette_count != 0) {
			payload_size += 768 * texture->inline_palette_count;
		} else {
			embedded_palette = (uint16_t *)((uint8_t *)texture +
							sizeof(*texture));
			if (texture_data_size == texture->texture_size) {
				texture_data_size = texture->data_size;
			}
			embedded_palette =
				(uint16_t *)((uint8_t *)embedded_palette +
					     texture_data_size);
			if (texture->palette == embedded_palette) {
				payload_size += 12288;
			}
		}
		break;
	}

	case OPT_FACEGROUP:
		cursor = xvt_opt_align_pointer(cursor);
		destination_node = (struct opt_node *)cursor;
		g_opt_convert_source_mesh_node = (*source_node);
		if (g_opt_convert_vertex_node == NULL) {
			struct opt_node *vertex_node;
			struct opt_node *tex_coord_node;
			struct opt_node *normal_node;
			struct opt_node **generated_children;
			struct opt_vector *vectors;
			struct opt_vector *saved_normals;
			int saved_vertex_count;
			int remaining;

			destination_node->node_type = OPT_GROUP;
			cursor += sizeof(*destination_node);
			if ((*source_node)->p_name != NULL) {
				destination_node->p_name = (char *)cursor;
				strcpy((char *)cursor, (*source_node)->p_name);
				cursor += strlen((*source_node)->p_name) + 1;
			} else {
				destination_node->p_name = NULL;
			}
			destination_node->payload = NULL;
			destination_node->payload_count = 0;
			destination_node->child_count = 4;
			cursor = xvt_opt_align_pointer(cursor);
			destination_node->p_children =
				(struct opt_node **)cursor;
			generated_children = destination_node->p_children;
			cursor += sizeof(struct opt_node *) * 4;

			cursor = xvt_opt_align_pointer(cursor);
			vertex_node = (struct opt_node *)cursor;
			generated_children[0] = vertex_node;
			vertex_node->node_type = OPT_MESHVERTS;
			cursor += sizeof(*vertex_node);
			vertex_node->p_name = NULL;
			vertex_node->payload = cursor;
			vertex_node->payload_count = 0;
			vertex_node->child_count = 0;
			vertex_node->p_children = NULL;
			opt_model_collect_unique_vertices(
				vertex_node, (*source_node), src_model,
				mesh_state);
			g_opt_convert_vertex_node = vertex_node;

			vectors = (struct opt_vector *)vertex_node->payload;
			maximum.x = vectors->x;
			minimum.x = maximum.x;
			maximum.y = vectors->y;
			minimum.y = maximum.y;
			maximum.z = vectors->z;
			minimum.z = maximum.z;
			remaining = vertex_node->payload_count;
			if (remaining > 0) {
				do {
					if (vectors->x < minimum.x) {
						minimum.x = vectors->x;
					}
					if (vectors->y < minimum.y) {
						minimum.y = vectors->y;
					}
					if (vectors->z < minimum.z) {
						minimum.z = vectors->z;
					}
					if (vectors->x > maximum.x) {
						maximum.x = vectors->x;
					}
					if (vectors->y > maximum.y) {
						maximum.y = vectors->y;
					}
					if (vectors->z > maximum.z) {
						maximum.z = vectors->z;
					}
					++vectors;
					--remaining;
				} while (remaining != 0);
			}
			vectors -= 2;
			if (vectors[0].x != minimum.x ||
			    vectors[0].y != minimum.y ||
			    vectors[0].z != minimum.z ||
			    vectors[1].x != maximum.x ||
			    vectors[1].y != maximum.y ||
			    vectors[1].z != maximum.z) {
				vectors += 2;
				vectors[0].x = minimum.x;
				vectors[0].y = minimum.y;
				vectors[0].z = minimum.z;
				++vectors;
				vectors[0].x = maximum.x;
				vectors[0].y = maximum.y;
				vectors[0].z = maximum.z;
				vertex_node->payload_count += 2;
			}
			cursor += sizeof(struct opt_vector) *
				  vertex_node->payload_count;

			cursor = xvt_opt_align_pointer(cursor);
			tex_coord_node = (struct opt_node *)cursor;
			generated_children[1] = tex_coord_node;
			tex_coord_node->node_type = OPT_TEXCOORDS;
			cursor += sizeof(*tex_coord_node);
			tex_coord_node->p_name = NULL;
			tex_coord_node->payload = cursor;
			tex_coord_node->payload_count = 0;
			tex_coord_node->child_count = 0;
			tex_coord_node->p_children = NULL;
			opt_model_collect_unique_tex_coords(
				tex_coord_node, (*source_node), src_model,
				mesh_state);
			g_opt_convert_tex_coord_node = tex_coord_node;
			cursor += sizeof(struct opt_tex_coord) *
				  tex_coord_node->payload_count;

			cursor = xvt_opt_align_pointer(cursor);
			normal_node = (struct opt_node *)cursor;
			generated_children[2] = normal_node;
			normal_node->node_type = OPT_VERTNORMALS;
			cursor += sizeof(*normal_node);
			normal_node->p_name = NULL;
			normal_node->payload = cursor;
			normal_node->payload_count = 0;
			normal_node->child_count = 0;
			normal_node->p_children = NULL;
			saved_normals = mesh_state->p_vert_normals;
			saved_vertex_count = g_cur_vertex_count;
			mesh_state->p_vert_normals = NULL;
			opt_model_collect_unique_vertex_normals(
				normal_node, (*source_node), src_model,
				mesh_state);
			mesh_state->p_vert_normals = saved_normals;
			g_opt_convert_vertex_normal_node = normal_node;
			g_cur_vertex_count = saved_vertex_count;
			cursor += sizeof(struct opt_vector) *
				  normal_node->payload_count;

			cursor = xvt_opt_align_pointer(cursor);
			destination_node = (struct opt_node *)cursor;
			generated_children[3] = destination_node;
			destination_node->node_type = OPT_FACEGROUP;
			cursor += sizeof(*destination_node);
			destination_node->p_name = NULL;
			cursor = xvt_opt_align_pointer(cursor);
			destination_node->payload = cursor;
			destination_node->payload_count =
				(*source_node)->payload_count;
			memcpy(cursor, (*source_node)->payload,
			       sizeof(int) * (*source_node)->payload_count);
			cursor += sizeof(int) * (*source_node)->payload_count;
			if ((*source_node)->child_count >
			    (*source_node)->payload_count) {
				destination_node->payload_count =
					(*source_node)->child_count;
				memset(cursor, 0,
				       sizeof(int) *
					       ((*source_node)->child_count -
						(*source_node)->payload_count));
				cursor += sizeof(int) *
					  ((*source_node)->child_count -
					   (*source_node)->payload_count);
			} else if ((*source_node)->child_count <
				   (*source_node)->payload_count) {
				destination_node->payload_count =
					(*source_node)->child_count;
				cursor += sizeof(int) *
					  ((*source_node)->child_count -
					   (*source_node)->payload_count);
			}
		} else {
			destination_node->node_type = OPT_FACEGROUP;
			cursor += sizeof(*destination_node);
			destination_node->p_name = NULL;
			cursor = xvt_opt_align_pointer(cursor);
			destination_node->payload = cursor;
			destination_node->payload_count =
				(*source_node)->payload_count;
			memcpy(cursor, (*source_node)->payload,
			       sizeof(int) * (*source_node)->payload_count);
			cursor += sizeof(int) * (*source_node)->payload_count;
			if ((*source_node)->child_count >
			    (*source_node)->payload_count) {
				destination_node->payload_count =
					(*source_node)->child_count;
				memset(cursor, 0,
				       sizeof(int) *
					       ((*source_node)->child_count -
						(*source_node)->payload_count));
				cursor += sizeof(int) *
					  ((*source_node)->child_count -
					   (*source_node)->payload_count);
			} else if ((*source_node)->child_count <
				   (*source_node)->payload_count) {
				destination_node->payload_count =
					(*source_node)->child_count;
				cursor += sizeof(int) *
					  ((*source_node)->child_count -
					   (*source_node)->payload_count);
			}
		}
		emit_node = 0;
		break;

	case OPT_HARDPOINT:
		payload_size = 16;
		break;

	case OPT_ROTSCALE:
		payload_size = 48;
		break;

	case OPT_MESHDESC:
		payload_size = 72;
		break;

	default:
		break;
	}

	if (emit_node == 1) {
		cursor = xvt_opt_align_pointer(cursor);
		destination_node = (struct opt_node *)cursor;
		cursor += sizeof(*destination_node);
		destination_node->node_type = (*source_node)->node_type;
		if ((*source_node)->p_name != NULL) {
			destination_node->p_name = (char *)cursor;
			strcpy((char *)cursor, (*source_node)->p_name);
			cursor += strlen((*source_node)->p_name) + 1;
		} else {
			destination_node->p_name = NULL;
		}
		destination_node->payload_count = (*source_node)->payload_count;
		cursor = xvt_opt_align_pointer(cursor);
		destination_node->payload = cursor;
		memcpy(cursor, (*source_node)->payload, payload_size);
		cursor += payload_size;
		if ((*source_node)->node_type == OPT_TEXTURE) {
			struct opt_texture_data *source_texture;
			uint16_t *embedded_palette;
			int texture_data_size;

			source_texture =
				(struct opt_texture_data *)(*source_node)
					->payload;
			if (source_texture->inline_palette_count == 0) {
				embedded_palette =
					(uint16_t *)((uint8_t *)source_texture +
						     sizeof(*source_texture));
				texture_data_size = source_texture->width *
						    source_texture->height;
				if (source_texture->texture_size ==
				    texture_data_size) {
					texture_data_size =
						source_texture->data_size;
				}
				embedded_palette =
					(uint16_t *)((uint8_t *)
							     embedded_palette +
						     texture_data_size);
				if (source_texture->palette !=
				    embedded_palette) {
					void *shared_texture_data;

					shared_texture_data =
						opt_model_find_earlier_shared_texture_data(
							source_texture->palette,
							dst_model,
							destination_node);
					if (shared_texture_data != NULL) {
						struct opt_texture_data
							*destination_texture;

						destination_texture =
							(struct opt_texture_data
								 *)destination_node
								->payload;
						destination_texture->palette =
							shared_texture_data;
					} else {
						const void *source_palette;
						struct opt_texture_data
							*destination_texture;

						source_palette =
							source_texture->palette;
						destination_texture =
							(struct opt_texture_data
								 *)destination_node
								->payload;
						destination_texture->palette =
							(uint16_t *)cursor;
						memcpy(cursor, source_palette,
						       12288);
						cursor += 12288;
					}
				} else {
					struct opt_texture_data
						*destination_texture;
					uint16_t *destination_palette;

					destination_texture =
						(struct opt_texture_data *)
							destination_node
								->payload;
					destination_palette =
						(uint16_t
							 *)((uint8_t *)
								    destination_texture +
							    sizeof(*destination_texture));
					texture_data_size =
						destination_texture->width *
						destination_texture->height;
					if (destination_texture->texture_size ==
					    texture_data_size) {
						texture_data_size =
							destination_texture
								->data_size;
					}
					destination_palette =
						(uint16_t
							 *)((uint8_t *)
								    destination_palette +
							    texture_data_size);
					destination_texture->palette =
						destination_palette;
				}
			}
		}
	}

	if (destination_node == NULL) {
		if ((*source_node)->child_count == 0) {
			return 0;
		}
		cursor = xvt_opt_align_pointer(cursor);
		destination_node = (struct opt_node *)cursor;
		cursor += sizeof(*destination_node);
		destination_node->p_name = NULL;
		destination_node->node_type = OPT_GROUP;
		destination_node->payload_count = 0;
		destination_node->payload = NULL;
	}

	destination_node->child_count = 0;
	destination_node->p_children = NULL;
	if ((*source_node)->child_count != 0) {
		if (g_opt_convert_vertex_node == NULL) {
			struct opt_node *vertex_node;
			struct opt_node *tex_coord_node;
			struct opt_node *normal_node;
			struct opt_vector *vectors;
			struct opt_vector *saved_normals;
			int saved_vertex_count;
			int remaining;

			destination_node->child_count =
				(*source_node)->child_count + 3;
			cursor = xvt_opt_align_pointer(cursor);
			destination_node->p_children =
				(struct opt_node **)cursor;
			cursor += sizeof(struct opt_node *) *
				  destination_node->child_count;

			cursor = xvt_opt_align_pointer(cursor);
			vertex_node = (struct opt_node *)cursor;
			destination_node->p_children[0] = vertex_node;
			vertex_node->node_type = OPT_MESHVERTS;
			cursor += sizeof(*vertex_node);
			vertex_node->p_name = NULL;
			vertex_node->payload = cursor;
			vertex_node->payload_count = 0;
			vertex_node->child_count = 0;
			vertex_node->p_children = NULL;
			opt_model_collect_unique_vertices(
				vertex_node, (*source_node), src_model,
				mesh_state);
			g_opt_convert_vertex_node = vertex_node;

			vectors = (struct opt_vector *)vertex_node->payload;
			maximum.x = vectors->x;
			minimum.x = maximum.x;
			maximum.y = vectors->y;
			minimum.y = maximum.y;
			maximum.z = vectors->z;
			minimum.z = maximum.z;
			remaining = vertex_node->payload_count;
			if (remaining > 0) {
				do {
					if (vectors->x < minimum.x) {
						minimum.x = vectors->x;
					}
					if (vectors->y < minimum.y) {
						minimum.y = vectors->y;
					}
					if (vectors->z < minimum.z) {
						minimum.z = vectors->z;
					}
					if (vectors->x > maximum.x) {
						maximum.x = vectors->x;
					}
					if (vectors->y > maximum.y) {
						maximum.y = vectors->y;
					}
					if (vectors->z > maximum.z) {
						maximum.z = vectors->z;
					}
					++vectors;
					--remaining;
				} while (remaining != 0);
			}
			vectors -= 2;
			if (vectors[0].x != minimum.x ||
			    vectors[0].y != minimum.y ||
			    vectors[0].z != minimum.z ||
			    vectors[1].x != maximum.x ||
			    vectors[1].y != maximum.y ||
			    vectors[1].z != maximum.z) {
				vectors += 2;
				vectors[0].x = minimum.x;
				vectors[0].y = minimum.y;
				vectors[0].z = minimum.z;
				++vectors;
				vectors[0].x = maximum.x;
				vectors[0].y = maximum.y;
				vectors[0].z = maximum.z;
				vertex_node->payload_count += 2;
			}
			cursor += sizeof(struct opt_vector) *
				  vertex_node->payload_count;

			cursor = xvt_opt_align_pointer(cursor);
			tex_coord_node = (struct opt_node *)cursor;
			destination_node->p_children[1] = tex_coord_node;
			tex_coord_node->node_type = OPT_TEXCOORDS;
			cursor += sizeof(*tex_coord_node);
			tex_coord_node->p_name = NULL;
			tex_coord_node->payload = cursor;
			tex_coord_node->payload_count = 0;
			tex_coord_node->child_count = 0;
			tex_coord_node->p_children = NULL;
			opt_model_collect_unique_tex_coords(
				tex_coord_node, (*source_node), src_model,
				mesh_state);
			g_opt_convert_tex_coord_node = tex_coord_node;
			cursor += sizeof(struct opt_tex_coord) *
				  tex_coord_node->payload_count;

			cursor = xvt_opt_align_pointer(cursor);
			normal_node = (struct opt_node *)cursor;
			destination_node->p_children[2] = normal_node;
			normal_node->node_type = OPT_VERTNORMALS;
			cursor += sizeof(*normal_node);
			normal_node->p_name = NULL;
			normal_node->payload = cursor;
			normal_node->payload_count = 0;
			normal_node->child_count = 0;
			normal_node->p_children = NULL;
			saved_normals = mesh_state->p_vert_normals;
			saved_vertex_count = g_cur_vertex_count;
			mesh_state->p_vert_normals = NULL;
			opt_model_collect_unique_vertex_normals(
				normal_node, (*source_node), src_model,
				mesh_state);
			mesh_state->p_vert_normals = saved_normals;
			g_opt_convert_vertex_normal_node = normal_node;
			g_cur_vertex_count = saved_vertex_count;
			first_child_index = 3;
			cursor += sizeof(struct opt_vector) *
				  normal_node->payload_count;
		} else {
			first_child_index = 0;
			destination_node->child_count =
				(*source_node)->child_count;
			cursor = xvt_opt_align_pointer(cursor);
			destination_node->p_children =
				(struct opt_node **)cursor;
			cursor += sizeof(struct opt_node *) *
				  destination_node->child_count;
		}

		child_mesh = *mesh_state;
		for (child_index = 0; child_index < (*source_node)->child_count;
		     ++child_index) {
			unsigned int child_size;

			cursor = xvt_opt_align_pointer(cursor);
			destination_node
				->p_children[first_child_index + child_index] =
				(struct opt_node *)cursor;
			child_size = opt_model_convert_legacy_node_to_optimized(
				cursor, (*source_node)->p_children[child_index],
				src_model, dst_model, &child_mesh);
			if (child_size == 0) {
				destination_node->p_children[first_child_index +
							     child_index] =
					NULL;
			}
			cursor += child_size;
		}
	}

	return (unsigned int)xvt_opt_align_size((size_t)(cursor - dst));
}

/* Appends to dst_vertex_node's list each vertex of every OPT_MESHVERTS node at or
 * below src_node that is not already in it, comparing the floats exactly, and
 * raises its payload_count. Follows OPT_NODEREF links and stops at one that does
 * not resolve. Does not check the list's room; mesh_state is unused. */
// FUNCTION: XVT 0x475740
void opt_model_collect_unique_vertices(struct opt_node *dst_vertex_node,
				       const struct opt_node *src_node,
				       struct optimized_poly_object *src_model,
				       struct scene_mesh *mesh_state)
{
	if (src_node == NULL) {
		return;
	}
	while (src_node->node_type == OPT_NODEREF) {
		src_node = opt_model_resolve_node_ref(
			src_model, (const char *)src_node->payload);
		if (src_node == NULL) {
			return;
		}
	}

	if (src_node->node_type == OPT_MESHVERTS) {
		float *source_vertex = (float *)src_node->payload;
		for (int source_index = 0;
		     source_index < src_node->payload_count; ++source_index) {
			float *destination_vertex =
				(float *)dst_vertex_node->payload;
			int destination_index = 0;
			int destination_count = dst_vertex_node->payload_count;
			while (destination_index < destination_count) {
				if (source_vertex[0] == destination_vertex[0] &&
				    source_vertex[1] == destination_vertex[1] &&
				    source_vertex[2] == destination_vertex[2]) {
					break;
				}
				destination_vertex += 3;
				++destination_index;
			}
			if (destination_index == destination_count) {
				destination_vertex[0] = source_vertex[0];
				destination_vertex[1] = source_vertex[1];
				destination_vertex[2] = source_vertex[2];
				++dst_vertex_node->payload_count;
			}
			source_vertex += 3;
		}
	}

	int child_index = 0;
	while (src_node->child_count > child_index) {
		opt_model_collect_unique_vertices(
			dst_vertex_node, src_node->p_children[child_index],
			src_model, mesh_state);
		++child_index;
	}
}

/* Appends to dst_tex_coord_node's list each texture coordinate of every
 * OPT_TEXCOORDS node at or below src_node that is not already in it, comparing
 * the floats exactly, and raises its payload_count. Follows OPT_NODEREF links
 * and stops at one that does not resolve. Does not check the list's room;
 * mesh_state is unused. */
// FUNCTION: XVT 0x475850
void opt_model_collect_unique_tex_coords(
	struct opt_node *dst_tex_coord_node, const struct opt_node *src_node,
	struct optimized_poly_object *src_model, struct scene_mesh *mesh_state)
{
	if (src_node == NULL) {
		return;
	}
	while (src_node->node_type == OPT_NODEREF) {
		src_node = opt_model_resolve_node_ref(
			src_model, (const char *)src_node->payload);
		if (src_node == NULL) {
			return;
		}
	}

	struct opt_node *destination_node;
	if (src_node->node_type == OPT_TEXCOORDS) {
		float *source_tex_coord = (float *)src_node->payload;
		destination_node = dst_tex_coord_node;
		int source_index = 0;
		while (source_index < src_node->payload_count) {
			float *destination_tex_coord =
				(float *)destination_node->payload;
			int destination_index = 0;
			int destination_count = destination_node->payload_count;
			while (destination_index < destination_count) {
				if (source_tex_coord[0] ==
					    destination_tex_coord[0] &&
				    source_tex_coord[1] ==
					    destination_tex_coord[1]) {
					break;
				}
				destination_tex_coord += 2;
				++destination_index;
			}
			if (destination_index == destination_count) {
				destination_tex_coord[0] = source_tex_coord[0];
				destination_tex_coord[1] = source_tex_coord[1];
				++destination_node->payload_count;
			}
			source_tex_coord += 2;
			++source_index;
		}
	} else {
		destination_node = dst_tex_coord_node;
	}

	int child_index = 0;
	while (child_index < src_node->child_count) {
		opt_model_collect_unique_tex_coords(
			destination_node, src_node->p_children[child_index],
			src_model, mesh_state);
		++child_index;
	}
}

/* Appends to dst_normal_node's list each vertex normal at or below src_node that
 * is not already in it, comparing the floats exactly, and raises its
 * payload_count. Normals come from OPT_VERTNORMALS nodes, which also become
 * mesh_state->p_vert_normals, and, while mesh_state has none, from the
 * g_cur_vertex_count normals stored after each face node's data. Sets
 * g_cur_vertex_count at each OPT_MESHVERTS node. For a version 0 source it clears
 * mesh_state->p_vert_normals after every node. Follows OPT_NODEREF links and stops
 * at one that does not resolve. Does not check the list's room. */
// FUNCTION: XVT 0x475940
void opt_model_collect_unique_vertex_normals(
	struct opt_node *dst_normal_node, struct opt_node *src_node,
	struct optimized_poly_object *src_model, struct scene_mesh *mesh_state)
{
	struct opt_node *node = src_node;
	if (node == NULL) {
		return;
	}
	while (node->node_type == OPT_NODEREF) {
		node = opt_model_resolve_node_ref(src_model,
						  (const char *)node->payload);
		if (node == NULL) {
			return;
		}
	}

	struct opt_node *destination_node = dst_normal_node;
	struct opt_vector *source_normal;
	int source_index;
	switch (node->node_type) {
	case OPT_FACEDATA:
	case OPT_FACEDATA_QUAD_MESH:
	case OPT_FACEDATA_FACE_SET:
	case OPT_FACEDATA_TRIANGLE_STRIP_SET:
		if (mesh_state->p_vert_normals == NULL) {
			struct opt_legacy_face_payload *face_data =
				(struct opt_legacy_face_payload *)node->payload;
			if (g_opt_source_is_version0) {
				source_normal =
					(struct opt_vector *)&(
						(struct
						 opt_legacy_face_payload_v0 *)
							face_data)
						->storage[node->payload_count];
			} else {
				source_normal =
					(struct opt_vector *)&face_data
						->storage[node->payload_count];
			}
			source_index = 0;
			if (g_cur_vertex_count > 0) {
				do {
					struct opt_vector *destination_normal =
						(struct opt_vector *)
							destination_node
								->payload;
					int destination_index = 0;
					int destination_count =
						destination_node->payload_count;
					if (destination_index <
					    destination_count) {
						do {
							if (destination_normal
								    ->x ==
							    source_normal->x) {
								if (source_normal->y ==
									    destination_normal
										    ->y &&
								    source_normal->z ==
									    destination_normal
										    ->z) {
									break;
								}
							}
							++destination_normal;
							++destination_index;
						} while (destination_index <
							 destination_count);
					}
					if (destination_index ==
					    destination_count) {
						destination_normal->x =
							source_normal->x;
						destination_normal->y =
							source_normal->y;
						destination_normal->z =
							source_normal->z;
						++destination_node
							  ->payload_count;
					}
					++source_normal;
					++source_index;
				} while (source_index < g_cur_vertex_count);
			}
		}
		break;

	case OPT_MESHVERTS:
		g_cur_vertex_count = node->payload_count;
		break;

	case OPT_VERTNORMALS:
		mesh_state->p_vert_normals = (struct opt_vector *)node->payload;
		source_normal = (struct opt_vector *)node->payload;
		source_index = 0;
		if (node->payload_count > 0) {
			do {
				struct opt_vector *destination_normal =
					(struct opt_vector *)
						destination_node->payload;
				int destination_index = 0;
				int destination_count =
					destination_node->payload_count;
				if (destination_index < destination_count) {
					do {
						if (destination_normal->x ==
						    source_normal->x) {
							if (source_normal->y ==
								    destination_normal
									    ->y &&
							    destination_normal
									    ->z ==
								    source_normal
									    ->z) {
								break;
							}
						}
						++destination_normal;
						++destination_index;
					} while (destination_index <
						 destination_count);
				}
				if (destination_index == destination_count) {
					destination_normal->x =
						source_normal->x;
					destination_normal->y =
						source_normal->y;
					destination_normal->z =
						source_normal->z;
					++destination_node->payload_count;
				}
				++source_normal;
				++source_index;
			} while (source_index < node->payload_count);
		}
		break;

	default:
		break;
	}

	if (g_opt_source_is_version0) {
		mesh_state->p_vert_normals = NULL;
	}
	int child_index = 0;
	while (child_index < node->child_count) {
		opt_model_collect_unique_vertex_normals(
			destination_node, node->p_children[child_index],
			src_model, mesh_state);
		++child_index;
	}
}

/* Returns the index in unique_vector_node's list of source_vectors[sourceIndex],
 * or -1 for a negative sourceIndex. The search starts at
 * g_opt_convert_vector_search_cursor minus (sourceIndex >> 1), or at 0 when that is
 * negative or over the list length, runs to the end, then runs over the whole
 * list from 0. Leaves g_opt_convert_vector_search_cursor at the index found.
 * Returns 0 when the vector is not in the list, leaving the cursor at the list
 * length. */
// FUNCTION: XVT 0x475B70
int opt_model_remap_vector_index(const struct opt_node *unique_vector_node,
				 const struct opt_vector *source_vectors,
				 int source_index)
{
	if (source_index < 0) {
		return -1;
	}

	source_vectors += source_index;
	const float *unique_vectors = unique_vector_node->payload;
	int cursor = g_opt_convert_vector_search_cursor;
	cursor -= source_index >> 1;
	g_opt_convert_vector_search_cursor = cursor;
	if (cursor < 0 || unique_vector_node->payload_count < cursor) {
		g_opt_convert_vector_search_cursor = 0;
		cursor = 0;
	}

	unique_vectors += 3 * g_opt_convert_vector_search_cursor;
	if (unique_vector_node->payload_count >
	    g_opt_convert_vector_search_cursor) {
		do {
			if (unique_vectors[0] != source_vectors->x ||
			    unique_vectors[1] != source_vectors->y ||
			    source_vectors->z != unique_vectors[2]) {
				unique_vectors += 3;
				cursor = g_opt_convert_vector_search_cursor;
				++cursor;
				g_opt_convert_vector_search_cursor = cursor;
			} else {
				return g_opt_convert_vector_search_cursor;
			}
		} while (unique_vector_node->payload_count >
			 g_opt_convert_vector_search_cursor);
	}

	g_opt_convert_vector_search_cursor = 0;
	unique_vectors = unique_vector_node->payload;
	if (unique_vector_node->payload_count > 0) {
		do {
			if (unique_vectors[0] != source_vectors->x ||
			    unique_vectors[1] != source_vectors->y ||
			    source_vectors->z != unique_vectors[2]) {
				unique_vectors += 3;
				cursor = g_opt_convert_vector_search_cursor;
				++cursor;
				g_opt_convert_vector_search_cursor = cursor;
			} else {
				return g_opt_convert_vector_search_cursor;
			}
		} while (unique_vector_node->payload_count >
			 g_opt_convert_vector_search_cursor);
	}

	return 0;
}

/* Returns the index in unique_tex_coord_node's list of
 * source_tex_coords[sourceIndex], or -1 for a negative sourceIndex. The search
 * starts at g_opt_convert_tex_coord_search_cursor minus (sourceIndex >> 1), or at 0
 * when that is negative or over the list length, runs to the end, then runs
 * over the whole list from 0. Leaves g_opt_convert_tex_coord_search_cursor at the
 * index found. Returns 0 when the coordinate is not in the list, leaving the
 * cursor at the list length. */
// FUNCTION: XVT 0x475C70
int opt_model_remap_tex_coord_index(
	const struct opt_node *unique_tex_coord_node,
	const struct opt_tex_coord *source_tex_coords, int source_index)
{
	if (source_index < 0) {
		return -1;
	}

	source_tex_coords += source_index;
	const float *unique_tex_coords = unique_tex_coord_node->payload;
	g_opt_convert_tex_coord_search_cursor -= source_index >> 1;
	if (g_opt_convert_tex_coord_search_cursor < 0 ||
	    unique_tex_coord_node->payload_count <
		    g_opt_convert_tex_coord_search_cursor) {
		g_opt_convert_tex_coord_search_cursor = 0;
	}

	unique_tex_coords += 2 * g_opt_convert_tex_coord_search_cursor;
	if (unique_tex_coord_node->payload_count >
	    g_opt_convert_tex_coord_search_cursor) {
		do {
			if (unique_tex_coords[0] != source_tex_coords->u ||
			    unique_tex_coords[1] != source_tex_coords->v) {
				unique_tex_coords += 2;
				++g_opt_convert_tex_coord_search_cursor;
			} else {
				return g_opt_convert_tex_coord_search_cursor;
			}
		} while (unique_tex_coord_node->payload_count >
			 g_opt_convert_tex_coord_search_cursor);
	}

	g_opt_convert_tex_coord_search_cursor = 0;
	unique_tex_coords = unique_tex_coord_node->payload;
	if (unique_tex_coord_node->payload_count > 0) {
		do {
			if (unique_tex_coords[0] != source_tex_coords->u ||
			    unique_tex_coords[1] != source_tex_coords->v) {
				unique_tex_coords += 2;
				++g_opt_convert_tex_coord_search_cursor;
			} else {
				return g_opt_convert_tex_coord_search_cursor;
			}
		} while (unique_tex_coord_node->payload_count >
			 g_opt_convert_tex_coord_search_cursor);
	}

	return 0;
}

/* Walks node and the nodes below it, following OPT_NODEREF links, and moves
 * into dst_face_node the faces of every face node from target_face_node on that has
 * a positive edge count while the last texture passed
 * (g_opt_convert_face_texture_node) is g_opt_convert_source_texture_node. Each moved
 * face's vertex, texture coordinate and normal indices are renumbered into the
 * merged lists (opt_model_remap_vector_index, opt_model_remap_tex_coord_index) and its
 * edge numbers raised by dst_face_node's edge count, a fourth edge of -1 kept.
 * The new faces go in front of dst_face_node's faces, in each of its three parts:
 * the 64-byte records, the face normals and the two texture gradient vectors. A
 * version 0 source has no normal indices, so its vertex indices serve. Adds the
 * moved node's face and edge counts to dst_face_node's and sets the moved node's
 * edge count to -1. Updates g_opt_convert_target_face_found,
 * g_opt_convert_face_texture_node, g_cur_mesh_vertices, g_cur_mesh_tex_coords and
 * mesh_state->p_vert_normals as it passes those nodes. Does not check
 * dst_face_node's room. */
// FUNCTION: XVT 0x475D50
void opt_model_append_converted_faces_for_node(
	struct opt_node *dst_face_node, struct opt_node *target_face_node,
	struct opt_node *node, struct optimized_poly_object *src_model,
	struct scene_mesh *mesh_state)
{
	if (node == NULL) {
		return;
	}

	while (node->node_type == OPT_NODEREF) {
		node = opt_model_resolve_node_ref(src_model,
						  (const char *)node->payload);
		if (node == NULL) {
			return;
		}
	}

	if (g_opt_convert_target_face_found == 0 && node == target_face_node) {
		g_opt_convert_target_face_found = 1;
	}

	switch (node->node_type) {
	case OPT_FACEDATA:
	case OPT_FACEDATA_QUAD_MESH:
	case OPT_FACEDATA_FACE_SET:
	case OPT_FACEDATA_TRIANGLE_STRIP_SET:
		if (g_opt_convert_target_face_found != 0 &&
		    g_opt_convert_face_texture_node ==
			    g_opt_convert_source_texture_node &&
		    *(int *)node->payload > 0) {
			uint8_t *destination_data = dst_face_node->payload;
			uint8_t *destination_bytes =
				destination_data + sizeof(int);
			int destination_edge_count = *(int *)destination_data;
			memmove(destination_bytes + 64 * node->payload_count,
				destination_bytes,
				100 * dst_face_node->payload_count);

			const int *source_cursor =
				(const int *)node->payload + 1;
			const struct opt_vector *source_vectors =
				mesh_state->p_vert_normals;
			if (source_vectors == NULL) {
				if (g_opt_source_is_version0) {
					source_vectors =
						(const struct opt_vector
							 *)((const uint8_t *)
								    source_cursor +
							    48 * node->payload_count +
							    36 * node->payload_count);
				} else {
					source_vectors =
						(const struct opt_vector
							 *)((const uint8_t *)
								    source_cursor +
							    64 * node->payload_count +
							    36 * node->payload_count);
				}
			}

			int *destination_cursor = (int *)destination_bytes;
			int face_index;
			for (face_index = 0; face_index < node->payload_count;
			     ++face_index) {
				*destination_cursor++ =
					opt_model_remap_vector_index(
						g_opt_convert_vertex_node,
						(const struct opt_vector *)
							g_cur_mesh_vertices,
						*source_cursor++);
				*destination_cursor++ =
					opt_model_remap_vector_index(
						g_opt_convert_vertex_node,
						(const struct opt_vector *)
							g_cur_mesh_vertices,
						*source_cursor++);
				*destination_cursor++ =
					opt_model_remap_vector_index(
						g_opt_convert_vertex_node,
						(const struct opt_vector *)
							g_cur_mesh_vertices,
						*source_cursor++);
				*destination_cursor++ =
					opt_model_remap_vector_index(
						g_opt_convert_vertex_node,
						(const struct opt_vector *)
							g_cur_mesh_vertices,
						*source_cursor++);

				*destination_cursor++ = destination_edge_count +
							*source_cursor++;
				*destination_cursor++ = destination_edge_count +
							*source_cursor++;
				*destination_cursor++ = destination_edge_count +
							*source_cursor++;
				int source_edge_index = *source_cursor++;
				if (source_edge_index == -1) {
					*destination_cursor++ = -1;
				} else {
					*destination_cursor++ =
						destination_edge_count +
						source_edge_index;
				}

				*destination_cursor++ =
					opt_model_remap_tex_coord_index(
						g_opt_convert_tex_coord_node,
						(const struct opt_tex_coord *)
							g_cur_mesh_tex_coords,
						*source_cursor++);
				*destination_cursor++ =
					opt_model_remap_tex_coord_index(
						g_opt_convert_tex_coord_node,
						(const struct opt_tex_coord *)
							g_cur_mesh_tex_coords,
						*source_cursor++);
				*destination_cursor++ =
					opt_model_remap_tex_coord_index(
						g_opt_convert_tex_coord_node,
						(const struct opt_tex_coord *)
							g_cur_mesh_tex_coords,
						*source_cursor++);
				*destination_cursor++ =
					opt_model_remap_tex_coord_index(
						g_opt_convert_tex_coord_node,
						(const struct opt_tex_coord *)
							g_cur_mesh_tex_coords,
						*source_cursor++);

				if (g_opt_source_is_version0) {
					source_cursor -= 12;
				}
				*destination_cursor++ =
					opt_model_remap_vector_index(
						g_opt_convert_vertex_normal_node,
						source_vectors,
						*source_cursor++);
				*destination_cursor++ =
					opt_model_remap_vector_index(
						g_opt_convert_vertex_normal_node,
						source_vectors,
						*source_cursor++);
				*destination_cursor++ =
					opt_model_remap_vector_index(
						g_opt_convert_vertex_normal_node,
						source_vectors,
						*source_cursor++);
				*destination_cursor++ =
					opt_model_remap_vector_index(
						g_opt_convert_vertex_normal_node,
						source_vectors,
						*source_cursor++);
				if (g_opt_source_is_version0) {
					source_cursor += 8;
				}
			}

			uint8_t *destination_trailing_bytes =
				destination_bytes +
				64 * (node->payload_count +
				      dst_face_node->payload_count);
			struct opt_vector *destination_face_normals =
				(struct opt_vector *)destination_trailing_bytes;
			memmove(destination_trailing_bytes +
					12 * node->payload_count,
				destination_trailing_bytes,
				36 * dst_face_node->payload_count);
			const struct opt_vector *source_face_normals;
			if (g_opt_source_is_version0) {
				source_face_normals =
					(const struct opt_vector
						 *)((const uint8_t *)
							    node->payload +
						    sizeof(int) +
						    48 * node->payload_count);
			} else {
				source_face_normals =
					(const struct opt_vector
						 *)((const uint8_t *)
							    node->payload +
						    sizeof(int) +
						    64 * node->payload_count);
			}
			for (face_index = 0; face_index < node->payload_count;
			     ++face_index) {
				destination_face_normals[face_index] =
					source_face_normals[face_index];
			}

			destination_trailing_bytes =
				(uint8_t *)&destination_face_normals
					[node->payload_count +
					 dst_face_node->payload_count];
			struct opt_vector *destination_texture_gradients =
				(struct opt_vector *)destination_trailing_bytes;
			memmove(destination_trailing_bytes +
					24 * node->payload_count,
				destination_trailing_bytes,
				24 * dst_face_node->payload_count);
			const struct opt_vector *source_texture_gradients =
				source_face_normals + node->payload_count;
			for (face_index = 0; face_index < node->payload_count;
			     ++face_index) {
				destination_texture_gradients[2 * face_index] =
					source_texture_gradients[2 *
								 face_index];
				destination_texture_gradients[2 * face_index +
							      1] =
					source_texture_gradients
						[2 * face_index + 1];
			}

			dst_face_node->payload_count += node->payload_count;
			*(int *)destination_data += *(int *)node->payload;
			*(int *)node->payload = -1;
		}
		break;

	case OPT_MESHVERTS:
		g_cur_mesh_vertices = node->payload;
		break;

	case OPT_VERTNORMALS:
		mesh_state->p_vert_normals = (struct opt_vector *)node->payload;
		break;

	case OPT_TEXCOORDS:
		g_cur_mesh_tex_coords = node->payload;
		break;

	case OPT_TEXTURE:
		g_opt_convert_face_texture_node = node;
		break;

	default:
		break;
	}

	for (int child_index = 0; child_index < node->child_count;
	     ++child_index) {
		opt_model_append_converted_faces_for_node(
			dst_face_node, target_face_node,
			node->p_children[child_index], src_model, mesh_state);
	}
}

/* Moves into dst_face_node, with opt_model_append_converted_faces_for_node, the faces
 * of target_face_node and of the later face nodes under the same texture, within
 * the child of g_opt_convert_source_mesh_node that holds target_face_node; children
 * before it are walked but give no faces. Sets g_opt_convert_face_texture_node to
 * g_opt_convert_source_texture_node and g_opt_convert_target_face_found to 0 first. */
// FUNCTION: XVT 0x476280
void opt_model_append_converted_faces_for_current_mesh(
	struct opt_node *dst_face_node, struct opt_node *target_face_node,
	struct optimized_poly_object *src_model, struct scene_mesh *mesh_state)
{
	g_opt_convert_face_texture_node = g_opt_convert_source_texture_node;
	int child_index = 0;
	g_opt_convert_target_face_found = 0;
	if (g_opt_convert_source_mesh_node->child_count > 0) {
		int child_offset = 0;
		do {
			opt_model_append_converted_faces_for_node(
				dst_face_node, target_face_node,
				*(struct opt_node *
					  *)((uint8_t *)
						     g_opt_convert_source_mesh_node
							     ->p_children +
					     child_offset),
				src_model, mesh_state);
			if (g_opt_convert_target_face_found != 0) {
				break;
			}
			child_offset += sizeof(struct opt_node *);
			++child_index;
		} while (g_opt_convert_source_mesh_node->child_count >
			 child_index);
	}
}

/* Builds a runtime copy of the packed model in source_handle and returns its new
 * Memory handle. It measures the copy with opt_model_build_runtime_node, allocates
 * it, builds it, then fixes its texture palette pointers with
 * opt_model_fixup_runtime_texture_pointers. Sets g_cur_mesh_vertices,
 * g_cur_mesh_tex_coords, g_cur_vert_normals, g_model_node_walk_unused_scratch2 and
 * g_cur_mesh_materials to NULL and g_cur_vertex_count to 0 first. A failed
 * allocation ends the program through fe_disk_io_fatal_error. The modern build
 * returns 0 for a source_handle of 0. */
// FUNCTION: XVT 0x4762F0
uint16_t opt_model_create_runtime_handle(unsigned int source_handle)
{
	if (!source_handle) {
		return 0;
	}
	struct optimized_poly_object *source_model =
		(struct optimized_poly_object *)memory_get_handle_block(
			source_handle);
	if (source_model->self_marker != source_model) {
		opt_model_adjust_optimized_poly_object_pointers(source_model);
	}
	struct scene_mesh mesh_state;
	memset(&mesh_state, 0, sizeof(mesh_state));
	g_cur_mesh_vertices = NULL;
	g_cur_mesh_tex_coords = NULL;
	g_cur_vert_normals = NULL;
	g_model_node_walk_unused_scratch2 = NULL;
	g_cur_mesh_materials = NULL;
	g_cur_vertex_count = 0;

	struct optimized_poly_object *runtime_model;
	unsigned int serialized_size =
		sizeof(struct opt_node *) *
			(unsigned int)source_model->root_node_count +
		sizeof(*runtime_model);

	int root_index;
	for (root_index = 0; root_index < source_model->root_node_count;
	     ++root_index) {
		serialized_size += opt_model_build_runtime_node(
			source_model->root_nodes[root_index], &mesh_state,
			NULL);
	}
	memory_handle_block_done_stub(source_handle);
	uint16_t runtime_handle = memory_alloc_handle(serialized_size, 0);
	if (runtime_handle == 0) {
		fe_disk_io_fatal_error(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
		return 0;
	}

	if (!source_handle) {
		return 0;
	}
	source_model = (struct optimized_poly_object *)memory_get_handle_block(
		source_handle);
	if (source_model->self_marker != source_model) {
		opt_model_adjust_optimized_poly_object_pointers(source_model);
	}
	runtime_model = (struct optimized_poly_object *)memory_get_handle_block(
		runtime_handle);
	memcpy(runtime_model, source_model, sizeof(*runtime_model));
	runtime_model->self_marker = runtime_model;
	runtime_model->root_nodes =
		(struct opt_node **)((uint8_t *)runtime_model +
				     sizeof(*runtime_model));
	uint8_t *node_storage = (uint8_t *)(runtime_model->root_nodes +
					    source_model->root_node_count);
	for (root_index = 0; root_index < source_model->root_node_count;
	     ++root_index) {
		runtime_model->root_nodes[root_index] =
			(struct opt_node *)node_storage;
		node_storage += opt_model_build_runtime_node(
			source_model->root_nodes[root_index], &mesh_state,
			node_storage);
	}
	for (root_index = 0; root_index < runtime_model->root_node_count;
	     ++root_index) {
		opt_model_fixup_runtime_texture_pointers(
			runtime_model->root_nodes[root_index], runtime_model,
			source_model);
	}
	XVT_LOG_DEBUG(
		"models.runtime_built handle=%u source=%u bytes=%u roots=%d bpp=%d hardware=%d mip=%d detail=%d",
		(unsigned)runtime_handle, source_handle, serialized_size,
		runtime_model->root_node_count, g_flight_bytes_per_pixel,
		g_use_hardware3d, g_mipmapping_enabled,
		g_texture_resolution_level);
	memory_handle_block_done_stub(runtime_handle);
	memory_handle_block_done_stub(source_handle);
	return runtime_handle;
}

/* Fixes the palette pointer of every texture at or below node in a runtime
 * copy, following OPT_NODEREF links in dst_model. A texture with an inline
 * palette gets inline_palette_count 0 and its pointer set to the palette copied
 * after its texels. A texture whose palette is not its own is pointed at the
 * palette copy of the texture that owned that palette in src_model, when
 * opt_model_find_corresponding_texture_node_in_model finds it. On a 16-bit display
 * each pointer is set 4096 bytes before the copy. */
// FUNCTION: XVT 0x476490
void opt_model_fixup_runtime_texture_pointers(
	struct opt_node *node, struct optimized_poly_object *dst_model,
	struct optimized_poly_object *src_model)
{
	struct opt_node *current_node = node;
	if (current_node != NULL) {
		while (current_node->node_type == OPT_NODEREF) {
			current_node = opt_model_resolve_node_ref(
				dst_model, (const char *)current_node->payload);
			if (current_node == NULL) {
				return;
			}
		}

		if (current_node->node_type == OPT_TEXTURE) {
			struct opt_texture_data *texture_data =
				(struct opt_texture_data *)
					current_node->payload;
			uint8_t *palette;
			int texture_data_size;
			if (texture_data->inline_palette_count != 0) {
				texture_data->inline_palette_count = 0;
				palette = (uint8_t *)texture_data +
					  sizeof(*texture_data);
				texture_data_size = texture_data->width *
						    texture_data->height;
				if (texture_data->texture_size ==
				    texture_data_size) {
					texture_data_size =
						texture_data->data_size;
				}
				palette += texture_data_size;
				if (g_flight_bytes_per_pixel == 2) {
					palette -= 4096;
				}
				texture_data->palette = (uint16_t *)palette;
			} else {
				uint16_t *source_palette =
					texture_data->palette;
				palette = (uint8_t *)source_palette;
				if (g_flight_bytes_per_pixel == 2) {
					palette += 4096;
				}
				palette -= sizeof(*texture_data);
				texture_data_size = texture_data->width *
						    texture_data->height;
				if (texture_data->texture_size ==
				    texture_data_size) {
					texture_data_size =
						texture_data->data_size;
				}
				palette -= texture_data_size;
				if (palette != (uint8_t *)texture_data) {
					struct opt_node *corresponding_node =
						opt_model_find_corresponding_texture_node_in_model(
							dst_model, src_model,
							source_palette);
					if (corresponding_node != NULL) {
						struct opt_texture_data *
							corresponding_texture_data =
								(struct
								 opt_texture_data
									 *)corresponding_node
									->payload;
						palette =
							(uint8_t *)
								corresponding_texture_data +
							sizeof(*corresponding_texture_data);
						texture_data_size =
							corresponding_texture_data
								->width *
							corresponding_texture_data
								->height;
						if (corresponding_texture_data
							    ->texture_size ==
						    texture_data_size) {
							texture_data_size =
								corresponding_texture_data
									->data_size;
						}
						palette += texture_data_size;
						if (g_flight_bytes_per_pixel ==
						    2) {
							palette -= 4096;
						}
						texture_data->palette =
							(uint16_t *)palette;
					}
				}
			}
		}

		int child_index = 0;
		int child_offset = 0;
		if (current_node->child_count > child_index) {
			int child_count;
			do {
				opt_model_fixup_runtime_texture_pointers(
					*(struct opt_node *
						  *)((uint8_t *)current_node
							     ->p_children +
						     child_offset),
					dst_model, src_model);
				child_offset +=
					sizeof(*current_node->p_children);
				++child_index;
				child_count = current_node->child_count;
			} while (child_count > child_index);
		}
	}
}

/* Walks src_node and dst_node side by side and returns the node of dst_node's tree
 * that stands where the source texture whose own palette is source_palette
 * stands, or NULL. A pair of nodes counts only when their types match, and
 * their children are searched only when their child counts match. Does not
 * follow OPT_NODEREF links. */
// FUNCTION: XVT 0x4765B0
struct opt_node *
opt_model_find_corresponding_texture_node(struct opt_node *src_node,
					  struct opt_node *dst_node,
					  const uint16_t *source_palette)
{
	struct opt_node *source = src_node;
	if (source == NULL) {
		return NULL;
	}
	struct opt_node *destination = dst_node;
	if (destination == NULL) {
		return NULL;
	}
	if (destination->node_type != source->node_type) {
		return NULL;
	}

	if (source->node_type == OPT_TEXTURE) {
		struct opt_texture_data *texture_data =
			(struct opt_texture_data *)source->payload;
		uint16_t *embedded_palette =
			(uint16_t *)((uint8_t *)texture_data +
				     sizeof(*texture_data));
		int texture_data_size =
			texture_data->width * texture_data->height;
		if (texture_data->texture_size == texture_data_size) {
			texture_data_size = texture_data->data_size;
		}
		embedded_palette = (uint16_t *)((uint8_t *)embedded_palette +
						texture_data_size);
		if ((texture_data->palette == embedded_palette ||
		     texture_data->inline_palette_count != 0) &&
		    source_palette == embedded_palette) {
			return destination;
		}
	}

	if (destination->child_count != source->child_count) {
		return NULL;
	}
	int child_index = 0;
	int child_offset = 0;
	if (source->child_count > 0) {
		do {
			struct opt_node *result =
				opt_model_find_corresponding_texture_node(
					*(struct opt_node *
						  *)((uint8_t *)source
							     ->p_children +
						     child_offset),
					*(struct opt_node *
						  *)((uint8_t *)destination
							     ->p_children +
						     child_offset),
					source_palette);
			if (result != NULL) {
				return result;
			}
			child_offset += sizeof(*source->p_children);
			++child_index;
		} while (source->child_count > child_index);
	}
	return NULL;
}

/* Runs opt_model_find_corresponding_texture_node on each pair of roots of src_model
 * and dst_model, in order, and returns its first match, or NULL. Uses src_model's
 * root count for both. */
// FUNCTION: XVT 0x476670
struct opt_node *opt_model_find_corresponding_texture_node_in_model(
	const struct optimized_poly_object *dst_model,
	const struct optimized_poly_object *src_model,
	const uint16_t *source_palette)
{
	int root_offset = 0;
	int root_index = 0;
	if (src_model->root_node_count > 0) {
		do {
			struct opt_node *result =
				opt_model_find_corresponding_texture_node(
					*(struct opt_node *
						  *)((uint8_t *)src_model
							     ->root_nodes +
						     root_offset),
					*(struct opt_node *
						  *)((uint8_t *)dst_model
							     ->root_nodes +
						     root_offset),
					source_palette);
			if (result != NULL) {
				return result;
			}
			root_offset += sizeof(*src_model->root_nodes);
			++root_index;
		} while (src_model->root_node_count > root_index);
	}
	return NULL;
}

/* Returns the bytes node and everything below it take in a packed model: the
 * opt_node, its name and the payload of its type. A face node counts its edge
 * count word, 64 bytes per face for the records and 36 for the normal and
 * gradients, and room for g_cur_vertex_count vertex normals when
 * parent_state->p_vert_normals is NULL. Raises g_scene_edge_flags_capacity to the
 * largest face node edge count and g_vertex_remap_capacity to the largest vertex
 * count. Sets g_cur_vertex_count, g_cur_mesh_materials, g_cur_vert_normals and
 * parent_state->p_vert_normals at those nodes, and sets g_cur_mesh_vertices,
 * g_cur_mesh_tex_coords, g_cur_vert_normals, g_model_node_walk_unused_scratch2 and
 * g_cur_mesh_materials to NULL before a node's children. Returns 0 for a NULL
 * node. An OPT_TEXTURE node with a NULL payload is read through that NULL
 * pointer. */
// FUNCTION: XVT 0x476810
unsigned int
opt_model_measure_node_and_raise_capacities(const struct opt_node *node,
					    struct scene_mesh *parent_state)
{
	if (node == NULL) {
		return 0;
	}

	unsigned int serialized_size = sizeof(struct opt_node);

	if (node->p_name != NULL) {

		serialized_size = (unsigned int)strlen(node->p_name) +
				  sizeof(struct opt_node) + 1;
	}

	int *param_data = node->payload;
	opt_node_type node_type = node->node_type;
	if (param_data != NULL) {
		switch (node_type) {
		case OPT_FACEDATA:
		case OPT_FACEDATA_QUAD_MESH:
		case OPT_FACEDATA_FACE_SET:
		case OPT_FACEDATA_TRIANGLE_STRIP_SET:
			if (g_scene_edge_flags_capacity < param_data[0]) {
				g_scene_edge_flags_capacity = param_data[0];
			}
			serialized_size += 4;
			serialized_size += (unsigned int)node->payload_count
					   << 6;
			serialized_size += 36 * node->payload_count;
			if (parent_state->p_vert_normals == NULL) {
				serialized_size += 12 * g_cur_vertex_count;
			}
			break;

		case OPT_TRANSFORM:
			serialized_size += 48;
			break;

		case OPT_MESHVERTS:
			g_cur_vertex_count = node->payload_count;
			serialized_size += 12 * g_cur_vertex_count;
			if (g_vertex_remap_capacity < node->payload_count) {
				g_vertex_remap_capacity = node->payload_count;
			}
			break;

		case OPT_TRANSLATION:
			serialized_size += 12;
			break;

		case OPT_ROTATION:
			serialized_size += 36;
			break;

		case OPT_SCALE:
			serialized_size += 12;
			break;

		case OPT_NODEREF:
			serialized_size +=
				(unsigned int)strlen((const char *)param_data) +
				1;
			break;

		case OPT_MATERIAL: {
			int record_count = node->payload_count;
			g_cur_mesh_materials = node->payload;
			int scaled_record_count = record_count << 3;
			scaled_record_count -= record_count;
			serialized_size += scaled_record_count << 3;
			break;
		}

		case OPT_VERTNORMALS: {
			int vector_value_count = 3 * node->payload_count;
			g_cur_vert_normals = node->payload;
			serialized_size += 4 * vector_value_count;
			parent_state->p_vert_normals =
				(struct opt_vector *)param_data;
			break;
		}

		case OPT_TEXCOORDS:
			serialized_size += 8 * node->payload_count;
			break;

		case OPT_BASE_COLOR:
			serialized_size += 12;
			break;

		case OPT_TEXTURE: {
			struct opt_texture_data *texture_data = node->payload;
			serialized_size += sizeof(*texture_data);
			int texture_byte_count =
				texture_data->height * texture_data->width;
			if (texture_byte_count == texture_data->texture_size) {
				serialized_size += texture_data->data_size;
			} else {
				serialized_size += texture_byte_count;
			}
			if (texture_data->inline_palette_count != 0) {
				serialized_size +=
					768 *
					texture_data->inline_palette_count;
			} else {
				uint8_t *embedded_palette =
					(uint8_t *)(texture_data + 1);
				if (texture_byte_count ==
				    texture_data->texture_size) {
					embedded_palette +=
						texture_data->data_size;
				} else {
					embedded_palette += texture_byte_count;
				}
				if ((uint8_t *)texture_data->palette ==
				    embedded_palette) {
					serialized_size += 12288;
				}
			}
			break;
		}

		case OPT_FACEGROUP:
			serialized_size += 4 * node->payload_count;
			break;

		case OPT_HARDPOINT:
			serialized_size += 16;
			break;

		case OPT_ROTSCALE:
			serialized_size += 48;
			break;

		case OPT_MESHDESC:
			serialized_size += 72;
			break;

		default:
			break;
		}
	} else if (node_type == OPT_TEXTURE) {
		struct opt_texture_data *texture_data = node->payload;
		serialized_size += sizeof(*texture_data);
		int texture_byte_count =
			texture_data->height * texture_data->width;
		if (texture_byte_count == texture_data->texture_size) {
			serialized_size += texture_data->data_size;
		} else {
			serialized_size += texture_byte_count;
		}
		if (texture_data->inline_palette_count != 0) {
			serialized_size +=
				768 * texture_data->inline_palette_count;
		} else {
			uint8_t *embedded_palette =
				(uint8_t *)(texture_data + 1);
			if (texture_byte_count == texture_data->texture_size) {
				embedded_palette += texture_data->data_size;
			} else {
				embedded_palette += texture_byte_count;
			}
			if ((uint8_t *)texture_data->palette ==
			    embedded_palette) {
				serialized_size += 12288;
			}
		}
	}

	if (node->child_count != 0) {
		struct scene_mesh child_state = *parent_state;
		g_cur_mesh_vertices = NULL;
		g_cur_mesh_tex_coords = NULL;
		g_cur_vert_normals = NULL;
		g_model_node_walk_unused_scratch2 = NULL;
		g_cur_mesh_materials = NULL;
		int child_index = 0;

		serialized_size +=
			sizeof(struct opt_node *) * node->child_count;

		if (node->child_count > 0) {
			int child_offset = 0;
			do {
				serialized_size +=
					opt_model_measure_node_and_raise_capacities(
						*(struct opt_node *
							  *)((uint8_t *)node
								     ->p_children +
							     child_offset),
						&child_state);
				child_offset += sizeof(*node->p_children);
				++child_index;
			} while (node->child_count > child_index);
		}
	}
	return serialized_size;
}

/* Rewrites entry_count RGB565 entries of palette, in place, in the display's
 * 16-bit format at the current brightness (flight_palette_build16_bpp_range). With
 * g_use_hardware3d set it first runs model_texture_filter_hardware_palette on the
 * palette. Does not check entry_count against its 4096-entry buffer. */
// FUNCTION: XVT 0x476B90
void opt_model_prepare_texture_palette(uint16_t *palette, int entry_count)
{
	if (g_use_hardware3d != 0) {
		model_texture_filter_hardware_palette(palette);
	}

	struct rgb_triplet src_rgb[4096];
	if (entry_count > 0) {
		uint8_t *rgb_cursor = (uint8_t *)src_rgb;
		uint16_t *palette_entry = palette;
		int entries_remaining = entry_count;
		do {
			unsigned int packed_color = *palette_entry++;
			uint8_t blue = (uint8_t)(packed_color & 0x1Fu);
			packed_color >>= 5;
			rgb_cursor[2] = (uint8_t)(2 * blue);
			uint8_t green = (uint8_t)(packed_color & 0x3Fu);
			packed_color >>= 6;
			uint8_t red = (uint8_t)(packed_color & 0x1Fu);
			rgb_cursor[1] = green;
			rgb_cursor[0] = (uint8_t)(2 * red);
			rgb_cursor += 3;
		} while (--entries_remaining != 0);
	}

	flight_palette_build16_bpp_range(src_rgb, palette, 0, entry_count);
}

/* Returns the bytes src_node and everything below it take in a runtime model;
 * with dst NULL it only measures, otherwise it also writes them there. Copies
 * each node with its name, child table and payload. A texture whose texture_size
 * equals width times height, its data holding the mip levels, drops the top
 * level when g_texture_resolution_level is 0 and both sides are over 8. Any other
 * texture copies its top level and gets mip levels added until a side is 1.
 * With g_mipmapping_enabled set each new texel is the 2-by-2 average of the
 * colors 8192 bytes into the palette, matched back to the nearest of the 256
 * there; otherwise the levels' bytes are left unwritten. Palettes are converted
 * for the display: on an 8-bit display the 4096 RGB565 colors map through
 * g_active_rgb565_to_palette_index_lut to 4096 bytes, and with
 * g_generate_mission_palette set the texels feed
 * image_quantizer_classify_indexed_rgb565_image once per 256-color sub-palette; on
 * a 16-bit display the 8192 bytes are copied and repacked by
 * opt_model_prepare_texture_palette. A texture that uses another's palette copies
 * none. Raises g_scene_edge_flags_capacity and g_vertex_remap_capacity, sets
 * g_cur_vertex_count, g_cur_mesh_materials, g_cur_vert_normals and
 * mesh_state->p_vert_normals at those nodes, and sets g_cur_mesh_vertices,
 * g_cur_mesh_tex_coords, g_cur_vert_normals, g_model_node_walk_unused_scratch2 and
 * g_cur_mesh_materials to NULL before a node's children. A child of size 0 leaves
 * a NULL slot. The modern build aligns each node. */
// FUNCTION: XVT 0x476C20
unsigned int opt_model_build_runtime_node(const struct opt_node *src_node,
					  struct scene_mesh *mesh_state,
					  uint8_t *dst)
{
	enum {
		OPT_TEXTURE_PALETTE_ENTRY_COUNT = 4096,
		OPT_TEXTURE_SUBPALETTE_COUNT = 16,
		OPT_TEXTURE_SUBPALETTE_ENTRY_COUNT = 256,
		OPT_TEXTURE_FULL_RES_THRESHOLD = 8,
		RGB565_GREEN_SHIFT = 5,
		RGB565_GREEN_BITS = 6,
		RGB565_GREEN_MASK = 0x3f,
		RGB565_RED_BLUE_MASK = 0x1f,
	};

	if (src_node == NULL) {
		return 0;
	}
	struct opt_node *runtime_node;
	if (dst != NULL) {
		memcpy(dst, src_node, sizeof(*src_node));
		runtime_node = (struct opt_node *)dst;
		dst += sizeof(*src_node);
	}
	unsigned int total_size = sizeof(*src_node);
	if (src_node->p_name != NULL) {
		if (dst != NULL) {
			runtime_node->p_name = (char *)dst;
			strcpy((char *)dst, src_node->p_name);
			dst += strlen(src_node->p_name) + 1;
		}
		total_size = (unsigned int)strlen(src_node->p_name) +
			     sizeof(*src_node) + 1;
	}
	total_size = (unsigned int)xvt_opt_align_size(total_size);
	if (dst) {
		dst = xvt_opt_align_pointer(dst);
	}
	if (src_node->child_count != 0) {
		if (dst != NULL) {
			runtime_node->p_children = (struct opt_node **)dst;
			dst += sizeof(*src_node->p_children) *
			       (unsigned int)src_node->child_count;
		}
		total_size += sizeof(*src_node->p_children) *
			      (unsigned int)src_node->child_count;
	}

	void *source_payload = src_node->payload;
	unsigned int payload_size;
	switch (src_node->node_type) {
	case OPT_FACEDATA:
	case OPT_FACEDATA_QUAD_MESH:
	case OPT_FACEDATA_FACE_SET:
	case OPT_FACEDATA_TRIANGLE_STRIP_SET:
		if (g_scene_edge_flags_capacity <
		    *(const int *)source_payload) {
			g_scene_edge_flags_capacity =
				*(const int *)source_payload;
		}
		payload_size =
			sizeof(int) +
			(unsigned int)src_node->payload_count *
				(sizeof(struct opt_packed_face_record) + 36u);
		if (mesh_state->p_vert_normals == NULL) {
			payload_size += sizeof(struct opt_vector) *
					(unsigned int)g_cur_vertex_count;
		}
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_TRANSFORM:
		payload_size = 48;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_MESHVERTS:
		payload_size = sizeof(struct opt_vector) *
			       (unsigned int)src_node->payload_count;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		g_cur_vertex_count = src_node->payload_count;
		if (src_node->payload_count > g_vertex_remap_capacity) {
			g_vertex_remap_capacity = src_node->payload_count;
		}
		break;
	case OPT_TRANSLATION:
		payload_size = 12;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_ROTATION:
		payload_size = 36;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_SCALE:
		payload_size = 12;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_NODEREF:
		payload_size =
			(unsigned int)strlen((const char *)source_payload) + 1;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_MATERIAL:
		payload_size = 56u * (unsigned int)src_node->payload_count;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		g_cur_mesh_materials = source_payload;
		total_size += payload_size;
		break;
	case OPT_VERTNORMALS:
		payload_size = sizeof(struct opt_vector) *
			       (unsigned int)src_node->payload_count;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		g_cur_vert_normals = (struct opt_vector *)source_payload;
		total_size += payload_size;
		mesh_state->p_vert_normals =
			(struct opt_vector *)source_payload;
		break;
	case OPT_TEXCOORDS:
		payload_size = 8u * (unsigned int)src_node->payload_count;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_BASE_COLOR:
		payload_size = 12;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_TEXTURE: {
		const struct opt_texture_data *source_texture =
			(const struct opt_texture_data *)src_node->payload;
		int palette_index;
		const uint8_t *source_palette;
		const uint16_t *source_palette16;
		const uint8_t *source_texels;
		if (dst != NULL && g_generate_mission_palette != 0 &&
		    g_flight_bytes_per_pixel == 1) {
			source_texels = (const uint8_t *)source_texture +
					sizeof(*source_texture);
			source_palette =
				source_texels +
				source_texture->height * source_texture->width;
			if ((unsigned int)source_texture->texture_size ==
			    (unsigned int)(source_texture->height *
					   source_texture->width)) {
				source_palette = source_texels +
						 source_texture->data_size;
			}
			source_palette16 =
				(const uint16_t
					 *)(source_palette +
					    OPT_TEXTURE_PALETTE_ENTRY_COUNT);
			for (palette_index = OPT_TEXTURE_SUBPALETTE_COUNT;
			     palette_index != 0; --palette_index) {
				image_quantizer_classify_indexed_rgb565_image(
					source_texels, source_palette16,
					(unsigned int)source_texture->width,
					(unsigned int)source_texture->height);
				source_palette16 +=
					OPT_TEXTURE_SUBPALETTE_ENTRY_COUNT;
			}
		}

		struct opt_texture_data *runtime_texture;
		unsigned int texture_payload_size;
		if ((unsigned int)source_texture->texture_size ==
		    (unsigned int)(source_texture->height *
				   source_texture->width)) {
			if (dst != NULL) {
				runtime_node->payload = dst;
				memcpy(dst, src_node->payload,
				       sizeof(*source_texture));
				source_texels =
					(const uint8_t *)source_texture +
					sizeof(*source_texture);
				dst += sizeof(*source_texture);
				runtime_texture = (struct opt_texture_data *)
							  runtime_node->payload;
				if (g_texture_resolution_level == 0 &&
				    runtime_texture->width >
					    OPT_TEXTURE_FULL_RES_THRESHOLD &&
				    runtime_texture->height >
					    OPT_TEXTURE_FULL_RES_THRESHOLD) {
					source_texels +=
						runtime_texture->height *
						runtime_texture->width;
					runtime_texture->data_size -=
						runtime_texture->height *
						runtime_texture->width;
					runtime_texture->width >>= 1;
					runtime_texture->height >>= 1;
					runtime_texture->texture_size =
						runtime_texture->width *
						runtime_texture->height;
				}
				memcpy(dst, source_texels,
				       (unsigned int)
					       runtime_texture->data_size);
				dst += runtime_texture->data_size;
				texture_payload_size =
					(unsigned int)
						runtime_texture->data_size +
					sizeof(*source_texture);
			} else {
				texture_payload_size =
					(unsigned int)
						source_texture->data_size +
					sizeof(*source_texture);
				if (g_texture_resolution_level == 0 &&
				    source_texture->width >
					    OPT_TEXTURE_FULL_RES_THRESHOLD &&
				    source_texture->height >
					    OPT_TEXTURE_FULL_RES_THRESHOLD) {
					texture_payload_size -=
						(unsigned int)(source_texture
								       ->height *
							       source_texture
								       ->width);
				}
			}
		} else {
			texture_payload_size =
				(unsigned int)(source_texture->height *
					       source_texture->width) +
				sizeof(*source_texture);
			int width;
			int height;
			if (dst != NULL) {
				runtime_node->payload = dst;
				memcpy(dst, src_node->payload,
				       texture_payload_size);
				dst += texture_payload_size;
				source_texels =
					(const uint8_t *)source_texture +
					sizeof(*source_texture);
				if (source_texture->inline_palette_count == 0) {
					source_palette =
						(const uint8_t *)
							source_texture->palette;
				} else {
					source_palette =
						source_texels +
						source_texture->height *
							source_texture->width;
				}
				source_palette16 =
					(const uint16_t
						 *)(source_palette +
						    2 * OPT_TEXTURE_PALETTE_ENTRY_COUNT);
				width = source_texture->width;
				height = source_texture->height;
				runtime_texture = (struct opt_texture_data *)
							  runtime_node->payload;
				runtime_texture->texture_size = width * height;
				runtime_texture->data_size = width * height;
				while (width > 1 && height > 1) {
					width >>= 1;
					height >>= 1;
					int mip_pixel_count = width * height;
					runtime_texture->data_size +=
						mip_pixel_count;
					if (g_mipmapping_enabled != 0 &&
					    height > 0) {
						int previous_width = 2 * width;
						int row_step = 4 * width;
						uint8_t *mip_row = dst;
						const uint8_t *mip_top_row =
							source_texels;
						const uint8_t *mip_bottom_row =
							source_texels +
							previous_width;
						for (int mip_y = 0;
						     mip_y < height; ++mip_y) {
							const uint8_t *top_texel =
								mip_top_row;
							const uint8_t *bottom_texel =
								mip_bottom_row;
							for (int mip_x = 0;
							     mip_x < width;
							     ++mip_x) {
								uint16_t packed_color = source_palette16
									[top_texel
										 [0]];
								int blue =
									packed_color &
									RGB565_RED_BLUE_MASK;
								packed_color >>=
									RGB565_GREEN_SHIFT;
								int green =
									packed_color &
									RGB565_GREEN_MASK;
								packed_color >>=
									RGB565_GREEN_BITS;
								int red =
									packed_color &
									RGB565_RED_BLUE_MASK;
								packed_color = source_palette16
									[top_texel
										 [1]];
								blue += packed_color &
									RGB565_RED_BLUE_MASK;
								packed_color >>=
									RGB565_GREEN_SHIFT;
								green +=
									packed_color &
									RGB565_GREEN_MASK;
								packed_color >>=
									RGB565_GREEN_BITS;
								red += packed_color &
								       RGB565_RED_BLUE_MASK;
								packed_color = source_palette16
									[bottom_texel
										 [0]];
								blue += packed_color &
									RGB565_RED_BLUE_MASK;
								packed_color >>=
									RGB565_GREEN_SHIFT;
								green +=
									packed_color &
									RGB565_GREEN_MASK;
								packed_color >>=
									RGB565_GREEN_BITS;
								red += packed_color &
								       RGB565_RED_BLUE_MASK;
								packed_color = source_palette16
									[bottom_texel
										 [1]];
								blue += packed_color &
									RGB565_RED_BLUE_MASK;
								packed_color >>=
									RGB565_GREEN_SHIFT;
								green +=
									packed_color &
									RGB565_GREEN_MASK;
								packed_color >>=
									RGB565_GREEN_BITS;
								red += packed_color &
								       RGB565_RED_BLUE_MASK;
								blue >>= 2;
								green >>= 2;
								red >>= 2;
								mip_row[mip_x] = color_find_nearest_rgb565_index(
									source_palette16,
									red,
									green,
									blue, 0,
									OPT_TEXTURE_SUBPALETTE_ENTRY_COUNT);
								top_texel += 2;
								bottom_texel +=
									2;
							}
							mip_row += width;
							mip_top_row += row_step;
							mip_bottom_row +=
								row_step;
						}
					}
					source_texels = dst;
					dst += mip_pixel_count;
					texture_payload_size +=
						(unsigned int)mip_pixel_count;
				}
			} else {
				width = source_texture->width;
				height = source_texture->height;
				while (width > 1 && height > 1) {
					width >>= 1;
					height >>= 1;
					texture_payload_size +=
						(unsigned int)(width * height);
				}
			}
		}

		source_texture =
			(const struct opt_texture_data *)src_node->payload;
		unsigned int source_palette_offset;
		if (source_texture->inline_palette_count != 0) {
			texture_payload_size +=
				(unsigned int)(source_texture
						       ->inline_palette_count *
					       g_flight_bytes_per_pixel) *
				OPT_TEXTURE_SUBPALETTE_ENTRY_COUNT;
			if (dst != NULL) {
				source_palette =
					(const uint8_t *)source_texture +
					sizeof(*source_texture);
				source_palette_offset =
					(unsigned int)(source_texture->height *
						       source_texture->width);
				if ((unsigned int)
					    source_texture->texture_size ==
				    source_palette_offset) {
					source_palette_offset =
						(unsigned int)source_texture
							->data_size;
				}
				source_palette += source_palette_offset;
				if (g_flight_bytes_per_pixel == 2) {
					source_palette +=
						(unsigned int)source_texture
							->inline_palette_count *
						OPT_TEXTURE_SUBPALETTE_ENTRY_COUNT;
				}
				if (g_flight_bytes_per_pixel == 1) {
					source_palette16 =
						(const uint16_t
							 *)(source_palette +
							    OPT_TEXTURE_PALETTE_ENTRY_COUNT);
					for (palette_index = 0;
					     palette_index <
					     OPT_TEXTURE_PALETTE_ENTRY_COUNT;
					     ++palette_index) {
						dst[palette_index] = g_active_rgb565_to_palette_index_lut
							[source_palette16
								 [palette_index]];
					}
				} else {
					memcpy(dst, source_palette,
					       (unsigned int)(g_flight_bytes_per_pixel *
							      OPT_TEXTURE_PALETTE_ENTRY_COUNT));
				}
				if (g_flight_bytes_per_pixel == 2) {
					if (src_node->p_name != NULL) {
						XVT_LOG_DEBUG(
							"models.node_palette node=\"%s\"",
							src_node->p_name);
					}
					opt_model_prepare_texture_palette(
						(uint16_t *)dst,
						OPT_TEXTURE_PALETTE_ENTRY_COUNT);
				}
				dst += (unsigned int)(source_texture
							      ->inline_palette_count *
						      g_flight_bytes_per_pixel) *
				       OPT_TEXTURE_SUBPALETTE_ENTRY_COUNT;
			}
		} else {
			source_palette = (const uint8_t *)source_texture +
					 sizeof(*source_texture);
			source_palette_offset =
				(unsigned int)(source_texture->height *
					       source_texture->width);
			if ((unsigned int)source_texture->texture_size ==
			    source_palette_offset) {
				source_palette_offset =
					(unsigned int)source_texture->data_size;
			}
			source_palette += source_palette_offset;
			if ((const uint8_t *)source_texture->palette ==
			    source_palette) {
				unsigned int palette_bytes =
					(unsigned int)g_flight_bytes_per_pixel *
					OPT_TEXTURE_PALETTE_ENTRY_COUNT;
				texture_payload_size += palette_bytes;
				if (dst != NULL) {
					if (g_flight_bytes_per_pixel == 2) {
						source_palette +=
							OPT_TEXTURE_PALETTE_ENTRY_COUNT;
					}
					if (g_flight_bytes_per_pixel == 1) {
						source_palette16 =
							(const uint16_t
								 *)(source_palette +
								    OPT_TEXTURE_PALETTE_ENTRY_COUNT);
						for (palette_index = 0;
						     palette_index <
						     OPT_TEXTURE_PALETTE_ENTRY_COUNT;
						     ++palette_index) {
							dst[palette_index] = g_active_rgb565_to_palette_index_lut
								[source_palette16
									 [palette_index]];
						}
					} else {
						memcpy(dst, source_palette,
						       palette_bytes);
					}
					if (g_flight_bytes_per_pixel == 2) {
						if (src_node->p_name != NULL) {
							XVT_LOG_DEBUG(
								"models.node_palette node=\"%s\"",
								src_node->p_name);
						}
						opt_model_prepare_texture_palette(
							(uint16_t *)dst,
							OPT_TEXTURE_PALETTE_ENTRY_COUNT);
					}
					dst += (unsigned int)
						       g_flight_bytes_per_pixel *
					       OPT_TEXTURE_PALETTE_ENTRY_COUNT;
				}
			}
		}
		total_size += texture_payload_size;
		break;
	}
	case OPT_FACEGROUP:
		payload_size = 4u * (unsigned int)src_node->payload_count;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_HARDPOINT:
		payload_size = 16;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_ROTSCALE:
		payload_size = 48;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	case OPT_MESHDESC:
		payload_size = 72;
		if (dst != NULL) {
			runtime_node->payload = dst;
			memcpy(dst, src_node->payload, payload_size);
			dst += payload_size;
		}
		total_size += payload_size;
		break;
	default:
		break;
	}
	total_size = (unsigned int)xvt_opt_align_size(total_size);
	if (dst) {
		dst = xvt_opt_align_pointer(dst);
	}
	if (src_node->child_count != 0) {
		struct scene_mesh child_mesh = *mesh_state;
		g_cur_mesh_vertices = NULL;
		g_cur_mesh_tex_coords = NULL;
		g_cur_vert_normals = NULL;
		g_model_node_walk_unused_scratch2 = NULL;
		g_cur_mesh_materials = NULL;
		for (int child_index = 0; child_index < src_node->child_count;
		     ++child_index) {
			if (dst != NULL) {
				((struct opt_node **)runtime_node
					 ->p_children)[child_index] =
					(struct opt_node *)dst;
			}
			payload_size = opt_model_build_runtime_node(
				src_node->p_children[child_index], &child_mesh,
				dst);
			if (dst != NULL) {
				if (payload_size == 0) {
					((struct opt_node **)runtime_node
						 ->p_children)[child_index] =
						NULL;
				}
				dst += payload_size;
			}
			total_size += payload_size;
		}
	}
	return (unsigned int)xvt_opt_align_size(total_size);
}

/* Returns the first node, roots in order and each depth first, whose name is
 * name ignoring case (opt_model_find_node_by_name), or NULL. */
// FUNCTION: XVT 0x479DE0
struct opt_node *
opt_model_resolve_node_ref(const struct optimized_poly_object *object,
			   const char *name)
{
	for (int root_index = 0; root_index < object->root_node_count;
	     ++root_index) {
		struct opt_node *result = opt_model_find_node_by_name(
			object->root_nodes[root_index], name);
		if (result != NULL) {
			return result;
		}
	}

	return NULL;
}

/* Returns node or the first node below it, depth first, whose name is name
 * ignoring case, or NULL. Does not follow OPT_NODEREF links. */
// FUNCTION: XVT 0x479E20
struct opt_node *opt_model_find_node_by_name(struct opt_node *node,
					     const char *name)
{
	if (node == NULL) {
		return NULL;
	}
	int name_compare;
	if (node->p_name != NULL) {
		name_compare = strcasecmp(node->p_name, name);
		if (name_compare == 0) {
			return node;
		}
	}

	for (int child_index = 0; child_index < node->child_count;
	     ++child_index) {
		struct opt_node *result = opt_model_find_node_by_name(
			node->p_children[child_index], name);
		if (result != NULL) {
			return result;
		}
	}

	return NULL;
}
