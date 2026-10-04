#ifndef XVT_FLIGHT_TRANSFM2_H
#define XVT_FLIGHT_TRANSFM2_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int g_cam_mat_r0_x;
extern int g_cam_mat_r0_y;
extern int g_cam_mat_r0_z;
extern int g_cam_mat_r1_x;
extern int g_cam_mat_r1_y;
extern int g_cam_mat_r1_z;
extern int g_cam_mat_r2_x;
extern int g_cam_mat_r2_y;
extern int g_cam_mat_r2_z;
extern int g_view_space_x;
extern int g_view_space_y;
extern int g_view_space_depth;
extern int32_t g_proj_scale_half_int;
extern uint8_t g_perspective_shift;
extern uint16_t g_proj_aspect_y;

int transfm2_clipobjecteyez(int x, int y, int z);
int transfm2_cam_mat_dot_row0(int x, int y, int z);
int transfm2_cam_mat_dot_row1(int x, int y, int z);
int transfm2_cam_mat_dot_row2(int x, int y, int z);
int transfm2_project_screen_x(int view_x, int view_z);
int transfm2_project_screen_y(int view_y, int view_z);
int transfm2_project_screen_x_fixed_point(int view_x, unsigned int depth);
int transfm2_project_screen_y_fixed_point(int view_y, unsigned int depth);

#ifdef __cplusplus
}
#endif

#endif
