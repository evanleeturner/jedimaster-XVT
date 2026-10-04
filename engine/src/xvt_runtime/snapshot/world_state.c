#include "xvt_runtime/snapshot/world_state.h"

#include <string.h>

#include "xvt/flight/flight.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/net/flight_sync.h"
#include "xvt/util/game_rand.h"
#include "xvt_runtime/runtime/flight_checkpoint.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/snapshot/records.h"
#include "xvt_runtime/snapshot/world_checksum.h"
#include "xvt_runtime/timing/flight_timing.h"

/* These trailer records already have their original fixed-width layout. */
typedef char xvt_snapshot_fg_size
	[(sizeof(struct mission_fg_runtime_stats) == 294) ? 1 : -1];
typedef char xvt_snapshot_guidance_size
	[(sizeof(struct warhead_guidance_state) == 10) ? 1 : -1];

static uint8_t *xvt_snapshot_save_objects(uint8_t *cursor)
{
	for (int i = 0; i < g_region_main_object_slot_end +
				    g_region_static_object_slot_count;
	     ++i) {
		if (i >= g_local_transient_slot_start &&
		    i < g_local_debris_slot_end) {
			continue;
		}
		const struct object_record *object = &g_object_table[i];
		*cursor++ = object->object_type;
		if (!object->object_type) {
			continue;
		}
		xvt_snapshot_encode_object_record(
			(struct xvt_snapshot_object_record *)cursor, object);
		cursor += sizeof(struct xvt_snapshot_object_record);
		const struct mobile_object *mobile = object->mobj;
		if (!mobile) {
			continue;
		}
		xvt_snapshot_encode_mobile_object(
			(struct xvt_snapshot_mobile_object *)cursor, mobile);
		cursor += sizeof(struct xvt_snapshot_mobile_object);
		if (mobile->p_craft) {
			xvt_snapshot_encode_craft_data(
				(struct xvt_snapshot_craft_data *)cursor,
				mobile->p_craft);
			cursor += sizeof(struct xvt_snapshot_craft_data);
		}
		if (mobile->p_warhead_guidance) {
			memcpy(cursor, mobile->p_warhead_guidance,
			       sizeof(struct warhead_guidance_state));
			cursor += sizeof(struct warhead_guidance_state);
		}
		if (mobile->p_char_data) {
			xvt_snapshot_encode_mobile_object_char_data(
				(struct xvt_snapshot_mobile_object_char_data *)
					cursor,
				mobile->p_char_data);
			cursor += sizeof(
				struct xvt_snapshot_mobile_object_char_data);
		}
	}
	return cursor;
}

static uint8_t *xvt_snapshot_restore_objects(uint8_t *cursor)
{
	for (int i = 0; i < g_region_main_object_slot_end +
				    g_region_static_object_slot_count;
	     ++i) {
		if (i >= g_local_transient_slot_start &&
		    i < g_local_debris_slot_end) {
			continue;
		}
		struct object_record *object = &g_object_table[i];
		object->object_type = *cursor++;
		if (!object->object_type) {
			/* The original clears only the scalar prefix, preserving pool ownership. */
			struct mobile_object *mobile = object->mobj;
			memset(object, 0, sizeof(*object));
			object->mobj = mobile;
			object->player_owner_idx = -1;
			if (mobile) {
				memset(mobile, 0,
				       offsetof(struct mobile_object,
						move_vector_dirty));
				mobile->iff = UINT8_MAX;
				if (mobile->p_craft) {
					struct xvt_snapshot_craft_data craft;
					xvt_snapshot_encode_craft_data(
						&craft, mobile->p_craft);
					memset(&craft, 0, 0x412);
					xvt_snapshot_decode_craft_data(
						mobile->p_craft, &craft);
				}
				if (mobile->p_warhead_guidance) {
					memset(mobile->p_warhead_guidance, 0,
					       sizeof(struct
						      warhead_guidance_state));
				}
				if (mobile->p_char_data) {
					memset(mobile->p_char_data, 0,
					       sizeof(struct
						      mobile_object_char_data));
				}
			}
			continue;
		}
		xvt_snapshot_decode_object_record(
			object,
			(const struct xvt_snapshot_object_record *)cursor);
		cursor += sizeof(struct xvt_snapshot_object_record);
		struct mobile_object *mobile = object->mobj;
		if (!mobile) {
			continue;
		}
		xvt_snapshot_decode_mobile_object(
			mobile,
			(const struct xvt_snapshot_mobile_object *)cursor);
		cursor += sizeof(struct xvt_snapshot_mobile_object);
		if (mobile->p_craft) {
			xvt_snapshot_decode_craft_data(
				mobile->p_craft,
				(const struct xvt_snapshot_craft_data *)cursor);
			cursor += sizeof(struct xvt_snapshot_craft_data);
		}
		if (mobile->p_warhead_guidance) {
			memcpy(mobile->p_warhead_guidance, cursor,
			       sizeof(struct warhead_guidance_state));
			cursor += sizeof(struct warhead_guidance_state);
		}
		if (mobile->p_char_data) {
			xvt_snapshot_decode_mobile_object_char_data(
				mobile->p_char_data,
				(const struct
				 xvt_snapshot_mobile_object_char_data *)cursor);
			cursor += sizeof(
				struct xvt_snapshot_mobile_object_char_data);
		}
	}
	return cursor;
}

