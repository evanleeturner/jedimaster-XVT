#include "xvt/net/net.h"

#include <string.h>

#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_draw.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/net/net_reliable.h"
#include "xvt/net/net_session.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log.h"

/* Reads every waiting DirectPlay message into the lobby receive queue,
 * g_front_state.net_runtime_recv_queue, after sending due keepalives
 * (net_send_sequence_keepalives) and checking for silent peers
 * (net_drop_silent_peers). Stops when DirectPlay has nothing more or 1023
 * entries are queued, and does nothing without DirectPlay. System messages
 * (sender 0) are queued whole, up to 512 bytes; messages for anyone but the
 * local player are dropped. A PING is answered with a PONG. A KEEPALIVE_ACK
 * stamps the sender's last_heard_ms and updates its g_net_player_connection_stats
 * entry: the counts it carries, and a latency sample (the time since the
 * echoed stamp, less 40 ms) when under 750 ms and not over half above the
 * average; a new entry starts with the sample capped at 750. A NACK, or a
 * WORLD_NACK once a flight has ended, resends the packet asked for from the
 * sent history or from the flight's world-message history, or a NOP with
 * that sequence when it is gone, and counts a drop on the sender after its
 * first 20 packets. A KEEPALIVE from the host is answered with a
 * KEEPALIVE_ACK carrying this side's counts; any KEEPALIVE makes it resend,
 * on each channel whose counter has moved on, the packet the sender expects
 * next. Every other packet stamps the sender's last_heard_ms and is queued with
 * its channel and sequence: a resend as a resent copy; otherwise its trailer
 * first, when the previous sequence was not seen (a drop), then the packet
 * itself unless its sequence was already seen. Bodies are cut to 508 bytes.
 * The back buffer is unlocked meanwhile and relocked when it was locked. */
