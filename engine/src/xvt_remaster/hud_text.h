#ifndef XVT_REMASTER_HUD_TEXT_H
#define XVT_REMASTER_HUD_TEXT_H
#include "xvt_remaster/hud_draw.h"
/* The cockpit's text fields and numeric readouts as glyphs, in the original's grammar: color codes (0xFE
 * then a byte, or a byte under 0x10) switch the foreground, letters are uppercased unless the field is
 * lowercase, and lines wrap and align inside the field's bounds. */

/* Draws a text field; 1 doing nothing when its caption is hidden. With clear_background, fills the
 * bounds first. The text runs from (x, y) in the tier's font, aligned left, centered, or right two
 * pixels in; a keyed color is transparent; with word_wrap, a word that would pass the right edge less
 * two wraps at its space (advancing the line height, plus one unless narrow, after filling the rest of
 * the line when clear_line), and a glyph that still passes the edge wraps before it by its height plus
 * two. Returns 0 for a font tier out of range, a font not loaded, a character the font lacks, or a
 * color code with no byte after it. */
int XvtHudText_DrawField(const XvtHudDraw* draw, const XvtCockpitTextField* field);
/* Draws a numeric readout as a literal text field with the number's position, bounds, tier, colors,
 * alignment and flags; 1 doing nothing when hidden. The value (its low 16 bits, or the signed value
 * with score) fills field_width places left to right, blank until the first nonzero digit or
 * minimum_digits, a digit over 9 shown as 9, a 16-bit value of 0xFFFF shown as zeros with no shadow, a
 * trailing space when asked. Returns 0 for a tier out of range or a width over 9, or as DrawField. */
int XvtHudText_DrawNumber(const XvtHudDraw* draw, const XvtCockpitNumber* number, int score);
#endif
