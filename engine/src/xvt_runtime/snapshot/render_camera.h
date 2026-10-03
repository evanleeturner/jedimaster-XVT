#ifndef XVT_RUNTIME_RENDER_CAMERA_H
#define XVT_RUNTIME_RENDER_CAMERA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mirror a completed FVIEW camera in double precision, tagged with its Q15 rows. */
/* Call after FVIEW has built the Q15 camera rows; the tag is taken from them. */
void xvt_render_camera_build(int16_t roll, int16_t pitch, int16_t yaw,
			     int16_t up_axis_angle, int16_t aim_x,
			     int16_t aim_y);
/* Writes the camera rows: the double-precision basis while it is valid and the live Q15 rows
 * still match its tag, otherwise the live Q15 rows scaled by 1/32768. */
void xvt_render_camera_copy_rows(float rows[9]);
/* Match the original single saved flight viewport. */
/* Save copies the camera; a second Save overwrites the first. Restore brings the
 * copy back and drops its precise basis unless the live Q15 rows still match its tag. */
void xvt_render_camera_save_viewport(void);
void xvt_render_camera_restore_viewport(void);

#ifdef __cplusplus
}
#endif
#endif
