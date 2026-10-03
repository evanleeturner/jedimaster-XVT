#ifndef XVT_RENDER_SCENE_BILLBOARD_H
#define XVT_RENDER_SCENE_BILLBOARD_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct scene_billboard_queue_entry {
	/* Object or type index render_quad_draw_model_texture draws for. */
	uint16_t object_or_type_index;
	/* Texture frame, 0x8000 to 0xFEFF from
	 * scene_billboard_draw_or_queue_object. */
	int16_t frame;
	/* Size, before render_quad_draw_model_texture scales it by depth. */
	int16_t screen_size;
	int16_t screen_x; /* Projected X on the viewport. */
	/* Projected Y, from scene_billboard_draw_or_queue_object counted up from
	 * the viewport's bottom. */
	int16_t screen_y;
	int depth_z; /* View depth; the queue is drawn largest first. */
	int16_t rotation_angle; /* Roll on screen, in angle units. */
};

extern uint16_t g_billboard_model_node_switch_index;
extern uint16_t g_billboard_target_selection_state;
extern uint16_t g_billboard_object_or_type_index;
extern int16_t g_scene_billboard_queue_count;

void scene_billboard_draw_or_queue_object(int object_index);
void scene_billboard_queue_projected_textured(int object_or_type_index,
					      int frame, int screen_size,
					      int screen_x, int screen_y,
					      int depth_z, int rotation_angle);
void scene_billboard_render_queued_textured(int16_t draw_target_markers);
void scene_billboard_draw_roll_aligned_object_model(uint16_t object_index);
int scene_billboard_compute_projected_size(int depth_z,
					   uint16_t model_max_extent,
					   uint16_t base_screen_size);

#ifdef __cplusplus
}
#endif

#endif
