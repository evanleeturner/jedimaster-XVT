#include "xvt/audio/music_cd.h"

#include "aeron/compat/mmsystem.h"
#include "xvt/audio/cd_audio.h"
#include "xvt/flight/flight.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log_both_builds.h"

#ifdef XVT_MODERN
#include "xvt_runtime/runtime/port.h"
#endif

#include <string.h>

/* 1 once the flight music track is marked complete. music_cd_initialize,
 * music_cd_play_track_from_time, music_cd_stop_track and music_cd_close_device set 0;
 * only music_cd_mark_playback_complete sets 1, and nothing calls it, so it stays
 * 0. */
// GLOBAL: XVT 0x622BC0
int g_music_cd_playback_complete = 0;
/* Tracks on the CD, read by music_cd_initialize; nothing resets it. */
// GLOBAL: XVT 0x622BC4
uint32_t g_music_cd_track_count = 0;
/* Volume, 0 to 65535, of the first CD audio auxiliary device with volume
 * control, saved by music_cd_initialize to put back at music_cd_close_device; -1
 * when none was saved, and 0 before the first music_cd_initialize. */
// GLOBAL: XVT 0x622BC8
static int g_music_cd_saved_aux_volume = 0;
/* Each track's length for flight music, filled by music_cd_initialize and
 * cleared when the device closes. */
// GLOBAL: XVT 0x622BCC
struct music_cd_track_cache g_music_cd_track_cache = {0};
/* Track music_cd_play_track_from_time last started for flight music, 0 when none;
 * music_cd_initialize, music_cd_stop_track and music_cd_close_device set 0. */
// GLOBAL: XVT 0x622C48
int g_music_cd_current_track = 0;
/* MCI device id of the CD opened for flight music, 0 when none is open;
 * music_cd_initialize sets it and music_cd_close_device sets 0.
 * xvt_cd_task_begin_fade and xvt_flight_task_update read it in the modern build. */
// GLOBAL: XVT 0x622C4C
uint32_t g_music_cd_mci_device_id = 0;

/* Opens the CD audio device through MCI for flight music. Returns 0 at once
 * when the window is not up: g_flight_main_window_handle NULL in the original
 * build, xvt_port_is_initialized false in the modern one. A device already open
 * is closed and its state cleared first. Sets g_music_cd_saved_aux_volume to -1,
 * then to the low 16 bits of the volume of the first auxiliary device that is a
 * CD audio device with volume control and whose volume reads. Opens "cdaudio"
 * into g_music_cd_mci_device_id, sets the time format to tracks, minutes, seconds
 * and frames, reads the track count into g_music_cd_track_count and each track's
 * length into g_music_cd_track_cache. Returns 1; 0 when the open fails, or when a
 * later step fails, after closing the device and setting g_music_cd_mci_device_id
 * to 0. Does not check the track count against the cache's 30 entries.
 * flight_main_loop calls it in the original build, xvt_flight_loading_runtime in
 * the modern one. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4A4EC0
int music_cd_initialize(void)
{
	struct {
		uint32_t saved_volume; /* Volume auxGetVolume reads. */
		/* Track count and length queries. */
		MCI_STATUS_PARMS status_parameters;
		MCI_SET_PARMS set_parameters; /* Time format to set. */
		/* Open request for "cdaudio". */
		MCI_OPEN_PARMSA open_parameters;
		AUXCAPSA
		device_caps; /* Capabilities of the device looked at. */
	} parameters;

