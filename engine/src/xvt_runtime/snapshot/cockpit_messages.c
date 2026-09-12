#include "xvt_runtime/snapshot/cockpit_messages.h"

#include "aeron/aeron.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/player/player.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/snapshot/cockpit_text.h"
#include "xvt_runtime/snapshot/render_hud.h"
#include <string.h>

typedef char XvtCockpitOverlayCapacityCheck[XVT_HUD_OVERLAY_GLYPHS ==
													XVT_COCKPIT_MESSAGE_COUNT * XVT_HUD_MESSAGE_GLYPHS +
														3 * XVT_HUD_ALERT_LINE_GLYPHS + XVT_HUD_LOADING_GLYPHS
												? 1
												: -1];

typedef struct MessageContent {
	XvtCockpitMessage state;
	int16_t origin_x, origin_y;
	XvtCockpitGlyph glyphs[XVT_HUD_MESSAGE_GLYPHS];
} MessageContent;

typedef struct AlertContent {
	XvtCockpitAlert state;
	XvtCockpitGlyph glyphs[3][XVT_HUD_ALERT_LINE_GLYPHS];
} AlertContent;

static MessageContent g_messages[XVT_COCKPIT_MESSAGE_COUNT], g_pending[XVT_COCKPIT_MESSAGE_COUNT],
	g_messageBuild;
static AlertContent g_alert, g_alertBuild;
static XvtCockpitLoading g_loading;
static int g_messageCapture = -1, g_alertCapture = -1, g_captureFailed;
static uint32_t g_messagePalette[256], g_alertPalette[256];
static uint64_t g_generation;

/* Loading text is emitted before mission initialization resets flight state. */
static struct {
	XvtCockpitGlyph glyphs[XVT_HUD_LOADING_GLYPHS];
	uint32_t palette[256];
	XvtSnapRect bounds;
	uint64_t generation;
	uint16_t count;
	uint8_t capturing, visible;
} g_loadingText;

static void CapturePalette(uint32_t palette[256]) {
	for (unsigned index = 0; index < 256; ++index)
		palette[index] = XvtRenderDraw_Color(index);
}

void XvtCockpitMessages_ResetWorking(void) {
	memset(g_messages, 0, sizeof g_messages);
	g_messageCapture = -1;
}

void XvtCockpitMessages_Reset(void) {
	XvtCockpitMessages_ResetWorking();
	memset(g_pending, 0, sizeof g_pending);
	memset(&g_alert, 0, sizeof g_alert);
	memset(&g_loading, 0, sizeof g_loading);
	g_alertCapture = -1;
	g_captureFailed = 0;
}

void XvtCockpitMessages_BeginFlightFrame(void) {
	g_loading.visible = 0;
	g_loadingText.visible = 0;
}

void XvtCockpitMessages_BeginLoadingText(void) {
	g_loadingText.count = 0;
	g_loadingText.visible = 0;
	g_loadingText.capturing = 1;
	g_loadingText.bounds = (XvtSnapRect) { 0, 0, (int)g_screenWidth, (int)g_screenHeight };
	CapturePalette(g_loadingText.palette);
}

void XvtCockpitMessages_EndLoadingText(void) {
	g_loadingText.capturing = 0;
	g_loadingText.visible = g_loadingText.count != 0;
	g_loadingText.generation = ++g_generation;
}

void XvtCockpitMessages_Clear(XvtCockpitMessageId pane) {
	if ((unsigned)pane >= XVT_COCKPIT_MESSAGE_COUNT || !g_messages[pane].state.visible)
		return;
	memset(&g_messages[pane], 0, sizeof g_messages[pane]);
	g_messages[pane].state.generation = ++g_generation;
}

void XvtCockpitMessages_BeginMessage(int pane_type) {
	XvtCockpitMessageId pane =
		pane_type == 8 ? XVT_COCKPIT_MESSAGE_FLIGHT_GROUP
					   : (pane_type == 3 || pane_type == 4 || pane_type == 7 ? XVT_COCKPIT_MESSAGE_SYSTEM
																			 : XVT_COCKPIT_MESSAGE_READY);
	const HudInFlightMessageRecord* message =
		pane == XVT_COCKPIT_MESSAGE_READY
			? &g_readyMessagePaneQueue[0]
			: (pane == XVT_COCKPIT_MESSAGE_SYSTEM ? &g_systemMessagePane : &g_flightGroupMessagePane);
	memset(&g_messageBuild, 0, sizeof g_messageBuild);
	g_messageBuild.origin_x = g_flightClipLeft;
	g_messageBuild.origin_y = g_flightClipTop;
	g_messageBuild.state.visible = 1;
	g_messageBuild.state.message_id = message->stateOrMessageId;
	g_messageBuild.state.sender_iff = message->senderIff;
	g_messageBuild.state.pane_type = (uint8_t)pane_type;
	g_messageBuild.state.font_tier = g_flightFontTier;
	g_messageBuild.state.age_ticks = message->ageTicks;
	g_messageCapture = pane;
	g_captureFailed = 0;
	CapturePalette(g_messagePalette);
}

