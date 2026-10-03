#ifndef XVT_FRONTEND_FRONTEND_TEXT_H
#define XVT_FRONTEND_FRONTEND_TEXT_H

#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct BitmapFont {
	/* Each glyph's rows, encoded by FrontImage_EncodeGlyphRow, end to end;
	 * a font built through GDI starts at glyph 1. */
	uint8_t *
		pGlyphBits; ///< Runtime pointer to the compressed glyph-byte blob; serialized in the .ABP header
			    ///< as the blob byte count.
	unsigned int glyphBitOffset
		[256]; ///< Per-character byte offsets into pGlyphBits.
	/* Each character's height in pixels. Entry 0 is the font's height that
	 * FrontendText_GetFontHeight returns; a font built through GDI copies
	 * it from entry 1. */
	uint8_t glyphHeight[256];
	/* Each character's width in pixels, its advance before charSpacing; 0
	 * for character 0 in a font built through GDI. */
	uint8_t glyphWidth[256];
	/* The size the font was built at, its index in
	 * g_frontState.fontBySize. */
	unsigned int pointSize;
	/* 1 while the slot holds a font; the free functions compare it with 1,
	 * and a loaded file sets it from its own header. */
	uint8_t inUse; ///< Font-slot occupancy flag; scanned when allocating and cleared when freeing.
	/* Pixels added after each glyph's width: 0 in a font built through GDI,
	 * else what the file holds. */
	uint8_t charSpacing;
	/* FrontendText_LoadFont sets it to 1 and a loaded file sets it from its
	 * header. No code reads the field itself;
	 * FrontendText_SaveFontAtlasFile copies it with the header. */
	uint8_t field_60A; ///< Set to 1 for generated fonts and persisted in the 1547-byte .ABP header; no
	///< runtime consumer is identified.
};

typedef uint16_t TextFadeColorCache[65536];

extern int g_activeTextFieldId;

int FrontendText_HandleEditableField(RECT *rect, char *text, int maxChars,
				     int fieldId, unsigned int fontSize,
				     const char *ignoredChars);
int FrontendText_LoadFont(int pointSize);
void FrontendText_FreeAllFonts(void);
void FrontendText_FreeFont(unsigned int pointSize);
int FrontendText_Draw(int fontSize, const char *str, int x, int y, int color);
int FrontendText_DrawCentered(int fontSize, const char *str, RECT *rect,
			      int color);
int FrontendText_DrawAlignedInRect(int fontSize, const char *str, RECT *rect,
				   int centerH, int centerV, int color);
int FrontendText_DrawLineArrayInRect(int fontSize, const char **lines,
				     int lineCount, const RECT *rect, int color,
				     int centerHorizontally,
				     int centerVertically, int lineSpacing);
int FrontendText_DrawWrapped(int fontSize, const char *str, RECT *rect,
			     int color, int lineSpacing, int firstVisibleLine);
int FrontendText_GetFontHeight(int fontSize);
int FrontendText_MeasureWidth(const char *str, int fontSize);
void FrontendText_SaveFontAtlasFile(char *fileName, void **font,
				    unsigned int glyphBlobSize);
int FrontendText_LoadFontAtlasFile(const char *fileName, int slotIndex);
int FrontendText_StartTextFadeIn(int frames);
int FrontendText_StopTextFade(void);
int FrontendText_SuspendTextFade(void);
int FrontendText_ResumeTextFade(void);
void FrontendText_DrawFormattedWrappedText(RECT *rect, const uint8_t *text,
					   int suppressCenteredHeadings);

#ifdef __cplusplus
}
#endif

#endif
