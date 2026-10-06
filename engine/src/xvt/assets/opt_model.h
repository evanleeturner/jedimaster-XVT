#ifndef XVT_ASSETS_OPT_MODEL_H
#define XVT_ASSETS_OPT_MODEL_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "xvt/assets/file.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct model_weapon_hardpoint {
	/* Side offset from the model's origin, the OPT hardpoint's x; readers
	 * pass x, z and y to pai_calcrotatedpoint as side, up and forward. */
	int16_t x;
	int16_t z; /* Up offset, the OPT hardpoint's z. */
	/* Forward offset: the OPT hardpoint's y negated, as
	 * model_mesh_get_hardpoint gives it. */
	int16_t y;
	/* For a laser slot on a turret mesh, the index in that mesh of its next
	 * hardpoint of the same type; 0xFF when there is none.
	 * laser_fireturretslot fires from it instead while
	 * g_mission_elapsed_clock.subsecond_ticks is odd, except on
	 * SUPER_STAR_DESTROYER_OBJECT_TYPE. Launcher slots keep the table's
	 * value. */
	uint8_t alternate_mesh_hardpoint_idx;
	/* Mesh that carries the point; readers skip the slot while the craft's
	 * component_hp for that mesh is 0. */
	uint8_t mesh_idx;
};

struct model_local_point {
	int side;    /* Side offset, the OPT hardpoint's x. */
	int up;	     /* Up offset, the OPT hardpoint's z. */
	int forward; /* Forward offset, the OPT hardpoint's y negated. */
};

struct model_hangar_points {
	struct model_local_point inside;  /* From the hardpoint of type 25. */
	struct model_local_point outside; /* From the hardpoint of type 26. */
};

