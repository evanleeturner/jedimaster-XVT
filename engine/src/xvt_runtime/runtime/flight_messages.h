#ifndef XVT_RUNTIME_FLIGHT_MESSAGES_H
#define XVT_RUNTIME_FLIGHT_MESSAGES_H
#include "xvt/flight/flight_input.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Network125 wire messages: input batches from each player, and world messages that carry the
 * authoritative inputs up to a target tick, split into parts of XVT_WORLD_PART_RECORDS records.
 * Received world messages wait in the pending queue; the replay queue holds messages to confirm
 * again after a state restore. Both queues are byte rings with a message limit. */

typedef enum xvt_flight_queue {
	XVT_QUEUE_PENDING,
	XVT_QUEUE_REPLAY,
	XVT_QUEUE_COUNT
} xvt_flight_queue;

struct xvt_flight_message {
	uint32_t target_flags;
	uint16_t count;
	uint8_t participant_mask;
	struct xvt_flight_world_input_wire records[XVT_WORLD_RECORDS];
};

/* Pushes message, stored with only its count records, onto queue; returns 0 when the queue is out of
 * messages or bytes. */
int xvt_flight_messages_enqueue(const struct xvt_flight_message *message,
				xvt_flight_queue queue);

/* Drops queued messages and a partial assembly targeting tick or earlier, then moves every pending
 * message, in order, onto the replay queue. Returns 0 when the replay queue fills, leaving the
 * rest pending. */
int xvt_flight_messages_prepare_recovery(unsigned tick);

/* Drops the partial world message assembly when it targets tick or earlier. */
void xvt_flight_messages_discard_assembly_through(unsigned tick);

/* The number of parts for count records; 1 for an empty message. */
unsigned xvt_flight_messages_part_count(unsigned count);
/* The number of messages on queue. */
unsigned xvt_flight_messages_count(xvt_flight_queue queue);
/* 1 when queue could take one more message of size bytes (size nonzero). */
int xvt_flight_messages_has_room(xvt_flight_queue queue, size_t size);
/* Writes tick, key, the encoded axes and the throttle, with the throttle-present flag carried in
 * bit 0 of axes[2]; an absent throttle is written as 0. */
void xvt_flight_wire_encode_input(
	struct xvt_flight_input_wire *record, int tick,
	const struct flight_input_frame_record *input);
/* Always stores the tick. Fills input from record and returns 1; returns 0 for an invalid tick or
 * a throttle value without its flag, leaving input untouched. */
int xvt_flight_wire_decode_input(const struct xvt_flight_input_wire *record,
				 int *tick,
				 struct flight_input_frame_record *input);
/* Writes an input batch of count records for cookie into out, which must hold the header and the
 * records. Returns its size, or 0 for a zero cookie, no records or more than
 * XVT_INPUT_BATCH_RECORDS. */
size_t
xvt_flight_messages_encode_batch(uint8_t *out, uint32_t cookie,
				 const struct xvt_flight_input_wire *records,
				 unsigned count);
/* Validates an input batch without extracting it: returns 1 when the opcode, cookie, count and
 * size match and every record decodes with strictly rising ticks. */
int xvt_flight_messages_validate_batch(const uint8_t *bytes, size_t size,
				       uint32_t cookie);
/* Writes part of message for cookie into out. Returns its size, or 0 for a zero cookie, a part out
 * of range or a message over XVT_WORLD_RECORDS records. */
size_t xvt_flight_messages_encode_part(uint8_t *out,
				       const struct xvt_flight_message *message,
				       uint32_t cookie, unsigned part);
/* Returns 1 for a complete message, 0 for pending/old, -1 for invalid. */
/* Assembles one world message at a time; on 1, copies it to out and remembers it as the last
 * assembled. Returns 0 for a tick at or before confirmed or before the last assembled, an exact
 * resend of a part already held, or a part still short of the whole. Returns -1 for a malformed
 * header or record, a record whose player is outside the mask or whose tick passes the target, a
 * resend that differs, a part of another message while one is being assembled, or a completed
 * message whose records are not ordered by player then rising tick; that message stays assembled
 * until discarded or Reset. */
int xvt_flight_messages_receive_part(const uint8_t *bytes, size_t size,
				     uint32_t cookie, int confirmed,
				     struct xvt_flight_message *out);

/* Appends size bytes as one message; returns 0 for size 0 or when the queue is out of messages or
 * bytes. */
int xvt_flight_messages_push(xvt_flight_queue queue, const void *bytes,
			     size_t size);
/* Copies the oldest message into bytes and returns its size; returns 0 when the queue is empty or
 * the message exceeds capacity. */
size_t xvt_flight_messages_peek(xvt_flight_queue queue, void *bytes,
				size_t capacity);
/* Drops the oldest message, if any. */
void xvt_flight_messages_pop(xvt_flight_queue queue);
/* Empties queue. */
void xvt_flight_messages_clear(xvt_flight_queue queue);
/* Empties both queues and forgets the partial and last assembled world messages. */
void xvt_flight_messages_reset(void);
#ifdef __cplusplus
}
#endif
#endif
