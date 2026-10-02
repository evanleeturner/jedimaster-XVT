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

typedef enum XvtFlightQueue { XVT_QUEUE_PENDING, XVT_QUEUE_REPLAY, XVT_QUEUE_COUNT } XvtFlightQueue;

typedef struct XvtFlightMessage {
	uint32_t target_flags;
	uint16_t count;
	uint8_t participant_mask;
	XvtFlightWorldInputWire records[XVT_WORLD_RECORDS];
} XvtFlightMessage;

/* Pushes message, stored with only its count records, onto queue; returns 0 when the queue is out of
 * messages or bytes. */
int XvtFlightMessages_Enqueue(const XvtFlightMessage* message, XvtFlightQueue queue);

/* Drops queued messages and a partial assembly targeting tick or earlier, then moves every pending
 * message, in order, onto the replay queue. Returns 0 when the replay queue fills, leaving the
 * rest pending. */
int XvtFlightMessages_PrepareRecovery(unsigned tick);

/* Drops the partial world message assembly when it targets tick or earlier. */
void XvtFlightMessages_DiscardAssemblyThrough(unsigned tick);

/* The number of parts for count records; 1 for an empty message. */
unsigned XvtFlightMessages_PartCount(unsigned count);
/* The number of messages on queue. */
unsigned XvtFlightMessages_Count(XvtFlightQueue queue);
/* 1 when queue could take one more message of size bytes (size nonzero). */
int XvtFlightMessages_HasRoom(XvtFlightQueue queue, size_t size);
/* Writes tick, key, the encoded axes and the throttle, with the throttle-present flag carried in
 * bit 0 of axes[2]; an absent throttle is written as 0. */
void XvtFlightWire_EncodeInput(XvtFlightInputWire* record, int tick, const FlightInputFrameRecord* input);
/* Always stores the tick. Fills input from record and returns 1; returns 0 for an invalid tick or
 * a throttle value without its flag, leaving input untouched. */
int XvtFlightWire_DecodeInput(const XvtFlightInputWire* record, int* tick, FlightInputFrameRecord* input);
/* Writes an input batch of count records for cookie into out, which must hold the header and the
 * records. Returns its size, or 0 for a zero cookie, no records or more than
 * XVT_INPUT_BATCH_RECORDS. */
size_t XvtFlightMessages_EncodeBatch(uint8_t* out, uint32_t cookie, const XvtFlightInputWire* records,
									 unsigned count);
/* Validates an input batch without extracting it: returns 1 when the opcode, cookie, count and
 * size match and every record decodes with strictly rising ticks. */
int XvtFlightMessages_ValidateBatch(const uint8_t* bytes, size_t size, uint32_t cookie);
/* Writes part of message for cookie into out. Returns its size, or 0 for a zero cookie, a part out
 * of range or a message over XVT_WORLD_RECORDS records. */
size_t XvtFlightMessages_EncodePart(uint8_t* out, const XvtFlightMessage* message, uint32_t cookie,
									unsigned part);
/* Returns 1 for a complete message, 0 for pending/old, -1 for invalid or a gap. */
/* Assembles one world message at a time; on 1, copies it to out and remembers it as the last
 * assembled. Returns 0 for a tick at or before confirmed or before the last assembled, an exact
 * resend of a part already held, or a part still short of the whole. Returns -1 for a malformed
 * header or record, a record whose player is outside the mask or whose tick passes the target, a
 * resend that differs, a part of another message while one is being assembled, or a completed
 * message whose records are not ordered by player then rising tick; that message stays assembled
 * until discarded or Reset. */
int XvtFlightMessages_ReceivePart(const uint8_t* bytes, size_t size, uint32_t cookie, int confirmed,
								  XvtFlightMessage* out);

/* Appends size bytes as one message; returns 0 for size 0 or when the queue is out of messages or
 * bytes. */
int XvtFlightMessages_Push(XvtFlightQueue queue, const void* bytes, size_t size);
/* Copies the oldest message into bytes and returns its size; returns 0 when the queue is empty or
 * the message exceeds capacity. */
size_t XvtFlightMessages_Peek(XvtFlightQueue queue, void* bytes, size_t capacity);
/* Drops the oldest message, if any. */
void XvtFlightMessages_Pop(XvtFlightQueue queue);
/* Empties queue. */
void XvtFlightMessages_Clear(XvtFlightQueue queue);
/* Empties both queues and forgets the partial and last assembled world messages. */
void XvtFlightMessages_Reset(void);
#ifdef __cplusplus
}
#endif
#endif
