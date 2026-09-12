#include "xvt_remaster/hud_text.h"
#include <string.h>

typedef struct TextCursor {
	XvtCockpitGlyph glyph;
	const XvtFontAtlas* font;
	unsigned phase, line_height;
	int lowercase, wrap, clear_line;
	uint32_t color_key;
	int keyed;
} TextCursor;

static uint32_t ResolveTextColor(const TextCursor* cursor, uint32_t color) {
	return cursor->keyed && color == cursor->color_key ? 0 : color;
}

static unsigned NormalizeCharacter(const TextCursor* cursor, unsigned ch) {
	return !cursor->lowercase && ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch;
}

static const XvtFontGlyph* FindGlyph(const TextCursor* cursor, unsigned ch) {
	ch = NormalizeCharacter(cursor, ch);
	return ch >= cursor->font->atlas.first_char &&
				   ch < cursor->font->atlas.first_char + cursor->font->atlas.num_chars
			   ? &cursor->font->glyphs[ch - cursor->font->atlas.first_char]
			   : NULL;
}

static int MeasureText(const TextCursor* cursor, const unsigned char* text, int word) {
	int width = 0;
	for (unsigned index = 0; text[index] && text[index] != '\n' && (!word || text[index] != ' '); ++index) {
		unsigned ch = text[index];
		if (ch == 0xfe && text[index + 1]) {
			++index;
			continue;
		}
		const XvtFontGlyph* glyph = ch >= ' ' ? FindGlyph(cursor, ch) : NULL;
		if (glyph)
			width += glyph->advance;
	}
	return width;
}

static void AdvanceLine(const XvtHudDraw* draw, TextCursor* cursor, int advance) {
	if (cursor->clear_line) {
		XvtSnapRect bounds = cursor->glyph.clip;
		int right = bounds.x + bounds.width, bottom = bounds.y + bounds.height;
		int x = cursor->glyph.x > bounds.x ? cursor->glyph.x : bounds.x;
		int y = cursor->glyph.y > bounds.y ? cursor->glyph.y : bounds.y;
		int end_y = cursor->glyph.y + (int)cursor->line_height;
		XvtHudDraw_TextFill(draw, cursor->font, cursor->phase,
							(XvtSnapRect) { x, y, right - x, (end_y < bottom ? end_y : bottom) - y },
							cursor->glyph.background_argb);
	}
	cursor->glyph.x = (int16_t)cursor->glyph.clip.x;
	cursor->glyph.y += (int16_t)advance;
}

static int DrawCharacter(const XvtHudDraw* draw, TextCursor* cursor, unsigned ch) {
	if (ch == '\n') {
		AdvanceLine(draw, cursor, cursor->line_height + !cursor->glyph.narrow);
		return 1;
	}
	if (ch < ' ')
		return 1;
	const XvtFontGlyph* metrics = FindGlyph(cursor, ch);
	if (!metrics)
		return 0;
	int right = cursor->glyph.clip.x + cursor->glyph.clip.width;
	if (cursor->wrap && cursor->glyph.x + metrics->advance >= right)
		AdvanceLine(draw, cursor, metrics->height + 2);
	cursor->glyph.character = (uint16_t)NormalizeCharacter(cursor, ch);
	cursor->glyph.advance = metrics->advance;
	cursor->glyph.height = metrics->height;
	if (!XvtHudDraw_Glyph(draw, &cursor->glyph, 0, 0, cursor->glyph.clip, cursor->phase))
		return 0;
	cursor->glyph.x += metrics->advance;
	if (cursor->wrap && cursor->glyph.x >= right)
		AdvanceLine(draw, cursor, metrics->height + 2);
	return 1;
}

static int DrawString(const XvtHudDraw* draw, TextCursor* cursor, const char* string, unsigned alignment,
					  int literal) {
	const unsigned char* text = (const unsigned char*)string;
	int right = cursor->glyph.clip.x + cursor->glyph.clip.width;
	if (alignment != XVT_COCKPIT_ALIGN_LEFT) {
		int width = MeasureText(cursor, text, 0);
		int x = alignment == XVT_COCKPIT_ALIGN_CENTER ? (right + cursor->glyph.clip.x) / 2 - (width >> 1)
													  : right - width - 2;
		cursor->glyph.x = (int16_t)(x < cursor->glyph.clip.x ? cursor->glyph.clip.x : x);
	}
	for (unsigned index = 0; text[index]; ++index) {
		unsigned ch = text[index];
		if (!literal && (ch == 0xfe || ch < 0x10)) {
			if (ch == 0xfe) {
				if (!text[index + 1])
					return 0;
				ch = text[++index];
			}
			cursor->glyph.foreground_argb = ResolveTextColor(cursor, draw->state->palette_argb[ch]);
			continue;
		}
		if (!literal && ch == ' ' && cursor->wrap) {
			const XvtFontGlyph* space = FindGlyph(cursor, ' ');
			if (space &&
				cursor->glyph.x + space->advance + MeasureText(cursor, text + index + 1, 1) > right - 2)
				ch = '\n';
		}
		if (!DrawCharacter(draw, cursor, ch))
			return 0;
	}
	return 1;
}

