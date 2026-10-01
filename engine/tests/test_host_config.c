/* Checks the command line parser and the host's Aeron configuration (xvt_app/host_config.h) against the
 * promises in its header. The module holds no state; each case builds its own argument list. Refused
 * command lines print a message on stderr, which this test does not read. */
#include "test_assert.h"
#include "xvt_app/host_config.h"
#include "xvt_app/window_icon.h"

#include <string.h>

/* Parses the arguments after the program name. The argument array lives until the calling function
 * returns, so a later check can compare the pointers the parser kept with the strings in g_argv. */
#define PARSE(options, ...) Parse((options), (char*[]) { "OpenXvT", __VA_ARGS__, NULL })

static char** g_argv;

/* Parses the NULL-terminated argv, keeping it in g_argv so a check can compare the borrowed pointers. */
static int Parse(XvtLaunchOptions* options, char** argv) {
	int argc = 0;
	while (argv[argc])
		++argc;
	g_argv = argv;
	return XvtLaunchOptions_Parse(argc, argv, options);
}

static void CheckZeroesOptions(void) {
	XvtLaunchOptions options;
	memset(&options, 0xFF, sizeof options);
	char* argv[] = { "OpenXvT", NULL };
	XVT_ASSERT_INT_EQ(XvtLaunchOptions_Parse(1, argv, &options), 1);
	XVT_ASSERT_TRUE(options.resource_root == NULL && options.game_data == NULL);
	XVT_ASSERT_TRUE(options.import_config == NULL && options.import_pilot == NULL);
	XVT_ASSERT_TRUE(options.pilot_name == NULL && options.log_level == NULL);
	XVT_ASSERT_TRUE(options.log_file == NULL);
	XVT_ASSERT_INT_EQ(options.show_help, 0);
	XVT_ASSERT_INT_EQ(options.setup, 0);
	XVT_ASSERT_INT_EQ(options.save_config, 0);
	XVT_ASSERT_INT_EQ(options.reset_config, 0);
	XVT_ASSERT_INT_EQ(options.check_installation, 0);
	XVT_ASSERT_INT_EQ(options.skip_intro, 0);
}

static void CheckFlags(void) {
	XvtLaunchOptions options;
	XVT_ASSERT_INT_EQ(PARSE(&options, "--help", "--skip-intro", "--save-config", "--reset-config",
							"--check-installation", "--skip-intro"),
					  1);
	XVT_ASSERT_INT_EQ(options.show_help, 1);
	XVT_ASSERT_INT_EQ(options.skip_intro, 1);
	XVT_ASSERT_INT_EQ(options.save_config, 1);
	XVT_ASSERT_INT_EQ(options.reset_config, 1);
	XVT_ASSERT_INT_EQ(options.check_installation, 1);
	XVT_ASSERT_INT_EQ(options.setup, 0);

	XVT_ASSERT_INT_EQ(PARSE(&options, "-h", "--setup"), 1);
	XVT_ASSERT_INT_EQ(options.show_help, 1);
	XVT_ASSERT_INT_EQ(options.setup, 1);

	/* A flag is matched whole. */
	XVT_ASSERT_INT_EQ(PARSE(&options, "--helpme"), 0);
	XVT_ASSERT_INT_EQ(PARSE(&options, "--setup=1"), 0);
	XVT_ASSERT_INT_EQ(PARSE(&options, "-H"), 0);
	XVT_ASSERT_INT_EQ(PARSE(&options, "setup"), 0);
}

static void CheckValues(void) {
	XvtLaunchOptions options;
	/* After '=' or as the next argument; kept as text, unchecked, borrowed from argv. */
	XVT_ASSERT_INT_EQ(PARSE(&options, "--resource-root=res", "--game-data", "data dir",
							"--import-config=x.cfg", "--import-pilot", "p.plt", "--pilot-name=Ace",
							"--log-level", "loud", "--log-file=runs/a.log"),
					  1);
	XVT_ASSERT_TRUE(options.resource_root == g_argv[1] + strlen("--resource-root="));
	XVT_ASSERT_TRUE(options.game_data == g_argv[3]);
	XVT_ASSERT_TRUE(options.import_config == g_argv[4] + strlen("--import-config="));
	XVT_ASSERT_TRUE(options.import_pilot == g_argv[6]);
	XVT_ASSERT_TRUE(options.pilot_name == g_argv[7] + strlen("--pilot-name="));
	XVT_ASSERT_TRUE(options.log_level == g_argv[9]);
	XVT_ASSERT_INT_EQ(strcmp(options.log_level, "loud"), 0);
	XVT_ASSERT_INT_EQ(strcmp(options.game_data, "data dir"), 0);
	XVT_ASSERT_TRUE(options.log_file == g_argv[10] + strlen("--log-file="));
	XVT_ASSERT_INT_EQ(PARSE(&options, "--log-file", "b.log"), 1);
	XVT_ASSERT_TRUE(options.log_file == g_argv[2]);
}

