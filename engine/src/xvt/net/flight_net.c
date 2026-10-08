#include "xvt/net/flight_net.h"

#include <stdio.h>
#include <string.h>

#include "xvt/assets/file.h"
#include "xvt/flight/fediskio.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/hud/flight_alert.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/flight_sync.h"
#include "xvt/net/net_reliable.h"
#include "xvt/net/net_session.h"
#include "xvt/render/flight_sw.h"
#include "xvt/util/time.h"
#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/log/log.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_network_exchange.h"
#include "xvt_runtime/runtime/resync_task.h"

/* DirectPlay id of the player the host is resending the world to; 0 means none.
 * Two functions write it: xvt_flight_network_control, which sets it from a
 * resync notice (the host sends 0 when a resync ends), and
 * xvt_flight_network_wait_for_mission_start, which resets it to 0. Nothing
 * reads it. */
// GLOBAL: XVT 0x5242DC
int g_flight_net_resync_player_dplay_id = 0;
/* Acks the host still waits for; while it is not 0 the host takes no
 * world-message turn. Set to 1 or 2 when the mission starts, and to 1 while a
 * resync is sent or applied; each ACK packet takes 1 off, and the last one also
 * zeroes g_flight_net_world_message_turn_timestamp. 7 functions write it:
 * xvt_flight_network_wait_for_mission_start, xvt_flight_network_control, and
 * the resync functions xvt_resync_begin_send, xvt_resync_end_send,
 * xvt_resync_begin_apply, xvt_resync_apply and xvt_resync_update. */
// GLOBAL: XVT 0x5242E0
int g_flight_net_pending_ack_count = 0;
/* The host's world-message turn clock, in adjusted input ticks
 * (g_input_timestamp plus g_flight_net_clock_adjust_accum_ticks): each turn
 * taken moves it on by one world-message interval, 8 ticks. 0 means not
 * started; the next turn check starts it at the adjusted time plus an eighth of
 * g_flight_net_clock_lead_ticks. Zeroed by
 * flight_net_reset_world_message_schedule and when the last pending ack
 * arrives; advanced by xvt_flight_network_take_world_send_turn. */
// GLOBAL: XVT 0x5242E4
int g_flight_net_world_message_turn_timestamp;
/* The tick stamped on the host's last world message; the next one carries this
 * plus one interval, 8 ticks. Written by xvt_flight_network_send_world; zeroed
 * by flight_net_reset_world_message_schedule at mission start. */
// GLOBAL: XVT 0x5242E8
int g_flight_net_last_sent_world_message_timestamp;
/* Set to 1 by flight_net_reset_world_message_schedule; nothing reads it. */
// GLOBAL: XVT 0x5242EC
static int g_unused_flight_net_mission_start_ack_init_flag;
/* Running total of the clock steering applied to g_input_timestamp, with the
 * opposite sign, so g_input_timestamp plus this is the input clock before
 * steering; in ticks. Written wherever the clock is steered:
 * xvt_flight_frame_adjust_clock in flight, and
 * xvt_flight_network_wait_for_mission_start, which first resets it to 0. */
// GLOBAL: XVT 0x52340C
int g_flight_net_clock_adjust_accum_ticks = 0;
/* 1 once a SESSION_ABORT packet has arrived this flight; the mission debrief
 * and xvt_flight_frame_network_update read it. Set by
 * xvt_flight_network_control; reset to 0 at flight load
 * (xvt_flight_loading_globals). */
// GLOBAL: XVT 0x52342C
int g_flight_net_host_abort_received = 0;
/* Ticks between the host's world messages. It starts at 29, and
 * xvt_flight_network_wait_for_mission_start sets it to 8 at mission start. In a
 * solo flight xvt_flight_frame_start_advance sets it each frame to that frame's
 * step, g_input_timestamp minus g_game_time. */
