#include "xvt/render/render_list.h"

#include "xvt/flight/flight_view.h"
#include "xvt/flight/object/object.h"
#include "xvt/flight/player/player.h"
#include "xvt/flight/transfm2.h"
#include "xvt_runtime/log/log_both_builds.h"

/* Entries queued in g_render_object_list_entries since the last render_list_reset,
 * 0 to 296; only render_list_queue_object and render_list_reset write it. */
// GLOBAL: XVT 0x9A7B5C
static int g_render_object_list_count;
/* First entry of the render list, linked through each entry's next; NULL when
 * the list is empty. render_list_queue_object puts each new entry first, the two
 * sorts reorder the list and render_list_reset sets NULL;
 * flight_map_draw_object_pass walks the list by moving it on and puts it back
 * after. */
// GLOBAL: XVT 0x9A8C1C
struct render_object_list_entry *g_render_list_head;
/* Storage for the render list, 296 entries (RENDER_OBJECT_LIST_CAPACITY) used
 * in the order they are queued; fe_disk_io_init_global_buffers and
 * fe_disk_io_lock_global_buffers lock it from its memory handle. */
// GLOBAL: XVT 0x9EC5F8
struct render_object_list_entry *g_render_object_list_entries = 0;

/* Adds an object to the front of the render list with its sort depth, taking
 * the next of the 296 entries. Does nothing when all 296 are used. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4362A0
void render_list_queue_object(int object_idx, int sort_depth)
{
	if (g_render_object_list_count < 296) {
		g_render_object_list_entries[g_render_object_list_count]
			.sort_depth = sort_depth;
		g_render_object_list_entries[g_render_object_list_count]
			.object_idx = object_idx;
		g_render_object_list_entries[g_render_object_list_count].next =
			g_render_list_head;
		g_render_list_head = &g_render_object_list_entries
					     [g_render_object_list_count];
		++g_render_object_list_count;
	} else {
		XVT_LOG_DEBUG("render.list_full object=%d depth=%d", object_idx,
			      sort_depth);
	}
}

/* Empties the render list: g_render_object_list_count to 0 and g_render_list_head to
 * NULL. */
// FUNCTION: XVT 0x436310
void render_list_reset(void)
{
	g_render_object_list_count = 0;
	g_render_list_head = 0;
}

/* Tells whether an object's bounds can be in the view of player
 * player_idx's camera. Stores the object's offset from the camera in
 * g_cam_rel_world_x, Y and Z, its view depth in g_view_space_depth and its view
 * X in g_view_space_x, and its view Y in g_view_space_y once the X test
 * passes. With far = depth + bounds_radius and r the larger of bounds_radius
 * and far >> 4, it returns 0 when far is negative, when the size of view X
 * less r exceeds far, or when the size of view Y less r does; else 1. */
// FUNCTION: XVT 0x436470
int render_list_project_object_bounds_for_culling(int object_idx,
						  unsigned int bounds_radius,
						  int player_idx)
{
	struct object_record *object = &g_object_table[object_idx];
	int camera_world_y = g_players[player_idx].view_state.camera_world_y;
	g_cam_rel_world_x = object->world_x -
			    g_players[player_idx].view_state.camera_world_x;
	int camera_world_z = g_players[player_idx].view_state.camera_world_z;
	g_cam_rel_world_y = object->world_y - camera_world_y;
	g_cam_rel_world_z = object->world_z - camera_world_z;
	g_view_space_depth = transfm2_cam_mat_dot_row2(
		g_cam_rel_world_x, g_cam_rel_world_y, g_cam_rel_world_z);
	int cull_radius = (int)bounds_radius;
	int far_z = (int)((unsigned int)g_view_space_depth +
			  (unsigned int)cull_radius);
	if (far_z < 0) {
		return 0;
	}
	if ((unsigned int)(far_z >> 4) > (unsigned int)cull_radius) {
		cull_radius = far_z >> 4;
	}

	g_view_space_x = transfm2_cam_mat_dot_row0(
		g_cam_rel_world_x, g_cam_rel_world_y, g_cam_rel_world_z);
	int abs_view_coord = g_view_space_x;
	if (abs_view_coord < 0) {
		abs_view_coord = (int)(0U - (unsigned int)abs_view_coord);
	}
	abs_view_coord =
		(int)((unsigned int)abs_view_coord - (unsigned int)cull_radius);
	if (far_z < abs_view_coord) {
		return 0;
	}

	g_view_space_y = transfm2_cam_mat_dot_row1(
		g_cam_rel_world_x, g_cam_rel_world_y, g_cam_rel_world_z);
	abs_view_coord = g_view_space_y;
	if (abs_view_coord < 0) {
		abs_view_coord = (int)(0U - (unsigned int)abs_view_coord);
	}
	abs_view_coord =
		(int)((unsigned int)abs_view_coord - (unsigned int)cull_radius);
	return far_z >= abs_view_coord;
}

