#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt/net/flight_net.h"

#include "xvt_runtime/input/flight_controls.h"
#include <limits.h>
#include <string.h>

void XvtFlightWire_EncodeInput(XvtFlightInputWire* record, int tick, const FlightInputFrameRecord* input) {
	XvtWire_Set32(record->tick, (uint32_t)tick);
	record->key = input->key;
	XvtFlightControls_EncodeAxes(record->axes, input);
	bool throttle = (input->flags & XVT_INPUT_THROTTLE_PRESENT) != 0;
	record->axes[2] |= throttle;
	XvtWire_Set16(record->throttle, throttle ? input->throttle : 0);
}

int XvtFlightWire_DecodeInput(const XvtFlightInputWire* record, int* tick, FlightInputFrameRecord* input) {
	*tick = (int)XvtWire_Get32(record->tick);
	if (!XvtFlightWire_ValidTick((uint32_t)*tick) ||
		(!(record->axes[2] & 1) && XvtWire_Get16(record->throttle)))
		return 0;
	memset(input, 0, sizeof *input);
	input->key = record->key;
	XvtFlightControls_DecodeAxes(record->axes, input);
	input->flags = (record->axes[2] & 1) ? XVT_INPUT_THROTTLE_PRESENT : 0;
	input->throttle = XvtWire_Get16(record->throttle);
	return 1;
}

size_t XvtFlightMessages_EncodeBatch(uint8_t* out, uint32_t cookie, const XvtFlightInputWire* records,
									 unsigned count) {
	if (!cookie || !count || count > XVT_INPUT_BATCH_RECORDS)
		return 0;
	XvtFlightBatchHeader header = { 0 };
	XvtWire_Set32(header.opcode, NET_PACKET_INPUT_BATCH);
	XvtWire_Set32(header.cookie, cookie);
	XvtWire_Set16(header.count, count);
	memcpy(out, &header, sizeof header);
	memcpy(out + sizeof header, records, count * sizeof *records);
	return sizeof header + count * sizeof *records;
}

int XvtFlightMessages_DecodeBatch(const uint8_t* bytes, size_t size, uint32_t cookie) {
	XvtFlightBatchHeader header;
	if (size < sizeof header)
		return 0;
	memcpy(&header, bytes, sizeof header);
	unsigned count = XvtWire_Get16(header.count);
	if (XvtWire_Get32(header.opcode) != NET_PACKET_INPUT_BATCH || !cookie ||
		XvtWire_Get32(header.cookie) != cookie || XvtWire_Get16(header.reserved) || !count ||
		count > XVT_INPUT_BATCH_RECORDS || size != sizeof header + count * sizeof(XvtFlightInputWire))
		return 0;
	int previous = 0;
	for (unsigned i = 0; i < count; ++i) {
		int tick;
		FlightInputFrameRecord input;
		XvtFlightInputWire record;
		memcpy(&record, bytes + sizeof header + i * sizeof record, sizeof record);
		if (!XvtFlightWire_DecodeInput(&record, &tick, &input) || tick <= previous)
			return 0;
		previous = tick;
	}
	return 1;
}

unsigned XvtFlightMessages_PartCount(unsigned count) {
	return count ? (count + XVT_WORLD_PART_RECORDS - 1) / XVT_WORLD_PART_RECORDS : 1;
}

size_t XvtFlightMessages_EncodePart(uint8_t* out, const XvtFlightMessage* message, uint32_t cookie,
									unsigned part) {
	unsigned parts = XvtFlightMessages_PartCount(message->count);
	if (message->count > XVT_WORLD_RECORDS || part >= parts || !cookie)
		return 0;
	unsigned first = part * XVT_WORLD_PART_RECORDS, count = message->count - first;
	if (count > XVT_WORLD_PART_RECORDS)
		count = XVT_WORLD_PART_RECORDS;
	XvtFlightWorldHeader header = { 0 };
	XvtWire_Set32(header.opcode, NET_PACKET_WORLD_MESSAGE);
	XvtWire_Set32(header.target_flags, message->target_flags);
	XvtWire_Set32(header.cookie, cookie);
	header.part_index = part;
	header.part_count = parts;
	header.record_count = count;
	header.participant_mask = message->mask;
	memcpy(out, &header, sizeof header);
	memcpy(out + sizeof header, message->records + first, count * sizeof message->records[0]);
	return sizeof header + count * sizeof message->records[0];
}

