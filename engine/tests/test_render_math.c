/* Checks the remaster's render math (xvt_remaster/render_math.h) against the
 * promises in its header, on camera records, objects and snapshots this file
 * builds itself; no game data is read.
 *
 * The header does not say which camera row is the view axis, so the projection
 * checks find it the way the header defines depth: depth is the distance along
 * the view axis, so its change over a step along each world axis gives that
 * axis. Floating-point results compare within a relative tolerance: each is a
 * handful of single-precision operations, about 6e-8 relative error apiece, so
 * 1e-5 is loose for correct code and tight for a wrong formula. */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "aeron/asset/opt_model.h"
#include "aeron/scene/scene3d.h"
#include "test_assert.h"
#include "xvt_remaster/component_animation.h"
#include "xvt_remaster/render_math.h"

#define WHY_FLOAT "a few single-precision operations"

static const double k_tol = 1e-5;

/* A camera record at world (100, -200, 300) with orthonormal rows, a 320 x 200 viewport whose
 * projection center is its middle, a focal length of 2^8 and no aspect scaling. */
static struct xvt_snap_camera clean_camera(void)
{
	struct xvt_snap_camera camera;
	memset(&camera, 0, sizeof camera);
	camera.valid = 1;
	camera.world_pos[0] = 100;
	camera.world_pos[1] = -200;
	camera.world_pos[2] = 300;
	/* Rows (0, 1, 0), (0, 0, 1) and their cross product (1, 0, 0). */
	camera.rows[1] = 1.0f;
	camera.rows[5] = 1.0f;
	camera.rows[6] = 1.0f;
	camera.viewport.width = 320;
	camera.viewport.height = 200;
	camera.center_x = 160;
	camera.center_y = 100;
	camera.screen_width = 320;
	camera.screen_height = 200;
	camera.perspective_shift = 8;
	return camera;
}

static const int32_t k_origin[3] = {90, -190, 310};

static void check_layout(void)
{
	struct xvt_layout_transform layout;
	XVT_ASSERT_INT_EQ(xvt_render_math_layout(640, 480, 1280, 1024, &layout),
			  1);
	XVT_ASSERT_CLOSE(layout.scale, 2.0, k_tol, WHY_FLOAT);
	XVT_ASSERT_CLOSE(layout.source_width, 640, k_tol, "the input itself");
	XVT_ASSERT_CLOSE(layout.source_height, 480, k_tol, "the input itself");
	XVT_ASSERT_CLOSE(layout.target_width, 1280, k_tol, "the input itself");
	XVT_ASSERT_CLOSE(layout.target_height, 1024, k_tol, "the input itself");

	/* The smaller ratio: the scaled source fits on both axes and fills one. */
	static const float sizes[][4] = {{320, 200, 1920, 1080},
					 {640, 480, 800, 1200},
					 {100, 100, 37, 53}};
	for (unsigned i = 0; i < 3; ++i) {
		const float *s = sizes[i];
		XVT_ASSERT_INT_EQ(
			xvt_render_math_layout(s[0], s[1], s[2], s[3], &layout),
			1);
		XVT_ASSERT_CLOSE(layout.scale, fmin(s[2] / s[0], s[3] / s[1]),
				 k_tol, WHY_FLOAT);
		XVT_ASSERT_TRUE(s[0] * layout.scale <= s[2] * (1 + k_tol));
		XVT_ASSERT_TRUE(s[1] * layout.scale <= s[3] * (1 + k_tol));
	}

	/* Refused, out untouched: a NULL out, and any size not positive or not finite. */
	XVT_ASSERT_INT_EQ(xvt_render_math_layout(640, 480, 1280, 1024, NULL),
			  0);
	const float bad[] = {0.0f, -1.0f, NAN, INFINITY};
	for (unsigned which = 0; which < 4; ++which) {
		for (unsigned b = 0; b < 4; ++b) {
			float args[4] = {640, 480, 1280, 1024};
			args[which] = bad[b];
			struct xvt_layout_transform before;
			memset(&before, 0xA5, sizeof before);
			struct xvt_layout_transform after = before;
			XVT_ASSERT_INT_EQ(xvt_render_math_layout(
						  args[0], args[1], args[2],
						  args[3], &after),
					  0);
			XVT_ASSERT_INT_EQ(
				memcmp(&before, &after, sizeof before), 0);
		}
	}
}

