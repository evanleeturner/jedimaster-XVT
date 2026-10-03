#include "xvt/audio/sound.h"

#include "aeron/compat/dsound.h"
#include "xvt/audio/direct_sound.h"
#include "xvt/audio/fsfx.h"

#include <string.h>

/* The DirectSound object sound_init_sound_engine creates; NULL before that and
 * after sound_shutdown_sound_engine releases it. Most Sound_ functions return 0
 * at once while it is NULL. */
// GLOBAL: XVT 0xA10514
IDirectSound *g_direct_sound = 0;
/* The primary buffer sound_init_sound_engine creates. Only
 * sound_get_primary_buffer_volume reads it, and nothing calls that;
 * sound_shutdown_sound_engine sets it to NULL without releasing it. */
// GLOBAL: XVT 0xA10518
IDirectSoundBuffer *g_sound_primary_buffer = 0;
/* The loaded effects: entries 0 to g_sound_count - 1, sorted by name for the
 * binary search in sound_find_effect_by_name. Entries past that are empty or stale
 * copies left by sound_remove_effect_def. */
// GLOBAL: XVT 0xA1051C
struct sound_effect_def g_sound_defs[1000] = {{0}};
/* The 8 slots for playing sounds, each a duplicate buffer of one effect; a free
 * slot has effect_index -1. */
// GLOBAL: XVT 0xA604CC
struct active_sound_instance g_active_sound_instances[8] = {{0}};
/* Effects waiting for sound_flush_queued_effects: entries 0 to
 * g_sound_queue_count - 1, highest priority first. sound_queue_effect keeps at
 * most 4; the fifth entry only takes the one an insert pushes out, which is
 * dropped. */
// GLOBAL: XVT 0xA6052C
struct sound_queue_entry g_sound_queue[5] = {{0}};
/* Entries in g_sound_queue, 0 to 4. sound_queue_effect adds one, stopping at 4,
 * and sound_flush_queued_effects sets 0. */
// GLOBAL: XVT 0xA606D0
int g_sound_queue_count = 0;
/* Effects loaded in g_sound_defs, 0 to 1000. sound_insert_effect_def_sorted adds
 * one, sound_remove_effect_def takes one off and sound_init_sound_engine sets 0;
 * sound_shutdown_sound_engine leaves it. */
// GLOBAL: XVT 0xA606D4
int g_sound_count = 0;
/* Slots in use in g_active_sound_instances, 0 to 8. sound_play_effect_now adds and
 * frees slots, sound_stop_oldest_instance frees them and sound_init_sound_engine
 * sets 0; sound_shutdown_sound_engine clears the slots but leaves the count. */
// GLOBAL: XVT 0xA606D8
static int g_active_sound_count = 0;
/* Sequence number the next started or restarted sound takes; each one adds 1.
 * The lowest sequence in a slot is the oldest sound. sound_init_sound_engine
 * and sound_shutdown_sound_engine set 0. */
// GLOBAL: XVT 0xA606DC
static unsigned int g_next_sound_instance_seq = 0;

/* Starts DirectSound for flight. Returns 1 at once when g_direct_sound is
 * already set. Otherwise it clears the 8 slots of g_active_sound_instances,
 * g_active_sound_count, g_sound_count, g_next_sound_instance_seq and the buffer and
 * name of every g_sound_defs entry, creates g_direct_sound with DirectSoundCreate
 * on the default device, sets cooperative level 2 (DSSCL_PRIORITY) for hwnd and
 * creates g_sound_primary_buffer. Returns 1, or 0 when a step fails, after
 * calling sound_shutdown_sound_engine when the object exists. It sets no format
 * on the primary buffer: the format it clears is never used. flight_main calls
 * it in the original build, xvt_flight_entry_create_devices in the modern one. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x42CD50
int sound_init_sound_engine(void *hwnd)
{
	int instance_index;
	int effect_index;
	DSBUFFERDESC primary_buffer_desc;
	WAVEFORMATEX primary_format;
	HRESULT result;

	if (g_direct_sound != NULL) {
		return 1;
	}
	for (instance_index = 0; instance_index < 8; ++instance_index) {
		g_active_sound_instances[instance_index].effect_index = -1;
		g_active_sound_instances[instance_index].sequence = 0;
		g_active_sound_instances[instance_index].buffer = NULL;
	}
	g_active_sound_count = 0;
	g_sound_count = 0;
	g_next_sound_instance_seq = 0;
	for (effect_index = 0; effect_index < 1000; ++effect_index) {
		g_sound_defs[effect_index].buffer = NULL;
		g_sound_defs[effect_index].name[0] = '\0';
	}

	if (DirectSoundCreate(NULL, (void **)&g_direct_sound, NULL) != 0) {
		return 0;
	}
	if (g_direct_sound->lpVtbl->SetCooperativeLevel(g_direct_sound, hwnd,
							2) != 0) {
		sound_shutdown_sound_engine();
		return 0;
	}

	memset(&primary_buffer_desc, 0, sizeof(primary_buffer_desc));
	primary_buffer_desc.dwSize = 20;
	primary_buffer_desc.dwFlags = DSBCAPS_PRIMARYBUFFER;
	result = g_direct_sound->lpVtbl->CreateSoundBuffer(
		g_direct_sound, &primary_buffer_desc, &g_sound_primary_buffer,
		NULL);
	if (result != 0) {
		sound_shutdown_sound_engine();
		return 0;
	}
	memset(&primary_format, 0, sizeof(primary_format));
	return 1;
}

/* Returns 1 at once when g_direct_sound is NULL. Otherwise it unloads the
 * effects with sound_unload_all_effects, releases g_direct_sound and sets it to
 * NULL, clears the buffer and name of every g_sound_defs entry and the 8
 * instance slots, sets g_sound_primary_buffer to NULL without releasing it and
 * g_next_sound_instance_seq to 0, and returns 1. It does not set g_sound_count or
 * g_active_sound_count back to 0. flight_main calls it in the original build,
 * xvt_flight_entry_cleanup in the modern one. */
