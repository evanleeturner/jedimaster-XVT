#include "xvt_runtime/runtime/flight_network.h"

#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/flight_checkpoint.h"
#include "xvt_runtime/runtime/flight_internal.h"
#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt_runtime/runtime/network_session.h"
#include "xvt_runtime/runtime/resync_task.h"
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
	char text[256];
	char *name;
	if (now - g_sync.status_time <= 200) {
		return;
	}
	g_sync.status_time = now;
	name = net_session_get_player_name(
		net_session_find_player_slot_by_dpid(sender));
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
	int sender;
	int size;
	int *packet;
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
	packet = xvt_flight_network_poll(&sender, &size, 60);
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
	uint8_t bytes[sizeof(struct xvt_flight_roster_header) +
		      XVT_FLIGHT_PLAYERS *
			      sizeof(struct xvt_flight_roster_player_wire) +
		      sizeof(struct xvt_flight_agreement_wire)];
	struct xvt_flight_roster_header header;
	xvt_wire_set32(header.opcode, NET_PACKET_PLAYER_OPTIONS_ROSTER);
	xvt_wire_set32(header.new_net, g_flight_conf_new_net);
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
	int sender;
	int size;
	int *packet;
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
	packet = xvt_flight_network_poll(&sender, &size,
					 g_sync.phase == SYNC_TAUNTS ? 30 : 60);
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
	int sender;
	int size;
	int *packet;
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
	packet = xvt_flight_network_poll(&sender, &size, 60);
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

static int xvt_flight_network_has_cookie(unsigned opcode)
{
	switch (opcode) {
	case NET_PACKET_PLAYER_DISCONNECTED:
	case NET_PACKET_WORLD_CHECKSUM:
	case NET_PACKET_SESSION_ABORT:
	case NET_PACKET_RESYNC_CHUNK_ACK:
	case NET_PACKET_FLIGHT_MISSION_START:
	case NET_PACKET_MISSION_LOADING_READY:
	case NET_PACKET_PLAYER_ABORT:
	case NET_PACKET_RESYNC_NOTICE:
	case NET_PACKET_SERVER_CHECKSUM:
	case NET_PACKET_ACK:
	case NET_PACKET_CLOCK_LEAD:
	case NET_PACKET_STILL_LOADING:
	case NET_PACKET_CLOCK_PROBE:
	case NET_PACKET_CLOCK_PROBE_REPLY:
	case NET_PACKET_RESYNC_CHECKSUMS:
	case NET_PACKET_RESYNC_REQUEST:
	case NET_PACKET_RESYNC_APPLY:
	case NET_PACKET_RESYNC_CHUNK:
		return 1;
	default:
		return 0;
	}
}

int xvt_flight_network_send_packet(int dpid, const unsigned *packet, int size)
{
	if (!g_mission_cookie || size < 4 ||
	    !xvt_flight_network_has_cookie(packet[0])) {
		return net_session_send_packet(dpid, (unsigned *)packet, size);
	}
	unsigned copy[XVT_FLIGHT_PACKET_BYTES / sizeof(unsigned)];
	if (size > XVT_FLIGHT_PACKET_BYTES - (int)sizeof(xvt_wire_u32)) {
		return 0;
	}
	memcpy(copy, packet, size);
	xvt_wire_set32((uint8_t *)copy + size, g_mission_cookie);
	return net_session_send_packet(dpid, copy,
				       size + (int)sizeof(xvt_wire_u32));
}

int xvt_flight_network_broadcast(const unsigned *packet, int size)
{
	if (!g_mission_cookie || size < 4 ||
	    !xvt_flight_network_has_cookie(packet[0])) {
		return net_session_broadcast_packet_to_players(
			(unsigned *)packet, size);
	}
	unsigned copy[XVT_FLIGHT_PACKET_BYTES / sizeof(unsigned)];
	if (size > XVT_FLIGHT_PACKET_BYTES - (int)sizeof(xvt_wire_u32)) {
		return 0;
	}
	memcpy(copy, packet, size);
	xvt_wire_set32((uint8_t *)copy + size, g_mission_cookie);
	return net_session_broadcast_packet_to_players(
		copy, size + (int)sizeof(xvt_wire_u32));
}

