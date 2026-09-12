#ifndef XVT_REMASTER_SKY_STARS_H
#define XVT_REMASTER_SKY_STARS_H

#include "aeron/scene/scene3d.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XvtRemasterSkyStars XvtRemasterSkyStars;

typedef struct XvtRemasterSkyStarsParams {
	float exposure;
	float brightness;
	float classic_pixel_scale;
	uint16_t density_divisor;
} XvtRemasterSkyStarsParams;

XvtRemasterSkyStars* XvtRemasterSkyStars_Create(void);
void XvtRemasterSkyStars_Destroy(XvtRemasterSkyStars* stars);

/* Captures the current scene transform after AeronScene_Begin. The public
 * jittered view-projection keeps stars aligned with temporally jittered meshes. */
int XvtRemasterSkyStars_Prepare(XvtRemasterSkyStars* stars, AeronCommandBuffer* cmd,
								const AeronScene3D* scene, const XvtRemasterSkyStarsParams* params);

/* AeronScene BEFORE_OPAQUE hook draw. */
void XvtRemasterSkyStars_Draw(AeronCommandBuffer* command_buffer, AeronRenderPass* render_pass, int rt_w,
							  int rt_h, void* user);

#ifdef __cplusplus
}
#endif

#endif /* XVT_REMASTER_SKY_STARS_H */