// FUNCTION: XVT 0x42CE60
int sound_shutdown_sound_engine(void)
{
	int effect_index;
	int instance_index;

	if (g_direct_sound == NULL) {
		return 1;
	}
	sound_unload_all_effects();
	g_direct_sound->lpVtbl->Release(g_direct_sound);
	g_direct_sound = NULL;
	for (effect_index = 0; effect_index < 1000; ++effect_index) {
		g_sound_defs[effect_index].buffer = NULL;
		g_sound_defs[effect_index].name[0] = '\0';
	}
	for (instance_index = 0; instance_index < 8; ++instance_index) {
		g_active_sound_instances[instance_index].effect_index = -1;
		g_active_sound_instances[instance_index].sequence = 0;
		g_active_sound_instances[instance_index].buffer = NULL;
	}
	g_sound_primary_buffer = NULL;
	g_next_sound_instance_seq = 0;
	return 1;
}

/* Calls sound_load_effect_ex with omit_software_and_frequency_caps 0, so the buffer
 * gets frequency control, and returns its result. fsfx_load_sfx_list is its only
 * caller. */
// FUNCTION: XVT 0x42CEE0
int sound_load_effect(const char *file_name, const char *name)
{
	return sound_load_effect_ex(file_name, name, 0);
}

/* Loads a WAV file as a named effect: direct_sound_load_wave_buffer makes its
 * buffer, which it rewinds to 0, and sound_insert_effect_def_sorted adds the entry
 * with the name (up to 63 characters), the file name (up to 191) and
 * current_priority 0. Returns 1, or 0 when either string is empty, 1000 effects
 * are loaded, g_direct_sound is NULL, the name is already loaded or the buffer
 * fails to load. Only sound_load_effect calls it, passing 0. */
// FUNCTION: XVT 0x42CF00
int sound_load_effect_ex(const char *file_name, const char *name,
			 int omit_software_and_frequency_caps)
{
	struct sound_effect_def effect;

	if (*file_name == '\0') {
		return 0;
	}
	if (*name == '\0') {
		return 0;
	}
	if (g_sound_count >= 1000) {
		return 0;
	}
	if (g_direct_sound == NULL) {
		return 0;
	}
	if (sound_find_loaded_effect_by_name(name) != -1) {
		return 0;
	}
	effect.buffer = direct_sound_load_wave_buffer(
		g_direct_sound, file_name, omit_software_and_frequency_caps);
	if (effect.buffer != NULL) {
		effect.buffer->lpVtbl->SetCurrentPosition(effect.buffer, 0);
		strncpy(effect.name, name, sizeof(effect.name));
		effect.name[63] = '\0';
		strncpy(effect.file_name, file_name, sizeof(effect.file_name));
		effect.file_name[191] = '\0';
		effect.current_priority = 0;
		sound_insert_effect_def_sorted(&effect);
		return effect.buffer != NULL;
	}
	return 0;
}

/* Calls sound_unload_effect_by_name with the name in each of the 1000 g_sound_defs
 * entries in index order. Each unload moves the later entries up one, so the
 * loop skips names: with n effects loaded it leaves (n - 1) / 2 of them loaded,
 * rounded down, buffers included. sound_shutdown_sound_engine and
 * fsfx_unload_all_effects_thunk call it. */
// FUNCTION: XVT 0x42D020
void sound_unload_all_effects(void)
{
	int effect_index;

	effect_index = 0;
	do {
		sound_unload_effect_by_name(g_sound_defs[effect_index].name);
		++effect_index;
	} while (effect_index < 1000);
}

/* Stops the named effect's sounds with sound_stop_oldest_instance until it
 * returns other than 1, releases the effect's buffer and removes its entry with
 * sound_remove_effect_def. Returns 1, or 0 when the name is empty or not
 * loaded. */
// FUNCTION: XVT 0x42D040
int sound_unload_effect_by_name(const char *name)
{
	int effect_index;

	if (*name == '\0') {
		return 0;
	}
	effect_index = sound_find_loaded_effect_by_name(name);
	if (effect_index == -1) {
		return 0;
	}
	while (sound_stop_oldest_instance(g_sound_defs[effect_index].name) ==
	       1) {
	}
	g_sound_defs[effect_index].buffer->lpVtbl->Release(
		g_sound_defs[effect_index].buffer);
	sound_remove_effect_def(effect_index);
	return 1;
}

/* Plays every queued effect with sound_play_effect_now in queue order, highest
 * priority first, and sets g_sound_queue_count to 0. The original build calls it
 * from flight_run_mission_loop and flight_sync_apply_world_message_packet, the modern
 * one from xvt_flight_frame_render, xvt_flight_frame_advance and
 * xvt_flight_frame_confirm. */
