/* Checks network125 flight networking (xvt_runtime/runtime/flight_network.h) against the promises in its
 * header, for every part that holds without a network peer: the mission cookie and its counter, the
 * single-player paths of Options, Start and the roster exchange and the exchange's timeout, the recovery
 * request, PlayerAbort's result, the packet budget, AdmitInput's refusals, InsertWorld, what Receive
 * consumes and where it puts it, DecodeControl, NextWakeDelayUs, ShouldSend, and SendWorld on a host flying
 * alone. No game data is read: the test sets the player records, the input histories, the network session's
 * ids and the flight network globals itself, and drives the host clock. Player 0 is the local player; the
 * roster gives player n the network id 100 + n, and the host's id is 500. Every check starts from that
 * world with no mission cookie, empty queues and histories, and the host clock at one second.
 *
 * Nothing here sends a packet: without a network session the game's send path loops packets back into its
 * own receive queue. So these need a second machine and are not checked: the multiplayer exchanges of
 * Session, Options and Start, FlushInput and FlushWorld to remote players, peer timeouts in SendWorld, and
 * that a player PlayerAbort excludes leaves later world messages. ProcessPackets is in flight_packets.c.
 * AdmitInput's staging needs the recorded controls, which need loaded settings and the game's DirectInput
 * keyboard device, so only its refusals before sampling are checked. */
#include "test_assert.h"
#include "xvt/flight/flight.h"
#include "xvt/flight/flight_input.h"
#include "xvt/flight/flight_surface.h"
#include "xvt/flight/player/player.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/flight_net.h"
#include "xvt/net/flight_sync.h"
#include "xvt/net/net.h"
#include "xvt/net/net_session.h"
#include "xvt/render/flight_sw.h"
#include "xvt/util/time.h"
#include "xvt_runtime/runtime/flight_checkpoint.h"
#include "xvt_runtime/runtime/flight_frame.h"
#include "xvt_runtime/runtime/flight_messages.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/flight_protocol.h"
#include "xvt_runtime/runtime/flight_wire.h"
#include "xvt_runtime/runtime/resync_task.h"
#include "xvt_runtime/timing/host_clock.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { HOST_DPID = 500, SECOND_US = 1000000 };

static XvtFlightMessage g_message, g_out;
static uint8_t g_packet[XVT_FLIGHT_PACKET_BYTES + 8];

static int Dpid(unsigned player) { return 100 + (int)player; }

static void World(int host) {
	memset(g_players, 0, sizeof g_players);
	for (int i = 0; i < 8; ++i) {
		g_players[i].objectIndex = -1;
		g_playerAbortFlags[i] = 0;
		g_flightNetPeerSilenceTicks[i] = 0;
	}
	g_players[0].participationState = 1;
	g_localPlayer = 0;
	memset(&g_netSession, 0, sizeof g_netSession);
	g_netSession.localIsHost = host;
	g_netSession.hostDplayId = HOST_DPID;
	for (unsigned i = 0; i < 8; ++i)
		g_netSession.players[i].directPlayId = Dpid(i);
	memset(g_inputHistory, 0, sizeof g_inputHistory);
	memset(g_inputFrameCount, 0, sizeof g_inputFrameCount);
	memset(&g_flightMissionState, 0, sizeof g_flightMissionState);
	g_gameTime = 0;
	g_serverTickTime = 0;
	g_activeFlightPlayerCount = 1;
	g_flightNetPendingAckCount = 0;
	g_flightNetClockAdjustAccumTicks = 0;
	g_flightNetWorldMessageTurnTimestamp = 0;
	g_flightNetClockLeadTicks = 0;
	g_flightNetLastSentWorldMessageTimestamp = 0;
	g_flightNetChecksumRequestAccumTicks = 0;
	XvtTime_Reset();
	XvtTime_AdvanceHostClock(SECOND_US);
	Time_ResetElapsedTicks();
	XvtResync_Reset();
	XvtFlightNetwork_Reset();
	XvtFlightNetwork_ClearCookies();
	XvtFlightNetwork_ResetMission();
	XvtFlightNetwork_ClearRecoveryRequest();
	XvtFlightCheckpoint_Begin(0x01);
}

