#include "xvt/net/flight_net.h"

#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/runtime/flight_network.h"

#include "xvt_runtime/runtime/resync_task.h"
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
#include "xvt_runtime/log/log_both_builds.h"

/* DirectPlay id of the player the host is resending the world to, named in the
 * communication-failure alert; 0 means none, and the alert then names the host.
 * 6 functions write it: the resync-notice handlers in
 * flight_net_process_incoming_packets, flight_net_handle_world_state_resync_packet and
 * xvt_flight_network_control (the host sends 0 when a resync ends);
 * flight_net_resolve_resync_player_name, which swaps in the host's id; and both
 * mission-start waits, which reset it to 0. Only
 * flight_net_resolve_resync_player_name reads it, and only the original build calls
 * that. */
// GLOBAL: XVT 0x5242DC
int g_flight_net_resync_player_dplay_id = 0;
/* Acks the host still waits for; while it is not 0 the host takes no
 * world-message turn. Set to 1 or 2 when the mission starts, and to 1 while a
 * resync is sent or applied; each ACK packet takes 1 off, and the last one also
 * zeroes g_flight_net_world_message_turn_timestamp. 11 functions write it, chiefly
 * both mission-start waits, both packet handlers
 * (flight_net_process_incoming_packets, xvt_flight_network_control), and the resync
 * senders here and among the XvtResync_ functions. */
// GLOBAL: XVT 0x5242E0
int g_flight_net_pending_ack_count = 0;
/* The host's world-message turn clock, in adjusted input ticks
 * (g_input_timestamp plus g_flight_net_clock_adjust_accum_ticks): each turn taken
 * moves it on by one world-message interval (g_net_update_interval_ticks in the
 * original, 8 ticks in the new code). 0 means not started; the next turn check
 * starts it at the adjusted time plus an eighth of g_flight_net_clock_lead_ticks.
 * Zeroed by flight_net_reset_world_message_schedule and when the last pending ack
 * arrives; advanced by flight_net_take_world_message_turn and
 * xvt_flight_network_take_world_send_turn. */
// GLOBAL: XVT 0x5242E4
int g_flight_net_world_message_turn_timestamp;
/* The tick stamped on the host's last world message; the next one carries this
 * plus one interval (g_net_update_interval_ticks in the original, 8 ticks in the
 * new code). Written by flight_net_broadcast_world_message and
 * xvt_flight_network_send_world; zeroed by flight_net_reset_world_message_schedule at
 * mission start. */
// GLOBAL: XVT 0x5242E8
int g_flight_net_last_sent_world_message_timestamp;
/* Set to 1 by flight_net_reset_world_message_schedule; nothing reads it. */
// GLOBAL: XVT 0x5242EC
static int g_unused_flight_net_mission_start_ack_init_flag;
/* Running total of the clock steering applied to g_input_timestamp, with the
 * opposite sign, so g_input_timestamp plus this is the input clock before
 * steering; in ticks. Written wherever the clock is steered:
 * flight_run_mission_loop and xvt_flight_frame_adjust_clock in flight, and both
 * mission-start waits, which first reset it to 0. */
// GLOBAL: XVT 0x52340C
int g_flight_net_clock_adjust_accum_ticks = 0;
/* 1 once a SESSION_ABORT packet has arrived this flight; the mission debrief
 * and xvt_flight_frame_network_update read it. Set by
 * flight_net_process_incoming_packets, flight_net_handle_world_state_resync_packet and
 * xvt_flight_network_control; reset to 0 at flight load (flight_main_loop,
 * xvt_flight_loading_globals). */
// GLOBAL: XVT 0x52342C
int g_flight_net_host_abort_received = 0;
/* Ticks between the host's world messages. At mission start the original sets
 * 59, 39 or 29 from the server update rate 4, 6 or 8 (default 29) and the new
 * code sets 8. In a solo flight both frame loops (flight_run_mission_loop,
 * xvt_flight_frame_start_advance) set it each frame to that frame's step,
 * g_input_timestamp minus g_game_time. */
// GLOBAL: XVT 0x523418
int g_net_update_interval_ticks = 29;
/* 1 while the communication-failure alert of flight_net_process_incoming_packets
 * is up; the host takes no world-message turn meanwhile. Only the original
 * build sets it: flight_net_process_incoming_packets opens and closes the alert,
 * and flight_net_wait_for_mission_start resets it to 0. */
