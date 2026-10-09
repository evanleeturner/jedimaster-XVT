/* The exchanges before flight (flight_network_exchange.h): roster, options
 * and taunts, and mission start, and the mission cookie they agree on. The
 * in-flight half is flight_network.c. */
#include "xvt_runtime/runtime/flight_network_exchange.h"

#include "xvt/net/net_session_receive.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/flight_internal.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/timing/host_clock.h"

struct xvt_flight_taunts_wire {
	struct xvt_flight_slot_wire header;
	uint8_t text[sizeof(g_game_config.taunts)];
	struct xvt_flight_agreement_wire agreement;
};

static void
xvt_flight_network_write_agreement(struct xvt_flight_agreement_wire *agreement);
static int xvt_flight_network_matches_agreement(
	const struct xvt_flight_agreement_wire *agreement);

enum {
	SYNC_IDLE,
	SYNC_HOST_PLAYERS,
	SYNC_ROSTER_SEND,
	SYNC_ROSTER_COUNT,
	SYNC_ROSTER_PLAYERS,
	SYNC_OPTIONS_HOST,
	SYNC_OPTIONS_ROSTER,
	SYNC_TAUNTS,
	SYNC_START_HOST,
	SYNC_START_PACKET,
	SYNC_START_ACKS
};

static struct {
	unsigned acknowledged_mask;
	unsigned taunts_seen;
	struct xvt_flight_agreement_wire taunt_agreement[XVT_FLIGHT_PLAYERS];
	int phase;
	int answer_count;
	int expected;
	int roster_index;
	int packet_type;
	int alert;
	int blink;
	uint64_t packet_deadline;
	uint64_t status_time;
} g_sync;

static uint32_t g_cookie_counter;
static uint32_t g_mission_cookie;

uint32_t xvt_flight_network_cookie(void) { return g_mission_cookie; }

void xvt_flight_network_clear_cookies(void)
{
	g_cookie_counter = 0;
	g_mission_cookie = 0;
}

static int xvt_flight_network_begin_agreement(void)
{
	if (g_cookie_counter == UINT32_MAX) {
		xvt_network_session_leave();
		return 0;
	}
	g_mission_cookie = ++g_cookie_counter;
	return 1;
}

static int xvt_flight_network_record_acknowledgement(int sender, int slot)
{
	if ((unsigned)slot >= 8 ||
	    net_session_find_player_slot_by_dpid(sender) != slot ||
	    g_players[slot].network.direct_play_id != sender ||
	    (g_sync.acknowledged_mask & (1u << slot))) {
		return 0;
	}
	g_sync.acknowledged_mask |= 1u << slot;
	++g_sync.answer_count;
	XVT_LOG_DEBUG("flight.sync_ack phase=%d slot=%d count=%d", g_sync.phase,
		      slot, g_sync.answer_count);
	return 1;
}

static void xvt_flight_network_set_packet_deadline(int seconds);

static void
xvt_flight_network_write_agreement(struct xvt_flight_agreement_wire *agreement)
{
	xvt_wire_set32(agreement->schema, XVT_TIMING_SCHEMA);
	xvt_wire_set32(agreement->profile, XVT_WIRE_PROFILE_NETWORK_125);
	xvt_wire_set32(agreement->cookie, g_mission_cookie);
}

static int xvt_flight_network_matches_agreement(
	const struct xvt_flight_agreement_wire *agreement)
{
	return xvt_wire_get32(agreement->schema) == XVT_TIMING_SCHEMA &&
	       xvt_wire_get32(agreement->profile) ==
		       XVT_WIRE_PROFILE_NETWORK_125 &&
	       xvt_wire_get32(agreement->cookie) == g_mission_cookie;
}

static void xvt_flight_network_send_taunts(void)
{
	struct xvt_flight_taunts_wire packet;
	xvt_wire_set32(packet.header.opcode, NET_PACKET_PLAYER_TAUNTS);
	xvt_wire_set32(packet.header.player, g_local_player);
	memcpy(packet.text, g_game_config.taunts, sizeof packet.text);
	xvt_flight_network_write_agreement(&packet.agreement);
	g_sync.answer_count = 0;
	g_sync.acknowledged_mask = 0;
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		if ((g_sync.taunts_seen & (1u << player)) &&
		    xvt_flight_network_matches_agreement(
			    &g_sync.taunt_agreement[player])) {
			g_sync.acknowledged_mask |= 1u << player;
			++g_sync.answer_count;
		}
	}
	xvt_flight_network_send_wire(0, &packet, sizeof packet);
	g_sync.phase = SYNC_TAUNTS;
	xvt_flight_network_set_packet_deadline(30);
}

