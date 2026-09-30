/** @file saturn/common/scsp.h Sound through the SCSP, driven from the SH-2.
 *
 * The sound CPU (68000) is stopped; the SH-2 writes sound RAM and the slot
 * registers itself. Samples are signed 8-bit PCM in sound RAM. */

#ifndef SATURN_SCSP_H
#define SATURN_SCSP_H

#include <stdint.h>

enum {
	SCSP_RAM_SIZE = 512 * 1024
};

/* Stop the sound CPU, clear the slots and set the master volume. Only the
 * first call does anything. */
extern void scsp_init(void);

/* Sound RAM allocation. Returns an offset into sound RAM, or -1. */
extern int32_t scsp_alloc(uint32_t size);
extern void scsp_free(int32_t offset);

/* Copy unsigned 8-bit PCM to sound RAM as signed 8-bit PCM. */
extern void scsp_upload_u8(int32_t offset, const uint8_t *pcm, uint32_t length);

/* Copy signed 8-bit PCM to sound RAM as it is. */
extern void scsp_upload_s8(int32_t offset, const int8_t *pcm, uint32_t length);

/* A note on one slot: signed 8-bit samples in sound RAM. */
typedef struct ScspNote {
	int32_t offset;             /* start in sound RAM */
	uint16_t loopStart;         /* in samples */
	uint16_t end;               /* last sample (loop end when looping) */
	uint8_t loop;               /* 1: loop from loopStart to end */
	uint8_t attack, decay1, decayLevel, decay2, release;   /* EG, 0..31 */
	uint8_t level;              /* TL: attenuation, 0.375 dB units */
	uint16_t pitch;             /* OCT/FNS register value (scsp_pitch()) */
	uint8_t pan;                /* DIPAN register value */
} ScspNote;

/* OCT/FNS register value for a playback rate of 44100 Hz * 2^(octave) *
 * (1 + fns / 1024); octave -8..7, fns 0..1023. */
static inline uint16_t scsp_pitch(int octave, uint16_t fns)
{
	return (uint16_t)(((octave & 0xF) << 11) | (fns & 0x3FF));
}

/* DIPAN value for a MIDI pan position (0 left, 64 centre, 127 right). */
extern uint8_t scsp_pan(uint8_t midiPan);

extern void scsp_note_on(int slot, const ScspNote *note);
extern void scsp_note_off(int slot);
extern void scsp_set_level(int slot, uint8_t level);
extern void scsp_set_pitch(int slot, uint16_t pitch);
extern void scsp_set_pan(int slot, uint8_t pan);

/* Play samples (signed 8-bit, at offset in sound RAM, at most 65535 of them)
 * once on slot, at rate Hz; volume 0..255. */
extern void scsp_play(int slot, int32_t offset, uint32_t samples, uint32_t rate, uint8_t volume);
extern void scsp_stop(int slot);

#endif /* SATURN_SCSP_H */
