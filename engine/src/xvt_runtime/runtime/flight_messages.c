#include "xvt_runtime/runtime/flight_messages.h"

#include <limits.h>
#include <string.h>

#include "xvt/net/flight_net.h"
#include "xvt_runtime/input/flight_controls.h"

void xvt_flight_wire_encode_input(struct xvt_flight_input_wire *record,
				  int tick,
				  const struct flight_input_frame_record *input)
{
	xvt_wire_set32(record->tick, (uint32_t)tick);
	record->key = input->key;
	xvt_flight_controls_encode_axes(record->axes, input);
	bool throttle = (input->flags & XVT_INPUT_THROTTLE_PRESENT) != 0;
	record->axes[2] |= throttle;
	xvt_wire_set16(record->throttle, throttle ? input->throttle : 0);
}

int xvt_flight_wire_decode_input(const struct xvt_flight_input_wire *record,
				 int *tick,
				 struct flight_input_frame_record *input)
{
	*tick = (int)xvt_wire_get32(record->tick);
	if (!xvt_flight_wire_valid_tick((uint32_t)*tick) ||
	    (!(record->axes[2] & 1) && xvt_wire_get16(record->throttle))) {
		return 0;
	}
	memset(input, 0, sizeof *input);
	input->key = record->key;
	xvt_flight_controls_decode_axes(record->axes, input);
	input->flags = (record->axes[2] & 1) ? XVT_INPUT_THROTTLE_PRESENT : 0;
	input->throttle = xvt_wire_get16(record->throttle);
	return 1;
}

size_t
xvt_flight_messages_encode_batch(uint8_t *out, uint32_t cookie,
				 const struct xvt_flight_input_wire *records,
				 unsigned count)
{
	if (!cookie || !count || count > XVT_INPUT_BATCH_RECORDS) {
		return 0;
	}
	struct xvt_flight_batch_header header = {0};
	xvt_wire_set32(header.opcode, NET_PACKET_INPUT_BATCH);
	xvt_wire_set32(header.cookie, cookie);
	xvt_wire_set16(header.count, count);
	memcpy(out, &header, sizeof header);
	memcpy(out + sizeof header, records, count * sizeof *records);
	return sizeof header + count * sizeof *records;
}

int xvt_flight_messages_validate_batch(const uint8_t *bytes, size_t size,
				       uint32_t cookie)
{
	struct xvt_flight_batch_header header;
	if (size < sizeof header) {
		return 0;
	}
	memcpy(&header, bytes, sizeof header);
	unsigned count = xvt_wire_get16(header.count);
	if (xvt_wire_get32(header.opcode) != NET_PACKET_INPUT_BATCH ||
	    !cookie || xvt_wire_get32(header.cookie) != cookie ||
	    xvt_wire_get16(header.reserved) || !count ||
	    count > XVT_INPUT_BATCH_RECORDS ||
	    size != sizeof header +
			    count * sizeof(struct xvt_flight_input_wire)) {
		return 0;
	}
	int previous = 0;
	for (unsigned i = 0; i < count; ++i) {
		struct xvt_flight_input_wire record;
		memcpy(&record, bytes + sizeof header + i * sizeof record,
		       sizeof record);
		int tick;
		struct flight_input_frame_record input;
		if (!xvt_flight_wire_decode_input(&record, &tick, &input) ||
		    tick <= previous) {
			return 0;
		}
		previous = tick;
	}
	return 1;
}

unsigned xvt_flight_messages_part_count(unsigned count)
{
	return count ? (count + XVT_WORLD_PART_RECORDS - 1) /
			       XVT_WORLD_PART_RECORDS
		     : 1;
}

size_t xvt_flight_messages_encode_part(uint8_t *out,
				       const struct xvt_flight_message *message,
				       uint32_t cookie, unsigned part)
{
	unsigned parts = xvt_flight_messages_part_count(message->count);
	if (message->count > XVT_WORLD_RECORDS || part >= parts || !cookie) {
		return 0;
	}
	unsigned first = part * XVT_WORLD_PART_RECORDS;
	unsigned count = message->count - first;
	if (count > XVT_WORLD_PART_RECORDS) {
		count = XVT_WORLD_PART_RECORDS;
	}
	struct xvt_flight_world_header header = {0};
	xvt_wire_set32(header.opcode, NET_PACKET_WORLD_MESSAGE);
	xvt_wire_set32(header.target_flags, message->target_flags);
	xvt_wire_set32(header.cookie, cookie);
	header.part_index = part;
	header.part_count = parts;
	header.record_count = count;
	header.participant_mask = message->participant_mask;
	memcpy(out, &header, sizeof header);
	memcpy(out + sizeof header, message->records + first,
	       count * sizeof message->records[0]);
	return sizeof header + count * sizeof message->records[0];
}

