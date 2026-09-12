#include "xvt_runtime/compat/pilot_port.h"

#include "aeron/vfs.h"
#include "xvt_runtime/storage/storage.h"

int Pilot_RemoveFileModern(const char* fileName) {
	int status = XvtStorage_Probe(AERON_VFS_ROOT_USER, fileName);
	return status == 0 || (status > 0 && XvtStorage_Remove(fileName) == 0);
}
