#ifndef XVT_KEYBOARD_SETTINGS_H
#define XVT_KEYBOARD_SETTINGS_H
#include "xvt_app/settings/bindings_editor.h"
#include "xvt_runtime/config/config.h"

typedef struct XvtKeyboardSettings {
	XvtKeyboardBindings original;
	XvtKeyboardBindings draft;
	XvtBindingsEditor editor;
	AeronKeyChord pending;
	XvtInputAction conflicting_action;
	int conflict_open;
	int restore_open;
	bool dirty;
	bool restore_defaults;
	char error[512];
} XvtKeyboardSettings;

void XvtKeyboardSettings_Open(XvtKeyboardSettings* settings, const XvtSettings* config);
void XvtKeyboardSettings_Draw(XvtKeyboardSettings* settings, AeronUiContext* ui);
void XvtKeyboardSettings_DrawModals(XvtKeyboardSettings* settings, AeronUiContext* ui);
void XvtKeyboardSettings_CancelCapture(XvtKeyboardSettings* settings, AeronUiContext* ui);
bool XvtKeyboardSettings_Commit(XvtKeyboardSettings* settings, char* error, size_t capacity);
#endif