size_t xvt_snapshot_encode(uint8_t *image, size_t capacity)
{
	if (!image || !xvt_snapshot_calculate_size() ||
	    capacity < xvt_snapshot_calculate_size()) {
		return 0;
	}
	uint8_t *cursor = xvt_snapshot_save_objects(image);
	memcpy(cursor, &g_mission_elapsed_clock,
	       sizeof(g_mission_elapsed_clock));
	cursor += sizeof(g_mission_elapsed_clock);
	memcpy(cursor, &g_mission_countdown_clock,
	       sizeof(g_mission_countdown_clock));
	cursor += sizeof(g_mission_countdown_clock);
	memcpy(cursor, &g_mission_header, 162);
	cursor += 162;
	memcpy(cursor, g_mission_fg_stats,
	       294 * (int16_t)g_mission_header.num_flight_groups);
	cursor += 294 * (int16_t)g_mission_header.num_flight_groups;
	memcpy(cursor, g_mission_flight_groups,
	       1382 * (int16_t)g_mission_header.num_flight_groups);
	cursor += 1382 * (int16_t)g_mission_header.num_flight_groups;
	xvt_snapshot_encode_flight_mission_state(
		(struct xvt_snapshot_flight_mission_state *)cursor,
		&g_flight_mission_state);
	cursor += 3376;
	memcpy(cursor, &g_flight_global_countdown_timers, 22);
	cursor += 22;
	memcpy(cursor, &g_mission_file_version, sizeof(g_mission_file_version));
	cursor += sizeof(g_mission_file_version);
	memcpy(cursor, &g_flight_player_count, sizeof(g_flight_player_count));
	cursor += sizeof(g_flight_player_count);
	*cursor++ = g_world_state_reserved_byte;

	memcpy(cursor, &g_craft_data_pool_capacity,
	       sizeof(g_craft_data_pool_capacity));
	cursor += sizeof(g_craft_data_pool_capacity);
	memcpy(cursor, &g_mobile_object_char_data_count,
	       sizeof(g_mobile_object_char_data_count));
	cursor += sizeof(g_mobile_object_char_data_count);
	memcpy(cursor, &g_projectile_object_slots_total,
	       sizeof(g_projectile_object_slots_total));
	cursor += sizeof(g_projectile_object_slots_total);
	memcpy(cursor, &g_debris_object_slots_total,
	       sizeof(g_debris_object_slots_total));
	cursor += sizeof(g_debris_object_slots_total);
	memcpy(cursor, &g_world_state_debris_slot_count,
	       sizeof(g_world_state_debris_slot_count));
	cursor += sizeof(g_world_state_debris_slot_count);
	memcpy(cursor, &g_local_debris_slot_count,
	       sizeof(g_local_debris_slot_count));
	cursor += sizeof(g_local_debris_slot_count);
	memcpy(cursor, &g_active_region_object_slot_start,
	       sizeof(g_active_region_object_slot_start));
	cursor += sizeof(g_active_region_object_slot_start);
	memcpy(cursor, &g_active_region_craft_object_slot_end,
	       sizeof(g_active_region_craft_object_slot_end));
	cursor += sizeof(g_active_region_craft_object_slot_end);
	memcpy(cursor, &g_mobile_object_char_data_slot_start,
	       sizeof(g_mobile_object_char_data_slot_start));
	cursor += sizeof(g_mobile_object_char_data_slot_start);
	memcpy(cursor, &g_mobile_object_char_data_slot_end,
	       sizeof(g_mobile_object_char_data_slot_end));
	cursor += sizeof(g_mobile_object_char_data_slot_end);
	memcpy(cursor, &g_projectile_object_slot_start,
	       sizeof(g_projectile_object_slot_start));
	cursor += sizeof(g_projectile_object_slot_start);
	memcpy(cursor, &g_projectile_object_slot_end,
	       sizeof(g_projectile_object_slot_end));
	cursor += sizeof(g_projectile_object_slot_end);
	memcpy(cursor, &g_debris_object_slot_start,
	       sizeof(g_debris_object_slot_start));
	cursor += sizeof(g_debris_object_slot_start);
	memcpy(cursor, &g_debris_object_slot_end,
	       sizeof(g_debris_object_slot_end));
	cursor += sizeof(g_debris_object_slot_end);
	memcpy(cursor, &g_explosion_object_slot_start,
	       sizeof(g_explosion_object_slot_start));
	cursor += sizeof(g_explosion_object_slot_start);
	memcpy(cursor, &g_explosion_object_slot_end,
	       sizeof(g_explosion_object_slot_end));
	cursor += sizeof(g_explosion_object_slot_end);
	memcpy(cursor, &g_local_transient_slot_start,
	       sizeof(g_local_transient_slot_start));
	cursor += sizeof(g_local_transient_slot_start);
	memcpy(cursor, &g_local_debris_slot_end,
	       sizeof(g_local_debris_slot_end));
	cursor += sizeof(g_local_debris_slot_end);
	memcpy(cursor, &g_region_main_object_slot_end,
	       sizeof(g_region_main_object_slot_end));
	cursor += sizeof(g_region_main_object_slot_end);
	memcpy(cursor, &g_region_static_object_slot_count,
	       sizeof(g_region_static_object_slot_count));
	cursor += sizeof(g_region_static_object_slot_count);
	memcpy(cursor, g_plan_table, 21760);
	cursor += 21760;
	memcpy(cursor, &g_plan_count, sizeof(g_plan_count));
	cursor += sizeof(g_plan_count);
	memcpy(cursor, &g_unused_world_state_serialized_dword,
	       sizeof(g_unused_world_state_serialized_dword));
	cursor += sizeof(g_unused_world_state_serialized_dword);
	memcpy(cursor, g_builtin_plan_id_by_name_index, 256);
	cursor += 256;
	memcpy(cursor, &g_game_rand_feedback_state,
	       sizeof(g_game_rand_feedback_state));
	cursor += sizeof(g_game_rand_feedback_state);
	memcpy(cursor, &g_next_object_signature,
	       sizeof(g_next_object_signature));
	cursor += sizeof(g_next_object_signature);
	memcpy(cursor, &g_laser_fire_timestamp_tracking_enabled,
	       sizeof(g_laser_fire_timestamp_tracking_enabled));
	cursor += sizeof(g_laser_fire_timestamp_tracking_enabled);
	for (int i = 0; i < 8; ++i) {
		xvt_snapshot_encode_player_data(
			(struct xvt_snapshot_player_data *)cursor,
			&g_players[i]);
		cursor += sizeof(struct xvt_snapshot_player_data);
	}
	return xvt_flight_timing_is_network125()
		       ? xvt_flight_checkpoint_append(image, cursor - image)
		       : (size_t)(cursor - image);
}

