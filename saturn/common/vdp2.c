/** @file saturn/common/vdp2.c VDP2 set up as an 8bpp bitmap (NBG0) and an overlay.
 *
 * NBG0 is a 512x256 bitmap of 256 colours at the start of VRAM (bank A),
 * shown in a 320x224 non-interlaced display. NBG1, in front of it, is a
 * 512x256 bitmap of 16 colours in bank B, colour 0 transparent, using its
 * own colours (colour RAM 256 on) for marks drawn over the picture.
 * Register values follow the VDP2 User's Manual. */

#include "saturn_hw.h"
#include "vdp2.h"

void vdp2_vblank_wait(void)
{
	while (VDP2_TVSTAT & VDP2_TVSTAT_VBLANK) {}
	while (!(VDP2_TVSTAT & VDP2_TVSTAT_VBLANK)) {}
}

void vdp2_bitmap_init(void)
{
	int i;

	VDP2_TVMD = 0x0000;         /* display off while configuring */
	vdp2_vblank_wait();

	VDP2_RAMCTL = 0x0000;       /* colour RAM mode 0, VRAM banks not partitioned */

	/* NBG0 256-colour bitmap needs 2 VRAM reads per cycle, the NBG1
	 * 16-colour one 1 (Table 3.3); the rest of the slots go to the CPU. */
	VDP2_CYCA0L = 0x44EE;
	VDP2_CYCA0U = 0xEEEE;
	VDP2_CYCA1L = 0xEEEE;
	VDP2_CYCA1U = 0xEEEE;
	VDP2_CYCB0L = 0x5EEE;
	VDP2_CYCB0U = 0xEEEE;
	VDP2_CYCB1L = 0xEEEE;
	VDP2_CYCB1U = 0xEEEE;

	VDP2_CHCTLA = (1 << 4)      /* N0CHCN = 256 colours */
	            | (0 << 2)      /* N0BMSZ = 512x256 */
	            | (1 << 1)      /* N0BMEN = bitmap */
	            | (0 << 12)     /* N1CHCN = 16 colours */
	            | (0 << 10)     /* N1BMSZ = 512x256 */
	            | (1 << 9);     /* N1BMEN = bitmap */
	VDP2_BMPNA = 0;             /* palette 0 for both */
	VDP2_MPOFN = (VDP2_OVERLAY_OFFSET / 0x20000) << 4;     /* NBG1 there, NBG0 at 0 */
	VDP2_CRAOFA = 1 << 4;       /* NBG0 colours from 0, NBG1 from 256 */

	VDP2_SCXIN0 = 0; VDP2_SCXDN0 = 0;
	VDP2_SCYIN0 = 0; VDP2_SCYDN0 = 0;
	VDP2_ZMXIN0 = 1; VDP2_ZMXDN0 = 0;  /* 1.0 = no zoom */
	VDP2_ZMYIN0 = 1; VDP2_ZMYDN0 = 0;

	VDP2_SCXIN1 = 0; VDP2_SCXDN1 = 0;
	VDP2_SCYIN1 = 0; VDP2_SCYDN1 = 0;
	VDP2_ZMXIN1 = 1; VDP2_ZMXDN1 = 0;
	VDP2_ZMYIN1 = 1; VDP2_ZMYDN1 = 0;

	VDP2_PRINA = (7 << 8) | 6;  /* NBG1 in front of NBG0 */

	/* back screen: single colour stored in the last word of VRAM */
	*(volatile uint16_t *)(VDP2_VRAM + 0x7FFFE) = RGB555(0, 0, 0);
	VDP2_BKTAU = (0x7FFFE >> 17) & 7;
	VDP2_BKTAL = (0x7FFFE >> 1) & 0xFFFF;

	VDP2_BGON = (1 << 8)        /* N0TPON: colour 0 is drawn, not transparent */
	          | (1 << 1)        /* N1ON (its colour 0 is transparent) */
	          | (1 << 0);       /* N0ON */

	/* clear the bitmaps */
	for (i = 0; i < VDP2_BITMAP_PITCH * 256; i += 4) *(volatile uint32_t *)(VDP2_VRAM + i) = 0;
	for (i = 0; i < VDP2_BITMAP_PITCH * 256 / 2; i += 4) *(volatile uint32_t *)(VDP2_VRAM + VDP2_OVERLAY_OFFSET + i) = 0;

	/* the overlay's colours */
	vdp2_set_color(256 + VDP2_OVERLAY_LIGHT, RGB555(31, 27, 6));
	vdp2_set_color(256 + VDP2_OVERLAY_DARK, RGB555(0, 0, 0));
}

void vdp2_overlay_pixel(int x, int y, int colour)
{
	volatile uint8_t *p;

	if (x < 0 || y < 0 || x >= VDP2_BITMAP_PITCH || y >= 256) return;
	p = (volatile uint8_t *)(VDP2_VRAM + VDP2_OVERLAY_OFFSET) + y * (VDP2_BITMAP_PITCH / 2) + x / 2;
	/* two pixels a byte, the left one in the high nibble */
	if (x & 1) *p = (uint8_t)((*p & 0xF0) | (colour & 0x0F));
	else *p = (uint8_t)((*p & 0x0F) | ((colour & 0x0F) << 4));
}

void vdp2_display_on(void)
{
	/* DISP on, border shows the back screen, 320x224 non-interlaced */
	VDP2_TVMD = 0x8000 | 0x0100;
}

void vdp2_set_color(int index, uint16_t rgb555)
{
	((volatile uint16_t *)VDP2_CRAM)[index] = rgb555;
}

void vdp2_set_scroll_y(int y)
{
	VDP2_SCYIN0 = (uint16_t)y;
	VDP2_SCYIN1 = (uint16_t)y;
}
