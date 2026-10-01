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
extern void Scsp_Init(void);

/* Sound RAM allocation. Returns an offset into sound RAM, or -1. */
extern int32_t Scsp_Alloc(uint32_t size);
extern void Scsp_Free(int32_t offset);
/* The largest size Scsp_Alloc() can give now. */
extern uint32_t Scsp_LargestFree(void);

/* Copy unsigned 8-bit PCM to sound RAM as signed 8-bit PCM. */
extern void Scsp_UploadU8(int32_t offset, const uint8_t *pcm, uint32_t length);

/* A note on one slot: signed 8-bit samples in sound RAM. */
typedef struct ScspNote {
	int32_t offset;             /* start in sound RAM */
	uint16_t loopStart;         /* in samples */
	uint16_t end;               /* last sample (loop end when looping) */
	uint8_t loop;               /* 1: loop from loopStart to end */
	uint8_t attack, decay1, decayLevel, decay2, release;   /* EG, 0..31 */
	uint8_t level;              /* TL: attenuation, 0.375 dB units */
	uint16_t pitch;             /* OCT/FNS register value (Scsp_Pitch()) */
	uint8_t pan;                /* DIPAN register value */
} ScspNote;

/* OCT/FNS register value for a playback rate of 44100 Hz * 2^(octave) *
 * (1 + fns / 1024); octave -8..7, fns 0..1023. */
static __inline__ uint16_t Scsp_Pitch(int octave, uint16_t fns)
{
	return (uint16_t)(((octave & 0xF) << 11) | (fns & 0x3FF));
}

extern void Scsp_NoteOn(int slot, const ScspNote *note);
extern void Scsp_NoteOff(int slot);

/* Copy signed 16-bit PCM (count samples) to sound RAM. */
extern void Scsp_UploadS16(int32_t offset, const int16_t *pcm, uint32_t count);

/* Raw slot register access for drivers that set up slots themselves
 * (opl_scsp.c); reg is the byte offset 0x00-0x16 within the slot. */
extern void Scsp_SlotWrite(int slot, int reg, uint16_t value);
extern uint16_t Scsp_SlotRead(int slot, int reg);

/* KEY_ON (on = 1) or KEY_OFF a slot, keeping its other settings. */
extern void Scsp_Key(int slot, int on);

/* Samples of silence a one-shot needs after its data (see Scsp_Play). */
enum { SCSP_TAIL = 32 };

/* Write SCSP_TAIL samples of 8-bit silence at offset. */
extern void Scsp_UploadTail(int32_t offset);

/* Play samples (signed 8-bit, at offset in sound RAM, at most 65535 -
 * SCSP_TAIL of them, followed by Scsp_UploadTail()) once on slot, at rate
 * Hz; volume 0..255. The slot then loops over the silent tail instead of
 * stopping: a stopped slot still keyed on would start again at the next
 * KEY_ON_EXECUTE of any slot. */
extern void Scsp_Play(int slot, int32_t offset, uint32_t samples, uint32_t rate, uint8_t volume);
extern void Scsp_Stop(int slot);

/* Call handler about every millisecond from timer A's interrupt. */
extern void Scsp_TimerStart(void (*handler)(void));

#endif /* SATURN_SCSP_H */
