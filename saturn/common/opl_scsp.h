/** @file saturn/common/opl_scsp.h An AdLib (OPL2) played by the SCSP.
 *
 * Takes OPL2 register writes, as a game's AdLib driver makes them, and plays
 * them with SCSP slots doing the same 2-operator FM. */

#ifndef SATURN_OPL_SCSP_H
#define SATURN_OPL_SCSP_H

#include <stdint.h>

/* Set up the waveforms and slots; needs scsp_init() and saturn_timer_init(). */
extern int opl_scsp_init(void);

/* One OPL2 register write. */
extern void opl_scsp_write(uint8_t reg, uint8_t val);

/* End of a driver tick: carry out its key offs (see opl_scsp.c). */
extern void opl_scsp_flush(void);

/* Run the software envelopes (slow attacks); call as often as possible,
 * at least every few milliseconds. */
extern void opl_scsp_update(void);

/* Envelope mapping for tuning by ear: SCSP attack rate = OPL effective
 * rate / 2 + attackOffset (default 1), decay/release = OPL effective
 * rate / 2 + decayQuarters / 4 (default 9). */
extern void opl_scsp_tune(int attackOffset, int decayQuarters);

/* Silence all OPL voices. */
extern void opl_scsp_reset(void);

#endif /* SATURN_OPL_SCSP_H */
