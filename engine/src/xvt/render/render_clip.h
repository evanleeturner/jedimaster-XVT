#ifndef XVT_RENDER_RENDER_CLIP_H
#define XVT_RENDER_RENDER_CLIP_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One polygon corner as the clip functions read and write it; laid out as
 * proj_vertex, whose list render_scene_draw_mesh_faces clips in place. */
struct render_clip_vertex {
	/* Screen x in pixels; view-space x while scaled_inverse_depth is
	 * negative. */
	float x;
	float y; /* Screen y in pixels; view-space y while that is negative. */
	/* g_proj_scale_int over the view depth; for a corner closer than depth 1,
	 * the depth minus 1, which is negative. */
	float scaled_inverse_depth;
	/* Light level, 0 to 1 as render_scene_compute_vertex_lighting sets it;
	 * render_scene_emit_flight_vertex turns it into the corner's gray. */
	float light_intensity;
	float u; /* Horizontal texture coordinate. */
	float v; /* Vertical texture coordinate. */
};

extern int g_clip_idx_a[32];
extern int g_clip_idx_b[32];
extern int g_clip_count_a;
extern int g_clip_count_b;
extern int g_clip_vert_cursor;
extern float g_inv_proj_scale;

int render_clip_clip_poly_top(int prev_vert_index, int cur_vert_index,
			      struct render_clip_vertex *vertices);
void render_clip_clip_poly_bottom(int prev_vert_index, int cur_vert_index,
				  struct render_clip_vertex *vertices);
int render_clip_clip_poly_left(int prev_vert_index, int cur_vert_index,
			       struct render_clip_vertex *vertices);
void render_clip_clip_poly_right(int prev_vert_index, int cur_vert_index,
				 struct render_clip_vertex *vertices);
void render_clip_clip_poly_near(int prev_vert_index, int cur_vert_index,
				struct render_clip_vertex *vertices);

#ifdef __cplusplus
}
#endif

#endif
