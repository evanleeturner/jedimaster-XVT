#ifndef XVT_APP_SETUP_UI_H
#define XVT_APP_SETUP_UI_H

#include "aeron/scene/ui_file_picker.h"
#include "xvt_app/setup.h"

int XvtSetupUi_OpenPicker(AeronUiFilePicker* picker, const char* path, char* error, size_t capacity);
/* A NULL path shows configuration recovery; otherwise Continue saves the
 * validated installation. Cancellation never saves the selection. */
XvtSetupResult XvtSetupUi_Run(XvtAppUi* ui, char* path, size_t path_capacity, char* error, size_t capacity);

#endif
