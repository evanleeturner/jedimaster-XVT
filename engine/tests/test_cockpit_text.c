/* Checks the named cockpit text fields (xvt_runtime/snapshot/cockpit_text.h) against the promises in its
 * header: the inline color map, recording a field from the live flight text state and when its generation
 * rises, clearing fields, which fields CopyFields hides, a field copied to a new place, and capturing one
 * glyph against the clip rectangle. The test sets the flight text globals and the palette itself; every
 * case starts from cleared fields and the same text state.
 *
 * Not checked here: color keying in RecordField (the header does not say when the live state is keyed),
 * and how CopyFields treats a field owned by a covered target panel or a shield outside text mode, which
 * the header leaves open. */
#include "test_assert.h"
#include "xvt/flight/hud/flight_text.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/render/flight_palette.h"
#include "xvt/render/flight_sw.h"
#include "xvt/render/renderer.h"
#include "xvt_runtime/snapshot/cockpit_text.h"
#include "xvt_runtime/snapshot/render_hud.h"

#include <string.h>

enum { BYPASS = 9, FOREGROUND = 7, BACKGROUND = 4, SHADOW = 5 };

static XvtCockpitState g_state;
static uint8_t g_framebuffer[16];

/* Every palette entry gets its own color: red is the index's low 6 bits, green the high 2. */
static void Palette(void)
{
	for (unsigned index = 0; index < 256; ++index) {
		g_swPalette[index] = (RgbTriplet){(uint8_t)(index & 63),
						  (uint8_t)(index >> 6), 7};
	}
}

/* Clip (10, 20) to (210, 120), cursor (30, 40), font tier 1, shadow on, wrap on; the framebuffer is not
 * the offscreen buffer. Every field is cleared. */
static void Start(void)
{
	Palette();
	g_flightClipLeft = 10;
	g_flightClipTop = 20;
	g_flightClipRight = 210;
	g_flightClipBottom = 120;
	g_flightCursorX = 30;
	g_flightCursorY = 40;
	g_flightFontTier = 1;
	g_flightFontHasLowercase = 0;
	g_flightTextColorIndex = FOREGROUND;
	g_flightTextBgColor = BACKGROUND;
	g_flightTextShadowColor = SHADOW;
	g_flightTextShadowEnabled = 1;
	g_flightWordWrapEnabled = 1;
	g_flightClearLineBgEnabled = 0;
	g_flightTransparentColorIndex = BYPASS;
	g_flightDrawCharFn = NULL;
	g_flightOffscreenBuffer = NULL;
	g_flightSwFramebufferBase = g_framebuffer;
	XvtCockpitText_ResetFields();
	memset(&g_state, 0, sizeof g_state);
}

/* The fields as CopyFields hands them out, in the forward view with every owner shown. */
static const XvtCockpitTextField *Fields(void)
{
	memset(&g_state, 0, sizeof g_state);
	g_state.view.hud_state = HUD_VIEW_FORWARD;
	XvtCockpitText_CopyFields(&g_state);
	return g_state.text_fields;
}

static void CheckResolveColor(void)
{
	Start();
	/* Codes from 0x40 go through the table, which has 32 entries. */
	for (unsigned index = 0; index < 32; ++index) {
		XVT_ASSERT_INT_EQ(
			XvtCockpitText_ResolveColor((uint8_t)(0x40 + index), 0),
			g_flightCharToColorLut[index]);
	}
	/* The bypass code is returned unchanged, even inside the table's range. */
	XVT_ASSERT_INT_EQ(XvtCockpitText_ResolveColor(0x41, 0x41), 0x41);
	/* Any other code is returned unchanged. */
	XVT_ASSERT_INT_EQ(XvtCockpitText_ResolveColor(0x3F, 0), 0x3F);
	XVT_ASSERT_INT_EQ(XvtCockpitText_ResolveColor(0x05, 0), 0x05);
	XVT_ASSERT_INT_EQ(XvtCockpitText_ResolveColor(0x60, 0), 0x60);
	XVT_ASSERT_INT_EQ(XvtCockpitText_ResolveColor(0xFE, 0), 0xFE);
}

