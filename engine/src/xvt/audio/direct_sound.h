#ifndef XVT_AUDIO_DIRECT_SOUND_H
#define XVT_AUDIO_DIRECT_SOUND_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct DirectSoundBufferSet {
	/* Samples of the file, inside its first read's file data; copied back
	 * into a lost buffer. */
	uint8_t *waveData;
	unsigned int waveDataSize; /* Bytes at waveData. */
	int bufferCount;	   /* Buffers in buffers[], at least 1. */
	/* Buffer DirectSound_AcquireWaveBufferSetBuffer tries first; 0 in a new
	 * set. */
	int nextBufferIndex;
	/* The bufferCount buffers; the allocation runs the array past its one
	 * declared entry. */
	IDirectSoundBuffer *buffers[1];
};

IDirectSoundBuffer *
DirectSound_LoadWaveBuffer(IDirectSound *directSound, const char *fileName,
			   int omitSoftwareAndFrequencyCaps);
int DirectSound_ReloadWaveBuffer(IDirectSoundBuffer *buffer,
				 const char *fileName);
int DirectSound_LoadFileAndFindAudioData(int unused, const char *fileName,
					 WAVEFORMATEX **format,
					 const void **sampleData,
					 unsigned int *sampleBytes);
struct DirectSoundBufferSet *
DirectSound_LoadWaveBufferSet(IDirectSound *directSound, const char *fileName,
			      int bufferCount);
void DirectSound_FreeWaveBufferSet(struct DirectSoundBufferSet *set);
IDirectSoundBuffer *
DirectSound_AcquireWaveBufferSetBuffer(struct DirectSoundBufferSet *set);
int DirectSound_PlayWaveBufferSet(struct DirectSoundBufferSet *set,
				  uint32_t playFlags);
int DirectSound_StopWaveBufferSet(struct DirectSoundBufferSet *set);
int DirectSound_CopyWaveDataToBuffer(IDirectSoundBuffer *buffer,
				     const void *sampleData,
				     unsigned int sampleBytes);
int DirectSound_FindFormatAndDataChunks(const void *riffData,
					WAVEFORMATEX **format,
					const void **sampleData,
					unsigned int *sampleBytes);

#ifdef __cplusplus
}
#endif

#endif
