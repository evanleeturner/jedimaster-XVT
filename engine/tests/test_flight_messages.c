/* Checks the network125 wire messages (xvt_runtime/runtime/flight_messages.h) against the promises in its
 * header: one input record and an input batch encoded and read back, the parts of a world message and how
 * the receiver assembles them, and the pending and replay queues. Every check starts from Reset; the module
 * reads no game globals, and the messages are built here. */
#include "test_assert.h"
#include "xvt/flight/flight_input.h"
#include "xvt/net/net.h"
#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/flight_wire.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { COOKIE = 0x5A17, TARGET = 256 };

static struct XvtFlightMessage g_message, g_other, g_out;
static uint8_t g_part[XVT_FLIGHT_PACKET_BYTES];
static uint8_t g_bytes[XVT_FLIGHT_PACKET_BYTES];

static struct FlightInputFrameRecord Input(uint8_t key, int8_t axis,
					   uint8_t mods, int throttle)
{
	struct FlightInputFrameRecord input;
	memset(&input, 0, sizeof input);
	input.key = key;
	input.axisX = axis;
	input.axisY = (int8_t)-axis;
	input.axisR = (int8_t)(axis / 2);
	input.keyMods = mods;
	if (throttle >= 0) {
		input.flags = XVT_INPUT_THROTTLE_PRESENT;
		input.throttle = (uint16_t)throttle;
	}
	return input;
}

/* A world message for target with count records, ordered by player then rising tick: the players are the
 * set bits of mask, taken in turn, each with ticks 2, 4, 6 ... up to the target. */
static void MakeMessage(struct XvtFlightMessage *message, unsigned target,
			uint8_t mask, unsigned count)
{
	memset(message, 0, sizeof *message);
	message->target_flags = target;
	message->participant_mask = mask;
	unsigned players = 0;
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		players += (mask >> player) & 1u;
	}
	unsigned per_player = (count + players - 1) / players;
	XVT_ASSERT_TRUE(per_player * XVT_NETWORK_STEP_TICKS <= target);
	unsigned written = 0;
	for (unsigned player = 0;
	     player < XVT_FLIGHT_PLAYERS && written < count; ++player) {
		if (!(mask & (1u << player))) {
			continue;
		}
		for (unsigned i = 0; i < per_player && written < count; ++i) {
			struct XvtFlightWorldInputWire *record =
				&message->records[written++];
			struct FlightInputFrameRecord input =
				Input((uint8_t)(player + i), (int8_t)(2 * i), 1,
				      (int)(100 * i));
			record->player = (uint8_t)player;
			XvtFlightWire_EncodeInput(
				&record->input,
				(int)((i + 1) * XVT_NETWORK_STEP_TICKS),
				&input);
		}
	}
	message->count = (uint16_t)written;
}

static size_t Part(const struct XvtFlightMessage *message, unsigned part)
{
	size_t size =
		XvtFlightMessages_EncodePart(g_part, message, COOKIE, part);
	XVT_ASSERT_TRUE(size > 0);
	return size;
}

static int SameMessage(const struct XvtFlightMessage *a,
		       const struct XvtFlightMessage *b)
{
	return a->target_flags == b->target_flags &&
	       a->participant_mask == b->participant_mask &&
	       a->count == b->count &&
	       !memcmp(a->records, b->records, a->count * sizeof a->records[0]);
}

