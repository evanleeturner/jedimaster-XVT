/* Checks the network125 wire messages (xvt_runtime/runtime/flight_messages.h) against the promises in its
 * header: one input record and an input batch encoded and read back, the parts of a world message and how
 * the receiver assembles them, and the pending and replay queues. Every check starts from Reset; the module
 * reads no game globals, and the messages are built here. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/flight/flight_input.h"
#include "xvt/net/net.h"
#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/flight_wire.h"

enum { COOKIE = 0x5A17, TARGET = 256 };

static struct xvt_flight_message g_message;
static struct xvt_flight_message g_other;
static struct xvt_flight_message g_out;
static uint8_t g_part[XVT_FLIGHT_PACKET_BYTES];
static uint8_t g_bytes[XVT_FLIGHT_PACKET_BYTES];

static struct flight_input_frame_record
flight_messages_input(uint8_t key, int8_t axis, uint8_t mods, int throttle)
{
	struct flight_input_frame_record input;
	memset(&input, 0, sizeof input);
	input.key = key;
	input.axis_x = axis;
	input.axis_y = (int8_t)-axis;
	input.axis_r = (int8_t)(axis / 2);
	input.key_mods = mods;
	if (throttle >= 0) {
		input.flags = XVT_INPUT_THROTTLE_PRESENT;
		input.throttle = (uint16_t)throttle;
	}
	return input;
}

/* A world message for target with count records, ordered by player then rising tick: the players are the
 * set bits of mask, taken in turn, each with ticks 2, 4, 6 ... up to the target. */
static void make_message(struct xvt_flight_message *message, unsigned target,
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
			struct xvt_flight_world_input_wire *record =
				&message->records[written++];
			struct flight_input_frame_record input =
				flight_messages_input((uint8_t)(player + i),
						      (int8_t)(2 * i), 1,
						      (int)(100 * i));
			record->player = (uint8_t)player;
			xvt_flight_wire_encode_input(
				&record->input,
				(int)((i + 1) * XVT_NETWORK_STEP_TICKS),
				&input);
		}
	}
	message->count = (uint16_t)written;
}

static size_t flight_messages_part(const struct xvt_flight_message *message,
				   unsigned part)
{
	size_t size =
		xvt_flight_messages_encode_part(g_part, message, COOKIE, part);
	XVT_ASSERT_TRUE(size > 0);
	return size;
}

static int same_message(const struct xvt_flight_message *a,
			const struct xvt_flight_message *b)
{
	return a->target_flags == b->target_flags &&
	       a->participant_mask == b->participant_mask &&
	       a->count == b->count &&
	       !memcmp(a->records, b->records, a->count * sizeof a->records[0]);
}

static void check_input_round_trip(void)
{
	const int throttles[] = {-1, 0, 1, 0x7FFF, UINT16_MAX};
	for (size_t t = 0; t < sizeof throttles / sizeof throttles[0]; ++t) {
		for (int axis = -128; axis <= 126; axis += 2) {
			for (uint8_t mods = 0; mods < 4; ++mods) {
				struct flight_input_frame_record in =
					flight_messages_input(
						0x41, (int8_t)axis, mods,
						throttles[t]);
				struct flight_input_frame_record out;
				struct xvt_flight_input_wire record;
				xvt_flight_wire_encode_input(&record, 1000,
							     &in);
				XVT_ASSERT_INT_EQ(xvt_wire_get32(record.tick),
						  1000);
				/* The throttle-present flag rides in bit 0 of the third axis byte. */
				XVT_ASSERT_INT_EQ(record.axes[2] & 1,
						  throttles[t] >= 0);

				int tick = 0;
				memset(&out, 0xAB, sizeof out);
				XVT_ASSERT_INT_EQ(xvt_flight_wire_decode_input(
							  &record, &tick, &out),
						  1);
				XVT_ASSERT_INT_EQ(tick, 1000);
				XVT_ASSERT_INT_EQ(out.key, in.key);
				XVT_ASSERT_INT_EQ(out.key_mods, in.key_mods);
				XVT_ASSERT_INT_EQ(out.flags, in.flags);
				XVT_ASSERT_INT_EQ(out.throttle, in.throttle);
				/* Even axes come back exactly (flight_controls.h: axes come back even). */
				XVT_ASSERT_INT_EQ(out.axis_x, in.axis_x);
				XVT_ASSERT_INT_EQ(out.axis_y, in.axis_y);
				if (in.axis_r % 2 == 0) {
					XVT_ASSERT_INT_EQ(out.axis_r,
							  in.axis_r);
				}
			}
		}
	}

	/* An absent throttle is written as 0, whatever the record held. */
	struct flight_input_frame_record in =
		flight_messages_input(0, 0, 0, -1);
	in.throttle = 1234;
	struct xvt_flight_input_wire record;
	xvt_flight_wire_encode_input(&record, 2, &in);
	XVT_ASSERT_INT_EQ(xvt_wire_get16(record.throttle), 0);
}

