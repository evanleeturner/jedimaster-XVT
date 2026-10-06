#ifndef XVT_FRONTEND_FRONTEND_SCREEN_H
#define XVT_FRONTEND_FRONTEND_SCREEN_H

#include <stdint.h>

#include "xvt/frontend/front_image.h"
#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*frontend_screen_update_fn)(int);

typedef int (*frontend_screen_exit_fn)(int);

enum { FRONTEND_SCREEN_MAX_STACK = 10 };

struct frontend_screen_state {
	/* The pixels under saved_rect, copied by frontend_screen_push_state when
	 * the next screen was pushed over this one and drawn back by
	 * frontend_screen_pop_state: at 8 bits per pixel RLE-compressed when
	 * front_image_compress_rle succeeds, else raw; raw at 16. 0 by 0 with no
	 * data when that push had no rect. */
	struct image_resource saved_image;
	/* The rect of the screen pushed over this one, clamped to 0 to 639 by 0
	 * to 479. */
	struct RECT saved_rect;
	/* Clip left edge before the push. frontend_screen_pop_state reads these
	 * four fields as one RECT to restore the clip. */
	int saved_clip_min_x;
	int saved_clip_min_y; /* Clip top edge before the push. */
	int saved_clip_max_x; /* Clip right edge before the push. */
	int saved_clip_max_y; /* Clip bottom edge before the push. */
	/* Called by the frame loop each frame while this screen is on top, with
	 * g_front_state.frame_counter. Returning 1 calls exit_fn, then ends a
	 * modal screen, or the program when the main loop ran it. */
	frontend_screen_update_fn update_fn;
	/* Called after an update that returned 1, or one during which
	 * frontend_screen_set_callbacks ran. */
	frontend_screen_exit_fn exit_fn;
	/* g_front_state.frame_counter before the push, restored by the pop. */
	int saved_frame_counter;
};

void frontend_screen_set_callbacks(frontend_screen_update_fn update_fn,
				   frontend_screen_exit_fn exit_fn);
int frontend_screen_queue_push(int (*update_fn)(int),
			       const struct RECT *screen_rect);
int frontend_screen_push_state(frontend_screen_update_fn update_fn,
			       struct RECT *screen_rect);
void frontend_screen_pop_state(void);

#ifdef __cplusplus
}
#endif

#endif
