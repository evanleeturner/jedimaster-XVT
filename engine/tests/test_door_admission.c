/* Checks the network door's admission (xvt_runtime/runtime/door_admission.h)
 * against the promises in its header. Each case reads a made-up description
 * through the real parser, so the seats and the fingerprint are the ones a
 * host would hold: a melee whose seats are Ace (the host), Rook and Wedge.
 * The room finder reads made-up matchmaker listings; the start packet is made
 * and read back. */
#include <stdio.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/net/net.h"
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
	/* A game of the build before the start packet speaks version 1. */
	refused("Rook", 1, g_setup, XVT_DOOR_REFUSE_PROTOCOL);
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
	XVT_ASSERT_INT_EQ(packet[0], 67);
	int protocol = 0;
	uint32_t setup = 0;
	XVT_ASSERT_INT_EQ(xvt_door_read_ask(packet, 12, &protocol, &setup), 1);
	XVT_ASSERT_INT_EQ(protocol, 3);
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
	XVT_ASSERT_INT_EQ(packet[0], 78);
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

/* 1 for a door packet's type, 0 for a type the game's own packets use past
 * the resync types (xvt/net/net.h). Its cases are the check that each door
 * type is a number of its own: a switch takes a number once, so this does
 * not compile while a door type repeats one of the game's. */
static int is_door_type(int type)
{
	switch (type) {
	case XVT_DOOR_PACKET_ASK:
	case XVT_DOOR_PACKET_ANSWER:
	case XVT_DOOR_PACKET_START:
		return 1;
	case NET_PACKET_PROBE_REQUEST:
	case NET_PACKET_STATE:
	case NET_PACKET_JOIN_REQUEST:
	case NET_PACKET_GAME_FULL:
	case NET_PACKET_PLAYER_ADMITTED:
	case NET_PACKET_HOST_CANCELLED:
	case NET_PACKET_PLAYER_LEFT:
	case NET_PACKET_TEAM_ASSIGNMENTS_READY:
	case NET_PACKET_FRONTEND_MISSION_START:
	case NET_PACKET_FRONTEND_OPCODE_74:
	case NET_PACKET_NEXT_TOURNAMENT_MISSION:
	case NET_PACKET_NEXT_BATTLE_MISSION:
	case NET_PACKET_CHAT:
	case NET_PACKET_TEAM_ASSIGNMENT:
	case NET_PACKET_FLIGHT_ASSIGNMENT_NOTIFY:
	case NET_PACKET_PLAYER_READY:
	case NET_PACKET_PLAYER_UNREADY:
	case NET_PACKET_RETURN_TO_SETUP:
	case NET_PACKET_CLEAR_TEAM_ASSIGNMENTS:
	case NET_PACKET_TEAM_ASSIGNMENTS:
	case NET_PACKET_CLEAR_FLIGHT_ASSIGNMENTS:
	case NET_PACKET_FLIGHT_ASSIGNMENTS:
	case NET_PACKET_MISSION_CHOICE:
	case NET_PACKET_GAME_OPTIONS:
	case NET_PACKET_REPLAY_MISSION:
	case NET_PACKET_PLAYER_KICKED:
	case NET_PACKET_SESSION_CANCELLED:
	case NET_PACKET_VERSION_MISMATCH:
	case NET_PACKET_PASSWORD_REQUIRED:
	case NET_PACKET_ROSTER_LOCKED:
	case NET_PACKET_LAUNCH_ROSTER_AND_ASSIGNMENTS:
	case NET_PACKET_CRAFT_LOADOUT:
	case NET_PACKET_BRIEFING_ENTERED:
	case NET_PACKET_BRIEFING_COUNTDOWN:
	case NET_PACKET_ASSIGNMENT_COUNTDOWN:
	case NET_PACKET_RETURN_TO_MISSION_SELECTION:
	case NET_PACKET_RELEASE_TEAM_RESERVATION:
	case NET_PACKET_TEAM_RESERVATION:
	case NET_PACKET_PLAYER_UNAVAILABLE:
	case NET_PACKET_RELEASE_FLIGHT_RESERVATION:
	case NET_PACKET_FLIGHT_RESERVATION:
	case NET_PACKET_CHAT_SYNC_REQUEST:
	case NET_PACKET_CHAT_SYNC_CHUNK:
	case NET_PACKET_FLIGHT_ASSIGNMENT:
	case NET_PACKET_LOBBY_SELECTION:
	case NET_PACKET_FLIGHT_ASSIGNMENTS_READY:
	case NET_PACKET_LOADOUT_ROSTER:
	case NET_PACKET_PROBE_RESPONSE:
	case NET_PACKET_READY_ROSTER:
	case NET_PACKET_PILOT_RATING:
	case NET_PACKET_REPLAY_CURRENT_MISSION:
	case NET_PACKET_BATTLE_CONTINUATION:
	case NET_PACKET_BATTLE_PROGRESS:
	case NET_PACKET_NEXT_CAMPAIGN_MISSION:
	case NET_PACKET_REPLAY_CAMPAIGN_MISSION:
	case NET_PACKET_CAMPAIGN_CONTINUATION:
	case NET_PACKET_MOVIE_SYNC:
	case NET_PACKET_SUBMIT_MISSION_CHOICE:
	default:
		return 0;
	}
}

