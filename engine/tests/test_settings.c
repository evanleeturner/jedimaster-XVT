/* Checks the typed settings parser and its error formatters (xvt_runtime/config/settings.h) against the
 * promises in the header. Most documents are a copy of the shipped defaults (resources/config.yaml) with one
 * value changed; a few small ones are written here. Each check starts from a fresh fixture folder
 * (config_fixture.h); no settings are loaded. */
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <string.h>

#include "config_fixture.h"
#include "test_assert.h"
#include "xvt_runtime/config/settings.h"

static struct xvt_scene_settings g_scene;
static struct xvt_settings g_out;

/* A copy of the shipped defaults, ready for one change. */
static AeronConfigFile *shipped(void) { return fixture_shipped_document(); }

/* Parses document into g_out with the shipped scene defaults, then destroys document. */
static int parse_and_destroy(AeronConfigFile *document)
{
	char error[1024] = "";
	int parsed = xvt_settings_parse(document, &g_scene, &g_out, error,
					sizeof error);
	XVT_ASSERT_TRUE(parsed || error[0] != 0);
	AeronConfigFile_Destroy(document);
	return parsed;
}

static AeronConfigFile *with_int(const char *path, int64_t value)
{
	AeronConfigFile *document = shipped();
	AeronConfigError detail;
	XVT_ASSERT_TRUE(AeronConfigFile_SetInt(document, path, value, &detail));
	return document;
}

static AeronConfigFile *with_float(const char *path, double value)
{
	AeronConfigFile *document = shipped();
	AeronConfigError detail;
	XVT_ASSERT_TRUE(
		AeronConfigFile_SetFloat(document, path, value, &detail));
	return document;
}

static AeronConfigFile *with_string(const char *path, const char *value)
{
	AeronConfigFile *document = shipped();
	AeronConfigError detail;
	XVT_ASSERT_TRUE(
		AeronConfigFile_SetString(document, path, value, &detail));
	return document;
}

static AeronConfigFile *without(const char *path)
{
	AeronConfigFile *document = shipped();
	AeronConfigError detail;
	XVT_ASSERT_TRUE(AeronConfigFile_Has(document, path));
	XVT_ASSERT_TRUE(AeronConfigFile_Remove(document, path, &detail));
	return document;
}

static void begin(void)
{
	fixture_begin();
	fixture_shipped_scene(&g_scene);
}

static void check_shipped_defaults(void)
{
	begin();
	XVT_ASSERT_INT_EQ(parse_and_destroy(shipped()), 1);
	fixture_end();
}

static void check_required_and_types(void)
{
	begin();
	/* Every value is required. */
	XVT_ASSERT_INT_EQ(parse_and_destroy(without("lighting.wrap")), 0);
	XVT_ASSERT_INT_EQ(parse_and_destroy(without("startup.skip_intro")), 0);
	XVT_ASSERT_INT_EQ(parse_and_destroy(without("flight.update_rate")), 0);
	/* Each of its type: a flag must be a boolean, an integer must not be written as a float. */
	XVT_ASSERT_INT_EQ(parse_and_destroy(with_int("startup.skip_intro", 1)),
			  0);
	XVT_ASSERT_INT_EQ(
		parse_and_destroy(with_float("render.msaa_samples", 2.0)), 0);
	XVT_ASSERT_INT_EQ(parse_and_destroy(with_int("paths.game_data", 5)), 0);
	/* A float may be written as an integer. */
	XVT_ASSERT_INT_EQ(parse_and_destroy(with_int("lighting.intensity", 1)),
			  1);
	XVT_ASSERT_CLOSE(g_out.render.lighting.intensity, 1.0, 0,
			 "a small integer converts to float exactly");
	XVT_ASSERT_INT_EQ(
		parse_and_destroy(with_float("lighting.intensity", 0.5)), 1);
	XVT_ASSERT_CLOSE(g_out.render.lighting.intensity, 0.5, 0,
			 "0.5 is exact in binary");
	/* Text must fit its field, with room for the terminator. */
	char path[XVT_PATH_CAPACITY + 1];
	memset(path, 'a', sizeof path);
	path[XVT_PATH_CAPACITY - 1] = 0;
	XVT_ASSERT_INT_EQ(
		parse_and_destroy(with_string("paths.game_data", path)), 1);
	XVT_ASSERT_INT_EQ(strlen(g_out.game_data), XVT_PATH_CAPACITY - 1);
	path[XVT_PATH_CAPACITY - 1] = 'a';
	path[XVT_PATH_CAPACITY] = 0;
	XVT_ASSERT_INT_EQ(
		parse_and_destroy(with_string("paths.game_data", path)), 0);
	fixture_end();
}

