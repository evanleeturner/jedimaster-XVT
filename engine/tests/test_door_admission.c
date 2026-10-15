/* Checks the network door's admission (xvt_runtime/runtime/door_admission.h)
 * against the promises in its header. Each case reads a made-up description
 * through the real parser, so the seats and the fingerprint are the ones a
 * host would hold: a melee whose seats are Ace (the host), Rook and Wedge. */
#include <stdio.h>
#include <string.h>

#include "test_assert.h"
#include "xvt_runtime/runtime/door_admission.h"

/* The host's description of the made-up melee; seed is its seed. */
static void read_host(struct xvt_launch_description *description, unsigned seed)
{
	char text[1024];
	snprintf(text, sizeof(text),
		 "{\"schema_version\":1,\"mode\":\"network\","
		 "\"game_type\":\"melee\",\"side\":\"none\","
		 "\"mission\":\"melee/m1.tie\",\"pilot\":\"Ace\",\"seed\":%u,"
		 "\"network\":{\"role\":\"host\",\"lobby\":\"https://h\","
		 "\"game\":\"0123abcd-4567-89ef-0123-456789abcdef\","
		 "\"players\":[\"Ace\",\"Rook\",\"Wedge\"]}}",
		 seed);
	char why[XVT_LAUNCH_WHY_CAPACITY];
	XVT_ASSERT_INT_EQ(xvt_launch_description_parse(text, description, why,
						       sizeof(why)),
			  1);
}

static struct xvt_door_seats g_seats;
static uint32_t g_setup;

/* Fresh seats for the made-up melee, the host alone in. */
static void fresh(void)
{
	struct xvt_launch_description description;
	read_host(&description, 7);
	xvt_door_seats_fill(&g_seats, &description);
	g_setup = xvt_launch_setup_fingerprint(&description);
}

static void check_fill(void)
{
	fresh();
	XVT_ASSERT_INT_EQ(g_seats.count, 3);
	XVT_ASSERT_INT_EQ(strcmp(g_seats.names[0], "Ace"), 0);
	XVT_ASSERT_INT_EQ(strcmp(g_seats.names[2], "Wedge"), 0);
	XVT_ASSERT_INT_EQ(g_seats.admitted[0], 1);
	XVT_ASSERT_INT_EQ(g_seats.admitted[1], 0);
	XVT_ASSERT_INT_EQ(g_seats.admitted[2], 0);
	XVT_ASSERT_INT_EQ(g_seats.setup, g_setup);
	XVT_ASSERT_INT_EQ(xvt_door_seats_full(&g_seats), 0);
}

static void check_admit_until_full(void)
{
	fresh();
	int seat = 99;
	XVT_ASSERT_INT_EQ(xvt_door_admit(&g_seats, "wedge", XVT_DOOR_PROTOCOL,
					 g_setup, &seat),
			  XVT_DOOR_ADMIT);
	XVT_ASSERT_INT_EQ(seat, 2);
	XVT_ASSERT_INT_EQ(g_seats.admitted[2], 1);
	XVT_ASSERT_INT_EQ(xvt_door_seats_full(&g_seats), 0);
	XVT_ASSERT_INT_EQ(xvt_door_admit(&g_seats, "Rook", XVT_DOOR_PROTOCOL,
					 g_setup, &seat),
			  XVT_DOOR_ADMIT);
	XVT_ASSERT_INT_EQ(seat, 1);
	XVT_ASSERT_INT_EQ(xvt_door_seats_full(&g_seats), 1);
}

/* Checks an asking is refused with verdict, leaving the seats as they were. */
static void refused(const char *name, int protocol, uint32_t setup,
		    enum xvt_door_verdict verdict)
{
	struct xvt_door_seats before = g_seats;
	int seat = 99;
	XVT_ASSERT_INT_EQ(
		xvt_door_admit(&g_seats, name, protocol, setup, &seat),
		verdict);
	XVT_ASSERT_INT_EQ(seat, -1);
	XVT_ASSERT_INT_EQ(memcmp(&before, &g_seats, sizeof(before)), 0);
}

