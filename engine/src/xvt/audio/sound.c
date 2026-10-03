#include "xvt/audio/sound.h"

#include "aeron/compat/dsound.h"
#include "xvt/audio/direct_sound.h"
#include "xvt/audio/fsfx.h"

#include <string.h>

/* The DirectSound object Sound_Init_Sound_Engine creates; NULL before that and
 * after Sound_Shutdown_Sound_Engine releases it. Most Sound_ functions return 0
 * at once while it is NULL. */
// GLOBAL: XVT 0xA10514
IDirectSound *g_directSound = 0;
/* The primary buffer Sound_Init_Sound_Engine creates. Only
 * Sound_GetPrimaryBufferVolume reads it, and nothing calls that;
 * Sound_Shutdown_Sound_Engine sets it to NULL without releasing it. */
// GLOBAL: XVT 0xA10518
IDirectSoundBuffer *g_soundPrimaryBuffer = 0;
/* The loaded effects: entries 0 to g_soundCount - 1, sorted by name for the
 * binary search in Sound_FindEffectByName. Entries past that are empty or stale
 * copies left by Sound_RemoveEffectDef. */
// GLOBAL: XVT 0xA1051C
struct SoundEffectDef g_soundDefs[1000] = {{0}};
/* The 8 slots for playing sounds, each a duplicate buffer of one effect; a free
 * slot has effectIndex -1. */
// GLOBAL: XVT 0xA604CC
struct ActiveSoundInstance g_activeSoundInstances[8] = {{0}};
/* Effects waiting for Sound_FlushQueuedEffects: entries 0 to
 * g_soundQueueCount - 1, highest priority first. Sound_QueueEffect keeps at
 * most 4; the fifth entry only takes the one an insert pushes out, which is
 * dropped. */
// GLOBAL: XVT 0xA6052C
struct SoundQueueEntry g_soundQueue[5] = {{0}};
/* Entries in g_soundQueue, 0 to 4. Sound_QueueEffect adds one, stopping at 4,
 * and Sound_FlushQueuedEffects sets 0. */
// GLOBAL: XVT 0xA606D0
int g_soundQueueCount = 0;
/* Effects loaded in g_soundDefs, 0 to 1000. Sound_InsertEffectDefSorted adds
 * one, Sound_RemoveEffectDef takes one off and Sound_Init_Sound_Engine sets 0;
 * Sound_Shutdown_Sound_Engine leaves it. */
// GLOBAL: XVT 0xA606D4
int g_soundCount = 0;
/* Slots in use in g_activeSoundInstances, 0 to 8. Sound_PlayEffectNow adds and
 * frees slots, Sound_StopOldestInstance frees them and Sound_Init_Sound_Engine
 * sets 0; Sound_Shutdown_Sound_Engine clears the slots but leaves the count. */
// GLOBAL: XVT 0xA606D8
int g_activeSoundCount = 0;
/* Sequence number the next started or restarted sound takes; each one adds 1.
 * The lowest sequence in a slot is the oldest sound. Sound_Init_Sound_Engine
 * and Sound_Shutdown_Sound_Engine set 0. */
// GLOBAL: XVT 0xA606DC
unsigned int g_nextSoundInstanceSeq = 0;

/* Starts DirectSound for flight. Returns 1 at once when g_directSound is
 * already set. Otherwise it clears the 8 slots of g_activeSoundInstances,
 * g_activeSoundCount, g_soundCount, g_nextSoundInstanceSeq and the buffer and
 * name of every g_soundDefs entry, creates g_directSound with DirectSoundCreate
 * on the default device, sets cooperative level 2 (DSSCL_PRIORITY) for hwnd and
 * creates g_soundPrimaryBuffer. Returns 1, or 0 when a step fails, after
 * calling Sound_Shutdown_Sound_Engine when the object exists. It sets no format
 * on the primary buffer: the format it clears is never used. Flight_Main calls
 * it in the original build, XvtFlightEntry_CreateDevices in the modern one. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x42CD50
int Sound_Init_Sound_Engine(void *hwnd)
{
	int instanceIndex;
	int effectIndex;
	DSBUFFERDESC primaryBufferDesc;
	WAVEFORMATEX primaryFormat;
	HRESULT result;

	if (g_directSound != NULL) {
		return 1;
	}
	for (instanceIndex = 0; instanceIndex < 8; ++instanceIndex) {
		g_activeSoundInstances[instanceIndex].effectIndex = -1;
		g_activeSoundInstances[instanceIndex].sequence = 0;
		g_activeSoundInstances[instanceIndex].buffer = NULL;
	}
	g_activeSoundCount = 0;
	g_soundCount = 0;
	g_nextSoundInstanceSeq = 0;
	for (effectIndex = 0; effectIndex < 1000; ++effectIndex) {
		g_soundDefs[effectIndex].buffer = NULL;
		g_soundDefs[effectIndex].name[0] = '\0';
	}

	if (DirectSoundCreate(NULL, (void **)&g_directSound, NULL) != 0) {
		return 0;
	}
	if (g_directSound->lpVtbl->SetCooperativeLevel(g_directSound, hwnd,
						       2) != 0) {
		Sound_Shutdown_Sound_Engine();
		return 0;
	}

	memset(&primaryBufferDesc, 0, sizeof(primaryBufferDesc));
	primaryBufferDesc.dwSize = 20;
	primaryBufferDesc.dwFlags = DSBCAPS_PRIMARYBUFFER;
	result = g_directSound->lpVtbl->CreateSoundBuffer(
		g_directSound, &primaryBufferDesc, &g_soundPrimaryBuffer, NULL);
	if (result != 0) {
		Sound_Shutdown_Sound_Engine();
		return 0;
	}
	memset(&primaryFormat, 0, sizeof(primaryFormat));
	return 1;
}

/* Returns 1 at once when g_directSound is NULL. Otherwise it unloads the
 * effects with Sound_UnloadAllEffects, releases g_directSound and sets it to
 * NULL, clears the buffer and name of every g_soundDefs entry and the 8
 * instance slots, sets g_soundPrimaryBuffer to NULL without releasing it and
 * g_nextSoundInstanceSeq to 0, and returns 1. It does not set g_soundCount or
 * g_activeSoundCount back to 0. Flight_Main calls it in the original build,
 * XvtFlightEntry_Cleanup in the modern one. */
