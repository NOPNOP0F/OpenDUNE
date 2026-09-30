/** @file saturn/common/crash.c Report SH-2 exceptions on the console. */

#include <stdint.h>
#include <stdio.h>
#include "bios.h"
#include "console.h"
#include "crash.h"

extern void crash_entry_4(void);
extern void crash_entry_6(void);
extern void crash_entry_9(void);
extern void crash_entry_10(void);

void crash_install(void)
{
	BIOS_SETSINT(4, crash_entry_4);
	BIOS_SETSINT(6, crash_entry_6);
	BIOS_SETSINT(9, crash_entry_9);
	BIOS_SETSINT(10, crash_entry_10);
}

void crash_report(uint32_t vector, uint32_t pc, uint32_t sr, uint32_t sp)
{
	static const char *const names[] = {
		[4] = "illegal instruction",
		[6] = "slot illegal instruction",
		[9] = "CPU address error",
		[10] = "DMA address error"
	};
	char text[160];
	int length;

	length = snprintf(text, sizeof(text),
		"\n*** CRASH: %s\nPC=%08lX SR=%08lX SP=%08lX\n"
		"sh-elf-addr2line -e opendune.elf %08lX\n",
		vector < sizeof(names) / sizeof(names[0]) && names[vector] != NULL ? names[vector] : "exception",
		(unsigned long)pc, (unsigned long)sr, (unsigned long)sp, (unsigned long)pc);
	console_write(text, length);
	console_show();
	for (;;) {}
}
