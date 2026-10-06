#ifndef XVT_FLIGHT_FLIGHT_INPUT_H
#define XVT_FLIGHT_FLIGHT_INPUT_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum flight_action_key {
	FLIGHT_KEY_NONE = 0x000,
	FLIGHT_KEY_BACKSPACE = 0x008,
	FLIGHT_KEY_TAB = 0x009,
	FLIGHT_KEY_ENTER = 0x00D,
	FLIGHT_KEY_ESCAPE = 0x01B,
	FLIGHT_KEY_SPACE = 0x020,
	FLIGHT_KEY_SHIFT_1 = 0x021,
	FLIGHT_KEY_QUOTES = 0x022,
	FLIGHT_KEY_SHIFT_3 = 0x023,
	FLIGHT_KEY_SHIFT_4 = 0x024,
	FLIGHT_KEY_APOSTROPHE = 0x027,
	FLIGHT_KEY_SHIFT_9 = 0x028,
	FLIGHT_KEY_SHIFT_0 = 0x029,
	FLIGHT_KEY_STAR = 0x02A,
	FLIGHT_KEY_PLUS = 0x02B,
	FLIGHT_KEY_COMMA = 0x02C,
	FLIGHT_KEY_MINUS = 0x02D,
	FLIGHT_KEY_PERIOD = 0x02E,
	FLIGHT_KEY_SLASH = 0x02F,
	FLIGHT_KEY_0 = 0x030,
	FLIGHT_KEY_1 = 0x031,
	FLIGHT_KEY_2 = 0x032,
	FLIGHT_KEY_3 = 0x033,
	FLIGHT_KEY_4 = 0x034,
	FLIGHT_KEY_5 = 0x035,
	FLIGHT_KEY_6 = 0x036,
	FLIGHT_KEY_7 = 0x037,
	FLIGHT_KEY_8 = 0x038,
	FLIGHT_KEY_9 = 0x039,
	FLIGHT_KEY_SEMICOLON = 0x03B,
	FLIGHT_KEY_LESS_THAN = 0x03C,
	FLIGHT_KEY_EQUAL = 0x03D,
	FLIGHT_KEY_SHIFT_2 = 0x040,
	FLIGHT_KEY_SHIFT_A = 0x041,
	FLIGHT_KEY_SHIFT_B = 0x042,
	FLIGHT_KEY_SHIFT_C = 0x043,
	FLIGHT_KEY_SHIFT_D = 0x044,
	FLIGHT_KEY_SHIFT_E = 0x045,
	FLIGHT_KEY_SHIFT_F = 0x046,
	FLIGHT_KEY_SHIFT_G = 0x047,
	FLIGHT_KEY_SHIFT_H = 0x048,
	FLIGHT_KEY_SHIFT_I = 0x049,
	FLIGHT_KEY_SHIFT_L = 0x04C,
	FLIGHT_KEY_SHIFT_M = 0x04D,
	FLIGHT_KEY_SHIFT_P = 0x050,
	FLIGHT_KEY_SHIFT_R = 0x052,
	FLIGHT_KEY_SHIFT_S = 0x053,
	FLIGHT_KEY_SHIFT_U = 0x055,
	FLIGHT_KEY_SHIFT_W = 0x057,
	FLIGHT_KEY_LEFT_BRACKET = 0x05B,
	FLIGHT_KEY_BACKSLASH = 0x05C,
	FLIGHT_KEY_RIGHT_BRACKET = 0x05D,
	FLIGHT_KEY_A = 0x061,
	FLIGHT_KEY_B = 0x062,
	FLIGHT_KEY_C = 0x063,
	FLIGHT_KEY_D = 0x064,
	FLIGHT_KEY_E = 0x065,
	FLIGHT_KEY_F = 0x066,
	FLIGHT_KEY_G = 0x067,
	FLIGHT_KEY_H = 0x068,
	FLIGHT_KEY_I = 0x069,
	FLIGHT_KEY_J = 0x06A,
	FLIGHT_KEY_K = 0x06B,
	FLIGHT_KEY_L = 0x06C,
	FLIGHT_KEY_M = 0x06D,
	FLIGHT_KEY_N = 0x06E,
	FLIGHT_KEY_O = 0x06F,
	FLIGHT_KEY_P = 0x070,
	FLIGHT_KEY_Q = 0x071,
	FLIGHT_KEY_R = 0x072,
	FLIGHT_KEY_S = 0x073,
	FLIGHT_KEY_T = 0x074,
	FLIGHT_KEY_U = 0x075,
	FLIGHT_KEY_V = 0x076,
	FLIGHT_KEY_W = 0x077,
	FLIGHT_KEY_X = 0x078,
	FLIGHT_KEY_Y = 0x079,
	FLIGHT_KEY_Z = 0x07A,
	FLIGHT_KEY_ALT_B = 0x081,
	FLIGHT_KEY_ALT_C = 0x082,
	FLIGHT_KEY_ALT_D = 0x083,
	FLIGHT_KEY_ALT_E = 0x084,
	FLIGHT_KEY_ALT_I = 0x088,
	FLIGHT_KEY_ALT_J = 0x089,
	FLIGHT_KEY_ALT_M = 0x08C,
	FLIGHT_KEY_ALT_N = 0x08D,
	FLIGHT_KEY_ALT_P = 0x08F,
	FLIGHT_KEY_ALT_Q = 0x090,
	FLIGHT_KEY_ALT_S = 0x092,
	FLIGHT_KEY_ALT_U = 0x094,
	FLIGHT_KEY_ALT_V = 0x095,
	FLIGHT_KEY_ALT_W = 0x096,
	FLIGHT_KEY_ALT_1 = 0x09B,
	FLIGHT_KEY_ALT_2 = 0x09C,
	FLIGHT_KEY_ALT_3 = 0x09D,
	FLIGHT_KEY_LEFT = 0x0A4,
	FLIGHT_KEY_RIGHT = 0x0A5,
	FLIGHT_KEY_UP = 0x0A6,
	FLIGHT_KEY_DOWN = 0x0A7,
	FLIGHT_KEY_INSERT = 0x0A8,
	FLIGHT_KEY_DELETE = 0x0A9,
	FLIGHT_KEY_HOME = 0x0AA,
	FLIGHT_KEY_END = 0x0AB,
	FLIGHT_KEY_PAGE_UP = 0x0AC,
	FLIGHT_KEY_PAGE_DOWN = 0x0AD,
	FLIGHT_KEY_SCROLL_LOCK = 0x0AF,
	FLIGHT_KEY_PAD_0 = 0x0B2,
	FLIGHT_KEY_PAD_1 = 0x0B3,
	FLIGHT_KEY_PAD_2 = 0x0B4,
	FLIGHT_KEY_PAD_3 = 0x0B5,
	FLIGHT_KEY_PAD_4 = 0x0B6,
	FLIGHT_KEY_PAD_5 = 0x0B7,
	FLIGHT_KEY_PAD_6 = 0x0B8,
	FLIGHT_KEY_PAD_7 = 0x0B9,
	FLIGHT_KEY_PAD_8 = 0x0BA,
	FLIGHT_KEY_PAD_9 = 0x0BB,
	FLIGHT_KEY_PAD_SLASH = 0x0BD,
	FLIGHT_KEY_PAD_STAR = 0x0BE,
	FLIGHT_KEY_PAD_MINUS = 0x0BF,
	FLIGHT_KEY_PAD_PLUS = 0x0C0,
	FLIGHT_KEY_PAD_ENTER = 0x0C1,
	FLIGHT_KEY_PAD_DOT = 0x0C2,
	FLIGHT_KEY_F1 = 0x0C3,
	FLIGHT_KEY_F2 = 0x0C4,
	FLIGHT_KEY_F3 = 0x0C5,
	FLIGHT_KEY_F4 = 0x0C6,
	FLIGHT_KEY_F5 = 0x0C7,
	FLIGHT_KEY_F6 = 0x0C8,
	FLIGHT_KEY_F7 = 0x0C9,
	FLIGHT_KEY_F8 = 0x0CA,
	FLIGHT_KEY_F9 = 0x0CB,
	FLIGHT_KEY_F10 = 0x0CC,
	FLIGHT_KEY_F11 = 0x0CD,
	FLIGHT_KEY_F12 = 0x0CE,
	FLIGHT_KEY_SHIFT_F1 = 0x0CF,
	FLIGHT_KEY_SHIFT_F2 = 0x0D0,
	FLIGHT_KEY_SHIFT_F3 = 0x0D1,
	FLIGHT_KEY_SHIFT_F5 = 0x0D3,
	FLIGHT_KEY_SHIFT_F6 = 0x0D4,
	FLIGHT_KEY_SHIFT_F7 = 0x0D5,
	FLIGHT_KEY_SHIFT_F9 = 0x0D7,
	FLIGHT_KEY_SHIFT_F10 = 0x0D8,
	FLIGHT_KEY_SHIFT_F11 = 0x0D9,
	FLIGHT_KEY_SHIFT_F12 = 0x0DA,
	FLIGHT_KEY_THROTTLE_1 = 0x0DB,
	FLIGHT_KEY_THROTTLE_2 = 0x0DC,
	FLIGHT_KEY_THROTTLE_3 = 0x0DD,
	FLIGHT_KEY_THROTTLE_4 = 0x0DE,
	FLIGHT_KEY_THROTTLE_6 = 0x0DF,
	FLIGHT_KEY_THROTTLE_7 = 0x0E0,
	FLIGHT_KEY_THROTTLE_8 = 0x0E1,
	FLIGHT_KEY_THROTTLE_9 = 0x0E2,
	FLIGHT_KEY_THROTTLE_10 = 0x0E3,
	FLIGHT_KEY_THROTTLE_11 = 0x0E4,
	FLIGHT_KEY_THROTTLE_12 = 0x0E5,
	FLIGHT_KEY_THROTTLE_13 = 0x0E6,
	FLIGHT_KEY_THROTTLE_14 = 0x0E7,
	FLIGHT_KEY_SCREENSHOT = 0x0E8,
} flight_action_key;