// FUNCTION: XVT 0x42CE60
int Sound_Shutdown_Sound_Engine(void)
{
	int effectIndex;
	int instanceIndex;

	if (g_directSound == NULL) {
		return 1;
	}
	Sound_UnloadAllEffects();
	g_directSound->lpVtbl->Release(g_directSound);
	g_directSound = NULL;
	for (effectIndex = 0; effectIndex < 1000; ++effectIndex) {
		g_soundDefs[effectIndex].buffer = NULL;
		g_soundDefs[effectIndex].name[0] = '\0';
	}
	for (instanceIndex = 0; instanceIndex < 8; ++instanceIndex) {
		g_activeSoundInstances[instanceIndex].effectIndex = -1;
		g_activeSoundInstances[instanceIndex].sequence = 0;
		g_activeSoundInstances[instanceIndex].buffer = NULL;
	}
	g_soundPrimaryBuffer = NULL;
	g_nextSoundInstanceSeq = 0;
	return 1;
}

/* Calls Sound_LoadEffectEx with omitSoftwareAndFrequencyCaps 0, so the buffer
 * gets frequency control, and returns its result. fsfx_LoadSfxList is its only
 * caller. */
// FUNCTION: XVT 0x42CEE0
int Sound_LoadEffect(const char *fileName, const char *name)
{
	return Sound_LoadEffectEx(fileName, name, 0);
}

/* Loads a WAV file as a named effect: DirectSound_LoadWaveBuffer makes its
 * buffer, which it rewinds to 0, and Sound_InsertEffectDefSorted adds the entry
 * with the name (up to 63 characters), the file name (up to 191) and
 * currentPriority 0. Returns 1, or 0 when either string is empty, 1000 effects
 * are loaded, g_directSound is NULL, the name is already loaded or the buffer
 * fails to load. Only Sound_LoadEffect calls it, passing 0. */
// FUNCTION: XVT 0x42CF00
int Sound_LoadEffectEx(const char *fileName, const char *name,
		       int omitSoftwareAndFrequencyCaps)
{
	struct SoundEffectDef effect;

	if (*fileName == '\0') {
		return 0;
	}
	if (*name == '\0') {
		return 0;
	}
	if (g_soundCount >= 1000) {
		return 0;
	}
	if (g_directSound == NULL) {
		return 0;
	}
	if (Sound_FindLoadedEffectByName(name) != -1) {
		return 0;
	}
	effect.buffer = DirectSound_LoadWaveBuffer(
		g_directSound, fileName, omitSoftwareAndFrequencyCaps);
	if (effect.buffer != NULL) {
		effect.buffer->lpVtbl->SetCurrentPosition(effect.buffer, 0);
		strncpy(effect.name, name, sizeof(effect.name));
		effect.name[63] = '\0';
		strncpy(effect.fileName, fileName, sizeof(effect.fileName));
		effect.fileName[191] = '\0';
		effect.currentPriority = 0;
		Sound_InsertEffectDefSorted(&effect);
		return effect.buffer != NULL;
	}
	return 0;
}

/* Calls Sound_UnloadEffectByName with the name in each of the 1000 g_soundDefs
 * entries in index order. Each unload moves the later entries up one, so the
 * loop skips names: with n effects loaded it leaves (n - 1) / 2 of them loaded,
 * rounded down, buffers included. Sound_Shutdown_Sound_Engine and
 * fsfx_UnloadAllEffects_Thunk call it. */
// FUNCTION: XVT 0x42D020
void Sound_UnloadAllEffects(void)
{
	int effectIndex;

	effectIndex = 0;
	do {
		Sound_UnloadEffectByName(g_soundDefs[effectIndex].name);
		++effectIndex;
	} while (effectIndex < 1000);
}

/* Stops the named effect's sounds with Sound_StopOldestInstance until it
 * returns other than 1, releases the effect's buffer and removes its entry with
 * Sound_RemoveEffectDef. Returns 1, or 0 when the name is empty or not
 * loaded. */
// FUNCTION: XVT 0x42D040
int Sound_UnloadEffectByName(const char *name)
{
	int effectIndex;

	if (*name == '\0') {
		return 0;
	}
	effectIndex = Sound_FindLoadedEffectByName(name);
	if (effectIndex == -1) {
		return 0;
	}
	while (Sound_StopOldestInstance(g_soundDefs[effectIndex].name) == 1) {
	}
	g_soundDefs[effectIndex].buffer->lpVtbl->Release(
		g_soundDefs[effectIndex].buffer);
	Sound_RemoveEffectDef(effectIndex);
	return 1;
}

/* Plays every queued effect with Sound_PlayEffectNow in queue order, highest
 * priority first, and sets g_soundQueueCount to 0. The original build calls it
 * from Flight_RunMissionLoop and FlightSync_ApplyWorldMessagePacket, the modern
 * one from XvtFlightFrame_Render, XvtFlightFrame_Advance and
 * XvtFlightFrame_Confirm. */
