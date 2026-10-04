/* Checks the video page's settings record (xvt_runtime/config/video_config.h) against the promises in its
 * header: Read, Equals, Validate and ApplyTo on records this file builds, and SetVideo and RestoreVideo on
 * settings loaded from a copy of the shipped defaults. Each check starts from a fresh fixture folder
 * (config_fixture.h). */
#define _XOPEN_SOURCE 700

#include <math.h>
#include <string.h>

#include "config_fixture.h"
#include "test_assert.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/config/video_config.h"

/* A record with a distinctive value in every field, for the copying checks; it is never validated. */
static struct xvt_video_settings distinct(void)
{
	struct xvt_video_settings v;
	memset(&v, 0, sizeof v);
	v.cockpit_undither = 1;
	v.fullscreen = 0;
	v.hdr = 1;
	v.sdr_gamma = 2.4f;
	v.paper_white_nits = 250.0f;
	v.ssao_quality = 1;
	v.shadows_enabled = 0;
	v.shadow_atlas_size = 2048;
	v.fsr_mode = AERON_TEMPORAL_OFF;
	v.fsr_sharpness = 0.25f;
	v.msaa_samples = 4;
	v.motion_blur_quality = 1;
	v.motion_blur_shutter = 0.75f;
	return v;
}

/* The shipped defaults' video fields with temporal upscaling off, so any MSAA count is allowed: the record
 * the video page starts from, which it must be able to check and store. */
static struct xvt_video_settings shipped_record(void)
{
	static struct xvt_settings settings;
	struct xvt_scene_settings scene;
	char error[512] = "";
	AeronConfigFile *shipped = fixture_shipped_document();
	fixture_shipped_scene(&scene);
	XVT_ASSERT_INT_EQ(xvt_settings_parse(shipped, &scene, &settings, error,
					     sizeof error),
			  1);
	AeronConfigFile_Destroy(shipped);
	struct xvt_video_settings v;
	xvt_video_settings_read(&settings, &v);
	v.fsr_mode = AERON_TEMPORAL_OFF;
	return v;
}

/* base with every field changed whose choices a header lists: the four flags, the gamma, the luminance,
 * the temporal mode and MSAA (1 under temporal upscaling). The other fields keep base's values. */
static struct xvt_video_settings
changed_from(const struct xvt_video_settings *base)
{
	struct xvt_video_settings v = *base;
	v.cockpit_undither = !base->cockpit_undither;
	v.fullscreen = !base->fullscreen;
	v.hdr = !base->hdr;
	v.shadows_enabled = !base->shadows_enabled;
	v.sdr_gamma = base->sdr_gamma == 2.2f ? 2.4f : 2.2f;
	v.paper_white_nits = base->paper_white_nits == 0.0f ? 200.0f : 0.0f;
	v.fsr_mode = base->fsr_mode == AERON_TEMPORAL_OFF
			     ? AERON_TEMPORAL_QUALITY
			     : AERON_TEMPORAL_OFF;
	v.msaa_samples = v.fsr_mode != AERON_TEMPORAL_OFF ? 1
			 : base->msaa_samples == 8	  ? 4
							  : 8;
	return v;
}

