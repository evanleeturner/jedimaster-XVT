#ifndef XVT_FRONTEND_FRONTEND_DIALOG_H
#define XVT_FRONTEND_FRONTEND_DIALOG_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern char g_frontDialogText0[256];
extern char g_frontDialogText1[256];
extern char g_frontDialogText2OrEdit[256];
extern char g_frontDialogOkayLabel[128];
extern char g_frontDialogCancelLabel[128];
extern int g_frontDialogSavedMouseX;
extern int g_frontDialogSavedMouseY;
extern int g_dialogResult;

int FrontendDialog_ShowConfirmDialog(const char* line1, const char* line2, const char* line3,
									 const char* okayLabel, const char* cancelLabel);
int FrontendDialog_ConfirmUpdateCallback(int frameState);
int FrontendDialog_HasNetworkDismissPacket(void);
int FrontendDialog_PromptForPilotName(char* outName);
int FrontendDialog_CreatePilotNameCallback(int frameState);
int FrontendDialog_ShowNetworkAbortError(const char* line1, const char* line2, const char* line3,
										 const char* okayLabel, const char* cancelLabel);
int FrontendDialog_NetworkAbortErrorCallback(int frameState);

#ifdef __cplusplus
}
#endif

#endif
