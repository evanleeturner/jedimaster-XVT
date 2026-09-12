#include "xvt/audio/direct_sound.h"

#include "xvt/assets/file.h"
#include "xvt/audio/sound.h"
#include "xvt/flight/fediskio.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef XVT_MODERN
__declspec(dllimport) void* __stdcall LocalAlloc(unsigned int flags, size_t bytes);
__declspec(dllimport) void* __stdcall LocalFree(void* memory);
#endif

// GLOBAL: XVT 0x5569D4
static void* g_waveFileDataBuffer = NULL;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x44B650
IDirectSoundBuffer* DirectSound_LoadWaveBuffer(IDirectSound* directSound, const char* fileName,
											   int create3DFlags) {
	IDirectSoundBuffer* buffer = NULL;
	const void* sampleData;
	DSBUFFERDESC desc = { 0 };

	g_waveFileDataBuffer = NULL;
	if (DirectSound_LoadFileAndFindAudioData(0, fileName, &desc.lpwfxFormat, &sampleData,
											 &desc.dwBufferBytes)) {
		desc.dwSize = sizeof(desc);
		desc.dwFlags = 194;
		if (create3DFlags == 0) {
			desc.dwFlags = 234;
		}
		if (directSound->lpVtbl->CreateSoundBuffer(directSound, &desc, &buffer, NULL) >= 0) {
			if (!DirectSound_CopyWaveDataToBuffer(buffer, sampleData, desc.dwBufferBytes)) {
				buffer->lpVtbl->Release(buffer);
				buffer = NULL;
			}
		} else {
			buffer = NULL;
		}
	}
	if (g_waveFileDataBuffer != NULL) {
		free(g_waveFileDataBuffer);
	}

	return buffer;
}

// FUNCTION: XVT 0x44B720
int DirectSound_ReloadWaveBuffer(IDirectSoundBuffer* buffer, const char* fileName) {
	int result;
	unsigned int sampleBytes;
	const void* sampleData;

	result = 0;
	g_waveFileDataBuffer = NULL;
	if (DirectSound_LoadFileAndFindAudioData(0, fileName, NULL, &sampleData, &sampleBytes) &&
		buffer->lpVtbl->Restore(buffer) >= 0 &&
		DirectSound_CopyWaveDataToBuffer(buffer, sampleData, sampleBytes)) {
		result = 1;
	}
	if (g_waveFileDataBuffer != NULL) {
		free(g_waveFileDataBuffer);
	}
	return result;
}

// FUNCTION: XVT 0x44B790
int DirectSound_LoadFileAndFindAudioData(int unused, const char* fileName, WAVEFORMATEX** format,
										 const void** sampleData, unsigned int* sampleBytes) {
	XvtFile* stream;
	size_t fileSize;
	void* fileData;

	(void)unused;
	stream = File_Open(fileName, "rb");
	if (stream != NULL) {
		File_Seek(stream, 0, SEEK_END);
		fileSize = (size_t)File_Tell(stream);
		File_Seek(stream, 0, SEEK_SET);
		fileData = g_waveFileDataBuffer = malloc(fileSize);
		if (fileData != NULL) {
			if (File_ReadCount(stream, fileData, fileSize) &&
				DirectSound_FindFormatAndDataChunks(fileData, format, sampleData, sampleBytes)) {
				File_Close(stream);
				return 1;
			}
		} else {
			FeDiskIo_FatalError(FILE_ERROR_STR_NOT_ENOUGH_MEMORY);
		}
		File_Close(stream);
	}
	return 0;
}

