/** @file saturn/common/start.c C entry point, called by crt0 once .bss is clear. */

#include <stdlib.h>
#include "crash.h"

extern int main(int argc, char **argv);

void saturn_start(void)
{
	static char name[] = "opendune";
	static char *argv[] = { name, NULL };

	crash_install();
	exit(main(1, argv));
}