static int xvt_flight_network_finish(int result)
{
	if (g_sync.alert || g_sync.phase == SYNC_TAUNTS) {
		flight_alert_restore_box_background();
	}
	memset(&g_sync, 0, sizeof(g_sync));
	return result;
}

/* Says which pre-flight wait ran out and how many answers it had, then ends the wait failed. */
static int xvt_flight_network_give_up(const char *stage)
{
	XVT_LOG_WARN(
		"flight.sync_timeout stage=\"%s\" phase=%d count=%d expected=%d",
		stage, g_sync.phase, g_sync.answer_count, g_sync.expected);
	return xvt_flight_network_finish(0);
}

static void xvt_flight_network_set_packet_deadline(int seconds)
{
	g_sync.packet_deadline =
		xvt_time_get_elapsed_us() / 1000 + (unsigned)seconds * 1000;
}

static int *xvt_flight_network_poll(int *sender, int *size, int seconds)
{
	int *packet = net_session_receive_game_packet(sender, size);
	if (packet) {
		xvt_flight_network_set_packet_deadline(seconds);
	}
	return packet;
}

static int xvt_flight_network_expired(void)
{
	return xvt_time_get_elapsed_us() / 1000 > g_sync.packet_deadline;
}

static void xvt_flight_network_alert(void)
{
	flight_alert_save_box_background();
	g_sync.alert = 1;
	g_sync.blink = 1;
	flight_alert_draw_box(
		1,
		g_str_disk_io_messages[DISK_IO_STR_WAITING_FOR_OTHER_PLAYERS],
		0x30);
}

static void xvt_flight_network_loading_status(int sender)
{
	uint64_t now = xvt_time_get_elapsed_us() / 1000;
	if (now - g_sync.status_time <= 200) {
		return;
	}
	g_sync.status_time = now;
	char *name = net_session_get_player_name(
		net_session_find_player_slot_by_dpid(sender));
	char text[256];
	if (!name) {
		strcpy(text, g_str_disk_io_messages
				     [DISK_IO_STR_OTHER_PLAYERS_STILL_LOADING]);
	} else {
		strcpy(text, name);
		g_sync.blink = !g_sync.blink;
		strcat(text,
		       g_str_disk_io_messages
			       [g_sync.blink
					? DISK_IO_STR_PLAYER_STILL_LOADING_MINUS
					: DISK_IO_STR_PLAYER_STILL_LOADING_PLUS]);
	}
	flight_alert_draw_box(3, text, 0x30);
}

int xvt_flight_network_begin_roster_exchange(int player_count, int in_progress)
{
	xvt_network_session_mark_flight_ready();
	memset(&g_sync, 0, sizeof(g_sync));
	g_sync.expected = player_count;
	xvt_flight_network_set_packet_deadline(60);
	if (net_session_is_local_host()) {
		g_sync.phase =
			player_count ? SYNC_HOST_PLAYERS : SYNC_ROSTER_SEND;
	} else {
		if (in_progress) {
			return 1;
		}
		g_sync.phase = SYNC_ROSTER_COUNT;
	}
	return XVT_FLIGHT_NETWORK_PENDING;
}

static void xvt_flight_network_send_roster_record(void)
{
	int index = g_sync.roster_index;
	if (index < 0) {
		g_net_session_scratch_packet.packet_type =
			NET_PACKET_ROSTER_COUNT;
		g_net_session_scratch_packet.payload_dwords[0] =
			g_net_session.player_count;
		xvt_flight_network_send_packet(
			0, (unsigned *)&g_net_session_scratch_packet, 8);
		g_sync.packet_type = NET_PACKET_ROSTER_COUNT;
	} else {
		g_net_session_scratch_packet.packet_type =
			NET_PACKET_ROSTER_ENTRY;
		g_net_session_scratch_packet.payload_dwords[0] = index;
		memcpy(&g_net_session_scratch_packet.payload_dwords[1],
		       &g_net_session.players[index],
		       sizeof(struct session_player_info));
		xvt_flight_network_send_packet(
			0, (unsigned *)&g_net_session_scratch_packet, 48);
		g_sync.packet_type = NET_PACKET_ROSTER_ENTRY;
	}
	xvt_flight_network_set_packet_deadline(60);
}