/* Agrees a mission cookie the way a host flying alone does, and returns it. */
static uint32_t AgreeCookie(void) {
	int host = g_netSession.localIsHost;
	g_netSession.localIsHost = 1;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_ExchangeOptions(), 1);
	g_netSession.localIsHost = host;
	XVT_ASSERT_TRUE(XvtFlightNetwork_Cookie() != 0);
	return XvtFlightNetwork_Cookie();
}

static FlightInputFrameRecord Controls(int8_t axis) {
	FlightInputFrameRecord input;
	memset(&input, 0, sizeof input);
	input.key = 0x61;
	input.axisX = axis;
	input.axisY = (int8_t)-axis;
	input.keyMods = 1;
	return input;
}

/* Appends a frame to player's history, which the caller keeps in tick order. */
static void AddFrame(unsigned player, int tick, int valid, int applied) {
	InputFrame* frame = &g_inputHistory[player][g_inputFrameCount[player]++];
	frame->timestamp = tick;
	frame->inputSource = valid;
	frame->awaitingRelay = applied;
	frame->input = Controls(10);
}

static const InputFrame* FrameAt(unsigned player, int tick) {
	for (int i = 0; i < g_inputFrameCount[player]; ++i)
		if (g_inputHistory[player][i].timestamp == tick)
			return &g_inputHistory[player][i];
	return NULL;
}

static void AddRecord(XvtFlightMessage* message, unsigned player, int tick, int8_t axis) {
	XvtFlightWorldInputWire* record = &message->records[message->count++];
	FlightInputFrameRecord input = Controls(axis);
	record->player = (uint8_t)player;
	XvtFlightWire_EncodeInput(&record->input, tick, &input);
}

static void CheckCookie(void) {
	World(1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Cookie(), 0);
	uint32_t first = AgreeCookie();
	/* Each agreement takes a new cookie. */
	uint32_t second = AgreeCookie();
	XVT_ASSERT_TRUE(second != first);
	/* Reset forgets the cookie and keeps the counter: the next cookie is new again. */
	XvtFlightNetwork_Reset();
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Cookie(), 0);
	uint32_t third = AgreeCookie();
	XVT_ASSERT_TRUE(third != first && third != second);
	/* CloseSession forgets both: the counter starts over, so the cookie after it is the one after the
	 * earlier CloseSession. */
	XvtFlightNetwork_ClearCookies();
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Cookie(), 0);
	XVT_ASSERT_INT_EQ(AgreeCookie(), first);

	/* A client flying alone takes no cookie of its own. */
	XvtFlightNetwork_ClearCookies();
	g_netSession.localIsHost = 0;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_ExchangeOptions(), 1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Cookie(), 0);
}

static void CheckOptionsAlone(void) {
	/* A single player fills its own resolution, rating and taunts. */
	World(1);
	g_flightResolutionMode = 2;
	g_pilotData.rating = 1234;
	for (int i = 0; i < 4; ++i)
		snprintf(g_gameConfig.taunts[i], sizeof g_gameConfig.taunts[i], "taunt %d", i);
	memset(g_playerTauntText, 0, sizeof g_playerTauntText);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_ExchangeOptions(), 1);
	XVT_ASSERT_INT_EQ(g_players[0].network.flightResolutionMode, 2);
	XVT_ASSERT_INT_EQ(g_players[0].pilotRating, 1234);
	XVT_ASSERT_INT_EQ(memcmp(g_playerTauntText[0], g_gameConfig.taunts, sizeof g_gameConfig.taunts), 0);
}

static void CheckStartAlone(void) {
	World(1);
	g_gameTime = 400;
	g_serverTickTime = 400;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_WaitForMissionStart(), 1);
	XVT_ASSERT_INT_EQ(g_gameTime, 0);
	XVT_ASSERT_INT_EQ(g_serverTickTime, 0);
}

