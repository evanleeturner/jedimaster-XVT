#ifndef XVT_ASSETS_INVENTOR_ASCII_H
#define XVT_ASSETS_INVENTOR_ASCII_H

#include "xvt/assets/file.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef XVT_MODERN
struct InventorNodeDef {
	const char* nodeName;
	int32_t fieldCount;
	const struct InventorFieldDef* const* fieldDefs;
};

void InventorAscii_SkipToOpenBrace(XvtFile* stream);
void InventorAscii_SkipToOpenBracket(XvtFile* stream);
void InventorAscii_SkipListSeparator(XvtFile* stream);
void InventorAscii_SkipToQuote(XvtFile* stream);
void InventorAscii_SkipToCloseBrace(XvtFile* stream);
void InventorAscii_SkipToCloseBracket(XvtFile* stream);
int InventorAscii_PeekNextIsQuote(XvtFile* stream);
int InventorAscii_PeekNextIsCloseBrace(XvtFile* stream);
int InventorAscii_PeekNextIsOpenBrace(XvtFile* stream);
int InventorAscii_PeekNextIsCloseBracket(XvtFile* stream);
int InventorAscii_PeekNextIsOpenBracket(XvtFile* stream);
void InventorAscii_SkipToEndOfLine(XvtFile* stream);
#endif

#ifdef __cplusplus
}
#endif

#endif
