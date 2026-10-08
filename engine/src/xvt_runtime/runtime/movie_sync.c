#include "xvt_runtime/runtime/movie_sync.h"

#include <stdio.h>

#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_string.h"
#include "xvt/frontend/frontend_text.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/movie.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/frontend_net_packets.h"
#include "xvt/net/net.h"
#include "xvt/util/time.h"

void xvt_movie_sync_begin(void)
{
	unsigned int count = net_count_ready_players();
	for (unsigned int index = 0; index < 8; ++index) {
		g_movie_multiplayer_sync_players[index].player_id =
			index < count ? g_mp_roster[index].player_id : 0;
		g_movie_multiplayer_sync_players[index].is_waiting = 0;
	}
	g_movie_playback_completion_state = 0;
	g_movie_multiplayer_sync_deadline_ms = 0;
}

void xvt_movie_sync_report_finished(void)
{
	if (g_movie_playback_completion_state) {
		return;
	}
	g_movie_playback_completion_state = 1;
	for (int index = 0; index < 8; ++index) {
		if (g_movie_multiplayer_sync_players[index].player_id ==
		    net_get_local_player_id()) {
			g_movie_multiplayer_sync_players[index].is_waiting = 1;
		}
	}
	int packet[2] = {NET_PACKET_MOVIE_SYNC, 0};
	net_send_packet_and_flush(0, packet, sizeof(packet));
	g_movie_multiplayer_sync_deadline_ms =
		GetTickCount() + (net_is_host() ? 5000 : 20000);
}

int xvt_movie_sync_update(void)
{
	frontend_net_process_network_packets();
	int still_watching = 0;
	for (int index = 0; index < 8; ++index) {
		if (g_movie_multiplayer_sync_players[index].player_id &&
		    !g_movie_multiplayer_sync_players[index].is_waiting) {
			++still_watching;
		}
	}
	if (g_movie_playback_completion_state == 1 &&
	    (int32_t)(GetTickCount() - g_movie_multiplayer_sync_deadline_ms) >
		    0) {
		g_movie_playback_completion_state = 2;
	}
	return still_watching == 0;
}

void xvt_movie_sync_draw(int top_margin, int bottom_margin)
{
	int count = net_count_ready_players();
	if (!g_movie_playback_completion_state) {
		return;
	}
	struct RECT rect;
	char text[128];
	for (int index = 0; index < 8; ++index) {
		if (!g_movie_multiplayer_sync_players[index].player_id) {
			continue;
		}
		text[0] = 0;
		for (int roster_index = 0;
		     roster_index < count && roster_index < 8; ++roster_index) {
			if (g_mp_roster[roster_index].player_id ==
			    g_movie_multiplayer_sync_players[index].player_id) {
				snprintf(
					text, sizeof(text), "%s%s",
					g_mp_roster[roster_index].name,
					frontend_string_get(
						g_movie_multiplayer_sync_players
								[index]
									.is_waiting
							? FRONTSTR_805_WAITING
							: FRONTSTR_804_WATCHING));
				break;
			}
		}
		rect = (struct RECT){32 + 144 * (index & 3),
				     top_margin / 2 * (index >> 2),
				     32 + 144 * ((index & 3) + 1),
				     top_margin / 2 * ((index >> 2) + 1)};
		frontend_text_draw_centered(12, text, &rect, 0xffff);
	}
	if (g_movie_playback_completion_state == 2 && bottom_margin > 0) {
		rect = (struct RECT){0, 480 - bottom_margin, 639, 479};
		frontend_draw_rect(&rect, 0, 0, 0, -1);
		frontend_text_draw_centered(
			12,
			frontend_string_get(
				net_is_host()
					? FRONTSTR_807_STILL_WAITING_FOR_OTHERS_HIT_C_TO_CONTINUE_THE_GAME
					: FRONTSTR_806_STILL_WAITING_FOR_OTHERS_HIT_E_TO_EXIT_THE_GAME),
			&rect, 0xffff);
	}
}