static struct {
	XvtFlightMessage message;
	uint8_t seen[XVT_WORLD_PARTS], counts[XVT_WORLD_PARTS], parts;
	unsigned received;
} g_parts;

static XvtFlightMessage g_lastAssembled;

int XvtFlightMessages_ReceivePart(const uint8_t* bytes, size_t size, uint32_t cookie, int confirmed,
								  XvtFlightMessage* out) {
	XvtFlightWorldHeader header;
	if (size < sizeof header)
		return -1;
	memcpy(&header, bytes, sizeof header);
	uint32_t flags = XvtWire_Get32(header.target_flags), tick = flags & INT32_MAX;
	unsigned part = header.part_index, parts = header.part_count, count = header.record_count,
			 mask = header.participant_mask;
	if (XvtWire_Get32(header.opcode) != NET_PACKET_WORLD_MESSAGE || !cookie ||
		XvtWire_Get32(header.cookie) != cookie || !XvtFlightWire_ValidTick(tick) || !parts ||
		parts > XVT_WORLD_PARTS || part >= parts || count > XVT_WORLD_PART_RECORDS || !mask ||
		size != sizeof header + count * sizeof(XvtFlightWorldInputWire) ||
		(part + 1 < parts && count != XVT_WORLD_PART_RECORDS) || (parts > 1 && !count) ||
		part * XVT_WORLD_PART_RECORDS + count > XVT_WORLD_RECORDS)
		return -1;
	if (tick <= (unsigned)confirmed)
		return 0;
	unsigned previous = g_lastAssembled.target_flags & INT32_MAX;
	if (tick < previous)
		return 0;
	unsigned first = part * XVT_WORLD_PART_RECORDS;
	const uint8_t* payload = bytes + sizeof header;
	size_t payload_size = count * sizeof(XvtFlightWorldInputWire);
	if (tick == previous) {
		unsigned expected_count = first <= g_lastAssembled.count ? g_lastAssembled.count - first : 0;
		if (expected_count > XVT_WORLD_PART_RECORDS)
			expected_count = XVT_WORLD_PART_RECORDS;
		return flags == g_lastAssembled.target_flags && mask == g_lastAssembled.mask &&
					   parts == XvtFlightMessages_PartCount(g_lastAssembled.count) &&
					   count == expected_count &&
					   !memcmp(g_lastAssembled.records + first, payload, payload_size)
				   ? 0
				   : -1;
	}
	for (unsigned i = 0; i < count; ++i) {
		XvtFlightWorldInputWire record;
		int time;
		FlightInputFrameRecord input;
		memcpy(&record, payload + i * sizeof record, sizeof record);
		if (record.player >= XVT_FLIGHT_PLAYERS || !(mask & (1u << record.player)) ||
			!XvtFlightWire_DecodeInput(&record.input, &time, &input) || (unsigned)time > tick)
			return -1;
	}
	if (g_parts.parts &&
		(g_parts.message.target_flags != flags || g_parts.parts != parts || g_parts.message.mask != mask))
		return -1;
	if (!g_parts.parts) {
		g_parts.parts = parts;
		g_parts.message.target_flags = flags;
		g_parts.message.mask = mask;
	}
	XvtFlightWorldInputWire* dest = g_parts.message.records + first;
	if (g_parts.seen[part])
		return g_parts.counts[part] == count && !memcmp(dest, payload, payload_size) ? 0 : -1;
	memcpy(dest, payload, payload_size);
	g_parts.seen[part] = 1;
	g_parts.counts[part] = count;
	g_parts.message.count += count;
	if (++g_parts.received != parts)
		return 0;
	for (unsigned i = 1; i < g_parts.message.count; ++i) {
		const XvtFlightWorldInputWire* a = &g_parts.message.records[i - 1];
		const XvtFlightWorldInputWire* b = &g_parts.message.records[i];
		if (a->player > b->player ||
			(a->player == b->player && XvtWire_Get32(a->input.tick) >= XvtWire_Get32(b->input.tick)))
			return -1;
	}
	*out = g_parts.message;
	g_lastAssembled = g_parts.message;
	memset(&g_parts, 0, sizeof g_parts);
	return 1;
}

typedef struct MessageQueue {
	uint8_t* bytes;
	size_t capacity, read, used, lengths[XVT_REPLAY_MESSAGES];
	unsigned head, count, limit;
} MessageQueue;

