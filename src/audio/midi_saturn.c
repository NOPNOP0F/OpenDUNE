/** @file src/audio/midi_saturn.c Sega Saturn MIDI: a General MIDI synthesizer on the SCSP.
 *
 * mt32mpu.c plays the game's XMIDI music and sound effects and sends MIDI
 * messages here; they become notes on SCSP slots 1-31 (slot 0 is speech,
 * see dsp_saturn.c). Instruments come from INSTR.BNK on the disc, built from
 * a General MIDI SoundFont by saturn/tools/make_bank.py: one sample per
 * melodic program and per drum note, loaded into sound RAM at start-up.
 *
 * INSTR.BNK, all big-endian:
 *   "DBNK", u16 version (1), u16 sample count,
 *   u16 melodic[128], u16 drum[128]: sample for a program / drum note, or 0xFFFF,
 *   32-byte entries: u32 offset, u32 length, u32 loop start, u32 loop end
 *     (0: no loop), s16 pitch (cents at the root key, relative to 44100 Hz),
 *     u8 root key, u8 attenuation (0.375 dB), u8 attack, decay 1, decay
 *     level, decay 2, release (SCSP EG values), u8 pan (MIDI, drums), u16 0,
 *   then the samples, signed 8-bit. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "midi.h"
#include "synth_tables_saturn.h"
#include "../os/error.h"

#include "saturn_timer.h"
#include "scsp.h"

enum {
	FIRST_SLOT = 1,
	SLOT_COUNT = 32,
	CHANNELS = 16,
	DRUM_CHANNEL = 9,
	ENTRY_SIZE = 32,
	NO_SAMPLE = 0xFFFF,
	BEND_RANGE_CENTS = 200
};

typedef struct Sample {
	int32 offset;           /* in sound RAM */
	uint32 length;
	uint32 loopStart;
	uint32 loopEnd;
	int16 pitch;
	uint8 rootKey;
	uint8 attenuation;
	uint8 attack, decay1, decayLevel, decay2, release;
	uint8 pan;
} Sample;

typedef struct Channel {
	uint8 program;
	uint8 volume;
	uint8 expression;
	uint8 pan;
	bool sustain;
	int16 bend;             /* cents */
} Channel;

typedef struct Voice {
	bool on;                /* key held (or sustained) */
	bool sustained;         /* released while the pedal was down */
	uint8 channel;
	uint8 note;
	uint8 velocity;
	uint32 started;         /* frame, to steal the oldest */
	const Sample *sample;
} Voice;

static bool s_ready = false;
static Sample *s_samples = NULL;
static uint16 s_sampleCount = 0;
static uint16 s_melodic[128];
static uint16 s_drum[128];
static int32 s_bankOffset = -1;
static Channel s_channels[CHANNELS];
static Voice s_voices[SLOT_COUNT];

static uint16 read_be16(const uint8 *p) { return (uint16)((p[0] << 8) | p[1]); }
static uint32 read_be32(const uint8 *p) { return ((uint32)read_be16(p) << 16) | read_be16(p + 2); }

static bool Midi_LoadBank(const char *path)
{
	FILE *f = fopen(path, "rb");
	uint8 header[8 + 512];
	uint8 *entries = NULL;
	int8 *pcm = NULL;
	uint32 pcmSize, entriesSize;
	long fileSize;
	int i;

	if (f == NULL) {
		Warning("MIDI: no instrument bank %s\n", path);
		return false;
	}
	fseek(f, 0, SEEK_END);
	fileSize = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (fread(header, 1, sizeof(header), f) != sizeof(header) || memcmp(header, "DBNK", 4) != 0 || read_be16(header + 4) != 1) {
		Warning("MIDI: %s is not an instrument bank\n", path);
		fclose(f);
		return false;
	}
	s_sampleCount = read_be16(header + 6);
	for (i = 0; i < 128; i++) {
		s_melodic[i] = read_be16(header + 8 + i * 2);
		s_drum[i] = read_be16(header + 8 + 256 + i * 2);
	}

	entriesSize = (uint32)s_sampleCount * ENTRY_SIZE;
	pcmSize = (uint32)fileSize - sizeof(header) - entriesSize;
	entries = malloc(entriesSize);
	pcm = malloc(pcmSize);
	s_samples = calloc(s_sampleCount, sizeof(Sample));
	s_bankOffset = scsp_alloc(pcmSize);
	if (entries == NULL || pcm == NULL || s_samples == NULL || s_bankOffset < 0 ||
			fread(entries, 1, entriesSize, f) != entriesSize || fread(pcm, 1, pcmSize, f) != pcmSize) {
		Warning("MIDI: cannot load %s (%lu bytes of samples)\n", path, (unsigned long)pcmSize);
		free(entries);
		free(pcm);
		fclose(f);
		return false;
	}
	fclose(f);

	for (i = 0; i < s_sampleCount; i++) {
		const uint8 *e = entries + i * ENTRY_SIZE;
		Sample *s = &s_samples[i];
		s->offset = s_bankOffset + (int32)read_be32(e);
		s->length = read_be32(e + 4);
		s->loopStart = read_be32(e + 8);
		s->loopEnd = read_be32(e + 12);
		s->pitch = (int16)read_be16(e + 16);
		s->rootKey = e[18];
		s->attenuation = e[19];
		s->attack = e[20];
		s->decay1 = e[21];
		s->decayLevel = e[22];
		s->decay2 = e[23];
		s->release = e[24];
		s->pan = e[25];
	}
	scsp_upload_s8(s_bankOffset, pcm, pcmSize);
	free(entries);
	free(pcm);
	return true;
}

