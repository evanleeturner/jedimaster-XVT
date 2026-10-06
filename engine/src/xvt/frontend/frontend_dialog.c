#include "xvt/frontend/frontend_dialog.h"

#include "xvt_runtime/runtime/dialog_task.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_button.h"
#include "xvt/frontend/frontend_cursor.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/input/keyboard.h"
#include "xvt/net/net.h"
#include "xvt_runtime/log/log_both_builds.h"

/* The confirm dialog's OK label, shown only as the OK button's tooltip; empty
 * picks a layout without it (frontend_dialog_confirm_update_callback). Written as
 * each dialog opens: by frontend_dialog_show_confirm_dialog and
 * frontend_dialog_show_network_abort_error in the original build, by
 * xvt_dialog_confirm in the modern build. */
// GLOBAL: XVT 0x665688
char g_front_dialog_okay_label[128] = {0};
/* The third line of the confirm dialog's message, drawn 40 pixels below the
 * first. Written as each dialog opens, like g_front_dialog_okay_label. */
// GLOBAL: XVT 0x665708
char g_front_dialog_line3[256] = {0};
/* Cursor x when the last confirm, network-error or pilot-name dialog opened.
 * The original build moves the cursor back there when a confirm dialog with
 * neither label closes; the modern build writes it through xvt_dialog_confirm
 * and never reads it. */
// GLOBAL: XVT 0x665808
int g_front_dialog_saved_mouse_x = 0;
/* Cursor y when the last dialog opened; see g_front_dialog_saved_mouse_x. */
// GLOBAL: XVT 0x66580C
int g_front_dialog_saved_mouse_y = 0;
/* The confirm dialog's Cancel label, shown only as the Cancel button's tooltip;
 * empty picks a layout without that button. Written as each dialog opens, like
 * g_front_dialog_okay_label. */
// GLOBAL: XVT 0x665810
char g_front_dialog_cancel_label[128] = {0};
/* The first line of the confirm dialog's message, or, in the pilot-name prompt,
 * the name being typed, at most 12 characters; the prompt clears it as it opens
 * and its caller copies the name out. Written as each confirm dialog opens,
 * like g_front_dialog_okay_label. */
// GLOBAL: XVT 0x665890
char g_front_dialog_line1_or_edit[256] = {0};
/* The second line of the confirm dialog's message, drawn 20 pixels below the
 * first. Written as each dialog opens, like g_front_dialog_okay_label. */
// GLOBAL: XVT 0x665990
char g_front_dialog_line2[256] = {0};
/* The last confirm dialog's answer: 1 for OK or Enter, 0 for Cancel, Esc or a
 * network dismissal. Written by frontend_dialog_confirm_update_callback and
 * frontend_dialog_network_abort_error_callback; the modern build's xvt_dialog_update
 * sets it to 0 as each dialog opens. */
// GLOBAL: XVT 0xAA62A0
int g_dialog_result = 0;

/* Shows the confirm dialog, three message lines and the OK and Cancel labels,
 * NULL meaning empty, and returns its answer: 1 for OK, 0 for Cancel or a
 * network dismissal. The original build runs it modally over the whole screen:
 * it turns overlay text off, plays "warningsound" when
 * g_game_config.sfx_datapad_enabled is set, saves the cursor position in
 * g_front_dialog_saved_mouse_x and g_front_dialog_saved_mouse_y, copies the texts into
 * the g_frontDialog buffers with strcpy, unchecked against their 256 and 128
 * bytes, runs frontend_dialog_confirm_update_callback, stops the text fade, turns
 * overlay text back on when it was on and returns g_dialog_result. The modern
 * build returns xvt_dialog_confirm's result: XVT_DIALOG_PENDING (-1) while the
 * dialog runs, then the answer on the first call after it closes. */
// FUNCTION: XVT 0x4DCB90
int frontend_dialog_show_confirm_dialog(const char *line1, const char *line2,
					const char *line3,
					const char *okay_label,
					const char *cancel_label)
{
	return xvt_dialog_confirm(line1, line2, line3, okay_label, cancel_label,
				  0);
}

