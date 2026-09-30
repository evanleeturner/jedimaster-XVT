#ifndef XVT_RUNTIME_REFERENCE_MOTION_H
#define XVT_RUNTIME_REFERENCE_MOTION_H
#include "xvt_runtime/timing/flight_state.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Each object slot's position at the last reference boundary and the time it held, kept only in
 * unlocked flights. From it, Displacement gives an object's movement over one reference period, which
 * unlocked flights use where the original took the world position minus the previous one. An entry
 * whose slot now holds another object (another signature or type) is cleared when next read. */

/* Frees any old table and allocates count entries, sampling each live object in the first count slots
 * of g_objectTable at g_gameTime. Returns 0 when allocation fails, leaving no table. */
int XvtReferenceMotion_Init(size_t count);
/* Frees the table. */
void XvtReferenceMotion_Shutdown(void);
/* Clears slot's entry; a slot out of range is ignored. */
void XvtReferenceMotion_Reset(unsigned slot);
/* Records that slot's current position holds at timestamp; object movement passes the end of the
 * current step. Does nothing unless the flight is unlocked. */
void XvtReferenceMotion_Committed(unsigned slot, int timestamp);
/* In an unlocked reference step, samples every slot: a live object's position, timed at its committed
 * time or else g_gameTime; an empty slot's entry is marked invalid. Otherwise does nothing. */
void XvtReferenceMotion_CommitBoundary(void);
/* Fills delta with slot's movement per reference period of 8 ticks: the change from the sampled
 * position to the current one, times 8, over the ticks between them, clamped to int32. The current time
 * is the committed one, else g_gameTime. All zero when the slot is out of range, holds no valid sample,
 * or no time has passed. */
void XvtReferenceMotion_Displacement(unsigned slot, int32_t delta[3]);
/* One axis of Displacement; 0 for an axis past 2. */
int32_t XvtReferenceMotion_Axis(unsigned slot, unsigned axis);
/* Canonical schema-1 record, 28 bytes. Decode validates before installation. */
/* Writes slot's entry as a record. A slot out of range or empty, or an entry that belongs to another
 * object, gives the empty record: zero but for the slot. */
void XvtReferenceMotion_Encode(unsigned slot, XvtReferenceMotionWire* out);
/* Returns 1 when record is well formed: slot in range, no unknown flag, a zero reserved field, and,
 * for type 0, exactly the empty record. With apply, it then replaces the slot's entry. Returns 0
 * otherwise and changes nothing. The record is not checked against the object in the slot; a later
 * read clears the entry if they differ. */
int XvtReferenceMotion_Decode(const XvtReferenceMotionWire* record, int apply);
/* Clears every entry except the local slots from g_localTransientSlotStart up to g_localDebrisSlotEnd.
 * Does nothing before Init. */
void XvtReferenceMotion_ResetShared(void);

#ifdef __cplusplus
}
#endif

#endif