struct model_def {
	/* Short name such as "X-W"; the HUD, radio messages and goal text show
	 * it. */
	char name[10];
	/* Full name such as "X-wing", shown by messages and goal text; NULL in
	 * the table until string_table_load_game_strings sets it. */
	char *name_long;
	/* Points the craft is worth; the scoring code multiplies it, by 40 in
	 * mission_compute_craft_point_value. */
	uint8_t craft_point_value;
	/* Weight in rating awards: mission_credit_destruction_damage_contributors
	 * multiplies a kill's rating points by the victim's weight and divides
	 * by the attacker's craft's; a victim with 0 gets the minimum award
	 * instead. */
	uint8_t rating_weight;
	/* Copied into a player's bound_craft_engine_glow_count when the
	 * player's craft spawns; the game code never reads that copy, only the
	 * state records. */
	uint8_t engine_glow_count;
	/* 0 for a model without a hyperdrive: spawning then flips
	 * CRAFT_SUBSYSTEM_FLAG_HYPERDRIVE in the craft's system_flags unless a
	 * spawn status is 9 or 16. The AI's orders check it before a hyperspace
	 * exit. */
	uint8_t has_hyperdrive;
	/* 0 for a model without shields: spawning then flips
	 * CRAFT_SUBSYSTEM_FLAG_SHIELDS and empties the shields unless a spawn
	 * status is 8 or 16, and build_craft_tech_stats rates the shields 0. */
	uint8_t has_shields;
	/* Half the most each of the craft's two shield energies holds:
	 * craft_get_object_max_shield returns 2 times it. build_craft_tech_stats
	 * rates it divided by 50. */
	int shield_strength;
	/* Hits paiman_attackmaneuver lets the craft take before it breaks off;
	 * halved with the front shield under an eighth, 1 at
	 * system_damage_hull_threshold, both only for a craft with shields. */
	uint8_t reaction_threshold;
	/* Nothing reads or writes it by name. */
	/* Static 0/1 model-class discriminator; exact runtime behavior is not
	 * yet identified. */
	uint8_t model_class_flag;
	/* Hull strength: copied into the craft's hull_max at spawn;
	 * build_craft_tech_stats rates it divided by 105. */
	int hull_strength;
	/* Hull damage from which hits reach the craft's systems; copied into
	 * the craft's system_damage_hull_threshold at spawn. */
	int system_damage_hull_threshold;
	/* Ion damage the craft's systems stand: collide_damagecraft compares
	 * the craft's subsystem_damage with it, and the HUD shows what is
	 * left. */
	uint16_t system_strength;
	/* Top speed, in an object's speed units: the base of the AI's speeds
	 * (ai_flight.max_speed_cache) and of the spawn speed; build_craft_tech_stats
	 * rates it. */
	uint16_t max_speed;
	/* Speed gained per simulated second: flight_slew_object_speed_toward_target
	 * accelerates by a quarter of it, at least 1, plus the rest times the
	 * throttle fraction over 65,536, tripled unless engine_overdrive_off is
	 * set. build_craft_tech_stats rates it. */
	uint16_t accel_rate;
	/* Speed lost per simulated second: flight_slew_object_speed_toward_target
	 * decelerates by a quarter of it, at least 1, plus the rest times
	 * 65,535 less the throttle fraction over 65,536. */
	uint16_t decel_rate;
	/* Yaw rate in angle units per SIMULATION_TICKS_PER_SECOND ticks at a
	 * full step; copied into ai_flight.turn_rate at spawn. */
	int16_t yaw_rate;
	/* Fraction of 65,536 of each AI yaw step that
	 * flight_update_craft_steering_and_speed also applies to roll while
	 * ai_flight.roll_state is 0 or 4. */
	uint16_t auto_bank_factor;
	/* Roll rate in angle units per SIMULATION_TICKS_PER_SECOND ticks at a
	 * full step; copied into ai_flight.roll_rate at spawn.
	 * build_craft_tech_stats adds it to pitch_rate for the maneuver rating. */
	int16_t roll_rate;
	/* Pitch rate in angle units per SIMULATION_TICKS_PER_SECOND ticks at a
	 * full step; copied into ai_flight.pitch_rate at spawn. */
	int16_t pitch_rate;
	/* Limit on a destroyed craft's tumble: its random roll rate, 0x2000
	 * plus game_rand() & 0x3FFF, is halved while it is over this value,
	 * then becomes its roll_impulse_rate. */
	uint16_t max_tumble_rate;
	/* Most a craft's push_accum_x moves it per SIMULATION_TICKS_PER_SECOND
	 * ticks (object_update_lifetime_and_movement), except in the board and
	 * dropoff maneuvers, which use BOARDING_PUSH_RATE and
	 * DROPOFF_PUSH_RATE. */
	uint16_t max_push_rate;
	/* Base name of the model's cockpit files, which
	 * hud_load_cockpit_resources appends to the resolution's cockpit folder
	 * for the local player's craft. */
	char cockpit_resource_name[9];
	/* Projectile type of each of the two laser groups, 0 for none.
	 * fe_disk_io_build_model_def fills an empty group with the type, minus 120
	 * as a byte, of a hardpoint whose g_opt_hardpoint_weapon_group_kind_by_type
	 * entry is 1. Spawning copies it into laser_state.projectile_type_id. */
	uint8_t laser_group_weapon_type[2];
	/* First weapon_hardpoints slot of each laser group. */
	uint8_t laser_group_first_slot[2];
	/* Last weapon_hardpoints slot of each laser group. */
	uint8_t laser_group_last_slot[2];
	/* Slots in each laser group. */
	uint8_t laser_group_slot_count[2];
	/* 2 for a gunner group, whose slots fire turret projectiles: the
	 * builder gives it to a group on a laser turret or gun mesh, or on a
	 * freighter, platform or starship. Otherwise fe_disk_io_build_model_def
	 * sets 1 for hardpoint types 5 and 16, else 0. */
	uint8_t laser_group_mount_type[2];
	/* Projectile type of each of the two warhead launchers, 0 for none; the
	 * builder fills an empty one like a laser group. build_craft_tech_stats
	 * counts a launcher that has one; spawning loads the flight group's
	 * warhead instead. */
	uint8_t warhead_launcher_type[2];
	/* First weapon_hardpoints slot of each warhead launcher. */
	uint8_t warhead_launcher_first_slot[2];
	/* Last weapon_hardpoints slot of each warhead launcher. */
	uint8_t warhead_launcher_last_slot[2];
	/* Slots in each warhead launcher. */
	uint8_t warhead_launcher_slot_count[2];
	/* Full load per launcher slot: spawning scales it by the flight group's
	 * warhead fraction (g_warhead_ammo_fraction_q16), at least 1, then doubles
	 * or halves it for spawn statuses 1 and 2. */
	uint8_t warhead_launcher_capacity[2];
	/* The weapon slots, the laser groups' first and then the launchers',
	 * which fe_disk_io_build_model_def fills from the model's hardpoints. */
	struct model_weapon_hardpoint weapon_hardpoints[16];
	/* Countermeasures a craft carries: its cm_ammo_count at spawn, 0xAAAC
	 * over 65,536 of it for flares, and again when paiman_boardmaneuver
	 * resupplies it. */
	uint8_t countermeasure_count;
	/* Forward offset of the model's hardpoint of type 31, HARDPOINT_COCKPIT
	 * among the importer's names; the player code turns it into the
	 * player's hardpointWorld position. */
	int16_t primary_hardpoint_y;
	/* Up offset of that type 31 hardpoint. */
	int16_t primary_hardpoint_z;
	/* Read by paiman_boardmaneuver and object_update_lifetime_and_movement. */
	/* Shared local-forward coordinate from OPT docking hardpoints 27-30. */
	int16_t dock_forward;
	/* fe_disk_io_build_model_def sets both from the model's largest z when the
	 * table's first is 0, before the hardpoints. */
	/* Local-up coordinates from OPT DockFromSmall (index 0) and DockFromBig
	 * (index 1); defaults to the model maximum up bound. */
	int16_t dock_from_up[2];
	/* fe_disk_io_build_model_def sets both from the model's smallest z when the
	 * table's first is 0, before the hardpoints. */
	/* Local-up coordinates from OPT DockToSmall (index 0) and DockToBig
	 * (index 1); defaults to the model minimum up bound. */
	int16_t dock_to_up[2];
	/* Read by the hangar orders, mission_spawn_flight_group_wave_craft and the
	 * collision code. */
	/* Local-space hangar path points from OPT hardpoints 25 (inside) and 26
	 * (outside). */
	struct model_hangar_points hangar_points;
	/* Times fe_disk_io_build_model_def halved the bound sizes to bring each to
	 * 0x280 or under; readers shift the sizes left by it. */
	uint16_t bound_size_shift;
	/* The model's size along x (model_bounds_get_size_x) shifted right by
	 * bound_size_shift; the targeting and HUD code use the three sizes'
	 * mean. */
	int16_t bound_size_x;
	int16_t bound_size_z; /* Size along z, shifted the same way. */
	int16_t bound_size_y; /* Size along y, shifted the same way. */
};

