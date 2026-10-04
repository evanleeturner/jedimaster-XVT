/* Checks the logical frame (xvt_runtime/runtime/presentation.h) against the promises in its header: the
 * frame width that follows the window's aspect, the centered classic rectangle and moves into it, the
 * mouse mapping through the largest centered 4:3 area, and the lifting of the classic flight rendering
 * suppression. Aeron runs without a window; the test builds its own input snapshots. Every case starts
 * from Init.
 *
 * Not checked here: WarpClassic and EndFrame, whose effect is on a real window and the DirectX 5 frame. */
#include <string.h>

#include "aeron/aeron.h"
#include "aeron/compat/host.h"
#include "test_assert.h"
#include "xvt_runtime/runtime/presentation.h"

static AeronInputSnapshot g_input;

static void check_rect(AeronRectI rect, int x, int y, int width, int height)
{
	XVT_ASSERT_INT_EQ(rect.x, x);
	XVT_ASSERT_INT_EQ(rect.y, y);
	XVT_ASSERT_INT_EQ(rect.width, width);
	XVT_ASSERT_INT_EQ(rect.height, height);
}

static void check_init_resets_frame(void)
{
	xvt_presentation_init();
	xvt_presentation_sync_to_window(1920, 1080);
	XVT_ASSERT_TRUE(xvt_presentation_logical_rect().width !=
			XVT_CLASSIC_WIDTH);
	xvt_presentation_init();
	check_rect(xvt_presentation_logical_rect(), 0, 0, XVT_CLASSIC_WIDTH,
		   XVT_CLASSIC_HEIGHT);
	check_rect(xvt_presentation_classic_rect(), 0, 0, 640, 480);
}

static void check_sync_to_window(void)
{
	xvt_presentation_init();
	/* 480 * 1920 / 1080 is 853.3: rounded to 853, made even. */
	xvt_presentation_sync_to_window(1920, 1080);
	check_rect(xvt_presentation_logical_rect(), 0, 0, 852, 480);

	/* 480 * 877 / 600 is 701.6: rounding gives 702, which is already even; cutting would give 700. */
	xvt_presentation_sync_to_window(877, 600);
	XVT_ASSERT_INT_EQ(xvt_presentation_logical_rect().width, 702);

	/* 4:3 and narrower windows give the classic width. */
	xvt_presentation_sync_to_window(800, 600);
	XVT_ASSERT_INT_EQ(xvt_presentation_logical_rect().width, 640);
	xvt_presentation_sync_to_window(600, 800);
	XVT_ASSERT_INT_EQ(xvt_presentation_logical_rect().width, 640);

	/* 32:9 is the widest frame: 1706.7 rounds to 1707, made even; anything wider is clamped there. */
	xvt_presentation_sync_to_window(3840, 1080);
	XVT_ASSERT_INT_EQ(xvt_presentation_logical_rect().width, 1706);
	xvt_presentation_sync_to_window(10000, 1000);
	XVT_ASSERT_INT_EQ(xvt_presentation_logical_rect().width, 1706);

	/* A non-positive size is ignored. */
	xvt_presentation_sync_to_window(0, 1080);
	xvt_presentation_sync_to_window(1920, 0);
	xvt_presentation_sync_to_window(-1920, -1080);
	check_rect(xvt_presentation_logical_rect(), 0, 0, 1706, 480);
}

static void check_sync_tells_aeron(void)
{
	int width = 0;
	int height = 0;
	xvt_presentation_init();
	XVT_ASSERT_INT_EQ(Aeron_SetLogicalSize(320, 200), 1);
	xvt_presentation_sync_to_window(1920, 1080);
	XVT_ASSERT_INT_EQ(Aeron_GetLogicalSize(&width, &height), 1);
	XVT_ASSERT_INT_EQ(width, 852);
	XVT_ASSERT_INT_EQ(height, 480);
	xvt_presentation_sync_to_window(3840, 1080);
	XVT_ASSERT_INT_EQ(Aeron_GetLogicalSize(&width, &height), 1);
	XVT_ASSERT_INT_EQ(width, 1706);
	XVT_ASSERT_INT_EQ(height, 480);
}

static void check_classic_rect(void)
{
	xvt_presentation_init();
	xvt_presentation_sync_to_window(1920, 1080);
	check_rect(xvt_presentation_classic_rect(), (852 - 640) / 2, 0, 640,
		   480);

	/* FromClassic moves a rectangle by the classic rectangle's offset and changes nothing else. */
	AeronRectI moved =
		xvt_presentation_from_classic((AeronRectI){10, 20, 30, 40});
	check_rect(moved, 10 + (852 - 640) / 2, 20, 30, 40);

	xvt_presentation_sync_to_window(800, 600);
	check_rect(xvt_presentation_classic_rect(), 0, 0, 640, 480);
	moved = xvt_presentation_from_classic((AeronRectI){10, 20, 30, 40});
	check_rect(moved, 10, 20, 30, 40);
}

