/** @file src/os/readdir_saturn.c Directory traversal on the Sega Saturn.
 *
 * There is no file system yet, so the data directory can't be listed. */

#include "types.h"
#include "readdir.h"
#include "error.h"

bool ReadDir_ProcessAllFiles(const char * dirpath, bool (*cb)(const char * name, const char * path, uint32 size))
{
	VARIABLE_NOT_USED(cb);
	Error("No file system to list %s\n", dirpath);
	return false;
}