int xvt_flight_network_decode_control(const uint8_t *packet, int *size)
{
	if (*size < (int)sizeof(xvt_wire_u32) ||
	    *size > XVT_FLIGHT_PACKET_BYTES) {
		return 0;
	}
	unsigned opcode = xvt_wire_get32(packet);
	if (xvt_flight_network_has_cookie(opcode)) {
		if (!g_mission_cookie ||
		    *size < (int)(2 * sizeof(xvt_wire_u32)) ||
		    xvt_wire_get32(packet + *size - sizeof(xvt_wire_u32)) !=
			    g_mission_cookie) {
			return 0;
		}
		*size -= sizeof(xvt_wire_u32);
	}
	size_t minimum = sizeof(xvt_wire_u32);
	switch (opcode) {
	case NET_PACKET_PLAYER_DISCONNECTED:
	case NET_PACKET_RESYNC_CHUNK_ACK:
	case NET_PACKET_PLAYER_ABORT:
	case NET_PACKET_RESYNC_NOTICE:
	case NET_PACKET_CLOCK_LEAD:
	case NET_PACKET_CLOCK_PROBE_REPLY:
		minimum = 2 * sizeof(xvt_wire_u32);
		break;
	case NET_PACKET_CLOCK_PROBE:
		minimum = sizeof(struct xvt_flight_clock_probe_wire);
		break;
	case NET_PACKET_RESYNC_REQUEST:
		minimum = sizeof(struct xvt_flight_resync_request_wire);
		break;
	case NET_PACKET_RESYNC_APPLY:
		minimum = sizeof(struct xvt_flight_resync_apply_wire);
		break;
	case NET_PACKET_RESYNC_CHUNK:
		minimum = sizeof(struct xvt_flight_chunk_header) +
			  sizeof(xvt_wire_u32);
		break;
	case NET_PACKET_WORLD_CHECKSUM:
		minimum = sizeof(struct xvt_flight_checksum_report_wire);
		break;
	case NET_PACKET_SERVER_CHECKSUM:
		minimum = sizeof(struct xvt_flight_checksum_wire);
		break;
	case NET_PACKET_RESYNC_CHECKSUMS:
		minimum = sizeof(struct xvt_flight_epoch_wire);
		break;
	default:
		break;
	}
	return (size_t)*size >= minimum;
}

int xvt_flight_network_send_wire(int dpid, const void *packet, size_t size)
{
	unsigned aligned[XVT_FLIGHT_PACKET_BYTES / sizeof(unsigned)];
	if (size < sizeof(xvt_wire_u32) || size > sizeof aligned) {
		return 0;
	}
	memcpy(aligned, packet, size);
	return xvt_flight_network_send_packet(dpid, aligned, (int)size);
}

int xvt_flight_network_broadcast_wire(const void *packet, size_t size)
{
	unsigned aligned[XVT_FLIGHT_PACKET_BYTES / sizeof(unsigned)];
	if (size < sizeof(xvt_wire_u32) || size > sizeof aligned) {
		return 0;
	}
	memcpy(aligned, packet, size);
	return xvt_flight_network_broadcast(aligned, (int)size);
}

/* Flight-data staging is separate from transport sequencing. */

static struct {
	struct xvt_flight_message outgoing;
	unsigned part;
	unsigned parts;
	unsigned batch_count;
	unsigned batch_sends;
	unsigned part_sends;
	unsigned packets_received;
	struct xvt_flight_input_wire batch[XVT_INPUT_STAGED_RECORDS];
	int sampled;
	int last_flush;
	int recovery_requested;
	unsigned departures;
	uint64_t iteration_start_us;
	struct flight_input_frame_record held;
} g_io;

static int
xvt_flight_network_record_input(unsigned player, int tick,
				const struct flight_input_frame_record *input,
				int authoritative)
{
	xvt_input_insert_status result = xvt_flight_history_insert_real(
		player, tick, input, authoritative);
	if (result == XVT_INPUT_FULL || result == XVT_INPUT_INVALID ||
	    result == XVT_INPUT_CONFLICT) {
		XVT_LOG_DEBUG(
			"network.input_rejected slot=%u tick=%d result=\"%s\" authoritative=%d",
			player, tick,
			result == XVT_INPUT_FULL      ? "full"
			: result == XVT_INPUT_INVALID ? "invalid"
						      : "conflict",
			authoritative);
		xvt_flight_network_request_recovery();
		return 0;
	}
	return 1;
}