static void check_layout_points(void)
{
	/* A 4:3 surface in a 16:9 window: scale 2, 320 units left over across, none down. */
	struct xvt_layout_transform layout;
	XVT_ASSERT_INT_EQ(xvt_render_math_layout(640, 480, 1600, 960, &layout),
			  1);
	float x;
	float y;

	/* Anchor 0 aligns the near edges, 1 the far edges, 0.5 the centers. */
	xvt_render_math_layout_point(&layout, 0, 0, 0, 0, &x, &y);
	XVT_ASSERT_CLOSE(x, 0, k_tol, WHY_FLOAT);
	XVT_ASSERT_CLOSE(y, 0, k_tol, WHY_FLOAT);
	xvt_render_math_layout_point(&layout, 1, 1, 640, 480, &x, &y);
	XVT_ASSERT_CLOSE(x, 1600, k_tol, WHY_FLOAT);
	XVT_ASSERT_CLOSE(y, 960, k_tol, WHY_FLOAT);
	xvt_render_math_layout_point(&layout, 0.5f, 0.5f, 320, 240, &x, &y);
	XVT_ASSERT_CLOSE(x, 800, k_tol, WHY_FLOAT);
	XVT_ASSERT_CLOSE(y, 480, k_tol, WHY_FLOAT);

	xvt_render_math_layout_point(&layout, 0.5f, 1, 10, 20, &x, &y);
	/* Scaled: two points map to points the scale times as far apart. */
	float x2;
	float y2;
	xvt_render_math_layout_point(&layout, 0.5f, 1, 110, 70, &x2, &y2);
	XVT_ASSERT_CLOSE(x2 - x, 100 * layout.scale, k_tol, WHY_FLOAT);
	XVT_ASSERT_CLOSE(y2 - y, 50 * layout.scale, k_tol, WHY_FLOAT);

	/* LayoutInverse undoes LayoutPoint with the same anchors, both ways round. */
	static const float anchors[][2] = {
		{0, 0}, {0.5f, 0.5f}, {1, 0}, {0.25f, 1}};
	XVT_ASSERT_INT_EQ(xvt_render_math_layout(320, 200, 1000, 1000, &layout),
			  1);
	for (unsigned i = 0; i < 4; ++i) {
		float ax = anchors[i][0];
		float ay = anchors[i][1];
		xvt_render_math_layout_point(&layout, ax, ay, 123.5f, 77.25f,
					     &x, &y);
		float back_x;
		float back_y;
		xvt_render_math_layout_inverse(&layout, ax, ay, x, y, &back_x,
					       &back_y);
		XVT_ASSERT_CLOSE(back_x, 123.5, k_tol, WHY_FLOAT);
		XVT_ASSERT_CLOSE(back_y, 77.25, k_tol, WHY_FLOAT);
		xvt_render_math_layout_inverse(&layout, ax, ay, 900, 40, &x,
					       &y);
		xvt_render_math_layout_point(&layout, ax, ay, x, y, &back_x,
					     &back_y);
		XVT_ASSERT_CLOSE(back_x, 900, k_tol, WHY_FLOAT);
		XVT_ASSERT_CLOSE(back_y, 40, k_tol, WHY_FLOAT);
	}
}

/* Calls BuildView on a view filled with a marker and checks it is refused with
 * the view untouched. */
static void expect_refused_untouched(const struct xvt_snap_camera *camera,
				     const int32_t *origin, int width,
				     int height)
{
	struct xvt_render_view before;
	memset(&before, 0xA5, sizeof before);
	struct xvt_render_view after = before;
	XVT_ASSERT_INT_EQ(xvt_render_math_build_view(camera, origin, width,
						     height, &after),
			  0);
	XVT_ASSERT_INT_EQ(memcmp(&before, &after, sizeof before), 0);
	after = before;
	XVT_ASSERT_INT_EQ(xvt_render_math_build_main_view(camera, origin, width,
							  height, &after),
			  0);
	XVT_ASSERT_INT_EQ(memcmp(&before, &after, sizeof before), 0);
}

/* Calls BuildView with a bad row and checks it is refused with the view zeroed. */
static void expect_refused_zeroed(const struct xvt_snap_camera *camera)
{
	struct xvt_render_view view;
	memset(&view, 0xA5, sizeof view);
	struct xvt_render_view zero;
	memset(&zero, 0, sizeof zero);
	XVT_ASSERT_INT_EQ(
		xvt_render_math_build_view(camera, k_origin, 640, 400, &view),
		0);
	XVT_ASSERT_INT_EQ(memcmp(&view, &zero, sizeof view), 0);
}

static void check_build_view_refusals(void)
{
	struct xvt_snap_camera camera = clean_camera();
	struct xvt_render_view view;
	XVT_ASSERT_INT_EQ(
		xvt_render_math_build_view(&camera, k_origin, 640, 400, &view),
		1);
	XVT_ASSERT_INT_EQ(
		xvt_render_math_build_view(&camera, k_origin, 640, 400, NULL),
		0);
	XVT_ASSERT_INT_EQ(xvt_render_math_build_main_view(&camera, k_origin,
							  640, 400, NULL),
			  0);

	expect_refused_untouched(NULL, k_origin, 640, 400);
	expect_refused_untouched(&camera, NULL, 640, 400);
	expect_refused_untouched(&camera, k_origin, 0, 400);
	expect_refused_untouched(&camera, k_origin, 640, -1);
	camera.valid = 0;
	expect_refused_untouched(&camera, k_origin, 640, 400);
	camera = clean_camera();
	camera.viewport.width = 0;
	expect_refused_untouched(&camera, k_origin, 640, 400);
	camera = clean_camera();
	camera.viewport.height = -5;
	expect_refused_untouched(&camera, k_origin, 640, 400);

	/* A first row shorter than 1e-6 or not finite. */
	camera = clean_camera();
	camera.rows[1] = 0.0f;
	expect_refused_zeroed(&camera);
	camera.rows[1] = 5e-7f;
	expect_refused_zeroed(&camera);
	camera.rows[1] = NAN;
	expect_refused_zeroed(&camera);
	camera.rows[1] = INFINITY;
	expect_refused_zeroed(&camera);

	/* A second row with nothing left once made perpendicular to the first, or not finite. */
	camera = clean_camera();
	camera.rows[3] = 0.0f;
	camera.rows[4] = -2.5f;
	camera.rows[5] = 0.0f;
	expect_refused_zeroed(&camera);
	camera.rows[4] = 0.0f;
	expect_refused_zeroed(&camera);
	camera.rows[5] = NAN;
	expect_refused_zeroed(&camera);
}

