#ifndef XVT_FRONTEND_FRONTEND_BOOTSTRAP_H
#define XVT_FRONTEND_FRONTEND_BOOTSTRAP_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int frontend_bootstrap_exit_intro_and_load_credits(int frame_counter);
int frontend_bootstrap_play_opening_and_enter_credits(int frame_counter);
int frontend_bootstrap_init_mode(void);
int frontend_bootstrap_exit_credits_and_load_frontend(int frame_counter);

#ifdef __cplusplus
}
#endif

#endif