/* The confirm dialog's frame function. On frame 0 it flushes the typed
 * characters, puts the cursor at (184, 255) on the OK button, or at (496, 255)
 * when only a Cancel label is set, draws the "dialogbox" sprite translucent
 * three times onto the offscreen surface, starts a 20-frame text fade-in and
 * returns 0. On later frames it sets g_dialog_result to 0 and ends when
 * frontend_dialog_has_network_dismiss_packet returns 1, draws the three message
 * lines centered in the size-15 font in 0xFFFF, from y 225, 20 pixels apart,
 * and runs the buttons. With neither label it shows an OK button whose tooltip
 * is string FRONTSTR_523_OKAY; when it is pressed, the original build moves the
 * cursor back to g_front_dialog_saved_mouse_x and Y. With only one label, that one
 * button; with both, both. OK, or Enter, sets g_dialog_result to 1; Cancel, or
 * Esc, sets it to 0. Returns 1 when the dialog ends, else 0. Each later frame
 * takes one character from the keyboard buffer, and with both buttons up to
 * three. */
// FUNCTION: XVT 0x4DCD30
int frontend_dialog_confirm_update_callback(int frame_counter)
{
	enum {
		CURSOR_OK_X = 184,
		CURSOR_CANCEL_X = 496,
		CURSOR_Y = 255,
		DIALOG_LEFT = 166,
		DIALOG_TOP = 225,
		DIALOG_RIGHT = 518,
		DIALOG_BOTTOM = 245,
		DIALOG_LINE_HEIGHT = 20,
		OK_BUTTON_LEFT = 171,
		OK_BUTTON_TOP = 240,
		OK_BUTTON_RIGHT = 202,
		BUTTON_BOTTOM = 275,
		CANCEL_BUTTON_LEFT = 481,
		CANCEL_BUTTON_RIGHT = 513,
		DIALOG_FONT_SIZE = 15,
		BUTTON_FONT_SIZE = 12,
		OK_HELD_SLOT = 20,
		CANCEL_HELD_SLOT = 21,
		TEXT_COLOR = 0xFFFF,
		KEY_ENTER = 13,
		KEY_ESCAPE = 27,
		SAVE_OFFSCREEN_BACKUP = 1,
		TEXT_FADE_FRAMES = 20,
	};

	int finished = 0;
	if (frame_counter == 0) {
		keyboard_flush_char_buffer();
		if (g_front_dialog_okay_label[0] ||
		    !g_front_dialog_cancel_label[0]) {
			frontend_cursor_set_pos(CURSOR_OK_X, CURSOR_Y);
		} else {
			frontend_cursor_set_pos(CURSOR_CANCEL_X, CURSOR_Y);
		}
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_translucent("dialogbox", 0, 0);
		front_image_draw_sprite_translucent("dialogbox", 0, 0);
		front_image_draw_sprite_translucent("dialogbox", 0, 0);
		frontend_display_unlock_offscreen_surface(
			SAVE_OFFSCREEN_BACKUP);
		frontend_text_start_text_fade_in(TEXT_FADE_FRAMES);
		XVT_LOG_DEBUG(
			"dialog.confirm_shown ok=%d cancel=%d line1=\"%s\" line2=\"%s\" line3=\"%s\"",
			g_front_dialog_okay_label[0] != '\0',
			g_front_dialog_cancel_label[0] != '\0',
			g_front_dialog_line1_or_edit, g_front_dialog_line2,
			g_front_dialog_line3);
		return 0;
	}

	if (frontend_dialog_has_network_dismiss_packet() != 0) {
		XVT_LOG_DEBUG("dialog.network_dismissed frame=%d",
			      frame_counter);
		g_dialog_result = 0;
		finished = 1;
	}
	struct RECT rect;
	frontend_draw_rect_assign(&rect, DIALOG_LEFT, DIALOG_TOP, DIALOG_RIGHT,
				  DIALOG_BOTTOM);
	rect.bottom = rect.top + DIALOG_LINE_HEIGHT;
	frontend_text_draw_centered(DIALOG_FONT_SIZE,
				    g_front_dialog_line1_or_edit, &rect,
				    TEXT_COLOR);
	frontend_draw_rect_offset_xy(&rect, 0, DIALOG_LINE_HEIGHT);
	frontend_text_draw_centered(DIALOG_FONT_SIZE, g_front_dialog_line2,
				    &rect, TEXT_COLOR);
	frontend_draw_rect_offset_xy(&rect, 0, DIALOG_LINE_HEIGHT);
	frontend_text_draw_centered(DIALOG_FONT_SIZE, g_front_dialog_line3,
				    &rect, TEXT_COLOR);

	int pressed;
	if (!g_front_dialog_okay_label[0] && !g_front_dialog_cancel_label[0]) {
		frontend_draw_rect_assign(&rect, OK_BUTTON_LEFT, OK_BUTTON_TOP,
					  OK_BUTTON_RIGHT, BUTTON_BOTTOM);
		pressed = frontend_button_handle_sprite_button(
			&rect, "dialogok", "dialogokd",
			frontend_string_get(FRONTSTR_523_OKAY),
			BUTTON_FONT_SIZE, TEXT_COLOR, OK_HELD_SLOT,
			"buttonsound");
		if (keyboard_dequeue_char() == KEY_ENTER) {
			pressed = 1;
		}
		if (pressed != 0) {
			finished = 1;
			g_dialog_result = 1;
		}
	} else if (!g_front_dialog_okay_label[0]) {
		frontend_draw_rect_assign(&rect, CANCEL_BUTTON_LEFT,
					  OK_BUTTON_TOP, CANCEL_BUTTON_RIGHT,
					  BUTTON_BOTTOM);
		pressed = frontend_button_handle_sprite_button(
			&rect, "dialogcancel", "dialogcanceld",
			g_front_dialog_cancel_label, BUTTON_FONT_SIZE,
			TEXT_COLOR, CANCEL_HELD_SLOT, "buttonsound");
		if (keyboard_dequeue_char() == KEY_ESCAPE) {
			pressed = 1;
		}
		if (pressed != 0) {
			g_dialog_result = 0;
			finished = 1;
		}
	} else if (!g_front_dialog_cancel_label[0]) {
		frontend_draw_rect_assign(&rect, OK_BUTTON_LEFT, OK_BUTTON_TOP,
					  OK_BUTTON_RIGHT, BUTTON_BOTTOM);
		pressed = frontend_button_handle_sprite_button(
			&rect, "dialogok", "dialogokd",
			g_front_dialog_okay_label, BUTTON_FONT_SIZE, TEXT_COLOR,
			OK_HELD_SLOT, "buttonsound");
		if (keyboard_dequeue_char() == KEY_ENTER) {
			pressed = 1;
		}
		if (pressed != 0) {
			finished = 1;
			g_dialog_result = 1;
		}
	} else {
		frontend_draw_rect_assign(&rect, OK_BUTTON_LEFT, OK_BUTTON_TOP,
					  OK_BUTTON_RIGHT, BUTTON_BOTTOM);
		pressed = frontend_button_handle_sprite_button(
			&rect, "dialogok", "dialogokd",
			g_front_dialog_okay_label, BUTTON_FONT_SIZE, TEXT_COLOR,
			OK_HELD_SLOT, "buttonsound");
		if (keyboard_peek_char() == KEY_ENTER) {
			pressed = 1;
			keyboard_dequeue_char();
		}
		if (pressed != 0) {
			finished = 1;
			g_dialog_result = 1;
		}
		frontend_draw_rect_assign(&rect, CANCEL_BUTTON_LEFT,
					  OK_BUTTON_TOP, CANCEL_BUTTON_RIGHT,
					  BUTTON_BOTTOM);
		pressed = frontend_button_handle_sprite_button(
			&rect, "dialogcancel", "dialogcanceld",
			g_front_dialog_cancel_label, BUTTON_FONT_SIZE,
			TEXT_COLOR, CANCEL_HELD_SLOT, "buttonsound");
		if (keyboard_dequeue_char() == KEY_ESCAPE) {
			pressed = 1;
			keyboard_discard_char();
		}
		if (pressed != 0) {
			g_dialog_result = 0;
			finished = 1;
		}
	}
	return finished != 0;
}