static void check_read(void)
{
	static struct xvt_settings settings;
	memset(&settings, 0, sizeof settings);
	settings.render.cockpit_undither = 1;
	settings.fullscreen = 1;
	settings.render.presentation.hdr_output = 1;
	settings.render.presentation.sdr_gamma = 2.2f;
	settings.render.presentation.paper_white_nits = 300.0f;
	settings.render.scene.ssao.ssao_quality = 2;
	settings.render.scene.shadows.enabled = 1;
	settings.render.scene.shadows.atlas_size = 8192;
	settings.render.temporal_mode = AERON_TEMPORAL_BALANCED;
	settings.render.temporal_sharpness = 0.5f;
	settings.render.msaa_samples = 1;
	settings.render.motion_blur.quality = 2;
	settings.render.motion_blur.shutter = 0.125f;
	struct xvt_video_settings v;
	xvt_video_settings_read(&settings, &v);
	XVT_ASSERT_INT_EQ(v.cockpit_undither, 1);
	XVT_ASSERT_INT_EQ(v.fullscreen, 1);
	XVT_ASSERT_INT_EQ(v.hdr, 1);
	XVT_ASSERT_CLOSE(v.sdr_gamma, 2.2f, 0, "a copied float is exact");
	XVT_ASSERT_CLOSE(v.paper_white_nits, 300.0f, 0,
			 "a copied float is exact");
	XVT_ASSERT_INT_EQ(v.ssao_quality, 2);
	XVT_ASSERT_INT_EQ(v.shadows_enabled, 1);
	XVT_ASSERT_INT_EQ(v.shadow_atlas_size, 8192);
	XVT_ASSERT_INT_EQ(v.fsr_mode, AERON_TEMPORAL_BALANCED);
	XVT_ASSERT_CLOSE(v.fsr_sharpness, 0.5f, 0, "a copied float is exact");
	XVT_ASSERT_INT_EQ(v.msaa_samples, 1);
	XVT_ASSERT_INT_EQ(v.motion_blur_quality, 2);
	XVT_ASSERT_CLOSE(v.motion_blur_shutter, 0.125f, 0,
			 "a copied float is exact");
}

/* Changes field number field (0 to 12, in the record's order) of *v. */
static void change(struct xvt_video_settings *v, int field)
{
	switch (field) {
	case 0:
		v->cockpit_undither = !v->cockpit_undither;
		break;
	case 1:
		v->fullscreen = !v->fullscreen;
		break;
	case 2:
		v->hdr = !v->hdr;
		break;
	case 3:
		v->sdr_gamma = v->sdr_gamma == 2.2f ? 2.4f : 2.2f;
		break;
	case 4:
		v->paper_white_nits += 1.0f;
		break;
	case 5:
		v->ssao_quality += 1;
		break;
	case 6:
		v->shadows_enabled = !v->shadows_enabled;
		break;
	case 7:
		v->shadow_atlas_size *= 2;
		break;
	case 8:
		v->fsr_mode = v->fsr_mode == AERON_TEMPORAL_OFF
				      ? AERON_TEMPORAL_QUALITY
				      : AERON_TEMPORAL_OFF;
		break;
	case 9:
		v->fsr_sharpness += 0.25f;
		break;
	case 10:
		v->msaa_samples *= 2;
		break;
	case 11:
		v->motion_blur_quality += 1;
		break;
	default:
		v->motion_blur_shutter += 0.125f;
		break;
	}
}

static void check_equals(void)
{
	const struct xvt_video_settings a = distinct();
	struct xvt_video_settings b = a;
	XVT_ASSERT_TRUE(xvt_video_settings_equals(&a, &b));
	for (int field = 0; field < 13; ++field) {
		b = a;
		change(&b, field);
		XVT_ASSERT_TRUE(!xvt_video_settings_equals(&a, &b));
		XVT_ASSERT_TRUE(!xvt_video_settings_equals(&b, &a));
	}
}

/* Expects v refused with the header's message; what names the bad field, printed if the check fails. */
static void expect_invalid(const struct xvt_video_settings *v, const char *what)
{
	char error[64];
	memset(error, 'x', sizeof error);
	fixture_case(what);
	XVT_ASSERT_TRUE(!xvt_video_settings_validate(v, error, sizeof error));
	XVT_ASSERT_INT_EQ(strcmp(error, "Invalid video settings"), 0);
	fixture_case(NULL);
}

