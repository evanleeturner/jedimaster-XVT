#include "xvt_app/settings/video_page.h"
#include "aeron/aeron.h"
#include "xvt_app/settings/settings.h"
#include "xvt_app/settings/video_options.h"
#include "xvt_remaster/hud_assets.h"
#include <stdio.h>

/* Draws the whole video page against one copy of the settings: every control edits that copy and marks
 * it changed, and the copy is sent once at the end, only if something changed. The sections stay in one
 * function so that cycle reads top to bottom instead of passing through a helper per section. */
static void xvt_video_page_draw_controls(AeronUiContext *ui)
{
	struct xvt_video_settings options;
	xvt_video_options_get(&options);
	bool changed = false;
	AeronUi_Header(ui, "Display");
	int fullscreen = options.fullscreen;
	if (AeronUi_Toggle(ui, "Fullscreen", &fullscreen)) {
		options.fullscreen = fullscreen != 0;
		changed = true;
	}
	int hdr = options.hdr;
	if (AeronUi_Toggle(ui, "HDR Output", &hdr)) {
		options.hdr = hdr != 0;
		changed = true;
	}
	if (options.hdr && !Aeron_OutputHdrEnabled()) {
		AeronUi_Help(
			ui, Aeron_OutputHdrStatusName(Aeron_OutputHdrStatus()));
	}
	AeronUi_Spacer(ui, 8.0f);
	AeronUi_Header(ui, "HDR Presentation");

	static const char *const gamma_labels[] = {"2.2", "2.4", "sRGB",
						   "Auto"};
	static const float gamma_values[] = {2.2f, 2.4f, 0, -1};
	int gamma_index = options.sdr_gamma < 0	      ? 3
			  : options.sdr_gamma == 0    ? 2
			  : options.sdr_gamma == 2.4f ? 1
						      : 0;
#if defined(__APPLE__)
	const int hdr_output_active = 0;
#else
	const int hdr_output_active = Aeron_OutputHdrEnabled();
#endif
	if (AeronUi_SelectorEnabled(ui, "SDR Content Gamma", &gamma_index,
				    gamma_labels, 4, hdr_output_active)) {
		options.sdr_gamma = gamma_values[gamma_index];
		changed = true;
	}
	static const float paper_white_values[] = {
		0.0f, 100.0f, 150.0f, 200.0f, 250.0f, 300.0f, 400.0f};
	const char *paper_white_labels[] = {"Auto",	"100 nits", "150 nits",
					    "200 nits", "250 nits", "300 nits",
					    "400 nits", NULL};
	char custom_white[32];
	int white_count = 7;
	int paper_white_index = 0;
	if (options.paper_white_nits > 0) {
		paper_white_index = 1;
		float best_distance = options.paper_white_nits -
				      paper_white_values[paper_white_index];
		if (best_distance < 0.0f) {
			best_distance = -best_distance;
		}
		for (int index = 2; index < 7; ++index) {
			float distance = options.paper_white_nits -
					 paper_white_values[index];
			if (distance < 0.0f) {
				distance = -distance;
			}
			if (distance < best_distance) {
				paper_white_index = index;
				best_distance = distance;
			}
		}
	}
	if (options.paper_white_nits != paper_white_values[paper_white_index]) {
		snprintf(custom_white, sizeof custom_white, "%.1f nits",
			 options.paper_white_nits);
		paper_white_labels[7] = custom_white;
		paper_white_index = 7;
		white_count = 8;
	}
	if (AeronUi_SelectorEnabled(ui, "HDR Paper White", &paper_white_index,
				    paper_white_labels, white_count,
				    hdr_output_active)) {
		if (paper_white_index < 7) {
			options.paper_white_nits =
				paper_white_values[paper_white_index];
		}
		changed = true;
	}
	AeronUi_Spacer(ui, 8.0f);
	AeronUi_Header(ui, "Flight Rendering");
	if (AeronUi_Toggle(ui, "Undither Cockpit Artwork",
			   &options.cockpit_undither)) {
		changed = true;
	}
	if (xvt_hud_assets_undither_pending(options.cockpit_undither)) {
		AeronUi_Help(
			ui,
			"This change will take effect on the next cockpit load.");
	}
	static const char *const quality_labels[] = {"Off", "Low", "High"};
	int ssao_quality = options.ssao_quality;
	if (AeronUi_Selector(ui, "SSAO Quality", &ssao_quality, quality_labels,
			     3)) {
		options.ssao_quality = ssao_quality;
		changed = true;
	}
	static const int shadow_atlas_values[] = {4096, 8192};
	const char *shadow_quality_labels[] = {"Standard", "High", NULL};
	char custom_shadow[48];
	int shadow_count = 2;
	int shadow_quality = options.shadow_atlas_size >= 8192 ? 1 : 0;
	if (!options.shadows_enabled || (options.shadow_atlas_size != 4096 &&
					 options.shadow_atlas_size != 8192)) {
		snprintf(custom_shadow, sizeof custom_shadow,
			 options.shadows_enabled ? "%d (configured)"
						 : "Off (configured)",
			 options.shadow_atlas_size);
		shadow_quality_labels[2] = custom_shadow;
		shadow_count = 3;
		shadow_quality = 2;
	}
	if (AeronUi_Selector(ui, "Shadow Quality", &shadow_quality,
			     shadow_quality_labels, shadow_count)) {
		if (shadow_quality < 2) {
			options.shadows_enabled = true;
			options.shadow_atlas_size =
				shadow_atlas_values[shadow_quality];
		}
		changed = true;
	}

	static const AeronTemporalMode fsr_values[] = {
		AERON_TEMPORAL_OFF, AERON_TEMPORAL_PERFORMANCE,
		AERON_TEMPORAL_BALANCED, AERON_TEMPORAL_QUALITY,
		AERON_TEMPORAL_NATIVE_AA};
	static const char *const fsr_labels[] = {
		"Off", "Performance", "Balanced", "Quality", "Native AA"};
	int fsr_index = 0;
	while (fsr_index < 4 && fsr_values[fsr_index] != options.fsr_mode) {
		++fsr_index;
	}
	if (AeronUi_Selector(ui, "FSR Upscaling", &fsr_index, fsr_labels, 5)) {
		options.fsr_mode = fsr_values[fsr_index];
		if (options.fsr_mode != AERON_TEMPORAL_OFF) {
			options.msaa_samples = 1;
		}
		changed = true;
	}

	static const int sample_values[] = {1, 2, 4, 8};
	static const char *const sample_labels[] = {"Off", "2x", "4x", "8x"};
	int sample_index = 0;
	while (sample_index < 3 &&
	       sample_values[sample_index] != options.msaa_samples) {
		++sample_index;
	}
	if (AeronUi_Selector(ui, "MSAA", &sample_index, sample_labels, 4)) {
		options.msaa_samples = sample_values[sample_index];
		if (options.msaa_samples > 1) {
			options.fsr_mode = AERON_TEMPORAL_OFF;
		}
		changed = true;
	}

	static const char *const motion_blur_labels[] = {"Off", "Low Quality",
							 "High Quality"};
	int blur = options.motion_blur_quality;
	if (AeronUi_Selector(ui, "Motion Blur", &blur, motion_blur_labels, 3)) {
		options.motion_blur_quality = blur;
		changed = true;
	}
	if (blur > 0) {
		int amount = (int)(options.motion_blur_shutter * 100.0f + 0.5f);
		if (amount < 0) {
			amount = 0;
		}
		if (amount > 100) {
			amount = 100;
		}
		if (AeronUi_SliderInt(ui, "Motion Blur Amount", &amount, 0, 100,
				      10, "%d%%")) {
			options.motion_blur_shutter = (float)amount / 100.0f;
			changed = true;
		}
	}
	if (changed) {
		char error[512];
		if (!xvt_video_options_request(&options, error, sizeof error)) {
			xvt_settings_menu_report_error(error);
		}
	}
	AeronUi_Spacer(ui, 8.0f);
	if (AeronUi_Button(ui, "Restore Defaults")) {
		xvt_video_options_restore_defaults();
	}
}

void xvt_video_page_draw(AeronUiContext *ui, const AeronInputSnapshot *input)
{
	(void)input;
	float height = AeronUi_AvailableHeight(ui) - 140;
	if (AeronUi_BeginScroll(ui, "Video Settings",
				height > 180 ? height : 180)) {
		xvt_video_page_draw_controls(ui);
		AeronUi_EndScroll(ui);
	}
}