static void CheckRecordFieldTakesLiveState(void)
{
	Start();
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS, "READY",
				   XVT_COCKPIT_ALIGN_LEFT);
	const XvtCockpitTextField *field =
		&Fields()[XVT_COCKPIT_TEXT_CRAFT_STATUS];
	XVT_ASSERT_INT_EQ(strcmp(field->caption.text, "READY"), 0);
	XVT_ASSERT_INT_EQ(field->caption.visible, 1);
	XVT_ASSERT_INT_EQ(field->caption.alignment, XVT_COCKPIT_ALIGN_LEFT);
	XVT_ASSERT_INT_EQ(field->caption.font_tier, 1);
	XVT_ASSERT_INT_EQ(field->caption.foreground, FOREGROUND);
	XVT_ASSERT_INT_EQ(field->caption.background, BACKGROUND);
	XVT_ASSERT_INT_EQ(field->bounds.x, 10);
	XVT_ASSERT_INT_EQ(field->bounds.y, 20);
	XVT_ASSERT_INT_EQ(field->bounds.width, 200);
	XVT_ASSERT_INT_EQ(field->bounds.height, 100);
	XVT_ASSERT_INT_EQ(field->x, 30);
	XVT_ASSERT_INT_EQ(field->y, 40);
	XVT_ASSERT_INT_EQ(field->shadow_enabled, 1);
	XVT_ASSERT_INT_EQ(field->shadow_color, SHADOW);
	XVT_ASSERT_INT_EQ(field->word_wrap, 1);
	XVT_ASSERT_TRUE(field->generation > 0);
}

static void CheckRecordFieldColorCodes(void)
{
	Start();
	/* An inline 0xFE code is resolved, and when it leads the text it sets the foreground. */
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS, "\xFE\x42Z",
				   XVT_COCKPIT_ALIGN_LEFT);
	const XvtCockpitTextField *field =
		&Fields()[XVT_COCKPIT_TEXT_CRAFT_STATUS];
	XVT_ASSERT_INT_EQ((uint8_t)field->caption.text[1],
			  g_flightCharToColorLut[2]);
	XVT_ASSERT_INT_EQ(field->caption.text[2], 'Z');
	XVT_ASSERT_INT_EQ(field->caption.foreground, g_flightCharToColorLut[2]);

	/* An inline code later in the text is resolved but leaves the foreground alone. */
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS, "ab\xFE\x43",
				   XVT_COCKPIT_ALIGN_LEFT);
	field = &Fields()[XVT_COCKPIT_TEXT_CRAFT_STATUS];
	XVT_ASSERT_INT_EQ((uint8_t)field->caption.text[3],
			  g_flightCharToColorLut[3]);
	XVT_ASSERT_INT_EQ(field->caption.foreground, FOREGROUND);

	/* A leading byte below 0x10 sets the foreground. */
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS,
				   "\x0B"
				   "abc",
				   XVT_COCKPIT_ALIGN_LEFT);
	XVT_ASSERT_INT_EQ(
		Fields()[XVT_COCKPIT_TEXT_CRAFT_STATUS].caption.foreground,
		0x0B);

	/* The bypass code is not resolved. */
	g_flightTransparentColorIndex = 0x44;
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS, "\xFE\x44Z",
				   XVT_COCKPIT_ALIGN_LEFT);
	field = &Fields()[XVT_COCKPIT_TEXT_CRAFT_STATUS];
	XVT_ASSERT_INT_EQ((uint8_t)field->caption.text[1], 0x44);
	XVT_ASSERT_INT_EQ(field->caption.foreground, 0x44);
}

static void CheckRecordFieldGeneration(void)
{
	Start();
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS, "AB",
				   XVT_COCKPIT_ALIGN_LEFT);
	uint64_t first = Fields()[XVT_COCKPIT_TEXT_CRAFT_STATUS].generation;

	/* The same text in the same state changes nothing. */
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS, "AB",
				   XVT_COCKPIT_ALIGN_LEFT);
	XVT_ASSERT_INT_EQ(Fields()[XVT_COCKPIT_TEXT_CRAFT_STATUS].generation,
			  first);

	/* New text, or the same text at a new cursor, is a change. */
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS, "AC",
				   XVT_COCKPIT_ALIGN_LEFT);
	uint64_t second = Fields()[XVT_COCKPIT_TEXT_CRAFT_STATUS].generation;
	XVT_ASSERT_TRUE(second > first);
	g_flightCursorY = 41;
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS, "AC",
				   XVT_COCKPIT_ALIGN_LEFT);
	XVT_ASSERT_TRUE(Fields()[XVT_COCKPIT_TEXT_CRAFT_STATUS].generation >
			second);
}

