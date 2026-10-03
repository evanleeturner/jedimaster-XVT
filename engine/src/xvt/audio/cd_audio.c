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

/* Opens the CD audio device through MCI for the front end's music. Returns 0 at
 * once when the window is not up: g_frontState.hWnd NULL in the original build,
 * XvtPort_IsInitialized false in the modern one. Closes a device already open
 * with CDAudio_CloseDevice. Takes the first auxiliary device that is a CD audio
 * device with volume control and whose volume reads, and stores the low 16 bits
 * of that volume in g_frontState.cdAudioSavedAuxVolume. Opens "cdaudio" into
 * cdAudioMciDeviceId, sets the time format to tracks, minutes, seconds and
 * frames (MCI_FORMAT_TMSF), and reads the track count into cdAudioTrackCount
 * and each track's length, in minutes, seconds and frames, into
 * cdAudioTrackCache.trackLengthMsfByTrack. Returns 1; 0 when the open fails, or
 * when a later step fails, after closing the device and setting
 * cdAudioMciDeviceId to 0. Does not check the track count against the cache's
 * 40 entries. */
// FUNCTION: XVT 0x4D2E50
int CDAudio_Initialize(void)
{
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
			auxGetDevCapsA(deviceIndex, &deviceCaps,
				       sizeof(deviceCaps));
			if (deviceCaps.wTechnology == AUXCAPS_CDAUDIO &&
			    (deviceCaps.dwSupport & AUXCAPS_VOLUME) != 0 &&
			    auxGetVolume(deviceIndex, &savedVolume) ==
				    MMSYSERR_NOERROR) {
				g_frontState.cdAudioSavedAuxVolume =
					savedVolume & UINT16_MAX;
				break;
			}
		} while (++deviceIndex < deviceCount);
	}

	openParameters.lpstrDeviceType = "cdaudio";
	if (mciSendCommandA(0, MCI_OPEN, MCI_OPEN_TYPE, &openParameters) !=
	    MMSYSERR_NOERROR) {
		g_frontState.cdAudioMciDeviceId = 0;
		return 0;
	}
	g_frontState.cdAudioMciDeviceId = openParameters.wDeviceID;
	setParameters.dwTimeFormat = MCI_FORMAT_TMSF;
	if (mciSendCommandA(g_frontState.cdAudioMciDeviceId, MCI_SET,
			    MCI_SET_TIME_FORMAT,
			    &setParameters) != MMSYSERR_NOERROR) {
		mciSendCommandA(g_frontState.cdAudioMciDeviceId, MCI_CLOSE, 0,
				NULL);
		g_frontState.cdAudioMciDeviceId = 0;
		return 0;
	}

	statusParameters.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
	if (mciSendCommandA(g_frontState.cdAudioMciDeviceId, MCI_STATUS,
			    MCI_STATUS_ITEM,
			    &statusParameters) != MMSYSERR_NOERROR) {
		mciSendCommandA(g_frontState.cdAudioMciDeviceId, MCI_CLOSE, 0,
				NULL);
		g_frontState.cdAudioMciDeviceId = 0;
		return 0;
	}

	g_frontState.cdAudioTrackCount = (int)statusParameters.dwReturn;
	/* From here deviceCount is no longer a device count: it is the 1-based track number of the loop
	 * that caches each track's length. */
	deviceCount = 1;
	if (g_frontState.cdAudioTrackCount < deviceCount) {
		return 1;
	}
	while (1) {
		statusParameters.dwItem = MCI_STATUS_LENGTH;
		statusParameters.dwTrack = deviceCount;
		if (mciSendCommandA(g_frontState.cdAudioMciDeviceId, MCI_STATUS,
				    MCI_STATUS_ITEM | MCI_TRACK,
				    &statusParameters) != MMSYSERR_NOERROR) {
			break;
		}
		g_frontState.cdAudioTrackCache
			.trackLengthMsfByTrack[deviceCount - 1] =
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

/* Plays track trackNumber through MCI from startMinute and startSecond to the
 * track's end, with MCI_NOTIFY to the front end's window. Returns 0 when the
 * track is not 1 to g_frontState.cdAudioTrackCount, no device is open, or
 * MCI_PLAY fails. Otherwise it sets cdAudioCurrentTrack, cdAudioTrackEndMs to
 * the track's whole length plus GetTickCount() plus 2000 whatever the start,
 * cdAudioPlaybackComplete to 0 and cdAudioSuspendState to CDAudio_NotSuspended,
 * and returns 1. Does not check startMinute against the 8 bits it fills in the
 * position. */
// FUNCTION: XVT 0x4D3020
int CDAudio_PlayTrackFromTime(int trackNumber, uint16_t startMinute,
			      uint8_t startSecond)
{
	struct {
		void *callback; /* Window MCI notifies when the play ends. */
		uint32_t from;	/* Start: track, minute, second, frame. */
		uint32_t to;	/* End, the track's length, the same way. */
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
		((uint8_t)trackNumber | ((unsigned int)startMinute << 8)) |
		((unsigned int)startSecond << 16);
	trackEndMsf = g_frontState.cdAudioTrackCache
			      .trackLengthMsfByTrack[trackNumber - 1];
	parameters.to = ((uint8_t)trackNumber |
			 ((unsigned int)(uint8_t)trackEndMsf << 8)) |
			(((unsigned int)(uint8_t)((uint16_t)trackEndMsf >> 8) |
			  ((unsigned int)(uint8_t)(trackEndMsf >> 16) << 8))
			 << 16);
	parameters.callback = g_frontState.hWnd;
	if (mciSendCommandA(g_frontState.cdAudioMciDeviceId, MCI_PLAY,
			    MCI_NOTIFY | MCI_FROM | MCI_TO,
			    &parameters) != MMSYSERR_NOERROR) {
		return 0;
	}

	g_frontState.cdAudioCurrentTrack = trackNumber;
	g_frontState.cdAudioTrackEndMs =
		CDAudio_GetTrackLengthMs(trackNumber) + GetTickCount() + 2000;
	g_frontState.cdAudioPlaybackComplete = 0;
	g_frontState.cdAudioSuspendState = CDAudio_NotSuspended;
	return 1;
}

/* Stops the current track with MCI_STOP, sets g_frontState.cdAudioCurrentTrack
 * and cdAudioPlaybackComplete to 0 and cdAudioSuspendState to
 * CDAudio_NotSuspended, and returns 1. Returns 0 when no device is open or no
 * track is current. */
// FUNCTION: XVT 0x4D3140
int CDAudio_StopCurrentTrack(void)
{
	MCI_GENERIC_PARMS parameters;

	if (g_frontState.cdAudioMciDeviceId == 0) {
		return 0;
	}
	if (g_frontState.cdAudioCurrentTrack == 0) {
		return 0;
	}
	mciSendCommandA(g_frontState.cdAudioMciDeviceId, MCI_STOP, 0,
			&parameters);
	g_frontState.cdAudioCurrentTrack = 0;
	g_frontState.cdAudioPlaybackComplete = 0;
	g_frontState.cdAudioSuspendState = CDAudio_NotSuspended;
	return 1;
}

/* Closes the CD audio device: stops a current track, closes the device and sets
 * g_frontState.cdAudioMciDeviceId to 0, and clears the cached track lengths,
 * cdAudioCurrentTrack, cdAudioPlaybackComplete and cdAudioSuspendState. When
 * cdAudioSavedAuxVolume is not -1 it puts that volume back on both channels of
 * every CD audio auxiliary device with volume control; then it sets
 * cdAudioSavedAuxVolume to -1. Does nothing when no device is open, but the
 * modern build first cancels a volume fade with XvtCdTask_CancelFade. */
// FUNCTION: XVT 0x4D31A0
void CDAudio_CloseDevice(void)
{
	MCI_GENERIC_PARMS parameters;
	AUXCAPSA deviceCaps;
	int deviceIndex;
	int deviceCount;
	uint32_t stereoVolume;
	uint32_t *mciDeviceId;
	int *savedAuxVolume;

#ifdef XVT_MODERN
	XvtCdTask_CancelFade();
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
	memset(g_frontState.cdAudioTrackCache.trackLengthMsfByTrack, 0,
	       sizeof(g_frontState.cdAudioTrackCache.trackLengthMsfByTrack));
	g_frontState.cdAudioCurrentTrack = 0;
	g_frontState.cdAudioPlaybackComplete = 0;
	g_frontState.cdAudioSuspendState = CDAudio_NotSuspended;
	deviceCount = (int)auxGetNumDevs();
	if (*savedAuxVolume != -1) {
		stereoVolume = (uint32_t)*savedAuxVolume * 65537;
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
			} while (++deviceIndex < deviceCount);
		}
	}
	*savedAuxVolume = -1;
}

