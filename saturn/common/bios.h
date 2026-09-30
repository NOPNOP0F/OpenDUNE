/** @file saturn/common/bios.h Saturn BIOS service calls (SBL6 SEGA_SYS.H). */

#ifndef SATURN_BIOS_H
#define SATURN_BIOS_H

#include <stdint.h>

/* Register a handler for an SCU interrupt vector. The BIOS dispatcher saves
 * registers and returns with RTE, so the handler is a plain C function. */
#define BIOS_SETUINT(vector, handler) \
	((**(void (**)(uint32_t, void (*)(void)))0x06000300)((vector), (handler)))

/* Put a handler directly into the SH-2 vector table (it must end in RTE). */
#define BIOS_SETSINT(vector, handler) \
	((**(void (**)(uint32_t, void (*)(void)))0x06000310)((vector), (handler)))

/* SCU interrupt mask = (mask & and_mask) | or_mask; a set bit masks. */
#define BIOS_CHGSCUIM(and_mask, or_mask) \
	((**(void (**)(uint32_t, uint32_t))0x06000344)((and_mask), (or_mask)))

/* Leave the program for the BIOS multiplayer screen: what A+B+C+Start
 * must do (SBL6 SYS_EXECDMP). */
#define BIOS_EXECDMP() \
	((**(void (**)(void))0x0600026C)())

/* SCU interrupt vectors and mask bits (SBL6 SEGA_INT.H) */
#define SCU_VECTOR_VBLANK_IN   0x40
#define SCU_MASK_VBLANK_IN     (1 << 0)

/* Allow all interrupt levels on the calling SH-2. */
static inline void cpu_interrupts_enable(void)
{
	uint32_t sr;
	__asm__ volatile ("stc sr, %0" : "=r" (sr));
	sr &= ~0xF0u;
	__asm__ volatile ("ldc %0, sr" : : "r" (sr));
}

/* Mask all interrupts on the calling SH-2; returns the old SR for
 * cpu_interrupts_restore(). */
static inline uint32_t cpu_interrupts_disable(void)
{
	uint32_t sr;
	__asm__ volatile ("stc sr, %0" : "=r" (sr));
	__asm__ volatile ("ldc %0, sr" : : "r" (sr | 0xF0u));
	return sr;
}

static inline void cpu_interrupts_restore(uint32_t sr)
{
	__asm__ volatile ("ldc %0, sr" : : "r" (sr));
}

#endif /* SATURN_BIOS_H */
