#include "xvt/audio/cd_audio.h"
#include "xvt_runtime/runtime/cd_task.h"
#include "xvt_runtime/runtime/port.h"
#include <string.h>

#include "aeron/compat/mmsystem.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log_both_builds.h"

/* Opens the CD audio device through MCI for the front end's music. Returns 0 at
 * once when the window is not up: g_front_state.hWnd NULL in the original build,
 * xvt_port_is_initialized false in the modern one. Closes a device already open
 * with cd_audio_close_device. Takes the first auxiliary device that is a CD audio
 * device with volume control and whose volume reads, and stores the low 16 bits
 * of that volume in g_front_state.cd_audio_saved_aux_volume. Opens "cdaudio" into
 * cd_audio_mci_device_id, sets the time format to tracks, minutes, seconds and
 * frames (MCI_FORMAT_TMSF), and reads the track count into cd_audio_track_count
 * and each track's length, in minutes, seconds and frames, into
 * cd_audio_track_cache.track_length_msf_by_track. Returns 1; 0 when the open fails, or
 * when a later step fails, after closing the device and setting
 * cd_audio_mci_device_id to 0. Does not check the track count against the cache's
 * 40 entries. */
// FUNCTION: XVT 0x4D2E50
int cd_audio_initialize(void)
{
	if (!xvt_port_is_initialized()) {
		return 0;
	}
	if (g_front_state.cd_audio_mci_device_id != 0) {
		cd_audio_close_device();
	}

	int device_index = 0;
	int device_count = (int)auxGetNumDevs();
	uint32_t saved_volume;
	AUXCAPSA device_caps;
	if (device_count > 0) {
		do {
			memset(&device_caps, 0, sizeof(device_caps));
			auxGetDevCapsA(device_index, &device_caps,
				       sizeof(device_caps));
			if (device_caps.wTechnology == AUXCAPS_CDAUDIO &&
			    (device_caps.dwSupport & AUXCAPS_VOLUME) != 0 &&
			    auxGetVolume(device_index, &saved_volume) ==
				    MMSYSERR_NOERROR) {
				g_front_state.cd_audio_saved_aux_volume =
					saved_volume & UINT16_MAX;
				break;
			}
		} while (++device_index < device_count);
	}

	MCI_OPEN_PARMSA open_parameters;
	open_parameters.lpstrDeviceType = "cdaudio";
	if (mciSendCommandA(0, MCI_OPEN, MCI_OPEN_TYPE, &open_parameters) !=
	    MMSYSERR_NOERROR) {
		XVT_LOG_WARN(
			"music.device_failed site=\"menus\" step=\"open\"");
		g_front_state.cd_audio_mci_device_id = 0;
		return 0;
	}
	g_front_state.cd_audio_mci_device_id = open_parameters.wDeviceID;
	MCI_SET_PARMS set_parameters;
	set_parameters.dwTimeFormat = MCI_FORMAT_TMSF;
	if (mciSendCommandA(g_front_state.cd_audio_mci_device_id, MCI_SET,
			    MCI_SET_TIME_FORMAT,
			    &set_parameters) != MMSYSERR_NOERROR) {
		XVT_LOG_WARN(
			"music.device_failed site=\"menus\" step=\"time_format\"");
		mciSendCommandA(g_front_state.cd_audio_mci_device_id, MCI_CLOSE,
				0, NULL);
		g_front_state.cd_audio_mci_device_id = 0;
		return 0;
	}

	MCI_STATUS_PARMS status_parameters;
	status_parameters.dwItem = MCI_STATUS_NUMBER_OF_TRACKS;
	if (mciSendCommandA(g_front_state.cd_audio_mci_device_id, MCI_STATUS,
			    MCI_STATUS_ITEM,
			    &status_parameters) != MMSYSERR_NOERROR) {
		XVT_LOG_WARN(
			"music.device_failed site=\"menus\" step=\"track_count\"");
		mciSendCommandA(g_front_state.cd_audio_mci_device_id, MCI_CLOSE,
				0, NULL);
		g_front_state.cd_audio_mci_device_id = 0;
		return 0;
	}

	g_front_state.cd_audio_track_count = (int)status_parameters.dwReturn;
	/* From here deviceCount is no longer a device count: it is the 1-based
	 * track number of the loop that caches each track's length. */
	device_count = 1;
	if (g_front_state.cd_audio_track_count < device_count) {
		XVT_LOG_INFO("music.device_opened site=\"menus\" tracks=%d",
			     g_front_state.cd_audio_track_count);
		return 1;
	}
	while (1) {
		status_parameters.dwItem = MCI_STATUS_LENGTH;
		status_parameters.dwTrack = device_count;
		if (mciSendCommandA(g_front_state.cd_audio_mci_device_id,
				    MCI_STATUS, MCI_STATUS_ITEM | MCI_TRACK,
				    &status_parameters) != MMSYSERR_NOERROR) {
			break;
		}
		g_front_state.cd_audio_track_cache
			.track_length_msf_by_track[device_count - 1] =
			(unsigned int)status_parameters.dwReturn;
		device_count++;
		if (g_front_state.cd_audio_track_count < device_count) {
			XVT_LOG_INFO(
				"music.device_opened site=\"menus\" tracks=%d",
				g_front_state.cd_audio_track_count);
			return 1;
		}
	}
	XVT_LOG_WARN(
		"music.device_failed site=\"menus\" step=\"track_length\"");
	mciSendCommandA(g_front_state.cd_audio_mci_device_id, MCI_CLOSE, 0,
			NULL);
	g_front_state.cd_audio_mci_device_id = 0;
	return 0;
}

