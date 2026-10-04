#ifndef XVT_AUDIO_DIRECT_SOUND_H
#define XVT_AUDIO_DIRECT_SOUND_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct direct_sound_buffer_set {
	/* Samples of the file, inside its first read's file data; copied back
	 * into a lost buffer. */
	uint8_t *wave_data;
	unsigned int wave_data_size; /* Bytes at wave_data. */
	int buffer_count;	     /* Buffers in buffers[], at least 1. */
	/* Buffer direct_sound_acquire_wave_buffer_set_buffer tries first; 0 in a new
	 * set. */
	int next_buffer_index;
	/* The bufferCount buffers; the allocation runs the array past its one
	 * declared entry. */
	IDirectSoundBuffer *buffers[1];
};

IDirectSoundBuffer *
direct_sound_load_wave_buffer(IDirectSound *direct_sound, const char *file_name,
			      int omit_software_and_frequency_caps);
int direct_sound_reload_wave_buffer(IDirectSoundBuffer *buffer,
				    const char *file_name);
int direct_sound_load_file_and_find_audio_data(int unused,
					       const char *file_name,
					       WAVEFORMATEX **format,
					       const void **sample_data,
					       unsigned int *sample_bytes);
struct direct_sound_buffer_set *
direct_sound_load_wave_buffer_set(IDirectSound *direct_sound,
				  const char *file_name, int buffer_count);
void direct_sound_free_wave_buffer_set(struct direct_sound_buffer_set *set);
IDirectSoundBuffer *direct_sound_acquire_wave_buffer_set_buffer(
	struct direct_sound_buffer_set *set);
int direct_sound_play_wave_buffer_set(struct direct_sound_buffer_set *set,
				      uint32_t play_flags);
int direct_sound_stop_wave_buffer_set(struct direct_sound_buffer_set *set);
int direct_sound_copy_wave_data_to_buffer(IDirectSoundBuffer *buffer,
					  const void *sample_data,
					  unsigned int sample_bytes);
int direct_sound_find_format_and_data_chunks(const void *riff_data,
					     WAVEFORMATEX **format,
					     const void **sample_data,
					     unsigned int *sample_bytes);

#ifdef __cplusplus
}
#endif

#endif
