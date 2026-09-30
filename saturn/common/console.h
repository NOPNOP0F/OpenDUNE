/** @file saturn/common/console.h Text console on the VDP2 bitmap.
 *
 * Receives stdout/stderr. It shows itself on the first write, until
 * console_release() hands the screen to the game; after that text is only
 * recorded, and console_show() brings it back (fatal errors, crashes). */

#ifndef SATURN_CONSOLE_H
#define SATURN_CONSOLE_H

extern void console_write(const char *text, int length);
extern void console_release(void);
extern void console_show(void);

#endif /* SATURN_CONSOLE_H */