// GLOBAL: XVT 0x557358
int g_flight_net_recovery_ui_active = 0;
/* The buffer each flight packet is built in just before it is sent; what it
 * holds lasts until the next packet is built. 24 functions write it: most
 * functions in this file, xvt_flight_network_wait_for_mission_start,
 * xvt_flight_network_answer_clock_probe and the XvtResync_ functions. */
// GLOBAL: XVT 0x557360
struct flight_net_scratch_packet g_flight_net_scratch_packet = {0};
/* The adjusted input time (g_input_timestamp plus
 * g_flight_net_clock_adjust_accum_ticks) sent in this client's last clock probe; a
 * probe reply counts only if it echoes this value. Written by
 * flight_net_send_clock_probe_to_host. */
// GLOBAL: XVT 0x556ED0
int g_flight_net_clock_probe_timestamp = 0;
/* Per player slot, on the host, ticks since that player was last heard from:
 * each world message adds one interval to every other player's count (the
 * original as the host receives its own message, the new code as it sends one),
 * and an input or a loading pulse from the player sets it back to 0. Past 7,080
 * ticks the host tells all players that player has aborted. Counts of -1 are
 * skipped, but no code sets -1. 7 functions write it, chiefly
 * flight_net_process_incoming_packets, xvt_flight_network_send_world and
 * xvt_flight_network_receive; both mission-start waits zero it. */
// GLOBAL: XVT 0x556ED8
int g_flight_net_peer_silence_ticks[8] = {0};
/* Input-clock tick at which the communication-failure alert opened or last
 * changed its text; the text alternates every 118 ticks. Only
 * flight_net_process_incoming_packets uses it, in the original build. */
// GLOBAL: XVT 0x556EFC
int g_flight_net_recovery_ui_blink_time = 0;
/* g_input_timestamp when the communication-failure alert opened;
 * g_input_timestamp returns to it when the alert closes. Only
 * flight_net_process_incoming_packets uses it, in the original build. */
// GLOBAL: XVT 0x55734C
int g_flight_net_recovery_saved_input_timestamp = 0;
/* The local input clock, in ticks: the tick stamped on this player's next
 * input. It runs ahead of g_server_tick_time by about g_flight_net_clock_lead_ticks
 * and is steered toward that gap (see g_flight_net_clock_adjust_accum_ticks). 28
 * functions write it, chiefly the frame loops (flight_run_mission_loop, the
 * XvtFlightFrame_ functions), flight_net_sample_local_input,
 * flight_net_process_incoming_packets and the resync waits; set to 0 at mission
 * start, or 30 in a solo flight. */
// GLOBAL: XVT 0x9A8C2C
int g_input_timestamp = 0;
/* The tick of the last confirmed world state: in network play the tick of the
 * last world message applied, in a solo flight the tick the simulation last
 * stepped to. 9 functions write it, chiefly flight_sync_apply_world_message_packet,
 * flight_run_mission_loop, xvt_flight_frame_advance, xvt_flight_frame_confirm and the
 * resync apply code; set to 0 at mission start. */
// GLOBAL: XVT 0xA07CC8
int g_server_tick_time = 0;
/* Per player slot, 1 until a PLAYER_DISCONNECTED for the slot is sent or
 * received, then 0; players send their inputs directly only to slots still at
 * 1, though in internet play the host gets them regardless. Set to 1 for every
 * slot at flight load (flight_main_loop, xvt_flight_loading_globals); cleared by
 * flight_net_broadcast_player_disconnected, flight_net_process_incoming_packets and
 * xvt_flight_network_control. */
// GLOBAL: XVT 0xA07BB0
int g_player_connected[8] = {0};
/* Per player slot, 1 once that player has left the flight: a PLAYER_ABORT for
 * it arrived, or this player left on its own. 7 functions write it, chiefly
 * flight_net_process_incoming_packets, flight_net_handle_world_state_resync_packet and
 * xvt_flight_network_control; xvt_flight_checkpoint_apply_confirmed_mask sets it for
 * each player who began the flight but is no longer confirmed. Reset to 0 at
 * flight load (flight_main_loop, xvt_flight_loading_globals). */