// FUNCTION: XVT 0x44B840
DirectSoundBufferSet* DirectSound_LoadWaveBufferSet(IDirectSound* directSound, const char* fileName,
													int bufferCount) {
	DirectSoundBufferSet* set;
	int actualBufferCount;
	int bufferIndex;
	IDirectSoundBuffer** bufferSlot;
	const void* sampleData;
	unsigned int sampleBytes;
	WAVEFORMATEX* format;
	size_t allocationSize;
	IDirectSound* device;

	set = NULL;
	if (DirectSound_LoadFileAndFindAudioData(0, fileName, &format, &sampleData, &sampleBytes)) {
		actualBufferCount = bufferCount;
		if (actualBufferCount < 1) {
			actualBufferCount = 1;
		}
		device = directSound;
		bufferIndex = 1;
		allocationSize =
			offsetof(DirectSoundBufferSet, buffers) + actualBufferCount * sizeof(IDirectSoundBuffer*);
#ifdef XVT_MODERN
		set = calloc(1, allocationSize);
#else
		set = LocalAlloc(0x40, allocationSize);
#endif
		if (set != NULL) {
			set->bufferCount = actualBufferCount;
			set->waveData = (uint8_t*)sampleData;
			set->waveDataSize = sampleBytes;
			set->buffers[0] = DirectSound_LoadWaveBuffer(device, fileName, 0);
			if (set->bufferCount > 1) {
				bufferSlot = &set->buffers[1];
				do {
					if (device->lpVtbl->DuplicateSoundBuffer(device, set->buffers[0], bufferSlot) < 0) {
						*bufferSlot = DirectSound_LoadWaveBuffer(device, fileName, 0);
						if (*bufferSlot == NULL) {
							DirectSound_FreeWaveBufferSet(set);
							set = NULL;
							break;
						}
					}
					++bufferSlot;
					++bufferIndex;
				} while (set->bufferCount > bufferIndex);
			}
		}
	}
	return set;
}

// FUNCTION: XVT 0x44B920
void DirectSound_FreeWaveBufferSet(DirectSoundBufferSet* set) {
	int bufferIndex;
	IDirectSoundBuffer** buffer;

	if (set != NULL) {
		bufferIndex = 0;
		if (set->bufferCount > 0) {
			buffer = set->buffers;
			do {
				if (*buffer != NULL) {
					(*buffer)->lpVtbl->Release(*buffer);
					*buffer = NULL;
				}
				++buffer;
				++bufferIndex;
			} while (set->bufferCount > bufferIndex);
		}
#ifdef XVT_MODERN
		free(set);
#else
		LocalFree(set);
#endif
	}
}

// FUNCTION: XVT 0x44B960
IDirectSoundBuffer* DirectSound_AcquireWaveBufferSetBuffer(DirectSoundBufferSet* set) {
	IDirectSoundBuffer* buffer;
	int nextBufferIndex;
	int bufferCount;
	int status;

	if (set == NULL)
		return NULL;
	buffer = set->buffers[set->nextBufferIndex];
	if (buffer != NULL) {
		if (buffer->lpVtbl->GetStatus(buffer, (uint32_t*)&status) < 0)
			status = 0;
		if (((uint8_t)status & 1) != 0) {
			bufferCount = set->bufferCount;
			if (bufferCount > 1) {
				nextBufferIndex = set->nextBufferIndex + 1;
				set->nextBufferIndex = nextBufferIndex;
				if (bufferCount <= nextBufferIndex)
					set->nextBufferIndex = 0;
				buffer = set->buffers[set->nextBufferIndex];
				if (buffer->lpVtbl->GetStatus(buffer, (uint32_t*)&status) >= 0 &&
					((uint8_t)status & 1) != 0) {
					buffer->lpVtbl->Stop(buffer);
					buffer->lpVtbl->SetCurrentPosition(buffer, 0);
				}
			} else
				buffer = NULL;
		}
		if (buffer != NULL && (status & 2) != 0 &&
			(buffer->lpVtbl->Restore(buffer) < 0 ||
			 !DirectSound_CopyWaveDataToBuffer(buffer, set->waveData, set->waveDataSize))) {
			buffer = NULL;
		}
	}
	return buffer;
}