/* Returns 1 when net_poll_for_packet_type_or_backlog reports a queued packet of any
 * of the 15 lobby packet types below, or a backlog of more than 512 packets,
 * else 0. Each poll pumps incoming packets; nothing is taken from the queue. */
// FUNCTION: XVT 0x4DD0F0
int frontend_dialog_has_network_dismiss_packet(void)
{

	if (net_poll_for_packet_type_or_backlog(NET_PACKET_PLAYER_ADMITTED) !=
	    0) {
		return 1;
	}
	if (net_poll_for_packet_type_or_backlog(NET_PACKET_HOST_CANCELLED) !=
	    0) {
		return 1;
	}
	if (net_poll_for_packet_type_or_backlog(NET_PACKET_PLAYER_LEFT) != 0) {
		return 1;
	}
	if (net_poll_for_packet_type_or_backlog(
		    NET_PACKET_TEAM_ASSIGNMENTS_READY) != 0) {
		return 1;
	}
	if (net_poll_for_packet_type_or_backlog(
		    NET_PACKET_FRONTEND_MISSION_START) != 0) {
		return 1;
	}
	if (net_poll_for_packet_type_or_backlog(
		    NET_PACKET_FRONTEND_OPCODE_74) != 0) {
		return 1;
	}
	if (net_poll_for_packet_type_or_backlog(
		    NET_PACKET_NEXT_TOURNAMENT_MISSION) != 0) {
		return 1;
	}
	if (net_poll_for_packet_type_or_backlog(
		    NET_PACKET_NEXT_BATTLE_MISSION) != 0) {
		return 1;
	}
	if (net_poll_for_packet_type_or_backlog(
		    NET_PACKET_REPLAY_CURRENT_MISSION) != 0) {
		return 1;
	}
	if (net_poll_for_packet_type_or_backlog(NET_PACKET_RETURN_TO_SETUP) !=
	    0) {
		return 1;
	}
	if (net_poll_for_packet_type_or_backlog(NET_PACKET_REPLAY_MISSION) !=
	    0) {
		return 1;
	}
	if (net_poll_for_packet_type_or_backlog(NET_PACKET_PLAYER_KICKED) !=
	    0) {
		return 1;
	}
	if (net_poll_for_packet_type_or_backlog(NET_PACKET_SESSION_CANCELLED) !=
	    0) {
		return 1;
	}
	if (net_poll_for_packet_type_or_backlog(
		    NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS) != 0) {
		return 1;
	}
	return net_poll_for_packet_type_or_backlog(
		       NET_PACKET_RETURN_TO_MISSION_SELECTION) != 0;
}

