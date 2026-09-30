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

#endif /* SATURN_TIMER_H */
