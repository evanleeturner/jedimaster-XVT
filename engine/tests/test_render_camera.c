/* Checks the render camera shadow (xvt_runtime/snapshot/render_camera.h) against the promises in its header.
 * The live Q15 camera rows are the recovered game's globals, which this file sets itself, or fills by
 * calling the recovered fview_build_camera_orient, which builds them and then calls xvt_render_camera_build as
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

struct angles {
	int16_t roll, pitch, yaw, angle_d, aim_x, aim_y;
};

static const struct angles k_views[] = {
	{0, 0, 0, 0, 0, 0},
	{0x1000, 0x0800, 0x2000, 0, 0, 0},
	{-0x2400, 0x3000, -0x6000, 0x0400, 0x0100, -0x0080},
	{0x7000, -0x1C00, 0x4A00, -0x0900, -0x0200, 0x0300},
};

/* Three made-up Q15 tags. None is a rotation, so the scaled tag can never be mistaken for a basis. */
static const int32_t k_tag1[9] = {1000, 2000, 3000, 4000, 5000,
				  6000, 7000, 8000, 9000};
static const int32_t k_tag2[9] = {-100, 200,  -300, 400, -500,
				  600,	-700, 800,  -900};
static const int32_t k_tag3[9] = {11, 22, 33, 44, 55, 66, 77, 88, 99};

static void set_live_rows(const int32_t q[9])
{
	g_cam_mat_r0_x = q[0];
	g_cam_mat_r0_y = q[1];
	g_cam_mat_r0_z = q[2];
	g_cam_mat_r1_x = q[3];
	g_cam_mat_r1_y = q[4];
	g_cam_mat_r1_z = q[5];
	g_cam_mat_r2_x = q[6];
	g_cam_mat_r2_y = q[7];
	g_cam_mat_r2_z = q[8];
}

static void get_live_rows(int32_t q[9])
{
	const int32_t rows[9] = {
		g_cam_mat_r0_x, g_cam_mat_r0_y, g_cam_mat_r0_z,
		g_cam_mat_r1_x, g_cam_mat_r1_y, g_cam_mat_r1_z,
		g_cam_mat_r2_x, g_cam_mat_r2_y, g_cam_mat_r2_z};
	for (int i = 0; i < 9; ++i) {
		q[i] = rows[i];
	}
}

/* Builds the precise basis for view with tag as the live Q15 rows. */
static void build_tagged(const struct angles *view, const int32_t tag[9])
{
	set_live_rows(tag);
	xvt_render_camera_build(view->roll, view->pitch, view->yaw,
				view->angle_d, view->aim_x, view->aim_y);
}

static void check_same_rows(const float actual[9], const float expected[9])
{
	for (int i = 0; i < 9; ++i) {
		XVT_ASSERT_CLOSE(actual[i], expected[i], 1e-7,
				 "the same basis written out twice");
	}
}

/* The rows are the live Q15 rows scaled by 1/32768. */
static void check_q15_rows(const float rows[9], const int32_t q[9])
{
	for (int i = 0; i < 9; ++i) {
		XVT_ASSERT_CLOSE(
			rows[i], q[i] / 32768.0, 1e-7,
			"a small integer over 32768 is exact in single precision");
	}
}

static int same_rows(const float a[9], const float b[9])
{
	for (int i = 0; i < 9; ++i) {
		if (a[i] != b[i]) {
			return 0;
		}
	}
	return 1;
}

/* After the recovered game builds a camera, the rows are a double-precision mirror of its Q15 rows: each
 * row points where the matching Q15 row points. */
static void check_mirrors_game_camera(void)
{
	for (size_t v = 0; v < sizeof k_views / sizeof k_views[0]; ++v) {
		const struct angles *view = &k_views[v];
		fview_build_camera_orient(view->roll, view->pitch, view->yaw,
					  view->angle_d, view->aim_x,
					  view->aim_y, NULL);
		int32_t q[9];
		float rows[9];
		get_live_rows(q);
		xvt_render_camera_copy_rows(rows);
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
			XVT_ASSERT_CLOSE(dot / sqrt(precise * fixed), 1.0, 1e-5,
					 "the Q15 rounding of one camera row");
		}
	}
}

/* The basis comes from the angles alone; the Q15 rows only tag it. While the live rows equal the tag the
 * basis is written; once they differ, the live rows scaled by 1/32768 are; when they match again, the basis
 * is back. */
static void check_tag_chooses_source(void)
{
	const struct angles *view = &k_views[2];
	float game[9], tagged[9], rows[9];
	fview_build_camera_orient(view->roll, view->pitch, view->yaw,
				  view->angle_d, view->aim_x, view->aim_y,
				  NULL);
	xvt_render_camera_copy_rows(game);

	build_tagged(view, k_tag1);
	xvt_render_camera_copy_rows(tagged);
	check_same_rows(tagged, game);

	int32_t moved[9];
	for (int i = 0; i < 9; ++i) {
		moved[i] = k_tag1[i];
	}
	moved[4] += 1;
	set_live_rows(moved);
	xvt_render_camera_copy_rows(rows);
	check_q15_rows(rows, moved);

	set_live_rows(k_tag1);
	xvt_render_camera_copy_rows(rows);
	check_same_rows(rows, game);
}

/* Restore brings the saved copy back, and keeps its basis when the live rows still
 * match its tag. */
static void check_restore_keeps_matching_basis(void)
{
	float first[9], second[9], rows[9];
	build_tagged(&k_views[1], k_tag1);
	xvt_render_camera_copy_rows(first);
	xvt_render_camera_save_viewport();
	build_tagged(&k_views[2], k_tag2);
	xvt_render_camera_copy_rows(second);
	XVT_ASSERT_TRUE(!same_rows(first, second));

	set_live_rows(k_tag1);
	xvt_render_camera_restore_viewport();
	xvt_render_camera_copy_rows(rows);
	check_same_rows(rows, first);
}

/* Restore drops the copy's basis when the live rows no longer match its tag: the
 * live rows are written, and stay written even after the rows come back to the old tag. */
static void check_restore_drops_stale_basis(void)
{
	float rows[9];
	build_tagged(&k_views[1], k_tag1);
	xvt_render_camera_save_viewport();
	build_tagged(&k_views[2], k_tag2);

	xvt_render_camera_restore_viewport();
	xvt_render_camera_copy_rows(rows);
	check_q15_rows(rows, k_tag2);

	set_live_rows(k_tag1);
	xvt_render_camera_copy_rows(rows);
	check_q15_rows(rows, k_tag1);
}

/* A second Save overwrites the first. */
static void check_second_save_overwrites(void)
{
	float second[9], rows[9];
	build_tagged(&k_views[1], k_tag1);
	xvt_render_camera_save_viewport();
	build_tagged(&k_views[2], k_tag2);
	xvt_render_camera_copy_rows(second);
	xvt_render_camera_save_viewport();
	build_tagged(&k_views[3], k_tag3);

	set_live_rows(k_tag2);
	xvt_render_camera_restore_viewport();
	xvt_render_camera_copy_rows(rows);
	check_same_rows(rows, second);

	/* The first copy is gone: its tag no longer brings a basis back. */
	set_live_rows(k_tag1);
	xvt_render_camera_restore_viewport();
	xvt_render_camera_copy_rows(rows);
	check_q15_rows(rows, k_tag1);
}

int main(void)
{
	check_mirrors_game_camera();
	check_tag_chooses_source();
	check_restore_keeps_matching_basis();
	check_restore_drops_stale_basis();
	check_second_save_overwrites();
	return 0;
}