int xvt_flight_network_needs_recovery(void) { return g_io.recovery_requested; }

void xvt_flight_network_request_recovery(void)
{
	if (!g_io.recovery_requested) {
		XVT_LOG_WARN("network.recovery_needed");
	}
	g_io.recovery_requested = 1;
}

void xvt_flight_network_clear_recovery_request(void)
{
	g_io.recovery_requested = 0;
}

void xvt_flight_network_recovered(void)
{
	g_io.recovery_requested = 0;
	g_io.sampled = 0;
	unsigned retained = 0;
	for (unsigned i = 0; i < g_io.batch_count; ++i) {
		if (xvt_wire_get32(g_io.batch[i].tick) >
		    (unsigned)g_game_time) {
			g_io.batch[retained++] = g_io.batch[i];
		}
	}
	g_io.batch_count = retained;
}

void xvt_flight_network_begin_iteration(void)
{
	uint64_t now_us = xvt_time_get_elapsed_us();
	if (g_io.iteration_start_us == now_us) {
		return;
	}
	g_io.iteration_start_us = now_us;
	g_io.sampled = 0;
	g_io.batch_sends = 0;
	g_io.part_sends = 0;
	g_io.packets_received = 0;
}

void xvt_flight_network_reset_mission(void)
{
	xvt_flight_messages_reset();
	memset(&g_io, 0, sizeof g_io);
	g_io.iteration_start_us = UINT64_MAX;
}

void xvt_flight_network_flush_input(int now)
{
	while (g_io.batch_count &&
	       g_io.batch_sends < XVT_INPUT_BATCHES_PER_ITERATION &&
	       (now - g_io.last_flush >= XVT_INPUT_BATCH_TICKS ||
		g_io.batch_count >= XVT_INPUT_BATCH_RECORDS)) {
		unsigned count = g_io.batch_count < XVT_INPUT_BATCH_RECORDS
					 ? g_io.batch_count
					 : XVT_INPUT_BATCH_RECORDS;
		unsigned packet[(sizeof(struct xvt_flight_batch_header) +
				 XVT_INPUT_BATCH_RECORDS *
					 sizeof(struct xvt_flight_input_wire)) /
				sizeof(unsigned)];
		size_t size = xvt_flight_messages_encode_batch(
			(uint8_t *)packet, xvt_flight_network_cookie(),
			g_io.batch, count);
		int host = net_session_get_host_dplay_id();
		for (unsigned i = 0; i < XVT_FLIGHT_PLAYERS; ++i) {
			if (i == (unsigned)g_local_player ||
			    !g_players[i].participation_state) {
				continue;
			}
			int dpid = g_players[i].network.direct_play_id;
			int send =
				!g_internet_play_enabled
					? g_player_connected[i]
					: dpid == host ||
						  (g_flight_net_small_session_player_threshold >
							   g_active_flight_player_count &&
						   g_player_connected[i]);
			if (send) {
				net_session_send_packet(dpid, packet,
							(int)size);
			}
		}
		g_io.batch_count -= count;
		XVT_LOG_DEBUG("network.batch_sent records=%u left=%u", count,
			      g_io.batch_count);
		memmove(g_io.batch, g_io.batch + count,
			g_io.batch_count * sizeof g_io.batch[0]);
		++g_io.batch_sends;
		g_io.last_flush = now;
	}
}

int xvt_flight_network_admit_input(int tick)
{
	if (g_io.recovery_requested ||
	    !xvt_flight_wire_valid_tick((unsigned)tick) ||
	    g_io.batch_count == XVT_INPUT_STAGED_RECORDS) {
		return 0;
	}
	/* Replay of retained local history must not consume a second device command. */
	for (int i = 0; i < g_input_frame_count[g_local_player]; ++i) {
		if (g_input_history[g_local_player][i].timestamp == tick) {
			return 1;
		}
	}
	if (g_input_frame_count[g_local_player] >= XVT_INPUT_HISTORY_CAPACITY) {
		xvt_flight_network_request_recovery();
		return 0;
	}
	if (!g_io.sampled) {
		xvt_flight_controls_sample_recorded(&g_io.held);
		g_io.sampled = 1;
	}
	if (!xvt_flight_network_record_input(g_local_player, tick, &g_io.held,
					     0)) {
		return 0;
	}
	XVT_LOG_DEBUG(
		"network.input tick=%d key=%u flags=%u x=%d y=%d r=%d mods=%u throttle=%u",
		tick, g_io.held.key, g_io.held.flags, g_io.held.axis_x,
		g_io.held.axis_y, g_io.held.axis_r, g_io.held.key_mods,
		g_io.held.throttle);
	xvt_flight_wire_encode_input(&g_io.batch[g_io.batch_count++], tick,
				     &g_io.held);
	g_io.held.key = 0;
	g_io.held.flags = 0;
	g_io.held.throttle = 0;
	return 1;
}