static void check_decode_input_refusals(void)
{
	struct flight_input_frame_record in =
		flight_messages_input(7, 10, 2, 500);
	struct flight_input_frame_record out;
	struct flight_input_frame_record untouched;
	struct xvt_flight_input_wire record;
	int tick;
	memset(&untouched, 0xAB, sizeof untouched);

	/* An odd tick: refused, the tick still stored, the input untouched. */
	xvt_flight_wire_encode_input(&record, 3, &in);
	tick = 0;
	out = untouched;
	XVT_ASSERT_INT_EQ(xvt_flight_wire_decode_input(&record, &tick, &out),
			  0);
	XVT_ASSERT_INT_EQ(tick, 3);
	XVT_ASSERT_INT_EQ(memcmp(&out, &untouched, sizeof out), 0);

	/* Tick 0 is not a usable tick either. */
	xvt_flight_wire_encode_input(&record, 0, &in);
	out = untouched;
	XVT_ASSERT_INT_EQ(xvt_flight_wire_decode_input(&record, &tick, &out),
			  0);
	XVT_ASSERT_INT_EQ(tick, 0);
	XVT_ASSERT_INT_EQ(memcmp(&out, &untouched, sizeof out), 0);

	/* A throttle value without its flag. */
	xvt_flight_wire_encode_input(&record, 8, &in);
	record.axes[2] &= (uint8_t)~1u;
	out = untouched;
	XVT_ASSERT_INT_EQ(xvt_flight_wire_decode_input(&record, &tick, &out),
			  0);
	XVT_ASSERT_INT_EQ(tick, 8);
	XVT_ASSERT_INT_EQ(memcmp(&out, &untouched, sizeof out), 0);

	/* The flag with a zero throttle is a present throttle of 0, and is accepted. */
	xvt_wire_set16(record.throttle, 0);
	record.axes[2] |= 1u;
	XVT_ASSERT_INT_EQ(xvt_flight_wire_decode_input(&record, &tick, &out),
			  1);
	XVT_ASSERT_INT_EQ(out.flags, XVT_INPUT_THROTTLE_PRESENT);
	XVT_ASSERT_INT_EQ(out.throttle, 0);
}

