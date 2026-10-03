#include "xvt/frontend/frontend_state.h"

/* The frontend's state, the one FrontendGlobalState: display, input, sound, CD
 * music, fonts, screens, the lobby's network session, strings and install
 * paths. Cleared whole by FrontendDisplay_Init when the original build's
 * display starts, and by XvtFrontendTask_Init in the modern build. Many
 * functions write it. */
// GLOBAL: XVT 0xAA6D00
struct FrontendGlobalState g_frontState = {0};