// GLOBAL: XVT 0x9D8A30
int g_player_abort_flags[8] = {0};
/* This player's input for the current frame: key, X and Y axes and key
 * modifiers, with roll and throttle in the new code. Filled by
 * flight_net_sample_local_input; cleared at flight load by flight_main_loop and
 * xvt_flight_loading_globals. */
// GLOBAL: XVT 0xA082A8
struct flight_input_frame_record g_current_input_frame = {0};
/* g_input_timestamp of the last input packet flight_net_sample_local_input built; 0
 * means none yet, which forces a full timestamp. Reset to 0 by
 * flight_net_wait_for_mission_start. Only the original build uses it. */
// GLOBAL: XVT 0x559788
int g_last_sent_input_timestamp = 0;
/* g_input_timestamp of the last input packet that carried a full 4-byte
 * timestamp; flight_net_sample_local_input sends a full one again when the
 * previous input came more than 236 ticks after it. Reset to 0 by
 * flight_net_wait_for_mission_start. Only the original build uses it. */
// GLOBAL: XVT 0x557348
int g_last_keyframe_time = 0;
/* The internet-play input batch being filled: packet type, record count, then
 * the records, each encoded as in flight_net_sample_local_input without the packet
 * type. flight_net_sample_local_input appends to it and sends it;
 * flight_net_wait_for_mission_start empties it. Only the original build uses it. */
// GLOBAL: XVT 0x557148
struct flight_net_input_batch_packet g_flight_net_input_batch_packet = {0};
/* Bytes used in g_flight_net_input_batch_packet, its 5-byte header included, so 5
 * when empty. Written by flight_net_sample_local_input and
 * flight_net_wait_for_mission_start; only the original build uses it. */
// GLOBAL: XVT 0x557354
int g_flight_net_input_batch_len = 0;
/* Per player slot, the timestamp of that player's last decoded input; a record
 * that carries only the low 7 bits takes the high bits from here, one step of
 * 128 later when the low bits went backward. Written as inputs are decoded by
 * flight_net_process_incoming_packets and flight_net_handle_world_state_resync_packet;
 * zeroed by flight_net_wait_for_mission_start. Only the original build uses it. */
// GLOBAL: XVT 0x557560
int g_flight_net_last_input_timestamp_by_player[8] = {0};
/* g_input_timestamp when flight_net_sample_local_input last sent the input batch;
 * zeroed by flight_net_wait_for_mission_start. Only the original build uses it. */
// GLOBAL: XVT 0x556EF8
int g_flight_net_last_input_batch_send_time = 0;
/* The input batch goes out once more than this many ticks have passed since the
 * last send. flight_net_wait_for_mission_start sets it to 23 and nothing else
 * writes it; only the original build uses it. */
// GLOBAL: XVT 0x557580
int g_flight_net_input_batch_interval_ticks = 0;
/* With internet play, sessions of at least this many players (3) send inputs to
 * the host only, and smaller ones also send them to every other connected
 * player. The head start a clock probe asks for is halved in smaller sessions,
 * and whenever internet play is off. Nothing changes it. */
// GLOBAL: XVT 0x5242C4
int g_flight_net_small_session_player_threshold = 3;
/* World messages received this run; flight_net_process_incoming_packets and
 * xvt_flight_network_receive count it up, and nothing reads or resets it. */
// GLOBAL: XVT 0x5242CC
int g_flight_net_received_world_message_count = 0;
/* When 1, flight_net_sample_local_input logs each local input to inputlog.txt and
 * flight_net_broadcast_world_message logs each world message to serverlog.txt.
 * Nothing sets it, so it stays 0 and neither log is written. */
// GLOBAL: XVT 0x5242D0
int g_input_log_enabled = 0;
/* inputlog.txt, opened by flight_net_sample_local_input on the first logged input
 * and never closed; NULL until then. */
// GLOBAL: XVT 0x5242D4
xvt_file *g_input_log_file = NULL;
/* World messages sent this run (the original also counts a call in a solo
 * flight); flight_net_broadcast_world_message and xvt_flight_network_send_world count
 * it up, and nothing reads or resets it. */
// GLOBAL: XVT 0x5242C8
int g_flight_net_sent_world_message_count = 0;
/* Ticks since the host last asked for world checksums. The original adds each
 * world message's tick minus g_server_tick_time, the new code adds 8 per message;
 * once it passes 472, the message being sent asks every player for a checksum
 * and this restarts at 0. Written by flight_net_broadcast_world_message and
 * xvt_flight_network_send_world; xvt_flight_network_wait_for_mission_start zeroes it at
 * mission start. */
