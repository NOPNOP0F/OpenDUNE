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

/* Stop the sound CPU, clear the slots and set the master volume. */
extern void scsp_init(void);

/* Sound RAM allocation. Returns an offset into sound RAM, or -1. */
extern int32_t scsp_alloc(uint32_t size);
extern void scsp_free(int32_t offset);

/* Copy unsigned 8-bit PCM to sound RAM as signed 8-bit PCM. */
extern void scsp_upload_u8(int32_t offset, const uint8_t *pcm, uint32_t length);

/* Play samples (signed 8-bit, at offset in sound RAM, at most 65535 of them)
 * once on slot, at rate Hz; volume 0..255. */
extern void scsp_play(int slot, int32_t offset, uint32_t samples, uint32_t rate, uint8_t volume);
extern void scsp_stop(int slot);

#endif /* SATURN_SCSP_H */