static void check_batch(void)
{
	struct xvt_flight_input_wire records[XVT_INPUT_BATCH_RECORDS + 1];
	for (unsigned i = 0; i < XVT_INPUT_BATCH_RECORDS + 1; ++i) {
		struct flight_input_frame_record input = flight_messages_input(
			(uint8_t)i, (int8_t)i, 0, i % 2 ? (int)i : -1);
		xvt_flight_wire_encode_input(
			&records[i], (int)(XVT_NETWORK_STEP_TICKS * (i + 1)),
			&input);
	}

	/* Refused: a zero cookie, no records, more than a batch holds. */
	XVT_ASSERT_INT_EQ(
		xvt_flight_messages_encode_batch(g_bytes, 0, records, 3), 0);
	XVT_ASSERT_INT_EQ(
		xvt_flight_messages_encode_batch(g_bytes, COOKIE, records, 0),
		0);
	XVT_ASSERT_INT_EQ(
		xvt_flight_messages_encode_batch(g_bytes, COOKIE, records,
						 XVT_INPUT_BATCH_RECORDS + 1),
		0);

	for (unsigned count = 1; count <= XVT_INPUT_BATCH_RECORDS; ++count) {
		size_t size = xvt_flight_messages_encode_batch(g_bytes, COOKIE,
							       records, count);
		XVT_ASSERT_INT_EQ(
			size,
			sizeof(struct xvt_flight_batch_header) +
				count * sizeof(struct xvt_flight_input_wire));
		XVT_ASSERT_INT_EQ(xvt_flight_messages_validate_batch(
					  g_bytes, size, COOKIE),
				  1);
		/* The size must match the count exactly, and the cookie must be the one it was written for. */
		XVT_ASSERT_INT_EQ(xvt_flight_messages_validate_batch(
					  g_bytes, size - 1, COOKIE),
				  0);
		XVT_ASSERT_INT_EQ(xvt_flight_messages_validate_batch(
					  g_bytes, size + 1, COOKIE),
				  0);
		XVT_ASSERT_INT_EQ(xvt_flight_messages_validate_batch(
					  g_bytes, size, COOKIE + 1),
				  0);
		XVT_ASSERT_INT_EQ(
			xvt_flight_messages_validate_batch(g_bytes, size, 0),
			0);
	}

	size_t size =
		xvt_flight_messages_encode_batch(g_bytes, COOKIE, records, 4);
	struct xvt_flight_batch_header header;
	memcpy(&header, g_bytes, sizeof header);

	/* Another opcode. */
	g_bytes[0] ^= 1u;
	XVT_ASSERT_INT_EQ(
		xvt_flight_messages_validate_batch(g_bytes, size, COOKIE), 0);
	g_bytes[0] ^= 1u;
	XVT_ASSERT_INT_EQ(
		xvt_flight_messages_validate_batch(g_bytes, size, COOKIE), 1);

	/* Ticks must rise strictly: a repeat of the previous tick is refused. */
	struct xvt_flight_input_wire copy = records[1];
	xvt_wire_set32(copy.tick, xvt_wire_get32(records[0].tick));
	memcpy(g_bytes + sizeof header + sizeof copy, &copy, sizeof copy);
	XVT_ASSERT_INT_EQ(
		xvt_flight_messages_validate_batch(g_bytes, size, COOKIE), 0);

	/* Every record must decode: an odd tick, still rising, is refused. */
	xvt_wire_set32(copy.tick, xvt_wire_get32(records[0].tick) + 1);
	memcpy(g_bytes + sizeof header + sizeof copy, &copy, sizeof copy);
	XVT_ASSERT_INT_EQ(
		xvt_flight_messages_validate_batch(g_bytes, size, COOKIE), 0);

	/* A header shorter than itself. */
	XVT_ASSERT_INT_EQ(xvt_flight_messages_validate_batch(
				  g_bytes, sizeof header - 1, COOKIE),
			  0);
}

static void check_part_count(void)
{
	XVT_ASSERT_INT_EQ(xvt_flight_messages_part_count(0), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_part_count(1), 1);
	XVT_ASSERT_INT_EQ(
		xvt_flight_messages_part_count(XVT_WORLD_PART_RECORDS), 1);
	XVT_ASSERT_INT_EQ(
		xvt_flight_messages_part_count(XVT_WORLD_PART_RECORDS + 1), 2);
	XVT_ASSERT_INT_EQ(
		xvt_flight_messages_part_count(2 * XVT_WORLD_PART_RECORDS), 2);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_part_count(XVT_WORLD_RECORDS),
			  XVT_WORLD_PARTS);
}

