#ifndef XVT_INPUT_DINPUT_H
#define XVT_INPUT_DINPUT_H

#include <stdint.h>

#include "aeron/compat/win_types.h"
#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t g_dinput_key_code_table[256];
extern const uint8_t g_dinput_shift_key_code_table[256];
extern const uint8_t g_dinput_ctrl_key_code_table[256];
extern const uint8_t g_dinput_alt_key_code_table[256];
extern struct IDirectInputDeviceA *g_dinput_keyboard_device;
extern int g_dinput_shift_down;
extern int g_dinput_ctrl_down;
extern int g_dinput_alt_down;

int dinput_init(void);
int dinput_skip_to_pending_key_press(void);
uint8_t dinput_get_key(void);
void dinput_update_keyboard_modifier_state(void);
HRESULT dinput_probe_keyboard_state(void);
void dinput_shutdown(void);
int dinput_reacquire_keyboard(void);

#ifdef __cplusplus
}
#endif

#endif
