#ifndef XVT_FRONTEND_FRONTEND_BOOTSTRAP_H
#define XVT_FRONTEND_FRONTEND_BOOTSTRAP_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int FrontendBootstrap_ExitIntroAndLoadCredits(int frameCounter);
int FrontendBootstrap_PlayOpeningAndEnterCredits(int frameCounter);
int FrontendBootstrap_InitMode(void);
int FrontendBootstrap_LoadResources(int frameCounter);

#ifdef __cplusplus
}
#endif

#endif
