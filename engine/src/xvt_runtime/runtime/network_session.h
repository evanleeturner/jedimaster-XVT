#ifndef XVT_RUNTIME_NETWORK_SESSION_H
#define XVT_RUNTIME_NETWORK_SESSION_H
#include "aeron/compat/dplay_directory.h"
#include "xvt/net/net.h"
#include "xvt/net/net_reliable.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { XVT_NETWORK_PENDING = -1 };

typedef enum XvtNetworkSessionState {
	XVT_NETWORK_SESSION_IDLE,
	XVT_NETWORK_SESSION_PENDING,
	XVT_NETWORK_SESSION_ADMISSION,
	XVT_NETWORK_SESSION_ESTABLISHED,
	XVT_NETWORK_SESSION_FAILED
} XvtNetworkSessionState;

typedef struct XvtNetworkSessionStatus {
	XvtNetworkSessionState state;
	AeronDplayDirectoryError error;
} XvtNetworkSessionStatus;

AeronDplayDirectoryError XvtNetworkSession_Configure(void);
/* Close the previous session, then advance setup through Tick without blocking. */
int XvtNetworkSession_BeginHost(const char* info, const char* player, const char* name, int online);
int XvtNetworkSession_BeginJoin(const char* info, const char* player, const GUID* room);
XvtNetworkSessionStatus XvtNetworkSession_GetStatus(void);
/* Application thread, after the game task: deadlines, close completion and metadata. */
void XvtNetworkSession_Service(void);
void XvtNetworkSession_Cancel(void);
void XvtNetworkSession_Leave(void);
/* Called by the recovered close path; it never recursively closes DirectPlay. */
void XvtNetworkSession_OnClose(void);
int XvtNetworkSession_Admission(DPID sender, DPID player);
void XvtNetworkSession_Reject(void);
void XvtNetworkSession_HostLost(void);
int XvtNetworkSession_IsLost(void);
void XvtNetworkSession_BeginFlight(void);
void XvtNetworkSession_FlightReady(void);
void XvtNetworkSession_EndFlight(void);

int XvtNetworkSession_Tick(void);
void XvtNetworkSession_Reset(void);
void XvtNetworkSession_Shutdown(void);
int XvtNetworkSession_CopyPlayerNames(const NetPlayerNameMessage* message, char* short_name,
									  size_t short_capacity, char* long_name, size_t long_capacity);

#ifdef __cplusplus
}
#endif
#endif
