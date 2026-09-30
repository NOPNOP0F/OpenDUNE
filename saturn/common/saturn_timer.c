/** @file saturn/common/saturn_timer.c Frame counter driven by the VBlank-in interrupt. */

#include <stddef.h>
#include "bios.h"
#include "saturn_hw.h"
#include "saturn_timer.h"

/* SH-2 free-running timer (SH7604 Hardware Manual, section 11) */
#define FRT_FRCH    REG8(0xFFFFFE12UL)
#define FRT_FRCL    REG8(0xFFFFFE13UL)
#define FRT_TCR     REG8(0xFFFFFE16UL)

static volatile uint32_t s_frames;
static volatile uint16_t s_frameStart;  /* free-running timer at the last VBlank */
static uint32_t s_frameRate = 60;
static void (*volatile s_vblankHook)(void);

static uint16_t frt_read(void);

static void vblank_in(void)
{
	s_frameStart = frt_read();
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

	/* free-running timer counts at the system clock / 8 (about 3.5 MHz) */
	FRT_TCR = 0x00;

	BIOS_SETUINT(SCU_VECTOR_VBLANK_IN, vblank_in);
	BIOS_CHGSCUIM(~SCU_MASK_VBLANK_IN, 0);
	cpu_interrupts_enable();
}

static uint16_t frt_read(void)
{
	/* Reading the high byte latches the low one in a register shared by
	 * everyone: an interrupt reading the counter in between would leave
	 * its own low byte there. */
	uint32_t sr = cpu_interrupts_disable();
	uint8_t high = FRT_FRCH;
	uint8_t low = FRT_FRCL;
	cpu_interrupts_restore(sr);
	return (uint16_t)((high << 8) | low);
}

void saturn_delay_us(uint32_t us)
{
	/* the 16-bit counter wraps after about 18 ms: wait in pieces */
	while (us > 10000) {
		saturn_delay_us(10000);
		us -= 10000;
	}
	{
		/* 26.8 MHz / 8 (NTSC 320 dots; PAL is close): about 3.36 counts
		 * per us, rounded up so the wait is never shorter */
		uint32_t counts = us * 7 / 2 + 1;
		uint16_t start = frt_read();
		while ((uint16_t)(frt_read() - start) < counts) {}
	}
}

uint64_t saturn_timer_us(void)
{
	static volatile uint64_t last = 0;
	uint32_t frames, since, frameUs = 1000000 / s_frameRate;
	uint16_t start;
	uint64_t now;
	uint32_t sr;

	/* read the frame count and its start time consistently */
	do {
		frames = s_frames;
		start = s_frameStart;
		since = (uint16_t)(frt_read() - start);
	} while (frames != s_frames);

	since = since * 2 / 7;      /* about 3.5 counts per us, as in saturn_delay_us() */
	if (since > frameUs) since = frameUs;
	now = (uint64_t)frames * frameUs + since;

	/* never go back, whoever asks (the estimate within a frame is rough) */
	sr = cpu_interrupts_disable();
	if (now < last) now = last;
	last = now;
	cpu_interrupts_restore(sr);
	return now;
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