static void CheckValueRefusals(void) {
	XvtLaunchOptions options;
	/* Missing, empty, or starting with '-'. */
	XVT_ASSERT_INT_EQ(PARSE(&options, "--game-data"), 0);
	XVT_ASSERT_INT_EQ(PARSE(&options, "--game-data="), 0);
	XVT_ASSERT_INT_EQ(PARSE(&options, "--game-data", ""), 0);
	XVT_ASSERT_INT_EQ(PARSE(&options, "--game-data", "-x"), 0);
	XVT_ASSERT_INT_EQ(PARSE(&options, "--log-level=-1"), 0);
	XVT_ASSERT_INT_EQ(PARSE(&options, "--log-file"), 0);
	XVT_ASSERT_INT_EQ(PARSE(&options, "--log-file="), 0);
	XVT_ASSERT_INT_EQ(PARSE(&options, "--log-file=a.log", "--log-file=b.log"), 0);
	XVT_ASSERT_INT_EQ(PARSE(&options, "--import-pilot", "--setup"), 0);

	/* A value option given twice, in either spelling. */
	XVT_ASSERT_INT_EQ(PARSE(&options, "--log-level=info", "--log-level", "debug"), 0);
	XVT_ASSERT_TRUE(options.log_level == g_argv[1] + strlen("--log-level="));

	/* An unknown argument, including an unknown name with a value. */
	XVT_ASSERT_INT_EQ(PARSE(&options, "--game-dat=x"), 0);
	XVT_ASSERT_INT_EQ(PARSE(&options, "--unknown"), 0);

	/* On a refusal, what was parsed before it is kept. */
	XVT_ASSERT_INT_EQ(PARSE(&options, "--setup", "--resource-root=r", "--bogus", "--skip-intro"), 0);
	XVT_ASSERT_INT_EQ(options.setup, 1);
	XVT_ASSERT_TRUE(options.resource_root == g_argv[2] + strlen("--resource-root="));
	XVT_ASSERT_INT_EQ(options.skip_intro, 0);
}

static void CheckCombinationRefusals(void) {
	XvtLaunchOptions options;
	XVT_ASSERT_INT_EQ(PARSE(&options, "--pilot-name=Ace"), 0);
	XVT_ASSERT_INT_EQ(PARSE(&options, "--pilot-name=Ace", "--import-pilot=a.plt"), 1);
	XVT_ASSERT_INT_EQ(PARSE(&options, "--import-pilot=a.plt"), 1);

	/* The combination is judged after the whole line is read. */
	XVT_ASSERT_INT_EQ(PARSE(&options, "--setup", "--game-data=d"), 0);
	XVT_ASSERT_INT_EQ(options.setup, 1);
	XVT_ASSERT_TRUE(options.game_data != NULL);
	XVT_ASSERT_INT_EQ(PARSE(&options, "--check-installation", "--setup"), 0);
	XVT_ASSERT_INT_EQ(PARSE(&options, "--check-installation", "--game-data=d"), 1);
	XVT_ASSERT_INT_EQ(PARSE(&options, "--setup", "--resource-root=r", "--skip-intro"), 1);
}

