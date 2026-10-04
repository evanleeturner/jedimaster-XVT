#ifndef XVT_RUNTIME_SNAPSHOT_WORLD_STATE_H
#define XVT_RUNTIME_SNAPSHOT_WORLD_STATE_H

/* World-state image: the modern bodies behind flight.c's world-state functions, plus the
 * buffer-level calls the resync code uses (Encode, Validate, Decode, ChecksumImage).
 *
 * Purpose: copy the whole flight world (object slots, mission tables, plans, players) into one
 * byte image and back, and checksum it, so the world can be restored and peers can compare
 * and resynchronize worlds.
 *
 * Image layout, in write order:
 *   1. per object slot, skipping the local transient range [g_local_transient_slot_start,
 *      g_local_debris_slot_end): one type byte; if nonzero, the object record, then the mobile
 *      record if the object has one, then its craft, warhead-guidance and character records
 *      if present
 *   2. mission clocks, header, per-flight-group stats and groups, mission state, timers,
 *      file version, player count and a reserved byte
 *   3. 20 dwords: 4 pool sizes, the world-state debris slot count and 15 slot-range bounds
 *   4. plan tables, random state, next object signature, laser-fire flag, 8 player records
 *   5. network 125 Hz profile only: the timing extension and footer (flight_checkpoint.h)
 *
 * Invariants:
 *   - a pointer to a pool entry is stored as its byte offset in the snapshot record array
 *     plus 1; 0 means none (records.c)
 *   - an image is only accepted by a world whose pool sizes, slot ranges and flight-group
 *     count equal the ones it was written with
 *
 * Call: Save/Restore/Checksum work on g_world_state_buffer; Encode/Validate/Decode/
 * ChecksumImage take any buffer. */

#include <stddef.h>
#include <stdint.h>

/* Writes the live world into image. Returns the bytes written, or 0 when image is NULL,
 * CalculateSize() is 0, or capacity is below CalculateSize().
 * Does not check the bytes written against capacity; it relies on CalculateSize()
 * being large enough. */
size_t xvt_snapshot_encode(uint8_t *image, size_t capacity);

/* Returns 1 when image has the layout of an image this world would write, else 0: image is
 * not NULL, size is at most CalculateSize(), every block fits, the object and mobile records' pool links are
 * aligned and in range, player slots are in range, each record's type equals its type byte, the flight-group
 * count, pool sizes, world-state debris slot count and slot ranges equal the live ones, and the length is
 * exact. In the network profile it also checks the footer, the timing extension's CRC, and that its records
 * agree with the image's slots. Does not check any other value; the craft record's object links are not
 * checked. Writes nothing. */
int xvt_snapshot_validate(const uint8_t *image, size_t size);

/* Validates as xvt_snapshot_validate, then overwrites the live world from image.
 * Returns 1 on success; 0 when validation fails, with the world untouched. In the network
 * profile it also installs the timing extension (xvt_flight_checkpoint_restore).
 * A slot whose type byte is 0 is cleared but keeps its pool links. */
int xvt_snapshot_decode(const uint8_t *image, size_t size);

/* Validates as xvt_snapshot_validate, then fills 16 regional byte sums of the world part and
 * each region's byte length. Returns 1 on success; 0 when validation fails, with both
 * arrays untouched.
 * Regions close at the first slot boundary or table checkpoint past about 1/16 of the world part
 * (1/15 in the network profile); unused entries stay 0. The sums skip each record's trailing
 * links: the object's pool reference, the mobile record's cached motion and pool references,
 * and the craft record's last 80 bytes. In the network profile entry 15 is the CRC32C of the
 * timing extension and footer, and the world part ends in entry 14.
 * Does not cover every byte: outside the network profile, a tail no longer than one
 * region target is summed but never stored. */
int xvt_snapshot_checksum_image(const uint8_t *image, size_t size,
				unsigned checksums[16], unsigned lengths[16]);

/* Encodes the live world into g_world_state_buffer and sets g_world_state_size (0 on failure).
 * Assumes the buffer holds CalculateSize() bytes; does not check it. */
void xvt_snapshot_save(void);

/* Decodes g_world_state_buffer (g_world_state_size bytes) into the live world. On failure the
 * world is untouched and g_flight_mission_state.mission_end_pending is set to 1. */
void xvt_snapshot_restore(void);

/* Returns the image buffer size this world needs (plus the timing extension's maximum in the
 * network profile), or 0 when a count is negative or exceeds its wire width (slot total over
 * 65535, flight groups over 32767, the craft, character or projectile pool over 65535).
 * It counts every main slot with a mobile record, every static slot without one, and every
 * pool entry as present. */
size_t xvt_snapshot_calculate_size(void);

/* Checksums g_world_state_buffer into g_world_checksum and g_world_checksum_region_lengths
 * (see xvt_snapshot_checksum_image). Both arguments are ignored; they keep the original
 * signature. On failure sets g_flight_mission_state.mission_end_pending to 1. */
void xvt_snapshot_checksum(int unused_arg0, int unused_arg1);

/* Summarizes an image's object section into out_map: a native int slot count, then for each
 * non-local slot in order, a byte of component flags (0x01 object, 0x02 mobile, 0x04 craft,
 * 0x08 warhead guidance, 0x10 character data) if it is occupied, with runs of empty slots
 * packed as one byte 0x80 | length (1 to 126). Returns the bytes written.
 * world_state must start at the image's first type byte; it is read, never written.
 * Does not validate the image or bound out_map: the worst case is 4 bytes plus one per slot. */
int xvt_snapshot_build_presence_map(uint8_t *out_map,
				    const uint8_t *world_state);

/* Reshapes g_world_state_dup_buffer in place so each slot's blocks match presence_map: a block
 * the map lacks is removed, a block the map has is inserted zero-filled. Updates
 * g_world_state_dup_size. Slots from the map's slot count onward are left alone.
 * Removing an object or mobile record leaves the blocks nested under it; inserting one adds
 * that record alone.
 * Does not rewrite type bytes or pool-reference fields, so afterwards they may disagree
 * with the blocks. Does not check that the buffer has room to grow. */
void xvt_snapshot_apply_presence_map(const uint8_t *presence_map);

/* Returns a rotate-and-xor checksum of the live world, not of an image. Besides what the
 * image carries it covers the mission messages, the 10 global goals, the plan order data,
 * g_flight_conf_new_net and the active player count. It leaves out the laser-fire flag, the
 * built-in plan index and g_flight_player_count, and covers only connected players.
 * Reads the world only. */
int xvt_snapshot_live_checksum(void);

#endif