// GLOBAL: XVT 0x523418
int g_net_update_interval_ticks = 29;
/* The buffer each flight packet is built in just before it is sent; what it
 * holds lasts until the next packet is built. 13 functions write it: the 6 that
 * send packets in this file, xvt_flight_network_wait_for_mission_start,
 * xvt_flight_network_answer_clock_probe and the resync functions
 * xvt_resync_apply, xvt_resync_begin_apply, xvt_resync_begin_send,
 * xvt_resync_checksums and xvt_resync_pulse. */
// GLOBAL: XVT 0x557360
struct flight_net_scratch_packet g_flight_net_scratch_packet = {0};
/* The adjusted input time (g_input_timestamp plus
 * g_flight_net_clock_adjust_accum_ticks) sent in this client's last clock probe; a
 * probe reply counts only if it echoes this value. Written by
 * flight_net_send_clock_probe_to_host. */
// GLOBAL: XVT 0x556ED0
int g_flight_net_clock_probe_timestamp = 0;
/* Per player slot, on the host, ticks since that player was last heard from:
 * each world message the host sends adds one interval to every other player's
 * count, and an input or a loading pulse from the player sets it back to 0.
 * Past 7,080 ticks the host tells all players that player has aborted. Counts
 * of -1 are skipped, but no code sets -1. 4 functions write it:
 * xvt_flight_network_send_world, xvt_flight_network_receive and
 * xvt_flight_network_control, and xvt_flight_network_wait_for_mission_start,
 * which zeroes it. */
// GLOBAL: XVT 0x556ED8
int g_flight_net_peer_silence_ticks[8] = {0};
/* The local input clock, in ticks: the tick stamped on this player's next
 * input. It runs ahead of g_server_tick_time by about
 * g_flight_net_clock_lead_ticks and is steered toward that gap (see
 * g_flight_net_clock_adjust_accum_ticks). 18 functions write it, chiefly the
 * xvt_flight_frame_ functions, xvt_flight_network_process_packets and the
 * xvt_resync_ functions; set to 0 at mission start, or 30 in a solo flight. */
// GLOBAL: XVT 0x9A8C2C
int g_input_timestamp = 0;
/* The tick of the last confirmed world state: in network play the tick of the
 * last world message applied, in a solo flight the tick the simulation last
 * stepped to. 7 functions write it: xvt_flight_frame_advance,
 * xvt_flight_frame_confirm, xvt_flight_sim_advance,
 * xvt_flight_sim_step_to_time, xvt_flight_checkpoint_restore,
 * xvt_flight_task_start_world and xvt_flight_network_wait_for_mission_start,
 * which sets it to 0 at mission start. */
// GLOBAL: XVT 0xA07CC8
int g_server_tick_time = 0;
/* Per player slot, 1 until a PLAYER_DISCONNECTED for the slot is sent or
 * received, then 0; players send their inputs directly only to slots still at
 * 1, though in internet play the host gets them regardless. Set to 1 for every
 * slot at flight load (xvt_flight_loading_globals); cleared by
 * xvt_flight_network_control. */
// GLOBAL: XVT 0xA07BB0
int g_player_connected[8] = {0};
/* Per player slot, 1 once that player has left the flight: a PLAYER_ABORT for
 * it arrived, or this player left on its own. 3 functions write it:
 * xvt_flight_network_control; xvt_flight_checkpoint_apply_confirmed_mask, which
 * sets it for each player who began the flight but is no longer confirmed; and
 * xvt_flight_loading_globals, which resets it to 0 at flight load. */
// GLOBAL: XVT 0x9D8A30
int g_player_abort_flags[8] = {0};
/* This player's input for the current frame: key, X and Y axes, key modifiers,
 * roll and throttle. Filled by flight_net_sample_local_input; cleared at flight
 * load by xvt_flight_loading_globals. */
// GLOBAL: XVT 0xA082A8
struct flight_input_frame_record g_current_input_frame = {0};
/* With internet play, sessions of at least this many players (3) send inputs to
 * the host only, and smaller ones also send them to every other connected
 * player. The head start a clock probe asks for is halved in smaller sessions,
 * and whenever internet play is off. Nothing changes it. */
