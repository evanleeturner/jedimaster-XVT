#ifndef XVT_ASSETS_MODEL_MESH_H
#define XVT_ASSETS_MODEL_MESH_H

#include "xvt/assets/opt_model.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stored as int32_t in the binary (IDB enum mesh_component_type). */
typedef int32_t mesh_component_type;

enum {
	MESH_COMPONENT_00_DEFAULT = 0x0,    ///< strings.txt line 1734: Hull
	MESH_COMPONENT_01_MAIN_HULL = 0x1,  ///< strings.txt line 1735: Hull
	MESH_COMPONENT_02_WING = 0x2,	    ///< strings.txt line 1736: Wing
	MESH_COMPONENT_03_FUSELAGE = 0x3,   ///< strings.txt line 1737: Fuselage
	MESH_COMPONENT_04_LASR_TUR = 0x4,   ///< strings.txt line 1738: Lasr Tur
	MESH_COMPONENT_05_LASR_GUN = 0x5,   ///< strings.txt line 1739: Lasr Gun
	MESH_COMPONENT_06_ENGINE = 0x6,	    ///< strings.txt line 1740: Engine
	MESH_COMPONENT_07_BRIDGE = 0x7,	    ///< strings.txt line 1741: Bridge
	MESH_COMPONENT_08_SHLD_GEN = 0x8,   ///< strings.txt line 1742: Shld Gen
	MESH_COMPONENT_09_ENRG_GEN = 0x9,   ///< strings.txt line 1743: Enrg Gen
	MESH_COMPONENT_10_WHEAD_LN = 0xA,   ///< strings.txt line 1744: Whead Ln
	MESH_COMPONENT_11_COMM_SYS = 0xB,   ///< strings.txt line 1745: Comm Sys
	MESH_COMPONENT_12_BEAM_SYS = 0xC,   ///< strings.txt line 1746: Beam Sys
	MESH_COMPONENT_13_COMM_SYS = 0xD,   ///< strings.txt line 1747: Comm Sys
	MESH_COMPONENT_14_DOCK_PLT = 0xE,   ///< strings.txt line 1748: Dock Plt
	MESH_COMPONENT_15_LAND_PLT = 0xF,   ///< strings.txt line 1749: Land Plt
	MESH_COMPONENT_16_HANGAR = 0x10,    ///< strings.txt line 1750: Hangar
	MESH_COMPONENT_17_CARGO = 0x11,	    ///< strings.txt line 1751: Cargo
	MESH_COMPONENT_18_MISC_HULL = 0x12, ///< strings.txt line 1752: Hull
	MESH_COMPONENT_19_ANTENNA = 0x13,   ///< strings.txt line 1753: Antenna
	MESH_COMPONENT_20_ROTATING_WING = 0x14, ///< strings.txt line 1754: Wing
	MESH_COMPONENT_21_ROTATING_LASR_TUR =
		0x15,			   ///< strings.txt line 1755: Lasr Tur
	MESH_COMPONENT_22_WHEAD_LN = 0x16, ///< strings.txt line 1756: Whead Ln
	MESH_COMPONENT_23_COMM_SYS = 0x17, ///< strings.txt line 1757: Comm Sys
	MESH_COMPONENT_24_BEAM_SYS = 0x18, ///< strings.txt line 1758: Beam Sys
	MESH_COMPONENT_25_COMM_SYS = 0x19, ///< strings.txt line 1759: Comm Sys
	MESH_COMPONENT_26_COCKPIT = 0x1A,  ///< strings.txt line 1760: Cockpit
	MESH_COMPONENT_27_HULL = 0x1B,	   ///< strings.txt line 1761: Hull
	MESH_COMPONENT_28_HULL = 0x1C,	   ///< strings.txt line 1762: Hull
	MESH_COMPONENT_29_HULL = 0x1D,	   ///< strings.txt line 1763: Hull
	MESH_COMPONENT_30_HULL = 0x1E,	   ///< strings.txt line 1764: Hull
	MESH_COMPONENT_31_HULL = 0x1F,	   ///< strings.txt line 1765: Hull
	MESH_COMPONENT_32_DASHES = 0x20,   ///< strings.txt line 1766: --------
};

struct mesh_descriptor {
	mesh_component_type mesh_type; /* The mesh's component type. */
	/* Flags: model_mesh_has_explosion_type_bit0 reads the 0x1 bit and
	 * model_mesh_is_object_type_mesh_damageable the 0x2 bit. */
	int component_flags;
	/* The mesh's size along each axis; model_mesh_get_component_max_extent
	 * takes the largest. */
	struct opt_vector span;
	struct opt_vector center;  /* Center of the mesh. */
	struct opt_vector box_min; /* Smallest corner of the mesh's box. */
	struct opt_vector box_max; /* Largest corner of the mesh's box. */
	/* When nonzero, target_point replaces center as the point
	 * model_mesh_get_component_focus_x and its two siblings give. */
	int target_id;
	/* The focus point used while targetId is nonzero. */
	struct opt_vector target_point;
};