static void check_choices(void)
{
	begin();
	/* flight.update_rate is native or unlocked. */
	XVT_ASSERT_INT_EQ(
		parse_and_destroy(with_string("flight.update_rate", "native")),
		1);
	XVT_ASSERT_INT_EQ(g_out.flight_unlocked, 0);
	XVT_ASSERT_INT_EQ(parse_and_destroy(with_string("flight.update_rate",
							"unlocked")),
			  1);
	XVT_ASSERT_TRUE(g_out.flight_unlocked != 0);
	XVT_ASSERT_INT_EQ(
		parse_and_destroy(with_string("flight.update_rate", "warp")),
		0);
	/* The other choices refuse a value that is not one of them. */
	XVT_ASSERT_INT_EQ(parse_and_destroy(with_string("video.window_mode",
							"borderless_cinema")),
			  0);
	XVT_ASSERT_INT_EQ(parse_and_destroy(with_string(
				  "render.temporal_upscaling.mode", "ultra")),
			  0);
	XVT_ASSERT_INT_EQ(
		parse_and_destroy(with_string("skybox.mode", "nebula")), 0);
	/* MSAA is 1, 2, 4 or 8. */
	const int counts[] = {1, 2, 4, 8};
	for (int i = 0; i < 4; ++i) {
		XVT_ASSERT_INT_EQ(parse_and_destroy(with_int(
					  "render.msaa_samples", counts[i])),
				  1);
		XVT_ASSERT_INT_EQ(g_out.render.msaa_samples, counts[i]);
	}
	XVT_ASSERT_INT_EQ(parse_and_destroy(with_int("render.msaa_samples", 3)),
			  0);
	fixture_end();
}

static void check_sky(void)
{
	begin();
	/* "procedural" is accepted and read as stars. */
	XVT_ASSERT_INT_EQ(
		parse_and_destroy(with_string("skybox.mode", "procedural")), 1);
	XVT_ASSERT_INT_EQ(g_out.render.sky.mode, XVT_SKY_STARS);
	XVT_ASSERT_INT_EQ(
		parse_and_destroy(with_string("skybox.mode", "stars")), 1);
	XVT_ASSERT_INT_EQ(g_out.render.sky.mode, XVT_SKY_STARS);
	/* The cube mode needs a path. */
	AeronConfigFile *document = with_string("skybox.mode", "cube");
	AeronConfigError detail;
	XVT_ASSERT_TRUE(AeronConfigFile_SetString(document, "skybox.path",
						  "sky/cube.ktx2", &detail));
	XVT_ASSERT_INT_EQ(parse_and_destroy(document), 1);
	XVT_ASSERT_INT_EQ(g_out.render.sky.mode, XVT_SKY_CUBE);
	XVT_ASSERT_INT_EQ(strcmp(g_out.render.sky.path, "sky/cube.ktx2"), 0);
	document = with_string("skybox.mode", "cube");
	XVT_ASSERT_TRUE(AeronConfigFile_SetString(document, "skybox.path", "",
						  &detail));
	XVT_ASSERT_INT_EQ(parse_and_destroy(document), 0);
	fixture_end();
}

