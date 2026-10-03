#include "xvt/net/flight_net.h"
#ifdef XVT_MODERN
#include "xvt_runtime/input/flight_controls.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/resync_task.h"
#endif
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
#include <stdio.h>
#include <string.h>

/* DirectPlay id of the player the host is resending the world to, named in the
 * communication-failure alert; 0 means none, and the alert then names the host.
 * 6 functions write it: the resync-notice handlers in
 * FlightNet_ProcessIncomingPackets, FlightNet_HandleWorldStateResyncPacket and
 * XvtFlightNetwork_Control (the host sends 0 when a resync ends);
 * FlightNet_ResolveResyncPlayerName, which swaps in the host's id; and both
 * mission-start waits, which reset it to 0. Only
 * FlightNet_ResolveResyncPlayerName reads it, and only the original build calls
 * that. */
// GLOBAL: XVT 0x5242DC
int g_flightNetResyncPlayerDplayId = 0;
/* Acks the host still waits for; while it is not 0 the host takes no
 * world-message turn. Set to 1 or 2 when the mission starts, and to 1 while a
 * resync is sent or applied; each ACK packet takes 1 off, and the last one also
 * zeroes g_flightNetWorldMessageTurnTimestamp. 11 functions write it, chiefly
 * both mission-start waits, both packet handlers
 * (FlightNet_ProcessIncomingPackets, XvtFlightNetwork_Control), and the resync
 * senders here and among the XvtResync_ functions. */
// GLOBAL: XVT 0x5242E0
int g_flightNetPendingAckCount = 0;
/* The host's world-message turn clock, in adjusted input ticks
 * (g_inputTimestamp plus g_flightNetClockAdjustAccumTicks): each turn taken
 * moves it on by one world-message interval (g_netUpdateIntervalTicks in the
 * original, 8 ticks in the new code). 0 means not started; the next turn check
 * starts it at the adjusted time plus an eighth of g_flightNetClockLeadTicks.
 * Zeroed by FlightNet_ResetWorldMessageSchedule and when the last pending ack
 * arrives; advanced by FlightNet_TakeWorldMessageTurn and
 * XvtFlightNetwork_TakeWorldSendTurn. */
// GLOBAL: XVT 0x5242E4
int g_flightNetWorldMessageTurnTimestamp;
/* The tick stamped on the host's last world message; the next one carries this
 * plus one interval (g_netUpdateIntervalTicks in the original, 8 ticks in the
 * new code). Written by FlightNet_BroadcastWorldMessage and
 * XvtFlightNetwork_SendWorld; zeroed by FlightNet_ResetWorldMessageSchedule at
 * mission start. */
// GLOBAL: XVT 0x5242E8
int g_flightNetLastSentWorldMessageTimestamp;
/* Set to 1 by FlightNet_ResetWorldMessageSchedule; nothing reads it. */
// GLOBAL: XVT 0x5242EC
static int g_unusedFlightNetMissionStartAckInitFlag;
/* Running total of the clock steering applied to g_inputTimestamp, with the
 * opposite sign, so g_inputTimestamp plus this is the input clock before
 * steering; in ticks. Written wherever the clock is steered:
 * Flight_RunMissionLoop and XvtFlightFrame_AdjustClock in flight, and both
 * mission-start waits, which first reset it to 0. */
// GLOBAL: XVT 0x52340C
int g_flightNetClockAdjustAccumTicks = 0;
/* 1 once a SESSION_ABORT packet has arrived this flight; the mission debrief
 * and XvtFlightFrame_NetworkUpdate read it. Set by
 * FlightNet_ProcessIncomingPackets, FlightNet_HandleWorldStateResyncPacket and
 * XvtFlightNetwork_Control; reset to 0 at flight load (Flight_MainLoop,
 * XvtFlightLoading_Globals). */
// GLOBAL: XVT 0x52342C
int g_flightNetHostAbortReceived = 0;
/* Ticks between the host's world messages. At mission start the original sets
 * 59, 39 or 29 from the server update rate 4, 6 or 8 (default 29) and the new
 * code sets 8. In a solo flight both frame loops (Flight_RunMissionLoop,
 * XvtFlightFrame_StartAdvance) set it each frame to that frame's step,
 * g_inputTimestamp minus g_gameTime. */
// GLOBAL: XVT 0x523418
int g_netUpdateIntervalTicks = 29;
/* 1 while the communication-failure alert of FlightNet_ProcessIncomingPackets
 * is up; the host takes no world-message turn meanwhile. Only the original
 * build sets it: FlightNet_ProcessIncomingPackets opens and closes the alert,
 * and FlightNet_WaitForMissionStart resets it to 0. */
// GLOBAL: XVT 0x557358
int g_flightNetRecoveryUiActive = 0;
/* The buffer each flight packet is built in just before it is sent; what it
 * holds lasts until the next packet is built. 24 functions write it: most
 * functions in this file, XvtFlightNetwork_WaitForMissionStart,
 * XvtFlightNetwork_AnswerClockProbe and the XvtResync_ functions. */
// GLOBAL: XVT 0x557360
FlightNetScratchPacket g_flightNetScratchPacket = {0};
/* The adjusted input time (g_inputTimestamp plus
 * g_flightNetClockAdjustAccumTicks) sent in this client's last clock probe; a
 * probe reply counts only if it echoes this value. Written by
 * FlightNet_SendClockProbeToHost. */
// GLOBAL: XVT 0x556ED0
int g_flightNetClockProbeTimestamp = 0;
/* Per player slot, on the host, ticks since that player was last heard from:
 * each world message adds one interval to every other player's count (the
 * original as the host receives its own message, the new code as it sends one),
 * and an input or a loading pulse from the player sets it back to 0. Past 7,080
 * ticks the host tells all players that player has aborted. Counts of -1 are
 * skipped, but no code sets -1. 7 functions write it, chiefly
 * FlightNet_ProcessIncomingPackets, XvtFlightNetwork_SendWorld and
 * XvtFlightNetwork_Receive; both mission-start waits zero it. */
// GLOBAL: XVT 0x556ED8
int g_flightNetPeerSilenceTicks[8] = {0};
/* Input-clock tick at which the communication-failure alert opened or last
 * changed its text; the text alternates every 118 ticks. Only
 * FlightNet_ProcessIncomingPackets uses it, in the original build. */
// GLOBAL: XVT 0x556EFC
int g_flightNetRecoveryUiBlinkTime = 0;
/* g_inputTimestamp when the communication-failure alert opened;
 * g_inputTimestamp returns to it when the alert closes. Only
 * FlightNet_ProcessIncomingPackets uses it, in the original build. */
// GLOBAL: XVT 0x55734C
int g_flightNetRecoverySavedInputTimestamp = 0;
/* The local input clock, in ticks: the tick stamped on this player's next
 * input. It runs ahead of g_serverTickTime by about g_flightNetClockLeadTicks
 * and is steered toward that gap (see g_flightNetClockAdjustAccumTicks). 28
 * functions write it, chiefly the frame loops (Flight_RunMissionLoop, the
 * XvtFlightFrame_ functions), FlightNet_SampleLocalInput,
 * FlightNet_ProcessIncomingPackets and the resync waits; set to 0 at mission
 * start, or 30 in a solo flight. */
// GLOBAL: XVT 0x9A8C2C
int g_inputTimestamp = 0;
/* The tick of the last confirmed world state: in network play the tick of the
 * last world message applied, in a solo flight the tick the simulation last
 * stepped to. 9 functions write it, chiefly FlightSync_ApplyWorldMessagePacket,
 * Flight_RunMissionLoop, XvtFlightFrame_Advance, XvtFlightFrame_Confirm and the
 * resync apply code; set to 0 at mission start. */
// GLOBAL: XVT 0xA07CC8
int g_serverTickTime = 0;
/* Per player slot, 1 until a PLAYER_DISCONNECTED for the slot is sent or
 * received, then 0; players send their inputs directly only to slots still at
 * 1, though in internet play the host gets them regardless. Set to 1 for every
 * slot at flight load (Flight_MainLoop, XvtFlightLoading_Globals); cleared by
 * FlightNet_BroadcastPlayerDisconnected, FlightNet_ProcessIncomingPackets and
 * XvtFlightNetwork_Control. */
// GLOBAL: XVT 0xA07BB0
int g_playerConnected[8] = {0};
/* Per player slot, 1 once that player has left the flight: a PLAYER_ABORT for
 * it arrived, or this player left on its own. 7 functions write it, chiefly
 * FlightNet_ProcessIncomingPackets, FlightNet_HandleWorldStateResyncPacket and
 * XvtFlightNetwork_Control; XvtFlightCheckpoint_ApplyConfirmedMask sets it for
 * each player who began the flight but is no longer confirmed. Reset to 0 at
 * flight load (Flight_MainLoop, XvtFlightLoading_Globals). */
// GLOBAL: XVT 0x9D8A30
int g_playerAbortFlags[8] = {0};
/* This player's input for the current frame: key, X and Y axes and key
 * modifiers, with roll and throttle in the new code. Filled by
 * FlightNet_SampleLocalInput; cleared at flight load by Flight_MainLoop and
 * XvtFlightLoading_Globals. */
// GLOBAL: XVT 0xA082A8
FlightInputFrameRecord g_currentInputFrame = {0};
/* g_inputTimestamp of the last input packet FlightNet_SampleLocalInput built; 0
 * means none yet, which forces a full timestamp. Reset to 0 by
 * FlightNet_WaitForMissionStart. Only the original build uses it. */
// GLOBAL: XVT 0x559788
int g_lastSentInputTimestamp = 0;
/* g_inputTimestamp of the last input packet that carried a full 4-byte
 * timestamp; FlightNet_SampleLocalInput sends a full one again when the
 * previous input came more than 236 ticks after it. Reset to 0 by
 * FlightNet_WaitForMissionStart. Only the original build uses it. */
// GLOBAL: XVT 0x557348
int g_lastKeyframeTime = 0;
/* The internet-play input batch being filled: packet type, record count, then
 * the records, each encoded as in FlightNet_SampleLocalInput without the packet
 * type. FlightNet_SampleLocalInput appends to it and sends it;
 * FlightNet_WaitForMissionStart empties it. Only the original build uses it. */
// GLOBAL: XVT 0x557148
FlightNetInputBatchPacket g_flightNetInputBatchPacket = {0};
/* Bytes used in g_flightNetInputBatchPacket, its 5-byte header included, so 5
 * when empty. Written by FlightNet_SampleLocalInput and
 * FlightNet_WaitForMissionStart; only the original build uses it. */
// GLOBAL: XVT 0x557354
int g_flightNetInputBatchLen = 0;
/* Per player slot, the timestamp of that player's last decoded input; a record
 * that carries only the low 7 bits takes the high bits from here, one step of
 * 128 later when the low bits went backward. Written as inputs are decoded by
 * FlightNet_ProcessIncomingPackets and FlightNet_HandleWorldStateResyncPacket;
 * zeroed by FlightNet_WaitForMissionStart. Only the original build uses it. */
// GLOBAL: XVT 0x557560
int g_flightNetLastInputTimestampByPlayer[8] = {0};
/* g_inputTimestamp when FlightNet_SampleLocalInput last sent the input batch;
 * zeroed by FlightNet_WaitForMissionStart. Only the original build uses it. */
// GLOBAL: XVT 0x556EF8
int g_flightNetLastInputBatchSendTime = 0;
/* The input batch goes out once more than this many ticks have passed since the
 * last send. FlightNet_WaitForMissionStart sets it to 23 and nothing else
 * writes it; only the original build uses it. */
// GLOBAL: XVT 0x557580
int g_flightNetInputBatchIntervalTicks = 0;
/* With internet play, sessions of at least this many players (3) send inputs to
 * the host only, and smaller ones also send them to every other connected
 * player. The head start a clock probe asks for is halved in smaller sessions,
 * and whenever internet play is off. Nothing changes it. */
// GLOBAL: XVT 0x5242C4
int g_flightNetSmallSessionPlayerThreshold = 3;
/* World messages received this run; FlightNet_ProcessIncomingPackets and
 * XvtFlightNetwork_Receive count it up, and nothing reads or resets it. */
// GLOBAL: XVT 0x5242CC
int g_flightNetReceivedWorldMessageCount = 0;
/* When 1, FlightNet_SampleLocalInput logs each local input to inputlog.txt and
 * FlightNet_BroadcastWorldMessage logs each world message to serverlog.txt.
 * Nothing sets it, so it stays 0 and neither log is written. */
// GLOBAL: XVT 0x5242D0
int g_inputLogEnabled = 0;
/* inputlog.txt, opened by FlightNet_SampleLocalInput on the first logged input
 * and never closed; NULL until then. */
// GLOBAL: XVT 0x5242D4
XvtFile *g_inputLogFile = NULL;
/* World messages sent this run (the original also counts a call in a solo
 * flight); FlightNet_BroadcastWorldMessage and XvtFlightNetwork_SendWorld count
 * it up, and nothing reads or resets it. */
// GLOBAL: XVT 0x5242C8
int g_flightNetSentWorldMessageCount = 0;
/* Ticks since the host last asked for world checksums. The original adds each
 * world message's tick minus g_serverTickTime, the new code adds 8 per message;
 * once it passes 472, the message being sent asks every player for a checksum
 * and this restarts at 0. Written by FlightNet_BroadcastWorldMessage and
 * XvtFlightNetwork_SendWorld; XvtFlightNetwork_WaitForMissionStart zeroes it at
 * mission start. */
// GLOBAL: XVT 0x557350
int g_flightNetChecksumRequestAccumTicks = 0;
/* Per player slot, on the host, the answer to the current world checksum round:
 * 0 none yet, 1 matched the host's, 2 did not. Cleared when a world message
 * asks for checksums (FlightNet_BroadcastWorldMessage,
 * XvtFlightNetwork_SendWorld) and at flight load (Flight_MainLoop,
 * XvtFlightLoading_Globals); set by FlightSync_HandleWorldChecksumPacket, and
 * to 2 by XvtResync_CompleteChecksum after a resync. When every player still
 * flying shows 1, the host clears
 * g_flightNetBufferWorldMessagesUntilChecksum. */
// GLOBAL: XVT 0x9D77D0
int g_flightNetWorldChecksumPeerStatus[8] = {0};
/* serverlog.txt, opened by FlightNet_BroadcastWorldMessage on the first logged
 * world message and never closed; NULL until then. */
// GLOBAL: XVT 0x5242D8
XvtFile *g_flightNetServerLogFile = NULL;
/* Ticks since this player last heard from the host: a world message or the
 * host's loading pulse sets it to 0, and the ticks that pass without one add up
 * (every frame on a client in the new code, during stalls and resyncs in the
 * original). Past 7,080 ticks the player gives up on the host and leaves the
 * flight. 8 functions write it, chiefly FlightNet_ProcessIncomingPackets,
 * FlightNet_HandleWorldStateResyncPacket, Flight_RunMissionLoop and
 * XvtFlightFrame_NetworkUpdate; both mission-start waits zero it. */
// GLOBAL: XVT 0x9A8C24
int g_flightNetHostTimeoutElapsedTicks = 0;
/* Set to 1 when any resync chunk ack arrives (FlightNet_ProcessIncomingPackets,
 * XvtFlightNetwork_Control); the wait for a resync apply ack
 * (FlightNet_SendWorldStateResyncApplyRequest, XvtResync_Apply) clears it and
 * restarts its wait window. */
// GLOBAL: XVT 0x556F00
int g_flightNetWorldStateAckReceivedFlag = 0;
/* Per chunk slot in the current batch of 16 resync chunks, 1 once the receiving
 * player has acked it. Set by the chunk-ack handlers
 * (FlightNet_ProcessIncomingPackets, XvtFlightNetwork_Control); cleared for
 * each new batch by FlightNet_SendWorldStateResyncToPlayer and the XvtResync_
 * functions. */
// GLOBAL: XVT 0x556F08
int g_flightNetWorldStateChunkAcked[16] = {0};
/* Per world-state segment, the host's checksum of the world it is resending,
 * filled by Flight_BuildWorldStateResyncSegmentChecksums for
 * FlightNet_SendWorldStateResyncToPlayer; segments whose checksum matches the
 * receiver's are not sent. Only the original build uses it. */
// GLOBAL: XVT 0x556F48
int g_flightNetLocalResyncChecksums[126] = {0};
/* Per world-state segment, the checksums the receiving player sent back in
 * RESYNC_CHECKSUMS, copied in by FlightNet_ProcessIncomingPackets. Only the
 * original build uses it. */
// GLOBAL: XVT 0x557588
int g_flightNetRemoteResyncChecksums[126] = {0};
/* The batch of up to 16 RESYNC_CHUNK packets being built and sent to one
 * player; built by FlightNet_SendWorldStateResyncToPlayer and the XvtResync_
 * chunk builders. */
