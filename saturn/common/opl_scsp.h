/** @file saturn/common/opl_scsp.h An AdLib (OPL2) played by the SCSP.
 *
 * Takes OPL2 register writes, as a game's AdLib driver makes them, and plays
 * them with SCSP slots doing the same 2-operator FM. */

#ifndef SATURN_OPL_SCSP_H
#define SATURN_OPL_SCSP_H

#include <stdint.h>

/* Set up the waveforms and slots; needs Scsp_Init() and SaturnTimer_Init(). */
extern int OplScsp_Init(void);

/* One OPL2 register write. */
extern void OplScsp_Write(uint8_t reg, uint8_t val);

/* End of a driver tick: carry out its key offs (see opl_scsp.c). */
extern void OplScsp_Flush(void);

/* Run the software envelopes (slow attacks); call as often as possible,
 * at least every few milliseconds. */
extern void OplScsp_Update(void);

/* Extra attenuation (0.375 dB units, 255: silent) for channels first..last,
 * for volume and fades. */
extern void OplScsp_SetAttenuation(int first, int last, uint8_t attenuation);

/* Envelope mapping for tuning by ear: SCSP attack rate = OPL effective
 * rate / 2 + attackOffset (default 1), decay/release = OPL effective
 * rate / 2 + decayQuarters / 4 (default 9). */
extern void OplScsp_Tune(int attackOffset, int decayQuarters);

/* Silence all OPL voices. */
extern void OplScsp_Reset(void);

#endif /* SATURN_OPL_SCSP_H */
