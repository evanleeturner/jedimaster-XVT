#include "xvt/render/render_clip.h"

#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"

/* One of the two lists of vertex indices a polygon passes between while it
 * is clipped, into the vertex array the clip functions are given: the near,
 * bottom and right clips append to it, and the finished polygon ends here.
 * Holds 32; nothing checks the count against that. Written by those three
 * clips, render_scene_draw_mesh_faces and render_quad_draw_rotated_sprite. */
// GLOBAL: XVT 0x53F8F0
int g_clip_idx_a[32];
/* The other list: the top and left clips append to it, and
 * render_scene_draw_mesh_faces copies a polygon into it for the near clip. Holds
 * 32; nothing checks the count against that. */
// GLOBAL: XVT 0x52F870
int g_clip_idx_b[32] = {0};
/* Indices in use in g_clip_idx_a. Callers set it to the polygon's corner count
 * or to 0 before a pass that appends to the list. */
// GLOBAL: XVT 0x54F978
int g_clip_count_a;
/* Indices in use in g_clip_idx_b. Callers set it to 0 before a pass that
 * appends to the list. */
// GLOBAL: XVT 0x54F998
int g_clip_count_b = 0;
/* Index in the vertex array of the next vertex a clip creates; each clip
 * that creates one takes it and adds 1. render_scene_draw_mesh_faces starts it
 * after the mesh's projected vertices, render_quad_draw_rotated_sprite after its
 * 4 corners. Nothing checks it against the array's size. */
// GLOBAL: XVT 0x54F99C
int g_clip_vert_cursor;
/* 1 over g_proj_scale_int, as a float: multiplying a screen offset by depth
 * and this gives a view-space coordinate. Only render_scene_initialize writes
 * it. */
// GLOBAL: XVT 0x99940C
float g_inv_proj_scale;

