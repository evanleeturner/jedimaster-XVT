#include "xvt/audio/direct_sound.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/audio/sound.h"
#include "xvt/flight/fediskio.h"

#ifndef XVT_MODERN
__declspec(dllimport) void *__stdcall LocalAlloc(unsigned int flags,
						 size_t bytes);
__declspec(dllimport) void *__stdcall LocalFree(void *memory);
#endif

/* Heap copy of the WAV file direct_sound_load_file_and_find_audio_data last read; the
 * format and sample pointers it returns point into it.
 * direct_sound_load_wave_buffer and direct_sound_reload_wave_buffer set it to NULL
 * before reading and free it after, without setting it back to NULL. */
// GLOBAL: XVT 0x5569D4
static void *g_wave_file_data_buffer = NULL;

/* Loads a WAV file into a new static DirectSound buffer: reads it with
 * direct_sound_load_file_and_find_audio_data, creates a buffer of the file's format
 * and data size with flags 194, or 234 when omit_software_and_frequency_caps is 0
 * (see the comment inside), and copies the samples in. Returns the buffer, or
 * NULL when the file fails to load or parse, the buffer cannot be created, or
 * the copy fails, releasing the buffer then. Sets g_wave_file_data_buffer to NULL
 * first and frees the file data at the end. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x44B650
IDirectSoundBuffer *
direct_sound_load_wave_buffer(IDirectSound *direct_sound, const char *file_name,
			      int omit_software_and_frequency_caps)
{
	IDirectSoundBuffer *buffer = NULL;
	const void *sample_data;
	DSBUFFERDESC desc = {0};

	g_wave_file_data_buffer = NULL;
	if (direct_sound_load_file_and_find_audio_data(
		    0, file_name, &desc.lpwfxFormat, &sample_data,
		    &desc.dwBufferBytes)) {
		desc.dwSize = sizeof(desc);
		/* 194 is DSBCAPS_STATIC | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME; 234 adds DSBCAPS_LOCSOFTWARE and
		 * DSBCAPS_CTRLFREQUENCY. */
		desc.dwFlags = 194;
		if (omit_software_and_frequency_caps == 0) {
			desc.dwFlags = 234;
		}
		if (direct_sound->lpVtbl->CreateSoundBuffer(
			    direct_sound, &desc, &buffer, NULL) >= 0) {
			if (!direct_sound_copy_wave_data_to_buffer(
				    buffer, sample_data, desc.dwBufferBytes)) {
				buffer->lpVtbl->Release(buffer);
				buffer = NULL;
			}
		} else {
			buffer = NULL;
		}
	}
	if (g_wave_file_data_buffer != NULL) {
		free(g_wave_file_data_buffer);
	}

	return buffer;
}

/* Refills an existing buffer from a WAV file: reads the file, calls Restore on
 * the buffer and copies the samples in. Returns 1 when all three work, else 0.
 * Sets g_wave_file_data_buffer to NULL first and frees the file data at the end.
 * Does not check that the file's format or size match the buffer.
 * sound_play_effect_now and frontend_sound_play_ui_sound call it. */
// FUNCTION: XVT 0x44B720
int direct_sound_reload_wave_buffer(IDirectSoundBuffer *buffer,
				    const char *file_name)
{
	int result;
	unsigned int sample_bytes;
	const void *sample_data;

	result = 0;
	g_wave_file_data_buffer = NULL;
	if (direct_sound_load_file_and_find_audio_data(
		    0, file_name, NULL, &sample_data, &sample_bytes) &&
	    buffer->lpVtbl->Restore(buffer) >= 0 &&
	    direct_sound_copy_wave_data_to_buffer(buffer, sample_data,
						  sample_bytes)) {
		result = 1;
	}
	if (g_wave_file_data_buffer != NULL) {
		free(g_wave_file_data_buffer);
	}
	return result;
}

/* Reads a whole file into a new heap block, stored in g_wave_file_data_buffer, and
 * finds its format and sample data with direct_sound_find_format_and_data_chunks;
 * the outputs point into the block. Returns 1 when the read and the search
 * succeed, else 0, leaving the block allocated for the caller to free; returns
 * 0 without allocating when the file does not open. A failed allocation calls
 * fe_disk_io_fatal_error, which ends the program. The first argument is
 * ignored. */
