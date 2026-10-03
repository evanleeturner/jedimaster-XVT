/* Checks the typed settings parser and its error formatters (xvt_runtime/config/settings.h) against the
 * promises in the header. Most documents are a copy of the shipped defaults (resources/config.yaml) with one
 * value changed; a few small ones are written here. Each check starts from a fresh fixture folder
 * (config_fixture.h); no settings are loaded. */
#define _XOPEN_SOURCE 700

#include "config_fixture.h"
#include "test_assert.h"
#include "xvt_runtime/config/settings.h"

#include <stdio.h>
#include <string.h>

static XvtSceneSettings g_scene;
static XvtSettings g_out;

/* A copy of the shipped defaults, ready for one change. */
static AeronConfigFile *Shipped(void) { return Fixture_ShippedDocument(); }

/* Parses document into g_out with the shipped scene defaults, then destroys document. */
static int ParseAndDestroy(AeronConfigFile *document)
{
	char error[1024] = "";
	int parsed = XvtSettings_Parse(document, &g_scene, &g_out, error,
				       sizeof error);
	XVT_ASSERT_TRUE(parsed || error[0] != 0);
	AeronConfigFile_Destroy(document);
	return parsed;
}

static AeronConfigFile *WithInt(const char *path, int64_t value)
{
	AeronConfigFile *document = Shipped();
	AeronConfigError detail;
	XVT_ASSERT_TRUE(AeronConfigFile_SetInt(document, path, value, &detail));
	return document;
}

static AeronConfigFile *WithFloat(const char *path, double value)
{
	AeronConfigFile *document = Shipped();
	AeronConfigError detail;
	XVT_ASSERT_TRUE(
		AeronConfigFile_SetFloat(document, path, value, &detail));
	return document;
}

static AeronConfigFile *WithString(const char *path, const char *value)
{
	AeronConfigFile *document = Shipped();
	AeronConfigError detail;
	XVT_ASSERT_TRUE(
		AeronConfigFile_SetString(document, path, value, &detail));
	return document;
}

static AeronConfigFile *Without(const char *path)
{
	AeronConfigFile *document = Shipped();
	AeronConfigError detail;
	XVT_ASSERT_TRUE(AeronConfigFile_Has(document, path));
	XVT_ASSERT_TRUE(AeronConfigFile_Remove(document, path, &detail));
	return document;
}

static void Begin(void)
{
	Fixture_Begin();
	Fixture_ShippedScene(&g_scene);
}

static void CheckShippedDefaults(void)
{
	Begin();
	XVT_ASSERT_INT_EQ(ParseAndDestroy(Shipped()), 1);
	Fixture_End();
}

static void CheckRequiredAndTypes(void)
{
	Begin();
	/* Every value is required. */
	XVT_ASSERT_INT_EQ(ParseAndDestroy(Without("lighting.wrap")), 0);
	XVT_ASSERT_INT_EQ(ParseAndDestroy(Without("startup.skip_intro")), 0);
	XVT_ASSERT_INT_EQ(ParseAndDestroy(Without("flight.update_rate")), 0);
	/* Each of its type: a flag must be a boolean, an integer must not be written as a float. */
	XVT_ASSERT_INT_EQ(ParseAndDestroy(WithInt("startup.skip_intro", 1)), 0);
	XVT_ASSERT_INT_EQ(
		ParseAndDestroy(WithFloat("render.msaa_samples", 2.0)), 0);
	XVT_ASSERT_INT_EQ(ParseAndDestroy(WithInt("paths.game_data", 5)), 0);
	/* A float may be written as an integer. */
	XVT_ASSERT_INT_EQ(ParseAndDestroy(WithInt("lighting.intensity", 1)), 1);
	XVT_ASSERT_CLOSE(g_out.render.lighting.intensity, 1.0, 0,
			 "a small integer converts to float exactly");
	XVT_ASSERT_INT_EQ(ParseAndDestroy(WithFloat("lighting.intensity", 0.5)),
			  1);
	XVT_ASSERT_CLOSE(g_out.render.lighting.intensity, 0.5, 0,
			 "0.5 is exact in binary");
	/* Text must fit its field, with room for the terminator. */
	char path[XVT_PATH_CAPACITY + 1];
	memset(path, 'a', sizeof path);
	path[XVT_PATH_CAPACITY - 1] = 0;
	XVT_ASSERT_INT_EQ(ParseAndDestroy(WithString("paths.game_data", path)),
			  1);
	XVT_ASSERT_INT_EQ(strlen(g_out.game_data), XVT_PATH_CAPACITY - 1);
	path[XVT_PATH_CAPACITY - 1] = 'a';
	path[XVT_PATH_CAPACITY] = 0;
	XVT_ASSERT_INT_EQ(ParseAndDestroy(WithString("paths.game_data", path)),
			  0);
	Fixture_End();
}