static void Midi_ResetChannels(void)
{
	int i;
	for (i = 0; i < CHANNELS; i++) {
		s_channels[i].program = 0;
		s_channels[i].volume = 100;
		s_channels[i].expression = 127;
		s_channels[i].pan = 64;
		s_channels[i].sustain = false;
		s_channels[i].bend = 0;
	}
}

bool midi_init(void)
{
	scsp_init();
	Midi_ResetChannels();
	memset(s_voices, 0, sizeof(s_voices));
	s_ready = Midi_LoadBank("CD/INSTR.BNK");
	return s_ready;
}

void midi_uninit(void)
{
	midi_reset();
	s_ready = false;
}

/* OCT/FNS for a note of a sample, with the channel's pitch bend. */
static uint16 Midi_Pitch(const Voice *v)
{
	int32 cents = v->sample->pitch + (v->note - v->sample->rootKey) * 100 + s_channels[v->channel].bend;
	int32 octave = (cents >= 0) ? cents / 1200 : -((-cents + 1199) / 1200);
	int32 rest = cents - octave * 1200;

	if (octave < -8) return scsp_pitch(-8, 0);
	if (octave > 7) return scsp_pitch(7, 1023);
	return scsp_pitch((int)octave, s_fnsTable[rest]);
}

static uint8 Midi_Level(const Voice *v)
{
	const Channel *c = &s_channels[v->channel];
	uint32 level = v->sample->attenuation + s_levelTable[v->velocity] + s_levelTable[c->volume] + s_levelTable[c->expression];
	return (uint8)((level > 255) ? 255 : level);
}

static uint8 Midi_Pan(const Voice *v)
{
	return scsp_pan(v->channel == DRUM_CHANNEL ? v->sample->pan : s_channels[v->channel].pan);
}

static void Midi_NoteOff(uint8 channel, uint8 note)
{
	int slot;
	for (slot = FIRST_SLOT; slot < SLOT_COUNT; slot++) {
		Voice *v = &s_voices[slot];
		if (!v->on || v->channel != channel || v->note != note) continue;
		if (s_channels[channel].sustain) {
			v->sustained = true;
			continue;
		}
		v->on = false;
		scsp_note_off(slot);
	}
}

static int Midi_FindSlot(void)
{
	int slot, oldest = FIRST_SLOT;

	/* a free slot, else the one released longest ago, else the oldest */
	for (slot = FIRST_SLOT; slot < SLOT_COUNT; slot++) {
		if (s_voices[slot].sample == NULL) return slot;
	}
	for (slot = FIRST_SLOT; slot < SLOT_COUNT; slot++) {
		if (!s_voices[slot].on && (s_voices[oldest].on || s_voices[slot].started < s_voices[oldest].started)) oldest = slot;
	}
	if (!s_voices[oldest].on) return oldest;
	for (slot = FIRST_SLOT; slot < SLOT_COUNT; slot++) {
		if (s_voices[slot].started < s_voices[oldest].started) oldest = slot;
	}
	return oldest;
}