/* Asks for a new pilot's name and copies it into out_name as 12 characters and a
 * NUL, so out_name needs 13 bytes. The original build turns overlay text off,
 * saves the cursor position, clears g_front_dialog_line1_or_edit, runs
 * frontend_dialog_create_pilot_name_callback modally over the whole screen until a
 * name is accepted, stops the text fade, turns overlay text back on when it was
 * on and returns 1. The modern build returns xvt_dialog_pilot_name's result:
 * XVT_DIALOG_PENDING (-1) while the prompt runs, then, on the first call after
 * it closes, the name copied the same way and 1 when one was entered, else
 * 0. */
// FUNCTION: XVT 0x4DD220
int frontend_dialog_prompt_for_pilot_name(char *out_name)
{
	return xvt_dialog_pilot_name(out_name);
}

/* The pilot-name prompt's frame function. On frame 0 it puts the cursor at
 * (417, 291), flushes the typed characters, registers frontres\create.bmp as
 * the image "backname" and draws it opaque on the offscreen surface, then the
 * "frame" sprite, "allactive" (or "clientactive" when g_host_cd_available is 0)
 * and "createoverlay" translucent. Every frame it draws the heading string
 * FRONTSTR_716_CREATE_A_NEW_PILOT, an edit field over g_front_dialog_line1_or_edit
 * that takes up to 12 characters, none of \ * $, and a text button labeled
 * FRONTSTR_717_CREATE_PILOT. Enter, the field's own Enter or Tab, or the button
 * accepts; with a name typed it then frees "backname" and returns 1. Returns 0
 * otherwise. An Esc next in the keyboard buffer is taken; the modern build then
 * clears the name, frees "backname" and returns 1, while the original build
 * keeps prompting. */