// GLOBAL: XVT 0x557350
int g_flight_net_checksum_request_accum_ticks = 0;
/* Per player slot, on the host, the answer to the current world checksum round:
 * 0 none yet, 1 matched the host's, 2 did not. Cleared when a world message
 * asks for checksums (flight_net_broadcast_world_message,
 * xvt_flight_network_send_world) and at flight load (flight_main_loop,
 * xvt_flight_loading_globals); set by flight_sync_handle_world_checksum_packet, and
 * to 2 by xvt_resync_complete_checksum after a resync. When every player still
 * flying shows 1, the host clears
 * g_flight_net_buffer_world_messages_until_checksum. */
// GLOBAL: XVT 0x9D77D0
int g_flight_net_world_checksum_peer_status[8] = {0};
/* serverlog.txt, opened by flight_net_broadcast_world_message on the first logged
 * world message and never closed; NULL until then. */
// GLOBAL: XVT 0x5242D8
xvt_file *g_flight_net_server_log_file = NULL;
/* Ticks since this player last heard from the host: a world message or the
 * host's loading pulse sets it to 0, and the ticks that pass without one add up
 * (every frame on a client in the new code, during stalls and resyncs in the
 * original). Past 7,080 ticks the player gives up on the host and leaves the
 * flight. 8 functions write it, chiefly flight_net_process_incoming_packets,
 * flight_net_handle_world_state_resync_packet, flight_run_mission_loop and
 * xvt_flight_frame_network_update; both mission-start waits zero it. */
// GLOBAL: XVT 0x9A8C24
int g_flight_net_host_timeout_elapsed_ticks = 0;
/* Set to 1 when any resync chunk ack arrives (flight_net_process_incoming_packets,
 * xvt_flight_network_control); the wait for a resync apply ack
 * (flight_net_send_world_state_resync_apply_request, xvt_resync_apply) clears it and
 * restarts its wait window. */
// GLOBAL: XVT 0x556F00
int g_flight_net_world_state_ack_received_flag = 0;
/* Per chunk slot in the current batch of 16 resync chunks, 1 once the receiving
 * player has acked it. Set by the chunk-ack handlers
 * (flight_net_process_incoming_packets, xvt_flight_network_control); cleared for
 * each new batch by flight_net_send_world_state_resync_to_player and the XvtResync_
 * functions. */
// GLOBAL: XVT 0x556F08
int g_flight_net_world_state_chunk_acked[16] = {0};
/* Per world-state segment, the host's checksum of the world it is resending,
 * filled by flight_build_world_state_resync_segment_checksums for
 * flight_net_send_world_state_resync_to_player; segments whose checksum matches the
 * receiver's are not sent. Only the original build uses it. */
// GLOBAL: XVT 0x556F48
int g_flight_net_local_resync_checksums[126] = {0};
/* Per world-state segment, the checksums the receiving player sent back in
 * RESYNC_CHECKSUMS, copied in by flight_net_process_incoming_packets. Only the
 * original build uses it. */
// GLOBAL: XVT 0x557588
int g_flight_net_remote_resync_checksums[126] = {0};
/* The batch of up to 16 RESYNC_CHUNK packets being built and sent to one
 * player; built by flight_net_send_world_state_resync_to_player and the XvtResync_
 * chunk builders. */
// GLOBAL: XVT 0x557788
struct flight_net_world_state_chunk_packet
	g_flight_net_world_state_chunk_packets[16] = {{0}};
/* Set to 1 when the receiving player's RESYNC_CHECKSUMS arrives; the resync
 * sender clears it before it waits and stops waiting once it is set. 5
 * functions write it: flight_net_process_incoming_packets,
 * flight_net_send_world_state_resync_to_player, xvt_resync_begin_send,
 * xvt_resync_checksums and xvt_resync_receive_packet. */
// GLOBAL: XVT 0x55978C
int g_flight_net_remote_resync_checksums_received_flag = 0;