#ifdef XVT_MODERN
	if (!xvt_port_is_initialized()) {
#else
	if (g_flight_main_window_handle == NULL) {
#endif
		return 0;
	}
	if (g_music_cd_mci_device_id != 0) {
		mciSendCommandA(g_music_cd_mci_device_id, MCI_CLOSE, 0, NULL);
		g_music_cd_mci_device_id = 0;
		memset(g_music_cd_track_cache.track_length_msf_by_track, 0,
		       sizeof(g_music_cd_track_cache
				      .track_length_msf_by_track));
		g_music_cd_current_track = 0;
		g_music_cd_playback_complete = 0;
	}

	g_music_cd_saved_aux_volume = -1;
	{
		unsigned int device_index = 0;
		unsigned int device_count = auxGetNumDevs();
		if (device_count > 0) {
			do {
				memset(&parameters.device_caps, 0,
				       sizeof(parameters.device_caps));
				auxGetDevCapsA(device_index,
					       &parameters.device_caps,
					       sizeof(parameters.device_caps));
				if (parameters.device_caps.wTechnology ==
					    AUXCAPS_CDAUDIO &&
				    (parameters.device_caps.dwSupport &
				     AUXCAPS_VOLUME) != 0 &&
				    auxGetVolume(device_index,
						 &parameters.saved_volume) ==
					    MMSYSERR_NOERROR) {
					g_music_cd_saved_aux_volume =
						parameters.saved_volume &
						UINT16_MAX;
					break;
				}
				++device_index;
				if (device_count > device_index) {
					continue;
				}
				break;
			} while (1);
		}
	}

	parameters.open_parameters.lpstrDeviceType = "cdaudio";
	if (mciSendCommandA(0, MCI_OPEN, MCI_OPEN_TYPE,
			    &parameters.open_parameters) != MMSYSERR_NOERROR) {
		XVT_LOG_WARN(
			"music.device_failed site=\"flight\" step=\"open\"");
		g_music_cd_mci_device_id = 0;
		return 0;
	}
	g_music_cd_mci_device_id = parameters.open_parameters.wDeviceID;
	parameters.set_parameters.dwTimeFormat = MCI_FORMAT_TMSF;
	if (mciSendCommandA(g_music_cd_mci_device_id, MCI_SET,
			    MCI_SET_TIME_FORMAT,
			    &parameters.set_parameters) != MMSYSERR_NOERROR) {
		XVT_LOG_WARN(
			"music.device_failed site=\"flight\" step=\"time_format\"");
		mciSendCommandA(g_music_cd_mci_device_id, MCI_CLOSE, 0, NULL);
		g_music_cd_mci_device_id = 0;
		return 0;
	}

	{
		parameters.status_parameters.dwItem =
			MCI_STATUS_NUMBER_OF_TRACKS;
		if (mciSendCommandA(g_music_cd_mci_device_id, MCI_STATUS,
				    MCI_STATUS_ITEM,
				    &parameters.status_parameters) !=
		    MMSYSERR_NOERROR) {
			XVT_LOG_WARN(
				"music.device_failed site=\"flight\" step=\"track_count\"");
			mciSendCommandA(g_music_cd_mci_device_id, MCI_CLOSE, 0,
					NULL);
			g_music_cd_mci_device_id = 0;
			return 0;
		}

		g_music_cd_track_count =
			(uint32_t)parameters.status_parameters.dwReturn;
		unsigned int track_number = 1;
		if (g_music_cd_track_count >= track_number) {
			uint32_t device_id = g_music_cd_mci_device_id;
			uint32_t track_count;
			do {
				parameters.status_parameters.dwItem =
					MCI_STATUS_LENGTH;
				parameters.status_parameters.dwTrack =
					track_number;
				MMRESULT result = mciSendCommandA(
					device_id, MCI_STATUS,
					MCI_STATUS_ITEM | MCI_TRACK,
					&parameters.status_parameters);
				track_count = g_music_cd_track_count;
				device_id = g_music_cd_mci_device_id;
				if (result != MMSYSERR_NOERROR) {
					XVT_LOG_WARN(
						"music.device_failed site=\"flight\" step=\"track_length\"");
					mciSendCommandA(device_id, MCI_CLOSE, 0,
							NULL);
					g_music_cd_mci_device_id = 0;
					return 0;
				}
				g_music_cd_track_cache.track_length_msf_by_track
					[track_number - 1] =
					(unsigned int)parameters
						.status_parameters.dwReturn;
				++track_number;
			} while (track_count >= track_number);
		}
		XVT_LOG_INFO("music.device_opened site=\"flight\" tracks=%d",
			     (int)g_music_cd_track_count);
		return 1;
	}
}

/* Plays track track_number through MCI from start_minute and start_second to the
 * track's end, with MCI_NOTIFY to g_flight_main_window_handle. Returns 0 when the
 * track is not 1 to g_music_cd_track_count, no device is open, or MCI_PLAY fails.
 * Otherwise sets g_music_cd_playback_complete to 0 and g_music_cd_current_track to
 * the track number, and returns 1. */
