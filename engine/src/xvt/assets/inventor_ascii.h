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
	/* Name as written in an Inventor file. A word read from the file
	 * matches when, ignoring case, it is this name or its start. */
	const char *nodeName;
	/* Entries in fieldDefs; also the parsed node's record count. */
	int32_t fieldCount;
	/* The node's fields, in record order; NULL when there are none. */
	const struct InventorFieldDef *const *fieldDefs;
};

void InventorAscii_SkipPastOpenBrace(XvtFile *stream);
void InventorAscii_SkipPastOpenBracket(XvtFile *stream);
void InventorAscii_SkipListSeparator(XvtFile *stream);
void InventorAscii_SkipPastQuote(XvtFile *stream);
void InventorAscii_SkipPastCloseBrace(XvtFile *stream);
void InventorAscii_SkipPastCloseBracket(XvtFile *stream);
int InventorAscii_PeekNextIsQuote(XvtFile *stream);
int InventorAscii_PeekNextIsCloseBrace(XvtFile *stream);
int InventorAscii_PeekNextIsOpenBrace(XvtFile *stream);
int InventorAscii_PeekNextIsCloseBracket(XvtFile *stream);
int InventorAscii_PeekNextIsOpenBracket(XvtFile *stream);
void InventorAscii_SkipToEndOfLine(XvtFile *stream);
#endif

#ifdef __cplusplus
}
#endif

#endif