enum { XVT_INPUT_THROTTLE_PRESENT = 1 };
/* One player's input for one simulation step, as recorded, sent between players
 * and replayed. It is 8 bytes, with roll, flags and throttle. */
struct flight_input_frame_record {
	/* Roll axis, signed, low bit cleared where sampled; replayed into
	 * g_xvt_control_roll. */
	int8_t axis_r;
	/* XVT_INPUT_THROTTLE_PRESENT when throttle holds a new position. */
	uint8_t flags;
	uint8_t key;   /* Action key, a flight_action_key; 0 for none. */
	int8_t axis_x; /* Stick X axis, signed; replayed into g_ctrl_axis_x. */
	int8_t axis_y; /* Stick Y axis, signed; replayed into g_ctrl_axis_y. */
	uint8_t key_mods; /* Button bits: bit 0 fire, bit 1 target. */
	/* Throttle position, 0 to 65,535, when flags says so. */
	uint16_t throttle;
};

/* One entry of a player's input history (g_input_history): an input with its
 * time stamp and where it came from. */
struct input_frame {
	/* Nonzero while the frame still awaits relay to the other players. */
	int awaiting_relay;
	/* Source: 0 server's world message, 1 real input, 2 predicted. */
	int input_source;
	int timestamp;				/* Tick the input applies at. */
	struct flight_input_frame_record input; /* The input itself. */
};
typedef char xvt_flight_input_record_size
	[(sizeof(struct flight_input_frame_record) == 8) ? 1 : -1];

