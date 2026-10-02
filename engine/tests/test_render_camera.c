/* Checks the render camera shadow (xvt_runtime/snapshot/render_camera.h) against the promises in its header.
 * The live Q15 camera rows are the recovered game's globals, which this file sets itself, or fills by
 * calling the recovered FVIEW_BuildCameraOrient, which builds them and then calls XvtRenderCamera_Build as
 * the game does. No game data is read.
 *
 * The double-precision basis is compared with the Q15 rows by the angle between matching rows: a mirror of
 * the same camera points each row the same way, within the Q15 rounding. */
#include "test_assert.h"
#include "xvt/flight/fview.h"
#include "xvt/flight/transfm2.h"
#include "xvt_runtime/snapshot/render_camera.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Angles {
	int16_t roll, pitch, yaw, angle_d, aim_x, aim_y;
} Angles;

static const Angles kViews[] = {
	{ 0, 0, 0, 0, 0, 0 },
	{ 0x1000, 0x0800, 0x2000, 0, 0, 0 },
	{ -0x2400, 0x3000, -0x6000, 0x0400, 0x0100, -0x0080 },
	{ 0x7000, -0x1C00, 0x4A00, -0x0900, -0x0200, 0x0300 },
};

/* Three made-up Q15 tags. None is a rotation, so the scaled tag can never be mistaken for a basis. */
static const int32_t kTag1[9] = { 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000 };
static const int32_t kTag2[9] = { -100, 200, -300, 400, -500, 600, -700, 800, -900 };
static const int32_t kTag3[9] = { 11, 22, 33, 44, 55, 66, 77, 88, 99 };

static void SetLiveRows(const int32_t q[9]) {
	g_camMatR0_X = q[0];
	g_camMatR0_Y = q[1];
	g_camMatR0_Z = q[2];
	g_camMatR1_X = q[3];
	g_camMatR1_Y = q[4];
	g_camMatR1_Z = q[5];
	g_camMatR2_X = q[6];
	g_camMatR2_Y = q[7];
	g_camMatR2_Z = q[8];
}

static void GetLiveRows(int32_t q[9]) {
	const int32_t rows[9] = { g_camMatR0_X, g_camMatR0_Y, g_camMatR0_Z, g_camMatR1_X, g_camMatR1_Y,
							  g_camMatR1_Z, g_camMatR2_X, g_camMatR2_Y, g_camMatR2_Z };
	for (int i = 0; i < 9; ++i)
		q[i] = rows[i];
}

/* Builds the precise basis for view with tag as the live Q15 rows. */
static void BuildTagged(const Angles* view, const int32_t tag[9]) {
	SetLiveRows(tag);
	XvtRenderCamera_Build(view->roll, view->pitch, view->yaw, view->angle_d, view->aim_x, view->aim_y);
}

static void CheckSameRows(const float actual[9], const float expected[9]) {
	for (int i = 0; i < 9; ++i)
		XVT_ASSERT_CLOSE(actual[i], expected[i], 1e-7, "the same basis written out twice");
}

/* The rows are the live Q15 rows scaled by 1/32768. */
static void CheckQ15Rows(const float rows[9], const int32_t q[9]) {
	for (int i = 0; i < 9; ++i)
		XVT_ASSERT_CLOSE(rows[i], q[i] / 32768.0, 1e-7,
						 "a small integer over 32768 is exact in single precision");
}

static int SameRows(const float a[9], const float b[9]) {
	for (int i = 0; i < 9; ++i)
		if (a[i] != b[i])
			return 0;
	return 1;
}

/* After the recovered game builds a camera, the rows are a double-precision mirror of its Q15 rows: each
 * row points where the matching Q15 row points. */
