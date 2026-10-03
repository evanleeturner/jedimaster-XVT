#ifndef XVT_AUDIO_CD_AUDIO_H
#define XVT_AUDIO_CD_AUDIO_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CDAudioTrackCache {
	/* CD volume, 0 to 65535, CDAudio_SetAuxVolume last set;
	 * FrontendDisplay_Init and
	 * FrontendDisplay_ResetGlobalStatePreservingNetworkSession clear it
	 * with the rest of g_frontState. */
	unsigned int currentAuxVolume;
	/* Length of track n + 1 in entry n, as MCI minutes, seconds and frames;
	 * CDAudio_Initialize fills it, CDAudio_CloseDevice clears it. */
	unsigned int trackLengthMsfByTrack[40];
};

typedef enum CDAudioSuspendState {
	CDAudio_NotSuspended = 0x0,
	CDAudio_Suspended = 0x1,
	CDAudio_ResumePending = 0x2,
} CDAudioSuspendState;

int CDAudio_Initialize(void);
int CDAudio_PlayTrackFromTime(int trackNumber, uint16_t startMinute,
			      uint8_t startSecond);
int CDAudio_StopCurrentTrack(void);
void CDAudio_CloseDevice(void);
int CDAudio_IsPlaybackComplete(void);
int CDAudio_GetTrackLengthMs(int trackNumber);
int CDAudio_EnableLoopCurrentTrack(void);
int CDAudio_DisableLoopCurrentTrack(void);
int CDAudio_SuspendPlayback(void);
int CDAudio_RequestResumePlayback(void);
int CDAudio_ResumeSuspendedPlayback(void);
int CDAudio_SetAuxVolume(unsigned int volume0To65535);
int CDAudio_FadeAuxVolume(unsigned int fromVolume, unsigned int toVolume,
			  int fadeDurationMs);

#ifdef __cplusplus
}
#endif

#endif
