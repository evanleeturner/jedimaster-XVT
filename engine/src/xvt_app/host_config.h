#ifndef XVT_APP_HOST_CONFIG_H
#define XVT_APP_HOST_CONFIG_H

#include "aeron/aeron.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XvtLaunchOptions {
	const char* resource_root;
	const char* game_data;
	const char* import_config;
	const char* import_pilot;
	const char* pilot_name;
	int show_help;
	int setup;
	int save_config;
	int reset_config;
	int check_installation;
	int skip_intro;
} XvtLaunchOptions;

/* Launch strings borrow argv for the lifetime of the application. */
int XvtLaunchOptions_Parse(int argc, char* argv[], XvtLaunchOptions* options);
void XvtHostConfig_InitAeron(const XvtLaunchOptions* options, AeronConfig* config);
int XvtHostConfig_ResolveResourceRoot(const XvtLaunchOptions* options, char* out, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