static void check_validate(void)
{
	fixture_begin();
	const struct xvt_video_settings good = shipped_record();
	char error[64] = "";
	XVT_ASSERT_TRUE(
		xvt_video_settings_validate(&good, error, sizeof error));
	struct xvt_video_settings v;

	/* Each flag is 0 or 1. */
	v = good;
	v.cockpit_undither = 2;
	expect_invalid(&v, "cockpit_undither 2");
	v = good;
	v.fullscreen = 2;
	expect_invalid(&v, "fullscreen 2");
	v = good;
	v.hdr = -1;
	expect_invalid(&v, "hdr -1");
	v = good;
	v.shadows_enabled = 2;
	expect_invalid(&v, "shadows_enabled 2");
	/* The gamma is one of auto (negative), sRGB (zero), 2.2 or 2.4. */
	const float gammas[] = {-1.0f, 0.0f, 2.2f, 2.4f};
	for (int i = 0; i < 4; ++i) {
		v = good;
		v.sdr_gamma = gammas[i];
		XVT_ASSERT_TRUE(
			xvt_video_settings_validate(&v, error, sizeof error));
	}
	v = good;
	v.sdr_gamma = 2.3f;
	expect_invalid(&v, "sdr_gamma 2.3");
	/* The luminance is zero (follow the system) or positive, and a number. */
	v = good;
	v.paper_white_nits = 0.0f;
	XVT_ASSERT_TRUE(xvt_video_settings_validate(&v, error, sizeof error));
	v.paper_white_nits = -1.0f;
	expect_invalid(&v, "paper_white_nits -1");
	v.paper_white_nits = NAN;
	expect_invalid(&v, "paper_white_nits NaN");
	/* The temporal mode is one of the modes. */
	v = good;
	v.fsr_mode = (AeronTemporalMode)99;
	expect_invalid(&v, "fsr_mode 99");
	/* MSAA is 1, 2, 4 or 8. */
	const int counts[] = {1, 2, 4, 8};
	for (int i = 0; i < 4; ++i) {
		v = good;
		v.msaa_samples = counts[i];
		XVT_ASSERT_TRUE(
			xvt_video_settings_validate(&v, error, sizeof error));
	}
	v = good;
	v.msaa_samples = 3;
	expect_invalid(&v, "msaa_samples 3");
	/* Temporal upscaling other than off needs MSAA 1. */
	v = good;
	v.fsr_mode = AERON_TEMPORAL_QUALITY;
	v.msaa_samples = 2;
	expect_invalid(&v, "temporal upscaling with msaa_samples 2");
	v.msaa_samples = 1;
	XVT_ASSERT_TRUE(xvt_video_settings_validate(&v, error, sizeof error));
	fixture_end();
}

static void check_apply_to(void)
{
	static struct xvt_render_settings render;
	memset(&render, 0, sizeof render);
	render.anisotropic = 7;
	render.bloom_intensity = 0.5f;
	const struct xvt_video_settings v = distinct();
	xvt_video_settings_apply_to(&v, &render);
	XVT_ASSERT_INT_EQ(render.cockpit_undither, v.cockpit_undither);
	XVT_ASSERT_INT_EQ(render.presentation.hdr_output, v.hdr);
	XVT_ASSERT_CLOSE(render.presentation.sdr_gamma, v.sdr_gamma, 0,
			 "a copied float is exact");
	XVT_ASSERT_CLOSE(render.presentation.paper_white_nits,
			 v.paper_white_nits, 0, "a copied float is exact");
	XVT_ASSERT_INT_EQ(render.scene.ssao.ssao_quality, v.ssao_quality);
	XVT_ASSERT_INT_EQ(render.scene.shadows.enabled, v.shadows_enabled);
	XVT_ASSERT_INT_EQ(render.scene.shadows.atlas_size, v.shadow_atlas_size);
	XVT_ASSERT_INT_EQ(render.temporal_mode, v.fsr_mode);
	XVT_ASSERT_CLOSE(render.temporal_sharpness, v.fsr_sharpness, 0,
			 "a copied float is exact");
	XVT_ASSERT_INT_EQ(render.msaa_samples, v.msaa_samples);
	XVT_ASSERT_INT_EQ(render.motion_blur.quality, v.motion_blur_quality);
	XVT_ASSERT_CLOSE(render.motion_blur.shutter, v.motion_blur_shutter, 0,
			 "a copied float is exact");
	/* Render settings the video page does not edit are left alone. */
	XVT_ASSERT_INT_EQ(render.anisotropic, 7);
	XVT_ASSERT_CLOSE(render.bloom_intensity, 0.5f, 0,
			 "an untouched float is exact");

	/* Applying a record and reading it back, with fullscreen kept outside the render settings, gives the
	 * record again. */
	static struct xvt_settings settings;
	memset(&settings, 0, sizeof settings);
	settings.fullscreen = v.fullscreen;
	xvt_video_settings_apply_to(&v, &settings.render);
	struct xvt_video_settings back;
	xvt_video_settings_read(&settings, &back);
	XVT_ASSERT_TRUE(xvt_video_settings_equals(&back, &v));
}