// GLOBAL: XVT 0x557788
FlightNetWorldStateChunkPacket g_flightNetWorldStateChunkPackets[16] = {{0}};
/* Set to 1 when the receiving player's RESYNC_CHECKSUMS arrives; the resync
 * sender clears it before it waits and stops waiting once it is set. 5
 * functions write it: FlightNet_ProcessIncomingPackets,
 * FlightNet_SendWorldStateResyncToPlayer, XvtResync_BeginSend,
 * XvtResync_Checksums and XvtResync_ReceivePacket. */
// GLOBAL: XVT 0x55978C
int g_flightNetRemoteResyncChecksumsReceivedFlag = 0;

/* Picks the player the communication-failure alert names: the one in
 * g_flightNetResyncPlayerDplayId, or the host when that id is 0 or the player
 * has aborted or no longer takes part. Stores the chosen id back in
 * g_flightNetResyncPlayerDplayId and returns NetSession_GetPlayerName for that
 * slot. Does not check for the 8 NetSession_FindPlayerSlotByDpid returns when
 * no player has the id. Only the original build calls this. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x462A10
char *FlightNet_ResolveResyncPlayerName(void)
{
	int playerDplayId;
	int playerSlot;

	playerDplayId = g_flightNetResyncPlayerDplayId;
	if (playerDplayId == 0) {
		playerDplayId = NetSession_GetHostDplayId();
	}
	g_flightNetResyncPlayerDplayId = playerDplayId;
	playerSlot = NetSession_FindPlayerSlotByDpid(playerDplayId);
	if (g_playerAbortFlags[playerSlot] != 0) {
		g_flightNetResyncPlayerDplayId = NetSession_GetHostDplayId();
		playerSlot = NetSession_FindPlayerSlotByDpid(
			NetSession_GetHostDplayId());
	}
	if (g_players[playerSlot].participationState == 0) {
		g_flightNetResyncPlayerDplayId = NetSession_GetHostDplayId();
		playerSlot = NetSession_FindPlayerSlotByDpid(
			NetSession_GetHostDplayId());
	}
	return NetSession_GetPlayerName(playerSlot);
}

/* Before a flight, shares each player's screen resolution mode, rating and
 * taunts. The modern build hands off to XvtFlightNetwork_ExchangeOptions and
 * returns its result: 1 done, 0 failed, XVT_FLIGHT_NETWORK_PENDING while it
 * waits, and it is called again each frame. The original blocks: a client sends
 * its mode and rating to the host and waits for the host's roster of every
 * player's, which also sets g_flightConfNewNet; the host collects one from each
 * other player, then broadcasts that roster and waits to receive it. Then every
 * player sends its taunts to all and stores what arrives in g_playerTauntText
 * until it has one set per active player or 30 s pass with no packet. Writes
 * g_players[].network.flightResolutionMode, g_players[].pilotRating and
 * g_flightNetScratchPacket, and shows the waiting alert with each still-loading
 * player's name. Returns 0 when 60 s pass with no packet before the roster
 * arrives, else 1; a solo flight copies its own values into slot 0 and returns
 * 1. Does not check the slot number in a taunt packet. */
// FUNCTION: XVT 0x463160
int FlightNet_SyncPlayerOptionsAndTaunts(void)
{
#ifdef XVT_MODERN
	return XvtFlightNetwork_ExchangeOptions();
#else
	int activePlayers;
	int *packet;
	int senderDpid;
	uint32_t statusUpdateTime;
	int receivedPlayerCount;
	int packetSize;
	uint32_t lastPacketTime;
	char statusText[256];
	int blinkState;
	int hostDplayId;
	int playerIndex;
	uint32_t currentTime;
	char *playerName;
	const char *loadingSuffix;

	blinkState = 1;
	playerIndex = 0;
	statusUpdateTime = playerIndex;
	hostDplayId = NetSession_GetHostDplayId();
	if (g_activeFlightPlayerCount > 1) {
		if (NetSession_IsLocalHost() != 0) {
			if (g_activeFlightPlayerCount > playerIndex) {
				int remainingPlayers =
					g_activeFlightPlayerCount;
				do {
					g_players[playerIndex]
						.network.flightResolutionMode =
						FLIGHT_RESOLUTION_320X240;
					g_players[playerIndex].pilotRating = 0;
					++playerIndex;
					--remainingPlayers;
				} while (remainingPlayers != 0);
			}
			NetSession_CountActivePlayers();
			g_players[g_localPlayer].network.flightResolutionMode =
				g_flightResolutionMode;
			g_players[g_localPlayer].pilotRating =
				g_pilotData.rating;

			FlightAlert_SaveBoxBackground();
			FlightAlert_DrawBox(
				1,
				g_strDiskIoMessages
					[DISK_IO_STR_WAITING_FOR_OTHER_PLAYERS],
				0x30);
			receivedPlayerCount = 0;
			lastPacketTime = timeGetTime();
			while (NetSession_CountActivePlayers() - 1 >
			       receivedPlayerCount) {
				packet = NetSession_WaitForGamePacket(
					&senderDpid, &packetSize, 60);
				currentTime = timeGetTime();
				if (packet != NULL) {
					lastPacketTime = currentTime;
					if (*packet ==
					    NET_PACKET_STILL_LOADING) {
						currentTime = timeGetTime();
						if ((int)(currentTime -
							  statusUpdateTime) >
						    200) {
							statusUpdateTime =
								currentTime;
							playerIndex =
								NetSession_FindPlayerSlotByDpid(
									senderDpid);
							playerName = NetSession_GetPlayerName(
								playerIndex);
							if (playerName ==
							    NULL) {
								strcpy(statusText,
								       g_strDiskIoMessages
									       [DISK_IO_STR_OTHER_PLAYERS_STILL_LOADING]);
							} else {
								strcpy(statusText,
								       playerName);
								blinkState =
									!blinkState;
								if (blinkState) {
									loadingSuffix = g_strDiskIoMessages
										[DISK_IO_STR_PLAYER_STILL_LOADING_MINUS];
								} else {
									loadingSuffix = g_strDiskIoMessages
										[DISK_IO_STR_PLAYER_STILL_LOADING_PLUS];
								}
								strcat(statusText,
								       loadingSuffix);
							}
							FlightAlert_DrawBox(
								3, statusText,
								0x30);
						}
					}
					if (*packet ==
					    NET_PACKET_PLAYER_OPTIONS) {
						playerIndex =
							NetSession_FindPlayerSlotByDpid(
								senderDpid);
						g_players[playerIndex]
							.network
							.flightResolutionMode =
							packet[1];
						g_players[playerIndex]
							.pilotRating =
							packet[2];
						++receivedPlayerCount;
					}
				} else if (currentTime - lastPacketTime >
					   60000) {
					return 0;
				}
			}
			FlightAlert_RestoreBoxBackground();

			{
				g_flightNetScratchPacket.payloadDwords[0] =
					g_flightConfNewNet;
				g_flightNetScratchPacket.packetType =
					NET_PACKET_PLAYER_OPTIONS_ROSTER;
				for (playerIndex = 0;
				     playerIndex < g_activeFlightPlayerCount;
				     ++playerIndex) {
					g_flightNetScratchPacket
						.payloadDwords[1 + playerIndex *
									   2] =
						g_players[playerIndex]
							.network
							.flightResolutionMode;
					g_flightNetScratchPacket
						.payloadDwords[2 + playerIndex *
									   2] =
						g_players[playerIndex]
							.pilotRating;
				}
			}

			NetSession_BroadcastPacketToPlayers(
				(unsigned int *)&g_flightNetScratchPacket,
				8 * g_activeFlightPlayerCount + 8);
			do {
				packet = NetSession_WaitForGamePacket(
					&senderDpid, &packetSize, 60);
				if (packet == NULL) {
					return 0;
				}
			} while (*packet != NET_PACKET_PLAYER_OPTIONS_ROSTER);
		} else {
			g_flightNetScratchPacket.packetType =
				NET_PACKET_PLAYER_OPTIONS;
			g_flightNetScratchPacket.payloadDwords[0] =
				g_flightResolutionMode;
			g_flightNetScratchPacket.payloadDwords[1] =
				g_pilotData.rating;

			NetSession_SendPacket(
				hostDplayId,
				(unsigned int *)&g_flightNetScratchPacket, 12);
			FlightAlert_SaveBoxBackground();
			FlightAlert_DrawBox(
				1,
				g_strDiskIoMessages
					[DISK_IO_STR_WAITING_FOR_OTHER_PLAYERS],
				0x30);
			do {
				packet = NetSession_WaitForGamePacket(
					&senderDpid, &packetSize, 60);
				if (packet == NULL) {
					return 0;
				}
				if (*packet == NET_PACKET_STILL_LOADING) {
					currentTime = timeGetTime();
					if ((int)(currentTime -
						  statusUpdateTime) > 200) {
						statusUpdateTime = currentTime;
						playerIndex =
							NetSession_FindPlayerSlotByDpid(
								senderDpid);
						playerName =
							NetSession_GetPlayerName(
								playerIndex);
						if (playerName == NULL) {
							strcpy(statusText,
							       g_strDiskIoMessages
								       [DISK_IO_STR_OTHER_PLAYERS_STILL_LOADING]);
						} else {
							strcpy(statusText,
							       playerName);
							blinkState =
								!blinkState;
							if (blinkState) {
								loadingSuffix = g_strDiskIoMessages
									[DISK_IO_STR_PLAYER_STILL_LOADING_MINUS];
							} else {
								loadingSuffix = g_strDiskIoMessages
									[DISK_IO_STR_PLAYER_STILL_LOADING_PLUS];
							}
							strcat(statusText,
							       loadingSuffix);
						}
						FlightAlert_DrawBox(
							3, statusText, 0x30);
					}
				}
			} while (*packet != NET_PACKET_PLAYER_OPTIONS_ROSTER);
			FlightAlert_RestoreBoxBackground();
			{
				int remainingPlayers;
				g_flightConfNewNet = packet[1];
				remainingPlayers = g_activeFlightPlayerCount;
				playerIndex = 0;
				if (remainingPlayers > 0) {
					do {
						g_players[playerIndex]
							.network
							.flightResolutionMode =
							packet[2 +
							       playerIndex * 2];
						g_players[playerIndex]
							.pilotRating =
							packet[3 +
							       playerIndex * 2];
						++playerIndex;
						--remainingPlayers;
					} while (remainingPlayers != 0);
				}
			}
		}

		g_flightNetScratchPacket.payloadDwords[0] = g_localPlayer;
		g_flightNetScratchPacket.packetType = NET_PACKET_PLAYER_TAUNTS;
		memcpy(&g_flightNetScratchPacket.payloadDwords[1],
		       g_gameConfig.taunts, sizeof(g_gameConfig.taunts));

		NetSession_SendPacket(0,
				      (unsigned int *)&g_flightNetScratchPacket,
				      8 + sizeof(g_gameConfig.taunts));
		receivedPlayerCount = 0;
		NetSession_CountActivePlayers();
		for (;;) {
			activePlayers = NetSession_CountActivePlayers();
			if (receivedPlayerCount >= activePlayers) {
				break;
			}
			packet = NetSession_WaitForGamePacket(&senderDpid,
							      &packetSize, 30);
			if (packet == NULL) {
				break;
			}
			if (*packet == NET_PACKET_STILL_LOADING) {
				currentTime = timeGetTime();
				if ((int)(currentTime - statusUpdateTime) >
				    200) {
					statusUpdateTime = currentTime;
					playerIndex =
						NetSession_FindPlayerSlotByDpid(
							senderDpid);
					playerName = NetSession_GetPlayerName(
						playerIndex);
					if (playerName == NULL) {
						strcpy(statusText,
						       g_strDiskIoMessages
							       [DISK_IO_STR_OTHER_PLAYERS_STILL_LOADING]);
					} else {
						strcpy(statusText, playerName);
						blinkState = !blinkState;
						if (blinkState) {
							loadingSuffix = g_strDiskIoMessages
								[DISK_IO_STR_PLAYER_STILL_LOADING_MINUS];
						} else {
							loadingSuffix = g_strDiskIoMessages
								[DISK_IO_STR_PLAYER_STILL_LOADING_PLUS];
						}
						strcat(statusText,
						       loadingSuffix);
					}
					FlightAlert_DrawBox(3, statusText,
							    0x30);
				}
				continue;
			}
			if (*packet == NET_PACKET_PLAYER_TAUNTS) {
				memcpy(g_playerTauntText[packet[1]], &packet[2],
				       sizeof(g_playerTauntText[packet[1]]));
				++receivedPlayerCount;
				continue;
			}
		}
		FlightAlert_RestoreBoxBackground();
	} else {
		g_players[0].network.flightResolutionMode =
			g_flightResolutionMode;
		g_players[0].pilotRating = g_pilotData.rating;
		memcpy(g_playerTauntText, g_gameConfig.taunts,
		       sizeof(g_gameConfig.taunts));
	}
	return 1;
#endif
}

/* Holds every player at the mission start and starts the flight clocks
 * together. The modern build hands off to XvtFlightNetwork_WaitForMissionStart
 * and returns its result: 1, 0, or XVT_FLIGHT_NETWORK_PENDING while it waits.
 * The original blocks. It resets g_flightNetLastInputTimestampByPlayer,
 * g_flightNetPeerSilenceTicks, g_lastSentInputTimestamp, g_lastKeyframeTime,
 * g_flightNetResyncPlayerDplayId, g_flightNetLastInputBatchSendTime,
 * g_flightNetInputBatchPacket and g_flightNetInputBatchLen,
 * g_flightNetRecoveryUiActive, g_flightNetPendingAckCount,
 * g_flightNetClockAdjustAccumTicks and g_flightNetHostTimeoutElapsedTicks, and
 * sets g_flightNetInputBatchIntervalTicks to 23; each player tells the host it
 * has loaded; the host waits for all active players, itself included, and
 * broadcasts the start; everyone waits for the start, acks it to the host, and
 * zeroes g_serverTickTime, g_gameTime and g_inputTimestamp. Sets
 * g_flightNetClockLeadTicks to 130 ticks for internet play, else 30, and
 * g_netUpdateIntervalTicks from the server update rate. The host then reads
 * packets until its 1 or 2 pending acks arrive or 100 ticks pass, and sets
 * g_flightNetClockLeadTicks to the time that took, at least 35 ticks, moving
 * g_inputTimestamp and g_flightNetClockAdjustAccumTicks to match. Returns 0
 * when a 60 s wait sees no packet, else 1; a solo flight sets
 * g_flightNetClockLeadTicks and g_inputTimestamp to 30 and returns 1 at
 * once. */