static void CheckInputRoundTrip(void)
{
	const int throttles[] = {-1, 0, 1, 0x7FFF, UINT16_MAX};
	for (size_t t = 0; t < sizeof throttles / sizeof throttles[0]; ++t) {
		for (int axis = -128; axis <= 126; axis += 2) {
			for (uint8_t mods = 0; mods < 4; ++mods) {
				struct FlightInputFrameRecord
					in = Input(0x41, (int8_t)axis, mods,
						   throttles[t]),
					out;
				struct XvtFlightInputWire record;
				XvtFlightWire_EncodeInput(&record, 1000, &in);
				XVT_ASSERT_INT_EQ(XvtWire_Get32(record.tick),
						  1000);
				/* The throttle-present flag rides in bit 0 of the third axis byte. */
				XVT_ASSERT_INT_EQ(record.axes[2] & 1,
						  throttles[t] >= 0);

				int tick = 0;
				memset(&out, 0xAB, sizeof out);
				XVT_ASSERT_INT_EQ(XvtFlightWire_DecodeInput(
							  &record, &tick, &out),
						  1);
				XVT_ASSERT_INT_EQ(tick, 1000);
				XVT_ASSERT_INT_EQ(out.key, in.key);
				XVT_ASSERT_INT_EQ(out.keyMods, in.keyMods);
				XVT_ASSERT_INT_EQ(out.flags, in.flags);
				XVT_ASSERT_INT_EQ(out.throttle, in.throttle);
				/* Even axes come back exactly (flight_controls.h: axes come back even). */
				XVT_ASSERT_INT_EQ(out.axisX, in.axisX);
				XVT_ASSERT_INT_EQ(out.axisY, in.axisY);
				if (in.axisR % 2 == 0) {
					XVT_ASSERT_INT_EQ(out.axisR, in.axisR);
				}
			}
		}
	}

	/* An absent throttle is written as 0, whatever the record held. */
	struct FlightInputFrameRecord in = Input(0, 0, 0, -1);
	in.throttle = 1234;
	struct XvtFlightInputWire record;
	XvtFlightWire_EncodeInput(&record, 2, &in);
	XVT_ASSERT_INT_EQ(XvtWire_Get16(record.throttle), 0);
}

static void CheckDecodeInputRefusals(void)
{
	struct FlightInputFrameRecord in = Input(7, 10, 2, 500), out, untouched;
	struct XvtFlightInputWire record;
	int tick;
	memset(&untouched, 0xAB, sizeof untouched);

	/* An odd tick: refused, the tick still stored, the input untouched. */
	XvtFlightWire_EncodeInput(&record, 3, &in);
	tick = 0;
	out = untouched;
	XVT_ASSERT_INT_EQ(XvtFlightWire_DecodeInput(&record, &tick, &out), 0);
	XVT_ASSERT_INT_EQ(tick, 3);
	XVT_ASSERT_INT_EQ(memcmp(&out, &untouched, sizeof out), 0);

	/* Tick 0 is not a usable tick either. */
	XvtFlightWire_EncodeInput(&record, 0, &in);
	out = untouched;
	XVT_ASSERT_INT_EQ(XvtFlightWire_DecodeInput(&record, &tick, &out), 0);
	XVT_ASSERT_INT_EQ(tick, 0);
	XVT_ASSERT_INT_EQ(memcmp(&out, &untouched, sizeof out), 0);

	/* A throttle value without its flag. */
	XvtFlightWire_EncodeInput(&record, 8, &in);
	record.axes[2] &= (uint8_t)~1u;
	out = untouched;
	XVT_ASSERT_INT_EQ(XvtFlightWire_DecodeInput(&record, &tick, &out), 0);
	XVT_ASSERT_INT_EQ(tick, 8);
	XVT_ASSERT_INT_EQ(memcmp(&out, &untouched, sizeof out), 0);

	/* The flag with a zero throttle is a present throttle of 0, and is accepted. */
	XvtWire_Set16(record.throttle, 0);
	record.axes[2] |= 1u;
	XVT_ASSERT_INT_EQ(XvtFlightWire_DecodeInput(&record, &tick, &out), 1);
	XVT_ASSERT_INT_EQ(out.flags, XVT_INPUT_THROTTLE_PRESENT);
	XVT_ASSERT_INT_EQ(out.throttle, 0);
}

