/** @file saturn/common/console.h Text console on the VDP2 bitmap.
 *
 * Receives stdout/stderr. It shows itself on the first write, until
 * console_release() hands the screen to the game; after that text is only
 * recorded, and console_show() brings it back (fatal errors, crashes). */

#ifndef SATURN_CONSOLE_H
#define SATURN_CONSOLE_H

#include <stdint.h>

enum { CONSOLE_LOG_SIZE = 8192 };

extern void console_write(const char *text, int length);
extern void console_release(void);
extern void console_show(void);

/* The console's 8x8 glyph of a character: 8 rows, bit 0 the leftmost pixel. */
extern const uint8_t *console_glyph(char c);

#endif /* SATURN_CONSOLE_H */
