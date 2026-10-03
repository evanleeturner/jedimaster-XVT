#ifndef XVT_RENDER_RENDER_QUAD_H
#define XVT_RENDER_RENDER_QUAD_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint32_t g_explosion_billboard_color_by_frame[32];

void render_quad_draw_model_texture(
	struct scene_billboard_queue_entry *quad_record);
void render_quad_draw_rotated_sprite(int angle, int screen_x, int screen_y,
				     uint16_t screen_size,
				     const void *texture_image);

#ifdef __cplusplus
}
#endif

#endif
