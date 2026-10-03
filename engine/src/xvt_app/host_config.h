#ifndef XVT_APP_HOST_CONFIG_H
#define XVT_APP_HOST_CONFIG_H

#include "aeron/aeron.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The command line and the host's Aeron configuration: the launch options parsed from argv, the config
 * Aeron starts with (names, the embedded window icon, the resource and shader folders, a 640x480 logical
 * size) and the resource root the windowless installation check resolves. No state. */

struct xvt_launch_options {
	const char *resource_root;
	const char *game_data;
	const char *import_config;
	const char *import_pilot;
	const char *pilot_name;
	const char *log_level;
	const char *log_file;
	int show_help;
	int setup;
	int save_config;
	int reset_config;
	int check_installation;
	int skip_intro;
};

/* Launch strings borrow argv for the lifetime of the application. */
/* Zeroes options, then reads argv from index 1. A flag is matched whole: --help or -h, --setup,
 * --skip-intro, --save-config, --reset-config, --check-installation; a repeat is harmless. A value option
 * (--resource-root, --game-data, --import-config, --import-pilot, --pilot-name, --log-level, --log-file)
 * takes its value after '=' or from the next argument; the value is kept as text, unchecked. Returns 0,
 * with a message on stderr, for an unknown argument, a value option given twice, or a value that is
 * missing, empty or starts with '-'; and after the loop when --pilot-name
 * lacks --import-pilot or --setup is combined with --game-data or --check-installation. On 0, options
 * holds what was parsed before the failure. */
int xvt_launch_options_parse(int argc, char *argv[],
			     struct xvt_launch_options *options);
/* Zeroes config and fills it for this host: organization "TotallyOpen", application and window title
 * "OpenXvT", the embedded window icon, the resource root from options (none for NULL options), the
 * "resources" and "shaders" folders, a 640x480 logical size presented aspect-fit, and an opaque black
 * clear color. Every other field stays zero. */
void xvt_host_config_fill_aeron_config(const struct xvt_launch_options *options,
				       AeronConfig *config);
/* With a nonempty resource root in options, copies it to out and returns 1, or 0 when it does not fit
 * with its terminator. Otherwise returns Aeron's answer for the "resources" folder beside the executable
 * or bundle resources, written to out. */
int xvt_host_config_resolve_resource_root(
	const struct xvt_launch_options *options, char *out, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