extern struct model_def g_model_defs[73];
extern const float g_sw3d_unit_float;
extern const float g_sw3d_triangle_corner_count;
extern const float g_sw3d_quad_corner_count;
extern const float g_sw3d_zero_float;
extern const float g_sw3d_distant_depth;
extern const float g_sw3d_span_length_reciprocal[70];

struct opt_vector {
	/* First component: x of a position or normal, red of a color. */
	float x;
	float y; /* Second component: y, or green. */
	float z; /* Third component: z, or blue. */
};

extern void *g_cur_mesh_vertices;
extern void *g_cur_mesh_tex_coords;
extern void *g_model_node_walk_unused_scratch2;
extern void *g_cur_mesh_materials;
extern int g_cur_vertex_count;
extern struct opt_vector *g_cur_vert_normals;

typedef enum opt_node_type {
	OPT_GROUP = 0x0,
	OPT_FACEDATA = 0x1,
	OPT_TRANSFORM = 0x2,
	OPT_MESHVERTS = 0x3,
	OPT_TRANSLATION = 0x4,
	OPT_ROTATION = 0x5,
	OPT_SCALE = 0x6,
	OPT_NODEREF = 0x7,
	OPT_DEF = 0x8,
	OPT_MATERIAL = 0x9,
	OPT_MATERIAL_BINDING = 0xA,
	OPT_VERTNORMALS = 0xB,
	OPT_NORMAL_BINDING = 0xC,
	OPT_TEXCOORDS = 0xD,
	OPT_TEXCOORD_BINDING = 0xE,
	OPT_FACEDATA_QUAD_MESH = 0xF,
	OPT_FACEDATA_FACE_SET = 0x10,
	OPT_FACEDATA_TRIANGLE_STRIP_SET = 0x11,
	OPT_INVENTOR_GROUP = 0x12,
	OPT_BASE_COLOR = 0x13,
	OPT_TEXTURE = 0x14,
	OPT_FACEGROUP = 0x15,
	OPT_HARDPOINT = 0x16,
	OPT_ROTSCALE = 0x17,
	OPT_NODESWITCH = 0x18,
	OPT_MESHDESC = 0x19,
} opt_node_type;

typedef intptr_t xvt_opt_value;

struct opt_node {
	/* The node's name, or NULL; OPT_NODEREF links find nodes by it. */
	char *p_name;
	opt_node_type
		node_type; /* What the node is; sets its payload's layout. */
	int child_count;   /* Entries in p_children. */
	/* The children, NULL when none; a slot may be NULL. */
	struct opt_node **p_children;
	/* Items in the payload for a list node; faces for a face node; the
	 * binding for a binding node. */
	/* Node-type-dependent scalar/count; OPT_NODEREF may store a relocated
	 * opt_node pointer value. */
	xvt_opt_value payload_count;
	/* For an OPT_NODEREF, the referenced name. */
	void *payload; ///< Relocated pointer to node-type-dependent payload data.
};

