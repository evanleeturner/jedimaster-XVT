#ifndef XVT_RUNTIME_FRONTEND_ACTIONS_H
#define XVT_RUNTIME_FRONTEND_ACTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
	XVT_ACTION_OWNER_COMMON = 1,
	XVT_ACTION_OWNER_PILOT,
	XVT_ACTION_OWNER_CONFIG,
	XVT_ACTION_OWNER_CONCOURSE
};

/* One pending frontend action at a time, held by the screen that triggered it (an XVT_ACTION_*
 * owner, never 0) until that screen finishes it, so a suspended screen can resume the action on a
 * later frame. */

/* With nothing pending, records owner and action and returns 1 when pressed, else returns 0. With
 * an action pending, returns 1 only for the same owner and action, and records nothing. */
int XvtFrontendAction_Trigger(int owner, int action, int pressed);
/* The action owner holds, or 0. */
int XvtFrontendAction_Pending(int owner);
/* Clears the pending action when owner holds it. */
void XvtFrontendAction_Finish(int owner);
/* Clears the pending action whoever holds it. */
void XvtFrontendAction_Reset(void);

#ifdef __cplusplus
}
#endif

#endif