static void check_build_view_fields(void)
{
	struct xvt_snap_camera camera = clean_camera();
	struct xvt_render_view view;
	XVT_ASSERT_INT_EQ(
		xvt_render_math_build_view(&camera, k_origin, 640, 600, &view),
		1);

	/* Position relative to the origin, which the view keeps. */
	XVT_ASSERT_CLOSE(view.camera.pos[0], 10, k_tol,
			 "a small integer difference");
	XVT_ASSERT_CLOSE(view.camera.pos[1], -10, k_tol,
			 "a small integer difference");
	XVT_ASSERT_CLOSE(view.camera.pos[2], -10, k_tol,
			 "a small integer difference");
	for (int i = 0; i < 3; ++i) {
		XVT_ASSERT_INT_EQ(view.origin_world[i], k_origin[i]);
	}

	/* The whole window, and window pixels per original pixel. */
	XVT_ASSERT_INT_EQ(view.camera.viewport.x, 0);
	XVT_ASSERT_INT_EQ(view.camera.viewport.y, 0);
	XVT_ASSERT_INT_EQ(view.camera.viewport.width, 640);
	XVT_ASSERT_INT_EQ(view.camera.viewport.height, 600);
	XVT_ASSERT_CLOSE(view.classic_pixel_scale, 600.0 / 200.0, k_tol,
			 WHY_FLOAT);

	/* Half the viewport height over the focal length 2^8; the horizontal
	 * angle from width:height. */
	XVT_ASSERT_CLOSE(tan(view.camera.v_half_rad), 100.0 / 256.0, k_tol,
			 WHY_FLOAT);
	XVT_ASSERT_CLOSE(tan(view.camera.h_half_rad),
			 tan(view.camera.v_half_rad) * 640.0 / 600.0, k_tol,
			 WHY_FLOAT);

	/* The Q16 aspect scales the focal length; 0 and 0xFFFF both count as 1. */
	camera.aspect_y_q16 = 0x8000;
	XVT_ASSERT_INT_EQ(
		xvt_render_math_build_view(&camera, k_origin, 640, 600, &view),
		1);
	XVT_ASSERT_CLOSE(tan(view.camera.v_half_rad), 100.0 / (256.0 * 0.5),
			 k_tol, WHY_FLOAT);
	camera.aspect_y_q16 = 0xFFFF;
	XVT_ASSERT_INT_EQ(
		xvt_render_math_build_view(&camera, k_origin, 640, 600, &view),
		1);
	XVT_ASSERT_CLOSE(tan(view.camera.v_half_rad), 100.0 / 256.0, k_tol,
			 WHY_FLOAT);
	camera.aspect_y_q16 = 0;
	camera.perspective_shift = 6;
	XVT_ASSERT_INT_EQ(
		xvt_render_math_build_view(&camera, k_origin, 640, 600, &view),
		1);
	XVT_ASSERT_CLOSE(tan(view.camera.v_half_rad), 100.0 / 64.0, k_tol,
			 WHY_FLOAT);

	/* The view-projection is the camera's, as AeronScene_ComputeViewProj computes it. */
	float expected[16];
	AeronScene_ComputeViewProj(&view.camera, expected);
	XVT_ASSERT_INT_EQ(memcmp(expected, view.view_proj, sizeof expected), 0);
}

static void check_build_view_rows(void)
{
	struct xvt_snap_camera clean = clean_camera();
	struct xvt_snap_camera messy = clean_camera();
	/* The first row scaled, the second leaning toward the first and scaled,
	 * the third anything. */
	messy.rows[1] = 3.0f;
	messy.rows[3] = 0.0f;
	messy.rows[4] = 0.5f;
	messy.rows[5] = 2.0f;
	messy.rows[6] = 7.0f;
	messy.rows[7] = -3.0f;
	messy.rows[8] = 2.0f;
	struct xvt_render_view a;
	XVT_ASSERT_INT_EQ(
		xvt_render_math_build_view(&clean, k_origin, 640, 400, &a), 1);
	struct xvt_render_view b;
	XVT_ASSERT_INT_EQ(
		xvt_render_math_build_view(&messy, k_origin, 640, 400, &b), 1);
	/* q and -q are the same rotation. */
	double dot = 0;
	for (int i = 0; i < 4; ++i) {
		dot += (double)a.camera.ori[i] * b.camera.ori[i];
	}
	XVT_ASSERT_CLOSE(fabs(dot), 1.0, k_tol, WHY_FLOAT);
}

static float depth(const struct xvt_render_view *view, int32_t x, int32_t y,
		   int32_t z)
{
	const int32_t world[3] = {x, y, z};
	float px;
	float py;
	float depth = NAN;
	xvt_render_math_project_world(view, world, &px, &py, &depth);
	return depth;
}