static void xvt_snapshot_decode_prefix(const uint8_t *image)
{
	uint8_t *cursor = xvt_snapshot_restore_objects((uint8_t *)image);
	memcpy(&g_mission_elapsed_clock, cursor,
	       sizeof(g_mission_elapsed_clock));
	cursor += sizeof(g_mission_elapsed_clock);
	memcpy(&g_mission_countdown_clock, cursor,
	       sizeof(g_mission_countdown_clock));
	cursor += sizeof(g_mission_countdown_clock);
	memcpy(&g_mission_header, cursor, sizeof(g_mission_header));
	cursor += sizeof(g_mission_header);
	memcpy(g_mission_fg_stats, cursor,
	       sizeof(*g_mission_fg_stats) *
		       g_mission_header.num_flight_groups);
	cursor += sizeof(*g_mission_fg_stats) *
		  g_mission_header.num_flight_groups;
	memcpy(g_mission_flight_groups, cursor,
	       sizeof(*g_mission_flight_groups) *
		       g_mission_header.num_flight_groups);
	cursor += sizeof(*g_mission_flight_groups) *
		  g_mission_header.num_flight_groups;
	xvt_snapshot_decode_flight_mission_state(
		&g_flight_mission_state,
		(const struct xvt_snapshot_flight_mission_state *)cursor);
	cursor += sizeof(struct xvt_snapshot_flight_mission_state);
	memcpy(&g_flight_global_countdown_timers, cursor,
	       sizeof(g_flight_global_countdown_timers));
	cursor += sizeof(g_flight_global_countdown_timers);
	memcpy(&g_mission_file_version, cursor, sizeof(g_mission_file_version));
	cursor += sizeof(g_mission_file_version);
	memcpy(&g_flight_player_count, cursor, sizeof(g_flight_player_count));
	cursor += sizeof(g_flight_player_count);
	g_world_state_reserved_byte = *cursor++;
	memcpy(&g_craft_data_pool_capacity, cursor,
	       sizeof(g_craft_data_pool_capacity));
	cursor += sizeof(g_craft_data_pool_capacity);
	memcpy(&g_mobile_object_char_data_count, cursor,
	       sizeof(g_mobile_object_char_data_count));
	cursor += sizeof(g_mobile_object_char_data_count);
	memcpy(&g_projectile_object_slots_total, cursor,
	       sizeof(g_projectile_object_slots_total));
	cursor += sizeof(g_projectile_object_slots_total);
	memcpy(&g_debris_object_slots_total, cursor,
	       sizeof(g_debris_object_slots_total));
	cursor += sizeof(g_debris_object_slots_total);
	memcpy(&g_world_state_debris_slot_count, cursor,
	       sizeof(g_world_state_debris_slot_count));
	cursor += sizeof(g_world_state_debris_slot_count);
	memcpy(&g_local_debris_slot_count, cursor,
	       sizeof(g_local_debris_slot_count));
	cursor += sizeof(g_local_debris_slot_count);
	memcpy(&g_active_region_object_slot_start, cursor,
	       sizeof(g_active_region_object_slot_start));
	cursor += sizeof(g_active_region_object_slot_start);
	memcpy(&g_active_region_craft_object_slot_end, cursor,
	       sizeof(g_active_region_craft_object_slot_end));
	cursor += sizeof(g_active_region_craft_object_slot_end);
	memcpy(&g_mobile_object_char_data_slot_start, cursor,
	       sizeof(g_mobile_object_char_data_slot_start));
	cursor += sizeof(g_mobile_object_char_data_slot_start);
	memcpy(&g_mobile_object_char_data_slot_end, cursor,
	       sizeof(g_mobile_object_char_data_slot_end));
	cursor += sizeof(g_mobile_object_char_data_slot_end);
	memcpy(&g_projectile_object_slot_start, cursor,
	       sizeof(g_projectile_object_slot_start));
	cursor += sizeof(g_projectile_object_slot_start);
	memcpy(&g_projectile_object_slot_end, cursor,
	       sizeof(g_projectile_object_slot_end));
	cursor += sizeof(g_projectile_object_slot_end);
	memcpy(&g_debris_object_slot_start, cursor,
	       sizeof(g_debris_object_slot_start));
	cursor += sizeof(g_debris_object_slot_start);
	memcpy(&g_debris_object_slot_end, cursor,
	       sizeof(g_debris_object_slot_end));
	cursor += sizeof(g_debris_object_slot_end);
	memcpy(&g_explosion_object_slot_start, cursor,
	       sizeof(g_explosion_object_slot_start));
	cursor += sizeof(g_explosion_object_slot_start);
	memcpy(&g_explosion_object_slot_end, cursor,
	       sizeof(g_explosion_object_slot_end));
	cursor += sizeof(g_explosion_object_slot_end);
	memcpy(&g_local_transient_slot_start, cursor,
	       sizeof(g_local_transient_slot_start));
	cursor += sizeof(g_local_transient_slot_start);
	memcpy(&g_local_debris_slot_end, cursor,
	       sizeof(g_local_debris_slot_end));
	cursor += sizeof(g_local_debris_slot_end);
	memcpy(&g_region_main_object_slot_end, cursor,
	       sizeof(g_region_main_object_slot_end));
	cursor += sizeof(g_region_main_object_slot_end);
	memcpy(&g_region_static_object_slot_count, cursor,
	       sizeof(g_region_static_object_slot_count));
	cursor += sizeof(g_region_static_object_slot_count);
	memcpy(g_plan_table, cursor, sizeof(g_plan_table));
	cursor += sizeof(g_plan_table);
	memcpy(&g_plan_count, cursor, sizeof(g_plan_count));
	cursor += sizeof(g_plan_count);
	memcpy(&g_unused_world_state_serialized_dword, cursor,
	       sizeof(g_unused_world_state_serialized_dword));
	cursor += sizeof(g_unused_world_state_serialized_dword);
	memcpy(g_builtin_plan_id_by_name_index, cursor,
	       sizeof(g_builtin_plan_id_by_name_index));
	cursor += sizeof(g_builtin_plan_id_by_name_index);
	memcpy(&g_game_rand_feedback_state, cursor,
	       sizeof(g_game_rand_feedback_state));
	cursor += sizeof(g_game_rand_feedback_state);
	memcpy(&g_next_object_signature, cursor,
	       sizeof(g_next_object_signature));
	cursor += sizeof(g_next_object_signature);
	memcpy(&g_laser_fire_timestamp_tracking_enabled, cursor,
	       sizeof(g_laser_fire_timestamp_tracking_enabled));
	cursor += sizeof(g_laser_fire_timestamp_tracking_enabled);
	for (int i = 0; i < 8; ++i) {
		xvt_snapshot_decode_player_data(
			&g_players[i],
			(const struct xvt_snapshot_player_data *)cursor);
		cursor += sizeof(struct xvt_snapshot_player_data);
	}
}