static struct {
	struct xvt_flight_message message;
	uint8_t seen[XVT_WORLD_PARTS];
	uint8_t counts[XVT_WORLD_PARTS];
	uint8_t parts;
	unsigned received;
} g_parts;

static struct xvt_flight_message g_last_assembled;

int xvt_flight_messages_receive_part(const uint8_t *bytes, size_t size,
				     uint32_t cookie, int confirmed,
				     struct xvt_flight_message *out)
{
	struct xvt_flight_world_header header;
	if (size < sizeof header) {
		return -1;
	}
	memcpy(&header, bytes, sizeof header);
	uint32_t flags = xvt_wire_get32(header.target_flags);
	uint32_t tick = flags & INT32_MAX;
	unsigned part = header.part_index;
	unsigned parts = header.part_count;
	unsigned count = header.record_count;
	unsigned mask = header.participant_mask;
	if (xvt_wire_get32(header.opcode) != NET_PACKET_WORLD_MESSAGE ||
	    !cookie || xvt_wire_get32(header.cookie) != cookie ||
	    !xvt_flight_wire_valid_tick(tick) || !parts ||
	    parts > XVT_WORLD_PARTS || part >= parts ||
	    count > XVT_WORLD_PART_RECORDS || !mask ||
	    size != sizeof header +
			    count * sizeof(struct
					   xvt_flight_world_input_wire) ||
	    (part + 1 < parts && count != XVT_WORLD_PART_RECORDS) ||
	    (parts > 1 && !count) ||
	    part * XVT_WORLD_PART_RECORDS + count > XVT_WORLD_RECORDS) {
		return -1;
	}
	if (tick <= (unsigned)confirmed) {
		return 0;
	}
	unsigned previous = g_last_assembled.target_flags & INT32_MAX;
	if (tick < previous) {
		return 0;
	}
	unsigned first = part * XVT_WORLD_PART_RECORDS;
	const uint8_t *payload = bytes + sizeof header;
	size_t payload_size =
		count * sizeof(struct xvt_flight_world_input_wire);
	if (tick == previous) {
		unsigned expected_count =
			first <= g_last_assembled.count
				? g_last_assembled.count - first
				: 0;
		if (expected_count > XVT_WORLD_PART_RECORDS) {
			expected_count = XVT_WORLD_PART_RECORDS;
		}
		return flags == g_last_assembled.target_flags &&
				       mask == g_last_assembled
						       .participant_mask &&
				       parts == xvt_flight_messages_part_count(
							g_last_assembled
								.count) &&
				       count == expected_count &&
				       !memcmp(g_last_assembled.records + first,
					       payload, payload_size)
			       ? 0
			       : -1;
	}
	for (unsigned i = 0; i < count; ++i) {
		struct xvt_flight_world_input_wire record;
		memcpy(&record, payload + i * sizeof record, sizeof record);
		int record_tick;
		struct flight_input_frame_record input;
		if (record.player >= XVT_FLIGHT_PLAYERS ||
		    !(mask & (1u << record.player)) ||
		    !xvt_flight_wire_decode_input(&record.input, &record_tick,
						  &input) ||
		    (unsigned)record_tick > tick) {
			return -1;
		}
	}
	if (g_parts.parts &&
	    (g_parts.message.target_flags != flags || g_parts.parts != parts ||
	     g_parts.message.participant_mask != mask)) {
		return -1;
	}
	if (!g_parts.parts) {
		g_parts.parts = parts;
		g_parts.message.target_flags = flags;
		g_parts.message.participant_mask = mask;
	}
	struct xvt_flight_world_input_wire *dest =
		g_parts.message.records + first;
	if (g_parts.seen[part]) {
		return g_parts.counts[part] == count &&
				       !memcmp(dest, payload, payload_size)
			       ? 0
			       : -1;
	}
	memcpy(dest, payload, payload_size);
	g_parts.seen[part] = 1;
	g_parts.counts[part] = count;
	g_parts.message.count += count;
	if (++g_parts.received != parts) {
		return 0;
	}
	for (unsigned i = 1; i < g_parts.message.count; ++i) {
		const struct xvt_flight_world_input_wire *a =
			&g_parts.message.records[i - 1];
		const struct xvt_flight_world_input_wire *b =
			&g_parts.message.records[i];
		if (a->player > b->player ||
		    (a->player == b->player &&
		     xvt_wire_get32(a->input.tick) >=
			     xvt_wire_get32(b->input.tick))) {
			return -1;
		}
	}
	*out = g_parts.message;
	g_last_assembled = g_parts.message;
	memset(&g_parts, 0, sizeof g_parts);
	return 1;
}

struct message_queue {
	uint8_t *bytes;
	size_t capacity;
	size_t read;
	size_t used;
	size_t lengths[XVT_REPLAY_MESSAGES];
	unsigned head;
	unsigned count;
	unsigned limit;
};