// FUNCTION: XVT 0x42D0D0
void sound_flush_queued_effects(void)
{
	int queue_index;

	queue_index = 0;
	if (g_sound_queue_count > 0) {
		do {
			sound_play_effect_now(
				g_sound_queue[queue_index].name,
				g_sound_queue[queue_index]
					.allow_restart_existing,
				g_sound_queue[queue_index].loop,
				g_sound_queue[queue_index].priority,
				g_sound_queue[queue_index].volume,
				g_sound_queue[queue_index].pan);
			++queue_index;
		} while (queue_index < g_sound_queue_count);
	}
	g_sound_queue_count = 0;
}

#ifndef XVT_MODERN
#pragma function(memcpy)
#endif
/* Queues a loaded effect for the next sound_flush_queued_effects, keeping
 * g_sound_queue ordered by priority, highest first, the new entry after those of
 * equal or higher priority. Returns 0 when g_direct_sound is NULL, the name is
 * empty or not loaded, or 4 entries wait, none of lower priority. Otherwise it
 * moves the later entries down one, writes the entry (the name by strncpy of 64
 * characters, not terminated for a name of 64 or more) and returns 1;
 * g_sound_queue_count grows by 1 but stops at 4, so an insert among 4 entries
 * drops the last. The modern build moves the entries with memmove, the original
 * with memcpy. */
// FUNCTION: XVT 0x42D120
int sound_queue_effect(const char *sound_name, int allow_restart_existing,
		       int loop, int priority, int volume, int pan)
{
	int queue_index;
	int queued_priority;
	int queue_count;
	const char *name;
	IDirectSound *direct_sound;
	struct sound_queue_entry *queue_entry;

	name = sound_name;
	direct_sound = g_direct_sound;
	if (direct_sound == NULL) {
		return 0;
	}
	if (*name == '\0') {
		return 0;
	}
	if (sound_find_loaded_effect_by_name(name) == -1) {
		return 0;
	}

	queue_index = 0;
	if (g_sound_queue_count > queue_index) {
		queued_priority = priority;
		do {
			if (g_sound_queue[queue_index].priority < priority) {
				break;
			}
			++queue_index;
		} while (g_sound_queue_count > queue_index);
	} else {
		queued_priority = priority;
	}
	if (g_sound_queue_count == queue_index && queue_index == 4) {
		return 0;
	}

	queue_entry = &g_sound_queue[queue_index];
#ifdef XVT_MODERN
	memmove(&g_sound_queue[queue_index + 1], queue_entry,
		sizeof(struct sound_queue_entry) *
			(g_sound_queue_count - queue_index));
#else
	memcpy(&g_sound_queue[queue_index + 1], queue_entry,
	       sizeof(struct sound_queue_entry) *
		       (g_sound_queue_count - queue_index));
#endif
	strncpy(queue_entry->name, name, sizeof(queue_entry->name));
	g_sound_queue[queue_index].loop = loop;
	g_sound_queue[queue_index].allow_restart_existing =
		allow_restart_existing;
	g_sound_queue[queue_index].priority = queued_priority;
	g_sound_queue[queue_index].volume = volume;
	g_sound_queue[queue_index].pan = pan;
	queue_count = g_sound_queue_count + 1;
	g_sound_queue_count = queue_count;
	if (queue_count > 4) {
		g_sound_queue_count = 4;
	}
	return 1;
}

/* Plays a loaded effect on a new duplicate of its buffer and records it in a
 * slot of g_active_sound_instances. Returns 0 when g_direct_sound is NULL or the
 * name is empty or not loaded. With all 8 slots in use it frees the first
 * slot whose buffer is neither playing nor looping, or whose GetStatus fails,
 * releasing the buffer. With all 8 still playing it takes the first slot
 * whose effect has the lowest current_priority under priority: when that slot
 * plays this effect it rewinds it to 0, gives it the next sequence and
 * returns 1, else it stops and releases its buffer and uses that slot. With
 * no slot under priority it returns 0, unless allow_restart_existing is set and
 * a slot plays this effect: then it rewinds the first such and gives it the
 * next sequence, returning 1. With fewer than 8 slots in use it takes the
 * first free one. It duplicates the effect's buffer, returning 0 when the
 * duplicate is NULL (the pointer is not cleared before the call), rewinds it,
 * sets its volume to 400 * (5 * v - 635) / 127 hundredths of a decibel and
 * its pan to 400 * (5 * p - 315) / 63, v and p being volume and pan clamped
 * to 0 to 127, and plays it, looping when loop is 1. When Play succeeds it
 * fills the slot (effect, buffer, the next sequence from
 * g_next_sound_instance_seq), adds 1 to g_active_sound_count and returns 1. When
 * Play returns DSERR_BUFFERLOST (0x88780096) it refills the effect's buffer
 * from its file with direct_sound_reload_wave_buffer and plays again, filling
 * the slot the same way on success; that path returns 0 whatever happens. Any
 * other failure of Play returns the HRESULT, a nonzero value. A duplicate
 * that fails to play is never released. */
