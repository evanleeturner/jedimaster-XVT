#include "xvt/audio/frontend_sound.h"

#include <stdio.h>
#include <string.h>

#include "aeron/compat/dsound.h"
#include "xvt/assets/file.h"
#include "xvt/audio/direct_sound.h"
#include "xvt/audio/sound.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_state.h"

struct frontend_sound_pcm_format {
	uint16_t format_tag;		   /* Wave format, 1 for PCM. */
	uint16_t channels;		   /* Channels, 2. */
	uint32_t samples_per_second;	   /* Sample rate, 11264. */
	uint32_t average_bytes_per_second; /* Bytes per second, 22528. */
	uint16_t block_align;		   /* Bytes per sample frame, 2. */
	uint16_t bits_per_sample;	   /* Bits per sample, 8. */
};

/* Starts the front end's DirectSound. Returns 1 at once when
 * g_front_state.frontend_direct_sound is set. Otherwise it clears the 12 voices,
 * frontend_active_voice_count, frontend_sound_buffer_count, frontend_sound_play_serial
 * and the buffer and name of the 128 buffer records, creates
 * frontend_direct_sound with DirectSoundCreate on the default device, asks for
 * frontend_primary_sound_buffer with a format of 11264 samples per second, 2
 * channels and 8 bits, without checking the result, and sets cooperative level
 * 1 (DSSCL_NORMAL) for hwnd. Returns 1, or 0 when DirectSoundCreate fails or,
 * after frontend_sound_shutdown_direct_sound, when setting the cooperative level
 * fails. frontend_display_init_main_window and frontend_display_reinit_surfaces call
 * it. */
// FUNCTION: XVT 0x4DE190
int frontend_sound_init_direct_sound(void *hwnd)
{
	if (g_front_state.frontend_direct_sound != NULL) {
		return 1;
	}
	for (int voice_index = 0; voice_index < 12; ++voice_index) {
		g_front_state.frontend_sound_voices[voice_index].buffer_index =
			-1;
		g_front_state.frontend_sound_voices[voice_index].play_serial =
			0;
		g_front_state.frontend_sound_voices[voice_index].buffer = NULL;
	}
	g_front_state.frontend_active_voice_count = 0;
	g_front_state.frontend_sound_buffer_count = 0;
	g_front_state.frontend_sound_play_serial = 0;
	for (int buffer_index = 0; buffer_index < 128; ++buffer_index) {
		g_front_state.frontend_sound_buffers[buffer_index].buffer =
			NULL;
		g_front_state.frontend_sound_buffers[buffer_index].name[0] =
			'\0';
	}
	if (DirectSoundCreate(NULL,
			      (void **)&g_front_state.frontend_direct_sound,
			      NULL) != 0) {
		return 0;
	}

	struct frontend_sound_pcm_format primary_format;
	memset(&primary_format, 0, sizeof(primary_format));
	primary_format.samples_per_second = 11264;
	primary_format.average_bytes_per_second = 22528;
	primary_format.format_tag = 1;
	primary_format.channels = 2;
	primary_format.block_align = 2;
	primary_format.bits_per_sample = 8;
	DSBUFFERDESC primary_buffer_desc;
	memset(&primary_buffer_desc, 0, sizeof(primary_buffer_desc));
	primary_buffer_desc.dwSize = sizeof(primary_buffer_desc);
	primary_buffer_desc.dwFlags = DSBCAPS_PRIMARYBUFFER;
	primary_buffer_desc.lpwfxFormat = (WAVEFORMATEX *)&primary_format;
	g_front_state.frontend_direct_sound->lpVtbl->CreateSoundBuffer(
		g_front_state.frontend_direct_sound, &primary_buffer_desc,
		&g_front_state.frontend_primary_sound_buffer, NULL);
	if (g_front_state.frontend_direct_sound->lpVtbl->SetCooperativeLevel(
		    g_front_state.frontend_direct_sound, hwnd, 1) != 0) {
		frontend_sound_shutdown_direct_sound();
		return 0;
	}
	return 1;
}

/* Returns 1 at once when g_front_state.frontend_direct_sound is NULL. Otherwise it
 * releases it and sets it to NULL, clears the buffer and name of the 128 buffer
 * records and the 12 voices, sets frontend_primary_sound_buffer to NULL without
 * releasing it and frontend_sound_play_serial to 0, and returns 1. It releases
 * none of the loaded buffers and leaves frontend_sound_buffer_count and
 * frontend_active_voice_count as they are. frontend_display_shutdown and
 * frontend_display_release_surfaces_for_flight call it, and in the original build
 * net_shutdown_direct_play_session_ex. */
// FUNCTION: XVT 0x4DE2E0
int frontend_sound_shutdown_direct_sound(void)
{
	if (g_front_state.frontend_direct_sound == NULL) {
		return 1;
	}

	g_front_state.frontend_direct_sound->lpVtbl->Release(
		g_front_state.frontend_direct_sound);
	g_front_state.frontend_direct_sound = NULL;
	for (int buffer_index = 0; buffer_index < 128; ++buffer_index) {
		g_front_state.frontend_sound_buffers[buffer_index].buffer =
			NULL;
		g_front_state.frontend_sound_buffers[buffer_index].name[0] =
			'\0';
	}
	for (int voice_index = 0; voice_index < 12; ++voice_index) {
		g_front_state.frontend_sound_voices[voice_index].buffer_index =
			-1;
		g_front_state.frontend_sound_voices[voice_index].play_serial =
			0;
		g_front_state.frontend_sound_voices[voice_index].buffer = NULL;
	}
	g_front_state.frontend_primary_sound_buffer = NULL;
	g_front_state.frontend_sound_play_serial = 0;
	return 1;
}

