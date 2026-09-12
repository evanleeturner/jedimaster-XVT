#include "xvt/assets/inventor_ascii.h"
#include "xvt/assets/file.h"

#ifndef XVT_MODERN
// GLOBAL: XVT 0x51BE2C
static const char g_inventorAsciiCharScanFormat[] = " %c";

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4138A0
void InventorAscii_SkipToOpenBrace(XvtFile* stream) {
	char character;

	while (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) == 1 && character != '{') {
	}
}

// FUNCTION: XVT 0x4138E0
void InventorAscii_SkipToOpenBracket(XvtFile* stream) {
	char character;

	while (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) == 1 && character != '[') {
	}
}

// FUNCTION: XVT 0x413920
void InventorAscii_SkipListSeparator(XvtFile* stream) {
	char character;

	if (InventorAscii_PeekNextIsCloseBracket(stream) == 0) {
		while (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) == 1 && character != ',') {
		}
	}
}

// FUNCTION: XVT 0x413970
void InventorAscii_SkipToQuote(XvtFile* stream) {
	char character;

	while (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) == 1 && character != '"') {
	}
}

// FUNCTION: XVT 0x4139B0
void InventorAscii_SkipToCloseBrace(XvtFile* stream) {
	char character;

	while (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) == 1 && character != '}') {
	}
}

// FUNCTION: XVT 0x4139F0
void InventorAscii_SkipToCloseBracket(XvtFile* stream) {
	char character;

	while (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) == 1 && character != ']') {
	}
}

// FUNCTION: XVT 0x413A30
int InventorAscii_PeekNextIsQuote(XvtFile* stream) {
	long position;
	char character;

	position = File_RawTell(stream);
	if (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) != 1) {
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

// FUNCTION: XVT 0x413AA0
int InventorAscii_PeekNextIsCloseBrace(XvtFile* stream) {
	long position;
	char character;

	position = File_RawTell(stream);
	if (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) != 1) {
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

// FUNCTION: XVT 0x413B10
int InventorAscii_PeekNextIsOpenBrace(XvtFile* stream) {
	long position;
	char character;

	position = File_RawTell(stream);
	if (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) != 1) {
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

// FUNCTION: XVT 0x413B80
int InventorAscii_PeekNextIsCloseBracket(XvtFile* stream) {
	long position;
	char character;

	position = File_RawTell(stream);
	if (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) != 1) {
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

// FUNCTION: XVT 0x413BF0
int InventorAscii_PeekNextIsOpenBracket(XvtFile* stream) {
	long position;
	char character;

	position = File_RawTell(stream);
	if (File_Scanf(stream, g_inventorAsciiCharScanFormat, &character) != 1) {
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

// FUNCTION: XVT 0x413C60
void InventorAscii_SkipToEndOfLine(XvtFile* stream) {

	while ((uint8_t)File_Getc(stream) != '\n') {
	}
}
#endif
