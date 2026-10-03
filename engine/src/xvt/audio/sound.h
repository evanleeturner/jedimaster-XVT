#ifndef XVT_AUDIO_SOUND_H
#define XVT_AUDIO_SOUND_H

#include "aeron/compat/win_types.h"
#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sound_queue_entry {
	/* Effect name, from strncpy; not terminated for a name of 64 or more
	 * characters. */
	char name[64];
	int volume; /* Volume; sound_play_effect_now clamps it to 0 to 127. */
	/* Pan; sound_play_effect_now clamps it to 0 to 127, 63 centered. */
	int pan;
	/* 1 plays the effect looping; any other value plays it once. */
	int loop;
	/* Lets sound_play_effect_now rewind a playing sound of the effect when
	 * all 8 slots play and none holds an effect of lower
	 * current_priority. */
	int allow_restart_existing;
	/* Queue order, highest first; sound_play_effect_now weighs it against the
	 * playing effects' current_priority. */
	int priority;
};

struct sound_effect_def {
	/* Name the effect is found by; g_sound_defs is sorted by it. Up to 63
	 * characters; empty in an unused entry. */
	char name[64];
	/* WAV file it was loaded from, cut at 191 characters;
	 * sound_play_effect_now reloads a lost buffer from it. */
	char file_name[256];
	/* The loaded buffer each play duplicates. */
	IDirectSoundBuffer *buffer;
	/* Priority, 0 to 255, of the effect's playing sounds when
	 * sound_play_effect_now needs a slot; 0 at load, and only
	 * sound_set_effect_current_priority, which no caller reaches, changes
	 * it. */
	uint8_t current_priority;
};

extern IDirectSound *g_direct_sound;
extern IDirectSoundBuffer *g_sound_primary_buffer;

struct active_sound_instance {
	/* g_sound_defs entry this slot plays, or -1 when free; kept in step as
	 * entries are inserted and removed. */
	int effect_index;
	/* g_next_sound_instance_seq when the sound started or was last rewound;
	 * the lowest is the oldest. */
	unsigned int sequence;
	/* The duplicate buffer playing; NULL when free. */
	IDirectSoundBuffer *buffer;
};

extern struct sound_effect_def g_sound_defs[1000];
extern struct active_sound_instance g_active_sound_instances[8];
extern struct sound_queue_entry g_sound_queue[5];
extern int g_sound_queue_count;
extern int g_sound_count;

int sound_init_sound_engine(void *hwnd);
int sound_shutdown_sound_engine(void);
int sound_load_effect(const char *file_name, const char *name);
int sound_load_effect_ex(const char *file_name, const char *name,
			 int omit_software_and_frequency_caps);
void sound_unload_all_effects(void);
int sound_unload_effect_by_name(const char *name);
void sound_flush_queued_effects(void);
int sound_queue_effect(const char *sound_name, int allow_restart_existing,
		       int loop, int priority, int volume, int pan);
int sound_play_effect_now(const char *sound_name, int allow_restart_existing,
			  int loop, int priority, int volume, int pan);
int sound_stop_oldest_instance(const char *name);
int sound_stop_all_instances(void);
int sound_get_primary_buffer_volume(void);
int sound_set_latest_instance_volume(const char *name, int volume);
int sound_get_latest_instance_volume(const char *name);
int sound_set_latest_instance_pan(const char *name, int pan);
int sound_get_latest_instance_pan(const char *name);
int sound_set_latest_instance_frequency(const char *name, uint32_t frequency);
int sound_set_effect_current_priority(const char *name, int priority);
int sound_get_effect_current_priority(const char *name);
int sound_count_playing_instances(const char *name);
void sound_insert_effect_def_sorted(const struct sound_effect_def *effect);
void sound_remove_effect_def(int effect_index);
int sound_find_loaded_effect_by_name(const char *name);
int sound_find_effect_by_name(const struct sound_effect_def *records,
			      int last_index, const char *name);
int sound_set_param(int flight_sound_id, int param_code, int value);
int sound_get_param(int flight_sound_id, int param_code);
int sound_unused_four_arg_stub(int arg1, int arg2, int arg3, int arg4);
int sound_stop_oldest_instance_by_id(int flight_sound_id);
void sound_empty_stub(void);

#ifdef __cplusplus
}
#endif

#endif
