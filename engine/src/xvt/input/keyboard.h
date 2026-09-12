#ifndef XVT_INPUT_KEYBOARD_H
#define XVT_INPUT_KEYBOARD_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int Keyboard_IsKeyDown(uint8_t virtualKey);
char Keyboard_DequeueChar(void);
char Keyboard_PeekChar(void);
int Keyboard_FlushCharBuffer(void);
int Keyboard_DiscardChar(void);

#ifdef __cplusplus
}
#endif

#endif