static void CheckSession(void) {
	/* A host expecting no players is done at once; a client joining a flight in progress too. */
	World(1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_BeginRosterExchange(0, 0), XVT_FLIGHT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_ExchangeRoster(), 1);
	World(0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_BeginRosterExchange(2, 1), 1);

	/* A host waiting for two players gives up after 60 seconds without a packet. */
	World(1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_BeginRosterExchange(2, 0), XVT_FLIGHT_NETWORK_PENDING);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_ExchangeRoster(), XVT_FLIGHT_NETWORK_PENDING);
	XvtTime_AdvanceHostClock(59 * SECOND_US);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_ExchangeRoster(), XVT_FLIGHT_NETWORK_PENDING);
	XvtTime_AdvanceHostClock(2 * SECOND_US);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_ExchangeRoster(), 0);

	/* So does a client waiting for the roster. */
	World(0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_BeginRosterExchange(2, 0), XVT_FLIGHT_NETWORK_PENDING);
	XvtTime_AdvanceHostClock(59 * SECOND_US);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_ExchangeRoster(), XVT_FLIGHT_NETWORK_PENDING);
	XvtTime_AdvanceHostClock(2 * SECOND_US);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_ExchangeRoster(), 0);
}

static void CheckRecoveryFlags(void) {
	World(1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 0);
	XvtFlightNetwork_RequestRecovery();
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 1);
	XvtFlightNetwork_RequestRecovery();
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 1);
	XvtFlightNetwork_ClearRecoveryRequest();
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 0);
	XvtFlightNetwork_RequestRecovery();
	XvtFlightNetwork_Recovered();
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 0);
}

static void CheckPlayerAbort(void) {
	for (int host = 0; host < 2; ++host) {
		World(host);
		XVT_ASSERT_INT_EQ(XvtFlightNetwork_PlayerAbort(XVT_FLIGHT_PLAYERS), 0);
		XVT_ASSERT_INT_EQ(XvtFlightNetwork_PlayerAbort(0), 0);
		XVT_ASSERT_INT_EQ(XvtFlightNetwork_PlayerAbort(1), 1);
		g_localPlayer = 3;
		XVT_ASSERT_INT_EQ(XvtFlightNetwork_PlayerAbort(3), 0);
		XVT_ASSERT_INT_EQ(XvtFlightNetwork_PlayerAbort(0), 1);
	}
}

static void CheckPacketBudget(void) {
	World(1);
	/* A fresh iteration has a budget; it runs out, and starting the same iteration again does not refill
	 * it. */
	unsigned budget = 0;
	while (XvtFlightNetwork_TakePacketBudget())
		XVT_ASSERT_TRUE(++budget < 100000);
	XVT_ASSERT_TRUE(budget > 0);
	XvtFlightNetwork_BeginIteration();
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakePacketBudget(), 0);
	/* A later host clock is a new iteration, with the whole budget again. */
	XvtTime_AdvanceHostClock(1);
	unsigned again = 0;
	while (XvtFlightNetwork_TakePacketBudget())
		XVT_ASSERT_TRUE(++again < 100000);
	XVT_ASSERT_INT_EQ(again, budget);
	/* Part of a budget stays spent across BeginIteration in the same iteration. */
	XvtTime_AdvanceHostClock(1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakePacketBudget(), 1);
	XvtFlightNetwork_BeginIteration();
	unsigned rest = 0;
	while (XvtFlightNetwork_TakePacketBudget())
		++rest;
	XVT_ASSERT_INT_EQ(rest, budget - 1);
}

static void CheckBeginMission(void) {
	World(1);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Push(XVT_QUEUE_PENDING, "p", 1), 1);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Push(XVT_QUEUE_REPLAY, "r", 1), 1);
	XvtFlightNetwork_ResetMission();
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 0);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_REPLAY), 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Outgoing(), 0);
}

static void CheckAdmitInputRefusals(void) {
	/* A tick the history already holds: 1 at once, and the history is left as it was. */
	World(0);
	AddFrame(0, 8, XVT_INPUT_REAL, 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_AdmitInput(8), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[0], 1);
	XVT_ASSERT_INT_EQ(g_inputHistory[0][0].input.axisX, 10);

	/* Invalid ticks. */
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_AdmitInput(0), 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_AdmitInput(9), 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_AdmitInput(-8), 0);

	/* During recovery, even a tick the history holds. */
	XvtFlightNetwork_RequestRecovery();
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_AdmitInput(8), 0);

	/* A full history requests recovery. */
	World(0);
	for (int i = 0; i < XVT_INPUT_HISTORY_CAPACITY; ++i)
		AddFrame(0, 2 * (i + 1), XVT_INPUT_REAL, 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_AdmitInput(2 * XVT_INPUT_HISTORY_CAPACITY + 2), 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[0], XVT_INPUT_HISTORY_CAPACITY);
}

