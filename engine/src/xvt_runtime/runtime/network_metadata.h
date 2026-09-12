#ifndef XVT_RUNTIME_NETWORK_METADATA_H
#define XVT_RUNTIME_NETWORK_METADATA_H

#include "aeron/compat/dplay.h"
#include "aeron/compat/dplay_directory.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XvtNetworkMetadata {
	AeronDplayRoomMetadata room;
	DPID players[AERON_DPLAY_DIRECTORY_MAX_PLAYERS];
} XvtNetworkMetadata;

void XvtNetworkMetadata_Build(XvtNetworkMetadata* out, int accepting);
void XvtNetworkMetadata_Flight(XvtNetworkMetadata* snapshot);
void XvtNetworkMetadata_ToUtf8(char* out, size_t capacity, const char* text, size_t size);
void XvtNetworkMetadata_FromUtf8(char* out, size_t capacity, const char* text);

#ifdef __cplusplus
}
#endif

#endif
