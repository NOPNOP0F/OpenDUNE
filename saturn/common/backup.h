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
extern int backup_has_cartridge(void);

/* If a backup cartridge is connected, ask on the console (with the pad)
 * whether to use it or the Saturn's own memory. Call before anything uses
 * backup memory, while no VBlank handler reads the pad. */
extern void backup_choose(void);

/* Use this device from now on (BACKUP_INTERNAL by default). */
extern void backup_select(int device);

/* Read a file into memory from malloc(); returns 0 if there is none (or it
 * can't be read). */
extern int backup_read(const char *name, uint8_t **data, uint32_t *size);

/* Write (or replace) a file; comment is shown by the BIOS's memory manager
 * (up to 10 characters). Returns 0 if it doesn't fit. */
extern int backup_write(const char *name, const char *comment, const uint8_t *data, uint32_t size);

extern void backup_delete(const char *name);

#endif /* SATURN_BACKUP_H */