// FUNCTION: XVT 0x42D0D0
void Sound_FlushQueuedEffects(void)
{
	int queueIndex;

	queueIndex = 0;
	if (g_soundQueueCount > 0) {
		do {
			Sound_PlayEffectNow(
				g_soundQueue[queueIndex].name,
				g_soundQueue[queueIndex].allowRestartExisting,
				g_soundQueue[queueIndex].loop,
				g_soundQueue[queueIndex].priority,
				g_soundQueue[queueIndex].volume,
				g_soundQueue[queueIndex].pan);
			++queueIndex;
		} while (queueIndex < g_soundQueueCount);
	}
	g_soundQueueCount = 0;
}

#ifndef XVT_MODERN
#pragma function(memcpy)
#endif
/* Queues a loaded effect for the next Sound_FlushQueuedEffects, keeping
 * g_soundQueue ordered by priority, highest first, the new entry after those of
 * equal or higher priority. Returns 0 when g_directSound is NULL, the name is
 * empty or not loaded, or 4 entries wait, none of lower priority. Otherwise it
 * moves the later entries down one, writes the entry (the name by strncpy of 64
 * characters, not terminated for a name of 64 or more) and returns 1;
 * g_soundQueueCount grows by 1 but stops at 4, so an insert among 4 entries
 * drops the last. The modern build moves the entries with memmove, the original
 * with memcpy. */
// FUNCTION: XVT 0x42D120
int Sound_QueueEffect(const char *soundName, int allowRestartExisting, int loop,
		      int priority, int volume, int pan)
{
	int queueIndex;
	int queuedPriority;
	int queueCount;
	const char *name;
	IDirectSound *directSound;
	struct SoundQueueEntry *queueEntry;

	name = soundName;
	directSound = g_directSound;
	if (directSound == NULL) {
		return 0;
	}
	if (*name == '\0') {
		return 0;
	}
	if (Sound_FindLoadedEffectByName(name) == -1) {
		return 0;
	}

	queueIndex = 0;
	if (g_soundQueueCount > queueIndex) {
		queuedPriority = priority;
		do {
			if (g_soundQueue[queueIndex].priority < priority) {
				break;
			}
			++queueIndex;
		} while (g_soundQueueCount > queueIndex);
	} else {
		queuedPriority = priority;
	}
	if (g_soundQueueCount == queueIndex && queueIndex == 4) {
		return 0;
	}

	queueEntry = &g_soundQueue[queueIndex];
#ifdef XVT_MODERN
	memmove(&g_soundQueue[queueIndex + 1], queueEntry,
		sizeof(struct SoundQueueEntry) *
			(g_soundQueueCount - queueIndex));
#else
	memcpy(&g_soundQueue[queueIndex + 1], queueEntry,
	       sizeof(struct SoundQueueEntry) *
		       (g_soundQueueCount - queueIndex));
#endif
	strncpy(queueEntry->name, name, sizeof(queueEntry->name));
	g_soundQueue[queueIndex].loop = loop;
	g_soundQueue[queueIndex].allowRestartExisting = allowRestartExisting;
	g_soundQueue[queueIndex].priority = queuedPriority;
	g_soundQueue[queueIndex].volume = volume;
	g_soundQueue[queueIndex].pan = pan;
	queueCount = g_soundQueueCount + 1;
	g_soundQueueCount = queueCount;
	if (queueCount > 4) {
		g_soundQueueCount = 4;
	}
	return 1;
}

/* Plays a loaded effect on a new duplicate of its buffer and records it in a
 * slot of g_activeSoundInstances. Returns 0 when g_directSound is NULL or the
 * name is empty or not loaded. With all 8 slots in use it frees the first
 * slot whose buffer is neither playing nor looping, or whose GetStatus fails,
 * releasing the buffer. With all 8 still playing it takes the first slot
 * whose effect has the lowest currentPriority under priority: when that slot
 * plays this effect it rewinds it to 0, gives it the next sequence and
 * returns 1, else it stops and releases its buffer and uses that slot. With
 * no slot under priority it returns 0, unless allowRestartExisting is set and
 * a slot plays this effect: then it rewinds the first such and gives it the
 * next sequence, returning 1. With fewer than 8 slots in use it takes the
 * first free one. It duplicates the effect's buffer, returning 0 when the
 * duplicate is NULL (the pointer is not cleared before the call), rewinds it,
 * sets its volume to 400 * (5 * v - 635) / 127 hundredths of a decibel and
 * its pan to 400 * (5 * p - 315) / 63, v and p being volume and pan clamped
 * to 0 to 127, and plays it, looping when loop is 1. When Play succeeds it
 * fills the slot (effect, buffer, the next sequence from
 * g_nextSoundInstanceSeq), adds 1 to g_activeSoundCount and returns 1. When
 * Play returns DSERR_BUFFERLOST (0x88780096) it refills the effect's buffer
 * from its file with DirectSound_ReloadWaveBuffer and plays again, filling
 * the slot the same way on success; that path returns 0 whatever happens. Any
 * other failure of Play returns the HRESULT, a nonzero value. A duplicate
 * that fails to play is never released. */