// FUNCTION: XVT 0x4A50D0
int music_cd_play_track_from_time(int track_number, int start_minute,
				  int start_second)
{
	struct {
		void *callback; /* Window MCI notifies when the play ends. */
		uint32_t from;	/* Start: track, minute, second, frame. */
		uint32_t to;	/* End, the track's length, the same way. */
	} parameters;

	if ((int)g_music_cd_track_count < track_number || track_number <= 0) {
		return 0;
	}
	uint32_t device_id = g_music_cd_mci_device_id;
	if (device_id == 0) {
		return 0;
	}

	memset(&parameters, 0, sizeof(parameters));
	parameters.from =
		MCI_MAKE_TMSF(track_number, start_minute, start_second, 0);
	unsigned int track_end_msf =
		g_music_cd_track_cache
			.track_length_msf_by_track[track_number - 1];
	parameters.callback = g_flight_main_window_handle;
	uint32_t to_time = MCI_MAKE_TMSF(
		track_number, MCI_MSF_MINUTE(track_end_msf),
		MCI_MSF_SECOND(track_end_msf), MCI_MSF_FRAME(track_end_msf));
	parameters.to = to_time;
	if (mciSendCommandA(device_id, MCI_PLAY, MCI_NOTIFY | MCI_FROM | MCI_TO,
			    &parameters) != MMSYSERR_NOERROR) {
		if (g_flight_sim_side_effects_suppressed == 0) {
			XVT_LOG_WARN(
				"music.play_failed site=\"flight\" track=%d",
				track_number);
		}
		return 0;
	}
	g_music_cd_playback_complete = 0;
	g_music_cd_current_track = MCI_TMSF_TRACK(parameters.from);
	XVT_LOG_DEBUG(
		"music.flight_track_started track=%d minute=%d second=%d predicted=%d",
		track_number, start_minute, start_second,
		g_flight_sim_side_effects_suppressed);
	return 1;
}

/* Stops the current track with MCI_STOP, sets g_music_cd_current_track and
 * g_music_cd_playback_complete to 0 and returns 1; returns 0 when no device is
 * open or no track is current. Nothing calls this. */
// FUNCTION: XVT 0x4A51B0
int music_cd_stop_track(void)
{
	if (g_music_cd_mci_device_id == 0) {
		return 0;
	}
	if (g_music_cd_current_track == 0) {
		return 0;
	}
	MCI_GENERIC_PARMS parameters;
	mciSendCommandA(g_music_cd_mci_device_id, MCI_STOP, 0, &parameters);
	g_music_cd_current_track = 0;
	g_music_cd_playback_complete = 0;
	return 1;
}

/* Closes the flight music CD device: stops a current track, closes the device
 * and sets g_music_cd_mci_device_id to 0, and clears the cached track lengths,
 * g_music_cd_current_track and g_music_cd_playback_complete. When
 * g_music_cd_saved_aux_volume is not -1 it puts that volume back on both channels
 * of every CD audio auxiliary device with volume control; then it sets
 * g_music_cd_saved_aux_volume to -1. Returns 0 when no device is open. After
 * closing one the modern build returns 0; the original build's code ends with
 * no return statement. */
// FUNCTION: XVT 0x4A5210
int music_cd_close_device(void)
{
	if (g_music_cd_mci_device_id == 0) {
		return 0;
	}

	if (g_music_cd_current_track != 0) {
		MCI_GENERIC_PARMS parameters;
		mciSendCommandA(g_music_cd_mci_device_id, MCI_STOP, 0,
				&parameters);
		g_music_cd_current_track = 0;
		g_music_cd_playback_complete = 0;
	}
	mciSendCommandA(g_music_cd_mci_device_id, MCI_CLOSE, 0, NULL);
	g_music_cd_mci_device_id = 0;
	memset(g_music_cd_track_cache.track_length_msf_by_track, 0,
	       sizeof(g_music_cd_track_cache.track_length_msf_by_track));
	g_music_cd_current_track = 0;
	g_music_cd_playback_complete = 0;

	int device_index = 0;
	int device_count = (int)auxGetNumDevs();
	AUXCAPSA device_caps;
	if (g_music_cd_saved_aux_volume != -1) {
		uint32_t stereo_volume =
			(uint32_t)g_music_cd_saved_aux_volume * 65537;
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
				++device_index;
			} while (device_index < device_count);
		}
	}
	XVT_LOG_DEBUG("music.device_closed site=\"flight\" restored=%d",
		      (int)(g_music_cd_saved_aux_volume != -1));
	g_music_cd_saved_aux_volume = -1;
