/* Checks the Sith setting (xvt_runtime/runtime/sith.h): none at start, the
 * three values it keeps, a value that is none of them read as none, the
 * launch description's unset value read as none, and the words the logs
 * print. The module touches no game data. */
#include <string.h>

#include "test_assert.h"
#include "xvt_runtime/runtime/launch_description.h"
#include "xvt_runtime/runtime/sith.h"

static void check_none_at_start(void)
{
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_NONE);
}

static void check_values_kept(void)
{
	xvt_sith_set_setting(XVT_SITH_LORD);
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_LORD);
	xvt_sith_set_setting(XVT_SITH_APPRENTICES);
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_APPRENTICES);
	xvt_sith_set_setting(XVT_SITH_NONE);
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_NONE);
}

/* A value outside the three is none, not kept. */
static void check_other_values_are_none(void)
{
	static const int others[] = {-1, 3, 4, 255, 1000000};
	for (size_t index = 0; index < sizeof(others) / sizeof(others[0]);
	     ++index) {
		xvt_sith_set_setting(XVT_SITH_LORD);
		xvt_sith_set_setting(others[index]);
		XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_NONE);
	}
}

/* A description's value sets the setting; a description that leaves it out
 * sets none, so a machine never keeps an earlier battle's setting. */
static void check_from_description(void)
{
	xvt_sith_set_from_description(2);
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_LORD);
	xvt_sith_set_from_description(1);
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_APPRENTICES);
	xvt_sith_set_from_description(2);
	xvt_sith_set_from_description(XVT_LAUNCH_SETTING_UNSET);
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_NONE);
	xvt_sith_set_from_description(0);
	XVT_ASSERT_INT_EQ(xvt_sith_get_setting(), XVT_SITH_NONE);
}

static void check_names(void)
{
	XVT_ASSERT_INT_EQ(strcmp(xvt_sith_setting_name(XVT_SITH_NONE), "none"),
			  0);
	XVT_ASSERT_INT_EQ(strcmp(xvt_sith_setting_name(XVT_SITH_APPRENTICES),
				 "apprentices"),
			  0);
	XVT_ASSERT_INT_EQ(strcmp(xvt_sith_setting_name(XVT_SITH_LORD), "lord"),
			  0);
	XVT_ASSERT_INT_EQ(strcmp(xvt_sith_setting_name(7), "none"), 0);
}

int main(void)
{
	check_none_at_start();
	check_values_kept();
	check_other_values_are_none();
	check_from_description();
	check_names();
	return 0;
}