// FUNCTION: XVT 0x42D230
int sound_play_effect_now(const char *sound_name, int allow_restart_existing,
			  int loop, int priority, int volume, int pan)
{
	int effect_index;
	int instance_index;
	int scan_index;
	int active_effect_index;
	int lowest_priority;
	int clamped_volume;
	int clamped_pan;
	uint32_t buffer_status;
	uint32_t play_flags;
	HRESULT result;
	struct active_sound_instance *instance;
	IDirectSoundBuffer *buffer;
	IDirectSoundBuffer *duplicate;

	if (g_direct_sound == NULL) {
		return 0;
	}
	if (*sound_name == '\0') {
		return 0;
	}
	effect_index = sound_find_loaded_effect_by_name(sound_name);
	if (effect_index == -1) {
		return 0;
	}

	if (g_active_sound_count == 8) {
		instance_index = 0;
		do {
			active_effect_index =
				g_active_sound_instances[instance_index]
					.effect_index;
			if (active_effect_index != -1) {
				buffer =
					g_active_sound_instances[instance_index]
						.buffer;
				if (buffer->lpVtbl->GetStatus(
					    buffer, &buffer_status) != 0) {
					break;
				}
				if ((buffer_status &
				     (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING)) ==
				    0) {
					break;
				}
			}
			++instance_index;
		} while (instance_index < 8);
		if (instance_index < 8) {
			g_active_sound_instances[instance_index]
				.buffer->lpVtbl->Release(
					g_active_sound_instances[instance_index]
						.buffer);
			g_active_sound_instances[instance_index].effect_index =
				-1;
			g_active_sound_instances[instance_index].buffer = NULL;
			g_active_sound_count = g_active_sound_count - 1;
		}

		if (instance_index == 8) {
			lowest_priority = priority;
			instance_index = 8;
			scan_index = 0;
			do {
				active_effect_index =
					g_active_sound_instances[scan_index]
						.effect_index;
				if (lowest_priority >
				    g_sound_defs[active_effect_index]
					    .current_priority) {
					instance_index = scan_index;
					lowest_priority =
						g_sound_defs[active_effect_index]
							.current_priority;
				}
				++scan_index;
			} while (scan_index < 8);

			if (instance_index != 8) {
				instance = &g_active_sound_instances
						   [instance_index];
				if (instance->effect_index == effect_index) {
					instance->buffer->lpVtbl
						->SetCurrentPosition(
							instance->buffer, 0);
					instance->sequence =
						g_next_sound_instance_seq++;
					return 1;
				}
				buffer = instance->buffer;
				buffer->lpVtbl->Stop(buffer);
				buffer->lpVtbl->Release(buffer);
				instance->effect_index = -1;
				instance->buffer = NULL;
				--g_active_sound_count;
			} else {
				if (allow_restart_existing != 0) {
					instance_index = 0;
					do {
						active_effect_index =
							g_active_sound_instances
								[instance_index]
									.effect_index;
						if (active_effect_index ==
						    effect_index) {
							instance =
								&g_active_sound_instances
									[instance_index];
							instance->buffer->lpVtbl
								->SetCurrentPosition(
									instance->buffer,
									0);
							instance->sequence =
								g_next_sound_instance_seq++;
							return 1;
						}
						++instance_index;
					} while (instance_index < 8);
				}
				return 0;
			}
		}
	} else {
		instance_index = 0;
		do {
			if (g_active_sound_instances[instance_index]
				    .effect_index == -1) {
				break;
			}
			++instance_index;
		} while (instance_index < 8);
	}

	g_direct_sound->lpVtbl->DuplicateSoundBuffer(
		g_direct_sound, g_sound_defs[effect_index].buffer, &duplicate);
	if (duplicate == NULL) {
		return 0;
	}
	duplicate->lpVtbl->SetCurrentPosition(duplicate, 0);
	clamped_volume = volume;
	if (clamped_volume > 127) {
		clamped_volume = 127;
	} else if (clamped_volume < 0) {
		clamped_volume = 0;
	}
	duplicate->lpVtbl->SetVolume(duplicate,
				     400 * (5 * clamped_volume - 635) / 127);
	clamped_pan = pan;
	if (clamped_pan > 127) {
		clamped_pan = 127;
	}
	if (clamped_pan < 0) {
		clamped_pan = 0;
	}
	duplicate->lpVtbl->SetPan(duplicate,
				  400 * (5 * clamped_pan - 315) / 63);
	play_flags = loop == 1;
	result = duplicate->lpVtbl->Play(duplicate, 0, 0, play_flags);
	if (result == (HRESULT)0x88780096) {
		result = direct_sound_reload_wave_buffer(
			g_sound_defs[effect_index].buffer,
			g_sound_defs[effect_index].file_name);
		if (result == 1) {
			duplicate->lpVtbl->SetCurrentPosition(duplicate, 0);
			result = duplicate->lpVtbl->Play(duplicate, 0, 0,
							 play_flags);
			if (result == 0) {
				buffer = duplicate;
				instance = &g_active_sound_instances
						   [instance_index];
				instance->effect_index = effect_index;
				instance->buffer = buffer;
				instance->sequence =
					g_next_sound_instance_seq++;
				++g_active_sound_count;
			} else {
				return 0;
			}
		}
	} else if (result == 0) {
		buffer = duplicate;
		instance = &g_active_sound_instances[instance_index];
		instance->effect_index = effect_index;
		instance->buffer = buffer;
		instance->sequence = g_next_sound_instance_seq++;
		++g_active_sound_count;
		return 1;
	}
	return result;
}

/* Stops the oldest sound of the named effect, the one with the lowest sequence:
 * stops and releases its buffer, frees the slot and lowers g_active_sound_count.
 * Returns 1 when Stop succeeded, else 0. Returns 0 with nothing stopped when
 * g_direct_sound is NULL, the name is empty or not loaded, no slot holds the
 * effect, or the slot's buffer is NULL. */