struct optimized_poly_object {
	/* The block's address when its pointers were last fixed; when it is not
	 * where the block is, opt_model_adjust_optimized_poly_object_pointers moves
	 * them. */
	void *self_marker;
	/* The decoder sets it from the 2 bytes at its place in the file's
	 * body. */
	/* Import packing stores the temporary packed-block handle here; the
	 * final returned block preserves the now-stale value. No runtime
	 * consumer is identified. */
	uint16_t reserved;
	int root_node_count;	      /* Entries in root_nodes. */
	struct opt_node **root_nodes; /* The top-level nodes. */
};

extern uint16_t g_loaded_models[201];
extern int g_cache_resolved_opt_node_refs;
extern int g_opt_source_is_version0;

struct face_record {
	/* Corner vertex indices; the fourth is -1 for a triangle. The
	 * renderer's name for the layout of opt_packed_face_record. */
	int vertex_idx[4];
	/* Edge number of each side; -1 for a triangle's fourth. */
	int edge_idx[4];
	int uv_idx[4];	   /* Texture coordinate index of each corner. */
	int normal_idx[4]; /* Vertex normal index of each corner. */
};

struct opt_tex_coord {
	float u; /* Horizontal texture coordinate. */
	float v; /* Vertical texture coordinate. */
};

struct opt_texture_data {
	/* The palette block, 4096 one-byte entries then 4096 RGB565 colors as
	 * 16 sub-palettes of 256. With inline_palette_count 0 it points at the
	 * texture's own block after its texels or at another texture's. With an
	 * inline palette the importer and the default texture store 256 here
	 * until a runtime copy points it at the copy. In a runtime model on a
	 * 16-bit display it points 4096 bytes before the converted colors. */
	uint16_t *palette;
	/* Nonzero when the palette is stored inline after the texels: its count
	 * of 256-color sub-palettes, 16 from the importer, 768 bytes each in a
	 * packed model. A runtime copy sets it to 0. */
	int inline_palette_count;
	/* Compared with width times height: when equal, the texels take
	 * dataSize bytes, else width times height. A runtime copy sets it to
	 * width times height. */
	int texture_size;
	/* Texel bytes, mip levels included, when texture_size equals width times
	 * height. */
	int data_size;
	int width;  /* Width of the top level in texels. */
	int height; /* Height of the top level in texels. */
};

struct opt_packed_face_record {
	/* Corner vertex indices; the fourth is -1 for a triangle. */
	int vertex_indices[4];
	/* Edge number of the side from each corner to the next, the last back
	 * to corner 0; the fourth is -1 for a triangle. */
	int edge_indices[4];
	int tex_coord_indices[4]; /* Texture coordinate index of each corner. */
	int normal_indices[4];	  /* Vertex normal index of each corner. */
};

struct opt_legacy_face_record_v0 {
	/* Corner vertex indices. No field of this record is read or written by
	 * name; the converter reads version 0 records as plain ints. */
	int vertex_indices[4];
	int edge_indices[4];	  /* Edge number of each side. */
	int tex_coord_indices[4]; /* Texture coordinate index of each corner. */
};

struct opt_legacy_face_data_v0 {
	/* Nothing uses this structure. */
	int edge_count;
	struct opt_legacy_face_record_v0
		records[1]; /* Nothing uses this structure. */
};

struct opt_legacy_face_storage_v0 {
	/* Not read or written by name: only the size of this structure is used,
	 * to find where a version 0 face node's data ends. The data holds all
	 * the records first, then all the normals, then all the gradients. */
	struct opt_legacy_face_record_v0 face_record;
	struct opt_vector face_normal; /* Not read or written by name. */
	struct opt_vector
		texture_gradients[2]; /* Not read or written by name. */
};

struct opt_legacy_face_storage {
	/* Not read or written by name: only the size of this structure is used,
	 * to find where a version 1 face node's data ends. The data holds all
	 * the records first, then all the normals, then all the gradients. */
	struct opt_packed_face_record face_record;
	struct opt_vector face_normal; /* Not read or written by name. */
	struct opt_vector
		texture_gradients[2]; /* Not read or written by name. */
};

struct opt_legacy_face_payload_v0 {
	/* The face node's edge count; the converter reads it as the payload's
	 * first int, never by this name. */
	int edge_count;
	/* Only &storage[payload_count] is used: the end of the face data, where
	 * the vertex normals of a mesh without a normal node follow. */
	struct opt_legacy_face_storage_v0 storage[1];
};

