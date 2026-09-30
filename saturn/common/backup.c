/** @file saturn/common/backup.c Files in backup memory, compressed.
 *
 * The backup library lives in the BIOS: BUP_Init() copies it into a 16 KB
 * buffer and takes an 8 KB work area. Both are only allocated while a file
 * is read or written. The reset button is off while the library runs, as
 * SBL's sample does, so a reset can't leave a file half written. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "backup.h"
#include "pack.h"
#include "saturn_hw.h"
#include "smpc.h"

/* From SBL's SEGA_BUP.H: the BIOS keeps the library's address and, after
 * BUP_Init, the address of its function table. */
#define BUP_LIB_ADDRESS     (*(volatile uint32_t *)(0x6000350 + 8))
#define BUP_VECTOR_ADDRESS  (*(volatile uint32_t *)(0x6000350 + 4))
#define BUP_FUNCTION(n)     (*(uint32_t *)(BUP_VECTOR_ADDRESS + 4 * (n)))

typedef struct BupConfig {
	uint16_t unit_id;       /* 0: not connected */
	uint16_t partition;
} BupConfig;

typedef struct BupStat {
	uint32_t totalsize, totalblock, blocksize, freesize, freeblock, datanum;
} BupStat;

typedef struct BupDir {
	uint8_t filename[12];
	uint8_t comment[11];
	uint8_t language;
	uint32_t date;
	uint32_t datasize;
	uint16_t blocksize;
} BupDir;

typedef struct BupDate {
	uint8_t year;           /* - 1980 */
	uint8_t month, day, time, min, week;
} BupDate;

#define BUP_Init    ((void (*)(volatile uint32_t *lib, uint32_t *work, BupConfig config[3]))BUP_LIB_ADDRESS)
#define BUP_Format  ((int32_t (*)(uint32_t device))BUP_FUNCTION(2))
#define BUP_Stat    ((int32_t (*)(uint32_t device, uint32_t size, BupStat *stat))BUP_FUNCTION(3))
#define BUP_Write   ((int32_t (*)(uint32_t device, BupDir *dir, volatile uint8_t *data, uint8_t keep))BUP_FUNCTION(4))
#define BUP_Read    ((int32_t (*)(uint32_t device, uint8_t *name, volatile uint8_t *data))BUP_FUNCTION(5))
#define BUP_Delete  ((int32_t (*)(uint32_t device, uint8_t *name))BUP_FUNCTION(6))
#define BUP_Dir     ((int32_t (*)(uint32_t device, uint8_t *name, uint16_t count, BupDir *dir))BUP_FUNCTION(7))
#define BUP_SetDate ((uint32_t (*)(BupDate *date))BUP_FUNCTION(10))

enum {
	LIB_SIZE = 16 * 1024,
	WORK_SIZE = 8 * 1024,
	BUP_ENGLISH = 1,
	BUP_UNFORMAT = 2,
	DIR_MAX = 16
};

static uint32_t *s_lib, *s_work;
static BupConfig s_config[3];
static int s_device = BACKUP_INTERNAL;

static int bup_begin(void)
{
	s_lib = malloc(LIB_SIZE);
	s_work = malloc(WORK_SIZE);
	if (s_lib == NULL || s_work == NULL) {
		free(s_lib);
		free(s_work);
		return 0;
	}
	smpc_command(SMPC_CMD_RESDISA);
	BUP_Init(s_lib, s_work, s_config);
	return 1;
}

static void bup_end(void)
{
	smpc_command(SMPC_CMD_RESENAB);
	free(s_lib);
	free(s_work);
	s_lib = s_work = NULL;
}

/* Whether the selected device is there and formatted (formatting it if it
 * isn't, as the BIOS would). */
static int bup_ready(void)
{
	BupStat stat;
	if (s_config[s_device].unit_id == 0) return 0;
	if (BUP_Stat((uint32_t)s_device, 0, &stat) == BUP_UNFORMAT && BUP_Format((uint32_t)s_device) != 0) return 0;
	return 1;
}

/* Find a file by its exact name (BUP_Dir matches the start of names). */
static int bup_find(const char *name, BupDir *found)
{
	BupDir dir[DIR_MAX];
	int32_t count = BUP_Dir((uint32_t)s_device, (uint8_t *)name, DIR_MAX, dir);
	int32_t i;

	if (count > DIR_MAX) count = DIR_MAX;
	for (i = 0; i < count; i++) {
		if (strncmp((const char *)dir[i].filename, name, BACKUP_NAME_LENGTH) == 0) {
			*found = dir[i];
			return 1;
		}
	}
	return 0;
}