/* Picks the player the communication-failure alert names: the one in
 * g_flight_net_resync_player_dplay_id, or the host when that id is 0 or the player
 * has aborted or no longer takes part. Stores the chosen id back in
 * g_flight_net_resync_player_dplay_id and returns net_session_get_player_name for that
 * slot. Does not check for the 8 net_session_find_player_slot_by_dpid returns when
 * no player has the id. Only the original build calls this. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x462A10
char *flight_net_resolve_resync_player_name(void)
{
	int player_dplay_id = g_flight_net_resync_player_dplay_id;
	if (player_dplay_id == 0) {
		player_dplay_id = net_session_get_host_dplay_id();
	}
	g_flight_net_resync_player_dplay_id = player_dplay_id;
	int player_slot = net_session_find_player_slot_by_dpid(player_dplay_id);
	if (g_player_abort_flags[player_slot] != 0) {
		g_flight_net_resync_player_dplay_id =
			net_session_get_host_dplay_id();
		player_slot = net_session_find_player_slot_by_dpid(
			net_session_get_host_dplay_id());
	}
	if (g_players[player_slot].participation_state == 0) {
		g_flight_net_resync_player_dplay_id =
			net_session_get_host_dplay_id();
		player_slot = net_session_find_player_slot_by_dpid(
			net_session_get_host_dplay_id());
	}
	return net_session_get_player_name(player_slot);
}

/* Before a flight, shares each player's screen resolution mode, rating and
 * taunts. The modern build hands off to xvt_flight_network_exchange_options and
 * returns its result: 1 done, 0 failed, XVT_FLIGHT_NETWORK_PENDING while it
 * waits, and it is called again each frame. The original blocks: a client sends
 * its mode and rating to the host and waits for the host's roster of every
 * player's, which also sets g_flight_conf_new_net; the host collects one from each
 * other player, then broadcasts that roster and waits to receive it. Then every
 * player sends its taunts to all and stores what arrives in g_player_taunt_text
 * until it has one set per active player or 30 s pass with no packet. Writes
 * g_players[].network.flight_resolution_mode, g_players[].pilot_rating and
 * g_flight_net_scratch_packet, and shows the waiting alert with each still-loading
 * player's name. Returns 0 when 60 s pass with no packet before the roster
 * arrives, else 1; a solo flight copies its own values into slot 0 and returns
 * 1. Does not check the slot number in a taunt packet. */
// FUNCTION: XVT 0x463160
int flight_net_sync_player_options_and_taunts(void)
{
	return xvt_flight_network_exchange_options();
}

/* Holds every player at the mission start and starts the flight clocks
 * together. The modern build hands off to xvt_flight_network_wait_for_mission_start
 * and returns its result: 1, 0, or XVT_FLIGHT_NETWORK_PENDING while it waits.
 * The original blocks. It resets g_flight_net_last_input_timestamp_by_player,
 * g_flight_net_peer_silence_ticks, g_last_sent_input_timestamp, g_last_keyframe_time,
 * g_flight_net_resync_player_dplay_id, g_flight_net_last_input_batch_send_time,
 * g_flight_net_input_batch_packet and g_flight_net_input_batch_len,
 * g_flight_net_recovery_ui_active, g_flight_net_pending_ack_count,
 * g_flight_net_clock_adjust_accum_ticks and g_flight_net_host_timeout_elapsed_ticks, and
 * sets g_flight_net_input_batch_interval_ticks to 23; each player tells the host it
 * has loaded; the host waits for all active players, itself included, and
 * broadcasts the start; everyone waits for the start, acks it to the host, and
 * zeroes g_server_tick_time, g_game_time and g_input_timestamp. Sets
 * g_flight_net_clock_lead_ticks to 130 ticks for internet play, else 30, and
 * g_net_update_interval_ticks from the server update rate. The host then reads
 * packets until its 1 or 2 pending acks arrive or 100 ticks pass, and sets
 * g_flight_net_clock_lead_ticks to the time that took, at least 35 ticks, moving
 * g_input_timestamp and g_flight_net_clock_adjust_accum_ticks to match. Returns 0
 * when a 60 s wait sees no packet, else 1; a solo flight sets
 * g_flight_net_clock_lead_ticks and g_input_timestamp to 30 and returns 1 at
 * once. */
// FUNCTION: XVT 0x463790
int flight_net_wait_for_mission_start(void)
{
	return xvt_flight_network_wait_for_mission_start();
}

