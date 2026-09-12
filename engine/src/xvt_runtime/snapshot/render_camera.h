#ifndef XVT_RUNTIME_RENDER_CAMERA_H
#define XVT_RUNTIME_RENDER_CAMERA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mirror a completed FVIEW camera in double precision, tagged with its Q15 rows. */
void XvtRenderCamera_Build(int16_t roll, int16_t pitch, int16_t yaw, int16_t angle_d, int16_t aim_x,
						   int16_t aim_y);
void XvtRenderCamera_CopyRows(float rows[9]);
/* Match the original single saved flight viewport. */
void XvtRenderCamera_SaveViewport(void);
void XvtRenderCamera_RestoreViewport(void);

#ifdef __cplusplus
}
#endif
#endif