static void check_set_and_restore(void)
{
	fixture_begin();
	char error[512];
	const struct xvt_video_settings shipped = shipped_record();
	/* Needs loaded settings. */
	XVT_ASSERT_TRUE(!xvt_config_set_video(&shipped, error, sizeof error));
	XVT_ASSERT_TRUE(!xvt_config_restore_video(error, sizeof error));

	fixture_load();
	struct xvt_video_settings defaults;
	struct xvt_video_settings now;
	xvt_video_settings_read(xvt_config_default_settings(), &defaults);

	/* Every field is written, even one equal to the shipped value: the settings.h paths of the window
	 * mode, the presentation choices, the temporal mode and MSAA all appear in the user overrides. */
	uint64_t generation = xvt_config_generation();
	XVT_ASSERT_TRUE(xvt_config_set_video(&defaults, error, sizeof error));
	XVT_ASSERT_TRUE(xvt_config_generation() > generation);
	const AeronConfigFile *user = xvt_config_user_document();
	XVT_ASSERT_TRUE(AeronConfigFile_Has(user, "video.window_mode"));
	XVT_ASSERT_TRUE(AeronConfigFile_Has(user, "presentation.sdr_gamma"));
	XVT_ASSERT_TRUE(
		AeronConfigFile_Has(user, "presentation.paper_white_nits"));
	XVT_ASSERT_TRUE(
		AeronConfigFile_Has(user, "render.temporal_upscaling.mode"));
	XVT_ASSERT_TRUE(AeronConfigFile_Has(user, "render.msaa_samples"));
	xvt_video_settings_read(xvt_config_settings(), &now);
	XVT_ASSERT_TRUE(xvt_video_settings_equals(&now, &defaults));

	/* A different record is stored and read back from the resolved settings; memory only. */
	const struct xvt_video_settings changed = changed_from(&defaults);
	XVT_ASSERT_TRUE(!xvt_video_settings_equals(&changed, &defaults));
	XVT_ASSERT_TRUE(xvt_config_set_video(&changed, error, sizeof error));
	xvt_video_settings_read(xvt_config_settings(), &now);
	XVT_ASSERT_TRUE(xvt_video_settings_equals(&now, &changed));
	XVT_ASSERT_TRUE(!fixture_exists("user/config.yaml"));

	/* An invalid record is refused before anything changes. */
	generation = xvt_config_generation();
	AeronConfigFile *before = fixture_user_copy();
	struct xvt_video_settings bad = changed;
	bad.fsr_mode = AERON_TEMPORAL_PERFORMANCE;
	bad.msaa_samples = 2;
	XVT_ASSERT_TRUE(!xvt_config_set_video(&bad, error, sizeof error));
	XVT_ASSERT_INT_EQ(xvt_config_generation(), generation);
	XVT_ASSERT_TRUE(
		fixture_same_document(xvt_config_user_document(), before));
	AeronConfigFile_Destroy(before);

	/* Restoring drops the video overrides, so the shipped values apply again; memory only. */
	XVT_ASSERT_TRUE(xvt_config_restore_video(error, sizeof error));
	user = xvt_config_user_document();
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "video.window_mode"));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "presentation.sdr_gamma"));
	XVT_ASSERT_TRUE(!AeronConfigFile_Has(user, "render.msaa_samples"));
	xvt_video_settings_read(xvt_config_settings(), &now);
	XVT_ASSERT_TRUE(xvt_video_settings_equals(&now, &defaults));
	XVT_ASSERT_TRUE(!fixture_exists("user/config.yaml"));
	fixture_end();
}

int main(void)
{
	check_read();
	check_equals();
	check_validate();
	check_apply_to();
	check_set_and_restore();
	return 0;
}
