#ifndef XVT_KEYBOARD_MAPPING_H
#define XVT_KEYBOARD_MAPPING_H

#include "aeron/input.h"
#include "xvt_runtime/input/actions.h"
#include <stddef.h>

#define XVT_KEYBOARD_BINDING_CAP 256

typedef struct XvtKeyboardBinding {
	AeronKeyChord source;
	XvtInputAction action;
} XvtKeyboardBinding;

typedef struct XvtKeyboardBindings {
	XvtKeyboardBinding bindings[XVT_KEYBOARD_BINDING_CAP];
	size_t count;
} XvtKeyboardBindings;

typedef enum XvtKeyboardShortcut {
	XVT_KEYBOARD_SHORTCUT_NONE,
	XVT_KEYBOARD_SHORTCUT_SETTINGS,
	XVT_KEYBOARD_SHORTCUT_DEBUG,
	XVT_KEYBOARD_SHORTCUT_RENDERER,
	XVT_KEYBOARD_SHORTCUT_MOUSE,
} XvtKeyboardShortcut;

void XvtKeyboardMapping_SetPolicy(bool debug_available);
int XvtKeyboardMapping_Trigger(const AeronInputSnapshot* input, XvtKeyboardShortcut shortcut);
XvtKeyboardShortcut XvtKeyboardMapping_Shortcut(AeronKeyChord source);
bool XvtKeyboardMapping_SourceValid(AeronKeyChord source);
void XvtKeyboardMapping_FormatSource(char* text, size_t capacity, AeronKeyChord source);
size_t XvtKeyboardMapping_Find(const XvtKeyboardBindings* profile, AeronKeyChord source);
bool XvtKeyboardMapping_Equal(const XvtKeyboardBindings* a, const XvtKeyboardBindings* b);
void XvtKeyboardMapping_Sort(XvtKeyboardBindings* profile);
void XvtKeyboardMapping_Remove(XvtKeyboardBindings* profile, size_t index);
void XvtKeyboardMapping_Install(const XvtKeyboardBindings* profile);
void XvtKeyboardMapping_Suspend(void);
void XvtKeyboardMapping_Enable(bool enabled, const AeronInputSnapshot* input);
void XvtKeyboardMapping_BeginFrame(const AeronInputSnapshot* input);
void XvtKeyboardMapping_Event(const AeronKeyEvent* event, bool suppressed);
uint16_t XvtKeyboardMapping_ReadKey(void);
uint16_t XvtKeyboardMapping_ReadButtons(void);

#endif