/* Calls frontend_sound_load_sound_file with omit_software_and_frequency_caps 0 and
 * returns its result. Only frontend_sound_load_list calls it. */
// FUNCTION: XVT 0x4DE370
int frontend_sound_load_sound(const char *file_name, const char *sound_name)
{
	return frontend_sound_load_sound_file(file_name, sound_name, 0);
}

/* Loads a WAV file as a named front-end sound: direct_sound_load_wave_buffer makes
 * its buffer, rewound to 0, and frontend_sound_insert_sorted_buffer adds the
 * record with the name (up to 63 characters), the file name (up to 191) and
 * priority 0. Returns 1, also when the name is already loaded; 0 when either
 * string is empty, 128 sounds are loaded, g_front_state.frontend_direct_sound is
 * NULL or the buffer fails to load. Unlocks the front end's back buffer around
 * its DirectSound calls and, when it was locked, locks it again into
 * g_draw_surface_ptr. Only frontend_sound_load_sound calls it, passing 0. */
// FUNCTION: XVT 0x4DE390
int frontend_sound_load_sound_file(const char *file_name,
				   const char *sound_name,
				   int omit_software_and_frequency_caps)
{
	if (*file_name == '\0') {
		return 0;
	}
	if (*sound_name == '\0') {
		return 0;
	}
	if (g_front_state.frontend_sound_buffer_count >= 128) {
		return 0;
	}
	if (g_front_state.frontend_direct_sound == NULL) {
		return 0;
	}
	if (frontend_sound_find_buffer_by_name(sound_name) != -1) {
		return 1;
	}

	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	struct frontend_sound_buffer_record record;
	record.buffer = direct_sound_load_wave_buffer(
		g_front_state.frontend_direct_sound, file_name,
		omit_software_and_frequency_caps);
	if (record.buffer != NULL) {
		record.buffer->lpVtbl->SetCurrentPosition(record.buffer, 0);
		strncpy(record.name, sound_name, sizeof(record.name));
		record.name[sizeof(record.name) - 1] = '\0';
		strncpy(record.file_name, file_name, sizeof(record.file_name));
		record.file_name[191] = '\0';
		record.priority = 0;
		frontend_sound_insert_sorted_buffer(&record);
		if (was_back_buffer_locked != 0) {
			g_draw_surface_ptr =
				frontend_display_lock_back_buffer();
		}
		return record.buffer != NULL;
	}
	if (was_back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return 0;
}

/* Calls frontend_sound_unload_buffer_by_name with the name in each of the 128
 * buffer records, from the last to the first; going down, it reaches every
 * loaded sound. frontend_display_release_surfaces_for_flight is its only caller. */
// FUNCTION: XVT 0x4DE4D0
void frontend_sound_unload_all_buffers(void)
{
	int buffer_index = 127;
	do {
		frontend_sound_unload_buffer_by_name(
			g_front_state.frontend_sound_buffers[buffer_index]
				.name);
	} while (--buffer_index >= 0);
}

/* Stops the named sound's voices with frontend_sound_stop_oldest_voice_by_name until
 * it returns other than 1, releases its buffer and removes its record with
 * frontend_sound_remove_buffer_record. Returns 1, or 0 when the name is empty or
 * not loaded. Unlocks the front end's back buffer around its DirectSound calls
 * and, when it was locked, locks it again into g_draw_surface_ptr.
 * frontend_sound_unload_all_buffers and frontend_sound_unload_list call it. */
// FUNCTION: XVT 0x4DE4F0
int frontend_sound_unload_buffer_by_name(const char *sound_name)
{
	if (*sound_name == '\0') {
		return 0;
	}
	int buffer_index = frontend_sound_find_buffer_by_name(sound_name);
	if (buffer_index == -1) {
		return 0;
	}

	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	while (frontend_sound_stop_oldest_voice_by_name(
		       g_front_state.frontend_sound_buffers[buffer_index]
			       .name) == 1) {
	}
	g_front_state.frontend_sound_buffers[buffer_index]
		.buffer->lpVtbl->Release(
			g_front_state.frontend_sound_buffers[buffer_index]
				.buffer);
	frontend_sound_remove_buffer_record(buffer_index);
	if (was_back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return 1;
}

/* Plays a loaded front-end sound on a new duplicate of its buffer and records
 * it in one of the 12 voices of g_front_state.frontend_sound_voices. Returns 0
 * when frontend_direct_sound is NULL or the name is empty or not loaded. With all
 * 12 voices in use it frees the first voice whose GetStatus fails or which is
 * neither playing (0x1) nor looping (0x4), releasing its buffer. With all 12
 * still playing it takes the first voice whose sound has the lowest priority
 * under priority, stops and releases its buffer and uses it; with none under
 * priority it returns 0, unless allow_restart_existing is set and a voice plays
 * this sound: then it rewinds the first such to 0, gives it the next play
 * serial and returns 1. With fewer than 12 in use it takes the first free
 * voice. It duplicates the sound's buffer, returning 0 when the duplicate is
 * NULL (the pointer is not cleared before the call), rewinds it, sets its
 * volume to 400 * (5 * v - 635) / 127 hundredths of a decibel and its pan to
 * 400 * (5 * p - 315) / 63, v and p being volume0_to127 and pan0_to127 clamped to
 * 0 to 127, and plays it, looping when loop is 1. When Play succeeds it fills
 * the voice (record index, buffer, the next serial from
 * frontend_sound_play_serial), adds 1 to frontend_active_voice_count and returns 1.
 * When Play returns DSERR_BUFFERLOST (0x88780096) it refills the sound's buffer
 * from its file with direct_sound_reload_wave_buffer and plays again, filling the
 * voice the same way on success; that path returns 0 whatever happens. Any
 * other failure of Play returns the HRESULT, a nonzero value, and the duplicate
 * is not released. Unlocks the front end's back buffer around its DirectSound
 * calls and, when it was locked, locks it again into g_draw_surface_ptr. */
// FUNCTION: XVT 0x4DE5A0
int frontend_sound_play_ui_sound(const char *sound_name,
				 int allow_restart_existing, int loop,
				 int priority, int volume0_to127,
				 int pan0_to127)
{
	if (g_front_state.frontend_direct_sound == NULL) {
		return 0;
	}
	if (*sound_name == '\0') {
		return 0;
	}
	int buffer_index = frontend_sound_find_buffer_by_name(sound_name);
	if (buffer_index == -1) {
		return 0;
	}

	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	int voice_index;
	uint32_t status;
	struct frontend_sound_voice *voice;
	if (g_front_state.frontend_active_voice_count == 12) {
		for (voice_index = 0; voice_index < 12; ++voice_index) {
			voice = &g_front_state
					 .frontend_sound_voices[voice_index];
			if (voice->buffer_index != -1 &&
			    (voice->buffer->lpVtbl->GetStatus(voice->buffer,
							      &status) != 0 ||
			     ((status & 1) == 0 && (status & 4) == 0))) {
				g_front_state.frontend_sound_voices[voice_index]
					.buffer->lpVtbl->Release(
						g_front_state
							.frontend_sound_voices
								[voice_index]
							.buffer);
				g_front_state.frontend_sound_voices[voice_index]
					.buffer_index = -1;
				g_front_state.frontend_sound_voices[voice_index]
					.buffer = NULL;
				--g_front_state.frontend_active_voice_count;
				break;
			}
		}
		if (voice_index == 12) {
			int candidate_voice_index = 12;
			int candidate_priority = priority;
			int scan_index = 0;
			do {
				int current_buffer_index =
					g_front_state
						.frontend_sound_voices
							[scan_index]
						.buffer_index;
				if (candidate_priority >
				    g_front_state
					    .frontend_sound_buffers
						    [current_buffer_index]
					    .priority) {
					candidate_voice_index = scan_index;
					candidate_priority =
						g_front_state
							.frontend_sound_buffers
								[current_buffer_index]
							.priority;
				}
				++scan_index;
			} while (scan_index < 12);

			voice_index = candidate_voice_index;
			if (candidate_voice_index != 12) {
				g_front_state.frontend_sound_voices[voice_index]
					.buffer->lpVtbl->Stop(
						g_front_state
							.frontend_sound_voices
								[voice_index]
							.buffer);
				g_front_state.frontend_sound_voices[voice_index]
					.buffer->lpVtbl->Release(
						g_front_state
							.frontend_sound_voices
								[voice_index]
							.buffer);
				g_front_state.frontend_sound_voices[voice_index]
					.buffer_index = -1;
				g_front_state.frontend_sound_voices[voice_index]
					.buffer = NULL;
				--g_front_state.frontend_active_voice_count;
			} else {
				if (allow_restart_existing != 0) {
					for (int restart_voice_index = 0;
					     restart_voice_index < 12;
					     ++restart_voice_index) {
						if (g_front_state
							    .frontend_sound_voices
								    [restart_voice_index]
							    .buffer_index ==
						    buffer_index) {
							g_front_state
								.frontend_sound_voices
									[restart_voice_index]
								.buffer->lpVtbl
								->SetCurrentPosition(
									g_front_state
										.frontend_sound_voices
											[restart_voice_index]
										.buffer,
									0);
							g_front_state
								.frontend_sound_voices
									[restart_voice_index]
								.play_serial =
								g_front_state
									.frontend_sound_play_serial++;
							if (was_back_buffer_locked !=
							    0) {
								g_draw_surface_ptr =
									frontend_display_lock_back_buffer();
							}
							return 1;
						}
					}
				}
				if (was_back_buffer_locked != 0) {
					g_draw_surface_ptr =
						frontend_display_lock_back_buffer();
				}
				return 0;
			}
		}
	} else {
		voice_index = 0;
		voice = g_front_state.frontend_sound_voices;
		while (voice_index < 12 && voice->buffer_index != -1) {
			++voice;
			++voice_index;
		}
	}

	IDirectSoundBuffer *duplicate_buffer;
	g_front_state.frontend_direct_sound->lpVtbl->DuplicateSoundBuffer(
		g_front_state.frontend_direct_sound,
		g_front_state.frontend_sound_buffers[buffer_index].buffer,
		&duplicate_buffer);
	if (duplicate_buffer == NULL) {
		if (was_back_buffer_locked != 0) {
			g_draw_surface_ptr =
				frontend_display_lock_back_buffer();
		}
		return 0;
	}

	duplicate_buffer->lpVtbl->SetCurrentPosition(duplicate_buffer, 0);
	int clamped_value = volume0_to127;
	if (clamped_value > 127) {
		clamped_value = 127;
	} else if (clamped_value < 0) {
		clamped_value = 0;
	}
	duplicate_buffer->lpVtbl->SetVolume(
		duplicate_buffer, 400 * (5 * clamped_value - 635) / 127);
	clamped_value = pan0_to127;
	if (clamped_value > 127) {
		clamped_value = 127;
	}
	if (clamped_value < 0) {
		clamped_value = 0;
	}
	duplicate_buffer->lpVtbl->SetPan(duplicate_buffer,
					 400 * (5 * clamped_value - 315) / 63);
	HRESULT play_result = duplicate_buffer->lpVtbl->Play(duplicate_buffer,
							     0, 0, loop == 1);
	int result = play_result;
	if (play_result == (HRESULT)0x88780096u) {
		result = direct_sound_reload_wave_buffer(
			g_front_state.frontend_sound_buffers[buffer_index]
				.buffer,
			g_front_state.frontend_sound_buffers[buffer_index]
				.file_name);
		if (result == 1) {
			duplicate_buffer->lpVtbl->SetCurrentPosition(
				duplicate_buffer, 0);
			result = duplicate_buffer->lpVtbl->Play(
				duplicate_buffer, 0, 0, loop == 1);
			if (result == 0) {
				g_front_state.frontend_sound_voices[voice_index]
					.buffer_index = buffer_index;
				g_front_state.frontend_sound_voices[voice_index]
					.buffer = duplicate_buffer;
				g_front_state.frontend_sound_voices[voice_index]
					.play_serial =
					g_front_state
						.frontend_sound_play_serial++;
				++g_front_state.frontend_active_voice_count;
			} else {
				result = 0;
			}
		}
	} else if (play_result == 0) {
		g_front_state.frontend_sound_voices[voice_index].buffer_index =
			buffer_index;
		g_front_state.frontend_sound_voices[voice_index].buffer =
			duplicate_buffer;
		g_front_state.frontend_sound_voices[voice_index].play_serial =
			g_front_state.frontend_sound_play_serial++;
		++g_front_state.frontend_active_voice_count;
		result = 1;
	}
	if (was_back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return result;
}

/* Stops the oldest voice of the named sound, the one with the lowest play
 * serial: stops and releases its buffer, frees the voice and lowers
 * g_front_state.frontend_active_voice_count. Returns 1 when Stop succeeded, else 0;
 * returns 0 with nothing stopped when frontend_direct_sound is NULL, the name is
 * empty or not loaded, no voice holds the sound, or the voice's buffer is NULL.
 * Unlocks the front end's back buffer around its DirectSound calls and, when it
 * was locked, locks it again into g_draw_surface_ptr.
 * frontend_sound_unload_buffer_by_name and frontend_sound_stop_all_voices call it. */
// FUNCTION: XVT 0x4DE9A0
int frontend_sound_stop_oldest_voice_by_name(const char *name)
{
	if (g_front_state.frontend_direct_sound == NULL) {
		return 0;
	}
	if (*name == '\0') {
		return 0;
	}
	int buffer_index = frontend_sound_find_buffer_by_name(name);
	if (buffer_index == -1) {
		return 0;
	}

	int oldest_voice_index = -1;
	int oldest_serial = g_front_state.frontend_sound_play_serial + 1;
	int voice_index = 0;
	struct frontend_sound_voice *voice =
		g_front_state.frontend_sound_voices;
	do {
		if (voice->buffer_index == buffer_index &&
		    voice->play_serial < oldest_serial) {
			oldest_serial = voice->play_serial;
			oldest_voice_index = voice_index;
		}
		++voice;
		++voice_index;
	} while (voice_index < 12);
	if (oldest_voice_index == -1) {
		return 0;
	}

	IDirectSoundBuffer *buffer =
		g_front_state.frontend_sound_voices[oldest_voice_index].buffer;
	if (buffer == NULL) {
		return 0;
	}
	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	HRESULT stop_result = buffer->lpVtbl->Stop(buffer);
	buffer->lpVtbl->Release(buffer);
	g_front_state.frontend_sound_voices[oldest_voice_index].buffer = NULL;
	g_front_state.frontend_sound_voices[oldest_voice_index].buffer_index =
		-1;
	--g_front_state.frontend_active_voice_count;
	if (was_back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return stop_result >= 0;
}

/* Calls frontend_sound_stop_oldest_voice_by_name for the sound of each voice in use,
 * in voice order, and returns 1 when every call returned 1, else 0. Each call
 * stops that sound's oldest voice, not necessarily that voice, and no voice is
 * looked at twice, so when a sound's older voice sits later, the earlier voice
 * keeps playing. Nothing calls this. */
// FUNCTION: XVT 0x4DEA90
int frontend_sound_stop_all_voices(void)
{
	int all_stopped = 1;
	for (int voice_index = 0; voice_index < 12; ++voice_index) {
		int buffer_index =
			g_front_state.frontend_sound_voices[voice_index]
				.buffer_index;
		if (buffer_index != -1) {
			all_stopped &= frontend_sound_stop_oldest_voice_by_name(
				g_front_state
					.frontend_sound_buffers[buffer_index]
					.name);
		}
	}
	return all_stopped;
}

/* Sets the primary buffer's volume to 400 * (5 * v - 635) / 127 hundredths of a
 * decibel, v being volume0_to127 clamped to 0 to 127. Returns 1 when SetVolume
 * succeeds; 0 when it fails, when there is no primary buffer, or when
 * g_front_state.frontend_direct_sound is NULL. Unlocks the front end's back buffer
 * around its DirectSound calls and, when it was locked, locks it again into
 * g_draw_surface_ptr. Nothing calls this. */
// FUNCTION: XVT 0x4DEAD0
int frontend_sound_set_primary_volume(int volume0_to127)
{
	if (g_front_state.frontend_direct_sound == NULL) {
		return 0;
	}

	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	HRESULT set_result = 1;
	frontend_display_unlock_back_buffer();
	if (g_front_state.frontend_primary_sound_buffer != NULL) {
		int clamped_volume = volume0_to127;
		if (clamped_volume > 127) {
			clamped_volume = 127;
		} else if (clamped_volume < 0) {
			clamped_volume = 0;
		}
		set_result =
			g_front_state.frontend_primary_sound_buffer->lpVtbl
				->SetVolume(
					g_front_state
						.frontend_primary_sound_buffer,
					400 * (5 * clamped_volume - 635) / 127);
	}
	if (was_back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return set_result == 0;
}

/* Returns the primary buffer's volume on the game's scale,
 * 127 * v / 2000 + 127, v in hundredths of a decibel; 0 when
 * g_front_state.frontend_direct_sound is NULL or GetVolume fails,
 * and on that failure it leaves the back buffer unlocked. Does
 * not check that the primary buffer exists. Otherwise it locks
 * the back buffer again into g_draw_surface_ptr when it was locked.
 * Nothing calls this. */
// FUNCTION: XVT 0x4DEB50
int frontend_sound_get_primary_volume(void)
{
	if (g_front_state.frontend_direct_sound == NULL) {
		return 0;
	}

	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	int32_t direct_sound_volume;
	if (g_front_state.frontend_primary_sound_buffer->lpVtbl->GetVolume(
		    g_front_state.frontend_primary_sound_buffer,
		    &direct_sound_volume) != 0) {
		return 0;
	}

	/* Convert the DirectSound attenuation back to the game's volume scale, where 127 is full volume. */
	direct_sound_volume = 127 * direct_sound_volume / 2000 + 127;
	if (was_back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return direct_sound_volume;
}

/* Sets the volume of the named sound's newest voice, the one with the highest
 * play serial, to 400 * (5 * v - 635) / 127 hundredths of a decibel, v being
 * volume0_to127 clamped to 0 to 127. Returns 1 when SetVolume succeeds, else 0;
 * 0 also when frontend_direct_sound is NULL, the name is empty or not loaded, or
 * no voice holds it. Unlocks the front end's back buffer around its DirectSound
 * calls and, when it was locked, locks it again into g_draw_surface_ptr. Nothing
 * calls this. */
// FUNCTION: XVT 0x4DEBC0
int frontend_sound_set_newest_voice_volume_by_name(const char *name,
						   int volume0_to127)
{
	if (g_front_state.frontend_direct_sound == NULL) {
		return 0;
	}
	if (*name == '\0') {
		return 0;
	}
	int buffer_index = frontend_sound_find_buffer_by_name(name);
	if (buffer_index == -1) {
		return 0;
	}

	int newest_serial = -1;
	int voice_index = 0;
	int newest_voice_index = -1;
	struct frontend_sound_voice *voice =
		g_front_state.frontend_sound_voices;
	do {
		if (voice->buffer_index == buffer_index &&
		    newest_serial < voice->play_serial) {
			newest_serial = voice->play_serial;
			newest_voice_index = voice_index;
		}
		voice++;
		voice_index++;
	} while (voice_index < 12);
	if (newest_voice_index == -1) {
		return 0;
	}

	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	int clamped_volume = volume0_to127;
	if (clamped_volume > 127) {
		clamped_volume = 127;
	} else if (clamped_volume < 0) {
		clamped_volume = 0;
	}
	int direct_sound_volume = 400 * (5 * clamped_volume - 635) / 127;
	HRESULT set_result =
		g_front_state.frontend_sound_voices[newest_voice_index]
			.buffer->lpVtbl->SetVolume(
				g_front_state
					.frontend_sound_voices
						[newest_voice_index]
					.buffer,
				direct_sound_volume);
	if (was_back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return set_result == 0;
}

/* Returns the volume of the named sound's newest voice on the game's scale,
 * 127 * v / 2000 + 127; 0 when frontend_direct_sound is NULL, the name is empty
 * or not loaded, no voice holds it, or GetVolume fails, and on that failure it
 * leaves the back buffer unlocked. Otherwise it locks the back buffer again
 * into g_draw_surface_ptr when it was locked. Nothing calls this. */
// FUNCTION: XVT 0x4DECA0
int frontend_sound_get_newest_voice_volume_by_name(const char *name)
{
	if (g_front_state.frontend_direct_sound == NULL) {
		return 0;
	}
	if (*name == '\0') {
		return 0;
	}
	int buffer_index = frontend_sound_find_buffer_by_name(name);
	if (buffer_index == -1) {
		return 0;
	}

	int newest_serial = -1;
	int voice_index = 0;
	int newest_voice_index = -1;
	struct frontend_sound_voice *voice =
		g_front_state.frontend_sound_voices;
	do {
		if (voice->buffer_index == buffer_index &&
		    newest_serial < voice->play_serial) {
			newest_serial = voice->play_serial;
			newest_voice_index = voice_index;
		}
		voice++;
		voice_index++;
	} while (voice_index < 12);
	if (newest_voice_index == -1) {
		return 0;
	}

	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	int32_t direct_sound_volume;
	if (g_front_state.frontend_sound_voices[newest_voice_index]
		    .buffer->lpVtbl->GetVolume(
			    g_front_state
				    .frontend_sound_voices[newest_voice_index]
				    .buffer,
			    &direct_sound_volume) != 0) {
		return 0;
	}
	/* Convert the DirectSound attenuation back to the game's volume scale, where 127 is full volume. */
	direct_sound_volume = 127 * direct_sound_volume / 2000 + 127;
	if (was_back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return direct_sound_volume;
}

/* Sets the pan of the named sound's newest voice to 400 * (5 * p - 315) / 63
 * hundredths of a decibel, p being pan0_to127 clamped to 0 to 127. Returns 1
 * when SetPan succeeds, else 0; 0 also when frontend_direct_sound is NULL, the
 * name is empty or not loaded, or no voice holds it. Unlocks the front end's
 * back buffer around its DirectSound calls and, when it was locked, locks it
 * again into g_draw_surface_ptr. Nothing calls this. */
// FUNCTION: XVT 0x4DED80
int frontend_sound_set_newest_voice_pan_by_name(const char *name,
						int pan0_to127)
{
	if (g_front_state.frontend_direct_sound == NULL) {
		return 0;
	}
	if (*name == '\0') {
		return 0;
	}
	int buffer_index = frontend_sound_find_buffer_by_name(name);
	if (buffer_index == -1) {
		return 0;
	}

	int newest_serial = -1;
	int voice_index = 0;
	int newest_voice_index = -1;
	do {
		if (g_front_state.frontend_sound_voices[voice_index]
				    .buffer_index == buffer_index &&
		    g_front_state.frontend_sound_voices[voice_index]
				    .play_serial > newest_serial) {
			newest_serial =
				g_front_state.frontend_sound_voices[voice_index]
					.play_serial;
			newest_voice_index = voice_index;
		}
		voice_index++;
	} while (voice_index < 12);
	if (newest_voice_index == -1) {
		return 0;
	}

	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	int clamped_pan = pan0_to127;
	if (clamped_pan > 127) {
		clamped_pan = 127;
	}
	if (clamped_pan < 0) {
		clamped_pan = 0;
	}
	int direct_sound_pan = 400 * (5 * clamped_pan - 315) / 63;
	HRESULT set_result =
		g_front_state.frontend_sound_voices[newest_voice_index]
			.buffer->lpVtbl->SetPan(
				g_front_state
					.frontend_sound_voices
						[newest_voice_index]
					.buffer,
				direct_sound_pan);
	if (was_back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return set_result == 0;
}

/* Returns the pan of the named sound's newest voice on the game's scale,
 * 63 * pan / 10000 + 63; 0 when frontend_direct_sound is NULL, the name is empty
 * or not loaded, no voice holds it, or GetPan fails. Unlocks the front end's
 * back buffer around its DirectSound calls and, when it was locked, locks it
 * again into g_draw_surface_ptr. Nothing calls this. */
// FUNCTION: XVT 0x4DEE60
int frontend_sound_get_newest_voice_pan_by_name(const char *name)
{
	if (g_front_state.frontend_direct_sound == NULL) {
		return 0;
	}
	if (*name == '\0') {
		return 0;
	}
	int buffer_index = frontend_sound_find_buffer_by_name(name);
	if (buffer_index == -1) {
		return 0;
	}

	int newest_serial = -1;
	int voice_index = 0;
	int newest_voice_index = -1;
	struct frontend_sound_voice *voice =
		g_front_state.frontend_sound_voices;
	do {
		if (voice->buffer_index == buffer_index &&
		    voice->play_serial > newest_serial) {
			newest_serial = voice->play_serial;
			newest_voice_index = voice_index;
		}
		++voice;
		++voice_index;
	} while (voice_index < 12);
	if (newest_voice_index == -1) {
		return 0;
	}

	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	frontend_display_unlock_back_buffer();
	int32_t direct_sound_pan;
	if (g_front_state.frontend_sound_voices[newest_voice_index]
		    .buffer->lpVtbl->GetPan(
			    g_front_state
				    .frontend_sound_voices[newest_voice_index]
				    .buffer,
			    &direct_sound_pan) != 0) {
		if (was_back_buffer_locked != 0) {
			g_draw_surface_ptr =
				frontend_display_lock_back_buffer();
		}
		return 0;
	}
	/* Convert the DirectSound pan back to the game's pan scale, where 63 is center. */
	direct_sound_pan = 63 * direct_sound_pan / 10000 + 63;
	if (was_back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return direct_sound_pan;
}

/* Sets the named sound's priority to priority0_to255 clamped to 0 to 255 and
 * returns 1; returns 0 when the name is not loaded. Does not check
 * frontend_direct_sound. Nothing calls this, so every sound keeps priority 0. */
// FUNCTION: XVT 0x4DEF50
int frontend_sound_set_buffer_priority_by_name(const char *name,
					       int priority0_to255)
{
	int buffer_index = frontend_sound_find_buffer_by_name(name);
	if (buffer_index == -1) {
		return 0;
	}

	int clamped_priority = priority0_to255;
	if (clamped_priority > 255) {
		clamped_priority = 255;
	}
	if (clamped_priority < 0) {
		clamped_priority = 0;
	}
	g_front_state.frontend_sound_buffers[buffer_index].priority =
		clamped_priority;
	return 1;
}

/* Returns the named sound's priority, or 0 when the name is not loaded. Nothing
 * calls this. */
// FUNCTION: XVT 0x4DEFA0
int frontend_sound_get_buffer_priority_by_name(const char *name)
{
	int buffer_index = frontend_sound_find_buffer_by_name(name);
	if (buffer_index == -1) {
		return 0;
	}

	return g_front_state.frontend_sound_buffers[buffer_index].priority;
}

/* Counts the voices holding the named sound whose buffer reports the 0x1
 * (playing) or 0x4 (looping) status bit; returns the count, 0 to 12, or 0 when
 * frontend_direct_sound is NULL or the name is empty or not loaded. Unlocks the
 * front end's back buffer around its DirectSound calls and, when it was locked,
 * locks it again into g_draw_surface_ptr. Nothing calls this. */
// FUNCTION: XVT 0x4DEFD0
int frontend_sound_get_playing_count(const char *name)
{
	if (g_front_state.frontend_direct_sound == NULL) {
		return 0;
	}
	if (name[0] == '\0') {
		return 0;
	}

	int buffer_index = frontend_sound_find_buffer_by_name(name);
	if (buffer_index == -1) {
		return 0;
	}

	int voice_index = 0;
	int was_back_buffer_locked = g_front_state.back_buffer_locked;
	int playing_count = 0;
	frontend_display_unlock_back_buffer();
	uint32_t status;
	do {
		if (g_front_state.frontend_sound_voices[voice_index]
				    .buffer_index == buffer_index &&
		    g_front_state.frontend_sound_voices[voice_index]
				    .buffer->lpVtbl->GetStatus(
					    g_front_state
						    .frontend_sound_voices
							    [voice_index]
						    .buffer,
					    &status) == 0 &&
		    ((status & 1) != 0 || (status & 4) != 0)) {
			++playing_count;
		}
		++voice_index;
	} while (voice_index < 12);

	if (was_back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
	return playing_count;
}

/* Inserts a copy of *record into g_front_state.frontend_sound_buffers, keeping it
 * sorted by name (strncmp over 64 characters) with the new record after equal
 * names, adds 1 to frontend_sound_buffer_count and adds 1 to each voice's
 * bufferIndex at or after the insertion point. Does not check that the table
 * has room; frontend_sound_load_sound_file checks the count under 128 first. */
// FUNCTION: XVT 0x4DF070
void frontend_sound_insert_sorted_buffer(
	const struct frontend_sound_buffer_record *record)
{
	int insertion_index = 0;
	if (g_front_state.frontend_sound_buffer_count > 0) {
		do {
			if (strncmp(record->name,
				    g_front_state
					    .frontend_sound_buffers
						    [insertion_index]
					    .name,
				    64) < 0) {
				break;
			}
			++insertion_index;
		} while (g_front_state.frontend_sound_buffer_count >
			 insertion_index);
	}
	if (g_front_state.frontend_sound_buffer_count > insertion_index) {
		int shift_index = g_front_state.frontend_sound_buffer_count;
		int remaining = g_front_state.frontend_sound_buffer_count -
				insertion_index;
		do {
			struct frontend_sound_buffer_record *destination =
				&g_front_state
					 .frontend_sound_buffers[shift_index];
			--shift_index;
			memcpy(destination,
			       &g_front_state
					.frontend_sound_buffers[shift_index],
			       sizeof(struct frontend_sound_buffer_record));
			--remaining;
		} while (remaining != 0);
	}
	memcpy(&g_front_state.frontend_sound_buffers[insertion_index], record,
	       sizeof(struct frontend_sound_buffer_record));
	++g_front_state.frontend_sound_buffer_count;
	int voice_index = 0;
	do {
		int buffer_index =
			g_front_state.frontend_sound_voices[voice_index]
				.buffer_index;
		if (insertion_index <= buffer_index) {
			g_front_state.frontend_sound_voices[voice_index]
				.buffer_index = buffer_index + 1;
		}
		++voice_index;
	} while (voice_index < 12);
}

/* Removes record bufferIndex from g_front_state.frontend_sound_buffers, moving the
 * later records up one, lowers frontend_sound_buffer_count and lowers by 1 each
 * voice's bufferIndex above it. Does nothing for an index outside 0 to
 * frontend_sound_buffer_count - 1. Leaves the old last record in place, does not
 * release the buffer and does not free voices that play it;
 * frontend_sound_unload_buffer_by_name stops them first. */
// FUNCTION: XVT 0x4DF130
void frontend_sound_remove_buffer_record(int buffer_index)
{
	if (buffer_index < 0 ||
	    buffer_index >= g_front_state.frontend_sound_buffer_count) {
		return;
	}

	int index;
	for (index = buffer_index;
	     index < g_front_state.frontend_sound_buffer_count - 1; ++index) {
		g_front_state.frontend_sound_buffers[index] =
			g_front_state.frontend_sound_buffers[index + 1];
	}

	--g_front_state.frontend_sound_buffer_count;
	for (index = 0; index < 12; ++index) {
		if (g_front_state.frontend_sound_voices[index].buffer_index >
		    buffer_index) {
			--g_front_state.frontend_sound_voices[index]
				  .buffer_index;
		}
	}
}

/* Returns the index of the named sound among the frontend_sound_buffer_count
 * loaded records, or -1, by frontend_sound_binary_search_buffer_by_name. */
// FUNCTION: XVT 0x4DF1B0
int frontend_sound_find_buffer_by_name(const char *name)
{
	return frontend_sound_binary_search_buffer_by_name(
		g_front_state.frontend_sound_buffers,
		g_front_state.frontend_sound_buffer_count - 1, name);
}

/* Binary search for name in records[0] to records[last_index], which must be
 * sorted by name, comparing up to 64 characters with strncmp. Returns the index
 * found, or -1. frontend_sound_find_buffer_by_name is its only caller. */
// FUNCTION: XVT 0x4DF1D0
int frontend_sound_binary_search_buffer_by_name(
	const struct frontend_sound_buffer_record *records, int last_index,
	const char *name)
{
	int base_index = 0;
	int search_last_index = last_index;
	while (1) {
		if (search_last_index < 0) {
			return -1;
		}
		int middle = search_last_index >> 1;
		const struct frontend_sound_buffer_record *middle_record =
			&records[middle];
		int comparison = strncmp(middle_record->name, name,
					 sizeof(middle_record->name));
		if (comparison == 0) {
			return middle + base_index;
		}
		if (search_last_index <= 0) {
			return -1;
		}
		if (comparison < 0) {
			base_index += middle + 1;
			search_last_index -= middle + 1;
			records = middle_record + 1;
		} else {
			search_last_index = middle - 1;
		}
	}
}

/* Loads the front end's sounds from a list file: skips the first line, read
 * with FILE_GETS into 255 bytes, then reads pairs of words, a WAV file and a
 * sound name, and loads each with frontend_sound_load_sound, ignoring its result.
 * Returns 1 at the end of the file, also when the first line cannot be read; 0
 * when the file does not open or a line does not hold two words. The original
 * build reads each word with %s into a 256-byte buffer without a limit; the
 * modern build stops at 255 characters. Every caller passes the front end's
 * sound list file. */
// FUNCTION: XVT 0x4DF700
int frontend_sound_load_list(const char *file_name)
{
	xvt_file *stream = file_open(file_name, "r");
	if (stream == NULL) {
		return 0;
	}
	char sound_file_name[256];
	if (FILE_GETS(sound_file_name, 255, stream) == NULL) {
		file_close(stream);
		return 1;
	}
	int field_count;
	char sound_name[256];
	while (1) {
#ifdef XVT_MODERN
		field_count = FILE_SCANF(stream, "%255s %255s\n",
					 sound_file_name, sound_name);
#else
		field_count = FILE_SCANF(stream, "%s %s\n", sound_file_name,
					 sound_name);
#endif
		if (field_count == EOF) {
			file_close(stream);
			return 1;
		}
		if (field_count != 2) {
			file_close(stream);
			return 0;
		}
		frontend_sound_load_sound(sound_file_name, sound_name);
	}
}

/* Reads a list file in frontend_sound_load_list's form and unloads each named
 * sound with frontend_sound_unload_buffer_by_name. Returns as
 * frontend_sound_load_list does. Nothing calls this. */
// FUNCTION: XVT 0x4DF7C0
int frontend_sound_unload_list(const char *file_name)
{
	xvt_file *stream = file_open(file_name, "r");
	if (stream == NULL) {
		return 0;
	}
	char ignored_file_name[256];
	if (FILE_GETS(ignored_file_name, 255, stream) == NULL) {
		file_close(stream);
		return 1;
	}
	int field_count;
	char sound_name[256];
	while (1) {
#ifdef XVT_MODERN
		field_count = FILE_SCANF(stream, "%255s %255s\n",
					 ignored_file_name, sound_name);
#else
		field_count = FILE_SCANF(stream, "%s %s\n", ignored_file_name,
					 sound_name);
#endif
		if (field_count == EOF) {
			file_close(stream);
			return 1;
		}
		if (field_count != 2) {
			file_close(stream);
			return 0;
		}
		frontend_sound_unload_buffer_by_name(sound_name);
	}
}
