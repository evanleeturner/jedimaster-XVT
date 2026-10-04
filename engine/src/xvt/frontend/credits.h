#ifndef XVT_FRONTEND_CREDITS_H
#define XVT_FRONTEND_CREDITS_H

#include <stdint.h>
#include <stdio.h>

#include "xvt/assets/file.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint16_t g_credits_current_text_color;
extern xvt_file *g_frontend_credits_file;
extern int g_credits_logo_x[2];
extern int g_credits_prev_logo_x[2];
extern int g_credits_logo_y[2];
extern int g_credits_prev_logo_y[2];
extern int g_credits_logo_id[2];
extern int g_credits_prev_logo_id[2];
extern int g_credits_has_more_pages;
extern int g_credits_page_index;
extern int g_credits_page_end_frame;
extern int g_credits_text_fade_frames;
extern int g_credits_text_y[2];
extern int g_credits_text_x[2];
extern unsigned int g_credits_buffer_idx;
extern char g_credits_text_lines[2][32][256];
extern uint16_t g_credits_text_colors[2][32];
extern int g_credits_exit_pending;

int credits_load_screen_resources(void);
int credits_parse_next_page(unsigned int *out_buffer_idx,
			    int *out_has_more_pages,
			    int *out_page_duration_frames,
			    int *out_text_fade_frames);
int credits_parse_text_line(const char *line, unsigned int buffer_idx,
			    unsigned int line_idx);

#ifdef __cplusplus
}
#endif

#endif
