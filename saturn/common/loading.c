/** @file saturn/common/loading.c A loading indicator while the game is busy. */

#include "bios.h"
#include "console.h"
#include "loading.h"
#include "saturn_timer.h"
#include "vdp2.h"

enum {
	SHOW_AFTER = 15,            /* frames without the video tick (a quarter second) */
	DOT_FRAMES = 15,            /* frames per step of the dots */
	LENGTH = 10,                /* "LOADING..." */
	/* right-aligned in the border under the 320x200 picture (lines 212-223) */
	TEXT_X = VDP2_DISPLAY_W - 8 - LENGTH * 8,
	TEXT_Y = VDP2_DISPLAY_H - 10
};

static volatile uint32_t s_alive = 0;       /* frame of the last video tick, 0 before the first */
static volatile int s_shown = -1;           /* dots shown, -1 when hidden */

static void draw_text(const char *text, int colour)
{
	int i, x, y;

	for (i = 0; text[i] != '\0'; i++) {
		const uint8_t *glyph = console_glyph(text[i]);
		for (y = 0; y < 8; y++) {
			for (x = 0; x < 8; x++) {
				vdp2_overlay_pixel(TEXT_X + i * 8 + x, TEXT_Y + y, ((glyph[y] >> x) & 1) ? colour : VDP2_OVERLAY_CLEAR);
			}
		}
	}
}

void loading_alive(void)
{
	s_alive = saturn_timer_frames();
	if (s_shown >= 0) {
		/* the interrupt draws it: keep it from drawing while it goes */
		uint32_t sr = cpu_interrupts_disable();
		draw_text("          ", VDP2_OVERLAY_CLEAR);
		s_shown = -1;
		cpu_interrupts_restore(sr);
	}
}

void loading_vblank(void)
{
	static const char *const texts[4] = { "LOADING   ", "LOADING.  ", "LOADING.. ", "LOADING..." };
	uint32_t frames = saturn_timer_frames();
	int dots;

	if (s_alive == 0 || frames - s_alive < SHOW_AFTER) return;
	dots = (int)((frames - s_alive - SHOW_AFTER) / DOT_FRAMES) % 4;
	if (dots == s_shown) return;
	draw_text(texts[dots], VDP2_OVERLAY_LIGHT);
	s_shown = dots;
}