static void CheckChoices(void)
{
	Begin();
	/* flight.update_rate is native or unlocked. */
	XVT_ASSERT_INT_EQ(
		ParseAndDestroy(WithString("flight.update_rate", "native")), 1);
	XVT_ASSERT_INT_EQ(g_out.flight_unlocked, 0);
	XVT_ASSERT_INT_EQ(
		ParseAndDestroy(WithString("flight.update_rate", "unlocked")),
		1);
	XVT_ASSERT_TRUE(g_out.flight_unlocked != 0);
	XVT_ASSERT_INT_EQ(
		ParseAndDestroy(WithString("flight.update_rate", "warp")), 0);
	/* The other choices refuse a value that is not one of them. */
	XVT_ASSERT_INT_EQ(ParseAndDestroy(WithString("video.window_mode",
						     "borderless_cinema")),
			  0);
	XVT_ASSERT_INT_EQ(ParseAndDestroy(WithString(
				  "render.temporal_upscaling.mode", "ultra")),
			  0);
	XVT_ASSERT_INT_EQ(ParseAndDestroy(WithString("skybox.mode", "nebula")),
			  0);
	/* MSAA is 1, 2, 4 or 8. */
	const int counts[] = {1, 2, 4, 8};
	for (int i = 0; i < 4; ++i) {
		XVT_ASSERT_INT_EQ(ParseAndDestroy(WithInt("render.msaa_samples",
							  counts[i])),
				  1);
		XVT_ASSERT_INT_EQ(g_out.render.msaa_samples, counts[i]);
	}
	XVT_ASSERT_INT_EQ(ParseAndDestroy(WithInt("render.msaa_samples", 3)),
			  0);
	Fixture_End();
}

static void CheckSky(void)
{
	Begin();
	/* "procedural" is accepted and read as stars. */
	XVT_ASSERT_INT_EQ(
		ParseAndDestroy(WithString("skybox.mode", "procedural")), 1);
	XVT_ASSERT_INT_EQ(g_out.render.sky.mode, XVT_SKY_STARS);
	XVT_ASSERT_INT_EQ(ParseAndDestroy(WithString("skybox.mode", "stars")),
			  1);
	XVT_ASSERT_INT_EQ(g_out.render.sky.mode, XVT_SKY_STARS);
	/* The cube mode needs a path. */
	AeronConfigFile *document = WithString("skybox.mode", "cube");
	AeronConfigError detail;
	XVT_ASSERT_TRUE(AeronConfigFile_SetString(document, "skybox.path",
						  "sky/cube.ktx2", &detail));
	XVT_ASSERT_INT_EQ(ParseAndDestroy(document), 1);
	XVT_ASSERT_INT_EQ(g_out.render.sky.mode, XVT_SKY_CUBE);
	XVT_ASSERT_INT_EQ(strcmp(g_out.render.sky.path, "sky/cube.ktx2"), 0);
	document = WithString("skybox.mode", "cube");
	XVT_ASSERT_TRUE(AeronConfigFile_SetString(document, "skybox.path", "",
						  &detail));
	XVT_ASSERT_INT_EQ(ParseAndDestroy(document), 0);
	Fixture_End();
}

