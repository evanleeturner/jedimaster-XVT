#ifndef XVT_FLIGHT_TARGETING_H
#define XVT_FLIGHT_TARGETING_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* No mesh chosen: the aim is measured at the object's position. Any mesh
 * index, 0xFFFF too, is a component. */
enum { TARGETING_NO_COMPONENT = -1 };

int16_t targeting_test_aim_cone_from(uint16_t object_idx, int16_t narrow_cone,
				     uint16_t viewer_idx, int32_t component);
int16_t targeting_test_aim_cone(uint16_t object_idx, int16_t narrow_cone,
				int player_idx);
extern uint16_t g_target_angle_score;
void targeting_draw_scene_object_boxes(void);
void targeting_draw_object_box(uint16_t object_idx, uint16_t component_idx,
			       uint8_t color_index);
int targeting_get_object_box_extent(unsigned int object_idx);
void targeting_project_object_or_mission_point(
	unsigned int obj_or_mission_point_ref, uint16_t component_idx,
	int *out_screen_x, int *out_screen_y, int *out_view_z);

#ifdef __cplusplus
}
#endif

#endif
