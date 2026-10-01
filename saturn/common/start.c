/** @file saturn/common/start.c C entry point, called by crt0 once .bss is clear. */

#include <stdlib.h>
#include "crash.h"

extern int main(int argc, char **argv);

/* Linker script symbols (see saturn.ld), named without the C prefix. */
extern void (*s_initArrayStart[])(void) __asm__("__init_array_start");
extern void (*s_initArrayEnd[])(void) __asm__("__init_array_end");

/**
 * The C start of the program, from crt0.S: clear BSS, run the static
 * constructors, then main().
 */
void Saturn_Start(void)
{
	static char name[] = "opendune";
	static char *argv[] = { name, NULL };
	void (**constructor)(void);

	Crash_Install();

	/* C++ static constructors (the AdLib driver's) */
	for (constructor = s_initArrayStart; constructor < s_initArrayEnd; constructor++) (*constructor)();

	exit(main(1, argv));
}
