#ifndef XVT_AUDIO_DIRECT_SOUND_H
#define XVT_AUDIO_DIRECT_SOUND_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct DirectSoundBufferSet {
	uint8_t* waveData;
	unsigned int waveDataSize;
	int bufferCount;
	int nextBufferIndex;
	IDirectSoundBuffer* buffers[1];
};

IDirectSoundBuffer* DirectSound_LoadWaveBuffer(IDirectSound* directSound, const char* fileName,
											   int create3DFlags);
int DirectSound_ReloadWaveBuffer(IDirectSoundBuffer* buffer, const char* fileName);
int DirectSound_LoadFileAndFindAudioData(int unused, const char* fileName, WAVEFORMATEX** format,
										 const void** sampleData, unsigned int* sampleBytes);
DirectSoundBufferSet* DirectSound_LoadWaveBufferSet(IDirectSound* directSound, const char* fileName,
													int bufferCount);
void DirectSound_FreeWaveBufferSet(DirectSoundBufferSet* set);
IDirectSoundBuffer* DirectSound_AcquireWaveBufferSetBuffer(DirectSoundBufferSet* set);
int DirectSound_PlayWaveBufferSet(DirectSoundBufferSet* set, uint32_t playFlags);
int DirectSound_StopWaveBufferSet(DirectSoundBufferSet* set);
int DirectSound_CopyWaveDataToBuffer(IDirectSoundBuffer* buffer, const void* sampleData,
									 unsigned int sampleBytes);
int DirectSound_FindFormatAndDataChunks(const void* riffData, WAVEFORMATEX** format, const void** sampleData,
										unsigned int* sampleBytes);

#ifdef __cplusplus
}
#endif

#endif