/* Clips one edge of a polygon, from vertex prev_vert_index to cur_vert_index,
 * against the top of the viewport (y of 0; y under 0 is outside) and appends
 * to g_clip_idx_b: the current vertex when both ends are inside; a new vertex
 * on the edge and then the current one when only the current end is; the new
 * vertex alone when only the previous end is; nothing when neither is.
 * Returns the new g_clip_count_b, or INT32_MIN when it appended nothing; callers
 * ignore it. The new vertex goes at g_clip_vert_cursor, which it advances:
 * x, light and scaled_inverse_depth are interpolated along the edge on the
 * screen, and u and v by the share of depth (g_proj_scale_int over
 * scaled_inverse_depth) unless the two ends' scaled_inverse_depth are equal. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4096D0
int render_clip_clip_poly_top(int prev_vert_index, int cur_vert_index,
			      struct render_clip_vertex *vertices)
{
	struct render_clip_vertex *previous = &vertices[prev_vert_index];
	float previous_y = previous->y;
	struct render_clip_vertex *current = &vertices[cur_vert_index];
	float current_y = current->y;
	int result = INT32_MIN;

	if (previous_y < 0.0f) {
		if (current_y >= 0.0f) {
			int output = g_clip_vert_cursor++;
			float previous_x = previous->x;
			float current_x = current->x;
			float previous_light_intensity =
				previous->light_intensity;
			float current_light_intensity =
				current->light_intensity;
			float previous_u = previous->u;
			float current_u = current->u;
			float previous_v = previous->v;
			float current_v = current->v;
			float previous_scaled_inverse_depth =
				previous->scaled_inverse_depth;
			float current_scaled_inverse_depth =
				current->scaled_inverse_depth;
			float delta_u = current_u - previous_u;
			float delta_x = current_x - previous_x;
			float delta_light_intensity = current_light_intensity -
						      previous_light_intensity;
			float delta_y = current_y - previous_y;
			float delta_v = current_v - previous_v;
			float delta_scaled_inverse_depth =
				current_scaled_inverse_depth -
				previous_scaled_inverse_depth;

			previous_y = -previous_y;
			struct render_clip_vertex *destination;
			float t;
			if (previous_y < current_y) {
				t = previous_y / delta_y;
				destination = &vertices[output];
				destination->x = previous_x + t * delta_x;
				destination->light_intensity =
					previous_light_intensity +
					t * delta_light_intensity;
				destination->scaled_inverse_depth =
					previous_scaled_inverse_depth +
					t * delta_scaled_inverse_depth;
				if (delta_scaled_inverse_depth != 0.0f) {
					float base_depth =
						(float)(unsigned int)
							g_proj_scale_int /
						previous_scaled_inverse_depth;
					float other_depth =
						(float)(unsigned int)
							g_proj_scale_int /
						current_scaled_inverse_depth;
					float destination_depth =
						(float)(unsigned int)
							g_proj_scale_int /
						destination
							->scaled_inverse_depth;
					float uv_t = (destination_depth -
						      base_depth) /
						     (other_depth - base_depth);
					destination->u =
						previous_u + delta_u * uv_t;
					destination->v =
						previous_v + delta_v * uv_t;
				} else {
					destination->u =
						previous_u + t * delta_u;
					destination->v =
						previous_v + t * delta_v;
				}
			} else {
				t = current_y / delta_y;
				destination = &vertices[output];
				destination->x = current_x - delta_x * t;
				destination->light_intensity =
					current_light_intensity -
					delta_light_intensity * t;
				destination->scaled_inverse_depth =
					current_scaled_inverse_depth -
					delta_scaled_inverse_depth * t;
				if (delta_scaled_inverse_depth != 0.0f) {
					float base_projection =
						(float)(unsigned int)
							g_proj_scale_int /
						current_scaled_inverse_depth;
					float other_projection =
						(float)(unsigned int)
							g_proj_scale_int /
						previous_scaled_inverse_depth;
					float destination_projection =
						(float)(unsigned int)
							g_proj_scale_int /
						destination
							->scaled_inverse_depth;
					float uv_t = (destination_projection -
						      base_projection) /
						     (other_projection -
						      base_projection);
					destination->u =
						current_u - delta_u * uv_t;
					destination->v =
						current_v - delta_v * uv_t;
				} else {
					destination->u =
						current_u - delta_u * t;
					destination->v =
						current_v - delta_v * t;
				}
			}
			destination->y = 0.0f;
			g_clip_idx_b[g_clip_count_b++] = output;
			g_clip_idx_b[g_clip_count_b++] = cur_vert_index;
			return g_clip_count_b;
		}
	} else if (current_y < 0.0f) {
		int output = g_clip_vert_cursor++;
		float previous_x = previous->x;
		float current_x = current->x;
		float previous_rhw = previous->light_intensity;
		float current_rhw = current->light_intensity;
		float previous_u = previous->u;
		float current_u = current->u;
		float previous_v = previous->v;
		float current_v = current->v;
		float previous_z = previous->scaled_inverse_depth;
		float current_z = current->scaled_inverse_depth;
		float delta_x = current_x - previous_x;
		float delta_y = current_y - previous_y;
		float delta_rhw = current_rhw - previous_rhw;
		float delta_u = current_u - previous_u;
		float delta_v = current_v - previous_v;
		float delta_z = current_z - previous_z;

		current_y = -current_y;
		struct render_clip_vertex *destination;
		float t;
		if (current_y > previous_y) {
			t = previous_y / delta_y;
			destination = &vertices[output];
			destination->x = previous_x - t * delta_x;
			destination->light_intensity =
				previous_rhw - t * delta_rhw;
			destination->scaled_inverse_depth =
				previous_z - t * delta_z;
			if (delta_z != 0.0f) {
				float base_projection =
					(float)(unsigned int)g_proj_scale_int /
					previous_z;
				float other_projection =
					(float)(unsigned int)g_proj_scale_int /
					current_z;
				float destination_projection =
					(float)(unsigned int)g_proj_scale_int /
					destination->scaled_inverse_depth;
				float uv_t =
					(destination_projection -
					 base_projection) /
					(other_projection - base_projection);
				destination->u = previous_u + delta_u * uv_t;
				destination->v = previous_v + delta_v * uv_t;
			} else {
				destination->u = previous_u - t * delta_u;
				destination->v = previous_v - t * delta_v;
			}
		} else {
			t = current_y / delta_y;
			destination = &vertices[output];
			destination->x = current_x + delta_x * t;
			destination->light_intensity =
				current_rhw + delta_rhw * t;
			destination->scaled_inverse_depth =
				current_z + delta_z * t;
			if (delta_z != 0.0f) {
				float base_projection =
					(float)(unsigned int)g_proj_scale_int /
					current_z;
				float other_projection =
					(float)(unsigned int)g_proj_scale_int /
					previous_z;
				float destination_projection =
					(float)(unsigned int)g_proj_scale_int /
					destination->scaled_inverse_depth;
				float uv_t =
					(destination_projection -
					 base_projection) /
					(other_projection - base_projection);
				destination->u = current_u - delta_u * uv_t;
				destination->v = current_v - delta_v * uv_t;
			} else {
				destination->u = current_u + delta_u * t;
				destination->v = current_v + delta_v * t;
			}
		}
		destination->y = 0.0f;
		g_clip_idx_b[g_clip_count_b++] = output;
		return g_clip_count_b;
	} else {
		g_clip_idx_b[g_clip_count_b++] = cur_vert_index;
		result = g_clip_count_b;
	}
	return result;
}

/* Clips one edge against the bottom of the viewport, y of g_flight_vp_max_y, the
 * way render_clip_clip_poly_top clips against the top, appending to g_clip_idx_a;
 * returns nothing. A current vertex exactly on the edge counts as inside after
 * an inside previous vertex but is dropped after an outside one. */