static uint8_t g_pendingBytes[XVT_PENDING_BYTES], g_replayBytes[XVT_REPLAY_BYTES];
static MessageQueue g_queues[XVT_QUEUE_COUNT] = {
	{ .bytes = g_pendingBytes, .capacity = sizeof g_pendingBytes, .limit = XVT_PENDING_MESSAGES },
	{ .bytes = g_replayBytes, .capacity = sizeof g_replayBytes, .limit = XVT_REPLAY_MESSAGES }
};

int XvtFlightMessages_Push(XvtFlightQueue queue, const void* bytes, size_t size) {
	MessageQueue* q = &g_queues[queue];
	if (!size || q->count == q->limit || size > q->capacity - q->used)
		return 0;
	size_t offset = (q->read + q->used) % q->capacity, first = q->capacity - offset;
	if (first > size)
		first = size;
	memcpy(q->bytes + offset, bytes, first);
	memcpy(q->bytes, (const uint8_t*)bytes + first, size - first);
	q->lengths[(q->head + q->count++) % q->limit] = size;
	q->used += size;
	return 1;
}

size_t XvtFlightMessages_Peek(XvtFlightQueue queue, void* bytes, size_t capacity) {
	MessageQueue* q = &g_queues[queue];
	if (!q->count)
		return 0;
	size_t size = q->lengths[q->head], first = q->capacity - q->read;
	if (size > capacity)
		return 0;
	if (first > size)
		first = size;
	memcpy(bytes, q->bytes + q->read, first);
	memcpy((uint8_t*)bytes + first, q->bytes, size - first);
	return size;
}

void XvtFlightMessages_Pop(XvtFlightQueue queue) {
	MessageQueue* q = &g_queues[queue];
	if (!q->count)
		return;
	size_t size = q->lengths[q->head];
	q->read = (q->read + size) % q->capacity;
	q->used -= size;
	q->head = (q->head + 1) % q->limit;
	--q->count;
}

void XvtFlightMessages_Clear(XvtFlightQueue queue) {
	MessageQueue* q = &g_queues[queue];
	q->read = q->used = q->head = q->count = 0;
}

void XvtFlightMessages_Reset(void) {
	memset(&g_lastAssembled, 0, sizeof g_lastAssembled);
	XvtFlightMessages_Clear(XVT_QUEUE_PENDING);
	XvtFlightMessages_Clear(XVT_QUEUE_REPLAY);
	memset(&g_parts, 0, sizeof g_parts);
}

static size_t XvtFlightMessages_StoreBytes(const XvtFlightMessage* message) {
	return offsetof(XvtFlightMessage, records) + message->count * sizeof message->records[0];
}

int XvtFlightMessages_Enqueue(const XvtFlightMessage* message, XvtFlightQueue queue) {
	return XvtFlightMessages_Push(queue, message, XvtFlightMessages_StoreBytes(message));
}

static void XvtFlightMessages_DiscardThrough(XvtFlightQueue queue, unsigned tick) {
	MessageQueue* storage = &g_queues[queue];
	unsigned count = storage->count;
	XvtFlightMessage message;
	for (unsigned i = 0; i < count; ++i) {
		size_t size = XvtFlightMessages_Peek(queue, &message, sizeof message);
		if (!size)
			break;
		XvtFlightMessages_Pop(queue);
		if ((message.target_flags & INT32_MAX) > tick)
			XvtFlightMessages_Push(queue, &message, size);
	}
}

int XvtFlightMessages_PrepareRecovery(unsigned tick) {
	XvtFlightMessages_DiscardThrough(XVT_QUEUE_REPLAY, tick);
	XvtFlightMessages_DiscardThrough(XVT_QUEUE_PENDING, tick);
	XvtFlightMessages_DiscardAssemblyThrough(tick);
	XvtFlightMessage message;
	while (XvtFlightMessages_Peek(XVT_QUEUE_PENDING, &message, sizeof message)) {
		if (!XvtFlightMessages_Enqueue(&message, XVT_QUEUE_REPLAY))
			return 0;
		XvtFlightMessages_Pop(XVT_QUEUE_PENDING);
	}
	return 1;
}

void XvtFlightMessages_DiscardAssemblyThrough(unsigned tick) {
	if (g_parts.parts && (g_parts.message.target_flags & INT32_MAX) <= tick)
		memset(&g_parts, 0, sizeof g_parts);
}

unsigned XvtFlightMessages_Count(XvtFlightQueue queue) { return g_queues[queue].count; }

int XvtFlightMessages_HasRoom(XvtFlightQueue queue, size_t size) {
	const MessageQueue* storage = &g_queues[queue];
	return size && storage->count < storage->limit && size <= storage->capacity - storage->used;
}