static void CheckInitAeron(void) {
	XvtLaunchOptions options;
	XVT_ASSERT_INT_EQ(PARSE(&options, "--resource-root=res"), 1);
	AeronConfig config;
	memset(&config, 0xFF, sizeof config);
	XvtHostConfig_InitAeron(&options, &config);

	XVT_ASSERT_INT_EQ(strcmp(config.org_name, "TotallyOpen"), 0);
	XVT_ASSERT_INT_EQ(strcmp(config.app_name, "OpenXvT"), 0);
	XVT_ASSERT_INT_EQ(strcmp(config.window_title, "OpenXvT"), 0);
	XVT_ASSERT_INT_EQ(config.window_icon_bmp_size, sizeof xvt_window_icon_bmp);
	XVT_ASSERT_INT_EQ(memcmp(config.window_icon_bmp, xvt_window_icon_bmp, sizeof xvt_window_icon_bmp), 0);
	XVT_ASSERT_TRUE(config.resource_root == options.resource_root);
	XVT_ASSERT_INT_EQ(strcmp(config.resource_path, "resources"), 0);
	XVT_ASSERT_INT_EQ(strcmp(config.shader_path, "shaders"), 0);
	XVT_ASSERT_INT_EQ(config.logical_width, 640);
	XVT_ASSERT_INT_EQ(config.logical_height, 480);
	XVT_ASSERT_INT_EQ(config.presentation_mode, AERON_PRESENTATION_ASPECT_FIT);

	/* An opaque black clear color, switched on. Each channel is 0 or 1, which a float holds exactly. */
	XVT_ASSERT_TRUE(config.clear_color_enabled != 0);
	XVT_ASSERT_CLOSE(config.clear_color_rgba[0], 0.0, 0.0, "black has no red; 0 is exact");
	XVT_ASSERT_CLOSE(config.clear_color_rgba[1], 0.0, 0.0, "black has no green; 0 is exact");
	XVT_ASSERT_CLOSE(config.clear_color_rgba[2], 0.0, 0.0, "black has no blue; 0 is exact");
	XVT_ASSERT_CLOSE(config.clear_color_rgba[3], 1.0, 0.0, "opaque is full alpha; 1 is exact");

	/* Every other field stays zero. */
	XVT_ASSERT_TRUE(config.asset_root == NULL);
	XVT_ASSERT_INT_EQ(config.window_width, 0);
	XVT_ASSERT_INT_EQ(config.window_height, 0);

	/* NULL options: no resource root. */
	memset(&config, 0xFF, sizeof config);
	XvtHostConfig_InitAeron(NULL, &config);
	XVT_ASSERT_TRUE(config.resource_root == NULL);
	XVT_ASSERT_INT_EQ(strcmp(config.resource_path, "resources"), 0);
}

static void CheckResolveResourceRoot(void) {
	XvtLaunchOptions options;
	char out[64];
	XVT_ASSERT_INT_EQ(PARSE(&options, "--resource-root=abcd"), 1);
	XVT_ASSERT_INT_EQ(XvtHostConfig_ResolveResourceRoot(&options, out, sizeof out), 1);
	XVT_ASSERT_INT_EQ(strcmp(out, "abcd"), 0);

	/* "abcd" needs five bytes with its terminator. */
	XVT_ASSERT_INT_EQ(XvtHostConfig_ResolveResourceRoot(&options, out, 5), 1);
	XVT_ASSERT_INT_EQ(XvtHostConfig_ResolveResourceRoot(&options, out, 4), 0);

	/* Without a nonempty root, the answer is Aeron's for the "resources" folder. */
	char aeron[4096], resolved[4096];
	int expected = Aeron_ApplicationPath("resources", aeron, sizeof aeron);
	memset(resolved, 0, sizeof resolved);
	XVT_ASSERT_INT_EQ(XvtHostConfig_ResolveResourceRoot(NULL, resolved, sizeof resolved), expected);
	if (expected)
		XVT_ASSERT_INT_EQ(strcmp(resolved, aeron), 0);
	options.resource_root = "";
	memset(resolved, 0, sizeof resolved);
	XVT_ASSERT_INT_EQ(XvtHostConfig_ResolveResourceRoot(&options, resolved, sizeof resolved), expected);
	if (expected)
		XVT_ASSERT_INT_EQ(strcmp(resolved, aeron), 0);
}

int main(void) {
	CheckZeroesOptions();
	CheckFlags();
	CheckValues();
	CheckValueRefusals();
	CheckCombinationRefusals();
	CheckInitAeron();
	CheckResolveResourceRoot();
	return 0;
}