// FUNCTION: XVT 0x463790
int FlightNet_WaitForMissionStart(void)
{
#ifdef XVT_MODERN
	return XvtFlightNetwork_WaitForMissionStart();
#else
	enum {
		PACKET_WAIT_TIMEOUT_SECONDS = 60,
		STATUS_UPDATE_INTERVAL_MS = 200,
		DEFAULT_CLOCK_LEAD_TICKS = 30,
		ASYNC_CLOCK_LEAD_TICKS = 130,
		MINIMUM_CLIENT_CLOCK_LEAD_TICKS = 35,
		MISSION_START_ACK_TIMEOUT_TICKS = 100,
		INPUT_BATCH_INTERVAL_TICKS = 23,
		ALERT_BACKGROUND_COLOR = 0x30,
	};

	struct MissionStartWaitState {
		/* timeGetTime ms of the last status redraw */
		uint32_t statusUpdateTime;
		int senderDplayId;     /* Sender of the last packet */
		int activePlayerCount; /* Active players when the wait began */
		int packetSize;	       /* Filled by each wait; never read */
		char statusText[256];  /* Loading status line for the alert */
	} waitState;

	int *packet;
	int readyPlayerCount;
	int blinkState;
	int playerSlot;
	int clockAdjustment;
	uint32_t currentTime;
	int hostDplayId;
	int serverUpdateRate;
	char *playerName;
	const char *loadingSuffix;

	waitState.statusUpdateTime = 0;
	blinkState = 1;
	memset(g_flightNetLastInputTimestampByPlayer, 0,
	       sizeof(g_flightNetLastInputTimestampByPlayer));
	memset(g_flightNetPeerSilenceTicks, 0,
	       sizeof(g_flightNetPeerSilenceTicks));
	g_lastSentInputTimestamp = 0;
	g_lastKeyframeTime = 0;
	g_flightNetResyncPlayerDplayId = 0;
	g_flightNetLastInputBatchSendTime = 0;
	memset(&g_flightNetInputBatchPacket.frameCount, 0, sizeof(int));
	g_flightNetRecoveryUiActive = 0;
	g_flightNetPendingAckCount = 0;
	g_flightNetClockAdjustAccumTicks = 0;
	g_flightNetHostTimeoutElapsedTicks = 0;
	g_flightNetInputBatchLen = 5;
	g_flightNetInputBatchPacket.packetType = NET_PACKET_INPUT_BATCH;
	g_flightNetInputBatchIntervalTicks = INPUT_BATCH_INTERVAL_TICKS;

	if (g_activeFlightPlayerCount == 1) {
		g_serverTickTime = 0;
		g_flightNetClockLeadTicks = DEFAULT_CLOCK_LEAD_TICKS;
		g_gameTime = 0;
		g_inputTimestamp = DEFAULT_CLOCK_LEAD_TICKS;
		Time_ConsumeElapsedTicks();
		return 1;
	}

	waitState.activePlayerCount = NetSession_CountActivePlayers();
	hostDplayId = NetSession_GetHostDplayId();
	g_flightNetScratchPacket.packetType = NET_PACKET_MISSION_LOADING_READY;

	NetSession_SendPacket(hostDplayId,
			      (unsigned int *)&g_flightNetScratchPacket,
			      sizeof(g_flightNetScratchPacket.packetType));
	if (NetSession_IsLocalHost() != 0) {
		readyPlayerCount = 0;
		for (;;) {
			if (waitState.activePlayerCount <= readyPlayerCount) {
				break;
			}
			do {
				packet = NetSession_WaitForGamePacket(
					&waitState.senderDplayId,
					&waitState.packetSize,
					PACKET_WAIT_TIMEOUT_SECONDS);
				if (packet == NULL) {
					return 0;
				}
			} while (*packet != NET_PACKET_MISSION_LOADING_READY);
			++readyPlayerCount;
		}
		g_flightNetScratchPacket.packetType =
			NET_PACKET_FLIGHT_MISSION_START;

		NetSession_BroadcastPacketToPlayers(
			(unsigned int *)&g_flightNetScratchPacket,
			sizeof(g_flightNetScratchPacket.packetType));
	}

	FlightAlert_SaveBoxBackground();
	FlightAlert_DrawBox(
		1, g_strDiskIoMessages[DISK_IO_STR_WAITING_FOR_OTHER_PLAYERS],
		ALERT_BACKGROUND_COLOR);
	do {
		packet = NetSession_WaitForGamePacket(
			&waitState.senderDplayId, &waitState.packetSize,
			PACKET_WAIT_TIMEOUT_SECONDS);
		if (packet == NULL) {
			return 0;
		}
		if (*packet == NET_PACKET_STILL_LOADING) {
			currentTime = timeGetTime();
			if ((int)(currentTime - waitState.statusUpdateTime) >
			    STATUS_UPDATE_INTERVAL_MS) {
				waitState.statusUpdateTime = currentTime;
				playerSlot = NetSession_FindPlayerSlotByDpid(
					waitState.senderDplayId);
				playerName =
					NetSession_GetPlayerName(playerSlot);
				if (playerName == NULL) {
					strcpy(waitState.statusText,
					       g_strDiskIoMessages
						       [DISK_IO_STR_OTHER_PLAYERS_STILL_LOADING]);
				} else {
					strcpy(waitState.statusText,
					       playerName);
					blinkState = !blinkState;
					if (blinkState) {
						loadingSuffix = g_strDiskIoMessages
							[DISK_IO_STR_PLAYER_STILL_LOADING_MINUS];
					} else {
						loadingSuffix = g_strDiskIoMessages
							[DISK_IO_STR_PLAYER_STILL_LOADING_PLUS];
					}
					strcat(waitState.statusText,
					       loadingSuffix);
				}
				FlightAlert_DrawBox(3, waitState.statusText,
						    ALERT_BACKGROUND_COLOR);
			}
		}
	} while (*packet != NET_PACKET_FLIGHT_MISSION_START);
	FlightAlert_RestoreBoxBackground();

	hostDplayId = NetSession_GetHostDplayId();
	g_flightNetScratchPacket.packetType = NET_PACKET_ACK;

	NetSession_SendPacket(hostDplayId,
			      (unsigned int *)&g_flightNetScratchPacket,
			      sizeof(g_flightNetScratchPacket.packetType));
	Time_ConsumeElapsedTicks();
	g_serverTickTime = 0;
	g_gameTime = 0;
	g_inputTimestamp = 0;
	if (g_internetPlayEnabled != 0) {
		g_flightNetClockLeadTicks = ASYNC_CLOCK_LEAD_TICKS;
	} else {
		g_flightNetClockLeadTicks = DEFAULT_CLOCK_LEAD_TICKS;
	}
	serverUpdateRate = g_gameConfig.serverUpdateRate;
	switch (serverUpdateRate) {
	case 4:
		g_netUpdateIntervalTicks = 59;
		break;
	case 6:
		g_netUpdateIntervalTicks = 39;
		break;
	case 8:
	default:
		g_netUpdateIntervalTicks = 29;
		break;
	}

	if (NetSession_IsLocalHost() != 0) {
		g_flightNetPendingAckCount = 1;
		if (waitState.activePlayerCount != 1) {
			g_flightNetPendingAckCount = 2;
		}
		FlightNet_ResetWorldMessageSchedule();
		while (g_flightNetPendingAckCount != 0 &&
		       (unsigned int)g_inputTimestamp <
			       MISSION_START_ACK_TIMEOUT_TICKS) {
			FlightNet_ProcessIncomingPackets();
			g_inputTimestamp += Time_ConsumeElapsedTicks();
		}
		g_flightNetPendingAckCount = 0;
		g_inputTimestamp += Time_ConsumeElapsedTicks();
		g_flightNetClockLeadTicks = g_inputTimestamp;
		if (g_inputTimestamp < MINIMUM_CLIENT_CLOCK_LEAD_TICKS) {
			clockAdjustment = MINIMUM_CLIENT_CLOCK_LEAD_TICKS -
					  g_inputTimestamp;
			g_flightNetClockLeadTicks += clockAdjustment;
			g_inputTimestamp += clockAdjustment;
			g_flightNetClockAdjustAccumTicks -= clockAdjustment;
		}
	}
	return 1;
#endif
}

/* Sends the host a clock probe holding the adjusted input time
 * (g_inputTimestamp plus g_flightNetClockAdjustAccumTicks) and this player's
 * g_flightNetClockLeadTicks, and keeps that time in
 * g_flightNetClockProbeTimestamp to match the reply. Writes
 * g_flightNetScratchPacket. The modern build sends through
 * XvtFlightNetwork_SendPacket. Returns the send function's result. */
// FUNCTION: XVT 0x463B60
int FlightNet_SendClockProbeToHost(void)
{
	FlightNetScratchPacket *packet;
	int inputTimestamp;

	packet = &g_flightNetScratchPacket;
	g_flightNetScratchPacket.packetType = NET_PACKET_CLOCK_PROBE;
	inputTimestamp = g_inputTimestamp;
	packet->payloadDwords[0] =
		inputTimestamp + g_flightNetClockAdjustAccumTicks;
	g_flightNetClockProbeTimestamp =
		inputTimestamp + g_flightNetClockAdjustAccumTicks;
	packet->payloadDwords[1] = g_flightNetClockLeadTicks;
	return
#ifdef XVT_MODERN
		XvtFlightNetwork_SendPacket
#else
		NetSession_SendPacket
#endif
		(NetSession_GetHostDplayId(), (unsigned int *)packet, 12);
}

/* Tells every player this one is still loading: a 4-byte STILL_LOADING packet
 * to DirectPlay id 0, which reaches all players and queues a copy for this one.
 * Writes g_flightNetScratchPacket. The modern build sends through
 * XvtFlightNetwork_SendPacket. Returns the send function's result. */
// FUNCTION: XVT 0x463BB0
int FlightNet_BroadcastStillLoadingPulse(void)
{
	g_flightNetScratchPacket.packetType = NET_PACKET_STILL_LOADING;
	return
#ifdef XVT_MODERN
		XvtFlightNetwork_SendPacket
#else
		NetSession_SendPacket
#endif
		(0, (unsigned int *)&g_flightNetScratchPacket, 4);
}

/* Sends SESSION_ABORT to every active player in the roster, this one included,
 * which ends the flight for each. Writes g_flightNetScratchPacket. The modern
 * build sends through XvtFlightNetwork_Broadcast. Returns the broadcast's
 * result. Does not check that this player is the host. */
// FUNCTION: XVT 0x463BD0
int FlightNet_BroadcastHostSessionAbort(void)
{
	g_flightNetScratchPacket.packetType = NET_PACKET_SESSION_ABORT;
	return
#ifdef XVT_MODERN
		XvtFlightNetwork_Broadcast
#else
		NetSession_BroadcastPacketToPlayers
#endif
		((unsigned int *)&g_flightNetScratchPacket, 4);
}

/* Tells the host alone that this player is still loading, with a 4-byte
 * STILL_LOADING packet; the host then resets its silence count for this player.
 * Writes g_flightNetScratchPacket. Returns the send function's result. Only the
 * original build calls this. */
// FUNCTION: XVT 0x463BF0
int FlightNet_SendStillLoadingPulse(void)
{
	g_flightNetScratchPacket.packetType = NET_PACKET_STILL_LOADING;
	return
#ifdef XVT_MODERN
		XvtFlightNetwork_SendPacket
#else
		NetSession_SendPacket
#endif
		(NetSession_GetHostDplayId(),
		 (unsigned int *)&g_flightNetScratchPacket, 4);
}

/* Tells every active player that the player in playerSlot has lost its link,
 * and clears g_playerConnected for that slot here; players then stop sending it
 * their inputs directly. Writes g_flightNetScratchPacket. Returns the
 * broadcast's result. Does not check the slot range. Only the original build
 * calls this. */
// FUNCTION: XVT 0x463C10
int FlightNet_BroadcastPlayerDisconnected(int playerSlot)
{
	int result;
	g_flightNetScratchPacket.packetType = NET_PACKET_PLAYER_DISCONNECTED;
	g_flightNetScratchPacket.payloadDwords[0] = playerSlot;
	result =
#ifdef XVT_MODERN
		XvtFlightNetwork_Broadcast
#else
		NetSession_BroadcastPacketToPlayers
#endif
		((unsigned int *)&g_flightNetScratchPacket, 8);
	g_playerConnected[playerSlot] = 0;
	return result;
}

/* Tells every active player, this one included, that the player in playerSlot
 * leaves the flight; the named player ends its flight when the packet reaches
 * it. Writes g_flightNetScratchPacket. The modern build sends through
 * XvtFlightNetwork_Broadcast. Returns the broadcast's result. */
// FUNCTION: XVT 0x463C50
int FlightNet_BroadcastPlayerAbort(int playerSlot)
{
	g_flightNetScratchPacket.packetType = NET_PACKET_PLAYER_ABORT;
	g_flightNetScratchPacket.payloadDwords[0] = playerSlot;
	return
#ifdef XVT_MODERN
		XvtFlightNetwork_Broadcast
#else
		NetSession_BroadcastPacketToPlayers
#endif
		((unsigned int *)&g_flightNetScratchPacket, 8);
}

/* Returns the index in g_pilotData.networkPlayers of the entry with the same
 * DirectPlay id as g_players[playerIdx], or 0 when none matches, which cannot
 * be told apart from a match on the first entry. Does not check playerIdx. */
// FUNCTION: XVT 0x463C80
int FlightNet_FindPilotNetworkPlayerIndex(int playerIdx)
{
	int networkPlayerIdx;
	int *directPlayId;
	int playerDirectPlayId;
	const uint8_t *networkPlayerEnd;

	networkPlayerIdx = 0;
	directPlayId = &g_pilotData.networkPlayers[0].directPlayId;
	playerDirectPlayId = g_players[playerIdx].network.directPlayId;
	networkPlayerEnd = (const uint8_t *)directPlayId +
			   sizeof(g_pilotData.networkPlayers);
	while (*directPlayId != playerDirectPlayId) {
		directPlayId = (int *)((uint8_t *)directPlayId +
				       sizeof(PilotNetworkPlayer));
		++networkPlayerIdx;
		if ((const uint8_t *)directPlayId >= networkPlayerEnd) {
			return 0;
		}
	}
	return networkPlayerIdx;
}

/* Sets hasLeft on the g_pilotData.networkPlayers entry of the player in
 * playerSlot; when no entry matches, it marks entry 0 instead. */
// FUNCTION: XVT 0x463CC0
void FlightNet_MarkPilotNetworkPlayerLeft(int playerSlot)
{
	g_pilotData
		.networkPlayers[FlightNet_FindPilotNetworkPlayerIndex(
			playerSlot)]
		.hasLeft = 1;
}

/* Reads and acts on every flight packet waiting in the queue. The modern build
 * hands off to XvtFlightNetwork_ProcessPackets. The original does nothing when
 * this player no longer takes part, and drops every packet in a solo flight.
 * Otherwise it reads until the queue is empty, the host first sending each
 * world message that is due. Inputs, single or batched, go into the sender's
 * history, marked for relay on the host; a sender who no longer takes part is
 * told it has aborted. A world message is applied and clears
 * g_flightNetHostTimeoutElapsedTicks; on the host it adds
 * g_netUpdateIntervalTicks to each other player's silence count and aborts any
 * player past 7,080 ticks. Clock probes and replies move
 * g_flightNetClockLeadTicks halfway (at least 1 tick) toward the value they
 * imply. Resync requests, applies and chunks from another checksum epoch are
 * dropped. A call that runs more than 826 ticks opens the communication-failure
 * alert (a client also announces itself disconnected and calls
 * NetReliable_KeepOnlyHostReceivedPackets); while it is open, ESC makes this
 * player leave, and the host also ends the session. Returns early when the
 * flight ends, the last pending ack arrives, or a resync times out or ends this
 * player's flight. Writes g_inputTimestamp, g_serverTickTime,
 * g_flightNetRecoveryUiActive, g_flightNetRecoveryUiBlinkTime,
 * g_flightNetRecoverySavedInputTimestamp, g_flightNetPeerSilenceTicks,
 * g_flightNetLastInputTimestampByPlayer, g_playerConnected, g_playerAbortFlags,
 * g_flightNetHostAbortReceived, g_flightNetHostTimeoutElapsedTicks,
 * g_flightNetPendingAckCount, g_flightNetWorldMessageTurnTimestamp,
 * g_flightNetResyncPlayerDplayId, g_flightNetWorldStateAckReceivedFlag,
 * g_flightNetWorldStateChunkAcked, g_flightNetRemoteResyncChecksums and its
 * received flag, g_flightNetClockLeadTicks, g_flightNetScratchPacket,
 * g_flightNetReceivedWorldMessageCount, g_flightMissionState.missionEndPending
 * and participationState. The timing breakdown it formats at the end is never
 * shown. Does not check for the slot 8 that NetSession_FindPlayerSlotByDpid
 * returns for an unknown sender. */
