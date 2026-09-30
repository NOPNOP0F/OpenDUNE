/** @file saturn/common/files.h Files for newlib: the CD and a RAM store.
 *
 * Paths starting with "CD/" name read-only files in the disc's root
 * directory (read through SBL's GFS). Any other path names a personal file
 * kept in RAM, identified by its last path component; "." is its directory. */

#ifndef SATURN_FILES_H
#define SATURN_FILES_H

#include <stddef.h>
#include <stdint.h>

#define FILES_CD_PREFIX "CD/"

/* Call back for every file in the disc's root directory; stops when the
 * callback returns 0. Returns 0 if the disc can't be read.
 * (int, not bool: the engine includes this and has its own bool.) */
extern int files_cd_list(int (*callback)(const char *name, uint32_t size, void *data), void *data);

/* write() for file descriptors other than stdout/stderr. */
extern int files_write(int fd, const void *buffer, size_t length);

#endif /* SATURN_FILES_H */
