/* Checks the flight loading steps in flight_loading.c against their promises in
 * xvt_runtime/runtime/flight_internal.h, for the two that need no game files or display: Reset and
 * MissionSetup. No game data is read: the test sets the globals they touch itself, with the flight drawing
 * to its own staging buffer rather than the frontend's surface.
 *
 * Not checked here: Globals loads the AI plans from the retail game files, Palette loads the flight palette
 * and the mission's .pal file from them and configures the display, and Runtime initializes the mission
 * runtime of a loaded mission and starts the music CD; they need the retail game files and a running
 * flight's display. */
#include "test_assert.h"
#include "xvt_runtime/runtime/flight_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void Surface(void) {
	g_flightRenderToFrontend = 0;
	g_flightPageFlip = 0;
	g_surfaceLockCount = 0;
}

static void CheckReset(void) {
	/* Handles and pool pointers are forgotten, not freed: the test frees the memory itself afterwards. */
	void* objects = malloc(64);
	void* mobiles = malloc(64);
	void* charData = malloc(64);
	void* craft = malloc(64);
	void* guidance = malloc(64);
	XVT_ASSERT_TRUE(objects && mobiles && charData && craft && guidance);
	g_objectTable = objects;
	g_mobileObjectPoolBase = mobiles;
	g_mobileObjectCharDataPool = charData;
	g_craftDataPoolBase = craft;
	g_projectileGuidanceStates = guidance;
	g_objectTableHandle = 11;
	g_mobileObjectPoolHandle = 12;
	g_mobileObjectCharDataHandle = 13;
	g_craftDataPoolHandle = 14;
	g_warheadGuidancePoolHandle = 15;
	g_stringDataHandle = 16;
	g_renderObjectListHandle = 17;
	g_messageLogHandle = 18;

	XvtFlightLoading_Reset();
	XVT_ASSERT_TRUE(g_objectTable == NULL);
	XVT_ASSERT_TRUE(g_mobileObjectPoolBase == NULL);
	XVT_ASSERT_TRUE(g_mobileObjectCharDataPool == NULL);
	XVT_ASSERT_TRUE(g_craftDataPoolBase == NULL);
	XVT_ASSERT_TRUE(g_projectileGuidanceStates == NULL);
	XVT_ASSERT_INT_EQ(g_objectTableHandle, 0);
	XVT_ASSERT_INT_EQ(g_mobileObjectPoolHandle, 0);
	XVT_ASSERT_INT_EQ(g_mobileObjectCharDataHandle, 0);
	XVT_ASSERT_INT_EQ(g_craftDataPoolHandle, 0);
	XVT_ASSERT_INT_EQ(g_warheadGuidancePoolHandle, 0);
	XVT_ASSERT_INT_EQ(g_stringDataHandle, 0);
	XVT_ASSERT_INT_EQ(g_renderObjectListHandle, 0);
	XVT_ASSERT_INT_EQ(g_messageLogHandle, 0);
	/* The sanitizer reports a double free here if Reset freed any of them. */
	free(objects);
	free(mobiles);
	free(charData);
	free(craft);
	free(guidance);
}

static void CheckProvingGrounds(void) {
	/* Craft 2, level 4 for traincourse... */
	Surface();
	g_flightConfTrainCourse = 1;
	g_flightMissionState.provingGroundsCraftType = 0;
	g_flightMissionState.provingGroundsLevel = 0;
	XvtFlightLoading_MissionSetup();
	XVT_ASSERT_INT_EQ(g_flightMissionState.provingGroundsCraftType, 2);
	XVT_ASSERT_INT_EQ(g_flightMissionState.provingGroundsLevel, 4);
	/* ...else none. */
	g_flightConfTrainCourse = 0;
	XvtFlightLoading_MissionSetup();
	XVT_ASSERT_INT_EQ(g_flightMissionState.provingGroundsCraftType, 0);
	XVT_ASSERT_INT_EQ(g_flightMissionState.provingGroundsLevel, 0);
	XVT_ASSERT_INT_EQ(g_surfaceLockCount, 0);
}

static void CheckResets(void) {
	/* The flight input, the message log and the MFD pages are reset. */
	Surface();
	g_flightConfTrainCourse = 0;
	g_keyMods = 3;
	g_mouseButtons = 1;
	g_messageLogTotalCount = 9;
	g_mfdActivePage = 2;
	g_mfdSecondaryPage = 3;
	for (int i = 0; i < MFD_PAGE_COUNT; ++i)
		g_mfdPageStates[i] = 1;
	XvtFlightLoading_MissionSetup();
	XVT_ASSERT_INT_EQ(g_keyMods, 0);
	XVT_ASSERT_INT_EQ(g_mouseButtons, 0);
	XVT_ASSERT_INT_EQ(g_messageLogTotalCount, 0);
	XVT_ASSERT_INT_EQ(g_mfdActivePage, MFD_PAGE_NONE);
	XVT_ASSERT_INT_EQ(g_mfdSecondaryPage, MFD_PAGE_NONE);
	for (int i = 0; i < MFD_PAGE_COUNT; ++i)
		XVT_ASSERT_INT_EQ(g_mfdPageStates[i], MFD_PAGE_STATE_CLOSED);
}

static void CheckNoiseTable(void) {
	/* The noise table is refilled from rand(): the same seed gives the same table whatever it held before,
	 * and another seed another table. */
	static uint8_t first[sizeof g_flightNoiseTable];
	Surface();
	srand(7);
	XvtFlightLoading_MissionSetup();
	memcpy(first, g_flightNoiseTable, sizeof first);
	memset(g_flightNoiseTable, 0xA5, sizeof g_flightNoiseTable);
	srand(7);
	XvtFlightLoading_MissionSetup();
	XVT_ASSERT_INT_EQ(memcmp(first, g_flightNoiseTable, sizeof first), 0);
	srand(8);
	XvtFlightLoading_MissionSetup();
	XVT_ASSERT_TRUE(memcmp(first, g_flightNoiseTable, sizeof first) != 0);
}

int main(void) {
	CheckReset();
	CheckProvingGrounds();
	CheckResets();
	CheckNoiseTable();
	return 0;
}
