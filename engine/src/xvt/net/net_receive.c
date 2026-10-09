#include "xvt/net/net_receive.h"

#include <stdio.h>
#include <string.h>

#include "xvt/frontend/frontend_state.h"
#include "xvt/net/net.h"
#include "xvt/net/net_peers.h"
#include "xvt/net/net_pump.h"
#include "xvt/net/net_send.h"
#include "xvt/util/time.h"
#include "xvt_runtime/log/log.h"

/* Hands out the next lobby packet in sequence order, or NULL. It first pumps
 * incoming packets and sends due keepalives. A DirectPlay system message is
 * returned only from the head of the queue. Packets of types below 51
 * (flight types) are dropped, their sequence counted as delivered. A packet
 * that is next on its channel is delivered. One that leaves a gap asks its
 * sender for the missing ones with a NACK, then again each second up to 20
 * more times (in the long-timeout mode it waits 20 seconds and asks no
 * more), and then skips the gap, delivering the first queued packet after
 * it; a resent copy that arrives in time fills the gap. Per call it stops
 * looking at a peer's packets once more than 90 of them, less the size of
 * each gap found, have been seen. Stale packets go when they reach the head
 * of the queue; packets from a sender that gets no peer slot (the table is
 * full) go at once. When 1023 or more stay queued, stale ones are dropped
 * and the first one already asked about is delivered past its gap. It
 * returns g_front_state.net_runtime_recv_scratch_packet.payload, valid until the
 * next call, with the sender and size in the out arguments. Updates the
 * peer slots' delivered sequences, counts and times. */
