/** @file saturn/common/saturn_timer.h Frame counter driven by the VBlank-in interrupt. */

#ifndef SATURN_TIMER_H
#define SATURN_TIMER_H

#include <stdint.h>

/* Start counting frames (registers the VBlank-in handler with the BIOS). */
extern void saturn_timer_init(void);

/* Frames since saturn_timer_init(). */
extern uint32_t saturn_timer_frames(void);

/* Milliseconds since saturn_timer_init(), with frame (16-20 ms) resolution. */
extern uint32_t saturn_timer_ms(void);

/* Microseconds since saturn_timer_init(): the frame count plus the time
 * since the last VBlank, from the free-running timer. */
extern uint64_t saturn_timer_us(void);

/* Busy-wait at least us microseconds (at most 18000), timed by the SH-2
 * free-running timer. */
extern void saturn_delay_us(uint32_t us);

/* Also run hook in every VBlank-in interrupt (NULL to stop). Keep it short. */
extern void saturn_timer_set_vblank_hook(void (*hook)(void));

#endif /* SATURN_TIMER_H */