// FUNCTION: XVT 0x463D00
void FlightNet_ProcessIncomingPackets(void)
{
#ifdef XVT_MODERN
	XvtFlightNetwork_ProcessPackets();
#else
	enum {
		PLAYER_COUNT = 8,
		RESYNC_CHECKSUM_COUNT = 126,
		WORLD_STATE_CHUNK_COUNT = 16,
		PACKET_CLOCK_PROBE_REPLY_SIZE = 2 * sizeof(int),
		RECOVERY_DELAY_TICKS = 826,
		RECOVERY_BLINK_TICKS = 118,
		PEER_TIMEOUT_TICKS = 7080,
		CLOCK_PROBE_BIAS_TICKS = 20,
		CLOCK_PROBE_LIMIT_TICKS = 472,
		FULL_TIMESTAMP_CODE = 0x7F,
		TIMESTAMP_CODE_MASK = 0x7F,
		KEY_PRESENT_FLAG = 0x80,
		RECOVERY_ALERT_COLOR = 0x34
	};

	FlightInputFrameRecord input;
	int senderDpid;

	struct {
		/* Two jobs: a remote-input record's timestamp code byte, or the frames left in an input batch. */
		int decodeValue;
		int serverSendElapsed; /* Ticks spent sending world messages */
		int worldFrameElapsed; /* Ticks spent applying world messages */
		int remoteInputElapsed; /* Ticks spent on single inputs */
		int receiveElapsed;	/* Ticks spent receiving packets */
		int blinkToggle; /* Which of two alert texts shows next */
		/* Input-clock tick the 826-tick alert limit counts from */
		int startTimestamp;
		int worldMessageCount; /* World messages applied this call */
		int payloadSize;       /* Filled by each receive; never read */
	} packetState;

	int currentTimestamp;
	char statusText[80];

	if (g_players[g_localPlayer].participationState == 0) {
		return;
	}

	packetState.worldMessageCount = 0;
	packetState.serverSendElapsed = 0;
	packetState.remoteInputElapsed = 0;
	packetState.receiveElapsed = 0;
	packetState.worldFrameElapsed = 0;
	packetState.blinkToggle = 0;

	if (g_flightPlayerCount == 1) {
		while (NetSession_ReceiveGamePacket(
			       &senderDpid, &packetState.payloadSize) != NULL) {
		}
		return;
	}

	if (g_flightNetRecoveryUiActive != 0) {
		int blinkTime = g_flightNetRecoveryUiBlinkTime;

		packetState.startTimestamp =
			g_flightNetRecoverySavedInputTimestamp -
			RECOVERY_DELAY_TICKS;
		currentTimestamp =
			blinkTime + (int)Time_ConsumeElapsedTicks() + 1;
	} else {
		currentTimestamp = g_inputTimestamp;
		currentTimestamp += (int)Time_ConsumeElapsedTicks();
		packetState.startTimestamp = currentTimestamp;
	}

	for (;;) {
		int *packet;

		if (currentTimestamp - packetState.startTimestamp >
		    RECOVERY_DELAY_TICKS) {
			if (g_flightNetRecoveryUiActive != 0) {
				if (FlightInput_HasKeyReady() != 0 &&
				    FlightInput_GetNextKey() ==
					    FLIGHT_KEY_ESCAPE) {
					g_flightNetRecoveryUiActive = 0;
					FlightAlert_RestoreBoxBackground();
					g_inputTimestamp =
						g_flightNetRecoverySavedInputTimestamp;
					g_serverTickTime = g_gameTime;
					g_flightMissionState.missionEndPending =
						1;
					g_players[g_localPlayer]
						.participationState = 0;
					FlightNet_BroadcastPlayerAbort(
						g_localPlayer);
					if (NetSession_IsLocalHost() == 0) {
						FlightNet_MarkPilotNetworkPlayerLeft(
							g_localPlayer);
					} else {
						FlightNet_BroadcastHostSessionAbort();
					}
					return;
				}
				if (currentTimestamp -
					    g_flightNetRecoveryUiBlinkTime >
				    RECOVERY_BLINK_TICKS) {
					g_flightNetRecoveryUiBlinkTime =
						currentTimestamp;
					packetState.blinkToggle =
						!packetState.blinkToggle;
					if (packetState.blinkToggle != 0) {
						FlightAlert_DrawBox(
							3,
							g_strDiskIoMessages
								[DISK_IO_STR_ESC_DISCONNECT],
							RECOVERY_ALERT_COLOR);
					} else {
						FlightAlert_DrawBox(
							3,
							g_strDiskIoMessages
								[DISK_IO_STR_RECOVERING_WAIT],
							RECOVERY_ALERT_COLOR);
					}
				}
			} else {
				char *playerName;

				packetState.blinkToggle = 1;
				FlightAlert_SaveBoxBackground();
				strcpy(statusText,
				       g_strDiskIoMessages
					       [DISK_IO_STR_COM_FAILURE_WAITING]);
				playerName =
					FlightNet_ResolveResyncPlayerName();
				if (playerName != NULL) {
					strcat(statusText, playerName);
				}
				FlightAlert_DrawBox(1, statusText,
						    RECOVERY_ALERT_COLOR);
				g_flightNetRecoveryUiBlinkTime =
					currentTimestamp;
				g_flightNetRecoveryUiActive = 1;
				g_flightNetRecoverySavedInputTimestamp =
					currentTimestamp;
				if (NetSession_IsLocalHost() == 0) {
					NetReliable_KeepOnlyHostReceivedPackets();
					FlightNet_BroadcastPlayerDisconnected(
						g_localPlayer);
				}
			}
		}

		currentTimestamp += (int)Time_ConsumeElapsedTicks();
		packet = NetSession_ReceiveGamePacket(&senderDpid,
						      &packetState.payloadSize);
		{
			int frameDelta = (int)Time_ConsumeElapsedTicks();

			packetState.receiveElapsed += frameDelta;
			currentTimestamp += frameDelta;
		}

		if (packet == NULL) {
			if (NetSession_IsLocalHost() == 0) {
				break;
			}

			currentTimestamp += (int)Time_ConsumeElapsedTicks();
			if (FlightNet_TakeWorldMessageTurn(g_inputTimestamp) !=
			    0) {
				int frameDelta;

				FlightNet_BroadcastWorldMessage(
					g_inputTimestamp);
				frameDelta = (int)Time_ConsumeElapsedTicks();
				packetState.serverSendElapsed += frameDelta;
				currentTimestamp += frameDelta;
				continue;
			}
			{
				int frameDelta =
					(int)Time_ConsumeElapsedTicks();

				packetState.serverSendElapsed += frameDelta;
				currentTimestamp += frameDelta;
			}
			break;
		}

		switch (packet[0]) {
		case NET_PACKET_REMOTE_INPUT: {
			const uint8_t *cursor;
			InputFrame *inserted;
			int frameDelta;
			int playerIndex;
			unsigned int timestamp;

			currentTimestamp += (int)Time_ConsumeElapsedTicks();
			playerIndex =
				NetSession_FindPlayerSlotByDpid(senderDpid);
			if (g_players[playerIndex].participationState != 0) {
				if (g_flightNetPeerSilenceTicks[playerIndex] >
				    0) {
					g_flightNetPeerSilenceTicks
						[playerIndex] = 0;
				}
				FlightSync_DiscardPredictedInputFrames(
					playerIndex);
				cursor = (const uint8_t *)&packet[1];
				memset(&input, 0, sizeof(input));
				packetState.decodeValue = *cursor;
				if ((packetState.decodeValue &
				     TIMESTAMP_CODE_MASK) ==
				    FULL_TIMESTAMP_CODE) {
					timestamp =
						*(const unsigned int *)(cursor +
									1);
					if ((packetState.decodeValue &
					     KEY_PRESENT_FLAG) != 0) {
						input.key = cursor[5];
						cursor += 6;
					} else {
						cursor += 5;
					}
				} else {
					int previousCode =
						g_flightNetLastInputTimestampByPlayer
							[playerIndex];
					int lowCode = packetState.decodeValue &
						      TIMESTAMP_CODE_MASK;

					if ((previousCode &
					     TIMESTAMP_CODE_MASK) > lowCode) {
						previousCode +=
							TIMESTAMP_CODE_MASK + 1;
					}
					timestamp =
						(unsigned int)lowCode |
						((unsigned int)previousCode &
						 ~TIMESTAMP_CODE_MASK);
					if ((packetState.decodeValue &
					     KEY_PRESENT_FLAG) != 0) {
						input.key = cursor[1];
						cursor += 2;
					} else {
						cursor += 1;
					}
				}
				g_flightNetLastInputTimestampByPlayer
					[playerIndex] = (int)timestamp;
				input.axisX =
					(int8_t)(cursor[0] & (uint8_t)~1u);
				input.axisY =
					(int8_t)(cursor[1] & (uint8_t)~1u);
				input.keyMods = cursor[1] & 1u;
				input.keyMods += input.keyMods;
				input.keyMods |= cursor[0] & 1u;
				inserted = FlightSync_InsertInputFrame(
					playerIndex, (int)timestamp, &input);
				if (inserted != NULL) {
					int localIsHost =
						NetSession_IsLocalHost();

					inserted->awaitingRelay = 1;
					if (localIsHost == 0) {
						inserted->awaitingRelay = 0;
					}
					inserted->inputSource = 1;
				}
			} else {
				g_flightNetScratchPacket.packetType =
					NET_PACKET_PLAYER_ABORT;
				g_flightNetScratchPacket.payloadDwords[0] =
					playerIndex;

				NetSession_SendPacket(
					senderDpid,
					(unsigned int
						 *)&g_flightNetScratchPacket,
					2 * sizeof(int));
			}
			frameDelta = (int)Time_ConsumeElapsedTicks();
			packetState.remoteInputElapsed += frameDelta;
			currentTimestamp += frameDelta;
			continue;
		}
		case NET_PACKET_WORLD_MESSAGE: {
			int frameDelta;
			int playerIndex;

			currentTimestamp += (int)Time_ConsumeElapsedTicks();
			g_flightNetHostTimeoutElapsedTicks = 0;
			++g_flightNetReceivedWorldMessageCount;
			FlightSync_ApplyWorldMessagePacket((uint8_t *)packet);
			if (NetSession_IsLocalHost() != 0) {
				for (playerIndex = 0;
				     playerIndex < PLAYER_COUNT;
				     ++playerIndex) {
					if (playerIndex != g_localPlayer &&
					    g_players[playerIndex]
							    .participationState !=
						    0 &&
					    g_flightNetPeerSilenceTicks
							    [playerIndex] !=
						    -1) {
						g_flightNetPeerSilenceTicks
							[playerIndex] +=
							g_netUpdateIntervalTicks;
						if (g_flightNetPeerSilenceTicks
							    [playerIndex] >
						    PEER_TIMEOUT_TICKS) {
							FlightNet_BroadcastPlayerAbort(
								playerIndex);
							g_flightNetPeerSilenceTicks
								[playerIndex] =
									0;
						}
					}
				}
			}
			if (g_flightMissionState.missionEndPending == 1) {
				if (g_flightNetRecoveryUiActive != 0) {
					g_flightNetRecoveryUiActive = 0;
					FlightAlert_RestoreBoxBackground();
					g_inputTimestamp =
						g_flightNetRecoverySavedInputTimestamp;
				}
				return;
			}
			frameDelta = (int)Time_ConsumeElapsedTicks();
			packetState.worldFrameElapsed += frameDelta;
			currentTimestamp += frameDelta;
			++packetState.worldMessageCount;
			continue;
		}
		case NET_PACKET_PLAYER_DISCONNECTED: {
			int playerIndex = packet[1];

			if (playerIndex >= 0 && playerIndex < PLAYER_COUNT) {
				g_playerConnected[playerIndex] = 0;
			}
			continue;
		}
		case NET_PACKET_WORLD_CHECKSUM: {
			int playerIndex =
				NetSession_FindPlayerSlotByDpid(senderDpid);

			if (g_players[playerIndex].participationState != 0) {
				FlightSync_HandleWorldChecksumPacket(senderDpid,
								     packet);
			}
			continue;
		}
		case NET_PACKET_SESSION_ABORT:
			g_flightNetHostAbortReceived = 1;
			g_flightMissionState.missionEndPending = 1;
			g_players[g_localPlayer].participationState = 0;
			if (g_flightNetRecoveryUiActive != 0) {
				g_flightNetRecoveryUiActive = 0;
				FlightAlert_RestoreBoxBackground();
				g_inputTimestamp =
					g_flightNetRecoverySavedInputTimestamp;
			}
			return;
		case NET_PACKET_RESYNC_CHUNK_ACK: {
			unsigned int chunkIndex;

			g_flightNetWorldStateAckReceivedFlag = 1;
			chunkIndex = (unsigned int)packet[1];
			if (chunkIndex < WORLD_STATE_CHUNK_COUNT) {
				g_flightNetWorldStateChunkAcked[chunkIndex] = 1;
			}
			continue;
		}
		case NET_PACKET_PLAYER_ABORT: {
			int playerIndex = packet[1];

			if (playerIndex >= 0 && playerIndex < PLAYER_COUNT) {
				g_playerAbortFlags[playerIndex] = 1;
			}
			if (playerIndex != g_localPlayer) {
				continue;
			}
			g_flightMissionState.missionEndPending = 1;
			g_players[g_localPlayer].participationState = 0;
			g_playerAbortFlags[g_localPlayer] = 1;
			FlightNet_MarkPilotNetworkPlayerLeft(g_localPlayer);
			if (g_flightNetRecoveryUiActive != 0) {
				g_flightNetRecoveryUiActive = 0;
				FlightAlert_RestoreBoxBackground();
				g_inputTimestamp =
					g_flightNetRecoverySavedInputTimestamp;
			}
			return;
		}
		case NET_PACKET_INPUT_BATCH: {
			const uint8_t *cursor;
			int frameCount;
			int playerIndex;

			playerIndex =
				NetSession_FindPlayerSlotByDpid(senderDpid);
			if (g_players[playerIndex].participationState != 0) {
				if (g_flightNetPeerSilenceTicks[playerIndex] >
				    0) {
					g_flightNetPeerSilenceTicks
						[playerIndex] = 0;
				}
				FlightSync_DiscardPredictedInputFrames(
					playerIndex);
				cursor = (const uint8_t *)packet + sizeof(int);
				frameCount = *cursor++;
				if (frameCount > 0) {
					packetState.decodeValue = frameCount;
					do {
						InputFrame *inserted;
						char timestampCode;
						uint8_t lowCode;
						unsigned int timestamp;

						memset(&input, 0,
						       sizeof(input));
						timestampCode = (int8_t)*cursor;
						lowCode =
							(uint8_t)timestampCode &
							TIMESTAMP_CODE_MASK;
						if (lowCode ==
						    FULL_TIMESTAMP_CODE) {
							timestamp = *(
								const unsigned int
									*)(cursor +
									   1);
							if ((timestampCode &
							     KEY_PRESENT_FLAG) !=
							    0) {
								input.key = cursor
									[5];
								cursor += 6;
							} else {
								cursor += 5;
							}
						} else {
							int previousCode = g_flightNetLastInputTimestampByPlayer
								[playerIndex];
							if ((previousCode &
							     TIMESTAMP_CODE_MASK) >
							    lowCode) {
								previousCode +=
									TIMESTAMP_CODE_MASK +
									1;
							}
							timestamp =
								(unsigned int)
									lowCode |
								((unsigned int)
									 previousCode &
								 ~TIMESTAMP_CODE_MASK);
							if ((timestampCode &
							     KEY_PRESENT_FLAG) !=
							    0) {
								input.key = cursor
									[1];
								cursor += 2;
							} else {
								cursor += 1;
							}
						}
						g_flightNetLastInputTimestampByPlayer
							[playerIndex] =
								(int)timestamp;
						input.axisX =
							(int8_t)(cursor[0] &
								 (uint8_t)~1u);
						input.axisY =
							(int8_t)(cursor[1] &
								 (uint8_t)~1u);
						input.keyMods = cursor[1] & 1u;
						input.keyMods += input.keyMods;
						input.keyMods |= cursor[0] & 1u;
						cursor += 2;
						inserted =
							FlightSync_InsertInputFrame(
								playerIndex,
								(int)timestamp,
								&input);
						if (inserted != NULL) {
							int localIsHost =
								NetSession_IsLocalHost();

							inserted->awaitingRelay =
								1;
							if (localIsHost == 0) {
								inserted->awaitingRelay =
									0;
							}
							inserted->inputSource =
								1;
						}
					} while (--packetState.decodeValue !=
						 0);
				}
			} else {
				g_flightNetScratchPacket.packetType =
					NET_PACKET_PLAYER_ABORT;
				g_flightNetScratchPacket.payloadDwords[0] =
					playerIndex;

				NetSession_SendPacket(
					senderDpid,
					(unsigned int
						 *)&g_flightNetScratchPacket,
					2 * sizeof(int));
			}
			continue;
		}
		case NET_PACKET_RESYNC_NOTICE:
			g_flightNetResyncPlayerDplayId = packet[1];
			continue;
		case NET_PACKET_SERVER_CHECKSUM:
			FlightSync_HandleServerChecksumPacket(
				(uint8_t *)packet);
			FlightNet_SendClockProbeToHost();
			continue;
		case NET_PACKET_ACK:
			if (g_flightNetPendingAckCount != 0) {
				--g_flightNetPendingAckCount;
				if (g_flightNetPendingAckCount == 0) {
					g_flightNetWorldMessageTurnTimestamp =
						0;
					if (g_flightNetRecoveryUiActive != 0) {
						g_flightNetRecoveryUiActive = 0;
						FlightAlert_RestoreBoxBackground();
						g_inputTimestamp =
							g_flightNetRecoverySavedInputTimestamp;
					}
					return;
				}
			}
			continue;
		case NET_PACKET_CLOCK_LEAD:
			g_flightNetClockLeadTicks = packet[1];
			continue;
		case NET_PACKET_STILL_LOADING:
			if (NetSession_GetHostDplayId() == senderDpid) {
				g_flightNetHostTimeoutElapsedTicks = 0;
			} else {
				int playerIndex =
					NetSession_FindPlayerSlotByDpid(
						senderDpid);

				if (g_players[playerIndex].participationState !=
					    0 &&
				    g_flightNetPeerSilenceTicks[playerIndex] >
					    0) {
					g_flightNetPeerSilenceTicks
						[playerIndex] = 0;
				}
			}
			continue;
		case NET_PACKET_CLOCK_PROBE: {
			int adjustment;
			int targetLead;

			g_flightNetScratchPacket.packetType =
				NET_PACKET_CLOCK_PROBE_REPLY;
			g_flightNetScratchPacket.payloadDwords[0] = packet[1];

			NetSession_SendPacket(
				senderDpid,
				(unsigned int *)&g_flightNetScratchPacket,
				PACKET_CLOCK_PROBE_REPLY_SIZE);
			targetLead = packet[2];
			if (g_internetPlayEnabled == 0 ||
			    g_flightNetSmallSessionPlayerThreshold >
				    g_activeFlightPlayerCount) {
				targetLead >>= 1;
			}
			if (g_flightNetClockLeadTicks < targetLead) {
				adjustment = (targetLead -
					      g_flightNetClockLeadTicks) >>
					     1;
				if (adjustment == 0) {
					adjustment = 1;
				}
				g_flightNetClockLeadTicks += adjustment;
			} else if (g_flightNetClockLeadTicks > targetLead) {
				adjustment = (g_flightNetClockLeadTicks -
					      targetLead) >>
					     1;
				if (adjustment == 0) {
					adjustment = 1;
				}
				g_flightNetClockLeadTicks -= adjustment;
			}
			continue;
		}
		case NET_PACKET_CLOCK_PROBE_REPLY:
			if (NetSession_IsLocalHost() == 0 &&
			    packet[1] == g_flightNetClockProbeTimestamp) {
				int adjustment;
				int targetLead;

				targetLead = g_flightNetClockAdjustAccumTicks;
				targetLead += g_inputTimestamp;
				targetLead -= packet[1];
				targetLead += CLOCK_PROBE_BIAS_TICKS;

				if (targetLead < CLOCK_PROBE_LIMIT_TICKS) {
					if (g_flightNetClockLeadTicks <
					    targetLead) {
						adjustment =
							(targetLead -
							 g_flightNetClockLeadTicks) >>
							1;
						if (adjustment == 0) {
							adjustment = 1;
						}
						g_flightNetClockLeadTicks +=
							adjustment;
					} else if (g_flightNetClockLeadTicks >
						   targetLead) {
						adjustment =
							(g_flightNetClockLeadTicks -
							 targetLead) >>
							1;
						if (adjustment == 0) {
							adjustment = 1;
						}
						g_flightNetClockLeadTicks -=
							adjustment;
					}
				}
			}
			continue;
		case NET_PACKET_RESYNC_CHECKSUMS:
			g_flightNetRemoteResyncChecksumsReceivedFlag = 1;
			memcpy(g_flightNetRemoteResyncChecksums, &packet[1],
			       sizeof(g_flightNetRemoteResyncChecksums[0]) *
				       RESYNC_CHECKSUM_COUNT);
			continue;
		case NET_PACKET_RESYNC_REQUEST:
		case NET_PACKET_RESYNC_APPLY:
		case NET_PACKET_RESYNC_CHUNK:
			if ((unsigned int)packet[1] !=
			    g_flightNetWorldChecksumEpoch) {
				continue;
			}
			FlightNet_HandleWorldStateResyncPacket(packet);
			if (g_flightNetHostTimeoutElapsedTicks >
				    PEER_TIMEOUT_TICKS ||
			    g_players[g_localPlayer].participationState == 0) {
				if (g_flightNetRecoveryUiActive != 0) {
					g_flightNetRecoveryUiActive = 0;
					FlightAlert_RestoreBoxBackground();
					g_inputTimestamp =
						g_flightNetRecoverySavedInputTimestamp;
				}
				return;
			}
			continue;
		default:
			continue;
		}
	}

	if (g_flightNetRecoveryUiActive != 0) {
		g_flightNetRecoveryUiActive = 0;
		FlightAlert_RestoreBoxBackground();
		currentTimestamp = g_flightNetRecoverySavedInputTimestamp;
		g_inputTimestamp = currentTimestamp;
	}
	currentTimestamp += (int)Time_ConsumeElapsedTicks();
	g_inputTimestamp = currentTimestamp;
	{
		int allInputElapsed =
			g_inputTimestamp - packetState.startTimestamp;
		int miscellaneousElapsed = allInputElapsed -
					   packetState.serverSendElapsed -
					   packetState.remoteInputElapsed -
					   packetState.receiveElapsed -
					   packetState.worldFrameElapsed;

		sprintf(statusText,
			"RcvMsg:%-2d In2Svr:%-2d SvrSnd:%-2d SvrFrm:%-2d NumFrm:%-2d AllPIN:%-2d Misc:%-2d\n",
			packetState.receiveElapsed,
			packetState.remoteInputElapsed,
			packetState.serverSendElapsed,
			packetState.worldFrameElapsed,
			packetState.worldMessageCount, allInputElapsed,
			miscellaneousElapsed);
	}
#endif
}