static void CheckMirrorsGameCamera(void) {
	for (size_t v = 0; v < sizeof kViews / sizeof kViews[0]; ++v) {
		const Angles* view = &kViews[v];
		FVIEW_BuildCameraOrient(view->roll, view->pitch, view->yaw, view->angle_d, view->aim_x, view->aim_y,
								NULL);
		int32_t q[9];
		float rows[9];
		GetLiveRows(q);
		XvtRenderCamera_CopyRows(rows);
		for (int r = 0; r < 9; r += 3) {
			double dot = 0, precise = 0, fixed = 0;
			for (int c = 0; c < 3; ++c) {
				dot += (double)rows[r + c] * q[r + c];
				precise += (double)rows[r + c] * rows[r + c];
				fixed += (double)q[r + c] * q[r + c];
			}
			XVT_ASSERT_TRUE(precise > 0 && fixed > 0);
			/* Q15 rounding turns a row by a few 1e-4 radians at most, which moves the cosine by under 1e-7
			 * (2.2e-8 at worst over these views); a wrong axis or sign, or an angle off by half a degree,
			 * moves it by more than 1e-5. */
			XVT_ASSERT_CLOSE(dot / sqrt(precise * fixed), 1.0, 1e-5, "the Q15 rounding of one camera row");
		}
	}
}

/* The basis comes from the angles alone; the Q15 rows only tag it. While the live rows equal the tag the
 * basis is written; once they differ, the live rows scaled by 1/32768 are; when they match again, the basis
 * is back. */
static void CheckTagChoosesSource(void) {
	const Angles* view = &kViews[2];
	float game[9], tagged[9], rows[9];
	FVIEW_BuildCameraOrient(view->roll, view->pitch, view->yaw, view->angle_d, view->aim_x, view->aim_y,
							NULL);
	XvtRenderCamera_CopyRows(game);

	BuildTagged(view, kTag1);
	XvtRenderCamera_CopyRows(tagged);
	CheckSameRows(tagged, game);

	int32_t moved[9];
	for (int i = 0; i < 9; ++i)
		moved[i] = kTag1[i];
	moved[4] += 1;
	SetLiveRows(moved);
	XvtRenderCamera_CopyRows(rows);
	CheckQ15Rows(rows, moved);

	SetLiveRows(kTag1);
	XvtRenderCamera_CopyRows(rows);
	CheckSameRows(rows, game);
}

/* Restore brings the saved copy back, and keeps its basis when the live rows still
 * match its tag. */
static void CheckRestoreKeepsMatchingBasis(void) {
	float first[9], second[9], rows[9];
	BuildTagged(&kViews[1], kTag1);
	XvtRenderCamera_CopyRows(first);
	XvtRenderCamera_SaveViewport();
	BuildTagged(&kViews[2], kTag2);
	XvtRenderCamera_CopyRows(second);
	XVT_ASSERT_TRUE(!SameRows(first, second));

	SetLiveRows(kTag1);
	XvtRenderCamera_RestoreViewport();
	XvtRenderCamera_CopyRows(rows);
	CheckSameRows(rows, first);
}

/* Restore drops the copy's basis when the live rows no longer match its tag: the
 * live rows are written, and stay written even after the rows come back to the old tag. */
static void CheckRestoreDropsStaleBasis(void) {
	float rows[9];
	BuildTagged(&kViews[1], kTag1);
	XvtRenderCamera_SaveViewport();
	BuildTagged(&kViews[2], kTag2);

	XvtRenderCamera_RestoreViewport();
	XvtRenderCamera_CopyRows(rows);
	CheckQ15Rows(rows, kTag2);

	SetLiveRows(kTag1);
	XvtRenderCamera_CopyRows(rows);
	CheckQ15Rows(rows, kTag1);
}

/* A second Save overwrites the first. */
static void CheckSecondSaveOverwrites(void) {
	float second[9], rows[9];
	BuildTagged(&kViews[1], kTag1);
	XvtRenderCamera_SaveViewport();
	BuildTagged(&kViews[2], kTag2);
	XvtRenderCamera_CopyRows(second);
	XvtRenderCamera_SaveViewport();
	BuildTagged(&kViews[3], kTag3);

	SetLiveRows(kTag2);
	XvtRenderCamera_RestoreViewport();
	XvtRenderCamera_CopyRows(rows);
	CheckSameRows(rows, second);

	/* The first copy is gone: its tag no longer brings a basis back. */
	SetLiveRows(kTag1);
	XvtRenderCamera_RestoreViewport();
	XvtRenderCamera_CopyRows(rows);
	CheckQ15Rows(rows, kTag1);
}

int main(void) {
	CheckMirrorsGameCamera();
	CheckTagChoosesSource();
	CheckRestoreKeepsMatchingBasis();
	CheckRestoreDropsStaleBasis();
	CheckSecondSaveOverwrites();
	return 0;
}
