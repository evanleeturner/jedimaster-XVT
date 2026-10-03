#ifndef XVT_RUNTIME_NETWORK_SESSION_H
#define XVT_RUNTIME_NETWORK_SESSION_H
#include "aeron/compat/dplay_directory.h"
#include "xvt/net/net.h"
#include "xvt/net/net_reliable.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The DirectPlay session behind hosting and joining, set up without blocking: Update advances through
 * closing the previous session, configuring the directory (online), creating DirectPlay, preparing
 * the join through the directory, opening, creating the local player, the host's group, the
 * roster, then registration with the directory (online host) or the handshake and the host's
 * admission (join). Service keeps the host's directory listing current. */

enum { XVT_NETWORK_PENDING = -1 };

typedef enum xvt_network_session_state {
	XVT_NETWORK_SESSION_IDLE,
	XVT_NETWORK_SESSION_PENDING,
	XVT_NETWORK_SESSION_ADMISSION,
	XVT_NETWORK_SESSION_ESTABLISHED,
	XVT_NETWORK_SESSION_FAILED
} xvt_network_session_state;

struct xvt_network_session_status {
	xvt_network_session_state state;
	AeronDplayDirectoryError error;
};

/* Configures the directory with the lobby URL from settings, remembering it on success; an
 * unchanged URL returns NONE at once. Returns NOT_CONFIGURED for an empty URL and INVALID_REQUEST
 * for one too long. */
AeronDplayDirectoryError xvt_network_session_configure(void);
/* Close the previous session, then advance setup through Update without blocking. */
/* Starts hosting as player with rating info under name ("<player>'s Game." when empty), listed in
 * the directory when online. Returns -1 when started, and also, doing nothing, while another
 * setup is under way; a missing or over-long argument fails the session and returns 0. */
int xvt_network_session_begin_host(const char *rating_text,
				   const char *player_name, const char *name,
				   int online);
/* BeginHost's rules for joining room through the directory, always online. */
int xvt_network_session_begin_join(const char *rating_text,
				   const char *player_name, const GUID *room);
/* FAILED, ESTABLISHED or ADMISSION for those phases, PENDING for any other setup phase or while a
 * close completes, else IDLE; error is the last failure's. */
struct xvt_network_session_status xvt_network_session_get_status(void);
/* Application thread, after the game task: deadlines, close completion and metadata. */
/* Finishes a close once DirectPlay is inactive, cancelling a deferred join. Fails a lost session
 * outside flight, and a join past its deadline or whose preparation failed. For an established,
 * registered host: publishes the room when it changes (joinable only on mission setup after its
 * first frame; in flight, only players still active once ready), skips an empty roster, and
 * retries a failed listing every 15 seconds. */
void xvt_network_session_service(void);
/* Leave. */
void xvt_network_session_cancel(void);
/* Shuts down the DirectPlay session. */
void xvt_network_session_leave(void);
/* Called by the recovered close path; it never recursively closes DirectPlay. */
/* Forgets the flight mission cookie and its counter, stops the directory listing, cancels a join (deferred
 * until the close completes when the session was opened), clears the flight and lost state and returns to
 * idle with a close pending. */
void xvt_network_session_on_close(void);
/* During admission, accepts the host's admission of the local player before the join deadline
 * and while not lost: finishes the join and returns 1 with the session established; otherwise 0. */
int xvt_network_session_accept_admission(DPID sender, DPID player);
/* For a refused join: leaves the session, finishes the directory join and drops a deferred
 * cancel. */
void xvt_network_session_reject(void);
/* Marks the session lost; outside flight, the next Service or Update fails it. */
void xvt_network_session_host_lost(void);
/* 1 once the session is marked lost. */
int xvt_network_session_is_lost(void);
/* When established: marks a flight begun, and an online host lists the room as in flight and not
 * joinable, with the current roster when it has players. */
void xvt_network_session_begin_flight(void);
/* Marks the flight ready, so the listing keeps only players still active. */
void xvt_network_session_mark_flight_ready(void);
/* Ends a begun flight and refreshes the player roster. */
void xvt_network_session_end_flight(void);

/* Advances setup; returns -1 while pending, 1 in admission or established, and 0 once failed or
 * idle. A lost session outside flight fails. */
int xvt_network_session_update(void);
/* Forgets the session state and the configured lobby URL, without closing DirectPlay. */
void xvt_network_session_shutdown(void);
/* Splits message's short and long names, each NUL-terminated, into the outputs, truncated to fit.
 * Returns 0 for a NULL or zero-capacity argument or a name without its terminator. */
int xvt_network_session_copy_player_names(
	const struct net_player_name_message *message, char *short_name,
	size_t short_capacity, char *long_name, size_t long_capacity);

#ifdef __cplusplus
}
#endif
#endif