/* Returns g_frontState.cdAudioPlaybackComplete, 1 once the current track ran
 * out with looping off; 0 when no device is open or no track is current. */
// FUNCTION: XVT 0x4D32A0
int CDAudio_IsPlaybackComplete(void)
{
	if (g_frontState.cdAudioMciDeviceId == 0) {
		return 0;
	}
	if (g_frontState.cdAudioCurrentTrack == 0) {
		return 0;
	}
	return g_frontState.cdAudioPlaybackComplete;
}

/* Returns the cached length of track trackNumber in milliseconds,
 * (minutes * 60 + seconds) * 1000 + frames * 1000 / 75, or 0 when no
 * device is open or the track is not 1 to g_frontState.cdAudioTrackCount.
 * Only CDAudio_PlayTrackFromTime and CDAudio_SuspendPlayback call it. */
// FUNCTION: XVT 0x4D32C0
int CDAudio_GetTrackLengthMs(int trackNumber)
{
	unsigned int trackEndMsf;

	if (g_frontState.cdAudioMciDeviceId == 0) {
		return 0;
	}
	if (trackNumber <= 0 || g_frontState.cdAudioTrackCount < trackNumber) {
		return 0;
	}

	trackEndMsf = g_frontState.cdAudioTrackCache
			      .trackLengthMsfByTrack[trackNumber - 1];
	return (MCI_MSF_MINUTE(trackEndMsf) * 60 +
		MCI_MSF_SECOND(trackEndMsf)) *
		       1000 +
	       MCI_MSF_FRAME(trackEndMsf) * 1000 / 75;
}

