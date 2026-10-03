#include "xvt/audio/music_cd.h"

#include "aeron/compat/mmsystem.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/flight/flight.h"
#include "xvt/util/time.h"

#ifdef XVT_MODERN
#include "xvt_runtime/runtime/port.h"
#endif

#include <string.h>

/* 1 once the flight music track is marked complete. MusicCd_Initialize,
 * MusicCd_PlayTrackFromTime, MusicCd_StopTrack and MusicCd_CloseDevice set 0;
 * only MusicCd_MarkPlaybackComplete sets 1, and nothing calls it, so it stays
 * 0. */
// GLOBAL: XVT 0x622BC0
int g_musicCdPlaybackComplete = 0;
/* Tracks on the CD, read by MusicCd_Initialize; nothing resets it. */
// GLOBAL: XVT 0x622BC4
uint32_t g_musicCdTrackCount = 0;
/* Volume, 0 to 65535, of the first CD audio auxiliary device with volume
 * control, saved by MusicCd_Initialize to put back at MusicCd_CloseDevice; -1
 * when none was saved, and 0 before the first MusicCd_Initialize. */
// GLOBAL: XVT 0x622BC8
int g_musicCdSavedAuxVolume = 0;
/* Each track's length for flight music, filled by MusicCd_Initialize and
 * cleared when the device closes. */
// GLOBAL: XVT 0x622BCC
struct MusicCdTrackCache g_musicCdTrackCache = {0};
/* Track MusicCd_PlayTrackFromTime last started for flight music, 0 when none;
 * MusicCd_Initialize, MusicCd_StopTrack and MusicCd_CloseDevice set 0. */
// GLOBAL: XVT 0x622C48
int g_musicCdCurrentTrack = 0;
/* MCI device id of the CD opened for flight music, 0 when none is open;
 * MusicCd_Initialize sets it and MusicCd_CloseDevice sets 0.
 * XvtCdTask_BeginFade and XvtFlightTask_Update read it in the modern build. */
// GLOBAL: XVT 0x622C4C
uint32_t g_musicCdMciDeviceId = 0;

/* Opens the CD audio device through MCI for flight music. Returns 0 at once
 * when the window is not up: g_flightMainWindowHandle NULL in the original
 * build, XvtPort_IsInitialized false in the modern one. A device already open
 * is closed and its state cleared first. Sets g_musicCdSavedAuxVolume to -1,
 * then to the low 16 bits of the volume of the first auxiliary device that is a
 * CD audio device with volume control and whose volume reads. Opens "cdaudio"
 * into g_musicCdMciDeviceId, sets the time format to tracks, minutes, seconds
 * and frames, reads the track count into g_musicCdTrackCount and each track's
 * length into g_musicCdTrackCache. Returns 1; 0 when the open fails, or when a
 * later step fails, after closing the device and setting g_musicCdMciDeviceId
 * to 0. Does not check the track count against the cache's 30 entries.
 * Flight_MainLoop calls it in the original build, XvtFlightLoading_Runtime in
 * the modern one. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4A4EC0
int MusicCd_Initialize(void)
{
	struct {
		uint32_t savedVolume; /* Volume auxGetVolume reads. */
		/* Track count and length queries. */
		MCI_STATUS_PARMS statusParameters;
		MCI_SET_PARMS setParameters; /* Time format to set. */
		/* Open request for "cdaudio". */
		MCI_OPEN_PARMSA openParameters;
		AUXCAPSA deviceCaps; /* Capabilities of the device looked at. */
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
		memset(g_musicCdTrackCache.trackLengthMsfByTrack, 0,
		       sizeof(g_musicCdTrackCache.trackLengthMsfByTrack));
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
				memset(&parameters.deviceCaps, 0,
				       sizeof(parameters.deviceCaps));
				auxGetDevCapsA(deviceIndex,
					       &parameters.deviceCaps,
					       sizeof(parameters.deviceCaps));
				if (parameters.deviceCaps.wTechnology ==
					    AUXCAPS_CDAUDIO &&
				    (parameters.deviceCaps.dwSupport &
				     AUXCAPS_VOLUME) != 0 &&
				    auxGetVolume(deviceIndex,
						 &parameters.savedVolume) ==
					    MMSYSERR_NOERROR) {
					g_musicCdSavedAuxVolume =
						parameters.savedVolume &
						UINT16_MAX;
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
	if (mciSendCommandA(0, MCI_OPEN, MCI_OPEN_TYPE,
			    &parameters.openParameters) != MMSYSERR_NOERROR) {
		g_musicCdMciDeviceId = 0;
		return 0;
	}
	g_musicCdMciDeviceId = parameters.openParameters.wDeviceID;
	parameters.setParameters.dwTimeFormat = MCI_FORMAT_TMSF;
	if (mciSendCommandA(g_musicCdMciDeviceId, MCI_SET, MCI_SET_TIME_FORMAT,
			    &parameters.setParameters) != MMSYSERR_NOERROR) {
		mciSendCommandA(g_musicCdMciDeviceId, MCI_CLOSE, 0, NULL);
		g_musicCdMciDeviceId = 0;
		return 0;
	}

	{
		MMRESULT result;
		uint32_t deviceId;
		uint32_t trackCount;
		unsigned int trackNumber;

		parameters.statusParameters.dwItem =
			MCI_STATUS_NUMBER_OF_TRACKS;
		if (mciSendCommandA(
			    g_musicCdMciDeviceId, MCI_STATUS, MCI_STATUS_ITEM,
			    &parameters.statusParameters) != MMSYSERR_NOERROR) {
			mciSendCommandA(g_musicCdMciDeviceId, MCI_CLOSE, 0,
					NULL);
			g_musicCdMciDeviceId = 0;
			return 0;
		}

		g_musicCdTrackCount =
			(uint32_t)parameters.statusParameters.dwReturn;
		trackNumber = 1;
		if (g_musicCdTrackCount >= trackNumber) {
			deviceId = g_musicCdMciDeviceId;
			do {
				parameters.statusParameters.dwItem =
					MCI_STATUS_LENGTH;
				parameters.statusParameters.dwTrack =
					trackNumber;
				result = mciSendCommandA(
					deviceId, MCI_STATUS,
					MCI_STATUS_ITEM | MCI_TRACK,
					&parameters.statusParameters);
				trackCount = g_musicCdTrackCount;
				deviceId = g_musicCdMciDeviceId;
				if (result != MMSYSERR_NOERROR) {
					mciSendCommandA(deviceId, MCI_CLOSE, 0,
							NULL);
					g_musicCdMciDeviceId = 0;
					return 0;
				}
				g_musicCdTrackCache
					.trackLengthMsfByTrack[trackNumber -
							       1] =
					(unsigned int)parameters
						.statusParameters.dwReturn;
				++trackNumber;
			} while (trackCount >= trackNumber);
		}
		return 1;
	}
}