// FUNCTION: XVT 0x4CE130
void net_pump_incoming_packets(void)
{
	enum {
		QUEUE_CAPACITY = 1024,
		QUEUE_LIMIT = QUEUE_CAPACITY - 1,
		SYSTEM_PACKET_SIZE_LIMIT = 512,
		HISTORY_CAPACITY = 128,
		SENT_WORLD_MESSAGE_HISTORY_CAPACITY = 256,
		PEER_CAPACITY = 40,
		MAX_PAYLOAD_SIZE = 508,
		SEQUENCE_LIMIT = 127,
		SEQUENCE_NOTHING_TO_RESEND = 128,
		LATENCY_SEND_BIAS_MS = 40,
		LATENCY_MAX_MS = 750,
		LATENCY_OUTLIER_PERCENT = 50,
		PACKET_DROP_WARMUP_COUNT = 20,
	};

	struct {
		uint16_t header;
		uint8_t data[1022];
	} wire_packet;

	int back_buffer_locked = g_front_state.back_buffer_locked;

	frontend_display_unlock_back_buffer();
	DPID from_id;
	DPID to_id;
	int packet_words[6];
	if (g_front_state.net_direct_play != NULL) {
		net_send_sequence_keepalives();
		net_drop_silent_peers();
		for (;;) {
			if (g_front_state.net_runtime_recv_queue_count >=
			    QUEUE_LIMIT) {
				XVT_LOG_WARN(
					"network.lobby_receive_queue_full queued=%d",
					g_front_state
						.net_runtime_recv_queue_count);
				if (back_buffer_locked != 0) {
					g_draw_surface_ptr =
						frontend_display_lock_back_buffer();
				}
				return;
			}
			uint32_t wire_size = sizeof(wire_packet);
			if (g_front_state.net_direct_play->lpVtbl->Receive(
				    g_front_state.net_direct_play, &from_id,
				    &to_id, 1, &wire_packet, &wire_size) != 0) {
				if (back_buffer_locked != 0) {
					g_draw_surface_ptr =
						frontend_display_lock_back_buffer();
				}
				return;
			}
			if (from_id == 0) {
				XVT_LOG_DEBUG(
					"network.lobby_system_received bytes=%u",
					(unsigned)wire_size);
				if (wire_size > SYSTEM_PACKET_SIZE_LIMIT) {
					wire_size = SYSTEM_PACKET_SIZE_LIMIT;
				}
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.direct_play_id = from_id;
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.payload_size = wire_size;
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.is_resent_copy = 0;
				memcpy(g_front_state
					       .net_runtime_recv_queue
						       [g_front_state
								.net_runtime_recv_queue_write_index]
					       .payload,
				       &wire_packet.header, wire_size);
				++g_front_state
					  .net_runtime_recv_queue_write_index;
				++g_front_state.net_runtime_recv_queue_count;
				if (g_front_state
					    .net_runtime_recv_queue_write_index >=
				    QUEUE_CAPACITY) {
					g_front_state
						.net_runtime_recv_queue_write_index =
						0;
				}
				continue;
			}
			if (to_id !=
			    g_front_state.net_runtime_local_player.player_id) {
				XVT_LOG_DEBUG(
					"network.lobby_packet_not_local from=%u to=%u",
					(unsigned)from_id, (unsigned)to_id);
				continue;
			}

			int *payload = (int *)wire_packet.data;
			unsigned int packet_type = wire_packet.header & 0x7F;
			int group_channel = (wire_packet.header & 0x80) != 0;
			int broadcast_channel =
				(wire_packet.header & 0x8000) == 0;
			int sequence = (wire_packet.header >> 8) & 0x7F;
			/* Outside the resync types (60-63) every packet carries
			 * a piggyback trailer after its payload, and a type
			 * with no fixed size also starts with a length word;
			 * has_length stands for both. */
			int has_length =
				packet_type < NET_PACKET_RESYNC_CHECKSUMS ||
				packet_type >= NET_PACKET_RESYNC_CHUNK + 1;
			uint32_t payload_size;
			if (has_length) {
				payload_size = (uint32_t)
					net_session_get_fixed_payload_size(
						(int)packet_type);
				if (payload_size == 0) {
					payload_size =
						*(const uint16_t *)
							 wire_packet.data;
					payload = (int *)(wire_packet.data +
							  sizeof(uint16_t));
				}
			}
			if (packet_type == NET_PACKET_PING) {
				packet_words[0] = NET_PACKET_PONG;
				net_send_packet_and_flush(
					(int)from_id, packet_words,
					sizeof(packet_words[0]));
				XVT_LOG_DEBUG(
					"network.lobby_ping_answered from=%u",
					(unsigned)from_id);
				continue;
			}

			unsigned int peer_index;
			if (packet_type == NET_PACKET_KEEPALIVE_ACK) {
				peer_index = net_find_or_create_peer_slot(
					(int)from_id);
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.last_heard_ms = GetTickCount();
				uint32_t echoed_send_ms = (uint32_t)payload[0];
				/* Until the average is computed below,
				 * average_latency_ms holds the current time
				 * less LATENCY_SEND_BIAS_MS, the clock the
				 * echoed send time is measured against. */
				uint32_t average_latency_ms =
					GetTickCount() - LATENCY_SEND_BIAS_MS;
				uint32_t latency_ms;
				if (echoed_send_ms >= average_latency_ms) {
					latency_ms = 1;
				} else {
					latency_ms = average_latency_ms -
						     echoed_send_ms;
				}

				unsigned int stats_index = 0;
				while (stats_index < PEER_CAPACITY &&
				       g_net_player_connection_stats
						       [stats_index]
							       .player_id !=
					       (int)from_id) {
					++stats_index;
				}
				if (stats_index < PEER_CAPACITY) {
					if (latency_ms < LATENCY_MAX_MS) {
						if (g_net_player_connection_stats
							    [stats_index]
								    .latency_sample_count !=
						    0) {
							uint32_t latency_total_ms =
								g_net_player_connection_stats
									[stats_index]
										.latency_total_ms;
							average_latency_ms =
								latency_total_ms /
								g_net_player_connection_stats
									[stats_index]
										.latency_sample_count;
							if (latency_ms >
							    average_latency_ms) {
								if (100 *
									    (latency_ms -
									     average_latency_ms) /
									    average_latency_ms >
								    LATENCY_OUTLIER_PERCENT) {
									g_net_player_connection_stats
										[stats_index]
											.packet_count = payload
										[1];
									g_net_player_connection_stats
										[stats_index]
											.packet_drop_count =
										payload[2];
									g_net_player_connection_stats
										[stats_index]
											.packet_retry_count =
										payload[3];
								} else {
									g_net_player_connection_stats
										[stats_index]
											.latency_total_ms =
										latency_ms +
										latency_total_ms;
									g_net_player_connection_stats
										[stats_index]
											.packet_count = payload
										[1];
									g_net_player_connection_stats
										[stats_index]
											.packet_drop_count =
										payload[2];
									g_net_player_connection_stats
										[stats_index]
											.packet_retry_count =
										payload[3];
									++g_net_player_connection_stats
										  [stats_index]
											  .latency_sample_count;
								}
							} else {
								g_net_player_connection_stats
									[stats_index]
										.latency_total_ms =
									latency_ms +
									latency_total_ms;
								g_net_player_connection_stats
									[stats_index]
										.packet_count =
									payload[1];
								g_net_player_connection_stats
									[stats_index]
										.packet_drop_count =
									payload[2];
								g_net_player_connection_stats
									[stats_index]
										.packet_retry_count =
									payload[3];
								++g_net_player_connection_stats
									  [stats_index]
										  .latency_sample_count;
							}
						}
					} else {
						g_net_player_connection_stats
							[stats_index]
								.packet_count =
							payload[1];
						g_net_player_connection_stats
							[stats_index]
								.packet_drop_count =
							payload[2];
						g_net_player_connection_stats
							[stats_index]
								.packet_retry_count =
							payload[3];
					}
				}

				if (stats_index >= PEER_CAPACITY) {
					stats_index = 0;
					while (stats_index < PEER_CAPACITY &&
					       g_net_player_connection_stats
							       [stats_index]
								       .player_id !=
						       0) {
						++stats_index;
					}
					if (stats_index < PEER_CAPACITY) {
						if (latency_ms >
						    LATENCY_MAX_MS) {
							latency_ms =
								LATENCY_MAX_MS;
						}
						g_net_player_connection_stats
							[stats_index]
								.player_id =
							(int)from_id;
						g_net_player_connection_stats
							[stats_index]
								.latency_total_ms =
							latency_ms;
						g_net_player_connection_stats
							[stats_index]
								.packet_count =
							payload[1];
						g_net_player_connection_stats
							[stats_index]
								.packet_drop_count =
							payload[2];
						g_net_player_connection_stats
							[stats_index]
								.packet_retry_count =
							payload[3];
						g_net_player_connection_stats
							[stats_index]
								.latency_sample_count =
							1;
					} else {
						XVT_LOG_WARN(
							"network.lobby_link_stats_full player=%u figure=\"all\"",
							(unsigned)from_id);
					}
				}
				XVT_LOG_DEBUG(
					"network.lobby_keepalive_ack from=%u latency=%u entry=%u packets=%d drops=%d retries=%d",
					(unsigned)from_id, (unsigned)latency_ms,
					stats_index, payload[1], payload[2],
					payload[3]);
				continue;
			}

			if (packet_type == NET_PACKET_WORLD_NACK) {
				if (g_front_state
					    .net_flight_sent_world_message_history !=
				    NULL) {
					int missing_world_tick = payload[0];
					int control_value1 = payload[1];
					peer_index =
						net_find_or_create_peer_slot(
							(int)from_id);
					if ((unsigned int)g_front_state
						    .net_runtime_reliable_peer_slots
							    [peer_index]
						    .packet_count >
					    PACKET_DROP_WARMUP_COUNT) {
						++g_front_state
							  .net_runtime_reliable_peer_slots
								  [peer_index]
							  .packet_drop_count;
					}

					int search_index =
						g_front_state
							.net_flight_sent_world_message_write_index;
					unsigned int search_count = 0;
					struct net_queued_packet *queued;
					while (search_count <
					       SENT_WORLD_MESSAGE_HISTORY_CAPACITY) {
						queued =
							&g_front_state.net_flight_sent_world_message_history
								 [search_index];
						if (queued->payload_size != 0) {
							if ((*(const uint32_t
								       *)&queued->payload
								      [sizeof(int)] &
							     0x7FFFFFFF) ==
							    (uint32_t)
								    missing_world_tick) {
								break;
							}
						}
						++search_count;
						++search_index;
						if ((unsigned int)
							    search_index >=
						    SENT_WORLD_MESSAGE_HISTORY_CAPACITY) {
							search_index = 0;
						}
					}
					if (search_count <
					    SENT_WORLD_MESSAGE_HISTORY_CAPACITY) {
						queued =
							&g_front_state.net_flight_sent_world_message_history
								 [search_index];
						net_send_sequenced_direct_play_packet(
							(int)from_id, 0,
							queued->sequence_byte,
							queued->payload,
							queued->payload_size);
					}
					if (search_count >=
					    SENT_WORLD_MESSAGE_HISTORY_CAPACITY) {
						packet_words[0] =
							NET_PACKET_NOP;
						net_send_sequenced_direct_play_packet(
							(int)from_id, 0,
							control_value1,
							packet_words,
							sizeof(packet_words
								       [0]));
					}
					XVT_LOG_DEBUG(
						"network.lobby_world_nack from=%u tick=%d found=%d",
						(unsigned)from_id,
						missing_world_tick,
						search_count <
							SENT_WORLD_MESSAGE_HISTORY_CAPACITY);
				} else {
					XVT_LOG_WARN(
						"network.lobby_world_nack_ignored from=%u tick=%d",
						(unsigned)from_id, payload[0]);
				}
				continue;
			}

			if (packet_type == NET_PACKET_NACK) {
				int expected_broadcast_sequence = payload[0];
				int control_value1 = payload[1];
				peer_index = net_find_or_create_peer_slot(
					(int)from_id);
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.last_heard_ms = GetTickCount();
				if ((unsigned int)g_front_state
					    .net_runtime_reliable_peer_slots
						    [peer_index]
					    .packet_count >
				    PACKET_DROP_WARMUP_COUNT) {
					++g_front_state
						  .net_runtime_reliable_peer_slots
							  [peer_index]
						  .packet_drop_count;
				}

				int search_index =
					g_front_state
						.net_runtime_sent_history_write_index;
				unsigned int search_count = 0;
				while (search_count < HISTORY_CAPACITY) {
					if (g_front_state
						    .net_runtime_sent_history
							    [search_index]
						    .payload_size != 0) {
						if (control_value1 == 0 ||
						    control_value1 == 2) {
							if (g_front_state.net_runtime_sent_history
									    [search_index]
										    .sequence_byte ==
								    expected_broadcast_sequence &&
							    g_front_state.net_runtime_sent_history
									    [search_index]
										    .packet_class ==
								    control_value1) {
								break;
							}
						} else if (
							g_front_state.net_runtime_sent_history
									[search_index]
										.direct_play_id ==
								from_id &&
							g_front_state.net_runtime_sent_history
									[search_index]
										.sequence_byte ==
								expected_broadcast_sequence &&
							g_front_state.net_runtime_sent_history
									[search_index]
										.packet_class ==
								control_value1) {
							break;
						}
					}
					++search_count;
					++search_index;
					if ((unsigned int)search_index >=
					    HISTORY_CAPACITY) {
						search_index = 0;
					}
				}
				if (search_count < HISTORY_CAPACITY) {
					net_send_sequenced_direct_play_packet(
						(int)from_id, control_value1,
						expected_broadcast_sequence,
						g_front_state
							.net_runtime_sent_history
								[search_index]
							.payload,
						g_front_state
							.net_runtime_sent_history
								[search_index]
							.payload_size);
				}
				if (search_count >= HISTORY_CAPACITY) {
					packet_words[0] = NET_PACKET_NOP;
					net_send_sequenced_direct_play_packet(
						(int)from_id, control_value1,
						expected_broadcast_sequence,
						packet_words,
						sizeof(packet_words[0]));
				}
				XVT_LOG_DEBUG(
					"network.lobby_nack from=%u seq=%d channel=%d found=%d",
					(unsigned)from_id,
					expected_broadcast_sequence,
					control_value1,
					search_count < HISTORY_CAPACITY);
				continue;
			}

			if (packet_type == NET_PACKET_KEEPALIVE) {
				int control_value0 = payload[0];
				int expected_group_sequence = payload[1];
				int expected_direct_sequence = payload[2];
				if (g_front_state.net_host_player_id ==
				    from_id) {
					peer_index =
						net_find_or_create_peer_slot(
							(int)from_id);
					g_front_state
						.net_runtime_reliable_peer_slots
							[peer_index]
						.last_heard_ms = GetTickCount();
					packet_words[0] =
						NET_PACKET_KEEPALIVE_ACK;
					packet_words[1] = payload[3];
					packet_words[2] =
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.packet_count;
					packet_words[3] =
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.packet_drop_count;
					packet_words[4] =
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.packet_retry_count;
					net_send_direct_play_packet(
						(int)g_front_state
							.net_host_player_id,
						packet_words,
						5 * sizeof(packet_words[0]), 0);
				}
				if (g_front_state
					    .net_runtime_broadcast_seq_counter ==
				    control_value0) {
					control_value0 =
						SEQUENCE_NOTHING_TO_RESEND;
				}
				if (g_front_state
					    .net_runtime_group_seq_counter ==
				    expected_group_sequence) {
					expected_group_sequence =
						SEQUENCE_NOTHING_TO_RESEND;
				}
				peer_index = net_find_or_create_peer_slot(
					(int)from_id);
				if (g_front_state.net_reliable_peer_slot_count ==
					    peer_index ||
				    g_front_state.net_runtime_reliable_peer_slots
						    [peer_index]
							    .send_seq ==
					    expected_direct_sequence) {
					expected_direct_sequence =
						SEQUENCE_NOTHING_TO_RESEND;
				}

				unsigned int search_count = HISTORY_CAPACITY;
				int search_index =
					g_front_state
						.net_runtime_sent_history_write_index;
				do {
					if (control_value0 ==
						    SEQUENCE_NOTHING_TO_RESEND &&
					    expected_group_sequence ==
						    SEQUENCE_NOTHING_TO_RESEND &&
					    expected_direct_sequence ==
						    SEQUENCE_NOTHING_TO_RESEND) {
						break;
					}
					if (g_front_state
						    .net_runtime_sent_history
							    [search_index]
						    .payload_size != 0) {
						uint8_t packet_class =
							g_front_state
								.net_runtime_sent_history
									[search_index]
								.packet_class;
						if (packet_class == 0) {
							if (g_front_state
								    .net_runtime_sent_history
									    [search_index]
								    .sequence_byte ==
							    control_value0) {
								net_send_sequenced_direct_play_packet(
									(int)from_id,
									0,
									control_value0,
									g_front_state
										.net_runtime_sent_history
											[search_index]
										.payload,
									g_front_state
										.net_runtime_sent_history
											[search_index]
										.payload_size);
								control_value0 =
									SEQUENCE_NOTHING_TO_RESEND;
							}
						} else if (packet_class == 2) {
							if (g_front_state
								    .net_runtime_sent_history
									    [search_index]
								    .sequence_byte ==
							    expected_group_sequence) {
								net_send_sequenced_direct_play_packet(
									(int)from_id,
									2,
									expected_group_sequence,
									g_front_state
										.net_runtime_sent_history
											[search_index]
										.payload,
									g_front_state
										.net_runtime_sent_history
											[search_index]
										.payload_size);
								expected_group_sequence =
									SEQUENCE_NOTHING_TO_RESEND;
							}
						} else {
							if (g_front_state.net_runtime_sent_history
									    [search_index]
										    .direct_play_id ==
								    from_id &&
							    g_front_state.net_runtime_sent_history
									    [search_index]
										    .sequence_byte ==
								    expected_direct_sequence) {
								net_send_sequenced_direct_play_packet(
									(int)from_id,
									1,
									expected_direct_sequence,
									g_front_state
										.net_runtime_sent_history
											[search_index]
										.payload,
									g_front_state
										.net_runtime_sent_history
											[search_index]
										.payload_size);
								expected_direct_sequence =
									SEQUENCE_NOTHING_TO_RESEND;
							}
						}
					}
					if ((unsigned int)++search_index >=
					    HISTORY_CAPACITY) {
						search_index = 0;
					}
				} while (--search_count != 0);
				XVT_LOG_DEBUG(
					"network.lobby_keepalive from=%u host=%d",
					(unsigned)from_id,
					g_front_state.net_host_player_id ==
						from_id);
				if (control_value0 !=
					    SEQUENCE_NOTHING_TO_RESEND ||
				    expected_group_sequence !=
					    SEQUENCE_NOTHING_TO_RESEND ||
				    expected_direct_sequence !=
					    SEQUENCE_NOTHING_TO_RESEND) {
					XVT_LOG_WARN(
						"network.lobby_resend_unavailable from=%u broadcast=%d group=%d direct=%d",
						(unsigned)from_id,
						control_value0,
						expected_group_sequence,
						expected_direct_sequence);
				}
				/* Keepalives are unsequenced control packets. */
				continue;
			}

			peer_index = net_find_or_create_peer_slot((int)from_id);
			g_front_state
				.net_runtime_reliable_peer_slots[peer_index]
				.last_heard_ms = GetTickCount();

			if (broadcast_channel != 0 && group_channel != 0) {
				if (wire_packet.data[0] != 0) {
					group_channel =
						wire_packet.data[0] == 2;
					broadcast_channel = 0;
				} else {
					group_channel = 0;
					broadcast_channel = 1;
				}
				uint8_t *retransmission_payload =
					wire_packet.data + 1;
				if (has_length) {
					payload_size = (uint32_t)
						net_session_get_fixed_payload_size(
							(int)packet_type);
					if (payload_size == 0) {
						uint16_t encoded_size;
						memcpy(&encoded_size,
						       wire_packet.data + 1,
						       sizeof(encoded_size));
						payload_size = encoded_size;
						retransmission_payload =
							wire_packet.data + 1 +
							sizeof(uint16_t);
					}
				} else {
					payload_size =
						wire_size -
						sizeof(wire_packet.header);
				}
				if (payload_size > MAX_PAYLOAD_SIZE) {
					payload_size = MAX_PAYLOAD_SIZE;
				}

				*(uint32_t *)g_front_state
					 .net_runtime_recv_queue
						 [g_front_state
							  .net_runtime_recv_queue_write_index]
					 .payload = packet_type;
				memcpy(g_front_state.net_runtime_recv_queue
						       [g_front_state
								.net_runtime_recv_queue_write_index]
							       .payload +
					       sizeof(packet_type),
				       retransmission_payload, payload_size);
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.direct_play_id = from_id;
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.payload_size =
					payload_size + sizeof(packet_type);
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.last_nack_ms = 0;
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.nack_retry_count = 0;
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.is_resent_copy = 1;
				peer_index = net_find_or_create_peer_slot(
					(int)from_id);
				if (broadcast_channel != 0) {
					g_front_state
						.net_runtime_recv_queue
							[g_front_state
								 .net_runtime_recv_queue_write_index]
						.packet_class = 0;
					if (g_front_state.net_reliable_peer_slot_count >
						    peer_index &&
					    peer_index < PEER_CAPACITY) {
						unsigned int next_sequence =
							(unsigned int)g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.recv_seq_channel_a +
							1;
						if (next_sequence >
						    SEQUENCE_LIMIT) {
							next_sequence = 0;
						}
						if (next_sequence ==
						    (unsigned int)sequence) {
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.recv_seq_channel_a =
								(int)next_sequence;
						}
					}
				} else if (group_channel != 0) {
					g_front_state
						.net_runtime_recv_queue
							[g_front_state
								 .net_runtime_recv_queue_write_index]
						.packet_class = 2;
					if (g_front_state.net_reliable_peer_slot_count >
						    peer_index &&
					    peer_index < PEER_CAPACITY) {
						unsigned int next_sequence =
							(unsigned int)g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.recv_seq_channel_b +
							1;
						if (next_sequence >
						    SEQUENCE_LIMIT) {
							next_sequence = 0;
						}
						if (next_sequence ==
						    (unsigned int)sequence) {
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.recv_seq_channel_b =
								(int)next_sequence;
						}
					}
				} else {
					g_front_state
						.net_runtime_recv_queue
							[g_front_state
								 .net_runtime_recv_queue_write_index]
						.packet_class = 1;
					if (g_front_state.net_reliable_peer_slot_count >
						    peer_index &&
					    peer_index < PEER_CAPACITY) {
						unsigned int next_sequence =
							(unsigned int)g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.recv_seq_default +
							1;
						if (next_sequence >
						    SEQUENCE_LIMIT) {
							next_sequence = 0;
						}
						if (next_sequence ==
						    (unsigned int)sequence) {
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.recv_seq_default =
								(int)next_sequence;
						}
					}
				}
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.sequence_byte = (uint8_t)sequence;
				XVT_LOG_DEBUG(
					"network.lobby_resent_received from=%u type=%u channel=%d seq=%d bytes=%u",
					(unsigned)from_id, packet_type,
					broadcast_channel != 0 ? 0
					: group_channel != 0   ? 2
							       : 1,
					sequence, (unsigned)payload_size);
				++g_front_state
					  .net_runtime_recv_queue_write_index;
				++g_front_state.net_runtime_recv_queue_count;
				if (g_front_state
					    .net_runtime_recv_queue_write_index >=
				    QUEUE_CAPACITY) {
					g_front_state
						.net_runtime_recv_queue_write_index =
						0;
				}
				continue;
			}

			if (has_length) {
				int previous_sequence = sequence == 0
								? SEQUENCE_LIMIT
								: sequence - 1;
				if (net_check_and_record_incoming_sequence(
					    (int)from_id, previous_sequence,
					    broadcast_channel,
					    group_channel) == 0) {
					if ((unsigned int)g_front_state
						    .net_runtime_reliable_peer_slots
							    [peer_index]
						    .packet_count >
					    PACKET_DROP_WARMUP_COUNT) {
						++g_front_state
							  .net_runtime_reliable_peer_slots
								  [peer_index]
							  .packet_drop_count;
					}
					uint8_t *piggyback_payload =
						(uint8_t *)payload +
						payload_size;
					int previous_packet_type =
						*piggyback_payload++;
					XVT_LOG_DEBUG(
						"network.lobby_packet_missed from=%u seq=%d channel=%d trailer=%u",
						(unsigned)from_id,
						previous_sequence,
						broadcast_channel != 0 ? 0
						: group_channel != 0   ? 2
								       : 1,
						(unsigned)previous_packet_type);
					if (previous_packet_type !=
					    NET_PACKET_NOP) {
						unsigned int piggyback_size =
							wire_size -
							(uint16_t)(piggyback_payload -
								   (uint8_t *)&wire_packet
									   .header);
						if (piggyback_size >
						    MAX_PAYLOAD_SIZE) {
							piggyback_size =
								MAX_PAYLOAD_SIZE;
						}
						*(uint32_t *)g_front_state
							 .net_runtime_recv_queue
								 [g_front_state
									  .net_runtime_recv_queue_write_index]
							 .payload =
							previous_packet_type;
						memcpy(g_front_state.net_runtime_recv_queue
								       [g_front_state
										.net_runtime_recv_queue_write_index]
									       .payload +
							       sizeof(int),
						       piggyback_payload,
						       piggyback_size);
						g_front_state
							.net_runtime_recv_queue
								[g_front_state
									 .net_runtime_recv_queue_write_index]
							.direct_play_id =
							from_id;
						g_front_state
							.net_runtime_recv_queue
								[g_front_state
									 .net_runtime_recv_queue_write_index]
							.payload_size =
							piggyback_size +
							sizeof(int);
						g_front_state
							.net_runtime_recv_queue
								[g_front_state
									 .net_runtime_recv_queue_write_index]
							.last_nack_ms = 0;
						g_front_state
							.net_runtime_recv_queue
								[g_front_state
									 .net_runtime_recv_queue_write_index]
							.nack_retry_count = 0;
						g_front_state
							.net_runtime_recv_queue
								[g_front_state
									 .net_runtime_recv_queue_write_index]
							.is_resent_copy = 0;
						if (broadcast_channel != 0) {
							g_front_state
								.net_runtime_recv_queue
									[g_front_state
										 .net_runtime_recv_queue_write_index]
								.packet_class =
								0;
						} else if (group_channel != 0) {
							g_front_state
								.net_runtime_recv_queue
									[g_front_state
										 .net_runtime_recv_queue_write_index]
								.packet_class =
								2;
						} else {
							g_front_state
								.net_runtime_recv_queue
									[g_front_state
										 .net_runtime_recv_queue_write_index]
								.packet_class =
								1;
						}
						if (sequence == 0) {
							g_front_state
								.net_runtime_recv_queue
									[g_front_state
										 .net_runtime_recv_queue_write_index]
								.sequence_byte =
								SEQUENCE_LIMIT;
						} else {
							g_front_state
								.net_runtime_recv_queue
									[g_front_state
										 .net_runtime_recv_queue_write_index]
								.sequence_byte =
								(uint8_t)(sequence -
									  1);
						}
						++g_front_state
							  .net_runtime_recv_queue_write_index;
						++g_front_state
							  .net_runtime_recv_queue_count;
						if (g_front_state
							    .net_runtime_recv_queue_write_index >=
						    QUEUE_CAPACITY) {
							g_front_state
								.net_runtime_recv_queue_write_index =
								0;
						}
					}
				}
			}

			/* peer_index is reused here as a flag: 1 when the
			 * sequence is stale or out of range (skipped). */
			peer_index = net_check_and_record_incoming_sequence(
				(int)from_id, sequence, broadcast_channel,
				group_channel);
			if (peer_index != 0) {
				XVT_LOG_DEBUG(
					"network.lobby_packet_repeat from=%u type=%u seq=%d channel=%d",
					(unsigned)from_id, packet_type,
					sequence,
					broadcast_channel != 0 ? 0
					: group_channel != 0   ? 2
							       : 1);
				continue;
			}
			payload = (int *)wire_packet.data;
			if (has_length) {
				payload_size = (uint32_t)
					net_session_get_fixed_payload_size(
						(int)packet_type);
				if (payload_size == 0) {
					payload_size =
						*(const uint16_t *)
							 wire_packet.data;
					payload = (int *)(wire_packet.data +
							  sizeof(uint16_t));
				}
			} else {
				payload_size =
					wire_size - sizeof(wire_packet.header);
			}
			if (payload_size > MAX_PAYLOAD_SIZE) {
				payload_size = MAX_PAYLOAD_SIZE;
			}
			*(uint32_t *)g_front_state
				 .net_runtime_recv_queue
					 [g_front_state
						  .net_runtime_recv_queue_write_index]
				 .payload = packet_type;
			memcpy(g_front_state.net_runtime_recv_queue
					       [g_front_state
							.net_runtime_recv_queue_write_index]
						       .payload +
				       sizeof(packet_type),
			       payload, payload_size);
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.direct_play_id = from_id;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.payload_size =
				payload_size + sizeof(packet_type);
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.last_nack_ms = 0;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.nack_retry_count = 0;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.is_resent_copy = 0;
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.is_resent_copy = peer_index != 0;
			if (broadcast_channel != 0) {
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.packet_class = 0;
			} else if (group_channel != 0) {
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.packet_class = 2;
			} else {
				g_front_state
					.net_runtime_recv_queue
						[g_front_state
							 .net_runtime_recv_queue_write_index]
					.packet_class = 1;
			}
			g_front_state
				.net_runtime_recv_queue
					[g_front_state
						 .net_runtime_recv_queue_write_index]
				.sequence_byte = (uint8_t)sequence;
			++g_front_state.net_runtime_recv_queue_write_index;
			++g_front_state.net_runtime_recv_queue_count;
			XVT_LOG_DEBUG(
				"network.lobby_packet_received from=%u type=%u seq=%d channel=%d bytes=%u queued=%d",
				(unsigned)from_id, packet_type, sequence,
				broadcast_channel != 0 ? 0
				: group_channel != 0   ? 2
						       : 1,
				(unsigned)payload_size,
				g_front_state.net_runtime_recv_queue_count);
			if (g_front_state.net_runtime_recv_queue_write_index >=
			    QUEUE_CAPACITY) {
				g_front_state
					.net_runtime_recv_queue_write_index = 0;
			}
		}
	}
	if (back_buffer_locked != 0) {
		g_draw_surface_ptr = frontend_display_lock_back_buffer();
	}
}