static void CheckRecordFieldRefusals(void)
{
	Start();
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS, "KEEP",
				   XVT_COCKPIT_ALIGN_LEFT);
	uint64_t generation =
		Fields()[XVT_COCKPIT_TEXT_CRAFT_STATUS].generation;

	/* NULL text and an out-of-range field are ignored. */
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS, NULL,
				   XVT_COCKPIT_ALIGN_LEFT);
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_FIELD_COUNT, "X",
				   XVT_COCKPIT_ALIGN_LEFT);
	XvtCockpitText_RecordField((XvtCockpitTextFieldId)-1, "X",
				   XVT_COCKPIT_ALIGN_LEFT);
	const XvtCockpitTextField *field =
		&Fields()[XVT_COCKPIT_TEXT_CRAFT_STATUS];
	XVT_ASSERT_INT_EQ(strcmp(field->caption.text, "KEEP"), 0);
	XVT_ASSERT_INT_EQ(field->generation, generation);

	/* The caption holds 255 characters and its terminator; 256 do not fit and are ignored. */
	char text[257];
	memset(text, 'w', 256);
	text[256] = 0;
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS, text,
				   XVT_COCKPIT_ALIGN_LEFT);
	field = &Fields()[XVT_COCKPIT_TEXT_CRAFT_STATUS];
	XVT_ASSERT_INT_EQ(strcmp(field->caption.text, "KEEP"), 0);
	XVT_ASSERT_INT_EQ(field->generation, generation);
	text[255] = 0;
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS, text,
				   XVT_COCKPIT_ALIGN_LEFT);
	XVT_ASSERT_INT_EQ(
		strlen(Fields()[XVT_COCKPIT_TEXT_CRAFT_STATUS].caption.text),
		255);

	/* Empty text records an invisible field. */
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS, "",
				   XVT_COCKPIT_ALIGN_LEFT);
	XVT_ASSERT_INT_EQ(
		Fields()[XVT_COCKPIT_TEXT_CRAFT_STATUS].caption.visible, 0);
}

static void CheckClearField(void)
{
	Start();
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS, "SHOWN",
				   XVT_COCKPIT_ALIGN_LEFT);
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_RESOURCE_NAME, "",
				   XVT_COCKPIT_ALIGN_LEFT);
	uint64_t shown = Fields()[XVT_COCKPIT_TEXT_CRAFT_STATUS].generation;
	uint64_t hidden = Fields()[XVT_COCKPIT_TEXT_RESOURCE_NAME].generation;

	/* A visible field is cleared and its generation rises. */
	XvtCockpitText_ClearField(XVT_COCKPIT_TEXT_CRAFT_STATUS);
	const XvtCockpitTextField *field =
		&Fields()[XVT_COCKPIT_TEXT_CRAFT_STATUS];
	XVT_ASSERT_INT_EQ(field->caption.visible, 0);
	XVT_ASSERT_INT_EQ(field->caption.text[0], 0);
	XVT_ASSERT_TRUE(field->generation > shown);

	/* An invisible field and an out-of-range one are left alone. */
	XvtCockpitText_ClearField(XVT_COCKPIT_TEXT_RESOURCE_NAME);
	XvtCockpitText_ClearField(XVT_COCKPIT_TEXT_FIELD_COUNT);
	XVT_ASSERT_INT_EQ(Fields()[XVT_COCKPIT_TEXT_RESOURCE_NAME].generation,
			  hidden);
}

static void CheckClearTargetFields(void)
{
	Start();
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_TARGET_NAME, "TIE",
				   XVT_COCKPIT_ALIGN_LEFT);
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CMD_TIME_UNKNOWN, "--",
				   XVT_COCKPIT_ALIGN_LEFT);
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS, "OK",
				   XVT_COCKPIT_ALIGN_LEFT);
	uint64_t status = Fields()[XVT_COCKPIT_TEXT_CRAFT_STATUS].generation;

	/* The range ends at CMD_TIME_UNKNOWN; the field after it keeps its text. */
	XvtCockpitText_ClearTargetFields();
	const XvtCockpitTextField *fields = Fields();
	XVT_ASSERT_INT_EQ(fields[XVT_COCKPIT_TEXT_TARGET_NAME].caption.text[0],
			  0);
	XVT_ASSERT_INT_EQ(
		fields[XVT_COCKPIT_TEXT_CMD_TIME_UNKNOWN].caption.text[0], 0);
	XVT_ASSERT_INT_EQ(
		strcmp(fields[XVT_COCKPIT_TEXT_CRAFT_STATUS].caption.text,
		       "OK"),
		0);
	XVT_ASSERT_INT_EQ(fields[XVT_COCKPIT_TEXT_CRAFT_STATUS].generation,
			  status);
}

