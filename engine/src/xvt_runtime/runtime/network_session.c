#include "xvt_runtime/runtime/network_session.h"
#include "aeron/aeron.h"
#include "xvt/frontend/frontend.h"
#include "xvt/frontend/frontend_display.h"
#include "xvt/frontend/frontend_mission.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/frontend_net.h"
#include "xvt/util/time.h"
#include "xvt_runtime/config/config.h"
#include "xvt_runtime/runtime/flight_network.h"
#include "xvt_runtime/runtime/network_metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	SESSION_IDLE,
	SESSION_CLOSE,
	SESSION_FACTORY,
	SESSION_PREPARE,
	SESSION_OPEN,
	SESSION_PLAYER,
	SESSION_GROUP,
	SESSION_ROSTER,
	SESSION_REGISTER,
	SESSION_HANDSHAKE,
	SESSION_ADMISSION,
	SESSION_ESTABLISHED,
	SESSION_FAILED
};

static struct {
	int phase, host, online, opened, registered, closing, cancel_join, flight, flight_ready, lost;
	GUID app, instance;
	char info[16], player[16], name[32];
	uint64_t deadline, retry;
	AeronDplayDirectoryError error;
	XvtNetworkMetadata metadata;
	AeronDplayRoomMetadata published;
} g_session;

static char g_origin[AERON_DPLAY_DIRECTORY_URL_CAPACITY];
static const GUID g_application = {
	0x09438c20, 0xe06a, 0x11ce, { 0x86, 0x81, 0, 0xaa, 0, 0x6c, 0x5d, 0x57 }
};

int XvtNetworkSession_CopyPlayerNames(const NetPlayerNameMessage* message, char* short_name,
									  size_t short_capacity, char* long_name, size_t long_capacity) {
	const char* short_end;
	const char* long_start;
	const char* long_end;
	size_t short_size, long_size, remaining;
	if (!message || !short_name || !long_name || !short_capacity || !long_capacity)
		return 0;
	short_end = memchr(message->names, 0, sizeof(message->names));
	if (!short_end)
		return 0;
	long_start = short_end + 1;
	remaining = sizeof(message->names) - (size_t)(long_start - message->names);
	long_end = memchr(long_start, 0, remaining);
	if (!long_end)
		return 0;
	short_size = (size_t)(short_end - message->names);
	long_size = (size_t)(long_end - long_start);
	if (short_size >= short_capacity)
		short_size = short_capacity - 1;
	if (long_size >= long_capacity)
		long_size = long_capacity - 1;
	memcpy(short_name, message->names, short_size);
	short_name[short_size] = 0;
	memcpy(long_name, long_start, long_size);
	long_name[long_size] = 0;
	return 1;
}

AeronDplayDirectoryError XvtNetworkSession_Configure(void) {
	const XvtSettings* settings = XvtConfig_Settings();
	const char* origin = settings ? settings->lobby_url : "";
	if (!*origin)
		return AERON_DPLAY_DIRECTORY_ERROR_NOT_CONFIGURED;
	if (!strcmp(origin, g_origin))
		return AERON_DPLAY_DIRECTORY_ERROR_NONE;
	if (strlen(origin) >= sizeof(g_origin))
		return AERON_DPLAY_DIRECTORY_ERROR_INVALID_REQUEST;
	AeronDplayDirectoryConfig config = { 0 };
	strcpy(config.lobby_url, origin);
	config.application_id = g_application;
	snprintf(config.game_version, sizeof(config.game_version), "%d", FRONTEND_NET_PROTOCOL_VERSION);
	AeronDplayDirectoryError error = AeronDplayDirectory_Configure(&config);
	if (!error)
		strcpy(g_origin, origin);
	return error;
}

void XvtNetworkSession_OnClose(void) {
	XvtFlightNetwork_CloseSession();
	if (g_session.registered)
		AeronDplayDirectory_StopHosting();
	if (g_session.online && !g_session.host) {
		if (g_session.opened)
			g_session.cancel_join = 1;
		else
			AeronDplayDirectory_CancelJoin();
	}
	g_session.registered = 0;
	g_session.flight = g_session.flight_ready = g_session.lost = 0;
	g_session.closing = 1;
	g_session.phase = SESSION_IDLE;
}

