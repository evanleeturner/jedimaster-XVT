#ifndef XVT_RUNTIME_FLIGHT_STATE_H
#define XVT_RUNTIME_FLIGHT_STATE_H
#include "xvt_runtime/runtime/flight_protocol.h"

enum {
	XVT_STATE_MAGIC = 0x32545658,
	XVT_STATE_SCHEMA = 2,
	XVT_STATE_POSITION_AXES = 3,
	XVT_Q15_SCALE = 1 << 15,
	XVT_Q16_SCALE = 1 << 16,
	XVT_LOCK_HALF_NONE = 0,
	XVT_LOCK_HALF_CHAFF = 1,
	XVT_LOCK_HALF_TARGET_LOSS = 2,
	XVT_ROLL_MODIFIER = 2,
	XVT_ROLL_MODIFIER_MASK = 0xe,
	XVT_STATE_INTEGRATION_CHANNELS = 12,
	XVT_STATE_PLAYER_CHANNELS = 9,
	XVT_MOTION_VALID = 1,
	XVT_MOTION_CURRENT_VALID = 2,
	XVT_PAIRED_OWNER_VALID = 1,
	XVT_PAIRED_CARRIED_VALID = 2,
	XVT_CONTROL_ROLL = 1,
	XVT_CONTROL_DISABLED = 2,
	XVT_CONTROL_BLOCKED = 4,
	XVT_CONTROL_MAP = 8,
	XVT_CONTROL_HYPERSPACE = 16,
	XVT_CONTROL_MASK = XVT_CONTROL_ROLL | XVT_CONTROL_DISABLED | XVT_CONTROL_BLOCKED | XVT_CONTROL_MAP |
					   XVT_CONTROL_HYPERSPACE
};

typedef struct XvtStateHeader {
	XvtWireU16 schema, reference_count, integration_count, player_count;
} XvtStateHeader;

typedef struct XvtStateFooter {
	XvtWireU32 magic;
	XvtWireU16 schema, profile;
	XvtWireU32 cookie, completed_tick, world_bytes, timing_bytes, timing_crc;
} XvtStateFooter;

typedef struct XvtReferenceMotionWire {
	XvtWireU16 slot, signature;
	uint8_t type, flags;
	XvtWireU16 reserved;
	XvtWireU32 position[XVT_STATE_POSITION_AXES], sample_tick, current_tick;
} XvtReferenceMotionWire;

typedef struct XvtIntegrationWire {
	XvtWireU16 slot, signature;
	uint8_t type, state;
	XvtWireU16 carried_slot, target_slot, target_signature;
	XvtWireU64 position[XVT_STATE_POSITION_AXES], remainder[XVT_STATE_INTEGRATION_CHANNELS];
	int8_t direction[XVT_STATE_INTEGRATION_CHANNELS];
} XvtIntegrationWire;

typedef struct XvtPlayerTimingWire {
	uint8_t player, valid;
	XvtWireU16 slot, signature, reserved;
	XvtWireU64 remainder[XVT_STATE_PLAYER_CHANNELS];
	int8_t direction[XVT_STATE_PLAYER_CHANNELS];
	uint8_t lock_mode, lock_half, control_valid;
	XvtWireU32 control_mode;
	XvtWireU16 lock_signature, lock_target, lock_target_signature, lock_weapon;
	XvtWireU64 lock_frame;
	XvtWireU16 camera_focus, reserved_tail;
} XvtPlayerTimingWire;

typedef struct XvtObjectIdentityWire {
	XvtWireU16 slot, signature;
} XvtObjectIdentityWire;

typedef struct XvtObjectMotionWire {
	XvtReferenceMotionWire reference;
	XvtIntegrationWire integration;
} XvtObjectMotionWire;

typedef struct XvtPairedMotionWire {
	uint8_t player, validity;
	XvtWireU16 reserved;
	XvtWireU32 saved_tick;
	XvtObjectIdentityWire owner_id, carried_id;
	XvtObjectMotionWire owner, carried;
} XvtPairedMotionWire;

typedef struct XvtMembershipWire {
	uint8_t initial, confirmed;
	XvtWireU16 reserved;
} XvtMembershipWire;

typedef char XvtStateHeader_layout[(sizeof(XvtStateHeader) == 8) ? 1 : -1];
typedef char XvtStateFooter_layout[(sizeof(XvtStateFooter) == 28) ? 1 : -1];
typedef char XvtReferenceMotionWire_layout[(sizeof(XvtReferenceMotionWire) == 28) ? 1 : -1];
typedef char XvtIntegrationWire_layout[(sizeof(XvtIntegrationWire) == 144) ? 1 : -1];
typedef char XvtPlayerTimingWire_layout[(sizeof(XvtPlayerTimingWire) == 116) ? 1 : -1];
typedef char XvtPairedMotionWire_layout[(sizeof(XvtPairedMotionWire) == 360) ? 1 : -1];
#endif