struct opt_legacy_face_payload {
	/* The face node's edge count; the converter reads it as the payload's
	 * first int, never by this name. */
	int edge_count;
	/* Only &storage[payload_count] is used: the end of the face data, where
	 * the vertex normals of a mesh without a normal node follow. */
	struct opt_legacy_face_storage storage[1];
};

struct opt_packed_face_data {
	/* Edges the faces number; the renderer's edge flag table holds at least
	 * this many (g_scene_edge_flags_capacity). */
	int edge_count;
	/* The face records, then one normal and two texture gradient vectors
	 * per face, then, when the mesh has no normal node, one normal per
	 * vertex. */
	struct opt_packed_face_record records[1];
};

struct opt_hardpoint {
	/* Type 0 to 31, named in g_hardpoint_type_names: weapons, then hangar,
	 * dock and cockpit points. */
	int hardpoint_type;
	struct opt_vector position; /* Position in the model's coordinates. */
};

uint16_t opt_model_load_handle(const char *model_filename);
void opt_model_adjust_optimized_poly_object_pointers(
	struct optimized_poly_object *model);
uint16_t opt_model_load_file_to_handle(char *filename);
unsigned int
opt_model_convert_legacy_model_to_optimized(unsigned int source_size);
void *opt_model_find_shared_texture_data_in_node_before_target(
	const void *texture_data, const struct opt_node *node,
	const struct opt_node *stop_node);
void *
opt_model_find_earlier_shared_texture_data(const void *texture_data,
					   struct optimized_poly_object *model,
					   const struct opt_node *stop_node);
unsigned int opt_model_convert_legacy_node_to_optimized(
	uint8_t *dst, struct opt_node *src_node,
	struct optimized_poly_object *src_model,
	struct optimized_poly_object *dst_model, struct scene_mesh *mesh_state);
void opt_model_collect_unique_vertices(struct opt_node *dst_vertex_node,
				       const struct opt_node *src_node,
				       struct optimized_poly_object *src_model,
				       struct scene_mesh *mesh_state);
void opt_model_collect_unique_tex_coords(
	struct opt_node *dst_tex_coord_node, const struct opt_node *src_node,
	struct optimized_poly_object *src_model, struct scene_mesh *mesh_state);
void opt_model_collect_unique_vertex_normals(
	struct opt_node *dst_normal_node, struct opt_node *src_node,
	struct optimized_poly_object *src_model, struct scene_mesh *mesh_state);
int opt_model_remap_vector_index(const struct opt_node *unique_vector_node,
				 const struct opt_vector *source_vectors,
				 int source_index);
int opt_model_remap_tex_coord_index(
	const struct opt_node *unique_tex_coord_node,
	const struct opt_tex_coord *source_tex_coords, int source_index);
void opt_model_append_converted_faces_for_node(
	struct opt_node *dst_face_node, struct opt_node *target_face_node,
	struct opt_node *node, struct optimized_poly_object *src_model,
	struct scene_mesh *mesh_state);
void opt_model_append_converted_faces_for_current_mesh(
	struct opt_node *dst_face_node, struct opt_node *target_face_node,
	struct optimized_poly_object *src_model, struct scene_mesh *mesh_state);
uint16_t opt_model_create_runtime_handle(unsigned int source_handle);
void opt_model_fixup_runtime_texture_pointers(
	struct opt_node *node, struct optimized_poly_object *dst_model,
	struct optimized_poly_object *src_model);
struct opt_node *
opt_model_find_corresponding_texture_node(struct opt_node *src_node,
					  struct opt_node *dst_node,
					  const uint16_t *source_palette);
struct opt_node *opt_model_find_corresponding_texture_node_in_model(
	const struct optimized_poly_object *dst_model,
	const struct optimized_poly_object *src_model,
	const uint16_t *source_palette);
unsigned int
opt_model_measure_node_and_raise_capacities(const struct opt_node *node,
					    struct scene_mesh *parent_state);
void opt_model_prepare_texture_palette(uint16_t *palette, int entry_count);
unsigned int opt_model_build_runtime_node(const struct opt_node *src_node,
					  struct scene_mesh *mesh_state,
					  uint8_t *dst);
struct opt_node *
opt_model_resolve_node_ref(const struct optimized_poly_object *object,
			   const char *name);
struct opt_node *opt_model_find_node_by_name(struct opt_node *node,
					     const char *name);

#ifdef __cplusplus
}
#endif

#endif
