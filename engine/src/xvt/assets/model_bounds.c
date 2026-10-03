#include "xvt/assets/model_bounds.h"
#include "xvt/assets/model_mesh.h"
#include "xvt/assets/object_type.h"
#include "xvt/util/memory.h"

/* Per object type, the smallest corner of its model's box, which
 * model_bounds_ensure_cached fills; only that function writes it. */
// GLOBAL: XVT 0x662CE8
struct opt_vector g_model_bounds_min[201] = {{0}};
/* Per object type, 1 once model_bounds_ensure_cached has filled its bounds, so
 * the getters stop calling it. Only that function writes it and nothing sets it
 * back to 0, so a model loaded later for the same type keeps the first bounds.
 * The table has one entry more than there are types. */
// GLOBAL: XVT 0x663658
int g_model_bounds_cached[202] = {0};
/* Per object type, the largest corner of its model's box, which
 * model_bounds_ensure_cached fills; only that function writes it. */
// GLOBAL: XVT 0x663980
struct opt_vector g_model_bounds_max[201] = {{0}};

/* Fills g_model_bounds_min and g_model_bounds_max for the object type and sets its
 * g_model_bounds_cached entry to 1. From the first OPT_MESHVERTS node of each
 * root other than an OPT_TEXTURE, when it has at least two vertices, it takes
 * the last two, which the converter makes the box's smallest and largest
 * corners: the smallest components of the second-to-last vertices and the
 * largest of the last ones are kept. A model with none gives 1073741800.0 for
 * every smallest and -1073741800.0 for every largest component. Does nothing
 * when the type's asset_flags lacks the 0x1 bit, and does not check that
 * g_loaded_models holds a handle. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4ADD60
void model_bounds_ensure_cached(int object_type)
{
	struct optimized_poly_object *model;
	int root_node_index;
	struct opt_node *root_node;
	struct opt_node *vertex_node;
	float *vertex_data;
	float *bounds;
	int vertex_count;
	struct opt_vector min_bounds;
	struct opt_vector max_bounds;

	min_bounds.x = min_bounds.y = min_bounds.z = 1073741800.0f;
	max_bounds.x = max_bounds.y = max_bounds.z = -1073741800.0f;
	if ((g_object_type_table[object_type].asset_flags & 1) != 0) {
		model = (struct optimized_poly_object *)memory_get_handle_block(
			g_loaded_models[object_type]);
		if (model->self_marker != model) {
			opt_model_adjust_optimized_poly_object_pointers(model);
		}

		for (root_node_index = 0;
		     root_node_index < model->root_node_count;
		     ++root_node_index) {
			root_node = model->root_nodes[root_node_index];
			if (root_node != 0 &&
			    root_node->node_type != OPT_TEXTURE) {
				vertex_node =
					model_mesh_find_first_mesh_verts_node(
						root_node);
				if (vertex_node != 0) {
					vertex_data =
						(float *)vertex_node->payload;
					vertex_count =
						vertex_node->payload_count;
					if (vertex_count >= 2) {
						bounds = vertex_data +
							 3 * vertex_count - 6;
						if (bounds[0] < min_bounds.x) {
							min_bounds.x =
								bounds[0];
						}
						if (bounds[1] < min_bounds.y) {
							min_bounds.y =
								bounds[1];
						}
						if (bounds[2] < min_bounds.z) {
							min_bounds.z =
								bounds[2];
						}
						bounds += 3;
						if (bounds[0] > max_bounds.x) {
							max_bounds.x =
								bounds[0];
						}
						if (bounds[1] > max_bounds.y) {
							max_bounds.y =
								bounds[1];
						}
						if (bounds[2] > max_bounds.z) {
							max_bounds.z =
								bounds[2];
						}
					}
				}
			}
		}

		g_model_bounds_min[object_type].x = min_bounds.x;
		g_model_bounds_min[object_type].y = min_bounds.y;
		g_model_bounds_min[object_type].z = min_bounds.z;
		g_model_bounds_max[object_type].x = max_bounds.x;
		g_model_bounds_max[object_type].y = max_bounds.y;
		g_model_bounds_cached[object_type] = 1;
		g_model_bounds_max[object_type].z = max_bounds.z;
		memory_handle_block_done_stub(g_loaded_models[object_type]);
	}
}

/* Returns the largest of the object type's three box sizes, cut to an int,
 * filling the cache first when needed (model_bounds_ensure_cached). */