// FUNCTION: XVT 0x42D230
int Sound_PlayEffectNow(const char *soundName, int allowRestartExisting,
			int loop, int priority, int volume, int pan)
{
	int effectIndex;
	int instanceIndex;
	int scanIndex;
	int activeEffectIndex;
	int lowestPriority;
	int clampedVolume;
	int clampedPan;
	uint32_t bufferStatus;
	uint32_t playFlags;
	HRESULT result;
	struct ActiveSoundInstance *instance;
	IDirectSoundBuffer *buffer;
	IDirectSoundBuffer *duplicate;

	if (g_directSound == NULL) {
		return 0;
	}
	if (*soundName == '\0') {
		return 0;
	}
	effectIndex = Sound_FindLoadedEffectByName(soundName);
	if (effectIndex == -1) {
		return 0;
	}

	if (g_activeSoundCount == 8) {
		instanceIndex = 0;
		do {
			activeEffectIndex =
				g_activeSoundInstances[instanceIndex]
					.effectIndex;
			if (activeEffectIndex != -1) {
				buffer = g_activeSoundInstances[instanceIndex]
						 .buffer;
				if (buffer->lpVtbl->GetStatus(
					    buffer, &bufferStatus) != 0) {
					break;
				}
				if ((bufferStatus & (DSBSTATUS_PLAYING |
						     DSBSTATUS_LOOPING)) == 0) {
					break;
				}
			}
			++instanceIndex;
		} while (instanceIndex < 8);
		if (instanceIndex < 8) {
			g_activeSoundInstances[instanceIndex]
				.buffer->lpVtbl->Release(
					g_activeSoundInstances[instanceIndex]
						.buffer);
			g_activeSoundInstances[instanceIndex].effectIndex = -1;
			g_activeSoundInstances[instanceIndex].buffer = NULL;
			g_activeSoundCount = g_activeSoundCount - 1;
		}

		if (instanceIndex == 8) {
			lowestPriority = priority;
			instanceIndex = 8;
			scanIndex = 0;
			do {
				activeEffectIndex =
					g_activeSoundInstances[scanIndex]
						.effectIndex;
				if (lowestPriority >
				    g_soundDefs[activeEffectIndex]
					    .currentPriority) {
					instanceIndex = scanIndex;
					lowestPriority =
						g_soundDefs[activeEffectIndex]
							.currentPriority;
				}
				++scanIndex;
			} while (scanIndex < 8);

			if (instanceIndex != 8) {
				instance =
					&g_activeSoundInstances[instanceIndex];
				if (instance->effectIndex == effectIndex) {
					instance->buffer->lpVtbl
						->SetCurrentPosition(
							instance->buffer, 0);
					instance->sequence =
						g_nextSoundInstanceSeq++;
					return 1;
				}
				buffer = instance->buffer;
				buffer->lpVtbl->Stop(buffer);
				buffer->lpVtbl->Release(buffer);
				instance->effectIndex = -1;
				instance->buffer = NULL;
				--g_activeSoundCount;
			} else {
				if (allowRestartExisting != 0) {
					instanceIndex = 0;
					do {
						activeEffectIndex =
							g_activeSoundInstances
								[instanceIndex]
									.effectIndex;
						if (activeEffectIndex ==
						    effectIndex) {
							instance =
								&g_activeSoundInstances
									[instanceIndex];
							instance->buffer->lpVtbl
								->SetCurrentPosition(
									instance->buffer,
									0);
							instance->sequence =
								g_nextSoundInstanceSeq++;
							return 1;
						}
						++instanceIndex;
					} while (instanceIndex < 8);
				}
				return 0;
			}
		}
	} else {
		instanceIndex = 0;
		do {
			if (g_activeSoundInstances[instanceIndex].effectIndex ==
			    -1) {
				break;
			}
			++instanceIndex;
		} while (instanceIndex < 8);
	}

	g_directSound->lpVtbl->DuplicateSoundBuffer(
		g_directSound, g_soundDefs[effectIndex].buffer, &duplicate);
	if (duplicate == NULL) {
		return 0;
	}
	duplicate->lpVtbl->SetCurrentPosition(duplicate, 0);
	clampedVolume = volume;
	if (clampedVolume > 127) {
		clampedVolume = 127;
	} else if (clampedVolume < 0) {
		clampedVolume = 0;
	}
	duplicate->lpVtbl->SetVolume(duplicate,
				     400 * (5 * clampedVolume - 635) / 127);
	clampedPan = pan;
	if (clampedPan > 127) {
		clampedPan = 127;
	}
	if (clampedPan < 0) {
		clampedPan = 0;
	}
	duplicate->lpVtbl->SetPan(duplicate, 400 * (5 * clampedPan - 315) / 63);
	playFlags = loop == 1;
	result = duplicate->lpVtbl->Play(duplicate, 0, 0, playFlags);
	if (result == (HRESULT)0x88780096) {
		result = DirectSound_ReloadWaveBuffer(
			g_soundDefs[effectIndex].buffer,
			g_soundDefs[effectIndex].fileName);
		if (result == 1) {
			duplicate->lpVtbl->SetCurrentPosition(duplicate, 0);
			result = duplicate->lpVtbl->Play(duplicate, 0, 0,
							 playFlags);
			if (result == 0) {
				buffer = duplicate;
				instance =
					&g_activeSoundInstances[instanceIndex];
				instance->effectIndex = effectIndex;
				instance->buffer = buffer;
				instance->sequence = g_nextSoundInstanceSeq++;
				++g_activeSoundCount;
			} else {
				return 0;
			}
		}
	} else if (result == 0) {
		buffer = duplicate;
		instance = &g_activeSoundInstances[instanceIndex];
		instance->effectIndex = effectIndex;
		instance->buffer = buffer;
		instance->sequence = g_nextSoundInstanceSeq++;
		++g_activeSoundCount;
		return 1;
	}
	return result;
}

/* Stops the oldest sound of the named effect, the one with the lowest sequence:
 * stops and releases its buffer, frees the slot and lowers g_activeSoundCount.
 * Returns 1 when Stop succeeded, else 0. Returns 0 with nothing stopped when
 * g_directSound is NULL, the name is empty or not loaded, no slot holds the
 * effect, or the slot's buffer is NULL. */
