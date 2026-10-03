#ifndef XVT_AUDIO_MUSIC_CD_H
#define XVT_AUDIO_MUSIC_CD_H

#include "xvt/xvt_typedefs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct music_cd_track_cache {
	unsigned int unused00; /* Nothing reads or writes it by name. */
	/* Length of track n + 1 in entry n, as MCI minutes, seconds and
	 * frames. */
	unsigned int track_length_msf_by_track[30];
};

extern int g_music_cd_playback_complete;
extern int g_music_cd_current_track;
extern uint32_t g_music_cd_mci_device_id;
extern uint32_t g_music_cd_track_count;
extern struct music_cd_track_cache g_music_cd_track_cache;

int music_cd_initialize(void);
int music_cd_play_track_from_time(int track_number, int start_minute,
				  int start_second);
int music_cd_stop_track(void);
int music_cd_close_device(void);
int music_cd_is_playback_complete(void);
uint32_t music_cd_get_device_id(void);
int music_cd_mark_playback_complete(void);
int music_cd_get_track_length_ms(int track_number);
int music_cd_set_aux_volume(unsigned int volume0_to65535);
int music_cd_fade_aux_volume(unsigned int from_volume, unsigned int to_volume,
			     int fade_duration_ms);

#ifdef __cplusplus
}
#endif

#endif
