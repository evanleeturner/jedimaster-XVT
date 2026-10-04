#ifndef XVT_RUNTIME_NETWORK_METADATA_H
#define XVT_RUNTIME_NETWORK_METADATA_H

#include <stddef.h>

#include "aeron/compat/dplay.h"
#include "aeron/compat/dplay_directory.h"

#ifdef __cplusplus
extern "C" {
#endif

struct xvt_network_metadata {
	AeronDplayRoomMetadata room;
	DPID players[AERON_DPLAY_DIRECTORY_MAX_PLAYERS];
};

/* The room advertisement for the multiplayer directory, and conversion between the game's
 * Windows-1252 text and the directory's UTF-8. players[i] is the player id of roster entry i. */

/* Fills out from the current session: its name ("Internet game." when empty),
 * the password flag, 8 player slots, and the pilot's mission when one is
 * selected. The roster lists players marked ready, once each, at most 8, from
 * the authoritative mission roster when set and the session roster otherwise;
 * each entry takes its name from the player's playerName ("No name" when empty)
 * and its rating from the first byte of long_name minus 1 (0 stays 0). The room
 * is joinable when accepting, the roster is not authoritative, and a slot is
 * free. */
void xvt_network_metadata_build(struct xvt_network_metadata *out,
				int accepting);
/* Keeps, in order, only the roster players still active in the network session, and clears the
 * rest. */
void xvt_network_metadata_keep_active_players(
	struct xvt_network_metadata *snapshot);
/* Converts up to size bytes of Windows-1252 text, stopping at a NUL, into UTF-8 in out. Control
 * characters, DEL and the five undefined 1252 bytes become '?'. Stops before a character that
 * would not fit; out is always terminated when capacity is nonzero. */
void xvt_network_metadata_to_utf8(char *out, size_t capacity, const char *text,
				  size_t size);
/* Converts UTF-8 text into Windows-1252 in out, one byte per character. A character 1252 cannot
 * hold, a control character, a surrogate or an overlong form becomes '?'; an invalid lead byte
 * yields one '?' per byte, and a sequence cut short yields one '?' before the byte that broke it
 * is read again. Truncates to
 * capacity - 1 bytes; out is always terminated when capacity is nonzero. */
void xvt_network_metadata_from_utf8(char *out, size_t capacity,
				    const char *text);

#ifdef __cplusplus
}
#endif

#endif
