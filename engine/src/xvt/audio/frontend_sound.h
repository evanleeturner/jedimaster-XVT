#ifndef XVT_AUDIO_FRONTEND_SOUND_H
#define XVT_AUDIO_FRONTEND_SOUND_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct FrontendSoundBufferRecord {
	/* Name the sound is found by; the records are sorted by it. Up to 63
	 * characters; empty in an unused record. */
	char name[64];
	/* WAV file it was loaded from, cut at 191 characters;
	 * FrontendSound_PlayUISound reloads a lost buffer from it. */
	char fileName[256];
	/* The loaded buffer each play duplicates. */
	IDirectSoundBuffer *buffer;
	/* Priority, 0 to 255, weighed when FrontendSound_PlayUISound needs a
	 * voice; 0 at load, and only FrontendSound_SetBufferPriorityByName,
	 * which nothing calls, changes it. */
	uint8_t priority;
};

struct FrontendSoundVoice {
	/* Record this voice plays, or -1 when free; kept in step as records are
	 * inserted and removed. */
	int bufferIndex;
	/* frontendSoundPlaySerial when the voice started or was last rewound;
	 * the lowest is the oldest. */
	int playSerial;
	/* The duplicate buffer playing; NULL when free. */
	IDirectSoundBuffer *buffer;
};

int FrontendSound_InitDirectSound(void *hwnd);
int FrontendSound_ShutdownDirectSound(void);
int FrontendSound_LoadSound(const char *fileName, const char *soundName);
int FrontendSound_LoadSoundFile(const char *fileName, const char *soundName,
				int omitSoftwareAndFrequencyCaps);
void FrontendSound_UnloadAllBuffers(void);
int FrontendSound_UnloadBufferByName(const char *soundName);
int FrontendSound_PlayUISound(const char *soundName, int allowRestartExisting,
			      int loop, int priority, int volume0To127,
			      int pan0To127);
int FrontendSound_StopOldestVoiceByName(const char *name);
int FrontendSound_StopAllVoices(void);
int FrontendSound_SetPrimaryVolume(int volume0To127);
int FrontendSound_GetPrimaryVolume(void);
int FrontendSound_SetNewestVoiceVolumeByName(const char *name,
					     int volume0To127);
int FrontendSound_GetNewestVoiceVolumeByName(const char *name);
int FrontendSound_SetNewestVoicePanByName(const char *name, int pan0To127);
int FrontendSound_GetNewestVoicePanByName(const char *name);
int FrontendSound_SetBufferPriorityByName(const char *name, int priority0To255);
int FrontendSound_GetBufferPriorityByName(const char *name);
int FrontendSound_GetPlayingCount(const char *name);
void FrontendSound_InsertSortedBuffer(
	const struct FrontendSoundBufferRecord *record);
void FrontendSound_RemoveBufferRecord(int bufferIndex);
int FrontendSound_FindBufferByName(const char *name);
int FrontendSound_BinarySearchBufferByName(
	const struct FrontendSoundBufferRecord *records, int lastIndex,
	const char *name);
int FrontendSound_LoadList(const char *fileName);
int FrontendSound_UnloadList(char *fileName);

#ifdef __cplusplus
}
#endif

#endif