/* Plays track track_number through MCI from start_minute and start_second to the
 * track's end, with MCI_NOTIFY to the front end's window. Returns 0 when the
 * track is not 1 to g_front_state.cd_audio_track_count, no device is open, or
 * MCI_PLAY fails. Otherwise it sets cd_audio_current_track, cd_audio_track_end_ms to
 * the track's whole length plus GetTickCount() plus 2000 whatever the start,
 * cd_audio_playback_complete to 0 and cd_audio_suspend_state to CD_AUDIO_NOT_SUSPENDED,
 * and returns 1. Does not check start_minute against the 8 bits it fills in the
 * position. */
// FUNCTION: XVT 0x4D3020
int cd_audio_play_track_from_time(int track_number, uint16_t start_minute,
				  uint8_t start_second)
{
	struct {
		void *callback; /* Window MCI notifies when the play ends. */
		uint32_t from;	/* Start: track, minute, second, frame. */
		uint32_t to;	/* End, the track's length, the same way. */
	} parameters;

	if (g_front_state.cd_audio_track_count < track_number ||
	    track_number <= 0) {
		XVT_LOG_DEBUG("music.track_unavailable track=%d tracks=%d",
			      track_number, g_front_state.cd_audio_track_count);
		return 0;
	}
	if (g_front_state.cd_audio_mci_device_id == 0) {
		return 0;
	}

	memset(&parameters, 0, sizeof(parameters));
	parameters.from =
		((uint8_t)track_number | ((unsigned int)start_minute << 8)) |
		((unsigned int)start_second << 16);
	unsigned int track_end_msf =
		g_front_state.cd_audio_track_cache
			.track_length_msf_by_track[track_number - 1];
	parameters.to =
		((uint8_t)track_number |
		 ((unsigned int)(uint8_t)track_end_msf << 8)) |
		(((unsigned int)(uint8_t)((uint16_t)track_end_msf >> 8) |
		  ((unsigned int)(uint8_t)(track_end_msf >> 16) << 8))
		 << 16);
	parameters.callback = g_front_state.hWnd;
	if (mciSendCommandA(g_front_state.cd_audio_mci_device_id, MCI_PLAY,
			    MCI_NOTIFY | MCI_FROM | MCI_TO,
			    &parameters) != MMSYSERR_NOERROR) {
		XVT_LOG_WARN("music.play_failed site=\"menus\" track=%d",
			     track_number);
		return 0;
	}

	g_front_state.cd_audio_current_track = track_number;
	g_front_state.cd_audio_track_end_ms =
		cd_audio_get_track_length_ms(track_number) + GetTickCount() +
		2000;
	g_front_state.cd_audio_playback_complete = 0;
	g_front_state.cd_audio_suspend_state = CD_AUDIO_NOT_SUSPENDED;
	XVT_LOG_DEBUG("music.track_started track=%d minute=%d second=%d",
		      track_number, (int)start_minute, (int)start_second);
	return 1;
}