extern int16_t g_scaled_input_yaw;
extern int16_t g_abs_scaled_input_yaw;
extern int16_t g_scaled_input_pitch;
extern int16_t g_ctrl_axis_x;
extern int16_t g_ctrl_axis_y;
extern uint16_t g_action_key;
extern uint16_t g_current_action_key;
extern uint16_t g_flight_key_mods;
extern uint16_t g_flight_mouse_enabled;
extern uint16_t g_joystick_available;
extern int16_t g_flight_mouse_delta_x;
extern int16_t g_flight_mouse_delta_y;
extern uint16_t g_mouse_buttons;
extern uint16_t g_key_mods;
extern uint16_t g_joystick_detect_result_word;
extern uint8_t g_throttle_key_table[17];
extern struct flight_input_frame_record g_replay_inputs[8];
extern int16_t g_flight_mouse_x;
extern int16_t g_flight_mouse_y;
extern int g_flight_conf_direct_input;
extern uint8_t g_last_key_code;
extern int g_key_ready;

void flight_input_latch_flight_controls(void);
void flight_input_apply_deadzone(void);
void flight_input_reset_runtime_state(void);
void flight_input_reset_control_state(void);
uint16_t flight_input_read(int player_idx_or_sentinel);
int flight_input_has_key_ready(void);
int flight_input_get_next_key(void);

#ifdef __cplusplus
}
#endif

#endif