void XvtNetworkSession_Leave(void) { Net_ShutdownDirectPlaySession(); }

void XvtNetworkSession_Cancel(void) { XvtNetworkSession_Leave(); }

static int XvtNetworkSession_Fail(AeronDplayDirectoryError error) {
	Aeron_LogWarn("xvt.network", "Session failed in phase %d (error %u)", g_session.phase, (unsigned)error);
	XvtNetworkSession_Leave();
	g_session.error = error;
	g_session.phase = SESSION_FAILED;
	return 0;
}

static int XvtNetworkSession_Start(const char* info, const char* player, const char* name, int host,
								   int online, const GUID* room) {
	if (g_session.phase != SESSION_IDLE && g_session.phase != SESSION_FAILED &&
		g_session.phase != SESSION_ESTABLISHED)
		return XVT_NETWORK_PENDING;
	XvtNetworkSession_Leave();
	/* Preserve completion of a preceding close while preparing the next session. */
	int closing = g_session.closing, cancel_join = g_session.cancel_join;
	memset(&g_session, 0, sizeof(g_session));
	g_session.closing = closing;
	g_session.cancel_join = cancel_join;
	g_session.app = g_application;
	g_session.host = host;
	g_session.online = online;
	if (!info || !player || !name || (!host && !room))
		return XvtNetworkSession_Fail(AERON_DPLAY_DIRECTORY_ERROR_INVALID_REQUEST);
	if (strlen(info) >= sizeof(g_session.info) || strlen(player) >= sizeof(g_session.player) ||
		strlen(name) >= sizeof(g_session.name))
		return XvtNetworkSession_Fail(AERON_DPLAY_DIRECTORY_ERROR_INVALID_REQUEST);
	strcpy(g_session.info, info);
	strcpy(g_session.player, player);
	if (*name)
		strcpy(g_session.name, name);
	else
		snprintf(g_session.name, sizeof(g_session.name), "%s's Game.", player);
	if (room)
		g_session.instance = *room;
	g_missionSetupIsHost = host;
	if (online)
		g_frontendMissionSessionMode =
			host ? FRONTEND_MISSION_SESSION_NET_HOST : FRONTEND_MISSION_SESSION_NET_CLIENT;
	g_session.phase = SESSION_CLOSE;
	return XVT_NETWORK_PENDING;
}

int XvtNetworkSession_BeginHost(const char* info, const char* player, const char* name, int online) {
	return XvtNetworkSession_Start(info, player, name, 1, online, NULL);
}

int XvtNetworkSession_BeginJoin(const char* info, const char* player, const GUID* room) {
	return XvtNetworkSession_Start(info, player, "", 0, 1, room);
}

