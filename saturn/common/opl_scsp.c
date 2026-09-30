/** @file saturn/common/opl_scsp.c An AdLib (OPL2) played by the SCSP.
 *
 * Each of the 9 OPL channels gets two SCSP slots, modulator and carrier,
 * playing one of the four OPL waveforms from a 1024-sample 16-bit table
 * (the SCSP's FM depth, MDL, is defined for 1024-sample waves):
 *   frequency   fnum * 49716 / 2^(20 - block) * multiple, as OCT/FNS
 *   FM          carrier modulated by the modulator's output, MDL 0xC
 *               (+-8 pi, like the OPL's full-level modulator); additive
 *               channels instead send both slots to the output
 *   feedback    the modulator modulates itself, MDL = feedback + 4
 *   envelope    OPL rates 0-15 -> SCSP rates 2 * rate + 3, key scale rate
 *               applied here; sustain level -> DL; non-sustaining
 *               operators keep decaying at the release rate (D2R)
 *   level       total level and key scale level -> TL
 *   AM / VIB    the slot's LFO (triangle), at the OPL's rates and depths
 * Register values from the SCSP User's Manual; the OPL side follows the
 * YM3812 behaviour as modelled by Nuked OPL3. Tuned by ear against the
 * reference renders of saturn/tools/adl_render.c. */

#include <stdbool.h>
#include <stddef.h>
#include "opl_scsp.h"
#include "saturn_timer.h"
#include "scsp.h"

enum {
	CHANNELS = 9,
	FIRST_SLOT = 1,             /* slot 0 plays speech */
	WAVE_SAMPLES = 1024,
	MIX_LEVEL = 5               /* DISDL of sounding operators: -12 dB */
};

/* operator register offset of each channel's modulator; carrier is +3 */
static const uint8_t s_opOffset[CHANNELS] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };

/* frequency multiple, in halves */
static const uint8_t s_multiple2[16] = { 1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30 };

/* key scale level base by fnum >> 6 (as in Nuked OPL3) */
static const uint8_t s_kslRom[16] = { 0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64 };
static const uint8_t s_kslShift[4] = { 8, 1, 2, 0 };

static uint8_t s_regs[256];
static int32_t s_wave[4] = { -1, -1, -1, -1 };  /* sound RAM offsets of the waveforms */
static uint8_t s_keyOn[CHANNELS];

static int op_slot(int channel, int op) { return FIRST_SLOT + channel * 2 + op; }
static uint8_t op_reg(int base, int channel, int op) { return s_regs[base + s_opOffset[channel] + op * 3]; }

/* The four OPL2 waveforms, 1025 samples each (the last repeats the first,
 * for the loop). The sine comes from the recurrence
 * s[n+1] = 2 cos(w) s[n] - s[n-1], in 16.16 fixed point. */
static int build_waves(void)
{
	static int16_t wave[WAVE_SAMPLES + 1];
	static int32_t quarter[WAVE_SAMPLES / 4 + 1];
	int32_t prev = 0, cur = 13176310;       /* sin(2 pi / 1024) * 32767, 16.16 */
	int n, w;

	quarter[0] = 0;
	for (n = 1; n <= WAVE_SAMPLES / 4; n++) {
		int32_t next = (int32_t)(((int64_t)2147443222 * cur) >> 30) - prev;   /* 2 cos(w), Q30 */
		quarter[n] = (cur + 0x8000) >> 16;
		prev = cur;
		cur = next;
	}
	quarter[WAVE_SAMPLES / 4] = 32767;

	for (w = 0; w < 4; w++) {
		int32_t offset = scsp_alloc(sizeof(wave));
		if (offset < 0) return 0;
		for (n = 0; n < WAVE_SAMPLES; n++) {
			int q = n % (WAVE_SAMPLES / 2);
			int32_t sine = (q <= WAVE_SAMPLES / 4) ? quarter[q] : quarter[WAVE_SAMPLES / 2 - q];
			bool secondHalf = n >= WAVE_SAMPLES / 2;
			int32_t value;
			switch (w) {
				default:
				case 0: value = secondHalf ? -sine : sine; break;             /* sine */
				case 1: value = secondHalf ? 0 : sine; break;                 /* half sine */
				case 2: value = sine; break;                                  /* absolute sine */
				case 3: value = (q < WAVE_SAMPLES / 4) ? sine : 0; break;     /* quarter sine */
			}
			wave[n] = (int16_t)value;
		}
		wave[WAVE_SAMPLES] = wave[0];
		scsp_upload_s16(offset, wave, WAVE_SAMPLES + 1);
		s_wave[w] = offset;
	}
	return 1;
}