int xvt_flight_network_exchange_roster(void)
{
	if (g_sync.phase == SYNC_HOST_PLAYERS &&
	    g_sync.answer_count >= g_sync.expected) {
		g_sync.phase = SYNC_ROSTER_SEND;
		g_sync.roster_index = -1;
	}
	if (g_sync.phase == SYNC_ROSTER_SEND) {
		if (g_sync.expected <= 1) {
			return xvt_flight_network_finish(1);
		}
		if (!g_sync.packet_type) {
			xvt_flight_network_send_roster_record();
		}
	}
	int sender;
	int size;
	int *packet = xvt_flight_network_poll(&sender, &size, 60);
	if (!packet) {
		if (!xvt_flight_network_expired()) {
			return XVT_FLIGHT_NETWORK_PENDING;
		}
		if (g_sync.phase != SYNC_ROSTER_PLAYERS) {
			g_net_session.dplay_interface = NULL;
		}
		return xvt_flight_network_give_up("roster");
	}
	if (size < 4) {
		return XVT_FLIGHT_NETWORK_PENDING;
	}
	if (g_sync.phase == SYNC_HOST_PLAYERS &&
	    packet[0] == NET_PACKET_STARTUP_READY) {
		++g_sync.answer_count;
	} else if (g_sync.phase == SYNC_ROSTER_SEND &&
		   packet[0] == g_sync.packet_type) {
		++g_sync.roster_index;
		g_sync.packet_type = NET_PACKET_NONE;
		if (g_sync.roster_index >= g_net_session.player_count) {
			g_net_session_scratch_packet.packet_type =
				NET_PACKET_NOP;
			xvt_flight_network_send_packet(
				0, (unsigned *)&g_net_session_scratch_packet,
				4);
			return xvt_flight_network_finish(1);
		}
	} else if (g_sync.phase == SYNC_ROSTER_COUNT &&
		   packet[0] == NET_PACKET_ROSTER_COUNT && size >= 8 &&
		   packet[1] >= 0 && packet[1] <= 8) {
		g_net_session.player_count = packet[1];
		g_sync.phase = SYNC_ROSTER_PLAYERS;
		g_sync.answer_count = 0;
		if (!g_net_session.player_count) {
			return xvt_flight_network_finish(1);
		}
	} else if (g_sync.phase == SYNC_ROSTER_PLAYERS &&
		   packet[0] == NET_PACKET_ROSTER_ENTRY &&
		   size >= 8 + (int)sizeof(struct session_player_info) &&
		   (unsigned)packet[1] < 8) {
		memcpy(&g_net_session.players[packet[1]], packet + 2,
		       sizeof(struct session_player_info));
		if (++g_sync.answer_count >= g_net_session.player_count) {
			return xvt_flight_network_finish(1);
		}
	}
	return XVT_FLIGHT_NETWORK_PENDING;
}

static void xvt_flight_network_send_options(void)
{
	struct xvt_flight_roster_header header;
	xvt_wire_set32(header.opcode, NET_PACKET_PLAYER_OPTIONS_ROSTER);
	xvt_wire_set32(header.new_net, g_flight_conf_new_net);
	uint8_t bytes[sizeof(struct xvt_flight_roster_header) +
		      XVT_FLIGHT_PLAYERS *
			      sizeof(struct xvt_flight_roster_player_wire) +
		      sizeof(struct xvt_flight_agreement_wire)];
	memcpy(bytes, &header, sizeof header);
	size_t offset = sizeof header;
	for (int player = 0; player < g_active_flight_player_count; ++player) {
		struct xvt_flight_roster_player_wire record;
		xvt_wire_set32(
			record.resolution,
			g_players[player].network.flight_resolution_mode);
		xvt_wire_set32(record.rating, g_players[player].pilot_rating);
		memcpy(bytes + offset, &record, sizeof record);
		offset += sizeof record;
	}
	struct xvt_flight_agreement_wire agreement;
	xvt_flight_network_write_agreement(&agreement);
	memcpy(bytes + offset, &agreement, sizeof agreement);
	offset += sizeof agreement;
	xvt_flight_network_broadcast_wire(bytes, offset);
	g_sync.phase = SYNC_OPTIONS_ROSTER;
	xvt_flight_network_set_packet_deadline(60);
}