enum flight_world_state_presence_flags {
	FLIGHT_WORLDSTATE_HAS_OBJECT = 0x01,
	FLIGHT_WORLDSTATE_HAS_MOBILE = 0x02,
	FLIGHT_WORLDSTATE_HAS_CRAFT = 0x04,
	FLIGHT_WORLDSTATE_HAS_WARHEAD_GUIDANCE = 0x08,
	FLIGHT_WORLDSTATE_HAS_CHAR_DATA = 0x10,
	FLIGHT_WORLDSTATE_EMPTY_RUN_FLAG = 0x80,
	FLIGHT_WORLDSTATE_EMPTY_RUN_LENGTH_MASK = 0x7F,
	FLIGHT_WORLDSTATE_MAX_EMPTY_RUN = 0x7E
};

size_t xvt_snapshot_calculate_size(void)
{
	if (g_region_main_object_slot_end < 0 ||
	    g_region_static_object_slot_count < 0 ||
	    (size_t)g_region_main_object_slot_end +
			    g_region_static_object_slot_count >
		    UINT16_MAX ||
	    (unsigned)g_mission_header.num_flight_groups > INT16_MAX ||
	    (unsigned)g_craft_data_pool_capacity > UINT16_MAX ||
	    (unsigned)g_mobile_object_char_data_count > UINT16_MAX ||
	    (unsigned)g_projectile_object_slots_total > UINT16_MAX) {
		return 0;
	}

	/* The fixed trailer contains 24 dwords, three words, and one byte
	 * around the fixed arrays. */
	size_t size =
		(int)(2 * sizeof(struct mission_clock) +
		      sizeof(struct mission_header) +
		      sizeof(struct xvt_snapshot_flight_mission_state) +
		      sizeof(struct flight_global_countdown_timers) +
		      sizeof(g_plan_table) +
		      sizeof(g_builtin_plan_id_by_name_index) +
		      8 * sizeof(struct xvt_snapshot_player_data) +
		      24 * sizeof(uint32_t) + 3 * sizeof(uint16_t) +
		      sizeof(uint8_t) + sizeof(struct warhead_guidance_state));
	size += (int)(sizeof(struct mission_fg_runtime_stats) +
		      sizeof(struct mission_flight_group)) *
		g_mission_header.num_flight_groups;
	size += (int)(sizeof(uint8_t) +
		      sizeof(struct xvt_snapshot_object_record)) *
		g_region_static_object_slot_count;
	size += (int)(sizeof(uint8_t) +
		      sizeof(struct xvt_snapshot_object_record) +
		      sizeof(struct xvt_snapshot_mobile_object)) *
		g_region_main_object_slot_end;
	size += (int)sizeof(struct xvt_snapshot_mobile_object_char_data) *
		(int)g_mobile_object_char_data_count;
	size += (int)sizeof(struct warhead_guidance_state) *
		(int)g_projectile_object_slots_total;
	size += (int)sizeof(struct xvt_snapshot_craft_data) *
		g_craft_data_pool_capacity;
	return (size_t)size + (xvt_flight_timing_is_network125()
				       ? xvt_flight_checkpoint_maximum()
				       : 0);
}