// FUNCTION: XVT 0x42D600
int Sound_StopOldestInstance(const char *name)
{
	int effectIndex;
	int instanceIndex;
	int oldestSequence;
	int oldestIndex;
	HRESULT stopResult;
	IDirectSoundBuffer *buffer;

	if (g_directSound == NULL) {
		return 0;
	}
	if (*name == '\0') {
		return 0;
	}
	effectIndex = Sound_FindLoadedEffectByName(name);
	if (effectIndex == -1) {
		return 0;
	}
	oldestSequence = (int)g_nextSoundInstanceSeq + 1;
	oldestIndex = -1;
	for (instanceIndex = 0; instanceIndex < 8; ++instanceIndex) {
		if (g_activeSoundInstances[instanceIndex].effectIndex !=
		    effectIndex) {
			continue;
		}
		if (oldestSequence <=
		    (int)g_activeSoundInstances[instanceIndex].sequence) {
			continue;
		}
		oldestSequence =
			(int)g_activeSoundInstances[instanceIndex].sequence;
		oldestIndex = instanceIndex;
	}
	if (oldestIndex == -1) {
		return 0;
	}
	buffer = g_activeSoundInstances[oldestIndex].buffer;
	if (buffer == NULL) {
		return 0;
	}
	stopResult = buffer->lpVtbl->Stop(buffer);
	buffer->lpVtbl->Release(buffer);
	g_activeSoundInstances[oldestIndex].buffer = NULL;
	g_activeSoundInstances[oldestIndex].effectIndex = -1;
	--g_activeSoundCount;
	return stopResult >= 0;
}

/* Calls Sound_StopOldestInstance for the effect of each slot in use, in slot
 * order, and returns 1 when every call returned 1, else 0. Each call stops that
 * effect's oldest sound, not necessarily that slot's, and a slot is never
 * looked at twice, so when an effect's older sound sits in a later slot, the
 * earlier slot keeps playing. */
// FUNCTION: XVT 0x42D6D0
int Sound_StopAllInstances(void)
{
	int result;
	int instanceIndex;

	result = 1;
	for (instanceIndex = 0; instanceIndex < 8; ++instanceIndex) {
		if (g_activeSoundInstances[instanceIndex].effectIndex != -1) {
			result &= Sound_StopOldestInstance(
				g_soundDefs
					[g_activeSoundInstances[instanceIndex]
						 .effectIndex]
						.name);
		}
	}
	return result;
}

/* Returns the primary buffer's volume on the game's scale,
 * 127 * millibels / 2000 + 127, or 0 when g_directSound is
 * NULL or GetVolume fails. Nothing calls this. */
// FUNCTION: XVT 0x42D770
int Sound_GetPrimaryBufferVolume(void)
{
	int32_t volumeMillibels;

	if (g_directSound == 0) {
		return 0;
	}
	if (g_soundPrimaryBuffer->lpVtbl->GetVolume(g_soundPrimaryBuffer,
						    &volumeMillibels) != 0) {
		return 0;
	}
	/* From here volumeMillibels holds the game's volume scale: 127 at full volume, 0 at -20 dB. */
	volumeMillibels = 127 * volumeMillibels / 2000 + 127;
	return volumeMillibels;
}

/* Sets the volume of the named effect's newest sound, the slot with the highest
 * sequence, to 400 * (5 * v - 635) / 127 hundredths of a decibel, v being
 * volume clamped to 0 to 127. Returns 1 when SetVolume succeeds, else 0; 0 also
 * when g_directSound is NULL, the name is empty or not loaded, or no slot holds
 * the effect. Only Sound_SetParam calls it, for code 0x600. */
// FUNCTION: XVT 0x42D7C0
int Sound_SetLatestInstanceVolume(const char *name, int volume)
{
	int effectIndex;
	int newestIndex;
	int instanceIndex;
	int newestSequence;
	int clampedVolume;

	if (g_directSound == 0) {
		return 0;
	}
	if (name[0] == '\0') {
		return 0;
	}

	effectIndex = Sound_FindLoadedEffectByName(name);
	if (effectIndex == -1) {
		return 0;
	}

	newestSequence = -1;
	instanceIndex = 0;
	newestIndex = -1;
	do {
		if (g_activeSoundInstances[instanceIndex].effectIndex ==
			    effectIndex &&
		    (int)g_activeSoundInstances[instanceIndex].sequence >
			    newestSequence) {
			newestSequence =
				g_activeSoundInstances[instanceIndex].sequence;
			newestIndex = instanceIndex;
		}
		++instanceIndex;
	} while (instanceIndex < 8);

	if (newestIndex == -1) {
		return 0;
	}

	clampedVolume = volume;
	if (clampedVolume > 127) {
		clampedVolume = 127;
	} else if (clampedVolume < 0) {
		clampedVolume = 0;
	}

	/* From here clampedVolume holds the DirectSound attenuation, in hundredths of a decibel (-2000 to 0). */
	clampedVolume = 400 * (5 * clampedVolume - 635) / 127;
	return g_activeSoundInstances[newestIndex].buffer->lpVtbl->SetVolume(
		       g_activeSoundInstances[newestIndex].buffer,
		       clampedVolume) == 0;
}

/* Returns the volume of the named effect's newest sound on the game's scale,
 * 127 * millibels / 2000 + 127, or 0 when g_directSound is NULL, the name is
 * empty or not loaded, no slot holds it, or GetVolume fails. Nothing calls
 * this. */
