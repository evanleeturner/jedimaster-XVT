#ifndef XVT_FRONTEND_FRONTEND_H
#define XVT_FRONTEND_FRONTEND_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int g_scrollableControlCount;
extern int g_scrollableControlCountSaved;
extern int g_scrollableControlIds[32];
extern int g_scrollableControlIdsSaved[32];
extern int g_colorPaleCyan;
extern int g_colorTeal;
extern int g_colorGreen;
extern int g_colorBlue;
extern int g_colorRed;
extern int g_colorLightBlue;
extern int g_colorGreen2;
extern int g_frontendFirstVisibleLine;
extern int g_colorGray;
extern int g_colorNavy;
extern int g_colorRed2;
extern int g_colorNavy2;
extern int g_colorBlue2;
extern int g_colorYellow2;
extern int g_colorViolet;
extern int g_colorSpringGreen;
extern int g_colorMutedGreen2;
extern int g_colorCyan;
extern int g_colorAzure;
extern int g_colorOrange;
extern int g_pulseColorRamp[12];
extern int g_concourseRedrawRequested;
extern int g_editableFieldBackgroundColor;
extern char g_frontendScratchBuffer[256];
extern char g_nextMissionDescription[256];
extern int g_hostCdAvailable;
extern int g_cdAudioWarningPending;
extern int g_skipMovieChecks;

int Frontend_LoadResources(void);
int Frontend_HandleCommonScreenControls(int screenContext);
int Frontend_FormatSecondsToClockString(unsigned int seconds);
int ErrorText_LoadLine(int lineIndex, char* outText);
int Frontend_MarkHostCdAvailable(void);
int Frontend_SavePersistentState(void);
int Frontend_IsScrollableControlFocused(int controlId);
int Frontend_RegisterScrollableControl(int controlId);
int Frontend_UnregisterScrollableControl(int controlId);
int Frontend_CycleScrollableFocus(void);
int Frontend_ResetScrollableControls(void);

#ifdef __cplusplus
}
#endif

#endif
