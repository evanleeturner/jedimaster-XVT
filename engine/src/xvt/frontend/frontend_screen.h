#ifndef XVT_FRONTEND_FRONTEND_SCREEN_H
#define XVT_FRONTEND_FRONTEND_SCREEN_H

#include "xvt/frontend/front_image.h"
#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*FrontendScreenUpdateFn)(int);

typedef int (*FrontendScreenExitFn)(int);

enum { FRONTEND_SCREEN_MAX_STACK = 10 };

struct FrontendScreenState {
	ImageResource savedImage;
	RECT savedRect;
	int savedClipMinX;
	int savedClipMinY;
	int savedClipMaxX;
	int savedClipMaxY;
	FrontendScreenUpdateFn updateFn;
	FrontendScreenExitFn exitFn;
	int savedFrameCounter;
};

void FrontendScreen_SetCallbacks(FrontendScreenUpdateFn updateFn, FrontendScreenExitFn exitFn);
int FrontendScreen_QueuePush(int (*updateFn)(int), const RECT* screenRect);
int FrontendScreen_RunModal(FrontendScreenUpdateFn updateFn, RECT* screenRect);
int FrontendScreen_PushState(FrontendScreenUpdateFn updateFn, RECT* screenRect);
void FrontendScreen_PopState(void);

#ifdef __cplusplus
}
#endif

#endif
