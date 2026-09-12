#ifndef XVT_AUDIO_CD_AUDIO_H
#define XVT_AUDIO_CD_AUDIO_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CDAudioTrackCache {
	unsigned int currentAuxVolume;
	unsigned int trackEndMsfByTrack[40];
};

typedef enum CDAudioSuspendState {
	CDAudio_NotSuspended = 0x0,
	CDAudio_Suspended = 0x1,
	CDAudio_ResumePending = 0x2,
} CDAudioSuspendState;

int CDAudio_Initialize(void);
int CDAudio_PlayTrackFromTime(int trackNumber, uint16_t startMinute, uint8_t startSecond);
int CDAudio_StopCurrentTrack(void);
void CDAudio_CloseDevice(void);
int CDAudio_IsPlaybackComplete(void);
int CDAudio_GetTrackEndTimeMs(int trackNumber);
int CDAudio_EnableLoopCurrentTrack(void);
int CDAudio_DisableLoopCurrentTrack(void);
int CDAudio_SuspendPlayback(void);
int CDAudio_RequestResumePlayback(void);
int CDAudio_ResumeSuspendedPlayback(void);
int CDAudio_SetAuxVolume(unsigned int volume0To65535);
int CDAudio_FadeAuxVolume(unsigned int fromVolume, unsigned int toVolume, int fadeDurationMs);

#ifdef __cplusplus
}
#endif

#endif