static void check_encode_part(void)
{
	xvt_flight_messages_reset();
	make_message(&g_message, TARGET, 0x05, XVT_WORLD_PART_RECORDS + 6);
	unsigned parts = xvt_flight_messages_part_count(g_message.count);
	XVT_ASSERT_INT_EQ(parts, 2);

	/* Refused: a zero cookie, a part past the last, a message over XVT_WORLD_RECORDS records. */
	XVT_ASSERT_INT_EQ(
		xvt_flight_messages_encode_part(g_part, &g_message, 0, 0), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_encode_part(g_part, &g_message,
							  COOKIE, parts),
			  0);
	g_other = g_message;
	g_other.count = XVT_WORLD_RECORDS + 1;
	XVT_ASSERT_INT_EQ(
		xvt_flight_messages_encode_part(g_part, &g_other, COOKIE, 0),
		0);

	/* The parts carry every record once: their payloads add up to the message's records. */
	size_t total = 0;
	for (unsigned part = 0; part < parts; ++part) {
		total += flight_messages_part(&g_message, part) -
			 sizeof(struct xvt_flight_world_header);
	}
	XVT_ASSERT_INT_EQ(total,
			  g_message.count *
				  sizeof(struct xvt_flight_world_input_wire));

	/* An empty message is one part with no records. */
	make_message(&g_other, TARGET, 0x01, 1);
	g_other.count = 0;
	XVT_ASSERT_INT_EQ(flight_messages_part(&g_other, 0),
			  sizeof(struct xvt_flight_world_header));
}

static void check_assembly(void)
{
	xvt_flight_messages_reset();
	make_message(&g_message, TARGET, 0x06, 2 * XVT_WORLD_PART_RECORDS + 3);
	unsigned parts = xvt_flight_messages_part_count(g_message.count);
	XVT_ASSERT_INT_EQ(parts, 3);

	/* In any order: every part but the last is short of the whole; the last completes it. */
	const unsigned order[3] = {2, 0, 1};
	for (unsigned i = 0; i < parts; ++i) {
		size_t size = flight_messages_part(&g_message, order[i]);
		memset(&g_out, 0, sizeof g_out);
		int result = xvt_flight_messages_receive_part(
			g_part, size, COOKIE, 0, &g_out);
		if (i + 1 < parts) {
			XVT_ASSERT_INT_EQ(result, 0);
			/* An exact resend of a part already held. */
			XVT_ASSERT_INT_EQ(
				xvt_flight_messages_receive_part(
					g_part, size, COOKIE, 0, &g_out),
				0);
		} else {
			XVT_ASSERT_INT_EQ(result, 1);
			XVT_ASSERT_TRUE(same_message(&g_out, &g_message));
		}
	}

	/* The message is remembered as the last assembled: an exact resend of any of its parts is old, a
	 * different one is invalid. */
	size_t size = flight_messages_part(&g_message, 1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  0);
	g_part[sizeof(struct xvt_flight_world_header) +
	       offsetof(struct xvt_flight_world_input_wire, input.key)] ^= 0x10;
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  -1);

	/* A message for an earlier tick than the last assembled is old. */
	make_message(&g_other, TARGET - XVT_WORLD_MESSAGE_TICKS, 0x02, 3);
	size = flight_messages_part(&g_other, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  0);

	/* The next message assembles. */
	make_message(&g_other, TARGET + XVT_WORLD_MESSAGE_TICKS, 0x02, 3);
	size = flight_messages_part(&g_other, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  1);
	XVT_ASSERT_TRUE(same_message(&g_out, &g_other));
}

static void check_assembly_old(void)
{
	xvt_flight_messages_reset();
	make_message(&g_message, TARGET, 0x01, 3);
	size_t size = flight_messages_part(&g_message, 0);
	/* A target at or before the confirmed tick is old. */
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   TARGET, &g_out),
			  0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   TARGET + 2, &g_out),
			  0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   TARGET - 2, &g_out),
			  1);
}

static void check_assembly_refusals(void)
{
	make_message(&g_message, TARGET, 0x03, 4);
	size_t size;

	/* Malformed: another cookie, a size that does not match the records, another opcode. */
	xvt_flight_messages_reset();
	size = flight_messages_part(&g_message, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(
				  g_part, size, COOKIE + 1, 0, &g_out),
			  -1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size - 1,
							   COOKIE, 0, &g_out),
			  -1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(
				  g_part,
				  sizeof(struct xvt_flight_world_header) - 1,
				  COOKIE, 0, &g_out),
			  -1);
	xvt_wire_set32(g_part, NET_PACKET_INPUT_BATCH);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  -1);

	/* A record whose player is outside the mask. */
	g_other = g_message;
	g_other.participant_mask = 0x01;
	size = flight_messages_part(&g_other, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  -1);

	/* A record whose tick passes the target. */
	g_other = g_message;
	g_other.target_flags = 2;
	size = flight_messages_part(&g_other, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  -1);

	/* The untouched message still assembles after all of that. */
	size = flight_messages_part(&g_message, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  1);
}

