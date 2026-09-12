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

int XvtNetworkDialogs_Resume(int result, int action);
int XvtNetworkDialogs_Connecting(void);
void XvtNetworkDialogs_Return(int host);
void XvtNetworkDialogs_Failed(AeronDplayDirectoryError error, int host);
int XvtNetworkDialogs_AdmissionFailed(void);

#ifdef __cplusplus
}
#endif

#endif