static void CheckBatch(void)
{
	struct XvtFlightInputWire records[XVT_INPUT_BATCH_RECORDS + 1];
	for (unsigned i = 0; i < XVT_INPUT_BATCH_RECORDS + 1; ++i) {
		struct FlightInputFrameRecord input =
			Input((uint8_t)i, (int8_t)i, 0, i % 2 ? (int)i : -1);
		XvtFlightWire_EncodeInput(
			&records[i], (int)(XVT_NETWORK_STEP_TICKS * (i + 1)),
			&input);
	}

	/* Refused: a zero cookie, no records, more than a batch holds. */
	XVT_ASSERT_INT_EQ(XvtFlightMessages_EncodeBatch(g_bytes, 0, records, 3),
			  0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_EncodeBatch(g_bytes, COOKIE, records, 0), 0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_EncodeBatch(g_bytes, COOKIE, records,
					      XVT_INPUT_BATCH_RECORDS + 1),
		0);

	for (unsigned count = 1; count <= XVT_INPUT_BATCH_RECORDS; ++count) {
		size_t size = XvtFlightMessages_EncodeBatch(g_bytes, COOKIE,
							    records, count);
		XVT_ASSERT_INT_EQ(
			size,
			sizeof(struct XvtFlightBatchHeader) +
				count * sizeof(struct XvtFlightInputWire));
		XVT_ASSERT_INT_EQ(
			XvtFlightMessages_ValidateBatch(g_bytes, size, COOKIE),
			1);
		/* The size must match the count exactly, and the cookie must be the one it was written for. */
		XVT_ASSERT_INT_EQ(XvtFlightMessages_ValidateBatch(
					  g_bytes, size - 1, COOKIE),
				  0);
		XVT_ASSERT_INT_EQ(XvtFlightMessages_ValidateBatch(
					  g_bytes, size + 1, COOKIE),
				  0);
		XVT_ASSERT_INT_EQ(XvtFlightMessages_ValidateBatch(g_bytes, size,
								  COOKIE + 1),
				  0);
		XVT_ASSERT_INT_EQ(
			XvtFlightMessages_ValidateBatch(g_bytes, size, 0), 0);
	}

	size_t size =
		XvtFlightMessages_EncodeBatch(g_bytes, COOKIE, records, 4);
	struct XvtFlightBatchHeader header;
	memcpy(&header, g_bytes, sizeof header);

	/* Another opcode. */
	g_bytes[0] ^= 1u;
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ValidateBatch(g_bytes, size, COOKIE), 0);
	g_bytes[0] ^= 1u;
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ValidateBatch(g_bytes, size, COOKIE), 1);

	/* Ticks must rise strictly: a repeat of the previous tick is refused. */
	struct XvtFlightInputWire copy = records[1];
	XvtWire_Set32(copy.tick, XvtWire_Get32(records[0].tick));
	memcpy(g_bytes + sizeof header + sizeof copy, &copy, sizeof copy);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ValidateBatch(g_bytes, size, COOKIE), 0);

	/* Every record must decode: an odd tick, still rising, is refused. */
	XvtWire_Set32(copy.tick, XvtWire_Get32(records[0].tick) + 1);
	memcpy(g_bytes + sizeof header + sizeof copy, &copy, sizeof copy);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ValidateBatch(g_bytes, size, COOKIE), 0);

	/* A header shorter than itself. */
	XVT_ASSERT_INT_EQ(XvtFlightMessages_ValidateBatch(
				  g_bytes, sizeof header - 1, COOKIE),
			  0);
}

static void CheckPartCount(void)
{
	XVT_ASSERT_INT_EQ(XvtFlightMessages_PartCount(0), 1);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_PartCount(1), 1);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_PartCount(XVT_WORLD_PART_RECORDS),
			  1);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_PartCount(XVT_WORLD_PART_RECORDS + 1), 2);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_PartCount(2 * XVT_WORLD_PART_RECORDS), 2);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_PartCount(XVT_WORLD_RECORDS),
			  XVT_WORLD_PARTS);
}

