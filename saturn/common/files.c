/** @file saturn/common/files.c Files for newlib: the CD and a RAM store.
 *
 * CD files are read in whole sectors through GFS into a small cache and
 * copied out from there, so any byte range can be read. Personal files
 * (configuration, saves) are kept in backup memory (backup.h) and worked on
 * in RAM: read in when opened, written back when a changed file is closed,
 * and dropped from RAM once stored and closed. */

#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "sega_gfs.h"

#include "backup.h"
#include "files.h"
#include "loading.h"

#undef errno
extern int errno;

enum {
	FD_FIRST = 3,           /* 0-2 are stdin/stdout/stderr */
	FD_COUNT = 16,
	OPEN_FILES_MAX = 8,
	DIR_MAX = 128,
	SECTOR_SIZE = 2048,
	CACHE_SECTORS = 16,
	RAM_FILE_MAX = 16,
	RAM_FILE_SIZE_MAX = 512 * 1024,
	NAME_MAX_LENGTH = 15
};

typedef struct RamFile {
	char name[NAME_MAX_LENGTH + 1];     /* empty: slot unused */
	uint8_t *data;
	uint32_t size;
	uint32_t capacity;
	int opens;          /* file descriptors on it */
	bool changed;       /* since it was read from backup memory */
	bool stored;        /* in backup memory as it is */
	bool deleted;       /* unlinked while open: gone once closed */
} RamFile;

typedef enum { FD_FREE, FD_CD, FD_RAM } FdKind;

typedef struct Fd {
	FdKind kind;
	int flags;
	uint32_t position;
	/* FD_CD */
	GfsHn gfs;
	int32_t fid;
	uint32_t size;
	/* FD_RAM */
	RamFile *ram;
} Fd;

static Fd s_fds[FD_COUNT];
static RamFile s_ramFiles[RAM_FILE_MAX];

static bool s_cdReady = false;
static uint32_t s_gfsWork[GFS_WORK_SIZE(OPEN_FILES_MAX) / sizeof(uint32_t) + 1];
static GfsDirName s_dirNames[DIR_MAX];
static GfsDirTbl s_dirTable;
static int32_t s_dirCount;

/* The sectors last read, kept when their file is closed: files on the disc
 * don't change, and the game reopens a file (a PAK) for each read. */
static uint8_t *s_cache;
static int32_t s_cacheFid = -1;
static uint32_t s_cacheFirst;       /* first sector held */
static uint32_t s_cacheCount;       /* sectors held */

/* Sectors read before, kept in VDP1's 512 KB of VRAM, which nothing else
 * uses (the picture is VDP2's): reading them again (tiles, scripts, the
 * Mentat's pictures, menus) then takes no CD seek. Slot i is at VRAM
 * (i + 1) * 2048; the first 2048 bytes keep an end-of-list command, so
 * VDP1 never takes the cached data for drawing commands. */
enum { VRAM_SLOTS = 255 };
#define VDP1_VRAM   0x25C00000UL
#define VDP1_PTMR   (*(volatile uint16_t *)0x25D00004UL)

typedef struct VramSlot {
	int32_t fid;            /* -1: free */
	uint32_t sector;
	uint32_t used;          /* when last used, for replacing the oldest */
} VramSlot;

static VramSlot s_vram[VRAM_SLOTS];
static uint32_t s_vramClock = 0;

static volatile uint32_t *Files_VramSlot(int i)
{
	return (volatile uint32_t *)(VDP1_VRAM + (uint32_t)(i + 1) * SECTOR_SIZE);
}

static void Files_VramInit(void)
{
	int i;
	VDP1_PTMR = 0;                                  /* no drawing */
	*(volatile uint16_t *)VDP1_VRAM = 0x8000;       /* end of command list */
	for (i = 0; i < VRAM_SLOTS; i++) s_vram[i].fid = -1;
}