static void CheckResetFields(void)
{
	Start();
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS, "OK",
				   XVT_COCKPIT_ALIGN_LEFT);
	XvtCockpitText_ResetFields();
	const XvtCockpitTextField *field =
		&Fields()[XVT_COCKPIT_TEXT_CRAFT_STATUS];
	XVT_ASSERT_INT_EQ(field->generation, 0);
	XVT_ASSERT_INT_EQ(field->caption.visible, 0);
	XVT_ASSERT_INT_EQ(field->caption.text[0], 0);
}

/* A state that shows every field's owner: readouts, target panel, course, map, shields and warning. */
static void ShowEveryOwner(XvtCockpitState *state)
{
	memset(state, 0, sizeof *state);
	state->view.hud_state = HUD_VIEW_FORWARD;
	state->view.map_active = 1;
	state->readouts.clock_minutes.visible =
		state->readouts.clock_seconds.visible = 1;
	state->readouts.throttle.visible = 1;
	state->target.visible = 1;
	state->target.labels_visible = 1;
	state->target.distance.visible =
		state->target.distance_fraction.visible = 1;
	state->target.shields.visible = state->target.hull.visible =
		state->target.systems.visible = 1;
	state->proving_grounds.visible = 1;
	state->systems.shields[0].visible = state->systems.shields[1].visible =
		1;
	state->systems.shields[0].text_mode =
		state->systems.shields[1].text_mode = 1;
	state->systems.critical_warning.visible = 1;
}

static int ShownIn(const XvtCockpitState *prepared, XvtCockpitTextFieldId id)
{
	g_state = *prepared;
	XvtCockpitText_CopyFields(&g_state);
	return g_state.text_fields[id].caption.visible;
}

static void CheckCopyFieldsHidesWithOwner(void)
{
	Start();
	static XvtCockpitState shown, hidden;
	for (unsigned id = 0; id < XVT_COCKPIT_TEXT_FIELD_COUNT; ++id) {
		XvtCockpitText_RecordField((XvtCockpitTextFieldId)id, "text",
					   XVT_COCKPIT_ALIGN_LEFT);
	}
	ShowEveryOwner(&shown);

	XVT_ASSERT_INT_EQ(ShownIn(&shown, XVT_COCKPIT_TEXT_CLOCK_SEPARATOR), 1);
	hidden = shown;
	hidden.readouts.clock_seconds.visible = 0;
	XVT_ASSERT_INT_EQ(ShownIn(&hidden, XVT_COCKPIT_TEXT_CLOCK_SEPARATOR),
			  0);
	/* A hidden field is still copied: only its visibility changes. */
	XVT_ASSERT_INT_EQ(
		strcmp(g_state.text_fields[XVT_COCKPIT_TEXT_CLOCK_SEPARATOR]
			       .caption.text,
		       "text"),
		0);

	XVT_ASSERT_INT_EQ(ShownIn(&shown, XVT_COCKPIT_TEXT_THROTTLE_PERCENT),
			  1);
	hidden = shown;
	hidden.readouts.throttle.visible = 0;
	XVT_ASSERT_INT_EQ(ShownIn(&hidden, XVT_COCKPIT_TEXT_THROTTLE_PERCENT),
			  0);

	XVT_ASSERT_INT_EQ(ShownIn(&shown, XVT_COCKPIT_TEXT_TARGET_NAME), 1);
	XVT_ASSERT_INT_EQ(
		ShownIn(&shown, XVT_COCKPIT_TEXT_TARGET_RANGE_SEPARATOR), 1);
	hidden = shown;
	hidden.target.distance.visible = 0;
	XVT_ASSERT_INT_EQ(
		ShownIn(&hidden, XVT_COCKPIT_TEXT_TARGET_RANGE_SEPARATOR), 0);
	hidden = shown;
	hidden.target.visible = 0;
	XVT_ASSERT_INT_EQ(ShownIn(&hidden, XVT_COCKPIT_TEXT_TARGET_NAME), 0);

	XVT_ASSERT_INT_EQ(ShownIn(&shown, XVT_COCKPIT_TEXT_COURSE_LABEL_FIRST),
			  1);
	hidden = shown;
	hidden.proving_grounds.visible = 0;
	XVT_ASSERT_INT_EQ(ShownIn(&hidden, XVT_COCKPIT_TEXT_COURSE_LABEL_FIRST),
			  0);

	XVT_ASSERT_INT_EQ(ShownIn(&shown, XVT_COCKPIT_TEXT_MAP_FOLLOWING), 1);
	hidden = shown;
	hidden.view.map_active = 0;
	XVT_ASSERT_INT_EQ(ShownIn(&hidden, XVT_COCKPIT_TEXT_MAP_FOLLOWING), 0);

	XVT_ASSERT_INT_EQ(ShownIn(&shown, XVT_COCKPIT_TEXT_SHIELD_FORE), 1);
	hidden = shown;
	hidden.systems.shields[0].visible = hidden.systems.shields[1].visible =
		0;
	XVT_ASSERT_INT_EQ(ShownIn(&hidden, XVT_COCKPIT_TEXT_SHIELD_FORE), 0);

	XVT_ASSERT_INT_EQ(ShownIn(&shown, XVT_COCKPIT_TEXT_CRITICAL_WARNING),
			  1);
	hidden = shown;
	hidden.systems.critical_warning.visible = 0;
	XVT_ASSERT_INT_EQ(ShownIn(&hidden, XVT_COCKPIT_TEXT_CRITICAL_WARNING),
			  0);
}