static void CheckEncodePart(void)
{
	XvtFlightMessages_Reset();
	MakeMessage(&g_message, TARGET, 0x05, XVT_WORLD_PART_RECORDS + 6);
	unsigned parts = XvtFlightMessages_PartCount(g_message.count);
	XVT_ASSERT_INT_EQ(parts, 2);

	/* Refused: a zero cookie, a part past the last, a message over XVT_WORLD_RECORDS records. */
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_EncodePart(g_part, &g_message, 0, 0), 0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_EncodePart(g_part, &g_message, COOKIE, parts),
		0);
	g_other = g_message;
	g_other.count = XVT_WORLD_RECORDS + 1;
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_EncodePart(g_part, &g_other, COOKIE, 0), 0);

	/* The parts carry every record once: their payloads add up to the message's records. */
	size_t total = 0;
	for (unsigned part = 0; part < parts; ++part) {
		total += Part(&g_message, part) -
			 sizeof(struct XvtFlightWorldHeader);
	}
	XVT_ASSERT_INT_EQ(total,
			  g_message.count *
				  sizeof(struct XvtFlightWorldInputWire));

	/* An empty message is one part with no records. */
	MakeMessage(&g_other, TARGET, 0x01, 1);
	g_other.count = 0;
	XVT_ASSERT_INT_EQ(Part(&g_other, 0),
			  sizeof(struct XvtFlightWorldHeader));
}

static void CheckAssembly(void)
{
	XvtFlightMessages_Reset();
	MakeMessage(&g_message, TARGET, 0x06, 2 * XVT_WORLD_PART_RECORDS + 3);
	unsigned parts = XvtFlightMessages_PartCount(g_message.count);
	XVT_ASSERT_INT_EQ(parts, 3);

	/* In any order: every part but the last is short of the whole; the last completes it. */
	const unsigned order[3] = {2, 0, 1};
	for (unsigned i = 0; i < parts; ++i) {
		size_t size = Part(&g_message, order[i]);
		memset(&g_out, 0, sizeof g_out);
		int result = XvtFlightMessages_ReceivePart(g_part, size, COOKIE,
							   0, &g_out);
		if (i + 1 < parts) {
			XVT_ASSERT_INT_EQ(result, 0);
			/* An exact resend of a part already held. */
			XVT_ASSERT_INT_EQ(
				XvtFlightMessages_ReceivePart(
					g_part, size, COOKIE, 0, &g_out),
				0);
		} else {
			XVT_ASSERT_INT_EQ(result, 1);
			XVT_ASSERT_TRUE(SameMessage(&g_out, &g_message));
		}
	}

	/* The message is remembered as the last assembled: an exact resend of any of its parts is old, a
	 * different one is invalid. */
	size_t size = Part(&g_message, 1);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		0);
	g_part[sizeof(struct XvtFlightWorldHeader) +
	       offsetof(struct XvtFlightWorldInputWire, input.key)] ^= 0x10;
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		-1);

	/* A message for an earlier tick than the last assembled is old. */
	MakeMessage(&g_other, TARGET - XVT_WORLD_MESSAGE_TICKS, 0x02, 3);
	size = Part(&g_other, 0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		0);

	/* The next message assembles. */
	MakeMessage(&g_other, TARGET + XVT_WORLD_MESSAGE_TICKS, 0x02, 3);
	size = Part(&g_other, 0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		1);
	XVT_ASSERT_TRUE(SameMessage(&g_out, &g_other));
}

static void CheckAssemblyOld(void)
{
	XvtFlightMessages_Reset();
	MakeMessage(&g_message, TARGET, 0x01, 3);
	size_t size = Part(&g_message, 0);
	/* A target at or before the confirmed tick is old. */
	XVT_ASSERT_INT_EQ(XvtFlightMessages_ReceivePart(g_part, size, COOKIE,
							TARGET, &g_out),
			  0);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_ReceivePart(g_part, size, COOKIE,
							TARGET + 2, &g_out),
			  0);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_ReceivePart(g_part, size, COOKIE,
							TARGET - 2, &g_out),
			  1);
}