/* The door's packets reach the other game with their own types: each type is
 * past the resync types, which travel without a length word, under 128,
 * since the wire keeps a type's low 7 bits, and a number of its own. */
static void check_packet_types(void)
{
	const int types[] = {XVT_DOOR_PACKET_ASK, XVT_DOOR_PACKET_ANSWER,
			     XVT_DOOR_PACKET_START};
	for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
		XVT_ASSERT_TRUE(types[i] > NET_PACKET_RESYNC_CHUNK);
		XVT_ASSERT_TRUE(types[i] < 128);
		XVT_ASSERT_INT_EQ(is_door_type(types[i]), 1);
	}
	XVT_ASSERT_INT_EQ(is_door_type(NET_PACKET_FRONTEND_OPCODE_74), 0);
}

/* A game that left frees its seat for another game of that pilot; the
 * host's seat and a seat outside the count stay as they are. */
static void check_seat_free(void)
{
	fresh();
	int seat;
	XVT_ASSERT_INT_EQ(xvt_door_admit(&g_seats, "Rook", XVT_DOOR_PROTOCOL,
					 g_setup, &seat),
			  XVT_DOOR_ADMIT);
	XVT_ASSERT_INT_EQ(xvt_door_admit(&g_seats, "Wedge", XVT_DOOR_PROTOCOL,
					 g_setup, &seat),
			  XVT_DOOR_ADMIT);
	XVT_ASSERT_INT_EQ(xvt_door_seats_full(&g_seats), 1);
	xvt_door_seat_free(&g_seats, 1);
	XVT_ASSERT_INT_EQ(xvt_door_seats_full(&g_seats), 0);
	XVT_ASSERT_INT_EQ(g_seats.admitted[2], 1);
	XVT_ASSERT_INT_EQ(xvt_door_admit(&g_seats, "rook", XVT_DOOR_PROTOCOL,
					 g_setup, &seat),
			  XVT_DOOR_ADMIT);
	XVT_ASSERT_INT_EQ(seat, 1);
	xvt_door_seat_free(&g_seats, 0);
	xvt_door_seat_free(&g_seats, 3);
	xvt_door_seat_free(&g_seats, -1);
	XVT_ASSERT_INT_EQ(g_seats.admitted[0], 1);
	XVT_ASSERT_INT_EQ(xvt_door_seats_full(&g_seats), 1);
}

