#ifndef XVT_FLIGHT_AI_BOARDED_H
#define XVT_FLIGHT_AI_BOARDED_H

/* The boarded side of a boarding, ported from the 1997 AI's Disabled and
 * Await Boarding plans (disabledpln and waitforboardpln, in paiorder.c, deleted
 * with the 1997 AI): a computer craft on either order is switched off, counts
 * the boardings done to it and has its systems back once its order's variable1
 * boardings are done. The pilot program moves the order on; the engine holds
 * no state of its own, the craft's working subsystems, boarding state and
 * order progress carry it. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One step of the craft's boarded side, run on its think after its dock step:
 * on Disabled or Await Boarding, unless the craft is captured, released or
 * has its slot's goal met, it is switched off, then a boarding done to it
 * (boarding state 2 or 3) is counted, and at the order's variable1 it is
 * restored. On Rendezvous the boardings are counted the same way, but the
 * craft is neither switched off nor restored. Any other order is left
 * alone. */
void ai_boarded_step(uint16_t object_index);

/* The craft's abort or departure mark was just set: an undamaged craft on
 * Await Boarding gets its systems back, once, as the 1997 abort and stop
 * orders gave them. Other orders are left alone. */
void ai_boarded_release(uint16_t object_index);

#ifdef __cplusplus
}
#endif

#endif