/* OCT/FNS for an operator: a 1024-sample wave at the operator frequency. */
static uint16_t op_pitch(int channel, int op)
{
	uint8_t bx = s_regs[0xB0 + channel];
	uint32_t fnum = ((uint32_t)(bx & 3) << 8) | s_regs[0xA0 + channel];
	uint32_t block = (bx >> 2) & 7;
	uint32_t x = (fnum * s_multiple2[op_reg(0x20, channel, op) & 0xF]) << block;
	/* rate / 44100 in 16.16: x * 49716 * 1024 / 2^20 / 44100 / 2 = x * 36.0751 */
	uint64_t ratio = ((uint64_t)x * 2364218) >> 16;
	int octave = 0;

	if (ratio == 0) return scsp_pitch(-8, 0);
	while (ratio >= (2u << 16)) { ratio >>= 1; octave++; }
	while (ratio < (1u << 16)) { ratio <<= 1; octave--; }
	if (octave > 7) return scsp_pitch(7, 1023);
	if (octave < -8) return scsp_pitch(-8, 0);
	return scsp_pitch(octave, (uint16_t)((ratio - 65536) >> 6));
}

/* SCSP rate for an OPL rate (0-15) with the channel's key scale offset. */
static uint16_t scsp_rate(uint8_t rate, int rateOffset)
{
	int effective;
	if (rate == 0) return 0;
	effective = rate * 4 + rateOffset;      /* 4..63, as in the OPL */
	if (effective >= 60) return 31;
	effective = effective / 2 + 3;
	return (uint16_t)(effective > 31 ? 31 : effective);
}

static void op_setup(int channel, int op)
{
	int slot = op_slot(channel, op);
	uint8_t r20 = op_reg(0x20, channel, op);
	uint8_t r40 = op_reg(0x40, channel, op);
	uint8_t r60 = op_reg(0x60, channel, op);
	uint8_t r80 = op_reg(0x80, channel, op);
	uint8_t c0 = s_regs[0xC0 + channel];
	uint8_t bx = s_regs[0xB0 + channel];
	uint32_t fnum = ((uint32_t)(bx & 3) << 8) | s_regs[0xA0 + channel];
	int block = (bx >> 2) & 7;
	int additive = c0 & 1;
	int sounding = additive || op == 1;
	int wave = (s_regs[0x01] & 0x20) ? (op_reg(0xE0, channel, op) & 3) : 0;
	int32_t start = s_wave[wave];
	int keyScale = block * 2 + ((s_regs[0x08] & 0x40) ? (fnum >> 8) & 1 : (fnum >> 9) & 1);
	int rateOffset = (r20 & 0x10) ? keyScale : keyScale >> 2;
	int ksl = (s_kslRom[fnum >> 6] << 2) - ((8 - block) << 5);
	uint32_t level;
	uint16_t sustain, lfo = 0, modulation = 0;

	/* level: total level in 0.75 dB, key scale level in 0.1875 dB */
	if (ksl < 0) ksl = 0;
	level = (r40 & 0x3F) * 2 + ((uint32_t)ksl >> s_kslShift[r40 >> 6]) / 2;
	if (level > 255) level = 255;

	/* sustaining operators hold at the sustain level, others keep decaying */
	sustain = (r20 & 0x20) ? 0 : scsp_rate(r80 & 0xF, rateOffset);

	if (r20 & 0xC0) {
		/* LFO: vibrato 6.15 Hz at 7 or 13.5 cents, tremolo 3.9 Hz at 0.8 or 3 dB */
		int deep = s_regs[0xBD];
		lfo = (uint16_t)(((r20 & 0x40) ? 0x14 : 0x12) << 10);
		if (r20 & 0x40) lfo |= (2 << 8) | (((deep & 0x40) ? 2 : 1) << 5);
		if (r20 & 0x80) lfo |= (2 << 3) | ((deep & 0x80) ? 4 : 2);
	}

	if (op == 0 && (c0 & 0x0E)) {
		/* feedback: the modulator's own previous output */
		modulation = (uint16_t)((((c0 >> 1) & 7) + 4) << 12);
	} else if (op == 1 && !additive) {
		/* the modulator's output moves the carrier's phase */
		uint16_t source = (uint16_t)((op_slot(channel, 0) - slot) & 0x3F);
		modulation = (uint16_t)((0xC << 12) | (source << 6) | source);
	}

	scsp_slot_write(slot, 0x00, (uint16_t)((scsp_slot_read(slot, 0x00) & (1 << 11)) | (1 << 5) | ((start >> 16) & 0xF)));
	scsp_slot_write(slot, 0x02, (uint16_t)start);
	scsp_slot_write(slot, 0x04, 0);
	scsp_slot_write(slot, 0x06, WAVE_SAMPLES);
	scsp_slot_write(slot, 0x08, (uint16_t)((sustain << 11) | (scsp_rate(r60 & 0xF, rateOffset) << 6) | scsp_rate(r60 >> 4, rateOffset)));
	scsp_slot_write(slot, 0x0A, (uint16_t)((0xF << 10) | (((r80 >> 4) == 15 ? 31 : (r80 >> 4)) << 5) | scsp_rate(r80 & 0xF, rateOffset)));
	scsp_slot_write(slot, 0x0C, (uint16_t)level);
	scsp_slot_write(slot, 0x0E, modulation);
	scsp_slot_write(slot, 0x10, op_pitch(channel, op));
	scsp_slot_write(slot, 0x12, lfo);
	scsp_slot_write(slot, 0x14, 0);
	scsp_slot_write(slot, 0x16, (uint16_t)(sounding ? (MIX_LEVEL << 13) : 0));
}