/* Samples this player's controls into g_currentInputFrame and files it in this
 * player's input history at g_inputTimestamp, not awaiting relay. The modern
 * build adds roll and throttle, sends nothing, and returns 0. The original
 * first adds the ticks elapsed to g_inputTimestamp and encodes the input: one
 * code byte with the timestamp's low 7 bits, or 127 and the full 4-byte
 * timestamp when g_lastSentInputTimestamp is 0, after a gap of 127 ticks or
 * more or a backward step, or when the previous input came more than 236 ticks
 * after the last full one (g_lastKeyframeTime); the code's top bit adds a key
 * byte; then the X and Y bytes, each carrying a key-modifier bit in its low
 * bit. With other players it logs the input to inputlog.txt through
 * g_inputLogFile when g_inputLogEnabled is 1. Without internet play it sends
 * the packet to each connected player still flying, itself included. With
 * internet play it appends the record to g_flightNetInputBatchPacket and, once
 * more than g_flightNetInputBatchIntervalTicks have passed, sends the batch to
 * the host, and in sessions under g_flightNetSmallSessionPlayerThreshold also
 * to each other connected player, then empties it. Also writes
 * g_lastSentInputTimestamp, g_flightNetScratchPacket, g_flightNetInputBatchLen
 * and g_flightNetLastInputBatchSendTime. Returns Flight_PumpWindowMessages's
 * result. Does not check that the batch has room for another record. */
// FUNCTION: XVT 0x464900
int32_t FlightNet_SampleLocalInput(void)
{
#ifdef XVT_MODERN
	InputFrame *inserted;

	FlightInput_Read(-2);
	memset(&g_currentInputFrame, 0, sizeof g_currentInputFrame);
	g_currentInputFrame.key = (uint8_t)g_actionKey;
	g_currentInputFrame.axisX = (int8_t)(g_ctrlAxisX & 0xfe);
	g_currentInputFrame.axisY = (int8_t)(g_ctrlAxisY & 0xfe);
	g_currentInputFrame.axisR = (int8_t)(g_xvtControlRoll & 0xfe);
	g_currentInputFrame.keyMods = (uint8_t)(g_keyMods & 3u);
	XvtFlightControls_SampleThrottle(&g_currentInputFrame);
	inserted = FlightSync_InsertInputFrame(g_localPlayer, g_inputTimestamp,
					       &g_currentInputFrame);
	if (inserted != NULL) {
		inserted->awaitingRelay = 0;
		inserted->inputSource = 1;
	}
	return Flight_PumpWindowMessages();
#else
	static uint8_t encodedTime;
	int packetLength;
	int directPlayerIndex;
	uint8_t *packetBytes = (uint8_t *)&g_flightNetScratchPacket;
	InputFrame *inserted;

	FlightInput_Read(-2);
	g_currentInputFrame.key = (uint8_t)g_actionKey;
	g_currentInputFrame.axisX = (int8_t)(g_ctrlAxisX & 0xfe);
	g_currentInputFrame.axisY = (int8_t)(g_ctrlAxisY & 0xfe);
	g_currentInputFrame.keyMods = (uint8_t)(g_keyMods & 3u);
	g_flightNetScratchPacket.packetType = NET_PACKET_REMOTE_INPUT;
	g_inputTimestamp += Time_ConsumeElapsedTicks();

	/* Until the packet bytes are laid out, packetLength holds the 7-bit timestamp code: the low bits of
	 * g_inputTimestamp, or 127 when a full timestamp is sent. */
	packetLength = g_inputTimestamp - g_lastSentInputTimestamp;
	if (packetLength >= 127 || packetLength < 0 ||
	    g_lastSentInputTimestamp == 0) {
		packetLength = 127;
	} else {
		packetLength = g_inputTimestamp & 0x7f;
	}
	if (g_lastSentInputTimestamp - g_lastKeyframeTime >
	    SIMULATION_TICKS_PER_SECOND) {
		packetLength = 127;
	}
	if (packetLength == 127) {
		g_lastKeyframeTime = g_inputTimestamp;
		packetBytes[4] = 127;
		memcpy(&packetBytes[5], &g_inputTimestamp,
		       sizeof(g_inputTimestamp));
		packetLength = 9;
	} else {
		packetBytes[4] = (uint8_t)packetLength;
		packetLength = 5;
	}
	g_lastSentInputTimestamp = g_inputTimestamp;
	if (g_currentInputFrame.key != 0) {
		encodedTime = packetBytes[4];
		++packetLength;
		packetBytes[4] = (uint8_t)(encodedTime | 0x80u);
		packetBytes[packetLength - 1] = g_currentInputFrame.key;
	}
	packetBytes[packetLength] = (uint8_t)g_currentInputFrame.axisX;
	packetBytes[packetLength + 1] = (uint8_t)g_currentInputFrame.axisY;
	if ((g_currentInputFrame.keyMods & 1u) != 0) {
		packetBytes[packetLength] |= 1u;
	}
	if ((g_currentInputFrame.keyMods & 2u) != 0) {
		packetBytes[packetLength + 1] |= 1u;
	}
	packetLength += 2;

	if (g_flightPlayerCount > 1) {
		if (g_inputLogEnabled == 1) {
			if (g_inputLogFile == NULL) {
				g_inputLogFile =
					File_RawOpen("inputlog.txt", "w");
			}
			if (g_inputLogFile != NULL) {
				File_Printf(g_inputLogFile,
					    "%8x %2x %2x %2x %2x\n",
					    g_flightNetScratchPacket
						    .payloadDwords[0],
					    g_currentInputFrame.key,
					    (uint8_t)g_currentInputFrame.axisX,
					    (uint8_t)g_currentInputFrame.axisY,
					    g_currentInputFrame.keyMods);
				File_Flush(g_inputLogFile);
			}
		}
		if (g_internetPlayEnabled == 0) {
			for (directPlayerIndex = 0; directPlayerIndex < 8;
			     ++directPlayerIndex) {
				if (g_players[directPlayerIndex]
						    .participationState != 0 &&
				    (g_playerConnected[directPlayerIndex] !=
					     0 ||
				     directPlayerIndex == g_localPlayer)) {

					NetSession_SendPacket(
						g_players[directPlayerIndex]
							.network.directPlayId,
						(unsigned int
							 *)&g_flightNetScratchPacket,
						packetLength);
				}
			}
		} else {
			int batchPlayerIndex;
			uint8_t *batchFrameCount;

			batchFrameCount =
				&g_flightNetInputBatchPacket.frameCount;
			g_flightNetInputBatchPacket.packetType =
				NET_PACKET_INPUT_BATCH;
			++*batchFrameCount;
			memcpy(&((uint8_t *)&g_flightNetInputBatchPacket)
				       [g_flightNetInputBatchLen],
			       g_flightNetScratchPacket.payloadDwords,
			       (size_t)(packetLength - 4));
			g_flightNetInputBatchLen += packetLength - 4;
			if ((unsigned int)(g_inputTimestamp -
					   g_flightNetLastInputBatchSendTime) >
			    (unsigned int)g_flightNetInputBatchIntervalTicks) {
				g_flightNetLastInputBatchSendTime =
					g_inputTimestamp;

				NetSession_SendPacket(
					NetSession_GetHostDplayId(),
					(unsigned int
						 *)&g_flightNetInputBatchPacket,
					g_flightNetInputBatchLen);
				if (g_flightNetSmallSessionPlayerThreshold >
				    g_activeFlightPlayerCount) {
					for (batchPlayerIndex = 0;
					     batchPlayerIndex < 8;
					     ++batchPlayerIndex) {
						if (g_players[batchPlayerIndex]
								    .participationState !=
							    0 &&
						    batchPlayerIndex !=
							    g_localPlayer &&
						    NetSession_GetHostDplayId() !=
							    g_players[batchPlayerIndex]
								    .network
								    .directPlayId &&
						    g_playerConnected
								    [batchPlayerIndex] !=
							    0) {

							NetSession_SendPacket(
								g_players[batchPlayerIndex]
									.network
									.directPlayId,
								(unsigned int
									 *)&g_flightNetInputBatchPacket,
								g_flightNetInputBatchLen);
						}
					}
				}
				g_flightNetInputBatchLen = 5;
				g_flightNetInputBatchPacket.packetType =
					NET_PACKET_INPUT_BATCH;
				g_flightNetInputBatchPacket.frameCount = 0;
			}
		}
	}

	inserted = FlightSync_InsertInputFrame(g_localPlayer, g_inputTimestamp,
					       &g_currentInputFrame);
	if (inserted != NULL) {
		inserted->awaitingRelay = 0;
		inserted->inputSource = 1;
	}
	return Flight_PumpWindowMessages();
#endif
}

/* Restarts the host's world-message schedule: zeroes
 * g_flightNetWorldMessageTurnTimestamp and
 * g_flightNetLastSentWorldMessageTimestamp, and sets
 * g_unusedFlightNetMissionStartAckInitFlag, which nothing reads. */
// FUNCTION: XVT 0x464C60
void FlightNet_ResetWorldMessageSchedule(void)
{
	g_unusedFlightNetMissionStartAckInitFlag = 1;
	g_flightNetWorldMessageTurnTimestamp = 0;
	g_flightNetLastSentWorldMessageTimestamp = 0;
}

/* Says whether the host should send a world message now, and if so moves
 * g_flightNetWorldMessageTurnTimestamp on by g_netUpdateIntervalTicks. Returns
 * 0 while acks are pending or the recovery alert is up, and until a full
 * interval has passed since the turn timestamp, which starts at the adjusted
 * time (inputTimestamp plus g_flightNetClockAdjustAccumTicks) plus an eighth of
 * g_flightNetClockLeadTicks. Returns 1 when more than 5 intervals have passed,
 * or when the latest input awaiting relay of every player still flying is later
 * than the last world message's tick plus one interval; else 0. Only the
 * original build calls this; its modern arm hands off to
 * XvtFlightNetwork_TakeWorldSendTurn. */
// FUNCTION: XVT 0x464C80
int FlightNet_TakeWorldMessageTurn(int inputTimestamp)
{
#ifdef XVT_MODERN
	return XvtFlightNetwork_TakeWorldSendTurn(inputTimestamp);
#else

	int adjustedTimestamp;
	int elapsedTimestamp;
	int oldestInputTimestamp;
	int playerIdx;
	uint8_t *participationStatePtr;
	const uint8_t *playersEnd;

	if (g_flightNetPendingAckCount != 0) {
		return 0;
	}
	if (g_flightNetRecoveryUiActive != 0) {
		return 0;
	}

	adjustedTimestamp = inputTimestamp;
	adjustedTimestamp += g_flightNetClockAdjustAccumTicks;
	if (g_flightNetWorldMessageTurnTimestamp == 0) {
		g_flightNetWorldMessageTurnTimestamp =
			adjustedTimestamp + (g_flightNetClockLeadTicks >> 3);
	}
	elapsedTimestamp =
		adjustedTimestamp - g_flightNetWorldMessageTurnTimestamp;
	if (elapsedTimestamp < g_netUpdateIntervalTicks) {
		return 0;
	}
	if (elapsedTimestamp > 5 * g_netUpdateIntervalTicks) {
		g_flightNetWorldMessageTurnTimestamp +=
			g_netUpdateIntervalTicks;
		return 1;
	}

	oldestInputTimestamp = 0x7FFFFFFF;
	playerIdx = 0;
	participationStatePtr = &g_players[0].participationState;
	playersEnd = (const uint8_t *)(g_players + 8);
	while (participationStatePtr < playersEnd) {
		if (*participationStatePtr != 0) {
			InputFrame *inputFrame;

			inputFrame = FlightSync_FindLastUnrelayedInputFrame(
				playerIdx);
			if (inputFrame == NULL) {
				oldestInputTimestamp = 0;
				break;
			}
			if (inputFrame->timestamp < oldestInputTimestamp) {
				oldestInputTimestamp = inputFrame->timestamp;
			}
		}
		participationStatePtr += sizeof(PlayerData);
		++playerIdx;
	}

	if (g_flightNetLastSentWorldMessageTimestamp +
		    g_netUpdateIntervalTicks <
	    oldestInputTimestamp) {
		g_flightNetWorldMessageTurnTimestamp +=
			g_netUpdateIntervalTicks;
		return 1;
	}
	return 0;

#endif
}

