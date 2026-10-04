/* Checks mouse flight (xvt_runtime/input/mouse_flight.h) against the promises in its header that hold
 * outside a flight: with mouse flight not allowed, nothing samples, queues or marks the HUD, and the
 * centered stick reads 0 through any mix of NULL pointers.
 *
 * Not checked here: the virtual stick itself (gain, sensitivity, the right-button tap and roll lock, the
 * button keys) runs only while mouse flight is allowed, which needs loaded settings and a running flight,
 * and while the pointer is in relative mode, which needs a window. */
#include <string.h>

#include "aeron/aeron.h"
#include "test_assert.h"
#include "xvt_runtime/input/capture.h"
#include "xvt_runtime/input/mouse_flight.h"

static AeronInputSnapshot *mouse_flight_host(void)
{
	return (AeronInputSnapshot *)Aeron_InputSnapshot();
}

/* Mouse flight switched on in the module's options, with the host showing motion and every button. */
static void mouse_flight_start(void)
{
	xvt_input_reset_capture();
	xvt_mouse_flight_reset();
	struct xvt_mouse_options options;
	memset(&options, 0, sizeof options);
	options.mouse_flight_enabled = true;
	options.mouse_sensitivity = XVT_MOUSE_SENSITIVITY_MIN;
	xvt_mouse_flight_set_options(&options);
	AeronInputSnapshot *host = mouse_flight_host();
	++host->frame_id;
	host->has_focus = 1;
	host->mouse.relative_x = 40.0f;
	host->mouse.relative_y = -40.0f;
	host->mouse.buttons = 0x1F;
	host->mouse.pressed_buttons = 0x1F;
}

static void check_nothing_while_not_allowed(void)
{
	mouse_flight_start();
	XVT_ASSERT_INT_EQ(xvt_input_mouse_flight_allowed(), 0);
	xvt_mouse_flight_pump();
	XVT_ASSERT_INT_EQ(xvt_mouse_flight_sample(), 0);
	XVT_ASSERT_INT_EQ(xvt_mouse_flight_read_key(), 0);
	int yaw = 1;
	int pitch = 1;
	XVT_ASSERT_INT_EQ(xvt_mouse_flight_get_hud_marker(&yaw, &pitch), 0);
	XVT_ASSERT_INT_EQ(xvt_mouse_flight_get_hud_marker(NULL, NULL), 0);
}

static void check_centered_axes(void)
{
	mouse_flight_start();
	xvt_mouse_flight_pump();
	xvt_mouse_flight_sample();
	xvt_mouse_flight_reset();

	/* Recentered: every axis reads 0, and any pointer may be NULL. */
	int yaw = 9;
	int pitch = 9;
	int roll = 9;
	xvt_mouse_flight_get_axes(&yaw, &pitch, &roll);
	XVT_ASSERT_INT_EQ(yaw, 0);
	XVT_ASSERT_INT_EQ(pitch, 0);
	XVT_ASSERT_INT_EQ(roll, 0);
	xvt_mouse_flight_get_axes(NULL, NULL, NULL);
	yaw = 9;
	xvt_mouse_flight_get_axes(&yaw, NULL, NULL);
	XVT_ASSERT_INT_EQ(yaw, 0);
	pitch = 9;
	xvt_mouse_flight_get_axes(NULL, &pitch, NULL);
	XVT_ASSERT_INT_EQ(pitch, 0);
	roll = 9;
	xvt_mouse_flight_get_axes(NULL, NULL, &roll);
	XVT_ASSERT_INT_EQ(roll, 0);
}

int main(void)
{
	check_nothing_while_not_allowed();
	check_centered_axes();
	xvt_mouse_flight_reset();
	xvt_input_reset_capture();
	return 0;
}