// FUNCTION: XVT 0x409C10
void render_clip_clip_poly_bottom(int prev_vert_index, int cur_vert_index,
				  struct render_clip_vertex *vertices)
{
	struct render_clip_vertex *previous = &vertices[prev_vert_index];
	float previous_y = previous->y;
	struct render_clip_vertex *current = &vertices[cur_vert_index];
	float current_y = current->y;
	int viewport_bottom = g_flight_vp_max_y;
	float boundary = (float)viewport_bottom;

	if (previous_y > boundary) {
		if (current_y >= boundary) {
			return;
		}

		{
			int output = g_clip_vert_cursor++;
			float previous_x = previous->x;
			float current_x = current->x;
			float previous_light_intensity =
				previous->light_intensity;
			float current_light_intensity =
				current->light_intensity;
			float previous_u = previous->u;
			float current_u = current->u;
			float previous_v = previous->v;
			float current_v = current->v;
			float previous_scaled_inverse_depth =
				previous->scaled_inverse_depth;
			float current_scaled_inverse_depth =
				current->scaled_inverse_depth;
			float delta_x = current_x - previous_x;
			float delta_y = current_y - previous_y;
			float delta_light_intensity = current_light_intensity -
						      previous_light_intensity;
			float delta_u = current_u - previous_u;
			float delta_v = current_v - previous_v;
			float delta_scaled_inverse_depth =
				current_scaled_inverse_depth -
				previous_scaled_inverse_depth;

			/* From here previous_y and current_y hold each vertex's distance from the bottom edge; whichever
			 * is divided by deltaY below then holds the fraction along the edge where it is clipped. */
			previous_y = previous_y - boundary;
			current_y = boundary - current_y;
			if (current_y > previous_y) {
				previous_y = previous_y / delta_y;
				vertices[output].x =
					previous_x - previous_y * delta_x;
				vertices[output].light_intensity =
					previous_light_intensity -
					previous_y * delta_light_intensity;
				vertices[output].scaled_inverse_depth =
					previous_scaled_inverse_depth -
					previous_y * delta_scaled_inverse_depth;
				if (delta_scaled_inverse_depth != 0.0f) {
					float projection =
						(float)g_proj_scale_int;
					float previous_depth =
						projection /
						previous_scaled_inverse_depth;
					float current_depth =
						projection /
						current_scaled_inverse_depth;
					float destination_depth =
						projection /
						vertices[output]
							.scaled_inverse_depth;
					float uv_t = (destination_depth -
						      previous_depth) /
						     (current_depth -
						      previous_depth);
					vertices[output].u =
						previous_u + delta_u * uv_t;
					vertices[output].v =
						previous_v + delta_v * uv_t;
				} else {
					vertices[output].u =
						previous_u -
						previous_y * delta_u;
					vertices[output].v =
						previous_v -
						previous_y * delta_v;
				}
			} else {
				current_y = current_y / delta_y;
				vertices[output].x =
					current_x + current_y * delta_x;
				vertices[output].light_intensity =
					current_light_intensity +
					delta_light_intensity * current_y;
				vertices[output].scaled_inverse_depth =
					current_scaled_inverse_depth +
					delta_scaled_inverse_depth * current_y;
				if (delta_scaled_inverse_depth != 0.0f) {
					float projection =
						(float)g_proj_scale_int;
					float previous_projection =
						projection /
						previous_scaled_inverse_depth;
					float current_projection =
						projection /
						current_scaled_inverse_depth;
					float destination_projection =
						projection /
						vertices[output]
							.scaled_inverse_depth;
					float uv_t = (destination_projection -
						      current_projection) /
						     (previous_projection -
						      current_projection);
					vertices[output].u =
						current_u - delta_u * uv_t;
					vertices[output].v =
						current_v - delta_v * uv_t;
				} else {
					vertices[output].u =
						current_u + delta_u * current_y;
					vertices[output].v =
						current_v + delta_v * current_y;
				}
			}
			vertices[output].y = (float)g_flight_vp_max_y;
			g_clip_idx_a[g_clip_count_a++] = output;
			g_clip_idx_a[g_clip_count_a++] = cur_vert_index;
		}
	} else if (current_y > boundary) {
		int output = g_clip_vert_cursor++;
		float previous_rhw = previous->light_intensity;
		float previous_x = previous->x;
		float current_x = current->x;
		float current_rhw = current->light_intensity;
		float previous_u = previous->u;
		float current_u = current->u;
		float previous_v = previous->v;
		float current_v = current->v;
		float previous_z = previous->scaled_inverse_depth;
		float current_z = current->scaled_inverse_depth;
		float delta_x = current_x - previous_x;
		float delta_y = current_y - previous_y;
		float delta_rhw = current_rhw - previous_rhw;
		float delta_u = current_u - previous_u;
		float delta_v = current_v - previous_v;
		float delta_z = current_z - previous_z;

		/* From here previous_y and current_y hold each vertex's distance from the bottom edge; whichever is
		 * divided by deltaY below then holds the fraction along the edge where it is clipped. */
		current_y = current_y - boundary;
		previous_y = boundary - previous_y;
		if (current_y > previous_y) {
			previous_y = previous_y / delta_y;
			vertices[output].x = previous_x + previous_y * delta_x;
			vertices[output].light_intensity =
				previous_rhw + previous_y * delta_rhw;
			vertices[output].scaled_inverse_depth =
				previous_z + previous_y * delta_z;
			if (delta_z != 0.0f) {
				float projection = (float)g_proj_scale_int;
				float previous_projection =
					projection / previous_z;
				float current_projection =
					projection / current_z;
				float destination_projection =
					projection /
					vertices[output].scaled_inverse_depth;
				float uv_t = (destination_projection -
					      previous_projection) /
					     (current_projection -
					      previous_projection);
				vertices[output].u =
					previous_u + delta_u * uv_t;
				vertices[output].v =
					previous_v + delta_v * uv_t;
			} else {
				vertices[output].u =
					previous_u + previous_y * delta_u;
				vertices[output].v =
					previous_v + previous_y * delta_v;
			}
		} else {
			current_y = current_y / delta_y;
			vertices[output].x = current_x - current_y * delta_x;
			vertices[output].light_intensity =
				current_rhw - delta_rhw * current_y;
			vertices[output].scaled_inverse_depth =
				current_z - delta_z * current_y;
			if (delta_z != 0.0f) {
				float projection = (float)g_proj_scale_int;
				float previous_projection =
					projection / previous_z;
				float current_projection =
					projection / current_z;
				float destination_projection =
					projection /
					vertices[output].scaled_inverse_depth;
				float uv_t = (destination_projection -
					      current_projection) /
					     (previous_projection -
					      current_projection);
				vertices[output].u = current_u - delta_u * uv_t;
				vertices[output].v = current_v - delta_v * uv_t;
			} else {
				vertices[output].u =
					current_u - delta_u * current_y;
				vertices[output].v =
					current_v - delta_v * current_y;
			}
		}
		vertices[output].y = (float)g_flight_vp_max_y;
		g_clip_idx_a[g_clip_count_a++] = output;
	} else {
		g_clip_idx_a[g_clip_count_a++] = cur_vert_index;
	}
}

