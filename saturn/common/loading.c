/** @file saturn/common/loading.c A loading indicator while the game is busy.
 *
 * Shown from the VBlank interrupt while the picture stands still (nothing
 * drawn for a quarter of a second) and either the CD has been read most of
 * the last half second (loads that let the game run between files, like a
 * house's voices) or the game hasn't run the video tick at all; drawn and
 * taken away only here. Moving pictures reading as they go (the intro)
 * don't get it. */

#include "console.h"
#include "loading.h"
#include "saturn_timer.h"
#include "vdp2.h"

enum {
	STALE_FRAMES = 15,          /* frames without the video tick (a quarter second) */
	STILL_FRAMES = 15,          /* frames without a change on screen */
	BUSY_FRAMES = 12,           /* of the last 32 with the CD being read */
	DOT_FRAMES = 15,            /* frames per step of the dots */
	LENGTH = 10,                /* "LOADING..." */
	/* right-aligned in the border under the 320x200 picture (lines 212-223) */
	TEXT_X = VDP2_DISPLAY_W - 8 - LENGTH * 8,
	TEXT_Y = VDP2_DISPLAY_H - 10
};

static volatile uint32_t s_alive = 0;       /* frame of the last video tick, 0 before the first */
static volatile uint32_t s_changed = 0;     /* frame the picture last changed */
static volatile int s_reading = 0;          /* the CD is being read now */
static uint32_t s_readFrames = 0;           /* a bit a frame, the latest lowest: read then */
static int s_shown = -1;                    /* dots shown, -1 when hidden */
static uint32_t s_shownSince;

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

static int bits(uint32_t v)
{
	int n = 0;
	for (; v != 0; v &= v - 1) n++;
	return n;
}

void loading_alive(void)
{
	s_alive = saturn_timer_frames();
}

void loading_screen_changed(void)
{
	s_changed = saturn_timer_frames();
}

void loading_disc(int reading)
{
	s_reading = reading;
}

void loading_vblank(void)
{
	static const char *const texts[4] = { "LOADING   ", "LOADING.  ", "LOADING.. ", "LOADING..." };
	uint32_t frames = saturn_timer_frames();
	int busy, dots;

	s_readFrames = (s_readFrames << 1) | (s_reading ? 1 : 0);
	if (s_alive == 0) return;       /* the game isn't showing anything yet */

	busy = frames - s_alive >= STALE_FRAMES ||
		(bits(s_readFrames) >= BUSY_FRAMES && frames - s_changed >= STILL_FRAMES);
	if (!busy) {
		if (s_shown >= 0) {
			draw_text("          ", VDP2_OVERLAY_CLEAR);
			s_shown = -1;
		}
		return;
	}

	if (s_shown < 0) s_shownSince = frames;
	dots = (int)((frames - s_shownSince) / DOT_FRAMES) % 4;
	if (dots == s_shown) return;
	draw_text(texts[dots], VDP2_OVERLAY_LIGHT);
	s_shown = dots;
}
