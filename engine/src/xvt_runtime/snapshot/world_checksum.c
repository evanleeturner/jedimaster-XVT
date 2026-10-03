/* World-state checksums: the regional byte sums of a world image and the
 * live checksum of the running world, both of which peers compare to find a
 * world that has drifted. world_state.c writes, reads and checks the image
 * these sums are taken over. */
#include "xvt_runtime/snapshot/world_checksum.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/hud/hud.h"
#include "xvt/flight/mission/mission.h"
#include "xvt/net/flight_sync.h"
#include "xvt/util/game_rand.h"
#include "xvt_runtime/runtime/flight_checkpoint.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/snapshot/records.h"
#include "xvt_runtime/snapshot/world_state.h"
#include "xvt_runtime/timing/flight_timing.h"
#include <string.h>

static unsigned int xvt_snapshot_sum_bytes(uint8_t **cursor, int count)
{
	unsigned int sum = 0;
	for (int i = 0; i < count; ++i) {
		sum += *(*cursor)++;
	}
	return sum;
}

/* Sums the bytes of one present slot's records at *cursor: the object, then its mobile, craft,
 * warhead-guidance and character records when present, leaving out the links and cached motion that
 * xvt_snapshot_checksum_image's comment lists. Moves *cursor past all of them; returns the sum. */
static unsigned int xvt_snapshot_sum_slot_records(uint8_t **cursor)
{
	unsigned int sum = 0;
	int bytes_remaining;

	struct xvt_snapshot_object_record *object_state;
	int object_data_bytes;
	uint32_t mobile_present;

	object_state = (struct xvt_snapshot_object_record *)*cursor;
	object_data_bytes = sizeof(*object_state) - sizeof(object_state->mobj);
	do {
		sum += *(*cursor)++;
	} while (--object_data_bytes != 0);
	*cursor = (uint8_t *)(object_state + 1);
	memcpy(&mobile_present, &object_state->mobj, sizeof(mobile_present));
	if (mobile_present != 0) {
		struct xvt_snapshot_mobile_object *mobile_state;
		int mobile_data_bytes;
		uint32_t craft_present;
		uint32_t guidance_present;
		uint32_t char_data_present;

		mobile_state = (struct xvt_snapshot_mobile_object *)*cursor;
		mobile_data_bytes = sizeof(*mobile_state) -
				    sizeof(mobile_state->move_vector_dirty) -
				    sizeof(mobile_state->move_x) -
				    sizeof(mobile_state->move_y) -
				    sizeof(mobile_state->move_z) -
				    sizeof(mobile_state->orient_matrix_dirty) -
				    sizeof(mobile_state->cached_fwd_x) -
				    sizeof(mobile_state->cached_fwd_y) -
				    sizeof(mobile_state->cached_fwd_z) -
				    sizeof(mobile_state->cached_side_x) -
				    sizeof(mobile_state->cached_side_y) -
				    sizeof(mobile_state->cached_side_z) -
				    sizeof(mobile_state->cached_up_x) -
				    sizeof(mobile_state->cached_up_y) -
				    sizeof(mobile_state->cached_up_z) -
				    sizeof(mobile_state->p_warhead_guidance) -
				    sizeof(mobile_state->p_craft) -
				    sizeof(mobile_state->p_char_data);
		do {
			sum += *(*cursor)++;
		} while (--mobile_data_bytes != 0);
		*cursor = (uint8_t *)(mobile_state + 1);
		memcpy(&craft_present, &mobile_state->p_craft,
		       sizeof(craft_present));
		if (craft_present != 0) {
			struct xvt_snapshot_craft_data *craft_state;
			int craft_data_bytes;

			craft_state = (struct xvt_snapshot_craft_data *)*cursor;
			craft_data_bytes =
				sizeof(*craft_state) -
				sizeof(craft_state->unused3f2) -
				sizeof(craft_state->turret_object_links) -
				sizeof(craft_state->effective_ai_object_link) +
				32;
			do {
				sum += *(*cursor)++;
			} while (--craft_data_bytes != 0);
			*cursor = (uint8_t *)(craft_state + 1);
		}
		memcpy(&guidance_present, &mobile_state->p_warhead_guidance,
		       sizeof(guidance_present));
		if (guidance_present != 0) {
			bytes_remaining = sizeof(struct warhead_guidance_state);
			do {
				sum += *(*cursor)++;
			} while (--bytes_remaining != 0);
		}
		memcpy(&char_data_present, &mobile_state->p_char_data,
		       sizeof(char_data_present));
		if (char_data_present != 0) {
			bytes_remaining = sizeof(
				struct xvt_snapshot_mobile_object_char_data);
			do {
				sum += *(*cursor)++;
			} while (--bytes_remaining != 0);
		}
	}
	return sum;
}