static int XvtNetworkSession_Factory(void) {
	const GUID* provider = Net_GetDirectPlayServiceProviderGuid(NET_TRANSPORT_TCPIP);
	IDirectPlay* temporary = NULL;
	HRESULT result;
	if (!provider)
		return 0;
	g_frontState.netRuntimeRecvHistoryCount = 0;
	g_frontState.netRuntimeBroadcastSeqCounter = g_frontState.netRuntimeGroupSeqCounter = 0;
	g_frontState.netRuntimeBroadcastPendingPayload.pendingFlush = 1;
	g_frontState.netRuntimeBroadcastPendingPayload.payload[0] = NET_PACKET_NOP;
	g_frontState.netRuntimeBroadcastPendingPayload.payloadLength = 1;
	g_frontState.netRuntimeGroupPendingPayload.pendingFlush = 1;
	g_frontState.netRuntimeGroupPendingPayload.payload[0] = NET_PACKET_NOP;
	g_frontState.netRuntimeGroupPendingPayload.payloadLength = 1;
	g_frontState.netSequenceCount = 0;
	g_frontState.netReliableRetryLongTimeoutMode = 0;
	g_frontState.netGroupDplayId = 0;
	g_frontState.netHostPlayerId = 0;
	g_frontState.netExportRecvQueuePtr = NULL;
	g_frontState.netExportRecvQueueHighWater = 0;
	for (int i = 0; i < 40; ++i) {
		NetReliablePeerSlot* peer = &g_frontState.netRuntimeReliablePeerSlots[i];
		peer->prevRecvSeqDefault = peer->prevRecvSeqChannelA = peer->prevRecvSeqChannelB = 127;
		peer->recvSeqDefault = peer->recvSeqChannelA = peer->recvSeqChannelB = 127;
		peer->sendSeq = 0;
		peer->directPlayId = 0;
		peer->lastPiggybackType = NET_PACKET_NOP;
		peer->piggybackLength = 1;
		peer->lastActivityMs = peer->lastKeepaliveMs = 0;
		peer->packetCount = peer->packetDropCount = peer->packetRetryCount = 0;
	}
	memset(g_frontState.netRuntimeRecvHistory, 0, sizeof(g_frontState.netRuntimeRecvHistory));
	memset(g_netPlayerConnectionStats, 0, sizeof(g_netPlayerConnectionStats));
	/* A new session owns fresh admission state; browsing never resets this roster. */
	memset(g_frontState.netPlayers, 0, sizeof(g_frontState.netPlayers));
	memset(&g_frontState.netRuntimeLocalPlayer, 0, sizeof(g_frontState.netRuntimeLocalPlayer));
	memset(g_mpRoster, 0, sizeof(g_mpRoster));
	g_missionSetupRosterAuthoritative = 0;
	g_frontState.netAppGuid = g_session.app;
	g_frontState.netIsHost = g_session.host;
	strcpy(g_frontState.netPlayers[0].playerName, g_session.info);
	strcpy(g_frontState.netPlayers[0].sessionName, g_session.player);
	strcpy(g_frontState.netSessionName, g_session.name);
	result = DirectPlayCreate(provider, &temporary, NULL);
	if (result)
		return 0;
	result =
		temporary->lpVtbl->QueryInterface(temporary, &IID_IDirectPlay2A, (void**)&g_frontState.netDirectPlay);
	temporary->lpVtbl->Release(temporary);
	return result == 0;
}

static int XvtNetworkSession_Handshake(void) {
	DPID sender;
	uint32_t size;
	for (int count = 0; count < 32; ++count) {
		int* packet = Net_GetNextAppPacket(&sender, &size);
		if (!packet)
			break;
		if (size < 16 || packet[0] != NET_PACKET_SEQUENCE_STATUS)
			continue;
		unsigned peers = (unsigned)packet[2];
		NetReliablePeerSlot saved = { 0 };
		if (peers > 40 || size < 16 + 8 * peers)
			continue;
		g_frontState.netHostPlayerId = sender;
		for (unsigned i = 0; i < g_frontState.netSequenceCount; ++i)
			if (g_frontState.netRuntimeReliablePeerSlots[i].directPlayId == sender)
				saved = g_frontState.netRuntimeReliablePeerSlots[i];
		g_frontState.netSequenceCount = peers;
		for (unsigned i = 0; i < peers; ++i) {
			NetReliablePeerSlot* peer = &g_frontState.netRuntimeReliablePeerSlots[i];
			const uint8_t* row = (const uint8_t*)packet + 16 + 8 * i;
			memcpy(&peer->directPlayId, row, 4);
			peer->prevRecvSeqChannelA = row[4];
			peer->prevRecvSeqChannelB = row[5];
			peer->recvSeqChannelA = row[6];
			peer->recvSeqChannelB = row[7];
			peer->prevRecvSeqDefault = peer->recvSeqDefault = 127;
			peer->sendSeq = 0;
			peer->lastPiggybackType = NET_PACKET_NOP;
			peer->piggybackLength = 1;
			peer->lastActivityMs = peer->lastKeepaliveMs = GetTickCount();
			if (saved.directPlayId && peer->directPlayId == saved.directPlayId) {
				peer->prevRecvSeqDefault = saved.prevRecvSeqDefault;
				peer->recvSeqDefault = saved.recvSeqDefault;
				peer->sendSeq = saved.sendSeq;
				memcpy(&peer->lastPiggybackType, &saved.lastPiggybackType, saved.piggybackLength);
				peer->piggybackLength = saved.piggybackLength;
				peer->lastActivityMs = saved.lastActivityMs;
				peer->lastKeepaliveMs = saved.lastKeepaliveMs;
			}
		}
		int response[5] = { NET_PACKET_KEEPALIVE_ACK, packet[3], 0, 0, 0 };
		Net_SendDirectPlayPacket(sender, response, sizeof(response), 0);
		int request[6] = { NET_PACKET_JOIN_REQUEST, FRONTEND_NET_PROTOCOL_VERSION, 0, 0, 0, 0 };
		memcpy(request + 2, g_gameConfig.password, sizeof(g_gameConfig.password));
		Net_SendPacketAndFlush(sender, request, sizeof(request));
		g_session.phase = SESSION_ADMISSION;
		return 1;
	}
	return XVT_NETWORK_PENDING;
}

