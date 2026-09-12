#ifndef XVT_RUNTIME_INPUT_CAPTURE_H
#define XVT_RUNTIME_INPUT_CAPTURE_H
#include "aeron/input.h"
#include <stdbool.h>
void XvtInput_BeginCaptureFrame(const AeronInputSnapshot* input, bool capture);
void XvtInput_SetCaptured(bool capture);
bool XvtInput_IsCaptured(void);
bool XvtInput_MouseMotionAllowed(void);
uint32_t XvtInput_FilterMouseButtons(uint32_t buttons);
void XvtInput_SuppressRendererTab(bool suppress);
void XvtInput_ResetCapture(void);
void XvtInput_FlushKeyboard(void);
void XvtInput_FlushRawKeyboard(void);
void XvtInput_BlockHeldKeys(void);
void XvtInput_SuppressKey(int key);
void XvtInput_UpdateMouseCapture(const AeronInputSnapshot* input);
bool XvtInput_MouseFlightAllowed(void);
#endif
