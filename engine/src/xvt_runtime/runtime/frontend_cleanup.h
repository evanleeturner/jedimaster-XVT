#ifndef XVT_RUNTIME_FRONTEND_CLEANUP_H
#define XVT_RUNTIME_FRONTEND_CLEANUP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Screen exit callbacks with the frontend_screen_exit_fn signature. The 1997
 * screen code casts the wrapped functions, which take no argument, to that
 * type; these wrappers ignore frame and return the wrapped function's
 * result. */

/* frontend_mission_list_free_screen_resources_and_clear_input_gate. */
int xvt_frontend_cleanup_mission_resources(int frame);
/* mission_setup_exit_current_mission. */
int xvt_frontend_cleanup_current_mission(int frame);
/* mission_setup_exit_next_mission. */
int xvt_frontend_cleanup_next_mission(int frame);
/* mission_setup_battle_choice_exit. */
int xvt_frontend_cleanup_battle_choice(int frame);

#ifdef __cplusplus
}
#endif

#endif
