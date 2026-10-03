#include "xvt/audio/frontend_sound.h"
#include "aeron/compat/dsound.h"
#include "xvt/assets/file.h"
#include "xvt/audio/direct_sound.h"
#include "xvt/audio/sound.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_state.h"

#include <stdio.h>
#include <string.h>

struct FrontendSoundPcmFormat {
	uint16_t formatTag;		/* Wave format, 1 for PCM. */
	uint16_t channels;		/* Channels, 2. */
	uint32_t samplesPerSecond;	/* Sample rate, 11264. */
	uint32_t averageBytesPerSecond; /* Bytes per second, 22528. */
	uint16_t blockAlign;		/* Bytes per sample frame, 2. */
	uint16_t bitsPerSample;		/* Bits per sample, 8. */
};

/* Starts the front end's DirectSound. Returns 1 at once when
 * g_frontState.frontendDirectSound is set. Otherwise it clears the 12 voices,
 * frontendActiveVoiceCount, frontendSoundBufferCount, frontendSoundPlaySerial
 * and the buffer and name of the 128 buffer records, creates
 * frontendDirectSound with DirectSoundCreate on the default device, asks for
 * frontendPrimarySoundBuffer with a format of 11264 samples per second, 2
 * channels and 8 bits, without checking the result, and sets cooperative level
 * 1 (DSSCL_NORMAL) for hwnd. Returns 1, or 0 when DirectSoundCreate fails or,
 * after FrontendSound_ShutdownDirectSound, when setting the cooperative level
 * fails. FrontendDisplay_InitMainWindow and FrontendDisplay_ReinitSurfaces call
 * it. */
// FUNCTION: XVT 0x4DE190
int FrontendSound_InitDirectSound(void *hwnd)
{
	int voiceIndex;
	int bufferIndex;
	struct FrontendSoundPcmFormat primaryFormat;
	DSBUFFERDESC primaryBufferDesc;

	if (g_frontState.frontendDirectSound != NULL) {
		return 1;
	}
	for (voiceIndex = 0; voiceIndex < 12; ++voiceIndex) {
		g_frontState.frontendSoundVoices[voiceIndex].bufferIndex = -1;
		g_frontState.frontendSoundVoices[voiceIndex].playSerial = 0;
		g_frontState.frontendSoundVoices[voiceIndex].buffer = NULL;
	}
	g_frontState.frontendActiveVoiceCount = 0;
	g_frontState.frontendSoundBufferCount = 0;
	g_frontState.frontendSoundPlaySerial = 0;
	for (bufferIndex = 0; bufferIndex < 128; ++bufferIndex) {
		g_frontState.frontendSoundBuffers[bufferIndex].buffer = NULL;
		g_frontState.frontendSoundBuffers[bufferIndex].name[0] = '\0';
	}
	if (DirectSoundCreate(NULL, (void **)&g_frontState.frontendDirectSound,
			      NULL) != 0) {
		return 0;
	}

	memset(&primaryFormat, 0, sizeof(primaryFormat));
	primaryFormat.samplesPerSecond = 11264;
	primaryFormat.averageBytesPerSecond = 22528;
	primaryFormat.formatTag = 1;
	primaryFormat.channels = 2;
	primaryFormat.blockAlign = 2;
	primaryFormat.bitsPerSample = 8;
	memset(&primaryBufferDesc, 0, sizeof(primaryBufferDesc));
	primaryBufferDesc.dwSize = sizeof(primaryBufferDesc);
	primaryBufferDesc.dwFlags = DSBCAPS_PRIMARYBUFFER;
	primaryBufferDesc.lpwfxFormat = (WAVEFORMATEX *)&primaryFormat;
	g_frontState.frontendDirectSound->lpVtbl->CreateSoundBuffer(
		g_frontState.frontendDirectSound, &primaryBufferDesc,
		&g_frontState.frontendPrimarySoundBuffer, NULL);
	if (g_frontState.frontendDirectSound->lpVtbl->SetCooperativeLevel(
		    g_frontState.frontendDirectSound, hwnd, 1) != 0) {
		FrontendSound_ShutdownDirectSound();
		return 0;
	}
	return 1;
}

/* Returns 1 at once when g_frontState.frontendDirectSound is NULL. Otherwise it
 * releases it and sets it to NULL, clears the buffer and name of the 128 buffer
 * records and the 12 voices, sets frontendPrimarySoundBuffer to NULL without
 * releasing it and frontendSoundPlaySerial to 0, and returns 1. It releases
 * none of the loaded buffers and leaves frontendSoundBufferCount and
 * frontendActiveVoiceCount as they are. FrontendDisplay_Shutdown and
 * FrontendDisplay_ReleaseSurfacesForFlight call it, and in the original build
 * Net_ShutdownDirectPlaySessionEx. */
// FUNCTION: XVT 0x4DE2E0
int FrontendSound_ShutdownDirectSound(void)
{
	int bufferIndex;
	int voiceIndex;

	if (g_frontState.frontendDirectSound == NULL) {
		return 1;
	}

	g_frontState.frontendDirectSound->lpVtbl->Release(
		g_frontState.frontendDirectSound);
	g_frontState.frontendDirectSound = NULL;
	for (bufferIndex = 0; bufferIndex < 128; ++bufferIndex) {
		g_frontState.frontendSoundBuffers[bufferIndex].buffer = NULL;
		g_frontState.frontendSoundBuffers[bufferIndex].name[0] = '\0';
	}
	for (voiceIndex = 0; voiceIndex < 12; ++voiceIndex) {
		g_frontState.frontendSoundVoices[voiceIndex].bufferIndex = -1;
		g_frontState.frontendSoundVoices[voiceIndex].playSerial = 0;
		g_frontState.frontendSoundVoices[voiceIndex].buffer = NULL;
	}
	g_frontState.frontendPrimarySoundBuffer = NULL;
	g_frontState.frontendSoundPlaySerial = 0;
	return 1;
}