static void CheckInsertWorld(void) {
	World(0);
	g_players[1].participationState = 1;
	g_players[2].participationState = 1;
	memset(&g_message, 0, sizeof g_message);
	AddRecord(&g_message, 1, 4, 10);
	AddRecord(&g_message, 1, 6, 20);
	AddRecord(&g_message, 2, 4, 30);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_InsertWorld(&g_message), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 2);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[2], 1);
	const InputFrame* frame = FrameAt(1, 6);
	XVT_ASSERT_INT_EQ(frame->inputSource, XVT_INPUT_AUTHORITATIVE);
	XVT_ASSERT_INT_EQ(frame->awaitingRelay, 0);
	XVT_ASSERT_INT_EQ(frame->input.axisX, 20);
	XVT_ASSERT_INT_EQ(FrameAt(2, 4)->input.axisX, 30);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 0);

	/* A record that fails to decode stops the insert there. */
	World(0);
	memset(&g_message, 0, sizeof g_message);
	AddRecord(&g_message, 1, 4, 10);
	AddRecord(&g_message, 1, 5, 20);
	AddRecord(&g_message, 2, 4, 30);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_InsertWorld(&g_message), 0);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[2], 0);

	/* A record that fails to record (an authoritative frame that differs) stops it and requests
	 * recovery. */
	World(0);
	memset(&g_message, 0, sizeof g_message);
	AddRecord(&g_message, 1, 4, 10);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_InsertWorld(&g_message), 1);
	memset(&g_message, 0, sizeof g_message);
	AddRecord(&g_message, 1, 4, 30);
	AddRecord(&g_message, 2, 4, 30);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_InsertWorld(&g_message), 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[2], 0);
	XVT_ASSERT_INT_EQ(FrameAt(1, 4)->input.axisX, 10);

	/* So does a record for a player out of range. */
	World(0);
	memset(&g_message, 0, sizeof g_message);
	AddRecord(&g_message, XVT_FLIGHT_PLAYERS, 4, 10);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_InsertWorld(&g_message), 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 1);
}

static void CheckReceiveOther(void) {
	World(0);
	AgreeCookie();
	/* Too short, or not a flight data packet: not consumed. */
	XvtWire_Set32(g_packet, NET_PACKET_INPUT_BATCH);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Receive(Dpid(1), g_packet, 3), 0);
	XvtWire_Set32(g_packet, NET_PACKET_ACK);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Receive(HOST_DPID, g_packet, 4), 0);
	/* Remote-input packets are consumed and dropped. */
	XvtWire_Set32(g_packet, NET_PACKET_REMOTE_INPUT);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Receive(Dpid(1), g_packet, 16), 1);
	for (unsigned player = 0; player < 8; ++player)
		XVT_ASSERT_INT_EQ(g_inputFrameCount[player], 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 0);
}

static size_t Batch(uint32_t cookie, const int* ticks, unsigned count) {
	XvtFlightInputWire records[XVT_INPUT_BATCH_RECORDS];
	for (unsigned i = 0; i < count; ++i) {
		FlightInputFrameRecord input = Controls((int8_t)(2 * i + 40));
		XvtFlightWire_EncodeInput(&records[i], ticks[i], &input);
	}
	size_t size = XvtFlightMessages_EncodeBatch(g_packet, cookie, records, count);
	XVT_ASSERT_TRUE(size > 0);
	return size;
}

