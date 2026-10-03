#ifndef XVT_RUNTIME_FLIGHT_NETWORK_H
#define XVT_RUNTIME_FLIGHT_NETWORK_H

#include "xvt/flight/flight_input.h"
#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt_runtime/timing/flight_timing.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Network125 flight networking. Before flight: the roster, options and taunts, and mission start
 * exchanges, which agree on a mission cookie that tags control packets. In flight: each player's
 * input is recorded, staged and sent in batches, and the host sends world messages that make the
 * applied input authoritative. The host is the side whose net_session_is_local_host() is
 * nonzero. */

/* With a mission cookie and the local player connected, reads packets up to the per-iteration
 * budget: drops those that fail DecodeControl or come from an unknown sender, and hands the rest
 * to Receive, the resync task or the flight control handler, which can end the read. When no
 * packet waits, the host sends a world message if TakeWorldSendTurn allows, else reading stops. Unless the
 * control handler ended it, the input clock then advances by the frame time read meanwhile. */
void xvt_flight_network_process_packets(void);
/* Host pacing for SendWorld: 1 once a message interval of clock-adjusted input time has passed and
 * every connected player's latest applied input lies beyond the next message's tick (a player with
 * none blocks it), or at once when XVT_WORLD_LATE_INTERVALS behind. 0 while parts are still
 * outgoing, recovery is needed, a resync state request is pending, the pending queue lacks room or
 * start acknowledgements are pending. */
int xvt_flight_network_take_world_send_turn(int input_timestamp);
/* 1 once recovery was requested, until ClearRecoveryRequest or Recovered. */
int xvt_flight_network_needs_recovery(void);
/* Marks that the flight state needs recovery, logging a warning the first time. */
void xvt_flight_network_request_recovery(void);
/* On the host, excludes player from later world messages. Returns 1 unless player is the local
 * player or out of range. */
int xvt_flight_network_player_abort(unsigned player);
/* Clears the recovery request. */
void xvt_flight_network_clear_recovery_request(void);
/* Clears the recovery request and the control sample, and drops staged input at or before the
 * game time. */
void xvt_flight_network_recovered(void);
/* Starts a new host iteration, keyed on the host clock: resets the control sample and the send
 * and receive budgets. Does nothing when called again in the same iteration. */
void xvt_flight_network_begin_iteration(void);
/* Takes one packet from this iteration's receive budget: 1, or 0 when it is spent. */
int xvt_flight_network_take_packet_budget(void);
/* Starts a mission: empties the message queues and the staged input and outgoing world message. */
void xvt_flight_network_reset_mission(void);
/* Sends staged input in batches of up to XVT_INPUT_BATCH_RECORDS, at most
 * XVT_INPUT_BATCHES_PER_ITERATION per iteration, once XVT_INPUT_BATCH_TICKS have passed since the
 * last flush or a batch is full. Each goes to every connected remote player, or with async on,
 * to the host and, in a session smaller than the small-session threshold, every connected player. */
void xvt_flight_network_flush_input(int now);
/* Records the local player's controls for tick as real input and stages them for sending. The
 * controls are sampled once per iteration; key, flags and throttle go only to the first tick
 * admitted from a sample. Returns 1, at once when the history already holds tick; returns 0
 * during recovery, for an invalid tick, with the staging full, or when recording fails (a full
 * history requests recovery). */
int xvt_flight_network_admit_input(int tick);
/* 1 while parts of a world message remain to be sent. */
int xvt_flight_network_outgoing(void);
/* 0 while world parts remain or, outside recovery, pending messages wait; else the time until the
 * next input flush when input is staged, else one world message interval. */
uint64_t xvt_flight_network_next_wake_delay_us(int now);
/* Sends up to XVT_WORLD_PARTS_PER_ITERATION parts of the outgoing world message to each remote
 * player in its mask. */
void xvt_flight_network_flush_world(void);
/* Host: builds the world message XVT_WORLD_MESSAGE_TICKS after the last one from every connected,
 * non-aborted, non-departed player's applied input up to that tick, flagged for a checksum every
 * XVT_WORLD_CHECKSUM_TICKS. Queues it pending for the host's own confirmation, marks that input
 * unapplied, aborts and excludes peers silent past XVT_PEER_TIMEOUT_TICKS, and starts sending its
 * parts. Does nothing while parts are outgoing, during recovery, or with no player to include; a
 * tick past the valid range ends the mission; a full pending queue requests recovery. */
void xvt_flight_network_send_world(void);
/* Records each input of message as authoritative. Returns 0 at the first record that fails to
 * decode or record (a record failure requests recovery), else 1. */
int xvt_flight_network_insert_world(const struct xvt_flight_message *message);
/* Consumes flight data packets and returns 1 for them; returns 0 for any other packet. An input
 * batch from a connected remote player, unless a resync holds input, replaces the player's
 * predicted frames with its records as real input, stopping at the first failure. A world message
 * part from the host is assembled; a complete message is queued pending, or for replay on a client
 * during a resync. A bad part, a mask outside the initial players or a full queue requests
 * recovery. Parts from other senders and remote-input packets are dropped. */
int xvt_flight_network_receive(int sender, const uint8_t *bytes, size_t size);

enum { XVT_FLIGHT_NETWORK_PENDING = -1 };

/* SendPacket and Broadcast for byte-packed wire records: copies packet to an aligned buffer first.
 * Returns 0 for a packet shorter than 4 bytes or longer than XVT_FLIGHT_PACKET_BYTES. */
int xvt_flight_network_send_wire(int dpid, const void *packet, size_t size);
int xvt_flight_network_broadcast_wire(const void *packet, size_t size);
/* Sends packet (Broadcast: to every player); with a mission cookie, a control opcode is sent with
 * the cookie appended. Returns 0 when that would exceed XVT_FLIGHT_PACKET_BYTES, else the network
 * session's result. */
int xvt_flight_network_send_packet(int dpid, const unsigned *packet, int size);
int xvt_flight_network_broadcast(const unsigned *packet, int size);
/* Checks a received packet: for a control opcode, the appended cookie must match and is removed
 * from size. Returns 1 when the remaining size meets the opcode's minimum; 0 for a size under 4 or
 * over XVT_FLIGHT_PACKET_BYTES, or a missing or wrong cookie. */
int xvt_flight_network_decode_control(const uint8_t *packet, int *size);
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
/* The mission start handshake. A single player resets the clocks and returns 1. Otherwise each
 * player tells the host it has loaded; the host, once all have, broadcasts the start. On the start
 * every player acknowledges and resets the clocks with a lead allowance of 130 ticks with async on,
 * 30 otherwise; the host then waits for the acknowledgements until its input clock reaches 100,
 * sets its lead allowance to the input clock reached, and raises both to 35 when lower. Returns -1 while
 * pending, 1 when started, and 0 after 60 seconds without a packet. */
int xvt_flight_network_wait_for_mission_start(void);
/* Ends any exchange in progress and forgets the mission cookie, keeping the counter. */
void xvt_flight_network_reset(void);

#ifdef __cplusplus
}
#endif
#endif