/* Sends the host's world message: every input awaiting relay, from each player
 * still flying, up to the message's tick. The original counts the call in
 * g_flightNetSentWorldMessageCount and stops there in a solo flight. The tick
 * is g_flightNetLastSentWorldMessageTimestamp plus g_netUpdateIntervalTicks,
 * stored back there; the ticks since g_serverTickTime add up in
 * g_flightNetChecksumRequestAccumTicks, and past 472 the tick's top bit asks
 * every player for a world checksum, g_flightNetWorldChecksumPeerStatus is
 * cleared and the sum restarts. Per player: a record count byte, then per input
 * a code byte holding the ticks before the message tick (0-124 as is; 125 and 1
 * more byte; 126 and 2 bytes; 127 and the full 4-byte timestamp), its top bit
 * adding a key byte, then the X and Y bytes with a key-modifier bit in each low
 * bit. Marks each input sent as relayed, and skips the rest of a player's
 * inputs once one more might not fit in 504 bytes less the player count. Sends
 * to DirectPlay id 0, which reaches all players. With g_inputLogEnabled at 1 it
 * writes serverlog.txt through g_flightNetServerLogFile, but reads the records
 * as fixed 10-byte entries the packet does not hold. inputTimestamp is unused.
 * Writes g_flightNetScratchPacket. Only the original build calls this; its
 * modern arm hands off to XvtFlightNetwork_SendWorld. */
// FUNCTION: XVT 0x464D70
void FlightNet_BroadcastWorldMessage(int inputTimestamp)
{
#ifdef XVT_MODERN
	(void)inputTimestamp;
	XvtFlightNetwork_SendWorld();
#else
	enum {
		PLAYER_SLOT_COUNT = 8,
		CHECKSUM_REQUEST_INTERVAL_TICKS = 472,
		PACKET_PLAYER_COUNT_OFFSET = 8,
		PACKET_HEADER_SIZE = 9,
		BANDWIDTH_BYTES_PER_SECOND = 3000,
		MAX_PACKET_PAYLOAD = 508,
		MAX_ENCODED_INPUT_RECORD_SIZE = 8,
		PACKET_STREAM_LIMIT = 504,
		FULL_TIMESTAMP_DELTA = 65536,
		SHORT_DELTA_THRESHOLD = 368,
		BYTE_DELTA_THRESHOLD = 125,
		FULL_TIMESTAMP_CODE = 127,
		SHORT_DELTA_CODE = 126,
		BYTE_DELTA_CODE = 125,
		KEY_PRESENT_FLAG = 0x80,
		DELTA_CODE_MASK = 0x7F,
		LOGGED_RECORD_SIZE = 10
	};

	uint8_t *packetBytes;
	uint8_t *dest;
	int currentTick;
	int packetLength;
	int playerIndex;
	int frameIndex;
	InputFrame *frame;
	int code;
	uint8_t *recordCount;
	int bandwidthBudget;
	unsigned int bytesPerPlayer;
	unsigned int maxRecordsPerPlayer;
	const uint8_t *logCursor;
	int loggedCount;
	int logRecordIndex;

	(void)inputTimestamp;

	++g_flightNetSentWorldMessageCount;
	if (g_flightPlayerCount <= 1) {
		return;
	}
	currentTick = g_flightNetLastSentWorldMessageTimestamp +
		      g_netUpdateIntervalTicks;
	g_flightNetLastSentWorldMessageTimestamp = currentTick;
	g_flightNetChecksumRequestAccumTicks += currentTick - g_serverTickTime;
	g_flightNetScratchPacket.payloadDwords[0] = currentTick;
	g_flightNetScratchPacket.packetType = NET_PACKET_WORLD_MESSAGE;
	if (g_flightNetChecksumRequestAccumTicks >
	    CHECKSUM_REQUEST_INTERVAL_TICKS) {
		g_flightNetChecksumRequestAccumTicks = 0;
		g_flightNetScratchPacket.payloadDwords[0] =
			currentTick | (int)0x80000000u;
		memset(g_flightNetWorldChecksumPeerStatus, 0,
		       sizeof(g_flightNetWorldChecksumPeerStatus));
	}

	packetBytes = (uint8_t *)&g_flightNetScratchPacket;
	packetBytes[PACKET_PLAYER_COUNT_OFFSET] = 0;
	bandwidthBudget = g_netUpdateIntervalTicks *
			  BANDWIDTH_BYTES_PER_SECOND /
			  SIMULATION_TICKS_PER_SECOND;
	if (bandwidthBudget > MAX_PACKET_PAYLOAD) {
		bandwidthBudget = MAX_PACKET_PAYLOAD;
	}
	bytesPerPlayer = (bandwidthBudget - PACKET_HEADER_SIZE -
			  g_activeFlightPlayerCount) /
			 g_activeFlightPlayerCount;
	maxRecordsPerPlayer = bytesPerPlayer / LOGGED_RECORD_SIZE;
	/* The original computes this budget but limits packets by encoded byte count. */
	(void)maxRecordsPerPlayer;
	dest = &packetBytes[PACKET_HEADER_SIZE];
	packetLength = PACKET_HEADER_SIZE;
	for (playerIndex = 0; playerIndex < PLAYER_SLOT_COUNT; ++playerIndex) {
		if (g_players[playerIndex].participationState == 0) {
			continue;
		}
		++packetBytes[PACKET_PLAYER_COUNT_OFFSET];
		recordCount = dest;
		*dest++ = 0;
		++packetLength;
		for (frameIndex = 0;
		     frameIndex < g_inputFrameCount[playerIndex];
		     ++frameIndex) {
			frame = &g_inputHistory[playerIndex][frameIndex];
			if (frame->awaitingRelay == 0 ||
			    frame->timestamp > currentTick) {
				continue;
			}
			if ((int)(dest - packetBytes) +
				    MAX_ENCODED_INPUT_RECORD_SIZE >
			    PACKET_STREAM_LIMIT - g_activeFlightPlayerCount) {
				break;
			}
			++*recordCount;
			code = currentTick - frame->timestamp;
			if (code >= FULL_TIMESTAMP_DELTA) {
				code = FULL_TIMESTAMP_CODE;
			} else if (code >= SHORT_DELTA_THRESHOLD) {
				code = SHORT_DELTA_CODE;
			} else if (code >= BYTE_DELTA_THRESHOLD) {
				code = BYTE_DELTA_CODE;
			}
			if (frame->input.key != 0) {
				code |= KEY_PRESENT_FLAG;
			}
			*dest++ = (uint8_t)code;
			++packetLength;
			if ((code & DELTA_CODE_MASK) == FULL_TIMESTAMP_CODE) {
				*(int *)dest = frame->timestamp;
				dest += sizeof(int);
				packetLength += sizeof(int);
			} else if ((code & DELTA_CODE_MASK) ==
				   SHORT_DELTA_CODE) {
				*(uint16_t *)dest =
					(uint16_t)(currentTick -
						   frame->timestamp);
				dest += sizeof(uint16_t);
				packetLength += sizeof(uint16_t);
			} else if ((code & DELTA_CODE_MASK) ==
				   BYTE_DELTA_CODE) {
				*dest++ = (uint8_t)(currentTick -
						    frame->timestamp -
						    BYTE_DELTA_THRESHOLD);
				++packetLength;
			}
			if ((code & KEY_PRESENT_FLAG) != 0) {
				*dest++ = frame->input.key;
				++packetLength;
			}
			dest[0] = (uint8_t)frame->input.axisX;
			dest[1] = (uint8_t)frame->input.axisY;
			dest[0] &= (uint8_t)~1u;
			dest[1] &= (uint8_t)~1u;
			dest[0] |= frame->input.keyMods & 1u;
			dest[1] |= (frame->input.keyMods & 2u) >> 1;
			dest += 2;
			packetLength += 2;
			frame->awaitingRelay = 0;
		}
	}

	NetSession_SendPacket(0, (unsigned int *)&g_flightNetScratchPacket,
			      packetLength);
	if (g_inputLogEnabled == 1) {
		if (g_flightNetServerLogFile == NULL) {
			g_flightNetServerLogFile =
				File_RawOpen("serverlog.txt", "w");
		}
		if (g_flightNetServerLogFile != NULL) {
			File_Printf(g_flightNetServerLogFile, "%8x\n",
				    g_flightNetScratchPacket.payloadDwords[0]);
			logCursor = &packetBytes[PACKET_HEADER_SIZE];
			for (playerIndex = 0; playerIndex < PLAYER_SLOT_COUNT;
			     ++playerIndex) {
				if (g_players[playerIndex].participationState ==
				    0) {
					continue;
				}
				loggedCount = *logCursor++;
				File_Printf(g_flightNetServerLogFile, " %2x\n",
					    loggedCount);
				for (logRecordIndex = 0;
				     logRecordIndex < loggedCount;
				     ++logRecordIndex) {
					const FlightInputFrameRecord *loggedInput =
						(const FlightInputFrameRecord
							 *)(logCursor +
							    sizeof(int));

					File_Printf(g_flightNetServerLogFile,
						    "  %8x %2x %2x %2x %2x\n",
						    *(const int *)logCursor,
						    loggedInput->key,
						    (uint8_t)loggedInput->axisX,
						    (uint8_t)loggedInput->axisY,
						    loggedInput->keyMods);
					logCursor =
						(const uint8_t *)(loggedInput +
								  1);
				}
			}
			File_Flush(g_flightNetServerLogFile);
		}
	}
#endif
}

/* Sends the host this player's world checksum: a WORLD_CHECKSUM packet holding
 * g_serverTickTime, checksumDwordCount checksum words, then as many region
 * lengths. The modern build adds one zero word after them, which the host's
 * FlightSync_HandleWorldChecksumPacket reads as a request to resend the world
 * when it is 1, and sends through XvtFlightNetwork_SendPacket. Writes
 * g_flightNetScratchPacket. Returns the send function's result. Does not check
 * that the words fit in the packet. */
// FUNCTION: XVT 0x4650E0
int FlightNet_SendWorldChecksumToHost(const int *worldChecksum,
				      const int *regionLengths,
				      int checksumDwordCount)
{
	g_flightNetScratchPacket.packetType = NET_PACKET_WORLD_CHECKSUM;
	g_flightNetScratchPacket.payloadDwords[0] = g_serverTickTime;
	memcpy(&g_flightNetScratchPacket.payloadDwords[1], worldChecksum,
	       (size_t)checksumDwordCount * sizeof(int));
	memcpy(&g_flightNetScratchPacket.payloadDwords[checksumDwordCount + 1],
	       regionLengths, (size_t)checksumDwordCount * sizeof(int));
#ifdef XVT_MODERN
	g_flightNetScratchPacket.payloadDwords[checksumDwordCount * 2 + 1] = 0;
	return XvtFlightNetwork_SendPacket(
		NetSession_GetHostDplayId(),
		(unsigned *)&g_flightNetScratchPacket,
		checksumDwordCount * 8 + 12);
#else
	return NetSession_SendPacket(NetSession_GetHostDplayId(),
				     (unsigned int *)&g_flightNetScratchPacket,
				     checksumDwordCount * 8 + 8);
#endif
}

/* Sends every active player, this one included, the host's world checksum: a
 * SERVER_CHECKSUM packet holding g_serverTickTime, checksumDwordCount checksum
 * words, then as many region lengths. Writes g_flightNetScratchPacket. The
 * modern build sends through XvtFlightNetwork_Broadcast. Returns the
 * broadcast's result. Does not check that the words fit in the packet or that
 * this player is the host. */
// FUNCTION: XVT 0x465150
int FlightNet_BroadcastWorldChecksum(const int *worldChecksum,
				     const int *regionLengths,
				     int checksumDwordCount)
{
	g_flightNetScratchPacket.packetType = NET_PACKET_SERVER_CHECKSUM;
	g_flightNetScratchPacket.payloadDwords[0] = g_serverTickTime;
	memcpy(&g_flightNetScratchPacket.payloadDwords[1], worldChecksum,
	       (size_t)checksumDwordCount * sizeof(int));
	memcpy(&g_flightNetScratchPacket.payloadDwords[checksumDwordCount + 1],
	       regionLengths, (size_t)checksumDwordCount * sizeof(int));
	return
#ifdef XVT_MODERN
		XvtFlightNetwork_Broadcast
#else
		NetSession_BroadcastPacketToPlayers
#endif
		((unsigned int *)&g_flightNetScratchPacket,
		 checksumDwordCount * 8 + 8);
}

/* Tells the player a resync went to that it may apply the world: sends a
 * RESYNC_APPLY with g_flightNetWorldChecksumEpoch, worldStateSize and
 * g_inputTimestamp, then waits for its ack in up to 10 passes of 236 ticks,
 * reading packets; a chunk ack restarts the pass, ESC ends the wait, and every
 * 236 ticks waited sends all players a STILL_LOADING. With no ack it tells all
 * players that player has aborted. Then sets g_inputTimestamp to
 * g_flightNetClockLeadTicks plus g_serverTickTime and sends all players a
 * RESYNC_NOTICE of 0, which ends the resync. Writes g_flightNetPendingAckCount,
 * g_flightNetWorldStateAckReceivedFlag and g_flightNetScratchPacket. Only the
 * original build calls this; its modern arm hands off to
 * XvtResync_BeginApply. */
// FUNCTION: XVT 0x4651F0
void FlightNet_SendWorldStateResyncApplyRequest(int directPlayId,
						int worldStateSize)
{
#ifdef XVT_MODERN
	XvtResync_BeginApply(directPlayId, worldStateSize);
#else
	enum {
		RESYNC_APPLY_SIZE = 4 * sizeof(int),
		RESYNC_NOTICE_SIZE = 2 * sizeof(int),
		ACK_WAIT_TICKS = 236,
		ACK_RETRY_COUNT = 10
	};

	int retriesRemaining;
	int stillLoadingElapsed;

	g_flightNetScratchPacket.payloadDwords[0] =
		(int)g_flightNetWorldChecksumEpoch;
	g_flightNetScratchPacket.payloadDwords[1] = worldStateSize;
	g_flightNetScratchPacket.payloadDwords[2] = g_inputTimestamp;
	g_flightNetScratchPacket.packetType = NET_PACKET_RESYNC_APPLY;

	NetSession_SendPacket(directPlayId,
			      (unsigned int *)&g_flightNetScratchPacket,
			      RESYNC_APPLY_SIZE);
	Time_ConsumeElapsedTicks();

	for (retriesRemaining = ACK_RETRY_COUNT, stillLoadingElapsed = 0;
	     retriesRemaining != 0; --retriesRemaining) {
		int passStartTimestamp;

		g_flightNetPendingAckCount = 1;
		passStartTimestamp = g_inputTimestamp;
		while ((unsigned int)(g_inputTimestamp - passStartTimestamp) <
		       (unsigned int)ACK_WAIT_TICKS) {
			if (FlightInput_HasKeyReady() != 0 &&
			    FlightInput_GetNextKey() == FLIGHT_KEY_ESCAPE) {
				g_inputTimestamp += ACK_WAIT_TICKS;
				retriesRemaining = 1;
				break;
			}

			FlightNet_ProcessIncomingPackets();
			g_inputTimestamp += (int)Time_ConsumeElapsedTicks();
			if (g_flightNetWorldStateAckReceivedFlag != 0) {
				g_flightNetWorldStateAckReceivedFlag = 0;
				g_inputTimestamp = passStartTimestamp;
			}
			if (g_flightNetPendingAckCount == 0) {
				break;
			}
		}

		stillLoadingElapsed += g_inputTimestamp - passStartTimestamp;
		if (stillLoadingElapsed >= ACK_WAIT_TICKS) {
			stillLoadingElapsed = 0;
			g_flightNetScratchPacket.packetType =
				NET_PACKET_STILL_LOADING;

			NetSession_BroadcastPacketToPlayers(
				(unsigned int *)&g_flightNetScratchPacket,
				sizeof(int));
		}
		if (g_flightNetPendingAckCount == 0) {
			break;
		}
	}

	if (g_flightNetPendingAckCount == 1) {
		int playerIndex = NetSession_FindPlayerSlotByDpid(directPlayId);

		FlightNet_BroadcastPlayerAbort(playerIndex);
		g_inputTimestamp += (int)Time_ConsumeElapsedTicks();
		g_flightNetPendingAckCount = 0;
	} else {
		g_inputTimestamp += (int)Time_ConsumeElapsedTicks();
	}

	g_inputTimestamp = g_flightNetClockLeadTicks + g_serverTickTime;
	g_flightNetScratchPacket.packetType = NET_PACKET_RESYNC_NOTICE;
	g_flightNetScratchPacket.payloadDwords[0] = 0;

	NetSession_BroadcastPacketToPlayers(
		(unsigned int *)&g_flightNetScratchPacket, RESYNC_NOTICE_SIZE);
#endif
}

