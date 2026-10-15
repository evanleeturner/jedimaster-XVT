#ifndef XVT_RUNTIME_DOOR_ADMISSION_H
#define XVT_RUNTIME_DOOR_ADMISSION_H

#include <stdint.h>

#include "xvt_runtime/runtime/launch_description.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Who the host of a network battle started from a description lets in, and
 * the two packets that carry a joiner's asking and the host's answer. The
 * 1997 lobby's admission (any player, by password) does not run in a battle
 * the door starts: the host admits by the description's seats and its setup
 * fingerprint. The caller holds the seat table; nothing here sends, receives
 * or touches the game's state. */

enum {
	/* The door's packet types, past the 1997 lobby's last (125) and in
	 * the range the network layer treats as lobby packets. */
	XVT_DOOR_PACKET_ASK = 200,
	XVT_DOOR_PACKET_ANSWER = 201,
	/* The version of the two packets; a joiner of another is refused. */
	XVT_DOOR_PROTOCOL = 1,
	/* Both packets are three 32-bit words. */
	XVT_DOOR_PACKET_WORDS = 3
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

#ifdef __cplusplus
}
#endif

#endif