int xvt_flight_network_outgoing(void) { return g_io.parts != 0; }

void xvt_flight_network_flush_world(void)
{
	while (g_io.parts && g_io.part_sends < XVT_WORLD_PARTS_PER_ITERATION) {
		unsigned packet[XVT_FLIGHT_PACKET_BYTES / sizeof(unsigned)];
		size_t size = xvt_flight_messages_encode_part(
			(uint8_t *)packet, &g_io.outgoing,
			xvt_flight_network_cookie(), g_io.part);
		for (unsigned i = 0; i < XVT_FLIGHT_PLAYERS; ++i) {
			if (i != (unsigned)g_local_player &&
			    (g_io.outgoing.participant_mask & (1u << i))) {
				net_session_send_packet(
					g_players[i].network.direct_play_id,
					packet, (int)size);
			}
		}
		++g_io.part_sends;
		if (++g_io.part == g_io.parts) {
			g_io.parts = 0;
			g_io.part = 0;
		}
	}
}

void xvt_flight_network_send_world(void)
{
	if (g_io.parts || g_io.recovery_requested) {
		return;
	}
	struct xvt_flight_message *message = &g_io.outgoing;
	memset(message, 0, sizeof *message);
	if (g_flight_net_last_sent_world_message_timestamp >
	    INT32_MAX - XVT_WORLD_MESSAGE_TICKS - 1) {
		g_flight_mission_state.mission_end_pending = 1;
		return;
	}
	int tick = g_flight_net_last_sent_world_message_timestamp +
		   XVT_WORLD_MESSAGE_TICKS;
	if (!xvt_flight_wire_valid_tick((unsigned)tick)) {
		g_flight_mission_state.mission_end_pending = 1;
		return;
	}
	message->target_flags = (unsigned)tick;
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		if (!g_players[player].participation_state ||
		    g_player_abort_flags[player] ||
		    (g_io.departures & (1u << player))) {
			continue;
		}
		message->participant_mask |= 1u << player;
		for (int i = 0; i < g_input_frame_count[player]; ++i) {
			struct input_frame *frame = &g_input_history[player][i];
			if (!frame->awaiting_relay || frame->timestamp > tick) {
				continue;
			}
			struct xvt_flight_world_input_wire *record =
				&message->records[message->count++];
			record->player = player;
			xvt_flight_wire_encode_input(&record->input,
						     frame->timestamp,
						     &frame->input);
		}
	}
	if (!message->participant_mask) {
		return;
	}
	g_flight_net_checksum_request_accum_ticks += XVT_WORLD_MESSAGE_TICKS;
	if (g_flight_net_checksum_request_accum_ticks >
	    XVT_WORLD_CHECKSUM_TICKS) {
		g_flight_net_checksum_request_accum_ticks = 0;
		message->target_flags |= XVT_WORLD_CHECKSUM_FLAG;
		memset(g_flight_net_world_checksum_peer_status, 0,
		       sizeof g_flight_net_world_checksum_peer_status);
	}
	if (!xvt_flight_messages_enqueue(message, XVT_QUEUE_PENDING)) {
		xvt_flight_network_request_recovery();
		return;
	}
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		for (int i = 0; i < g_input_frame_count[player]; ++i) {
			struct input_frame *frame = &g_input_history[player][i];
			if ((message->participant_mask & (1u << player)) &&
			    frame->awaiting_relay && frame->timestamp <= tick) {
				frame->awaiting_relay = 0;
			}
		}
	}
	g_flight_net_last_sent_world_message_timestamp = tick;
	++g_flight_net_sent_world_message_count;
	XVT_LOG_DEBUG(
		"network.world_sent tick=%d mask=%02x records=%u checksum=%d",
		tick, message->participant_mask, message->count,
		(message->target_flags & XVT_WORLD_CHECKSUM_FLAG) != 0);
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		if (player == (unsigned)g_local_player ||
		    !(message->participant_mask & (1u << player)) ||
		    g_flight_net_peer_silence_ticks[player] == -1) {
			continue;
		}
		g_flight_net_peer_silence_ticks[player] +=
			XVT_WORLD_MESSAGE_TICKS;
		if (g_flight_net_peer_silence_ticks[player] >
		    XVT_PEER_TIMEOUT_TICKS) {
			XVT_LOG_WARN("network.player_silent slot=%u ticks=%d",
				     player,
				     g_flight_net_peer_silence_ticks[player]);
			flight_net_broadcast_player_abort(player);
			g_io.departures |= 1u << player;
		}
	}
	g_io.parts = xvt_flight_messages_part_count(message->count);
	xvt_flight_network_flush_world();
}