/* Closes the current checksum region once it has grown past the target size
 * while regions remain: records its length and sum, then starts the next
 * region at the cursor with a zero sum. */
static void xvt_snapshot_close_checksum_region(
	unsigned checksums[16], unsigned lengths[16], int last_region,
	int region_target_size, const uint8_t *cursor, uint8_t **region_start,
	int *checksum_region_index, unsigned int *checksum)
{
	if (*checksum_region_index < last_region &&
	    cursor - *region_start > region_target_size) {
		lengths[*checksum_region_index] =
			(unsigned int)(cursor - *region_start);
		checksums[(*checksum_region_index)++] = *checksum;
		*checksum = 0;
		*region_start = (uint8_t *)cursor;
	}
}

void xvt_snapshot_checksum_prefix(const uint8_t *image, size_t prefix,
				  unsigned checksums[16], unsigned lengths[16])
{
	uint8_t *cursor;
	uint8_t *region_start;
	unsigned int checksum;
	int region_target_size;
	int checksum_region_index;
	int object_index;
	int object_count;
	int bytes_remaining;
	int flight_group_count;

	int network = xvt_flight_timing_is_network125();
	int last_region = network ? 14 : 15;
	memset(checksums, 0, 16 * sizeof *checksums);
	memset(lengths, 0, 16 * sizeof *lengths);

	region_target_size = (int)prefix / (network ? 15 : 16);
	cursor = (uint8_t *)image;
	region_start = (uint8_t *)image;
	checksum = 0;
	checksum_region_index = 0;
	object_index = 0;
	object_count = g_region_main_object_slot_end +
		       g_region_static_object_slot_count;
	if (object_count > 0) {
		do {
			if (g_local_transient_slot_start > object_index ||
			    g_local_debris_slot_end <= object_index) {
				uint8_t object_present;

				object_present = *cursor++;
				if (object_present != 0) {
					checksum +=
						xvt_snapshot_sum_slot_records(
							&cursor);
				}
				xvt_snapshot_close_checksum_region(
					checksums, lengths, last_region,
					region_target_size, cursor,
					&region_start, &checksum_region_index,
					&checksum);
			}
			++object_index;
		} while (object_count > object_index);
	}

	checksum += xvt_snapshot_sum_bytes(&cursor, 8);
	checksum += xvt_snapshot_sum_bytes(&cursor, 8);
	checksum +=
		xvt_snapshot_sum_bytes(&cursor, sizeof(struct mission_header));
	flight_group_count = (int16_t)g_mission_header.num_flight_groups;
	bytes_remaining = 294 * flight_group_count;
	if (bytes_remaining > 0) {
		do {
			checksum += *cursor++;
		} while (--bytes_remaining != 0);
	}
	xvt_snapshot_close_checksum_region(
		checksums, lengths, last_region, region_target_size, cursor,
		&region_start, &checksum_region_index, &checksum);

	bytes_remaining = 1382 * flight_group_count;
	if (bytes_remaining > 0) {
		do {
			checksum += *cursor++;
		} while (--bytes_remaining != 0);
	}
	xvt_snapshot_close_checksum_region(
		checksums, lengths, last_region, region_target_size, cursor,
		&region_start, &checksum_region_index, &checksum);

	checksum += xvt_snapshot_sum_bytes(&cursor, 3376);
	xvt_snapshot_close_checksum_region(
		checksums, lengths, last_region, region_target_size, cursor,
		&region_start, &checksum_region_index, &checksum);

	checksum += xvt_snapshot_sum_bytes(&cursor, 22);
	checksum += xvt_snapshot_sum_bytes(&cursor, 2);
	xvt_snapshot_close_checksum_region(
		checksums, lengths, last_region, region_target_size, cursor,
		&region_start, &checksum_region_index, &checksum);

	checksum += xvt_snapshot_sum_bytes(&cursor, 4);
	checksum += *cursor++;
	/* The 20 range dwords: 4 pool sizes, the world-state debris slot count and 15 slot-range bounds. */
	checksum += xvt_snapshot_sum_bytes(&cursor, 20 * 4);
	checksum += xvt_snapshot_sum_bytes(&cursor, 21760);
	checksum += xvt_snapshot_sum_bytes(&cursor, 4);
	checksum += xvt_snapshot_sum_bytes(&cursor, 4);
	checksum += xvt_snapshot_sum_bytes(&cursor, 256);
	xvt_snapshot_close_checksum_region(
		checksums, lengths, last_region, region_target_size, cursor,
		&region_start, &checksum_region_index, &checksum);

	checksum += xvt_snapshot_sum_bytes(&cursor, 2);
	checksum += xvt_snapshot_sum_bytes(&cursor, 2);
	checksum += xvt_snapshot_sum_bytes(&cursor, 4);
	checksum += xvt_snapshot_sum_bytes(&cursor, 11752);
	if (network || cursor - region_start > region_target_size) {
		if (network) {
			checksum_region_index = 14;
		}
		lengths[checksum_region_index] =
			(unsigned)(cursor - region_start);
		checksums[checksum_region_index] = checksum;
	}
}

