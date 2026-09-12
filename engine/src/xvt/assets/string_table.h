#ifndef XVT_ASSETS_STRING_TABLE_H
#define XVT_ASSETS_STRING_TABLE_H

#include "xvt/assets/file.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

extern char* g_strFileErrorMessages[4];

void StringTable_LoadGameStrings(int loadFromDisk);
int StringTable_ReadNonCommentLine(XvtFile* stream, char* buffer);

#ifdef __cplusplus
}
#endif

#endif