/* Plays track trackNumber through MCI from startMinute and startSecond to the
 * track's end, with MCI_NOTIFY to g_flightMainWindowHandle. Returns 0 when the
 * track is not 1 to g_musicCdTrackCount, no device is open, or MCI_PLAY fails.
 * Otherwise sets g_musicCdPlaybackComplete to 0 and g_musicCdCurrentTrack to
 * the track number, and returns 1. */
// FUNCTION: XVT 0x4A50D0
int MusicCd_PlayTrackFromTime(int trackNumber, int startMinute, int startSecond)
{
	struct {
		void *callback; /* Window MCI notifies when the play ends. */
		uint32_t from;	/* Start: track, minute, second, frame. */
		uint32_t to;	/* End, the track's length, the same way. */
	} parameters;

	uint32_t deviceId;
	uint32_t toTime;
	unsigned int trackEndMsf;

	if ((int)g_musicCdTrackCount < trackNumber || trackNumber <= 0) {
		return 0;
	}
	deviceId = g_musicCdMciDeviceId;
	if (deviceId == 0) {
		return 0;
	}

	memset(&parameters, 0, sizeof(parameters));
	parameters.from =
		MCI_MAKE_TMSF(trackNumber, startMinute, startSecond, 0);
	trackEndMsf =
		g_musicCdTrackCache.trackLengthMsfByTrack[trackNumber - 1];
	parameters.callback = g_flightMainWindowHandle;
	toTime = MCI_MAKE_TMSF(trackNumber, MCI_MSF_MINUTE(trackEndMsf),
			       MCI_MSF_SECOND(trackEndMsf),
			       MCI_MSF_FRAME(trackEndMsf));
	parameters.to = toTime;
	if (mciSendCommandA(deviceId, MCI_PLAY, MCI_NOTIFY | MCI_FROM | MCI_TO,
			    &parameters) != MMSYSERR_NOERROR) {
		return 0;
	}
	g_musicCdPlaybackComplete = 0;
	g_musicCdCurrentTrack = MCI_TMSF_TRACK(parameters.from);
	return 1;
}

/* Stops the current track with MCI_STOP, sets g_musicCdCurrentTrack and
 * g_musicCdPlaybackComplete to 0 and returns 1; returns 0 when no device is
 * open or no track is current. Nothing calls this. */
