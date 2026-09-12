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
	char name[64];
	int volume;
	int pan;
	int loop;
	int param2;
	int priority;
};

struct SoundEffectDef {
	char name[64];
	char fileName[256];
	IDirectSoundBuffer* buffer;
	uint8_t currentPriority;
};

struct IDirectSoundBuffer {
	struct IDirectSoundBufferVtbl* lpVtbl;
};

struct IDirectSoundBufferVtbl {
	HRESULT(AERON_DXAPI* QueryInterface)(IDirectSoundBuffer* This, const GUID* const, void**);
	uint32_t(AERON_DXAPI* AddRef)(IDirectSoundBuffer* This);
	uint32_t(AERON_DXAPI* Release)(IDirectSoundBuffer* This);
	HRESULT(AERON_DXAPI* GetCaps)(IDirectSoundBuffer* This, DSBCAPS* pDSBufferCaps);
	HRESULT(AERON_DXAPI* GetCurrentPosition)
	(IDirectSoundBuffer* This, uint32_t* pdwCurrentPlayCursor, uint32_t* pdwCurrentWriteCursor);
	HRESULT(AERON_DXAPI* GetFormat)
	(IDirectSoundBuffer* This, WAVEFORMATEX* pwfxFormat, uint32_t dwSizeAllocated, uint32_t* pdwSizeWritten);
	HRESULT(AERON_DXAPI* GetVolume)(IDirectSoundBuffer* This, int32_t* plVolume);
	HRESULT(AERON_DXAPI* GetPan)(IDirectSoundBuffer* This, int32_t* plPan);
	HRESULT(AERON_DXAPI* GetFrequency)(IDirectSoundBuffer* This, uint32_t* pdwFrequency);
	HRESULT(AERON_DXAPI* GetStatus)(IDirectSoundBuffer* This, uint32_t* pdwStatus);
	HRESULT(AERON_DXAPI* Initialize)
	(IDirectSoundBuffer* This, IDirectSound* pDirectSound, const DSBUFFERDESC* pcDSBufferDesc);
	HRESULT(AERON_DXAPI* Lock)
	(IDirectSoundBuffer* This, uint32_t dwOffset, uint32_t dwBytes, void** ppvAudioPtr1,
	 uint32_t* pdwAudioBytes1, void** ppvAudioPtr2, uint32_t* pdwAudioBytes2, uint32_t dwFlags);
	HRESULT(AERON_DXAPI* Play)
	(IDirectSoundBuffer* This, uint32_t dwReserved1, uint32_t dwPriority, uint32_t dwFlags);
	HRESULT(AERON_DXAPI* SetCurrentPosition)(IDirectSoundBuffer* This, uint32_t dwNewPosition);
	HRESULT(AERON_DXAPI* SetFormat)(IDirectSoundBuffer* This, WAVEFORMATEX* pcfxFormat);
	HRESULT(AERON_DXAPI* SetVolume)(IDirectSoundBuffer* This, int32_t lVolume);
	HRESULT(AERON_DXAPI* SetPan)(IDirectSoundBuffer* This, int32_t lPan);
	HRESULT(AERON_DXAPI* SetFrequency)(IDirectSoundBuffer* This, uint32_t dwFrequency);
	HRESULT(AERON_DXAPI* Stop)(IDirectSoundBuffer* This);
	HRESULT(AERON_DXAPI* Unlock)
	(IDirectSoundBuffer* This, void* pvAudioPtr1, uint32_t dwAudioBytes1, void* pvAudioPtr2,
	 uint32_t dwAudioBytes2);
	HRESULT(AERON_DXAPI* Restore)(IDirectSoundBuffer* This);
};

struct DSBCAPS {
	uint32_t dwSize;
	uint32_t dwFlags;
	uint32_t dwBufferBytes;
	uint32_t dwUnlockTransferRate;
	uint32_t dwPlayCpuOverhead;
};

extern IDirectSound* g_directSound;
extern IDirectSoundBuffer* g_soundPrimaryBuffer;

struct WAVEFORMATEX {
	uint16_t wFormatTag;
	uint16_t nChannels;
	uint32_t nSamplesPerSec;
	uint32_t nAvgBytesPerSec;
	uint16_t nBlockAlign;
	uint16_t wBitsPerSample;
	uint16_t cbSize;
};

struct IDirectSound {
	struct IDirectSoundVtbl* lpVtbl;
};

struct DSBUFFERDESC {
	uint32_t dwSize;
	uint32_t dwFlags;
	uint32_t dwBufferBytes;
	uint32_t dwReserved;
	WAVEFORMATEX* lpwfxFormat;
};