static void check_assembly_conflicts(void)
{
	/* While a message is being assembled, a part of another message is invalid, and a resend of a held
	 * part that differs is invalid. */
	xvt_flight_messages_reset();
	make_message(&g_message, TARGET, 0x01, XVT_WORLD_PART_RECORDS + 1);
	size_t size = flight_messages_part(&g_message, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  0);
	make_message(&g_other, TARGET + XVT_WORLD_MESSAGE_TICKS, 0x01, 2);
	size = flight_messages_part(&g_other, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  -1);
	size = flight_messages_part(&g_message, 0);
	g_part[sizeof(struct xvt_flight_world_header) +
	       offsetof(struct xvt_flight_world_input_wire, input.key)] ^= 0x10;
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  -1);
	/* The assembly survived both: the missing part completes it. */
	size = flight_messages_part(&g_message, 1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  1);
	XVT_ASSERT_TRUE(same_message(&g_out, &g_message));
}

static void check_unordered_message_stays(void)
{
	/* A complete message whose records are not ordered by player then rising tick is invalid... */
	xvt_flight_messages_reset();
	make_message(&g_message, TARGET, 0x03, 4);
	struct xvt_flight_world_input_wire first = g_message.records[0];
	g_message.records[0] = g_message.records[g_message.count - 1];
	g_message.records[g_message.count - 1] = first;
	size_t size = flight_messages_part(&g_message, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  -1);

	/* ...and stays assembled: the next message is a part of another message, until the assembly is
	 * discarded. */
	make_message(&g_other, TARGET + XVT_WORLD_MESSAGE_TICKS, 0x01, 2);
	size = flight_messages_part(&g_other, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  -1);
	xvt_flight_messages_discard_assembly_through(TARGET);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  1);

	/* Reset also drops it. */
	xvt_flight_messages_reset();
	size = flight_messages_part(&g_message, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  -1);
	xvt_flight_messages_reset();
	size = flight_messages_part(&g_other, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  1);
}

static void check_discard_assembly(void)
{
	xvt_flight_messages_reset();
	make_message(&g_message, TARGET, 0x01, XVT_WORLD_PART_RECORDS + 1);
	size_t size = flight_messages_part(&g_message, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  0);

	/* A tick before the target leaves the partial message alone: the other part completes it. */
	xvt_flight_messages_discard_assembly_through(TARGET -
						     XVT_NETWORK_STEP_TICKS);
	size = flight_messages_part(&g_message, 1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  1);

	/* At the target it is dropped: the second part alone is again short of the whole. */
	xvt_flight_messages_reset();
	size = flight_messages_part(&g_message, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  0);
	xvt_flight_messages_discard_assembly_through(TARGET);
	size = flight_messages_part(&g_message, 1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  0);
	size = flight_messages_part(&g_message, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  1);

	/* Reset forgets the last assembled message: its parts are new again and assemble once more. */
	xvt_flight_messages_reset();
	size = flight_messages_part(&g_message, 1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  0);
	size = flight_messages_part(&g_message, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  1);
}