/* The view axis in world terms, from depth's change over 1000 units along each world axis. */
static void view_axis(const struct xvt_render_view *view, const int32_t at[3],
		      double axis[3])
{
	double base = depth(view, at[0], at[1], at[2]);
	axis[0] = (depth(view, at[0] + 1000, at[1], at[2]) - base) / 1000;
	axis[1] = (depth(view, at[0], at[1] + 1000, at[2]) - base) / 1000;
	axis[2] = (depth(view, at[0], at[1], at[2] + 1000) - base) / 1000;
}

/* The point d units from `at` along an axis-aligned direction. */
static void along(const int32_t at[3], const double axis[3], int32_t d,
		  int32_t out[3])
{
	for (int i = 0; i < 3; ++i) {
		out[i] = at[i] + (int32_t)lround(axis[i]) * d;
	}
}

static void check_project_world(void)
{
	struct xvt_snap_camera camera = clean_camera();
	struct xvt_render_view view;
	XVT_ASSERT_INT_EQ(
		xvt_render_math_build_view(&camera, k_origin, 640, 400, &view),
		1);
	double axis[3];
	view_axis(&view, camera.world_pos, axis);
	/* Depth is a distance: its gradient is a unit vector. These rows put it
	 * along a world axis. */
	XVT_ASSERT_CLOSE(
		sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]),
		1.0, k_tol, WHY_FLOAT);
	XVT_ASSERT_CLOSE(fabs(axis[0]) + fabs(axis[1]) + fabs(axis[2]), 1.0,
			 k_tol, WHY_FLOAT);

	/* Every point on the view axis in front of the camera lands on the same
	 * pixel, at its distance. */
	int32_t point[3];
	along(camera.world_pos, axis, 100, point);
	float x0;
	float y0;
	float depth;
	XVT_ASSERT_INT_EQ(
		xvt_render_math_project_world(&view, point, &x0, &y0, &depth),
		1);
	XVT_ASSERT_CLOSE(depth, 100, k_tol, WHY_FLOAT);
	static const int32_t distances[] = {1, 1000, 1000000};
	float x;
	float y;
	for (unsigned i = 0; i < 3; ++i) {
		along(camera.world_pos, axis, distances[i], point);
		XVT_ASSERT_INT_EQ(xvt_render_math_project_world(&view, point,
								&x, &y, &depth),
				  1);
		XVT_ASSERT_CLOSE(depth, distances[i], k_tol, WHY_FLOAT);
		XVT_ASSERT_CLOSE(x, x0, k_tol, WHY_FLOAT);
		XVT_ASSERT_CLOSE(y, y0, k_tol, WHY_FLOAT);
	}

	/* Depth may be left out. */
	XVT_ASSERT_INT_EQ(
		xvt_render_math_project_world(&view, point, &x, &y, NULL), 1);

	/* Behind the camera, or at it: refused with depth written and x, y untouched. */
	x = -12345.0f;
	y = -12345.0f;
	along(camera.world_pos, axis, -100, point);
	XVT_ASSERT_INT_EQ(
		xvt_render_math_project_world(&view, point, &x, &y, &depth), 0);
	XVT_ASSERT_CLOSE(depth, -100, k_tol, WHY_FLOAT);
	XVT_ASSERT_CLOSE(x, -12345.0, 0, "untouched");
	XVT_ASSERT_CLOSE(y, -12345.0, 0, "untouched");
	XVT_ASSERT_INT_EQ(xvt_render_math_project_world(&view, camera.world_pos,
							&x, &y, &depth),
			  0);
	XVT_ASSERT_CLOSE(x, -12345.0, 0, "untouched");

	/* In front but far off to the side: not clipped, it lands outside the window. */
	along(camera.world_pos, axis, 100, point);
	point[0] += axis[0] == 0 ? 100000 : 0;
	point[1] += axis[0] != 0 ? 100000 : 0;
	XVT_ASSERT_INT_EQ(
		xvt_render_math_project_world(&view, point, &x, &y, &depth), 1);
	XVT_ASSERT_TRUE(x < 0 || x > 640 || y < 0 || y > 400);

	/* NULL view, world, x or y: refused, nothing written. */
	x = -1.0f;
	y = -1.0f;
	depth = -1.0f;
	XVT_ASSERT_INT_EQ(
		xvt_render_math_project_world(NULL, point, &x, &y, &depth), 0);
	XVT_ASSERT_INT_EQ(
		xvt_render_math_project_world(&view, NULL, &x, &y, &depth), 0);
	XVT_ASSERT_INT_EQ(
		xvt_render_math_project_world(&view, point, NULL, &y, &depth),
		0);
	XVT_ASSERT_INT_EQ(
		xvt_render_math_project_world(&view, point, &x, NULL, &depth),
		0);
	XVT_ASSERT_CLOSE(x, -1.0, 0, "untouched");
	XVT_ASSERT_CLOSE(y, -1.0, 0, "untouched");
	XVT_ASSERT_CLOSE(depth, -1.0, 0, "untouched");
}