static void CheckCopyFieldsViews(void)
{
	Start();
	static XvtCockpitState view;
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_CRAFT_STATUS, "OK",
				   XVT_COCKPIT_ALIGN_LEFT);
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_RESOURCE_NAME, "NAME",
				   XVT_COCKPIT_ALIGN_LEFT);
	ShowEveryOwner(&view);

	/* A field with no owner shows in the forward and HUD-only views... */
	XVT_ASSERT_INT_EQ(ShownIn(&view, XVT_COCKPIT_TEXT_CRAFT_STATUS), 1);
	view.view.hud_state = HUD_VIEW_HUD_ONLY;
	XVT_ASSERT_INT_EQ(ShownIn(&view, XVT_COCKPIT_TEXT_CRAFT_STATUS), 1);
	/* ...and in no other, except the resource name. */
	view.view.hud_state = HUD_VIEW_TARGET_CAMERA;
	XVT_ASSERT_INT_EQ(ShownIn(&view, XVT_COCKPIT_TEXT_CRAFT_STATUS), 0);
	XVT_ASSERT_INT_EQ(ShownIn(&view, XVT_COCKPIT_TEXT_RESOURCE_NAME), 1);
	view.view.hud_state = HUD_VIEW_CRAFT_LIST;
	XVT_ASSERT_INT_EQ(ShownIn(&view, XVT_COCKPIT_TEXT_RESOURCE_NAME), 1);
}

static void CheckCopyPlacedField(void)
{
	Start();
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_NETWORK_PING, "PING",
				   XVT_COCKPIT_ALIGN_LEFT);
	XvtCockpitTextField placed;
	XvtCockpitText_CopyPlacedField(&placed, XVT_COCKPIT_TEXT_NETWORK_PING,
				       5, 7);
	XVT_ASSERT_INT_EQ(strcmp(placed.caption.text, "PING"), 0);
	XVT_ASSERT_INT_EQ(placed.x, 30 + 5);
	XVT_ASSERT_INT_EQ(placed.y, 40 + 7);
	XVT_ASSERT_INT_EQ(placed.bounds.x, 10 + 5);
	XVT_ASSERT_INT_EQ(placed.bounds.y, 20 + 7);
	XVT_ASSERT_INT_EQ(placed.caption.phase, XVT_COCKPIT_AFTER_CRT);
	XVT_ASSERT_INT_EQ(placed.keyed, 1);
	XVT_ASSERT_INT_EQ(placed.color_key_argb, XvtRenderDraw_Color(BYPASS));

	/* Right-aligned text keeps its x and still moves down by the offset. */
	XvtCockpitText_RecordField(XVT_COCKPIT_TEXT_NETWORK_LAG, "LAG",
				   XVT_COCKPIT_ALIGN_RIGHT);
	XvtCockpitTextField recorded = Fields()[XVT_COCKPIT_TEXT_NETWORK_LAG];
	XvtCockpitText_CopyPlacedField(&placed, XVT_COCKPIT_TEXT_NETWORK_LAG, 5,
				       7);
	XVT_ASSERT_INT_EQ(placed.x, recorded.x);
	XVT_ASSERT_INT_EQ(placed.y, recorded.y + 7);
	XVT_ASSERT_INT_EQ(placed.bounds.y, recorded.bounds.y + 7);
}