static void CheckReceiveBatch(void) {
	const int ticks[] = { 4, 6 };

	/* From a connected remote player: its predicted frames are replaced by the records, as real input. */
	World(0);
	uint32_t cookie = AgreeCookie();
	g_players[1].participationState = 1;
	AddFrame(1, 2, XVT_INPUT_REAL, 1);
	AddFrame(1, 4, XVT_INPUT_PREDICTED, 0);
	AddFrame(1, 8, XVT_INPUT_PREDICTED, 0);
	size_t size = Batch(cookie, ticks, 2);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Receive(Dpid(1), g_packet, size), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], 3);
	XVT_ASSERT_TRUE(FrameAt(1, 2) != NULL);
	XVT_ASSERT_TRUE(FrameAt(1, 8) == NULL);
	for (int i = 0; i < 2; ++i) {
		const InputFrame* frame = FrameAt(1, ticks[i]);
		XVT_ASSERT_TRUE(frame != NULL);
		XVT_ASSERT_INT_EQ(frame->inputSource, XVT_INPUT_REAL);
		XVT_ASSERT_INT_EQ(frame->input.axisX, 2 * i + 40);
	}

	/* Consumed but dropped: an invalid batch, an unknown sender, the local player, a player not
	 * connected. */
	World(0);
	cookie = AgreeCookie();
	g_players[1].participationState = 1;
	size = Batch(cookie + 1, ticks, 2);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Receive(Dpid(1), g_packet, size), 1);
	size = Batch(cookie, ticks, 2);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Receive(999, g_packet, size), 1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Receive(Dpid(0), g_packet, size), 1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Receive(Dpid(3), g_packet, size), 1);
	for (unsigned player = 0; player < 8; ++player)
		XVT_ASSERT_INT_EQ(g_inputFrameCount[player], 0);

	/* A full history requests recovery. */
	World(0);
	cookie = AgreeCookie();
	g_players[1].participationState = 1;
	for (int i = 0; i < XVT_INPUT_HISTORY_CAPACITY; ++i)
		AddFrame(1, 2 * (i + 1), XVT_INPUT_REAL, 1);
	const int late[] = { 2 * XVT_INPUT_HISTORY_CAPACITY + 2 };
	size = Batch(cookie, late, 1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Receive(Dpid(1), g_packet, size), 1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 1);
	XVT_ASSERT_INT_EQ(g_inputFrameCount[1], XVT_INPUT_HISTORY_CAPACITY);
}

static size_t WorldPart(uint32_t cookie, unsigned target, uint8_t mask) {
	memset(&g_message, 0, sizeof g_message);
	g_message.target_flags = target;
	g_message.participant_mask = mask;
	AddRecord(&g_message, 0, 2, 10);
	size_t size = XvtFlightMessages_EncodePart(g_packet, &g_message, cookie, 0);
	XVT_ASSERT_TRUE(size > 0);
	return size;
}

static void CheckReceiveWorld(void) {
	/* A complete message from the host is queued pending. */
	World(0);
	uint32_t cookie = AgreeCookie();
	size_t size = WorldPart(cookie, 8, 0x01);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Receive(HOST_DPID, g_packet, size), 1);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 1);
	XVT_ASSERT_TRUE(XvtFlightMessages_Peek(XVT_QUEUE_PENDING, &g_out, sizeof g_out) > 0);
	XVT_ASSERT_INT_EQ(g_out.target_flags, 8);
	XVT_ASSERT_INT_EQ(g_out.participant_mask, 0x01);
	XVT_ASSERT_INT_EQ(g_out.count, 1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 0);

	/* From anyone else it is consumed and dropped. */
	size = WorldPart(cookie, 16, 0x01);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Receive(Dpid(1), g_packet, size), 1);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 0);

	/* A bad part requests recovery. */
	size = WorldPart(cookie + 1, 16, 0x01);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Receive(HOST_DPID, g_packet, size), 1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 1);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 1);

	/* So does a mask outside the initial players, and the message is not queued. */
	World(0);
	cookie = AgreeCookie();
	XvtFlightCheckpoint_Begin(0x01);
	size = WorldPart(cookie, 8, 0x03);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Receive(HOST_DPID, g_packet, size), 1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 1);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 0);
	/* The same mask within the initial players is queued. */
	World(0);
	cookie = AgreeCookie();
	XvtFlightCheckpoint_Begin(0x03);
	size = WorldPart(cookie, 8, 0x03);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Receive(HOST_DPID, g_packet, size), 1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 0);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 1);

	/* And so does a full queue. */
	World(0);
	cookie = AgreeCookie();
	while (XvtFlightMessages_Push(XVT_QUEUE_PENDING, "x", 1))
		;
	unsigned full = XvtFlightMessages_Count(XVT_QUEUE_PENDING);
	size = WorldPart(cookie, 8, 0x01);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_Receive(HOST_DPID, g_packet, size), 1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 1);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), full);
}

