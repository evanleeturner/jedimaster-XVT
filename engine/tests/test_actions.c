/* Checks the input action table (xvt_runtime/input/actions.h) against the
 * promises in its header: every action has a settings name that leads back to
 * it, a label and a category; names match exactly; and each lookup gives its
 * stated fallback for an action or category out of range. The table has no
 * state. */
#include <string.h>

#include "test_assert.h"
#include "xvt_runtime/input/actions.h"

/* Values just outside each enum's range, on both sides. */
static const int k_out_of_range[] = {XVT_INPUT_ACTION_COUNT,
				     XVT_INPUT_ACTION_COUNT + 7, -1};

static void check_names_lead_back(void)
{
	for (int i = 0; i < XVT_INPUT_ACTION_COUNT; ++i) {
		xvt_input_action action = (xvt_input_action)i;
		const char *name = xvt_input_actions_to_name(action);
		XVT_ASSERT_TRUE(name != NULL && name[0] != '\0');
		XVT_ASSERT_INT_EQ(xvt_input_actions_from_name(name), action);
	}
}

static void check_names_match_exactly(void)
{
	XVT_ASSERT_INT_EQ(xvt_input_actions_from_name(NULL),
			  XVT_INPUT_ACTION_NONE);
	XVT_ASSERT_INT_EQ(xvt_input_actions_from_name(""),
			  XVT_INPUT_ACTION_NONE);
	XVT_ASSERT_INT_EQ(xvt_input_actions_from_name("no_such_action"),
			  XVT_INPUT_ACTION_NONE);

	/* A name that differs from fire_weapon's by case, by a missing last
	 * letter or by a trailing space does not find fire_weapon. */
	const char *name =
		xvt_input_actions_to_name(XVT_INPUT_ACTION_FIRE_WEAPON);
	char changed[64];
	size_t length = strlen(name);
	XVT_ASSERT_TRUE(length + 2 <= sizeof changed);

	memcpy(changed, name, length + 1);
	changed[0] = (char)(changed[0] >= 'a' && changed[0] <= 'z'
				    ? changed[0] - 'a' + 'A'
				    : changed[0] + 1);
	XVT_ASSERT_TRUE(xvt_input_actions_from_name(changed) !=
			XVT_INPUT_ACTION_FIRE_WEAPON);

	memcpy(changed, name, length + 1);
	changed[length - 1] = '\0';
	XVT_ASSERT_TRUE(xvt_input_actions_from_name(changed) !=
			XVT_INPUT_ACTION_FIRE_WEAPON);

	memcpy(changed, name, length);
	changed[length] = ' ';
	changed[length + 1] = '\0';
	XVT_ASSERT_TRUE(xvt_input_actions_from_name(changed) !=
			XVT_INPUT_ACTION_FIRE_WEAPON);
}

static void check_labels_and_categories(void)
{
	for (int i = 0; i < XVT_INPUT_ACTION_COUNT; ++i) {
		xvt_input_action action = (xvt_input_action)i;
		const char *label = xvt_input_actions_display_name(action);
		XVT_ASSERT_TRUE(label != NULL && label[0] != '\0');
		xvt_input_action_category category =
			xvt_input_actions_category(action);
		XVT_ASSERT_TRUE((unsigned)category <
				XVT_INPUT_ACTION_CATEGORY_COUNT);
	}
	for (int i = 0; i < XVT_INPUT_ACTION_CATEGORY_COUNT; ++i) {
		const char *label = xvt_input_actions_category_name(
			(xvt_input_action_category)i);
		XVT_ASSERT_TRUE(label != NULL && label[0] != '\0');
	}
}

static void check_out_of_range(void)
{
	for (size_t i = 0; i < sizeof k_out_of_range / sizeof k_out_of_range[0];
	     ++i) {
		xvt_input_action action = (xvt_input_action)k_out_of_range[i];
		XVT_ASSERT_INT_EQ(
			strcmp(xvt_input_actions_to_name(action), "none"), 0);
		XVT_ASSERT_INT_EQ(
			strcmp(xvt_input_actions_display_name(action), "None"),
			0);
		XVT_ASSERT_INT_EQ(xvt_input_actions_category(action),
				  XVT_INPUT_ACTION_CATEGORY_SYSTEM);
		XVT_ASSERT_INT_EQ(xvt_input_actions_key(action), 0);
	}
	XVT_ASSERT_INT_EQ(strcmp(xvt_input_actions_category_name(
					 XVT_INPUT_ACTION_CATEGORY_COUNT),
				 ""),
			  0);
	XVT_ASSERT_INT_EQ(strcmp(xvt_input_actions_category_name(
					 (xvt_input_action_category)-1),
				 ""),
			  0);
}

static void check_keyboard_bindable(void)
{
	for (int i = 0; i < XVT_INPUT_ACTION_COUNT; ++i) {
		xvt_input_action action = (xvt_input_action)i;
		bool excluded = action == XVT_INPUT_ACTION_NONE ||
				action == XVT_INPUT_ACTION_CHAT_SEND ||
				action == XVT_INPUT_ACTION_CHAT_CANCEL;
		XVT_ASSERT_INT_EQ(xvt_input_actions_keyboard_bindable(action),
				  !excluded);
	}
}

int main(void)
{
	check_names_lead_back();
	check_names_match_exactly();
	check_labels_and_categories();
	check_out_of_range();
	check_keyboard_bindable();
	return 0;
}