/* Clips one edge against the left of the viewport (x of 0; x under 0 is
 * outside) the way render_clip_clip_poly_top clips against the top, appending to
 * g_clip_idx_b and returning the same values. */
// FUNCTION: XVT 0x40A1A0
int render_clip_clip_poly_left(int prev_vert_index, int cur_vert_index,
			       struct render_clip_vertex *vertices)
{
	struct render_clip_vertex *previous = &vertices[prev_vert_index];
	float previous_x = previous->x;
	struct render_clip_vertex *current = &vertices[cur_vert_index];
	float current_x = current->x;
	int result = INT32_MIN;

	if (previous_x < 0.0f) {
		if (current_x < 0.0f) {
			return result;
		}

		{
			int output = g_clip_vert_cursor++;
			float previous_y = previous->y;
			float current_y = current->y;
			float previous_light_intensity =
				previous->light_intensity;
			float current_light_intensity =
				current->light_intensity;
			float previous_u = previous->u;
			float current_u = current->u;
			float previous_v = previous->v;
			float current_v = current->v;
			float previous_scaled_inverse_depth =
				previous->scaled_inverse_depth;
			float current_scaled_inverse_depth =
				current->scaled_inverse_depth;
			float delta_x = current_x - previous_x;
			float delta_y = current_y - previous_y;
			float delta_light_intensity = current_light_intensity -
						      previous_light_intensity;
			float delta_u = current_u - previous_u;
			float delta_v = current_v - previous_v;
			float delta_scaled_inverse_depth =
				current_scaled_inverse_depth -
				previous_scaled_inverse_depth;

			/* From here previous_x and current_x hold each vertex's distance from the left edge; whichever
			 * is divided by deltaX below then holds the fraction along the edge where it is clipped. */
			previous_x = -previous_x;
			struct render_clip_vertex *destination;
			if (previous_x < current_x) {
				previous_x = previous_x / delta_x;
				destination = &vertices[output];
				destination->y =
					previous_y + previous_x * delta_y;
				destination->light_intensity =
					previous_light_intensity +
					previous_x * delta_light_intensity;
				destination->scaled_inverse_depth =
					previous_scaled_inverse_depth +
					previous_x * delta_scaled_inverse_depth;
				if (delta_scaled_inverse_depth != 0.0f) {
					float projection = (float)(unsigned int)
						g_proj_scale_int;
					float base_depth =
						projection /
						previous_scaled_inverse_depth;
					float other_depth =
						projection /
						current_scaled_inverse_depth;
					float uv_t =
						(projection /
							 destination
								 ->scaled_inverse_depth -
						 base_depth) /
						(other_depth - base_depth);
					destination->u =
						previous_u + delta_u * uv_t;
					destination->v =
						previous_v + delta_v * uv_t;
				} else {
					destination->u = previous_u +
							 previous_x * delta_u;
					destination->v = previous_v +
							 previous_x * delta_v;
				}
			} else {
				current_x = current_x / delta_x;
				destination = &vertices[output];
				destination->y =
					current_y - current_x * delta_y;
				destination->light_intensity =
					current_light_intensity -
					current_x * delta_light_intensity;
				destination->scaled_inverse_depth =
					current_scaled_inverse_depth -
					current_x * delta_scaled_inverse_depth;
				if (delta_scaled_inverse_depth != 0.0f) {
					float projection = (float)(unsigned int)
						g_proj_scale_int;
					float base_projection =
						projection /
						current_scaled_inverse_depth;
					float other_projection =
						projection /
						previous_scaled_inverse_depth;
					float uv_t =
						(projection /
							 destination
								 ->scaled_inverse_depth -
						 base_projection) /
						(other_projection -
						 base_projection);
					destination->u =
						current_u - delta_u * uv_t;
					destination->v =
						current_v - delta_v * uv_t;
				} else {
					destination->u =
						current_u - current_x * delta_u;
					destination->v =
						current_v - current_x * delta_v;
				}
			}
			destination->x = 0.0f;
			g_clip_idx_b[g_clip_count_b++] = output;
			g_clip_idx_b[g_clip_count_b++] = cur_vert_index;
			return g_clip_count_b;
		}
	} else if (current_x < 0.0f) {
		{
			int output = g_clip_vert_cursor++;
			float previous_y = previous->y;
			float current_y = current->y;
			float previous_rhw = previous->light_intensity;
			float current_rhw = current->light_intensity;
			float previous_u = previous->u;
			float current_u = current->u;
			float previous_v = previous->v;
			float current_v = current->v;
			float previous_z = previous->scaled_inverse_depth;
			float current_z = current->scaled_inverse_depth;
			float delta_x = current_x - previous_x;
			float delta_y = current_y - previous_y;
			float delta_rhw = current_rhw - previous_rhw;
			float delta_u = current_u - previous_u;
			float delta_v = current_v - previous_v;
			float delta_z = current_z - previous_z;

			/* From here previous_x and current_x hold each vertex's distance from the left edge; whichever
			 * is divided by deltaX below then holds the fraction along the edge where it is clipped. */
			current_x = -current_x;
			struct render_clip_vertex *destination;
			if (current_x > previous_x) {
				previous_x = previous_x / delta_x;
				destination = &vertices[output];
				destination->y =
					previous_y - previous_x * delta_y;
				destination->light_intensity =
					previous_rhw - previous_x * delta_rhw;
				destination->scaled_inverse_depth =
					previous_z - previous_x * delta_z;
				if (delta_z != 0.0f) {
					float projection = (float)(unsigned int)
						g_proj_scale_int;
					float base_projection =
						projection / previous_z;
					float other_projection =
						projection / current_z;
					float uv_t =
						(projection /
							 destination
								 ->scaled_inverse_depth -
						 base_projection) /
						(other_projection -
						 base_projection);
					destination->u =
						previous_u + delta_u * uv_t;
					destination->v =
						previous_v + delta_v * uv_t;
				} else {
					destination->u = previous_u -
							 previous_x * delta_u;
					destination->v = previous_v -
							 previous_x * delta_v;
				}
			} else {
				current_x = current_x / delta_x;
				destination = &vertices[output];
				destination->y =
					current_y + current_x * delta_y;
				destination->light_intensity =
					current_rhw + current_x * delta_rhw;
				destination->scaled_inverse_depth =
					current_z + current_x * delta_z;
				if (delta_z != 0.0f) {
					float projection = (float)(unsigned int)
						g_proj_scale_int;
					float base_projection =
						projection / current_z;
					float other_projection =
						projection / previous_z;
					float uv_t =
						(projection /
							 destination
								 ->scaled_inverse_depth -
						 base_projection) /
						(other_projection -
						 base_projection);
					destination->u =
						current_u - delta_u * uv_t;
					destination->v =
						current_v - delta_v * uv_t;
				} else {
					destination->u =
						current_u + current_x * delta_u;
					destination->v =
						current_v + current_x * delta_v;
				}
			}
			destination->x = 0.0f;
			g_clip_idx_b[g_clip_count_b++] = output;
			return g_clip_count_b;
		}
	} else {
		g_clip_idx_b[g_clip_count_b++] = cur_vert_index;
		result = g_clip_count_b;
	}
	return result;
}