// FUNCTION: XVT 0x42D600
int sound_stop_oldest_instance(const char *name)
{
	int effect_index;
	int instance_index;
	int oldest_sequence;
	int oldest_index;
	HRESULT stop_result;
	IDirectSoundBuffer *buffer;

	if (g_direct_sound == NULL) {
		return 0;
	}
	if (*name == '\0') {
		return 0;
	}
	effect_index = sound_find_loaded_effect_by_name(name);
	if (effect_index == -1) {
		return 0;
	}
	oldest_sequence = (int)g_next_sound_instance_seq + 1;
	oldest_index = -1;
	for (instance_index = 0; instance_index < 8; ++instance_index) {
		if (g_active_sound_instances[instance_index].effect_index !=
		    effect_index) {
			continue;
		}
		if (oldest_sequence <=
		    (int)g_active_sound_instances[instance_index].sequence) {
			continue;
		}
		oldest_sequence =
			(int)g_active_sound_instances[instance_index].sequence;
		oldest_index = instance_index;
	}
	if (oldest_index == -1) {
		return 0;
	}
	buffer = g_active_sound_instances[oldest_index].buffer;
	if (buffer == NULL) {
		return 0;
	}
	stop_result = buffer->lpVtbl->Stop(buffer);
	buffer->lpVtbl->Release(buffer);
	g_active_sound_instances[oldest_index].buffer = NULL;
	g_active_sound_instances[oldest_index].effect_index = -1;
	--g_active_sound_count;
	return stop_result >= 0;
}

/* Calls sound_stop_oldest_instance for the effect of each slot in use, in slot
 * order, and returns 1 when every call returned 1, else 0. Each call stops that
 * effect's oldest sound, not necessarily that slot's, and a slot is never
 * looked at twice, so when an effect's older sound sits in a later slot, the
 * earlier slot keeps playing. */
// FUNCTION: XVT 0x42D6D0
int sound_stop_all_instances(void)
{
	int result;
	int instance_index;

	result = 1;
	for (instance_index = 0; instance_index < 8; ++instance_index) {
		if (g_active_sound_instances[instance_index].effect_index !=
		    -1) {
			result &= sound_stop_oldest_instance(
				g_sound_defs[g_active_sound_instances
						     [instance_index]
							     .effect_index]
					.name);
		}
	}
	return result;
}

/* Returns the primary buffer's volume on the game's scale,
 * 127 * millibels / 2000 + 127, or 0 when g_direct_sound is
 * NULL or GetVolume fails. Nothing calls this. */
// FUNCTION: XVT 0x42D770
int sound_get_primary_buffer_volume(void)
{
	int32_t volume_millibels;

	if (g_direct_sound == 0) {
		return 0;
	}
	if (g_sound_primary_buffer->lpVtbl->GetVolume(g_sound_primary_buffer,
						      &volume_millibels) != 0) {
		return 0;
	}
	/* From here volume_millibels holds the game's volume scale: 127 at full volume, 0 at -20 dB. */
	volume_millibels = 127 * volume_millibels / 2000 + 127;
	return volume_millibels;
}

/* Sets the volume of the named effect's newest sound, the slot with the highest
 * sequence, to 400 * (5 * v - 635) / 127 hundredths of a decibel, v being
 * volume clamped to 0 to 127. Returns 1 when SetVolume succeeds, else 0; 0 also
 * when g_direct_sound is NULL, the name is empty or not loaded, or no slot holds
 * the effect. Only sound_set_param calls it, for code 0x600. */
// FUNCTION: XVT 0x42D7C0
int sound_set_latest_instance_volume(const char *name, int volume)
{
	int effect_index;
	int newest_index;
	int instance_index;
	int newest_sequence;
	int clamped_volume;

	if (g_direct_sound == 0) {
		return 0;
	}
	if (name[0] == '\0') {
		return 0;
	}

	effect_index = sound_find_loaded_effect_by_name(name);
	if (effect_index == -1) {
		return 0;
	}

	newest_sequence = -1;
	instance_index = 0;
	newest_index = -1;
	do {
		if (g_active_sound_instances[instance_index].effect_index ==
			    effect_index &&
		    (int)g_active_sound_instances[instance_index].sequence >
			    newest_sequence) {
			newest_sequence =
				g_active_sound_instances[instance_index]
					.sequence;
			newest_index = instance_index;
		}
		++instance_index;
	} while (instance_index < 8);

	if (newest_index == -1) {
		return 0;
	}

	clamped_volume = volume;
	if (clamped_volume > 127) {
		clamped_volume = 127;
	} else if (clamped_volume < 0) {
		clamped_volume = 0;
	}

	/* From here clamped_volume holds the DirectSound attenuation, in hundredths of a decibel (-2000 to 0). */
	clamped_volume = 400 * (5 * clamped_volume - 635) / 127;
	return g_active_sound_instances[newest_index].buffer->lpVtbl->SetVolume(
		       g_active_sound_instances[newest_index].buffer,
		       clamped_volume) == 0;
}

/* Returns the volume of the named effect's newest sound on the game's scale,
 * 127 * millibels / 2000 + 127, or 0 when g_direct_sound is NULL, the name is
 * empty or not loaded, no slot holds it, or GetVolume fails. Nothing calls
 * this. */
