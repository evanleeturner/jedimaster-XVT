#ifndef XVT_APP_SETTINGS_H
#define XVT_APP_SETTINGS_H
#include "xvt_app/ui.h"
typedef void (*XvtSettingsPageFn)(AeronUiContext* ui, const AeronInputSnapshot* input);
bool XvtSettingsMenu_Init(XvtAppUi* ui, char* error, size_t capacity);
void XvtSettingsMenu_Shutdown(void);
void XvtSettingsMenu_FlushForExit(void);
void XvtSettingsMenu_SetPage(int page, XvtSettingsPageFn draw);
bool XvtSettingsMenu_Open(void);
bool XvtSettingsMenu_CapturesController(void);
bool XvtSettingsMenu_CapturesKeyboard(void);
void XvtSettingsMenu_Show(void);
void XvtSettingsMenu_RequestClose(void);
bool XvtSettingsMenu_CloseRequested(void);
void XvtSettingsMenu_CompleteClose(void);
void XvtSettingsMenu_ReportError(const char* error);
bool XvtSettingsMenu_BeginFrame(const AeronInputSnapshot* input);
bool XvtSettingsMenu_ConsumeRuntimeRequest(void);
void XvtSettingsMenu_Frame(const AeronInputSnapshot* input, float seconds);
#endif