// GLOBAL: XVT 0x5242C4
int g_flight_net_small_session_player_threshold = 3;
/* World messages received this run; xvt_flight_network_receive counts it up,
 * and nothing reads or resets it. */
// GLOBAL: XVT 0x5242CC
int g_flight_net_received_world_message_count = 0;
/* World messages sent this run; xvt_flight_network_send_world counts it up, and
 * nothing reads or resets it. */
// GLOBAL: XVT 0x5242C8
int g_flight_net_sent_world_message_count = 0;
/* Ticks since the host last asked for world checksums. Each world message adds
 * 8; once it passes 472, the message being sent asks every player for a
 * checksum and this restarts at 0. Written by xvt_flight_network_send_world;
 * xvt_flight_network_wait_for_mission_start zeroes it at mission start. */
// GLOBAL: XVT 0x557350
int g_flight_net_checksum_request_accum_ticks = 0;
/* Per player slot, on the host, the answer to the current world checksum round:
 * 0 none yet, 1 matched the host's, 2 did not. Cleared when a world message
 * asks for checksums (xvt_flight_network_send_world) and at flight load
 * (xvt_flight_loading_globals); set by
 * flight_sync_record_checksum_status, and to 2 by
 * xvt_resync_complete_checksum after a resync. When every player still flying
 * shows 1, the host clears
 * g_flight_net_buffer_world_messages_until_checksum. */
// GLOBAL: XVT 0x9D77D0
int g_flight_net_world_checksum_peer_status[8] = {0};
/* Ticks since this player last heard from the host: a world message or the
 * host's loading pulse sets it to 0, and on a client the ticks that pass
 * without one add up every frame. Past 7,080 ticks the player gives up on the
 * host and leaves the flight. 4 functions write it: xvt_flight_network_receive,
 * xvt_flight_network_control, xvt_flight_frame_network_update, and
 * xvt_flight_network_wait_for_mission_start, which zeroes it. */
// GLOBAL: XVT 0x9A8C24
int g_flight_net_host_timeout_elapsed_ticks = 0;
/* Set to 1 when any resync chunk ack arrives (xvt_flight_network_control); the
 * wait for a resync apply ack (xvt_resync_apply) clears it and restarts its
 * wait window. */
// GLOBAL: XVT 0x556F00
int g_flight_net_world_state_ack_received_flag = 0;
/* Per chunk slot in the current batch of 16 resync chunks, 1 once the receiving
 * player has acked it. Set by the chunk-ack handler
 * (xvt_flight_network_control); cleared for each new batch by the xvt_resync_
 * functions. */
// GLOBAL: XVT 0x556F08
int g_flight_net_world_state_chunk_acked[16] = {0};
/* The batch of up to 16 RESYNC_CHUNK packets being built and sent to one
 * player; built by the xvt_resync_ chunk builders. */
// GLOBAL: XVT 0x557788
struct flight_net_world_state_chunk_packet
	g_flight_net_world_state_chunk_packets[16] = {{0}};
/* Set to 1 when the receiving player's RESYNC_CHECKSUMS arrives; the resync
 * sender clears it before it waits and stops waiting once it is set. 3
 * functions write it: xvt_resync_begin_send, xvt_resync_checksums and
 * xvt_resync_receive_packet. */
// GLOBAL: XVT 0x55978C
int g_flight_net_remote_resync_checksums_received_flag = 0;

/* Before a flight, shares each player's screen resolution mode, rating and
 * taunts. Hands off to xvt_flight_network_exchange_options and returns its
 * result: 1 done, 0 failed, XVT_FLIGHT_NETWORK_PENDING while it waits, and it
 * is called again each frame. */
// FUNCTION: XVT 0x463160
int flight_net_sync_player_options_and_taunts(void)
{
	return xvt_flight_network_exchange_options();
}