int xvt_flight_network_insert_world(const struct xvt_flight_message *message)
{
	for (unsigned i = 0; i < message->count; ++i) {
		const struct xvt_flight_world_input_wire *record =
			&message->records[i];
		int tick;
		struct flight_input_frame_record input;
		if (!xvt_flight_wire_decode_input(&record->input, &tick,
						  &input) ||
		    !xvt_flight_network_record_input(record->player, tick,
						     &input, 1)) {
			return 0;
		}
	}
	return 1;
}

int xvt_flight_network_receive(int sender, const uint8_t *bytes, size_t size)
{
	if (size < 4) {
		return 0;
	}
	unsigned opcode = xvt_wire_get32(bytes);
	if (opcode == NET_PACKET_INPUT_BATCH) {
		int player = net_session_find_player_slot_by_dpid(sender);
		if ((unsigned)player >= XVT_FLIGHT_PLAYERS ||
		    player == g_local_player ||
		    !g_players[player].participation_state ||
		    !xvt_flight_messages_validate_batch(
			    bytes, size, xvt_flight_network_cookie())) {
			XVT_LOG_DEBUG(
				"network.batch_dropped slot=%d reason=\"invalid\"",
				player);
			return 1;
		}
		if (xvt_resync_holds_input()) {
			XVT_LOG_DEBUG(
				"network.batch_dropped slot=%d reason=\"resync\"",
				player);
			return 1;
		}
		flight_sync_discard_predicted_input_frames(player);
		g_flight_net_peer_silence_ticks[player] = 0;
		struct xvt_flight_batch_header header;
		memcpy(&header, bytes, sizeof header);
		unsigned count = xvt_wire_get16(header.count);
		/* The wire records are byte arrays, so the cast needs no alignment. */
		const struct xvt_flight_input_wire *records =
			(const struct xvt_flight_input_wire *)(bytes +
							       sizeof header);
		XVT_LOG_DEBUG(
			"network.batch_received slot=%d records=%u first=%u last=%u",
			player, count,
			count ? xvt_wire_get32(records[0].tick) : 0u,
			count ? xvt_wire_get32(records[count - 1].tick) : 0u);
		for (unsigned i = 0; i < count; ++i) {
			int tick;
			struct flight_input_frame_record input;
			struct xvt_flight_input_wire record;
			memcpy(&record,
			       bytes + sizeof header + i * sizeof record,
			       sizeof record);
			xvt_flight_wire_decode_input(&record, &tick, &input);
			if (!xvt_flight_network_record_input(player, tick,
							     &input, 0)) {
				break;
			}
		}
		return 1;
	}
	if (opcode != NET_PACKET_WORLD_MESSAGE) {
		return opcode == NET_PACKET_REMOTE_INPUT;
	}
	if (sender != net_session_get_host_dplay_id()) {
		return 1;
	}
	static struct xvt_flight_message message;
	int result = xvt_flight_messages_receive_part(
		bytes, size, xvt_flight_network_cookie(),
		xvt_resync_receive_floor(), &message);
	if (result < 0) {
		XVT_LOG_DEBUG("network.world_rejected reason=\"part\"");
		xvt_flight_network_request_recovery();
	}
	if (result == 1) {
		if ((message.participant_mask &
		     ~xvt_flight_checkpoint_initial_mask()) ||
		    !xvt_flight_messages_enqueue(
			    &message,
			    xvt_resync_is_active() &&
					    !net_session_is_local_host()
				    ? XVT_QUEUE_REPLAY
				    : XVT_QUEUE_PENDING)) {
			XVT_LOG_DEBUG("network.world_rejected reason=\"%s\"",
				      (message.participant_mask &
				       ~xvt_flight_checkpoint_initial_mask())
					      ? "mask"
					      : "queue_full");
			xvt_flight_network_request_recovery();
		} else {
			XVT_LOG_DEBUG(
				"network.world_received tick=%u mask=%02x records=%u queue=\"%s\"",
				(unsigned)(message.target_flags & INT32_MAX),
				message.participant_mask, message.count,
				xvt_resync_is_active() &&
						!net_session_is_local_host()
					? "replay"
					: "pending");
			g_flight_net_host_timeout_elapsed_ticks = 0;
			++g_flight_net_received_world_message_count;
		}
	}
	return 1;
}