// FUNCTION: XVT 0x42D880
int sound_get_latest_instance_volume(const char *name)
{
	int effect_index;
	int newest_index;
	int instance_index;
	int newest_sequence;
	int32_t volume_millibels;

	if (g_direct_sound == 0) {
		return 0;
	}
	if (name[0] == '\0') {
		return 0;
	}
	effect_index = sound_find_loaded_effect_by_name(name);
	if (effect_index == -1) {
		return 0;
	}
	newest_sequence = -1;
	instance_index = 0;
	newest_index = -1;
	do {
		if (g_active_sound_instances[instance_index].effect_index ==
			    effect_index &&
		    (int)g_active_sound_instances[instance_index].sequence >
			    newest_sequence) {
			newest_sequence =
				g_active_sound_instances[instance_index]
					.sequence;
			newest_index = instance_index;
		}
		++instance_index;
	} while (instance_index < 8);
	if (newest_index == -1) {
		return 0;
	}
	if (g_active_sound_instances[newest_index].buffer->lpVtbl->GetVolume(
		    g_active_sound_instances[newest_index].buffer,
		    &volume_millibels) != 0) {
		return 0;
	}
	/* From here volume_millibels holds the game's volume scale: 127 at full volume, 0 at -20 dB. */
	volume_millibels = 127 * volume_millibels / 2000 + 127;
	return volume_millibels;
}

/* Sets the pan of the named effect's newest sound to 400 * (5 * p - 315) / 63
 * hundredths of a decibel, p being pan clamped to 0 to 127: -2000 at 0, 0 at
 * 63. Returns 1 when SetPan succeeds, else 0; 0 also when g_direct_sound is
 * NULL, the name is empty or not loaded, or no slot holds the effect. Only
 * sound_set_param calls it, for code 0x700, which none of its callers passes. */
// FUNCTION: XVT 0x42D940
int sound_set_latest_instance_pan(const char *name, int pan)
{
	int effect_index;
	int newest_index;
	int instance_index;
	int newest_sequence;
	int clamped_pan;

	if (g_direct_sound == 0) {
		return 0;
	}
	if (name[0] == '\0') {
		return 0;
	}

	effect_index = sound_find_loaded_effect_by_name(name);
	if (effect_index == -1) {
		return 0;
	}

	newest_sequence = -1;
	instance_index = 0;
	newest_index = -1;
	do {
		if (g_active_sound_instances[instance_index].effect_index ==
			    effect_index &&
		    (int)g_active_sound_instances[instance_index].sequence >
			    newest_sequence) {
			newest_sequence =
				g_active_sound_instances[instance_index]
					.sequence;
			newest_index = instance_index;
		}
		++instance_index;
	} while (instance_index < 8);

	if (newest_index == -1) {
		return 0;
	}

	clamped_pan = pan;
	if (clamped_pan > 127) {
		clamped_pan = 127;
	}
	if (clamped_pan < 0) {
		clamped_pan = 0;
	}

	/* From here clamped_pan holds the DirectSound pan, in hundredths of a decibel (0 is center). */
	clamped_pan = 400 * (5 * clamped_pan - 315) / 63;
	return g_active_sound_instances[newest_index].buffer->lpVtbl->SetPan(
		       g_active_sound_instances[newest_index].buffer,
		       clamped_pan) == 0;
}

/* Returns the pan of the named effect's newest sound on the game's scale,
 * 63 * pan / 10000 + 63, or 0 when g_direct_sound is NULL, the name is empty or
 * not loaded, no slot holds it, or GetPan fails. Nothing calls this. */
// FUNCTION: XVT 0x42DA00
int sound_get_latest_instance_pan(const char *name)
{
	int effect_index;
	int newest_sequence;
	int instance_index;
	int newest_index;
	int32_t pan_millibels;

	if (g_direct_sound == 0) {
		return 0;
	}
	if (name[0] == '\0') {
		return 0;
	}
	effect_index = sound_find_loaded_effect_by_name(name);
	if (effect_index == -1) {
		return 0;
	}
	newest_sequence = -1;
	instance_index = 0;
	newest_index = -1;
	do {
		if (g_active_sound_instances[instance_index].effect_index ==
			    effect_index &&
		    (int)g_active_sound_instances[instance_index].sequence >
			    newest_sequence) {
			newest_sequence =
				g_active_sound_instances[instance_index]
					.sequence;
			newest_index = instance_index;
		}
		++instance_index;
	} while (instance_index < 8);
	if (newest_index == -1) {
		return 0;
	}
	if (g_active_sound_instances[newest_index].buffer->lpVtbl->GetPan(
		    g_active_sound_instances[newest_index].buffer,
		    &pan_millibels) != 0) {
		return 0;
	}
	/* From here pan_millibels holds the game's pan: 0 full left, 63 centered, 126 full right. */
	pan_millibels = 63 * pan_millibels / 10000 + 63;
	return pan_millibels;
}

/* Sets the playback frequency, in samples per second, of the named effect's
 * newest sound. Returns 1 when SetFrequency succeeds, else 0; 0 also when
 * g_direct_sound is NULL, the name is empty or not loaded, or no slot holds the
 * effect. Only sound_set_param calls it, for code 0x777. */
