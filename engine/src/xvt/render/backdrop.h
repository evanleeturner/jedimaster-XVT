#ifndef XVT_RENDER_BACKDROP_H
#define XVT_RENDER_BACKDROP_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int32_t g_backdrop_cam_r0x_steps[16];
extern int32_t g_backdrop_cam_r0y_steps[16];
extern int32_t g_backdrop_cam_r0z_steps[16];
extern int32_t g_backdrop_cam_r1x_steps[16];
extern int32_t g_backdrop_cam_r1y_steps[16];
extern int32_t g_backdrop_cam_r1z_steps[16];
extern int32_t g_backdrop_cam_r2x_steps[16];
extern int32_t g_backdrop_cam_r2y_steps[16];
extern int32_t g_backdrop_cam_r2z_steps[16];
extern uint8_t g_backdrop_model_types[64];
extern uint8_t g_backdrop_packed_directions[64];
extern uint16_t g_backdrop_positive_y_count;
extern uint16_t g_backdrop_negative_y_count;
extern uint16_t g_backdrop_positive_z_count;
extern uint16_t g_backdrop_negative_z_count;
extern uint16_t g_backdrop_positive_x_count;
extern uint16_t g_backdrop_negative_x_count;

void backdrop_draw_model_tex_quad_at_screen(int model_type, int screen_x,
					    int screen_y, int angle);
void backdrop_build_star_offsets_and_render(void);
void backdrop_project_and_draw_screen_quad(int view_x, int view_y, int view_z,
					   int angle, int backdrop_number);
void backdrop_generate_default_records(void);

#ifdef __cplusplus
}
#endif

#endif