#ifdef XVT_MODERN
	return 0;
#endif
}

/* Returns g_music_cd_playback_complete, or 0 when no device is open or no track is
 * current. Nothing calls this. */
// FUNCTION: XVT 0x4A5300
int music_cd_is_playback_complete(void)
{
	if (g_music_cd_mci_device_id == 0) {
		return 0;
	}
	if (g_music_cd_current_track == 0) {
		return 0;
	}
	return g_music_cd_playback_complete;
}

/* Returns g_music_cd_mci_device_id. Nothing calls this. */
// FUNCTION: XVT 0x4A5320
uint32_t music_cd_get_device_id(void) { return g_music_cd_mci_device_id; }

/* Sets g_music_cd_playback_complete to 1 and returns 1. Nothing calls this. */
// FUNCTION: XVT 0x4A5330
int music_cd_mark_playback_complete(void)
{
	g_music_cd_playback_complete = 1;
	return 1;
}

/* Returns the cached length of track track_number in milliseconds,
 * frames * 1000 / 75 + 1000 * (seconds + 60 * minutes), or 0 when no device is
 * open or the track is not 1 to g_music_cd_track_count. */
// FUNCTION: XVT 0x4A5340
int music_cd_get_track_length_ms(int track_number)
{
	if (g_music_cd_mci_device_id == 0) {
		return 0;
	}
	if (track_number <= 0 || track_number > (int)g_music_cd_track_count) {
		return 0;
	}

	unsigned int track_end_msf =
		g_music_cd_track_cache
			.track_length_msf_by_track[track_number - 1];
	return MCI_MSF_FRAME(track_end_msf) * 1000 / 75 +
	       1000 * (MCI_MSF_SECOND(track_end_msf) +
		       60 * MCI_MSF_MINUTE(track_end_msf));
}

/* Calls cd_audio_set_aux_volume and returns its result, 1; that also stores the
 * volume in g_front_state.cd_audio_track_cache.current_aux_volume. */
// FUNCTION: XVT 0x4A53B0
int music_cd_set_aux_volume(unsigned int volume0_to65535)
{
	return cd_audio_set_aux_volume(volume0_to65535);
}

/* Moves the CD volume from from_volume to to_volume over about fade_duration_ms
 * milliseconds, as the original build's cd_audio_fade_aux_volume does but timed
 * with timeGetTime and needing g_music_cd_mci_device_id: returns 0 when no device
 * is open, 1 at once when the volumes are equal, else 1 after stepping the
 * volume 256 at a time with music_cd_set_aux_volume until it reaches or passes
 * to_volume. Only the original build calls this, from flight_main_loop. */
// FUNCTION: XVT 0x4A53C0
int music_cd_fade_aux_volume(unsigned int from_volume, unsigned int to_volume,
			     int fade_duration_ms)
{
	if (g_music_cd_mci_device_id == 0) {
		return 0;
	}
	if (to_volume == from_volume) {
		return 1;
	}
	int fade_up;
	unsigned int step_delay_ms;
	if (to_volume < from_volume) {
		fade_up = 0;
		step_delay_ms =
			(fade_duration_ms << 8) / (from_volume - to_volume);
	} else {
		fade_up = 1;
		step_delay_ms =
			(fade_duration_ms << 8) / (to_volume - from_volume);
	}

	uint32_t previous_time_ms = timeGetTime();
	while (1) {
		int current_time_ms = timeGetTime();
		if ((int)(previous_time_ms + step_delay_ms) < current_time_ms) {
			if (fade_up != 0) {
				unsigned int next_volume = from_volume + 256;
				if (next_volume > 65535) {
					from_volume = 65535;
				} else {
					from_volume = next_volume;
				}
			} else {
				if (from_volume < 256) {
					from_volume = 0;
				} else {
					from_volume -= 256;
				}
			}
			music_cd_set_aux_volume(from_volume);
			previous_time_ms = current_time_ms;
		}
		if (fade_up != 0) {
			if (to_volume <= from_volume) {
				break;
			}
		} else if (to_volume >= from_volume) {
			break;
		}
	}
	return 1;
}
