/* Checks mouse flight (xvt_runtime/input/mouse_flight.h) against the promises in its header that hold
 * outside a flight: with mouse flight not allowed, nothing samples, queues or marks the HUD, and the
 * centered stick reads 0 through any mix of NULL pointers.
 *
 * Not checked here: the virtual stick itself (gain, sensitivity, the right-button tap and roll lock, the
 * button keys) runs only while mouse flight is allowed, which needs loaded settings and a running flight,
 * and while the pointer is in relative mode, which needs a window. */
#include "aeron/aeron.h"
#include "test_assert.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/mouse_flight.h"

#include <string.h>

static AeronInputSnapshot *Host(void)
{
	return (AeronInputSnapshot *)Aeron_InputSnapshot();
}

/* Mouse flight switched on in the module's options, with the host showing motion and every button. */
static void Start(void)
{
	XvtInput_ResetCapture();
	XvtMouseFlight_Reset();
	XvtMouseOptions options;
	memset(&options, 0, sizeof options);
	options.mouse_flight_enabled = true;
	options.mouse_sensitivity = XVT_MOUSE_SENSITIVITY_MIN;
	XvtMouseFlight_SetOptions(&options);
	AeronInputSnapshot *host = Host();
	++host->frame_id;
	host->has_focus = 1;
	host->mouse.relative_x = 40.0f;
	host->mouse.relative_y = -40.0f;
	host->mouse.buttons = 0x1F;
	host->mouse.pressed_buttons = 0x1F;
}

static void CheckNothingWhileNotAllowed(void)
{
	Start();
	XVT_ASSERT_INT_EQ(XvtInput_MouseFlightAllowed(), 0);
	XvtMouseFlight_Pump();
	XVT_ASSERT_INT_EQ(XvtMouseFlight_Sample(), 0);
	XVT_ASSERT_INT_EQ(XvtMouseFlight_ReadKey(), 0);
	int yaw = 1, pitch = 1;
	XVT_ASSERT_INT_EQ(XvtMouseFlight_GetHudMarker(&yaw, &pitch), 0);
	XVT_ASSERT_INT_EQ(XvtMouseFlight_GetHudMarker(NULL, NULL), 0);
}

static void CheckCenteredAxes(void)
{
	Start();
	XvtMouseFlight_Pump();
	XvtMouseFlight_Sample();
	XvtMouseFlight_Reset();

	/* Recentered: every axis reads 0, and any pointer may be NULL. */
	int yaw = 9, pitch = 9, roll = 9;
	XvtMouseFlight_GetAxes(&yaw, &pitch, &roll);
	XVT_ASSERT_INT_EQ(yaw, 0);
	XVT_ASSERT_INT_EQ(pitch, 0);
	XVT_ASSERT_INT_EQ(roll, 0);
	XvtMouseFlight_GetAxes(NULL, NULL, NULL);
	yaw = 9;
	XvtMouseFlight_GetAxes(&yaw, NULL, NULL);
	XVT_ASSERT_INT_EQ(yaw, 0);
	pitch = 9;
	XvtMouseFlight_GetAxes(NULL, &pitch, NULL);
	XVT_ASSERT_INT_EQ(pitch, 0);
	roll = 9;
	XvtMouseFlight_GetAxes(NULL, NULL, &roll);
	XVT_ASSERT_INT_EQ(roll, 0);
}

int main(void)
{
	CheckNothingWhileNotAllowed();
	CheckCenteredAxes();
	XvtMouseFlight_Reset();
	XvtInput_ResetCapture();
	return 0;
}