static void check_integer_origin(void)
{
	/* Far from 0 a float cannot hold a world coordinate to the unit, but
	 * positions are measured from the integer origin first: one unit along
	 * the view axis is a depth of one. */
	struct xvt_snap_camera camera = clean_camera();
	camera.world_pos[0] = 2000000000;
	camera.world_pos[1] = -2000000000;
	camera.world_pos[2] = 1500000003;
	const int32_t origin[3] = {2000000000, -2000000000, 1500000000};
	struct xvt_render_view view;
	XVT_ASSERT_INT_EQ(
		xvt_render_math_build_view(&camera, origin, 640, 400, &view),
		1);
	XVT_ASSERT_CLOSE(view.camera.pos[2], 3, k_tol,
			 "a small integer difference");
	double axis[3];
	view_axis(&view, camera.world_pos, axis);
	int32_t point[3];
	along(camera.world_pos, axis, 1, point);
	XVT_ASSERT_CLOSE(depth(&view, point[0], point[1], point[2]), 1, k_tol,
			 WHY_FLOAT);
}

static void check_build_main_view(void)
{
	struct xvt_snap_camera camera = clean_camera();
	camera.viewport.x = 16;
	camera.viewport.y = 8;
	camera.viewport.width = 288;
	camera.viewport.height = 168;
	camera.center_x = 144;
	camera.center_y = 70;
	camera.projection_offset_y = 10;
	struct xvt_render_view view;

	XVT_ASSERT_INT_EQ(xvt_render_math_build_main_view(&camera, k_origin,
							  800, 400, &view),
			  1);
	/* The uniform fit of the record's 320 x 200 screen, as Layout computes it. */
	struct xvt_layout_transform layout;
	XVT_ASSERT_INT_EQ(xvt_render_math_layout(320, 200, 800, 400, &layout),
			  1);
	XVT_ASSERT_CLOSE(view.classic_pixel_scale, layout.scale, k_tol,
			 WHY_FLOAT);
	XVT_ASSERT_INT_EQ(xvt_render_math_build_main_view(&camera, k_origin,
							  640, 400, &view),
			  1);
	XVT_ASSERT_CLOSE(view.classic_pixel_scale, 2.0, k_tol, WHY_FLOAT);

	/* Both half-angles from the scaled focal length 2^8 * 2 over the whole window. */
	XVT_ASSERT_CLOSE(tan(view.camera.h_half_rad), 640.0 / (2 * 256.0 * 2),
			 k_tol, WHY_FLOAT);
	XVT_ASSERT_CLOSE(tan(view.camera.v_half_rad), 400.0 / (2 * 256.0 * 2),
			 k_tol, WHY_FLOAT);

	/* The window has the screen's shape, so the fitted frame is the window:
	 * the view axis lands on the viewport origin plus its center (plus the
	 * offset on y), scaled. */
	double axis[3];
	view_axis(&view, camera.world_pos, axis);
	int32_t point[3];
	along(camera.world_pos, axis, 500, point);
	float x;
	float y;
	XVT_ASSERT_INT_EQ(
		xvt_render_math_project_world(&view, point, &x, &y, NULL), 1);
	XVT_ASSERT_CLOSE(x, (16 + 144) * 2.0, k_tol, WHY_FLOAT);
	XVT_ASSERT_CLOSE(y, (8 + 70 + 10) * 2.0, k_tol, WHY_FLOAT);

	camera.screen_height = 0;
	/* No screen size: refused, with BuildView's result left in out. */
	struct xvt_render_view built;
	XVT_ASSERT_INT_EQ(
		xvt_render_math_build_view(&camera, k_origin, 640, 400, &built),
		1);
	struct xvt_render_view main_view;
	XVT_ASSERT_INT_EQ(xvt_render_math_build_main_view(&camera, k_origin,
							  640, 400, &main_view),
			  0);
	XVT_ASSERT_INT_EQ(memcmp(&built, &main_view, sizeof built), 0);
}

static void expect_rotation_columns(const float m[16], double length)
{
	for (int i = 0; i < 3; ++i) {
		double norm =
			sqrt((double)m[i] * m[i] + (double)m[4 + i] * m[4 + i] +
			     (double)m[8 + i] * m[8 + i]);
		XVT_ASSERT_CLOSE(norm, length, k_tol, WHY_FLOAT);
		for (int j = i + 1; j < 3; ++j) {
			double dot = (double)m[i] * m[j] +
				     (double)m[4 + i] * m[4 + j] +
				     (double)m[8 + i] * m[8 + j];
			XVT_ASSERT_TRUE(fabs(dot) <= k_tol * length * length);
		}
	}
}