static void CheckCaptureGlyph(void)
{
	Start();
	uint32_t palette[256];
	for (unsigned index = 0; index < 256; ++index) {
		palette[index] = 0xFF000000u | (index * 0x010203u);
	}
	XvtCockpitGlyph glyph;
	XVT_ASSERT_INT_EQ(XvtCockpitText_CaptureGlyph(&glyph, 'Q', 8, 10, 0, 4,
						      6, palette, 0),
			  1);
	XVT_ASSERT_INT_EQ(glyph.character, 'Q');
	XVT_ASSERT_INT_EQ(glyph.advance, 8);
	XVT_ASSERT_INT_EQ(glyph.height, 10);
	XVT_ASSERT_INT_EQ(glyph.x, 30 - 4);
	XVT_ASSERT_INT_EQ(glyph.y, 40 - 6);
	XVT_ASSERT_INT_EQ(glyph.foreground_argb, palette[FOREGROUND]);
	XVT_ASSERT_INT_EQ(glyph.background_argb, palette[BACKGROUND]);
	XVT_ASSERT_INT_EQ(glyph.shadow_argb, palette[SHADOW]);

	/* Keyed, a color equal to the bypass color becomes 0; unkeyed it is kept. */
	g_flightTextColorIndex = BYPASS;
	g_flightTextBgColor = BYPASS;
	g_flightTextShadowColor = BYPASS;
	XVT_ASSERT_INT_EQ(XvtCockpitText_CaptureGlyph(&glyph, 'Q', 8, 10, 0, 0,
						      0, palette, 1),
			  1);
	XVT_ASSERT_INT_EQ(glyph.foreground_argb, 0);
	XVT_ASSERT_INT_EQ(glyph.background_argb, 0);
	XVT_ASSERT_INT_EQ(glyph.shadow_argb, 0);
	XVT_ASSERT_INT_EQ(XvtCockpitText_CaptureGlyph(&glyph, 'Q', 8, 10, 0, 0,
						      0, palette, 0),
			  1);
	XVT_ASSERT_INT_EQ(glyph.foreground_argb, palette[BYPASS]);
}

static void CheckCaptureGlyphOutsideClip(void)
{
	Start();
	uint32_t palette[256] = {0};
	static const int16_t cursors[][2] = {
		{300, 40}, {30, 200}, {-50, 40}, {30, -50}};
	for (unsigned index = 0; index < sizeof cursors / sizeof cursors[0];
	     ++index) {
		g_flightCursorX = cursors[index][0];
		g_flightCursorY = cursors[index][1];
		XvtCockpitGlyph glyph, before;
		memset(&glyph, 0x5A, sizeof glyph);
		memcpy(&before, &glyph, sizeof before);
		XVT_ASSERT_INT_EQ(XvtCockpitText_CaptureGlyph(&glyph, 'Q', 8,
							      10, 0, 0, 0,
							      palette, 0),
				  0);
		XVT_ASSERT_INT_EQ(memcmp(&glyph, &before, sizeof glyph), 0);
	}
}

int main(void)
{
	CheckResolveColor();
	CheckRecordFieldTakesLiveState();
	CheckRecordFieldColorCodes();
	CheckRecordFieldGeneration();
	CheckRecordFieldRefusals();
	CheckClearField();
	CheckClearTargetFields();
	CheckResetFields();
	CheckCopyFieldsHidesWithOwner();
	CheckCopyFieldsViews();
	CheckCopyPlacedField();
	CheckCaptureGlyph();
	CheckCaptureGlyphOutsideClip();
	return 0;
}
