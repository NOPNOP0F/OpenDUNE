/** @file saturn/common/saturn_timer.c Frame counter driven by the VBlank-in interrupt. */

#include <stddef.h>
#include "bios.h"
#include "saturn_hw.h"
#include "saturn_timer.h"

static volatile uint32_t s_frames;
static uint32_t s_frameRate = 60;
static void (*volatile s_vblankHook)(void);

static void vblank_in(void)
{
	s_frames++;
	if (s_vblankHook != NULL) s_vblankHook();
}

void saturn_timer_set_vblank_hook(void (*hook)(void))
{
	s_vblankHook = hook;
}

void saturn_timer_init(void)
{
	s_frameRate = (VDP2_TVSTAT & VDP2_TVSTAT_PAL) ? 50 : 60;
	s_frames = 0;

	BIOS_SETUINT(SCU_VECTOR_VBLANK_IN, vblank_in);
	BIOS_CHGSCUIM(~SCU_MASK_VBLANK_IN, 0);
	cpu_interrupts_enable();
}

uint32_t saturn_timer_frames(void)
{
	return s_frames;
}

uint32_t saturn_timer_ms(void)
{
	/* 64-bit so the product doesn't wrap after 20 hours */
	return (uint32_t)((uint64_t)s_frames * 1000 / s_frameRate);
}