static void CheckDecodeControl(void) {
	World(1);
	int size;

	/* Sizes under 4 or over a packet, whatever the opcode. */
	XvtWire_Set32(g_packet, NET_PACKET_INPUT_BATCH);
	size = 3;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_DecodeControl(g_packet, &size), 0);
	size = XVT_FLIGHT_PACKET_BYTES + 1;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_DecodeControl(g_packet, &size), 0);

	/* The mission start, a control packet of the start handshake, with no cookie agreed: refused. */
	XvtWire_Set32(g_packet, NET_PACKET_FLIGHT_MISSION_START);
	XvtWire_Set32(g_packet + 4, 1);
	size = 8;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_DecodeControl(g_packet, &size), 0);

	/* With a cookie: the right one appended is removed from the size; a wrong or missing one is
	 * refused. */
	uint32_t cookie = AgreeCookie();
	XvtWire_Set32(g_packet + 4, cookie);
	size = 8;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_DecodeControl(g_packet, &size), 1);
	XVT_ASSERT_INT_EQ(size, 4);
	XvtWire_Set32(g_packet + 4, cookie + 1);
	size = 8;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_DecodeControl(g_packet, &size), 0);
	size = 4;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_DecodeControl(g_packet, &size), 0);

	/* A flight data packet carries its cookie in its own header (flight_messages.h) and passes as it
	 * is, since Receive takes what DecodeControl passes. */
	XvtFlightInputWire record;
	FlightInputFrameRecord input = Controls(4);
	XvtFlightWire_EncodeInput(&record, 4, &input);
	size = (int)XvtFlightMessages_EncodeBatch(g_packet, cookie, &record, 1);
	int batch = size;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_DecodeControl(g_packet, &size), 1);
	XVT_ASSERT_INT_EQ(size, batch);
}

static void CheckNextWakeDelay(void) {
	/* Nothing pending, outgoing or staged: one world message interval. The host clock sits on a whole
	 * millisecond with the frame-delta clock reset, so no part of an interval has passed. */
	World(1);
	uint64_t interval = (uint64_t)XVT_WORLD_MESSAGE_TICKS * XVT_FLIGHT_TICK_US;
	XVT_ASSERT_TRUE(XvtFlightNetwork_NextWakeDelayUs(0) == interval);
	/* A pending message outside recovery: work now. */
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Push(XVT_QUEUE_PENDING, "p", 1), 1);
	XVT_ASSERT_TRUE(XvtFlightNetwork_NextWakeDelayUs(0) == 0);
	/* During recovery pending messages do not wait for work. */
	XvtFlightNetwork_RequestRecovery();
	XVT_ASSERT_TRUE(XvtFlightNetwork_NextWakeDelayUs(0) == interval);
}

enum { PRIME = 1000 };

/* A host whose next world message is due: the schedule was started at PRIME and every connected player
 * has applied input past the next message's tick. */
static void SendReady(void) {
	World(1);
	g_players[1].participationState = 1;
	AddFrame(0, 20, XVT_INPUT_REAL, 1);
	AddFrame(1, 20, XVT_INPUT_REAL, 1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(PRIME), 0);
}