static void Midi_NoteOn(uint8 channel, uint8 note, uint8 velocity)
{
	uint16 index = (channel == DRUM_CHANNEL) ? s_drum[note] : s_melodic[s_channels[channel].program];
	const Sample *sample;
	ScspNote n;
	Voice *v;
	int slot;

	if (index == NO_SAMPLE || index >= s_sampleCount) return;
	sample = &s_samples[index];

	/* the same note again on a channel restarts it */
	Midi_NoteOff(channel, note);

	slot = Midi_FindSlot();
	v = &s_voices[slot];
	v->on = true;
	v->sustained = false;
	v->channel = channel;
	v->note = note;
	v->velocity = velocity;
	v->started = saturn_timer_frames();
	v->sample = sample;

	n.offset = sample->offset;
	n.loop = (sample->loopEnd != 0);
	n.loopStart = (uint16)(n.loop ? sample->loopStart : 0);
	n.end = (uint16)((n.loop ? sample->loopEnd : sample->length) - 1);
	n.attack = sample->attack;
	n.decay1 = sample->decay1;
	n.decayLevel = sample->decayLevel;
	n.decay2 = sample->decay2;
	n.release = sample->release;
	n.level = Midi_Level(v);
	n.pitch = Midi_Pitch(v);
	n.pan = Midi_Pan(v);
	scsp_note_on(slot, &n);
}

static void Midi_AllNotesOff(uint8 channel, bool immediately)
{
	int slot;
	for (slot = FIRST_SLOT; slot < SLOT_COUNT; slot++) {
		Voice *v = &s_voices[slot];
		if (v->sample == NULL || v->channel != channel) continue;
		v->on = false;
		v->sustained = false;
		scsp_note_off(slot);
		if (immediately) v->sample = NULL;
	}
}

/* Apply a channel change to the notes it is playing. */
static void Midi_UpdateChannel(uint8 channel, bool level, bool pitch, bool pan)
{
	int slot;
	for (slot = FIRST_SLOT; slot < SLOT_COUNT; slot++) {
		Voice *v = &s_voices[slot];
		if (v->sample == NULL || v->channel != channel) continue;
		if (level) scsp_set_level(slot, Midi_Level(v));
		if (pitch) scsp_set_pitch(slot, Midi_Pitch(v));
		if (pan) scsp_set_pan(slot, Midi_Pan(v));
	}
}

static void Midi_Controller(uint8 channel, uint8 controller, uint8 value)
{
	Channel *c = &s_channels[channel];

	switch (controller) {
		case 7:  c->volume = value; Midi_UpdateChannel(channel, true, false, false); break;
		case 11: c->expression = value; Midi_UpdateChannel(channel, true, false, false); break;
		case 10: c->pan = value; Midi_UpdateChannel(channel, false, false, true); break;
		case 64:
			c->sustain = (value >= 64);
			if (!c->sustain) {
				int slot;
				for (slot = FIRST_SLOT; slot < SLOT_COUNT; slot++) {
					Voice *v = &s_voices[slot];
					if (v->on && v->sustained && v->channel == channel) {
						v->on = false;
						v->sustained = false;
						scsp_note_off(slot);
					}
				}
			}
			break;
		case 120: Midi_AllNotesOff(channel, true); break;
		case 121:
			c->expression = 127;
			c->sustain = false;
			c->bend = 0;
			Midi_UpdateChannel(channel, true, true, false);
			break;
		case 123: Midi_AllNotesOff(channel, false); break;
		default: break;
	}
}

void midi_send(uint32 data)
{
	uint8 status = data & 0xFF;
	uint8 data1 = (data >> 8) & 0x7F;
	uint8 data2 = (data >> 16) & 0x7F;
	uint8 channel = status & 0x0F;

	if (!s_ready) return;

	switch (status & 0xF0) {
		case 0x80: Midi_NoteOff(channel, data1); break;
		case 0x90:
			if (data2 == 0) Midi_NoteOff(channel, data1);
			else Midi_NoteOn(channel, data1, data2);
			break;
		case 0xB0: Midi_Controller(channel, data1, data2); break;
		case 0xC0: s_channels[channel].program = data1; break;
		case 0xE0:
			s_channels[channel].bend = (int16)((((data2 << 7) | data1) - 8192) * BEND_RANGE_CENTS / 8192);
			Midi_UpdateChannel(channel, false, true, false);
			break;
		default: break;
	}
}

uint16 midi_send_string(const uint8 *data, uint16 len)
{
	/* System exclusive messages are for the MT-32; nothing to do */
	VARIABLE_NOT_USED(data);
	return len;
}

void midi_reset(void)
{
	uint8 channel;

	if (!s_ready) return;
	for (channel = 0; channel < CHANNELS; channel++) Midi_AllNotesOff(channel, true);
	Midi_ResetChannels();
}
