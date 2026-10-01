/** @file saturn/common/backup.h Files in backup memory, compressed.
 *
 * Through the BIOS's backup library (SBL's sega_bup.h): the Saturn's own
 * 32 KB, or a backup cartridge. Each file holds its unpacked size (4 bytes,
 * high byte first) and the data packed with pack.h. Names are up to 11
 * characters. */

#ifndef SATURN_BACKUP_H
#define SATURN_BACKUP_H

#include <stdint.h>

enum {
	BACKUP_INTERNAL = 0,
	BACKUP_CARTRIDGE = 1,
	BACKUP_NAME_LENGTH = 11
};

/* Whether a backup cartridge is connected. */
extern int Backup_HasCartridge(void);

/* Free bytes on a device, or -1 if it isn't there. */
extern int32_t Backup_Free(int device);

/* Use this device from now on (BACKUP_INTERNAL by default). */
extern void Backup_Select(int device);

/* Read a file into memory from malloc(); returns 0 if there is none (or it
 * can't be read). */
extern int Backup_Read(const char *name, uint8_t **data, uint32_t *size);

/* Write (or replace) a file; comment is shown by the BIOS's memory manager
 * (up to 10 characters). Returns 0 if it doesn't fit. */
extern int Backup_Write(const char *name, const char *comment, const uint8_t *data, uint32_t size);

extern void Backup_Delete(const char *name);

#endif /* SATURN_BACKUP_H */