// FUNCTION: XVT 0x42D880
int Sound_GetLatestInstanceVolume(const char *name)
{
	int effectIndex;
	int newestIndex;
	int instanceIndex;
	int newestSequence;
	int32_t volumeMillibels;

	if (g_directSound == 0) {
		return 0;
	}
	if (name[0] == '\0') {
		return 0;
	}
	effectIndex = Sound_FindLoadedEffectByName(name);
	if (effectIndex == -1) {
		return 0;
	}
	newestSequence = -1;
	instanceIndex = 0;
	newestIndex = -1;
	do {
		if (g_activeSoundInstances[instanceIndex].effectIndex ==
			    effectIndex &&
		    (int)g_activeSoundInstances[instanceIndex].sequence >
			    newestSequence) {
			newestSequence =
				g_activeSoundInstances[instanceIndex].sequence;
			newestIndex = instanceIndex;
		}
		++instanceIndex;
	} while (instanceIndex < 8);
	if (newestIndex == -1) {
		return 0;
	}
	if (g_activeSoundInstances[newestIndex].buffer->lpVtbl->GetVolume(
		    g_activeSoundInstances[newestIndex].buffer,
		    &volumeMillibels) != 0) {
		return 0;
	}
	/* From here volumeMillibels holds the game's volume scale: 127 at full volume, 0 at -20 dB. */
	volumeMillibels = 127 * volumeMillibels / 2000 + 127;
	return volumeMillibels;
}

/* Sets the pan of the named effect's newest sound to 400 * (5 * p - 315) / 63
 * hundredths of a decibel, p being pan clamped to 0 to 127: -2000 at 0, 0 at
 * 63. Returns 1 when SetPan succeeds, else 0; 0 also when g_directSound is
 * NULL, the name is empty or not loaded, or no slot holds the effect. Only
 * Sound_SetParam calls it, for code 0x700, which none of its callers passes. */
// FUNCTION: XVT 0x42D940
int Sound_SetLatestInstancePan(const char *name, int pan)
{
	int effectIndex;
	int newestIndex;
	int instanceIndex;
	int newestSequence;
	int clampedPan;

	if (g_directSound == 0) {
		return 0;
	}
	if (name[0] == '\0') {
		return 0;
	}

	effectIndex = Sound_FindLoadedEffectByName(name);
	if (effectIndex == -1) {
		return 0;
	}

	newestSequence = -1;
	instanceIndex = 0;
	newestIndex = -1;
	do {
		if (g_activeSoundInstances[instanceIndex].effectIndex ==
			    effectIndex &&
		    (int)g_activeSoundInstances[instanceIndex].sequence >
			    newestSequence) {
			newestSequence =
				g_activeSoundInstances[instanceIndex].sequence;
			newestIndex = instanceIndex;
		}
		++instanceIndex;
	} while (instanceIndex < 8);

	if (newestIndex == -1) {
		return 0;
	}

	clampedPan = pan;
	if (clampedPan > 127) {
		clampedPan = 127;
	}
	if (clampedPan < 0) {
		clampedPan = 0;
	}

	/* From here clampedPan holds the DirectSound pan, in hundredths of a decibel (0 is center). */
	clampedPan = 400 * (5 * clampedPan - 315) / 63;
	return g_activeSoundInstances[newestIndex].buffer->lpVtbl->SetPan(
		       g_activeSoundInstances[newestIndex].buffer,
		       clampedPan) == 0;
}

/* Returns the pan of the named effect's newest sound on the game's scale,
 * 63 * pan / 10000 + 63, or 0 when g_directSound is NULL, the name is empty or
 * not loaded, no slot holds it, or GetPan fails. Nothing calls this. */
// FUNCTION: XVT 0x42DA00
int Sound_GetLatestInstancePan(const char *name)
{
	int effectIndex;
	int newestSequence;
	int instanceIndex;
	int newestIndex;
	int32_t panMillibels;

	if (g_directSound == 0) {
		return 0;
	}
	if (name[0] == '\0') {
		return 0;
	}
	effectIndex = Sound_FindLoadedEffectByName(name);
	if (effectIndex == -1) {
		return 0;
	}
	newestSequence = -1;
	instanceIndex = 0;
	newestIndex = -1;
	do {
		if (g_activeSoundInstances[instanceIndex].effectIndex ==
			    effectIndex &&
		    (int)g_activeSoundInstances[instanceIndex].sequence >
			    newestSequence) {
			newestSequence =
				g_activeSoundInstances[instanceIndex].sequence;
			newestIndex = instanceIndex;
		}
		++instanceIndex;
	} while (instanceIndex < 8);
	if (newestIndex == -1) {
		return 0;
	}
	if (g_activeSoundInstances[newestIndex].buffer->lpVtbl->GetPan(
		    g_activeSoundInstances[newestIndex].buffer,
		    &panMillibels) != 0) {
		return 0;
	}
	/* From here panMillibels holds the game's pan: 0 full left, 63 centered, 126 full right. */
	panMillibels = 63 * panMillibels / 10000 + 63;
	return panMillibels;
}

/* Sets the playback frequency, in samples per second, of the named effect's
 * newest sound. Returns 1 when SetFrequency succeeds, else 0; 0 also when
 * g_directSound is NULL, the name is empty or not loaded, or no slot holds the
 * effect. Only Sound_SetParam calls it, for code 0x777. */
