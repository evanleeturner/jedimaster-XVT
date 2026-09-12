#include "xvt/audio/music_cd.h"

#include "aeron/compat/mmsystem.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/flight/flight.h"
#include "xvt/util/time.h"

#ifdef XVT_MODERN
#include "xvt_runtime/runtime/port.h"
#endif

#include <string.h>

// GLOBAL: XVT 0x622BC0
int g_musicCdPlaybackComplete = 0;
// GLOBAL: XVT 0x622BC4
uint32_t g_musicCdTrackCount = 0;
// GLOBAL: XVT 0x622BC8
int g_musicCdSavedAuxVolume = 0;
// GLOBAL: XVT 0x622BCC
struct MusicCdTrackCache g_musicCdTrackCache = { 0 };
// GLOBAL: XVT 0x622C48
int g_musicCdCurrentTrack = 0;
// GLOBAL: XVT 0x622C4C
uint32_t g_musicCdMciDeviceId = 0;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4A4EC0
int MusicCd_Initialize(void) {
	struct {
		uint32_t savedVolume;
		MCI_STATUS_PARMS statusParameters;
		MCI_SET_PARMS setParameters;
		MCI_OPEN_PARMSA openParameters;
		AUXCAPSA deviceCaps;
	} parameters;

#ifdef XVT_MODERN
	if (!XvtPort_IsInitialized()) {
#else
	if (g_flightMainWindowHandle == NULL) {
#endif
		return 0;
	}
	if (g_musicCdMciDeviceId != 0) {
		mciSendCommandA(g_musicCdMciDeviceId, MCI_CLOSE, 0, NULL);
		g_musicCdMciDeviceId = 0;
		memset(g_musicCdTrackCache.trackEndMsfByTrack, 0, sizeof(g_musicCdTrackCache.trackEndMsfByTrack));
		g_musicCdCurrentTrack = 0;
		g_musicCdPlaybackComplete = 0;
	}

	g_musicCdSavedAuxVolume = -1;
	{
		unsigned int deviceIndex;
		unsigned int deviceCount;

		deviceIndex = 0;
		deviceCount = auxGetNumDevs();
		if (deviceCount > 0) {
			do {
				memset(&parameters.deviceCaps, 0, sizeof(parameters.deviceCaps));
				auxGetDevCapsA(deviceIndex, &parameters.deviceCaps, sizeof(parameters.deviceCaps));
				if (parameters.deviceCaps.wTechnology == AUXCAPS_CDAUDIO &&
					(parameters.deviceCaps.dwSupport & AUXCAPS_VOLUME) != 0 &&
					auxGetVolume(deviceIndex, &parameters.savedVolume) == MMSYSERR_NOERROR) {
					g_musicCdSavedAuxVolume = parameters.savedVolume & UINT16_MAX;
					break;
				}
				++deviceIndex;
				if (deviceCount > deviceIndex) {
					continue;
				}
				break;
			} while (1);
		}
	}

	parameters.openParameters.lpstrDeviceType = "cdaudio";
	if (mciSendCommandA(0, MCI_OPEN, MCI_OPEN_TYPE, &parameters.openParameters) != MMSYSERR_NOERROR) {
		g_musicCdMciDeviceId = 0;
		return 0;
	}
	g_musicCdMciDeviceId = parameters.openParameters.wDeviceID;
	parameters.setParameters.dwTimeFormat = MCI_FORMAT_TMSF;
	if (mciSendCommandA(g_musicCdMciDeviceId, MCI_SET, MCI_SET_TIME_FORMAT, &parameters.setParameters) !=
		MMSYSERR_NOERROR) {
		mciSendCommandA(g_musicCdMciDeviceId, MCI_CLOSE, 0, NULL);
		g_musicCdMciDeviceId = 0;
		return 0;
	}

	{
		MMRESULT result;
		uint32_t deviceId;
		uint32_t trackCount;
		unsigned int trackNumber;

		parameters.statusParameters.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
		if (mciSendCommandA(g_musicCdMciDeviceId, MCI_STATUS, MCI_STATUS_ITEM,
							&parameters.statusParameters) != MMSYSERR_NOERROR) {
			mciSendCommandA(g_musicCdMciDeviceId, MCI_CLOSE, 0, NULL);
			g_musicCdMciDeviceId = 0;
			return 0;
		}

		g_musicCdTrackCount = (uint32_t)parameters.statusParameters.dwReturn;
		trackNumber = 1;
		if (g_musicCdTrackCount >= trackNumber) {
			deviceId = g_musicCdMciDeviceId;
			do {
				parameters.statusParameters.dwItem = MCI_STATUS_LENGTH;
				parameters.statusParameters.dwTrack = trackNumber;
				result = mciSendCommandA(deviceId, MCI_STATUS, MCI_STATUS_ITEM | MCI_TRACK,
										 &parameters.statusParameters);
				trackCount = g_musicCdTrackCount;
				deviceId = g_musicCdMciDeviceId;
				if (result != MMSYSERR_NOERROR) {
					mciSendCommandA(deviceId, MCI_CLOSE, 0, NULL);
					g_musicCdMciDeviceId = 0;
					return 0;
				}
				g_musicCdTrackCache.trackEndMsfByTrack[trackNumber - 1] =
					(unsigned int)parameters.statusParameters.dwReturn;
				++trackNumber;
			} while (trackCount >= trackNumber);
		}
		return 1;
	}
}

// FUNCTION: XVT 0x4A50D0
int MusicCd_PlayTrackFromTime(int trackNumber, int startMinute, int startSecond) {
	struct {
		void* callback;
		uint32_t from;
		uint32_t to;
	} parameters;

	uint32_t deviceId;
	uint32_t toTime;
	unsigned int trackEndMsf;

	if ((int)g_musicCdTrackCount < trackNumber || trackNumber <= 0)
		return 0;
	deviceId = g_musicCdMciDeviceId;
	if (deviceId == 0)
		return 0;

	memset(&parameters, 0, sizeof(parameters));
	parameters.from = MCI_MAKE_TMSF(trackNumber, startMinute, startSecond, 0);
	trackEndMsf = g_musicCdTrackCache.trackEndMsfByTrack[trackNumber - 1];
	parameters.callback = g_flightMainWindowHandle;
	toTime = MCI_MAKE_TMSF(trackNumber, MCI_MSF_MINUTE(trackEndMsf), MCI_MSF_SECOND(trackEndMsf),
						   MCI_MSF_FRAME(trackEndMsf));
	parameters.to = toTime;
	if (mciSendCommandA(deviceId, MCI_PLAY, MCI_NOTIFY | MCI_FROM | MCI_TO, &parameters) !=
		MMSYSERR_NOERROR) {
		return 0;
	}
	g_musicCdPlaybackComplete = 0;
	g_musicCdCurrentTrack = MCI_TMSF_TRACK(parameters.from);
	return 1;
}

// FUNCTION: XVT 0x4A51B0
int MusicCd_StopTrack(void) {
	MCI_GENERIC_PARMS parameters;

	if (g_musicCdMciDeviceId == 0) {
		return 0;
	}
	if (g_musicCdCurrentTrack == 0) {
		return 0;
	}
	mciSendCommandA(g_musicCdMciDeviceId, MCI_STOP, 0, &parameters);
	g_musicCdCurrentTrack = 0;
	g_musicCdPlaybackComplete = 0;
	return 1;
}

// FUNCTION: XVT 0x4A5210
int MusicCd_CloseDevice(void) {
	MCI_GENERIC_PARMS parameters;
	AUXCAPSA deviceCaps;
	int deviceCount;
	int deviceIndex;
	uint32_t stereoVolume;

	if (g_musicCdMciDeviceId == 0) {
		return 0;
	}

	if (g_musicCdCurrentTrack != 0) {
		mciSendCommandA(g_musicCdMciDeviceId, MCI_STOP, 0, &parameters);
		g_musicCdCurrentTrack = 0;
		g_musicCdPlaybackComplete = 0;
	}
	mciSendCommandA(g_musicCdMciDeviceId, MCI_CLOSE, 0, NULL);
	g_musicCdMciDeviceId = 0;
	memset(g_musicCdTrackCache.trackEndMsfByTrack, 0, sizeof(g_musicCdTrackCache.trackEndMsfByTrack));
	g_musicCdCurrentTrack = 0;
	g_musicCdPlaybackComplete = 0;

	deviceIndex = 0;
	deviceCount = (int)auxGetNumDevs();
	if (g_musicCdSavedAuxVolume != -1) {
		stereoVolume = (uint32_t)g_musicCdSavedAuxVolume * 65537;
		if (deviceCount > 0) {
			do {
				memset(&deviceCaps, 0, sizeof(deviceCaps));
				auxGetDevCapsA((uintptr_t)deviceIndex, &deviceCaps, sizeof(deviceCaps));
				if (deviceCaps.wTechnology == AUXCAPS_CDAUDIO &&
					(deviceCaps.dwSupport & AUXCAPS_VOLUME) != 0) {
					auxSetVolume((uintptr_t)deviceIndex, stereoVolume);
				}
				++deviceIndex;
			} while (deviceIndex < deviceCount);
		}
	}
	g_musicCdSavedAuxVolume = -1;
#ifdef XVT_MODERN
	return 0;
#endif
}

// FUNCTION: XVT 0x4A5300
int MusicCd_IsPlaybackComplete(void) {
	if (g_musicCdMciDeviceId == 0) {
		return 0;
	}
	if (g_musicCdCurrentTrack == 0) {
		return 0;
	}
	return g_musicCdPlaybackComplete;
}

// FUNCTION: XVT 0x4A5320
uint32_t MusicCd_GetDeviceId(void) { return g_musicCdMciDeviceId; }

// FUNCTION: XVT 0x4A5330
int MusicCd_MarkPlaybackComplete(void) {
	g_musicCdPlaybackComplete = 1;
	return 1;
}

// FUNCTION: XVT 0x4A5340
int MusicCd_GetTrackEndTimeMs(int trackNumber) {
	unsigned int trackEndMsf;

	if (g_musicCdMciDeviceId == 0) {
		return 0;
	}
	if (trackNumber <= 0 || trackNumber > (int)g_musicCdTrackCount) {
		return 0;
	}

	trackEndMsf = g_musicCdTrackCache.trackEndMsfByTrack[trackNumber - 1];
	return MCI_MSF_FRAME(trackEndMsf) * 1000 / 75 +
		   1000 * (MCI_MSF_SECOND(trackEndMsf) + 60 * MCI_MSF_MINUTE(trackEndMsf));
}

// FUNCTION: XVT 0x4A53B0
int MusicCd_SetAuxVolume(unsigned int volume0To65535) { return CDAudio_SetAuxVolume(volume0To65535); }

// FUNCTION: XVT 0x4A53C0
int MusicCd_FadeAuxVolume(unsigned int fromVolume, unsigned int toVolume, int fadeDurationMs) {
	int fadeUp;
	unsigned int stepDelayMs;
	uint32_t previousTick;
	int currentTick;
	unsigned int nextVolume;

	if (g_musicCdMciDeviceId == 0) {
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

	previousTick = timeGetTime();
	while (1) {
		currentTick = timeGetTime();
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
			MusicCd_SetAuxVolume(fromVolume);
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
}
