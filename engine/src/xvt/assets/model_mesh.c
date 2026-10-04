#include "xvt/assets/model_mesh.h"
#ifdef XVT_MODERN
#include "xvt_runtime/assets/opt_native.h"
#endif
#include "xvt/assets/model_mesh_internal.h"
#include "xvt/assets/object_type.h"
#include "xvt/math/math.h"
#include "xvt/math/trig2.h"
#include "xvt/util/memory.h"

/* X of the vector the last point rotation produced. Many functions write it,
 * chiefly pai_calcrotatedpoint, which turns a local vector into world axes
 * here, and model_mesh_apply_animated_mesh_rotation_to_point; callers read it right
 * after the call and often scale or offset it in place. */
// GLOBAL: XVT 0x9D8A58
int g_rotated_x = 0;
/* Y of the vector the last point rotation produced; written and read like
 * g_rotated_x. */
// GLOBAL: XVT 0x9D8A54
int g_rotated_y = 0;
/* Z of the vector the last point rotation produced; written and read like
 * g_rotated_x. */
// GLOBAL: XVT 0x9D8B60
int g_rotated_z = 0;

/* Per object type 0 to 72, its mesh count and, for up to 50 meshes, each mesh's
 * type and descriptor. Only model_mesh_build_object_type_mesh_cache fills it, after
 * the flight resources load. */
// GLOBAL: XVT 0xA00870
struct model_mesh_object_type_cache g_object_type_mesh_cache[73] = {0};
/* Hardpoints passed so far in the current model_mesh_find_nth_hardpoint_node
 * search: that function sets it to 0 and
 * model_mesh_find_nth_hardpoint_node_recursive raises it. */
// GLOBAL: XVT 0x528128
int g_opt_hardpoint_search_index = 0;

/* Sets g_rotated_x, g_rotated_y and g_rotated_z to the local point, then, when
 * mesh mesh_index has an OPT_ROTSCALE node (model_mesh_get_rot_scale_data), turns
 * that point by angle_q16 (65,536 a full circle) about the node's axis: its
 * floats 3 to 5 cast to int and taken as 1.15 fixed point, through the point of
 * its floats 0 to 2 with the y negated. Leaves the result in the same three
 * globals. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4285A0
void model_mesh_apply_animated_mesh_rotation_to_point(int16_t angle_q16,
						      int object_type,
						      int mesh_index,
						      int local_x, int local_y,
						      int local_z)
{
	float *rot_scale_data;
	int axis_x;
	int axis_y;
	int axis_z;
	int cosine;
	int sine;
	int coefficient00;
	int coefficient01;
	int coefficient02;
	int coefficient10;
	int coefficient11;
	int coefficient12;
	int coefficient20;
	int coefficient21;
	int coefficient22;
	int transformed_x;
	int transformed_y;
	int transformed_z;

	g_rotated_x = local_x;
	g_rotated_y = local_y;
	g_rotated_z = local_z;
	rot_scale_data = model_mesh_get_rot_scale_data(object_type, mesh_index);
	if (rot_scale_data == NULL) {
		return;
	}

	axis_x = (int)rot_scale_data[3];
	axis_y = (int)rot_scale_data[4];
	axis_z = (int)rot_scale_data[5];
	cosine = trig2_getsignedcos(angle_q16);
	sine = trig2_getsignedsin(angle_q16);
	if (cosine >= 0) {
		const int one_minus_cosine = MODEL_MESH_Q15_ONE - cosine;

		coefficient00 = math_rodrigues_term_nonnegative_cos(
			axis_x, axis_x, one_minus_cosine, cosine);
		coefficient01 = math_rodrigues_term_nonnegative_cos(
			axis_x, axis_y, one_minus_cosine,
			math_mul_q15(sine, axis_z));
		coefficient02 = math_rodrigues_term_nonnegative_cos(
			axis_x, axis_z, one_minus_cosine,
			-math_mul_q15(sine, axis_y));
		coefficient10 = math_rodrigues_term_nonnegative_cos(
			axis_x, axis_y, one_minus_cosine,
			-math_mul_q15(sine, axis_z));
		coefficient11 = math_rodrigues_term_nonnegative_cos(
			axis_y, axis_y, one_minus_cosine, cosine);
		coefficient12 = math_rodrigues_term_nonnegative_cos(
			axis_y, axis_z, one_minus_cosine,
			math_mul_q15(sine, axis_x));
		coefficient20 = math_rodrigues_term_nonnegative_cos(
			axis_x, axis_z, one_minus_cosine,
			math_mul_q15(sine, axis_y));
		coefficient21 = math_rodrigues_term_nonnegative_cos(
			axis_y, axis_z, one_minus_cosine,
			-math_mul_q15(sine, axis_x));
		coefficient22 = math_rodrigues_term_nonnegative_cos(
			axis_z, axis_z, one_minus_cosine, cosine);
	} else {
		coefficient00 = math_rodrigues_term_negative_cos(
			axis_x, axis_x, -cosine, cosine);
		coefficient01 = math_rodrigues_term_negative_cos(
			axis_x, axis_y, -cosine, math_mul_q15(sine, axis_z));
		coefficient02 = math_rodrigues_term_negative_cos(
			axis_x, axis_z, -cosine, -math_mul_q15(sine, axis_y));
		coefficient10 = math_rodrigues_term_negative_cos(
			axis_x, axis_y, -cosine, -math_mul_q15(sine, axis_z));
		coefficient11 = math_rodrigues_term_negative_cos(
			axis_y, axis_y, -cosine, cosine);
		coefficient12 = math_rodrigues_term_negative_cos(
			axis_y, axis_z, -cosine, math_mul_q15(sine, axis_x));
		coefficient20 = math_rodrigues_term_negative_cos(
			axis_x, axis_z, -cosine, math_mul_q15(sine, axis_y));
		coefficient21 = math_rodrigues_term_negative_cos(
			axis_y, axis_z, -cosine, -math_mul_q15(sine, axis_x));
		coefficient22 = math_rodrigues_term_negative_cos(
			axis_z, axis_z, -cosine, cosine);
	}

	local_x -= (int)rot_scale_data[0];
	local_y += (int)rot_scale_data[1];
	local_z -= (int)rot_scale_data[2];
	transformed_x =
		math_dot3q15_wrapped(local_x, local_y, local_z, coefficient00,
				     coefficient10, coefficient20);
	transformed_y =
		math_dot3q15_wrapped(local_x, local_y, local_z, coefficient01,
				     coefficient11, coefficient21);
	transformed_z =
		math_dot3q15_wrapped(local_x, local_y, local_z, coefficient02,
				     coefficient12, coefficient22);
	transformed_y -= (int)rot_scale_data[1];
	transformed_z += (int)rot_scale_data[2];
	transformed_x += (int)rot_scale_data[0];
	g_rotated_x = transformed_x;
	g_rotated_y = transformed_y;
	g_rotated_z = transformed_z;
}

/* Returns the object type's mesh count: its model's root count, 1 less when the
 * first root is an OPT_TEXTURE, capped at 50. Returns 0 when g_loaded_models
 * holds no handle for it or its asset_flags lacks the 0x1 bit. */
