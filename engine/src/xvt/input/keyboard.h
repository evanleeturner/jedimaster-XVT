#ifndef XVT_INPUT_KEYBOARD_H
#define XVT_INPUT_KEYBOARD_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int keyboard_is_key_down(uint8_t virtual_key);
char keyboard_dequeue_char(void);
char keyboard_peek_char(void);
int keyboard_flush_char_buffer(void);
int keyboard_discard_char(void);

#ifdef __cplusplus
}
#endif

#endif
