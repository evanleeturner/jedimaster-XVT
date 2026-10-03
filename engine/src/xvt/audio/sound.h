#ifndef XVT_AUDIO_SOUND_H
#define XVT_AUDIO_SOUND_H

#include "aeron/compat/win_types.h"
#include "xvt/util/win32.h"
#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct SoundQueueEntry {
	/* Effect name, from strncpy; not terminated for a name of 64 or more
	 * characters. */
	char name[64];
	int volume; /* Volume; Sound_PlayEffectNow clamps it to 0 to 127. */
	/* Pan; Sound_PlayEffectNow clamps it to 0 to 127, 63 centered. */
	int pan;
	/* 1 plays the effect looping; any other value plays it once. */
	int loop;
	/* Lets Sound_PlayEffectNow rewind a playing sound of the effect when
	 * all 8 slots play and none holds an effect of lower
	 * currentPriority. */
	int allowRestartExisting;
	/* Queue order, highest first; Sound_PlayEffectNow weighs it against the
	 * playing effects' currentPriority. */
	int priority;
};

struct SoundEffectDef {
	/* Name the effect is found by; g_soundDefs is sorted by it. Up to 63
	 * characters; empty in an unused entry. */
	char name[64];
	/* WAV file it was loaded from, cut at 191 characters;
	 * Sound_PlayEffectNow reloads a lost buffer from it. */
	char fileName[256];
	/* The loaded buffer each play duplicates. */
	IDirectSoundBuffer *buffer;
	/* Priority, 0 to 255, of the effect's playing sounds when
	 * Sound_PlayEffectNow needs a slot; 0 at load, and only
	 * Sound_SetEffectCurrentPriority, which no caller reaches, changes
	 * it. */
	uint8_t currentPriority;
};

extern IDirectSound *g_directSound;
extern IDirectSoundBuffer *g_soundPrimaryBuffer;

struct ActiveSoundInstance {
	/* g_soundDefs entry this slot plays, or -1 when free; kept in step as
	 * entries are inserted and removed. */
	int effectIndex;
	/* g_nextSoundInstanceSeq when the sound started or was last rewound;
	 * the lowest is the oldest. */
	unsigned int sequence;
	/* The duplicate buffer playing; NULL when free. */
	IDirectSoundBuffer *buffer;
};

extern struct SoundEffectDef g_soundDefs[1000];
extern struct ActiveSoundInstance g_activeSoundInstances[8];
extern struct SoundQueueEntry g_soundQueue[5];
extern int g_soundQueueCount;
extern int g_soundCount;

int Sound_Init_Sound_Engine(void *hwnd);
int Sound_Shutdown_Sound_Engine(void);
int Sound_LoadEffect(const char *fileName, const char *name);
int Sound_LoadEffectEx(const char *fileName, const char *name,
		       int omitSoftwareAndFrequencyCaps);
void Sound_UnloadAllEffects(void);
int Sound_UnloadEffectByName(const char *name);
void Sound_FlushQueuedEffects(void);
int Sound_QueueEffect(const char *soundName, int allowRestartExisting, int loop,
		      int priority, int volume, int pan);
int Sound_PlayEffectNow(const char *soundName, int allowRestartExisting,
			int loop, int priority, int volume, int pan);
int Sound_StopOldestInstance(const char *name);
int Sound_StopAllInstances(void);
int Sound_GetPrimaryBufferVolume(void);
int Sound_SetLatestInstanceVolume(const char *name, int volume);
int Sound_GetLatestInstanceVolume(const char *name);
int Sound_SetLatestInstancePan(const char *name, int pan);
int Sound_GetLatestInstancePan(const char *name);
int Sound_SetLatestInstanceFrequency(const char *name, uint32_t frequency);
int Sound_SetEffectCurrentPriority(const char *name, int priority);
int Sound_GetEffectCurrentPriority(const char *name);
int Sound_CountPlayingInstances(const char *name);
void Sound_InsertEffectDefSorted(const struct SoundEffectDef *effect);
void Sound_RemoveEffectDef(int effectIndex);
int Sound_FindLoadedEffectByName(const char *name);
int Sound_FindEffectByName(const struct SoundEffectDef *records, int lastIndex,
			   const char *name);
int Sound_SetParam(int flightSoundId, int paramCode, int value);
int Sound_GetParam(int flightSoundId, int paramCode);
int Sound_UnusedFourArgStub(int arg1, int arg2, int arg3, int arg4);
int Sound_StopOldestInstanceById(int flightSoundId);
void Sound_EmptyStub(void);

#ifdef __cplusplus
}
#endif

#endif
