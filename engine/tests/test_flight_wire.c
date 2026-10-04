/* Checks the network flight protocol's byte helpers
 * (xvt_runtime/runtime/flight_wire.h) against the promises in its header: which
 * ticks are usable on the network, little-endian reads and writes of each
 * width, the all-zero test, and the CRC-32C. The module keeps no state and
 * reads no game globals. */
#include <stdint.h>
#include <string.h>

#include "test_assert.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/flight_wire.h"

static void check_valid_tick(void)
{
	XVT_ASSERT_INT_EQ(xvt_flight_wire_valid_tick(0), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_wire_valid_tick(XVT_NETWORK_STEP_TICKS),
			  1);
	XVT_ASSERT_INT_EQ(
		xvt_flight_wire_valid_tick(4 * XVT_NETWORK_STEP_TICKS), 1);
	/* Off the step: one past a valid tick. */
	XVT_ASSERT_INT_EQ(
		xvt_flight_wire_valid_tick(XVT_NETWORK_STEP_TICKS + 1), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_wire_valid_tick(1), 0);

	/* The top of the range: INT32_MAX - 1 is on the step and allowed; the
	 * next step up is not. */
	uint32_t top = INT32_MAX - 1u;
	XVT_ASSERT_INT_EQ(top % XVT_NETWORK_STEP_TICKS, 0);
	XVT_ASSERT_INT_EQ(xvt_flight_wire_valid_tick(top), 1);
	XVT_ASSERT_INT_EQ(
		xvt_flight_wire_valid_tick(top + XVT_NETWORK_STEP_TICKS), 0);
	XVT_ASSERT_INT_EQ(xvt_flight_wire_valid_tick(UINT32_MAX - 1u), 0);
}

static void check_little_endian(void)
{
	xvt_wire_u64 bytes;

	/* Set writes the low byte first and only its own width. */
	memset(bytes, 0xEE, sizeof bytes);
	xvt_wire_set16(bytes, 0x0201);
	XVT_ASSERT_INT_EQ(bytes[0], 0x01);
	XVT_ASSERT_INT_EQ(bytes[1], 0x02);
	XVT_ASSERT_INT_EQ(bytes[2], 0xEE);

	memset(bytes, 0xEE, sizeof bytes);
	xvt_wire_set32(bytes, 0x04030201u);
	for (int i = 0; i < 4; ++i) {
		XVT_ASSERT_INT_EQ(bytes[i], i + 1);
	}
	XVT_ASSERT_INT_EQ(bytes[4], 0xEE);

	xvt_wire_set64(bytes, UINT64_C(0x0807060504030201));
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(bytes[i], i + 1);
	}

	/* Get reads the same order back. */
	const uint8_t order[8] = {0x10, 0x32, 0x54, 0x76,
				  0x98, 0xBA, 0xDC, 0xFE};
	XVT_ASSERT_INT_EQ(xvt_wire_get16(order), 0x3210);
	XVT_ASSERT_INT_EQ(xvt_wire_get32(order), 0x76543210u);
	XVT_ASSERT_TRUE(xvt_wire_get64(order) == UINT64_C(0xFEDCBA9876543210));
}

static void check_round_trips(void)
{
	const uint16_t values16[] = {0, 1, 0x00FF, 0xFF00, 0x8000, UINT16_MAX};
	xvt_wire_u16 b16;
	for (size_t i = 0; i < sizeof values16 / sizeof values16[0]; ++i) {
		xvt_wire_set16(b16, values16[i]);
		XVT_ASSERT_INT_EQ(xvt_wire_get16(b16), values16[i]);
	}
	const uint32_t values32[] = {0, 1, 0x80000000u, 0x12345678u,
				     UINT32_MAX};
	xvt_wire_u32 b32;
	for (size_t i = 0; i < sizeof values32 / sizeof values32[0]; ++i) {
		xvt_wire_set32(b32, values32[i]);
		XVT_ASSERT_INT_EQ(xvt_wire_get32(b32), values32[i]);
	}
	const uint64_t values64[] = {0, 1, UINT64_C(0x8000000000000000),
				     UINT64_C(0x0123456789ABCDEF), UINT64_MAX};
	xvt_wire_u64 b64;
	for (size_t i = 0; i < sizeof values64 / sizeof values64[0]; ++i) {
		xvt_wire_set64(b64, values64[i]);
		XVT_ASSERT_TRUE(xvt_wire_get64(b64) == values64[i]);
	}
}

static void check_is_zero(void)
{
	uint8_t bytes[16];
	memset(bytes, 0, sizeof bytes);
	XVT_ASSERT_INT_EQ(xvt_wire_is_zero(bytes, 0), 1);
	XVT_ASSERT_INT_EQ(xvt_wire_is_zero(bytes, sizeof bytes), 1);

	/* Any nonzero byte inside the size makes it 0; one just past the size does not count. */
	bytes[sizeof bytes - 1] = 1;
	XVT_ASSERT_INT_EQ(xvt_wire_is_zero(bytes, sizeof bytes), 0);
	XVT_ASSERT_INT_EQ(xvt_wire_is_zero(bytes, sizeof bytes - 1), 1);
	bytes[0] = 0x80;
	XVT_ASSERT_INT_EQ(xvt_wire_is_zero(bytes, 1), 0);
	/* Size 0 is all zero whatever the bytes hold. */
	XVT_ASSERT_INT_EQ(xvt_wire_is_zero(bytes, 0), 1);
}

static void check_crc32c(void)
{
	/* The check value the header names. */
	XVT_ASSERT_INT_EQ(
		xvt_flight_wire_crc32c((const uint8_t *)"123456789", 9),
		0xE3069283u);

	/* With no bytes, the initial value is only inverted back: 0. */
	uint8_t bytes[32];
	XVT_ASSERT_INT_EQ(xvt_flight_wire_crc32c(bytes, 0), 0);

	/* The CRC-32C test vectors of RFC 3720, appendix B.4: 32 bytes of
	 * zeros, of ones, and counting up. */
	memset(bytes, 0, sizeof bytes);
	XVT_ASSERT_INT_EQ(xvt_flight_wire_crc32c(bytes, sizeof bytes),
			  0x8A9136AAu);
	memset(bytes, 0xFF, sizeof bytes);
	XVT_ASSERT_INT_EQ(xvt_flight_wire_crc32c(bytes, sizeof bytes),
			  0x62A8AB43u);
	for (unsigned i = 0; i < sizeof bytes; ++i) {
		bytes[i] = (uint8_t)i;
	}
	XVT_ASSERT_INT_EQ(xvt_flight_wire_crc32c(bytes, sizeof bytes),
			  0x46DD794Eu);
}

int main(void)
{
	check_valid_tick();
	check_little_endian();
	check_round_trips();
	check_is_zero();
	check_crc32c();
	return 0;
}
