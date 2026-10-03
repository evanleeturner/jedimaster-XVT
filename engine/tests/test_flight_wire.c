/* Checks the network flight protocol's byte helpers (xvt_runtime/runtime/flight_wire.h) against the promises
 * in its header: which ticks are usable on the network, little-endian reads and writes of each width, the
 * all-zero test, and the CRC-32C. The module keeps no state and reads no game globals. */
#include "test_assert.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/flight_wire.h"

#include <stdint.h>
#include <string.h>

static void CheckValidTick(void)
{
	XVT_ASSERT_INT_EQ(XvtFlightWire_ValidTick(0), 0);
	XVT_ASSERT_INT_EQ(XvtFlightWire_ValidTick(XVT_NETWORK_STEP_TICKS), 1);
	XVT_ASSERT_INT_EQ(XvtFlightWire_ValidTick(4 * XVT_NETWORK_STEP_TICKS),
			  1);
	/* Off the step: one past a valid tick. */
	XVT_ASSERT_INT_EQ(XvtFlightWire_ValidTick(XVT_NETWORK_STEP_TICKS + 1),
			  0);
	XVT_ASSERT_INT_EQ(XvtFlightWire_ValidTick(1), 0);

	/* The top of the range: INT32_MAX - 1 is on the step and allowed; the next step up is not. */
	uint32_t top = INT32_MAX - 1u;
	XVT_ASSERT_INT_EQ(top % XVT_NETWORK_STEP_TICKS, 0);
	XVT_ASSERT_INT_EQ(XvtFlightWire_ValidTick(top), 1);
	XVT_ASSERT_INT_EQ(XvtFlightWire_ValidTick(top + XVT_NETWORK_STEP_TICKS),
			  0);
	XVT_ASSERT_INT_EQ(XvtFlightWire_ValidTick(UINT32_MAX - 1u), 0);
}

static void CheckLittleEndian(void)
{
	XvtWireU64 bytes;

	/* Set writes the low byte first and only its own width. */
	memset(bytes, 0xEE, sizeof bytes);
	XvtWire_Set16(bytes, 0x0201);
	XVT_ASSERT_INT_EQ(bytes[0], 0x01);
	XVT_ASSERT_INT_EQ(bytes[1], 0x02);
	XVT_ASSERT_INT_EQ(bytes[2], 0xEE);

	memset(bytes, 0xEE, sizeof bytes);
	XvtWire_Set32(bytes, 0x04030201u);
	for (int i = 0; i < 4; ++i) {
		XVT_ASSERT_INT_EQ(bytes[i], i + 1);
	}
	XVT_ASSERT_INT_EQ(bytes[4], 0xEE);

	XvtWire_Set64(bytes, UINT64_C(0x0807060504030201));
	for (int i = 0; i < 8; ++i) {
		XVT_ASSERT_INT_EQ(bytes[i], i + 1);
	}

	/* Get reads the same order back. */
	const uint8_t order[8] = {0x10, 0x32, 0x54, 0x76,
				  0x98, 0xBA, 0xDC, 0xFE};
	XVT_ASSERT_INT_EQ(XvtWire_Get16(order), 0x3210);
	XVT_ASSERT_INT_EQ(XvtWire_Get32(order), 0x76543210u);
	XVT_ASSERT_TRUE(XvtWire_Get64(order) == UINT64_C(0xFEDCBA9876543210));
}

static void CheckRoundTrips(void)
{
	const uint16_t values16[] = {0, 1, 0x00FF, 0xFF00, 0x8000, UINT16_MAX};
	const uint32_t values32[] = {0, 1, 0x80000000u, 0x12345678u,
				     UINT32_MAX};
	const uint64_t values64[] = {0, 1, UINT64_C(0x8000000000000000),
				     UINT64_C(0x0123456789ABCDEF), UINT64_MAX};
	XvtWireU16 b16;
	XvtWireU32 b32;
	XvtWireU64 b64;
	for (size_t i = 0; i < sizeof values16 / sizeof values16[0]; ++i) {
		XvtWire_Set16(b16, values16[i]);
		XVT_ASSERT_INT_EQ(XvtWire_Get16(b16), values16[i]);
	}
	for (size_t i = 0; i < sizeof values32 / sizeof values32[0]; ++i) {
		XvtWire_Set32(b32, values32[i]);
		XVT_ASSERT_INT_EQ(XvtWire_Get32(b32), values32[i]);
	}
	for (size_t i = 0; i < sizeof values64 / sizeof values64[0]; ++i) {
		XvtWire_Set64(b64, values64[i]);
		XVT_ASSERT_TRUE(XvtWire_Get64(b64) == values64[i]);
	}
}

static void CheckIsZero(void)
{
	uint8_t bytes[16];
	memset(bytes, 0, sizeof bytes);
	XVT_ASSERT_INT_EQ(XvtWire_IsZero(bytes, 0), 1);
	XVT_ASSERT_INT_EQ(XvtWire_IsZero(bytes, sizeof bytes), 1);

	/* Any nonzero byte inside the size makes it 0; one just past the size does not count. */
	bytes[sizeof bytes - 1] = 1;
	XVT_ASSERT_INT_EQ(XvtWire_IsZero(bytes, sizeof bytes), 0);
	XVT_ASSERT_INT_EQ(XvtWire_IsZero(bytes, sizeof bytes - 1), 1);
	bytes[0] = 0x80;
	XVT_ASSERT_INT_EQ(XvtWire_IsZero(bytes, 1), 0);
	/* Size 0 is all zero whatever the bytes hold. */
	XVT_ASSERT_INT_EQ(XvtWire_IsZero(bytes, 0), 1);
}

static void CheckCrc32c(void)
{
	/* The check value the header names. */
	XVT_ASSERT_INT_EQ(XvtFlightWire_Crc32c((const uint8_t *)"123456789", 9),
			  0xE3069283u);

	/* With no bytes, the initial value is only inverted back: 0. */
	uint8_t bytes[32];
	XVT_ASSERT_INT_EQ(XvtFlightWire_Crc32c(bytes, 0), 0);

	/* The CRC-32C test vectors of RFC 3720, appendix B.4: 32 bytes of zeros, of ones, and counting up. */
	memset(bytes, 0, sizeof bytes);
	XVT_ASSERT_INT_EQ(XvtFlightWire_Crc32c(bytes, sizeof bytes),
			  0x8A9136AAu);
	memset(bytes, 0xFF, sizeof bytes);
	XVT_ASSERT_INT_EQ(XvtFlightWire_Crc32c(bytes, sizeof bytes),
			  0x62A8AB43u);
	for (unsigned i = 0; i < sizeof bytes; ++i) {
		bytes[i] = (uint8_t)i;
	}
	XVT_ASSERT_INT_EQ(XvtFlightWire_Crc32c(bytes, sizeof bytes),
			  0x46DD794Eu);
}

int main(void)
{
	CheckValidTick();
	CheckLittleEndian();
	CheckRoundTrips();
	CheckIsZero();
	CheckCrc32c();
	return 0;
}