static int Files_VramFind(int32_t fid, uint32_t sector)
{
	int i;
	for (i = 0; i < VRAM_SLOTS; i++) {
		if (s_vram[i].fid == fid && s_vram[i].sector == sector) {
			s_vram[i].used = ++s_vramClock;
			return i;
		}
	}
	return -1;
}

static void Files_VramStore(int32_t fid, uint32_t sector, const uint8_t *data)
{
	volatile uint32_t *dst;
	const uint32_t *src = (const uint32_t *)data;
	int i, oldest = 0;

	if (Files_VramFind(fid, sector) >= 0) return;
	for (i = 0; i < VRAM_SLOTS; i++) {
		if (s_vram[i].fid < 0) {
			oldest = i;
			break;
		}
		if (s_vram[i].used < s_vram[oldest].used) oldest = i;
	}
	dst = Files_VramSlot(oldest);
	for (i = 0; i < SECTOR_SIZE / 4; i++) dst[i] = src[i];
	s_vram[oldest].fid = fid;
	s_vram[oldest].sector = sector;
	s_vram[oldest].used = ++s_vramClock;
}

static void Files_VramLoad(int slot, uint8_t *data)
{
	volatile uint32_t *src = Files_VramSlot(slot);
	uint32_t *dst = (uint32_t *)data;
	int i;
	for (i = 0; i < SECTOR_SIZE / 4; i++) dst[i] = src[i];
}

static bool Files_CdInit(void)
{
	if (s_cdReady) return true;

	s_cache = malloc(CACHE_SECTORS * SECTOR_SIZE);
	if (s_cache == NULL) return false;
	Files_VramInit();

	GFS_DIRTBL_TYPE(&s_dirTable) = GFS_DIR_NAME;
	GFS_DIRTBL_DIRNAME(&s_dirTable) = s_dirNames;
	GFS_DIRTBL_NDIR(&s_dirTable) = DIR_MAX;
	s_dirCount = GFS_Init(OPEN_FILES_MAX, s_gfsWork, &s_dirTable);
	if (s_dirCount < 0) return false;

	s_cdReady = true;
	return true;
}

/* Last component of a path. */
static const char *Files_BaseName(const char *path)
{
	const char *slash = strrchr(path, '/');
	return (slash != NULL) ? slash + 1 : path;
}

static bool Files_IsCdPath(const char *path)
{
	return strncmp(path, FILES_CD_PREFIX, sizeof(FILES_CD_PREFIX) - 1) == 0;
}

static bool Files_IsDirectory(const char *path)
{
	return strcmp(path, ".") == 0 || strcmp(path, "./") == 0 ||
	       strcmp(path, "CD") == 0 || strcmp(path, FILES_CD_PREFIX) == 0;
}

static void Files_UpperName(char *dst, const char *src, size_t size)
{
	size_t i;
	for (i = 0; i + 1 < size && src[i] != '\0'; i++) {
		char c = src[i];
		dst[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
	}
	dst[i] = '\0';
}

static int32_t Files_CdFind(const char *path)
{
	char name[GFS_FNAME_LEN + 1];
	if (!Files_CdInit()) return -1;
	Files_UpperName(name, path + sizeof(FILES_CD_PREFIX) - 1, sizeof(name));
	return GFS_NameToId((Sint8 *)name);
}

static uint32_t Files_CdSize(int32_t fid)
{
	return (uint32_t)GFS_DIR_SIZE(&s_dirNames[fid]);
}

static RamFile *Files_RamFind(const char *path)
{
	char name[NAME_MAX_LENGTH + 1];
	int i;
	Files_UpperName(name, Files_BaseName(path), sizeof(name));
	for (i = 0; i < RAM_FILE_MAX; i++) {
		if (s_ramFiles[i].name[0] != '\0' && !s_ramFiles[i].deleted && strcmp(s_ramFiles[i].name, name) == 0) return &s_ramFiles[i];
	}
	return NULL;
}

static RamFile *Files_RamCreate(const char *path)
{
	int i;
	for (i = 0; i < RAM_FILE_MAX; i++) {
		RamFile *f = &s_ramFiles[i];
		if (f->name[0] != '\0') continue;
		Files_UpperName(f->name, Files_BaseName(path), sizeof(f->name));
		f->data = NULL;
		f->size = f->capacity = 0;
		return f;
	}
	return NULL;
}

static void Files_RamFree(RamFile *r)
{
	free(r->data);
	memset(r, 0, sizeof(*r));
}

/* Name in backup memory: "D2_" and up to 8 letters and digits of the file
 * name, before the extension ("_save000.dat" -> "D2_SAVE000"). */
static void Files_BackupName(const char *path, char name[BACKUP_NAME_LENGTH + 1])
{
	const char *src = Files_BaseName(path);
	int length = 3;

	memcpy(name, "D2_", 3);
	for (; *src != '\0' && *src != '.' && length < 3 + 8; src++) {
		char c = *src;
		if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) name[length++] = c;
	}
	name[length] = '\0';
}

