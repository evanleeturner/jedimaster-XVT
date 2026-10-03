#include "xvt/util/memory.h"

#ifdef XVT_MODERN
#include "xvt_runtime/snapshot/render_assets.h"
#endif

#include <stdlib.h>
#include <string.h>

#ifndef XVT_MODERN
__declspec(dllimport) int __stdcall VirtualProtect(void *address, size_t size,
						   unsigned int newProtection,
						   unsigned int *oldProtection);
#endif

/* 1 once the first Memory_AllocHandleInternal call has cleared g_handleTables;
 * only that function writes it, and nothing sets it back to 0. */
// GLOBAL: XVT 0x5280DC
uint8_t g_handleAllocatorInitialized = 0;
/* Counts calls to Memory_AllocHandleInternal, failed ones included. Only that
 * function writes it, and nothing reads it. */
// GLOBAL: XVT 0x5280E0
unsigned int g_handleAllocationAttemptCount = 0;
/* The handle allocator's tables, indexed by handle - 1: each block's pointer
 * and size, with a NULL pointer marking a free slot. Only
 * Memory_AllocHandleInternal and Memory_FreeHandle write it. The modern build
 * also reads it: XvtFrontendTask_Shutdown frees every live handle, and
 * XvtRenderCapture_CaptureView reads the object table's size from it. */
// GLOBAL: XVT 0x622CE8
MemoryHandleTableState g_handleTables = {0};

/* In the original build, makes size bytes from address readable, writable and
 * executable (VirtualProtect with 0x40) and returns VirtualProtect's result:
 * nonzero on success, 0 on failure. The modern build changes nothing and
 * returns 1. Its one caller, RenderScene_AllocateBuffers, passes an address
 * 0x217 bytes into its own code and a size of 0x80000, and ignores the
 * result. */
// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4AC490
int Memory_SetRegionExecuteReadWrite(void *address, size_t size)
{
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

/* Memory_AllocHandleInternal with the block filled with zeros. */
// FUNCTION: XVT 0x4AC5D0
uint16_t Memory_AllocHandleZeroed(size_t size, int legacyTag)
{
	return Memory_AllocHandleInternal(size, legacyTag, 1);
}

/* Memory_AllocHandleInternal without clearing: the block holds whatever malloc
 * left in it. */
// FUNCTION: XVT 0x4AC5F0
uint16_t Memory_AllocHandle(size_t size, int legacyTag)
{
	return Memory_AllocHandleInternal(size, legacyTag, 0);
}

/* Allocates size bytes with malloc and returns a handle to them, 1 to 32768:
 * one more than the first free slot of g_handleTables. Returns 0 when all 32768
 * slots are in use or malloc fails, and the slot stays free. Fills the block
 * with zeros when clearFlag is nonzero. The first call clears both tables and
 * sets g_handleAllocatorInitialized; every call adds 1 to
 * g_handleAllocationAttemptCount. Ignores legacyTag. */
// FUNCTION: XVT 0x4AC610
uint16_t Memory_AllocHandleInternal(size_t size, int legacyTag, int clearFlag)
{
	uint16_t tableIndex;
	unsigned int slotIndex;
	void *block;

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

/* Frees a handle's block and marks its slot of g_handleTables free (NULL
 * pointer, size 0); a slot already free is just cleared again. The modern build
 * first passes the handle to XvtRenderAssets_RetireHandle. Does not check the
 * handle: 0 or one over 32768 writes outside the tables. */
// FUNCTION: XVT 0x4AC6E0
void Memory_FreeHandle(unsigned int handle)
{
#ifdef XVT_MODERN
	XvtRenderAssets_RetireHandle(handle);
#endif
	if (g_handleTables.ptrTable[handle - 1] != NULL) {
		free(g_handleTables.ptrTable[handle - 1]);
	}
	g_handleTables.sizeTable[handle - 1] = 0;
	g_handleTables.ptrTable[handle - 1] = NULL;
}

/* Returns a handle's block, NULL when its slot is free. Despite the name,
 * nothing is locked: a block keeps its address until Memory_FreeHandle. Does
 * not check the handle: 0 or one over 32768 reads outside the table. */
// FUNCTION: XVT 0x4AC720
void *Memory_LockHandle(uint16_t handle)
{
	return g_handleTables.ptrTable[handle - 1];
}

/* Does nothing: Memory_LockHandle locks nothing to undo. */
// FUNCTION: XVT 0x4AC740
void Memory_UnlockHandle(uint16_t handle) { (void)handle; }