// FUNCTION: XVT 0x4ADC40
int model_mesh_get_object_type_mesh_count(int object_type)
{
	uint16_t model_handle;
	struct optimized_poly_object *model;
	int mesh_count;

	model_handle = g_loaded_models[object_type];
	if (model_handle == 0) {
		return 0;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		model_handle);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}
	mesh_count = model->root_node_count;
	if (model->root_nodes[0]->node_type == OPT_TEXTURE) {
		--mesh_count;
	}
	memory_handle_block_done_stub(g_loaded_models[object_type]);

	if (mesh_count > 50) {
		mesh_count = 50;
	}

	return mesh_count;
}

/* Returns the first OPT_MESHVERTS node at or below node, depth first, or NULL.
 * Skips NULL child slots and does not follow OPT_NODEREF links. */
// FUNCTION: XVT 0x4ADCC0
struct opt_node *model_mesh_find_first_mesh_verts_node(struct opt_node *node)
{
	struct opt_node *result;
	int child_index;

	if (node == 0) {
		return 0;
	}

	if (node->node_type == OPT_MESHVERTS) {
		return node;
	}

	for (child_index = 0; child_index < node->child_count; child_index++) {
		if (node->p_children[child_index] != 0) {
			result = model_mesh_find_first_mesh_verts_node(
				node->p_children[child_index]);
			if (result != 0) {
				return result;
			}
		}
	}

	return 0;
}

/* Returns the first OPT_ROTSCALE node at or below node, depth first, or NULL.
 * Skips NULL child slots and does not follow OPT_NODEREF links. */
// FUNCTION: XVT 0x4ADD10
struct opt_node *model_mesh_find_first_rot_scale_node(struct opt_node *node)
{
	struct opt_node *result;
	int child_index;

	if (node == 0) {
		return 0;
	}

	if (node->node_type == OPT_ROTSCALE) {
		return node;
	}

	for (child_index = 0; child_index < node->child_count; child_index++) {
		if (node->p_children[child_index] != 0) {
			result = model_mesh_find_first_rot_scale_node(
				node->p_children[child_index]);
			if (result != 0) {
				return result;
			}
		}
	}

	return 0;
}

/* Returns, as a mesh_descriptor, the payload of the first OPT_MESHDESC node at
 * or below node, depth first, or NULL. Skips NULL child slots, does not follow
 * OPT_NODEREF links and ignores model. */
// FUNCTION: XVT 0x4AE1A0
struct mesh_descriptor *
model_mesh_find_descriptor_node_recursive(const struct opt_node *node,
					  struct optimized_poly_object *model)
{
	struct mesh_descriptor *descriptor;
	int child_index;

	if (node == NULL) {
		return NULL;
	}
	if (node->node_type == OPT_MESHDESC) {
		return (struct mesh_descriptor *)node->payload;
	}

	for (child_index = 0; child_index < node->child_count; ++child_index) {
		if (node->p_children[child_index] != NULL) {
			descriptor = model_mesh_find_descriptor_node_recursive(
				node->p_children[child_index], model);
			if (descriptor != NULL) {
				return descriptor;
			}
		}
	}

	return NULL;
}

/* Returns the mesh_descriptor of mesh mesh_index of the object type's model, or
 * NULL; NULL also when g_loaded_models holds no handle for it, mesh_index is
 * negative or its asset_flags lacks the 0x1 bit. Mesh n is root node n, or n + 1
 * when the first root is an OPT_TEXTURE, cut to the last root; the other
 * ModelMesh getters find a mesh the same way. */