/* Stops the current track with MCI_STOP, sets g_front_state.cd_audio_current_track
 * and cd_audio_playback_complete to 0 and cd_audio_suspend_state to
 * CD_AUDIO_NOT_SUSPENDED, and returns 1. Returns 0 when no device is open or no
 * track is current. */
// FUNCTION: XVT 0x4D3140
int cd_audio_stop_current_track(void)
{
	if (g_front_state.cd_audio_mci_device_id == 0) {
		return 0;
	}
	if (g_front_state.cd_audio_current_track == 0) {
		return 0;
	}
	MCI_GENERIC_PARMS parameters;
	mciSendCommandA(g_front_state.cd_audio_mci_device_id, MCI_STOP, 0,
			&parameters);
	g_front_state.cd_audio_current_track = 0;
	XVT_LOG_DEBUG("music.track_stopped");
	g_front_state.cd_audio_playback_complete = 0;
	g_front_state.cd_audio_suspend_state = CD_AUDIO_NOT_SUSPENDED;
	return 1;
}

/* Closes the CD audio device: stops a current track, closes the device and sets
 * g_front_state.cd_audio_mci_device_id to 0, and clears the cached track lengths,
 * cd_audio_current_track, cd_audio_playback_complete and cd_audio_suspend_state. When
 * cd_audio_saved_aux_volume is not -1 it puts that volume back on both channels of
 * every CD audio auxiliary device with volume control; then it sets
 * cd_audio_saved_aux_volume to -1. Does nothing when no device is open, but the
 * modern build first cancels a volume fade with xvt_cd_task_cancel_fade. */
// FUNCTION: XVT 0x4D31A0
void cd_audio_close_device(void)
{
	xvt_cd_task_cancel_fade();
	if (g_front_state.cd_audio_mci_device_id == 0) {
		return;
	}
	uint32_t *mci_device_id = &g_front_state.cd_audio_mci_device_id;
	int *saved_aux_volume = &g_front_state.cd_audio_saved_aux_volume;
	if (g_front_state.cd_audio_current_track != 0) {
		MCI_GENERIC_PARMS parameters;
		mciSendCommandA(*mci_device_id, MCI_STOP, 0, &parameters);
		g_front_state.cd_audio_current_track = 0;
		g_front_state.cd_audio_playback_complete = 0;
	}
	int device_index = 0;
	mciSendCommandA(*mci_device_id, MCI_CLOSE, 0, NULL);
	*mci_device_id = 0;
	memset(g_front_state.cd_audio_track_cache.track_length_msf_by_track, 0,
	       sizeof(g_front_state.cd_audio_track_cache
			      .track_length_msf_by_track));
	g_front_state.cd_audio_current_track = 0;
	g_front_state.cd_audio_playback_complete = 0;
	g_front_state.cd_audio_suspend_state = CD_AUDIO_NOT_SUSPENDED;
	int device_count = (int)auxGetNumDevs();
	AUXCAPSA device_caps;
	if (*saved_aux_volume != -1) {
		uint32_t stereo_volume = (uint32_t)*saved_aux_volume * 65537;
		if (device_count > 0) {
			do {
				memset(&device_caps, 0, sizeof(device_caps));
				auxGetDevCapsA((uintptr_t)device_index,
					       &device_caps,
					       sizeof(device_caps));
				if (device_caps.wTechnology ==
					    AUXCAPS_CDAUDIO &&
				    (device_caps.dwSupport & AUXCAPS_VOLUME) !=
					    0) {
					auxSetVolume((uintptr_t)device_index,
						     stereo_volume);
				}
			} while (++device_index < device_count);
		}
	}
	XVT_LOG_DEBUG("music.device_closed site=\"menus\" restored=%d",
		      (int)(g_front_state.cd_audio_saved_aux_volume != -1));
	*saved_aux_volume = -1;
}

/* Returns g_front_state.cd_audio_playback_complete, 1 once the current track ran
 * out with looping off; 0 when no device is open or no track is current. */
// FUNCTION: XVT 0x4D32A0
int cd_audio_is_playback_complete(void)
{
	if (g_front_state.cd_audio_mci_device_id == 0) {
		return 0;
	}
	if (g_front_state.cd_audio_current_track == 0) {
		return 0;
	}
	return g_front_state.cd_audio_playback_complete;
}

