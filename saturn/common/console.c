/** @file saturn/common/console.c Text console on the VDP2 bitmap. */

#include <stdint.h>
#include <string.h>
#include "console.h"
#include "font8x8.h"
#include "saturn_hw.h"
#include "vdp2.h"

enum {
	COLUMNS = VDP2_DISPLAY_W / 8,
	ROWS = VDP2_DISPLAY_H / 8,
	COLOR_BACK = 0,
	COLOR_TEXT = 15
};

static enum { CONSOLE_OFF, CONSOLE_SHOWN, CONSOLE_RELEASED } s_state = CONSOLE_OFF;

static char s_text[ROWS][COLUMNS];
static int s_row, s_column;

/* Everything written, the last CONSOLE_LOG_SIZE bytes of it, for reading out
 * of an emulator's memory dump (saturn/tools/ymir-dump.py finds it through
 * the link map). */
char console_log[CONSOLE_LOG_SIZE];
uint32_t console_log_written;

static void draw_char(int row, int column)
{
	const uint8_t *glyph;
	volatile uint8_t *dst = VDP2_BITMAP + row * 8 * VDP2_BITMAP_PITCH + column * 8;
	unsigned char c = (unsigned char)s_text[row][column];
	int x, y;

	if (c < 32 || c > 126) c = ' ';
	glyph = s_font8x8[c - 32];
	for (y = 0; y < 8; y++, dst += VDP2_BITMAP_PITCH) {
		for (x = 0; x < 8; x++) dst[x] = ((glyph[y] >> x) & 1) ? COLOR_TEXT : COLOR_BACK;
	}
}

static void draw_all(void)
{
	int row, column;
	for (row = 0; row < ROWS; row++) {
		for (column = 0; column < COLUMNS; column++) draw_char(row, column);
	}
}

static void new_line(void)
{
	s_column = 0;
	if (++s_row < ROWS) return;

	memmove(s_text[0], s_text[1], sizeof(s_text) - sizeof(s_text[0]));
	memset(s_text[ROWS - 1], ' ', COLUMNS);
	s_row = ROWS - 1;
	if (s_state == CONSOLE_SHOWN) draw_all();
}

void console_show(void)
{
	if (s_state == CONSOLE_OFF) memset(s_text, ' ', sizeof(s_text));
	s_state = CONSOLE_SHOWN;

	vdp2_bitmap_init();
	vdp2_set_color(COLOR_BACK, RGB555(0, 0, 8));
	vdp2_set_color(COLOR_TEXT, RGB555(31, 31, 31));
	draw_all();
	vdp2_display_on();
}

void console_release(void)
{
	if (s_state == CONSOLE_OFF) memset(s_text, ' ', sizeof(s_text));
	s_state = CONSOLE_RELEASED;
}

void console_write(const char *text, int length)
{
	int i;

	if (s_state == CONSOLE_OFF) console_show();

	for (i = 0; i < length; i++) console_log[(console_log_written + (uint32_t)i) % CONSOLE_LOG_SIZE] = text[i];
	console_log_written += (uint32_t)length;

	for (i = 0; i < length; i++) {
		char c = text[i];
		if (c == '\n') {
			new_line();
			continue;
		}
		if (c == '\r') continue;
		if (s_column == COLUMNS) new_line();
		s_text[s_row][s_column] = c;
		if (s_state == CONSOLE_SHOWN) draw_char(s_row, s_column);
		s_column++;
	}
}
