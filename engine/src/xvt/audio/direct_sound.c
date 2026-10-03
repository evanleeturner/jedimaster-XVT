#include "xvt/audio/direct_sound.h"

#include "xvt/assets/file.h"
#include "xvt/audio/sound.h"
#include "xvt/flight/fediskio.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef XVT_MODERN
__declspec(dllimport) void *__stdcall LocalAlloc(unsigned int flags,
						 size_t bytes);
__declspec(dllimport) void *__stdcall LocalFree(void *memory);
#endif

/* Heap copy of the WAV file DirectSound_LoadFileAndFindAudioData last read; the
 * format and sample pointers it returns point into it.
 * DirectSound_LoadWaveBuffer and DirectSound_ReloadWaveBuffer set it to NULL
 * before reading and free it after, without setting it back to NULL. */
// GLOBAL: XVT 0x5569D4
static void *g_waveFileDataBuffer = NULL;

/* Loads a WAV file into a new static DirectSound buffer: reads it with
 * DirectSound_LoadFileAndFindAudioData, creates a buffer of the file's format
 * and data size with flags 194, or 234 when omitSoftwareAndFrequencyCaps is 0
 * (see the comment inside), and copies the samples in. Returns the buffer, or
 * NULL when the file fails to load or parse, the buffer cannot be created, or
 * the copy fails, releasing the buffer then. Sets g_waveFileDataBuffer to NULL
 * first and frees the file data at the end. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x44B650
IDirectSoundBuffer *DirectSound_LoadWaveBuffer(IDirectSound *directSound,
					       const char *fileName,
					       int omitSoftwareAndFrequencyCaps)
{
	IDirectSoundBuffer *buffer = NULL;
	const void *sampleData;
	DSBUFFERDESC desc = {0};

	g_waveFileDataBuffer = NULL;
	if (DirectSound_LoadFileAndFindAudioData(0, fileName, &desc.lpwfxFormat,
						 &sampleData,
						 &desc.dwBufferBytes)) {
		desc.dwSize = sizeof(desc);
		/* 194 is DSBCAPS_STATIC | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME; 234 adds DSBCAPS_LOCSOFTWARE and
		 * DSBCAPS_CTRLFREQUENCY. */
		desc.dwFlags = 194;
		if (omitSoftwareAndFrequencyCaps == 0) {
			desc.dwFlags = 234;
		}
		if (directSound->lpVtbl->CreateSoundBuffer(
			    directSound, &desc, &buffer, NULL) >= 0) {
			if (!DirectSound_CopyWaveDataToBuffer(
				    buffer, sampleData, desc.dwBufferBytes)) {
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

/* Refills an existing buffer from a WAV file: reads the file, calls Restore on
 * the buffer and copies the samples in. Returns 1 when all three work, else 0.
 * Sets g_waveFileDataBuffer to NULL first and frees the file data at the end.
 * Does not check that the file's format or size match the buffer.
 * Sound_PlayEffectNow and FrontendSound_PlayUISound call it. */
// FUNCTION: XVT 0x44B720
int DirectSound_ReloadWaveBuffer(IDirectSoundBuffer *buffer,
				 const char *fileName)
{
	int result;
	unsigned int sampleBytes;
	const void *sampleData;

	result = 0;
	g_waveFileDataBuffer = NULL;
	if (DirectSound_LoadFileAndFindAudioData(0, fileName, NULL, &sampleData,
						 &sampleBytes) &&
	    buffer->lpVtbl->Restore(buffer) >= 0 &&
	    DirectSound_CopyWaveDataToBuffer(buffer, sampleData, sampleBytes)) {
		result = 1;
	}
	if (g_waveFileDataBuffer != NULL) {
		free(g_waveFileDataBuffer);
	}
	return result;
}

/* Reads a whole file into a new heap block, stored in g_waveFileDataBuffer, and
 * finds its format and sample data with DirectSound_FindFormatAndDataChunks;
 * the outputs point into the block. Returns 1 when the read and the search
 * succeed, else 0, leaving the block allocated for the caller to free; returns
 * 0 without allocating when the file does not open. A failed allocation calls
 * FeDiskIo_FatalError, which ends the program. The first argument is
 * ignored. */
// FUNCTION: XVT 0x44B790
int DirectSound_LoadFileAndFindAudioData(int unused, const char *fileName,
					 WAVEFORMATEX **format,
					 const void **sampleData,
					 unsigned int *sampleBytes)
{
	XvtFile *stream;
	size_t fileSize;
	void *fileData;

	(void)unused;
	stream = File_Open(fileName, "rb");
	if (stream != NULL) {
		File_Seek(stream, 0, SEEK_END);
		fileSize = (size_t)File_Tell(stream);
		File_Seek(stream, 0, SEEK_SET);
		fileData = g_waveFileDataBuffer = malloc(fileSize);
		if (fileData != NULL) {
			if (File_ReadBytes(stream, fileData, fileSize) &&
			    DirectSound_FindFormatAndDataChunks(
				    fileData, format, sampleData,
				    sampleBytes)) {
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

/* Builds a set of bufferCount buffers (at least 1) for one WAV file: reads the
 * file, allocates the set (zeroed), loads the first buffer with
 * DirectSound_LoadWaveBuffer and makes each other one with
 * DuplicateSoundBuffer, loading the file again when a duplicate fails. Returns
 * the set, or NULL when the file fails to load, the allocation fails, or a
 * reload returns NULL (it frees the set then). waveData points into the file
 * data of the first read, which nothing frees. Does not check that the first
 * buffer loaded. Nothing calls this. */
// FUNCTION: XVT 0x44B840
struct DirectSoundBufferSet *
DirectSound_LoadWaveBufferSet(IDirectSound *directSound, const char *fileName,
			      int bufferCount)
{
	struct DirectSoundBufferSet *set;
	int actualBufferCount;
	int bufferIndex;
	IDirectSoundBuffer **bufferSlot;
	const void *sampleData;
	unsigned int sampleBytes;
	WAVEFORMATEX *format;
	size_t allocationSize;
	IDirectSound *device;

	set = NULL;
	if (DirectSound_LoadFileAndFindAudioData(0, fileName, &format,
						 &sampleData, &sampleBytes)) {
		actualBufferCount = bufferCount;
		if (actualBufferCount < 1) {
			actualBufferCount = 1;
		}
		device = directSound;
		bufferIndex = 1;
		allocationSize =
			offsetof(struct DirectSoundBufferSet, buffers) +
			actualBufferCount * sizeof(IDirectSoundBuffer *);
#ifdef XVT_MODERN
		set = calloc(1, allocationSize);
#else
		set = LocalAlloc(0x40, allocationSize);
#endif
		if (set != NULL) {
			set->bufferCount = actualBufferCount;
			set->waveData = (uint8_t *)sampleData;
			set->waveDataSize = sampleBytes;
			set->buffers[0] =
				DirectSound_LoadWaveBuffer(device, fileName, 0);
			if (set->bufferCount > 1) {
				bufferSlot = &set->buffers[1];
				do {
					if (device->lpVtbl
						    ->DuplicateSoundBuffer(
							    device,
							    set->buffers[0],
							    bufferSlot) < 0) {
						*bufferSlot =
							DirectSound_LoadWaveBuffer(
								device,
								fileName, 0);
						if (*bufferSlot == NULL) {
							DirectSound_FreeWaveBufferSet(
								set);
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

/* Releases each buffer of the set that is not NULL, setting its slot to NULL,
 * and frees the set (LocalFree in the original build, free in the modern one);
 * waveData stays allocated. Does nothing for NULL. Only
 * DirectSound_LoadWaveBufferSet calls it, and nothing calls that. */
// FUNCTION: XVT 0x44B920
void DirectSound_FreeWaveBufferSet(struct DirectSoundBufferSet *set)
{
	int bufferIndex;
	IDirectSoundBuffer **buffer;

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

/* Picks the set's buffer to play next. Returns the buffer at nextBufferIndex
 * when it is not playing (the 0x1 status bit clear, or GetStatus failed). When
 * it plays, a set of one buffer returns NULL; a larger set advances
 * nextBufferIndex, wrapping to 0 at bufferCount, and returns that buffer, first
 * stopping it and rewinding it to 0 when it plays too. When the status read
 * last has the 0x2 bit (buffer lost) it calls Restore and copies waveData back
 * in, returning NULL when either fails. Returns NULL for a NULL set or slot.
 * Only DirectSound_PlayWaveBufferSet calls it, and nothing calls that. */
// FUNCTION: XVT 0x44B960
IDirectSoundBuffer *
DirectSound_AcquireWaveBufferSetBuffer(struct DirectSoundBufferSet *set)
{
	IDirectSoundBuffer *buffer;
	int nextBufferIndex;
	int bufferCount;
	int status;

	if (set == NULL) {
		return NULL;
	}
	buffer = set->buffers[set->nextBufferIndex];
	if (buffer != NULL) {
		if (buffer->lpVtbl->GetStatus(buffer, (uint32_t *)&status) <
		    0) {
			status = 0;
		}
		if (((uint8_t)status & 1) != 0) {
			bufferCount = set->bufferCount;
			if (bufferCount > 1) {
				nextBufferIndex = set->nextBufferIndex + 1;
				set->nextBufferIndex = nextBufferIndex;
				if (bufferCount <= nextBufferIndex) {
					set->nextBufferIndex = 0;
				}
				buffer = set->buffers[set->nextBufferIndex];
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
		     !DirectSound_CopyWaveDataToBuffer(buffer, set->waveData,
						       set->waveDataSize))) {
			buffer = NULL;
		}
	}
	return buffer;
}

/* Plays the buffer DirectSound_AcquireWaveBufferSetBuffer picks with playFlags;
 * a looping play (the 0x1 flag) is allowed only for a set of one buffer.
 * Returns 1 when Play succeeds, else 0, and 0 for NULL. Nothing calls this. */
// FUNCTION: XVT 0x44BA30
int DirectSound_PlayWaveBufferSet(struct DirectSoundBufferSet *set,
				  uint32_t playFlags)
{
	int result;
	IDirectSoundBuffer *buffer;

	result = 0;
	if (set == NULL) {
		return 0;
	}
	if ((playFlags & 1) == 0 || set->bufferCount == 1) {
		buffer = DirectSound_AcquireWaveBufferSetBuffer(set);
		if (buffer != NULL) {
			result = buffer->lpVtbl->Play(buffer, 0, 0,
						      playFlags) >= 0;
		}
	}
	return result;
}

/* Stops every buffer of the set and rewinds it to 0. Returns 1, or 0 for a NULL
 * set. Does not check for NULL buffers. Nothing calls this. */
// FUNCTION: XVT 0x44BA80
int DirectSound_StopWaveBufferSet(struct DirectSoundBufferSet *set)
{
	int bufferIndex;
	IDirectSoundBuffer **buffer;

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

/* Locks the first sampleBytes of the buffer, copies the samples into the one or
 * two regions the lock returns and unlocks it. Returns 1, or 0 when the buffer
 * or data is NULL, sampleBytes is 0 or the lock fails. Does not check that the
 * regions add up to sampleBytes. */
// FUNCTION: XVT 0x44BAD0
int DirectSound_CopyWaveDataToBuffer(IDirectSoundBuffer *buffer,
				     const void *sampleData,
				     unsigned int sampleBytes)
{
	void *region1;
	uint32_t region1Bytes;
	void *region2;
	uint32_t region2Bytes;

	if (buffer == NULL || sampleData == NULL || sampleBytes == 0 ||
	    buffer->lpVtbl->Lock(buffer, 0, sampleBytes, &region1,
				 &region1Bytes, &region2, &region2Bytes,
				 0) < 0) {
		return 0;
	}
	memcpy(region1, sampleData, region1Bytes);
	if (region2Bytes != 0) {
		memcpy(region2, (const uint8_t *)sampleData + region1Bytes,
		       region2Bytes);
	}
	buffer->lpVtbl->Unlock(buffer, region1, region1Bytes, region2,
			       region2Bytes);
	return 1;
}

/* Finds the format ("fmt ") and sample ("data") chunks of a RIFF WAVE file in
 * memory, for whichever of format, sampleData and sampleBytes are not NULL; it
 * sets those to NULL or 0 first. Returns 0 unless the data starts with "RIFF"
 * and "WAVE". It walks the chunks up to the RIFF size, each padded to an even
 * length, takes the first format chunk and the first data chunk, and returns 1
 * once it holds all it was asked for; it returns 0 when the walk ends first or
 * a format chunk is under 14 bytes. Does not check the chunk sizes against the
 * data it was given. */
// FUNCTION: XVT 0x44BB90
int DirectSound_FindFormatAndDataChunks(const void *riffData,
					WAVEFORMATEX **format,
					const void **sampleData,
					unsigned int *sampleBytes)
{
	const uint32_t *chunk;
	const uint8_t *riffEnd;
	const uint32_t *riffWords;

	if (format != NULL) {
		*format = NULL;
	}
	if (sampleData != NULL) {
		*sampleData = NULL;
	}
	if (sampleBytes != NULL) {
		*sampleBytes = 0;
	}

	riffWords = (const uint32_t *)riffData;
	chunk = riffWords + 3;
	if (riffWords[0] == 0x46464952 && riffWords[2] == 0x45564157) {
		riffEnd = (const uint8_t *)chunk + riffWords[1] - 4;
		if (riffEnd > (const uint8_t *)chunk) {
			do {
				uint32_t chunkId;
				uint32_t chunkSize;
				const uint8_t *chunkData;

				chunkId = chunk[0];
				chunkSize = chunk[1];
				++chunk;
				++chunk;
				chunkData = (const uint8_t *)chunk;
				switch (chunkId) {
				case 0x20746D66:
					if (format != NULL && *format == NULL) {
						if (chunkSize < 14) {
							riffEnd = (const uint8_t
									   *)
								chunk;
							break;
						}
						*format = (WAVEFORMATEX *)
							chunkData;
						if ((sampleData == NULL ||
						     *sampleData != NULL) &&
						    (sampleBytes == NULL ||
						     *sampleBytes != 0)) {
							return 1;
						}
					}
					break;
				case 0x61746164:
					if (!((sampleData == NULL ||
					       *sampleData != NULL) &&
					      (sampleBytes == NULL ||
					       *sampleBytes != 0))) {
						if (sampleData != NULL) {
							*sampleData = chunkData;
						}
						if (sampleBytes != NULL) {
							*sampleBytes =
								chunkSize;
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
				chunk = (const uint32_t *)(chunkData +
							   ((chunkSize + 1) &
							    0xFFFFFFFEu));
			} while ((const uint8_t *)chunk < riffEnd);
		}
	}
	return 0;
}