// FUNCTION: XVT 0x44BA30
int DirectSound_PlayWaveBufferSet(DirectSoundBufferSet* set, uint32_t playFlags) {
	int result;
	IDirectSoundBuffer* buffer;

	result = 0;
	if (set == NULL)
		return 0;
	if ((playFlags & 1) == 0 || set->bufferCount == 1) {
		buffer = DirectSound_AcquireWaveBufferSetBuffer(set);
		if (buffer != NULL)
			result = buffer->lpVtbl->Play(buffer, 0, 0, playFlags) >= 0;
	}
	return result;
}

// FUNCTION: XVT 0x44BA80
int DirectSound_StopWaveBufferSet(DirectSoundBufferSet* set) {
	int bufferIndex;
	IDirectSoundBuffer** buffer;

	if (set == NULL) {
		return 0;
	}

	bufferIndex = 0;
	if (set->bufferCount > 0) {
		buffer = set->buffers;
		do {
			(*buffer)->lpVtbl->Stop(*buffer);
			(*buffer)->lpVtbl->SetCurrentPosition(*buffer, 0);
			++buffer;
			++bufferIndex;
		} while (set->bufferCount > bufferIndex);
	}
	return 1;
}

// FUNCTION: XVT 0x44BAD0
int DirectSound_CopyWaveDataToBuffer(IDirectSoundBuffer* buffer, const void* sampleData,
									 unsigned int sampleBytes) {
	void* region1;
	uint32_t region1Bytes;
	void* region2;
	uint32_t region2Bytes;

	if (buffer == NULL || sampleData == NULL || sampleBytes == 0 ||
		buffer->lpVtbl->Lock(buffer, 0, sampleBytes, &region1, &region1Bytes, &region2, &region2Bytes, 0) < 0)
		return 0;
	memcpy(region1, sampleData, region1Bytes);
	if (region2Bytes != 0)
		memcpy(region2, (const uint8_t*)sampleData + region1Bytes, region2Bytes);
	buffer->lpVtbl->Unlock(buffer, region1, region1Bytes, region2, region2Bytes);
	return 1;
}

// FUNCTION: XVT 0x44BB90
int DirectSound_FindFormatAndDataChunks(const void* riffData, WAVEFORMATEX** format, const void** sampleData,
										unsigned int* sampleBytes) {
	const uint32_t* chunk;
	const uint8_t* riffEnd;
	const uint32_t* riffWords;

	if (format != NULL) {
		*format = NULL;
	}
	if (sampleData != NULL) {
		*sampleData = NULL;
	}
	if (sampleBytes != NULL) {
		*sampleBytes = 0;
	}

	riffWords = (const uint32_t*)riffData;
	chunk = riffWords + 3;
	if (riffWords[0] == 0x46464952 && riffWords[2] == 0x45564157) {
		riffEnd = (const uint8_t*)chunk + riffWords[1] - 4;
		if (riffEnd > (const uint8_t*)chunk) {
			do {
				uint32_t chunkId;
				uint32_t chunkSize;
				const uint8_t* chunkData;

				chunkId = chunk[0];
				chunkSize = chunk[1];
				++chunk;
				++chunk;
				chunkData = (const uint8_t*)chunk;
				switch (chunkId) {
					case 0x20746D66:
						if (format != NULL && *format == NULL) {
							if (chunkSize < 14) {
								riffEnd = (const uint8_t*)chunk;
								break;
							}
							*format = (WAVEFORMATEX*)chunkData;
							if ((sampleData == NULL || *sampleData != NULL) &&
								(sampleBytes == NULL || *sampleBytes != 0)) {
								return 1;
							}
						}
						break;
					case 0x61746164:
						if (!((sampleData == NULL || *sampleData != NULL) &&
							  (sampleBytes == NULL || *sampleBytes != 0))) {
							if (sampleData != NULL) {
								*sampleData = chunkData;
							}
							if (sampleBytes != NULL) {
								*sampleBytes = chunkSize;
							}
							if (format == NULL || *format != NULL) {
								return 1;
							}
						}
						break;
					default:
						break;
				}
				chunk = (const uint32_t*)(chunkData + ((chunkSize + 1) & 0xFFFFFFFEu));
			} while ((const uint8_t*)chunk < riffEnd);
		}
	}
	return 0;
}