/* Returns the cached length of track track_number in milliseconds,
 * (minutes * 60 + seconds) * 1000 + frames * 1000 / 75, or 0 when no
 * device is open or the track is not 1 to g_front_state.cd_audio_track_count.
 * Only cd_audio_play_track_from_time and cd_audio_suspend_playback call it. */
// FUNCTION: XVT 0x4D32C0
int cd_audio_get_track_length_ms(int track_number)
{
	if (g_front_state.cd_audio_mci_device_id == 0) {
		return 0;
	}
	if (track_number <= 0 ||
	    g_front_state.cd_audio_track_count < track_number) {
		return 0;
	}

	unsigned int track_end_msf =
		g_front_state.cd_audio_track_cache
			.track_length_msf_by_track[track_number - 1];
	return (MCI_MSF_MINUTE(track_end_msf) * 60 +
		MCI_MSF_SECOND(track_end_msf)) *
		       1000 +
	       MCI_MSF_FRAME(track_end_msf) * 1000 / 75;
}

/* Sets g_front_state.cd_audio_loop_current_track to 1, so the front end's frame loop
 * plays the current track again from its start when it ends. Returns 1. */
// FUNCTION: XVT 0x4D3330
int cd_audio_enable_loop_current_track(void)
{
	g_front_state.cd_audio_loop_current_track = 1;
	return 1;
}

/* Sets g_front_state.cd_audio_loop_current_track to 0, so the frame loop marks the
 * track complete when it ends. Returns 1. credits_update_screen is its only
 * caller. */
// FUNCTION: XVT 0x4D3340
int cd_audio_disable_loop_current_track(void)
{
	g_front_state.cd_audio_loop_current_track = 0;
	return 1;
}

/* Stops the current track and remembers where it was, for
 * cd_audio_resume_suspended_playback. While not suspended, with a track
 * current and not complete, it stores the time left,
 * cd_audio_track_end_ms - GetTickCount() - 2000, in
 * g_front_state.cd_audio_suspend_remaining_ms and the track length less
 * that in cd_audio_suspend_elapsed_ms, both in milliseconds, sends
 * MCI_STOP and sets cd_audio_suspend_state to CD_AUDIO_SUSPENDED. A
 * pending resume (CD_AUDIO_RESUME_PENDING) goes back to
 * CD_AUDIO_SUSPENDED. Returns 1, or 0 when no device is open. */
// FUNCTION: XVT 0x4D3350
int cd_audio_suspend_playback(void)
{
	if (g_front_state.cd_audio_mci_device_id == 0) {
		return 0;
	}

	if (g_front_state.cd_audio_suspend_state == CD_AUDIO_NOT_SUSPENDED) {
		if (g_front_state.cd_audio_current_track != 0 &&
		    g_front_state.cd_audio_playback_complete == 0) {
			uint32_t track_end_ms =
				g_front_state.cd_audio_track_end_ms;
			g_front_state.cd_audio_suspend_remaining_ms =
				track_end_ms - GetTickCount() - 2000;
			g_front_state.cd_audio_suspend_elapsed_ms =
				(uint32_t)cd_audio_get_track_length_ms(
					g_front_state.cd_audio_current_track) -
				g_front_state.cd_audio_suspend_remaining_ms;
			MCI_GENERIC_PARMS parameters;
			mciSendCommandA(g_front_state.cd_audio_mci_device_id,
					MCI_STOP, 0, &parameters);
			g_front_state.cd_audio_suspend_state =
				CD_AUDIO_SUSPENDED;
			XVT_LOG_DEBUG(
				"music.suspended track=%d remaining_ms=%u elapsed_ms=%u",
				g_front_state.cd_audio_current_track,
				(unsigned)g_front_state
					.cd_audio_suspend_remaining_ms,
				(unsigned)g_front_state
					.cd_audio_suspend_elapsed_ms);
			return 1;
		}
	} else if (g_front_state.cd_audio_suspend_state ==
		   CD_AUDIO_RESUME_PENDING) {
		g_front_state.cd_audio_suspend_state = CD_AUDIO_SUSPENDED;
	}

	return 1;
}

/* When playback is suspended, sets g_front_state.cd_audio_suspend_state to
 * CD_AUDIO_RESUME_PENDING and cd_audio_resume_due_ms to GetTickCount() + 1000; the
 * front end's frame loop resumes the track once that time passes. Returns 1. */