/* Sets the test's input snapshot to a window of the given size with the mouse at raw x, y. */
static const AeronInputSnapshot *window(int width, int height, int raw_x,
					int raw_y, int inside)
{
	memset(&g_input, 0, sizeof g_input);
	g_input.window_width = width;
	g_input.window_height = height;
	g_input.mouse.raw_x = raw_x;
	g_input.mouse.raw_y = raw_y;
	g_input.mouse.inside_content = inside;
	return &g_input;
}

static void check_mouse_to_classic(void)
{
	int x = -1;
	int y = -1;
	xvt_presentation_init();

	/* 1280x720: the 4:3 area is 960x720, starting 160 points in. Its center is the classic center. */
	XVT_ASSERT_INT_EQ(xvt_presentation_mouse_to_classic(
				  window(1280, 720, 160 + 480, 360, 1), &x, &y),
			  1);
	XVT_ASSERT_INT_EQ(x, 320);
	XVT_ASSERT_INT_EQ(y, 240);
	/* The area's corners: the top left is classic 0, 0; one point short of the right edge is inside. */
	XVT_ASSERT_INT_EQ(xvt_presentation_mouse_to_classic(
				  window(1280, 720, 160, 0, 1), &x, &y),
			  1);
	XVT_ASSERT_INT_EQ(x, 0);
	XVT_ASSERT_INT_EQ(y, 0);
	XVT_ASSERT_INT_EQ(xvt_presentation_mouse_to_classic(
				  window(1280, 720, 160 + 959, 719, 1), &x, &y),
			  1);
	XVT_ASSERT_INT_EQ(xvt_presentation_mouse_to_classic(
				  window(1280, 720, 160 + 960, 719, 1), &x, &y),
			  0);

	/* Outside the area, in the side bar: 0, with the coordinates still written. */
	XVT_ASSERT_INT_EQ(xvt_presentation_mouse_to_classic(
				  window(1280, 720, 100, 360, 1), &x, &y),
			  0);
	XVT_ASSERT_INT_EQ(x, -60 * 640 / 960);
	XVT_ASSERT_INT_EQ(y, 240);

	/* Inside the area but not inside the content: 0, with the coordinates written. */
	x = -1;
	y = -1;
	XVT_ASSERT_INT_EQ(xvt_presentation_mouse_to_classic(
				  window(1280, 720, 160 + 480, 360, 0), &x, &y),
			  0);
	XVT_ASSERT_INT_EQ(x, 320);
	XVT_ASSERT_INT_EQ(y, 240);

	/* A tall window: 640x960 maps through a 640x480 area starting 240 points down. */
	XVT_ASSERT_INT_EQ(xvt_presentation_mouse_to_classic(
				  window(640, 960, 320, 240 + 240, 1), &x, &y),
			  1);
	XVT_ASSERT_INT_EQ(x, 320);
	XVT_ASSERT_INT_EQ(y, 240);
	XVT_ASSERT_INT_EQ(xvt_presentation_mouse_to_classic(
				  window(640, 960, 320, 100, 1), &x, &y),
			  0);
	XVT_ASSERT_TRUE(y < 0);
}

static void check_mouse_to_classic_refusals(void)
{
	int x = 77;
	int y = 88;
	xvt_presentation_init();
	XVT_ASSERT_INT_EQ(xvt_presentation_mouse_to_classic(NULL, &x, &y), 0);
	XVT_ASSERT_INT_EQ(xvt_presentation_mouse_to_classic(
				  window(0, 720, 0, 0, 1), &x, &y),
			  0);
	XVT_ASSERT_INT_EQ(xvt_presentation_mouse_to_classic(
				  window(1280, 0, 0, 0, 1), &x, &y),
			  0);
	XVT_ASSERT_INT_EQ(xvt_presentation_mouse_to_classic(
				  window(-5, -5, 0, 0, 1), &x, &y),
			  0);
	/* A 1x1 window has no 4:3 area of whole points. */
	XVT_ASSERT_INT_EQ(xvt_presentation_mouse_to_classic(
				  window(1, 1, 0, 0, 1), &x, &y),
			  0);
	XVT_ASSERT_INT_EQ(x, 77);
	XVT_ASSERT_INT_EQ(y, 88);
}

static void check_suppression_lifted(void)
{
	xvt_presentation_init();
	AeronDx5_SetClassicFlightRenderingSuppressed(1);
	xvt_presentation_require_classic();
	XVT_ASSERT_INT_EQ(AeronDx5_IsClassicFlightRenderingSuppressed(), 0);

	AeronDx5_SetClassicFlightRenderingSuppressed(1);
	xvt_presentation_shutdown();
	XVT_ASSERT_INT_EQ(AeronDx5_IsClassicFlightRenderingSuppressed(), 0);
}

int main(void)
{
	check_init_resets_frame();
	check_sync_to_window();
	check_sync_tells_aeron();
	check_classic_rect();
	check_mouse_to_classic();
	check_mouse_to_classic_refusals();
	check_suppression_lifted();
	return 0;
}