// FUNCTION: XVT 0x4ADF10
int model_bounds_get_max_extent(int object_type)
{
	float size[3];

	if (g_model_bounds_cached[object_type] == 0) {
		model_bounds_ensure_cached(object_type);
	}

	size[0] = g_model_bounds_max[object_type].x -
		  g_model_bounds_min[object_type].x;
	size[1] = g_model_bounds_max[object_type].y -
		  g_model_bounds_min[object_type].y;
	size[2] = g_model_bounds_max[object_type].z -
		  g_model_bounds_min[object_type].z;

	if (size[1] >= size[0] && size[1] >= size[2]) {
		size[0] = size[1];
	} else if (size[2] >= size[0] && size[1] <= size[2]) {
		size[0] = size[2];
	}
	return (int)size[0];
}

/* Returns the y of the object type's smallest box corner, cut to an int,
 * filling the cache first when needed. */
// FUNCTION: XVT 0x4ADFF0
int model_bounds_get_min_y(int object_type)
{
	if (g_model_bounds_cached[object_type] == 0) {
		model_bounds_ensure_cached(object_type);
	}
	return (int)g_model_bounds_min[object_type].y;
}

/* Returns the z of the object type's smallest box corner, cut to an int,
 * filling the cache first when needed. */
// FUNCTION: XVT 0x4AE020
int model_bounds_get_min_z(int object_type)
{
	if (g_model_bounds_cached[object_type] == 0) {
		model_bounds_ensure_cached(object_type);
	}
	return (int)g_model_bounds_min[object_type].z;
}

/* Returns the y of the object type's largest box corner, cut to an int, filling
 * the cache first when needed. */
// FUNCTION: XVT 0x4AE080
int model_bounds_get_max_y(int object_type)
{
	if (g_model_bounds_cached[object_type] == 0) {
		model_bounds_ensure_cached(object_type);
	}
	return (int)g_model_bounds_max[object_type].y;
}

/* Returns the z of the object type's largest box corner, cut to an int, filling
 * the cache first when needed. */
// FUNCTION: XVT 0x4AE0B0
int model_bounds_get_max_z(int object_type)
{
	if (g_model_bounds_cached[object_type] == 0) {
		model_bounds_ensure_cached(object_type);
	}
	return (int)g_model_bounds_max[object_type].z;
}

/* Returns the object type's box size along x, largest minus smallest corner,
 * cut to an int, filling the cache first when needed. */
// FUNCTION: XVT 0x4AE0E0
int model_bounds_get_size_x(int object_type)
{
	if (g_model_bounds_cached[object_type] == 0) {
		model_bounds_ensure_cached(object_type);
	}
	return (int)(g_model_bounds_max[object_type].x -
		     g_model_bounds_min[object_type].x);
}

/* Returns the object type's box size along y, largest minus smallest corner,
 * cut to an int, filling the cache first when needed. */
// FUNCTION: XVT 0x4AE120
int model_bounds_get_size_y(int object_type)
{
	if (g_model_bounds_cached[object_type] == 0) {
		model_bounds_ensure_cached(object_type);
	}

	return (int)(g_model_bounds_max[object_type].y -
		     g_model_bounds_min[object_type].y);
}

/* Returns the object type's box size along z, largest minus smallest corner,
 * cut to an int, filling the cache first when needed. */
// FUNCTION: XVT 0x4AE160
int model_bounds_get_size_z(int object_type)
{
	if (g_model_bounds_cached[object_type] == 0) {
		model_bounds_ensure_cached(object_type);
	}

	return (int)(g_model_bounds_max[object_type].z -
		     g_model_bounds_min[object_type].z);
}