int xvt_snapshot_build_presence_map(uint8_t *out_map,
				    const uint8_t *world_state)
{
	uint8_t *map_start = out_map;
	{
		int object_count = g_region_static_object_slot_count +
				   g_region_main_object_slot_end;
		memcpy(out_map, &object_count, sizeof(object_count));
	}
	out_map += sizeof(int);
	int empty_run_length = 0;
	int object_index = 0;
	while (object_index < g_region_static_object_slot_count +
				      g_region_main_object_slot_end) {
		if (object_index < g_local_transient_slot_start ||
		    object_index >= g_local_debris_slot_end) {
			uint8_t component_flags = 0;
			if (*world_state++ != 0) {
				component_flags = FLIGHT_WORLDSTATE_HAS_OBJECT;
				const struct xvt_snapshot_object_record
					*object_state =
						(const struct
						 xvt_snapshot_object_record *)
							world_state;
				world_state += sizeof(*object_state);
				uint32_t mobile_object_present;
				memcpy(&mobile_object_present,
				       &object_state->mobj,
				       sizeof(mobile_object_present));
				if (mobile_object_present != 0) {
					component_flags |=
						FLIGHT_WORLDSTATE_HAS_MOBILE;
					const struct xvt_snapshot_mobile_object
						*mobile_object_state =
							(const struct
							 xvt_snapshot_mobile_object
								 *)world_state;
					world_state +=
						sizeof(*mobile_object_state);
					uint32_t craft_present;
					memcpy(&craft_present,
					       &mobile_object_state->p_craft,
					       sizeof(craft_present));
					if (craft_present != 0) {
						component_flags |=
							FLIGHT_WORLDSTATE_HAS_CRAFT;
						world_state += sizeof(
							struct
							xvt_snapshot_craft_data);
					}
					uint32_t warhead_guidance_present;
					memcpy(&warhead_guidance_present,
					       &mobile_object_state
							->p_warhead_guidance,
					       sizeof(warhead_guidance_present));
					if (warhead_guidance_present != 0) {
						component_flags |=
							FLIGHT_WORLDSTATE_HAS_WARHEAD_GUIDANCE;
						world_state += sizeof(
							struct
							warhead_guidance_state);
					}
					uint32_t char_data_present;
					memcpy(&char_data_present,
					       &mobile_object_state
							->p_char_data,
					       sizeof(char_data_present));
					if (char_data_present != 0) {
						component_flags |=
							FLIGHT_WORLDSTATE_HAS_CHAR_DATA;
						world_state += sizeof(
							struct
							xvt_snapshot_mobile_object_char_data);
					}
				}
			}

			if (component_flags == 0) {
				++empty_run_length;
				if (empty_run_length >=
				    FLIGHT_WORLDSTATE_MAX_EMPTY_RUN) {
					*out_map++ =
						(uint8_t)(empty_run_length |
							  FLIGHT_WORLDSTATE_EMPTY_RUN_FLAG);
					empty_run_length = 0;
				}
			} else {
				if (empty_run_length != 0) {
					*out_map++ =
						(uint8_t)(empty_run_length |
							  FLIGHT_WORLDSTATE_EMPTY_RUN_FLAG);
					empty_run_length = 0;
				}
				*out_map++ = component_flags;
			}
		}
		++object_index;
	}

	if (empty_run_length != 0) {
		*out_map++ = (uint8_t)(empty_run_length |
				       FLIGHT_WORLDSTATE_EMPTY_RUN_FLAG);
	}
	return (int)(out_map - map_start);
}

/* Makes one optional block at *cursor match the host's presence bit. Present on both sides: steps
 * over it and returns 1, so the caller can go on to the blocks nested under it. Present here only:
 * removes it, closing the gap, and leaves *cursor where it began. Present at the host only: inserts
 * it zero-filled and steps past it. *end, the end of the image's bytes, moves with every change.
 * Returns 0 unless both sides have the block. */
static int xvt_snapshot_match_block(uint8_t **cursor, uint8_t **end,
				    size_t size, int here, int host)
{
	if (here && host) {
		*cursor += size;
		return 1;
	}
	uint8_t *block_start;
	if (here) {
		block_start = *cursor;
		*cursor += size;
		memmove(block_start, *cursor, (size_t)(*end - *cursor));
		*cursor = block_start;
		*end -= size;
	} else if (host) {
		block_start = *cursor;
		*cursor += size;
		memmove(*cursor, block_start, (size_t)(*end - block_start));
		memset(block_start, 0, (size_t)(*cursor - block_start));
		*end += size;
	}
	return 0;
}

