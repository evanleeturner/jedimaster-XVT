#ifndef XVT_AUDIO_FRONTEND_SOUND_H
#define XVT_AUDIO_FRONTEND_SOUND_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct frontend_sound_buffer_record {
	/* Name the sound is found by; the records are sorted by it. Up to 63
	 * characters; empty in an unused record. */
	char name[64];
	/* WAV file it was loaded from, cut at 191 characters;
	 * frontend_sound_play_ui_sound reloads a lost buffer from it. */
	char file_name[256];
	/* The loaded buffer each play duplicates. */
	IDirectSoundBuffer *buffer;
	/* Priority, 0 to 255, weighed when frontend_sound_play_ui_sound needs a
	 * voice; 0 at load, and only frontend_sound_set_buffer_priority_by_name,
	 * which nothing calls, changes it. */
	uint8_t priority;
};

struct frontend_sound_voice {
	/* Record this voice plays, or -1 when free; kept in step as records are
	 * inserted and removed. */
	int buffer_index;
	/* frontend_sound_play_serial when the voice started or was last rewound;
	 * the lowest is the oldest. */
	int play_serial;
	/* The duplicate buffer playing; NULL when free. */
	IDirectSoundBuffer *buffer;
};

int frontend_sound_init_direct_sound(void *hwnd);
int frontend_sound_shutdown_direct_sound(void);
int frontend_sound_load_sound(const char *file_name, const char *sound_name);
int frontend_sound_load_sound_file(const char *file_name,
				   const char *sound_name,
				   int omit_software_and_frequency_caps);
void frontend_sound_unload_all_buffers(void);
int frontend_sound_unload_buffer_by_name(const char *sound_name);
int frontend_sound_play_ui_sound(const char *sound_name,
				 int allow_restart_existing, int loop,
				 int priority, int volume0_to127,
				 int pan0_to127);
int frontend_sound_stop_oldest_voice_by_name(const char *name);
int frontend_sound_stop_all_voices(void);
int frontend_sound_set_primary_volume(int volume0_to127);
int frontend_sound_get_primary_volume(void);
int frontend_sound_set_newest_voice_volume_by_name(const char *name,
						   int volume0_to127);
int frontend_sound_get_newest_voice_volume_by_name(const char *name);
int frontend_sound_set_newest_voice_pan_by_name(const char *name,
						int pan0_to127);
int frontend_sound_get_newest_voice_pan_by_name(const char *name);
int frontend_sound_set_buffer_priority_by_name(const char *name,
					       int priority0_to255);
int frontend_sound_get_buffer_priority_by_name(const char *name);
int frontend_sound_get_playing_count(const char *name);
void frontend_sound_insert_sorted_buffer(
	const struct frontend_sound_buffer_record *record);
void frontend_sound_remove_buffer_record(int buffer_index);
int frontend_sound_find_buffer_by_name(const char *name);
int frontend_sound_binary_search_buffer_by_name(
	const struct frontend_sound_buffer_record *records, int last_index,
	const char *name);
int frontend_sound_load_list(const char *file_name);
int frontend_sound_unload_list(char *file_name);

#ifdef __cplusplus
}
#endif

#endif