// FUNCTION: XVT 0x42DAC0
int Sound_SetLatestInstanceFrequency(const char *name, uint32_t frequency)
{
	int effectIndex;
	int newestSequence;
	int instanceIndex;
	int newestIndex;

	if (g_directSound == 0) {
		return 0;
	}
	if (name[0] == '\0') {
		return 0;
	}

	effectIndex = Sound_FindLoadedEffectByName(name);
	if (effectIndex == -1) {
		return 0;
	}

	newestSequence = -1;
	instanceIndex = 0;
	newestIndex = -1;
	do {
		if (g_activeSoundInstances[instanceIndex].effectIndex ==
			    effectIndex &&
		    (int)g_activeSoundInstances[instanceIndex].sequence >
			    newestSequence) {
			newestSequence =
				g_activeSoundInstances[instanceIndex].sequence;
			newestIndex = instanceIndex;
		}
		++instanceIndex;
	} while (instanceIndex < 8);

	if (newestIndex == -1) {
		return 0;
	}

	return g_activeSoundInstances[newestIndex].buffer->lpVtbl->SetFrequency(
		       g_activeSoundInstances[newestIndex].buffer, frequency) ==
	       0;
}

/* Sets the named effect's currentPriority to priority clamped to 0 to 255 and
 * returns 1; returns 0 when the name is not loaded. Does not check
 * g_directSound. Only Sound_SetParam calls it, for code 0x500, which none of
 * its callers passes, so every effect keeps currentPriority 0. */
// FUNCTION: XVT 0x42DB50
int Sound_SetEffectCurrentPriority(const char *name, int priority)
{
	int effectIndex;
	int clampedPriority;

	effectIndex = Sound_FindLoadedEffectByName(name);
	if (effectIndex == -1) {
		return 0;
	}

	clampedPriority = priority;
	if (clampedPriority > 255) {
		clampedPriority = 255;
	}
	if (clampedPriority < 0) {
		clampedPriority = 0;
	}
	g_soundDefs[effectIndex].currentPriority = clampedPriority;
	return 1;
}

/* Returns the named effect's currentPriority, or 0 when the name is not loaded.
 * Only Sound_GetParam calls it, for code 0x500, which none of its callers
 * passes. */
// FUNCTION: XVT 0x42DBA0
int Sound_GetEffectCurrentPriority(const char *name)
{
	int effectIndex;

	effectIndex = Sound_FindLoadedEffectByName(name);
	if (effectIndex == -1) {
		return 0;
	}

	return g_soundDefs[effectIndex].currentPriority;
}

/* Counts the slots holding the named effect whose buffer reports the 0x1
 * (playing) or 0x4 (looping) status bit; returns the count, 0 to 8. Returns 0
 * when g_directSound is NULL or the name is empty or not loaded. Only
 * Sound_GetParam calls it, for code 0x100. */
// FUNCTION: XVT 0x42DBD0
int Sound_CountPlayingInstances(const char *name)
{
	int effectIndex;
	int playingCount;
	int instanceIndex;
	uint32_t status;

	if (g_directSound == 0) {
		return 0;
	}
	if (name[0] == '\0') {
		return 0;
	}

	effectIndex = Sound_FindLoadedEffectByName(name);
	if (effectIndex == -1) {
		return 0;
	}

	playingCount = 0;
	instanceIndex = 0;
	do {
		if (g_activeSoundInstances[instanceIndex].effectIndex ==
			    effectIndex &&
		    g_activeSoundInstances[instanceIndex]
				    .buffer->lpVtbl->GetStatus(
					    g_activeSoundInstances
						    [instanceIndex]
							    .buffer,
					    &status) == 0 &&
		    ((status & 1) != 0 || (status & 4) != 0)) {
			++playingCount;
		}
		++instanceIndex;
	} while (instanceIndex < 8);

	return playingCount;
}

/* Inserts a copy of *effect into g_soundDefs, keeping it sorted by name
 * (strncmp over 64 characters) with the new entry after equal names, adds 1 to
 * g_soundCount and adds 1 to each instance slot's effectIndex at or after the
 * insertion point. Does not check that the table has room; Sound_LoadEffectEx
 * checks g_soundCount under 1000 first. */
// FUNCTION: XVT 0x42DC60
void Sound_InsertEffectDefSorted(const struct SoundEffectDef *effect)
{
	int insertIndex;
	struct SoundEffectDef *current;
	const struct SoundEffectDef *sourceEffect;
	int destinationIndex;
	int remaining;
	int instanceIndex;

	insertIndex = 0;
	if (g_soundCount > 0) {
		current = g_soundDefs;
		sourceEffect = effect;
		do {
			if (strncmp(sourceEffect->name, current->name,
				    sizeof(sourceEffect->name)) < 0) {
				break;
			}
			++current;
			++insertIndex;
		} while (g_soundCount > insertIndex);
	} else {
		sourceEffect = effect;
	}
	if (g_soundCount > insertIndex) {
		destinationIndex = g_soundCount;
		remaining = g_soundCount - insertIndex;
		do {
			g_soundDefs[destinationIndex] =
				g_soundDefs[destinationIndex - 1];
			--destinationIndex;
			--remaining;
		} while (remaining != 0);
	}
	g_soundDefs[insertIndex] = *sourceEffect;
	++g_soundCount;
	instanceIndex = 0;
	do {
		if (insertIndex <=
		    g_activeSoundInstances[instanceIndex].effectIndex) {
			++g_activeSoundInstances[instanceIndex].effectIndex;
		}
		++instanceIndex;
	} while (instanceIndex < 8);
}

/* Removes entry effectIndex from g_soundDefs, moving the later entries up one,
 * lowers g_soundCount and lowers by 1 each instance slot's effectIndex above
 * it. Does nothing for an index outside 0 to g_soundCount - 1. Leaves the old
 * last entry in place, does not release the buffer and does not free slots that
 * play the entry; Sound_UnloadEffectByName stops them first. */