/* Resends the world state to a player whose checksum did not match. Shows the
 * communication-failure alert with the player's name, tells all players a
 * resync for that DirectPlay id has begun, and sends the player a
 * RESYNC_REQUEST with the object presence map. It then waits up to 10 passes of
 * 236 ticks for the player's segment checksums, sending it and all players a
 * STILL_LOADING every 236 ticks; ESC ends the wait. With no answer it tells all
 * players that player has aborted and returns 0. Otherwise it sends only the
 * segments whose checksums differ from g_flightNetLocalResyncChecksums, as
 * RESYNC_CHUNK packets of (offset, size, bytes) records ended by an offset of
 * -1, in batches of 16 that the player must ack; it always sends a last chunk,
 * even an empty one. Returns 0 when an ack wait fails, else 1. Writes
 * g_flightNetWorldStateChunkPackets, g_flightNetWorldStateChunkAcked,
 * g_flightNetRemoteResyncChecksumsReceivedFlag, g_flightNetPendingAckCount (1
 * while busy, 0 after) and g_flightNetScratchPacket, and adds the ticks elapsed
 * on entry to g_inputTimestamp. Only the original build calls this; its modern
 * arm hands off to XvtResync_BeginSend. */
// FUNCTION: XVT 0x465390
int FlightNet_SendWorldStateResyncToPlayer(int directPlayId,
					   uint8_t *worldState,
					   int worldStateSize)
{
#ifdef XVT_MODERN
	return XvtResync_BeginSend(directPlayId, worldState, worldStateSize);
#else
	enum {
		CHECKSUM_RETRY_COUNT = 10,
		CHECKSUM_POLL_INTERVAL_TICKS = 236,
		CHUNK_RECORD_HEADER_SIZE =
			sizeof(FlightNetWorldStateChunkRecordHeader),
		CHUNK_FREE_BYTES =
			sizeof(g_flightNetWorldStateChunkPackets[0].payload) -
			CHUNK_RECORD_HEADER_SIZE,
		CHUNK_FLUSH_THRESHOLD = 8 * sizeof(int),
		CHUNK_FINAL_SEND_THRESHOLD =
			sizeof(g_flightNetWorldStateChunkPackets[0].payload) -
			sizeof(int),
		CHUNK_PACKET_SEND_BASE_SIZE =
			sizeof(FlightNetWorldStateChunkPacket) - sizeof(int),
		CHUNK_BATCH_SIZE = sizeof(g_flightNetWorldStateChunkPackets) /
				   sizeof(g_flightNetWorldStateChunkPackets[0]),
		ALERT_BACKGROUND_COLOR = 0x30,
	};

	int stillLoadingElapsed;
	int alertToggle;
	char statusText[256];
	int chunkSlot;
	int segmentIndex;
	int worldOffset;
	int packetFreeBytes;
	/* Holds in turn: ticks consumed on entry, presence map byte size, segment checksum count. */
	int buildResult;
	int segmentSize;
	int result;
	uint8_t *payload;
	char *playerName;

	result = 1;
	buildResult = Time_ConsumeElapsedTicks();
	stillLoadingElapsed = 0;
	g_inputTimestamp += buildResult;
	FlightAlert_SaveBoxBackground();

	strcpy(statusText,
	       g_strDiskIoMessages[DISK_IO_STR_COM_FAILURE_SENDING]);
	playerName = NetSession_GetPlayerName(
		NetSession_FindPlayerSlotByDpid(directPlayId));
	if (playerName != NULL) {
		strcat(statusText, playerName);
	}
	FlightAlert_DrawBox(1, statusText, ALERT_BACKGROUND_COLOR);

	g_flightNetScratchPacket.packetType = NET_PACKET_RESYNC_NOTICE;
	g_flightNetScratchPacket.payloadDwords[0] = directPlayId;

	NetSession_BroadcastPacketToPlayers(
		(unsigned int *)&g_flightNetScratchPacket, 2 * sizeof(int));

	g_flightNetScratchPacket.packetType = NET_PACKET_RESYNC_REQUEST;
	g_flightNetScratchPacket.payloadDwords[0] =
		(int)g_flightNetWorldChecksumEpoch;
	buildResult = Flight_BuildWorldStateObjectPresenceMap(
		(uint8_t *)&g_flightNetScratchPacket.payloadDwords[1],
		worldState);

	NetSession_SendPacket(directPlayId,
			      (unsigned int *)&g_flightNetScratchPacket,
			      buildResult + 2 * sizeof(int));

	{
		int retryCount;

		g_flightNetPendingAckCount = 1;
		alertToggle = 0;
		retryCount = CHECKSUM_RETRY_COUNT;
		do {
			int elapsedThisPass;
			int savedInputTimestamp;

			elapsedThisPass = 0;
			g_flightNetRemoteResyncChecksumsReceivedFlag = 0;
			while (elapsedThisPass < CHECKSUM_POLL_INTERVAL_TICKS) {
				if (FlightInput_HasKeyReady() != 0 &&
				    FlightInput_GetNextKey() ==
					    FLIGHT_KEY_ESCAPE) {
					retryCount = 1;
					elapsedThisPass =
						CHECKSUM_POLL_INTERVAL_TICKS;
					break;
				}

				savedInputTimestamp = g_inputTimestamp;
				FlightNet_ProcessIncomingPackets();
				elapsedThisPass -= savedInputTimestamp;
				g_inputTimestamp += Time_ConsumeElapsedTicks();
				elapsedThisPass += g_inputTimestamp;
				g_inputTimestamp = savedInputTimestamp;
				if (g_flightNetRemoteResyncChecksumsReceivedFlag !=
				    0) {
					break;
				}
			}

			stillLoadingElapsed += elapsedThisPass;
			if (stillLoadingElapsed >=
			    CHECKSUM_POLL_INTERVAL_TICKS) {
				stillLoadingElapsed = 0;
				g_flightNetScratchPacket.packetType =
					NET_PACKET_STILL_LOADING;

				NetSession_SendPacket(
					directPlayId,
					(unsigned int
						 *)&g_flightNetScratchPacket,
					sizeof(int));

				NetSession_BroadcastPacketToPlayers(
					(unsigned int
						 *)&g_flightNetScratchPacket,
					sizeof(int));
				alertToggle = !alertToggle;
				if (alertToggle != 0) {
					FlightAlert_DrawBox(
						3,
						g_strDiskIoMessages
							[DISK_IO_STR_ESC_BOOT_PLAYER],
						ALERT_BACKGROUND_COLOR);
				} else {
					FlightAlert_DrawBox(
						3,
						g_strDiskIoMessages
							[DISK_IO_STR_RECOVERING_WAIT],
						ALERT_BACKGROUND_COLOR);
				}
			}
			if (g_flightNetRemoteResyncChecksumsReceivedFlag != 0) {
				break;
			}
			--retryCount;
		} while (retryCount != 0 &&
			 g_flightNetRemoteResyncChecksumsReceivedFlag == 0);
	}

	if (g_flightNetRemoteResyncChecksumsReceivedFlag == 0) {
		FlightNet_BroadcastPlayerAbort(
			NetSession_FindPlayerSlotByDpid(directPlayId));
		FlightAlert_RestoreBoxBackground();
		Time_ConsumeElapsedTicks();
		g_flightNetPendingAckCount = 0;
		return 0;
	}

	chunkSlot = 0;
	buildResult = Flight_BuildWorldStateResyncSegmentChecksums(
		g_flightNetLocalResyncChecksums, worldState, worldStateSize);
	memset(g_flightNetWorldStateChunkAcked, 0,
	       sizeof(g_flightNetWorldStateChunkAcked));
	worldOffset = 0;
	segmentSize = Flight_ComputeWorldStateResyncSegmentSize(worldStateSize);
	g_flightNetWorldStateChunkPackets[0].packetType =
		NET_PACKET_RESYNC_CHUNK;
	packetFreeBytes = CHUNK_FREE_BYTES;
	g_flightNetWorldStateChunkPackets[0].checksumEpoch =
		(int)g_flightNetWorldChecksumEpoch;
	payload = g_flightNetWorldStateChunkPackets[0].payload;
	g_flightNetWorldStateChunkPackets[0].chunkIndex = 0;

	for (segmentIndex = 0; segmentIndex < buildResult; ++segmentIndex) {
		int remainingSegmentBytes;

		if (g_flightNetRemoteResyncChecksums[segmentIndex] ==
		    g_flightNetLocalResyncChecksums[segmentIndex]) {
			worldOffset += segmentSize;
			continue;
		}

		remainingSegmentBytes = segmentSize;
		if (worldOffset + remainingSegmentBytes > worldStateSize) {
			remainingSegmentBytes = worldStateSize - worldOffset;
			if (remainingSegmentBytes < 0) {
				remainingSegmentBytes = 0;
			}
		}

		while (remainingSegmentBytes != 0) {
			FlightNetWorldStateChunkRecordHeader *recordHeader;
			int recordBytes;

			recordBytes = remainingSegmentBytes +
				      CHUNK_RECORD_HEADER_SIZE;
			if (recordBytes > packetFreeBytes) {
				recordBytes = packetFreeBytes;
			}
			recordHeader =
				(FlightNetWorldStateChunkRecordHeader *)payload;
			recordHeader->worldOffset = worldOffset;
			recordHeader->dataSize =
				recordBytes - CHUNK_RECORD_HEADER_SIZE;
			memcpy(payload + CHUNK_RECORD_HEADER_SIZE,
			       &worldState[worldOffset],
			       (size_t)recordHeader->dataSize);
			remainingSegmentBytes -= recordHeader->dataSize;
			worldOffset += recordHeader->dataSize;
			packetFreeBytes -= recordBytes;
			payload += recordBytes;

			if ((unsigned int)packetFreeBytes <
			    CHUNK_FLUSH_THRESHOLD) {
				memset(payload, UINT8_MAX, sizeof(int));

				NetSession_SendPacket(
					directPlayId,
					(unsigned int
						 *)&g_flightNetWorldStateChunkPackets
						[chunkSlot],
					CHUNK_PACKET_SEND_BASE_SIZE -
						packetFreeBytes);
				++chunkSlot;
				if (chunkSlot == CHUNK_BATCH_SIZE) {
					if (FlightNet_WaitForWorldStateChunkAcks(
						    directPlayId, chunkSlot) ==
					    0) {
						FlightAlert_RestoreBoxBackground();
						Time_ConsumeElapsedTicks();
						g_flightNetPendingAckCount = 0;
						return 0;
					}
					g_flightNetScratchPacket.packetType =
						NET_PACKET_STILL_LOADING;

					NetSession_BroadcastPacketToPlayers(
						(unsigned int
							 *)&g_flightNetScratchPacket,
						sizeof(int));
					alertToggle = !alertToggle;
					if (alertToggle != 0) {
						FlightAlert_DrawBox(
							3,
							g_strDiskIoMessages
								[DISK_IO_STR_ESC_BOOT_PLAYER],
							ALERT_BACKGROUND_COLOR);
					} else {
						FlightAlert_DrawBox(
							3,
							g_strDiskIoMessages
								[DISK_IO_STR_RECOVERING_WAIT],
							ALERT_BACKGROUND_COLOR);
					}
					chunkSlot = 0;
					memset(g_flightNetWorldStateChunkAcked,
					       0,
					       sizeof(g_flightNetWorldStateChunkAcked));
				}

				packetFreeBytes = CHUNK_FREE_BYTES;
				g_flightNetWorldStateChunkPackets[chunkSlot]
					.packetType = NET_PACKET_RESYNC_CHUNK;
				g_flightNetWorldStateChunkPackets[chunkSlot]
					.checksumEpoch =
					(int)g_flightNetWorldChecksumEpoch;
				g_flightNetWorldStateChunkPackets[chunkSlot]
					.chunkIndex = chunkSlot;
				payload = g_flightNetWorldStateChunkPackets
						  [chunkSlot]
							  .payload;
			}
		}
	}

	if ((unsigned int)packetFreeBytes < CHUNK_FINAL_SEND_THRESHOLD) {
		memset(payload, UINT8_MAX, sizeof(int));

		NetSession_SendPacket(
			directPlayId,
			(unsigned int *)&g_flightNetWorldStateChunkPackets
				[chunkSlot],
			CHUNK_PACKET_SEND_BASE_SIZE - packetFreeBytes);
		if (FlightNet_WaitForWorldStateChunkAcks(directPlayId,
							 chunkSlot + 1) == 0) {
			result = 0;
		}
	}

	FlightAlert_RestoreBoxBackground();
	Time_ConsumeElapsedTicks();
	g_flightNetPendingAckCount = 0;
	return result;
#endif
}

/* Waits until the player has acked the first chunkCount resync chunks in
 * g_flightNetWorldStateChunkAcked, reading packets, and returns 1. Every 236
 * ticks it sends all players a STILL_LOADING and changes the alert text; after
 * 20 of those in a row with no new ack, or on ESC, it tells all players that
 * player has aborted and returns 0. Writes g_flightNetScratchPacket;
 * g_inputTimestamp is put back after each read. Only the original build calls
 * this; its modern arm hands off to XvtResync_WaitAcks. */
// FUNCTION: XVT 0x4658C0
int FlightNet_WaitForWorldStateChunkAcks(int directPlayId, int chunkCount)
{
#ifdef XVT_MODERN
	return XvtResync_WaitAcks(directPlayId, chunkCount);
#else
	enum {
		ACK_POLL_INTERVAL_TICKS = 236,
		UNCHANGED_ACK_RETRY_COUNT = 20,
		ALERT_BACKGROUND_COLOR = 0x30
	};

	int ackCount;
	int alertToggle;

	int retryCountdown;
	int stillLoadingElapsed;
	int lastAckCount;

	alertToggle = 0;
	stillLoadingElapsed = alertToggle;
	lastAckCount = alertToggle;
	retryCountdown = UNCHANGED_ACK_RETRY_COUNT;
	do {
		int elapsedThisPass = 0;
		int savedInputTimestamp;

		while (elapsedThisPass < ACK_POLL_INTERVAL_TICKS) {

			if (FlightInput_HasKeyReady() &&
			    FlightInput_GetNextKey() == FLIGHT_KEY_ESCAPE) {
				elapsedThisPass = ACK_POLL_INTERVAL_TICKS;
				ackCount = lastAckCount;
				retryCountdown = 1;
				break;
			}

			savedInputTimestamp = g_inputTimestamp;
			FlightNet_ProcessIncomingPackets();
			elapsedThisPass -= savedInputTimestamp;
			g_inputTimestamp += Time_ConsumeElapsedTicks();
			elapsedThisPass += g_inputTimestamp;
			g_inputTimestamp = savedInputTimestamp;

			for (ackCount = 0; ackCount < chunkCount; ++ackCount) {
				if (g_flightNetWorldStateChunkAcked[ackCount] ==
				    0) {
					break;
				}
			}
			if (ackCount == chunkCount) {
				break;
			}
		}

		if (ackCount == chunkCount) {
			break;
		}

		stillLoadingElapsed += elapsedThisPass;
		if (stillLoadingElapsed >= ACK_POLL_INTERVAL_TICKS) {
			stillLoadingElapsed = 0;
			g_flightNetScratchPacket.packetType =
				NET_PACKET_STILL_LOADING;

			NetSession_BroadcastPacketToPlayers(
				(unsigned int *)&g_flightNetScratchPacket,
				sizeof(int));
			alertToggle = !alertToggle;
			if (alertToggle != 0) {
				FlightAlert_DrawBox(
					3,
					g_strDiskIoMessages
						[DISK_IO_STR_ESC_BOOT_PLAYER],
					ALERT_BACKGROUND_COLOR);
			} else {
				FlightAlert_DrawBox(
					3,
					g_strDiskIoMessages
						[DISK_IO_STR_RESENDING_PACKET_WAIT],
					ALERT_BACKGROUND_COLOR);
			}
		}

		if (lastAckCount == ackCount) {
			--retryCountdown;
		} else {
			retryCountdown = UNCHANGED_ACK_RETRY_COUNT;
		}
		lastAckCount = ackCount;
	} while (retryCountdown != 0);

	if (retryCountdown == 0) {
		FlightNet_BroadcastPlayerAbort(
			NetSession_FindPlayerSlotByDpid(directPlayId));
		return 0;
	}
	return 1;
#endif
}