/* Sends the host a clock probe holding the adjusted input time
 * (g_input_timestamp plus g_flight_net_clock_adjust_accum_ticks) and this player's
 * g_flight_net_clock_lead_ticks, and keeps that time in
 * g_flight_net_clock_probe_timestamp to match the reply. Writes
 * g_flight_net_scratch_packet. The modern build sends through
 * xvt_flight_network_send_packet. Returns the send function's result. */
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
 * Writes g_flight_net_scratch_packet. The modern build sends through
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
 * which ends the flight for each. Writes g_flight_net_scratch_packet. The modern
 * build sends through xvt_flight_network_broadcast. Returns the broadcast's
 * result. Does not check that this player is the host. */
// FUNCTION: XVT 0x463BD0
int flight_net_broadcast_host_session_abort(void)
{
	g_flight_net_scratch_packet.packet_type = NET_PACKET_SESSION_ABORT;
	XVT_LOG_INFO("network.session_abort_sent tick=%d", g_server_tick_time);
	return xvt_flight_network_broadcast(
		(unsigned int *)&g_flight_net_scratch_packet, 4);
}

/* Tells the host alone that this player is still loading, with a 4-byte
 * STILL_LOADING packet; the host then resets its silence count for this player.
 * Writes g_flight_net_scratch_packet. Returns the send function's result. Only the
 * original build calls this. */
// FUNCTION: XVT 0x463BF0
int flight_net_send_still_loading_pulse(void)
{
	g_flight_net_scratch_packet.packet_type = NET_PACKET_STILL_LOADING;
	return xvt_flight_network_send_packet(
		net_session_get_host_dplay_id(),
		(unsigned int *)&g_flight_net_scratch_packet, 4);
}

/* Tells every active player that the player in player_slot has lost its link,
 * and clears g_player_connected for that slot here; players then stop sending it
 * their inputs directly. Writes g_flight_net_scratch_packet. Returns the
 * broadcast's result. Does not check the slot range. Only the original build
 * calls this. */
// FUNCTION: XVT 0x463C10
int flight_net_broadcast_player_disconnected(int player_slot)
{
	g_flight_net_scratch_packet.packet_type =
		NET_PACKET_PLAYER_DISCONNECTED;
	g_flight_net_scratch_packet.payload_dwords[0] = player_slot;
	int result;
	result = xvt_flight_network_broadcast(
		(unsigned int *)&g_flight_net_scratch_packet, 8);
	g_player_connected[player_slot] = 0;
	return result;
}

/* Tells every active player, this one included, that the player in player_slot
 * leaves the flight; the named player ends its flight when the packet reaches
 * it. Writes g_flight_net_scratch_packet. The modern build sends through
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

/* Reads and acts on every flight packet waiting in the queue. The modern build
 * hands off to xvt_flight_network_process_packets. The original does nothing when
 * this player no longer takes part, and drops every packet in a solo flight.
 * Otherwise it reads until the queue is empty, the host first sending each
 * world message that is due. Inputs, single or batched, go into the sender's
 * history, marked for relay on the host; a sender who no longer takes part is
 * told it has aborted. A world message is applied and clears
 * g_flight_net_host_timeout_elapsed_ticks; on the host it adds
 * g_net_update_interval_ticks to each other player's silence count and aborts any
 * player past 7,080 ticks. Clock probes and replies move
 * g_flight_net_clock_lead_ticks halfway (at least 1 tick) toward the value they
 * imply. Resync requests, applies and chunks from another checksum epoch are
 * dropped. A call that runs more than 826 ticks opens the communication-failure
 * alert (a client also announces itself disconnected and calls
 * net_reliable_keep_only_host_received_packets); while it is open, ESC makes this
 * player leave, and the host also ends the session. Returns early when the
 * flight ends, the last pending ack arrives, or a resync times out or ends this
 * player's flight. Writes g_input_timestamp, g_server_tick_time,
 * g_flight_net_recovery_ui_active, g_flight_net_recovery_ui_blink_time,
 * g_flight_net_recovery_saved_input_timestamp, g_flight_net_peer_silence_ticks,
 * g_flight_net_last_input_timestamp_by_player, g_player_connected, g_player_abort_flags,
 * g_flight_net_host_abort_received, g_flight_net_host_timeout_elapsed_ticks,
 * g_flight_net_pending_ack_count, g_flight_net_world_message_turn_timestamp,
 * g_flight_net_resync_player_dplay_id, g_flight_net_world_state_ack_received_flag,
 * g_flight_net_world_state_chunk_acked, g_flight_net_remote_resync_checksums and its
 * received flag, g_flight_net_clock_lead_ticks, g_flight_net_scratch_packet,
 * g_flight_net_received_world_message_count, g_flight_mission_state.mission_end_pending
 * and participation_state. The timing breakdown it formats at the end is never
 * shown. Does not check for the slot 8 that net_session_find_player_slot_by_dpid
 * returns for an unknown sender. */
