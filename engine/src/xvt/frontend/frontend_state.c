#include "xvt/frontend/frontend_state.h"

/* The frontend's state, the one frontend_global_state: display, input, sound, CD
 * music, fonts, screens, the lobby's network session, strings and install
 * paths. Cleared whole by frontend_display_init when the original build's
 * display starts, and by xvt_frontend_task_init in the modern build. Many
 * functions write it. */
// GLOBAL: XVT 0xAA6D00
struct frontend_global_state g_front_state = {0};