static void CheckAssemblyRefusals(void)
{
	MakeMessage(&g_message, TARGET, 0x03, 4);
	size_t size;

	/* Malformed: another cookie, a size that does not match the records, another opcode. */
	XvtFlightMessages_Reset();
	size = Part(&g_message, 0);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_ReceivePart(g_part, size,
							COOKIE + 1, 0, &g_out),
			  -1);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_ReceivePart(g_part, size - 1,
							COOKIE, 0, &g_out),
			  -1);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_ReceivePart(
				  g_part,
				  sizeof(struct XvtFlightWorldHeader) - 1,
				  COOKIE, 0, &g_out),
			  -1);
	XvtWire_Set32(g_part, NET_PACKET_INPUT_BATCH);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		-1);

	/* A record whose player is outside the mask. */
	g_other = g_message;
	g_other.participant_mask = 0x01;
	size = Part(&g_other, 0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		-1);

	/* A record whose tick passes the target. */
	g_other = g_message;
	g_other.target_flags = 2;
	size = Part(&g_other, 0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		-1);

	/* The untouched message still assembles after all of that. */
	size = Part(&g_message, 0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		1);
}

static void CheckAssemblyConflicts(void)
{
	/* While a message is being assembled, a part of another message is invalid, and a resend of a held
	 * part that differs is invalid. */
	XvtFlightMessages_Reset();
	MakeMessage(&g_message, TARGET, 0x01, XVT_WORLD_PART_RECORDS + 1);
	size_t size = Part(&g_message, 0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		0);
	MakeMessage(&g_other, TARGET + XVT_WORLD_MESSAGE_TICKS, 0x01, 2);
	size = Part(&g_other, 0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		-1);
	size = Part(&g_message, 0);
	g_part[sizeof(struct XvtFlightWorldHeader) +
	       offsetof(struct XvtFlightWorldInputWire, input.key)] ^= 0x10;
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		-1);
	/* The assembly survived both: the missing part completes it. */
	size = Part(&g_message, 1);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		1);
	XVT_ASSERT_TRUE(SameMessage(&g_out, &g_message));
}

static void CheckUnorderedMessageStays(void)
{
	/* A complete message whose records are not ordered by player then rising tick is invalid... */
	XvtFlightMessages_Reset();
	MakeMessage(&g_message, TARGET, 0x03, 4);
	struct XvtFlightWorldInputWire first = g_message.records[0];
	g_message.records[0] = g_message.records[g_message.count - 1];
	g_message.records[g_message.count - 1] = first;
	size_t size = Part(&g_message, 0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		-1);

	/* ...and stays assembled: the next message is a part of another message, until the assembly is
	 * discarded. */
	MakeMessage(&g_other, TARGET + XVT_WORLD_MESSAGE_TICKS, 0x01, 2);
	size = Part(&g_other, 0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		-1);
	XvtFlightMessages_DiscardAssemblyThrough(TARGET);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		1);

	/* Reset also drops it. */
	XvtFlightMessages_Reset();
	size = Part(&g_message, 0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		-1);
	XvtFlightMessages_Reset();
	size = Part(&g_other, 0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		1);
}

static void CheckDiscardAssembly(void)
{
	XvtFlightMessages_Reset();
	MakeMessage(&g_message, TARGET, 0x01, XVT_WORLD_PART_RECORDS + 1);
	size_t size = Part(&g_message, 0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		0);

	/* A tick before the target leaves the partial message alone: the other part completes it. */
	XvtFlightMessages_DiscardAssemblyThrough(TARGET -
						 XVT_NETWORK_STEP_TICKS);
	size = Part(&g_message, 1);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		1);

	/* At the target it is dropped: the second part alone is again short of the whole. */
	XvtFlightMessages_Reset();
	size = Part(&g_message, 0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		0);
	XvtFlightMessages_DiscardAssemblyThrough(TARGET);
	size = Part(&g_message, 1);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		0);
	size = Part(&g_message, 0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		1);

	/* Reset forgets the last assembled message: its parts are new again and assemble once more. */
	XvtFlightMessages_Reset();
	size = Part(&g_message, 1);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		0);
	size = Part(&g_message, 0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		1);
}