// FUNCTION: XVT 0x4DD2C0
int frontend_dialog_create_pilot_name_callback(int frame_counter)
{
	if (frame_counter == 0) {
		frontend_cursor_set_pos(417, 291);
		keyboard_flush_char_buffer();
		front_image_register_resource_default("frontres\\create.bmp",
						      "backname");
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_opaque("backname", 0, 0);
		front_image_draw_sprite("frame", 0, 0);
		if (g_host_cd_available != 0) {
			front_image_draw_sprite("allactive", 0, 0);
		} else {
			front_image_draw_sprite("clientactive", 0, 0);
		}
		front_image_draw_sprite_translucent("createoverlay", 0, 0);
		frontend_display_unlock_offscreen_surface(1);
	}

	struct RECT rect;
	frontend_draw_rect_assign(&rect, 245, 225, 445, 245);
	frontend_text_draw_centered(
		15, frontend_string_get(FRONTSTR_716_CREATE_A_NEW_PILOT), &rect,
		0xFFFF);
	frontend_draw_rect_assign(&rect, 250, 245, 440, 265);
	int accepted = frontend_text_handle_editable_field(
		&rect, g_front_dialog_line1_or_edit, 13, 0, 12, "\\*$");
	frontend_draw_rect_assign(&rect, 250, 275, 440, 295);
	accepted |= frontend_button_handle_text_button(
		&rect, frontend_string_get(FRONTSTR_717_CREATE_PILOT), 15,
		0xFFFF, 20, "buttonsound");

	if (keyboard_peek_char() == 13) {
		keyboard_discard_char();
		accepted = 1;
	} else if (keyboard_peek_char() == 27) {
		keyboard_discard_char();
		g_front_dialog_line1_or_edit[0] = 0;
		front_image_free_resource_by_name("backname");
		return 1;
	}

	if (!accepted || g_front_dialog_line1_or_edit[0] == '\0') {
		if (accepted) {
			XVT_LOG_DEBUG("dialog.pilot_name_empty frame=%d",
				      frame_counter);
		}
		return 0;
	}
	front_image_free_resource_by_name("backname");
	return 1;
}

/* Only the original build calls this, when destroying the local DirectPlay
 * player took longer than NET_DESTROY_PLAYER_TIMEOUT_MS. Shows the dialog the
 * way frontend_dialog_show_confirm_dialog does, with
 * frontend_dialog_network_abort_error_callback, which no network packet dismisses,
 * and returns g_dialog_result. The modern build's body returns
 * xvt_dialog_confirm's result for the same dialog. */
// FUNCTION: XVT 0x4DD480
int frontend_dialog_show_network_abort_error(const char *line1,
					     const char *line2,
					     const char *line3,
					     const char *okay_label,
					     const char *cancel_label)
{
	return xvt_dialog_confirm(line1, line2, line3, okay_label, cancel_label,
				  1);
}

/* The network-error dialog's frame function: the same as
 * frontend_dialog_confirm_update_callback, without the check for a dismissing
 * network packet. Returns 1 when the dialog ends, else 0. Only the original
 * build reaches it. */