struct IDirectSoundVtbl {
	HRESULT(AERON_DXAPI* QueryInterface)(IDirectSound* This, const GUID* const, void**);
	uint32_t(AERON_DXAPI* AddRef)(IDirectSound* This);
	uint32_t(AERON_DXAPI* Release)(IDirectSound* This);
	HRESULT(AERON_DXAPI* CreateSoundBuffer)
	(IDirectSound* This, const DSBUFFERDESC* pcDSBufferDesc, IDirectSoundBuffer** ppDSBuffer,
	 void* pUnkOuter);
	HRESULT(AERON_DXAPI* GetCaps)(IDirectSound* This, DSCAPS* pDSCaps);
	HRESULT(AERON_DXAPI* DuplicateSoundBuffer)
	(IDirectSound* This, IDirectSoundBuffer* pDSBufferOriginal, IDirectSoundBuffer** ppDSBufferDuplicate);
	HRESULT(AERON_DXAPI* SetCooperativeLevel)(IDirectSound* This, void* hwnd, uint32_t dwLevel);
	HRESULT(AERON_DXAPI* Compact)(IDirectSound* This);
	HRESULT(AERON_DXAPI* GetSpeakerConfig)(IDirectSound* This, uint32_t* pdwSpeakerConfig);
	HRESULT(AERON_DXAPI* SetSpeakerConfig)(IDirectSound* This, uint32_t dwSpeakerConfig);
	HRESULT(AERON_DXAPI* Initialize)(IDirectSound* This, const GUID* pcGuidDevice);
};

struct DSCAPS {
	uint32_t dwSize;
	uint32_t dwFlags;
	uint32_t dwMinSecondarySampleRate;
	uint32_t dwMaxSecondarySampleRate;
	uint32_t dwPrimaryBuffers;
	uint32_t dwMaxHwMixingAllBuffers;
	uint32_t dwMaxHwMixingStaticBuffers;
	uint32_t dwMaxHwMixingStreamingBuffers;
	uint32_t dwFreeHwMixingAllBuffers;
	uint32_t dwFreeHwMixingStaticBuffers;
	uint32_t dwFreeHwMixingStreamingBuffers;
	uint32_t dwMaxHw3DAllBuffers;
	uint32_t dwMaxHw3DStaticBuffers;
	uint32_t dwMaxHw3DStreamingBuffers;
	uint32_t dwFreeHw3DAllBuffers;
	uint32_t dwFreeHw3DStaticBuffers;
	uint32_t dwFreeHw3DStreamingBuffers;
	uint32_t dwTotalHwMemBytes;
	uint32_t dwFreeHwMemBytes;
	uint32_t dwMaxContigFreeHwMemBytes;
	uint32_t dwUnlockTransferRateHwBuffers;
	uint32_t dwPlayCpuOverheadSwBuffers;
	uint32_t dwReserved1;
	uint32_t dwReserved2;
};

struct ActiveSoundInstance {
	int soundId;
	unsigned int sequence;
	IDirectSoundBuffer* buffer;
};

extern SoundEffectDef g_soundDefs[1000];
extern ActiveSoundInstance g_activeSoundInstances[8];
extern SoundQueueEntry g_soundQueue[5];
extern int g_soundQueueCount;
extern int g_soundCount;

int Sound_Init_Sound_Engine(void* hwnd);
int Sound_Shutdown_Sound_Engine(void);
int Sound_LoadEffect(const char* fileName, const char* name);
int Sound_LoadEffectEx(const char* fileName, const char* name, int createFlags);
void Sound_UnloadAllEffects(void);
int Sound_UnloadEffectByName(const char* name);
void Sound_FlushQueuedEffects(void);
int Sound_QueueEffect(const char* soundName, int param2, int loop, int priority, int volume, int pan);
int Sound_PlayEffectNow(const char* soundName, int param2, int loop, int priority, int volume, int pan);
int Sound_StopOldestInstance(const char* name);
int Sound_StopAllInstances(void);
int Sound_GetPrimaryBufferVolume(void);
int Sound_SetLatestInstanceVolume(const char* name, int volume);
int Sound_GetLatestInstanceVolume(const char* name);
int Sound_SetLatestInstancePan(const char* name, int pan);
int Sound_GetLatestInstancePan(const char* name);
int Sound_SetLatestInstanceFrequency(const char* name, uint32_t frequency);
int Sound_SetEffectCurrentPriority(const char* name, int priority);
int Sound_GetEffectCurrentPriority(const char* name);
int Sound_CountPlayingInstances(const char* name);
void Sound_InsertEffectDefSorted(const SoundEffectDef* effect);
void Sound_RemoveEffectDef(int soundId);
int Sound_FindLoadedEffectByName(const char* name);
int Sound_FindEffectByName(const SoundEffectDef* records, int lastIndex, const char* name);
int Sound_SetParam(int soundId, int param, int value);
int Sound_GetParam(int soundId, int param);
int Sound_UnusedFourArgStub(int arg1, int arg2, int arg3, int arg4);
int Sound_StopOldestInstanceById(int soundId);
void nullsub_10(void);

#ifdef __cplusplus
}
#endif

#endif