static void CheckDisplay(void)
{
	Begin();
	/* sdr_gamma: auto is negative (the platform default), srgb is zero, or 2.2 or 2.4. */
	XVT_ASSERT_INT_EQ(
		ParseAndDestroy(WithString("presentation.sdr_gamma", "auto")),
		1);
	XVT_ASSERT_TRUE(g_out.render.presentation.sdr_gamma < 0.0f);
	XVT_ASSERT_INT_EQ(
		ParseAndDestroy(WithString("presentation.sdr_gamma", "srgb")),
		1);
	XVT_ASSERT_CLOSE(g_out.render.presentation.sdr_gamma, 0.0, 0,
			 "zero is exact");
	XVT_ASSERT_INT_EQ(
		ParseAndDestroy(WithFloat("presentation.sdr_gamma", 2.2)), 1);
	XVT_ASSERT_CLOSE(g_out.render.presentation.sdr_gamma, 2.2, 1e-6,
			 "2.2 is stored as the nearest float");
	XVT_ASSERT_INT_EQ(
		ParseAndDestroy(WithFloat("presentation.sdr_gamma", 2.4)), 1);
	XVT_ASSERT_CLOSE(g_out.render.presentation.sdr_gamma, 2.4, 1e-6,
			 "2.4 is stored as the nearest float");
	XVT_ASSERT_INT_EQ(
		ParseAndDestroy(WithFloat("presentation.sdr_gamma", 2.3)), 0);
	XVT_ASSERT_INT_EQ(
		ParseAndDestroy(WithString("presentation.sdr_gamma", "linear")),
		0);
	/* paper_white_nits: auto is zero (the system's reference white), or a positive number. */
	XVT_ASSERT_INT_EQ(ParseAndDestroy(WithString(
				  "presentation.paper_white_nits", "auto")),
			  1);
	XVT_ASSERT_CLOSE(g_out.render.presentation.paper_white_nits, 0.0, 0,
			 "zero is exact");
	XVT_ASSERT_INT_EQ(
		ParseAndDestroy(WithInt("presentation.paper_white_nits", 200)),
		1);
	XVT_ASSERT_CLOSE(g_out.render.presentation.paper_white_nits, 200.0, 0,
			 "a small integer is exact in float");
	XVT_ASSERT_INT_EQ(
		ParseAndDestroy(WithInt("presentation.paper_white_nits", 0)),
		0);
	XVT_ASSERT_INT_EQ(
		ParseAndDestroy(WithInt("presentation.paper_white_nits", -80)),
		0);
	XVT_ASSERT_INT_EQ(ParseAndDestroy(WithString(
				  "presentation.paper_white_nits", "bright")),
			  0);
	Fixture_End();
}

static void CheckSceneOverlay(void)
{
	Begin();
	/* The render block is laid over the scene defaults the caller passes: a value the document leaves
	 * out comes from them, one it sets replaces theirs. */
	g_scene.ssao.ssao_intensity = 0.5f;
	g_scene.ssao.ssao_quality = 2;
	XVT_ASSERT_INT_EQ(ParseAndDestroy(Shipped()), 1);
	XVT_ASSERT_CLOSE(g_out.render.scene.ssao.ssao_intensity, 0.5, 0,
			 "a copied float is exact");
	XVT_ASSERT_INT_EQ(g_out.render.scene.ssao.ssao_quality, 2);
	XVT_ASSERT_INT_EQ(g_out.render.scene.shadows.atlas_size,
			  g_scene.shadows.atlas_size);
	XVT_ASSERT_INT_EQ(ParseAndDestroy(WithInt("render.ssao.quality", 1)),
			  1);
	XVT_ASSERT_INT_EQ(g_out.render.scene.ssao.ssao_quality, 1);
	XVT_ASSERT_CLOSE(g_out.render.scene.ssao.ssao_intensity, 0.5, 0,
			 "a copied float is exact");
	Fixture_End();
}

