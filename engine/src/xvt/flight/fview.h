#ifndef XVT_FLIGHT_FVIEW_H
#define XVT_FLIGHT_FVIEW_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void fview_build_camera_orient(int16_t view_roll, int16_t view_pitch,
			       int16_t view_yaw, int16_t view_up_axis_angle,
			       int16_t hud_aim_x, int16_t hud_aim_y,
			       struct object_record *obj_record);
int fview_set_object_transform(int16_t roll, int16_t pitch, int16_t yaw,
			       int16_t up_axis_angle,
			       struct object_record *obj_record);
void fview_calcrotatemove(int16_t pitch, int16_t yaw,
			  struct object_record *obj_record);
void fview_calcrotateorient(int16_t roll, int16_t up_axis_angle,
			    struct object_record *obj_record);
int fview_compute_object_view_matrix(void);
void fview_transformaxes(int axis_x_q15, int axis_y_q15, int axis_z_q15,
			 int16_t angle_q16);

extern int g_fview_forward_x_q15;
extern int g_fview_forward_y_q15;
extern int g_fview_forward_z_q15;
extern int g_fview_side_x_q15;
extern int g_fview_side_y_q15;
extern int g_fview_side_z_q15;
extern int g_fview_up_x_q15;
extern int g_fview_up_y_q15;
extern int g_fview_up_z_q15;

#ifdef __cplusplus
}
#endif

#endif