static void CheckQueueBasics(void)
{
	XvtFlightMessages_Reset();
	for (int q = 0; q < XVT_QUEUE_COUNT; ++q) {
		XvtFlightQueue queue = (XvtFlightQueue)q;
		XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(queue), 0);
		XVT_ASSERT_INT_EQ(
			XvtFlightMessages_Peek(queue, g_bytes, sizeof g_bytes),
			0);
		XvtFlightMessages_Pop(queue);
		XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(queue), 0);

		/* Size 0 is refused, by Push and by HasRoom. */
		XVT_ASSERT_INT_EQ(XvtFlightMessages_HasRoom(queue, 0), 0);
		XVT_ASSERT_INT_EQ(XvtFlightMessages_Push(queue, "x", 0), 0);
		XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(queue), 0);

		/* First in, first out, each message whole. */
		XVT_ASSERT_INT_EQ(XvtFlightMessages_Push(queue, "first", 5), 1);
		XVT_ASSERT_INT_EQ(XvtFlightMessages_Push(queue, "second!", 7),
				  1);
		XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(queue), 2);
		/* Peek refuses a buffer smaller than the message, and leaves it queued. */
		XVT_ASSERT_INT_EQ(XvtFlightMessages_Peek(queue, g_bytes, 4), 0);
		XVT_ASSERT_INT_EQ(XvtFlightMessages_Peek(queue, g_bytes, 5), 5);
		XVT_ASSERT_INT_EQ(memcmp(g_bytes, "first", 5), 0);
		/* Peek does not consume. */
		XVT_ASSERT_INT_EQ(
			XvtFlightMessages_Peek(queue, g_bytes, sizeof g_bytes),
			5);
		XvtFlightMessages_Pop(queue);
		XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(queue), 1);
		XVT_ASSERT_INT_EQ(
			XvtFlightMessages_Peek(queue, g_bytes, sizeof g_bytes),
			7);
		XVT_ASSERT_INT_EQ(memcmp(g_bytes, "second!", 7), 0);

		XVT_ASSERT_INT_EQ(XvtFlightMessages_Push(queue, "third", 5), 1);
		XvtFlightMessages_Clear(queue);
		XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(queue), 0);
		XVT_ASSERT_INT_EQ(
			XvtFlightMessages_Peek(queue, g_bytes, sizeof g_bytes),
			0);
	}

	/* The queues are separate: Clear empties only its own. */
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Push(XVT_QUEUE_PENDING, "p", 1), 1);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Push(XVT_QUEUE_REPLAY, "r", 1), 1);
	XvtFlightMessages_Clear(XVT_QUEUE_REPLAY);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 1);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_REPLAY), 0);

	/* Reset empties both. */
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Push(XVT_QUEUE_REPLAY, "r", 1), 1);
	XvtFlightMessages_Reset();
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 0);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_REPLAY), 0);
}

/* Fills queue with messages of size bytes, checking before each push that HasRoom predicts it, until a
 * push is refused; returns how many went in. The bytes of message i all hold i + 1, cut to 8 bits. */
static unsigned Fill(XvtFlightQueue queue, uint8_t *buffer, size_t size,
		     unsigned first)
{
	unsigned pushed = 0;
	for (;;) {
		memset(buffer, (int)((first + pushed + 1) & 0xFF), size);
		int room = XvtFlightMessages_HasRoom(queue, size);
		int result = XvtFlightMessages_Push(queue, buffer, size);
		XVT_ASSERT_INT_EQ(result, room);
		if (!result) {
			return pushed;
		}
		++pushed;
		XVT_ASSERT_TRUE(pushed < 1000000);
	}
}

