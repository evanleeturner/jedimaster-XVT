#ifndef XVT_RUNTIME_FLIGHT_CHECKPOINT_H
#define XVT_RUNTIME_FLIGHT_CHECKPOINT_H
#include "xvt_runtime/timing/flight_state.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The timing extension of a network125 world checkpoint: appended after the saved world bytes, it
 * holds the reference motion and integration of every shared object slot (all slots but the
 * local transient and debris range), each player's timing, the per-player paired motion saved at
 * a lockstep tick, and the membership masks, closed by a footer with a CRC-32C of the extension. */

/* Starts a flight: clears every paired record and sets the initial and confirmed masks to mask. */
void XvtFlightCheckpoint_Begin(uint8_t mask);
/* The largest extension Append can write, footer included, for the current world. */
size_t XvtFlightCheckpoint_Maximum(void);
/* Writes the extension after prefix world bytes in image, which must hold prefix plus Maximum()
 * bytes. The footer records the network125 profile, the session cookie, the current game time and
 * both lengths. Returns the whole image size. */
size_t XvtFlightCheckpoint_Append(uint8_t* image, size_t prefix);
/* Read without keeping the view: stores the world length and tick and returns 1 when the image is
 * valid, else returns 0. */
int XvtFlightCheckpoint_Validate(const uint8_t* image, size_t size, size_t* prefix, int* tick);

typedef struct XvtFlightCheckpointView {
	size_t prefix;
	int tick;
	unsigned objects;
	const uint8_t *reference, *integration, *players, *paired;
	XvtMembershipWire membership;
} XvtFlightCheckpointView;

/* Checks the extension of image against this session and world without changing any state: the
 * footer's magic, schema, profile and cookie, a non-negative tick on a step boundary, the lengths,
 * the CRC, the shared-slot count, a dry decode of every record, and membership whose initial mask
 * matches this flight and whose confirmed mask stays within it. The world bytes are not checked.
 * On 1, view points into image, which must outlive it. */
int XvtFlightCheckpoint_Read(const uint8_t* image, size_t size, XvtFlightCheckpointView* view);
/* Install only a view validated with Read and the recovered world prefix. */
/* Replaces every shared motion, integration and player timing record and the paired records,
 * applies the confirmed mask through SetMask, and sets the game time and network tick to the
 * view's tick. */
void XvtFlightCheckpoint_Restore(const XvtFlightCheckpointView* view);
/* Network125 only: replaces player's paired record with the motion of the player's object, and of
 * the object it carries when that is another live shared slot, tagged with tick. The record stays
 * invalid when the player's object is not a live shared slot. */
void XvtFlightCheckpoint_SavePlayer(unsigned player, int tick);
/* Network125 only: restores player's paired motion when the record matches the player's current
 * object slot and signature and was saved at the player's lockstep tick; the carried object's
 * motion is restored when it still matches, else its integration is reset when it is shared. On a
 * mismatch, resets the integration of the player's object and invalidates the record. */
void XvtFlightCheckpoint_RestorePlayer(unsigned player);
/* Clears player's paired record; ignored for an out-of-range player. */
void XvtFlightCheckpoint_InvalidatePlayer(unsigned player);
/* The players present at flight start, one bit each. */
uint8_t XvtFlightCheckpoint_InitialMask(void);
/* The initial players still confirmed present. */
uint8_t XvtFlightCheckpoint_ConfirmedMask(void);
/* Sets the confirmed mask to mask within the initial mask, raises the abort flag of each initial
 * player it drops, and clears every other player's abort flag. */
void XvtFlightCheckpoint_ApplyConfirmedMask(uint8_t mask);
#ifdef __cplusplus
}
#endif
#endif