static int DrawStyledField(const XvtHudDraw* draw, const XvtCockpitTextField* field, int literal) {
	if (!field->caption.visible)
		return 1;
	unsigned tier = field->caption.font_tier;
	if (tier >= XVT_HUD_FONT_TIERS)
		return 0;
	const XvtCockpitFontBinding* font = &draw->state->definition.fonts[tier];
	TextCursor cursor = { .font = XvtHudAssets_FindFont(font->asset_id),
						  .phase = field->caption.phase,
						  .line_height = font->line_height,
						  .lowercase = field->lowercase,
						  .wrap = field->word_wrap,
						  .clear_line = field->clear_line,
						  .keyed = field->keyed,
						  .color_key = field->color_key_argb };
	if (!cursor.font)
		return 0;
	cursor.glyph = (XvtCockpitGlyph) {
		.font_asset_id = font->asset_id,
		.clip = field->bounds,
		.x = field->x,
		.y = field->y,
		.narrow = field->narrow,
		.shadow_enabled = field->shadow_enabled,
		.foreground_argb = ResolveTextColor(&cursor, draw->state->palette_argb[field->caption.foreground]),
		.background_argb = ResolveTextColor(&cursor, draw->state->palette_argb[field->caption.background]),
		.shadow_argb = ResolveTextColor(&cursor, draw->state->palette_argb[field->shadow_color])
	};
	if (field->clear_background)
		XvtHudDraw_TextFill(draw, cursor.font, cursor.phase, field->bounds, cursor.glyph.background_argb);
	return DrawString(draw, &cursor, field->caption.text, field->caption.alignment, literal);
}

int XvtHudText_DrawField(const XvtHudDraw* draw, const XvtCockpitTextField* field) {
	return DrawStyledField(draw, field, 0);
}

int XvtHudText_DrawNumber(const XvtHudDraw* draw, const XvtCockpitNumber* number, int score) {
	if (!number->visible)
		return 1;
	if (number->font_tier >= XVT_HUD_FONT_TIERS || number->field_width > 9)
		return 0;
	XvtCockpitTextField field = { 0 };
	field.caption.visible = 1;
	field.caption.font_tier = number->font_tier;
	field.caption.foreground = number->foreground;
	field.caption.background = number->background;
	field.caption.alignment = number->alignment;
	field.caption.phase = number->phase;
	field.bounds = number->bounds;
	field.x = number->x;
	field.y = number->y;
	field.shadow_enabled = number->shadow_enabled;
	field.shadow_color = number->shadow_color;
	field.clear_background = number->clear_background;
	field.word_wrap = number->word_wrap;
	field.clear_line = number->clear_line;
	field.narrow = number->narrow;
	field.keyed = number->keyed;
	field.color_key_argb = number->color_key_argb;
	int64_t value = score ? number->value : (uint16_t)number->value;
	int started = 0;
	for (unsigned index = 0; index < number->field_width; ++index) {
		unsigned remaining = number->field_width - index;
		int divisor = 1;
		for (unsigned place = 1; place < remaining; ++place)
			divisor *= 10;
		int digit = (int)(value / divisor);
		value = score ? (int32_t)((uint32_t)value - (uint32_t)divisor * (uint16_t)digit)
					  : (uint16_t)(value - (int64_t)divisor * (uint16_t)digit);
		started |= remaining <= number->minimum_digits || (uint16_t)digit != 0;
		field.caption.text[index] = started ? (char)('0' + ((uint16_t)digit > 9 ? 9 : digit)) : ' ';
	}
	if (!score && (uint16_t)number->value == UINT16_MAX) {
		memset(field.caption.text, '0', number->field_width);
		field.shadow_enabled = 0;
	}
	if (number->trailing_space)
		field.caption.text[number->field_width] = ' ';
	return DrawStyledField(draw, &field, 1);
}