// FUNCTION: XVT 0x4AE200
struct mesh_descriptor *model_mesh_get_descriptor(int object_type,
						  int mesh_index)
{
	uint16_t model_handle;
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;

	model_handle = g_loaded_models[object_type];
	if (model_handle == 0) {
		return NULL;
	}
	if (mesh_index < 0) {
		return NULL;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return NULL;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		model_handle);
	memory_handle_block_done_stub(g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	if (mesh_index >= model->root_node_count) {
		mesh_index = model->root_node_count - 1;
	}
	return model_mesh_find_descriptor_node_recursive(root_nodes[mesh_index],
							 model);
}

/* Returns the mesh_type of mesh mesh_index's descriptor;
 * MESH_COMPONENT_00_DEFAULT when it has none, when mesh_index is negative, when
 * g_loaded_models holds no handle or when asset_flags lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AE2A0
mesh_component_type model_mesh_get_object_type_mesh_type(int object_type,
							 int mesh_index)
{
	uint16_t model_handle;
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct mesh_descriptor *descriptor;

	model_handle = g_loaded_models[object_type];
	if (model_handle == 0) {
		return MESH_COMPONENT_00_DEFAULT;
	}
	if (mesh_index < 0) {
		return MESH_COMPONENT_00_DEFAULT;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return MESH_COMPONENT_00_DEFAULT;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		model_handle);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	if (mesh_index >= model->root_node_count) {
		mesh_index = model->root_node_count - 1;
	}

	descriptor = model_mesh_find_descriptor_node_recursive(
		root_nodes[mesh_index], model);
	/* From here mesh_index holds the result, no longer a root node index: the descriptor's mesh type,
	 * or MESH_COMPONENT_00_DEFAULT without a descriptor. */
	if (descriptor != NULL) {
		mesh_index = descriptor->mesh_type;
	} else {
		mesh_index = MESH_COMPONENT_00_DEFAULT;
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return mesh_index;
}

/* Returns the vertex count of mesh mesh_index's first OPT_MESHVERTS node; 0 when
 * asset_flags lacks the 0x1 bit. Does not check for a missing model, a negative
 * mesh_index or a mesh without a vertex node. */
// FUNCTION: XVT 0x4AE340
int model_mesh_get_vertex_count(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	int node_type;
	struct opt_node *vertex_node;
	int vertex_count;

	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	node_type = root_nodes[0]->node_type;
	if (node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	if (mesh_index >= model->root_node_count) {
		mesh_index = model->root_node_count - 1;
	}

	vertex_node =
		model_mesh_find_first_mesh_verts_node(root_nodes[mesh_index]);
	vertex_count = vertex_node->payload_count;

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return vertex_count;
}

/* Returns, cut to an int, the x of vertex vertex_index, lowered to the last one,
 * of mesh mesh_index's first OPT_MESHVERTS node; 0 when asset_flags lacks the 0x1
 * bit. Does not check for a missing model, a negative index or a mesh without a
 * vertex node. */
// FUNCTION: XVT 0x4AE3C0
int model_mesh_get_vertex_x(int object_type, int mesh_index, int vertex_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct opt_node *vertex_node;
	struct opt_vector *vertices;
	int clamped_vertex_index;
	int result;

	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	if (mesh_index >= model->root_node_count) {
		mesh_index = model->root_node_count - 1;
	}

	vertex_node =
		model_mesh_find_first_mesh_verts_node(root_nodes[mesh_index]);
	vertices = (struct opt_vector *)vertex_node->payload;
	clamped_vertex_index = vertex_index;
	if (clamped_vertex_index >= vertex_node->payload_count) {
		clamped_vertex_index = vertex_node->payload_count - 1;
	}
	result = (int)vertices[clamped_vertex_index].x;

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return result;
}

/* Returns, cut to an int, the y of vertex vertex_index, lowered to the last one,
 * of mesh mesh_index's first OPT_MESHVERTS node; 0 when asset_flags lacks the 0x1
 * bit. Does not check for a missing model, a negative index or a mesh without a
 * vertex node. */
// FUNCTION: XVT 0x4AE460
int model_mesh_get_vertex_y(int object_type, int mesh_index, int vertex_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct opt_node *vertex_node;
	struct opt_vector *vertices;
	int clamped_vertex_index;
	int result;

	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	if (mesh_index >= model->root_node_count) {
		mesh_index = model->root_node_count - 1;
	}

	vertex_node =
		model_mesh_find_first_mesh_verts_node(root_nodes[mesh_index]);
	vertices = (struct opt_vector *)vertex_node->payload;
	clamped_vertex_index = vertex_index;
	if (clamped_vertex_index >= vertex_node->payload_count) {
		clamped_vertex_index = vertex_node->payload_count - 1;
	}
	result = (int)vertices[clamped_vertex_index].y;

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return result;
}

/* Returns, cut to an int, the z of vertex vertex_index, lowered to the last one,
 * of mesh mesh_index's first OPT_MESHVERTS node; 0 when asset_flags lacks the 0x1
 * bit. Does not check for a missing model, a negative index or a mesh without a
 * vertex node. */
// FUNCTION: XVT 0x4AE500
int model_mesh_get_vertex_z(int object_type, int mesh_index, int vertex_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct opt_node *vertex_node;
	struct opt_vector *vertices;
	int clamped_vertex_index;
	int result;

	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	if (mesh_index >= model->root_node_count) {
		mesh_index = model->root_node_count - 1;
	}

	vertex_node =
		model_mesh_find_first_mesh_verts_node(root_nodes[mesh_index]);
	vertices = (struct opt_vector *)vertex_node->payload;
	clamped_vertex_index = vertex_index;
	if (clamped_vertex_index >= vertex_node->payload_count) {
		clamped_vertex_index = vertex_node->payload_count - 1;
	}
	result = (int)vertices[clamped_vertex_index].z;

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return result;
}

/* Returns, cut to an int, center.x of mesh mesh_index's descriptor; 0 without
 * one, for a negative mesh_index, or when asset_flags lacks the 0x1 bit. Does not
 * check for a missing model. */
// FUNCTION: XVT 0x4AE5A0
int model_mesh_get_center_x(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct mesh_descriptor *descriptor;
	int node_index;
	int result;

	if (mesh_index < 0) {
		return 0;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	node_index = mesh_index;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++node_index;
	}
	if (node_index >= model->root_node_count) {
		node_index = model->root_node_count - 1;
	}

	descriptor = model_mesh_find_descriptor_node_recursive(
		root_nodes[node_index], model);
	if (descriptor != NULL) {
		result = (int)descriptor->center.x;
	} else {
		result = 0;
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return result;
}

/* Returns, cut to an int, center.y of mesh mesh_index's descriptor; 0 without
 * one, for a negative mesh_index, or when asset_flags lacks the 0x1 bit. Does not
 * check for a missing model. */
// FUNCTION: XVT 0x4AE640
int model_mesh_get_center_y(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct mesh_descriptor *descriptor;
	int root_node_count;
	int result;

	if (mesh_index < 0) {
		return 0;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	root_node_count = model->root_node_count;
	if (root_node_count <= mesh_index) {
		mesh_index = root_node_count - 1;
	}

	descriptor = model_mesh_find_descriptor_node_recursive(
		root_nodes[mesh_index], model);
	if (descriptor != NULL) {
		result = (int)descriptor->center.y;
	} else {
		result = 0;
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return result;
}

/* Returns, cut to an int, center.z of mesh mesh_index's descriptor; 0 without
 * one, for a negative mesh_index, or when asset_flags lacks the 0x1 bit. Does not
 * check for a missing model. */
// FUNCTION: XVT 0x4AE6E0
int model_mesh_get_center_z(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct mesh_descriptor *descriptor;
	int result;

	if (mesh_index < 0) {
		return 0;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	mesh_index = mesh_index < model->root_node_count
			     ? mesh_index
			     : model->root_node_count - 1;

	descriptor = model_mesh_find_descriptor_node_recursive(
		root_nodes[mesh_index], model);
	if (descriptor != NULL) {
		result = (int)descriptor->center.z;
	} else {
		result = 0;
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return result;
}

/* Returns, cut to an int, box_min.x of mesh mesh_index's descriptor; 0 without
 * one, for a negative mesh_index, or when asset_flags lacks the 0x1 bit. Nothing
 * calls this. */
// FUNCTION: XVT 0x4AE780
int model_mesh_get_bounds_min_x(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct mesh_descriptor *descriptor;
	int node_index;
	int result;

	if (mesh_index < 0) {
		return 0;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	node_index = mesh_index;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++node_index;
	}
	if (node_index >= model->root_node_count) {
		node_index = model->root_node_count - 1;
	}

	descriptor = model_mesh_find_descriptor_node_recursive(
		root_nodes[node_index], model);
	if (descriptor != NULL) {
		result = (int)descriptor->box_min.x;
	} else {
		result = 0;
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return result;
}

/* Returns, cut to an int, box_min.y of mesh mesh_index's descriptor; 0 without
 * one, for a negative mesh_index, or when asset_flags lacks the 0x1 bit. Nothing
 * calls this. */
// FUNCTION: XVT 0x4AE820
int model_mesh_get_bounds_min_y(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct mesh_descriptor *descriptor;
	int node_index;
	int result;

	if (mesh_index < 0) {
		return 0;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	node_index = mesh_index;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++node_index;
	}
	if (node_index >= model->root_node_count) {
		node_index = model->root_node_count - 1;
	}
	descriptor = model_mesh_find_descriptor_node_recursive(
		root_nodes[node_index], model);
	if (descriptor != NULL) {
		result = (int)descriptor->box_min.y;
	} else {
		result = 0;
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return result;
}

/* Returns, cut to an int, box_min.z of mesh mesh_index's descriptor; 0 without
 * one, for a negative mesh_index, or when asset_flags lacks the 0x1 bit. Nothing
 * calls this. */
// FUNCTION: XVT 0x4AE8C0
int model_mesh_get_bounds_min_z(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct mesh_descriptor *descriptor;
	int result;

	if (mesh_index < 0) {
		return 0;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	mesh_index = mesh_index < model->root_node_count
			     ? mesh_index
			     : model->root_node_count - 1;

	descriptor = model_mesh_find_descriptor_node_recursive(
		root_nodes[mesh_index], model);
	if (descriptor != NULL) {
		result = (int)descriptor->box_min.z;
	} else {
		result = 0;
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return result;
}

/* Returns, cut to an int, box_max.x of mesh mesh_index's descriptor; 0 without
 * one, for a negative mesh_index, or when asset_flags lacks the 0x1 bit. Nothing
 * calls this. */
// FUNCTION: XVT 0x4AE960
int model_mesh_get_bounds_max_x(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct mesh_descriptor *descriptor;
	int node_index;
	int result;

	if (mesh_index < 0) {
		return 0;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	node_index = mesh_index;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++node_index;
	}
	if (node_index >= model->root_node_count) {
		node_index = model->root_node_count - 1;
	}

	descriptor = model_mesh_find_descriptor_node_recursive(
		root_nodes[node_index], model);
	if (descriptor != NULL) {
		result = (int)descriptor->box_max.x;
	} else {
		result = 0;
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return result;
}

/* Returns, cut to an int, box_max.y of mesh mesh_index's descriptor; 0 without
 * one, for a negative mesh_index, or when asset_flags lacks the 0x1 bit. Nothing
 * calls this. */
// FUNCTION: XVT 0x4AEA00
int model_mesh_get_bounds_max_y(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct mesh_descriptor *descriptor;
	int result;

	if (mesh_index < 0) {
		return 0;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	mesh_index = mesh_index < model->root_node_count
			     ? mesh_index
			     : model->root_node_count - 1;

	descriptor = model_mesh_find_descriptor_node_recursive(
		root_nodes[mesh_index], model);
	if (descriptor != NULL) {
		result = (int)descriptor->box_max.y;
	} else {
		result = 0;
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return result;
}

/* Returns, cut to an int, box_max.z of mesh mesh_index's descriptor; 0 without
 * one, for a negative mesh_index, or when asset_flags lacks the 0x1 bit. Nothing
 * calls this. */
// FUNCTION: XVT 0x4AEAA0
int model_mesh_get_bounds_max_z(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct mesh_descriptor *descriptor;
	int result;

	if (mesh_index < 0) {
		return 0;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	mesh_index = mesh_index < model->root_node_count
			     ? mesh_index
			     : model->root_node_count - 1;

	descriptor = model_mesh_find_descriptor_node_recursive(
		root_nodes[mesh_index], model);
	if (descriptor != NULL) {
		result = (int)descriptor->box_max.z;
	} else {
		result = 0;
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return result;
}

/* Returns the targetId of mesh mesh_index's descriptor; 0 without one, for a
 * negative mesh_index, or when asset_flags lacks the 0x1 bit. Does not check for
 * a missing model. */
// FUNCTION: XVT 0x4AEB40
int model_mesh_get_target_id(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct mesh_descriptor *descriptor;

	if (mesh_index < 0) {
		return 0;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	if (model->root_node_count <= mesh_index) {
		mesh_index = model->root_node_count - 1;
	}

	descriptor = model_mesh_find_descriptor_node_recursive(
		root_nodes[mesh_index], model);
	/* From here mesh_index holds the result, no longer a root node index: the descriptor's target id,
	 * or 0 without a descriptor. */
	if (descriptor != NULL) {
		mesh_index = descriptor->target_id;
	} else {
		mesh_index = 0;
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return mesh_index;
}

/* Returns, cut to an int, target_point.x of mesh mesh_index's descriptor when its
 * targetId is nonzero, else center.x; 0 without a descriptor, for a negative
 * mesh_index, or when asset_flags lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AEBE0
int model_mesh_get_component_focus_x(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct mesh_descriptor *descriptor;
	int value;

	if (mesh_index < 0) {
		return 0;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	if (mesh_index >= model->root_node_count) {
		mesh_index = model->root_node_count - 1;
	}

	descriptor = model_mesh_find_descriptor_node_recursive(
		root_nodes[mesh_index], model);
	if (descriptor != NULL) {
		if (descriptor->target_id != 0) {
			value = (int)descriptor->target_point.x;
		} else {
			value = (int)descriptor->center.x;
		}
	} else {
		value = 0;
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return value;
}

/* Returns, cut to an int, target_point.y of mesh mesh_index's descriptor when its
 * targetId is nonzero, else center.y; 0 without a descriptor, for a negative
 * mesh_index, or when asset_flags lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AEC90
int model_mesh_get_component_focus_y(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct mesh_descriptor *descriptor;
	int value;

	if (mesh_index < 0) {
		return 0;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	if (mesh_index >= model->root_node_count) {
		mesh_index = model->root_node_count - 1;
	}

	descriptor = model_mesh_find_descriptor_node_recursive(
		root_nodes[mesh_index], model);
	if (descriptor != NULL) {
		if (descriptor->target_id != 0) {
			value = (int)descriptor->target_point.y;
		} else {
			value = (int)descriptor->center.y;
		}
	} else {
		value = 0;
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return value;
}

/* Returns, cut to an int, target_point.z of mesh mesh_index's descriptor when its
 * targetId is nonzero, else center.z; 0 without a descriptor, for a negative
 * mesh_index, or when asset_flags lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AED40
int model_mesh_get_component_focus_z(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct mesh_descriptor *descriptor;
	int value;

	if (mesh_index < 0) {
		return 0;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	if (mesh_index >= model->root_node_count) {
		mesh_index = model->root_node_count - 1;
	}

	descriptor = model_mesh_find_descriptor_node_recursive(
		root_nodes[mesh_index], model);
	if (descriptor != NULL) {
		if (descriptor->target_id != 0) {
			value = (int)descriptor->target_point.z;
		} else {
			value = (int)descriptor->center.z;
		}
	} else {
		value = 0;
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return value;
}

/* Returns the largest of the three span components of mesh mesh_index's
 * descriptor, each cut to an int; 0 without a descriptor, for a negative
 * mesh_index, or when asset_flags lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AEDF0
int model_mesh_get_component_max_extent(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct mesh_descriptor *descriptor;
	int extent_x;
	int extent_y;
	int extent_z;

	if (mesh_index < 0) {
		return 0;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}
	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}
	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	if (mesh_index >= model->root_node_count) {
		mesh_index = model->root_node_count - 1;
	}
	descriptor = model_mesh_find_descriptor_node_recursive(
		root_nodes[mesh_index], model);
	if (descriptor != NULL) {
		extent_x = (int)descriptor->span.x;
		extent_y = (int)descriptor->span.y;
		extent_z = (int)descriptor->span.z;
		if (extent_y >= extent_x && extent_z <= extent_y) {
			extent_x = extent_y;
		} else if (extent_z >= extent_x && extent_z >= extent_y) {
			extent_x = extent_z;
		}
	} else {
		extent_x = 0;
	}
	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return extent_x;
}

/* Returns the 0x2 bit of mesh mesh_index's component_flags, so 2 or 0; 0 without
 * a descriptor, for a negative mesh_index, or when asset_flags lacks the 0x1 bit.
 * Spawning gives a mesh with the bit set its component hit points, and the
 * damage code damages the component of a hit mesh with it. */
// FUNCTION: XVT 0x4AEEC0
int model_mesh_is_object_type_mesh_damageable(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct mesh_descriptor *descriptor;

	if (mesh_index < 0) {
		return 0;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	if (model->root_node_count <= mesh_index) {
		mesh_index = model->root_node_count - 1;
	}

	descriptor = model_mesh_find_descriptor_node_recursive(
		root_nodes[mesh_index], model);
	/* From here mesh_index holds the result, no longer a root node index: bit 1 (value 2) of the
	 * descriptor's component flags, or 0 without a descriptor. */
	if (descriptor != NULL) {
		mesh_index = descriptor->component_flags & 2;
	} else {
		mesh_index = 0;
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return mesh_index;
}

/* Returns the 0x1 bit of mesh mesh_index's component_flags; 0 without a
 * descriptor, for a negative mesh_index, or when asset_flags lacks the 0x1 bit.
 * Its one caller, collide_damagecraft, damages the hit mesh's component with
 * craft_damage_component when the bit is set and either the difficulty is 0 or
 * both shield energies are 0. */
// FUNCTION: XVT 0x4AEF60
int model_mesh_has_explosion_type_bit0(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct mesh_descriptor *descriptor;

	if (mesh_index < 0) {
		return 0;
	}
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	if (model->root_node_count <= mesh_index) {
		mesh_index = model->root_node_count - 1;
	}

	descriptor = model_mesh_find_descriptor_node_recursive(
		root_nodes[mesh_index], model);
	/* From here mesh_index holds the result, no longer a root node index: bit 0 of the descriptor's
	 * component flags, or 0 without a descriptor. */
	if (descriptor != NULL) {
		mesh_index = descriptor->component_flags & 1;
	} else {
		mesh_index = 0;
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return mesh_index;
}

/* Returns the payload of mesh mesh_index's first OPT_ROTSCALE node, 12 floats:
 * the pivot point, then three axes; NULL without one or when asset_flags lacks
 * the 0x1 bit. Does not check for a missing model or a negative mesh_index. */
// FUNCTION: XVT 0x4AF000
float *model_mesh_get_rot_scale_data(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct opt_node *rot_scale_node;
	float *result;

	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	if (mesh_index >= model->root_node_count) {
		mesh_index = model->root_node_count - 1;
	}

	rot_scale_node = root_nodes[mesh_index];
	rot_scale_node = model_mesh_find_first_rot_scale_node(rot_scale_node);
	if (rot_scale_node != 0) {
		result = (float *)rot_scale_node->payload;
	} else {
		result = 0;
	}
	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return result;
}

/* Walks node and the nodes below it depth first, counting OPT_HARDPOINT nodes
 * in g_opt_hardpoint_search_index, and returns the hardpoint met while the count
 * equals hardpoint_index, or NULL. Follows OPT_NODEREF links; while
 * g_cache_resolved_opt_node_refs is set it keeps each target in the link node,
 * through xvt_opt_resolve_cached in the modern build, and in the original build
 * in its pName, blanking the first character of its name. A link that does not
 * resolve ends that branch. */
// FUNCTION: XVT 0x4AF090
struct opt_node *model_mesh_find_nth_hardpoint_node_recursive(
	struct opt_node *node, struct optimized_poly_object *model,
	int hardpoint_index)
{
	struct opt_node *resolved_node;
	struct opt_node *result;
	int child_index;
	int visited_child_count;
#ifndef XVT_MODERN
	char **reference_name;
#endif

	resolved_node = node;
	if (resolved_node == NULL) {
		return NULL;
	}
	while (resolved_node->node_type == OPT_NODEREF) {
		if (g_cache_resolved_opt_node_refs != 0) {
#ifdef XVT_MODERN
			resolved_node =
				xvt_opt_resolve_cached(model, resolved_node);
#else

			reference_name = (char **)&resolved_node->payload;
			if (**reference_name == '\0') {
				resolved_node = (struct opt_node *)
							resolved_node->p_name;
			} else {
				resolved_node->p_name =
					(char *)opt_model_resolve_node_ref(
						model, *reference_name);
				**reference_name = '\0';
				resolved_node = (struct opt_node *)
							resolved_node->p_name;
			}
#endif
		} else {
			resolved_node = opt_model_resolve_node_ref(
				model, (const char *)resolved_node->payload);
		}
		if (resolved_node == NULL) {
			return NULL;
		}
	}
	if (resolved_node->node_type == OPT_HARDPOINT) {
		if (hardpoint_index == g_opt_hardpoint_search_index) {
			return resolved_node;
		}
		++g_opt_hardpoint_search_index;
	}
	child_index = 0;
	visited_child_count = 0;
	while (resolved_node->child_count > visited_child_count) {
		result = model_mesh_find_nth_hardpoint_node_recursive(
			resolved_node->p_children[child_index], model,
			hardpoint_index);
		if (result != NULL) {
			return result;
		}
		++child_index;
		++visited_child_count;
	}
	return NULL;
}

/* Sets g_opt_hardpoint_search_index to 0 and returns hardpoint number
 * hardpoint_index, from 0, at or below node
 * (model_mesh_find_nth_hardpoint_node_recursive), or NULL. */
// FUNCTION: XVT 0x4AF150
struct opt_node *
model_mesh_find_nth_hardpoint_node(struct opt_node *node,
				   struct optimized_poly_object *model,
				   int hardpoint_index)
{
	g_opt_hardpoint_search_index = 0;
	return model_mesh_find_nth_hardpoint_node_recursive(node, model,
							    hardpoint_index);
}

/* Returns the number of OPT_HARDPOINT nodes at or below node, following
 * OPT_NODEREF links as model_mesh_find_nth_hardpoint_node_recursive does; a node
 * reached through two links counts twice. */
// FUNCTION: XVT 0x4AF180
int model_mesh_count_hardpoint_nodes_recursive(
	struct opt_node *node, struct optimized_poly_object *model)
{
	struct opt_node *resolved_node;
	int count;
	int child_index;
	int visited_child_count;
#ifndef XVT_MODERN
	char **reference_name;
#endif

	count = 0;
	resolved_node = node;
	if (resolved_node == NULL) {
		return 0;
	}
	while (resolved_node->node_type == OPT_NODEREF) {
		if (g_cache_resolved_opt_node_refs != 0) {
#ifdef XVT_MODERN
			resolved_node =
				xvt_opt_resolve_cached(model, resolved_node);
#else

			reference_name = (char **)&resolved_node->payload;
			if (**reference_name == '\0') {
				resolved_node = (struct opt_node *)
							resolved_node->p_name;
			} else {
				resolved_node->p_name =
					(char *)opt_model_resolve_node_ref(
						model, *reference_name);
				**reference_name = '\0';
				resolved_node = (struct opt_node *)
							resolved_node->p_name;
			}
#endif
		} else {
			resolved_node = opt_model_resolve_node_ref(
				model, (const char *)resolved_node->payload);
		}
		if (resolved_node == NULL) {
			return 0;
		}
	}
	if (resolved_node->node_type == OPT_HARDPOINT) {
		count = 1;
	}
	visited_child_count = 0;
	if (resolved_node->child_count > 0) {
		child_index = 0;
		do {
			count += model_mesh_count_hardpoint_nodes_recursive(
				resolved_node->p_children[child_index], model);
			++child_index;
			++visited_child_count;
		} while (resolved_node->child_count > visited_child_count);
	}
	return count;
}

/* Returns the number of hardpoints in mesh mesh_index; 0 when asset_flags lacks
 * the 0x1 bit. Does not check for a missing model or a negative mesh_index. */
// FUNCTION: XVT 0x4AF250
int model_mesh_count_hardpoints(int object_type, int mesh_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	int hardpoint_count;

	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}
	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}
	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	if (mesh_index >= model->root_node_count) {
		mesh_index = model->root_node_count - 1;
	}
	hardpoint_count = model_mesh_count_hardpoint_nodes_recursive(
		root_nodes[mesh_index], model);
	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return hardpoint_count;
}

/* Returns hardpoint_index as given and ignores objectType and mesh_index.
 * fe_disk_io_build_model_def stores the result as a weapon slot's
 * alternate_mesh_hardpoint_idx. */
// FUNCTION: XVT 0x4AF2D0
int model_mesh_get_hardpoint_index(int object_type, int mesh_index,
				   int hardpoint_index)
{
	(void)object_type;
	(void)mesh_index;

	return hardpoint_index;
}

/* Returns, cut to an int, the x of hardpoint hardpoint_index of mesh mesh_index
 * (model_mesh_find_nth_hardpoint_node); 0 without that hardpoint or when asset_flags
 * lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AF2E0
int model_mesh_get_hardpoint_x(int object_type, int mesh_index,
			       int hardpoint_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct opt_node *root_node;
	struct opt_node *hardpoint_node;
	int result;

	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}
	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}
	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	if (mesh_index >= model->root_node_count) {
		mesh_index = model->root_node_count - 1;
	}
	root_node = root_nodes[mesh_index];
	hardpoint_node = model_mesh_find_nth_hardpoint_node(root_node, model,
							    hardpoint_index);
	if (hardpoint_node != NULL) {
		result = (int)((struct opt_hardpoint *)hardpoint_node->payload)
				 ->position.x;
	} else {
		result = 0;
	}
	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return result;
}

/* Returns, cut to an int and negated, the y of hardpoint hardpoint_index of mesh
 * mesh_index (model_mesh_find_nth_hardpoint_node); 0 without that hardpoint or when
 * asset_flags lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AF380
int model_mesh_get_hardpoint_y(int object_type, int mesh_index,
			       int hardpoint_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct opt_node *root_node;
	struct opt_node *hardpoint_node;
	int result;

	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}
	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}
	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	if (mesh_index >= model->root_node_count) {
		mesh_index = model->root_node_count - 1;
	}
	root_node = root_nodes[mesh_index];
	hardpoint_node = model_mesh_find_nth_hardpoint_node(root_node, model,
							    hardpoint_index);
	if (hardpoint_node != NULL) {
		result = (int)((struct opt_hardpoint *)hardpoint_node->payload)
				 ->position.y;
	} else {
		result = 0;
	}
	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return -result;
}

/* Returns, cut to an int, the z of hardpoint hardpoint_index of mesh mesh_index
 * (model_mesh_find_nth_hardpoint_node); 0 without that hardpoint or when asset_flags
 * lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AF420
int model_mesh_get_hardpoint_z(int object_type, int mesh_index,
			       int hardpoint_index)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct opt_node *root_node;
	struct opt_node *hardpoint_node;
	int root_node_count;
	int result;

	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}
	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}
	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	root_node_count = model->root_node_count;
	if (root_node_count <= mesh_index) {
		mesh_index = root_node_count - 1;
	}
	root_node = root_nodes[mesh_index];
	hardpoint_node = model_mesh_find_nth_hardpoint_node(root_node, model,
							    hardpoint_index);
	if (hardpoint_node != NULL) {
		result = (int)((struct opt_hardpoint *)hardpoint_node->payload)
				 ->position.z;
	} else {
		result = 0;
	}
	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return result;
}

/* Stores the type of hardpoint hardpoint_index of mesh mesh_index in *out_type and
 * its position, cut to ints, in *outX, *outY and *outZ, with the y negated; all
 * four 0 when there is no such hardpoint. Writes nothing when asset_flags lacks
 * the 0x1 bit. */
// FUNCTION: XVT 0x4AF4C0
void model_mesh_get_hardpoint(int object_type, int mesh_index,
			      int hardpoint_index, int *out_type, int *out_x,
			      int *out_y, int *out_z)
{
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct opt_node *hardpoint_node;
	const struct opt_hardpoint *hardpoint;
	const struct opt_vector *position;

	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	root_nodes = model->root_nodes;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		++mesh_index;
	}
	mesh_index = mesh_index < model->root_node_count
			     ? mesh_index
			     : model->root_node_count - 1;

	hardpoint_node = model_mesh_find_nth_hardpoint_node(
		root_nodes[mesh_index], model, hardpoint_index);
	if (hardpoint_node == NULL) {
		*out_type = 0;
		*out_x = 0;
		*out_y = 0;
		*out_z = 0;
	} else {
		hardpoint =
			(const struct opt_hardpoint *)hardpoint_node->payload;
		position = &hardpoint->position;
		*out_type = hardpoint->hardpoint_type;
		*out_x = (int)position->x;
		*out_y = -(int)position->y;
		*out_z = (int)position->z;
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
}

/* Returns 1 when a root of the object type's model, other than an OPT_TEXTURE,
 * has a descriptor of type MESH_COMPONENT_03_FUSELAGE; else 0, also when
 * asset_flags lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AF5B0
int model_mesh_has_fuselage(int object_type)
{
	struct optimized_poly_object *model;
	int root_index;

	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	for (root_index = 0; root_index < model->root_node_count;
	     ++root_index) {
		struct opt_node *root_node;
		struct mesh_descriptor *descriptor;

		root_node = model->root_nodes[root_index];
		if (root_node != NULL && root_node->node_type != OPT_TEXTURE) {
			descriptor = model_mesh_find_descriptor_node_recursive(
				root_node, model);
			if (descriptor != NULL &&
			    descriptor->mesh_type ==
				    MESH_COMPONENT_03_FUSELAGE) {
				memory_handle_block_done_stub(
					g_loaded_models[object_type]);
				return 1;
			}
		}
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return 0;
}

/* Returns the mesh index of the MESH_COMPONENT_01_MAIN_HULL mesh whose
 * descriptor box lies nearest the point, the distance being the largest of the
 * three per-axis gaps; stops at the first box that holds the point. Returns 0
 * when asset_flags lacks the 0x1 bit, and an uninitialized value when the model
 * has no main hull mesh. */
// FUNCTION: XVT 0x4AF660
int model_mesh_find_nearest_main_hull_by_bounds(int object_type, int local_x,
						int local_y, int local_z)
{
	float point_x;
	float point_y;
	float point_z;
	float nearest_distance;
	float bounds_distance;
	float axis_distance;
	int nearest_mesh_index;
	int root_node_index;
	struct optimized_poly_object *model;
	struct opt_node *root_node;
	struct mesh_descriptor *descriptor;

	point_x = (float)local_x;
	point_y = (float)local_y;
	nearest_distance = 2147483648.0f;
	point_z = (float)local_z;
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}

	for (root_node_index = 0; root_node_index < model->root_node_count;
	     ++root_node_index) {
		root_node = model->root_nodes[root_node_index];
		if (root_node->node_type == OPT_TEXTURE) {
			continue;
		}

		descriptor = model_mesh_find_descriptor_node_recursive(
			root_node, model);
		if (descriptor == NULL ||
		    descriptor->mesh_type != MESH_COMPONENT_01_MAIN_HULL) {
			continue;
		}

		if (point_x > descriptor->box_max.x) {
			axis_distance = point_x - descriptor->box_max.x;
		} else if (point_x < descriptor->box_min.x) {
			axis_distance = descriptor->box_min.x - point_x;
		} else {
			axis_distance = 0.0f;
		}
		bounds_distance = axis_distance;

		if (point_y > descriptor->box_max.y) {
			axis_distance = point_y - descriptor->box_max.y;
		} else if (point_y < descriptor->box_min.y) {
			axis_distance = descriptor->box_min.y - point_y;
		} else {
			axis_distance = 0.0f;
		}
		if (bounds_distance < axis_distance) {
			bounds_distance = axis_distance;
		}

		if (point_z > descriptor->box_max.z) {
			axis_distance = point_z - descriptor->box_max.z;
		} else if (point_z < descriptor->box_min.z) {
			axis_distance = descriptor->box_min.z - point_z;
		} else {
			axis_distance = 0.0f;
		}
		if (bounds_distance < axis_distance) {
			bounds_distance = axis_distance;
		}

		if (bounds_distance < nearest_distance) {
			nearest_mesh_index = root_node_index;
			nearest_distance = bounds_distance;
			if (bounds_distance == 0.0f) {
				break;
			}
		}
	}

	if (model->root_nodes[0]->node_type == OPT_TEXTURE) {
		--nearest_mesh_index;
	}
	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return nearest_mesh_index;
}

/* Returns the index of a vertex of mesh mesh_index's first OPT_MESHVERTS node,
 * for nearest_rank 0 the one nearest the point. It leaves out the last two
 * vertices when there are more than two, and looks at the first 256 at most.
 * For a nearest_rank above 0 each pass keeps the best distance found by the
 * passes before, so it finds no new vertex and the swaps move other entries:
 * rank 1 always gives vertex 0. Returns 0 when asset_flags lacks the 0x1 bit. */
// FUNCTION: XVT 0x4AF850
int model_mesh_find_nearest_vertex_for_point(int object_type, int local_x,
					     int local_y, int local_z,
					     int mesh_index, int nearest_rank)
{
	float nearest_distance_sq;
	float point_x;
	float point_y;
	float point_z;
	float vertex_distance_sq[256];
	int vertex_indices[256];
	struct optimized_poly_object *model;
	struct opt_node **root_nodes;
	struct opt_node *vertices_node;
	struct opt_vector *vertices;
	int root_node_index;
	int vertex_count;
	int vertex_index;
	int selected_count;
	int candidate_index;
	int nearest_index;
	float delta_x;
	float delta_y;
	float delta_z;
	float swap_distance;
	int swap_index;

	point_x = (float)local_x;
	point_y = (float)local_y;
	point_z = (float)local_z;
	if ((g_object_type_table[object_type].asset_flags & 1) == 0) {
		return 0;
	}

	model = (struct optimized_poly_object *)memory_get_handle_block(
		g_loaded_models[object_type]);
	if (model->self_marker != model) {
		opt_model_adjust_optimized_poly_object_pointers(model);
	}
	root_nodes = model->root_nodes;
	root_node_index = mesh_index;
	if (root_nodes[0]->node_type == OPT_TEXTURE) {
		root_node_index++;
	}
	if (root_node_index >= model->root_node_count) {
		root_node_index = model->root_node_count - 1;
	}
	vertices_node = model_mesh_find_first_mesh_verts_node(
		root_nodes[root_node_index]);
	vertices = (struct opt_vector *)vertices_node->payload;
	vertex_count = vertices_node->payload_count;
	if (vertex_count > 2) {
		vertex_count -= 2;
	}
	if (vertex_count > 256) {
		vertex_count = 256;
	}
	if (nearest_rank >= vertex_count) {
		nearest_rank = vertex_count - 1;
	}

	for (vertex_index = 0; vertex_index < vertex_count; vertex_index++) {
		delta_x = vertices[vertex_index].x - point_x;
		delta_y = vertices[vertex_index].y - point_y;
		delta_z = vertices[vertex_index].z - point_z;
		vertex_distance_sq[vertex_index] = delta_x * delta_x +
						   delta_y * delta_y +
						   delta_z * delta_z;
		vertex_indices[vertex_index] = vertex_index;
	}

	selected_count = 0;
	nearest_distance_sq = 4611686018427387904.0f;
	if (nearest_rank + 1 > 0) {
		nearest_index = vertex_indices[0];
		do {
			for (candidate_index = selected_count;
			     candidate_index < vertex_count;
			     candidate_index++) {
				if (vertex_distance_sq[candidate_index] <
				    nearest_distance_sq) {
					nearest_index = candidate_index;
					nearest_distance_sq = vertex_distance_sq
						[candidate_index];
				}
			}
			swap_distance = vertex_distance_sq[selected_count];
			vertex_distance_sq[nearest_index] = swap_distance;
			swap_index = vertex_indices[nearest_index];
			vertex_distance_sq[selected_count] =
				nearest_distance_sq;
			vertex_indices[nearest_index] =
				vertex_indices[selected_count];
			vertex_indices[selected_count] = swap_index;
			selected_count++;
		} while (nearest_rank + 1 > selected_count);
	}

	memory_handle_block_done_stub(g_loaded_models[object_type]);
	return vertex_indices[nearest_rank];
}

/* Returns the mesh index, counting the roots that are not an OPT_TEXTURE, of
 * the first mesh whose descriptor type is MESH_COMPONENT_07_BRIDGE, or -1 when
 * there is none. */
// FUNCTION: XVT 0x4AFA30
int model_mesh_find_bridge_index(struct optimized_poly_object *model)
{
	int root_index;
	int mesh_index;

	mesh_index = 0;
	for (root_index = 0; root_index < model->root_node_count;
	     ++root_index) {
		struct opt_node *root_node;
		struct mesh_descriptor *descriptor;

		root_node = model->root_nodes[root_index];
		if (root_node->node_type == OPT_TEXTURE) {
			continue;
		}
		descriptor = model_mesh_find_descriptor_node_recursive(
			root_node, model);
		if (descriptor != NULL &&
		    descriptor->mesh_type == MESH_COMPONENT_07_BRIDGE) {
			break;
		}
		++mesh_index;
	}

	if (root_index < model->root_node_count) {
		return mesh_index;
	}
	return -1;
}

/* Fills g_object_type_mesh_cache for all 73 object types. The pointer returned is one past the end of
 * the array, not a cache entry; it must not be dereferenced. */
/* Each entry gets model_mesh_get_object_type_mesh_count and, per mesh, its type and
 * descriptor. fe_disk_io_init_resources calls this after
 * fe_disk_io_load_resources. */
// FUNCTION: XVT 0x4AFA90
struct model_mesh_object_type_cache *
model_mesh_build_object_type_mesh_cache(void)
{
	struct model_mesh_object_type_cache *cache;
	int object_type;

	object_type = 0;
	do {
		int mesh_index;
		int mesh_count;

		cache = &g_object_type_mesh_cache[object_type];
		mesh_index = 0;
		mesh_count = model_mesh_get_object_type_mesh_count(object_type);
		cache->mesh_count = mesh_count;
		while (mesh_index < mesh_count) {
			cache->mesh_types[mesh_index] =
				model_mesh_get_object_type_mesh_type(
					object_type, mesh_index);
			cache->mesh_descriptors[mesh_index] =
				model_mesh_get_descriptor(object_type,
							  mesh_index);
			++mesh_index;
		}

		++object_type;
	} while (object_type < 73);

	return &g_object_type_mesh_cache[73];
}
