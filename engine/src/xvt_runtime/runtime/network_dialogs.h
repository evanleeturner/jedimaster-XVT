#ifndef XVT_RUNTIME_NETWORK_DIALOGS_H
#define XVT_RUNTIME_NETWORK_DIALOGS_H
#include "aeron/compat/dplay_directory.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum XvtNetworkDialogAction {
	XVT_NETWORK_ACCESS_REJECTED,
	XVT_NETWORK_ACCESS_PASSWORD
} XvtNetworkDialogAction;

/* The connecting screen and the failure dialogs of a network host or join, and the return to the
 * host or join screen afterward. */

/* Dialog continuation for an access action: returns to the join screen, first queuing the options
 * datapad for a password. result is ignored; returns 0. */
int XvtNetworkDialogs_Resume(int result, int action);
/* Draws the connecting screen with a cancel button; returns 1 when cancel is clicked. */
int XvtNetworkDialogs_Connecting(void);
/* Opens the host screen when host is set, else the join screen, as a network session, with the
 * entry movie skipped and the selected game, probe, mission id and briefing text cleared. */
void XvtNetworkDialogs_Return(int host);
/* Resets the network session and shows the message for error in a confirm dialog; once it is
 * dismissed, or at once when the dialog does not wait, returns to the host or join screen. */
void XvtNetworkDialogs_Failed(AeronDplayDirectoryError error, int host);
/* When the network session has failed, reports it through Failed as a join and returns 1;
 * otherwise returns 0. */
int XvtNetworkDialogs_AdmissionFailed(void);

#ifdef __cplusplus
}
#endif

#endif
