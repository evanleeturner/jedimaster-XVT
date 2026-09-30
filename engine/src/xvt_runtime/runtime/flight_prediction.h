#ifndef XVT_RUNTIME_FLIGHT_PREDICTION_H
#define XVT_RUNTIME_FLIGHT_PREDICTION_H

#include "xvt/flight/flight_input.h"

/* Input prediction for remote players in network flight. Queue fills a tick that has no real or
 * authoritative input with a held copy of that player's last known controls, marked predicted. */

/* Forgets every player's confirmed controls. */
void XvtFlightPrediction_Reset(void);
/* Stores input as player's confirmed controls at tick, tied to the object the player is bound to
 * now; Queue uses them only while the player stays bound to that object. Ignored for an
 * out-of-range player or NULL input. */
/* Only successfully replayed authoritative controls establish the fallback. */
void XvtFlightPrediction_Confirm(unsigned player, int tick, const FlightInputFrameRecord* input);
/* For each connected player other than the local one, inserts a predicted frame at tick, unless
 * a real or authoritative frame is already there. The source is the newest non-predicted history
 * frame before tick, or the confirmed controls when they are newer and still bound; with neither,
 * nothing is inserted. The copy keeps the three axes and the keyMods 0x02 bit (named roll in the
 * body) and clears key, flags, throttle and every other modifier bit. An unapplied prediction
 * already at tick is replaced. Returns 1; returns 0 for an invalid tick, and requests state
 * recovery and returns 0 when a history is corrupt or full, leaving predictions already inserted
 * for earlier players. */
int XvtFlightPrediction_Queue(int tick);

#endif
