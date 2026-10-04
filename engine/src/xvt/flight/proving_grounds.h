#ifndef XVT_FLIGHT_PROVING_GROUNDS_H
#define XVT_FLIGHT_PROVING_GROUNDS_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum proving_grounds_status_label_id {
	PROVING_STATUS_LEVEL = 0x0,
	PROVING_STATUS_SEGMENTS_LEFT = 0x1,
	PROVING_STATUS_SEGMENTS_DONE = 0x2,
	PROVING_STATUS_TARGETS_HIT = 0x3,
	PROVING_STATUS_SCORE = 0x4,
} proving_grounds_status_label_id;

extern int g_proving_grounds_score_decimal_divisors[9];
extern const char *g_proving_grounds_status_labels[5];
extern int16_t g_proving_grounds_local_player_roll_history[4];
extern int16_t g_proving_grounds_local_player_pitch_history[4];
extern int16_t g_proving_grounds_local_player_yaw_history[4];
extern int g_proving_grounds_local_player_world_z_history[4];
extern int g_proving_grounds_local_player_world_x_history[4];
extern int g_proving_grounds_local_player_world_y_history[4];
extern uint16_t g_proving_grounds_current_checkpoint_obj_idx;

void proving_grounds_record_local_player_pose_history(void);
void proving_grounds_draw_course_object(uint16_t object_index);
void proving_grounds_init_course_objects(void);
void proving_grounds_start_level(uint16_t level);
void proving_grounds_update_course(void);
int proving_grounds_has_player_crossed_checkpoint(uint16_t checkpoint_obj_idx);
void proving_grounds_draw_status_panel(int16_t x, int16_t y);
void proving_grounds_draw_score_decimal(int score, unsigned int width,
					unsigned int min_digits);
void proving_grounds_render_time_bonus_frame(void);

#ifdef __cplusplus
}
#endif

#endif
