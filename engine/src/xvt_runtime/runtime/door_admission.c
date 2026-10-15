#include "xvt_runtime/runtime/door_admission.h"

#include <string.h>
#include <strings.h>

/* One sentence per verdict, in the enumeration's order. */
static const char *const g_sentences[XVT_DOOR_VERDICT_COUNT] = {
	"The host let this pilot in.",
	"The host's game speaks another version of the door's packets; both games need the same build.",
	"The host's launcher described another battle: the mission, seed, settings or seats differ.",
	"This pilot is not among the host's seats for this battle.",
	"This pilot's seat is already taken by a game that joined first.",
	"This pilot is the host's own seat; the host does not join its own battle."};

void xvt_door_seats_fill(struct xvt_door_seats *seats,
			 const struct xvt_launch_description *description)
{
	memset(seats, 0, sizeof(*seats));
	seats->count = description->network.player_count;
	memcpy(seats->names, description->network.players,
	       sizeof(seats->names));
	seats->admitted[0] = 1;
	seats->setup = xvt_launch_setup_fingerprint(description);
}

enum xvt_door_verdict xvt_door_admit(struct xvt_door_seats *seats,
				     const char *name, int protocol,
				     uint32_t setup, int *seat)
{
	*seat = -1;
	if (protocol != XVT_DOOR_PROTOCOL) {
		return XVT_DOOR_REFUSE_PROTOCOL;
	}
	if (setup != seats->setup) {
		return XVT_DOOR_REFUSE_SETUP;
	}
	int found = -1;
	for (int index = 0; index < seats->count; ++index) {
		if (!strcasecmp(seats->names[index], name)) {
			found = index;
			break;
		}
	}
	if (found < 0) {
		return XVT_DOOR_REFUSE_NOT_LISTED;
	}
	if (found == 0) {
		return XVT_DOOR_REFUSE_HOST_SEAT;
	}
	if (seats->admitted[found]) {
		return XVT_DOOR_REFUSE_SEAT_TAKEN;
	}
	seats->admitted[found] = 1;
	*seat = found;
	return XVT_DOOR_ADMIT;
}

int xvt_door_seats_full(const struct xvt_door_seats *seats)
{
	for (int index = 0; index < seats->count; ++index) {
		if (!seats->admitted[index]) {
			return 0;
		}
	}
	return 1;
}

const char *xvt_door_verdict_sentence(int verdict)
{
	if (verdict < 0 || verdict >= XVT_DOOR_VERDICT_COUNT) {
		return "The host answered with a verdict this game does not know.";
	}
	return g_sentences[verdict];
}

void xvt_door_make_ask(int32_t packet[XVT_DOOR_PACKET_WORDS], uint32_t setup)
{
	packet[0] = XVT_DOOR_PACKET_ASK;
	packet[1] = XVT_DOOR_PROTOCOL;
	packet[2] = (int32_t)setup;
}

void xvt_door_make_answer(int32_t packet[XVT_DOOR_PACKET_WORDS],
			  enum xvt_door_verdict verdict, int seat)
{
	packet[0] = XVT_DOOR_PACKET_ANSWER;
	packet[1] = (int32_t)verdict;
	packet[2] = verdict == XVT_DOOR_ADMIT ? seat : -1;
}

int xvt_door_read_ask(const int32_t *packet, uint32_t size, int *protocol,
		      uint32_t *setup)
{
	if (!packet || size != XVT_DOOR_PACKET_WORDS * sizeof(int32_t) ||
	    packet[0] != XVT_DOOR_PACKET_ASK) {
		return 0;
	}
	*protocol = packet[1];
	*setup = (uint32_t)packet[2];
	return 1;
}

int xvt_door_read_answer(const int32_t *packet, uint32_t size, int *verdict,
			 int *seat)
{
	if (!packet || size != XVT_DOOR_PACKET_WORDS * sizeof(int32_t) ||
	    packet[0] != XVT_DOOR_PACKET_ANSWER) {
		return 0;
	}
	*verdict = packet[1];
	*seat = packet[2];
	return 1;
}

int xvt_door_find_room(const AeronDplayDirectorySnapshot *snapshot,
		       const char *host)
{
	for (uint32_t index = 0; index < snapshot->room_count &&
				 index < AERON_DPLAY_DIRECTORY_MAX_ROOMS;
	     ++index) {
		const AeronDplayRoomMetadata *room =
			&snapshot->rooms[index].metadata;
		if (!room->joinable) {
			continue;
		}
		for (unsigned player = 0;
		     player < room->players &&
		     player < AERON_DPLAY_DIRECTORY_MAX_PLAYERS;
		     ++player) {
			if (!strcasecmp(room->roster[player].name, host)) {
				return (int)index;
			}
		}
	}
	return -1;
}