static int xvt_flight_network_read_taunts(int sender, const void *bytes,
					  unsigned size)
{
	if (size != sizeof(struct xvt_flight_taunts_wire)) {
		return 0;
	}
	struct xvt_flight_taunts_wire packet;
	memcpy(&packet, bytes, sizeof packet);
	unsigned slot = xvt_wire_get32(packet.header.player);
	if (slot >= XVT_FLIGHT_PLAYERS ||
	    net_session_find_player_slot_by_dpid(sender) != (int)slot ||
	    g_players[slot].network.direct_play_id != sender ||
	    xvt_wire_get32(packet.agreement.schema) != XVT_TIMING_SCHEMA ||
	    xvt_wire_get32(packet.agreement.profile) !=
		    XVT_WIRE_PROFILE_NETWORK_125 ||
	    !xvt_wire_get32(packet.agreement.cookie)) {
		return 0;
	}
	if (g_sync.phase == SYNC_OPTIONS_ROSTER) {
		g_sync.taunts_seen |= 1u << slot;
		g_sync.taunt_agreement[slot] = packet.agreement;
		memcpy(g_player_taunt_text[slot], packet.text,
		       sizeof packet.text);
	} else if (g_sync.phase == SYNC_TAUNTS &&
		   xvt_flight_network_matches_agreement(&packet.agreement) &&
		   xvt_flight_network_record_acknowledgement(sender, slot)) {
		memcpy(g_player_taunt_text[slot], packet.text,
		       sizeof packet.text);
	}
	return 1;
}

static int xvt_flight_network_accept_roster(int sender, const void *packet,
					    unsigned size)
{
	size_t offset = sizeof(struct xvt_flight_roster_header) +
			g_active_flight_player_count *
				sizeof(struct xvt_flight_roster_player_wire);
	if (sender != net_session_get_host_dplay_id() ||
	    size != offset + sizeof(struct xvt_flight_agreement_wire)) {
		return 0;
	}
	struct xvt_flight_agreement_wire agreement;
	const uint8_t *bytes = packet;
	memcpy(&agreement, bytes + offset, sizeof agreement);
	if (xvt_wire_get32(agreement.schema) != XVT_TIMING_SCHEMA ||
	    xvt_wire_get32(agreement.profile) != XVT_WIRE_PROFILE_NETWORK_125 ||
	    !xvt_wire_get32(agreement.cookie)) {
		return 0;
	}
	g_mission_cookie = xvt_wire_get32(agreement.cookie);
	if (!net_session_is_local_host()) {
		flight_alert_restore_box_background();
		g_sync.alert = 0;
		struct xvt_flight_roster_header header;
		memcpy(&header, bytes, sizeof header);
		g_flight_conf_new_net = xvt_wire_get32(header.new_net);
		for (int player = 0; player < g_active_flight_player_count;
		     ++player) {
			struct xvt_flight_roster_player_wire record;
			memcpy(&record,
			       bytes + sizeof header + player * sizeof record,
			       sizeof record);
			g_players[player].network.flight_resolution_mode =
				xvt_wire_get32(record.resolution);
			g_players[player].pilot_rating =
				xvt_wire_get32(record.rating);
		}
	}
	xvt_flight_network_send_taunts();
	return 1;
}

