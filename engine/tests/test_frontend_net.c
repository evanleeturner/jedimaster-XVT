/* Tests for xvt/net/frontend_net.c, the multiplayer frontend's packet handler:
 * the host's answers to a join request and the chat log rebuilt from synced
 * chunks. Each check clears the frontend state, makes this player the host or a
 * client of a lobby with no DirectPlay interface, and puts the packet to handle
 * at the head of the lobby's receive queue, as the next one expected from
 * player 200. The answers sent go nowhere, so the checks read the answer the
 * handler built in g_frontend_net_packet_scratch. No game data is read.
 *
 * Not checked here: the lobby screens, which need the frontend's fonts and
 * images, and the other packets, whose stores need a running lobby. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "test_assert.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/net/frontend_net.h"
#include "xvt/net/net.h"
#include "xvt/net/net_reliable.h"

enum {
	PEER_DPID = 200,
	CHAT_LOG_BYTES = 1024,
};

/* A lobby with this player as host or client, no players ready, no password
 * and the roster open, and an empty receive queue. */
static void lobby_world(int host)
{
	memset(&g_front_state, 0, sizeof g_front_state);
	g_front_state.net_is_host = host;
	g_front_state.net_host_player_id = host ? 1 : PEER_DPID;
	g_game_config.require_password = 0;
	g_mission_setup_roster_authoritative = 0;
	memset(&g_frontend_net_packet_scratch, 0,
	       sizeof g_frontend_net_packet_scratch);
}

/* Queues a packet of size bytes from player 200 as the next one expected on
 * its direct channel, and handles it; returns the handler's result. */
static int handle_packet(const int *packet, uint32_t size)
{
	struct net_queued_packet *entry =
		&g_front_state.net_runtime_recv_queue
			 [g_front_state.net_runtime_recv_queue_write_index];
	memset(entry, 0, sizeof *entry);
	entry->direct_play_id = PEER_DPID;
	entry->packet_class = 1;
	entry->sequence_byte = 0;
	entry->payload_size = size;
	memcpy(entry->payload, packet, size);
	++g_front_state.net_runtime_recv_queue_write_index;
	++g_front_state.net_runtime_recv_queue_count;
	return frontend_net_process_network_packets();
}

/* A join request with the given protocol version and password. */
static int join_request(int version, const char *password)
{
	int packet[6];
	memset(packet, 0, sizeof packet);
	packet[0] = NET_PACKET_JOIN_REQUEST;
	packet[1] = version;
	strncpy((char *)&packet[2], password, 4 * sizeof(int));
	return handle_packet(packet, sizeof packet);
}

/* ------------------------------------------------------------------------ */
/* Join requests. */

/* The host refuses a request with the wrong protocol version, one with the
 * wrong password, and every request while the roster is locked or 8 players
 * are admitted, each with its own answer. */
static void check_join_refusals(void)
{
	lobby_world(1);
	join_request(FRONTEND_NET_PROTOCOL_VERSION - 1, "");
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_VERSION_MISMATCH);

	lobby_world(1);
	g_game_config.require_password = 1;
	strncpy(g_game_config.password, "yoda", sizeof g_game_config.password);
	join_request(FRONTEND_NET_PROTOCOL_VERSION, "vader");
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_PASSWORD_REQUIRED);
	g_game_config.require_password = 0;

	/* Full is answered before the version is looked at: with 8 players
	 * ready a request with the wrong version is refused as full, with 7 for
	 * its version. */
	lobby_world(1);
	for (int i = 0; i < 8; ++i) {
		g_front_state.net_players[i].ready_flag = 1;
	}
	join_request(FRONTEND_NET_PROTOCOL_VERSION - 1, "");
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_GAME_FULL);
	lobby_world(1);
	for (int i = 0; i < 7; ++i) {
		g_front_state.net_players[i].ready_flag = 1;
	}
	join_request(FRONTEND_NET_PROTOCOL_VERSION - 1, "");
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_VERSION_MISMATCH);

	lobby_world(1);
	g_mission_setup_roster_authoritative = 1;
	join_request(FRONTEND_NET_PROTOCOL_VERSION, "");
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type,
			  NET_PACKET_ROSTER_LOCKED);
	g_mission_setup_roster_authoritative = 0;
}

/* A request reaching a client, or of the wrong size, is dropped unanswered. */
static void check_join_dropped(void)
{
	lobby_world(0);
	XVT_ASSERT_INT_EQ(join_request(FRONTEND_NET_PROTOCOL_VERSION - 1, ""),
			  NET_PACKET_NONE);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type, 0);

	lobby_world(1);
	int packet[5] = {NET_PACKET_JOIN_REQUEST, 0, 0, 0, 0};
	XVT_ASSERT_INT_EQ(handle_packet(packet, sizeof packet),
			  NET_PACKET_NONE);
	XVT_ASSERT_INT_EQ(g_frontend_net_packet_scratch.packet_type, 0);
}

/* With no packet waiting the handler returns NET_PACKET_NONE. */
static void check_no_packet(void)
{
	lobby_world(1);
	XVT_ASSERT_INT_EQ(frontend_net_process_network_packets(),
			  NET_PACKET_NONE);
}