/* Clips one edge against the right of the viewport, x of g_flight_vp_width (x
 * over it is outside), the way render_clip_clip_poly_top clips against the top,
 * appending to g_clip_idx_a; returns nothing. */
// FUNCTION: XVT 0x40A6E0
void render_clip_clip_poly_right(int prev_vert_index, int cur_vert_index,
				 struct render_clip_vertex *vertices)
{
	struct render_clip_vertex *previous = &vertices[prev_vert_index];
	float previous_x = previous->x;
	struct render_clip_vertex *current = &vertices[cur_vert_index];
	float current_x = current->x;
	int viewport_width = g_flight_vp_width;
	float boundary = (float)viewport_width;

	if (previous_x > boundary) {
		if (current_x > boundary) {
			return;
		}

		{
			int output = g_clip_vert_cursor++;
			float previous_y = previous->y;
			float current_y = current->y;
			float previous_light_intensity =
				previous->light_intensity;
			float current_light_intensity =
				current->light_intensity;
			float previous_u = previous->u;
			float current_u = current->u;
			float previous_v = previous->v;
			float current_v = current->v;
			float previous_scaled_inverse_depth =
				previous->scaled_inverse_depth;
			float current_scaled_inverse_depth =
				current->scaled_inverse_depth;
			float delta_x = current_x - previous_x;
			float delta_y = current_y - previous_y;
			float delta_light_intensity = current_light_intensity -
						      previous_light_intensity;
			float delta_u = current_u - previous_u;
			float delta_v = current_v - previous_v;
			float delta_scaled_inverse_depth =
				current_scaled_inverse_depth -
				previous_scaled_inverse_depth;

			/* From here previous_x and current_x hold each vertex's distance from the right edge; whichever
			 * is divided by deltaX below then holds the fraction along the edge where it is clipped. */
			previous_x = previous_x - boundary;
			current_x = boundary - current_x;
			if (current_x > previous_x) {
				previous_x = previous_x / delta_x;
				vertices[output].y =
					previous_y - previous_x * delta_y;
				vertices[output].light_intensity =
					previous_light_intensity -
					previous_x * delta_light_intensity;
				vertices[output].scaled_inverse_depth =
					previous_scaled_inverse_depth -
					previous_x * delta_scaled_inverse_depth;
				if (delta_scaled_inverse_depth != 0.0f) {
					float projection =
						(float)g_proj_scale_int;
					float previous_depth =
						projection /
						previous_scaled_inverse_depth;
					float current_depth =
						projection /
						current_scaled_inverse_depth;
					float destination_depth =
						projection /
						vertices[output]
							.scaled_inverse_depth;
					float uv_t = (destination_depth -
						      previous_depth) /
						     (current_depth -
						      previous_depth);
					vertices[output].u =
						previous_u + delta_u * uv_t;
					vertices[output].v =
						previous_v + delta_v * uv_t;
				} else {
					vertices[output].u =
						previous_u -
						previous_x * delta_u;
					vertices[output].v =
						previous_v -
						previous_x * delta_v;
				}
			} else {
				current_x = current_x / delta_x;
				vertices[output].y =
					current_y + current_x * delta_y;
				vertices[output].light_intensity =
					current_light_intensity +
					delta_light_intensity * current_x;
				vertices[output].scaled_inverse_depth =
					current_scaled_inverse_depth +
					delta_scaled_inverse_depth * current_x;
				if (delta_scaled_inverse_depth != 0.0f) {
					float projection =
						(float)g_proj_scale_int;
					float previous_projection =
						projection /
						previous_scaled_inverse_depth;
					float current_projection =
						projection /
						current_scaled_inverse_depth;
					float destination_projection =
						projection /
						vertices[output]
							.scaled_inverse_depth;
					float uv_t = (destination_projection -
						      current_projection) /
						     (previous_projection -
						      current_projection);
					vertices[output].u =
						current_u - delta_u * uv_t;
					vertices[output].v =
						current_v - delta_v * uv_t;
				} else {
					vertices[output].u =
						current_u + delta_u * current_x;
					vertices[output].v =
						current_v + delta_v * current_x;
				}
			}
			vertices[output].x = (float)g_flight_vp_width;
			g_clip_idx_a[g_clip_count_a++] = output;
			g_clip_idx_a[g_clip_count_a++] = cur_vert_index;
		}
	} else if (current_x > boundary) {
		int output = g_clip_vert_cursor++;
		float previous_rhw = previous->light_intensity;
		float previous_y = previous->y;
		float current_y = current->y;
		float current_rhw = current->light_intensity;
		float previous_u = previous->u;
		float current_u = current->u;
		float previous_v = previous->v;
		float current_v = current->v;
		float previous_z = previous->scaled_inverse_depth;
		float current_z = current->scaled_inverse_depth;
		float delta_x = current_x - previous_x;
		float delta_y = current_y - previous_y;
		float delta_rhw = current_rhw - previous_rhw;
		float delta_u = current_u - previous_u;
		float delta_v = current_v - previous_v;
		float delta_z = current_z - previous_z;

		/* From here previous_x and current_x hold each vertex's distance from the right edge; whichever is
		 * divided by deltaX below then holds the fraction along the edge where it is clipped. */
		previous_x = boundary - previous_x;
		current_x = current_x - boundary;
		if (current_x > previous_x) {
			previous_x = previous_x / delta_x;
			vertices[output].y = previous_y + previous_x * delta_y;
			vertices[output].light_intensity =
				previous_rhw + previous_x * delta_rhw;
			vertices[output].scaled_inverse_depth =
				previous_z + previous_x * delta_z;
			if (delta_z != 0.0f) {
				float projection = (float)g_proj_scale_int;
				float previous_projection =
					projection / previous_z;
				float current_projection =
					projection / current_z;
				float destination_projection =
					projection /
					vertices[output].scaled_inverse_depth;
				float uv_t = (destination_projection -
					      previous_projection) /
					     (current_projection -
					      previous_projection);
				vertices[output].u =
					previous_u + delta_u * uv_t;
				vertices[output].v =
					previous_v + delta_v * uv_t;
			} else {
				vertices[output].u =
					previous_u + previous_x * delta_u;
				vertices[output].v =
					previous_v + previous_x * delta_v;
			}
		} else {
			current_x = current_x / delta_x;
			vertices[output].y = current_y - current_x * delta_y;
			vertices[output].light_intensity =
				current_rhw - delta_rhw * current_x;
			vertices[output].scaled_inverse_depth =
				current_z - delta_z * current_x;
			if (delta_z != 0.0f) {
				float projection = (float)g_proj_scale_int;
				float previous_projection =
					projection / previous_z;
				float current_projection =
					projection / current_z;
				float destination_projection =
					projection /
					vertices[output].scaled_inverse_depth;
				float uv_t = (destination_projection -
					      current_projection) /
					     (previous_projection -
					      current_projection);
				vertices[output].u = current_u - delta_u * uv_t;
				vertices[output].v = current_v - delta_v * uv_t;
			} else {
				vertices[output].u =
					current_u - delta_u * current_x;
				vertices[output].v =
					current_v - delta_v * current_x;
			}
		}
		vertices[output].x = (float)g_flight_vp_width;
		g_clip_idx_a[g_clip_count_a++] = output;
	} else {
		g_clip_idx_a[g_clip_count_a++] = cur_vert_index;
	}
}