// FUNCTION: XVT 0x4D0540
void *net_dequeue_incoming_packet(DPID *out_sender_id,
				  uint32_t *out_packet_size)
{
	enum {
		NET_RECV_QUEUE_CAPACITY = 1024,
		NET_RECV_QUEUE_PRESSURE_THRESHOLD = NET_RECV_QUEUE_CAPACITY - 1,
		NET_RELIABLE_PEER_CAPACITY = 40,
		NET_RELIABLE_CHANNEL_COUNT = 3,
		NET_RELIABLE_CHANNEL_A = 0,
		NET_RELIABLE_CHANNEL_DEFAULT = 1,
		NET_RELIABLE_CHANNEL_B = 2,
		NET_RELIABLE_SEQUENCE_LIMIT = 127,
		NET_RELIABLE_SEQUENCE_COUNT = NET_RELIABLE_SEQUENCE_LIMIT + 1,
		NET_FIRST_NON_FLIGHT_PACKET_TYPE = NET_PACKET_WORLD_NACK,
		NET_RELIABLE_REORDER_ALLOWANCE = 90,
		NET_RELIABLE_NEGATIVE_WINDOW = -28,
		NET_RELIABLE_PRESSURE_NEGATIVE_WINDOW = -27,
		NET_RELIABLE_POSITIVE_WINDOW = 100,
		NET_RELIABLE_RETRY_PACKET_WORD_COUNT = 3,
		NET_RELIABLE_RETRY_COUNT_THRESHOLD = 20,
		NET_RELIABLE_SHORT_RETRY_LIMIT = 20,
		NET_RELIABLE_SHORT_RETRY_TIMEOUT_MS = 1000,
		NET_RELIABLE_LONG_RETRY_LIMIT = 0,
		NET_RELIABLE_LONG_RETRY_TIMEOUT_MS = 20000
	};

	net_pump_incoming_packets();
	net_send_sequence_keepalives();
	if (g_front_state.net_runtime_recv_queue_count == 0) {
		return NULL;
	}

	uint8_t processed_packet_counts[NET_RELIABLE_PEER_CAPACITY];
	memset(processed_packet_counts, 0, sizeof(processed_packet_counts));
	uint8_t reorder_allowances[NET_RELIABLE_PEER_CAPACITY];
	memset(reorder_allowances, NET_RELIABLE_REORDER_ALLOWANCE,
	       sizeof(reorder_allowances));
	uint8_t last_seen_sequences[NET_RELIABLE_PEER_CAPACITY]
				   [NET_RELIABLE_CHANNEL_COUNT];
	memset(last_seen_sequences, 0, sizeof(last_seen_sequences));
	int channel_index;
	/* channel_index first walks the peer slots; the test below compares the
	 * slot count it is left at with peer_index to spot a new slot. Only
	 * later does it hold a channel. */
	for (channel_index = 0;
	     channel_index < (int)g_front_state.net_reliable_peer_slot_count;
	     ++channel_index) {
		last_seen_sequences[channel_index][NET_RELIABLE_CHANNEL_A] =
			(uint8_t)g_front_state
				.net_runtime_reliable_peer_slots[channel_index]
				.last_delivered_seq_channel_a;
		last_seen_sequences
			[channel_index][NET_RELIABLE_CHANNEL_DEFAULT] =
				(uint8_t)g_front_state
					.net_runtime_reliable_peer_slots
						[channel_index]
					.last_delivered_seq_default;
		last_seen_sequences[channel_index][NET_RELIABLE_CHANNEL_B] =
			(uint8_t)g_front_state
				.net_runtime_reliable_peer_slots[channel_index]
				.last_delivered_seq_channel_b;
	}

	int scan_index = g_front_state.net_runtime_recv_queue_read_index;
	int remaining_packet_count = g_front_state.net_runtime_recv_queue_count;
	int received_sequence;
	int expected_sequence = 0;
	int use_channel_a;
	int use_channel_b;
	char debug_text[256];
	uint32_t retry_packet[128];
	unsigned int peer_index;
	uint32_t retry_timeout_ms;
	int sequence_delta;
	for (; remaining_packet_count > 0; --remaining_packet_count) {
		if (g_front_state.net_runtime_recv_queue[scan_index]
			    .direct_play_id == 0) {
			if (g_front_state.net_runtime_recv_queue_read_index !=
			    scan_index) {
				if (++scan_index >= NET_RECV_QUEUE_CAPACITY) {
					scan_index = 0;
				}
				continue;
			}
			{
				g_front_state.net_runtime_recv_scratch_packet =
					g_front_state.net_runtime_recv_queue
						[scan_index];
				*out_sender_id = g_front_state
							 .net_runtime_recv_queue
								 [scan_index]
							 .direct_play_id;
				*out_packet_size =
					g_front_state
						.net_runtime_recv_queue
							[scan_index]
						.payload_size;
				--g_front_state.net_runtime_recv_queue_count;
				++g_front_state
					  .net_runtime_recv_queue_read_index;
				if (g_front_state
					    .net_runtime_recv_queue_read_index >=
				    NET_RECV_QUEUE_CAPACITY) {
					g_front_state
						.net_runtime_recv_queue_read_index =
						0;
				}
				return g_front_state
					.net_runtime_recv_scratch_packet
					.payload;
			}
		} else {
			received_sequence =
				g_front_state.net_runtime_recv_queue[scan_index]
					.sequence_byte;
			use_channel_a =
				g_front_state.net_runtime_recv_queue[scan_index]
					.packet_class == NET_RELIABLE_CHANNEL_A;
			use_channel_b =
				g_front_state.net_runtime_recv_queue[scan_index]
					.packet_class == NET_RELIABLE_CHANNEL_B;
			int packet_type =
				*(const int *)g_front_state
					 .net_runtime_recv_queue[scan_index]
					 .payload;
			peer_index = net_find_or_create_peer_slot(
				(int)g_front_state
					.net_runtime_recv_queue[scan_index]
					.direct_play_id);
			if (channel_index == (int)peer_index &&
			    peer_index < NET_RELIABLE_PEER_CAPACITY) {
				last_seen_sequences
					[peer_index][NET_RELIABLE_CHANNEL_A] =
						NET_RELIABLE_SEQUENCE_LIMIT;
				last_seen_sequences
					[peer_index]
					[NET_RELIABLE_CHANNEL_DEFAULT] =
						NET_RELIABLE_SEQUENCE_LIMIT;
				last_seen_sequences
					[peer_index][NET_RELIABLE_CHANNEL_B] =
						NET_RELIABLE_SEQUENCE_LIMIT;
			}

			if (g_front_state.net_reliable_peer_slot_count >
			    peer_index) {
				if ((unsigned int)packet_type <
				    NET_FIRST_NON_FLIGHT_PACKET_TYPE) {
					if (g_front_state
						    .net_runtime_recv_queue
							    [scan_index]
						    .is_resent_copy == 0) {
						if (use_channel_a != 0) {
							last_seen_sequences
								[peer_index]
								[NET_RELIABLE_CHANNEL_A] =
									(uint8_t)
										received_sequence;
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.last_delivered_seq_channel_a =
								received_sequence;
						} else if (use_channel_b != 0) {
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.last_delivered_seq_channel_b =
								received_sequence;
							last_seen_sequences
								[peer_index]
								[NET_RELIABLE_CHANNEL_B] =
									(uint8_t)
										received_sequence;
						} else {
							g_front_state
								.net_runtime_reliable_peer_slots
									[peer_index]
								.last_delivered_seq_default =
								received_sequence;
							last_seen_sequences
								[peer_index]
								[NET_RELIABLE_CHANNEL_DEFAULT] =
									(uint8_t)
										received_sequence;
						}
					}
					XVT_LOG_DEBUG(
						"network.lobby_flight_packet_dropped player=%u channel=%u seq=%d type=%d resent=%u",
						(unsigned)g_front_state
							.net_runtime_recv_queue
								[scan_index]
							.direct_play_id,
						(unsigned)g_front_state
							.net_runtime_recv_queue
								[scan_index]
							.packet_class,
						received_sequence, packet_type,
						(unsigned)g_front_state
							.net_runtime_recv_queue
								[scan_index]
							.is_resent_copy);
					if (net_remove_incoming_packet_at_index(
						    (unsigned int)scan_index) ==
					    0) {
						continue;
					}
					if (++scan_index >=
					    NET_RECV_QUEUE_CAPACITY) {
						scan_index = 0;
					}
					continue;
				}
				if (reorder_allowances[peer_index] <
				    processed_packet_counts[peer_index]) {
					XVT_LOG_DEBUG(
						"network.lobby_peer_deferred player=%u seen=%u allowance=%u",
						(unsigned)g_front_state
							.net_runtime_recv_queue
								[scan_index]
							.direct_play_id,
						(unsigned)
							processed_packet_counts
								[peer_index],
						(unsigned)reorder_allowances
							[peer_index]);
					if (++scan_index >=
					    NET_RECV_QUEUE_CAPACITY) {
						scan_index = 0;
					}
					continue;
				}
				g_front_state
					.net_runtime_reliable_peer_slots
						[peer_index]
					.last_heard_ms = GetTickCount();
				if (g_front_state
					    .net_runtime_recv_queue[scan_index]
					    .is_resent_copy == 0) {
					++processed_packet_counts[peer_index];
				}

				if (use_channel_a != 0) {
					expected_sequence =
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_delivered_seq_channel_a +
						1;
					if (expected_sequence >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						expected_sequence = 0;
					}
				} else if (use_channel_b != 0) {
					expected_sequence =
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_delivered_seq_channel_b +
						1;
					if (expected_sequence >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						expected_sequence = 0;
					}
				} else {
					expected_sequence =
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_delivered_seq_default +
						1;
					if (expected_sequence >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						expected_sequence = 0;
					}
				}
			}
			sequence_delta = received_sequence - expected_sequence;
			if (g_front_state.net_reliable_peer_slot_count >
				    peer_index &&
			    (sequence_delta < NET_RELIABLE_NEGATIVE_WINDOW ||
			     (sequence_delta >= 0 &&
			      sequence_delta < NET_RELIABLE_POSITIVE_WINDOW))) {
				if (expected_sequence == received_sequence) {
					g_front_state
						.net_runtime_reliable_peer_slots
							[peer_index]
						.last_activity_ms =
						GetTickCount();
					if (use_channel_a != 0) {
						sprintf(debug_text, "(RB %u) ",
							received_sequence);
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_delivered_seq_channel_a =
							received_sequence;
					} else if (use_channel_b != 0) {
						sprintf(debug_text, "(RG %u) ",
							received_sequence);
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_delivered_seq_channel_b =
							received_sequence;
					} else {
						sprintf(debug_text, "(RS %u) ",
							received_sequence);
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_delivered_seq_default =
							received_sequence;
					}
					++g_front_state
						  .net_runtime_reliable_peer_slots
							  [peer_index]
						  .packet_count;
					g_front_state
						.net_runtime_recv_scratch_packet =
						g_front_state
							.net_runtime_recv_queue
								[scan_index];
					net_remove_incoming_packet_at_index(
						(unsigned int)scan_index);
					*out_sender_id =
						g_front_state
							.net_runtime_recv_scratch_packet
							.direct_play_id;
					*out_packet_size =
						g_front_state
							.net_runtime_recv_scratch_packet
							.payload_size;
					XVT_LOG_DEBUG(
						"network.lobby_delivered player=%u peer=%u channel=%u seq=%u type=%d bytes=%u queued=%d path=\"in_order\"",
						(unsigned)g_front_state
							.net_runtime_recv_scratch_packet
							.direct_play_id,
						peer_index,
						(unsigned)g_front_state
							.net_runtime_recv_scratch_packet
							.packet_class,
						(unsigned)g_front_state
							.net_runtime_recv_scratch_packet
							.sequence_byte,
						packet_type,
						(unsigned)g_front_state
							.net_runtime_recv_scratch_packet
							.payload_size,
						g_front_state
							.net_runtime_recv_queue_count);
					return g_front_state
						.net_runtime_recv_scratch_packet
						.payload;
				}
				if (g_front_state
					    .net_runtime_recv_queue[scan_index]
					    .is_resent_copy == 0) {
					if (use_channel_a != 0) {
						channel_index =
							NET_RELIABLE_CHANNEL_A;
					} else if (use_channel_b != 0) {
						channel_index =
							NET_RELIABLE_CHANNEL_B;
					} else {
						channel_index =
							NET_RELIABLE_CHANNEL_DEFAULT;
					}
					int sequence_cursor =
						(int)last_seen_sequences
							[peer_index]
							[channel_index] +
						1;
					last_seen_sequences
						[peer_index]
						[channel_index] = (uint8_t)
							received_sequence;
					if (sequence_cursor >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						sequence_cursor = 0;
					}
					int missing_packet_count =
						received_sequence -
						(int)sequence_cursor;
					if (missing_packet_count < 0) {
						missing_packet_count +=
							NET_RELIABLE_SEQUENCE_COUNT;
					}
					if (reorder_allowances[peer_index] >=
					    missing_packet_count) {
						reorder_allowances
							[peer_index] -=
							(uint8_t)
								missing_packet_count;
					} else {
						reorder_allowances[peer_index] =
							0;
					}
					int sent_retry_request = 0;
					int first_missing_sequence =
						sequence_cursor;
					while (sequence_cursor !=
					       received_sequence) {
						int queued_packet_index =
							net_find_queued_sequenced_packet(
								scan_index,
								(int)sequence_cursor,
								use_channel_a,
								use_channel_b,
								(int)peer_index);
						if (queued_packet_index <
							    NET_RECV_QUEUE_CAPACITY &&
						    queued_packet_index >= 0) {
							if (expected_sequence ==
							    (int)sequence_cursor) {
								g_front_state
									.net_runtime_reliable_peer_slots
										[peer_index]
									.last_activity_ms =
									GetTickCount();
								g_front_state
									.net_runtime_recv_scratch_packet =
									g_front_state
										.net_runtime_recv_queue
											[queued_packet_index];
								if (use_channel_a !=
								    0) {
									sprintf(debug_text,
										"(ROOB %u) ",
										expected_sequence);
									g_front_state
										.net_runtime_reliable_peer_slots
											[peer_index]
										.last_delivered_seq_channel_a =
										expected_sequence;
								} else if (
									use_channel_b !=
									0) {
									sprintf(debug_text,
										"(ROOG %u) ",
										expected_sequence);
									g_front_state
										.net_runtime_reliable_peer_slots
											[peer_index]
										.last_delivered_seq_channel_b =
										expected_sequence;
								} else {
									sprintf(debug_text,
										"(ROOS %u) ",
										expected_sequence);
									g_front_state
										.net_runtime_reliable_peer_slots
											[peer_index]
										.last_delivered_seq_default =
										expected_sequence;
								}
								++g_front_state
									  .net_runtime_reliable_peer_slots
										  [peer_index]
									  .packet_count;
								*out_sender_id =
									g_front_state
										.net_runtime_recv_scratch_packet
										.direct_play_id;
								*out_packet_size =
									g_front_state
										.net_runtime_recv_scratch_packet
										.payload_size;
								if (missing_packet_count <=
								    1) {
									g_front_state
										.net_runtime_recv_queue
											[scan_index]
										.nack_retry_count =
										0;
									g_front_state
										.net_runtime_recv_queue
											[scan_index]
										.last_nack_ms =
										0;
								}
								net_remove_incoming_packet_at_index(
									queued_packet_index);
								XVT_LOG_DEBUG(
									"network.lobby_delivered player=%u peer=%u channel=%u seq=%u type=%d bytes=%u queued=%d path=\"gap_filled\"",
									(unsigned)g_front_state
										.net_runtime_recv_scratch_packet
										.direct_play_id,
									peer_index,
									(unsigned)g_front_state
										.net_runtime_recv_scratch_packet
										.packet_class,
									(unsigned)g_front_state
										.net_runtime_recv_scratch_packet
										.sequence_byte,
									*(const int
										  *)g_front_state
										 .net_runtime_recv_scratch_packet
										 .payload,
									(unsigned)g_front_state
										.net_runtime_recv_scratch_packet
										.payload_size,
									g_front_state
										.net_runtime_recv_queue_count);
								return g_front_state
									.net_runtime_recv_scratch_packet
									.payload;
							}
							--missing_packet_count;
						} else if (
							g_front_state
								.net_runtime_recv_queue
									[scan_index]
								.nack_retry_count ==
							0) {
							sprintf(debug_text,
								"(RP %d) ",
								sequence_cursor);
							if ((unsigned int)g_front_state
								    .net_runtime_reliable_peer_slots
									    [peer_index]
								    .packet_count >
							    NET_RELIABLE_RETRY_COUNT_THRESHOLD) {
								++g_front_state
									  .net_runtime_reliable_peer_slots
										  [peer_index]
									  .packet_retry_count;
							}
							retry_packet[1] =
								sequence_cursor;
							retry_packet[0] =
								NET_PACKET_NACK;
							retry_packet[2] =
								use_channel_a
									? NET_RELIABLE_CHANNEL_A
									: (use_channel_b
										   ? NET_RELIABLE_CHANNEL_B
										   : NET_RELIABLE_CHANNEL_DEFAULT);
							net_send_direct_play_packet(
								(int)g_front_state
									.net_runtime_recv_queue
										[scan_index]
									.direct_play_id,
								retry_packet,
								NET_RELIABLE_RETRY_PACKET_WORD_COUNT *
									sizeof(retry_packet
										       [0]),
								1);
							XVT_LOG_DEBUG(
								"network.lobby_nack_sent player=%u channel=%u seq=%d retries=%u gaps=%d",
								(unsigned)g_front_state
									.net_runtime_recv_queue
										[scan_index]
									.direct_play_id,
								(unsigned)retry_packet
									[2],
								sequence_cursor,
								(unsigned)g_front_state
									.net_runtime_recv_queue
										[scan_index]
									.nack_retry_count,
								g_front_state
									.net_runtime_reliable_peer_slots
										[peer_index]
									.packet_retry_count);
							sent_retry_request = 1;
						} else {
							uint32_t now =
								GetTickCount();
							/* queued_packet_index
							 * is reused here as the
							 * NACK retry limit. */
							if (g_front_state
								    .net_reliable_retry_long_timeout_mode ==
							    1) {
								queued_packet_index =
									NET_RELIABLE_LONG_RETRY_LIMIT;
								retry_timeout_ms =
									NET_RELIABLE_LONG_RETRY_TIMEOUT_MS;
							} else {
								queued_packet_index =
									NET_RELIABLE_SHORT_RETRY_LIMIT;
								retry_timeout_ms =
									NET_RELIABLE_SHORT_RETRY_TIMEOUT_MS;
							}
							if (now - (uint32_t)g_front_state
									    .net_runtime_recv_queue
										    [scan_index]
									    .last_nack_ms >
							    retry_timeout_ms) {
								if (g_front_state
									    .net_runtime_recv_queue
										    [scan_index]
									    .nack_retry_count <=
								    (unsigned int)
									    queued_packet_index) {
									sprintf(debug_text,
										"(RP %d) ",
										sequence_cursor);
									retry_packet
										[1] = sequence_cursor;
									retry_packet
										[0] = NET_PACKET_NACK;
									retry_packet[2] =
										use_channel_a
											? NET_RELIABLE_CHANNEL_A
											: (use_channel_b
												   ? NET_RELIABLE_CHANNEL_B
												   : NET_RELIABLE_CHANNEL_DEFAULT);
									net_send_direct_play_packet(
										(int)g_front_state
											.net_runtime_recv_queue
												[scan_index]
											.direct_play_id,
										retry_packet,
										NET_RELIABLE_RETRY_PACKET_WORD_COUNT *
											sizeof(retry_packet
												       [0]),
										1);
									XVT_LOG_DEBUG(
										"network.lobby_nack_sent player=%u channel=%u seq=%d retries=%u gaps=%d",
										(unsigned)g_front_state
											.net_runtime_recv_queue
												[scan_index]
											.direct_play_id,
										(unsigned)retry_packet
											[2],
										sequence_cursor,
										(unsigned)g_front_state
											.net_runtime_recv_queue
												[scan_index]
											.nack_retry_count,
										g_front_state
											.net_runtime_reliable_peer_slots
												[peer_index]
											.packet_retry_count);
									sent_retry_request =
										1;
								} else {
									XVT_LOG_WARN(
										"network.lobby_nack_gave_up player=%u channel=%u first=%d retries=%u",
										(unsigned)g_front_state
											.net_runtime_recv_queue
												[scan_index]
											.direct_play_id,
										(unsigned)g_front_state
											.net_runtime_recv_queue
												[scan_index]
											.packet_class,
										first_missing_sequence,
										(unsigned)g_front_state
											.net_runtime_recv_queue
												[scan_index]
											.nack_retry_count);
									sequence_cursor =
										first_missing_sequence;
									g_front_state
										.net_runtime_reliable_peer_slots
											[peer_index]
										.last_activity_ms =
										GetTickCount();
									for (;
									     ;) {
										queued_packet_index = net_find_queued_sequenced_packet(
											scan_index,
											(int)sequence_cursor,
											use_channel_a,
											use_channel_b,
											(int)peer_index);
										if (queued_packet_index >=
											    0 &&
										    queued_packet_index <=
											    NET_RECV_QUEUE_CAPACITY) {
											g_front_state
												.net_runtime_recv_scratch_packet =
												g_front_state
													.net_runtime_recv_queue
														[queued_packet_index];
											net_remove_incoming_packet_at_index(
												queued_packet_index);
											if (use_channel_a !=
											    0) {
												sprintf(debug_text,
													"(ROOB %u) ",
													expected_sequence);
												g_front_state
													.net_runtime_reliable_peer_slots
														[peer_index]
													.last_delivered_seq_channel_a =
													(int)sequence_cursor;
											} else if (
												use_channel_b !=
												0) {
												sprintf(debug_text,
													"(ROOG %u) ",
													expected_sequence);
												g_front_state
													.net_runtime_reliable_peer_slots
														[peer_index]
													.last_delivered_seq_channel_b =
													(int)sequence_cursor;
											} else {
												sprintf(debug_text,
													"(ROOS %u) ",
													expected_sequence);
												g_front_state
													.net_runtime_reliable_peer_slots
														[peer_index]
													.last_delivered_seq_default =
													(int)sequence_cursor;
											}
											++g_front_state
												  .net_runtime_reliable_peer_slots
													  [peer_index]
												  .packet_count;
											*out_sender_id =
												g_front_state
													.net_runtime_recv_scratch_packet
													.direct_play_id;
											*out_packet_size =
												g_front_state
													.net_runtime_recv_scratch_packet
													.payload_size;
											XVT_LOG_DEBUG(
												"network.lobby_delivered player=%u peer=%u channel=%u seq=%u type=%d bytes=%u queued=%d path=\"past_gap\"",
												(unsigned)g_front_state
													.net_runtime_recv_scratch_packet
													.direct_play_id,
												peer_index,
												(unsigned)g_front_state
													.net_runtime_recv_scratch_packet
													.packet_class,
												(unsigned)g_front_state
													.net_runtime_recv_scratch_packet
													.sequence_byte,
												*(const int
													  *)g_front_state
													 .net_runtime_recv_scratch_packet
													 .payload,
												(unsigned)g_front_state
													.net_runtime_recv_scratch_packet
													.payload_size,
												g_front_state
													.net_runtime_recv_queue_count);
											return g_front_state
												.net_runtime_recv_scratch_packet
												.payload;
										}
										if (received_sequence ==
										    (int)sequence_cursor) {
											if (use_channel_a !=
											    0) {
												sprintf(debug_text,
													"(ROOB %u) ",
													received_sequence);
												g_front_state
													.net_runtime_reliable_peer_slots
														[peer_index]
													.last_delivered_seq_channel_a =
													received_sequence;
											} else if (
												use_channel_b !=
												0) {
												sprintf(debug_text,
													"(ROOG %u) ",
													received_sequence);
												g_front_state
													.net_runtime_reliable_peer_slots
														[peer_index]
													.last_delivered_seq_channel_b =
													received_sequence;
											} else {
												sprintf(debug_text,
													"(ROOS %u) ",
													received_sequence);
												g_front_state
													.net_runtime_reliable_peer_slots
														[peer_index]
													.last_delivered_seq_default =
													received_sequence;
											}
											++g_front_state
												  .net_runtime_reliable_peer_slots
													  [peer_index]
												  .packet_count;
											g_front_state
												.net_runtime_recv_scratch_packet =
												g_front_state
													.net_runtime_recv_queue
														[scan_index];
											net_remove_incoming_packet_at_index(
												(unsigned int)
													scan_index);
											*out_sender_id =
												g_front_state
													.net_runtime_recv_scratch_packet
													.direct_play_id;
											*out_packet_size =
												g_front_state
													.net_runtime_recv_scratch_packet
													.payload_size;
											XVT_LOG_DEBUG(
												"network.lobby_delivered player=%u peer=%u channel=%u seq=%u type=%d bytes=%u queued=%d path=\"past_gap\"",
												(unsigned)g_front_state
													.net_runtime_recv_scratch_packet
													.direct_play_id,
												peer_index,
												(unsigned)g_front_state
													.net_runtime_recv_scratch_packet
													.packet_class,
												(unsigned)g_front_state
													.net_runtime_recv_scratch_packet
													.sequence_byte,
												packet_type,
												(unsigned)g_front_state
													.net_runtime_recv_scratch_packet
													.payload_size,
												g_front_state
													.net_runtime_recv_queue_count);
											return g_front_state
												.net_runtime_recv_scratch_packet
												.payload;
										}
										++sequence_cursor;
										if (sequence_cursor >
										    NET_RELIABLE_SEQUENCE_LIMIT) {
											sequence_cursor =
												0;
										}
									}
								}
							}
						}
						++sequence_cursor;
						if (sequence_cursor >
						    NET_RELIABLE_SEQUENCE_LIMIT) {
							sequence_cursor = 0;
						}
					}

					if (missing_packet_count <= 0) {
						g_front_state
							.net_runtime_recv_queue
								[scan_index]
							.nack_retry_count = 0;
						g_front_state
							.net_runtime_recv_queue
								[scan_index]
							.last_nack_ms = 0;
					} else {
						if (sent_retry_request == 1) {
							++g_front_state
								  .net_runtime_recv_queue
									  [scan_index]
								  .nack_retry_count;
							g_front_state
								.net_runtime_recv_queue
									[scan_index]
								.last_nack_ms =
								(int)GetTickCount();
						}
					}
				} else {
					if (g_front_state
						    .net_runtime_recv_queue_read_index ==
					    scan_index) {
						XVT_LOG_DEBUG(
							"network.lobby_packet_dropped player=%u channel=%u seq=%d expected=%d reason=\"resent_early\"",
							(unsigned)g_front_state
								.net_runtime_recv_queue
									[scan_index]
								.direct_play_id,
							(unsigned)g_front_state
								.net_runtime_recv_queue
									[scan_index]
								.packet_class,
							received_sequence,
							expected_sequence);
						if (net_remove_incoming_packet_at_index(
							    (unsigned int)
								    scan_index) ==
						    0) {
							continue;
						}
					}
				}
			} else {
				XVT_LOG_DEBUG(
					"network.lobby_packet_dropped player=%u channel=%u seq=%d expected=%d reason=\"%s\"",
					(unsigned)g_front_state
						.net_runtime_recv_queue
							[scan_index]
						.direct_play_id,
					(unsigned)g_front_state
						.net_runtime_recv_queue
							[scan_index]
						.packet_class,
					received_sequence, expected_sequence,
					g_front_state.net_reliable_peer_slot_count >
							peer_index
						? "stale"
						: "no_peer");
				if (net_remove_incoming_packet_at_index(
					    (unsigned int)scan_index) == 0) {
					continue;
				}
			}
		}

		if (++scan_index >= NET_RECV_QUEUE_CAPACITY) {
			scan_index = 0;
		}
	}

	if (g_front_state.net_runtime_recv_queue_count <
	    NET_RECV_QUEUE_PRESSURE_THRESHOLD) {
		XVT_LOG_DEBUG(
			"network.lobby_receive_waiting queued=%d pass=\"normal\"",
			g_front_state.net_runtime_recv_queue_count);
		return NULL;
	}
	XVT_LOG_WARN("network.lobby_queue_pressure queued=%d",
		     g_front_state.net_runtime_recv_queue_count);
	scan_index = g_front_state.net_runtime_recv_queue_read_index;
	remaining_packet_count = g_front_state.net_runtime_recv_queue_count;
	for (; remaining_packet_count > 0; --remaining_packet_count) {
		if (g_front_state.net_runtime_recv_queue[scan_index]
			    .direct_play_id != 0) {
			received_sequence =
				g_front_state.net_runtime_recv_queue[scan_index]
					.sequence_byte;
			use_channel_a =
				g_front_state.net_runtime_recv_queue[scan_index]
					.packet_class == NET_RELIABLE_CHANNEL_A;
			use_channel_b =
				g_front_state.net_runtime_recv_queue[scan_index]
					.packet_class == NET_RELIABLE_CHANNEL_B;
			peer_index = net_find_or_create_peer_slot(
				(int)g_front_state
					.net_runtime_recv_queue[scan_index]
					.direct_play_id);
			expected_sequence = 0;
			if (peer_index <
				    g_front_state
					    .net_reliable_peer_slot_count &&
			    peer_index < NET_RELIABLE_PEER_CAPACITY) {
				if (use_channel_a != 0) {
					expected_sequence =
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_delivered_seq_channel_a +
						1;
					if (expected_sequence >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						expected_sequence = 0;
					}
				} else if (use_channel_b != 0) {
					expected_sequence =
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_delivered_seq_channel_b +
						1;
					if (expected_sequence >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						expected_sequence = 0;
					}
				} else {
					expected_sequence =
						g_front_state
							.net_runtime_reliable_peer_slots
								[peer_index]
							.last_delivered_seq_default +
						1;
					if (expected_sequence >
					    NET_RELIABLE_SEQUENCE_LIMIT) {
						expected_sequence = 0;
					}
				}
			}
			sequence_delta = received_sequence - expected_sequence;
			if (peer_index >=
				    g_front_state
					    .net_reliable_peer_slot_count ||
			    (sequence_delta >=
				     NET_RELIABLE_PRESSURE_NEGATIVE_WINDOW &&
			     (sequence_delta < 0 ||
			      sequence_delta >=
				      NET_RELIABLE_POSITIVE_WINDOW))) {
				XVT_LOG_DEBUG(
					"network.lobby_packet_dropped player=%u channel=%u seq=%d expected=%d reason=\"%s\"",
					(unsigned)g_front_state
						.net_runtime_recv_queue
							[scan_index]
						.direct_play_id,
					(unsigned)g_front_state
						.net_runtime_recv_queue
							[scan_index]
						.packet_class,
					received_sequence, expected_sequence,
					peer_index >= g_front_state
								.net_reliable_peer_slot_count
						? "no_peer"
						: "stale");
				if (net_remove_incoming_packet_at_index(
					    (unsigned int)scan_index) == 0) {
					continue;
				}
			} else if (g_front_state
					   .net_runtime_recv_queue[scan_index]
					   .nack_retry_count != 0) {
				if (use_channel_a != 0) {
					sprintf(debug_text, "(ROOB %u) ",
						received_sequence);
					g_front_state
						.net_runtime_reliable_peer_slots
							[peer_index]
						.last_delivered_seq_channel_a =
						received_sequence;
				} else if (use_channel_b != 0) {
					sprintf(debug_text, "(ROOG %u) ",
						received_sequence);
					g_front_state
						.net_runtime_reliable_peer_slots
							[peer_index]
						.last_delivered_seq_channel_b =
						received_sequence;
				} else {
					sprintf(debug_text, "(ROOS %u) ",
						received_sequence);
					g_front_state
						.net_runtime_reliable_peer_slots
							[peer_index]
						.last_delivered_seq_default =
						received_sequence;
				}
				++g_front_state
					  .net_runtime_reliable_peer_slots
						  [peer_index]
					  .packet_count;
				g_front_state.net_runtime_recv_scratch_packet =
					g_front_state.net_runtime_recv_queue
						[scan_index];
				net_remove_incoming_packet_at_index(
					(unsigned int)scan_index);
				*out_sender_id =
					g_front_state
						.net_runtime_recv_scratch_packet
						.direct_play_id;
				*out_packet_size =
					g_front_state
						.net_runtime_recv_scratch_packet
						.payload_size;
				XVT_LOG_DEBUG(
					"network.lobby_delivered player=%u peer=%u channel=%u seq=%u type=%d bytes=%u queued=%d path=\"queue_full\"",
					(unsigned)g_front_state
						.net_runtime_recv_scratch_packet
						.direct_play_id,
					peer_index,
					(unsigned)g_front_state
						.net_runtime_recv_scratch_packet
						.packet_class,
					(unsigned)g_front_state
						.net_runtime_recv_scratch_packet
						.sequence_byte,
					*(const int *)g_front_state
						 .net_runtime_recv_scratch_packet
						 .payload,
					(unsigned)g_front_state
						.net_runtime_recv_scratch_packet
						.payload_size,
					g_front_state
						.net_runtime_recv_queue_count);
				return g_front_state
					.net_runtime_recv_scratch_packet
					.payload;
			}
		}
		if (++scan_index >= NET_RECV_QUEUE_CAPACITY) {
			scan_index = 0;
		}
	}
	XVT_LOG_DEBUG(
		"network.lobby_receive_waiting queued=%d pass=\"pressure\"",
		g_front_state.net_runtime_recv_queue_count);
	return NULL;
}
