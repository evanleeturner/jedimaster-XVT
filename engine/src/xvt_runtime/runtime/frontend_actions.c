#include "xvt_runtime/runtime/frontend_actions.h"

static int g_owner;
static int g_action;

/* Only the suspended parent can resume an action; new input cannot replace it. */
int xvt_frontend_action_trigger(int owner, int action, int pressed)
{
	if (g_owner) {
		return g_owner == owner && g_action == action;
	}
	if (!pressed) {
		return 0;
	}
	g_owner = owner;
	g_action = action;
	return 1;
}

int xvt_frontend_action_pending(int owner)
{
	return g_owner == owner ? g_action : 0;
}

void xvt_frontend_action_finish(int owner)
{
	if (g_owner == owner) {
		xvt_frontend_action_reset();
	}
}

void xvt_frontend_action_reset(void)
{
	g_owner = 0;
	g_action = 0;
}