int xvt_flight_network_exchange_options(void)
{
	if (!g_sync.phase) {
		if (net_session_is_local_host() &&
		    !xvt_flight_network_begin_agreement()) {
			return 0;
		}
		if (g_active_flight_player_count <= 1) {
			g_players[0].network.flight_resolution_mode =
				g_flight_resolution_mode;
			g_players[0].pilot_rating = g_pilot_data.rating;
			memcpy(g_player_taunt_text, g_game_config.taunts,
			       sizeof(g_game_config.taunts));
			return 1;
		}
		if (net_session_is_local_host()) {
			for (int i = 0; i < g_active_flight_player_count; ++i) {
				g_players[i].network.flight_resolution_mode =
					FLIGHT_RESOLUTION_320X240;
				g_players[i].pilot_rating = 0;
			}
			net_session_count_active_players();
			g_players[g_local_player]
				.network.flight_resolution_mode =
				g_flight_resolution_mode;
			g_players[g_local_player].pilot_rating =
				g_pilot_data.rating;
			g_sync.phase = SYNC_OPTIONS_HOST;
		} else {
			struct xvt_flight_options_wire options;
			xvt_wire_set32(options.opcode,
				       NET_PACKET_PLAYER_OPTIONS);
			xvt_wire_set32(options.resolution,
				       g_flight_resolution_mode);
			xvt_wire_set32(options.rating, g_pilot_data.rating);
			xvt_wire_set32(options.schema, XVT_TIMING_SCHEMA);
			xvt_flight_network_send_wire(
				net_session_get_host_dplay_id(), &options,
				sizeof options);
			g_sync.phase = SYNC_OPTIONS_ROSTER;
		}
		xvt_flight_network_alert();
		xvt_flight_network_set_packet_deadline(60);
	}
	if (g_sync.phase == SYNC_OPTIONS_HOST &&
	    g_sync.answer_count >= net_session_count_active_players() - 1) {
		flight_alert_restore_box_background();
		g_sync.alert = 0;
		xvt_flight_network_send_options();
	}
	if (g_sync.phase == SYNC_TAUNTS &&
	    g_sync.answer_count >= net_session_count_active_players()) {
		return xvt_flight_network_finish(1);
	}
	int sender;
	int size;
	int *packet = xvt_flight_network_poll(
		&sender, &size, g_sync.phase == SYNC_TAUNTS ? 30 : 60);
	if (!packet) {
		return xvt_flight_network_expired()
			       ? xvt_flight_network_give_up("options")
			       : XVT_FLIGHT_NETWORK_PENDING;
	}
	if (size < 4) {
		return XVT_FLIGHT_NETWORK_PENDING;
	}
	if (packet[0] == NET_PACKET_STILL_LOADING) {
		xvt_flight_network_loading_status(sender);
	}
	if ((g_sync.phase == SYNC_OPTIONS_ROSTER ||
	     g_sync.phase == SYNC_TAUNTS) &&
	    packet[0] == NET_PACKET_PLAYER_TAUNTS) {
		xvt_flight_network_read_taunts(sender, packet, size);
	} else if (g_sync.phase == SYNC_OPTIONS_HOST &&
		   packet[0] == NET_PACKET_PLAYER_OPTIONS &&
		   size == sizeof(struct xvt_flight_options_wire)) {
		struct xvt_flight_options_wire options;
		memcpy(&options, packet, sizeof options);
		int player = net_session_find_player_slot_by_dpid(sender);
		if (xvt_wire_get32(options.schema) == XVT_TIMING_SCHEMA &&
		    xvt_flight_network_record_acknowledgement(sender, player)) {
			g_players[player].network.flight_resolution_mode =
				xvt_wire_get32(options.resolution);
			g_players[player].pilot_rating =
				xvt_wire_get32(options.rating);
		}
	} else if (g_sync.phase == SYNC_OPTIONS_ROSTER &&
		   packet[0] == NET_PACKET_PLAYER_OPTIONS_ROSTER) {
		xvt_flight_network_accept_roster(sender, packet, size);
	}
	return XVT_FLIGHT_NETWORK_PENDING;
}