int xvt_flight_network_player_abort(unsigned player)
{
	if (player >= XVT_FLIGHT_PLAYERS) {
		return 0;
	}
	XVT_LOG_INFO("network.player_left slot=%u local=%d", player,
		     player == (unsigned)g_local_player);
	if (net_session_is_local_host()) {
		g_io.departures |= 1u << player;
	}
	return player != (unsigned)g_local_player;
}

uint64_t xvt_flight_network_next_wake_delay_us(int now)
{
	if (g_io.parts || (xvt_flight_messages_count(XVT_QUEUE_PENDING) &&
			   !g_io.recovery_requested)) {
		return 0;
	}
	if (g_io.batch_count) {
		int remaining = XVT_INPUT_BATCH_TICKS - (now - g_io.last_flush);
		return remaining > 0 ? xvt_flight_time_delay_for_ticks(
					       (unsigned)remaining)
				     : 0;
	}
	return xvt_flight_time_delay_for_ticks(XVT_WORLD_MESSAGE_TICKS);
}

int xvt_flight_network_take_world_send_turn(int input_timestamp)
{
	int interval = XVT_WORLD_MESSAGE_TICKS;
	if (xvt_flight_network_outgoing() ||
	    xvt_flight_network_needs_recovery() ||
	    xvt_resync_has_state_request() ||
	    !xvt_flight_messages_has_room(XVT_QUEUE_PENDING,
					  sizeof(struct xvt_flight_message))) {
		return 0;
	}
	if (g_flight_net_pending_ack_count) {
		return 0;
	}
	int adjusted = input_timestamp + g_flight_net_clock_adjust_accum_ticks;
	if (!g_flight_net_world_message_turn_timestamp) {
		g_flight_net_world_message_turn_timestamp =
			adjusted + (g_flight_net_clock_lead_ticks >>
				    XVT_WORLD_START_LEAD_SHIFT);
	}
	int elapsed = adjusted - g_flight_net_world_message_turn_timestamp;
	if (elapsed < interval) {
		return 0;
	}
	if (elapsed > XVT_WORLD_LATE_INTERVALS * interval) {
		g_flight_net_world_message_turn_timestamp += interval;
		return 1;
	}
	int oldest = INT32_MAX;
	for (unsigned player = 0; player < XVT_FLIGHT_PLAYERS; ++player) {
		if (!g_players[player].participation_state) {
			continue;
		}
		const struct input_frame *input =
			flight_sync_find_last_unrelayed_input_frame(player);
		if (!input) {
			oldest = 0;
			break;
		}
		if (input->timestamp < oldest) {
			oldest = input->timestamp;
		}
	}
	if (g_flight_net_last_sent_world_message_timestamp + interval >=
	    oldest) {
		return 0;
	}
	g_flight_net_world_message_turn_timestamp += interval;
	return 1;
}

int xvt_flight_network_take_packet_budget(void)
{
	xvt_flight_network_begin_iteration();
	if (g_io.packets_received >= XVT_NETWORK_PACKETS_PER_ITERATION) {
		return 0;
	}
	++g_io.packets_received;
	return 1;
}