static void channel_key(int channel, int on)
{
	if (on) {
		op_setup(channel, 0);
		op_setup(channel, 1);
		/* the driver keys off and on in one go: leave the SCSP at least one
		 * sample (22.7 us) to see the key off, so the note restarts */
		saturn_delay_us(30);
	}
	scsp_key(op_slot(channel, 0), on);
	scsp_key(op_slot(channel, 1), on);
}

void opl_scsp_reset(void)
{
	int channel;
	for (channel = 0; channel < CHANNELS; channel++) {
		scsp_key(op_slot(channel, 0), 0);
		scsp_key(op_slot(channel, 1), 0);
		s_keyOn[channel] = 0;
	}
}

int opl_scsp_init(void)
{
	if (s_wave[0] < 0 && !build_waves()) return 0;
	opl_scsp_reset();
	return 1;
}

void opl_scsp_write(uint8_t reg, uint8_t val)
{
	int channel;

	s_regs[reg] = val;

	if (reg >= 0x20 && reg < 0xA0) {
		/* operator registers: find the channel and operator */
		int offset = reg & 0x1F;
		int op, i;
		for (i = 0; i < CHANNELS; i++) {
			for (op = 0; op < 2; op++) {
				if (s_opOffset[i] + op * 3 == offset) {
					if (s_keyOn[i]) op_setup(i, op);
					return;
				}
			}
		}
		return;
	}
	if (reg >= 0xE0 && reg <= 0xF5) {
		/* waveform: used from the next note on */
		return;
	}
	if (reg >= 0xA0 && reg <= 0xA8) {
		channel = reg - 0xA0;
		if (s_keyOn[channel]) {
			scsp_slot_write(op_slot(channel, 0), 0x10, op_pitch(channel, 0));
			scsp_slot_write(op_slot(channel, 1), 0x10, op_pitch(channel, 1));
		}
		return;
	}
	if (reg >= 0xB0 && reg <= 0xB8) {
		int on = (val >> 5) & 1;
		channel = reg - 0xB0;
		if (on != s_keyOn[channel]) {
			s_keyOn[channel] = (uint8_t)on;
			channel_key(channel, on);
		} else if (on) {
			scsp_slot_write(op_slot(channel, 0), 0x10, op_pitch(channel, 0));
			scsp_slot_write(op_slot(channel, 1), 0x10, op_pitch(channel, 1));
		}
		return;
	}
	if (reg >= 0xC0 && reg <= 0xC8) {
		channel = reg - 0xC0;
		if (s_keyOn[channel]) {
			op_setup(channel, 0);
			op_setup(channel, 1);
		}
		return;
	}
}
