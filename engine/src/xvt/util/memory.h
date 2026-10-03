#ifndef XVT_UTIL_MEMORY_H
#define XVT_UTIL_MEMORY_H

#include "xvt/xvt_typedefs.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct MemoryHandleTableState {
	/* Bytes in each handle's block; 0 when free. */
	size_t sizeTable[32768];
	void *ptrTable[32768]; /* Each handle's block; NULL when free. */
};

extern uint8_t g_handleAllocatorInitialized;
extern unsigned int g_handleAllocationAttemptCount;
extern struct MemoryHandleTableState g_handleTables;

int Memory_SetRegionExecuteReadWrite(void *address, size_t size);
uint16_t Memory_AllocHandleZeroed(size_t size, int legacyTag);
uint16_t Memory_AllocHandle(size_t size, int legacyTag);
uint16_t Memory_AllocHandleInternal(size_t size, int legacyTag, int clearFlag);
void Memory_FreeHandle(unsigned int handle);
void *Memory_GetHandleBlock(uint16_t handle);
void Memory_HandleBlockDoneStub(uint16_t handle);

#ifdef __cplusplus
}
#endif

#endif