// FUNCTION: XVT 0x463D00
void flight_net_process_incoming_packets(void)
{
	xvt_flight_network_process_packets();
}

/* Samples this player's controls into g_current_input_frame and files it in this
 * player's input history at g_input_timestamp, not awaiting relay. The modern
 * build adds roll and throttle, sends nothing, and returns 0. The original
 * first adds the ticks elapsed to g_input_timestamp and encodes the input: one
 * code byte with the timestamp's low 7 bits, or 127 and the full 4-byte
 * timestamp when g_last_sent_input_timestamp is 0, after a gap of 127 ticks or
 * more or a backward step, or when the previous input came more than 236 ticks
 * after the last full one (g_last_keyframe_time); the code's top bit adds a key
 * byte; then the X and Y bytes, each carrying a key-modifier bit in its low
 * bit. With other players it logs the input to inputlog.txt through
 * g_input_log_file when g_input_log_enabled is 1. Without internet play it sends
 * the packet to each connected player still flying, itself included. With
 * internet play it appends the record to g_flight_net_input_batch_packet and, once
 * more than g_flight_net_input_batch_interval_ticks have passed, sends the batch to
 * the host, and in sessions under g_flight_net_small_session_player_threshold also
 * to each other connected player, then empties it. Also writes
 * g_last_sent_input_timestamp, g_flight_net_scratch_packet, g_flight_net_input_batch_len
 * and g_flight_net_last_input_batch_send_time. Returns flight_pump_window_messages's
 * result. Does not check that the batch has room for another record. */
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

/* Says whether the host should send a world message now, and if so moves
 * g_flight_net_world_message_turn_timestamp on by g_net_update_interval_ticks. Returns
 * 0 while acks are pending or the recovery alert is up, and until a full
 * interval has passed since the turn timestamp, which starts at the adjusted
 * time (input_timestamp plus g_flight_net_clock_adjust_accum_ticks) plus an eighth of
 * g_flight_net_clock_lead_ticks. Returns 1 when more than 5 intervals have passed,
 * or when the latest input awaiting relay of every player still flying is later
 * than the last world message's tick plus one interval; else 0. Only the
 * original build calls this; its modern arm hands off to
 * xvt_flight_network_take_world_send_turn. */
// FUNCTION: XVT 0x464C80
int flight_net_take_world_message_turn(int input_timestamp)
{
	return xvt_flight_network_take_world_send_turn(input_timestamp);
}

/* Sends the host's world message: every input awaiting relay, from each player
 * still flying, up to the message's tick. The original counts the call in
 * g_flight_net_sent_world_message_count and stops there in a solo flight. The tick
 * is g_flight_net_last_sent_world_message_timestamp plus g_net_update_interval_ticks,
 * stored back there; the ticks since g_server_tick_time add up in
 * g_flight_net_checksum_request_accum_ticks, and past 472 the tick's top bit asks
 * every player for a world checksum, g_flight_net_world_checksum_peer_status is
 * cleared and the sum restarts. Per player: a record count byte, then per input
 * a code byte holding the ticks before the message tick (0-124 as is; 125 and 1
 * more byte; 126 and 2 bytes; 127 and the full 4-byte timestamp), its top bit
 * adding a key byte, then the X and Y bytes with a key-modifier bit in each low
 * bit. Marks each input sent as relayed, and skips the rest of a player's
 * inputs once one more might not fit in 504 bytes less the player count. Sends
 * to DirectPlay id 0, which reaches all players. With g_input_log_enabled at 1 it
 * writes serverlog.txt through g_flight_net_server_log_file, but reads the records
 * as fixed 10-byte entries the packet does not hold. input_timestamp is unused.
 * Writes g_flight_net_scratch_packet. Only the original build calls this; its
 * modern arm hands off to xvt_flight_network_send_world. */
