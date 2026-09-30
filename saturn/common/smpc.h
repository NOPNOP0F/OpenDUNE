/** @file saturn/common/smpc.h Controller input through the SMPC. */

#ifndef SATURN_SMPC_H
#define SATURN_SMPC_H

#include <stdint.h>

/* Read the standard pad on port 1 (PAD_* bits of saturn_hw.h, 1 = pressed).
 * Returns 0 if no pad is connected. Call during V-BLANK. */
extern uint16_t smpc_pad_read(void);

/* Non-blocking alternative for use from the VBlank-in interrupt: collects
 * the result of the previous INTBACK, then issues the next one. */
extern void smpc_vblank(void);

/* Pad state collected by smpc_vblank() (one frame old). */
extern uint16_t smpc_pad_state(void);

#endif /* SATURN_SMPC_H */
