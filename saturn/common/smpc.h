/** @file saturn/common/smpc.h Controller input through the SMPC. */

#ifndef SATURN_SMPC_H
#define SATURN_SMPC_H

#include <stdint.h>

/* Read the standard pad on port 1 (PAD_* bits of saturn_hw.h, 1 = pressed).
 * Returns 0 if no pad is connected. Call during V-BLANK. */
extern uint16_t smpc_pad_read(void);

/* Issue an SMPC command without parameters (SNDOFF, ...) and wait for it. */
extern void smpc_command(uint8_t command);

/* Read the real-time clock: year (2 bytes), weekday << 4 | month, day,
 * hours, minutes, seconds, in BCD (SMPC User's Manual, Table 3.9). */
extern void smpc_read_clock(uint8_t clock[7]);

/* Non-blocking alternative for use from the VBlank-in interrupt: collects
 * the result of the previous INTBACK, then issues the next one. */
extern void smpc_vblank(void);

/* Pad state collected by smpc_vblank() (one frame old). */
extern uint16_t smpc_pad_state(void);

#endif /* SATURN_SMPC_H */