/* Holds every player at the mission start and starts the flight clocks
 * together. Hands off to xvt_flight_network_wait_for_mission_start and returns
 * its result: 1, 0, or XVT_FLIGHT_NETWORK_PENDING while it waits. */
// FUNCTION: XVT 0x463790
int flight_net_wait_for_mission_start(void)
{
	return xvt_flight_network_wait_for_mission_start();
}

/* Sends the host a clock probe holding the adjusted input time
 * (g_input_timestamp plus g_flight_net_clock_adjust_accum_ticks) and this
 * player's g_flight_net_clock_lead_ticks, and keeps that time in
 * g_flight_net_clock_probe_timestamp to match the reply. Writes
 * g_flight_net_scratch_packet. Sends through xvt_flight_network_send_packet.
 * Returns the send function's result. */
// FUNCTION: XVT 0x463B60
int flight_net_send_clock_probe_to_host(void)
{
	struct flight_net_scratch_packet *packet = &g_flight_net_scratch_packet;
	g_flight_net_scratch_packet.packet_type = NET_PACKET_CLOCK_PROBE;
	int input_timestamp = g_input_timestamp;
	packet->payload_dwords[0] =
		input_timestamp + g_flight_net_clock_adjust_accum_ticks;
	g_flight_net_clock_probe_timestamp =
		input_timestamp + g_flight_net_clock_adjust_accum_ticks;
	packet->payload_dwords[1] = g_flight_net_clock_lead_ticks;
	XVT_LOG_DEBUG("network.clock_probe_sent tick=%d adjust=%d lead=%d",
		      g_flight_net_clock_probe_timestamp,
		      g_flight_net_clock_adjust_accum_ticks,
		      (int)g_flight_net_clock_lead_ticks);
	return xvt_flight_network_send_packet(net_session_get_host_dplay_id(),
					      (unsigned int *)packet, 12);
}

/* Tells every player this one is still loading: a 4-byte STILL_LOADING packet
 * to DirectPlay id 0, which reaches all players and queues a copy for this one.
 * Writes g_flight_net_scratch_packet. Sends through
 * xvt_flight_network_send_packet. Returns the send function's result. */
// FUNCTION: XVT 0x463BB0
int flight_net_broadcast_still_loading_pulse(void)
{
	g_flight_net_scratch_packet.packet_type = NET_PACKET_STILL_LOADING;
	XVT_LOG_DEBUG("network.loading_pulse_sent");
	return xvt_flight_network_send_packet(
		0, (unsigned int *)&g_flight_net_scratch_packet, 4);
}

/* Sends SESSION_ABORT to every active player in the roster, this one included,
 * which ends the flight for each. Writes g_flight_net_scratch_packet. Sends
 * through xvt_flight_network_broadcast. Returns the broadcast's result. Does
 * not check that this player is the host. */
// FUNCTION: XVT 0x463BD0
int flight_net_broadcast_host_session_abort(void)
{
	g_flight_net_scratch_packet.packet_type = NET_PACKET_SESSION_ABORT;
	XVT_LOG_INFO("network.session_abort_sent tick=%d", g_server_tick_time);
	return xvt_flight_network_broadcast(
		(unsigned int *)&g_flight_net_scratch_packet, 4);
}

/* Tells every active player, this one included, that the player in player_slot
 * leaves the flight; the named player ends its flight when the packet reaches
 * it. Writes g_flight_net_scratch_packet. Sends through
 * xvt_flight_network_broadcast. Returns the broadcast's result. */
// FUNCTION: XVT 0x463C50
int flight_net_broadcast_player_abort(int player_slot)
{
	g_flight_net_scratch_packet.packet_type = NET_PACKET_PLAYER_ABORT;
	g_flight_net_scratch_packet.payload_dwords[0] = player_slot;
	XVT_LOG_DEBUG("network.player_abort_sent slot=%d local=%d", player_slot,
		      player_slot == g_local_player);
	return xvt_flight_network_broadcast(
		(unsigned int *)&g_flight_net_scratch_packet, 8);
}