static void check_display(void)
{
	begin();
	/* sdr_gamma: auto is negative (the platform default), srgb is zero, or 2.2 or 2.4. */
	XVT_ASSERT_INT_EQ(parse_and_destroy(with_string(
				  "presentation.sdr_gamma", "auto")),
			  1);
	XVT_ASSERT_TRUE(g_out.render.presentation.sdr_gamma < 0.0f);
	XVT_ASSERT_INT_EQ(parse_and_destroy(with_string(
				  "presentation.sdr_gamma", "srgb")),
			  1);
	XVT_ASSERT_CLOSE(g_out.render.presentation.sdr_gamma, 0.0, 0,
			 "zero is exact");
	XVT_ASSERT_INT_EQ(
		parse_and_destroy(with_float("presentation.sdr_gamma", 2.2)),
		1);
	XVT_ASSERT_CLOSE(g_out.render.presentation.sdr_gamma, 2.2, 1e-6,
			 "2.2 is stored as the nearest float");
	XVT_ASSERT_INT_EQ(
		parse_and_destroy(with_float("presentation.sdr_gamma", 2.4)),
		1);
	XVT_ASSERT_CLOSE(g_out.render.presentation.sdr_gamma, 2.4, 1e-6,
			 "2.4 is stored as the nearest float");
	XVT_ASSERT_INT_EQ(
		parse_and_destroy(with_float("presentation.sdr_gamma", 2.3)),
		0);
	XVT_ASSERT_INT_EQ(parse_and_destroy(with_string(
				  "presentation.sdr_gamma", "linear")),
			  0);
	/* paper_white_nits: auto is zero (the system's reference white), or a positive number. */
	XVT_ASSERT_INT_EQ(parse_and_destroy(with_string(
				  "presentation.paper_white_nits", "auto")),
			  1);
	XVT_ASSERT_CLOSE(g_out.render.presentation.paper_white_nits, 0.0, 0,
			 "zero is exact");
	XVT_ASSERT_INT_EQ(parse_and_destroy(with_int(
				  "presentation.paper_white_nits", 200)),
			  1);
	XVT_ASSERT_CLOSE(g_out.render.presentation.paper_white_nits, 200.0, 0,
			 "a small integer is exact in float");
	XVT_ASSERT_INT_EQ(
		parse_and_destroy(with_int("presentation.paper_white_nits", 0)),
		0);
	XVT_ASSERT_INT_EQ(parse_and_destroy(with_int(
				  "presentation.paper_white_nits", -80)),
			  0);
	XVT_ASSERT_INT_EQ(parse_and_destroy(with_string(
				  "presentation.paper_white_nits", "bright")),
			  0);
	fixture_end();
}

static void check_scene_overlay(void)
{
	begin();
	/* The render block is laid over the scene defaults the caller passes: a value the document leaves
	 * out comes from them, one it sets replaces theirs. */
	g_scene.ssao.ssao_intensity = 0.5f;
	g_scene.ssao.ssao_quality = 2;
	XVT_ASSERT_INT_EQ(parse_and_destroy(shipped()), 1);
	XVT_ASSERT_CLOSE(g_out.render.scene.ssao.ssao_intensity, 0.5, 0,
			 "a copied float is exact");
	XVT_ASSERT_INT_EQ(g_out.render.scene.ssao.ssao_quality, 2);
	XVT_ASSERT_INT_EQ(g_out.render.scene.shadows.atlas_size,
			  g_scene.shadows.atlas_size);
	XVT_ASSERT_INT_EQ(parse_and_destroy(with_int("render.ssao.quality", 1)),
			  1);
	XVT_ASSERT_INT_EQ(g_out.render.scene.ssao.ssao_quality, 1);
	XVT_ASSERT_CLOSE(g_out.render.scene.ssao.ssao_intensity, 0.5, 0,
			 "a copied float is exact");
	fixture_end();
}

static void check_subsections(void)
{
	begin();
	/* The keyboard, the controllers, the gamepad defaults and the mouse are parsed too. */
	XVT_ASSERT_INT_EQ(parse_and_destroy(with_string(
				  "input.keyboard.fire_weapon", "NoSuchKey")),
			  0);
	XVT_ASSERT_INT_EQ(parse_and_destroy(without("input.controllers")), 0);
	XVT_ASSERT_INT_EQ(parse_and_destroy(with_string(
				  "input.gamepad_defaults.color", "red")),
			  0);
	XVT_ASSERT_INT_EQ(
		parse_and_destroy(with_int("input.mouse_sensitivity", 0)), 0);
	XVT_ASSERT_INT_EQ(
		parse_and_destroy(with_int("input.mouse_sensitivity", 3)), 1);
	XVT_ASSERT_INT_EQ(g_out.mouse.mouse_sensitivity, 3);
	fixture_end();
}

