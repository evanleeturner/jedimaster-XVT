#ifndef XVT_RUNTIME_FLIGHT_NETWORK_EXCHANGE_H
#define XVT_RUNTIME_FLIGHT_NETWORK_EXCHANGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The exchanges before flight: roster, options and taunts, and mission
 * start, and the mission cookie they agree on, which tags control packets
 * in flight (flight_network.h). Each exchange returns
 * XVT_FLIGHT_NETWORK_PENDING while it waits. */
enum { XVT_FLIGHT_NETWORK_PENDING = -1 };

/* The current mission cookie; 0 before one is agreed. */
uint32_t xvt_flight_network_cookie(void);
/* Forgets the mission cookie and the cookie counter. */
void xvt_flight_network_clear_cookies(void);
/* Marks the network session's flight ready and starts the roster exchange: the host waits for
 * players startup-ready packets (none when 0) and then sends the roster; a client waits for it,
 * or returns 1 at once for an in-progress launch. Returns -1 otherwise. */
int xvt_flight_network_begin_roster_exchange(int player_count, int in_progress);
/* Advances the roster exchange. Returns -1 while pending, 1 when done (at once for a host
 * expecting at most one player), and 0 after 60 seconds without a packet. */
int xvt_flight_network_exchange_roster(void);
/* Exchanges each player's resolution, rating and taunts; the host first takes a new mission cookie
 * and returns 0 when the counter is spent, leaving the session. A single player fills its own and
 * returns 1. Returns -1 while pending, 1 once every active player's taunts arrived, and 0 after 60
 * seconds without a packet (30 during taunts). */
int xvt_flight_network_exchange_options(void);
/* The mission start handshake. A single player resets the clocks and returns 1.
 * Otherwise each player tells the host it has loaded; the host, once all have,
 * broadcasts the start. On the start every player acknowledges and resets the
 * clocks with a lead allowance of 130 ticks with async on, 30 otherwise; the
 * host then waits for the acknowledgements until its input clock reaches 100,
 * sets its lead allowance to the input clock reached, and raises both to 35
 * when lower. Returns -1 while pending, 1 when started, and 0 after 60 seconds
 * without a packet. */
int xvt_flight_network_wait_for_mission_start(void);
/* Ends any exchange in progress and forgets the mission cookie, keeping the counter. */
void xvt_flight_network_reset(void);

#ifdef __cplusplus
}
#endif
#endif