void xvt_snapshot_apply_presence_map(const uint8_t *presence_map)
{
	uint8_t *cursor = g_world_state_dup_buffer;
	uint8_t *end = &g_world_state_dup_buffer[g_world_state_dup_size];
	int map_slot_limit;
	memcpy(&map_slot_limit, presence_map, sizeof(map_slot_limit));
	presence_map += sizeof(map_slot_limit);
	int empty_run_remaining = 0;
	int object_index = 0;
	while (object_index < g_region_static_object_slot_count +
				      g_region_main_object_slot_end) {
		if (g_local_transient_slot_start > object_index ||
		    g_local_debris_slot_end <= object_index) {
			int8_t presence;

			if (empty_run_remaining != 0) {
				presence = 0;
				--empty_run_remaining;
			} else {
				if (map_slot_limit > object_index) {
					presence = (int8_t)*presence_map++;
				} else {
					break;
				}
				if (presence < 0) {
					uint16_t empty_run_length =
						presence &
						FLIGHT_WORLDSTATE_EMPTY_RUN_LENGTH_MASK;
					presence = 0;
					empty_run_remaining =
						empty_run_length - 1;
				}
			}

			int8_t object_type = *cursor++;
			if (xvt_snapshot_match_block(
				    &cursor, &end,
				    sizeof(struct xvt_snapshot_object_record),
				    object_type != 0,
				    (presence & FLIGHT_WORLDSTATE_HAS_OBJECT) !=
					    0)) {
				const struct xvt_snapshot_object_record
					*object_state;

				object_state =
					(const struct xvt_snapshot_object_record
						 *)(cursor -
						    sizeof(*object_state));
				uint32_t mobile_present;
				memcpy(&mobile_present, &object_state->mobj,
				       sizeof(mobile_present));
				if (xvt_snapshot_match_block(
					    &cursor, &end,
					    sizeof(struct
						   xvt_snapshot_mobile_object),
					    mobile_present != 0,
					    (presence &
					     FLIGHT_WORLDSTATE_HAS_MOBILE) !=
						    0)) {
					const struct xvt_snapshot_mobile_object
						*mobile_state;

					mobile_state =
						(const struct
						 xvt_snapshot_mobile_object
							 *)(cursor -
							    sizeof(*mobile_state));
					uint32_t craft_present;
					memcpy(&craft_present,
					       &mobile_state->p_craft,
					       sizeof(craft_present));
					xvt_snapshot_match_block(
						&cursor, &end,
						sizeof(struct
						       xvt_snapshot_craft_data),
						craft_present != 0,
						(presence &
						 FLIGHT_WORLDSTATE_HAS_CRAFT) !=
							0);
					uint32_t warhead_guidance_present;
					memcpy(&warhead_guidance_present,
					       &mobile_state
							->p_warhead_guidance,
					       sizeof(warhead_guidance_present));
					xvt_snapshot_match_block(
						&cursor, &end,
						sizeof(struct
						       warhead_guidance_state),
						warhead_guidance_present != 0,
						(presence &
						 FLIGHT_WORLDSTATE_HAS_WARHEAD_GUIDANCE) !=
							0);
					uint32_t char_data_present;
					memcpy(&char_data_present,
					       &mobile_state->p_char_data,
					       sizeof(char_data_present));
					xvt_snapshot_match_block(
						&cursor, &end,
						sizeof(struct
						       xvt_snapshot_mobile_object_char_data),
						char_data_present != 0,
						(presence &
						 FLIGHT_WORLDSTATE_HAS_CHAR_DATA) !=
							0);
				}
			}
		}
		++object_index;
	}

	g_world_state_dup_size = (int)(end - g_world_state_dup_buffer);
}

static int xvt_snapshot_is_pool_link(uint32_t value, size_t stride,
				     unsigned count)
{
	return !value ||
	       ((value - 1) % stride == 0 && (value - 1) / stride < count);
}

struct xvt_snapshot_world_ranges {
	int32_t craft_capacity;
	int32_t character_count;
	int32_t projectile_count;
	int32_t debris_count;
	int32_t debris_slot_count;
	int32_t local_debris_slot_count;
	int32_t active_start;
	int32_t craft_end;
	int32_t character_start;
	int32_t character_end;
	int32_t projectile_start;
	int32_t projectile_end;
	int32_t debris_start;
	int32_t debris_end;
	int32_t explosion_start;
	int32_t explosion_end;
	int32_t local_start;
	int32_t local_end;
	int32_t main_end;
	int32_t static_count;
};

/* Returns 1 when the 20 dwords at image_ranges (pool sizes, the world-state
 * debris slot count and the slot-range bounds) equal this flight's live values,
 * else 0. */
