#ifndef XVT_APP_INSTALLATION_PAGE_H
#define XVT_APP_INSTALLATION_PAGE_H
#include "aeron/scene/ui.h"
#include <stdbool.h>
bool XvtInstallationPage_Init(char* error, size_t capacity);
void XvtInstallationPage_Shutdown(void);
void XvtInstallationPage_Open(void);
void XvtInstallationPage_CancelPicker(void);
bool XvtInstallationPage_PickerOpen(void);
void XvtInstallationPage_Draw(AeronUiContext* ui, const AeronInputSnapshot* input);
void XvtInstallationPage_DrawPicker(AeronUiContext* ui);
bool XvtInstallationPage_Flush(char* error, size_t capacity);
#endif