static void CheckSubsections(void)
{
	Begin();
	/* The keyboard, the controllers, the gamepad defaults and the mouse are parsed too. */
	XVT_ASSERT_INT_EQ(ParseAndDestroy(WithString(
				  "input.keyboard.fire_weapon", "NoSuchKey")),
			  0);
	XVT_ASSERT_INT_EQ(ParseAndDestroy(Without("input.controllers")), 0);
	XVT_ASSERT_INT_EQ(ParseAndDestroy(WithString(
				  "input.gamepad_defaults.color", "red")),
			  0);
	XVT_ASSERT_INT_EQ(
		ParseAndDestroy(WithInt("input.mouse_sensitivity", 0)), 0);
	XVT_ASSERT_INT_EQ(
		ParseAndDestroy(WithInt("input.mouse_sensitivity", 3)), 1);
	XVT_ASSERT_INT_EQ(g_out.mouse.mouse_sensitivity, 3);
	Fixture_End();
}

static void CheckFailureLeavesOutput(void)
{
	Begin();
	char error[1024] = "";
	memset(&g_out, 0x5A, sizeof g_out);
	static XvtSettings before;
	memcpy(&before, &g_out, sizeof before);
	AeronConfigFile *document = WithInt("render.msaa_samples", 3);
	XVT_ASSERT_INT_EQ(XvtSettings_Parse(document, &g_scene, &g_out, error,
					    sizeof error),
			  0);
	XVT_ASSERT_TRUE(error[0] != 0);
	XVT_ASSERT_INT_EQ(memcmp(&g_out, &before, sizeof before), 0);
	AeronConfigFile_Destroy(document);
	Fixture_End();
}

static void CheckNodeError(void)
{
	Begin();
	AeronConfigFile *document = Fixture_Yaml("top:\n  inner: 5\n");
	char error[512], expected[512];
	/* A node that exists: its file, line and column. Fixture_Yaml writes the first document as
	 * temp/document1.yaml, in the TEMP root. */
	const AeronConfigNode *node =
		AeronConfigFile_GetNode(document, "top.inner");
	XVT_ASSERT_TRUE(node != NULL);
	snprintf(expected, sizeof expected,
		 "TEMP/document1.yaml:%d:%d: top.inner: bad value",
		 AeronConfigNode_Line(node), AeronConfigNode_Column(node));
	XVT_ASSERT_INT_EQ(XvtSettings_NodeError(document, "top.inner",
						"bad value", error,
						sizeof error),
			  0);
	XVT_ASSERT_INT_EQ(strcmp(error, expected), 0);
	/* A path that is absent: the document root's place, with the path as given. */
	node = AeronConfigFile_Root(document);
	snprintf(expected, sizeof expected,
		 "TEMP/document1.yaml:%d:%d: top.missing: not there",
		 AeronConfigNode_Line(node), AeronConfigNode_Column(node));
	XVT_ASSERT_INT_EQ(XvtSettings_NodeError(document, "top.missing",
						"not there", error,
						sizeof error),
			  0);
	XVT_ASSERT_INT_EQ(strcmp(error, expected), 0);
	AeronConfigFile_Destroy(document);

	/* A node of the shipped defaults names the RESOURCE root. */
	document = Shipped();
	node = AeronConfigFile_GetNode(document, "version");
	snprintf(expected, sizeof expected,
		 "RESOURCE/config.yaml:%d:%d: version: old",
		 AeronConfigNode_Line(node), AeronConfigNode_Column(node));
	XVT_ASSERT_INT_EQ(XvtSettings_NodeError(document, "version", "old",
						error, sizeof error),
			  0);
	XVT_ASSERT_INT_EQ(strcmp(error, expected), 0);
	AeronConfigFile_Destroy(document);
	Fixture_End();
}

static void CheckFileError(void)
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
			XvtSettings_FileError(&detail, error, sizeof error), 0);
		XVT_ASSERT_INT_EQ(strcmp(error, cases[i].expected), 0);
	}
}

int main(void)
{
	CheckShippedDefaults();
	CheckRequiredAndTypes();
	CheckChoices();
	CheckSky();
	CheckDisplay();
	CheckSceneOverlay();
	CheckSubsections();
	CheckFailureLeavesOutput();
	CheckNodeError();
	CheckFileError();
	return 0;
}