#ifndef XVT_MODERN
/* Receives a world resync from the host; acts only on a RESYNC_REQUEST. Shows
 * the communication-failure alert, applies the request's object presence map,
 * and sends the host the segment checksums of its duplicate world state. Then
 * reads packets: each RESYNC_CHUNK of the current checksum epoch has its
 * (offset, size, bytes) records copied in and its index acked to the host; a
 * RESYNC_APPLY of the epoch makes it apply the world and replay world messages,
 * ack the host, set g_inputTimestamp to g_flightNetClockLeadTicks plus
 * g_serverTickTime and return, or end its flight if this player has aborted.
 * Meanwhile it decodes inputs into the senders' histories, applies world
 * messages, and acts on aborts, resync notices and loading pulses; a second
 * RESYNC_REQUEST or ESC makes this player leave. Host silence adds up in
 * g_flightNetHostTimeoutElapsedTicks; past 7,080 ticks it closes the alert and
 * returns, and when fewer than 50 steps of 118 ticks remain it shows a
 * countdown. Unlike FlightNet_ProcessIncomingPackets it clears the decoded
 * input once per batch, not per record, so a key carries into later records of
 * the batch that have none. Writes g_inputTimestamp, g_flightNetScratchPacket,
 * g_flightNetPeerSilenceTicks, g_flightNetLastInputTimestampByPlayer,
 * g_flightNetResyncPlayerDplayId, g_flightNetHostAbortReceived,
 * g_playerAbortFlags, g_flightMissionState.missionEndPending and
 * participationState. Only the original build calls this. */
// FUNCTION: XVT 0x465A20
void FlightNet_HandleWorldStateResyncPacket(const int *packet)
{
	enum {
		PLAYER_COUNT = 8,
		FULL_TIMESTAMP_CODE = 0x7F,
		TIMESTAMP_CODE_MASK = 0x7F,
		KEY_PRESENT_FLAG = 0x80,
		ALERT_BACKGROUND_COLOR = 0x30,
		HOST_TIMEOUT_TICKS = 7080,
		COUNTDOWN_INTERVAL_TICKS = 118,
		COUNTDOWN_THRESHOLD_HALF_SECONDS = 50,
		COUNTDOWN_HALF_SECOND_TENTHS = 5
	};

	FlightInputFrameRecord input;
	/* Two jobs: the low 7-bit timestamp code of an input record, or the frames left in an input batch. */
	int decodeValue;
	int senderDpid;
	int countdownValue;
	int receivedMatchingPacket;
	int receivedPayloadSize;
	char statusText[80];

	countdownValue = 0;
	if (packet[0] != NET_PACKET_RESYNC_REQUEST) {
		return;
	}

	g_inputTimestamp += Time_ConsumeElapsedTicks();
	FlightAlert_SaveBoxBackground();
	FlightAlert_DrawBox(
		1, g_strDiskIoMessages[DISK_IO_STR_COM_FAILURE_RECEIVING],
		ALERT_BACKGROUND_COLOR);
	Flight_ApplyWorldStateObjectPresenceMap((const uint8_t *)packet +
						2 * sizeof(int));
	g_flightNetScratchPacket.packetType = NET_PACKET_RESYNC_CHECKSUMS;
	/* Before any packet is received, this holds the byte size of the outgoing checksum payload. */
	receivedPayloadSize =
		(int)(sizeof(int) *
		      Flight_BuildWorldStateResyncSegmentChecksums(
			      g_flightNetScratchPacket.payloadDwords,
			      Flight_GetDuplicateWorldStateBuffer(),
			      Flight_GetDuplicateWorldStateSize()));

	NetSession_SendPacket(NetSession_GetHostDplayId(),
			      (unsigned int *)&g_flightNetScratchPacket,
			      receivedPayloadSize + sizeof(int));

	for (;;) {
		int *receivedPacket;

		if (FlightInput_HasKeyReady() != 0 &&
		    FlightInput_GetNextKey() == FLIGHT_KEY_ESCAPE) {
			break;
		}

		receivedMatchingPacket = 0;
		do {
			int elapsedTicks;
			int savedInputTimestamp;
			int remainingHalfSeconds;

			savedInputTimestamp = g_inputTimestamp;
			g_inputTimestamp += Time_ConsumeElapsedTicks();
			elapsedTicks = g_inputTimestamp - savedInputTimestamp;
			g_inputTimestamp = savedInputTimestamp;
			g_flightNetHostTimeoutElapsedTicks += elapsedTicks;
			if (g_flightNetHostTimeoutElapsedTicks >
			    HOST_TIMEOUT_TICKS) {
				FlightAlert_RestoreBoxBackground();
				return;
			}

			remainingHalfSeconds =
				(HOST_TIMEOUT_TICKS -
				 g_flightNetHostTimeoutElapsedTicks) /
				COUNTDOWN_INTERVAL_TICKS;
			if (countdownValue != remainingHalfSeconds) {
				countdownValue = remainingHalfSeconds;
				if (remainingHalfSeconds >=
				    COUNTDOWN_THRESHOLD_HALF_SECONDS) {
					if ((remainingHalfSeconds & 1) != 0) {
						FlightAlert_DrawBox(
							3,
							g_strDiskIoMessages
								[DISK_IO_STR_ESC_DISCONNECT],
							ALERT_BACKGROUND_COLOR);
					} else {
						FlightAlert_DrawBox(
							3,
							g_strDiskIoMessages
								[DISK_IO_STR_RECOVERING_WAIT],
							ALERT_BACKGROUND_COLOR);
					}
				} else {
					sprintf(statusText,
						g_strDiskIoMessages
							[DISK_IO_STR_DISCONNECT_COUNTDOWN],
						remainingHalfSeconds / 2,
						COUNTDOWN_HALF_SECOND_TENTHS *
							(remainingHalfSeconds &
							 1));
					FlightAlert_DrawBox(
						3, statusText,
						ALERT_BACKGROUND_COLOR);
				}
			}

			receivedPacket = NetSession_ReceiveGamePacket(
				&senderDpid, &receivedPayloadSize);
			if (receivedPacket == NULL) {
				continue;
			}

			switch (receivedPacket[0]) {
			case NET_PACKET_REMOTE_INPUT: {
				unsigned int timestamp;
				const uint8_t *cursor;
				InputFrame *inserted;
				int playerIndex;
				uint8_t timestampCode;
				unsigned int lowCode;

				playerIndex = NetSession_FindPlayerSlotByDpid(
					senderDpid);
				if (g_players[playerIndex].participationState !=
				    0) {
					if (g_flightNetPeerSilenceTicks
						    [playerIndex] > 0) {
						g_flightNetPeerSilenceTicks
							[playerIndex] = 0;
					}
					FlightSync_DiscardPredictedInputFrames(
						playerIndex);
					memset(&input, 0, sizeof(input));
					cursor = (const uint8_t *)
							 receivedPacket +
						 sizeof(int);
					timestampCode = *cursor;
					lowCode = (unsigned int)timestampCode &
						  TIMESTAMP_CODE_MASK;
					if (lowCode == FULL_TIMESTAMP_CODE) {
						timestamp = *(
							const unsigned int
								*)(cursor + 1);
						if ((timestampCode &
						     KEY_PRESENT_FLAG) != 0) {
							input.key = cursor[5];
							cursor += 6;
						} else {
							cursor += 5;
						}
					} else {
						int previousCode =
							g_flightNetLastInputTimestampByPlayer
								[playerIndex];

						decodeValue = lowCode;
						if ((previousCode &
						     TIMESTAMP_CODE_MASK) >
						    decodeValue) {
							previousCode +=
								TIMESTAMP_CODE_MASK +
								1;
						}
						timestamp =
							(unsigned int)
								decodeValue |
							((unsigned int)
								 previousCode &
							 ~TIMESTAMP_CODE_MASK);
						if ((timestampCode &
						     KEY_PRESENT_FLAG) != 0) {
							input.key = cursor[1];
							cursor += 2;
						} else {
							cursor += 1;
						}
					}
					g_flightNetLastInputTimestampByPlayer
						[playerIndex] = (int)timestamp;
					input.axisX = (int8_t)(cursor[0] &
							       (uint8_t)~1u);
					input.axisY = (int8_t)(cursor[1] &
							       (uint8_t)~1u);
					input.keyMods = cursor[1] & 1u;
					input.keyMods += input.keyMods;
					input.keyMods |= cursor[0] & 1u;
					inserted = FlightSync_InsertInputFrame(
						playerIndex, (int)timestamp,
						&input);
					if (inserted != NULL) {
						int localIsHost =
							NetSession_IsLocalHost();

						inserted->awaitingRelay = 1;
						if (localIsHost == 0) {
							inserted->awaitingRelay =
								0;
						}
						inserted->inputSource = 1;
					}
				} else {
					g_flightNetScratchPacket.packetType =
						NET_PACKET_PLAYER_ABORT;
					g_flightNetScratchPacket
						.payloadDwords[0] = playerIndex;

					NetSession_SendPacket(
						senderDpid,
						(unsigned int
							 *)&g_flightNetScratchPacket,
						2 * sizeof(int));
				}
				break;
			}
			case NET_PACKET_WORLD_MESSAGE: {
				int savedTimestamp = g_inputTimestamp;

				FlightSync_ApplyWorldMessagePacket(
					(uint8_t *)receivedPacket);
				Time_ConsumeElapsedTicks();
				g_inputTimestamp = savedTimestamp;
				if (g_flightMissionState.missionEndPending !=
				    0) {
					return;
				}
				break;
			}
			case NET_PACKET_SESSION_ABORT:
				g_flightMissionState.missionEndPending = 1;
				g_flightNetHostAbortReceived = 1;
				g_players[g_localPlayer].participationState = 0;
				return;
			case NET_PACKET_PLAYER_ABORT: {
				int playerIndex = receivedPacket[1];

				if (playerIndex >= 0 &&
				    playerIndex < PLAYER_COUNT) {
					g_playerAbortFlags[playerIndex] = 1;
				}
				if (playerIndex == g_localPlayer) {
					g_flightMissionState.missionEndPending =
						1;
					g_players[g_localPlayer]
						.participationState = 0;
					g_playerAbortFlags[g_localPlayer] = 1;
					FlightNet_MarkPilotNetworkPlayerLeft(
						g_localPlayer);
					return;
				}
				break;
			}
			case NET_PACKET_INPUT_BATCH: {
				const uint8_t *cursor;
				int playerIndex;

				playerIndex = NetSession_FindPlayerSlotByDpid(
					senderDpid);
				if (g_players[playerIndex].participationState !=
				    0) {
					if (g_flightNetPeerSilenceTicks
						    [playerIndex] > 0) {
						g_flightNetPeerSilenceTicks
							[playerIndex] = 0;
					}
					cursor = (const uint8_t *)
							 receivedPacket +
						 sizeof(int);
					FlightSync_DiscardPredictedInputFrames(
						playerIndex);
					memset(&input, 0, sizeof(input));
					decodeValue = *cursor++;
					while (decodeValue > 0) {
						InputFrame *inserted;
						uint8_t lowCode;
						uint8_t timestampCode;
						unsigned int timestamp;

						timestampCode = *cursor;
						lowCode = timestampCode &
							  TIMESTAMP_CODE_MASK;
						if (lowCode ==
						    FULL_TIMESTAMP_CODE) {
							timestamp = *(
								const unsigned int
									*)(cursor +
									   1);
							if ((timestampCode &
							     KEY_PRESENT_FLAG) !=
							    0) {
								input.key = cursor
									[5];
								cursor += 6;
							} else {
								cursor += 5;
							}
						} else {
							int previousCode = g_flightNetLastInputTimestampByPlayer
								[playerIndex];

							if ((previousCode &
							     TIMESTAMP_CODE_MASK) >
							    lowCode) {
								previousCode +=
									TIMESTAMP_CODE_MASK +
									1;
							}
							timestamp =
								(unsigned int)
									lowCode |
								((unsigned int)
									 previousCode &
								 ~TIMESTAMP_CODE_MASK);
							if ((timestampCode &
							     KEY_PRESENT_FLAG) !=
							    0) {
								input.key = cursor
									[1];
								cursor += 2;
							} else {
								cursor += 1;
							}
						}
						g_flightNetLastInputTimestampByPlayer
							[playerIndex] =
								(int)timestamp;
						input.axisX =
							(int8_t)(cursor[0] &
								 (uint8_t)~1u);
						input.axisY =
							(int8_t)(cursor[1] &
								 (uint8_t)~1u);
						input.keyMods = cursor[1] & 1u;
						input.keyMods += input.keyMods;
						input.keyMods |= cursor[0] & 1u;
						cursor += 2;
						inserted =
							FlightSync_InsertInputFrame(
								playerIndex,
								(int)timestamp,
								&input);
						if (inserted != NULL) {
							int localIsHost =
								NetSession_IsLocalHost();

							inserted->awaitingRelay =
								1;
							if (localIsHost == 0) {
								inserted->awaitingRelay =
									0;
							}
							inserted->inputSource =
								1;
						}
						--decodeValue;
					}
				} else {
					g_flightNetScratchPacket.packetType =
						NET_PACKET_PLAYER_ABORT;
					g_flightNetScratchPacket
						.payloadDwords[0] = playerIndex;

					NetSession_SendPacket(
						senderDpid,
						(unsigned int
							 *)&g_flightNetScratchPacket,
						2 * sizeof(int));
				}
				break;
			}
			case NET_PACKET_RESYNC_NOTICE:
				g_flightNetResyncPlayerDplayId =
					receivedPacket[1];
				break;
			case NET_PACKET_STILL_LOADING:
				if (NetSession_GetHostDplayId() == senderDpid) {
					g_flightNetHostTimeoutElapsedTicks = 0;
				} else {
					int playerIndex =
						NetSession_FindPlayerSlotByDpid(
							senderDpid);

					if (g_players[playerIndex]
							    .participationState !=
						    0 &&
					    g_flightNetPeerSilenceTicks
							    [playerIndex] > 0) {
						g_flightNetPeerSilenceTicks
							[playerIndex] = 0;
					}
				}
				break;
			case NET_PACKET_RESYNC_REQUEST:
				FlightNet_BroadcastPlayerAbort(g_localPlayer);
				g_flightMissionState.missionEndPending = 1;
				g_players[g_localPlayer].participationState = 0;
				g_playerAbortFlags[g_localPlayer] = 1;
				FlightNet_MarkPilotNetworkPlayerLeft(
					g_localPlayer);
				return;
			case NET_PACKET_RESYNC_APPLY:
			case NET_PACKET_RESYNC_CHUNK:
				if (receivedPacket[1] ==
				    (int)g_flightNetWorldChecksumEpoch) {
					receivedMatchingPacket = 1;
				}
				break;
			default:
				break;
			}
		} while (receivedMatchingPacket == 0);

		if (receivedPacket[0] == NET_PACKET_RESYNC_APPLY) {
			if (g_playerAbortFlags[g_localPlayer] != 0) {
				g_playerAbortFlags[g_localPlayer] = 1;
				g_flightMissionState.missionEndPending = 1;
				g_players[g_localPlayer].participationState = 0;
				FlightNet_MarkPilotNetworkPlayerLeft(
					g_localPlayer);
			} else {
				Time_ConsumeElapsedTicks();
				FlightSync_ApplyResyncAndReplayWorldMessages(
					(unsigned int)receivedPacket[2],
					receivedPacket[1]);
				g_flightNetScratchPacket.packetType =
					NET_PACKET_ACK;

				NetSession_SendPacket(
					NetSession_GetHostDplayId(),
					(unsigned int
						 *)&g_flightNetScratchPacket,
					sizeof(int));
				g_inputTimestamp = g_flightNetClockLeadTicks +
						   g_serverTickTime;
				FlightAlert_RestoreBoxBackground();
			}
			return;
		}

		{
			int chunkIndex = receivedPacket[2];

			while (receivedPacket[3] != -1) {
				FlightSync_CopyWorldStateResyncChunk(
					(const uint8_t *)&receivedPacket[5],
					receivedPacket[3],
					(unsigned int)receivedPacket[4]);
				receivedPacket =
					(int *)((uint8_t *)receivedPacket +
						2 * sizeof(int) +
						receivedPacket[4]);
			}
			g_flightNetScratchPacket.payloadDwords[0] = chunkIndex;
			g_flightNetScratchPacket.packetType =
				NET_PACKET_RESYNC_CHUNK_ACK;
		}

		NetSession_SendPacket(NetSession_GetHostDplayId(),
				      (unsigned int *)&g_flightNetScratchPacket,
				      2 * sizeof(int));
	}

	FlightNet_BroadcastPlayerAbort(g_localPlayer);
	g_flightMissionState.missionEndPending = 1;
	g_players[g_localPlayer].participationState = 0;
	g_playerAbortFlags[g_localPlayer] = 1;
	FlightNet_MarkPilotNetworkPlayerLeft(g_localPlayer);
}
#endif
