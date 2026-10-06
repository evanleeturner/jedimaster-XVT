#ifndef XVT_ASSETS_MODEL_PREVIEW_H
#define XVT_ASSETS_MODEL_PREVIEW_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int g_node_switch_index;
extern int g_world_light_direction_x;
extern int g_world_light_direction_y;
extern int g_world_light_direction_z;
extern int g_model_preview_skip_scene_reset;
extern int g_model_preview_render_resources_initialized;
extern struct optimized_poly_object *g_model_preview_model_data;
extern uint16_t g_model_preview_aux_buffer_handle;
extern unsigned int g_model_preview_aux_buffer_capacity_bytes;

struct model_preview_craft_position {
	/* Preview world x mission_setup_draw_craft_loadout gives a craft type,
	 * from its row of g_model_preview_craft_positions. */
	int x;
	int y; /* Preview world y for the craft type. */
	int z; /* Preview world z for the craft type. */
};

int model_preview_load_model(const char *model_file_name);
void model_preview_free_resources(void);
int model_preview_render_viewport(int x, int y, int width, int height, ...);
void model_preview_scale_opt_node_tree(struct opt_node *node,
				       struct optimized_poly_object *opt,
				       double scale);
void model_preview_scale_opt_root_nodes(struct optimized_poly_object *opt,
					double scale);
void model_preview_accumulate_opt_node_bounds(
	struct opt_node *node, struct optimized_poly_object *object);
double
model_preview_compute_opt_bounds_extent(struct optimized_poly_object *object,
					int axis);
int model_preview_reset_view_and_render_state(void);
void model_preview_set_light_direction(int x, int y, int z);
void model_preview_set_object_euler_degrees(float pitch_deg, float yaw_deg,
					    float roll_deg);
void model_preview_set_node_switch_index(int node_switch_index);
void model_preview_set_object_world_position(int x, int y, int z);
void model_preview_save_state(void);
void model_preview_restore_state(void);
void model_preview_set_object_up_axis_angle_degrees(float angle_deg);
int model_preview_get_displayed_size_meters(void);

#ifdef __cplusplus
}
#endif

#endif