static void check_failure_leaves_output(void)
{
	begin();
	char error[1024] = "";
	memset(&g_out, 0x5A, sizeof g_out);
	static struct xvt_settings before;
	memcpy(&before, &g_out, sizeof before);
	AeronConfigFile *document = with_int("render.msaa_samples", 3);
	XVT_ASSERT_INT_EQ(xvt_settings_parse(document, &g_scene, &g_out, error,
					     sizeof error),
			  0);
	XVT_ASSERT_TRUE(error[0] != 0);
	XVT_ASSERT_INT_EQ(memcmp(&g_out, &before, sizeof before), 0);
	AeronConfigFile_Destroy(document);
	fixture_end();
}

static void check_node_error(void)
{
	begin();
	AeronConfigFile *document = fixture_yaml("top:\n  inner: 5\n");
	char error[512];
	char expected[512];
	/* A node that exists: its file, line and column. fixture_yaml writes the first document as
	 * temp/document1.yaml, in the TEMP root. */
	const AeronConfigNode *node =
		AeronConfigFile_GetNode(document, "top.inner");
	XVT_ASSERT_TRUE(node != NULL);
	snprintf(expected, sizeof expected,
		 "TEMP/document1.yaml:%d:%d: top.inner: bad value",
		 AeronConfigNode_Line(node), AeronConfigNode_Column(node));
	XVT_ASSERT_INT_EQ(xvt_settings_node_error(document, "top.inner",
						  "bad value", error,
						  sizeof error),
			  0);
	XVT_ASSERT_INT_EQ(strcmp(error, expected), 0);
	/* A path that is absent: the document root's place, with the path as given. */
	node = AeronConfigFile_Root(document);
	snprintf(expected, sizeof expected,
		 "TEMP/document1.yaml:%d:%d: top.missing: not there",
		 AeronConfigNode_Line(node), AeronConfigNode_Column(node));
	XVT_ASSERT_INT_EQ(xvt_settings_node_error(document, "top.missing",
						  "not there", error,
						  sizeof error),
			  0);
	XVT_ASSERT_INT_EQ(strcmp(error, expected), 0);
	AeronConfigFile_Destroy(document);

	/* A node of the shipped defaults names the RESOURCE root. */
	document = shipped();
	node = AeronConfigFile_GetNode(document, "version");
	snprintf(expected, sizeof expected,
		 "RESOURCE/config.yaml:%d:%d: version: old",
		 AeronConfigNode_Line(node), AeronConfigNode_Column(node));
	XVT_ASSERT_INT_EQ(xvt_settings_node_error(document, "version", "old",
						  error, sizeof error),
			  0);
	XVT_ASSERT_INT_EQ(strcmp(error, expected), 0);
	AeronConfigFile_Destroy(document);
	fixture_end();
}

static void check_file_error(void)
{
	static const struct {
		AeronVfsRoot root;
		const char *expected;
	} cases[] = {
		{AERON_VFS_ROOT_ASSET, "ASSET/dir/file.yaml:4:7: it broke"},
		{AERON_VFS_ROOT_USER, "USER/dir/file.yaml:4:7: it broke"},
		{AERON_VFS_ROOT_TEMP, "TEMP/dir/file.yaml:4:7: it broke"},
		{AERON_VFS_ROOT_RESOURCE,
		 "RESOURCE/dir/file.yaml:4:7: it broke"},
	};

	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		AeronConfigError detail;
		memset(&detail, 0, sizeof detail);
		detail.root = cases[i].root;
		strcpy(detail.path, "dir/file.yaml");
		detail.line = 4;
		detail.column = 7;
		strcpy(detail.message, "it broke");
		char error[256];
		XVT_ASSERT_INT_EQ(
			xvt_settings_file_error(&detail, error, sizeof error),
			0);
		XVT_ASSERT_INT_EQ(strcmp(error, cases[i].expected), 0);
	}
}

int main(void)
{
	check_shipped_defaults();
	check_required_and_types();
	check_choices();
	check_sky();
	check_display();
	check_scene_overlay();
	check_subsections();
	check_failure_leaves_output();
	check_node_error();
	check_file_error();
	return 0;
}
