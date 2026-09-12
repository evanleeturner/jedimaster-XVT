#ifndef XVT_REMASTER_FLIGHT_MAP_H
#define XVT_REMASTER_FLIGHT_MAP_H
#include "xvt_remaster/flight.h"
#ifdef __cplusplus
extern "C" {
#endif
int XvtFlightMap_Render(AeronCommandBuffer* cmd, const XvtRenderSnapshot* snapshot,
						const XvtRenderView* view);
void XvtFlightMap_Shutdown(void);
int XvtFlightMap_PrepareResources(int width, int height);
#ifdef __cplusplus
}
#endif
#endif