static int xvt_snapshot_ranges_match_live(const uint8_t *image_ranges)
{
	struct xvt_snapshot_world_ranges ranges;
	memcpy(&ranges, image_ranges, sizeof ranges);
	const struct xvt_snapshot_world_ranges expected = {
		.craft_capacity = g_craft_data_pool_capacity,
		.character_count = g_mobile_object_char_data_count,
		.projectile_count = g_projectile_object_slots_total,
		.debris_count = g_debris_object_slots_total,
		.debris_slot_count = g_world_state_debris_slot_count,
		.local_debris_slot_count = g_local_debris_slot_count,
		.active_start = g_active_region_object_slot_start,
		.craft_end = g_active_region_craft_object_slot_end,
		.character_start = g_mobile_object_char_data_slot_start,
		.character_end = g_mobile_object_char_data_slot_end,
		.projectile_start = g_projectile_object_slot_start,
		.projectile_end = g_projectile_object_slot_end,
		.debris_start = g_debris_object_slot_start,
		.debris_end = g_debris_object_slot_end,
		.explosion_start = g_explosion_object_slot_start,
		.explosion_end = g_explosion_object_slot_end,
		.local_start = g_local_transient_slot_start,
		.local_end = g_local_debris_slot_end,
		.main_end = g_region_main_object_slot_end,
		.static_count = g_region_static_object_slot_count};
	return memcmp(&ranges, &expected, sizeof ranges) == 0;
}

/* Walks the image once, in its layout order: the player records at its end,
 * then each object slot (against the checkpoint's timing rows in the network
 * profile), then the fixed tables. Each check reads the cursor and the bytes
 * left where the previous check stopped, so the walk stays in one place. */
static int
xvt_snapshot_validate_prefix(const uint8_t *image, size_t size,
			     const struct xvt_flight_checkpoint_view *timing)
{
	int network = timing != NULL;
	struct xvt_player_timing_wire player_timing[XVT_FLIGHT_PLAYERS];
	if (network) {
		memcpy(player_timing, timing->players, sizeof player_timing);
	}
	if (size <
	    XVT_FLIGHT_PLAYERS * sizeof(struct xvt_snapshot_player_data)) {
		return 0;
	}
	const uint8_t *players =
		image + size -
		XVT_FLIGHT_PLAYERS * sizeof(struct xvt_snapshot_player_data);
	for (unsigned i = 0; i < XVT_FLIGHT_PLAYERS; ++i) {
		int slot = (int32_t)xvt_wire_get32(
			players + i * sizeof(struct xvt_snapshot_player_data) +
			offsetof(struct xvt_snapshot_player_data,
				 object_index));
		if (slot < -1 || slot >= g_region_main_object_slot_end) {
			return 0;
		}
		if (network) {
			const struct xvt_player_timing_wire *state =
				&player_timing[i];
			if (state->valid &&
			    xvt_wire_get16(state->slot) != (unsigned)slot) {
				return 0;
			}
		}
	}
	const uint8_t *cursor = image;
	size_t left = size;
	unsigned row = 0;
	for (int slot = 0; slot < g_region_main_object_slot_end +
					  g_region_static_object_slot_count;
	     ++slot) {
		if (slot >= g_local_transient_slot_start &&
		    slot < g_local_debris_slot_end) {
			continue;
		}
		if (!left) {
			return 0;
		}
		uint8_t type = *cursor++;
		--left;
		struct xvt_reference_motion_wire reference = {0};
		struct xvt_integration_wire integration = {0};
		if (network) {
			memcpy(&reference,
			       timing->reference + row * sizeof reference,
			       sizeof reference);
			memcpy(&integration,
			       timing->integration + row * sizeof integration,
			       sizeof integration);
		}
		++row;
		if (!type) {
			if (network && (reference.type || integration.type)) {
				return 0;
			}
			continue;
		}
		if (left < sizeof(struct xvt_snapshot_object_record)) {
			return 0;
		}
		struct xvt_snapshot_object_record object;
		memcpy(&object, cursor, sizeof object);
		cursor += sizeof object;
		left -= sizeof object;
		if (network) {
			if ((reference.type &&
			     (reference.type != type ||
			      xvt_wire_get16(reference.signature) !=
				      object.object_signature)) ||
			    (integration.type &&
			     (integration.type != type ||
			      xvt_wire_get16(integration.signature) !=
				      object.object_signature))) {
				return 0;
			}
			for (unsigned i = 0; i < XVT_FLIGHT_PLAYERS; ++i) {
				const struct xvt_player_timing_wire *state =
					&player_timing[i];
				if (state->valid &&
				    xvt_wire_get16(state->slot) ==
					    (unsigned)slot &&
				    xvt_wire_get16(state->signature) !=
					    object.object_signature) {
					return 0;
				}
			}
		}
		if (object.object_type != type ||
		    !xvt_snapshot_is_pool_link(
			    object.mobj,
			    sizeof(struct xvt_snapshot_mobile_object),
			    g_region_main_object_slot_end)) {
			return 0;
		}
		if (!object.mobj) {
			continue;
		}
		if (left < sizeof(struct xvt_snapshot_mobile_object)) {
			return 0;
		}
		struct xvt_snapshot_mobile_object mobile;
		memcpy(&mobile, cursor, sizeof mobile);
		cursor += sizeof mobile;
		left -= sizeof mobile;
		if (network && integration.type &&
		    integration.family != mobile.family) {
			return 0;
		}
		if (!xvt_snapshot_is_pool_link(
			    mobile.p_craft,
			    sizeof(struct xvt_snapshot_craft_data),
			    g_craft_data_pool_capacity) ||
		    !xvt_snapshot_is_pool_link(
			    mobile.p_warhead_guidance,
			    sizeof(struct warhead_guidance_state),
			    g_projectile_object_slots_total) ||
		    !xvt_snapshot_is_pool_link(
			    mobile.p_char_data,
			    sizeof(struct xvt_snapshot_mobile_object_char_data),
			    g_mobile_object_char_data_count)) {
			return 0;
		}
		size_t extra =
			(mobile.p_craft ? sizeof(struct xvt_snapshot_craft_data)
					: 0) +
			(mobile.p_warhead_guidance
				 ? sizeof(struct warhead_guidance_state)
				 : 0) +
			(mobile.p_char_data
				 ? sizeof(struct
					  xvt_snapshot_mobile_object_char_data)
				 : 0);
		if (left < extra) {
			return 0;
		}
		cursor += extra;
		left -= extra;
	}

	size_t clocks_and_header = sizeof(g_mission_elapsed_clock) +
				   sizeof(g_mission_countdown_clock) +
				   sizeof(struct mission_header);
	if (left < clocks_and_header) {
		return 0;
	}
	struct mission_header header;
	memcpy(&header,
	       cursor + sizeof(g_mission_elapsed_clock) +
		       sizeof(g_mission_countdown_clock),
	       sizeof header);
	if (header.num_flight_groups != g_mission_header.num_flight_groups) {
		return 0;
	}
	size_t groups = (sizeof(struct mission_fg_runtime_stats) +
			 sizeof(struct mission_flight_group)) *
			header.num_flight_groups;
	size_t before_ranges =
		clocks_and_header + groups +
		sizeof(struct xvt_snapshot_flight_mission_state) +
		sizeof(g_flight_global_countdown_timers) +
		sizeof(g_mission_file_version) + sizeof(g_flight_player_count) +
		sizeof(g_world_state_reserved_byte);
	size_t after_ranges =
		sizeof(g_plan_table) + sizeof(g_plan_count) +
		sizeof(g_unused_world_state_serialized_dword) +
		sizeof(g_builtin_plan_id_by_name_index) +
		sizeof(g_game_rand_feedback_state) +
		sizeof(g_next_object_signature) +
		sizeof(g_laser_fire_timestamp_tracking_enabled) +
		XVT_FLIGHT_PLAYERS * sizeof(struct xvt_snapshot_player_data);
	if (left != before_ranges + sizeof(struct xvt_snapshot_world_ranges) +
			    after_ranges) {
		return 0;
	}
	return xvt_snapshot_ranges_match_live(cursor + before_ranges);
}