static unsigned int xvt_snapshot_checksum_mobile_object_char_data(
	const struct mobile_object_char_data *live)
{
	struct xvt_snapshot_mobile_object_char_data record;
	xvt_snapshot_encode_mobile_object_char_data(&record, live);
	return flight_checksum_buffer_rotate_xor(&record, 0x4C);
}

static unsigned int
xvt_snapshot_checksum_mobile_object(const struct mobile_object *live)
{
	struct xvt_snapshot_mobile_object record;
	xvt_snapshot_encode_mobile_object(&record, live);
	return flight_checksum_buffer_rotate_xor(&record, 0x8B);
}

static unsigned int
xvt_snapshot_checksum_object_record(const struct object_record *live)
{
	struct xvt_snapshot_object_record record;
	xvt_snapshot_encode_object_record(&record, live);
	return flight_checksum_buffer_rotate_xor(&record, 0x1F);
}

static unsigned int
xvt_snapshot_checksum_craft_data(const struct craft_data *live)
{
	struct xvt_snapshot_craft_data record;
	xvt_snapshot_encode_craft_data(&record, live);
	return flight_checksum_buffer_rotate_xor(&record, 0x412);
}

static unsigned int
xvt_snapshot_checksum_player_data(const struct player_data *live)
{
	struct xvt_snapshot_player_data record;
	xvt_snapshot_encode_player_data(&record, live);
	return flight_checksum_buffer_rotate_xor(&record, 0x5BD);
}

/* Folds one value into the live checksum: exclusive-or, then rotate left by one bit. */
static uint32_t xvt_snapshot_mix_checksum(uint32_t checksum, uint32_t value)
{
	return flight_rotate_checksum_left(checksum ^ value);
}

/* Folds every occupied pool record into checksum, pool by pool: character data, mobile records,
 * main-slot objects, static objects, craft, then warhead guidance. Returns the new checksum. */