static void CheckQueueLimits(void)
{
	/* Each queue has a message limit: one-byte messages run out of messages before bytes. */
	uint8_t *buffer = malloc(2 * XVT_REPLAY_BYTES);
	XVT_ASSERT_TRUE(buffer != NULL);
	XvtFlightMessages_Reset();
	for (int q = 0; q < XVT_QUEUE_COUNT; ++q) {
		XvtFlightQueue queue = (XvtFlightQueue)q;
		unsigned limit = Fill(queue, buffer, 1, 0);
		XVT_ASSERT_TRUE(limit > 0);
		XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(queue), limit);
		/* Popping one makes room for exactly one more. */
		XvtFlightMessages_Pop(queue);
		XVT_ASSERT_INT_EQ(Fill(queue, buffer, 1, limit), 1);
		XvtFlightMessages_Clear(queue);
	}

	/* And a byte limit: large messages run out of bytes while a one-byte message still has room. The
	 * queue's bytes then hold fewer than count + 1 of them, so one message of that many bytes never fits,
	 * even in the emptied queue. */
	for (int q = 0; q < XVT_QUEUE_COUNT; ++q) {
		XvtFlightQueue queue = (XvtFlightQueue)q;
		size_t size = (q == XVT_QUEUE_PENDING ? XVT_PENDING_BYTES
						      : XVT_REPLAY_BYTES) /
				      10 +
			      1;
		unsigned count = Fill(queue, buffer, size, 0);
		XVT_ASSERT_TRUE(count > 0);
		XVT_ASSERT_INT_EQ(XvtFlightMessages_HasRoom(queue, 1), 1);
		XvtFlightMessages_Clear(queue);
		size_t whole = (count + 1) * size;
		XVT_ASSERT_TRUE(whole <= 2 * XVT_REPLAY_BYTES);
		XVT_ASSERT_INT_EQ(XvtFlightMessages_HasRoom(queue, whole), 0);
		XVT_ASSERT_INT_EQ(XvtFlightMessages_Push(queue, buffer, whole),
				  0);
		XVT_ASSERT_INT_EQ(XvtFlightMessages_HasRoom(queue, size), 1);
	}
	free(buffer);
}

static void CheckQueueWraps(void)
{
	/* Fill a queue with large messages, drop the oldest two, and refill: the new messages wrap past the
	 * end of the ring and still come back whole, in order. */
	enum { SIZE = XVT_PENDING_BYTES / 7 + 3 };

	static uint8_t buffer[SIZE], back[SIZE], expected[SIZE];
	XvtFlightMessages_Reset();
	unsigned count = Fill(XVT_QUEUE_PENDING, buffer, SIZE, 0);
	XVT_ASSERT_TRUE(count >= 3);
	XvtFlightMessages_Pop(XVT_QUEUE_PENDING);
	XvtFlightMessages_Pop(XVT_QUEUE_PENDING);
	unsigned more = Fill(XVT_QUEUE_PENDING, buffer, SIZE, count);
	XVT_ASSERT_INT_EQ(more, 2);
	for (unsigned i = 2; i < count + more; ++i) {
		XVT_ASSERT_INT_EQ(XvtFlightMessages_Peek(XVT_QUEUE_PENDING,
							 back, sizeof back),
				  SIZE);
		memset(expected, (int)((i + 1) & 0xFF), sizeof expected);
		XVT_ASSERT_INT_EQ(memcmp(back, expected, SIZE), 0);
		XvtFlightMessages_Pop(XVT_QUEUE_PENDING);
	}
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 0);
}

static void CheckEnqueue(void)
{
	XvtFlightMessages_Reset();
	MakeMessage(&g_message, TARGET, 0x01, 2);
	MakeMessage(&g_other, TARGET, 0x01, 1);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_Enqueue(&g_message, XVT_QUEUE_PENDING), 1);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_Enqueue(&g_other, XVT_QUEUE_PENDING), 1);

	/* Stored with only its count records: one record more is one record's bytes more. */
	memset(&g_out, 0, sizeof g_out);
	size_t two =
		XvtFlightMessages_Peek(XVT_QUEUE_PENDING, &g_out, sizeof g_out);
	XVT_ASSERT_TRUE(SameMessage(&g_out, &g_message));
	XvtFlightMessages_Pop(XVT_QUEUE_PENDING);
	memset(&g_out, 0, sizeof g_out);
	size_t one =
		XvtFlightMessages_Peek(XVT_QUEUE_PENDING, &g_out, sizeof g_out);
	XVT_ASSERT_TRUE(SameMessage(&g_out, &g_other));
	XVT_ASSERT_INT_EQ(two - one, sizeof(struct XvtFlightWorldInputWire));
	XVT_ASSERT_TRUE(two < sizeof(struct XvtFlightMessage));
}