/* Read a personal file in from backup memory, if it is there. */
static RamFile *Files_RamLoad(const char *path)
{
	char name[BACKUP_NAME_LENGTH + 1];
	uint8_t *data;
	uint32_t size;
	RamFile *r;

	Files_BackupName(path, name);
	if (!Backup_Read(name, &data, &size)) return NULL;
	r = Files_RamCreate(path);
	if (r == NULL) {
		free(data);
		return NULL;
	}
	r->data = data;
	r->size = r->capacity = size;
	r->stored = true;
	return r;
}

/* Write a changed personal file to backup memory. */
static bool Files_RamStore(RamFile *r)
{
	char name[BACKUP_NAME_LENGTH + 1];

	Files_BackupName(r->name, name);
	if (!Backup_Write(name, "Dune II", r->data, r->size)) return false;
	r->changed = false;
	r->stored = true;
	return true;
}

static Fd *Files_FdGet(int fd)
{
	if (fd < FD_FIRST || fd >= FD_FIRST + FD_COUNT) return NULL;
	if (s_fds[fd - FD_FIRST].kind == FD_FREE) return NULL;
	return &s_fds[fd - FD_FIRST];
}

static int Files_FdAlloc(void)
{
	int i;
	for (i = 0; i < FD_COUNT; i++) {
		if (s_fds[i].kind == FD_FREE) return FD_FIRST + i;
	}
	return -1;
}

int Files_CdList(int (*callback)(const char *name, uint32_t size, void *data), void *data)
{
	int32_t i;
	if (!Files_CdInit()) return 0;

	/* Entries 0 and 1 are the directory itself and its parent. The root
	 * holds no other directories. (The attribute byte can't tell: plain
	 * files come back with GFS_ATR_DIR set, at least in Ymir.) */
	for (i = 2; i < s_dirCount; i++) {
		char name[GFS_FNAME_LEN + 1];
		char *version;
		memcpy(name, s_dirNames[i].fname, GFS_FNAME_LEN);
		name[GFS_FNAME_LEN] = '\0';
		version = strchr(name, ';');
		if (version != NULL) *version = '\0';
		if (!callback(name, Files_CdSize(i), data)) break;
	}
	return 1;
}

int _open(const char *path, int flags, int mode)
{
	int fd = Files_FdAlloc();
	Fd *f;
	(void)mode;

	if (fd < 0) {
		errno = EMFILE;
		return -1;
	}
	f = &s_fds[fd - FD_FIRST];
	memset(f, 0, sizeof(*f));
	f->flags = flags;

	if (Files_IsCdPath(path)) {
		if ((flags & O_ACCMODE) != O_RDONLY) {
			errno = EROFS;
			return -1;
		}
		f->fid = Files_CdFind(path);
		if (f->fid < 0) {
			errno = ENOENT;
			return -1;
		}
		f->gfs = GFS_Open(f->fid);
		if (f->gfs == NULL) {
			errno = EIO;
			return -1;
		}
		f->size = Files_CdSize(f->fid);
		f->kind = FD_CD;
		return fd;
	}

	f->ram = Files_RamFind(path);
	if (f->ram == NULL && (flags & O_TRUNC) == 0) f->ram = Files_RamLoad(path);
	if (f->ram == NULL) {
		if ((flags & O_CREAT) == 0) {
			errno = ENOENT;
			return -1;
		}
		f->ram = Files_RamCreate(path);
		if (f->ram == NULL) {
			errno = ENOSPC;
			return -1;
		}
		f->ram->changed = true;     /* even if nothing is written: it exists */
	}
	if (flags & O_TRUNC) {
		f->ram->size = 0;
		f->ram->changed = true;
	}
	f->ram->opens++;
	f->kind = FD_RAM;
	return fd;
}