/* Clips one edge against the plane at view depth 1 and appends to g_clip_idx_a
 * as the other clips do. A vertex is behind the plane when its
 * scaled_inverse_depth is negative (depth minus 1) and its x and y are then in
 * view space. The new vertex goes at g_clip_vert_cursor, which it advances, at
 * depth 1: x, y, light, u and v are interpolated in view space, then x and y
 * projected with scaled_inverse_depth set to g_proj_scale_int. Returns nothing. */
// FUNCTION: XVT 0x40AC80
void render_clip_clip_poly_near(int prev_vert_index, int cur_vert_index,
				struct render_clip_vertex *vertices)
{
	struct render_clip_vertex *previous = &vertices[prev_vert_index];
	float previous_scaled_inverse_depth = previous->scaled_inverse_depth;
	struct render_clip_vertex *current = &vertices[cur_vert_index];
	float current_scaled_inverse_depth = current->scaled_inverse_depth;
	int output;

	if (previous_scaled_inverse_depth < 0.0f) {
		if (current_scaled_inverse_depth < 0.0f) {
			return;
		}
		output = g_clip_vert_cursor++;
		float previous_x = previous->x;
		float previous_y = previous->y;
		float previous_light_intensity = previous->light_intensity;
		float previous_u = previous->u;
		float current_depth = (float)(unsigned int)g_proj_scale_int /
				      current_scaled_inverse_depth;
		float previous_v = previous->v;
		float current_x =
			(current->x - (float)(g_flight_vp_width >> 1)) *
			current_depth;
		/* From here current_x is the edge's x difference (current vertex minus previous). */
		current_x = current_x * g_inv_proj_scale - previous_x;
		float current_y =
			(current->y -
			 (float)(g_proj_offset_y + (g_flight_vp_height >> 1))) *
			current_depth;
		/* From here current_y is the edge's y difference (current vertex minus previous). */
		current_y = current_y * g_inv_proj_scale - previous_y;
		float delta_u = current->u - previous_u;
		float delta_v = current->v - previous_v;
		float t = current_depth - previous_scaled_inverse_depth;
		t = previous_scaled_inverse_depth / (t - g_render_unit_float);
		vertices[output].light_intensity =
			previous_light_intensity -
			(current->light_intensity - previous_light_intensity) *
				t;
		vertices[output].x = previous_x - current_x * t;
		vertices[output].y = previous_y - current_y * t;
		vertices[output].v = previous_v - t * delta_v;
		vertices[output].u = previous_u - delta_u * t;
		float projection = (float)(unsigned int)g_proj_scale_int;
		vertices[output].scaled_inverse_depth = projection;
		vertices[output].x = vertices[output].x * projection;
		vertices[output].y = projection * vertices[output].y;
		vertices[output].x =
			(float)(g_flight_vp_width >> 1) + vertices[output].x;
		vertices[output].y =
			(float)(g_proj_offset_y + (g_flight_vp_height >> 1)) +
			vertices[output].y;
		g_clip_idx_a[g_clip_count_a++] = output;
		g_clip_idx_a[g_clip_count_a++] = cur_vert_index;
	} else if (current_scaled_inverse_depth < 0.0f) {
		output = g_clip_vert_cursor++;
		float current_x = current->x;
		float previous_depth = (float)(unsigned int)g_proj_scale_int /
				       previous_scaled_inverse_depth;
		float current_y = current->y;
		float current_light_intensity = current->light_intensity;
		float current_u = current->u;
		float current_v = current->v;
		float previous_x =
			(previous->x - (float)(g_flight_vp_width >> 1)) *
			previous_depth;
		/* From here previous_x is the edge's x difference (current vertex minus previous). */
		previous_x = current_x - previous_x * g_inv_proj_scale;
		float previous_y =
			(previous->y -
			 (float)(g_proj_offset_y + (g_flight_vp_height >> 1))) *
			previous_depth;
		/* From here previous_y is the edge's y difference (current vertex minus previous). */
		previous_y = current_y - previous_y * g_inv_proj_scale;
		float delta_u = current_u - previous->u;
		float delta_v = current_v - previous->v;
		float t = current_scaled_inverse_depth /
			  (current_scaled_inverse_depth - previous_depth +
			   g_render_unit_float);
		vertices[output].light_intensity =
			current_light_intensity -
			(current_light_intensity - previous->light_intensity) *
				t;
		vertices[output].x = current_x - previous_x * t;
		vertices[output].y = current_y - previous_y * t;
		vertices[output].u = current_u - delta_u * t;
		vertices[output].v = current_v - t * delta_v;
		float projection = (float)(unsigned int)g_proj_scale_int;
		vertices[output].scaled_inverse_depth = projection;
		vertices[output].x = vertices[output].x * projection;
		vertices[output].y = projection * vertices[output].y;
		vertices[output].x =
			(float)(g_flight_vp_width >> 1) + vertices[output].x;
		vertices[output].y =
			(float)(g_proj_offset_y + (g_flight_vp_height >> 1)) +
			vertices[output].y;
		g_clip_idx_a[g_clip_count_a++] = output;
	} else {
		g_clip_idx_a[g_clip_count_a++] = cur_vert_index;
	}
}
