/** @file saturn/common/saturn_timer.h Frame counter driven by the VBlank-in interrupt. */

#ifndef SATURN_TIMER_H
#define SATURN_TIMER_H

#include <stdint.h>

/* Start counting frames (registers the VBlank-in handler with the BIOS). */
extern void SaturnTimer_Init(void);

/* Frames since SaturnTimer_Init(). */
extern uint32_t SaturnTimer_Frames(void);

/* Milliseconds since SaturnTimer_Init(), with frame (16-20 ms) resolution. */
extern uint32_t SaturnTimer_Ms(void);

/* Microseconds since SaturnTimer_Init(): the frame count plus the time
 * since the last VBlank, from the free-running timer. */
extern uint64_t SaturnTimer_Us(void);

/* Busy-wait at least us microseconds, timed by the SH-2 free-running timer. */
extern void SaturnTimer_DelayUs(uint32_t us);

/* Also run hook in every VBlank-in interrupt (NULL to stop). Keep it short. */
extern void SaturnTimer_SetVBlankHook(void (*hook)(void));

#endif /* SATURN_TIMER_H */
