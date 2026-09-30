/** @file saturn/common/scsp.c Sound through the SCSP, driven from the SH-2.
 *
 * Register layout from the SCSP User's Manual (Figure 4.2, Table 4.4). */

#include <stddef.h>
#include "saturn_hw.h"
#include "scsp.h"
#include "smpc.h"

#define SCSP_RAM        ((volatile uint16_t *)0x25A00000UL)
#define SCSP_SLOT(n, r) REG16(0x25B00000UL + (n) * 0x20 + (r))
#define SCSP_COMMON     REG16(0x25B00400UL)

enum {
	SMPC_CMD_SNDOFF = 0x07,
	SLOT_COUNT = 32,
	BLOCK_MAX = 128,
	OUTPUT_RATE = 44100
};

/* slot register bits */
#define KEY_ON_EXECUTE  (1 << 12)
#define KEY_ON          (1 << 11)
#define PCM_8BIT        (1 << 4)

/* Sound RAM blocks, in address order, covering all of it. */
typedef struct Block {
	int32_t offset;
	uint32_t size;
	int used;
} Block;

static Block s_blocks[BLOCK_MAX];
static int s_blockCount;

void scsp_init(void)
{
	int slot;

	smpc_command(SMPC_CMD_SNDOFF);

	/* 4 Mbit of sound memory, 16-bit DAC, full master volume */
	SCSP_COMMON = (1 << 9) | 0xF;

	for (slot = 0; slot < SLOT_COUNT; slot++) {
		SCSP_SLOT(slot, 0x00) = 0;
		SCSP_SLOT(slot, 0x16) = 0;
	}
	SCSP_SLOT(0, 0x00) = KEY_ON_EXECUTE;

	s_blocks[0].offset = 0;
	s_blocks[0].size = SCSP_RAM_SIZE;
	s_blocks[0].used = 0;
	s_blockCount = 1;
}

int32_t scsp_alloc(uint32_t size)
{
	int i;

	size = (size + 1) & ~1u;    /* keep blocks on 16-bit boundaries */
	for (i = 0; i < s_blockCount; i++) {
		Block *b = &s_blocks[i];
		if (b->used || b->size < size) continue;

		if (b->size > size && s_blockCount < BLOCK_MAX) {
			int j;
			for (j = s_blockCount; j > i + 1; j--) s_blocks[j] = s_blocks[j - 1];
			s_blockCount++;
			s_blocks[i + 1].offset = b->offset + (int32_t)size;
			s_blocks[i + 1].size = b->size - size;
			s_blocks[i + 1].used = 0;
			b->size = size;
		}
		b->used = 1;
		return b->offset;
	}
	return -1;
}

void scsp_free(int32_t offset)
{
	int i;

	for (i = 0; i < s_blockCount; i++) {
		if (s_blocks[i].offset == offset && s_blocks[i].used) break;
	}
	if (i == s_blockCount) return;
	s_blocks[i].used = 0;

	/* merge with free neighbours */
	if (i + 1 < s_blockCount && !s_blocks[i + 1].used) {
		int j;
		s_blocks[i].size += s_blocks[i + 1].size;
		for (j = i + 1; j < s_blockCount - 1; j++) s_blocks[j] = s_blocks[j + 1];
		s_blockCount--;
	}
	if (i > 0 && !s_blocks[i - 1].used) {
		int j;
		s_blocks[i - 1].size += s_blocks[i].size;
		for (j = i; j < s_blockCount - 1; j++) s_blocks[j] = s_blocks[j + 1];
		s_blockCount--;
	}
}

void scsp_upload_u8(int32_t offset, const uint8_t *pcm, uint32_t length)
{
	volatile uint16_t *dst = SCSP_RAM + offset / 2;
	uint32_t i;

	/* signed = unsigned ^ 0x80; two samples per 16-bit write, first one high */
	for (i = 0; i + 1 < length; i += 2) {
		*dst++ = (uint16_t)(((pcm[i] ^ 0x80) << 8) | (pcm[i + 1] ^ 0x80));
	}
	if (i < length) *dst = (uint16_t)((pcm[i] ^ 0x80) << 8);
}

void scsp_play(int slot, int32_t offset, uint32_t samples, uint32_t rate, uint8_t volume)
{
	/* pitch: rate / 44100 = 2^OCT * (1 + FNS / 1024) */
	uint32_t f = (rate << 10) / OUTPUT_RATE;
	int octave = 0;

	if (samples == 0 || f == 0) return;
	if (samples > 0xFFFF) samples = 0xFFFF;
	while (f < 1024) { f <<= 1; octave--; }
	while (f >= 2048) { f >>= 1; octave++; }

	scsp_stop(slot);

	SCSP_SLOT(slot, 0x00) = PCM_8BIT | ((offset >> 16) & 0xF);   /* no loop */
	SCSP_SLOT(slot, 0x02) = (uint16_t)offset;
	SCSP_SLOT(slot, 0x04) = 0;                                   /* LSA */
	SCSP_SLOT(slot, 0x06) = (uint16_t)(samples - 1);             /* LEA */
	SCSP_SLOT(slot, 0x08) = 0x001F;                              /* AR = fastest */
	SCSP_SLOT(slot, 0x0A) = 0x3C1F;                              /* KRS off, RR = fastest */
	SCSP_SLOT(slot, 0x0C) = (uint16_t)((255 - volume) >> 1);     /* TL: attenuation */
	SCSP_SLOT(slot, 0x0E) = 0;
	SCSP_SLOT(slot, 0x10) = (uint16_t)(((octave & 0xF) << 11) | (f - 1024));
	SCSP_SLOT(slot, 0x12) = 0;
	SCSP_SLOT(slot, 0x14) = 0;
	SCSP_SLOT(slot, 0x16) = 0xE000;                              /* direct out, full, centre */

	SCSP_SLOT(slot, 0x00) |= KEY_ON;
	SCSP_SLOT(slot, 0x00) |= KEY_ON_EXECUTE;
}

void scsp_stop(int slot)
{
	SCSP_SLOT(slot, 0x00) = (SCSP_SLOT(slot, 0x00) & ~(KEY_ON | KEY_ON_EXECUTE)) | KEY_ON_EXECUTE;
}