static void CheckShouldSend(void) {
	/* Once an interval of input time has passed it sends; not before. */
	SendReady();
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(PRIME + XVT_WORLD_MESSAGE_TICKS - 1), 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(PRIME + XVT_WORLD_MESSAGE_TICKS), 1);

	/* The input time is clock-adjusted. */
	SendReady();
	g_flightNetClockAdjustAccumTicks = XVT_WORLD_MESSAGE_TICKS;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(PRIME), 1);

	/* A connected player with no applied input blocks it, as does applied input that does not pass the
	 * next message's tick; a player not connected does not. */
	SendReady();
	g_players[2].participationState = 1;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(PRIME + XVT_WORLD_MESSAGE_TICKS), 0);
	AddFrame(2, XVT_WORLD_MESSAGE_TICKS, XVT_INPUT_REAL, 1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(PRIME + XVT_WORLD_MESSAGE_TICKS), 0);
	AddFrame(2, XVT_WORLD_MESSAGE_TICKS + 2, XVT_INPUT_REAL, 1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(PRIME + XVT_WORLD_MESSAGE_TICKS), 1);
	SendReady();
	AddFrame(3, 2, XVT_INPUT_REAL, 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(PRIME + XVT_WORLD_MESSAGE_TICKS), 1);

	/* Far enough behind, it sends at once, even while a player blocks it. */
	SendReady();
	g_players[2].participationState = 1;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(PRIME + XVT_WORLD_MESSAGE_TICKS), 0);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(
						  PRIME + (XVT_WORLD_LATE_INTERVALS + 1) * XVT_WORLD_MESSAGE_TICKS + 1),
					  1);

	/* Refused while recovery is needed, a resync state request is pending, the pending queue has no room
	 * or start acknowledgements are pending. */
	int due = PRIME + XVT_WORLD_MESSAGE_TICKS;
	SendReady();
	XvtFlightNetwork_RequestRecovery();
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(due), 0);
	XvtFlightNetwork_ClearRecoveryRequest();
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(due), 1);

	SendReady();
	XvtFlightChecksumReportWire report;
	memset(&report, 0, sizeof report);
	XvtWire_Set32(report.checksum.opcode, NET_PACKET_WORLD_CHECKSUM);
	XvtWire_Set32(report.request_state, XVT_CHECKSUM_REQUEST_STATE);
	int aligned[sizeof report / sizeof(int)];
	memcpy(aligned, &report, sizeof report);
	XvtResync_DeferChecksum(Dpid(1), aligned);
	XVT_ASSERT_INT_EQ(XvtResync_HasStateRequest(), 1);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(due), 0);
	XvtResync_Reset();
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(due), 1);

	SendReady();
	while (XvtFlightMessages_Push(XVT_QUEUE_PENDING, "x", 1))
		;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(due), 0);
	XvtFlightMessages_Clear(XVT_QUEUE_PENDING);
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(due), 1);

	SendReady();
	g_flightNetPendingAckCount = 1;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(due), 0);
	g_flightNetPendingAckCount = 0;
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_TakeWorldSendTurn(due), 1);
}

/* Takes the oldest pending message into g_out; returns 0 when there is none. */
static int TakePending(void) {
	if (!XvtFlightMessages_Peek(XVT_QUEUE_PENDING, &g_out, sizeof g_out))
		return 0;
	XvtFlightMessages_Pop(XVT_QUEUE_PENDING);
	return 1;
}

static void CheckSendWorld(void) {
	/* A host flying alone: the message carries the applied input up to its tick, is queued pending, and
	 * that input is marked unapplied. */
	World(1);
	g_flightNetLastSentWorldMessageTimestamp = 16;
	AddFrame(0, 18, XVT_INPUT_REAL, 1);
	AddFrame(0, 20, XVT_INPUT_REAL, 0);
	AddFrame(0, 24, XVT_INPUT_REAL, 1);
	AddFrame(0, 26, XVT_INPUT_REAL, 1);
	XvtFlightNetwork_SendWorld();
	XVT_ASSERT_INT_EQ(TakePending(), 1);
	XVT_ASSERT_INT_EQ(g_out.target_flags & INT32_MAX, 16 + XVT_WORLD_MESSAGE_TICKS);
	XVT_ASSERT_INT_EQ(g_out.participant_mask, 0x01);
	XVT_ASSERT_INT_EQ(g_out.count, 2);
	int tick = 0;
	FlightInputFrameRecord input;
	XVT_ASSERT_INT_EQ(XvtFlightWire_DecodeInput(&g_out.records[0].input, &tick, &input), 1);
	XVT_ASSERT_INT_EQ(tick, 18);
	XVT_ASSERT_INT_EQ(XvtFlightWire_DecodeInput(&g_out.records[1].input, &tick, &input), 1);
	XVT_ASSERT_INT_EQ(tick, 24);
	XVT_ASSERT_INT_EQ(FrameAt(0, 18)->awaitingRelay, 0);
	XVT_ASSERT_INT_EQ(FrameAt(0, 24)->awaitingRelay, 0);
	XVT_ASSERT_INT_EQ(FrameAt(0, 26)->awaitingRelay, 1);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 0);

	/* The next message is XVT_WORLD_MESSAGE_TICKS after that one. */
	XvtTime_AdvanceHostClock(1);
	XvtFlightNetwork_BeginIteration();
	XvtFlightNetwork_SendWorld();
	XVT_ASSERT_INT_EQ(TakePending(), 1);
	XVT_ASSERT_INT_EQ(g_out.target_flags & INT32_MAX, 16 + 2 * XVT_WORLD_MESSAGE_TICKS);
	XVT_ASSERT_INT_EQ(g_out.count, 1);
}

