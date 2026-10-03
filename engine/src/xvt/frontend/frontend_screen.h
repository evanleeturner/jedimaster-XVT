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
	/* The pixels under savedRect, copied by FrontendScreen_PushState when
	 * the next screen was pushed over this one and drawn back by
	 * FrontendScreen_PopState: at 8 bits per pixel RLE-compressed when
	 * FrontImage_CompressRLE succeeds, else raw; raw at 16. 0 by 0 with no
	 * data when that push had no rect. */
	ImageResource savedImage;
	/* The rect of the screen pushed over this one, clamped to 0 to 639 by 0
	 * to 479. */
	RECT savedRect;
	/* Clip left edge before the push. FrontendScreen_PopState reads these
	 * four fields as one RECT to restore the clip. */
	int savedClipMinX;
	int savedClipMinY; /* Clip top edge before the push. */
	int savedClipMaxX; /* Clip right edge before the push. */
	int savedClipMaxY; /* Clip bottom edge before the push. */
	/* Called by the frame loop each frame while this screen is on top, with
	 * g_frontState.frameCounter. Returning 1 calls exitFn, then ends a
	 * modal screen, or the program when the main loop ran it. */
	FrontendScreenUpdateFn updateFn;
	/* Called after an update that returned 1, or one during which
	 * FrontendScreen_SetCallbacks ran. */
	FrontendScreenExitFn exitFn;
	/* g_frontState.frameCounter before the push, restored by the pop. */
	int savedFrameCounter;
};

void FrontendScreen_SetCallbacks(FrontendScreenUpdateFn updateFn,
				 FrontendScreenExitFn exitFn);
int FrontendScreen_QueuePush(int (*updateFn)(int), const RECT *screenRect);
int FrontendScreen_RunModal(FrontendScreenUpdateFn updateFn, RECT *screenRect);
int FrontendScreen_PushState(FrontendScreenUpdateFn updateFn, RECT *screenRect);
void FrontendScreen_PopState(void);

#ifdef __cplusplus
}
#endif

#endif