static void check_refusals(void)
{
	fresh();
	refused("Rook", XVT_DOOR_PROTOCOL + 1, g_setup,
		XVT_DOOR_REFUSE_PROTOCOL);
	refused("Rook", XVT_DOOR_PROTOCOL, g_setup + 1, XVT_DOOR_REFUSE_SETUP);
	refused("Biggs", XVT_DOOR_PROTOCOL, g_setup,
		XVT_DOOR_REFUSE_NOT_LISTED);
	refused("ACE", XVT_DOOR_PROTOCOL, g_setup, XVT_DOOR_REFUSE_HOST_SEAT);
	int seat;
	XVT_ASSERT_INT_EQ(xvt_door_admit(&g_seats, "Rook", XVT_DOOR_PROTOCOL,
					 g_setup, &seat),
			  XVT_DOOR_ADMIT);
	refused("rook", XVT_DOOR_PROTOCOL, g_setup, XVT_DOOR_REFUSE_SEAT_TAKEN);

	/* The order of the checks: a stranger of another protocol is told
	 * about the protocol, and one with another setup about the setup. */
	refused("Biggs", XVT_DOOR_PROTOCOL + 1, g_setup + 1,
		XVT_DOOR_REFUSE_PROTOCOL);
	refused("Biggs", XVT_DOOR_PROTOCOL, g_setup + 1, XVT_DOOR_REFUSE_SETUP);

	/* A joiner whose launcher wrote another seed is told the setup
	 * differs. */
	struct xvt_launch_description other;
	read_host(&other, 8);
	refused("Wedge", XVT_DOOR_PROTOCOL,
		xvt_launch_setup_fingerprint(&other), XVT_DOOR_REFUSE_SETUP);
}

static void check_sentences(void)
{
	const struct {
		int verdict;
		const char *needle;
	} cases[] = {
		{XVT_DOOR_ADMIT, "let this pilot in"},
		{XVT_DOOR_REFUSE_PROTOCOL, "same build"},
		{XVT_DOOR_REFUSE_SETUP, "described another battle"},
		{XVT_DOOR_REFUSE_NOT_LISTED, "not among the host's seats"},
		{XVT_DOOR_REFUSE_SEAT_TAKEN, "already taken"},
		{XVT_DOOR_REFUSE_HOST_SEAT, "host's own seat"},
		{XVT_DOOR_VERDICT_COUNT, "does not know"},
		{-1, "does not know"},
	};
	for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]);
	     ++index) {
		const char *sentence =
			xvt_door_verdict_sentence(cases[index].verdict);
		if (!strstr(sentence, cases[index].needle)) {
			fprintf(stderr, "verdict %d: \"%s\" lacks \"%s\"\n",
				cases[index].verdict, sentence,
				cases[index].needle);
			XVT_ASSERT_TRUE(0);
		}
	}
}

static void check_packets(void)
{
	int32_t packet[XVT_DOOR_PACKET_WORDS];
	xvt_door_make_ask(packet, 0xfedcba98u);
	XVT_ASSERT_INT_EQ(packet[0], 200);
	int protocol = 0;
	uint32_t setup = 0;
	XVT_ASSERT_INT_EQ(xvt_door_read_ask(packet, 12, &protocol, &setup), 1);
	XVT_ASSERT_INT_EQ(protocol, 1);
	XVT_ASSERT_INT_EQ(setup, 0xfedcba98u);
	/* A packet one word short or long, or of the other type, is no
	 * asking. */
	XVT_ASSERT_INT_EQ(xvt_door_read_ask(packet, 8, &protocol, &setup), 0);
	XVT_ASSERT_INT_EQ(xvt_door_read_ask(packet, 16, &protocol, &setup), 0);
	XVT_ASSERT_INT_EQ(xvt_door_read_ask(NULL, 12, &protocol, &setup), 0);
	int verdict = 99;
	int seat = 99;
	XVT_ASSERT_INT_EQ(xvt_door_read_answer(packet, 12, &verdict, &seat), 0);
	XVT_ASSERT_INT_EQ(verdict, 99);

	xvt_door_make_answer(packet, XVT_DOOR_ADMIT, 2);
	XVT_ASSERT_INT_EQ(packet[0], 201);
	XVT_ASSERT_INT_EQ(xvt_door_read_answer(packet, 12, &verdict, &seat), 1);
	XVT_ASSERT_INT_EQ(verdict, XVT_DOOR_ADMIT);
	XVT_ASSERT_INT_EQ(seat, 2);
	/* A refusal carries no seat, whatever the caller passed. */
	xvt_door_make_answer(packet, XVT_DOOR_REFUSE_SETUP, 2);
	XVT_ASSERT_INT_EQ(xvt_door_read_answer(packet, 12, &verdict, &seat), 1);
	XVT_ASSERT_INT_EQ(verdict, XVT_DOOR_REFUSE_SETUP);
	XVT_ASSERT_INT_EQ(seat, -1);
	XVT_ASSERT_INT_EQ(xvt_door_read_answer(packet, 13, &verdict, &seat), 0);
	XVT_ASSERT_INT_EQ(xvt_door_read_ask(packet, 12, &protocol, &setup), 0);
}

int main(void)
{
	check_fill();
	check_admit_until_full();
	check_refusals();
	check_sentences();
	check_packets();
	return 0;
}
