#ifndef XVT_APP_VIDEO_OPTIONS_H
#define XVT_APP_VIDEO_OPTIONS_H
#include "xvt_runtime/config/video_config.h"
typedef bool (*XvtVideoApplyFn)(const XvtVideoSettings* previous, const XvtVideoSettings* requested,
								char* error, size_t capacity);
void XvtVideoOptions_Configure(XvtVideoApplyFn apply);
void XvtVideoOptions_Get(XvtVideoSettings* out);
bool XvtVideoOptions_Request(const XvtVideoSettings* options, char* error, size_t capacity);
void XvtVideoOptions_RestoreDefaults(void);
bool XvtVideoOptions_ApplyPending(char* error, size_t capacity);
bool XvtVideoOptions_Flush(bool exiting, char* error, size_t capacity);
#endif