/* Calls FrontendSound_LoadSoundFile with omitSoftwareAndFrequencyCaps 0 and
 * returns its result. Only FrontendSound_LoadList calls it. */
// FUNCTION: XVT 0x4DE370
int FrontendSound_LoadSound(const char *fileName, const char *soundName)
{
	return FrontendSound_LoadSoundFile(fileName, soundName, 0);
}

/* Loads a WAV file as a named front-end sound: DirectSound_LoadWaveBuffer makes
 * its buffer, rewound to 0, and FrontendSound_InsertSortedBuffer adds the
 * record with the name (up to 63 characters), the file name (up to 191) and
 * priority 0. Returns 1, also when the name is already loaded; 0 when either
 * string is empty, 128 sounds are loaded, g_frontState.frontendDirectSound is
 * NULL or the buffer fails to load. Unlocks the front end's back buffer around
 * its DirectSound calls and, when it was locked, locks it again into
 * g_drawSurfacePtr. Only FrontendSound_LoadSound calls it, passing 0. */
// FUNCTION: XVT 0x4DE390
int FrontendSound_LoadSoundFile(const char *fileName, const char *soundName,
				int omitSoftwareAndFrequencyCaps)
{
	struct FrontendSoundBufferRecord record;
	int wasBackBufferLocked;

	if (*fileName == '\0') {
		return 0;
	}
	if (*soundName == '\0') {
		return 0;
	}
	if (g_frontState.frontendSoundBufferCount >= 128) {
		return 0;
	}
	if (g_frontState.frontendDirectSound == NULL) {
		return 0;
	}
	if (FrontendSound_FindBufferByName(soundName) != -1) {
		return 1;
	}

	wasBackBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	record.buffer = DirectSound_LoadWaveBuffer(
		g_frontState.frontendDirectSound, fileName,
		omitSoftwareAndFrequencyCaps);
	if (record.buffer != NULL) {
		record.buffer->lpVtbl->SetCurrentPosition(record.buffer, 0);
		strncpy(record.name, soundName, sizeof(record.name));
		record.name[sizeof(record.name) - 1] = '\0';
		strncpy(record.fileName, fileName, sizeof(record.fileName));
		record.fileName[191] = '\0';
		record.priority = 0;
		FrontendSound_InsertSortedBuffer(&record);
		if (wasBackBufferLocked != 0) {
			g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
		}
		return record.buffer != NULL;
	}
	if (wasBackBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return 0;
}

/* Calls FrontendSound_UnloadBufferByName with the name in each of the 128
 * buffer records, from the last to the first; going down, it reaches every
 * loaded sound. FrontendDisplay_ReleaseSurfacesForFlight is its only caller. */
// FUNCTION: XVT 0x4DE4D0
void FrontendSound_UnloadAllBuffers(void)
{
	int bufferIndex;

	bufferIndex = 127;
	do {
		FrontendSound_UnloadBufferByName(
			g_frontState.frontendSoundBuffers[bufferIndex].name);
	} while (--bufferIndex >= 0);
}

/* Stops the named sound's voices with FrontendSound_StopOldestVoiceByName until
 * it returns other than 1, releases its buffer and removes its record with
 * FrontendSound_RemoveBufferRecord. Returns 1, or 0 when the name is empty or
 * not loaded. Unlocks the front end's back buffer around its DirectSound calls
 * and, when it was locked, locks it again into g_drawSurfacePtr.
 * FrontendSound_UnloadAllBuffers and FrontendSound_UnloadList call it. */
// FUNCTION: XVT 0x4DE4F0
int FrontendSound_UnloadBufferByName(const char *soundName)
{
	int bufferIndex;
	int wasBackBufferLocked;

	if (*soundName == '\0') {
		return 0;
	}
	bufferIndex = FrontendSound_FindBufferByName(soundName);
	if (bufferIndex == -1) {
		return 0;
	}

	wasBackBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	while (FrontendSound_StopOldestVoiceByName(
		       g_frontState.frontendSoundBuffers[bufferIndex].name) ==
	       1) {
	}
	g_frontState.frontendSoundBuffers[bufferIndex].buffer->lpVtbl->Release(
		g_frontState.frontendSoundBuffers[bufferIndex].buffer);
	FrontendSound_RemoveBufferRecord(bufferIndex);
	if (wasBackBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return 1;
}

/* Plays a loaded front-end sound on a new duplicate of its buffer and records
 * it in one of the 12 voices of g_frontState.frontendSoundVoices. Returns 0
 * when frontendDirectSound is NULL or the name is empty or not loaded. With all
 * 12 voices in use it frees the first voice whose GetStatus fails or which is
 * neither playing (0x1) nor looping (0x4), releasing its buffer. With all 12
 * still playing it takes the first voice whose sound has the lowest priority
 * under priority, stops and releases its buffer and uses it; with none under
 * priority it returns 0, unless allowRestartExisting is set and a voice plays
 * this sound: then it rewinds the first such to 0, gives it the next play
 * serial and returns 1. With fewer than 12 in use it takes the first free
 * voice. It duplicates the sound's buffer, returning 0 when the duplicate is
 * NULL (the pointer is not cleared before the call), rewinds it, sets its
 * volume to 400 * (5 * v - 635) / 127 hundredths of a decibel and its pan to
 * 400 * (5 * p - 315) / 63, v and p being volume0To127 and pan0To127 clamped to
 * 0 to 127, and plays it, looping when loop is 1. When Play succeeds it fills
 * the voice (record index, buffer, the next serial from
 * frontendSoundPlaySerial), adds 1 to frontendActiveVoiceCount and returns 1.
 * When Play returns DSERR_BUFFERLOST (0x88780096) it refills the sound's buffer
 * from its file with DirectSound_ReloadWaveBuffer and plays again, filling the
 * voice the same way on success; that path returns 0 whatever happens. Any
 * other failure of Play returns the HRESULT, a nonzero value, and the duplicate
 * is not released. Unlocks the front end's back buffer around its DirectSound
 * calls and, when it was locked, locks it again into g_drawSurfacePtr. */
// FUNCTION: XVT 0x4DE5A0
int FrontendSound_PlayUISound(const char *soundName, int allowRestartExisting,
			      int loop, int priority, int volume0To127,
			      int pan0To127)
{
	int bufferIndex;
	int voiceIndex;
	int scanIndex;
	int restartVoiceIndex;
	int candidateVoiceIndex;
	int candidatePriority;
	int currentBufferIndex;
	int wasBackBufferLocked;
	uint32_t status;
	IDirectSoundBuffer *duplicateBuffer;
	struct FrontendSoundVoice *voice;
	int clampedValue;
	HRESULT playResult;
	int result;

	if (g_frontState.frontendDirectSound == NULL) {
		return 0;
	}
	if (*soundName == '\0') {
		return 0;
	}
	bufferIndex = FrontendSound_FindBufferByName(soundName);
	if (bufferIndex == -1) {
		return 0;
	}

	wasBackBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	if (g_frontState.frontendActiveVoiceCount == 12) {
		for (voiceIndex = 0; voiceIndex < 12; ++voiceIndex) {
			voice = &g_frontState.frontendSoundVoices[voiceIndex];
			if (voice->bufferIndex != -1 &&
			    (voice->buffer->lpVtbl->GetStatus(voice->buffer,
							      &status) != 0 ||
			     ((status & 1) == 0 && (status & 4) == 0))) {
				g_frontState.frontendSoundVoices[voiceIndex]
					.buffer->lpVtbl->Release(
						g_frontState
							.frontendSoundVoices
								[voiceIndex]
							.buffer);
				g_frontState.frontendSoundVoices[voiceIndex]
					.bufferIndex = -1;
				g_frontState.frontendSoundVoices[voiceIndex]
					.buffer = NULL;
				--g_frontState.frontendActiveVoiceCount;
				break;
			}
		}
		if (voiceIndex == 12) {
			candidateVoiceIndex = 12;
			candidatePriority = priority;
			scanIndex = 0;
			do {
				currentBufferIndex =
					g_frontState
						.frontendSoundVoices[scanIndex]
						.bufferIndex;
				if (candidatePriority >
				    g_frontState
					    .frontendSoundBuffers
						    [currentBufferIndex]
					    .priority) {
					candidateVoiceIndex = scanIndex;
					candidatePriority =
						g_frontState
							.frontendSoundBuffers
								[currentBufferIndex]
							.priority;
				}
				++scanIndex;
			} while (scanIndex < 12);

			voiceIndex = candidateVoiceIndex;
			if (candidateVoiceIndex != 12) {
				g_frontState.frontendSoundVoices[voiceIndex]
					.buffer->lpVtbl->Stop(
						g_frontState
							.frontendSoundVoices
								[voiceIndex]
							.buffer);
				g_frontState.frontendSoundVoices[voiceIndex]
					.buffer->lpVtbl->Release(
						g_frontState
							.frontendSoundVoices
								[voiceIndex]
							.buffer);
				g_frontState.frontendSoundVoices[voiceIndex]
					.bufferIndex = -1;
				g_frontState.frontendSoundVoices[voiceIndex]
					.buffer = NULL;
				--g_frontState.frontendActiveVoiceCount;
			} else {
				if (allowRestartExisting != 0) {
					for (restartVoiceIndex = 0;
					     restartVoiceIndex < 12;
					     ++restartVoiceIndex) {
						if (g_frontState
							    .frontendSoundVoices
								    [restartVoiceIndex]
							    .bufferIndex ==
						    bufferIndex) {
							g_frontState
								.frontendSoundVoices
									[restartVoiceIndex]
								.buffer->lpVtbl
								->SetCurrentPosition(
									g_frontState
										.frontendSoundVoices
											[restartVoiceIndex]
										.buffer,
									0);
							g_frontState
								.frontendSoundVoices
									[restartVoiceIndex]
								.playSerial =
								g_frontState
									.frontendSoundPlaySerial++;
							if (wasBackBufferLocked !=
							    0) {
								g_drawSurfacePtr =
									FrontendDisplay_LockBackBuffer();
							}
							return 1;
						}
					}
				}
				if (wasBackBufferLocked != 0) {
					g_drawSurfacePtr =
						FrontendDisplay_LockBackBuffer();
				}
				return 0;
			}
		}
	} else {
		voiceIndex = 0;
		voice = g_frontState.frontendSoundVoices;
		while (voiceIndex < 12 && voice->bufferIndex != -1) {
			++voice;
			++voiceIndex;
		}
	}

	g_frontState.frontendDirectSound->lpVtbl->DuplicateSoundBuffer(
		g_frontState.frontendDirectSound,
		g_frontState.frontendSoundBuffers[bufferIndex].buffer,
		&duplicateBuffer);
	if (duplicateBuffer == NULL) {
		if (wasBackBufferLocked != 0) {
			g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
		}
		return 0;
	}

	duplicateBuffer->lpVtbl->SetCurrentPosition(duplicateBuffer, 0);
	clampedValue = volume0To127;
	if (clampedValue > 127) {
		clampedValue = 127;
	} else if (clampedValue < 0) {
		clampedValue = 0;
	}
	duplicateBuffer->lpVtbl->SetVolume(
		duplicateBuffer, 400 * (5 * clampedValue - 635) / 127);
	clampedValue = pan0To127;
	if (clampedValue > 127) {
		clampedValue = 127;
	}
	if (clampedValue < 0) {
		clampedValue = 0;
	}
	duplicateBuffer->lpVtbl->SetPan(duplicateBuffer,
					400 * (5 * clampedValue - 315) / 63);
	playResult =
		duplicateBuffer->lpVtbl->Play(duplicateBuffer, 0, 0, loop == 1);
	result = playResult;
	if (playResult == (HRESULT)0x88780096u) {
		result = DirectSound_ReloadWaveBuffer(
			g_frontState.frontendSoundBuffers[bufferIndex].buffer,
			g_frontState.frontendSoundBuffers[bufferIndex]
				.fileName);
		if (result == 1) {
			duplicateBuffer->lpVtbl->SetCurrentPosition(
				duplicateBuffer, 0);
			result = duplicateBuffer->lpVtbl->Play(duplicateBuffer,
							       0, 0, loop == 1);
			if (result == 0) {
				g_frontState.frontendSoundVoices[voiceIndex]
					.bufferIndex = bufferIndex;
				g_frontState.frontendSoundVoices[voiceIndex]
					.buffer = duplicateBuffer;
				g_frontState.frontendSoundVoices[voiceIndex]
					.playSerial =
					g_frontState.frontendSoundPlaySerial++;
				++g_frontState.frontendActiveVoiceCount;
			} else {
				result = 0;
			}
		}
	} else if (playResult == 0) {
		g_frontState.frontendSoundVoices[voiceIndex].bufferIndex =
			bufferIndex;
		g_frontState.frontendSoundVoices[voiceIndex].buffer =
			duplicateBuffer;
		g_frontState.frontendSoundVoices[voiceIndex].playSerial =
			g_frontState.frontendSoundPlaySerial++;
		++g_frontState.frontendActiveVoiceCount;
		result = 1;
	}
	if (wasBackBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return result;
}

/* Stops the oldest voice of the named sound, the one with the lowest play
 * serial: stops and releases its buffer, frees the voice and lowers
 * g_frontState.frontendActiveVoiceCount. Returns 1 when Stop succeeded, else 0;
 * returns 0 with nothing stopped when frontendDirectSound is NULL, the name is
 * empty or not loaded, no voice holds the sound, or the voice's buffer is NULL.
 * Unlocks the front end's back buffer around its DirectSound calls and, when it
 * was locked, locks it again into g_drawSurfacePtr.
 * FrontendSound_UnloadBufferByName and FrontendSound_StopAllVoices call it. */
// FUNCTION: XVT 0x4DE9A0
int FrontendSound_StopOldestVoiceByName(const char *name)
{
	int bufferIndex;
	int oldestVoiceIndex;
	int oldestSerial;
	int voiceIndex;
	struct FrontendSoundVoice *voice;
	IDirectSoundBuffer *buffer;
	int wasBackBufferLocked;
	HRESULT stopResult;

	if (g_frontState.frontendDirectSound == NULL) {
		return 0;
	}
	if (*name == '\0') {
		return 0;
	}
	bufferIndex = FrontendSound_FindBufferByName(name);
	if (bufferIndex == -1) {
		return 0;
	}

	oldestVoiceIndex = -1;
	oldestSerial = g_frontState.frontendSoundPlaySerial + 1;
	voiceIndex = 0;
	voice = g_frontState.frontendSoundVoices;
	do {
		if (voice->bufferIndex == bufferIndex &&
		    voice->playSerial < oldestSerial) {
			oldestSerial = voice->playSerial;
			oldestVoiceIndex = voiceIndex;
		}
		++voice;
		++voiceIndex;
	} while (voiceIndex < 12);
	if (oldestVoiceIndex == -1) {
		return 0;
	}

	buffer = g_frontState.frontendSoundVoices[oldestVoiceIndex].buffer;
	if (buffer == NULL) {
		return 0;
	}
	wasBackBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	stopResult = buffer->lpVtbl->Stop(buffer);
	buffer->lpVtbl->Release(buffer);
	g_frontState.frontendSoundVoices[oldestVoiceIndex].buffer = NULL;
	g_frontState.frontendSoundVoices[oldestVoiceIndex].bufferIndex = -1;
	--g_frontState.frontendActiveVoiceCount;
	if (wasBackBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return stopResult >= 0;
}

/* Calls FrontendSound_StopOldestVoiceByName for the sound of each voice in use,
 * in voice order, and returns 1 when every call returned 1, else 0. Each call
 * stops that sound's oldest voice, not necessarily that voice, and no voice is
 * looked at twice, so when a sound's older voice sits later, the earlier voice
 * keeps playing. Nothing calls this. */
// FUNCTION: XVT 0x4DEA90
int FrontendSound_StopAllVoices(void)
{
	int allStopped;
	int voiceIndex;
	int bufferIndex;

	allStopped = 1;
	for (voiceIndex = 0; voiceIndex < 12; ++voiceIndex) {
		bufferIndex = g_frontState.frontendSoundVoices[voiceIndex]
				      .bufferIndex;
		if (bufferIndex != -1) {
			allStopped &= FrontendSound_StopOldestVoiceByName(
				g_frontState.frontendSoundBuffers[bufferIndex]
					.name);
		}
	}
	return allStopped;
}

/* Sets the primary buffer's volume to 400 * (5 * v - 635) / 127 hundredths of a
 * decibel, v being volume0To127 clamped to 0 to 127. Returns 1 when SetVolume
 * succeeds; 0 when it fails, when there is no primary buffer, or when
 * g_frontState.frontendDirectSound is NULL. Unlocks the front end's back buffer
 * around its DirectSound calls and, when it was locked, locks it again into
 * g_drawSurfacePtr. Nothing calls this. */
// FUNCTION: XVT 0x4DEAD0
int FrontendSound_SetPrimaryVolume(int volume0To127)
{
	HRESULT setResult;
	int wasBackBufferLocked;
	int clampedVolume;

	if (g_frontState.frontendDirectSound == NULL) {
		return 0;
	}

	wasBackBufferLocked = g_frontState.backBufferLocked;
	setResult = 1;
	FrontendDisplay_UnlockBackBuffer();
	if (g_frontState.frontendPrimarySoundBuffer != NULL) {
		clampedVolume = volume0To127;
		if (clampedVolume > 127) {
			clampedVolume = 127;
		} else if (clampedVolume < 0) {
			clampedVolume = 0;
		}
		setResult =
			g_frontState.frontendPrimarySoundBuffer->lpVtbl
				->SetVolume(
					g_frontState.frontendPrimarySoundBuffer,
					400 * (5 * clampedVolume - 635) / 127);
	}
	if (wasBackBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return setResult == 0;
}

/* Returns the primary buffer's volume on the game's scale,
 * 127 * v / 2000 + 127, v in hundredths of a decibel; 0 when
 * g_frontState.frontendDirectSound is NULL or GetVolume fails,
 * and on that failure it leaves the back buffer unlocked. Does
 * not check that the primary buffer exists. Otherwise it locks
 * the back buffer again into g_drawSurfacePtr when it was locked.
 * Nothing calls this. */
// FUNCTION: XVT 0x4DEB50
int FrontendSound_GetPrimaryVolume(void)
{
	int wasBackBufferLocked;
	int32_t directSoundVolume;

	if (g_frontState.frontendDirectSound == NULL) {
		return 0;
	}

	wasBackBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	if (g_frontState.frontendPrimarySoundBuffer->lpVtbl->GetVolume(
		    g_frontState.frontendPrimarySoundBuffer,
		    &directSoundVolume) != 0) {
		return 0;
	}

	/* Convert the DirectSound attenuation back to the game's volume scale, where 127 is full volume. */
	directSoundVolume = 127 * directSoundVolume / 2000 + 127;
	if (wasBackBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return directSoundVolume;
}

/* Sets the volume of the named sound's newest voice, the one with the highest
 * play serial, to 400 * (5 * v - 635) / 127 hundredths of a decibel, v being
 * volume0To127 clamped to 0 to 127. Returns 1 when SetVolume succeeds, else 0;
 * 0 also when frontendDirectSound is NULL, the name is empty or not loaded, or
 * no voice holds it. Unlocks the front end's back buffer around its DirectSound
 * calls and, when it was locked, locks it again into g_drawSurfacePtr. Nothing
 * calls this. */
// FUNCTION: XVT 0x4DEBC0
int FrontendSound_SetNewestVoiceVolumeByName(const char *name, int volume0To127)
{
	int bufferIndex;
	int newestSerial;
	struct FrontendSoundVoice *voice;
	int voiceIndex;
	int newestVoiceIndex;
	int wasBackBufferLocked;
	int clampedVolume;
	int directSoundVolume;
	HRESULT setResult;

	if (g_frontState.frontendDirectSound == NULL) {
		return 0;
	}
	if (*name == '\0') {
		return 0;
	}
	bufferIndex = FrontendSound_FindBufferByName(name);
	if (bufferIndex == -1) {
		return 0;
	}

	newestSerial = -1;
	voiceIndex = 0;
	newestVoiceIndex = -1;
	voice = g_frontState.frontendSoundVoices;
	do {
		if (voice->bufferIndex == bufferIndex &&
		    newestSerial < voice->playSerial) {
			newestSerial = voice->playSerial;
			newestVoiceIndex = voiceIndex;
		}
		voice++;
		voiceIndex++;
	} while (voiceIndex < 12);
	if (newestVoiceIndex == -1) {
		return 0;
	}

	wasBackBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	clampedVolume = volume0To127;
	if (clampedVolume > 127) {
		clampedVolume = 127;
	} else if (clampedVolume < 0) {
		clampedVolume = 0;
	}
	directSoundVolume = 400 * (5 * clampedVolume - 635) / 127;
	setResult =
		g_frontState.frontendSoundVoices[newestVoiceIndex]
			.buffer->lpVtbl->SetVolume(
				g_frontState
					.frontendSoundVoices[newestVoiceIndex]
					.buffer,
				directSoundVolume);
	if (wasBackBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return setResult == 0;
}

/* Returns the volume of the named sound's newest voice on the game's scale,
 * 127 * v / 2000 + 127; 0 when frontendDirectSound is NULL, the name is empty
 * or not loaded, no voice holds it, or GetVolume fails, and on that failure it
 * leaves the back buffer unlocked. Otherwise it locks the back buffer again
 * into g_drawSurfacePtr when it was locked. Nothing calls this. */
// FUNCTION: XVT 0x4DECA0
int FrontendSound_GetNewestVoiceVolumeByName(const char *name)
{
	int bufferIndex;
	int newestSerial;
	struct FrontendSoundVoice *voice;
	int voiceIndex;
	int newestVoiceIndex;
	int wasBackBufferLocked;
	int32_t directSoundVolume;

	if (g_frontState.frontendDirectSound == NULL) {
		return 0;
	}
	if (*name == '\0') {
		return 0;
	}
	bufferIndex = FrontendSound_FindBufferByName(name);
	if (bufferIndex == -1) {
		return 0;
	}

	newestSerial = -1;
	voiceIndex = 0;
	newestVoiceIndex = -1;
	voice = g_frontState.frontendSoundVoices;
	do {
		if (voice->bufferIndex == bufferIndex &&
		    newestSerial < voice->playSerial) {
			newestSerial = voice->playSerial;
			newestVoiceIndex = voiceIndex;
		}
		voice++;
		voiceIndex++;
	} while (voiceIndex < 12);
	if (newestVoiceIndex == -1) {
		return 0;
	}

	wasBackBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	if (g_frontState.frontendSoundVoices[newestVoiceIndex]
		    .buffer->lpVtbl->GetVolume(
			    g_frontState.frontendSoundVoices[newestVoiceIndex]
				    .buffer,
			    &directSoundVolume) != 0) {
		return 0;
	}
	/* Convert the DirectSound attenuation back to the game's volume scale, where 127 is full volume. */
	directSoundVolume = 127 * directSoundVolume / 2000 + 127;
	if (wasBackBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return directSoundVolume;
}

/* Sets the pan of the named sound's newest voice to 400 * (5 * p - 315) / 63
 * hundredths of a decibel, p being pan0To127 clamped to 0 to 127. Returns 1
 * when SetPan succeeds, else 0; 0 also when frontendDirectSound is NULL, the
 * name is empty or not loaded, or no voice holds it. Unlocks the front end's
 * back buffer around its DirectSound calls and, when it was locked, locks it
 * again into g_drawSurfacePtr. Nothing calls this. */
// FUNCTION: XVT 0x4DED80
int FrontendSound_SetNewestVoicePanByName(const char *name, int pan0To127)
{
	int bufferIndex;
	int newestSerial;
	int voiceIndex;
	int newestVoiceIndex;
	int wasBackBufferLocked;
	int clampedPan;
	int directSoundPan;
	HRESULT setResult;

	if (g_frontState.frontendDirectSound == NULL) {
		return 0;
	}
	if (*name == '\0') {
		return 0;
	}
	bufferIndex = FrontendSound_FindBufferByName(name);
	if (bufferIndex == -1) {
		return 0;
	}

	newestSerial = -1;
	voiceIndex = 0;
	newestVoiceIndex = -1;
	do {
		if (g_frontState.frontendSoundVoices[voiceIndex].bufferIndex ==
			    bufferIndex &&
		    g_frontState.frontendSoundVoices[voiceIndex].playSerial >
			    newestSerial) {
			newestSerial =
				g_frontState.frontendSoundVoices[voiceIndex]
					.playSerial;
			newestVoiceIndex = voiceIndex;
		}
		voiceIndex++;
	} while (voiceIndex < 12);
	if (newestVoiceIndex == -1) {
		return 0;
	}

	wasBackBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	clampedPan = pan0To127;
	if (clampedPan > 127) {
		clampedPan = 127;
	}
	if (clampedPan < 0) {
		clampedPan = 0;
	}
	directSoundPan = 400 * (5 * clampedPan - 315) / 63;
	setResult =
		g_frontState.frontendSoundVoices[newestVoiceIndex]
			.buffer->lpVtbl->SetPan(
				g_frontState
					.frontendSoundVoices[newestVoiceIndex]
					.buffer,
				directSoundPan);
	if (wasBackBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return setResult == 0;
}

/* Returns the pan of the named sound's newest voice on the game's scale,
 * 63 * pan / 10000 + 63; 0 when frontendDirectSound is NULL, the name is empty
 * or not loaded, no voice holds it, or GetPan fails. Unlocks the front end's
 * back buffer around its DirectSound calls and, when it was locked, locks it
 * again into g_drawSurfacePtr. Nothing calls this. */
// FUNCTION: XVT 0x4DEE60
int FrontendSound_GetNewestVoicePanByName(const char *name)
{
	int bufferIndex;
	int newestSerial;
	int voiceIndex;
	int newestVoiceIndex;
	struct FrontendSoundVoice *voice;
	int wasBackBufferLocked;
	int32_t directSoundPan;

	if (g_frontState.frontendDirectSound == NULL) {
		return 0;
	}
	if (*name == '\0') {
		return 0;
	}
	bufferIndex = FrontendSound_FindBufferByName(name);
	if (bufferIndex == -1) {
		return 0;
	}

	newestSerial = -1;
	voiceIndex = 0;
	newestVoiceIndex = -1;
	voice = g_frontState.frontendSoundVoices;
	do {
		if (voice->bufferIndex == bufferIndex &&
		    voice->playSerial > newestSerial) {
			newestSerial = voice->playSerial;
			newestVoiceIndex = voiceIndex;
		}
		++voice;
		++voiceIndex;
	} while (voiceIndex < 12);
	if (newestVoiceIndex == -1) {
		return 0;
	}

	wasBackBufferLocked = g_frontState.backBufferLocked;
	FrontendDisplay_UnlockBackBuffer();
	if (g_frontState.frontendSoundVoices[newestVoiceIndex]
		    .buffer->lpVtbl->GetPan(
			    g_frontState.frontendSoundVoices[newestVoiceIndex]
				    .buffer,
			    &directSoundPan) != 0) {
		if (wasBackBufferLocked != 0) {
			g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
		}
		return 0;
	}
	/* Convert the DirectSound pan back to the game's pan scale, where 63 is center. */
	directSoundPan = 63 * directSoundPan / 10000 + 63;
	if (wasBackBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return directSoundPan;
}

/* Sets the named sound's priority to priority0To255 clamped to 0 to 255 and
 * returns 1; returns 0 when the name is not loaded. Does not check
 * frontendDirectSound. Nothing calls this, so every sound keeps priority 0. */
// FUNCTION: XVT 0x4DEF50
int FrontendSound_SetBufferPriorityByName(const char *name, int priority0To255)
{
	int bufferIndex;
	int clampedPriority;

	bufferIndex = FrontendSound_FindBufferByName(name);
	if (bufferIndex == -1) {
		return 0;
	}

	clampedPriority = priority0To255;
	if (clampedPriority > 255) {
		clampedPriority = 255;
	}
	if (clampedPriority < 0) {
		clampedPriority = 0;
	}
	g_frontState.frontendSoundBuffers[bufferIndex].priority =
		clampedPriority;
	return 1;
}

/* Returns the named sound's priority, or 0 when the name is not loaded. Nothing
 * calls this. */
// FUNCTION: XVT 0x4DEFA0
int FrontendSound_GetBufferPriorityByName(const char *name)
{
	int bufferIndex;

	bufferIndex = FrontendSound_FindBufferByName(name);
	if (bufferIndex == -1) {
		return 0;
	}

	return g_frontState.frontendSoundBuffers[bufferIndex].priority;
}

/* Counts the voices holding the named sound whose buffer reports the 0x1
 * (playing) or 0x4 (looping) status bit; returns the count, 0 to 12, or 0 when
 * frontendDirectSound is NULL or the name is empty or not loaded. Unlocks the
 * front end's back buffer around its DirectSound calls and, when it was locked,
 * locks it again into g_drawSurfacePtr. Nothing calls this. */
// FUNCTION: XVT 0x4DEFD0
int FrontendSound_GetPlayingCount(const char *name)
{
	int bufferIndex;
	int voiceIndex;
	int wasBackBufferLocked;
	int playingCount;
	uint32_t status;

	if (g_frontState.frontendDirectSound == NULL) {
		return 0;
	}
	if (name[0] == '\0') {
		return 0;
	}

	bufferIndex = FrontendSound_FindBufferByName(name);
	if (bufferIndex == -1) {
		return 0;
	}

	voiceIndex = 0;
	wasBackBufferLocked = g_frontState.backBufferLocked;
	playingCount = 0;
	FrontendDisplay_UnlockBackBuffer();
	do {
		if (g_frontState.frontendSoundVoices[voiceIndex].bufferIndex ==
			    bufferIndex &&
		    g_frontState.frontendSoundVoices[voiceIndex]
				    .buffer->lpVtbl->GetStatus(
					    g_frontState
						    .frontendSoundVoices
							    [voiceIndex]
						    .buffer,
					    &status) == 0 &&
		    ((status & 1) != 0 || (status & 4) != 0)) {
			++playingCount;
		}
		++voiceIndex;
	} while (voiceIndex < 12);

	if (wasBackBufferLocked != 0) {
		g_drawSurfacePtr = FrontendDisplay_LockBackBuffer();
	}
	return playingCount;
}

/* Inserts a copy of *record into g_frontState.frontendSoundBuffers, keeping it
 * sorted by name (strncmp over 64 characters) with the new record after equal
 * names, adds 1 to frontendSoundBufferCount and adds 1 to each voice's
 * bufferIndex at or after the insertion point. Does not check that the table
 * has room; FrontendSound_LoadSoundFile checks the count under 128 first. */
// FUNCTION: XVT 0x4DF070
void FrontendSound_InsertSortedBuffer(
	const struct FrontendSoundBufferRecord *record)
{
	int insertionIndex;
	int shiftIndex;
	int remaining;
	int voiceIndex;
	int bufferIndex;
	struct FrontendSoundBufferRecord *destination;

	insertionIndex = 0;
	if (g_frontState.frontendSoundBufferCount > 0) {
		do {
			if (strncmp(record->name,
				    g_frontState
					    .frontendSoundBuffers
						    [insertionIndex]
					    .name,
				    64) < 0) {
				break;
			}
			++insertionIndex;
		} while (g_frontState.frontendSoundBufferCount >
			 insertionIndex);
	}
	if (g_frontState.frontendSoundBufferCount > insertionIndex) {
		shiftIndex = g_frontState.frontendSoundBufferCount;
		remaining =
			g_frontState.frontendSoundBufferCount - insertionIndex;
		do {
			destination =
				&g_frontState.frontendSoundBuffers[shiftIndex];
			--shiftIndex;
			memcpy(destination,
			       &g_frontState.frontendSoundBuffers[shiftIndex],
			       sizeof(struct FrontendSoundBufferRecord));
			--remaining;
		} while (remaining != 0);
	}
	memcpy(&g_frontState.frontendSoundBuffers[insertionIndex], record,
	       sizeof(struct FrontendSoundBufferRecord));
	++g_frontState.frontendSoundBufferCount;
	voiceIndex = 0;
	do {
		bufferIndex = g_frontState.frontendSoundVoices[voiceIndex]
				      .bufferIndex;
		if (insertionIndex <= bufferIndex) {
			g_frontState.frontendSoundVoices[voiceIndex]
				.bufferIndex = bufferIndex + 1;
		}
		++voiceIndex;
	} while (voiceIndex < 12);
}

/* Removes record bufferIndex from g_frontState.frontendSoundBuffers, moving the
 * later records up one, lowers frontendSoundBufferCount and lowers by 1 each
 * voice's bufferIndex above it. Does nothing for an index outside 0 to
 * frontendSoundBufferCount - 1. Leaves the old last record in place, does not
 * release the buffer and does not free voices that play it;
 * FrontendSound_UnloadBufferByName stops them first. */
// FUNCTION: XVT 0x4DF130
void FrontendSound_RemoveBufferRecord(int bufferIndex)
{
	int index;

	if (bufferIndex < 0 ||
	    bufferIndex >= g_frontState.frontendSoundBufferCount) {
		return;
	}

	for (index = bufferIndex;
	     index < g_frontState.frontendSoundBufferCount - 1; ++index) {
		g_frontState.frontendSoundBuffers[index] =
			g_frontState.frontendSoundBuffers[index + 1];
	}

	--g_frontState.frontendSoundBufferCount;
	for (index = 0; index < 12; ++index) {
		if (g_frontState.frontendSoundVoices[index].bufferIndex >
		    bufferIndex) {
			--g_frontState.frontendSoundVoices[index].bufferIndex;
		}
	}
}

/* Returns the index of the named sound among the frontendSoundBufferCount
 * loaded records, or -1, by FrontendSound_BinarySearchBufferByName. */
// FUNCTION: XVT 0x4DF1B0
int FrontendSound_FindBufferByName(const char *name)
{
	return FrontendSound_BinarySearchBufferByName(
		g_frontState.frontendSoundBuffers,
		g_frontState.frontendSoundBufferCount - 1, name);
}

/* Binary search for name in records[0] to records[lastIndex], which must be
 * sorted by name, comparing up to 64 characters with strncmp. Returns the index
 * found, or -1. FrontendSound_FindBufferByName is its only caller. */
// FUNCTION: XVT 0x4DF1D0
int FrontendSound_BinarySearchBufferByName(
	const struct FrontendSoundBufferRecord *records, int lastIndex,
	const char *name)
{
	int baseIndex;
	int searchLastIndex;
	int middle;
	int comparison;
	const struct FrontendSoundBufferRecord *middleRecord;

	baseIndex = 0;
	searchLastIndex = lastIndex;
	while (1) {
		if (searchLastIndex < 0) {
			return -1;
		}
		middle = searchLastIndex >> 1;
		middleRecord = &records[middle];
		comparison = strncmp(middleRecord->name, name,
				     sizeof(middleRecord->name));
		if (comparison == 0) {
			return middle + baseIndex;
		}
		if (searchLastIndex <= 0) {
			return -1;
		}
		if (comparison < 0) {
			baseIndex += middle + 1;
			searchLastIndex -= middle + 1;
			records = middleRecord + 1;
		} else {
			searchLastIndex = middle - 1;
		}
	}
}

/* Loads the front end's sounds from a list file: skips the first line, read
 * with File_Gets into 255 bytes, then reads pairs of words, a WAV file and a
 * sound name, and loads each with FrontendSound_LoadSound, ignoring its result.
 * Returns 1 at the end of the file, also when the first line cannot be read; 0
 * when the file does not open or a line does not hold two words. The original
 * build reads each word with %s into a 256-byte buffer without a limit; the
 * modern build stops at 255 characters. Every caller passes the front end's
 * sound list file. */
// FUNCTION: XVT 0x4DF700
int FrontendSound_LoadList(const char *fileName)
{
	XvtFile *stream;
	int fieldCount;
	char soundFileName[256];
	char soundName[256];

	stream = File_Open(fileName, "r");
	if (stream == NULL) {
		return 0;
	}
	if (File_Gets(soundFileName, 255, stream) == NULL) {
		File_Close(stream);
		return 1;
	}
	while (1) {
#ifdef XVT_MODERN
		fieldCount = File_Scanf(stream, "%255s %255s\n", soundFileName,
					soundName);
#else
		fieldCount =
			File_Scanf(stream, "%s %s\n", soundFileName, soundName);
#endif
		if (fieldCount == EOF) {
			File_Close(stream);
			return 1;
		}
		if (fieldCount != 2) {
			File_Close(stream);
			return 0;
		}
		FrontendSound_LoadSound(soundFileName, soundName);
	}
}

/* Reads a list file in FrontendSound_LoadList's form and unloads each named
 * sound with FrontendSound_UnloadBufferByName. Returns as
 * FrontendSound_LoadList does. Nothing calls this. */
// FUNCTION: XVT 0x4DF7C0
int FrontendSound_UnloadList(char *fileName)
{
	XvtFile *stream;
	int fieldCount;
	char ignoredFileName[256];
	char soundName[256];

	stream = File_Open(fileName, "r");
	if (stream == NULL) {
		return 0;
	}
	if (File_Gets(ignoredFileName, 255, stream) == NULL) {
		File_Close(stream);
		return 1;
	}
	while (1) {
#ifdef XVT_MODERN
		fieldCount = File_Scanf(stream, "%255s %255s\n",
					ignoredFileName, soundName);
#else
		fieldCount = File_Scanf(stream, "%s %s\n", ignoredFileName,
					soundName);
#endif
		if (fieldCount == EOF) {
			File_Close(stream);
			return 1;
		}
		if (fieldCount != 2) {
			File_Close(stream);
			return 0;
		}
		FrontendSound_UnloadBufferByName(soundName);
	}
}