static void check_queue_basics(void)
{
	xvt_flight_messages_reset();
	for (int q = 0; q < XVT_QUEUE_COUNT; ++q) {
		xvt_flight_queue queue = (xvt_flight_queue)q;
		XVT_ASSERT_INT_EQ(xvt_flight_messages_count(queue), 0);
		XVT_ASSERT_INT_EQ(xvt_flight_messages_peek(queue, g_bytes,
							   sizeof g_bytes),
				  0);
		xvt_flight_messages_pop(queue);
		XVT_ASSERT_INT_EQ(xvt_flight_messages_count(queue), 0);

		/* Size 0 is refused, by Push and by HasRoom. */
		XVT_ASSERT_INT_EQ(xvt_flight_messages_has_room(queue, 0), 0);
		XVT_ASSERT_INT_EQ(xvt_flight_messages_push(queue, "x", 0), 0);
		XVT_ASSERT_INT_EQ(xvt_flight_messages_count(queue), 0);

		/* First in, first out, each message whole. */
		XVT_ASSERT_INT_EQ(xvt_flight_messages_push(queue, "first", 5),
				  1);
		XVT_ASSERT_INT_EQ(xvt_flight_messages_push(queue, "second!", 7),
				  1);
		XVT_ASSERT_INT_EQ(xvt_flight_messages_count(queue), 2);
		/* Peek refuses a buffer smaller than the message, and leaves it queued. */
		XVT_ASSERT_INT_EQ(xvt_flight_messages_peek(queue, g_bytes, 4),
				  0);
		XVT_ASSERT_INT_EQ(xvt_flight_messages_peek(queue, g_bytes, 5),
				  5);
		XVT_ASSERT_INT_EQ(memcmp(g_bytes, "first", 5), 0);
		/* Peek does not consume. */
		XVT_ASSERT_INT_EQ(xvt_flight_messages_peek(queue, g_bytes,
							   sizeof g_bytes),
				  5);
		xvt_flight_messages_pop(queue);
		XVT_ASSERT_INT_EQ(xvt_flight_messages_count(queue), 1);
		XVT_ASSERT_INT_EQ(xvt_flight_messages_peek(queue, g_bytes,
							   sizeof g_bytes),
				  7);
		XVT_ASSERT_INT_EQ(memcmp(g_bytes, "second!", 7), 0);

		XVT_ASSERT_INT_EQ(xvt_flight_messages_push(queue, "third", 5),
				  1);
		xvt_flight_messages_clear(queue);
		XVT_ASSERT_INT_EQ(xvt_flight_messages_count(queue), 0);
		XVT_ASSERT_INT_EQ(xvt_flight_messages_peek(queue, g_bytes,
							   sizeof g_bytes),
				  0);
	}

	/* The queues are separate: Clear empties only its own. */
	XVT_ASSERT_INT_EQ(xvt_flight_messages_push(XVT_QUEUE_PENDING, "p", 1),
			  1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_push(XVT_QUEUE_REPLAY, "r", 1),
			  1);
	xvt_flight_messages_clear(XVT_QUEUE_REPLAY);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_REPLAY), 0);

	/* Reset empties both. */
	XVT_ASSERT_INT_EQ(xvt_flight_messages_push(XVT_QUEUE_REPLAY, "r", 1),
			  1);
	xvt_flight_messages_reset();
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_REPLAY), 0);
}

/* Fills queue with messages of size bytes, checking before each push that HasRoom predicts it, until a
 * push is refused; returns how many went in. The bytes of message i all hold i + 1, cut to 8 bits. */
static unsigned fill(xvt_flight_queue queue, uint8_t *buffer, size_t size,
		     unsigned first)
{
	unsigned pushed = 0;
	for (;;) {
		memset(buffer, (int)((first + pushed + 1) & 0xFF), size);
		int room = xvt_flight_messages_has_room(queue, size);
		int result = xvt_flight_messages_push(queue, buffer, size);
		XVT_ASSERT_INT_EQ(result, room);
		if (!result) {
			return pushed;
		}
		++pushed;
		XVT_ASSERT_TRUE(pushed < 1000000);
	}
}