/* Returns the index in g_pilot_data.network_players of the entry with the same
 * DirectPlay id as g_players[player_idx], or 0 when none matches, which cannot
 * be told apart from a match on the first entry. Does not check player_idx. */
// FUNCTION: XVT 0x463C80
int flight_net_find_pilot_network_player_index(int player_idx)
{
	int network_player_idx = 0;
	int *direct_play_id = &g_pilot_data.network_players[0].direct_play_id;
	int player_direct_play_id =
		g_players[player_idx].network.direct_play_id;
	const uint8_t *network_player_end =
		(const uint8_t *)direct_play_id +
		sizeof(g_pilot_data.network_players);
	while (*direct_play_id != player_direct_play_id) {
		direct_play_id = (int *)((uint8_t *)direct_play_id +
					 sizeof(struct pilot_network_player));
		++network_player_idx;
		if ((const uint8_t *)direct_play_id >= network_player_end) {
			XVT_LOG_WARN("network.pilot_entry_missing slot=%d",
				     player_idx);
			return 0;
		}
	}
	XVT_LOG_DEBUG("network.pilot_entry slot=%d entry=%d", player_idx,
		      network_player_idx);
	return network_player_idx;
}

/* Sets has_left on the g_pilot_data.network_players entry of the player in
 * player_slot; when no entry matches, it marks entry 0 instead. */
// FUNCTION: XVT 0x463CC0
void flight_net_mark_pilot_network_player_left(int player_slot)
{
	g_pilot_data
		.network_players[flight_net_find_pilot_network_player_index(
			player_slot)]
		.has_left = 1;
}

/* Reads and acts on every flight packet waiting in the queue. Hands off to
 * xvt_flight_network_process_packets. */
// FUNCTION: XVT 0x463D00
void flight_net_process_incoming_packets(void)
{
	xvt_flight_network_process_packets();
}

/* Samples this player's controls into g_current_input_frame and files it in
 * this player's input history at g_input_timestamp, not awaiting relay. Adds
 * roll and throttle, sends nothing, and returns the result of
 * flight_pump_window_messages, which is always 0. */
// FUNCTION: XVT 0x464900
int32_t flight_net_sample_local_input(void)
{
	flight_input_read(-2);
	memset(&g_current_input_frame, 0, sizeof g_current_input_frame);
	g_current_input_frame.key = (uint8_t)g_action_key;
	g_current_input_frame.axis_x = (int8_t)(g_ctrl_axis_x & 0xfe);
	g_current_input_frame.axis_y = (int8_t)(g_ctrl_axis_y & 0xfe);
	g_current_input_frame.axis_r = (int8_t)(g_xvt_control_roll & 0xfe);
	g_current_input_frame.key_mods = (uint8_t)(g_key_mods & 3u);
	xvt_flight_controls_sample_throttle(&g_current_input_frame);
	struct input_frame *inserted = flight_sync_insert_input_frame(
		g_local_player, g_input_timestamp, &g_current_input_frame);
	if (inserted != NULL) {
		inserted->awaiting_relay = 0;
		inserted->input_source = 1;
		XVT_LOG_DEBUG(
			"network.input tick=%d key=%u flags=%u x=%d y=%d r=%d mods=%u throttle=%u",
			g_input_timestamp, (unsigned)g_current_input_frame.key,
			(unsigned)g_current_input_frame.flags,
			(int)g_current_input_frame.axis_x,
			(int)g_current_input_frame.axis_y,
			(int)g_current_input_frame.axis_r,
			(unsigned)g_current_input_frame.key_mods,
			(unsigned)g_current_input_frame.throttle);
	} else {
		XVT_LOG_WARN("network.local_input_lost tick=%d frames=%d",
			     g_input_timestamp,
			     g_input_frame_count[g_local_player]);
	}
	return flight_pump_window_messages();
}

