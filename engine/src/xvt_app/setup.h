#ifndef XVT_APP_SETUP_H
#define XVT_APP_SETUP_H
#include "xvt_app/host_config.h"
#include "xvt_app/ui.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef enum XvtSetupResult {
	XVT_SETUP_ERROR,
	XVT_SETUP_SUCCESS,
	XVT_SETUP_CANCELLED,
} XvtSetupResult;

/* A NULL UI keeps installation checks windowless. Otherwise setup initializes
 * the application-owned UI for use throughout the rest of the session. */
XvtSetupResult XvtSetup_Run(const XvtLaunchOptions* options, XvtAppUi* ui, char* error, size_t capacity);
/* Resolves an XvT root or its BalanceOfPower child; output changes only on success.
 * Input and output may share a buffer. */
int XvtSetup_ResolveInstallation(const char* path, char* resolved, size_t resolved_capacity, char* error,
								 size_t capacity);
const char* XvtSetup_Installation(void);
#ifdef __cplusplus
}
#endif

#endif