static int xvt_snapshot_read_image(const uint8_t *image, size_t size,
				   struct xvt_flight_checkpoint_view *timing)
{
	if (!image || size > xvt_snapshot_calculate_size()) {
		return 0;
	}
	memset(timing, 0, sizeof *timing);
	timing->prefix = size;
	if (xvt_flight_timing_is_network125() &&
	    !xvt_flight_checkpoint_read(image, size, timing)) {
		return 0;
	}
	return xvt_snapshot_validate_prefix(
		image, timing->prefix,
		xvt_flight_timing_is_network125() ? timing : NULL);
}

int xvt_snapshot_validate(const uint8_t *image, size_t size)
{
	struct xvt_flight_checkpoint_view timing;
	return xvt_snapshot_read_image(image, size, &timing);
}

int xvt_snapshot_decode(const uint8_t *image, size_t size)
{
	struct xvt_flight_checkpoint_view timing;
	if (!xvt_snapshot_read_image(image, size, &timing)) {
		return 0;
	}
	xvt_snapshot_decode_prefix(image);
	if (xvt_flight_timing_is_network125()) {
		xvt_flight_checkpoint_restore(&timing);
	}
	return 1;
}

void xvt_snapshot_save(void)
{
	g_world_state_size = (unsigned)xvt_snapshot_encode(
		g_world_state_buffer, xvt_snapshot_calculate_size());
}

void xvt_snapshot_restore(void)
{
	if (!xvt_snapshot_decode(g_world_state_buffer, g_world_state_size)) {
		g_flight_mission_state.mission_end_pending = 1;
	}
}

int xvt_snapshot_checksum_image(const uint8_t *image, size_t size,
				unsigned checksums[XVT_WORLD_CHECKSUM_REGIONS],
				unsigned lengths[XVT_WORLD_CHECKSUM_REGIONS])
{
	struct xvt_flight_checkpoint_view timing;
	if (!xvt_snapshot_read_image(image, size, &timing)) {
		return 0;
	}
	xvt_snapshot_checksum_prefix(image, timing.prefix, checksums, lengths);
	if (xvt_flight_timing_is_network125()) {
		checksums[XVT_TIMING_CHECKSUM_REGION] = xvt_flight_wire_crc32c(
			image + timing.prefix, size - timing.prefix);
		lengths[XVT_TIMING_CHECKSUM_REGION] =
			(unsigned)(size - timing.prefix);
	}
	return 1;
}

void xvt_snapshot_checksum(int unused_arg0, int unused_arg1)
{
	(void)unused_arg0;
	(void)unused_arg1;
	if (!xvt_snapshot_checksum_image(g_world_state_buffer,
					 g_world_state_size, g_world_checksum,
					 g_world_checksum_region_lengths)) {
		g_flight_mission_state.mission_end_pending = 1;
	}
}
