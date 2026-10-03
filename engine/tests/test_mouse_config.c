/* Checks the mouse settings (xvt_runtime/config/mouse_config.h) against the promises in its header: the
 * parser on documents this file writes itself, and xvt_config_set_mouse on settings loaded from a copy of the
 * shipped defaults. Each check starts from a fresh fixture folder (config_fixture.h). */
#define _XOPEN_SOURCE 700

#include "config_fixture.h"
#include "test_assert.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/config/mouse_config.h"

#include <string.h>

/* Parses text's mouse settings into *options and returns what the parser returned. */
static bool parse_text(const char *text, struct xvt_mouse_options *options,
		       char *error, size_t capacity)
{
	AeronConfigFile *document = fixture_yaml(text);
	error[0] = 0;
	bool parsed =
		xvt_mouse_config_parse(document, options, error, capacity);
	AeronConfigFile_Destroy(document);
	return parsed;
}

static void expect_refused(const char *text)
{
	struct xvt_mouse_options options;
	char error[256];
	fixture_case(text);
	XVT_ASSERT_TRUE(!parse_text(text, &options, error, sizeof error));
	XVT_ASSERT_TRUE(error[0] != 0);
	fixture_case(NULL);
}

static void check_parse(void)
{
	fixture_begin();
	struct xvt_mouse_options options;
	char error[256];
	XVT_ASSERT_TRUE(parse_text(
		"input:\n  mouse_flight: true\n  mouse_sensitivity: 7\n  mouse_invert_y: false\n",
		&options, error, sizeof error));
	XVT_ASSERT_TRUE(options.mouse_flight_enabled != 0);
	XVT_ASSERT_INT_EQ(options.mouse_sensitivity, 7);
	XVT_ASSERT_INT_EQ(options.mouse_invert_y, 0);

	XVT_ASSERT_TRUE(parse_text(
		"input:\n  mouse_flight: false\n  mouse_sensitivity: 1\n  mouse_invert_y: true\n",
		&options, error, sizeof error));
	XVT_ASSERT_INT_EQ(options.mouse_flight_enabled, 0);
	XVT_ASSERT_INT_EQ(options.mouse_sensitivity, XVT_MOUSE_SENSITIVITY_MIN);
	XVT_ASSERT_TRUE(options.mouse_invert_y != 0);

	XVT_ASSERT_TRUE(parse_text(
		"input:\n  mouse_flight: false\n  mouse_sensitivity: 9\n  mouse_invert_y: true\n",
		&options, error, sizeof error));
	XVT_ASSERT_INT_EQ(options.mouse_sensitivity, XVT_MOUSE_SENSITIVITY_MAX);
	fixture_end();
}

static void check_parse_refusals(void)
{
	fixture_begin();
	/* Sensitivity outside 1 to 9, or not a number. */
	expect_refused(
		"input:\n  mouse_flight: true\n  mouse_sensitivity: 0\n  mouse_invert_y: false\n");
	expect_refused(
		"input:\n  mouse_flight: true\n  mouse_sensitivity: 10\n  mouse_invert_y: false\n");
	expect_refused(
		"input:\n  mouse_flight: true\n  mouse_sensitivity: \"5\"\n  mouse_invert_y: false\n");
	/* The two flags must be booleans. */
	expect_refused(
		"input:\n  mouse_flight: 1\n  mouse_sensitivity: 5\n  mouse_invert_y: false\n");
	expect_refused(
		"input:\n  mouse_flight: true\n  mouse_sensitivity: 5\n  mouse_invert_y: 0\n");
	/* All three are required. */
	expect_refused(
		"input:\n  mouse_sensitivity: 5\n  mouse_invert_y: false\n");
	expect_refused(
		"input:\n  mouse_flight: true\n  mouse_invert_y: false\n");
	expect_refused(
		"input:\n  mouse_flight: true\n  mouse_sensitivity: 5\n");
	fixture_end();
}

static void check_set_mouse(void)
{
	fixture_begin();
	char error[512];
	struct xvt_mouse_options options = {0, 5, 0};
	/* Needs loaded settings. */
	XVT_ASSERT_TRUE(!xvt_config_set_mouse(&options, error, sizeof error));

	fixture_load();
	const struct xvt_mouse_options defaults =
		xvt_config_default_settings()->mouse;
	uint64_t generation = xvt_config_generation();

	/* Two options differ from the shipped default and are stored; the third equals it and is not. */
	options = defaults;
	options.mouse_flight_enabled = !defaults.mouse_flight_enabled;
	options.mouse_sensitivity =
		defaults.mouse_sensitivity == XVT_MOUSE_SENSITIVITY_MAX
			? XVT_MOUSE_SENSITIVITY_MIN
			: defaults.mouse_sensitivity + 1;
	XVT_ASSERT_TRUE(xvt_config_set_mouse(&options, error, sizeof error));
	XVT_ASSERT_TRUE(xvt_config_generation() > generation);
	const AeronConfigFile *user = xvt_config_user_document();
	XVT_ASSERT_INT_EQ(
		AeronConfigFile_GetBool(user, "input.mouse_flight", -1),
		options.mouse_flight_enabled);
	XVT_ASSERT_INT_EQ(
		AeronConfigFile_GetInt(user, "input.mouse_sensitivity", -1),
		options.mouse_sensitivity);
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "input.mouse_invert_y"));
	const struct xvt_mouse_options *stored = &xvt_config_settings()->mouse;
	XVT_ASSERT_INT_EQ(stored->mouse_flight_enabled,
			  options.mouse_flight_enabled);
	XVT_ASSERT_INT_EQ(stored->mouse_sensitivity, options.mouse_sensitivity);
	XVT_ASSERT_INT_EQ(stored->mouse_invert_y, options.mouse_invert_y);
	/* In memory only. */
	XVT_ASSERT_TRUE(!fixture_exists("user/config.yaml"));

	/* Setting the shipped values again drops all three overrides. */
	XVT_ASSERT_TRUE(xvt_config_set_mouse(&defaults, error, sizeof error));
	user = xvt_config_user_document();
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "input.mouse_flight"));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "input.mouse_sensitivity"));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "input.mouse_invert_y"));
	stored = &xvt_config_settings()->mouse;
	XVT_ASSERT_INT_EQ(stored->mouse_flight_enabled,
			  defaults.mouse_flight_enabled);
	XVT_ASSERT_INT_EQ(stored->mouse_sensitivity,
			  defaults.mouse_sensitivity);
	XVT_ASSERT_INT_EQ(stored->mouse_invert_y, defaults.mouse_invert_y);

	/* A sensitivity out of range is refused and nothing changes. */
	generation = xvt_config_generation();
	AeronConfigFile *before = fixture_user_copy();
	options.mouse_sensitivity = XVT_MOUSE_SENSITIVITY_MAX + 1;
	XVT_ASSERT_TRUE(!xvt_config_set_mouse(&options, error, sizeof error));
	XVT_ASSERT_INT_EQ(xvt_config_generation(), generation);
	XVT_ASSERT_TRUE(
		fixture_same_document(xvt_config_user_document(), before));
	XVT_ASSERT_INT_EQ(xvt_config_settings()->mouse.mouse_sensitivity,
			  defaults.mouse_sensitivity);
	AeronConfigFile_Destroy(before);
	fixture_end();
}

int main(void)
{
	check_parse();
	check_parse_refusals();
	check_set_mouse();
	return 0;
}
