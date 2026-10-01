/* Checks the input action table (xvt_runtime/input/actions.h) against the promises in its header: every
 * action has a settings name that leads back to it, a label and a category; names match exactly; and each
 * lookup gives its stated fallback for an action or category out of range. The table has no state. */
#include "test_assert.h"
#include "xvt_runtime/input/actions.h"

#include <string.h>

/* Values just outside each enum's range, on both sides. */
static const int kOutOfRange[] = { XVT_INPUT_ACTION_COUNT, XVT_INPUT_ACTION_COUNT + 7, -1 };

static void CheckNamesLeadBack(void) {
	for (int i = 0; i < XVT_INPUT_ACTION_COUNT; ++i) {
		XvtInputAction action = (XvtInputAction)i;
		const char* name = XvtInputActions_ToName(action);
		XVT_ASSERT_TRUE(name != NULL && name[0] != '\0');
		XVT_ASSERT_INT_EQ(XvtInputActions_FromName(name), action);
	}
}

static void CheckNamesMatchExactly(void) {
	XVT_ASSERT_INT_EQ(XvtInputActions_FromName(NULL), XVT_INPUT_ACTION_NONE);
	XVT_ASSERT_INT_EQ(XvtInputActions_FromName(""), XVT_INPUT_ACTION_NONE);
	XVT_ASSERT_INT_EQ(XvtInputActions_FromName("no_such_action"), XVT_INPUT_ACTION_NONE);

	/* A name that differs from fire_weapon's by case, by a missing last letter or by a trailing space does
	 * not find fire_weapon. */
	const char* name = XvtInputActions_ToName(XVT_INPUT_ACTION_FIRE_WEAPON);
	char changed[64];
	size_t length = strlen(name);
	XVT_ASSERT_TRUE(length + 2 <= sizeof changed);

	memcpy(changed, name, length + 1);
	changed[0] = (char)(changed[0] >= 'a' && changed[0] <= 'z' ? changed[0] - 'a' + 'A' : changed[0] + 1);
	XVT_ASSERT_TRUE(XvtInputActions_FromName(changed) != XVT_INPUT_ACTION_FIRE_WEAPON);

	memcpy(changed, name, length + 1);
	changed[length - 1] = '\0';
	XVT_ASSERT_TRUE(XvtInputActions_FromName(changed) != XVT_INPUT_ACTION_FIRE_WEAPON);

	memcpy(changed, name, length);
	changed[length] = ' ';
	changed[length + 1] = '\0';
	XVT_ASSERT_TRUE(XvtInputActions_FromName(changed) != XVT_INPUT_ACTION_FIRE_WEAPON);
}

static void CheckLabelsAndCategories(void) {
	for (int i = 0; i < XVT_INPUT_ACTION_COUNT; ++i) {
		XvtInputAction action = (XvtInputAction)i;
		const char* label = XvtInputActions_DisplayName(action);
		XVT_ASSERT_TRUE(label != NULL && label[0] != '\0');
		XvtInputActionCategory category = XvtInputActions_Category(action);
		XVT_ASSERT_TRUE((unsigned)category < XVT_INPUT_ACTION_CATEGORY_COUNT);
	}
	for (int i = 0; i < XVT_INPUT_ACTION_CATEGORY_COUNT; ++i) {
		const char* label = XvtInputActions_CategoryName((XvtInputActionCategory)i);
		XVT_ASSERT_TRUE(label != NULL && label[0] != '\0');
	}
}

static void CheckOutOfRange(void) {
	for (size_t i = 0; i < sizeof kOutOfRange / sizeof kOutOfRange[0]; ++i) {
		XvtInputAction action = (XvtInputAction)kOutOfRange[i];
		XVT_ASSERT_INT_EQ(strcmp(XvtInputActions_ToName(action), "none"), 0);
		XVT_ASSERT_INT_EQ(strcmp(XvtInputActions_DisplayName(action), "None"), 0);
		XVT_ASSERT_INT_EQ(XvtInputActions_Category(action), XVT_INPUT_ACTION_CATEGORY_SYSTEM);
		XVT_ASSERT_INT_EQ(XvtInputActions_Key(action), 0);
	}
	XVT_ASSERT_INT_EQ(strcmp(XvtInputActions_CategoryName(XVT_INPUT_ACTION_CATEGORY_COUNT), ""), 0);
	XVT_ASSERT_INT_EQ(strcmp(XvtInputActions_CategoryName((XvtInputActionCategory)-1), ""), 0);
}

static void CheckKeyboardBindable(void) {
	for (int i = 0; i < XVT_INPUT_ACTION_COUNT; ++i) {
		XvtInputAction action = (XvtInputAction)i;
		bool excluded = action == XVT_INPUT_ACTION_NONE || action == XVT_INPUT_ACTION_CHAT_SEND ||
						action == XVT_INPUT_ACTION_CHAT_CANCEL;
		XVT_ASSERT_INT_EQ(XvtInputActions_KeyboardBindable(action), !excluded);
	}
}

int main(void) {
	CheckNamesLeadBack();
	CheckNamesMatchExactly();
	CheckLabelsAndCategories();
	CheckOutOfRange();
	CheckKeyboardBindable();
	return 0;
}
