#ifndef XVT_AUDIO_CD_AUDIO_H
#define XVT_AUDIO_CD_AUDIO_H

#include <stdint.h>

#include "xvt/xvt_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cd_audio_track_cache {
	/* CD volume, 0 to 65535, cd_audio_set_aux_volume last set;
	 * frontend_display_init and
	 * frontend_display_reset_global_state_preserving_network_session clear it
	 * with the rest of g_front_state. */
	unsigned int current_aux_volume;
	/* Length of track n + 1 in entry n, as MCI minutes, seconds and frames;
	 * cd_audio_initialize fills it, cd_audio_close_device clears it. */
	unsigned int track_length_msf_by_track[40];
};

typedef enum cd_audio_suspend_state {
	CD_AUDIO_NOT_SUSPENDED = 0x0,
	CD_AUDIO_SUSPENDED = 0x1,
	CD_AUDIO_RESUME_PENDING = 0x2,
} cd_audio_suspend_state;

int cd_audio_initialize(void);
int cd_audio_play_track_from_time(int track_number, uint16_t start_minute,
				  uint8_t start_second);
int cd_audio_stop_current_track(void);
void cd_audio_close_device(void);
int cd_audio_is_playback_complete(void);
int cd_audio_get_track_length_ms(int track_number);
int cd_audio_enable_loop_current_track(void);
int cd_audio_disable_loop_current_track(void);
int cd_audio_suspend_playback(void);
int cd_audio_request_resume_playback(void);
int cd_audio_resume_suspended_playback(void);
int cd_audio_set_aux_volume(unsigned int volume0_to65535);
int cd_audio_fade_aux_volume(unsigned int from_volume, unsigned int to_volume,
			     int fade_duration_ms);

#ifdef __cplusplus
}
#endif

#endif