// FUNCTION: XVT 0x464D70
void flight_net_broadcast_world_message(int input_timestamp)
{
	(void)input_timestamp;
	xvt_flight_network_send_world();
}

/* Sends the host this player's world checksum: a WORLD_CHECKSUM packet holding
 * g_server_tick_time, checksum_dword_count checksum words, then as many region
 * lengths. The modern build adds one zero word after them, which the host's
 * flight_sync_handle_world_checksum_packet reads as a request to resend the world
 * when it is 1, and sends through xvt_flight_network_send_packet. Writes
 * g_flight_net_scratch_packet. Returns the send function's result. Does not check
 * that the words fit in the packet. */
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
 * SERVER_CHECKSUM packet holding g_server_tick_time, checksum_dword_count checksum
 * words, then as many region lengths. Writes g_flight_net_scratch_packet. The
 * modern build sends through xvt_flight_network_broadcast. Returns the
 * broadcast's result. Does not check that the words fit in the packet or that
 * this player is the host. */
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

/* Tells the player a resync went to that it may apply the world: sends a
 * RESYNC_APPLY with g_flight_net_world_checksum_epoch, world_state_size and
 * g_input_timestamp, then waits for its ack in up to 10 passes of 236 ticks,
 * reading packets; a chunk ack restarts the pass, ESC ends the wait, and every
 * 236 ticks waited sends all players a STILL_LOADING. With no ack it tells all
 * players that player has aborted. Then sets g_input_timestamp to
 * g_flight_net_clock_lead_ticks plus g_server_tick_time and sends all players a
 * RESYNC_NOTICE of 0, which ends the resync. Writes g_flight_net_pending_ack_count,
 * g_flight_net_world_state_ack_received_flag and g_flight_net_scratch_packet. Only the
 * original build calls this; its modern arm hands off to
 * xvt_resync_begin_apply. */
// FUNCTION: XVT 0x4651F0
void flight_net_send_world_state_resync_apply_request(int direct_play_id,
						      int world_state_size)
{
	xvt_resync_begin_apply(direct_play_id, world_state_size);
}

/* Resends the world state to a player whose checksum did not match. Shows the
 * communication-failure alert with the player's name, tells all players a
 * resync for that DirectPlay id has begun, and sends the player a
 * RESYNC_REQUEST with the object presence map. It then waits up to 10 passes of
 * 236 ticks for the player's segment checksums, sending it and all players a
 * STILL_LOADING every 236 ticks; ESC ends the wait. With no answer it tells all
 * players that player has aborted and returns 0. Otherwise it sends only the
 * segments whose checksums differ from g_flight_net_local_resync_checksums, as
 * RESYNC_CHUNK packets of (offset, size, bytes) records ended by an offset of
 * -1, in batches of 16 that the player must ack; it always sends a last chunk,
 * even an empty one. Returns 0 when an ack wait fails, else 1. Writes
 * g_flight_net_world_state_chunk_packets, g_flight_net_world_state_chunk_acked,
 * g_flight_net_remote_resync_checksums_received_flag, g_flight_net_pending_ack_count (1
 * while busy, 0 after) and g_flight_net_scratch_packet, and adds the ticks elapsed
 * on entry to g_input_timestamp. Only the original build calls this; its modern
 * arm hands off to xvt_resync_begin_send. */
// FUNCTION: XVT 0x465390
int flight_net_send_world_state_resync_to_player(int direct_play_id,
						 uint8_t *world_state,
						 int world_state_size)
{
	return xvt_resync_begin_send(direct_play_id, world_state,
				     world_state_size);
}

/* Waits until the player has acked the first chunk_count resync chunks in
 * g_flight_net_world_state_chunk_acked, reading packets, and returns 1. Every 236
 * ticks it sends all players a STILL_LOADING and changes the alert text; after
 * 20 of those in a row with no new ack, or on ESC, it tells all players that
 * player has aborted and returns 0. Writes g_flight_net_scratch_packet;
 * g_input_timestamp is put back after each read. Only the original build calls
 * this; its modern arm hands off to xvt_resync_wait_acks. */
// FUNCTION: XVT 0x4658C0
int flight_net_wait_for_world_state_chunk_acks(int direct_play_id,
					       int chunk_count)
{
	return xvt_resync_wait_acks(direct_play_id, chunk_count);
}
