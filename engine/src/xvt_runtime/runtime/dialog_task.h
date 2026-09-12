#ifndef XVT_RUNTIME_DIALOG_TASK_H
#define XVT_RUNTIME_DIALOG_TASK_H

#include "xvt/frontend/frontend_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { XVT_DIALOG_PENDING = -1 };

typedef int (*XvtDialogContinuation)(int result, int context);

int XvtDialog_ContinueWith(XvtDialogContinuation continuation, int context);
int XvtDialog_ResumeContinuation(int* frame_result);

int XvtDialog_Begin(FrontendScreenUpdateFn update, const RECT* rect);
void XvtDialog_Tick(void);
int XvtDialog_IsActive(void);
int XvtDialog_IsTextPrompt(void);
int XvtDialog_HasResult(void);
int XvtDialog_TakeResult(int* result);
int XvtDialog_Confirm(const char* a, const char* b, const char* c, const char* okay, const char* cancel,
					  int network);
int XvtDialog_PilotName(char* name);
void XvtDialog_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
