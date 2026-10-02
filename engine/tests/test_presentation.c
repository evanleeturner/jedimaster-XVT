/* Checks the logical frame (xvt_runtime/runtime/presentation.h) against the promises in its header: the
 * frame width that follows the window's aspect, the centered classic rectangle and moves into it, the
 * mouse mapping through the largest centered 4:3 area, and the lifting of the classic flight rendering
 * suppression. Aeron runs without a window; the test builds its own input snapshots. Every case starts
 * from Init.
 *
 * Not checked here: WarpClassic and EndFrame, whose effect is on a real window and the DirectX 5 frame. */
#include "aeron/aeron.h"
#include "aeron/compat/host.h"
#include "test_assert.h"
#include "xvt_runtime/runtime/presentation.h"

#include <string.h>

static AeronInputSnapshot g_input;

static void CheckRect(AeronRectI rect, int x, int y, int width, int height) {
	XVT_ASSERT_INT_EQ(rect.x, x);
	XVT_ASSERT_INT_EQ(rect.y, y);
	XVT_ASSERT_INT_EQ(rect.width, width);
	XVT_ASSERT_INT_EQ(rect.height, height);
}

static void CheckInitResetsFrame(void) {
	XvtPresentation_Init();
	XvtPresentation_SyncToWindow(1920, 1080);
	XVT_ASSERT_TRUE(XvtPresentation_LogicalRect().width != XVT_CLASSIC_WIDTH);
	XvtPresentation_Init();
	CheckRect(XvtPresentation_LogicalRect(), 0, 0, XVT_CLASSIC_WIDTH, XVT_CLASSIC_HEIGHT);
	CheckRect(XvtPresentation_ClassicRect(), 0, 0, 640, 480);
}

static void CheckSyncToWindow(void) {
	XvtPresentation_Init();
	/* 480 * 1920 / 1080 is 853.3: rounded to 853, made even. */
	XvtPresentation_SyncToWindow(1920, 1080);
	CheckRect(XvtPresentation_LogicalRect(), 0, 0, 852, 480);

	/* 480 * 877 / 600 is 701.6: rounding gives 702, which is already even; cutting would give 700. */
	XvtPresentation_SyncToWindow(877, 600);
	XVT_ASSERT_INT_EQ(XvtPresentation_LogicalRect().width, 702);

	/* 4:3 and narrower windows give the classic width. */
	XvtPresentation_SyncToWindow(800, 600);
	XVT_ASSERT_INT_EQ(XvtPresentation_LogicalRect().width, 640);
	XvtPresentation_SyncToWindow(600, 800);
	XVT_ASSERT_INT_EQ(XvtPresentation_LogicalRect().width, 640);

	/* 32:9 is the widest frame: 1706.7 rounds to 1707, made even; anything wider is clamped there. */
	XvtPresentation_SyncToWindow(3840, 1080);
	XVT_ASSERT_INT_EQ(XvtPresentation_LogicalRect().width, 1706);
	XvtPresentation_SyncToWindow(10000, 1000);
	XVT_ASSERT_INT_EQ(XvtPresentation_LogicalRect().width, 1706);

	/* A non-positive size is ignored. */
	XvtPresentation_SyncToWindow(0, 1080);
	XvtPresentation_SyncToWindow(1920, 0);
	XvtPresentation_SyncToWindow(-1920, -1080);
	CheckRect(XvtPresentation_LogicalRect(), 0, 0, 1706, 480);
}

static void CheckSyncTellsAeron(void) {
	int width = 0;
	int height = 0;
	XvtPresentation_Init();
	XVT_ASSERT_INT_EQ(Aeron_SetLogicalSize(320, 200), 1);
	XvtPresentation_SyncToWindow(1920, 1080);
	XVT_ASSERT_INT_EQ(Aeron_GetLogicalSize(&width, &height), 1);
	XVT_ASSERT_INT_EQ(width, 852);
	XVT_ASSERT_INT_EQ(height, 480);
	XvtPresentation_SyncToWindow(3840, 1080);
	XVT_ASSERT_INT_EQ(Aeron_GetLogicalSize(&width, &height), 1);
	XVT_ASSERT_INT_EQ(width, 1706);
	XVT_ASSERT_INT_EQ(height, 480);
}

static void CheckClassicRect(void) {
	XvtPresentation_Init();
	XvtPresentation_SyncToWindow(1920, 1080);
	CheckRect(XvtPresentation_ClassicRect(), (852 - 640) / 2, 0, 640, 480);

	/* FromClassic moves a rectangle by the classic rectangle's offset and changes nothing else. */
	AeronRectI moved = XvtPresentation_FromClassic((AeronRectI) { 10, 20, 30, 40 });
	CheckRect(moved, 10 + (852 - 640) / 2, 20, 30, 40);

	XvtPresentation_SyncToWindow(800, 600);
	CheckRect(XvtPresentation_ClassicRect(), 0, 0, 640, 480);
	moved = XvtPresentation_FromClassic((AeronRectI) { 10, 20, 30, 40 });
	CheckRect(moved, 10, 20, 30, 40);
}