int xvt_flight_network_wait_for_mission_start(void)
{
	if (!g_sync.phase) {
		flight_net_reset_world_message_schedule();
		g_net_update_interval_ticks = XVT_WORLD_MESSAGE_TICKS;
		g_flight_net_checksum_request_accum_ticks = 0;
		memset(g_flight_net_peer_silence_ticks, 0,
		       sizeof(g_flight_net_peer_silence_ticks));
		g_flight_net_resync_player_dplay_id = 0;
		g_flight_net_pending_ack_count = 0;
		g_flight_net_clock_adjust_accum_ticks = 0;
		g_flight_net_host_timeout_elapsed_ticks = 0;
		if (g_active_flight_player_count == 1) {
			g_server_tick_time = 0;
			g_game_time = 0;
			g_input_timestamp = 30;
			g_flight_net_clock_lead_ticks = 30;
			time_consume_elapsed_ticks();
			return 1;
		}
		g_sync.expected = net_session_count_active_players();
		g_flight_net_scratch_packet.packet_type =
			NET_PACKET_MISSION_LOADING_READY;
		xvt_flight_network_send_packet(
			net_session_get_host_dplay_id(),
			(unsigned *)&g_flight_net_scratch_packet, 4);
		g_sync.phase = net_session_is_local_host() ? SYNC_START_HOST
							   : SYNC_START_PACKET;
		if (g_sync.phase == SYNC_START_PACKET) {
			xvt_flight_network_alert();
		}
		xvt_flight_network_set_packet_deadline(60);
	}
	if (g_sync.phase == SYNC_START_HOST &&
	    g_sync.answer_count >= g_sync.expected) {
		g_flight_net_scratch_packet.packet_type =
			NET_PACKET_FLIGHT_MISSION_START;
		xvt_flight_network_broadcast(
			(unsigned *)&g_flight_net_scratch_packet, 4);
		xvt_flight_network_alert();
		g_sync.phase = SYNC_START_PACKET;
	}
	if (g_sync.phase == SYNC_START_ACKS) {
		flight_net_process_incoming_packets();
		g_input_timestamp += time_consume_elapsed_ticks();
		if (g_flight_net_pending_ack_count &&
		    (unsigned)g_input_timestamp < 100) {
			return XVT_FLIGHT_NETWORK_PENDING;
		}
		g_flight_net_pending_ack_count = 0;
		g_input_timestamp += time_consume_elapsed_ticks();
		g_flight_net_clock_lead_ticks = g_input_timestamp;
		if (g_input_timestamp < 35) {
			int adjustment = 35 - g_input_timestamp;
			g_flight_net_clock_lead_ticks += adjustment;
			g_input_timestamp += adjustment;
			g_flight_net_clock_adjust_accum_ticks -= adjustment;
		}
		return xvt_flight_network_finish(1);
	}
	int sender;
	int size;
	int *packet = xvt_flight_network_poll(&sender, &size, 60);
	if (!packet) {
		return xvt_flight_network_expired()
			       ? xvt_flight_network_give_up("start")
			       : XVT_FLIGHT_NETWORK_PENDING;
	}
	if (!xvt_flight_network_decode_control((const uint8_t *)packet,
					       &size)) {
		return XVT_FLIGHT_NETWORK_PENDING;
	}
	if (g_sync.phase == SYNC_START_HOST &&
	    packet[0] == NET_PACKET_MISSION_LOADING_READY) {
		xvt_flight_network_record_acknowledgement(
			sender, net_session_find_player_slot_by_dpid(sender));
	} else if (g_sync.phase == SYNC_START_PACKET) {
		if (packet[0] == NET_PACKET_STILL_LOADING) {
			xvt_flight_network_loading_status(sender);
		}
		if (packet[0] == NET_PACKET_FLIGHT_MISSION_START &&
		    sender == net_session_get_host_dplay_id()) {
			flight_alert_restore_box_background();
			g_sync.alert = 0;
			g_flight_net_scratch_packet.packet_type =
				NET_PACKET_ACK;
			xvt_flight_network_send_packet(
				net_session_get_host_dplay_id(),
				(unsigned *)&g_flight_net_scratch_packet, 4);
			time_consume_elapsed_ticks();
			g_server_tick_time = 0;
			g_game_time = 0;
			g_input_timestamp = 0;
			g_flight_net_clock_lead_ticks =
				g_internet_play_enabled ? 130 : 30;
			if (!net_session_is_local_host()) {
				return xvt_flight_network_finish(1);
			}
			g_flight_net_pending_ack_count =
				g_sync.expected == 1 ? 1 : 2;
			flight_net_reset_world_message_schedule();
			g_sync.phase = SYNC_START_ACKS;
		}
	}
	return XVT_FLIGHT_NETWORK_PENDING;
}

void xvt_flight_network_reset(void)
{
	xvt_flight_network_finish(0);
	g_mission_cookie = 0;
}
