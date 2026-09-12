#include "xvt/util/memory.h"

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_assets.h"
#endif

#include <stdlib.h>
#include <string.h>

#ifndef XVT_MODERN
__declspec(dllimport) int __stdcall VirtualProtect(void* address, size_t size, unsigned int newProtection,
												   unsigned int* oldProtection);
#endif

// GLOBAL: XVT 0x5280DC
uint8_t g_handleAllocatorInitialized = 0;
// GLOBAL: XVT 0x5280E0
unsigned int g_handleAllocationAttemptCount = 0;
// GLOBAL: XVT 0x622CE8
MemoryHandleTableState g_handleTables = { 0 };

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4AC490
int Memory_SetRegionExecuteReadWrite(void* address, size_t size) {
#ifdef XVT_MODERN
	/* The source port does not patch its generated machine code. */
	(void)address;
	(void)size;
	return 1;
#else
	unsigned int oldProtection;

	return VirtualProtect(address, size, 0x40u, &oldProtection);
#endif
}

// FUNCTION: XVT 0x4AC5D0
uint16_t Memory_AllocHandleZeroed(size_t size, int legacyTag) {
	return Memory_AllocHandleInternal(size, legacyTag, 1);
}

// FUNCTION: XVT 0x4AC5F0
uint16_t Memory_AllocHandle(size_t size, int legacyTag) {
	return Memory_AllocHandleInternal(size, legacyTag, 0);
}

// FUNCTION: XVT 0x4AC610
uint16_t Memory_AllocHandleInternal(size_t size, int legacyTag, int clearFlag) {
	uint16_t tableIndex;
	unsigned int slotIndex;
	void* block;

	(void)legacyTag;
	++g_handleAllocationAttemptCount;
	if (g_handleAllocatorInitialized == 0) {
		for (tableIndex = 0; tableIndex < 32768u; ++tableIndex) {
			g_handleTables.ptrTable[tableIndex] = NULL;
			g_handleTables.sizeTable[tableIndex] = 0;
		}
		g_handleAllocatorInitialized = 1;
	}

	for (tableIndex = 0; tableIndex < 32768u; ++tableIndex) {
		if (g_handleTables.ptrTable[tableIndex] == NULL) {
			break;
		}
	}
	if (tableIndex == 32768u) {
		return 0;
	}

	slotIndex = tableIndex;
	block = malloc(size);
	g_handleTables.ptrTable[slotIndex] = block;
	if (block == NULL) {
		return 0;
	}
	if (clearFlag != 0) {
		memset(block, 0, size);
	}
	g_handleTables.sizeTable[slotIndex] = size;
	return tableIndex + 1;
}

// FUNCTION: XVT 0x4AC6E0
void Memory_FreeHandle(unsigned int handle) {
#ifdef XVT_MODERN
	XvtRenderAssets_FreeHandle(handle);
#endif
	if (g_handleTables.ptrTable[handle - 1] != NULL) {
		free(g_handleTables.ptrTable[handle - 1]);
	}
	g_handleTables.sizeTable[handle - 1] = 0;
	g_handleTables.ptrTable[handle - 1] = NULL;
}

// FUNCTION: XVT 0x4AC720
void* Memory_LockHandle(uint16_t handle) { return g_handleTables.ptrTable[handle - 1]; }

// FUNCTION: XVT 0x4AC740
void Memory_UnlockHandle(uint16_t handle) { (void)handle; }
