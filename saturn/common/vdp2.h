/** @file saturn/common/vdp2.h VDP2 set up as an 8bpp bitmap (NBG0) and an overlay (NBG1). */

#ifndef SATURN_VDP2_H
#define SATURN_VDP2_H

#include <stdint.h>

enum {
	VDP2_BITMAP_PITCH = 512,    /* NBG0 bitmap is 512x256, 1 byte per pixel */
	VDP2_DISPLAY_W = 320,
	VDP2_DISPLAY_H = 224,
	VDP2_OVERLAY_OFFSET = 0x40000,  /* NBG1 in VRAM: bank B */
	/* overlay colours (0 is transparent) */
	VDP2_OVERLAY_CLEAR = 0,
	VDP2_OVERLAY_LIGHT = 1,
	VDP2_OVERLAY_DARK = 2
};

/* Pixel (0, 0) of the NBG0 bitmap in VRAM. */
#define VDP2_BITMAP ((volatile uint8_t *)0x25E00000UL)

extern void vdp2_bitmap_init(void);
extern void vdp2_display_on(void);
extern void vdp2_vblank_wait(void);
extern void vdp2_set_color(int index, uint16_t rgb555);
extern void vdp2_set_scroll_y(int y);

/* Set a pixel of the overlay, in front of the bitmap (VDP2_OVERLAY_*). */
extern void vdp2_overlay_pixel(int x, int y, int colour);

#endif /* SATURN_VDP2_H */
