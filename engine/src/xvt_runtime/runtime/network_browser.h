#ifndef XVT_RUNTIME_NETWORK_BROWSER_H
#define XVT_RUNTIME_NETWORK_BROWSER_H
#ifdef __cplusplus
extern "C" {
#endif
int XvtNetworkBrowser_Screen(int first_frame);
int XvtNetworkBrowser_DrawList(void);
int XvtNetworkBrowser_DrawRoster(void);
int XvtNetworkBrowser_DrawMission(void);
#ifdef __cplusplus
}
#endif
#endif