struct model_mesh_object_type_cache {
	int mesh_count;	    /* Meshes cached, up to 50. */
	int mesh_types[50]; /* Each mesh's component type. */
	/* Each mesh's descriptor inside the loaded model, or NULL. */
	struct mesh_descriptor *mesh_descriptors[50];
};

extern struct model_mesh_object_type_cache g_object_type_mesh_cache[73];
extern int g_opt_hardpoint_search_index;
extern uint8_t g_opt_hardpoint_weapon_group_kind_by_type[32];

static __inline mesh_component_type
model_mesh_get_cached_object_type_mesh_type(int object_type, int mesh_index)
{
	if (mesh_index < 0) {
		return MESH_COMPONENT_00_DEFAULT;
	}
	if (g_object_type_mesh_cache[object_type].mesh_count <= mesh_index) {
		mesh_index =
			g_object_type_mesh_cache[object_type].mesh_count - 1;
	}
	return g_object_type_mesh_cache[object_type].mesh_types[mesh_index];
}

void model_mesh_apply_animated_mesh_rotation_to_point(int16_t angle_q16,
						      int object_type,
						      int mesh_index,
						      int local_x, int local_y,
						      int local_z);
int model_mesh_get_object_type_mesh_count(int object_type);
struct opt_node *model_mesh_find_first_mesh_verts_node(struct opt_node *node);
struct opt_node *model_mesh_find_first_rot_scale_node(struct opt_node *node);
struct mesh_descriptor *
model_mesh_find_descriptor_node_recursive(struct opt_node *node,
					  struct optimized_poly_object *model);
struct mesh_descriptor *model_mesh_get_descriptor(int object_type,
						  int mesh_index);
mesh_component_type model_mesh_get_object_type_mesh_type(int object_type,
							 int mesh_index);
int model_mesh_get_vertex_count(int object_type, int mesh_index);
int model_mesh_get_vertex_x(int object_type, int mesh_index, int vertex_index);
int model_mesh_get_vertex_y(int object_type, int mesh_index, int vertex_index);
int model_mesh_get_vertex_z(int object_type, int mesh_index, int vertex_index);
int model_mesh_get_center_x(int object_type, int mesh_index);
int model_mesh_get_center_y(int object_type, int mesh_index);
int model_mesh_get_center_z(int object_type, int mesh_index);
int model_mesh_get_bounds_min_x(int object_type, int mesh_index);
int model_mesh_get_bounds_min_y(int object_type, int mesh_index);
int model_mesh_get_bounds_min_z(int object_type, int mesh_index);
int model_mesh_get_bounds_max_x(int object_type, int mesh_index);
int model_mesh_get_bounds_max_y(int object_type, int mesh_index);
int model_mesh_get_bounds_max_z(int object_type, int mesh_index);
int model_mesh_get_target_id(int object_type, int mesh_index);
int model_mesh_get_component_focus_x(int object_type, int mesh_index);
int model_mesh_get_component_focus_y(int object_type, int mesh_index);
int model_mesh_get_component_focus_z(int object_type, int mesh_index);
int model_mesh_get_component_max_extent(int object_type, int mesh_index);
int model_mesh_is_object_type_mesh_damageable(int object_type, int mesh_index);
int model_mesh_has_explosion_type_bit0(int object_type, int mesh_index);
float *model_mesh_get_rot_scale_data(int object_type, int mesh_index);
struct opt_node *model_mesh_find_nth_hardpoint_node_recursive(
	struct opt_node *node, struct optimized_poly_object *model,
	int hardpoint_index);
struct opt_node *
model_mesh_find_nth_hardpoint_node(struct opt_node *node,
				   struct optimized_poly_object *model,
				   int hardpoint_index);
int model_mesh_count_hardpoint_nodes_recursive(
	struct opt_node *node, struct optimized_poly_object *model);
int model_mesh_count_hardpoints(int object_type, int mesh_index);
int model_mesh_get_hardpoint_index(int object_type, int mesh_index,
				   int hardpoint_index);
int model_mesh_get_hardpoint_x(int object_type, int mesh_index,
			       int hardpoint_index);
int model_mesh_get_hardpoint_y(int object_type, int mesh_index,
			       int hardpoint_index);
int model_mesh_get_hardpoint_z(int object_type, int mesh_index,
			       int hardpoint_index);
void model_mesh_get_hardpoint(int object_type, int mesh_index,
			      int hardpoint_index, int *out_type, int *out_x,
			      int *out_y, int *out_z);
int model_mesh_has_fuselage(int object_type);
int model_mesh_find_nearest_main_hull_by_bounds(int object_type, int local_x,
						int local_y, int local_z);
int model_mesh_find_nearest_vertex_for_point(int object_type, int local_x,
					     int local_y, int local_z,
					     int mesh_index, int nearest_rank);
int model_mesh_find_bridge_index(struct optimized_poly_object *model);
struct model_mesh_object_type_cache *
model_mesh_build_object_type_mesh_cache(void);

#ifdef __cplusplus
}
#endif

#endif