static int XvtNetworkSession_Register(void) {
	if (!g_session.registered) {
		XvtNetworkMetadata_Build(&g_session.metadata, 0);
		AeronDplayDirectoryError error =
			AeronDplayDirectory_StartHosting(&g_session.instance, &g_session.metadata.room);
		if (error)
			return XvtNetworkSession_Fail(error);
		g_session.registered = 1;
		g_session.published = g_session.metadata.room;
	}
	AeronDplayDirectoryStatus status;
	AeronDplayDirectory_GetHostStatus(&status);
	if (status.state == AERON_DPLAY_DIRECTORY_FAILED)
		return XvtNetworkSession_Fail(status.error);
	if (status.state != AERON_DPLAY_DIRECTORY_SUCCEEDED)
		return XVT_NETWORK_PENDING;
	g_session.phase = SESSION_ESTABLISHED;
	return 1;
}

int XvtNetworkSession_Tick(void) {
	HRESULT result;
	if (g_session.lost && !g_session.flight)
		return XvtNetworkSession_Fail(AERON_DPLAY_DIRECTORY_ERROR_CONNECTION_FAILED);
	switch (g_session.phase) {
		case SESSION_CLOSE: {
			if (g_session.closing || AeronDplay_IsActive())
				return XVT_NETWORK_PENDING;
			AeronDplayDirectoryError error = g_session.online ? XvtNetworkSession_Configure() : 0;
			if (error == AERON_DPLAY_DIRECTORY_ERROR_BUSY)
				return XVT_NETWORK_PENDING;
			if (error)
				return XvtNetworkSession_Fail(error);
			g_session.phase = SESSION_FACTORY;
			break;
		}
		case SESSION_FACTORY:
			if (!XvtNetworkSession_Factory())
				return XvtNetworkSession_Fail(AERON_DPLAY_DIRECTORY_ERROR_CONNECTION_FAILED);
			if (!g_session.host) {
				AeronDplayDirectoryError error = AeronDplayDirectory_BeginJoin(&g_session.instance);
				if (error)
					return XvtNetworkSession_Fail(error);
				AeronDplayJoinStatus join;
				AeronDplayDirectory_GetJoinStatus(&join);
				g_session.deadline = join.deadline_us;
			}
			g_session.phase = g_session.host ? SESSION_OPEN : SESSION_PREPARE;
			break;
		case SESSION_PREPARE: {
			AeronDplayJoinStatus join;
			AeronDplayDirectory_GetJoinStatus(&join);
			if (join.preparation.state == AERON_DPLAY_DIRECTORY_FAILED ||
				join.preparation.state == AERON_DPLAY_DIRECTORY_CANCELLED)
				return XvtNetworkSession_Fail(join.preparation.error);
			if (join.preparation.state == AERON_DPLAY_DIRECTORY_SUCCEEDED)
				g_session.phase = SESSION_OPEN;
			break;
		}
		case SESSION_OPEN: {
			DPSESSIONDESC2 desc = { 0 };
			desc.dwSize = sizeof(desc);
			desc.guidApplication = g_session.app;
			desc.guidInstance = g_session.instance;
			desc.dwMaxPlayers = 16;
			desc.lpszSessionNameA = g_session.name;
			g_session.opened = 1;
			result = g_frontState.netDirectPlay->lpVtbl->Open(g_frontState.netDirectPlay, &desc,
															  g_session.host ? DPOPEN_CREATE : DPOPEN_JOIN);
			if (result == DPERR_PENDING || result == DPERR_BUSY)
				break;
			if (result)
				return XvtNetworkSession_Fail(AERON_DPLAY_DIRECTORY_ERROR_CONNECTION_FAILED);
			g_session.instance = g_frontState.netJoinedSessionGuid = desc.guidInstance;
			g_netActiveTransportType = NET_TRANSPORT_TCPIP;
			g_session.phase = SESSION_PLAYER;
			break;
		}
		case SESSION_PLAYER: {
			int player = Net_CreateDirectPlayPlayer(g_session.info, g_session.player);
			if (player == XVT_NETWORK_PENDING)
				break;
			if (!player)
				return XvtNetworkSession_Fail(AERON_DPLAY_DIRECTORY_ERROR_CONNECTION_FAILED);
			g_frontState.netPlayers[0].playerId = player;
			g_frontState.netRuntimeLocalPlayer = g_frontState.netPlayers[0];
			g_session.phase = g_session.host ? SESSION_GROUP : SESSION_ROSTER;
			break;
		}
		case SESSION_GROUP:
			result = g_frontState.netDirectPlay->lpVtbl->CreateGroup(
				g_frontState.netDirectPlay, &g_frontState.netGroupDplayId, NULL, NULL, 0, 0);
			if (result == DPERR_PENDING || result == DPERR_BUSY)
				break;
			if (result)
				return XvtNetworkSession_Fail(AERON_DPLAY_DIRECTORY_ERROR_CONNECTION_FAILED);
			g_frontState.netRuntimeReliablePeerSlots[0].directPlayId = g_frontState.netGroupDplayId;
			g_frontState.netSequenceCount = 1;
			g_session.phase = SESSION_ROSTER;
			break;
		case SESSION_ROSTER:
			g_frontState.netPlayerCount = 1;
			Net_RefreshPlayerRoster();
			g_frontState.netRuntimeRecvQueueReadIndex = g_frontState.netRuntimeRecvQueueWriteIndex = 0;
			g_frontState.netRuntimeRecvQueueCount = 0;
			if (g_session.host) {
				g_frontState.netHostPlayerId = g_frontState.netRuntimeLocalPlayer.playerId;
				Net_SetPlayerReady(Net_GetLocalPlayerId());
				snprintf(g_mpRoster[0].name, sizeof(g_mpRoster[0].name), "%s", g_session.player);
				g_mpRoster[0].playerId = Net_GetLocalPlayerId();
				g_mpRoster[0].pilotRating = g_pilotData.rating;
				g_session.phase = g_session.online ? SESSION_REGISTER : SESSION_ESTABLISHED;
			} else
				g_session.phase = SESSION_HANDSHAKE;
			break;
		case SESSION_REGISTER:
			Net_PumpIncomingPackets();
			return XvtNetworkSession_Register();
		case SESSION_HANDSHAKE:
			return XvtNetworkSession_Handshake();
		case SESSION_ADMISSION:
		case SESSION_ESTABLISHED:
			return 1;
		case SESSION_FAILED:
		case SESSION_IDLE:
			return 0;
	}
	return XVT_NETWORK_PENDING;
}

