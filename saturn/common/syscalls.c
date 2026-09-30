/** @file saturn/common/syscalls.c System calls for newlib on the Saturn.
 *
 * stdout/stderr go to the text console. The heap is high work RAM after the
 * program (minus the stack), then the 1 MB of low work RAM. There is no file
 * system yet: every other file operation fails. The clock counts from
 * power on. */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include "console.h"
#include "saturn_timer.h"

#undef errno
extern int errno;

/* Linker script symbols (see saturn.ld), named without the C prefix. */
extern char s_heapStart[] __asm__("__heap_start");
extern char s_stackTop[] __asm__("__stack_top");

enum { STACK_RESERVE = 64 * 1024 };

#define LWRAM_START 0x00200000UL
#define LWRAM_END   0x00300000UL

static uintptr_t s_break = 0;
static uintptr_t s_regionEnd = 0;

void *_sbrk(ptrdiff_t increment)
{
	uintptr_t previous;

	if (s_break == 0) {
		s_break = (uintptr_t)s_heapStart;
		s_regionEnd = (uintptr_t)s_stackTop - STACK_RESERVE;
	}

	if (s_break + increment > s_regionEnd) {
		/* high work RAM is full: continue in low work RAM. malloc copes
		 * with the break jumping, it just can't merge across the gap. */
		if (s_regionEnd == LWRAM_END || LWRAM_START + increment > LWRAM_END) {
			errno = ENOMEM;
			return (void *)-1;
		}
		s_break = LWRAM_START;
		s_regionEnd = LWRAM_END;
	}

	previous = s_break;
	s_break += increment;
	return (void *)previous;
}

int _write(int fd, const void *buffer, size_t length)
{
	if (fd != 1 && fd != 2) {
		errno = EBADF;
		return -1;
	}
	console_write(buffer, (int)length);
	return (int)length;
}

int _read(int fd, void *buffer, size_t length)
{
	(void)fd; (void)buffer; (void)length;
	errno = EBADF;
	return -1;
}

int _open(const char *name, int flags, int mode)
{
	(void)name; (void)flags; (void)mode;
	errno = ENOENT;
	return -1;
}

int _close(int fd)
{
	(void)fd;
	errno = EBADF;
	return -1;
}

off_t _lseek(int fd, off_t offset, int whence)
{
	(void)fd; (void)offset; (void)whence;
	errno = EBADF;
	return -1;
}

int _fstat(int fd, struct stat *st)
{
	if (fd > 2) {
		errno = EBADF;
		return -1;
	}
	st->st_mode = S_IFCHR;
	return 0;
}

int _stat(const char *name, struct stat *st)
{
	(void)name; (void)st;
	errno = ENOENT;
	return -1;
}

int _unlink(const char *name)
{
	(void)name;
	errno = ENOENT;
	return -1;
}

int mkdir(const char *name, mode_t mode)
{
	(void)name; (void)mode;
	errno = EROFS;
	return -1;
}

/* Time since power on; there is no calendar clock yet. */
int _gettimeofday(struct timeval *tv, void *tz)
{
	uint32_t ms = saturn_timer_ms();
	(void)tz;
	tv->tv_sec = ms / 1000;
	tv->tv_usec = (ms % 1000) * 1000;
	return 0;
}

int _isatty(int fd)
{
	return fd <= 2;
}

int _getpid(void)
{
	return 1;
}

int _kill(int pid, int signal)
{
	(void)pid; (void)signal;
	errno = EINVAL;
	return -1;
}

void _exit(int status)
{
	char text[32];
	int length = snprintf(text, sizeof(text), "\nexit(%d)\n", status);
	console_write(text, length);
	console_show();
	for (;;) {}
}