// FUNCTION: XVT 0x42DAC0
int sound_set_latest_instance_frequency(const char *name, uint32_t frequency)
{
	int effect_index;
	int newest_sequence;
	int instance_index;
	int newest_index;

	if (g_direct_sound == 0) {
		return 0;
	}
	if (name[0] == '\0') {
		return 0;
	}

	effect_index = sound_find_loaded_effect_by_name(name);
	if (effect_index == -1) {
		return 0;
	}

	newest_sequence = -1;
	instance_index = 0;
	newest_index = -1;
	do {
		if (g_active_sound_instances[instance_index].effect_index ==
			    effect_index &&
		    (int)g_active_sound_instances[instance_index].sequence >
			    newest_sequence) {
			newest_sequence =
				g_active_sound_instances[instance_index]
					.sequence;
			newest_index = instance_index;
		}
		++instance_index;
	} while (instance_index < 8);

	if (newest_index == -1) {
		return 0;
	}

	return g_active_sound_instances[newest_index]
		       .buffer->lpVtbl->SetFrequency(
			       g_active_sound_instances[newest_index].buffer,
			       frequency) == 0;
}

/* Sets the named effect's current_priority to priority clamped to 0 to 255 and
 * returns 1; returns 0 when the name is not loaded. Does not check
 * g_direct_sound. Only sound_set_param calls it, for code 0x500, which none of
 * its callers passes, so every effect keeps current_priority 0. */
// FUNCTION: XVT 0x42DB50
int sound_set_effect_current_priority(const char *name, int priority)
{
	int effect_index;
	int clamped_priority;

	effect_index = sound_find_loaded_effect_by_name(name);
	if (effect_index == -1) {
		return 0;
	}

	clamped_priority = priority;
	if (clamped_priority > 255) {
		clamped_priority = 255;
	}
	if (clamped_priority < 0) {
		clamped_priority = 0;
	}
	g_sound_defs[effect_index].current_priority = clamped_priority;
	return 1;
}

/* Returns the named effect's current_priority, or 0 when the name is not loaded.
 * Only sound_get_param calls it, for code 0x500, which none of its callers
 * passes. */
// FUNCTION: XVT 0x42DBA0
int sound_get_effect_current_priority(const char *name)
{
	int effect_index;

	effect_index = sound_find_loaded_effect_by_name(name);
	if (effect_index == -1) {
		return 0;
	}

	return g_sound_defs[effect_index].current_priority;
}

/* Counts the slots holding the named effect whose buffer reports the 0x1
 * (playing) or 0x4 (looping) status bit; returns the count, 0 to 8. Returns 0
 * when g_direct_sound is NULL or the name is empty or not loaded. Only
 * sound_get_param calls it, for code 0x100. */
// FUNCTION: XVT 0x42DBD0
int sound_count_playing_instances(const char *name)
{
	int effect_index;
	int playing_count;
	int instance_index;
	uint32_t status;

	if (g_direct_sound == 0) {
		return 0;
	}
	if (name[0] == '\0') {
		return 0;
	}

	effect_index = sound_find_loaded_effect_by_name(name);
	if (effect_index == -1) {
		return 0;
	}

	playing_count = 0;
	instance_index = 0;
	do {
		if (g_active_sound_instances[instance_index].effect_index ==
			    effect_index &&
		    g_active_sound_instances[instance_index]
				    .buffer->lpVtbl->GetStatus(
					    g_active_sound_instances
						    [instance_index]
							    .buffer,
					    &status) == 0 &&
		    ((status & 1) != 0 || (status & 4) != 0)) {
			++playing_count;
		}
		++instance_index;
	} while (instance_index < 8);

	return playing_count;
}

/* Inserts a copy of *effect into g_sound_defs, keeping it sorted by name
 * (strncmp over 64 characters) with the new entry after equal names, adds 1 to
 * g_sound_count and adds 1 to each instance slot's effect_index at or after the
 * insertion point. Does not check that the table has room; sound_load_effect_ex
 * checks g_sound_count under 1000 first. */
// FUNCTION: XVT 0x42DC60
void sound_insert_effect_def_sorted(const struct sound_effect_def *effect)
{
	int insert_index;
	struct sound_effect_def *current;
	const struct sound_effect_def *source_effect;
	int destination_index;
	int remaining;
	int instance_index;

	insert_index = 0;
	if (g_sound_count > 0) {
		current = g_sound_defs;
		source_effect = effect;
		do {
			if (strncmp(source_effect->name, current->name,
				    sizeof(source_effect->name)) < 0) {
				break;
			}
			++current;
			++insert_index;
		} while (g_sound_count > insert_index);
	} else {
		source_effect = effect;
	}
	if (g_sound_count > insert_index) {
		destination_index = g_sound_count;
		remaining = g_sound_count - insert_index;
		do {
			g_sound_defs[destination_index] =
				g_sound_defs[destination_index - 1];
			--destination_index;
			--remaining;
		} while (remaining != 0);
	}
	g_sound_defs[insert_index] = *source_effect;
	++g_sound_count;
	instance_index = 0;
	do {
		if (insert_index <=
		    g_active_sound_instances[instance_index].effect_index) {
			++g_active_sound_instances[instance_index].effect_index;
		}
		++instance_index;
	} while (instance_index < 8);
}

/* Removes entry effect_index from g_sound_defs, moving the later entries up one,
 * lowers g_sound_count and lowers by 1 each instance slot's effect_index above
 * it. Does nothing for an index outside 0 to g_sound_count - 1. Leaves the old
 * last entry in place, does not release the buffer and does not free slots that
 * play the entry; sound_unload_effect_by_name stops them first. */
