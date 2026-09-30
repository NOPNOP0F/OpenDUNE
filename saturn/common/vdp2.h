/** @file saturn/common/vdp2.h VDP2 set up as one 8bpp bitmap (NBG0). */

#ifndef SATURN_VDP2_H
#define SATURN_VDP2_H

#include <stdint.h>

enum {
	VDP2_BITMAP_PITCH = 512,    /* NBG0 bitmap is 512x256, 1 byte per pixel */
	VDP2_DISPLAY_W = 320,
	VDP2_DISPLAY_H = 224
};

/* Pixel (0, 0) of the NBG0 bitmap in VRAM. */
#define VDP2_BITMAP ((volatile uint8_t *)0x25E00000UL)

extern void vdp2_bitmap_init(void);
extern void vdp2_display_on(void);
extern void vdp2_vblank_wait(void);
extern void vdp2_set_color(int index, uint16_t rgb555);
extern void vdp2_set_scroll_y(int y);

#endif /* SATURN_VDP2_H */