/* ------------------------------------------------------------------------ */
/* The chat log. */

static char *g_chat_log;

/* Gives the frontend a chat log of 1,024 bytes, full of 'x'. */
static void hold_chat_log(void)
{
	free(g_chat_log);
	g_chat_log = malloc(CHAT_LOG_BYTES);
	XVT_ASSERT_TRUE(g_chat_log != NULL);
	memset(g_chat_log, 'x', CHAT_LOG_BYTES);
	g_frontend_chat_log_buffer = g_chat_log;
	g_frontend_chat_log_used_bytes = 0;
}

/* A chat sync chunk with the given index and text. */
static int chat_chunk(int chunk_index, const char *text, size_t length)
{
	int packet[2 + 400 / sizeof(int)];
	memset(packet, 0, sizeof packet);
	packet[0] = NET_PACKET_CHAT_SYNC_CHUNK;
	packet[1] = chunk_index;
	memcpy(&packet[2], text, length);
	return handle_packet(packet, (uint32_t)(2 * sizeof(int) + length));
}

/* Chunk 0 clears the log and starts it; chunk 1 goes 400 bytes in. Every byte
 * from 0 to 6 becomes color code 6. */
static void check_chat_chunks(void)
{
	lobby_world(0);
	hold_chat_log();
	XVT_ASSERT_INT_EQ(chat_chunk(0, "\005Hi\001", 4),
			  NET_PACKET_CHAT_SYNC_CHUNK);
	XVT_ASSERT_INT_EQ(memcmp(g_chat_log, "\006Hi\006", 4), 0);
	XVT_ASSERT_INT_EQ(g_chat_log[4], 0);
	XVT_ASSERT_INT_EQ(g_chat_log[CHAT_LOG_BYTES - 1], 0);
	XVT_ASSERT_INT_EQ(g_frontend_chat_log_used_bytes, 4);
	lobby_world(0);
	XVT_ASSERT_INT_EQ(chat_chunk(1, "Yes", 3), NET_PACKET_CHAT_SYNC_CHUNK);
	XVT_ASSERT_INT_EQ(memcmp(g_chat_log + 400, "Yes", 3), 0);
	XVT_ASSERT_INT_EQ(memcmp(g_chat_log, "\006Hi\006", 4), 0);
	XVT_ASSERT_INT_EQ(g_frontend_chat_log_used_bytes, 7);
}

/* ------------------------------------------------------------------------ */
/* Known failures. */

/* Known failure join_refused_as_full, issue #111: the comment says a join
 * request is refused as full when 8 players are admitted. None is; player 200
 * passes every check but is not in the host's player list, so admitting it
 * fails, and the host answers GAME_FULL anyway. */
static void check_join_not_full(void)
{
	lobby_world(1);
	join_request(FRONTEND_NET_PROTOCOL_VERSION, "");
	XVT_ASSERT_TRUE(g_frontend_net_packet_scratch.packet_type !=
			NET_PACKET_GAME_FULL);
}

/* Known failure chat_chunk_without_log, issue #22: the comment states the
 * handler does not check the chat log for NULL before a sync chunk. With no
 * log held, chunk 0 clears a NULL log and the program stops; a chunk should
 * be dropped when there is no log to hold it. */
static void check_chat_chunk_without_log(void)
{
	lobby_world(0);
	g_frontend_chat_log_buffer = NULL;
	chat_chunk(0, "Hi", 2);
	XVT_ASSERT_TRUE(g_frontend_chat_log_buffer == NULL);
}

/* Known failure chat_chunk_past_log, issue #22: the comment states the
 * handler does not check chunk indices. Chunk 3 of a 1,024-byte log is copied
 * 1,200 bytes in, past its end, and the sanitizer stops the program on the
 * write; nothing past the log should be written. */
static void check_chat_chunk_past_log(void)
{
	char text[400];
	memset(text, 'a', sizeof text);
	lobby_world(0);
	hold_chat_log();
	chat_chunk(3, text, sizeof text);
	XVT_ASSERT_INT_EQ(g_frontend_chat_log_used_bytes, 0);
}

int main(int argc, char **argv)
{
	/* "known-failure <check>" runs one check the code is known to fail; an
	 * unknown name runs nothing. */
	if (argc == 3 && strcmp(argv[1], "known-failure") == 0) {
		static const struct {
			const char *name;
			void (*check)(void);
		} known_failures[] = {
			{"join_refused_as_full", check_join_not_full},
			{"chat_chunk_without_log",
			 check_chat_chunk_without_log},
			{"chat_chunk_past_log", check_chat_chunk_past_log},
		};
		for (size_t i = 0;
		     i < sizeof known_failures / sizeof known_failures[0];
		     ++i) {
			if (strcmp(argv[2], known_failures[i].name) == 0) {
				known_failures[i].check();
			}
		}
		return 0;
	}
	check_no_packet();
	check_join_refusals();
	check_join_dropped();
	check_chat_chunks();
	free(g_chat_log);
	g_frontend_chat_log_buffer = NULL;
	return 0;
}