// FUNCTION: XVT 0x42DD20
void sound_remove_effect_def(int effect_index)
{
	int current_index;

	if (effect_index < 0 || g_sound_count <= effect_index) {
		return;
	}

	current_index = effect_index;
	if (g_sound_count - 1 > effect_index) {
		struct sound_effect_def *current_effect;

		current_effect = &g_sound_defs[effect_index];
		do {
			*current_effect = current_effect[1];
			++current_effect;
			++current_index;
		} while (g_sound_count - 1 > current_index);
	}

	--g_sound_count;
	{
		int instance_index;

		for (instance_index = 0; instance_index < 8; ++instance_index) {
			if (g_active_sound_instances[instance_index]
				    .effect_index > effect_index) {
				--g_active_sound_instances[instance_index]
					  .effect_index;
			}
		}
	}
}

/* Returns the index of the named effect among the g_sound_count loaded entries
 * of g_sound_defs, or -1, by sound_find_effect_by_name. */
// FUNCTION: XVT 0x42DDA0
int sound_find_loaded_effect_by_name(const char *name)
{
	return sound_find_effect_by_name(g_sound_defs, g_sound_count - 1, name);
}

/* Binary search for name in records[0] to records[last_index], which must be
 * sorted by name, comparing up to 64 characters with strncmp. Returns the index
 * found, or -1. sound_find_loaded_effect_by_name is its only caller. */
// FUNCTION: XVT 0x42DDC0
int sound_find_effect_by_name(const struct sound_effect_def *records,
			      int last_index, const char *name)
{
	int search_last_index;
	int middle;
	int base_index;
	int comparison;
	const struct sound_effect_def *middle_effect;

	base_index = 0;
	search_last_index = last_index;
	while (1) {
		if (search_last_index < 0) {
			return -1;
		}

		middle = search_last_index >> 1;
		middle_effect = &records[middle];
		comparison = strncmp(middle_effect->name, name,
				     sizeof(middle_effect->name));
		if (comparison == 0) {
			return middle + base_index;
		}
		if (search_last_index <= 0) {
			return -1;
		}

		if (comparison < 0) {
			search_last_index -= middle + 1;
			base_index += middle + 1;
			records = middle_effect + 1;
		} else {
			search_last_index = middle - 1;
		}
	}
}

/* param_code selects what to set: 0x500 the effect's priority, 0x600 the latest instance's volume,
 * 0x700 its pan and 0x777 its frequency. Any other code, or a sound id outside 4..837, returns 0. */
/* Returns what the chosen function returns. The name comes from
 * g_fsfx_sfx_name_table[flight_sound_id]. Its callers pass only codes 0x600 (1536)
 * and 0x777 (1911). */
// FUNCTION: XVT 0x4A9180
int sound_set_param(int flight_sound_id, int param_code, int value)
{
	if (flight_sound_id < 4 || flight_sound_id > 837) {
		return 0;
	}
	switch (param_code) {
	case 0x500:
		return sound_set_effect_current_priority(
			g_fsfx_sfx_name_table[flight_sound_id], value);
	case 0x600:
		return sound_set_latest_instance_volume(
			g_fsfx_sfx_name_table[flight_sound_id], value);
	case 0x700:
		return sound_set_latest_instance_pan(
			g_fsfx_sfx_name_table[flight_sound_id], value);
	case 0x777:
		return sound_set_latest_instance_frequency(
			g_fsfx_sfx_name_table[flight_sound_id], value);
	default:
		return 0;
	}
}

/* param_code selects what to read: 0x100 the number of playing instances, 0x500 the effect's
 * priority. Any other code, or a sound id outside 4..837, returns 0. */
/* Returns what the chosen function returns. The name comes from
 * g_fsfx_sfx_name_table[flight_sound_id]. Its callers pass only code 0x100 (256). */
// FUNCTION: XVT 0x4A9300
int sound_get_param(int flight_sound_id, int param_code)
{
	if (flight_sound_id < 4 || flight_sound_id > 837) {
		return 0;
	}
	switch (param_code) {
	case 0x100:
		return sound_count_playing_instances(
			g_fsfx_sfx_name_table[flight_sound_id]);
	case 0x500:
		return sound_get_effect_current_priority(
			g_fsfx_sfx_name_table[flight_sound_id]);
	default:
		return 0;
	}
}

/* Returns 0 and ignores its arguments. Only flight_sync_unused_four_arg_forwarder
 * calls it, and nothing calls that. */
// FUNCTION: XVT 0x4A9370
int sound_unused_four_arg_stub(int arg1, int arg2, int arg3, int arg4)
{
	(void)arg1;
	(void)arg2;
	(void)arg3;
	(void)arg4;

	return 0;
}

/* Stops the oldest sound of flight sound id flight_sound_id, named by
 * g_fsfx_sfx_name_table, with sound_stop_oldest_instance and returns its result;
 * returns 0 for an id outside 4 to 837. */
// FUNCTION: XVT 0x4A9380
int sound_stop_oldest_instance_by_id(int flight_sound_id)
{
	if (flight_sound_id < 4 || flight_sound_id > 837) {
		return 0;
	}
	return sound_stop_oldest_instance(
		g_fsfx_sfx_name_table[flight_sound_id]);
}

/* Does nothing. flight_main_loop calls it in the original build and
 * xvt_flight_task_release_mission in the modern one, each after
 * sound_stop_all_instances. */
// FUNCTION: XVT 0x4A93B0
void sound_empty_stub(void) {}
