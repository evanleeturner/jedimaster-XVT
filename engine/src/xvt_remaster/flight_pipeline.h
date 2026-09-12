#ifndef XVT_REMASTER_FLIGHT_PIPELINE_H
#define XVT_REMASTER_FLIGHT_PIPELINE_H
#include "xvt_remaster/flight.h"
#ifdef __cplusplus
extern "C" {
#endif
void XvtFlightPipeline_Post(AeronScene3D* scene, float shutter, int motion);
int XvtFlightPipeline_Begin(AeronScene3D* scene, const XvtRenderSnapshot* snapshot,
							const XvtPreparedFlight* frame, int reset);
int XvtFlightPipeline_Finish(AeronCommandBuffer* cmd, AeronScene3D* scene);
int XvtFlightPipeline_Resolve(AeronCommandBuffer* cmd, AeronTexture* color, int width, int height, int bloom);
AeronTexture* XvtFlightPipeline_Output(void);
void XvtFlightPipeline_Shutdown(void);
int XvtFlightPipeline_SetDirect(int enabled, int width, int height);
int XvtFlightPipeline_Retain(AeronCommandBuffer* cmd);
int XvtFlightPipeline_SubmitDirect(void);
int XvtFlightPipeline_NeedsRetain(void);
void XvtFlightPipeline_ForgetSources(void);
int XvtFlightPipeline_DrawStandaloneHud(AeronCommandBuffer* cmd, int width, int height);
int XvtFlightPipeline_PrepareSceneResources(AeronScene3D* scene, int flight);
#ifdef __cplusplus
}
#endif
#endif
