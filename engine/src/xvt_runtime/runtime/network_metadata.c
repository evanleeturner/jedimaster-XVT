#include "xvt_runtime/runtime/network_metadata.h"
#include "xvt/frontend/config.h"
#include "xvt/frontend/frontend_state.h"
#include "xvt/frontend/mission_setup.h"
#include "xvt/frontend/pilot_record.h"
#include "xvt/net/net.h"
#include "xvt/net/net_session.h"
#include <string.h>

static const uint16_t g_windows1252[32] = { 0x20ac, 0,      0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
											0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017d, 0,
											0,      0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
											0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0,      0x017e, 0x0178 };

void XvtNetworkMetadata_ToUtf8(char* out, size_t capacity, const char* text, size_t size) {
	size_t written = 0;
	if (!capacity)
		return;
	for (size_t i = 0; i < size && text[i]; ++i) {
		unsigned cp = (uint8_t)text[i];
		if (cp >= 128 && cp < 160)
			cp = g_windows1252[cp - 128];
		if (cp < 32 || cp == 127)
			cp = '?';
		unsigned bytes = cp < 128 ? 1 : cp < 2048 ? 2 : 3;
		if (written + bytes >= capacity)
			break;
		if (bytes == 3) {
			out[written++] = (char)(0xe0 | (cp >> 12));
			out[written++] = (char)(0x80 | ((cp >> 6) & 63));
		} else if (bytes == 2)
			out[written++] = (char)(0xc0 | (cp >> 6));
		out[written++] = (char)(bytes == 1 ? cp : 0x80 | (cp & 63));
	}
	out[written] = 0;
}

void XvtNetworkMetadata_FromUtf8(char* out, size_t capacity, const char* text) {
	size_t written = 0;
	const uint8_t* source = (const uint8_t*)text;
	if (!capacity)
		return;
	while (*source && written + 1 < capacity) {
		unsigned lead = *source++, cp = lead, extra = 0, minimum = 0;
		if (lead >= 0xc2 && lead <= 0xdf) {
			cp &= 31;
			extra = 1;
			minimum = 128;
		} else if (lead >= 0xe0 && lead <= 0xef) {
			cp &= 15;
			extra = 2;
			minimum = 2048;
		} else if (lead >= 0xf0 && lead <= 0xf4) {
			cp &= 7;
			extra = 3;
			minimum = 65536;
		} else if (lead >= 128)
			cp = 0;
		for (unsigned i = 0; i < extra; ++i) {
			if ((*source & 0xc0) != 0x80) {
				cp = 0;
				break;
			}
			cp = (cp << 6) | (*source++ & 63);
		}
		unsigned byte = '?';
		if (cp >= minimum && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff)) {
			if ((cp >= 32 && cp < 127) || (cp >= 160 && cp <= 255))
				byte = cp;
			else
				for (unsigned i = 0; i < 32; ++i)
					if (cp && cp == g_windows1252[i]) {
						byte = 128 + i;
						break;
					}
		}
		out[written++] = (char)byte;
	}
	out[written] = 0;
}

static void XvtNetworkMetadata_Add(XvtNetworkMetadata* out, const NetPlayerInfo* player) {
	unsigned index = out->room.players;
	if (!player || !player->playerId || player->readyFlag != 1 || index == 8)
		return;
	for (unsigned i = 0; i < index; ++i)
		if (out->players[i] == player->playerId)
			return;
	out->players[index] = player->playerId;
	XvtNetworkMetadata_ToUtf8(out->room.roster[index].name, sizeof(out->room.roster[index].name),
							  player->sessionName, sizeof(player->sessionName));
	if (!out->room.roster[index].name[0])
		strcpy(out->room.roster[index].name, "No name");
	unsigned rating = (uint8_t)player->playerName[0];
	out->room.roster[index].rating = rating ? (uint8_t)(rating - 1) : 0;
	++out->room.players;
}

void XvtNetworkMetadata_Build(XvtNetworkMetadata* out, int accepting) {
	memset(out, 0, sizeof(*out));
	out->room.max_players = 8;
	out->room.password_required = g_gameConfig.requirePassword != 0;
	XvtNetworkMetadata_ToUtf8(out->room.name, sizeof(out->room.name), g_frontState.netSessionName,
							  sizeof(g_frontState.netSessionName));
	if (!out->room.name[0])
		strcpy(out->room.name, "Internet game.");
	int directory = g_pilotData.missionDirectoryId;
	if ((unsigned)directory < 6 && g_pilotData.missionDescriptionIds[directory] >= 0) {
		out->room.mission.present = 1;
		out->room.mission.directory = (uint8_t)directory;
		out->room.mission.id = g_pilotData.missionDescriptionIds[directory];
	}
	if (g_missionSetupRosterAuthoritative) {
		for (unsigned i = 0; i < 8; ++i)
			XvtNetworkMetadata_Add(out, Net_FindPlayer(g_mpRoster[i].playerId));
	} else {
		int count;
		NetPlayerInfo* players = Net_GetPlayerRoster(&count);
		for (int i = 0; i < count && i < 32; ++i)
			XvtNetworkMetadata_Add(out, &players[i]);
	}
	out->room.joinable = accepting && !g_missionSetupRosterAuthoritative && out->room.players < 8;
}

void XvtNetworkMetadata_Flight(XvtNetworkMetadata* snapshot) {
	unsigned count = 0;
	for (unsigned i = 0; i < snapshot->room.players; ++i) {
		for (unsigned j = 0; j < 8; ++j) {
			if ((DPID)g_netSession.players[j].directPlayId == snapshot->players[i] &&
				g_netSession.players[j].activeFlag) {
				snapshot->players[count] = snapshot->players[i];
				snapshot->room.roster[count++] = snapshot->room.roster[i];
				break;
			}
		}
	}
	for (unsigned i = count; i < 8; ++i) {
		snapshot->players[i] = 0;
		memset(&snapshot->room.roster[i], 0, sizeof(snapshot->room.roster[i]));
	}
	snapshot->room.players = (uint8_t)count;
}
