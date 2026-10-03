#include "xvt/assets/inventor_ascii.h"
#include "xvt/assets/file.h"

#ifndef XVT_MODERN
/* The fscanf format " %c": skip blanks, then read one character. Every function
 * in this file reads with it. */
// GLOBAL: XVT 0x51BE2C
static const char g_inventorAsciiCharScanFormat[] = " %c";

/* Consumes the stream up to and including the next '{', or to the end of the
 * file. Only the original build calls this. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4138A0
void InventorAscii_SkipPastOpenBrace(XvtFile *stream)
{
	char character;

	while (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) ==
		       1 &&
	       character != '{') {
	}
}

/* Consumes the stream up to and including the next '[', or to the end of the
 * file. Only the original build calls this. */
// FUNCTION: XVT 0x4138E0
void InventorAscii_SkipPastOpenBracket(XvtFile *stream)
{
	char character;

	while (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) ==
		       1 &&
	       character != '[') {
	}
}

/* Unless the next non-blank character is ']', consumes the stream up to and
 * including the next ',', or to the end of the file, so items with no comma
 * between them are consumed too. Only the original build calls this. */
// FUNCTION: XVT 0x413920
void InventorAscii_SkipListSeparator(XvtFile *stream)
{
	char character;

	if (InventorAscii_PeekNextIsCloseBracket(stream) == 0) {
		while (File_Scanf(stream, g_inventorAsciiCharScanFormat,
				  &character) == 1 &&
		       character != ',') {
		}
	}
}

/* Consumes the stream up to and including the next '"', or to the end of the
 * file. Only the original build calls this. */
// FUNCTION: XVT 0x413970
void InventorAscii_SkipPastQuote(XvtFile *stream)
{
	char character;

	while (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) ==
		       1 &&
	       character != '"') {
	}
}

/* Consumes the stream up to and including the next '}', or to the end of the
 * file. Only the original build calls this. */
// FUNCTION: XVT 0x4139B0
void InventorAscii_SkipPastCloseBrace(XvtFile *stream)
{
	char character;

	while (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) ==
		       1 &&
	       character != '}') {
	}
}

/* Consumes the stream up to and including the next ']', or to the end of the
 * file. Only the original build calls this. */
// FUNCTION: XVT 0x4139F0
void InventorAscii_SkipPastCloseBracket(XvtFile *stream)
{
	char character;

	while (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) ==
		       1 &&
	       character != ']') {
	}
}

/* Returns 1 when the next non-blank character is '"', else 0, also at the end
 * of the file; seeks back, so the stream does not move. Only the original build
 * calls this. */
// FUNCTION: XVT 0x413A30
int InventorAscii_PeekNextIsQuote(XvtFile *stream)
{
	long position;
	char character;

	position = File_RawTell(stream);
	if (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) !=
	    1) {
		File_RawSeek(stream, position, SEEK_SET);
		return 0;
	}

	if (character == '"') {
		File_RawSeek(stream, position, SEEK_SET);
		return 1;
	}

	File_RawSeek(stream, position, SEEK_SET);
	return 0;
}

/* Returns 1 when the next non-blank character is '}', else 0, also at the end
 * of the file; seeks back, so the stream does not move. Only the original build
 * calls this. */
// FUNCTION: XVT 0x413AA0
int InventorAscii_PeekNextIsCloseBrace(XvtFile *stream)
{
	long position;
	char character;

	position = File_RawTell(stream);
	if (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) !=
	    1) {
		File_RawSeek(stream, position, SEEK_SET);
		return 0;
	}

	if (character == '}') {
		File_RawSeek(stream, position, SEEK_SET);
		return 1;
	}

	File_RawSeek(stream, position, SEEK_SET);
	return 0;
}

/* Returns 1 when the next non-blank character is '{', else 0, also at the end
 * of the file; seeks back, so the stream does not move. Only the original build
 * calls this. */
// FUNCTION: XVT 0x413B10
int InventorAscii_PeekNextIsOpenBrace(XvtFile *stream)
{
	long position;
	char character;

	position = File_RawTell(stream);
	if (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) !=
	    1) {
		File_RawSeek(stream, position, SEEK_SET);
		return 0;
	}

	if (character == '{') {
		File_RawSeek(stream, position, SEEK_SET);
		return 1;
	}

	File_RawSeek(stream, position, SEEK_SET);
	return 0;
}

/* Returns 1 when the next non-blank character is ']', else 0, also at the end
 * of the file; seeks back, so the stream does not move. Only the original build
 * calls this. */
// FUNCTION: XVT 0x413B80
int InventorAscii_PeekNextIsCloseBracket(XvtFile *stream)
{
	long position;
	char character;

	position = File_RawTell(stream);
	if (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) !=
	    1) {
		File_RawSeek(stream, position, SEEK_SET);
		return 0;
	}

	if (character == ']') {
		File_RawSeek(stream, position, SEEK_SET);
		return 1;
	}

	File_RawSeek(stream, position, SEEK_SET);
	return 0;
}

/* Returns 1 when the next non-blank character is '[', else 0, also at the end
 * of the file; seeks back, so the stream does not move. Only the original build
 * calls this. */
// FUNCTION: XVT 0x413BF0
int InventorAscii_PeekNextIsOpenBracket(XvtFile *stream)
{
	long position;
	char character;

	position = File_RawTell(stream);
	if (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) !=
	    1) {
		File_RawSeek(stream, position, SEEK_SET);
		return 0;
	}

	if (character == '[') {
		File_RawSeek(stream, position, SEEK_SET);
		return 1;
	}

	File_RawSeek(stream, position, SEEK_SET);
	return 0;
}

/* Reads characters up to and including the next '\n'. Never stops at the end of
 * the file: EOF cast to a byte is 0xFF, not '\n', so it loops forever there.
 * Only the original build calls this, for a '#' comment line. */
// FUNCTION: XVT 0x413C60
void InventorAscii_SkipToEndOfLine(XvtFile *stream)
{

	while ((uint8_t)File_Getc(stream) != '\n') {
	}
}
#endif