/* Sets the test's input snapshot to a window of the given size with the mouse at raw x, y. */
static const AeronInputSnapshot* Window(int width, int height, int raw_x, int raw_y, int inside) {
	memset(&g_input, 0, sizeof g_input);
	g_input.window_width = width;
	g_input.window_height = height;
	g_input.mouse.raw_x = raw_x;
	g_input.mouse.raw_y = raw_y;
	g_input.mouse.inside_content = inside;
	return &g_input;
}

static void CheckMouseToClassic(void) {
	int x = -1;
	int y = -1;
	XvtPresentation_Init();

	/* 1280x720: the 4:3 area is 960x720, starting 160 points in. Its center is the classic center. */
	XVT_ASSERT_INT_EQ(XvtPresentation_MouseToClassic(Window(1280, 720, 160 + 480, 360, 1), &x, &y), 1);
	XVT_ASSERT_INT_EQ(x, 320);
	XVT_ASSERT_INT_EQ(y, 240);
	/* The area's corners: the top left is classic 0, 0; one point short of the right edge is inside. */
	XVT_ASSERT_INT_EQ(XvtPresentation_MouseToClassic(Window(1280, 720, 160, 0, 1), &x, &y), 1);
	XVT_ASSERT_INT_EQ(x, 0);
	XVT_ASSERT_INT_EQ(y, 0);
	XVT_ASSERT_INT_EQ(XvtPresentation_MouseToClassic(Window(1280, 720, 160 + 959, 719, 1), &x, &y), 1);
	XVT_ASSERT_INT_EQ(XvtPresentation_MouseToClassic(Window(1280, 720, 160 + 960, 719, 1), &x, &y), 0);

	/* Outside the area, in the side bar: 0, with the coordinates still written. */
	XVT_ASSERT_INT_EQ(XvtPresentation_MouseToClassic(Window(1280, 720, 100, 360, 1), &x, &y), 0);
	XVT_ASSERT_INT_EQ(x, -60 * 640 / 960);
	XVT_ASSERT_INT_EQ(y, 240);

	/* Inside the area but not inside the content: 0, with the coordinates written. */
	x = y = -1;
	XVT_ASSERT_INT_EQ(XvtPresentation_MouseToClassic(Window(1280, 720, 160 + 480, 360, 0), &x, &y), 0);
	XVT_ASSERT_INT_EQ(x, 320);
	XVT_ASSERT_INT_EQ(y, 240);

	/* A tall window: 640x960 maps through a 640x480 area starting 240 points down. */
	XVT_ASSERT_INT_EQ(XvtPresentation_MouseToClassic(Window(640, 960, 320, 240 + 240, 1), &x, &y), 1);
	XVT_ASSERT_INT_EQ(x, 320);
	XVT_ASSERT_INT_EQ(y, 240);
	XVT_ASSERT_INT_EQ(XvtPresentation_MouseToClassic(Window(640, 960, 320, 100, 1), &x, &y), 0);
	XVT_ASSERT_TRUE(y < 0);
}

static void CheckMouseToClassicRefusals(void) {
	int x = 77;
	int y = 88;
	XvtPresentation_Init();
	XVT_ASSERT_INT_EQ(XvtPresentation_MouseToClassic(NULL, &x, &y), 0);
	XVT_ASSERT_INT_EQ(XvtPresentation_MouseToClassic(Window(0, 720, 0, 0, 1), &x, &y), 0);
	XVT_ASSERT_INT_EQ(XvtPresentation_MouseToClassic(Window(1280, 0, 0, 0, 1), &x, &y), 0);
	XVT_ASSERT_INT_EQ(XvtPresentation_MouseToClassic(Window(-5, -5, 0, 0, 1), &x, &y), 0);
	/* A 1x1 window has no 4:3 area of whole points. */
	XVT_ASSERT_INT_EQ(XvtPresentation_MouseToClassic(Window(1, 1, 0, 0, 1), &x, &y), 0);
	XVT_ASSERT_INT_EQ(x, 77);
	XVT_ASSERT_INT_EQ(y, 88);
}

static void CheckSuppressionLifted(void) {
	XvtPresentation_Init();
	AeronDx5_SetClassicFlightRenderingSuppressed(1);
	XvtPresentation_RequireClassic();
	XVT_ASSERT_INT_EQ(AeronDx5_IsClassicFlightRenderingSuppressed(), 0);

	AeronDx5_SetClassicFlightRenderingSuppressed(1);
	XvtPresentation_Shutdown();
	XVT_ASSERT_INT_EQ(AeronDx5_IsClassicFlightRenderingSuppressed(), 0);
}

int main(void) {
	CheckInitResetsFrame();
	CheckSyncToWindow();
	CheckSyncTellsAeron();
	CheckClassicRect();
	CheckMouseToClassic();
	CheckMouseToClassicRefusals();
	CheckSuppressionLifted();
	return 0;
}
