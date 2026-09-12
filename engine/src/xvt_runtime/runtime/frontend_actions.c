#include "xvt_runtime/runtime/frontend_actions.h"

static int g_owner;
static int g_action;

/* Only the suspended parent can resume an action; new input cannot replace it. */
int XvtFrontendAction_Trigger(int owner, int action, int pressed) {
	if (g_owner)
		return g_owner == owner && g_action == action;
	if (!pressed)
		return 0;
	g_owner = owner;
	g_action = action;
	return 1;
}

int XvtFrontendAction_Pending(int owner) { return g_owner == owner ? g_action : 0; }

void XvtFrontendAction_Finish(int owner) {
	if (g_owner == owner)
		XvtFrontendAction_Reset();
}

void XvtFrontendAction_Reset(void) {
	g_owner = 0;
	g_action = 0;
}
