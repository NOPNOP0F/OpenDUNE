/** @file src/os/readdir_saturn.c Directory traversal on the Sega Saturn.
 *
 * Only the data directory exists as a real directory: the root of the disc,
 * reached through the "CD/" prefix (see saturn/common/files.h). */

#include <stdio.h>
#include <string.h>

#include "types.h"
#include "readdir.h"
#include "error.h"

#include "files.h"

typedef struct ReadDirContext {
	const char *dirpath;
	bool (*callback)(const char *name, const char *path, uint32 size);
	bool ok;
} ReadDirContext;

static int ReadDir_ProcessCdFile(const char *name, uint32_t size, void *data)
{
	ReadDirContext *context = data;
	char path[64];

	snprintf(path, sizeof(path), "%s%s", context->dirpath, name);
	if (!context->callback(name, path, (uint32)size)) {
		context->ok = false;
		return 0;
	}
	return 1;
}

bool ReadDir_ProcessAllFiles(const char * dirpath, bool (*cb)(const char * name, const char * path, uint32 size))
{
	ReadDirContext context;

	if (strcmp(dirpath, FILES_CD_PREFIX) != 0) {
		Error("Only %s can be listed, not %s\n", FILES_CD_PREFIX, dirpath);
		return false;
	}

	context.dirpath = dirpath;
	context.callback = cb;
	context.ok = true;
	if (!files_cd_list(ReadDir_ProcessCdFile, &context)) {
		Error("Cannot read the disc\n");
		return false;
	}
	return context.ok;
}