int XvtNetworkSession_Admission(DPID sender, DPID player) {
	if (g_session.phase != SESSION_ADMISSION || sender != g_frontState.netHostPlayerId ||
		player != g_frontState.netRuntimeLocalPlayer.playerId || g_session.lost ||
		Aeron_NowUs() >= g_session.deadline)
		return 0;
	AeronDplayDirectory_FinishJoin();
	g_session.deadline = 0;
	g_session.phase = SESSION_ESTABLISHED;
	return 1;
}

void XvtNetworkSession_Reject(void) {
	XvtNetworkSession_Leave();
	AeronDplayDirectory_FinishJoin();
	g_session.cancel_join = 0;
}

void XvtNetworkSession_HostLost(void) { g_session.lost = 1; }

int XvtNetworkSession_IsLost(void) { return g_session.lost; }

XvtNetworkSessionStatus XvtNetworkSession_GetStatus(void) {
	XvtNetworkSessionStatus status = { XVT_NETWORK_SESSION_IDLE, g_session.error };
	if (g_session.phase == SESSION_FAILED)
		status.state = XVT_NETWORK_SESSION_FAILED;
	else if (g_session.phase == SESSION_ESTABLISHED)
		status.state = XVT_NETWORK_SESSION_ESTABLISHED;
	else if (g_session.phase == SESSION_ADMISSION)
		status.state = XVT_NETWORK_SESSION_ADMISSION;
	else if (g_session.phase != SESSION_IDLE || g_session.closing)
		status.state = XVT_NETWORK_SESSION_PENDING;
	return status;
}

