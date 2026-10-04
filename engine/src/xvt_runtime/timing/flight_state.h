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
	XVT_CONTROL_MASK = XVT_CONTROL_ROLL | XVT_CONTROL_DISABLED |
			   XVT_CONTROL_BLOCKED | XVT_CONTROL_MAP |
			   XVT_CONTROL_HYPERSPACE
};

struct xvt_state_header {
	xvt_wire_u16 schema;
	xvt_wire_u16 reference_count;
	xvt_wire_u16 integration_count;
	xvt_wire_u16 player_count;
};

struct xvt_state_footer {
	xvt_wire_u32 magic;
	xvt_wire_u16 schema;
	xvt_wire_u16 profile;
	xvt_wire_u32 cookie;
	xvt_wire_u32 completed_tick;
	xvt_wire_u32 world_bytes;
	xvt_wire_u32 timing_bytes;
	xvt_wire_u32 timing_crc;
};

struct xvt_reference_motion_wire {
	xvt_wire_u16 slot;
	xvt_wire_u16 signature;
	uint8_t type;
	uint8_t flags;
	xvt_wire_u16 reserved;
	xvt_wire_u32 position[XVT_STATE_POSITION_AXES];
	xvt_wire_u32 sample_tick;
	xvt_wire_u32 current_tick;
};

struct xvt_integration_wire {
	xvt_wire_u16 slot;
	xvt_wire_u16 signature;
	uint8_t type;
	uint8_t family;
	xvt_wire_u16 carried_slot;
	xvt_wire_u16 target_slot;
	xvt_wire_u16 target_signature;
	xvt_wire_u64 position_remainder[XVT_STATE_POSITION_AXES];
	xvt_wire_u64 remainder[XVT_STATE_INTEGRATION_CHANNELS];
	int8_t direction[XVT_STATE_INTEGRATION_CHANNELS];
};

struct xvt_player_timing_wire {
	uint8_t player;
	uint8_t valid;
	xvt_wire_u16 slot;
	xvt_wire_u16 signature;
	xvt_wire_u16 reserved;
	xvt_wire_u64 remainder[XVT_STATE_PLAYER_CHANNELS];
	int8_t direction[XVT_STATE_PLAYER_CHANNELS];
	uint8_t lock_mode;
	uint8_t lock_odd_tick;
	uint8_t control_valid;
	xvt_wire_u32 control_mode;
	xvt_wire_u16 lock_signature;
	xvt_wire_u16 lock_target;
	xvt_wire_u16 lock_target_signature;
	xvt_wire_u16 lock_weapon;
	xvt_wire_u64 lock_serial;
	xvt_wire_u16 camera_focus;
	xvt_wire_u16 reserved_tail;
};

struct xvt_object_identity_wire {
	xvt_wire_u16 slot;
	xvt_wire_u16 signature;
};

struct xvt_object_motion_wire {
	struct xvt_reference_motion_wire reference;
	struct xvt_integration_wire integration;
};

struct xvt_paired_motion_wire {
	uint8_t player;
	uint8_t validity;
	xvt_wire_u16 reserved;
	xvt_wire_u32 saved_tick;
	struct xvt_object_identity_wire owner_id;
	struct xvt_object_identity_wire carried_id;
	struct xvt_object_motion_wire owner;
	struct xvt_object_motion_wire carried;
};

struct xvt_membership_wire {
	uint8_t initial;
	uint8_t confirmed;
	xvt_wire_u16 reserved;
};

typedef char xvt_state_header_layout[(sizeof(struct xvt_state_header) == 8)
					     ? 1
					     : -1];
typedef char xvt_state_footer_layout[(sizeof(struct xvt_state_footer) == 28)
					     ? 1
					     : -1];
typedef char xvt_reference_motion_wire_layout
	[(sizeof(struct xvt_reference_motion_wire) == 28) ? 1 : -1];
typedef char xvt_integration_wire_layout
	[(sizeof(struct xvt_integration_wire) == 144) ? 1 : -1];
typedef char xvt_player_timing_wire_layout
	[(sizeof(struct xvt_player_timing_wire) == 116) ? 1 : -1];
typedef char xvt_paired_motion_wire_layout
	[(sizeof(struct xvt_paired_motion_wire) == 360) ? 1 : -1];
#endif
