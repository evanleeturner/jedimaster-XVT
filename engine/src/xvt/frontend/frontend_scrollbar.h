#ifndef XVT_FRONTEND_FRONTEND_SCROLLBAR_H
#define XVT_FRONTEND_FRONTEND_SCROLLBAR_H

#include <stdint.h>

#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

int frontend_scrollbar_save_state(void);
int frontend_scrollbar_restore_state(void);
extern int g_scrollbar_repeat_countdown;
extern int g_scrollbar_repeat_interval;
int frontend_scrollbar_draw(const struct RECT *bar_rect, int current_value,
			    int maximum_exclusive, int minimum, int page_step,
			    unsigned int color, int control_id);

#ifdef __cplusplus
}
#endif

#endif
