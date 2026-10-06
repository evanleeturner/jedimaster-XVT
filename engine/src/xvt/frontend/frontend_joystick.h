#ifndef XVT_FRONTEND_FRONTEND_JOYSTICK_H
#define XVT_FRONTEND_FRONTEND_JOYSTICK_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct joystick_entry {
	/* The action's code, the first field of its line in joystick.txt read
	 * with atoi; compared with the codes in g_game_config.joy_buttons. */
	uint8_t action_code;
	/* The action's short name, the line's second space-separated field,
	 * copied without a length check. */
	char name[20];
	/* The rest of the line after the name and the space that ends it, the
	 * action's description; copied without a length check. */
	char description[128];
};

extern struct joystick_entry g_joystick_entries[128];
extern int g_joystick_entry_count;

int joystick_init_devices(void);
void joystick_update_state(int joy_slot);
int joystick_get_first_pressed_button(int joy_slot);
int joystick_get_pov_direction(int joy_slot);
int joystick_has_pov(int joy_slot);
int joystick_get_button_count(int joy_slot);
unsigned int joystick_get_device_id(int joy_slot);

#ifdef __cplusplus
}
#endif

#endif
