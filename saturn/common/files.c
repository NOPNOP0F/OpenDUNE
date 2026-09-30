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

static bool cd_init(void)
{
	if (s_cdReady) return true;

	s_cache = malloc(CACHE_SECTORS * SECTOR_SIZE);
	if (s_cache == NULL) return false;

	GFS_DIRTBL_TYPE(&s_dirTable) = GFS_DIR_NAME;
	GFS_DIRTBL_DIRNAME(&s_dirTable) = s_dirNames;
	GFS_DIRTBL_NDIR(&s_dirTable) = DIR_MAX;
	s_dirCount = GFS_Init(OPEN_FILES_MAX, s_gfsWork, &s_dirTable);
	if (s_dirCount < 0) return false;

	s_cdReady = true;
	return true;
}

/* Last component of a path. */
static const char *base_name(const char *path)
{
	const char *slash = strrchr(path, '/');
	return (slash != NULL) ? slash + 1 : path;
}

static bool is_cd_path(const char *path)
{
	return strncmp(path, FILES_CD_PREFIX, sizeof(FILES_CD_PREFIX) - 1) == 0;
}

static bool is_directory(const char *path)
{
	return strcmp(path, ".") == 0 || strcmp(path, "./") == 0 ||
	       strcmp(path, "CD") == 0 || strcmp(path, FILES_CD_PREFIX) == 0;
}

