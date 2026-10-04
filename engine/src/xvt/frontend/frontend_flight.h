#ifndef XVT_FRONTEND_FRONTEND_FLIGHT_H
#define XVT_FRONTEND_FRONTEND_FLIGHT_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int g_flight_loading_ready_screen_start_ms;
extern int g_flight_loading_ready_screen_now_ms;
extern int g_unused_flight_loading_ready_screen_flag;
extern int g_frontend_launch_human_player_count;
extern char g_frontend_flight_command_line[256];

int flight_loading_update_ready_screen(int frame_counter);
int frontend_flight_no_op_exit(int frame_counter);
int frontend_flight_launch_session(int frame_counter);

#ifdef __cplusplus
}
#endif

#endif