static void check_object_matrix(void)
{
	struct xvt_snap_object object;
	memset(&object, 0, sizeof object);
	object.world_pos[0] = 1000;
	object.world_pos[1] = 2000;
	object.world_pos[2] = -3000;
	object.yaw = 0x1234;
	object.pitch = 0x2345;
	object.roll = 0x3456;
	const int32_t origin[3] = {900, 2100, -3000};
	float m[16];

	/* Translation in elements 3, 7 and 11; an affine bottom row. */
	xvt_render_math_object_matrix(&object, origin, m);
	XVT_ASSERT_CLOSE(m[3], 100, k_tol, "a small integer difference");
	XVT_ASSERT_CLOSE(m[7], -100, k_tol, "a small integer difference");
	XVT_ASSERT_CLOSE(m[11], 0, k_tol, "a small integer difference");
	XVT_ASSERT_CLOSE(m[12], 0, k_tol, "exact");
	XVT_ASSERT_CLOSE(m[13], 0, k_tol, "exact");
	XVT_ASSERT_CLOSE(m[14], 0, k_tol, "exact");
	XVT_ASSERT_CLOSE(m[15], 1, k_tol, "exact");

	/* From the angles: a rotation scaled by AERON_OPT_UNITS_PER_METER. */
	expect_rotation_columns(m, AERON_OPT_UNITS_PER_METER);

	/* No mobile record: the angles count and the cached rows do not. */
	object.cached_rows_q15[0] = 1234;
	float other[16];
	xvt_render_math_object_matrix(&object, origin, other);
	XVT_ASSERT_INT_EQ(memcmp(m, other, sizeof m), 0);
	object.roll = 0x0100;
	xvt_render_math_object_matrix(&object, origin, other);
	XVT_ASSERT_TRUE(memcmp(m, other, sizeof m) != 0);

	/* A mobile record with a dirty orientation: the same. */
	object.has_mobile = 1;
	object.orient_dirty = 1;
	xvt_render_math_object_matrix(&object, origin, m);
	object.cached_rows_q15[4] = -999;
	xvt_render_math_object_matrix(&object, origin, other);
	XVT_ASSERT_INT_EQ(memcmp(m, other, sizeof m), 0);

	/* A mobile record with a clean orientation: the cached Q15 rows count
	 * and the angles do not. Rows of a Q15 orthonormal basis give a
	 * rotation scaled by AERON_OPT_UNITS_PER_METER * 32767 / 32768. */
	object.orient_dirty = 0;
	static const int16_t basis[9] = {0,	 32767,	 0, 0, 0,
					 -32767, -32767, 0, 0};
	memcpy(object.cached_rows_q15, basis, sizeof basis);
	xvt_render_math_object_matrix(&object, origin, m);
	expect_rotation_columns(m,
				AERON_OPT_UNITS_PER_METER * 32767.0 / 32768.0);
	object.yaw = 0x7777;
	object.pitch = 0x1111;
	xvt_render_math_object_matrix(&object, origin, other);
	XVT_ASSERT_INT_EQ(memcmp(m, other, sizeof m), 0);
	static const int16_t other_basis[9] = {0, 0, 32767, 32767, 0,
					       0, 0, 32767, 0};
	memcpy(object.cached_rows_q15, other_basis, sizeof other_basis);
	xvt_render_math_object_matrix(&object, origin, other);
	XVT_ASSERT_TRUE(memcmp(m, other, sizeof m) != 0);
	expect_rotation_columns(other,
				AERON_OPT_UNITS_PER_METER * 32767.0 / 32768.0);
}

static struct xvt_render_snapshot *g_current;
static struct xvt_render_snapshot *g_previous;

/* Two identical snapshots of a valid locked flight with a camera and two objects. */
static void same_snapshots(void)
{
	memset(g_current, 0, sizeof *g_current);
	g_current->flight_valid = 1;
	g_current->camera = clean_camera();
	g_current->object_count = 2;
	for (int i = 0; i < 2; ++i) {
		struct xvt_snap_object *o = &g_current->objects[i];
		o->id.slot = (uint16_t)(i + 3);
		o->id.signature = (uint16_t)(0x40 + i);
		o->object_type = 1;
		o->has_mobile = 1;
		o->world_pos[0] = 100 * i;
		o->mesh_rotation[0] = 9;
	}
	g_current->view_time_ticks = 50;
	memcpy(g_previous, g_current, sizeof *g_current);
}

/* Flips one byte of the current snapshot, asks PoseChanged, and flips it back. */
static int changed_with_byte_flipped(size_t offset)
{
	uint8_t *bytes = (uint8_t *)g_current;
	bytes[offset] ^= 0x5A;
	int changed = xvt_render_math_pose_changed(g_current, g_previous);
	bytes[offset] ^= 0x5A;
	return changed;
}

struct field {
	size_t offset;
	size_t size;
	int compared;
	const char *name;
};