static void upper_name(char *dst, const char *src, size_t size)
{
	size_t i;
	for (i = 0; i + 1 < size && src[i] != '\0'; i++) {
		char c = src[i];
		dst[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
	}
	dst[i] = '\0';
}

static int32_t cd_find(const char *path)
{
	char name[GFS_FNAME_LEN + 1];
	if (!cd_init()) return -1;
	upper_name(name, path + sizeof(FILES_CD_PREFIX) - 1, sizeof(name));
	return GFS_NameToId((Sint8 *)name);
}

static uint32_t cd_size(int32_t fid)
{
	return (uint32_t)GFS_DIR_SIZE(&s_dirNames[fid]);
}

static RamFile *ram_find(const char *path)
{
	char name[NAME_MAX_LENGTH + 1];
	int i;
	upper_name(name, base_name(path), sizeof(name));
	for (i = 0; i < RAM_FILE_MAX; i++) {
		if (s_ramFiles[i].name[0] != '\0' && !s_ramFiles[i].deleted && strcmp(s_ramFiles[i].name, name) == 0) return &s_ramFiles[i];
	}
	return NULL;
}

static RamFile *ram_create(const char *path)
{
	int i;
	for (i = 0; i < RAM_FILE_MAX; i++) {
		RamFile *f = &s_ramFiles[i];
		if (f->name[0] != '\0') continue;
		upper_name(f->name, base_name(path), sizeof(f->name));
		f->data = NULL;
		f->size = f->capacity = 0;
		return f;
	}
	return NULL;
}

static void ram_free(RamFile *r)
{
	free(r->data);
	memset(r, 0, sizeof(*r));
}

/* Name in backup memory: "D2_" and up to 8 letters and digits of the file
 * name, before the extension ("_save000.dat" -> "D2_SAVE000"). */
static void backup_name(const char *path, char name[BACKUP_NAME_LENGTH + 1])
{
	const char *src = base_name(path);
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
static RamFile *ram_load(const char *path)
{
	char name[BACKUP_NAME_LENGTH + 1];
	uint8_t *data;
	uint32_t size;
	RamFile *r;

	backup_name(path, name);
	if (!backup_read(name, &data, &size)) return NULL;
	r = ram_create(path);
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
static bool ram_store(RamFile *r)
{
	char name[BACKUP_NAME_LENGTH + 1];

	backup_name(r->name, name);
	if (!backup_write(name, "Dune II", r->data, r->size)) return false;
	r->changed = false;
	r->stored = true;
	return true;
}

static Fd *fd_get(int fd)
{
	if (fd < FD_FIRST || fd >= FD_FIRST + FD_COUNT) return NULL;
	if (s_fds[fd - FD_FIRST].kind == FD_FREE) return NULL;
	return &s_fds[fd - FD_FIRST];
}

static int fd_alloc(void)
{
	int i;
	for (i = 0; i < FD_COUNT; i++) {
		if (s_fds[i].kind == FD_FREE) return FD_FIRST + i;
	}
	return -1;
}

int files_cd_list(int (*callback)(const char *name, uint32_t size, void *data), void *data)
{
	int32_t i;
	if (!cd_init()) return 0;

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
		if (!callback(name, cd_size(i), data)) break;
	}
	return 1;
}

int _open(const char *path, int flags, int mode)
{
	int fd = fd_alloc();
	Fd *f;
	(void)mode;

	if (fd < 0) {
		errno = EMFILE;
		return -1;
	}
	f = &s_fds[fd - FD_FIRST];
	memset(f, 0, sizeof(*f));
	f->flags = flags;

	if (is_cd_path(path)) {
		if ((flags & O_ACCMODE) != O_RDONLY) {
			errno = EROFS;
			return -1;
		}
		f->fid = cd_find(path);
		if (f->fid < 0) {
			errno = ENOENT;
			return -1;
		}
		f->gfs = GFS_Open(f->fid);
		if (f->gfs == NULL) {
			errno = EIO;
			return -1;
		}
		f->size = cd_size(f->fid);
		f->kind = FD_CD;
		return fd;
	}

	f->ram = ram_find(path);
	if (f->ram == NULL && (flags & O_TRUNC) == 0) f->ram = ram_load(path);
	if (f->ram == NULL) {
		if ((flags & O_CREAT) == 0) {
			errno = ENOENT;
			return -1;
		}
		f->ram = ram_create(path);
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
	Fd *f = fd_get(fd);
	if (f == NULL) {
		errno = EBADF;
		return -1;
	}
	if (f->kind == FD_CD) {
		GFS_Close(f->gfs);
	} else {
		RamFile *r = f->ram;
		bool ok = r->deleted || !r->changed || ram_store(r);

		f->kind = FD_FREE;
		r->opens--;
		if (r->opens == 0 && (r->stored || r->deleted || !ok)) {
			/* stored: backup memory has it; not stored: it didn't fit, and
			 * shouldn't look saved */
			ram_free(r);
		}
		if (!ok) {
			errno = ENOSPC;
			return -1;
		}
	}
	f->kind = FD_FREE;
	return 0;
}

static int cd_read(Fd *f, uint8_t *buffer, uint32_t length)
{
	uint32_t done = 0;

	if (f->position >= f->size) return 0;
	if (length > f->size - f->position) length = f->size - f->position;

	while (done < length) {
		uint32_t sector = f->position / SECTOR_SIZE;
		uint32_t offset, chunk;

		if (s_cacheFid != f->fid || sector < s_cacheFirst || sector >= s_cacheFirst + s_cacheCount) {
			uint32_t fileSectors = (f->size + SECTOR_SIZE - 1) / SECTOR_SIZE;
			uint32_t count = fileSectors - sector;
			int32_t got;

			if (count > CACHE_SECTORS) count = CACHE_SECTORS;
			s_cacheFid = -1;
			if (GFS_Seek(f->gfs, (Sint32)sector, GFS_SEEK_SET) < 0) break;
			got = GFS_Fread(f->gfs, (Sint32)count, s_cache, (Sint32)(count * SECTOR_SIZE));
			if (got <= 0) break;
			s_cacheFid = f->fid;
			s_cacheFirst = sector;
			s_cacheCount = ((uint32_t)got + SECTOR_SIZE - 1) / SECTOR_SIZE;
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
	Fd *f = fd_get(fd);
	uint32_t count;

	if (f == NULL || (f->flags & O_ACCMODE) == O_WRONLY) {
		errno = EBADF;
		return -1;
	}
	if (f->kind == FD_CD) return cd_read(f, buffer, (uint32_t)length);

	if (f->position >= f->ram->size) return 0;
	count = f->ram->size - f->position;
	if (count > length) count = (uint32_t)length;
	memcpy(buffer, f->ram->data + f->position, count);
	f->position += count;
	return (int)count;
}

int files_write(int fd, const void *buffer, size_t length)
{
	Fd *f = fd_get(fd);
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
	Fd *f = fd_get(fd);
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
	f = fd_get(fd);
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
	if (is_directory(path)) {
		st->st_mode = S_IFDIR;
		return 0;
	}
	if (is_cd_path(path)) {
		int32_t fid = cd_find(path);
		if (fid < 0) {
			errno = ENOENT;
			return -1;
		}
		st->st_mode = S_IFREG;
		st->st_size = (off_t)cd_size(fid);
		return 0;
	} else {
		RamFile *r = ram_find(path);
		bool loaded = false;

		if (r == NULL) {
			r = ram_load(path);
			loaded = true;
		}
		if (r == NULL) {
			errno = ENOENT;
			return -1;
		}
		st->st_mode = S_IFREG;
		st->st_size = (off_t)r->size;
		if (loaded) ram_free(r);
		return 0;
	}
}

int _unlink(const char *path)
{
	RamFile *r;
	char name[BACKUP_NAME_LENGTH + 1];
	if (is_cd_path(path)) {
		errno = EROFS;
		return -1;
	}
	r = ram_find(path);
	if (r != NULL) {
		/* descriptors still open keep it until they are closed */
		if (r->opens > 0) r->deleted = true;
		else ram_free(r);
	}
	backup_name(path, name);
	backup_delete(name);
	return 0;
}
