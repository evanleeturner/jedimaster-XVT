#ifndef XVT_FRONTEND_FRONTEND_SCROLLBAR_H
#define XVT_FRONTEND_FRONTEND_SCROLLBAR_H

#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int FrontendScrollbar_SaveState(void);
int FrontendScrollbar_RestoreState(void);
extern int g_scrollbarRepeatCountdown;
extern int g_scrollbarRepeatInterval;
int FrontendScrollbar_Draw(const RECT* src, int currentValue, int maximumExclusive, int minimum, int pageStep,
						   unsigned int color, int controlId);

#ifdef __cplusplus
}
#endif

#endif
