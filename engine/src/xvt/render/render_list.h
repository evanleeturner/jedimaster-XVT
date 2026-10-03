#ifndef XVT_RENDER_RENDER_LIST_H
#define XVT_RENDER_RENDER_LIST_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct render_object_list_entry {
	int sort_depth; /* Depth the list is sorted by, given by the caller. */
	int object_idx; /* Object table index to draw. */
	/* Next entry in the list; NULL at the end. */
	struct render_object_list_entry *next;
};

extern struct render_object_list_entry *g_render_list_head;
extern struct render_object_list_entry *g_render_object_list_entries;

void render_list_queue_object(int object_idx, int sort_depth);
void render_list_reset(void);
int render_list_project_object_bounds_for_culling(int object_idx,
						  unsigned int bounds_radius,
						  int player_idx);
void render_list_sort_depth_descending(void);
void render_list_sort_depth_ascending(void);

#ifdef __cplusplus
}
#endif

#endif
