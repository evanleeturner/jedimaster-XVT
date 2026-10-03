/* Checks the mouse settings (xvt_runtime/config/mouse_config.h) against the promises in its header: the
 * parser on documents this file writes itself, and XvtConfig_SetMouse on settings loaded from a copy of the
 * shipped defaults. Each check starts from a fresh fixture folder (config_fixture.h). */
#define _XOPEN_SOURCE 700

#include "config_fixture.h"
#include "test_assert.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/config/mouse_config.h"

#include <string.h>

/* Parses text's mouse settings into *options and returns what the parser returned. */
static bool ParseText(const char *text, struct XvtMouseOptions *options,
		      char *error, size_t capacity)
{
	AeronConfigFile *document = Fixture_Yaml(text);
	error[0] = 0;
	bool parsed = XvtMouseConfig_Parse(document, options, error, capacity);
	AeronConfigFile_Destroy(document);
	return parsed;
}

static void ExpectRefused(const char *text)
{
	struct XvtMouseOptions options;
	char error[256];
	Fixture_Case(text);
	XVT_ASSERT_TRUE(!ParseText(text, &options, error, sizeof error));
	XVT_ASSERT_TRUE(error[0] != 0);
	Fixture_Case(NULL);
}

static void CheckParse(void)
{
	Fixture_Begin();
	struct XvtMouseOptions options;
	char error[256];
	XVT_ASSERT_TRUE(ParseText(
		"input:\n  mouse_flight: true\n  mouse_sensitivity: 7\n  mouse_invert_y: false\n",
		&options, error, sizeof error));
	XVT_ASSERT_TRUE(options.mouse_flight_enabled != 0);
	XVT_ASSERT_INT_EQ(options.mouse_sensitivity, 7);
	XVT_ASSERT_INT_EQ(options.mouse_invert_y, 0);

	XVT_ASSERT_TRUE(ParseText(
		"input:\n  mouse_flight: false\n  mouse_sensitivity: 1\n  mouse_invert_y: true\n",
		&options, error, sizeof error));
	XVT_ASSERT_INT_EQ(options.mouse_flight_enabled, 0);
	XVT_ASSERT_INT_EQ(options.mouse_sensitivity, XVT_MOUSE_SENSITIVITY_MIN);
	XVT_ASSERT_TRUE(options.mouse_invert_y != 0);

	XVT_ASSERT_TRUE(ParseText(
		"input:\n  mouse_flight: false\n  mouse_sensitivity: 9\n  mouse_invert_y: true\n",
		&options, error, sizeof error));
	XVT_ASSERT_INT_EQ(options.mouse_sensitivity, XVT_MOUSE_SENSITIVITY_MAX);
	Fixture_End();
}

static void CheckParseRefusals(void)
{
	Fixture_Begin();
	/* Sensitivity outside 1 to 9, or not a number. */
	ExpectRefused(
		"input:\n  mouse_flight: true\n  mouse_sensitivity: 0\n  mouse_invert_y: false\n");
	ExpectRefused(
		"input:\n  mouse_flight: true\n  mouse_sensitivity: 10\n  mouse_invert_y: false\n");
	ExpectRefused(
		"input:\n  mouse_flight: true\n  mouse_sensitivity: \"5\"\n  mouse_invert_y: false\n");
	/* The two flags must be booleans. */
	ExpectRefused(
		"input:\n  mouse_flight: 1\n  mouse_sensitivity: 5\n  mouse_invert_y: false\n");
	ExpectRefused(
		"input:\n  mouse_flight: true\n  mouse_sensitivity: 5\n  mouse_invert_y: 0\n");
	/* All three are required. */
	ExpectRefused(
		"input:\n  mouse_sensitivity: 5\n  mouse_invert_y: false\n");
	ExpectRefused(
		"input:\n  mouse_flight: true\n  mouse_invert_y: false\n");
	ExpectRefused("input:\n  mouse_flight: true\n  mouse_sensitivity: 5\n");
	Fixture_End();
}

static void CheckSetMouse(void)
{
	Fixture_Begin();
	char error[512];
	struct XvtMouseOptions options = {0, 5, 0};
	/* Needs loaded settings. */
	XVT_ASSERT_TRUE(!XvtConfig_SetMouse(&options, error, sizeof error));

	Fixture_Load();
	const struct XvtMouseOptions defaults =
		XvtConfig_DefaultSettings()->mouse;
	uint64_t generation = XvtConfig_Generation();

	/* Two options differ from the shipped default and are stored; the third equals it and is not. */
	options = defaults;
	options.mouse_flight_enabled = !defaults.mouse_flight_enabled;
	options.mouse_sensitivity =
		defaults.mouse_sensitivity == XVT_MOUSE_SENSITIVITY_MAX
			? XVT_MOUSE_SENSITIVITY_MIN
			: defaults.mouse_sensitivity + 1;
	XVT_ASSERT_TRUE(XvtConfig_SetMouse(&options, error, sizeof error));
	XVT_ASSERT_TRUE(XvtConfig_Generation() > generation);
	const AeronConfigFile *user = XvtConfig_UserDocument();
	XVT_ASSERT_INT_EQ(
		AeronConfigFile_GetBool(user, "input.mouse_flight", -1),
		options.mouse_flight_enabled);
	XVT_ASSERT_INT_EQ(
		AeronConfigFile_GetInt(user, "input.mouse_sensitivity", -1),
		options.mouse_sensitivity);
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "input.mouse_invert_y"));
	const struct XvtMouseOptions *stored = &XvtConfig_Settings()->mouse;
	XVT_ASSERT_INT_EQ(stored->mouse_flight_enabled,
			  options.mouse_flight_enabled);
	XVT_ASSERT_INT_EQ(stored->mouse_sensitivity, options.mouse_sensitivity);
	XVT_ASSERT_INT_EQ(stored->mouse_invert_y, options.mouse_invert_y);
	/* In memory only. */
	XVT_ASSERT_TRUE(!Fixture_Exists("user/config.yaml"));

	/* Setting the shipped values again drops all three overrides. */
	XVT_ASSERT_TRUE(XvtConfig_SetMouse(&defaults, error, sizeof error));
	user = XvtConfig_UserDocument();
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "input.mouse_flight"));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "input.mouse_sensitivity"));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "input.mouse_invert_y"));
	stored = &XvtConfig_Settings()->mouse;
	XVT_ASSERT_INT_EQ(stored->mouse_flight_enabled,
			  defaults.mouse_flight_enabled);
	XVT_ASSERT_INT_EQ(stored->mouse_sensitivity,
			  defaults.mouse_sensitivity);
	XVT_ASSERT_INT_EQ(stored->mouse_invert_y, defaults.mouse_invert_y);

	/* A sensitivity out of range is refused and nothing changes. */
	generation = XvtConfig_Generation();
	AeronConfigFile *before = Fixture_UserCopy();
	options.mouse_sensitivity = XVT_MOUSE_SENSITIVITY_MAX + 1;
	XVT_ASSERT_TRUE(!XvtConfig_SetMouse(&options, error, sizeof error));
	XVT_ASSERT_INT_EQ(XvtConfig_Generation(), generation);
	XVT_ASSERT_TRUE(Fixture_SameDocument(XvtConfig_UserDocument(), before));
	XVT_ASSERT_INT_EQ(XvtConfig_Settings()->mouse.mouse_sensitivity,
			  defaults.mouse_sensitivity);
	AeronConfigFile_Destroy(before);
	Fixture_End();
}

int main(void)
{
	CheckParse();
	CheckParseRefusals();
	CheckSetMouse();
	return 0;
}
