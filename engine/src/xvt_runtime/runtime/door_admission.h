#ifndef XVT_RUNTIME_DOOR_ADMISSION_H
#define XVT_RUNTIME_DOOR_ADMISSION_H

#include <stdint.h>

#include "aeron/compat/dplay_directory.h"
#include "xvt_runtime/runtime/launch_description.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Who the host of a network battle started from a description lets in, and
 * the three packets that carry a joiner's asking, the host's answer and the
 * host's start of the battle once every seat is in. The
 * 1997 lobby's admission (any player, by password) does not run in a battle
 * the door starts: the host admits by the description's seats and its setup
 * fingerprint, and a joiner finds the host's room by the battle's id in place
 * of the browser screen. The caller holds the seat table; nothing here sends,
 * receives or touches the game's state. */

enum {
	/* The door's packet types, past the 1997 lobby's last (125) and in
	 * the range the network layer treats as lobby packets. */
	XVT_DOOR_PACKET_ASK = 200,
	XVT_DOOR_PACKET_ANSWER = 201,
	XVT_DOOR_PACKET_START = 202,
	/* The version of the door's packets; a joiner of another is refused.
	 * Version 1 had no start packet. */
	XVT_DOOR_PROTOCOL = 2,
	/* The asking and the answer are three 32-bit words. */
	XVT_DOOR_PACKET_WORDS = 3,
	/* The start is its type, the seat count, and a player id and a rating
	 * for each of the most seats a battle has. */
	XVT_DOOR_START_WORDS = 2 + 2 * XVT_LAUNCH_SEAT_CAPACITY
};

enum xvt_door_verdict {
	XVT_DOOR_ADMIT = 0,
	XVT_DOOR_REFUSE_PROTOCOL = 1,
	XVT_DOOR_REFUSE_SETUP = 2,
	XVT_DOOR_REFUSE_NOT_LISTED = 3,
	XVT_DOOR_REFUSE_SEAT_TAKEN = 4,
	XVT_DOOR_REFUSE_HOST_SEAT = 5,
	XVT_DOOR_VERDICT_COUNT = 6
};

/* The host's seats: the description's pilots in seat order, which of them
 * are in, and the host's setup fingerprint. */
struct xvt_door_seats {
	int count;
	char names[XVT_LAUNCH_SEAT_CAPACITY][XVT_LAUNCH_NAME_CAPACITY];
	int admitted[XVT_LAUNCH_SEAT_CAPACITY];
	uint32_t setup;
};

/* The battle's seats as the host starts it, in seat order: the player id
 * the session gave each seat's game and that pilot's rating. */
struct xvt_door_start {
	int count;
	uint32_t players[XVT_LAUNCH_SEAT_CAPACITY];
	int32_t ratings[XVT_LAUNCH_SEAT_CAPACITY];
};

/* Fills seats from a network description: its players, the setup
 * fingerprint, and seat 0, the host's own, marked in. */
void xvt_door_seats_fill(struct xvt_door_seats *seats,
			 const struct xvt_launch_description *description);
/* Decides on a joiner whose game plays as name (any letter case) and asked
 * with protocol and setup. The checks run in this order: the protocol, the
 * setup fingerprint, the name among the seats, the host's own seat, a seat
 * already in. On XVT_DOOR_ADMIT marks the seat in and puts its index in
 * *seat; on a refusal leaves seats as they were and *seat at -1. */
enum xvt_door_verdict xvt_door_admit(struct xvt_door_seats *seats,
				     const char *name, int protocol,
				     uint32_t setup, int *seat);
/* 1 when every seat is in, else 0. */
int xvt_door_seats_full(const struct xvt_door_seats *seats);
/* Frees seat, whose game left before the start, so that it can be taken
 * again. The host's own seat, 0, and a seat outside the count stay as they
 * are. */
void xvt_door_seat_free(struct xvt_door_seats *seats, int seat);
/* A plain sentence for a verdict, for the host's log and the joiner's
 * result file; a number outside the verdicts gets a sentence saying so. */
const char *xvt_door_verdict_sentence(int verdict);

/* Fills packet with the asking: the type, XVT_DOOR_PROTOCOL and setup. */
void xvt_door_make_ask(int32_t packet[XVT_DOOR_PACKET_WORDS], uint32_t setup);
/* Fills packet with the answer: the type, the verdict and the seat (-1 on a
 * refusal). */
void xvt_door_make_answer(int32_t packet[XVT_DOOR_PACKET_WORDS],
			  enum xvt_door_verdict verdict, int seat);
/* Reads an asking of size bytes. Returns 1 and fills protocol and setup, or
 * 0 when the packet is not an asking of exactly three words. */
int xvt_door_read_ask(const int32_t *packet, uint32_t size, int *protocol,
		      uint32_t *setup);
/* Reads an answer of size bytes. Returns 1 and fills verdict and seat, or 0
 * when the packet is not an answer of exactly three words. */
int xvt_door_read_answer(const int32_t *packet, uint32_t size, int *verdict,
			 int *seat);
/* Fills packet with the start: the type, start's count, then each seat's
 * player id and rating, the words past the count's seats zero. */
void xvt_door_make_start(int32_t packet[XVT_DOOR_START_WORDS],
			 const struct xvt_door_start *start);
/* Reads a start of size bytes. Returns 1 and fills start, its entries past
 * the count zero, or 0 when the packet is not a start of exactly
 * XVT_DOOR_START_WORDS words, its count is outside 1 to
 * XVT_LAUNCH_SEAT_CAPACITY, or a seat under the count holds player id 0. */
int xvt_door_read_start(const int32_t *packet, uint32_t size,
			struct xvt_door_start *start);

/* The battle's room among snapshot's rooms, which the matchmaker lists under
 * the battle's own application id: the first that takes joiners and lists
 * host, the host's pilot name, among its players in any letter case. Returns
 * its index, or -1 when no room does. */
int xvt_door_find_room(const AeronDplayDirectorySnapshot *snapshot,
		       const char *host);

#ifdef __cplusplus
}
#endif

#endif
