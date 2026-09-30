/** @file saturn/common/scsp.c Sound through the SCSP, driven from the SH-2.
 *
 * Register layout from the SCSP User's Manual (Figure 4.2, Table 4.4). */

#include <stddef.h>
#include "bios.h"
#include "saturn_hw.h"
#include "scsp.h"
#include "smpc.h"

#define SCSP_RAM        ((volatile uint16_t *)0x25A00000UL)
#define SCSP_SLOT(n, r) REG16(0x25B00000UL + (n) * 0x20 + (r))
#define SCSP_COMMON     REG16(0x25B00400UL)
#define SCSP_TIMER_A    REG16(0x25B00418UL)     /* TACTL (10-8), TIMA (7-0) */
#define SCSP_MCIEB      REG16(0x25B0042AUL)     /* main CPU interrupt enable */
#define SCSP_MCIPD      REG16(0x25B0042CUL)     /* main CPU interrupt pending */
#define SCSP_MCIRE      REG16(0x25B0042EUL)     /* main CPU interrupt reset */

enum {
	SMPC_CMD_SNDOFF = 0x07,
	INT_TIMER_A = 1 << 6,       /* SCSP interrupt bit (MAME's SCSP has the same) */
	SCU_VECTOR_SOUND = 0x46,    /* SCU interrupt from the SCSP */
	SCU_MASK_SOUND = 1 << 6,
	/* timer A counts at 44.1 kHz and interrupts at 0xFF: 44 counts = 1 ms */
	TIMER_A_VALUE = (0 << 8) | (255 - 44),
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
static int s_initialised;

void scsp_init(void)
{
	int slot;

	if (s_initialised) return;
	s_initialised = 1;

	smpc_command(SMPC_CMD_SNDOFF);

	/* 4 Mbit of sound memory, 16-bit DAC, full master volume */
	SCSP_COMMON = (1 << 9) | 0xF;

	for (slot = 0; slot < SLOT_COUNT; slot++) {
		SCSP_SLOT(slot, 0x00) = 0;
		SCSP_SLOT(slot, 0x16) = 0;
	}
	SCSP_SLOT(0, 0x00) = KEY_ON_EXECUTE;

	/* CD audio (EXTS0/1, set through slots 16/17): full level, left/right */
	SCSP_SLOT(16, 0x16) = (7 << 5) | 0x1F;
	SCSP_SLOT(17, 0x16) = (7 << 5) | 0x0F;

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

void scsp_upload_s16(int32_t offset, const int16_t *pcm, uint32_t count)
{
	volatile uint16_t *dst = SCSP_RAM + offset / 2;
	uint32_t i;
	for (i = 0; i < count; i++) dst[i] = (uint16_t)pcm[i];
}

void scsp_slot_write(int slot, int reg, uint16_t value)
{
	SCSP_SLOT(slot, reg) = value;
}

uint16_t scsp_slot_read(int slot, int reg)
{
	return SCSP_SLOT(slot, reg);
}

void scsp_key(int slot, int on)
{
	uint16_t value = SCSP_SLOT(slot, 0x00) & ~(KEY_ON | KEY_ON_EXECUTE);
	if (on) value |= KEY_ON;
	SCSP_SLOT(slot, 0x00) = value;
	SCSP_SLOT(slot, 0x00) = value | KEY_ON_EXECUTE;
}

void scsp_note_on(int slot, const ScspNote *note)
{
	scsp_note_off(slot);

	SCSP_SLOT(slot, 0x00) = PCM_8BIT | ((note->loop ? 1 : 0) << 5) | ((note->offset >> 16) & 0xF);
	SCSP_SLOT(slot, 0x02) = (uint16_t)note->offset;
	SCSP_SLOT(slot, 0x04) = note->loopStart;
	SCSP_SLOT(slot, 0x06) = note->end;
	SCSP_SLOT(slot, 0x08) = (uint16_t)((note->decay2 << 11) | (note->decay1 << 6) | note->attack);
	SCSP_SLOT(slot, 0x0A) = (uint16_t)((0xF << 10) | (note->decayLevel << 5) | note->release);   /* KRS off */
	SCSP_SLOT(slot, 0x0C) = note->level;
	SCSP_SLOT(slot, 0x0E) = 0;
	SCSP_SLOT(slot, 0x10) = note->pitch;
	SCSP_SLOT(slot, 0x12) = 0;
	SCSP_SLOT(slot, 0x14) = 0;
	SCSP_SLOT(slot, 0x16) = (uint16_t)(0xE000 | ((note->pan & 0x1F) << 8));   /* direct out, full level */

	SCSP_SLOT(slot, 0x00) |= KEY_ON;
	SCSP_SLOT(slot, 0x00) |= KEY_ON_EXECUTE;
}

void scsp_note_off(int slot)
{
	SCSP_SLOT(slot, 0x00) = (SCSP_SLOT(slot, 0x00) & ~(KEY_ON | KEY_ON_EXECUTE)) | KEY_ON_EXECUTE;
}

void scsp_upload_tail(int32_t offset)
{
	volatile uint16_t *dst = SCSP_RAM + offset / 2;
	int i;
	for (i = 0; i < SCSP_TAIL / 2; i++) dst[i] = 0;
}

void scsp_play(int slot, int32_t offset, uint32_t samples, uint32_t rate, uint8_t volume)
{
	/* pitch: rate / 44100 = 2^OCT * (1 + FNS / 1024) */
	uint32_t f = (rate << 10) / OUTPUT_RATE;
	int octave = 0;
	ScspNote note;

	if (samples == 0 || f == 0) return;
	if (samples > 0xFFFF - SCSP_TAIL) samples = 0xFFFF - SCSP_TAIL;
	while (f < 1024) { f <<= 1; octave--; }
	while (f >= 2048) { f >>= 1; octave++; }

	/* once through the samples, then round the silent tail */
	note.offset = offset;
	note.loopStart = (uint16_t)samples;
	note.end = (uint16_t)(samples + SCSP_TAIL - 1);
	note.loop = 1;
	note.attack = 31;
	note.decay1 = 0;
	note.decayLevel = 0;
	note.decay2 = 0;
	note.release = 31;
	note.level = (uint8_t)((255 - volume) >> 1);
	note.pitch = scsp_pitch(octave, (uint16_t)(f - 1024));
	note.pan = 0;
	scsp_note_on(slot, &note);
}

void scsp_stop(int slot)
{
	scsp_note_off(slot);
}

static void (*s_timerHandler)(void);

static void scsp_interrupt(void)
{
	if (!(SCSP_MCIPD & INT_TIMER_A)) return;
	SCSP_MCIRE = INT_TIMER_A;
	SCSP_TIMER_A = TIMER_A_VALUE;       /* count the next millisecond */
	if (s_timerHandler != NULL) s_timerHandler();
}

void scsp_timer_start(void (*handler)(void))
{
	s_timerHandler = handler;
	SCSP_MCIRE = 0x7FF;
	SCSP_TIMER_A = TIMER_A_VALUE;
	SCSP_MCIEB = INT_TIMER_A;
	BIOS_SETUINT(SCU_VECTOR_SOUND, scsp_interrupt);
	BIOS_CHGSCUIM(~SCU_MASK_SOUND, 0);
}