/* Now, as a backup library date stamp. */
static uint32_t bup_now(void)
{
	uint8_t clock[7];
	BupDate date;
	int year;

	smpc_read_clock(clock);
	year = (clock[0] >> 4) * 1000 + (clock[0] & 0xF) * 100 + (clock[1] >> 4) * 10 + (clock[1] & 0xF);
	date.year = (uint8_t)(year - 1980);
	date.month = clock[2] & 0xF;
	date.week = clock[2] >> 4;
	date.day = (uint8_t)((clock[3] >> 4) * 10 + (clock[3] & 0xF));
	date.time = (uint8_t)((clock[4] >> 4) * 10 + (clock[4] & 0xF));
	date.min = (uint8_t)((clock[5] >> 4) * 10 + (clock[5] & 0xF));
	return BUP_SetDate(&date);
}

int backup_has_cartridge(void)
{
	int present;
	if (!bup_begin()) return 0;
	present = s_config[BACKUP_CARTRIDGE].unit_id != 0;
	bup_end();
	return present;
}

/* Free bytes on a device, or -1 if it isn't there. */
static int32_t bup_free(int device)
{
	BupStat stat;
	int saved = s_device;
	int32_t free_bytes = -1;

	s_device = device;
	if (bup_ready() && BUP_Stat((uint32_t)device, 0, &stat) == 0) free_bytes = (int32_t)stat.freesize;
	s_device = saved;
	return free_bytes;
}

void backup_choose(void)
{
	int32_t internal, cartridge;
	uint16_t pad;

	if (!bup_begin()) return;
	if (s_config[BACKUP_CARTRIDGE].unit_id == 0) {
		bup_end();
		return;
	}
	internal = bup_free(BACKUP_INTERNAL);
	cartridge = bup_free(BACKUP_CARTRIDGE);
	bup_end();

	printf("Backup memory cartridge found.\n\nSave games and settings go to:\n\n");
	printf("  A  the Saturn's memory (%ld KB free)\n", (long)(internal < 0 ? 0 : internal / 1024));
	printf("  C  the cartridge (%ld KB free)\n", (long)(cartridge < 0 ? 0 : cartridge / 1024));

	while (smpc_pad_read() != 0) {}
	do {
		pad = smpc_pad_read();
	} while ((pad & (PAD_A | PAD_C)) == 0);
	s_device = (pad & PAD_C) ? BACKUP_CARTRIDGE : BACKUP_INTERNAL;
	printf("\nUsing %s.\n", s_device == BACKUP_CARTRIDGE ? "the cartridge" : "the Saturn's memory");
	while (smpc_pad_read() != 0) {}
}

void backup_select(int device)
{
	s_device = device;
}

int backup_read(const char *name, uint8_t **data, uint32_t *size)
{
	BupDir dir;
	uint8_t *packed = NULL, *unpacked = NULL;
	uint32_t length = 0;
	int ok = 0;

	if (!bup_begin()) return 0;
	if (bup_ready() && bup_find(name, &dir) && dir.datasize >= 4) {
		packed = malloc(dir.datasize);
		if (packed != NULL && BUP_Read((uint32_t)s_device, (uint8_t *)name, packed) == 0) ok = 1;
	}
	bup_end();

	if (ok) {
		length = ((uint32_t)packed[0] << 24) | ((uint32_t)packed[1] << 16) | ((uint32_t)packed[2] << 8) | packed[3];
		unpacked = malloc(length != 0 ? length : 1);
		ok = unpacked != NULL && pack_decompress(packed + 4, dir.datasize - 4, unpacked, length) == length;
	}
	free(packed);
	if (!ok) {
		free(unpacked);
		return 0;
	}
	*data = unpacked;
	*size = length;
	return 1;
}

int backup_write(const char *name, const char *comment, const uint8_t *data, uint32_t size)
{
	uint32_t capacity = size + size / 128 + 16;
	uint8_t *packed = malloc(4 + capacity);
	uint32_t length;
	BupDir dir;
	int ok = 0;

	if (packed == NULL) return 0;
	length = pack_compress(data, size, packed + 4, capacity);
	if (length == 0 && size != 0) {
		free(packed);
		return 0;
	}
	packed[0] = (uint8_t)(size >> 24);
	packed[1] = (uint8_t)(size >> 16);
	packed[2] = (uint8_t)(size >> 8);
	packed[3] = (uint8_t)size;

	if (bup_begin()) {
		if (bup_ready()) {
			memset(&dir, 0, sizeof(dir));
			strncpy((char *)dir.filename, name, BACKUP_NAME_LENGTH);
			strncpy((char *)dir.comment, comment, sizeof(dir.comment) - 1);
			dir.language = BUP_ENGLISH;
			dir.date = bup_now();
			dir.datasize = 4 + length;
			ok = BUP_Write((uint32_t)s_device, &dir, packed, 0) == 0;
		}
		bup_end();
	}
	free(packed);
	return ok;
}

void backup_delete(const char *name)
{
	if (!bup_begin()) return;
	if (bup_ready()) BUP_Delete((uint32_t)s_device, (uint8_t *)name);
	bup_end();
}
