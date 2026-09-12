#ifndef XVT_REMASTER_HYPERSPACE_H
#define XVT_REMASTER_HYPERSPACE_H
#include "xvt_remaster/flight.h"
#ifdef __cplusplus
extern "C" {
#endif
int XvtHyperspace_Prepare(AeronCommandBuffer* cmd, const XvtRenderSnapshot* snapshot, AeronScene3D* scene,
						  const XvtRenderView* view);
void XvtHyperspace_Draw(AeronCommandBuffer* cmd, AeronRenderPass* pass, int width, int height, void* user);
void XvtHyperspace_Shutdown(void);

typedef struct XvtHyperLighting {
	AeronTexture* texture;
	AeronSampler* sampler;
	float direction[3], color[3];
} XvtHyperLighting;

/* Only the scene prepared for this tunnel may consume its environment. */
int XvtHyperspace_Lighting(AeronScene3D* scene, XvtHyperLighting* out);
#ifdef __cplusplus
}
#endif
#endif
