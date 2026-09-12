#ifndef XVT_AUDIO_MUSIC_CD_H
#define XVT_AUDIO_MUSIC_CD_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct MusicCdTrackCache {
	unsigned int unusedTrackZeroEndMsf;
	unsigned int trackEndMsfByTrack[30];
};

extern int g_musicCdPlaybackComplete;
extern int g_musicCdCurrentTrack;
extern uint32_t g_musicCdMciDeviceId;
extern uint32_t g_musicCdTrackCount;
extern struct MusicCdTrackCache g_musicCdTrackCache;

int MusicCd_Initialize(void);
int MusicCd_PlayTrackFromTime(int trackNumber, int startMinute, int startSecond);
int MusicCd_StopTrack(void);
int MusicCd_CloseDevice(void);
int MusicCd_IsPlaybackComplete(void);
uint32_t MusicCd_GetDeviceId(void);
int MusicCd_MarkPlaybackComplete(void);
int MusicCd_GetTrackEndTimeMs(int trackNumber);
int MusicCd_SetAuxVolume(unsigned int volume0To65535);
int MusicCd_FadeAuxVolume(unsigned int fromVolume, unsigned int toVolume, int fadeDurationMs);

#ifdef __cplusplus
}
#endif

#endif