int _close(int fd)
{
	Fd *f = Files_FdGet(fd);
	if (f == NULL) {
		errno = EBADF;
		return -1;
	}
	if (f->kind == FD_CD) {
		GFS_Close(f->gfs);
	} else {
		RamFile *r = f->ram;
		bool ok = r->deleted || !r->changed || Files_RamStore(r);

		f->kind = FD_FREE;
		r->opens--;
		if (r->opens == 0 && (r->stored || r->deleted || !ok)) {
			/* stored: backup memory has it; not stored: it didn't fit, and
			 * shouldn't look saved */
			Files_RamFree(r);
		}
		if (!ok) {
			errno = ENOSPC;
			return -1;
		}
	}
	f->kind = FD_FREE;
	return 0;
}

static int Files_CdRead(Fd *f, uint8_t *buffer, uint32_t length)
{
	uint32_t done = 0;

	if (f->position >= f->size) return 0;
	if (length > f->size - f->position) length = f->size - f->position;

	while (done < length) {
		uint32_t sector = f->position / SECTOR_SIZE;
		uint32_t offset, chunk;

		if (s_cacheFid != f->fid || sector < s_cacheFirst || sector >= s_cacheFirst + s_cacheCount) {
			int slot = Files_VramFind(f->fid, sector);

			if (slot >= 0) {
				/* read before: from VDP1's VRAM */
				Files_VramLoad(slot, s_cache);
				s_cacheFid = f->fid;
				s_cacheFirst = sector;
				s_cacheCount = 1;
			} else {
				uint32_t fileSectors = (f->size + SECTOR_SIZE - 1) / SECTOR_SIZE;
				uint32_t count = fileSectors - sector, i;
				int32_t got;

				if (count > CACHE_SECTORS) count = CACHE_SECTORS;
				s_cacheFid = -1;
				Loading_Disc(1);
				if (GFS_Seek(f->gfs, (Sint32)sector, GFS_SEEK_SET) < 0) {
					Loading_Disc(0);
					break;
				}
				got = GFS_Fread(f->gfs, (Sint32)count, s_cache, (Sint32)(count * SECTOR_SIZE));
				Loading_Disc(0);
				if (got <= 0) break;
				s_cacheFid = f->fid;
				s_cacheFirst = sector;
				s_cacheCount = ((uint32_t)got + SECTOR_SIZE - 1) / SECTOR_SIZE;
				for (i = 0; i < s_cacheCount; i++) Files_VramStore(f->fid, sector + i, s_cache + i * SECTOR_SIZE);
			}
		}

		offset = f->position - s_cacheFirst * SECTOR_SIZE;
		chunk = s_cacheCount * SECTOR_SIZE - offset;
		if (chunk > length - done) chunk = length - done;
		memcpy(buffer + done, s_cache + offset, chunk);
		done += chunk;
		f->position += chunk;
	}

	if (done == 0 && length != 0) {
		errno = EIO;
		return -1;
	}
	return (int)done;
}

int _read(int fd, void *buffer, size_t length)
{
	Fd *f = Files_FdGet(fd);
	uint32_t count;

	if (f == NULL || (f->flags & O_ACCMODE) == O_WRONLY) {
		errno = EBADF;
		return -1;
	}
	if (f->kind == FD_CD) return Files_CdRead(f, buffer, (uint32_t)length);

	if (f->position >= f->ram->size) return 0;
	count = f->ram->size - f->position;
	if (count > length) count = (uint32_t)length;
	memcpy(buffer, f->ram->data + f->position, count);
	f->position += count;
	return (int)count;
}