void XvtCockpitMessages_RecordReveal(unsigned characters) {
	if (g_messageCapture >= 0)
		g_messageBuild.state.revealed_characters = (uint16_t)characters;
}

void XvtCockpitMessages_EndMessage(void) {
	if (g_messageCapture < 0)
		return;
	unsigned pane = (unsigned)g_messageCapture;
	if (!g_captureFailed) {
		g_messageBuild.state.generation = g_messages[pane].state.generation;
		if (!g_messages[pane].state.visible || g_messageBuild.origin_x != g_messages[pane].origin_x ||
			g_messageBuild.origin_y != g_messages[pane].origin_y ||
			g_messageBuild.state.glyph_count != g_messages[pane].state.glyph_count ||
			memcmp(g_messageBuild.glyphs, g_messages[pane].glyphs,
				   g_messageBuild.state.glyph_count * sizeof g_messageBuild.glyphs[0]))
			g_messageBuild.state.generation = ++g_generation;
		g_messages[pane] = g_messageBuild;
	}
	g_messageCapture = -1;
}

void XvtCockpitMessages_BeginPlacement(void) {
	for (unsigned pane = 0; pane < XVT_COCKPIT_MESSAGE_COUNT; ++pane)
		g_pending[pane].state.visible = 0;
}

void XvtCockpitMessages_Latch(XvtCockpitMessageId pane, int source_x, int source_y, int x, int y, int width,
							  int height) {
	if ((unsigned)pane >= XVT_COCKPIT_MESSAGE_COUNT)
		return;
	MessageContent* destination = &g_pending[pane];
	const MessageContent* source = &g_messages[pane];
	destination->state = source->state;
	destination->state.placement = (XvtSnapRect) { x, y, width, height };
	const HudInFlightMessageRecord* message =
		pane == XVT_COCKPIT_MESSAGE_READY
			? &g_readyMessagePaneQueue[0]
			: (pane == XVT_COCKPIT_MESSAGE_SYSTEM ? &g_systemMessagePane : &g_flightGroupMessagePane);
	if (message->stateOrMessageId == destination->state.message_id)
		destination->state.age_ticks = message->ageTicks;
	destination->state.timer_ticks =
		pane == XVT_COCKPIT_MESSAGE_READY
			? g_playerFlightTransientTimers[g_localPlayer].readyMessagePaneTimer
			: (pane == XVT_COCKPIT_MESSAGE_SYSTEM
				   ? g_playerFlightTransientTimers[g_localPlayer].systemMessagePaneTimer
				   : g_playerFlightTransientTimers[g_localPlayer].flightGroupMessagePaneTimer);
	int offset_x = source->origin_x - source_x, offset_y = source->origin_y - source_y;
	for (unsigned index = 0; index < source->state.glyph_count; ++index) {
		XvtCockpitGlyph* glyph = &destination->glyphs[index];
		*glyph = source->glyphs[index];
		glyph->x += (int16_t)offset_x;
		glyph->y += (int16_t)offset_y;
		glyph->clip.x += offset_x;
		glyph->clip.y += offset_y;
	}
}

void XvtCockpitMessages_RecordGlyph(unsigned character, unsigned advance, unsigned height, int narrow) {
	XvtCockpitGlyph glyph;
	if (g_loadingText.capturing) {
		if (!XvtCockpitText_CaptureGlyph(&glyph, character, advance, height, narrow, 0, 0,
										 g_loadingText.palette, 0))
			return;
		if (g_loadingText.count == XVT_HUD_LOADING_GLYPHS) {
			Aeron_RequestFatalError("Loading text capture", "The loading text exceeds its glyph capacity.");
			return;
		}
		g_loadingText.glyphs[g_loadingText.count++] = glyph;
		return;
	}
	if (g_messageCapture >= 0) {
		if (!XvtCockpitText_CaptureGlyph(&glyph, character, advance, height, narrow, g_messageBuild.origin_x,
										 g_messageBuild.origin_y, g_messagePalette, 1))
			return;
		if (g_messageBuild.state.glyph_count == XVT_HUD_MESSAGE_GLYPHS) {
			Aeron_LogError("xvt.snapshot", "message pane %d exceeds glyph capacity", g_messageCapture);
			g_captureFailed = 1;
			return;
		}
		g_messageBuild.glyphs[g_messageBuild.state.glyph_count++] = glyph;
	} else if (g_alertCapture >= 0) {
		if (!XvtCockpitText_CaptureGlyph(&glyph, character, advance, height, narrow,
										 g_alertBuild.state.placement.x, g_alertBuild.state.placement.y,
										 g_alertPalette, 0))
			return;
		uint16_t* count = &g_alertBuild.state.glyph_count[g_alertCapture];
		if (*count == XVT_HUD_ALERT_LINE_GLYPHS) {
			Aeron_LogError("xvt.snapshot", "alert line %d exceeds glyph capacity", g_alertCapture);
			g_captureFailed = 1;
			return;
		}
		g_alertBuild.glyphs[g_alertCapture][(*count)++] = glyph;
	}
}

void XvtCockpitMessages_BeginAlert(void) {
	memset(&g_alert, 0, sizeof g_alert);
	g_alertCapture = -1;
}