#define OBJECT_FIELD(member, compared)                                         \
	{offsetof(struct xvt_snap_object, member),                             \
	 sizeof(((struct xvt_snap_object *)0)->member), compared, #member}

static const struct field k_object_fields[] = {
	OBJECT_FIELD(id.slot, 1),
	OBJECT_FIELD(id.signature, 1),
	OBJECT_FIELD(object_type, 1),
	OBJECT_FIELD(genus, 1),
	OBJECT_FIELD(flight_group, 0),
	OBJECT_FIELD(slot_class, 1),
	OBJECT_FIELD(world_pos, 1),
	OBJECT_FIELD(prev_world_pos, 0),
	OBJECT_FIELD(player_owner, 0),
	OBJECT_FIELD(yaw, 1),
	OBJECT_FIELD(pitch, 1),
	OBJECT_FIELD(roll, 1),
	OBJECT_FIELD(type_specific_word, 0),
	OBJECT_FIELD(type_specific, 1),
	OBJECT_FIELD(has_mobile, 1),
	OBJECT_FIELD(has_craft, 1),
	OBJECT_FIELD(orient_dirty, 1),
	OBJECT_FIELD(move_dirty, 0),
	OBJECT_FIELD(family, 1),
	OBJECT_FIELD(iff, 0),
	OBJECT_FIELD(team, 0),
	OBJECT_FIELD(node_switch, 1),
	OBJECT_FIELD(source_type, 1),
	OBJECT_FIELD(light_scale, 1),
	OBJECT_FIELD(source_slot, 1),
	OBJECT_FIELD(speed, 1),
	OBJECT_FIELD(cached_rows_q15, 1),
	OBJECT_FIELD(move_q15, 0),
	OBJECT_FIELD(component_state, 1),
	OBJECT_FIELD(mesh_rotation, 1),
	OBJECT_FIELD(component_hp, 0),
	OBJECT_FIELD(sfoil_state, 0),
	OBJECT_FIELD(object_kind, 1),
	OBJECT_FIELD(working_subsystems, 1),
	OBJECT_FIELD(installed_subsystems, 0),
	OBJECT_FIELD(throttle, 1),
	OBJECT_FIELD(overdrive_off, 1),
	OBJECT_FIELD(max_speed, 1),
	OBJECT_FIELD(laser_recharge_level, 1),
	OBJECT_FIELD(shield_recharge_level, 1),
	OBJECT_FIELD(beam_recharge_level, 1),
};

#define SNAPSHOT_FIELD(member)                                                 \
	{offsetof(struct xvt_render_snapshot, member),                         \
	 sizeof(((struct xvt_render_snapshot *)0)->member), 0, #member}

/* Every snapshot field PoseChanged does not compare. view_time_ticks and
 * flight_unlocked count only while the component animation reports movement,
 * which it does not here. */
static const struct field k_other_fields[] = {
	SNAPSHOT_FIELD(snapshot_serial),
	SNAPSHOT_FIELD(flight_frame_serial),
	SNAPSHOT_FIELD(capture_host_us),
	SNAPSHOT_FIELD(component_event_serial),
	SNAPSHOT_FIELD(component_event_time),
	SNAPSHOT_FIELD(flight_unlocked),
	SNAPSHOT_FIELD(presentation_serial),
	SNAPSHOT_FIELD(frontend_generation),
	SNAPSHOT_FIELD(presented_scene),
	SNAPSHOT_FIELD(presented_target),
	SNAPSHOT_FIELD(frontend_surfaces_released),
	SNAPSHOT_FIELD(mission_generation),
	SNAPSHOT_FIELD(world_generation),
	SNAPSHOT_FIELD(game_time_ticks),
	SNAPSHOT_FIELD(view_time_ticks),
	SNAPSHOT_FIELD(scene_kind),
	SNAPSHOT_FIELD(dropped_records),
	SNAPSHOT_FIELD(focused),
	SNAPSHOT_FIELD(paused),
	SNAPSHOT_FIELD(text_entry_active),
	SNAPSHOT_FIELD(lighting),
	SNAPSHOT_FIELD(cockpit),
	SNAPSHOT_FIELD(cockpit_resources),
	SNAPSHOT_FIELD(map),
	SNAPSHOT_FIELD(sky),
	SNAPSHOT_FIELD(hyperspace),
	SNAPSHOT_FIELD(cursor),
	SNAPSHOT_FIELD(flight_palette_argb),
	SNAPSHOT_FIELD(fuselage_sequence),
	SNAPSHOT_FIELD(types),
	SNAPSHOT_FIELD(target_boxes),
	SNAPSHOT_FIELD(target_box_count),
	SNAPSHOT_FIELD(sprites),
	SNAPSHOT_FIELD(sprite_count),
	SNAPSHOT_FIELD(glyphs),
	SNAPSHOT_FIELD(glyph_count),
	SNAPSHOT_FIELD(paint),
	SNAPSHOT_FIELD(paint_count),
	SNAPSHOT_FIELD(copies),
	SNAPSHOT_FIELD(copy_count),
	SNAPSHOT_FIELD(surface_events),
	SNAPSHOT_FIELD(surface_event_count),
	SNAPSHOT_FIELD(previews),
	SNAPSHOT_FIELD(preview_count),
	SNAPSHOT_FIELD(opt_asset_generation),
	SNAPSHOT_FIELD(texture_asset_generation),
	SNAPSHOT_FIELD(image_asset_generation),
	SNAPSHOT_FIELD(opt_assets),
	SNAPSHOT_FIELD(opt_asset_count),
	SNAPSHOT_FIELD(texture_assets),
	SNAPSHOT_FIELD(texture_asset_count),
	SNAPSHOT_FIELD(image_assets),
	SNAPSHOT_FIELD(image_asset_count),
};

static void expect_field_result(size_t base, const struct field *field)
{
	int first = changed_with_byte_flipped(base + field->offset);
	int last = changed_with_byte_flipped(base + field->offset +
					     field->size - 1);
	if (first != field->compared || last != field->compared) {
		fprintf(stderr,
			"field %s: PoseChanged gave %d and %d, expected %d\n",
			field->name, first, last, field->compared);
	}
	XVT_ASSERT_INT_EQ(first, field->compared);
	XVT_ASSERT_INT_EQ(last, field->compared);
}

static void check_pose_changed(void)
{
	xvt_component_animation_reset();
	same_snapshots();
	XVT_ASSERT_INT_EQ(xvt_render_math_pose_changed(g_current, g_previous),
			  0);
	XVT_ASSERT_INT_EQ(xvt_render_math_pose_changed(NULL, g_previous), 1);
	XVT_ASSERT_INT_EQ(xvt_render_math_pose_changed(g_current, NULL), 1);

	/* flight_valid differs. */
	g_current->flight_valid = 0;
	XVT_ASSERT_INT_EQ(xvt_render_math_pose_changed(g_current, g_previous),
			  1);
	XVT_ASSERT_INT_EQ(xvt_render_math_pose_changed(g_previous, g_current),
			  1);

	/* Neither valid: nothing else matters. */
	g_previous->flight_valid = 0;
	g_current->camera.world_pos[0] += 5;
	g_current->object_count = 1;
	g_current->objects[0].yaw = 0x4000;
	XVT_ASSERT_INT_EQ(xvt_render_math_pose_changed(g_current, g_previous),
			  0);

	/* Any byte of the camera record. */
	same_snapshots();
	const size_t camera = offsetof(struct xvt_render_snapshot, camera);
	for (size_t i = 0; i < sizeof(struct xvt_snap_camera); ++i) {
		XVT_ASSERT_INT_EQ(changed_with_byte_flipped(camera + i), 1);
	}

	/* The object count. */
	g_current->object_count = 1;
	XVT_ASSERT_INT_EQ(xvt_render_math_pose_changed(g_current, g_previous),
			  1);
	g_current->object_count = 2;

	/* Each field of each object in the count: compared, or one of the
	 * fields the header leaves out. */
	for (unsigned o = 0; o < 2; ++o) {
		const size_t base =
			offsetof(struct xvt_render_snapshot, objects) +
			o * sizeof(struct xvt_snap_object);
		for (unsigned f = 0;
		     f < sizeof k_object_fields / sizeof k_object_fields[0];
		     ++f) {
			expect_field_result(base, &k_object_fields[f]);
		}
	}

	/* Nothing else: an object past the count, and every other snapshot field. */
	const struct field past = {
		offsetof(struct xvt_render_snapshot, objects) +
			2 * sizeof(struct xvt_snap_object),
		sizeof(struct xvt_snap_object), 0, "objects[object_count]"};
	expect_field_result(0, &past);
	for (unsigned f = 0;
	     f < sizeof k_other_fields / sizeof k_other_fields[0]; ++f) {
		expect_field_result(0, &k_other_fields[f]);
	}
}

/* Makes the component animation report movement: a craft whose component turns
 * between two frames. */
static void animation_moves(void)
{
	xvt_component_animation_reset();
	struct xvt_render_snapshot *frame = g_current;
	memset(frame, 0, sizeof *frame);
	frame->flight_valid = 1;
	frame->flight_unlocked = 1;
	frame->object_count = 1;
	frame->objects[0].has_craft = 1;
	frame->objects[0].object_type = 1;
	frame->objects[0].id.signature = 7;
	frame->flight_frame_serial = 1;
	frame->component_event_serial = 1;
	xvt_component_animation_prepare(frame);
	frame->flight_frame_serial = 2;
	frame->component_event_serial = 2;
	frame->view_time_ticks = 16;
	frame->objects[0].mesh_rotation[0] = 64;
	xvt_component_animation_prepare(frame);
	XVT_ASSERT_INT_EQ(xvt_component_animation_changed(), 1);
}

static void check_pose_changed_view_time(void)
{
	/* An unlocked flight whose view time moved while the animation reports movement. */
	animation_moves();
	same_snapshots();
	g_current->flight_unlocked = 1;
	g_previous->flight_unlocked = 1;
	XVT_ASSERT_INT_EQ(xvt_render_math_pose_changed(g_current, g_previous),
			  0);
	g_current->view_time_ticks += 1;
	XVT_ASSERT_INT_EQ(xvt_render_math_pose_changed(g_current, g_previous),
			  1);

	/* A locked flight, or no movement reported: the view time alone is no change. */
	g_current->flight_unlocked = 0;
	g_previous->flight_unlocked = 0;
	XVT_ASSERT_INT_EQ(xvt_render_math_pose_changed(g_current, g_previous),
			  0);
	g_current->flight_unlocked = 1;
	g_previous->flight_unlocked = 1;
	xvt_component_animation_reset();
	XVT_ASSERT_INT_EQ(xvt_render_math_pose_changed(g_current, g_previous),
			  0);
}

int main(void)
{
	g_current = calloc(1, sizeof *g_current);
	g_previous = calloc(1, sizeof *g_previous);
	XVT_ASSERT_TRUE(g_current != NULL && g_previous != NULL);
	check_layout();
	check_layout_points();
	check_build_view_refusals();
	check_build_view_fields();
	check_build_view_rows();
	check_project_world();
	check_integer_origin();
	check_build_main_view();
	check_object_matrix();
	check_pose_changed();
	check_pose_changed_view_time();
	free(g_current);
	free(g_previous);
	return 0;
}
