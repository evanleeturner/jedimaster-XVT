#ifndef XVT_FRONTEND_BRIEFING_TEXT_H
#define XVT_FRONTEND_BRIEFING_TEXT_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern char *g_briefing_map_label_texts[32];
extern char *g_briefing_text_blocks[32];
extern char *g_briefing_unused_buffers[20];
extern char *g_mission_text;

int16_t briefing_text_free_allocated_buffers_exit(void);
void briefing_text_free_allocated_buffers(void);

#ifdef __cplusplus
}
#endif

#endif
