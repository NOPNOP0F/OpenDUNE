/** @file saturn/common/vdp2.c VDP2 set up as one 8bpp bitmap (NBG0).
 *
 * NBG0 is a 512x256 bitmap of 256 colours at the start of VRAM, shown in a
 * 320x224 non-interlaced display. Register values follow the VDP2 User's
 * Manual. */

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

	/* NBG0 256-colour bitmap needs 2 VRAM reads per cycle (Table 3.3);
	 * the rest of the slots go to the CPU. */
	VDP2_CYCA0L = 0x44EE;
	VDP2_CYCA0U = 0xEEEE;
	VDP2_CYCA1L = 0xEEEE;
	VDP2_CYCA1U = 0xEEEE;
	VDP2_CYCB0L = 0xEEEE;
	VDP2_CYCB0U = 0xEEEE;
	VDP2_CYCB1L = 0xEEEE;
	VDP2_CYCB1U = 0xEEEE;

	VDP2_CHCTLA = (1 << 4)      /* N0CHCN = 256 colours */
	            | (0 << 2)      /* N0BMSZ = 512x256 */
	            | (1 << 1);     /* N0BMEN = bitmap */
	VDP2_BMPNA = 0;             /* palette 0 */
	VDP2_MPOFN = 0;             /* bitmap at VRAM + 0 */
	VDP2_CRAOFA = 0;            /* colour RAM offset 0 */

	VDP2_SCXIN0 = 0; VDP2_SCXDN0 = 0;
	VDP2_SCYIN0 = 0; VDP2_SCYDN0 = 0;
	VDP2_ZMXIN0 = 1; VDP2_ZMXDN0 = 0;  /* 1.0 = no zoom */
	VDP2_ZMYIN0 = 1; VDP2_ZMYDN0 = 0;

	VDP2_PRINA = 7;             /* NBG0 priority */

	/* back screen: single colour stored in the last word of VRAM */
	*(volatile uint16_t *)(VDP2_VRAM + 0x7FFFE) = RGB555(0, 0, 0);
	VDP2_BKTAU = (0x7FFFE >> 17) & 7;
	VDP2_BKTAL = (0x7FFFE >> 1) & 0xFFFF;

	VDP2_BGON = (1 << 8)        /* N0TPON: colour 0 is drawn, not transparent */
	          | (1 << 0);       /* N0ON */

	/* clear the bitmap */
	for (i = 0; i < VDP2_BITMAP_PITCH * 256; i += 4) *(volatile uint32_t *)(VDP2_VRAM + i) = 0;
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
}