// FUNCTION: XVT 0x4A51B0
int MusicCd_StopTrack(void)
{
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

/* Closes the flight music CD device: stops a current track, closes the device
 * and sets g_musicCdMciDeviceId to 0, and clears the cached track lengths,
 * g_musicCdCurrentTrack and g_musicCdPlaybackComplete. When
 * g_musicCdSavedAuxVolume is not -1 it puts that volume back on both channels
 * of every CD audio auxiliary device with volume control; then it sets
 * g_musicCdSavedAuxVolume to -1. Returns 0 when no device is open. After
 * closing one the modern build returns 0; the original build's code ends with
 * no return statement. */
// FUNCTION: XVT 0x4A5210
int MusicCd_CloseDevice(void)
{
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
	memset(g_musicCdTrackCache.trackLengthMsfByTrack, 0,
	       sizeof(g_musicCdTrackCache.trackLengthMsfByTrack));
	g_musicCdCurrentTrack = 0;
	g_musicCdPlaybackComplete = 0;

	deviceIndex = 0;
	deviceCount = (int)auxGetNumDevs();
	if (g_musicCdSavedAuxVolume != -1) {
		stereoVolume = (uint32_t)g_musicCdSavedAuxVolume * 65537;
		if (deviceCount > 0) {
			do {
				memset(&deviceCaps, 0, sizeof(deviceCaps));
				auxGetDevCapsA((uintptr_t)deviceIndex,
					       &deviceCaps, sizeof(deviceCaps));
				if (deviceCaps.wTechnology == AUXCAPS_CDAUDIO &&
				    (deviceCaps.dwSupport & AUXCAPS_VOLUME) !=
					    0) {
					auxSetVolume((uintptr_t)deviceIndex,
						     stereoVolume);
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

/* Returns g_musicCdPlaybackComplete, or 0 when no device is open or no track is
 * current. Nothing calls this. */
// FUNCTION: XVT 0x4A5300
int MusicCd_IsPlaybackComplete(void)
{
	if (g_musicCdMciDeviceId == 0) {
		return 0;
	}
	if (g_musicCdCurrentTrack == 0) {
		return 0;
	}
	return g_musicCdPlaybackComplete;
}

/* Returns g_musicCdMciDeviceId. Nothing calls this. */
// FUNCTION: XVT 0x4A5320
uint32_t MusicCd_GetDeviceId(void) { return g_musicCdMciDeviceId; }

/* Sets g_musicCdPlaybackComplete to 1 and returns 1. Nothing calls this. */
// FUNCTION: XVT 0x4A5330
int MusicCd_MarkPlaybackComplete(void)
{
	g_musicCdPlaybackComplete = 1;
	return 1;
}

/* Returns the cached length of track trackNumber in milliseconds,
 * frames * 1000 / 75 + 1000 * (seconds + 60 * minutes), or 0 when no device is
 * open or the track is not 1 to g_musicCdTrackCount. */
// FUNCTION: XVT 0x4A5340
int MusicCd_GetTrackLengthMs(int trackNumber)
{
	unsigned int trackEndMsf;

	if (g_musicCdMciDeviceId == 0) {
		return 0;
	}
	if (trackNumber <= 0 || trackNumber > (int)g_musicCdTrackCount) {
		return 0;
	}

	trackEndMsf =
		g_musicCdTrackCache.trackLengthMsfByTrack[trackNumber - 1];
	return MCI_MSF_FRAME(trackEndMsf) * 1000 / 75 +
	       1000 * (MCI_MSF_SECOND(trackEndMsf) +
		       60 * MCI_MSF_MINUTE(trackEndMsf));
}

/* Calls CDAudio_SetAuxVolume and returns its result, 1; that also stores the
 * volume in g_frontState.cdAudioTrackCache.currentAuxVolume. */
// FUNCTION: XVT 0x4A53B0
int MusicCd_SetAuxVolume(unsigned int volume0To65535)
{
	return CDAudio_SetAuxVolume(volume0To65535);
}

/* Moves the CD volume from fromVolume to toVolume over about fadeDurationMs
 * milliseconds, as the original build's CDAudio_FadeAuxVolume does but timed
 * with timeGetTime and needing g_musicCdMciDeviceId: returns 0 when no device
 * is open, 1 at once when the volumes are equal, else 1 after stepping the
 * volume 256 at a time with MusicCd_SetAuxVolume until it reaches or passes
 * toVolume. Only the original build calls this, from Flight_MainLoop. */
// FUNCTION: XVT 0x4A53C0
int MusicCd_FadeAuxVolume(unsigned int fromVolume, unsigned int toVolume,
			  int fadeDurationMs)
{
	int fadeUp;
	unsigned int stepDelayMs;
	uint32_t previousTimeMs;
	int currentTimeMs;
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

	previousTimeMs = timeGetTime();
	while (1) {
		currentTimeMs = timeGetTime();
		if ((int)(previousTimeMs + stepDelayMs) < currentTimeMs) {
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
			previousTimeMs = currentTimeMs;
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