/* The target ticks of the messages on queue, oldest first; returns how many. */
static unsigned Targets(XvtFlightQueue queue, unsigned *targets,
			unsigned capacity)
{
	unsigned count = XvtFlightMessages_Count(queue);
	XVT_ASSERT_TRUE(count <= capacity);
	for (unsigned i = 0; i < count; ++i) {
		size_t size =
			XvtFlightMessages_Peek(queue, &g_out, sizeof g_out);
		XVT_ASSERT_TRUE(size > 0);
		targets[i] = g_out.target_flags & INT32_MAX;
		/* Rotate, so the queue is as it was afterwards. */
		XvtFlightMessages_Pop(queue);
		XVT_ASSERT_INT_EQ(XvtFlightMessages_Push(queue, &g_out, size),
				  1);
	}
	return count;
}

static void EnqueueAt(unsigned target, XvtFlightQueue queue)
{
	MakeMessage(&g_other, target, 0x01, 1);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Enqueue(&g_other, queue), 1);
}

static void CheckPrepareRecovery(void)
{
	XvtFlightMessages_Reset();
	EnqueueAt(8, XVT_QUEUE_REPLAY);
	EnqueueAt(24, XVT_QUEUE_REPLAY);
	EnqueueAt(8, XVT_QUEUE_PENDING);
	EnqueueAt(16, XVT_QUEUE_PENDING);
	EnqueueAt(40, XVT_QUEUE_PENDING);
	EnqueueAt(32 | XVT_WORLD_CHECKSUM_FLAG, XVT_QUEUE_PENDING);
	/* A partial assembly targeting tick 16. */
	MakeMessage(&g_message, 16, 0x3F, XVT_WORLD_PART_RECORDS + 1);
	size_t size = Part(&g_message, 0);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		0);

	/* Everything targeting 16 or earlier goes; what is left pending moves, in order, after the replay. */
	XVT_ASSERT_INT_EQ(XvtFlightMessages_PrepareRecovery(16), 1);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 0);
	unsigned targets[8];
	XVT_ASSERT_INT_EQ(Targets(XVT_QUEUE_REPLAY, targets, 8), 3);
	XVT_ASSERT_INT_EQ(targets[0], 24);
	XVT_ASSERT_INT_EQ(targets[1], 40);
	XVT_ASSERT_INT_EQ(targets[2], 32);
	/* The partial assembly went with them: its second part is short of the whole. */
	size = Part(&g_message, 1);
	XVT_ASSERT_INT_EQ(
		XvtFlightMessages_ReceivePart(g_part, size, COOKIE, 0, &g_out),
		0);
}

static void CheckPrepareRecoveryFull(void)
{
	/* With the replay queue full, recovery fails and the pending messages stay pending. */
	XvtFlightMessages_Reset();
	unsigned replayed = 0;
	MakeMessage(&g_other, 400, 0x01, 1);
	while (XvtFlightMessages_Enqueue(&g_other, XVT_QUEUE_REPLAY)) {
		XVT_ASSERT_TRUE(++replayed < 1000000);
	}
	EnqueueAt(200, XVT_QUEUE_PENDING);
	EnqueueAt(208, XVT_QUEUE_PENDING);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_PrepareRecovery(100), 0);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 2);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_REPLAY), replayed);
	unsigned targets[2];
	XVT_ASSERT_INT_EQ(Targets(XVT_QUEUE_PENDING, targets, 2), 2);
	XVT_ASSERT_INT_EQ(targets[0], 200);
	XVT_ASSERT_INT_EQ(targets[1], 208);
}

int main(void)
{
	CheckInputRoundTrip();
	CheckDecodeInputRefusals();
	CheckBatch();
	CheckPartCount();
	CheckEncodePart();
	CheckAssembly();
	CheckAssemblyOld();
	CheckAssemblyRefusals();
	CheckAssemblyConflicts();
	CheckUnorderedMessageStays();
	CheckDiscardAssembly();
	CheckQueueBasics();
	CheckQueueLimits();
	CheckQueueWraps();
	CheckEnqueue();
	CheckPrepareRecovery();
	CheckPrepareRecoveryFull();
	XvtFlightMessages_Reset();
	return 0;
}