/* Sets g_frontState.cdAudioLoopCurrentTrack to 1, so the front end's frame loop
 * plays the current track again from its start when it ends. Returns 1. */
// FUNCTION: XVT 0x4D3330
int CDAudio_EnableLoopCurrentTrack(void)
{
	g_frontState.cdAudioLoopCurrentTrack = 1;
	return 1;
}

/* Sets g_frontState.cdAudioLoopCurrentTrack to 0, so the frame loop marks the
 * track complete when it ends. Returns 1. Credits_UpdateScreen is its only
 * caller. */
// FUNCTION: XVT 0x4D3340
int CDAudio_DisableLoopCurrentTrack(void)
{
	g_frontState.cdAudioLoopCurrentTrack = 0;
	return 1;
}

/* Stops the current track and remembers where it was, for
 * CDAudio_ResumeSuspendedPlayback. While not suspended, with a track
 * current and not complete, it stores the time left,
 * cdAudioTrackEndMs - GetTickCount() - 2000, in
 * g_frontState.cdAudioSuspendRemainingMs and the track length less
 * that in cdAudioSuspendElapsedMs, both in milliseconds, sends
 * MCI_STOP and sets cdAudioSuspendState to CDAudio_Suspended. A
 * pending resume (CDAudio_ResumePending) goes back to
 * CDAudio_Suspended. Returns 1, or 0 when no device is open. */
// FUNCTION: XVT 0x4D3350
int CDAudio_SuspendPlayback(void)
{
	uint32_t trackEndMs;
	MCI_GENERIC_PARMS parameters;

	if (g_frontState.cdAudioMciDeviceId == 0) {
		return 0;
	}

	if (g_frontState.cdAudioSuspendState == CDAudio_NotSuspended) {
		if (g_frontState.cdAudioCurrentTrack != 0 &&
		    g_frontState.cdAudioPlaybackComplete == 0) {
			trackEndMs = g_frontState.cdAudioTrackEndMs;
			g_frontState.cdAudioSuspendRemainingMs =
				trackEndMs - GetTickCount() - 2000;
			g_frontState.cdAudioSuspendElapsedMs =
				(uint32_t)CDAudio_GetTrackLengthMs(
					g_frontState.cdAudioCurrentTrack) -
				g_frontState.cdAudioSuspendRemainingMs;
			mciSendCommandA(g_frontState.cdAudioMciDeviceId,
					MCI_STOP, 0, &parameters);
			g_frontState.cdAudioSuspendState = CDAudio_Suspended;
			return 1;
		}
	} else if (g_frontState.cdAudioSuspendState == CDAudio_ResumePending) {
		g_frontState.cdAudioSuspendState = CDAudio_Suspended;
	}

	return 1;
}