static uint32_t xvt_snapshot_mix_pools(uint32_t checksum)
{
	int first_slot;
	int char_data_index;
	int mobile_object_index;
	int object_index;
	int static_object_index;
	int craft_index;
	int projectile_index;

	first_slot = g_object_slot_range_by_genus[16].start;
	for (char_data_index = 0;
	     char_data_index < (int)g_mobile_object_char_data_count;
	     char_data_index++) {
		if (g_object_table[first_slot + char_data_index].object_type !=
		    0) {
			checksum = xvt_snapshot_mix_checksum(
				checksum,
				xvt_snapshot_checksum_mobile_object_char_data(
					&g_mobile_object_char_data_pool
						[char_data_index]));
		}
	}

	for (mobile_object_index = 0;
	     mobile_object_index <
	     g_region_main_object_slot_end - g_local_debris_slot_count;
	     mobile_object_index++) {
		if (g_object_table[mobile_object_index].object_type != 0) {
			checksum = xvt_snapshot_mix_checksum(
				checksum,
				xvt_snapshot_checksum_mobile_object(
					&g_mobile_object_pool_base
						[mobile_object_index]));
		}
	}
	for (object_index = 0; object_index < g_region_main_object_slot_end -
						      g_local_debris_slot_count;
	     object_index++) {
		if (g_object_table[object_index].object_type != 0) {
			checksum = xvt_snapshot_mix_checksum(
				checksum,
				xvt_snapshot_checksum_object_record(
					&g_object_table[object_index]));
		}
	}
	for (static_object_index = g_region_main_object_slot_end;
	     static_object_index <
	     g_region_main_object_slot_end + g_region_static_object_slot_count;
	     static_object_index++) {
		if (g_object_table[static_object_index].object_type != 0) {
			checksum = xvt_snapshot_mix_checksum(
				checksum,
				xvt_snapshot_checksum_object_record(
					&g_object_table[static_object_index]));
		}
	}

	first_slot = g_object_slot_range_by_genus[0].start;
	for (craft_index = 0; craft_index < g_craft_data_pool_capacity;
	     craft_index++) {
		if (g_object_table[first_slot + craft_index].object_type != 0) {
			checksum = xvt_snapshot_mix_checksum(
				checksum,
				xvt_snapshot_checksum_craft_data(
					&g_craft_data_pool_base[craft_index]));
		}
	}
	first_slot = g_object_slot_range_by_genus[6].start;
	for (projectile_index = 0;
	     projectile_index < (int)g_projectile_object_slots_total;
	     projectile_index++) {
		if (g_object_table[first_slot + projectile_index].object_type !=
		    0) {
			checksum = xvt_snapshot_mix_checksum(
				checksum, flight_checksum_buffer_rotate_xor(
						  &g_projectile_guidance_states
							  [projectile_index],
						  0xA));
		}
	}
	return checksum;
}

