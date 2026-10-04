#ifndef XVT_FRONTEND_FRONTEND_STATE_H
#define XVT_FRONTEND_FRONTEND_STATE_H

#include "aeron/compat/mmsystem.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/audio/frontend_sound.h"
#include "xvt/frontend/front_image.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_screen.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/net/net.h"
#include "xvt/net/net_reliable.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct frontend_global_state;

/* The frontend's whole state in one block: display, input, sound, CD music,
 * fonts, screens, the lobby's DirectPlay session, the string table and the
 * install paths; g_front_state is the one instance. */
struct frontend_global_state {
	/* Scratch row front_image_compress_rle and the original build's glyph
	 * encoding fill with one encoded image row. */
	struct front_image_rle_row_buffer rle_row_buffer;
	/* Heap table of the loaded images, room for 512, kept sorted by name
	 * for front_image_find_resource_by_name. */
	struct front_image_resource_record *resource_table;
	int resource_count; /* Images in resource_table. */
	/* Cursor x in the 640 by 480 screen, from the window's mouse moves,
	 * frontend_cursor_set_pos or the modern input bridge. */
	int mouse_x;
	int mouse_y;		/* Cursor y, kept like mouse_x. */
	int cursor_prev_draw_x; /* x where frontend_cursor_draw last drew it. */
	int cursor_prev_draw_y; /* y where frontend_cursor_draw last drew it. */
	/* 1 while the frame loop draws the cursor after each frame. */
	uint8_t cursor_visible;
	/* The default 10 by 10 cursor, copied from g_default_cursor_bitmap by
	 * frontend_cursor_init: 0 is clear; at 16 bits 1 draws color 31 and
	 * 0xFF white. */
	uint8_t cursor_default_mask[100];
	/* Screen under the default cursor, 100 pixels of up to 2 bytes. */
	uint8_t cursor_default_save_buf[200];
	/* Pixels of the cursor: cursor_default_mask, or the image
	 * frontend_cursor_set_image_from_resource_name picks. */
	uint8_t *cursor_mask_pixels;
	/* Where frontend_cursor_draw keeps the screen under the cursor and
	 * frontend_cursor_restore puts it back from. */
	uint8_t *cursor_save_buf;
	int cursor_width;  /* Cursor width in pixels, 10 by default. */
	int cursor_height; /* Cursor height in pixels, 10 by default. */
	int cursor_prev_draw_width; /* Part of the cursor last drawn, clipped. */
	int cursor_prev_draw_height; /* Part of the cursor last drawn, clipped. */
	/* Image name the cursor is drawn from; empty for the default cursor,
	 * drawn from cursor_mask_pixels. */
	char cursor_sprite_name[64];
	uint8_t mouse_left_down;  /* 1 while the left button is held. */
	uint8_t mouse_right_down; /* 1 while the right button is held. */
	/* 1 once the left button is released, until the end of the frame. */
	uint8_t mouse_left_click_latch;
	/* 1 once the right button is released, until the end of the frame. */
	uint8_t mouse_right_click_latch;
	/* Id of the control that has the mouse, or 0; while it is set the
	 * plain FrontendMouse getters report no buttons. */
	int mouse_input_gate;
	/* Windows joystick id of each of the two joystick slots. */
	unsigned int joy_device_ids[2];
	/* Set to 1 by joystick_init_devices; nothing reads it. */
	uint8_t joystick_init_flags[2];
	/* 1 for a slot with a working joystick. */
	uint8_t joystick_present[2];
	uint8_t joystick_has_pov[2];	  /* 1 when the joystick has a hat. */
	uint8_t joystick_button_count[2]; /* Buttons the joystick reports. */
	/* Per slot and button, 1 while held at the last update. */
	uint8_t joystick_button_held[2][32];
	/* Per slot and button, 1 when released at the last update; cleared
	 * after every frame. */
	uint8_t joystick_button_released[2][32];
	/* Hat direction: 0 centered, else the hat's angle in hundredths of a
	 * degree divided by 0x2328, plus 1. */
	uint8_t joystick_pov_direction[2];
	/* Stick x: 0 while no more than 1000 from center, else the distance
	 * from center divided by joystick_x_negative_scale or
	 * joystick_x_positive_scale. */
	int joystick_axis_x[2];
	/* Stick y, worked out like joystick_axis_x. */
	int joystick_axis_y[2];
	uint32_t joystick_x_min[2]; /* Lowest x the driver reports. */
	uint32_t joystick_x_max[2]; /* Highest x the driver reports. */
	uint32_t joystick_y_min[2]; /* Lowest y the driver reports. */
	uint32_t joystick_y_max[2]; /* Highest y the driver reports. */
	/* x read when joystick_init_devices ran, taken as the center. */
	int joystick_x_center[2];
	/* y read when joystick_init_devices ran, taken as the center. */
	int joystick_y_center[2];
	/* (center x - lowest x) / 255; at least 1 in the modern build. */
	int joystick_x_negative_scale[2];
	/* (highest x - center x) / 255; at least 1 in the modern build. */
	int joystick_x_positive_scale[2];
	/* (center y - lowest y) / 255; at least 1 in the modern build. */
	int joystick_y_negative_scale[2];
	/* (highest y - center y) / 255; at least 1 in the modern build. */
	int joystick_y_positive_scale[2];
	/* Windows key state by virtual key, the 0x80 bit set while down;
	 * filled each frame by GetKeyboardState, or the modern input bridge. */
	uint8_t key_state[256];
	/* Cleared on every key release; nothing reads it. */
	uint8_t key_down_state[256];
	/* Ring of typed characters, written by the window procedure or the
	 * modern input bridge and read by keyboard_dequeue_char. */
	char char_ring_buffer[1024];
	int char_write_idx; /* Next free entry of char_ring_buffer. */
	/* Oldest unread entry; equal to char_write_idx when empty. */
	int char_read_idx;
	/* 1 while Esc quits the game: set when the display starts, cleared by
	 * frontend_display_disable_escape_close. */
	uint8_t escape_close_enabled;
	/* Surface record of the locked back buffer; its lpSurface is the
	 * drawing pointer. */
	DDSURFACEDESC back_buffer_desc;
	/* 1 while the back buffer is locked; many callers save it and lock
	 * again after work that unlocks. */
	uint8_t back_buffer_locked;
	/* 1 to clear the back buffer after each present. */
	uint8_t clear_back_buffer_after_present;
	uint32_t surface_clear_color; /* Fill color for clearing surfaces. */
	/* Bytes per row of the surface g_draw_surface_ptr points into. */
	int draw_surface_pitch;
	int back_buffer_pitch;	     /* Bytes per row of the back buffer. */
	int offscreen_surface_pitch; /* Bytes per row of the offscreen surface. */
	int display_bpp;	     /* Bits per pixel, 8 or 16. */
	/* 1 when the 16-bit display packs 5-5-5 (the green mask lacks the
	 * 0x400 bit), 0 for 5-6-5. */
	uint8_t pixel_format555;
	/* 1 to refill the back buffer from the offscreen surface after each
	 * present; the offscreen surface holds the screen's fixed
	 * background. */
	uint8_t offscreen_restore_enabled;
	/* 1 when DirectDraw runs on the driver frontend_display_load_driver_guid
	 * named, 0 on the default one. */
	uint8_t secondary_direct_draw_active;
	void *hWnd; /* The game window. */
	/* Which window procedure frontend_display_wnd_proc forwards to: 0
	 * frontend, 1 flight, 2 movie. */
	int frontend_display_wnd_proc_mode;
	IDirectDraw *direct_draw; /* The DirectDraw object. */
	/* Surface the fixed background is drawn into. */
	IDirectDrawSurface *offscreen_surface;
	IDirectDrawSurface *primary_surface; /* The visible surface. */
	/* Surface the frame is drawn into before it is shown. */
	IDirectDrawSurface *back_buffer_surface;
	/* Nonzero while the game is the active application; the original
	 * frame loop runs frames only then. */
	int app_active;
	/* Never read or written by name. */
	uint8_t unused_display_state_e42[0x40];
	int32_t clip_min_x; /* Left edge of the screen clip, inclusive. */
	int32_t clip_max_x; /* Right edge of the screen clip, inclusive. */
	int32_t clip_min_y; /* Top edge of the screen clip, inclusive. */
	int32_t clip_max_y; /* Bottom edge of the screen clip, inclusive. */
	/* Copy of the offscreen surface, 480 rows, saved on request when it is
	 * unlocked and put back after the game is activated again. */
	void *offscreen_backup_buffer;
	/* 1 after the game is activated, until the next present puts
	 * offscreen_backup_buffer back. */
	int restore_offscreen_overlay_after_activate;
	IDirectDrawPalette *dd_palette; /* Palette of the 8-bit display. */
	struct frontend_palette_entry
		display_palette[256]; /* The display's colors. */
	/* Checked and cleared by the present at 8 bits; nothing sets it to
	 * 1. */
	uint8_t palette_needs_set;
	/* Colors that text codes 2 to 6 switch to, entries 1 to 5; entry 0 is
	 * set to 0xFFFF. */
	int text_color_codes[6];
	IDirectSound *frontend_direct_sound; /* The frontend's DirectSound. */
	/* DirectSound's primary buffer. */
	IDirectSoundBuffer *frontend_primary_sound_buffer;
	/* Heap table of the loaded sounds, up to 128, kept sorted by name. */
	struct frontend_sound_buffer_record *frontend_sound_buffers;
	/* Heap table of the 12 voices that play sounds. */
	struct frontend_sound_voice *frontend_sound_voices;
	int frontend_sound_buffer_count; /* Sounds in frontend_sound_buffers. */
	int frontend_active_voice_count; /* Voices playing, 0 to 12. */
	/* Count of sounds started; each voice keeps its start number, and
	 * the oldest gives way when all 12 are busy. */
	int frontend_sound_play_serial;
	/* MCI id of the open CD audio device; 0 when none is open. */
	MCIDEVICEID cd_audio_mci_device_id;
	int cd_audio_current_track; /* Track playing, 0 for none. */
	int cd_audio_track_count;   /* Tracks on the CD. */
	/* 1 once a track that does not loop has run past its end time. */
	int cd_audio_playback_complete;
	/* GetTickCount at which the track playing ends: its length plus 2000
	 * ms after it started. */
	uint32_t cd_audio_track_end_ms;
	/* 1 to start the track again when it ends. */
	int cd_audio_loop_current_track;
	/* Milliseconds of the track left when it was suspended. */
	int cd_audio_suspend_remaining_ms;
	/* Milliseconds of the track played when it was suspended; playback
	 * resumes from there. */
	int cd_audio_suspend_elapsed_ms;
	/* GetTickCount after which a pending resume plays again: 1000 ms
	 * after cd_audio_request_resume_playback. */
	uint32_t cd_audio_resume_due_ms;
	/* Not suspended, suspended, or resume pending. */
	cd_audio_suspend_state cd_audio_suspend_state;
	/* CD aux volume that cd_audio_initialize found, low 16 bits. */
	int cd_audio_saved_aux_volume;
	/* Volume set last and each track's length. */
	struct cd_audio_track_cache cd_audio_track_cache;
	struct bitmap_font font_slots[10]; /* The loaded fonts. */
	/* Font of each point size, NULL when not loaded. */
	struct bitmap_font *font_by_size[256];
	/* Frames left in the text fade-in; text is drawn faded while it is
	 * nonzero. */
	int text_fade_frames_left;
	int text_fade_frame_count; /* Length of the text fade-in, in frames. */
	/* Faded value of each 16-bit color for the current fade frame, 0 when
	 * not yet worked out; cleared every frame of a fade. */
	text_fade_color_cache text_fade_color_cache;
	/* Function the original frame loop runs once before the first frame;
	 * a nonzero result shuts the display down. */
	int (*mode_init_fn)(void);
	/* Screen frontend_screen_queue_push asked for, pushed after the frame;
	 * NULL when none waits. */
	frontend_screen_update_fn pending_screen_update_fn;
	struct RECT
		pending_screen_rect; /* Area of the screen waiting to be pushed. */
	int screen_stack_top; /* Index in screen_states of the screen running. */
	/* 1 after frontend_screen_set_callbacks, until the frame loop has run
	 * the old screen's exit function. */
	int screen_callbacks_dirty;
	/* The stack of screens: callbacks, and what a pushed screen saved of
	 * the one under it. */
	struct frontend_screen_state screen_states[10];
	/* Milliseconds per frontend frame: 1000 divided by the frame rate. */
	int frame_interval_ms;
	/* Frames the running screen has had, from 0; set to -1 when its
	 * callbacks change so the new screen starts at 0. */
	int frame_counter;
	/* DirectPlay object a session start creates and releases once it has
	 * the IDirectPlay2A interface. */
	IDirectPlay *net_temp_direct_play;
	/* The lobby session's IDirectPlay2A interface; NULL without one. */
	IDirectPlay2A *net_direct_play;
	/* DirectPlay lobby object, held while a lobby connection opens. */
	IDirectPlayLobbyA *net_direct_play_lobby;
	uint8_t unused_net_state_27ecd[4]; /* Never read or written by name. */
	GUID net_app_guid; /* The game's DirectPlay application GUID. */
	GUID net_joined_session_guid; /* GUID of the session joined. */
	DPID net_host_player_id; /* The host's DirectPlay id; 0 until known. */
	/* DirectPlay group of the session's players. */
	DPID net_group_dplay_id;
	int net_is_host;      /* Nonzero on the session's host. */
	int net_player_count; /* Entries in net_players. */
	/* Set when a ready player leaves (seen by the host); cleared every
	 * frontend frame. */
	int net_ready_player_left_this_frame;
	char net_session_name[32]; /* Name of the session. */
	/* The lobby roster: entry 0 the local player, then the others. */
	struct net_player_info net_players[32];
	struct net_player_info
		net_runtime_local_player; /* The local player's entry. */
	/* Next sequence, 0 to 127, for packets to all players. */
	int net_runtime_broadcast_seq_counter;
	/* Last packet to all players, sent again behind the next one. */
	struct net_piggyback_payload net_runtime_broadcast_pending_payload;
	/* Next sequence, 0 to 127, for the group channel. */
	int net_runtime_group_seq_counter;
	/* Last group-channel packet, sent again behind the next one. */
	struct net_piggyback_payload net_runtime_group_pending_payload;
	uint8_t unused_net_state_28865[4]; /* Never read or written by name. */
	/* Set to 1 by frontend_display_reset_global_state_preserving_network_session,
	 * whose one caller nothing calls; nothing reads it. */
	int frontend_post_reset_marker;
	/* Only ever set to 0. While nonzero, a gap in the lobby's packets
	 * would wait 20 seconds and ask no more. */
	int net_reliable_retry_long_timeout_mode;
	/* The lobby's receive queue, a ring of 1024 packets. */
	struct net_queued_packet net_runtime_recv_queue[1024];
	int net_runtime_recv_queue_write_index; /* Next free entry of the queue. */
	int net_runtime_recv_queue_read_index; /* Oldest entry of the queue. */
	int net_runtime_recv_queue_count;      /* Entries in the queue. */
	/* The last 128 packets sent, kept for resends. */
	struct net_queued_packet net_runtime_sent_history[128];
	/* Entry net_runtime_sent_history writes next. */
	int net_runtime_sent_history_write_index;
	/* Copy of the packet handed out last; callers get a pointer into
	 * it. */
	struct net_queued_packet net_runtime_recv_scratch_packet;
	/* Per-peer delivery state of the lobby session. */
	struct net_reliable_peer_slot net_runtime_reliable_peer_slots[40];
	/* Never read or written by name. */
	uint8_t unused_net_state_c2b51[0x238];
	/* net_runtime_reliable_peer_slots in use. */
	uint32_t net_reliable_peer_slot_count;
	/* After a flight, net_session_export_runtime_state points it at the
	 * flight's 256-entry sent world-message history, which
	 * net_pump_incoming_packets resends from on a WORLD_NACK. */
	struct net_queued_packet *net_flight_sent_world_message_history;
	/* The write index of that world-message history. */
	int net_flight_sent_world_message_write_index;
	/* Heap table of each string's offset in ui_string_data. */
	unsigned int *ui_string_offsets;
	/* Heap block of the string table's text, from fronttxt.txt. */
	char *ui_string_data;
	unsigned int ui_string_count;	 /* Strings in the table. */
	unsigned int ui_string_capacity; /* Entries ui_string_offsets holds. */
	/* Lowercase drive letter of the install folder; 0 in the modern
	 * build. */
	char install_drive_letter;
	/* Drive letter of the game CD; 0 when none was found and in the
	 * modern build. */
	char cd_drive_letter;
	/* The install folder; "BalanceOfPower" in the modern build. */
	char install_path[256];
	/* The base game's install folder; empty in the modern build. */
	char base_game_install_path[256];
};

typedef char
	xvt_size_frontend_global_state[(sizeof(void *) != 4 ||
					sizeof(struct frontend_global_state) ==
						0xC2FA7)
					       ? 1
					       : -1];

extern struct frontend_global_state g_front_state;

#ifdef __cplusplus
}
#endif

#endif
