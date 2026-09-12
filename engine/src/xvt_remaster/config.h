#ifndef XVT_REMASTER_CONFIG_H
#define XVT_REMASTER_CONFIG_H

#include "xvt_runtime/config/settings.h"
#include "xvt_runtime/config/video_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Apply a new requested generation at a host frame boundary. */
int XvtRemasterConfig_Sync(void);
bool XvtRemasterConfig_ApplyVideo(const XvtVideoSettings* previous, const XvtVideoSettings* requested,
								  char* error, size_t capacity);
const XvtRenderSettings* XvtRemasterConfig_Effective(void);
uint64_t XvtRemasterConfig_Generation(void);
/* Borrowed until the next settings boundary; bind on every scene begin. */
AeronSampler* XvtRemasterConfig_MeshSampler(void);
void XvtRemasterConfig_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
