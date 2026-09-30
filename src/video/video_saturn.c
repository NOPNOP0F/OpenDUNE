/** @file src/video/video_saturn.c Sega Saturn video driver.
 *
 * SCREEN_0 lives in work RAM; every tick the rows marked dirty are copied
 * into the VDP2 NBG0 bitmap (512x256, 8bpp), with the 320x200 picture
 * centred in the 320x224 display. The palette goes to colour RAM. The pad
 * is read here too (input/pad_saturn.c), as other drivers read events. */

#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "video.h"
#include "../gfx.h"
#include "../input/pad_saturn.h"
#include "../os/error.h"

#include "console.h"
#include "loading.h"
#include "saturn_hw.h"
#include "saturn_timer.h"
#include "smpc.h"
#include "vdp2.h"

enum {
	TOP = (VDP2_DISPLAY_H - SCREEN_HEIGHT) / 2,
	EXTRA_LINES = 4     /* Video_SetOffset() shows up to 4 lines further down */
};

static uint8 *s_framebuffer = NULL;
static uint16 s_screenOffset = 0;   /* VGA start address, in units of 4 bytes */
static bool s_repaintAll = true;

/* The VBlank interrupt: the controllers, and the loading indicator. */
static void Video_VBlank(void)
{
	smpc_vblank();
	loading_vblank();
}

bool Video_Init(int screen_magnification, VideoScaleFilter filter)
{
	VARIABLE_NOT_USED(screen_magnification);
	VARIABLE_NOT_USED(filter);

	s_framebuffer = calloc(1, SCREEN_WIDTH * (SCREEN_HEIGHT + EXTRA_LINES));
	if (s_framebuffer == NULL) {
		Error("Failed to allocate %d bytes.\n", SCREEN_WIDTH * (SCREEN_HEIGHT + EXTRA_LINES));
		return false;
	}

	/* from here on the game owns the screen; the console only records */
	console_release();
	PadSaturn_Init();
	saturn_timer_set_vblank_hook(Video_VBlank);
	vdp2_bitmap_init();
	vdp2_display_on();
	s_repaintAll = true;
	return true;
}

void Video_Uninit(void)
{
	free(s_framebuffer);
	s_framebuffer = NULL;
}

static void Video_CopyRows(uint16 top, uint16 bottom)
{
	const uint8 *src = s_framebuffer + (s_screenOffset << 2) + top * SCREEN_WIDTH;
	volatile uint8 *dst = VDP2_BITMAP + (TOP + top) * VDP2_BITMAP_PITCH;
	uint16 y;

	for (y = top; y < bottom; y++, src += SCREEN_WIDTH, dst += VDP2_BITMAP_PITCH) {
		memcpy((void *)dst, src, SCREEN_WIDTH);
	}
}

void Video_Tick(void)
{
	struct dirty_area *area;

	if (s_framebuffer == NULL) return;

	loading_alive();
	PadSaturn_Tick();

	if (s_repaintAll) {
		loading_screen_changed();
		Video_CopyRows(0, SCREEN_HEIGHT);
		s_repaintAll = false;
		GFX_Screen_SetClean(SCREEN_0);
		return;
	}

	if (!GFX_Screen_IsDirty(SCREEN_0)) return;

	area = GFX_Screen_GetDirtyArea(SCREEN_0);
	if (area != NULL && area->top < area->bottom) {
		loading_screen_changed();
		Video_CopyRows(area->top, (area->bottom > SCREEN_HEIGHT) ? SCREEN_HEIGHT : area->bottom);
	}
	GFX_Screen_SetClean(SCREEN_0);
}

void Video_SetPalette(void *palette, int from, int length)
{
	const uint8 *p = palette;
	int i;

	/* VGA palette entries are 6 bits per component */
	for (i = from; i < from + length; i++, p += 3) {
		vdp2_set_color(i, RGB555(p[0] >> 1, p[1] >> 1, p[2] >> 1));
	}
}

void Video_Mouse_SetPosition(uint16 x, uint16 y)
{
	PadSaturn_SetPosition(x, y);
}

void Video_Mouse_SetRegion(uint16 minX, uint16 maxX, uint16 minY, uint16 maxY)
{
	PadSaturn_SetRegion(minX, maxX, minY, maxY);
}

void Video_SetOffset(uint16 offset)
{
	s_screenOffset = offset;
	s_repaintAll = true;
}

void *Video_GetFrameBuffer(uint16 size)
{
	if (size > SCREEN_WIDTH * (SCREEN_HEIGHT + EXTRA_LINES)) {
		Error("Video_GetFrameBuffer(%u) : size too big\n", size);
		return NULL;
	}
	return s_framebuffer;
}
