#ifndef XVT_AUDIO_FRONTEND_SOUND_H
#define XVT_AUDIO_FRONTEND_SOUND_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct FrontendSoundBufferRecord {
	char name[64];
	char fileName[256];
	IDirectSoundBuffer* buffer;
	uint8_t priority;
};

struct FrontendSoundVoice {
	int bufferIndex;
	int playSerial;
	IDirectSoundBuffer* buffer;
};

int FrontendSound_InitDirectSound(void* hwnd);
int FrontendSound_ShutdownDirectSound(void);
int FrontendSound_LoadSound(const char* fileName, const char* soundName);
int FrontendSound_LoadSoundFile(const char* fileName, const char* soundName, int create3DFlags);
void FrontendSound_UnloadAllBuffers(void);
int FrontendSound_UnloadBufferByName(const char* soundName);
int FrontendSound_PlayUISound(const char* soundName, int allowRestartExisting, int loop, int priority,
							  int volume0To127, int pan0To127);
int FrontendSound_StopOldestVoiceByName(const char* name);
int FrontendSound_StopAllVoices(void);
int FrontendSound_SetPrimaryVolume(int volume0To127);
int FrontendSound_GetPrimaryVolume(void);
int FrontendSound_SetNewestVoiceVolumeByName(const char* name, int volume0To127);
int FrontendSound_GetNewestVoiceVolumeByName(const char* name);
int FrontendSound_SetNewestVoicePanByName(const char* name, int pan0To127);
int FrontendSound_GetNewestVoicePanByName(const char* name);
int FrontendSound_SetBufferPriorityByName(const char* name, int priority0To255);
int FrontendSound_GetBufferPriorityByName(const char* name);
int FrontendSound_GetPlayingCount(const char* name);
void FrontendSound_InsertSortedBuffer(const FrontendSoundBufferRecord* record);
void FrontendSound_RemoveBufferRecord(int bufferIndex);
int FrontendSound_FindBufferByName(const char* name);
int FrontendSound_BinarySearchBufferByName(const FrontendSoundBufferRecord* records, int lastIndex,
										   const char* name);
int FrontendSound_LoadList(const char* fileName);
int FrontendSound_UnloadList(char* fileName);

#ifdef __cplusplus
}
#endif

#endif