int xvt_snapshot_live_checksum(void)
{
	struct xvt_snapshot_flight_mission_state mission_state;
	uint32_t checksum;
	int flight_group_index;
	int goal_index;
	int player_index;

	checksum = 0;
	checksum = xvt_snapshot_mix_pools(checksum);

	checksum = xvt_snapshot_mix_checksum(
		checksum, flight_checksum_buffer_rotate_xor(
				  &g_mission_elapsed_clock,
				  sizeof(g_mission_elapsed_clock)));
	checksum = xvt_snapshot_mix_checksum(
		checksum, flight_checksum_buffer_rotate_xor(
				  &g_mission_countdown_clock,
				  sizeof(g_mission_countdown_clock)));
	for (flight_group_index = 0;
	     flight_group_index < (int16_t)g_mission_header.num_flight_groups;
	     flight_group_index++) {
		checksum = xvt_snapshot_mix_checksum(
			checksum,
			flight_checksum_buffer_rotate_xor(
				&g_mission_fg_stats[flight_group_index],
				0x126));
	}

	xvt_snapshot_encode_flight_mission_state(&mission_state,
						 &g_flight_mission_state);
	checksum = xvt_snapshot_mix_checksum(
		checksum, flight_checksum_buffer_rotate_xor(
				  &mission_state, sizeof(mission_state)));
	checksum = xvt_snapshot_mix_checksum(checksum, g_next_object_signature);
	checksum = xvt_snapshot_mix_checksum(
		checksum, flight_checksum_buffer_rotate_xor(
				  &g_flight_global_countdown_timers, 0x16));
	checksum = xvt_snapshot_mix_checksum(
		checksum, (uint32_t)(int16_t)g_mission_file_version);
	checksum = xvt_snapshot_mix_checksum(
		checksum,
		flight_checksum_buffer_rotate_xor(&g_mission_header, 0xA2));
	for (flight_group_index = 0;
	     flight_group_index < (int16_t)g_mission_header.num_flight_groups;
	     flight_group_index++) {
		checksum = xvt_snapshot_mix_checksum(
			checksum,
			flight_checksum_buffer_rotate_xor(
				&g_mission_flight_groups[flight_group_index],
				0x562));
	}
	checksum = xvt_snapshot_mix_checksum(
		checksum,
		flight_checksum_buffer_rotate_xor(g_mission_messages,
						  sizeof(g_mission_messages)));
	for (goal_index = 0; goal_index < 10; goal_index++) {
		checksum = xvt_snapshot_mix_checksum(
			checksum, flight_checksum_buffer_rotate_xor(
					  g_mission_global_goals[goal_index],
					  sizeof(g_mission_global_goals[0])));
	}

	checksum = xvt_snapshot_mix_checksum(checksum,
					     g_active_flight_player_count);
	checksum = xvt_snapshot_mix_checksum(checksum,
					     g_world_state_reserved_byte);
	checksum =
		xvt_snapshot_mix_checksum(checksum, g_craft_data_pool_capacity);
	checksum = xvt_snapshot_mix_checksum(checksum,
					     g_mobile_object_char_data_count);
	checksum = xvt_snapshot_mix_checksum(checksum,
					     g_projectile_object_slots_total);
	checksum = xvt_snapshot_mix_checksum(checksum,
					     g_debris_object_slots_total);
	checksum = xvt_snapshot_mix_checksum(checksum,
					     g_world_state_debris_slot_count);
	checksum =
		xvt_snapshot_mix_checksum(checksum, g_local_debris_slot_count);
	checksum = xvt_snapshot_mix_checksum(checksum,
					     g_active_region_object_slot_start);
	checksum = xvt_snapshot_mix_checksum(
		checksum, g_active_region_craft_object_slot_end);
	checksum = xvt_snapshot_mix_checksum(
		checksum, g_mobile_object_char_data_slot_start);
	checksum = xvt_snapshot_mix_checksum(
		checksum, g_mobile_object_char_data_slot_end);
	checksum = xvt_snapshot_mix_checksum(checksum,
					     g_projectile_object_slot_start);
	checksum = xvt_snapshot_mix_checksum(checksum,
					     g_projectile_object_slot_end);
	checksum =
		xvt_snapshot_mix_checksum(checksum, g_debris_object_slot_start);
	checksum =
		xvt_snapshot_mix_checksum(checksum, g_debris_object_slot_end);
	checksum = xvt_snapshot_mix_checksum(checksum,
					     g_explosion_object_slot_start);
	checksum = xvt_snapshot_mix_checksum(checksum,
					     g_explosion_object_slot_end);
	checksum = xvt_snapshot_mix_checksum(checksum,
					     g_local_transient_slot_start);
	checksum = xvt_snapshot_mix_checksum(checksum, g_local_debris_slot_end);
	checksum = xvt_snapshot_mix_checksum(checksum,
					     g_region_main_object_slot_end);
	checksum = xvt_snapshot_mix_checksum(checksum,
					     g_region_static_object_slot_count);
	checksum = xvt_snapshot_mix_checksum(
		checksum,
		flight_checksum_buffer_rotate_xor(g_plan_table, 0x5500));
	checksum = xvt_snapshot_mix_checksum(checksum, g_plan_count);
	checksum = xvt_snapshot_mix_checksum(
		checksum,
		flight_checksum_buffer_rotate_xor(g_plan_order_data, 0x1FFFF));
	checksum = xvt_snapshot_mix_checksum(
		checksum, g_unused_world_state_serialized_dword);
	checksum = xvt_snapshot_mix_checksum(
		checksum, (uint16_t)g_game_rand_feedback_state);
	checksum = xvt_snapshot_mix_checksum(checksum, g_flight_conf_new_net);

	for (player_index = 0; player_index < 8; player_index++) {
		if (g_players[player_index].participation_state != 0) {
			checksum = xvt_snapshot_mix_checksum(
				checksum, xvt_snapshot_checksum_player_data(
						  &g_players[player_index]));
		}
	}
	return (int)checksum;
}
