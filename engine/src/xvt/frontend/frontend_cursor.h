#ifndef XVT_FRONTEND_FRONTEND_CURSOR_H
#define XVT_FRONTEND_FRONTEND_CURSOR_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t g_default_cursor_bitmap[100];

int frontend_cursor_set_image_from_resource_name(const char *resource_name,
						 void *save_buf);
void frontend_cursor_init(void);
void frontend_cursor_draw(void);
void frontend_cursor_restore(void);
int *frontend_cursor_get_pos(int *out_x, int *out_y);
int frontend_cursor_set_pos(int x, int y);
void frontend_cursor_show(void);
void frontend_cursor_hide(void);
int frontend_cursor_is_visible(void);
int frontend_cursor_get_dimensions(int *out_width, int *out_height);
int frontend_cursor_hide_os_cursor(void);
int frontend_cursor_show_os_cursor(void);

#ifdef __cplusplus
}
#endif

#endif