// FUNCTION: XVT 0x42DD20
void Sound_RemoveEffectDef(int effectIndex)
{
	int currentIndex;

	if (effectIndex < 0 || g_soundCount <= effectIndex) {
		return;
	}

	currentIndex = effectIndex;
	if (g_soundCount - 1 > effectIndex) {
		struct SoundEffectDef *currentEffect;

		currentEffect = &g_soundDefs[effectIndex];
		do {
			*currentEffect = currentEffect[1];
			++currentEffect;
			++currentIndex;
		} while (g_soundCount - 1 > currentIndex);
	}

	--g_soundCount;
	{
		int instanceIndex;

		for (instanceIndex = 0; instanceIndex < 8; ++instanceIndex) {
			if (g_activeSoundInstances[instanceIndex].effectIndex >
			    effectIndex) {
				--g_activeSoundInstances[instanceIndex]
					  .effectIndex;
			}
		}
	}
}

/* Returns the index of the named effect among the g_soundCount loaded entries
 * of g_soundDefs, or -1, by Sound_FindEffectByName. */
// FUNCTION: XVT 0x42DDA0
int Sound_FindLoadedEffectByName(const char *name)
{
	return Sound_FindEffectByName(g_soundDefs, g_soundCount - 1, name);
}

/* Binary search for name in records[0] to records[lastIndex], which must be
 * sorted by name, comparing up to 64 characters with strncmp. Returns the index
 * found, or -1. Sound_FindLoadedEffectByName is its only caller. */
// FUNCTION: XVT 0x42DDC0
int Sound_FindEffectByName(const struct SoundEffectDef *records, int lastIndex,
			   const char *name)
{
	int searchLastIndex;
	int middle;
	int baseIndex;
	int comparison;
	const struct SoundEffectDef *middleEffect;

	baseIndex = 0;
	searchLastIndex = lastIndex;
	while (1) {
		if (searchLastIndex < 0) {
			return -1;
		}

		middle = searchLastIndex >> 1;
		middleEffect = &records[middle];
		comparison = strncmp(middleEffect->name, name,
				     sizeof(middleEffect->name));
		if (comparison == 0) {
			return middle + baseIndex;
		}
		if (searchLastIndex <= 0) {
			return -1;
		}

		if (comparison < 0) {
			searchLastIndex -= middle + 1;
			baseIndex += middle + 1;
			records = middleEffect + 1;
		} else {
			searchLastIndex = middle - 1;
		}
	}
}

/* paramCode selects what to set: 0x500 the effect's priority, 0x600 the latest instance's volume,
 * 0x700 its pan and 0x777 its frequency. Any other code, or a sound id outside 4..837, returns 0. */
/* Returns what the chosen function returns. The name comes from
 * g_fsfxSfxNameTable[flightSoundId]. Its callers pass only codes 0x600 (1536)
 * and 0x777 (1911). */
// FUNCTION: XVT 0x4A9180
int Sound_SetParam(int flightSoundId, int paramCode, int value)
{
	if (flightSoundId < 4 || flightSoundId > 837) {
		return 0;
	}
	switch (paramCode) {
	case 0x500:
		return Sound_SetEffectCurrentPriority(
			g_fsfxSfxNameTable[flightSoundId], value);
	case 0x600:
		return Sound_SetLatestInstanceVolume(
			g_fsfxSfxNameTable[flightSoundId], value);
	case 0x700:
		return Sound_SetLatestInstancePan(
			g_fsfxSfxNameTable[flightSoundId], value);
	case 0x777:
		return Sound_SetLatestInstanceFrequency(
			g_fsfxSfxNameTable[flightSoundId], value);
	default:
		return 0;
	}
}

/* paramCode selects what to read: 0x100 the number of playing instances, 0x500 the effect's
 * priority. Any other code, or a sound id outside 4..837, returns 0. */
/* Returns what the chosen function returns. The name comes from
 * g_fsfxSfxNameTable[flightSoundId]. Its callers pass only code 0x100 (256). */
// FUNCTION: XVT 0x4A9300
int Sound_GetParam(int flightSoundId, int paramCode)
{
	if (flightSoundId < 4 || flightSoundId > 837) {
		return 0;
	}
	switch (paramCode) {
	case 0x100:
		return Sound_CountPlayingInstances(
			g_fsfxSfxNameTable[flightSoundId]);
	case 0x500:
		return Sound_GetEffectCurrentPriority(
			g_fsfxSfxNameTable[flightSoundId]);
	default:
		return 0;
	}
}

/* Returns 0 and ignores its arguments. Only FlightSync_UnusedFourArgForwarder
 * calls it, and nothing calls that. */
// FUNCTION: XVT 0x4A9370
int Sound_UnusedFourArgStub(int arg1, int arg2, int arg3, int arg4)
{
	(void)arg1;
	(void)arg2;
	(void)arg3;
	(void)arg4;

	return 0;
}

/* Stops the oldest sound of flight sound id flightSoundId, named by
 * g_fsfxSfxNameTable, with Sound_StopOldestInstance and returns its result;
 * returns 0 for an id outside 4 to 837. */
// FUNCTION: XVT 0x4A9380
int Sound_StopOldestInstanceById(int flightSoundId)
{
	if (flightSoundId < 4 || flightSoundId > 837) {
		return 0;
	}
	return Sound_StopOldestInstance(g_fsfxSfxNameTable[flightSoundId]);
}

/* Does nothing. Flight_MainLoop calls it in the original build and
 * XvtFlightTask_ReleaseMission in the modern one, each after
 * Sound_StopAllInstances. */
// FUNCTION: XVT 0x4A93B0
void Sound_EmptyStub(void) {}
