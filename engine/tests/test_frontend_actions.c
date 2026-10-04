/* Checks the pending frontend action (xvt_runtime/runtime/frontend_actions.h)
 * against the promises in its header: one action at a time, recorded only on a
 * press with nothing pending, held by its owner until that owner finishes it,
 * and cleared for anyone by Reset. The module keeps its own state; every case
 * starts from Reset. */
#include "test_assert.h"
#include "xvt_runtime/runtime/frontend_actions.h"

enum { ACTION_A = 7, ACTION_B = 9 };

static void check_trigger_records_on_press(void)
{
	xvt_frontend_action_reset();
	/* With nothing pending, a release records nothing. */
	XVT_ASSERT_INT_EQ(xvt_frontend_action_trigger(XVT_ACTION_OWNER_PILOT,
						      ACTION_A, 0),
			  0);
	XVT_ASSERT_INT_EQ(xvt_frontend_action_pending(XVT_ACTION_OWNER_PILOT),
			  0);

	/* A press records owner and action. */
	XVT_ASSERT_INT_EQ(xvt_frontend_action_trigger(XVT_ACTION_OWNER_PILOT,
						      ACTION_A, 1),
			  1);
	XVT_ASSERT_INT_EQ(xvt_frontend_action_pending(XVT_ACTION_OWNER_PILOT),
			  ACTION_A);
	XVT_ASSERT_INT_EQ(xvt_frontend_action_pending(XVT_ACTION_OWNER_CONFIG),
			  0);
}

static void check_pending_action_holds_input(void)
{
	xvt_frontend_action_reset();
	XVT_ASSERT_INT_EQ(xvt_frontend_action_trigger(XVT_ACTION_OWNER_CONFIG,
						      ACTION_A, 1),
			  1);

	/* The same owner and action is resumed, pressed or not. */
	XVT_ASSERT_INT_EQ(xvt_frontend_action_trigger(XVT_ACTION_OWNER_CONFIG,
						      ACTION_A, 0),
			  1);
	XVT_ASSERT_INT_EQ(xvt_frontend_action_trigger(XVT_ACTION_OWNER_CONFIG,
						      ACTION_A, 1),
			  1);

	/* Another action or another owner is refused and records nothing, even when pressed. */
	XVT_ASSERT_INT_EQ(xvt_frontend_action_trigger(XVT_ACTION_OWNER_CONFIG,
						      ACTION_B, 1),
			  0);
	XVT_ASSERT_INT_EQ(xvt_frontend_action_trigger(XVT_ACTION_OWNER_COMMON,
						      ACTION_A, 1),
			  0);
	XVT_ASSERT_INT_EQ(xvt_frontend_action_pending(XVT_ACTION_OWNER_CONFIG),
			  ACTION_A);
	XVT_ASSERT_INT_EQ(xvt_frontend_action_pending(XVT_ACTION_OWNER_COMMON),
			  0);
}

static void check_finish(void)
{
	xvt_frontend_action_reset();
	XVT_ASSERT_INT_EQ(xvt_frontend_action_trigger(
				  XVT_ACTION_OWNER_CONCOURSE, ACTION_B, 1),
			  1);

	/* Only the owner can finish its action. */
	xvt_frontend_action_finish(XVT_ACTION_OWNER_PILOT);
	XVT_ASSERT_INT_EQ(
		xvt_frontend_action_pending(XVT_ACTION_OWNER_CONCOURSE),
		ACTION_B);
	xvt_frontend_action_finish(XVT_ACTION_OWNER_CONCOURSE);
	XVT_ASSERT_INT_EQ(
		xvt_frontend_action_pending(XVT_ACTION_OWNER_CONCOURSE), 0);

	/* With the action finished, a new press by another owner is recorded. */
	XVT_ASSERT_INT_EQ(xvt_frontend_action_trigger(XVT_ACTION_OWNER_PILOT,
						      ACTION_A, 1),
			  1);
	XVT_ASSERT_INT_EQ(xvt_frontend_action_pending(XVT_ACTION_OWNER_PILOT),
			  ACTION_A);
}

static void check_reset(void)
{
	xvt_frontend_action_reset();
	XVT_ASSERT_INT_EQ(xvt_frontend_action_trigger(XVT_ACTION_OWNER_COMMON,
						      ACTION_A, 1),
			  1);
	xvt_frontend_action_reset();
	XVT_ASSERT_INT_EQ(xvt_frontend_action_pending(XVT_ACTION_OWNER_COMMON),
			  0);
	XVT_ASSERT_INT_EQ(xvt_frontend_action_trigger(XVT_ACTION_OWNER_CONFIG,
						      ACTION_B, 1),
			  1);
	XVT_ASSERT_INT_EQ(xvt_frontend_action_pending(XVT_ACTION_OWNER_CONFIG),
			  ACTION_B);
	xvt_frontend_action_reset();
}

int main(void)
{
	check_trigger_records_on_press();
	check_pending_action_holds_input();
	check_finish();
	check_reset();
	return 0;
}
