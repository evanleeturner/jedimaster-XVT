#include "xvt/audio/cd_audio.h"
#ifdef XVT_MODERN
#include "xvt_runtime/runtime/cd_task.h"
#include "xvt_runtime/runtime/port.h"
#endif
#include "xvt/frontend/frontend_state.h"

#include "aeron/compat/mmsystem.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/util/time.h"
#include <string.h>

// FUNCTION: XVT 0x4D2E50
int CDAudio_Initialize(void) {
	uint32_t savedVolume;
	MCI_STATUS_PARMS statusParameters;
	MCI_SET_PARMS setParameters;
	MCI_OPEN_PARMSA openParameters;
	AUXCAPSA deviceCaps;
	int deviceCount;
	int deviceIndex;

#ifdef XVT_MODERN
	if (!XvtPort_IsInitialized()) {
#else
	if (g_frontState.hWnd == NULL) {
#endif
		return 0;
	}
	if (g_frontState.cdAudioMciDeviceId != 0) {
		CDAudio_CloseDevice();
	}

	deviceIndex = 0;
	deviceCount = (int)auxGetNumDevs();
	if (deviceCount > 0) {
		do {
			memset(&deviceCaps, 0, sizeof(deviceCaps));
			auxGetDevCapsA(deviceIndex, &deviceCaps, sizeof(deviceCaps));
			if (deviceCaps.wTechnology == AUXCAPS_CDAUDIO && (deviceCaps.dwSupport & AUXCAPS_VOLUME) != 0 &&
				auxGetVolume(deviceIndex, &savedVolume) == MMSYSERR_NOERROR) {
				g_frontState.cdAudioSavedAuxVolume = savedVolume & UINT16_MAX;
				break;
			}
		} while (++deviceIndex < deviceCount);
	}

	openParameters.lpstrDeviceType = "cdaudio";
	if (mciSendCommandA(0, MCI_OPEN, MCI_OPEN_TYPE, &openParameters) != MMSYSERR_NOERROR) {
		g_frontState.cdAudioMciDeviceId = 0;
		return 0;
	}
	g_frontState.cdAudioMciDeviceId = openParameters.wDeviceID;
	setParameters.dwTimeFormat = MCI_FORMAT_TMSF;
	if (mciSendCommandA(g_frontState.cdAudioMciDeviceId, MCI_SET, MCI_SET_TIME_FORMAT, &setParameters) !=
		MMSYSERR_NOERROR) {
		mciSendCommandA(g_frontState.cdAudioMciDeviceId, MCI_CLOSE, 0, NULL);
		g_frontState.cdAudioMciDeviceId = 0;
		return 0;
	}

	statusParameters.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
	if (mciSendCommandA(g_frontState.cdAudioMciDeviceId, MCI_STATUS, MCI_STATUS_ITEM, &statusParameters) !=
		MMSYSERR_NOERROR) {
		mciSendCommandA(g_frontState.cdAudioMciDeviceId, MCI_CLOSE, 0, NULL);
		g_frontState.cdAudioMciDeviceId = 0;
		return 0;
	}

	g_frontState.cdAudioTrackCount = (int)statusParameters.dwReturn;
	deviceCount = 1;
	if (g_frontState.cdAudioTrackCount < deviceCount) {
		return 1;
	}
	while (1) {
		statusParameters.dwItem = MCI_STATUS_LENGTH;
		statusParameters.dwTrack = deviceCount;
		if (mciSendCommandA(g_frontState.cdAudioMciDeviceId, MCI_STATUS, MCI_STATUS_ITEM | MCI_TRACK,
							&statusParameters) != MMSYSERR_NOERROR) {
			break;
		}
		g_frontState.cdAudioTrackCache.trackEndMsfByTrack[deviceCount - 1] =
			(unsigned int)statusParameters.dwReturn;
		deviceCount++;
		if (g_frontState.cdAudioTrackCount < deviceCount) {
			return 1;
		}
	}
	mciSendCommandA(g_frontState.cdAudioMciDeviceId, MCI_CLOSE, 0, NULL);
	g_frontState.cdAudioMciDeviceId = 0;
	return 0;
}

// FUNCTION: XVT 0x4D3020
int CDAudio_PlayTrackFromTime(int trackNumber, uint16_t startMinute, uint8_t startSecond) {
	struct {
		void* callback;
		uint32_t from;
		uint32_t to;
	} parameters;

	unsigned int trackEndMsf;

	if (g_frontState.cdAudioTrackCount < trackNumber || trackNumber <= 0) {
		return 0;
	}
	if (g_frontState.cdAudioMciDeviceId == 0) {
		return 0;
	}

	memset(&parameters, 0, sizeof(parameters));
	parameters.from =
		((uint8_t)trackNumber | ((unsigned int)startMinute << 8)) | ((unsigned int)startSecond << 16);
	trackEndMsf = g_frontState.cdAudioTrackCache.trackEndMsfByTrack[trackNumber - 1];
	parameters.to = ((uint8_t)trackNumber | ((unsigned int)(uint8_t)trackEndMsf << 8)) |
					(((unsigned int)(uint8_t)((uint16_t)trackEndMsf >> 8) |
					  ((unsigned int)(uint8_t)(trackEndMsf >> 16) << 8))
					 << 16);
	parameters.callback = g_frontState.hWnd;
	if (mciSendCommandA(g_frontState.cdAudioMciDeviceId, MCI_PLAY, MCI_NOTIFY | MCI_FROM | MCI_TO,
						&parameters) != MMSYSERR_NOERROR) {
		return 0;
	}

	g_frontState.cdAudioCurrentTrack = trackNumber;
	g_frontState.cdAudioTrackEndTick = CDAudio_GetTrackEndTimeMs(trackNumber) + GetTickCount() + 2000;
	g_frontState.cdAudioPlaybackComplete = 0;
	g_frontState.cdAudioSuspendState = CDAudio_NotSuspended;
	return 1;
}

// FUNCTION: XVT 0x4D3140
int CDAudio_StopCurrentTrack(void) {
	MCI_GENERIC_PARMS parameters;

	if (g_frontState.cdAudioMciDeviceId == 0) {
		return 0;
	}
	if (g_frontState.cdAudioCurrentTrack == 0) {
		return 0;
	}
	mciSendCommandA(g_frontState.cdAudioMciDeviceId, MCI_STOP, 0, &parameters);
	g_frontState.cdAudioCurrentTrack = 0;
	g_frontState.cdAudioPlaybackComplete = 0;
	g_frontState.cdAudioSuspendState = CDAudio_NotSuspended;
	return 1;
}

// FUNCTION: XVT 0x4D31A0
void CDAudio_CloseDevice(void) {
	MCI_GENERIC_PARMS parameters;
	AUXCAPSA deviceCaps;
	int deviceIndex;
	int deviceCount;
	uint32_t stereoVolume;
	uint32_t* mciDeviceId;
	int* savedAuxVolume;

#ifdef XVT_MODERN
	XvtCdTask_Cancel();
#endif
	if (g_frontState.cdAudioMciDeviceId == 0) {
		return;
	}
	mciDeviceId = &g_frontState.cdAudioMciDeviceId;
	savedAuxVolume = &g_frontState.cdAudioSavedAuxVolume;
	if (g_frontState.cdAudioCurrentTrack != 0) {
		mciSendCommandA(*mciDeviceId, MCI_STOP, 0, &parameters);
		g_frontState.cdAudioCurrentTrack = 0;
		g_frontState.cdAudioPlaybackComplete = 0;
	}
	deviceIndex = 0;
	mciSendCommandA(*mciDeviceId, MCI_CLOSE, 0, NULL);
	*mciDeviceId = 0;
	memset(g_frontState.cdAudioTrackCache.trackEndMsfByTrack, 0,
		   sizeof(g_frontState.cdAudioTrackCache.trackEndMsfByTrack));
	g_frontState.cdAudioCurrentTrack = 0;
	g_frontState.cdAudioPlaybackComplete = 0;
	g_frontState.cdAudioSuspendState = CDAudio_NotSuspended;
	deviceCount = (int)auxGetNumDevs();
	if (*savedAuxVolume != -1) {
		stereoVolume = (uint32_t)*savedAuxVolume * 65537;
		if (deviceCount > 0) {
			do {
				memset(&deviceCaps, 0, sizeof(deviceCaps));
				auxGetDevCapsA((uintptr_t)deviceIndex, &deviceCaps, sizeof(deviceCaps));
				if (deviceCaps.wTechnology == AUXCAPS_CDAUDIO &&
					(deviceCaps.dwSupport & AUXCAPS_VOLUME) != 0) {
					auxSetVolume((uintptr_t)deviceIndex, stereoVolume);
				}
			} while (++deviceIndex < deviceCount);
		}
	}
	*savedAuxVolume = -1;
}

// FUNCTION: XVT 0x4D32A0
int CDAudio_IsPlaybackComplete(void) {
	if (g_frontState.cdAudioMciDeviceId == 0) {
		return 0;
	}
	if (g_frontState.cdAudioCurrentTrack == 0) {
		return 0;
	}
	return g_frontState.cdAudioPlaybackComplete;
}

// FUNCTION: XVT 0x4D32C0
int CDAudio_GetTrackEndTimeMs(int trackNumber) {
	unsigned int trackEndMsf;

	if (g_frontState.cdAudioMciDeviceId == 0) {
		return 0;
	}
	if (trackNumber <= 0 || g_frontState.cdAudioTrackCount < trackNumber) {
		return 0;
	}

	trackEndMsf = g_frontState.cdAudioTrackCache.trackEndMsfByTrack[trackNumber - 1];
	return (MCI_MSF_MINUTE(trackEndMsf) * 60 + MCI_MSF_SECOND(trackEndMsf)) * 1000 +
		   MCI_MSF_FRAME(trackEndMsf) * 1000 / 75;
}

// FUNCTION: XVT 0x4D3330
int CDAudio_EnableLoopCurrentTrack(void) {
	g_frontState.cdAudioLoopCurrentTrack = 1;
	return 1;
}

// FUNCTION: XVT 0x4D3340
int CDAudio_DisableLoopCurrentTrack(void) {
	g_frontState.cdAudioLoopCurrentTrack = 0;
	return 1;
}

// FUNCTION: XVT 0x4D3350
int CDAudio_SuspendPlayback(void) {
	uint32_t trackEndTick;
	MCI_GENERIC_PARMS parameters;

	if (g_frontState.cdAudioMciDeviceId == 0)
		return 0;

	if (g_frontState.cdAudioSuspendState == CDAudio_NotSuspended) {
		if (g_frontState.cdAudioCurrentTrack != 0 && g_frontState.cdAudioPlaybackComplete == 0) {
			trackEndTick = g_frontState.cdAudioTrackEndTick;
			g_frontState.cdAudioSuspendRemainingMs = trackEndTick - GetTickCount() - 2000;
			g_frontState.cdAudioSuspendElapsedMs =
				(uint32_t)CDAudio_GetTrackEndTimeMs(g_frontState.cdAudioCurrentTrack) -
				g_frontState.cdAudioSuspendRemainingMs;
			mciSendCommandA(g_frontState.cdAudioMciDeviceId, MCI_STOP, 0, &parameters);
			g_frontState.cdAudioSuspendState = CDAudio_Suspended;
			return 1;
		}
	} else if (g_frontState.cdAudioSuspendState == CDAudio_ResumePending) {
		g_frontState.cdAudioSuspendState = CDAudio_Suspended;
	}

	return 1;
}

// FUNCTION: XVT 0x4D3400
int CDAudio_RequestResumePlayback(void) {
	if (g_frontState.cdAudioSuspendState == CDAudio_Suspended) {
		g_frontState.cdAudioSuspendState = CDAudio_ResumePending;
		g_frontState.cdAudioResumeDueTick = GetTickCount() + 1000;
	}
	return 1;
}

// FUNCTION: XVT 0x4D3430
int CDAudio_ResumeSuspendedPlayback(void) {
	unsigned int startMinute;
	unsigned int startSecond;

	startMinute = g_frontState.cdAudioSuspendElapsedMs / 60000;
	startSecond = startMinute * 60000;
	startSecond = g_frontState.cdAudioSuspendElapsedMs - startSecond;
	startSecond /= 1000;
	CDAudio_PlayTrackFromTime(g_frontState.cdAudioCurrentTrack, (uint16_t)startMinute, (uint8_t)startSecond);
	g_frontState.cdAudioTrackEndTick = GetTickCount() + g_frontState.cdAudioSuspendRemainingMs + 2000;
	g_frontState.cdAudioSuspendState = CDAudio_NotSuspended;
	return 1;
}

// FUNCTION: XVT 0x4D34A0
int CDAudio_SetAuxVolume(unsigned int volume0To65535) {
	unsigned int deviceCount;
	unsigned int stereoVolume;
	unsigned int deviceIndex;
	AUXCAPSA deviceCaps;

	deviceCount = auxGetNumDevs();
	if (volume0To65535 > 65535)
		volume0To65535 = 65535;
	g_frontState.cdAudioTrackCache.currentAuxVolume = volume0To65535;
	stereoVolume = volume0To65535 * 65537;

	for (deviceIndex = 0; deviceCount > deviceIndex; deviceIndex++) {
		memset(&deviceCaps, 0, sizeof(deviceCaps));
		auxGetDevCapsA(deviceIndex, &deviceCaps, sizeof(deviceCaps));
		if (deviceCaps.wTechnology == AUXCAPS_CDAUDIO && (deviceCaps.dwSupport & AUXCAPS_VOLUME) != 0)
			auxSetVolume(deviceIndex, stereoVolume);
	}

	return 1;
}

// FUNCTION: XVT 0x4D3520
int CDAudio_FadeAuxVolume(unsigned int fromVolume, unsigned int toVolume, int fadeDurationMs) {
#ifdef XVT_MODERN
	return XvtCdTask_BeginFade(fromVolume, toVolume, fadeDurationMs);
#else
	int fadeUp;
	unsigned int stepDelayMs;
	uint32_t previousTick;
	int currentTick;
	unsigned int nextVolume;

	if (g_frontState.cdAudioMciDeviceId == 0) {
		return 0;
	}
	if (toVolume == fromVolume) {
		return 1;
	}
	if (toVolume < fromVolume) {
		fadeUp = 0;
		stepDelayMs = (fadeDurationMs << 8) / (fromVolume - toVolume);
	} else {
		fadeUp = 1;
		stepDelayMs = (fadeDurationMs << 8) / (toVolume - fromVolume);
	}

	previousTick = GetTickCount();
	while (1) {
		currentTick = GetTickCount();
		if ((int)(previousTick + stepDelayMs) < currentTick) {
			if (fadeUp != 0) {
				nextVolume = fromVolume + 256;
				if (nextVolume > 65535) {
					fromVolume = 65535;
				} else {
					fromVolume = nextVolume;
				}
			} else {
				if (fromVolume < 256) {
					fromVolume = 0;
				} else {
					fromVolume -= 256;
				}
			}
			CDAudio_SetAuxVolume(fromVolume);
			previousTick = currentTick;
		}
		if (fadeUp != 0) {
			if (toVolume <= fromVolume) {
				break;
			}
		} else if (toVolume >= fromVolume) {
			break;
		}
	}
	return 1;
#endif
}
