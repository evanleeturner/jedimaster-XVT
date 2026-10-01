#ifndef XVT_REMASTER_SKY_STARS_H
#define XVT_REMASTER_SKY_STARS_H

#include "aeron/scene/scene3d.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The original starfield as instanced sprites: 3,072 stars on three axis-aligned grid planes, positions
 * and shades drawn from the original's random tables with a private copy of the game's generator
 * (seed 0x2357), so renderer start-up never consumes simulation randomness. */
typedef struct XvtRemasterSkyStars XvtRemasterSkyStars;

/* density_divisor: the grid per plane is 32 / density_divisor stars on a side, so 1 is every star. */
typedef struct XvtRemasterSkyStarsParams {
	float exposure;
	float brightness;
	float classic_pixel_scale;
	uint16_t density_divisor;
} XvtRemasterSkyStarsParams;

/* Allocates the stars with their shaders and instance buffer and fills the position and shade tables
 * (sRGB shades 8 to 23 of 31, linearized). Returns NULL, logging an error, when a GPU resource fails. */
XvtRemasterSkyStars* XvtRemasterSkyStars_Create(void);
/* Releases everything; NULL is accepted. */
void XvtRemasterSkyStars_Destroy(XvtRemasterSkyStars* stars);

/* Captures the current scene transform after AeronScene_Begin. The public
 * jittered view-projection keeps stars aligned with temporally jittered meshes. */
/* Regenerates and uploads the instances when the density divisor changed, then sets the uniforms: the
 * view-projection, the pixel-to-clip scale from the render size, a half sprite size of half a classic
 * pixel scaled into the render target, and brightness = exposure * brightness. Returns 0 for a NULL
 * argument, a divisor of 0 or over 32, a classic pixel scale that is not positive, a scene without a
 * jittered view-projection or with a zero render or output size, or a failed upload. */
int XvtRemasterSkyStars_Prepare(XvtRemasterSkyStars* stars, AeronCommandBuffer* cmd,
								const AeronScene3D* scene, const XvtRemasterSkyStarsParams* params);

/* AeronScene BEFORE_OPAQUE hook draw. */
/* user is the stars. Does nothing for a NULL stars or pass, or before any instances exist. Creates the
 * pipeline for the pass's sample count on first use or a change (on failure marks the command buffer
 * failed and draws nothing), then draws six vertices per star, depth-tested without writing depth,
 * blended premultiplied. rt_w and rt_h are unused. */
void XvtRemasterSkyStars_Draw(AeronCommandBuffer* command_buffer, AeronRenderPass* render_pass, int rt_w,
							  int rt_h, void* user);

#ifdef __cplusplus
}
#endif

#endif /* XVT_REMASTER_SKY_STARS_H */