// FUNCTION: XVT 0x44B790
int direct_sound_load_file_and_find_audio_data(int unused,
					       const char *file_name,
					       WAVEFORMATEX **format,
					       const void **sample_data,
					       unsigned int *sample_bytes)
{
	xvt_file *stream;
	size_t file_size;
	void *file_data;

	(void)unused;
	stream = file_open(file_name, "rb");
	if (stream != NULL) {
		file_seek(stream, 0, SEEK_END);
		file_size = (size_t)file_tell(stream);
		file_seek(stream, 0, SEEK_SET);
		g_wave_file_data_buffer = malloc(file_size);
		file_data = g_wave_file_data_buffer;
		if (file_data != NULL) {
			if (file_read_bytes(stream, file_data, file_size) &&
			    direct_sound_find_format_and_data_chunks(
				    file_data, format, sample_data,
				    sample_bytes)) {
				file_close(stream);
				return 1;
			}
		} else {
			fe_disk_io_fatal_error(
				FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
		}
		file_close(stream);
	}
	return 0;
}

/* Builds a set of bufferCount buffers (at least 1) for one WAV file: reads the
 * file, allocates the set (zeroed), loads the first buffer with
 * direct_sound_load_wave_buffer and makes each other one with
 * DuplicateSoundBuffer, loading the file again when a duplicate fails. Returns
 * the set, or NULL when the file fails to load, the allocation fails, or a
 * reload returns NULL (it frees the set then). wave_data points into the file
 * data of the first read, which nothing frees. Does not check that the first
 * buffer loaded. Nothing calls this. */
// FUNCTION: XVT 0x44B840
struct direct_sound_buffer_set *
direct_sound_load_wave_buffer_set(IDirectSound *direct_sound,
				  const char *file_name, int buffer_count)
{
	struct direct_sound_buffer_set *set;
	int actual_buffer_count;
	int buffer_index;
	IDirectSoundBuffer **buffer_slot;
	const void *sample_data;
	unsigned int sample_bytes;
	WAVEFORMATEX *format;
	size_t allocation_size;
	IDirectSound *device;

	set = NULL;
	if (direct_sound_load_file_and_find_audio_data(
		    0, file_name, &format, &sample_data, &sample_bytes)) {
		actual_buffer_count = buffer_count;
		if (actual_buffer_count < 1) {
			actual_buffer_count = 1;
		}
		device = direct_sound;
		buffer_index = 1;
		allocation_size =
			offsetof(struct direct_sound_buffer_set, buffers) +
			actual_buffer_count * sizeof(IDirectSoundBuffer *);
#ifdef XVT_MODERN
		set = calloc(1, allocation_size);
#else
		set = LocalAlloc(0x40, allocation_size);
#endif
		if (set != NULL) {
			set->buffer_count = actual_buffer_count;
			set->wave_data = (uint8_t *)sample_data;
			set->wave_data_size = sample_bytes;
			set->buffers[0] = direct_sound_load_wave_buffer(
				device, file_name, 0);
			if (set->buffer_count > 1) {
				buffer_slot = &set->buffers[1];
				do {
					if (device->lpVtbl
						    ->DuplicateSoundBuffer(
							    device,
							    set->buffers[0],
							    buffer_slot) < 0) {
						*buffer_slot =
							direct_sound_load_wave_buffer(
								device,
								file_name, 0);
						if (*buffer_slot == NULL) {
							direct_sound_free_wave_buffer_set(
								set);
							set = NULL;
							break;
						}
					}
					++buffer_slot;
					++buffer_index;
				} while (set->buffer_count > buffer_index);
			}
		}
	}
	return set;
}

/* Releases each buffer of the set that is not NULL, setting its slot to NULL,
 * and frees the set (LocalFree in the original build, free in the modern one);
 * wave_data stays allocated. Does nothing for NULL. Only
 * direct_sound_load_wave_buffer_set calls it, and nothing calls that. */
// FUNCTION: XVT 0x44B920
void direct_sound_free_wave_buffer_set(struct direct_sound_buffer_set *set)
{
	int buffer_index;
	IDirectSoundBuffer **buffer;

	if (set != NULL) {
		buffer_index = 0;
		if (set->buffer_count > 0) {
			buffer = set->buffers;
			do {
				if (*buffer != NULL) {
					(*buffer)->lpVtbl->Release(*buffer);
					*buffer = NULL;
				}
				++buffer;
				++buffer_index;
			} while (set->buffer_count > buffer_index);
		}
#ifdef XVT_MODERN
		free(set);
#else
		LocalFree(set);
#endif
	}
}

/* Picks the set's buffer to play next. Returns the buffer at next_buffer_index
 * when it is not playing (the 0x1 status bit clear, or GetStatus failed). When
 * it plays, a set of one buffer returns NULL; a larger set advances
 * next_buffer_index, wrapping to 0 at bufferCount, and returns that buffer, first
 * stopping it and rewinding it to 0 when it plays too. When the status read
 * last has the 0x2 bit (buffer lost) it calls Restore and copies wave_data back
 * in, returning NULL when either fails. Returns NULL for a NULL set or slot.
 * Only direct_sound_play_wave_buffer_set calls it, and nothing calls that. */
// FUNCTION: XVT 0x44B960
IDirectSoundBuffer *
direct_sound_acquire_wave_buffer_set_buffer(struct direct_sound_buffer_set *set)
{
	IDirectSoundBuffer *buffer;
	int next_buffer_index;
	int buffer_count;
	int status;

	if (set == NULL) {
		return NULL;
	}
	buffer = set->buffers[set->next_buffer_index];
	if (buffer != NULL) {
		if (buffer->lpVtbl->GetStatus(buffer, (uint32_t *)&status) <
		    0) {
			status = 0;
		}
		if (((uint8_t)status & 1) != 0) {
			buffer_count = set->buffer_count;
			if (buffer_count > 1) {
				next_buffer_index = set->next_buffer_index + 1;
				set->next_buffer_index = next_buffer_index;
				if (buffer_count <= next_buffer_index) {
					set->next_buffer_index = 0;
				}
				buffer = set->buffers[set->next_buffer_index];
				if (buffer->lpVtbl->GetStatus(
					    buffer, (uint32_t *)&status) >= 0 &&
				    ((uint8_t)status & 1) != 0) {
					buffer->lpVtbl->Stop(buffer);
					buffer->lpVtbl->SetCurrentPosition(
						buffer, 0);
				}
			} else {
				buffer = NULL;
			}
		}
		if (buffer != NULL && (status & 2) != 0 &&
		    (buffer->lpVtbl->Restore(buffer) < 0 ||
		     !direct_sound_copy_wave_data_to_buffer(
			     buffer, set->wave_data, set->wave_data_size))) {
			buffer = NULL;
		}
	}
	return buffer;
}

/* Plays the buffer direct_sound_acquire_wave_buffer_set_buffer picks with play_flags;
 * a looping play (the 0x1 flag) is allowed only for a set of one buffer.
 * Returns 1 when Play succeeds, else 0, and 0 for NULL. Nothing calls this. */
// FUNCTION: XVT 0x44BA30
int direct_sound_play_wave_buffer_set(struct direct_sound_buffer_set *set,
				      uint32_t play_flags)
{
	int result;
	IDirectSoundBuffer *buffer;

	result = 0;
	if (set == NULL) {
		return 0;
	}
	if ((play_flags & 1) == 0 || set->buffer_count == 1) {
		buffer = direct_sound_acquire_wave_buffer_set_buffer(set);
		if (buffer != NULL) {
			result = buffer->lpVtbl->Play(buffer, 0, 0,
						      play_flags) >= 0;
		}
	}
	return result;
}

/* Stops every buffer of the set and rewinds it to 0. Returns 1, or 0 for a NULL
 * set. Does not check for NULL buffers. Nothing calls this. */
// FUNCTION: XVT 0x44BA80
int direct_sound_stop_wave_buffer_set(struct direct_sound_buffer_set *set)
{
	int buffer_index;
	IDirectSoundBuffer **buffer;

	if (set == NULL) {
		return 0;
	}

	buffer_index = 0;
	if (set->buffer_count > 0) {
		buffer = set->buffers;
		do {
			(*buffer)->lpVtbl->Stop(*buffer);
			(*buffer)->lpVtbl->SetCurrentPosition(*buffer, 0);
			++buffer;
			++buffer_index;
		} while (set->buffer_count > buffer_index);
	}
	return 1;
}

/* Locks the first sample_bytes of the buffer, copies the samples into the one or
 * two regions the lock returns and unlocks it. Returns 1, or 0 when the buffer
 * or data is NULL, sample_bytes is 0 or the lock fails. Does not check that the
 * regions add up to sample_bytes. */
// FUNCTION: XVT 0x44BAD0
int direct_sound_copy_wave_data_to_buffer(IDirectSoundBuffer *buffer,
					  const void *sample_data,
					  unsigned int sample_bytes)
{
	void *region1;
	uint32_t region1_bytes;
	void *region2;
	uint32_t region2_bytes;

	if (buffer == NULL || sample_data == NULL || sample_bytes == 0 ||
	    buffer->lpVtbl->Lock(buffer, 0, sample_bytes, &region1,
				 &region1_bytes, &region2, &region2_bytes,
				 0) < 0) {
		return 0;
	}
	memcpy(region1, sample_data, region1_bytes);
	if (region2_bytes != 0) {
		memcpy(region2, (const uint8_t *)sample_data + region1_bytes,
		       region2_bytes);
	}
	buffer->lpVtbl->Unlock(buffer, region1, region1_bytes, region2,
			       region2_bytes);
	return 1;
}

/* Finds the format ("fmt ") and sample ("data") chunks of a RIFF WAVE file in
 * memory, for whichever of format, sample_data and sample_bytes are not NULL; it
 * sets those to NULL or 0 first. Returns 0 unless the data starts with "RIFF"
 * and "WAVE". It walks the chunks up to the RIFF size, each padded to an even
 * length, takes the first format chunk and the first data chunk, and returns 1
 * once it holds all it was asked for; it returns 0 when the walk ends first or
 * a format chunk is under 14 bytes. Does not check the chunk sizes against the
 * data it was given. */
// FUNCTION: XVT 0x44BB90
int direct_sound_find_format_and_data_chunks(const void *riff_data,
					     WAVEFORMATEX **format,
					     const void **sample_data,
					     unsigned int *sample_bytes)
{
	const uint32_t *chunk;
	const uint8_t *riff_end;
	const uint32_t *riff_words;

	if (format != NULL) {
		*format = NULL;
	}
	if (sample_data != NULL) {
		*sample_data = NULL;
	}
	if (sample_bytes != NULL) {
		*sample_bytes = 0;
	}

	riff_words = (const uint32_t *)riff_data;
	chunk = riff_words + 3;
	if (riff_words[0] == 0x46464952 && riff_words[2] == 0x45564157) {
		riff_end = (const uint8_t *)chunk + riff_words[1] - 4;
		if (riff_end > (const uint8_t *)chunk) {
			do {
				uint32_t chunk_id;
				uint32_t chunk_size;
				const uint8_t *chunk_data;

				chunk_id = chunk[0];
				chunk_size = chunk[1];
				++chunk;
				++chunk;
				chunk_data = (const uint8_t *)chunk;
				switch (chunk_id) {
				case 0x20746D66:
					if (format != NULL && *format == NULL) {
						if (chunk_size < 14) {
							riff_end =
								(const uint8_t
									 *)
									chunk;
							break;
						}
						*format = (WAVEFORMATEX *)
							chunk_data;
						if ((sample_data == NULL ||
						     *sample_data != NULL) &&
						    (sample_bytes == NULL ||
						     *sample_bytes != 0)) {
							return 1;
						}
					}
					break;
				case 0x61746164:
					if (!((sample_data == NULL ||
					       *sample_data != NULL) &&
					      (sample_bytes == NULL ||
					       *sample_bytes != 0))) {
						if (sample_data != NULL) {
							*sample_data =
								chunk_data;
						}
						if (sample_bytes != NULL) {
							*sample_bytes =
								chunk_size;
						}
						if (format == NULL ||
						    *format != NULL) {
							return 1;
						}
					}
					break;
				default:
					break;
				}
				chunk = (const uint32_t *)(chunk_data +
							   ((chunk_size + 1) &
							    0xFFFFFFFEu));
			} while ((const uint8_t *)chunk < riff_end);
		}
	}
	return 0;
}