void XvtCockpitMessages_BeginAlertLine(int mode, int x, int y, int width, int height) {
	if (mode < 0 || mode > 3)
		return;
	g_alertBuild = g_alert;
	if (!g_alert.state.active)
		memset(&g_alertBuild, 0, sizeof g_alertBuild);
	g_alertBuild.state.active = 1;
	g_alertBuild.state.placement = (XvtSnapRect) { x, y, width, height };
	CapturePalette(g_alertPalette);
	unsigned first_row = mode <= 1 ? 0 : (unsigned)mode;
	if (mode == 1)
		g_alertBuild.state.border_argb = g_alertPalette[g_flightTextBgColor];
	/* The original changes the inner color immediately after its optional border fill. */
	for (unsigned row = first_row; row < 5; ++row)
		g_alertBuild.state.row_background_argb[row] = 0;
	for (unsigned line = first_row ? first_row - 1 : 0; line < 3; ++line) {
		g_alertBuild.state.line_visible[line] = 0;
		g_alertBuild.state.glyph_count[line] = 0;
		memset(g_alertBuild.glyphs[line], 0, sizeof g_alertBuild.glyphs[line]);
	}
	g_alertCapture = mode <= 1 ? 0 : mode - 1;
	g_alertBuild.state.line_visible[g_alertCapture] = 1;
	g_captureFailed = 0;
}

void XvtCockpitMessages_EndAlertLine(void) {
	if (g_alertCapture < 0)
		return;
	unsigned first_row = g_alertCapture == 0 ? 0 : (unsigned)g_alertCapture + 1;
	for (unsigned row = first_row; row < 5; ++row)
		g_alertBuild.state.row_background_argb[row] = g_alertPalette[g_flightTextBgColor];
	if (!g_captureFailed) {
		g_alertBuild.state.generation = g_alert.state.generation;
		if (memcmp(&g_alertBuild, &g_alert, sizeof g_alertBuild))
			g_alertBuild.state.generation = ++g_generation;
		g_alert = g_alertBuild;
	}
	g_alertCapture = -1;
}

void XvtCockpitMessages_EndAlert(void) {
	if (g_alert.state.active) {
		g_alert.state.active = 0;
		g_alert.state.generation = ++g_generation;
	}
}

void XvtCockpitMessages_RecordProgress(unsigned step, int x, int y, int width, int height, int filled_width) {
	XvtCockpitLoading next;
	memset(&next, 0, sizeof next);
	next.visible = 1;
	next.placement = (XvtSnapRect) { x, y, width, height };
	next.progress_step = (uint16_t)step;
	next.filled_width = (uint16_t)filled_width;
	next.foreground_argb = XvtRenderDraw_Color(g_flightTextBgColor);
	next.background_argb = XvtRenderDraw_Color(0);
	next.generation = g_loading.generation;
	if (memcmp(&next, &g_loading, sizeof next))
		next.generation = ++g_generation;
	g_loading = next;
}

void XvtCockpitMessages_ClearProgress(void) {
	g_loading.visible = 0;
	memset(&g_loadingText, 0, sizeof g_loadingText);
}

void XvtCockpitMessages_Export(XvtCockpitState* state) {
	XvtCockpitOverlayStore* store = &state->overlay_content;
	store->glyph_count = 0;
	for (unsigned pane = 0; pane < XVT_COCKPIT_MESSAGE_COUNT; ++pane) {
		XvtCockpitMessage* message = &state->messages.panes[pane];
		*message = g_pending[pane].state;
		message->first_glyph = store->glyph_count;
		if (!message->visible) {
			message->glyph_count = 0;
			continue;
		}
		memcpy(&store->glyphs[store->glyph_count], g_pending[pane].glyphs,
			   message->glyph_count * sizeof store->glyphs[0]);
		store->glyph_count += message->glyph_count;
	}
	state->alert = g_alert.state;
	for (unsigned line = 0; line < 3; ++line) {
		state->alert.first_glyph[line] = store->glyph_count;
		if (!state->alert.active || !state->alert.line_visible[line]) {
			state->alert.glyph_count[line] = 0;
			continue;
		}
		memcpy(&store->glyphs[store->glyph_count], g_alert.glyphs[line],
			   state->alert.glyph_count[line] * sizeof store->glyphs[0]);
		store->glyph_count += state->alert.glyph_count[line];
	}
	state->loading = g_loading;
	state->loading.text_generation = g_loadingText.generation;
	state->loading.text_visible = g_loadingText.visible;
	state->loading.text_bounds = g_loadingText.bounds;
	state->loading.first_glyph = store->glyph_count;
	if (g_loadingText.visible) {
		state->loading.glyph_count = g_loadingText.count;
		memcpy(&store->glyphs[store->glyph_count], g_loadingText.glyphs,
			   g_loadingText.count * sizeof *store->glyphs);
		store->glyph_count += g_loadingText.count;
	}
	if (state->alert.active || state->loading.visible || state->loading.text_visible) {
		state->view.screen_width = (uint16_t)g_screenWidth;
		state->view.screen_height = (uint16_t)g_screenHeight;
	}
}
