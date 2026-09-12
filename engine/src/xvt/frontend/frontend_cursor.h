#ifndef XVT_FRONTEND_FRONTEND_CURSOR_H
#define XVT_FRONTEND_FRONTEND_CURSOR_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t g_defaultCursorBitmap[100];

int FrontendCursor_SetImageFromResourceName(const char* resourceName, void* saveBuf);
void FrontendCursor_Init(void);
void FrontendCursor_Draw(void);
void FrontendCursor_Restore(void);
int* FrontendCursor_GetPos(int* outX, int* outY);
int FrontendCursor_SetPos(int x, int y);
void FrontendCursor_Show(void);
void FrontendCursor_Hide(void);
int FrontendCursor_IsVisible(void);
int FrontendCursor_GetDimensions(int* outWidth, int* outHeight);
int FrontendCursor_HideOsCursor(void);
int FrontendCursor_ShowOsCursor(void);

#ifdef __cplusplus
}
#endif

#endif