// FUNCTION: XVT 0x4D3400
int cd_audio_request_resume_playback(void)
{
	if (g_front_state.cd_audio_suspend_state == CD_AUDIO_SUSPENDED) {
		g_front_state.cd_audio_suspend_state = CD_AUDIO_RESUME_PENDING;
		g_front_state.cd_audio_resume_due_ms = GetTickCount() + 1000;
	}
	return 1;
}

/* Plays the current track on from g_front_state.cd_audio_suspend_elapsed_ms, cut to
 * whole minutes and seconds, with cd_audio_play_track_from_time, then sets
 * cd_audio_track_end_ms to GetTickCount() + cd_audio_suspend_remaining_ms + 2000 and
 * cd_audio_suspend_state to CD_AUDIO_NOT_SUSPENDED. Returns 1, also when the play
 * fails. Does not check that playback was suspended. The front end's frame loop
 * calls it: frontend_display_run_main_loop in the original build, xvt_cd_task_update
 * in the modern one. */
// FUNCTION: XVT 0x4D3430
int cd_audio_resume_suspended_playback(void)
{
	unsigned int start_minute =
		g_front_state.cd_audio_suspend_elapsed_ms / 60000;
	unsigned int start_second = start_minute * 60000;
	start_second = g_front_state.cd_audio_suspend_elapsed_ms - start_second;
	start_second /= 1000;
	cd_audio_play_track_from_time(g_front_state.cd_audio_current_track,
				      (uint16_t)start_minute,
				      (uint8_t)start_second);
	g_front_state.cd_audio_track_end_ms =
		GetTickCount() + g_front_state.cd_audio_suspend_remaining_ms +
		2000;
	g_front_state.cd_audio_suspend_state = CD_AUDIO_NOT_SUSPENDED;
	XVT_LOG_DEBUG("music.resumed track=%d from_ms=%u remaining_ms=%u",
		      g_front_state.cd_audio_current_track,
		      (unsigned)g_front_state.cd_audio_suspend_elapsed_ms,
		      (unsigned)g_front_state.cd_audio_suspend_remaining_ms);
	return 1;
}

/* Sets every CD audio auxiliary device with volume control to volume0_to65535,
 * capped at 65535, on both channels, and stores the capped value in
 * g_front_state.cd_audio_track_cache.current_aux_volume. Returns 1. Needs no open MCI
 * device. */
// FUNCTION: XVT 0x4D34A0
int cd_audio_set_aux_volume(unsigned int volume0_to65535)
{
	unsigned int device_count = auxGetNumDevs();
	if (volume0_to65535 > 65535) {
		volume0_to65535 = 65535;
	}
	g_front_state.cd_audio_track_cache.current_aux_volume = volume0_to65535;
	unsigned int stereo_volume = volume0_to65535 * 65537;

	AUXCAPSA device_caps;
	for (unsigned int device_index = 0; device_count > device_index;
	     device_index++) {
		memset(&device_caps, 0, sizeof(device_caps));
		auxGetDevCapsA(device_index, &device_caps, sizeof(device_caps));
		if (device_caps.wTechnology == AUXCAPS_CDAUDIO &&
		    (device_caps.dwSupport & AUXCAPS_VOLUME) != 0) {
			auxSetVolume(device_index, stereo_volume);
		}
	}

	return 1;
}

/* Moves the CD volume from from_volume to to_volume over about fade_duration_ms
 * milliseconds. The modern build hands the fade to xvt_cd_task_begin_fade and
 * returns its result. The original build returns 0 when no device is open and 1
 * at once when the two volumes are equal; otherwise it waits in a loop, moving
 * the volume 256 toward to_volume with cd_audio_set_aux_volume, kept within 0 to
 * 65535, each time more than (fade_duration_ms << 8) / the difference
 * milliseconds have passed, until it reaches or passes to_volume, and returns
 * 1. */
// FUNCTION: XVT 0x4D3520
int cd_audio_fade_aux_volume(unsigned int from_volume, unsigned int to_volume,
			     int fade_duration_ms)
{
	XVT_LOG_DEBUG("music.fade_started from=%u to=%u ms=%d", from_volume,
		      to_volume, fade_duration_ms);
	return xvt_cd_task_begin_fade(from_volume, to_volume, fade_duration_ms);
}
