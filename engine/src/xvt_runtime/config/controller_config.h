#ifndef XVT_RUNTIME_CONFIG_CONTROLLER_CONFIG_H
#define XVT_RUNTIME_CONFIG_CONTROLLER_CONFIG_H
#include "aeron/config_file.h"
#include "xvt_runtime/input/controller_options.h"
bool XvtControllerConfig_Parse(const AeronConfigFile* document, XvtControllerOptions* out, char* error,
							   size_t capacity);
bool XvtControllerConfig_Write(AeronConfigFile* document, const XvtControllerOptions* options,
							   AeronConfigError* error);
bool XvtControllerConfig_ReadProfile(const AeronConfigFile* document, const char* path,
									 AeronControllerKind kind, XvtControllerProfile* profile, char* error,
									 size_t capacity);
#endif