static void check_start_packet(void)
{
	struct xvt_door_start start;
	memset(&start, 0, sizeof(start));
	start.count = 3;
	const uint32_t players[3] = {0x10u, 0x8000002au, 0x33u};
	const int32_t ratings[3] = {4, 0, -1};
	for (int seat = 0; seat < 3; ++seat) {
		start.players[seat] = players[seat];
		start.ratings[seat] = ratings[seat];
	}
	int32_t packet[XVT_DOOR_START_WORDS];
	memset(packet, 0x55, sizeof(packet));
	xvt_door_make_start(packet, &start);
	const uint32_t size = sizeof(packet);
	XVT_ASSERT_INT_EQ(size, 72);
	XVT_ASSERT_INT_EQ(packet[0], 93);
	XVT_ASSERT_INT_EQ(packet[1], 3);
	XVT_ASSERT_INT_EQ(packet[2], 0x10);
	XVT_ASSERT_INT_EQ(packet[3], 4);
	XVT_ASSERT_INT_EQ(packet[4], (int32_t)0x8000002au);
	XVT_ASSERT_INT_EQ(packet[7], -1);
	/* The words past the seats are zero. */
	for (int word = 8; word < XVT_DOOR_START_WORDS; ++word) {
		XVT_ASSERT_INT_EQ(packet[word], 0);
	}

	struct xvt_door_start read;
	memset(&read, 0x55, sizeof(read));
	XVT_ASSERT_INT_EQ(xvt_door_read_start(packet, size, &read), 1);
	XVT_ASSERT_INT_EQ(read.count, 3);
	for (int seat = 0; seat < 3; ++seat) {
		XVT_ASSERT_INT_EQ(read.players[seat], players[seat]);
		XVT_ASSERT_INT_EQ(read.ratings[seat], ratings[seat]);
	}
	XVT_ASSERT_INT_EQ(read.players[3], 0);
	XVT_ASSERT_INT_EQ(read.ratings[7], 0);

	/* A packet of another size or type is no start, and leaves start as
	 * it was. */
	memset(&read, 0, sizeof(read));
	XVT_ASSERT_INT_EQ(xvt_door_read_start(packet, size - 4, &read), 0);
	XVT_ASSERT_INT_EQ(xvt_door_read_start(packet, size + 4, &read), 0);
	XVT_ASSERT_INT_EQ(xvt_door_read_start(NULL, size, &read), 0);
	packet[0] = XVT_DOOR_PACKET_ANSWER;
	XVT_ASSERT_INT_EQ(xvt_door_read_start(packet, size, &read), 0);
	packet[0] = XVT_DOOR_PACKET_START;
	/* A count of no seats. */
	packet[1] = 0;
	XVT_ASSERT_INT_EQ(xvt_door_read_start(packet, size, &read), 0);
	/* A seat under the count without a player. */
	packet[1] = 3;
	packet[6] = 0;
	XVT_ASSERT_INT_EQ(xvt_door_read_start(packet, size, &read), 0);
	XVT_ASSERT_INT_EQ(read.count, 0);
	/* With the count at 2 that seat is past it, and the start reads. */
	packet[1] = 2;
	XVT_ASSERT_INT_EQ(xvt_door_read_start(packet, size, &read), 1);
	XVT_ASSERT_INT_EQ(read.count, 2);
	XVT_ASSERT_INT_EQ(read.players[2], 0);

	/* Eight seats fill the packet. */
	start.count = XVT_LAUNCH_SEAT_CAPACITY;
	for (int seat = 0; seat < XVT_LAUNCH_SEAT_CAPACITY; ++seat) {
		start.players[seat] = (uint32_t)(seat + 1);
		start.ratings[seat] = seat;
	}
	xvt_door_make_start(packet, &start);
	XVT_ASSERT_INT_EQ(packet[XVT_DOOR_START_WORDS - 2], 8);
	XVT_ASSERT_INT_EQ(packet[XVT_DOOR_START_WORDS - 1], 7);
	XVT_ASSERT_INT_EQ(xvt_door_read_start(packet, size, &read), 1);
	XVT_ASSERT_INT_EQ(read.players[7], 8);

	/* Nine seats are more than a battle has, though every seat holds a
	 * player and a ninth pair of words follows the packet. */
	int32_t longer[XVT_DOOR_START_WORDS + 2];
	memcpy(longer, packet, sizeof(packet));
	longer[1] = XVT_LAUNCH_SEAT_CAPACITY + 1;
	longer[XVT_DOOR_START_WORDS] = 9;
	longer[XVT_DOOR_START_WORDS + 1] = 9;
	memset(&read, 0, sizeof(read));
	XVT_ASSERT_INT_EQ(xvt_door_read_start(longer, size, &read), 0);
	XVT_ASSERT_INT_EQ(read.count, 0);
}

/* A matchmaker's listing, too big for the stack. */
static AeronDplayDirectorySnapshot g_listing;

/* Puts room index in the listing: joinable or not, with players names. */
static void list_room(unsigned index, int joinable, unsigned players,
		      const char *const *names)
{
	AeronDplayRoomMetadata *room = &g_listing.rooms[index].metadata;
	memset(room, 0, sizeof(*room));
	room->joinable = (uint8_t)joinable;
	room->players = (uint8_t)players;
	room->max_players = 8;
	for (unsigned player = 0; player < players; ++player) {
		snprintf(room->roster[player].name,
			 sizeof(room->roster[player].name), "%s",
			 names[player]);
	}
}

static void check_find_room(void)
{
	static const char *const others[] = {"Han", "Leia"};
	static const char *const ace_second[] = {"Rook", "ace"};
	static const char *const ace_first[] = {"Ace"};
	memset(&g_listing, 0, sizeof(g_listing));
	/* An empty listing holds no room. */
	XVT_ASSERT_INT_EQ(xvt_door_find_room(&g_listing, "Ace"), -1);
	/* A room that takes no joiners is passed over, and so is one whose
	 * players do not include the host. */
	list_room(0, 0, 1, ace_first);
	list_room(1, 1, 2, others);
	g_listing.room_count = 2;
	XVT_ASSERT_INT_EQ(xvt_door_find_room(&g_listing, "Ace"), -1);
	/* The host found anywhere among a room's players, in any letter
	 * case. */
	list_room(2, 1, 2, ace_second);
	g_listing.room_count = 3;
	XVT_ASSERT_INT_EQ(xvt_door_find_room(&g_listing, "Ace"), 2);
	/* Of two rooms that list the host, the first. */
	list_room(3, 1, 1, ace_first);
	g_listing.room_count = 4;
	XVT_ASSERT_INT_EQ(xvt_door_find_room(&g_listing, "ACE"), 2);
	/* A name past the room's player count is not one of its players. */
	g_listing.rooms[2].metadata.players = 1;
	XVT_ASSERT_INT_EQ(xvt_door_find_room(&g_listing, "Ace"), 3);
	/* Rooms past the listing's count are not in it. */
	g_listing.room_count = 3;
	XVT_ASSERT_INT_EQ(xvt_door_find_room(&g_listing, "Ace"), -1);
}

int main(void)
{
	check_fill();
	check_admit_until_full();
	check_refusals();
	check_sentences();
	check_packets();
	check_packet_types();
	check_seat_free();
	check_start_packet();
	check_find_room();
	return 0;
}