/* Restarts the host's world-message schedule: zeroes
 * g_flight_net_world_message_turn_timestamp and
 * g_flight_net_last_sent_world_message_timestamp, and sets
 * g_unused_flight_net_mission_start_ack_init_flag, which nothing reads. */
// FUNCTION: XVT 0x464C60
void flight_net_reset_world_message_schedule(void)
{
	XVT_LOG_DEBUG("network.world_schedule_reset turn=%d last=%d",
		      g_flight_net_world_message_turn_timestamp,
		      g_flight_net_last_sent_world_message_timestamp);
	g_unused_flight_net_mission_start_ack_init_flag = 1;
	g_flight_net_world_message_turn_timestamp = 0;
	g_flight_net_last_sent_world_message_timestamp = 0;
}

/* Sends the host this player's world checksum: a WORLD_CHECKSUM packet holding
 * g_server_tick_time, checksum_dword_count checksum words, then as many region
 * lengths. One zero word follows them, which the host's
 * flight_sync_handle_world_checksum_packet reads as a request to resend the
 * world when it is 1. Sends through xvt_flight_network_send_packet. Writes
 * g_flight_net_scratch_packet. Returns the send function's result. Does not
 * check that the words fit in the packet. */
// FUNCTION: XVT 0x4650E0
int flight_net_send_world_checksum_to_host(const int *world_checksum,
					   const int *region_lengths,
					   int checksum_dword_count)
{
	g_flight_net_scratch_packet.packet_type = NET_PACKET_WORLD_CHECKSUM;
	g_flight_net_scratch_packet.payload_dwords[0] = g_server_tick_time;
	memcpy(&g_flight_net_scratch_packet.payload_dwords[1], world_checksum,
	       (size_t)checksum_dword_count * sizeof(int));
	memcpy(&g_flight_net_scratch_packet
			.payload_dwords[checksum_dword_count + 1],
	       region_lengths, (size_t)checksum_dword_count * sizeof(int));
	g_flight_net_scratch_packet
		.payload_dwords[checksum_dword_count * 2 + 1] = 0;
	XVT_LOG_DEBUG(
		"network.checksum_sent to=\"host\" tick=%d regions=%d bytes=%d",
		g_server_tick_time, checksum_dword_count,
		checksum_dword_count * 8 + 12);
	return xvt_flight_network_send_packet(
		net_session_get_host_dplay_id(),
		(unsigned *)&g_flight_net_scratch_packet,
		checksum_dword_count * 8 + 12);
}

/* Sends every active player, this one included, the host's world checksum: a
 * SERVER_CHECKSUM packet holding g_server_tick_time, checksum_dword_count
 * checksum words, then as many region lengths. Writes
 * g_flight_net_scratch_packet. Sends through xvt_flight_network_broadcast.
 * Returns the broadcast's result. Does not check that the words fit in the
 * packet or that this player is the host. */
// FUNCTION: XVT 0x465150
int flight_net_broadcast_world_checksum(const int *world_checksum,
					const int *region_lengths,
					int checksum_dword_count)
{
	g_flight_net_scratch_packet.packet_type = NET_PACKET_SERVER_CHECKSUM;
	g_flight_net_scratch_packet.payload_dwords[0] = g_server_tick_time;
	memcpy(&g_flight_net_scratch_packet.payload_dwords[1], world_checksum,
	       (size_t)checksum_dword_count * sizeof(int));
	memcpy(&g_flight_net_scratch_packet
			.payload_dwords[checksum_dword_count + 1],
	       region_lengths, (size_t)checksum_dword_count * sizeof(int));
	XVT_LOG_DEBUG(
		"network.checksum_sent to=\"all\" tick=%d regions=%d bytes=%d",
		g_server_tick_time, checksum_dword_count,
		checksum_dword_count * 8 + 8);
	return xvt_flight_network_broadcast(
		(unsigned int *)&g_flight_net_scratch_packet,
		checksum_dword_count * 8 + 8);
}