static void check_queue_limits(void)
{
	/* Each queue has a message limit: one-byte messages run out of messages before bytes. */
	uint8_t *buffer = malloc(2 * XVT_REPLAY_BYTES);
	XVT_ASSERT_TRUE(buffer != NULL);
	xvt_flight_messages_reset();
	for (int q = 0; q < XVT_QUEUE_COUNT; ++q) {
		xvt_flight_queue queue = (xvt_flight_queue)q;
		unsigned limit = fill(queue, buffer, 1, 0);
		XVT_ASSERT_TRUE(limit > 0);
		XVT_ASSERT_INT_EQ(xvt_flight_messages_count(queue), limit);
		/* Popping one makes room for exactly one more. */
		xvt_flight_messages_pop(queue);
		XVT_ASSERT_INT_EQ(fill(queue, buffer, 1, limit), 1);
		xvt_flight_messages_clear(queue);
	}

	/* And a byte limit: large messages run out of bytes while a one-byte message still has room. The
	 * queue's bytes then hold fewer than count + 1 of them, so one message of that many bytes never fits,
	 * even in the emptied queue. */
	for (int q = 0; q < XVT_QUEUE_COUNT; ++q) {
		xvt_flight_queue queue = (xvt_flight_queue)q;
		size_t size = (q == XVT_QUEUE_PENDING ? XVT_PENDING_BYTES
						      : XVT_REPLAY_BYTES) /
				      10 +
			      1;
		unsigned count = fill(queue, buffer, size, 0);
		XVT_ASSERT_TRUE(count > 0);
		XVT_ASSERT_INT_EQ(xvt_flight_messages_has_room(queue, 1), 1);
		xvt_flight_messages_clear(queue);
		size_t whole = (count + 1) * size;
		XVT_ASSERT_TRUE(whole <= 2 * XVT_REPLAY_BYTES);
		XVT_ASSERT_INT_EQ(xvt_flight_messages_has_room(queue, whole),
				  0);
		XVT_ASSERT_INT_EQ(
			xvt_flight_messages_push(queue, buffer, whole), 0);
		XVT_ASSERT_INT_EQ(xvt_flight_messages_has_room(queue, size), 1);
	}
	free(buffer);
}

static void check_queue_wraps(void)
{
	/* Fill a queue with large messages, drop the oldest two, and refill: the new messages wrap past the
	 * end of the ring and still come back whole, in order. */
	enum { SIZE = XVT_PENDING_BYTES / 7 + 3 };

	static uint8_t buffer[SIZE];
	static uint8_t back[SIZE];
	static uint8_t expected[SIZE];
	xvt_flight_messages_reset();
	unsigned count = fill(XVT_QUEUE_PENDING, buffer, SIZE, 0);
	XVT_ASSERT_TRUE(count >= 3);
	xvt_flight_messages_pop(XVT_QUEUE_PENDING);
	xvt_flight_messages_pop(XVT_QUEUE_PENDING);
	unsigned more = fill(XVT_QUEUE_PENDING, buffer, SIZE, count);
	XVT_ASSERT_INT_EQ(more, 2);
	for (unsigned i = 2; i < count + more; ++i) {
		XVT_ASSERT_INT_EQ(xvt_flight_messages_peek(XVT_QUEUE_PENDING,
							   back, sizeof back),
				  SIZE);
		memset(expected, (int)((i + 1) & 0xFF), sizeof expected);
		XVT_ASSERT_INT_EQ(memcmp(back, expected, SIZE), 0);
		xvt_flight_messages_pop(XVT_QUEUE_PENDING);
	}
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 0);
}

static void check_enqueue(void)
{
	xvt_flight_messages_reset();
	make_message(&g_message, TARGET, 0x01, 2);
	make_message(&g_other, TARGET, 0x01, 1);
	XVT_ASSERT_INT_EQ(
		xvt_flight_messages_enqueue(&g_message, XVT_QUEUE_PENDING), 1);
	XVT_ASSERT_INT_EQ(
		xvt_flight_messages_enqueue(&g_other, XVT_QUEUE_PENDING), 1);

	/* Stored with only its count records: one record more is one record's bytes more. */
	memset(&g_out, 0, sizeof g_out);
	size_t two = xvt_flight_messages_peek(XVT_QUEUE_PENDING, &g_out,
					      sizeof g_out);
	XVT_ASSERT_TRUE(same_message(&g_out, &g_message));
	xvt_flight_messages_pop(XVT_QUEUE_PENDING);
	memset(&g_out, 0, sizeof g_out);
	size_t one = xvt_flight_messages_peek(XVT_QUEUE_PENDING, &g_out,
					      sizeof g_out);
	XVT_ASSERT_TRUE(same_message(&g_out, &g_other));
	XVT_ASSERT_INT_EQ(two - one,
			  sizeof(struct xvt_flight_world_input_wire));
	XVT_ASSERT_TRUE(two < sizeof(struct xvt_flight_message));
}