/* When playback is suspended, sets g_frontState.cdAudioSuspendState to
 * CDAudio_ResumePending and cdAudioResumeDueMs to GetTickCount() + 1000; the
 * front end's frame loop resumes the track once that time passes. Returns 1. */
// FUNCTION: XVT 0x4D3400
int CDAudio_RequestResumePlayback(void)
{
	if (g_frontState.cdAudioSuspendState == CDAudio_Suspended) {
		g_frontState.cdAudioSuspendState = CDAudio_ResumePending;
		g_frontState.cdAudioResumeDueMs = GetTickCount() + 1000;
	}
	return 1;
}

/* Plays the current track on from g_frontState.cdAudioSuspendElapsedMs, cut to
 * whole minutes and seconds, with CDAudio_PlayTrackFromTime, then sets
 * cdAudioTrackEndMs to GetTickCount() + cdAudioSuspendRemainingMs + 2000 and
 * cdAudioSuspendState to CDAudio_NotSuspended. Returns 1, also when the play
 * fails. Does not check that playback was suspended. The front end's frame loop
 * calls it: FrontendDisplay_RunMainLoop in the original build, XvtCdTask_Update
 * in the modern one. */
// FUNCTION: XVT 0x4D3430
int CDAudio_ResumeSuspendedPlayback(void)
{
	unsigned int startMinute;
	unsigned int startSecond;

	startMinute = g_frontState.cdAudioSuspendElapsedMs / 60000;
	startSecond = startMinute * 60000;
	startSecond = g_frontState.cdAudioSuspendElapsedMs - startSecond;
	startSecond /= 1000;
	CDAudio_PlayTrackFromTime(g_frontState.cdAudioCurrentTrack,
				  (uint16_t)startMinute, (uint8_t)startSecond);
	g_frontState.cdAudioTrackEndMs =
		GetTickCount() + g_frontState.cdAudioSuspendRemainingMs + 2000;
	g_frontState.cdAudioSuspendState = CDAudio_NotSuspended;
	return 1;
}

/* Sets every CD audio auxiliary device with volume control to volume0To65535,
 * capped at 65535, on both channels, and stores the capped value in
 * g_frontState.cdAudioTrackCache.currentAuxVolume. Returns 1. Needs no open MCI
 * device. */
// FUNCTION: XVT 0x4D34A0
int CDAudio_SetAuxVolume(unsigned int volume0To65535)
{
	unsigned int deviceCount;
	unsigned int stereoVolume;
	unsigned int deviceIndex;
	AUXCAPSA deviceCaps;

	deviceCount = auxGetNumDevs();
	if (volume0To65535 > 65535) {
		volume0To65535 = 65535;
	}
	g_frontState.cdAudioTrackCache.currentAuxVolume = volume0To65535;
	stereoVolume = volume0To65535 * 65537;

	for (deviceIndex = 0; deviceCount > deviceIndex; deviceIndex++) {
		memset(&deviceCaps, 0, sizeof(deviceCaps));
		auxGetDevCapsA(deviceIndex, &deviceCaps, sizeof(deviceCaps));
		if (deviceCaps.wTechnology == AUXCAPS_CDAUDIO &&
		    (deviceCaps.dwSupport & AUXCAPS_VOLUME) != 0) {
			auxSetVolume(deviceIndex, stereoVolume);
		}
	}

	return 1;
}

/* Moves the CD volume from fromVolume to toVolume over about fadeDurationMs
 * milliseconds. The modern build hands the fade to XvtCdTask_BeginFade and
 * returns its result. The original build returns 0 when no device is open and 1
 * at once when the two volumes are equal; otherwise it waits in a loop, moving
 * the volume 256 toward toVolume with CDAudio_SetAuxVolume, kept within 0 to
 * 65535, each time more than (fadeDurationMs << 8) / the difference
 * milliseconds have passed, until it reaches or passes toVolume, and returns
 * 1. */
// FUNCTION: XVT 0x4D3520
int CDAudio_FadeAuxVolume(unsigned int fromVolume, unsigned int toVolume,
			  int fadeDurationMs)
{
#ifdef XVT_MODERN
	return XvtCdTask_BeginFade(fromVolume, toVolume, fadeDurationMs);
#else
	int fadeUp;
	unsigned int stepDelayMs;
	uint32_t previousTimeMs;
	int currentTimeMs;
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

	previousTimeMs = GetTickCount();
	while (1) {
		currentTimeMs = GetTickCount();
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
			CDAudio_SetAuxVolume(fromVolume);
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
#endif
}
