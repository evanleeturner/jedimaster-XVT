#include "xvt_runtime/compat/pilot_port.h"

#include "aeron/vfs.h"
#include "xvt_runtime/storage/storage.h"

int pilot_remove_file_modern(const char *file_name)
{
	int status = xvt_storage_probe(AERON_VFS_ROOT_USER, file_name);
	return status == 0 ||
	       (status > 0 && xvt_storage_remove(file_name) == 0);
}