/* The target ticks of the messages on queue, oldest first; returns how many. */
static unsigned flight_messages_targets(xvt_flight_queue queue,
					unsigned *targets, unsigned capacity)
{
	unsigned count = xvt_flight_messages_count(queue);
	XVT_ASSERT_TRUE(count <= capacity);
	for (unsigned i = 0; i < count; ++i) {
		size_t size =
			xvt_flight_messages_peek(queue, &g_out, sizeof g_out);
		XVT_ASSERT_TRUE(size > 0);
		targets[i] = g_out.target_flags & INT32_MAX;
		/* Rotate, so the queue is as it was afterwards. */
		xvt_flight_messages_pop(queue);
		XVT_ASSERT_INT_EQ(xvt_flight_messages_push(queue, &g_out, size),
				  1);
	}
	return count;
}

static void enqueue_at(unsigned target, xvt_flight_queue queue)
{
	make_message(&g_other, target, 0x01, 1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_enqueue(&g_other, queue), 1);
}

static void check_prepare_recovery(void)
{
	xvt_flight_messages_reset();
	enqueue_at(8, XVT_QUEUE_REPLAY);
	enqueue_at(24, XVT_QUEUE_REPLAY);
	enqueue_at(8, XVT_QUEUE_PENDING);
	enqueue_at(16, XVT_QUEUE_PENDING);
	enqueue_at(40, XVT_QUEUE_PENDING);
	enqueue_at(32 | XVT_WORLD_CHECKSUM_FLAG, XVT_QUEUE_PENDING);
	/* A partial assembly targeting tick 16. */
	make_message(&g_message, 16, 0x3F, XVT_WORLD_PART_RECORDS + 1);
	size_t size = flight_messages_part(&g_message, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  0);

	/* Everything targeting 16 or earlier goes; what is left pending moves, in order, after the replay. */
	XVT_ASSERT_INT_EQ(xvt_flight_messages_prepare_recovery(16), 1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 0);
	unsigned targets[8];
	XVT_ASSERT_INT_EQ(flight_messages_targets(XVT_QUEUE_REPLAY, targets, 8),
			  3);
	XVT_ASSERT_INT_EQ(targets[0], 24);
	XVT_ASSERT_INT_EQ(targets[1], 40);
	XVT_ASSERT_INT_EQ(targets[2], 32);
	/* The partial assembly went with them: its second part is short of the whole. */
	size = flight_messages_part(&g_message, 1);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_receive_part(g_part, size, COOKIE,
							   0, &g_out),
			  0);
}

static void check_prepare_recovery_full(void)
{
	/* With the replay queue full, recovery fails and the pending messages stay pending. */
	xvt_flight_messages_reset();
	unsigned replayed = 0;
	make_message(&g_other, 400, 0x01, 1);
	while (xvt_flight_messages_enqueue(&g_other, XVT_QUEUE_REPLAY)) {
		XVT_ASSERT_TRUE(++replayed < 1000000);
	}
	enqueue_at(200, XVT_QUEUE_PENDING);
	enqueue_at(208, XVT_QUEUE_PENDING);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_prepare_recovery(100), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_PENDING), 2);
	XVT_ASSERT_INT_EQ(xvt_flight_messages_count(XVT_QUEUE_REPLAY),
			  replayed);
	unsigned targets[2];
	XVT_ASSERT_INT_EQ(
		flight_messages_targets(XVT_QUEUE_PENDING, targets, 2), 2);
	XVT_ASSERT_INT_EQ(targets[0], 200);
	XVT_ASSERT_INT_EQ(targets[1], 208);
}

int main(void)
{
	check_input_round_trip();
	check_decode_input_refusals();
	check_batch();
	check_part_count();
	check_encode_part();
	check_assembly();
	check_assembly_old();
	check_assembly_refusals();
	check_assembly_conflicts();
	check_unordered_message_stays();
	check_discard_assembly();
	check_queue_basics();
	check_queue_limits();
	check_queue_wraps();
	check_enqueue();
	check_prepare_recovery();
	check_prepare_recovery_full();
	xvt_flight_messages_reset();
	return 0;
}