static void CheckSendWorldRefusals(void) {
	/* During recovery: nothing. */
	World(1);
	XvtFlightNetwork_RequestRecovery();
	XvtFlightNetwork_SendWorld();
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 0);
	XVT_ASSERT_INT_EQ(g_flightNetLastSentWorldMessageTimestamp, 0);

	/* No player to include: the only connected player has aborted. */
	World(1);
	g_playerAbortFlags[0] = 1;
	XvtFlightNetwork_SendWorld();
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 0);
	XVT_ASSERT_INT_EQ(g_flightNetLastSentWorldMessageTimestamp, 0);
	World(1);
	g_players[0].participationState = 0;
	XvtFlightNetwork_SendWorld();
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 0);

	/* A tick past the valid range ends the mission. */
	World(1);
	g_flightNetLastSentWorldMessageTimestamp = INT32_MAX - 1;
	XvtFlightNetwork_SendWorld();
	XVT_ASSERT_INT_EQ(g_flightMissionState.missionEndPending, 1);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), 0);

	/* A full pending queue requests recovery. */
	World(1);
	while (XvtFlightMessages_Push(XVT_QUEUE_PENDING, "x", 1))
		;
	unsigned full = XvtFlightMessages_Count(XVT_QUEUE_PENDING);
	XvtFlightNetwork_SendWorld();
	XVT_ASSERT_INT_EQ(XvtFlightNetwork_NeedsRecovery(), 1);
	XVT_ASSERT_INT_EQ(XvtFlightMessages_Count(XVT_QUEUE_PENDING), full);
	XVT_ASSERT_INT_EQ(g_flightNetLastSentWorldMessageTimestamp, 0);
}

static void CheckChecksumFlag(void) {
	/* Messages are flagged for a checksum every XVT_WORLD_CHECKSUM_TICKS, to within one message. */
	World(1);
	int flagged[8], count = 0;
	for (int i = 0; i < 3 * XVT_WORLD_CHECKSUM_TICKS / XVT_WORLD_MESSAGE_TICKS; ++i) {
		XvtTime_AdvanceHostClock(1);
		XvtFlightNetwork_BeginIteration();
		XvtFlightNetwork_SendWorld();
		XVT_ASSERT_INT_EQ(TakePending(), 1);
		if (g_out.target_flags & XVT_WORLD_CHECKSUM_FLAG) {
			XVT_ASSERT_TRUE(count < 8);
			flagged[count++] = (int)(g_out.target_flags & INT32_MAX);
		}
	}
	XVT_ASSERT_TRUE(count >= 2);
	for (int i = 1; i < count; ++i) {
		int gap = flagged[i] - flagged[i - 1];
		XVT_ASSERT_TRUE(gap >= XVT_WORLD_CHECKSUM_TICKS - XVT_WORLD_MESSAGE_TICKS);
		XVT_ASSERT_TRUE(gap <= XVT_WORLD_CHECKSUM_TICKS + XVT_WORLD_MESSAGE_TICKS);
	}
}

int main(void) {
	CheckCookie();
	CheckOptionsAlone();
	CheckStartAlone();
	CheckSession();
	CheckRecoveryFlags();
	CheckPlayerAbort();
	CheckPacketBudget();
	CheckBeginMission();
	CheckAdmitInputRefusals();
	CheckInsertWorld();
	CheckReceiveOther();
	CheckReceiveBatch();
	CheckReceiveWorld();
	CheckDecodeControl();
	CheckNextWakeDelay();
	CheckShouldSend();
	CheckSendWorld();
	CheckSendWorldRefusals();
	CheckChecksumFlag();
	World(0);
	XvtFlightNetwork_ClearCookies();
	return 0;
}
