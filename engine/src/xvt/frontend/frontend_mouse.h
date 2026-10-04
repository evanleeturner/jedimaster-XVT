#ifndef XVT_FRONTEND_FRONTEND_MOUSE_H
#define XVT_FRONTEND_FRONTEND_MOUSE_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

int frontend_mouse_set_input_gate(int gate_id);
int frontend_mouse_clear_input_gate(void);
int frontend_mouse_get_left_down(void);
int frontend_mouse_get_right_down(void);
int frontend_mouse_get_left_click(void);
int frontend_mouse_get_right_click(void);
int frontend_mouse_get_left_click_for(int gate_id);
int frontend_mouse_get_right_click_for(int gate_id);
int frontend_mouse_is_gate_owner(int gate_id);
int frontend_mouse_is_gate_open(void);
int frontend_mouse_clear_clicks(void);

#ifdef __cplusplus
}
#endif

#endif
