#ifndef XVT_INPUT_DINPUT_H
#define XVT_INPUT_DINPUT_H

#include "aeron/compat/win_types.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t g_dinputKeyCodeTable[256];
extern const uint8_t g_dinputShiftKeyCodeTable[256];
extern const uint8_t g_dinputCtrlKeyCodeTable[256];
extern const uint8_t g_dinputAltKeyCodeTable[256];
extern struct IDirectInputDeviceA* g_dinputKeyboardDevice;
extern int g_dinputShiftDown, g_dinputCtrlDown, g_dinputAltDown;

int DInput_Init(void);
int DInput_HasKeyReady(void);
uint8_t DInput_GetKey(void);
void DInput_UpdateKeyboardModifierState(void);
HRESULT DInput_ReadKeyboardState(void);
void DInput_Shutdown(void);
int DInput_ReacquireKeyboard(void);

#ifdef __cplusplus
}
#endif

#endif