/* Sorts the render list by sort_depth, largest first, merging runs of 1, 2, 4
 * and so on in place; entries of equal depth keep their order. Only
 * flight_map_render_view calls it. */
// FUNCTION: XVT 0x436580
void render_list_sort_depth_descending(void)
{
	struct render_object_list_entry *left_tail;

	int object_count = g_render_object_list_count;
	int run_length = 1;
	if (object_count > run_length) {
		do {
			struct render_object_list_entry *right =
				g_render_list_head;
			struct render_object_list_entry *previous = 0;
			struct render_object_list_entry *left =
				g_render_list_head;
			int processed_count = 0;
			while (processed_count < g_render_object_list_count) {
				int left_run_count = 0;
				while (left_run_count < run_length &&
				       right != 0) {
					left_tail = right;
					++left_run_count;
					right = right->next;
				}
				if (right == 0) {
					break;
				}

				int right_run_count = 0;
				while (right_run_count < run_length) {
					int right_depth = right->sort_depth;
					int left_depth = left->sort_depth;
					while (left_depth >= right_depth) {
						previous = left;
						left = left->next;
						if (left_tail == previous) {
							break;
						}
						left_depth = left->sort_depth;
					}
					if (left_tail == previous) {
						break;
					}

					left_tail->next = right->next;
					if (previous != 0) {
						previous->next = right;
						previous = right;
						right->next = left;
					} else {
						g_render_list_head = right;
						right->next = left;
						previous = g_render_list_head;
					}
					right = left_tail->next;
					if (right == 0) {
						break;
					}
					++right_run_count;
				}

				if (left_tail == previous) {
					while (right_run_count < run_length &&
					       right != 0) {
						left_tail = right;
						++right_run_count;
						right = right->next;
					}
				}
				left = right;
				previous = left_tail;
				if (right == 0) {
					break;
				}
				processed_count += 2 * run_length;
			}
			run_length *= 2;
			object_count = g_render_object_list_count;
		} while (object_count > run_length);
	}
}

/* Sorts the render list by sort_depth, smallest first, the same way as
 * render_list_sort_depth_descending. Only flight_view_render calls it. */
// FUNCTION: XVT 0x436680
void render_list_sort_depth_ascending(void)
{
	struct render_object_list_entry *left_tail;

	for (int run_length = 1; run_length < g_render_object_list_count;
	     run_length *= 2) {
		struct render_object_list_entry *right = g_render_list_head;
		struct render_object_list_entry *previous = 0;
		struct render_object_list_entry *left = g_render_list_head;
		int processed_count = 0;
		while (processed_count < g_render_object_list_count) {
			int left_run_count = 0;
			while (left_run_count < run_length && right != 0) {
				left_tail = right;
				++left_run_count;
				right = right->next;
			}
			if (right == 0) {
				break;
			}

			int right_run_count = 0;
			while (right_run_count < run_length) {
				while (left->sort_depth <= right->sort_depth) {
					previous = left;
					left = left->next;
					if (left_tail == previous) {
						break;
					}
				}
				if (left_tail == previous) {
					break;
				}

				left_tail->next = right->next;
				if (previous != 0) {
					previous->next = right;
					previous = right;
					right->next = left;
				} else {
					g_render_list_head = right;
					right->next = left;
					previous = g_render_list_head;
				}
				right = left_tail->next;
				if (right == 0) {
					break;
				}
				++right_run_count;
			}

			if (left_tail == previous) {
				while (right_run_count < run_length &&
				       right != 0) {
					left_tail = right;
					++right_run_count;
					right = right->next;
				}
			}
			left = right;
			previous = left_tail;
			if (right == 0) {
				break;
			}
			processed_count += 2 * run_length;
		}
	}
}