void XvtNetworkSession_Service(void) {
	if (g_session.closing && !AeronDplay_IsActive()) {
		if (g_session.cancel_join)
			AeronDplayDirectory_CancelJoin();
		g_session.cancel_join = g_session.closing = 0;
	}
	if (g_session.phase == SESSION_FAILED || g_session.phase == SESSION_IDLE || g_session.closing)
		return;
	if (g_session.lost && !g_session.flight) {
		XvtNetworkSession_Fail(AERON_DPLAY_DIRECTORY_ERROR_CONNECTION_FAILED);
		return;
	}
	if (g_session.deadline) {
		AeronDplayJoinStatus join;
		AeronDplayDirectory_GetJoinStatus(&join);
		if (Aeron_NowUs() >= g_session.deadline || join.preparation.state == AERON_DPLAY_DIRECTORY_FAILED) {
			XvtNetworkSession_Fail(join.preparation.error ? join.preparation.error
														  : AERON_DPLAY_DIRECTORY_ERROR_TIMEOUT);
			return;
		}
	}
	if (!g_session.host || !g_session.registered || g_session.phase != SESSION_ESTABLISHED || g_session.lost)
		return;
	XvtNetworkMetadata current;
	if (g_session.flight) {
		current = g_session.metadata;
		if (g_session.flight_ready)
			XvtNetworkMetadata_Flight(&current);
	} else {
		int accepting =
			g_frontState.screenStates[g_frontState.screenStackTop].updateFn == MissionSetup_Update &&
			g_frontState.frameCounter > 0;
		XvtNetworkMetadata_Build(&current, accepting);
	}
	if (!current.room.players)
		return;
	if (memcmp(&current.room, &g_session.published, sizeof(current.room)) &&
		!AeronDplayDirectory_UpdateHost(&current.room))
		g_session.published = current.room;
	g_session.metadata = current;
	AeronDplayDirectoryStatus status;
	AeronDplayDirectory_GetHostStatus(&status);
	uint64_t now = Aeron_NowUs();
	if (status.state == AERON_DPLAY_DIRECTORY_FAILED && now >= g_session.retry) {
		AeronDplayDirectory_StartHosting(&g_session.instance, &g_session.published);
		g_session.retry = now + 15000000;
	}
}

void XvtNetworkSession_BeginFlight(void) {
	if (g_session.phase != SESSION_ESTABLISHED)
		return;
	if (g_session.host && g_session.registered) {
		XvtNetworkMetadata current;
		XvtNetworkMetadata_Build(&current, 0);
		if (current.room.players)
			g_session.metadata = current;
		g_session.metadata.room.state = AERON_DPLAY_ROOM_FLIGHT;
		g_session.metadata.room.joinable = 0;
		if (!AeronDplayDirectory_UpdateHost(&g_session.metadata.room))
			g_session.published = g_session.metadata.room;
	}
	g_session.flight = 1;
	g_session.flight_ready = 0;
}

void XvtNetworkSession_FlightReady(void) { g_session.flight_ready = 1; }

void XvtNetworkSession_EndFlight(void) {
	if (!g_session.flight)
		return;
	g_session.flight = g_session.flight_ready = 0;
	Net_RefreshPlayerRoster();
}

void XvtNetworkSession_Reset(void) { XvtNetworkSession_Cancel(); }

void XvtNetworkSession_Shutdown(void) {
	memset(&g_session, 0, sizeof(g_session));
	g_origin[0] = 0;
}
