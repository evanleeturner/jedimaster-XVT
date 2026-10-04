#ifndef XVT_FRONTEND_FRONTEND_DIALOG_H
#define XVT_FRONTEND_FRONTEND_DIALOG_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern char g_front_dialog_line1_or_edit[256];
extern char g_front_dialog_line2[256];
extern char g_front_dialog_line3[256];
extern char g_front_dialog_okay_label[128];
extern char g_front_dialog_cancel_label[128];
extern int g_front_dialog_saved_mouse_x;
extern int g_front_dialog_saved_mouse_y;
extern int g_dialog_result;

int frontend_dialog_show_confirm_dialog(const char *line1, const char *line2,
					const char *line3,
					const char *okay_label,
					const char *cancel_label);
int frontend_dialog_confirm_update_callback(int frame_counter);
int frontend_dialog_has_network_dismiss_packet(void);
int frontend_dialog_prompt_for_pilot_name(char *out_name);
int frontend_dialog_create_pilot_name_callback(int frame_counter);
int frontend_dialog_show_network_abort_error(const char *line1,
					     const char *line2,
					     const char *line3,
					     const char *okay_label,
					     const char *cancel_label);
int frontend_dialog_network_abort_error_callback(int frame_counter);

#ifdef __cplusplus
}
#endif

#endif