// FUNCTION: XVT 0x4DD620
int frontend_dialog_network_abort_error_callback(int frame_counter)
{
	int finished = 0;
	if (frame_counter == 0) {
		keyboard_flush_char_buffer();
		if (g_front_dialog_okay_label[0] != '\0' ||
		    g_front_dialog_cancel_label[0] == '\0') {
			frontend_cursor_set_pos(184, 255);
		} else {
			frontend_cursor_set_pos(496, 255);
		}
		frontend_display_lock_offscreen_surface();
		front_image_draw_sprite_translucent("dialogbox", 0, 0);
		front_image_draw_sprite_translucent("dialogbox", 0, 0);
		front_image_draw_sprite_translucent("dialogbox", 0, 0);
		frontend_display_unlock_offscreen_surface(1);
		frontend_text_start_text_fade_in(20);
		return 0;
	}

	struct RECT rect;
	frontend_draw_rect_assign(&rect, 166, 225, 518, 245);
	rect.bottom = rect.top + 20;
	frontend_text_draw_centered(15, g_front_dialog_line1_or_edit, &rect,
				    0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 20);
	frontend_text_draw_centered(15, g_front_dialog_line2, &rect, 0xFFFF);
	frontend_draw_rect_offset_xy(&rect, 0, 20);
	frontend_text_draw_centered(15, g_front_dialog_line3, &rect, 0xFFFF);

	int pressed;
	if (g_front_dialog_okay_label[0] == '\0') {
		if (g_front_dialog_cancel_label[0] == '\0') {
			frontend_draw_rect_assign(&rect, 171, 240, 202, 275);
			pressed = frontend_button_handle_sprite_button(
				&rect, "dialogok", "dialogokd",
				frontend_string_get(FRONTSTR_523_OKAY), 12,
				0xFFFF, 20, "buttonsound");
			if (keyboard_dequeue_char() == 13) {
				pressed = 1;
			}
			if (pressed != 0) {
				finished = 1;
				g_dialog_result = 1;
			}
		} else {
			frontend_draw_rect_assign(&rect, 481, 240, 513, 275);
			pressed = frontend_button_handle_sprite_button(
				&rect, "dialogcancel", "dialogcanceld",
				g_front_dialog_cancel_label, 12, 0xFFFF, 21,
				"buttonsound");
			if (keyboard_dequeue_char() == 27) {
				pressed = 1;
			}
			if (pressed != 0) {
				g_dialog_result = 0;
				finished = 1;
			}
		}
	} else {
		frontend_draw_rect_assign(&rect, 171, 240, 202, 275);
		if (g_front_dialog_cancel_label[0] == '\0') {
			pressed = frontend_button_handle_sprite_button(
				&rect, "dialogok", "dialogokd",
				g_front_dialog_okay_label, 12, 0xFFFF, 20,
				"buttonsound");
			if (keyboard_dequeue_char() == 13) {
				pressed = 1;
			}
			if (pressed != 0) {
				finished = 1;
				g_dialog_result = 1;
			}
		} else {
			pressed = frontend_button_handle_sprite_button(
				&rect, "dialogok", "dialogokd",
				g_front_dialog_okay_label, 12, 0xFFFF, 20,
				"buttonsound");
			if (keyboard_peek_char() == 13) {
				pressed = 1;
				keyboard_dequeue_char();
			}
			if (pressed != 0) {
				finished = 1;
				g_dialog_result = 1;
			}
			frontend_draw_rect_assign(&rect, 481, 240, 513, 275);
			pressed = frontend_button_handle_sprite_button(
				&rect, "dialogcancel", "dialogcanceld",
				g_front_dialog_cancel_label, 12, 0xFFFF, 21,
				"buttonsound");
			if (keyboard_dequeue_char() == 27) {
				pressed = 1;
				keyboard_discard_char();
			}
			if (pressed != 0) {
				g_dialog_result = 0;
				finished = 1;
			}
		}
	}
	return finished != 0;
}
