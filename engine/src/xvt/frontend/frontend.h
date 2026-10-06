#ifndef XVT_FRONTEND_FRONTEND_H
#define XVT_FRONTEND_FRONTEND_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int g_scrollable_control_count;
extern int g_scrollable_control_count_saved;
extern int g_scrollable_control_ids[32];
extern int g_scrollable_control_ids_saved[32];
extern int g_color_pale_cyan;
extern int g_color_teal;
extern int g_color_green;
extern int g_color_blue;
extern int g_color_red;
extern int g_color_yellow;
extern int g_color_green2;
extern int g_frontend_first_visible_line;
extern int g_color_gray;
extern int g_color_navy;
extern int g_color_red2;
extern int g_color_navy2;
extern int g_color_blue2;
extern int g_color_yellow2;
extern int g_color_violet;
extern int g_color_spring_green;
extern int g_color_muted_green2;
extern int g_color_cyan;
extern int g_color_azure;
extern int g_color_orange;
extern int g_pulse_color_ramp[12];
extern int g_pilot_record_pages_need_rebuild;
extern int g_editable_field_background_color;
extern char g_frontend_scratch_buffer[256];
extern char g_mission_sequence_description[256];
extern int g_host_cd_available;
extern int g_cd_audio_warning_pending;
extern int g_skip_movie_checks;

int frontend_load_resources(void);
int frontend_handle_common_screen_controls(int screen_context);
int frontend_format_seconds_to_clock_string(unsigned int seconds);
int error_text_load_line(int line_index, char *out_text);
int frontend_check_host_cd_present(void);
int frontend_is_scrollable_control_focused(int control_id);
int frontend_register_scrollable_control(int control_id);
int frontend_unregister_scrollable_control(int control_id);
int frontend_cycle_scrollable_focus(void);
int frontend_reset_scrollable_controls(void);

#ifdef __cplusplus
}
#endif

#endif
