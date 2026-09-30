#ifndef XVT_RUNTIME_RESYNC_TASK_H
#define XVT_RUNTIME_RESYNC_TASK_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Network125 state recovery. The host sends a client its checkpointed world image: a checksum
 * table and request, chunks in acknowledged batches of XVT_RESYNC_CHUNKS_PER_BATCH, then an apply
 * the client acknowledges. The client verifies and installs the image, then replays its buffered
 * world messages to catch up. Retries run every XVT_RESYNC_RETRY_TICKS; Escape during a send
 * boots the peer. Checksum reports that arrive during a transfer are deferred until it ends. */

/* Network125 only. A client sends the host a checksum report flagged as a state request and gives
 * it a peer-timeout deadline; with a request already out, it aborts the local player and ends the
 * mission once that deadline passes. The host cannot receive an image, so it ends the mission and
 * announces that it left. */
void XvtResync_RequestState(void);
/* 1 when a deferred checksum report carries a state request. */
int XvtResync_HasStateRequest(void);
/* The tick at or below which a world message is old: the incoming image's tick while it is
 * received or replayed, else the server tick. */
int XvtResync_ReceiveFloor(void);
/* Returns 1 when it consumed the packet, else 0 so the flight control handler sees it. On the host
 * during a send, a state request from the peer is deferred and restarts the send, and the peer's
 * ready signal starts the chunks. On a client, from the host only: the checksum table is kept; the
 * request starts the receive, moving queued world messages to replay; each chunk is validated
 * whole before it is written and acknowledged; the apply verifies the image against the table,
 * decodes it, installs the world and starts the replay. A receive whose image or queues fail
 * restarts once; a second failure ends the mission. */
int XvtResync_ReceivePacket(int sender, const uint8_t* bytes, unsigned size);
/* Host: starts sending world, a checkpoint image of size bytes, to player: announces the resync,
 * shows the alert and sends the checksum table and request from a pinned copy. Returns -1 when
 * started, and also, doing nothing, while a transfer is under way; returns 0, ending the send,
 * when the image fails validation, copying or checksumming. */
int XvtResync_BeginSend(int player, uint8_t* world, int size);
/* Sends player the apply for size bytes, stamped with the input tick, and waits in the apply phase
 * for the acknowledgement, resending each retry interval and booting player after
 * XVT_RESYNC_RETRIES attempts. */
void XvtResync_BeginApply(int player, int size);
/* Processes packets and returns 1 once chunks 0 through count - 1 are acknowledged, and -1 while
 * waiting or after a restart request. After XVT_RESYNC_ACK_RETRIES intervals without progress,
 * or on Escape, boots player and returns 0. */
int XvtResync_WaitAcks(int player, int count);
/* 1 while a send, receive or replay runs. */
int XvtResync_IsActive(void);
/* On a client: 1 while a state request is out or an image is received or replayed; incoming input
 * batches are then ignored. */
int XvtResync_HoldsInput(void);
/* 0 when work is ready now; the receive deadline while an image is requested or received; the
 * time to the next retry during a send; UINT64_MAX when idle. */
uint64_t XvtResync_NextWakeDelayUs(void);
/* Drops a send its peer restarted, restarts a failed receive, serves deferred checksum reports
 * while idle, flushes world parts, and advances the current phase. A receive past its deadline
 * ends the mission. A replay that confirms every buffered message acknowledges the host, resets
 * the input clock to the server tick plus the lead allowance, recovers the controls and returns to
 * idle. A pending mission end resets a running resync. */
void XvtResync_Tick(void);
/* Drops any send, receive, pinned image and deferred reports, restoring the alert box. */
void XvtResync_Reset(void);
/* Queues a checksum report from sender to be handled after the current transfer; a full queue logs
 * an error and ends the mission. */
void XvtResync_DeferChecksum(int sender, const int* packet);
/* Called after the received world and buffered messages have been applied. */
/* Tells the render capture that the world changed. */
void XvtResync_WorldApplied(void);

#ifdef __cplusplus
}
#endif
#endif
