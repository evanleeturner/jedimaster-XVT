#ifndef XVT_RUNTIME_FLIGHT_PROTOCOL_H
#define XVT_RUNTIME_FLIGHT_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

enum {
	XVT_FLIGHT_PLAYERS = 8,
	XVT_INPUT_HISTORY_CAPACITY = 450,
	XVT_FLIGHT_PACKET_BYTES = 508,
	XVT_WORLD_RECORDS = XVT_FLIGHT_PLAYERS * XVT_INPUT_HISTORY_CAPACITY,
	XVT_WORLD_PART_RECORDS = 44,
	XVT_WORLD_PARTS = (XVT_WORLD_RECORDS + XVT_WORLD_PART_RECORDS - 1) /
			  XVT_WORLD_PART_RECORDS,
	XVT_INPUT_BATCH_RECORDS = 16,
	XVT_INPUT_BATCHES_PER_ITERATION = 4,
	XVT_INPUT_STAGED_RECORDS =
		XVT_INPUT_BATCH_RECORDS * XVT_INPUT_BATCHES_PER_ITERATION,
	XVT_WORLD_PARTS_PER_ITERATION = 4,
	XVT_NETWORK_PACKETS_PER_ITERATION = 64,
	XVT_WORLD_LATE_INTERVALS = 5,
	XVT_WORLD_START_LEAD_SHIFT = 3,
	XVT_INPUT_AUTHORITATIVE = 0,
	XVT_INPUT_REAL = 1,
	XVT_INPUT_PREDICTED = 2,
	XVT_NETWORK_STEP_TICKS = 2,
	XVT_NATIVE_STEP_TICKS = 8,
	XVT_OFFLINE_STEP_TICKS = 1,
	XVT_COMPONENT_EVENT_TICKS = 32,
	XVT_COMPONENT_TIMER_TICKS = 29,
	XVT_REFERENCE_TICKS = 8,
	XVT_INPUT_BATCH_TICKS = 4,
	XVT_WORLD_MESSAGE_TICKS = 8,
	XVT_PREDICTION_LEAD_TICKS = 256,
	XVT_SIM_STEPS_PER_ITERATION = 16,
	XVT_SIM_BUDGET_US = 2000,
	XVT_FLIGHT_TICK_US = 4000,
	XVT_WORLD_CHECKSUM_TICKS = 472,
	XVT_WORLD_CHECKSUM_REGIONS = 16,
	XVT_TIMING_CHECKSUM_REGION = XVT_WORLD_CHECKSUM_REGIONS - 1,
	XVT_RESYNC_CHUNKS_PER_BATCH = 16,
	XVT_RESYNC_CHUNK_MIN_FREE = 32,
	XVT_RESYNC_RETRIES = 10,
	XVT_RESYNC_ACK_RETRIES = 20,
	XVT_RESYNC_RETRY_TICKS = 236,
	XVT_RESYNC_REPLAY_RESTARTS = 1,
	XVT_PEER_TIMEOUT_TICKS = 7080,
	XVT_PENDING_MESSAGES = 128,
	XVT_PENDING_BYTES = 256 * 1024,
	XVT_REPLAY_MESSAGES = 512,
	XVT_REPLAY_BYTES = 1024 * 1024,
	XVT_DEFERRED_CHECKSUMS = 256,
	XVT_TIMING_SCHEMA = 5,
	XVT_WIRE_PROFILE_NETWORK_125 = 1,
	XVT_CHECKSUM_REPORT = 0,
	XVT_CHECKSUM_REQUEST_STATE = 1,
};

static const uint32_t XVT_WORLD_CHECKSUM_FLAG = UINT32_C(0x80000000);

/* Byte-array scalars keep wire layouts endian-independent and naturally byte aligned. */
typedef uint8_t xvt_wire_u16[2];
typedef uint8_t xvt_wire_u32[4];
typedef uint8_t xvt_wire_u64[8];

struct xvt_flight_input_wire {
	xvt_wire_u32 tick;
	uint8_t key, axes[3];
	xvt_wire_u16 throttle;
};

struct xvt_flight_world_input_wire {
	uint8_t player;
	struct xvt_flight_input_wire input;
};

struct xvt_flight_batch_header {
	xvt_wire_u32 opcode, cookie;
	xvt_wire_u16 count, reserved;
};

struct xvt_flight_world_header {
	xvt_wire_u32 opcode, target_flags, cookie;
	uint8_t part_index, part_count, record_count, participant_mask;
};

struct xvt_flight_agreement_wire {
	xvt_wire_u32 schema, profile, cookie;
};

struct xvt_flight_options_wire {
	xvt_wire_u32 opcode, resolution, rating, schema;
};

struct xvt_flight_roster_header {
	xvt_wire_u32 opcode, new_net;
};

struct xvt_flight_roster_player_wire {
	xvt_wire_u32 resolution, rating;
};

struct xvt_flight_slot_wire {
	xvt_wire_u32 opcode, player;
};

struct xvt_flight_epoch_wire {
	xvt_wire_u32 opcode, epoch;
};

struct xvt_flight_clock_probe_wire {
	xvt_wire_u32 opcode, timestamp, lead;
};

struct xvt_flight_checksum_wire {
	xvt_wire_u32 opcode, epoch;
	xvt_wire_u32 checksums[XVT_WORLD_CHECKSUM_REGIONS],
		lengths[XVT_WORLD_CHECKSUM_REGIONS];
};

struct xvt_flight_checksum_report_wire {
	struct xvt_flight_checksum_wire checksum;
	xvt_wire_u32 request_state;
};

struct xvt_flight_resync_request_wire {
	xvt_wire_u32 opcode, epoch, image_bytes, completed_tick;
};

struct xvt_flight_resync_apply_wire {
	xvt_wire_u32 opcode, epoch, image_bytes, input_tick;
};

struct xvt_flight_chunk_header {
	xvt_wire_u32 opcode, epoch, index;
};

struct xvt_flight_chunk_span {
	xvt_wire_u32 offset, bytes;
};

struct xvt_flight_chunk_ack_wire {
	xvt_wire_u32 opcode, index;
};

typedef char xvt_flight_input_wire_layout
	[(sizeof(struct xvt_flight_input_wire) == 10) ? 1 : -1];
typedef char xvt_flight_world_input_wire_layout
	[(sizeof(struct xvt_flight_world_input_wire) == 11) ? 1 : -1];
typedef char xvt_flight_world_header_layout
	[(sizeof(struct xvt_flight_world_header) == 16) ? 1 : -1];
#endif