static uint8_t g_pending_bytes[XVT_PENDING_BYTES];
static uint8_t g_replay_bytes[XVT_REPLAY_BYTES];
static struct message_queue g_queues[XVT_QUEUE_COUNT] = {
	{.bytes = g_pending_bytes,
	 .capacity = sizeof g_pending_bytes,
	 .limit = XVT_PENDING_MESSAGES},
	{.bytes = g_replay_bytes,
	 .capacity = sizeof g_replay_bytes,
	 .limit = XVT_REPLAY_MESSAGES}};

int xvt_flight_messages_push(xvt_flight_queue queue, const void *bytes,
			     size_t size)
{
	struct message_queue *q = &g_queues[queue];
	if (!size || q->count == q->limit || size > q->capacity - q->used) {
		return 0;
	}
	size_t offset = (q->read + q->used) % q->capacity;
	size_t first = q->capacity - offset;
	if (first > size) {
		first = size;
	}
	memcpy(q->bytes + offset, bytes, first);
	memcpy(q->bytes, (const uint8_t *)bytes + first, size - first);
	q->lengths[(q->head + q->count++) % q->limit] = size;
	q->used += size;
	return 1;
}

size_t xvt_flight_messages_peek(xvt_flight_queue queue, void *bytes,
				size_t capacity)
{
	struct message_queue *q = &g_queues[queue];
	if (!q->count) {
		return 0;
	}
	size_t size = q->lengths[q->head];
	size_t first = q->capacity - q->read;
	if (size > capacity) {
		return 0;
	}
	if (first > size) {
		first = size;
	}
	memcpy(bytes, q->bytes + q->read, first);
	memcpy((uint8_t *)bytes + first, q->bytes, size - first);
	return size;
}

void xvt_flight_messages_pop(xvt_flight_queue queue)
{
	struct message_queue *q = &g_queues[queue];
	if (!q->count) {
		return;
	}
	size_t size = q->lengths[q->head];
	q->read = (q->read + size) % q->capacity;
	q->used -= size;
	q->head = (q->head + 1) % q->limit;
	--q->count;
}

void xvt_flight_messages_clear(xvt_flight_queue queue)
{
	struct message_queue *q = &g_queues[queue];
	q->count = 0;
	q->head = q->count;
	q->used = q->head;
	q->read = q->used;
}

void xvt_flight_messages_reset(void)
{
	memset(&g_last_assembled, 0, sizeof g_last_assembled);
	xvt_flight_messages_clear(XVT_QUEUE_PENDING);
	xvt_flight_messages_clear(XVT_QUEUE_REPLAY);
	memset(&g_parts, 0, sizeof g_parts);
}

static size_t
xvt_flight_messages_stored_size(const struct xvt_flight_message *message)
{
	return offsetof(struct xvt_flight_message, records) +
	       message->count * sizeof message->records[0];
}

int xvt_flight_messages_enqueue(const struct xvt_flight_message *message,
				xvt_flight_queue queue)
{
	return xvt_flight_messages_push(
		queue, message, xvt_flight_messages_stored_size(message));
}

static void xvt_flight_messages_discard_through(xvt_flight_queue queue,
						unsigned tick)
{
	struct message_queue *storage = &g_queues[queue];
	unsigned count = storage->count;
	struct xvt_flight_message message;
	for (unsigned i = 0; i < count; ++i) {
		size_t size = xvt_flight_messages_peek(queue, &message,
						       sizeof message);
		if (!size) {
			break;
		}
		xvt_flight_messages_pop(queue);
		if ((message.target_flags & INT32_MAX) > tick) {
			xvt_flight_messages_push(queue, &message, size);
		}
	}
}

int xvt_flight_messages_prepare_recovery(unsigned tick)
{
	xvt_flight_messages_discard_through(XVT_QUEUE_REPLAY, tick);
	xvt_flight_messages_discard_through(XVT_QUEUE_PENDING, tick);
	xvt_flight_messages_discard_assembly_through(tick);
	struct xvt_flight_message message;
	while (xvt_flight_messages_peek(XVT_QUEUE_PENDING, &message,
					sizeof message)) {
		if (!xvt_flight_messages_enqueue(&message, XVT_QUEUE_REPLAY)) {
			return 0;
		}
		xvt_flight_messages_pop(XVT_QUEUE_PENDING);
	}
	return 1;
}

void xvt_flight_messages_discard_assembly_through(unsigned tick)
{
	if (g_parts.parts &&
	    (g_parts.message.target_flags & INT32_MAX) <= tick) {
		memset(&g_parts, 0, sizeof g_parts);
	}
}

unsigned xvt_flight_messages_count(xvt_flight_queue queue)
{
	return g_queues[queue].count;
}

int xvt_flight_messages_has_room(xvt_flight_queue queue, size_t size)
{
	const struct message_queue *storage = &g_queues[queue];
	return size && storage->count < storage->limit &&
	       size <= storage->capacity - storage->used;
}
