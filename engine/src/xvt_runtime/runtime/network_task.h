#ifndef XVT_RUNTIME_NETWORK_TASK_H
#define XVT_RUNTIME_NETWORK_TASK_H
#include "aeron/compat/dplay_directory.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { XVT_NETWORK_HOST, XVT_NETWORK_AUTO_HOST, XVT_NETWORK_CONNECT };

typedef struct XvtNetworkPreview {
	char title[256], text[4096];
	int scroll;
} XvtNetworkPreview;

void XvtNetworkTask_Begin(int action);
int XvtNetworkTask_IsActive(void);
void XvtNetworkTask_OpenBrowser(void);
void XvtNetworkTask_Refresh(void);
void XvtNetworkTask_ServiceBrowser(void);
int XvtNetworkTask_BrowserVisible(void);
const AeronDplayDirectorySnapshot* XvtNetworkTask_Snapshot(void);
const AeronDplayDirectoryRoom* XvtNetworkTask_SelectedRoom(void);
int XvtNetworkTask_SelectedIndex(void);
int* XvtNetworkTask_ScrollOffset(void);
XvtNetworkPreview* XvtNetworkTask_Preview(void);
unsigned XvtNetworkTask_SnapshotAge(void);
AeronDplayDirectoryError XvtNetworkTask_BrowserError(void);
int XvtNetworkTask_Compatible(const AeronDplayDirectoryRoom* room);
int XvtNetworkTask_CanJoin(void);
void XvtNetworkTask_Select(int index);
/* Called under the frontend draw lock in place of the suspended screen update. */
int XvtNetworkTask_Resume(int* result);
void XvtNetworkTask_Cancel(void);
void XvtNetworkTask_Shutdown(void);
#ifdef __cplusplus
}
#endif
#endif
