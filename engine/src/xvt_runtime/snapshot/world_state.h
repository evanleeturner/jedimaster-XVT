#ifndef XVT_RUNTIME_SNAPSHOT_WORLD_STATE_H
#define XVT_RUNTIME_SNAPSHOT_WORLD_STATE_H

#include <stddef.h>
#include <stdint.h>

size_t XvtSnapshot_Encode(uint8_t* image, size_t capacity);
int XvtSnapshot_Validate(const uint8_t* image, size_t size);
int XvtSnapshot_Decode(const uint8_t* image, size_t size);
int XvtSnapshot_ChecksumImage(const uint8_t* image, size_t size, unsigned checksums[16],
							  unsigned lengths[16]);
void XvtSnapshot_Save(void);
void XvtSnapshot_Restore(void);
size_t XvtSnapshot_CalculateSize(void);
void XvtSnapshot_Checksum(int unusedArg0, int unusedArg1);
int XvtSnapshot_BuildPresenceMap(uint8_t* outMap, uint8_t* worldState);
void XvtSnapshot_ApplyPresenceMap(const uint8_t* presenceMap);
int XvtSnapshot_LiveChecksum(void);

#endif