int Files_Write(int fd, const void *buffer, size_t length)
{
	Fd *f = Files_FdGet(fd);
	RamFile *r;

	if (f == NULL || f->kind != FD_RAM || (f->flags & O_ACCMODE) == O_RDONLY) {
		errno = EBADF;
		return -1;
	}
	r = f->ram;
	if (f->flags & O_APPEND) f->position = r->size;
	if (f->position > RAM_FILE_SIZE_MAX || length > RAM_FILE_SIZE_MAX - f->position) {
		errno = EFBIG;
		return -1;
	}
	if (f->position + length > r->capacity) {
		uint32_t capacity = (uint32_t)(f->position + length + 1023) & ~1023u;
		uint8_t *data = realloc(r->data, capacity);
		if (data == NULL) {
			errno = ENOSPC;
			return -1;
		}
		r->data = data;
		r->capacity = capacity;
	}
	if (f->position > r->size) memset(r->data + r->size, 0, f->position - r->size);
	memcpy(r->data + f->position, buffer, length);
	r->changed = true;
	f->position += (uint32_t)length;
	if (f->position > r->size) r->size = f->position;
	return (int)length;
}

off_t _lseek(int fd, off_t offset, int whence)
{
	Fd *f = Files_FdGet(fd);
	off_t base;

	if (f == NULL) {
		errno = EBADF;
		return -1;
	}
	switch (whence) {
		case SEEK_SET: base = 0; break;
		case SEEK_CUR: base = (off_t)f->position; break;
		case SEEK_END: base = (off_t)((f->kind == FD_CD) ? f->size : f->ram->size); break;
		default: errno = EINVAL; return -1;
	}
	if (base + offset < 0) {
		errno = EINVAL;
		return -1;
	}
	f->position = (uint32_t)(base + offset);
	return (off_t)f->position;
}

int _fstat(int fd, struct stat *st)
{
	Fd *f;

	memset(st, 0, sizeof(*st));
	if (fd >= 0 && fd < FD_FIRST) {
		st->st_mode = S_IFCHR;
		return 0;
	}
	f = Files_FdGet(fd);
	if (f == NULL) {
		errno = EBADF;
		return -1;
	}
	st->st_mode = S_IFREG;
	st->st_size = (off_t)((f->kind == FD_CD) ? f->size : f->ram->size);
	st->st_blksize = SECTOR_SIZE;
	return 0;
}

int _stat(const char *path, struct stat *st)
{
	memset(st, 0, sizeof(*st));
	if (Files_IsDirectory(path)) {
		st->st_mode = S_IFDIR;
		return 0;
	}
	if (Files_IsCdPath(path)) {
		int32_t fid = Files_CdFind(path);
		if (fid < 0) {
			errno = ENOENT;
			return -1;
		}
		st->st_mode = S_IFREG;
		st->st_size = (off_t)Files_CdSize(fid);
		return 0;
	} else {
		RamFile *r = Files_RamFind(path);
		bool loaded = false;

		if (r == NULL) {
			r = Files_RamLoad(path);
			loaded = true;
		}
		if (r == NULL) {
			errno = ENOENT;
			return -1;
		}
		st->st_mode = S_IFREG;
		st->st_size = (off_t)r->size;
		if (loaded) Files_RamFree(r);
		return 0;
	}
}

int _unlink(const char *path)
{
	RamFile *r;
	char name[BACKUP_NAME_LENGTH + 1];
	if (Files_IsCdPath(path)) {
		errno = EROFS;
		return -1;
	}
	r = Files_RamFind(path);
	if (r != NULL) {
		/* descriptors still open keep it until they are closed */
		if (r->opens > 0) r->deleted = true;
		else Files_RamFree(r);
	}
	Files_BackupName(path, name);
	Backup_Delete(name);
	return 0;
}
