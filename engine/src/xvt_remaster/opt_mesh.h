#ifndef XVT_REMASTER_OPT_MESH_H
#define XVT_REMASTER_OPT_MESH_H
#include "aeron/asset/flight_model.h"
#include "aeron/vfs.h"
#include "xvt_runtime/config/settings.h"

#ifdef __cplusplus
extern "C" {
#endif

bool XvtRemasterOptMesh_Init(AeronVfs* vfs, char* error, size_t capacity);
void XvtRemasterOptMesh_Shutdown(void);
bool XvtRemasterOptMesh_Build(AeronVfs* vfs, const char* resolved_path, const XvtModelSettings* settings,
							  AeronFlightModel* out, char* error, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
