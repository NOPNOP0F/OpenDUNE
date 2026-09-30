#!/usr/bin/env python3
"""Build INSTR.BNK, the instrument bank of the Saturn MIDI synthesizer.

Takes the General MIDI programs and drum notes that Dune II's music and
sound effects (.C55 files in SOUND.PAK) use, pulls one sample for each from
a General MIDI SoundFont (.sf2), and writes them as signed 8-bit PCM sized
to fit a sound RAM budget. The format is described in
src/audio/midi_saturn.c.

Usage: make_bank.py SOUND.PAK SOUNDFONT.sf2 OUT.BNK [--budget BYTES]
"""

import argparse
import collections
import math
import struct
import sys

import numpy as np

import xmi

OUTPUT_RATE = 44100
MIN_RATE = 8000
MAX_RATE = 22050
MAX_SECONDS_MELODIC = 0.3   # kept of a sample; longer ones get a new loop
MAX_SECONDS_DRUM = 0.4      # one-shots are cut (with a fade) after this
LOOP_SECONDS = 0.04         # about this long, a whole number of cycles
TL_STEP_DB = 0.375
EG_RANGE_DB = 96.0

# SoundFont generator numbers
GEN_START, GEN_END, GEN_LOOP_START, GEN_LOOP_END = 0, 1, 2, 3
GEN_START_COARSE, GEN_END_COARSE = 4, 12
GEN_PAN = 17
GEN_ATTACK, GEN_HOLD, GEN_DECAY, GEN_SUSTAIN, GEN_RELEASE = 34, 35, 36, 37, 38
GEN_INSTRUMENT, GEN_KEY_RANGE, GEN_VEL_RANGE = 41, 43, 44
GEN_LOOP_START_COARSE = 45
GEN_ATTENUATION = 48
GEN_LOOP_END_COARSE = 50
GEN_COARSE_TUNE, GEN_FINE_TUNE, GEN_SAMPLE, GEN_SAMPLE_MODES = 51, 52, 53, 54
GEN_ROOT_KEY = 58


# --- SoundFont --------------------------------------------------------------

class SoundFont:
    def __init__(self, path):
        data = open(path, 'rb').read()
        if data[0:4] != b'RIFF' or data[8:12] != b'sfbk':
            sys.exit('%s is not a SoundFont' % path)
        self.chunks = {}
        self._walk(data, 12, len(data))
        self.samples = np.frombuffer(self.chunks[b'smpl'], dtype='<i2')
        self.presets = self._records(b'phdr', '<20sHHHIII')
        self.pbag = self._records(b'pbag', '<HH')
        self.pgen = self._records(b'pgen', '<HH')
        self.insts = self._records(b'inst', '<20sH')
        self.ibag = self._records(b'ibag', '<HH')
        self.igen = self._records(b'igen', '<HH')
        self.shdr = self._records(b'shdr', '<20sIIIIIBbHH')

    def _walk(self, data, pos, end):
        while pos + 8 <= end:
            tag = data[pos:pos + 4]
            size = struct.unpack_from('<I', data, pos + 4)[0]
            if tag == b'LIST':
                self._walk(data, pos + 12, pos + 8 + size)
            else:
                self.chunks[tag] = data[pos + 8:pos + 8 + size]
            pos += 8 + size + (size & 1)

    def _records(self, tag, fmt):
        size = struct.calcsize(fmt)
        chunk = self.chunks[tag]
        return [struct.unpack_from(fmt, chunk, i) for i in range(0, len(chunk) - size + 1, size)]

    @staticmethod
    def _in_range(gens, gen, value):
        if gen not in gens:
            return True
        lo, hi = gens[gen] & 0xFF, gens[gen] >> 8
        return lo <= value <= hi

    def _zones(self, bags, gens, first, last):
        """Generator dicts of zones first..last-1; a leading global zone is merged into the others."""
        zones = []
        for b in range(first, last):
            g0, g1 = bags[b][0], bags[b + 1][0]
            zones.append({op: amount for op, amount in gens[g0:g1]})
        return zones

    def find(self, bank, program, key, velocity=100):
        """Return (instrument generators, preset generators) for a note, or None."""
        for p, preset in enumerate(self.presets[:-1]):
            if preset[1] == program and preset[2] == bank:
                break
        else:
            return None
        pzones = self._zones(self.pbag, self.pgen, preset[3], self.presets[p + 1][3])
        pglobal = pzones[0] if pzones and GEN_INSTRUMENT not in pzones[0] else {}
        for pz in pzones:
            if GEN_INSTRUMENT not in pz or not self._in_range(pz, GEN_KEY_RANGE, key) or not self._in_range(pz, GEN_VEL_RANGE, velocity):
                continue
            inst = pz[GEN_INSTRUMENT]
            izones = self._zones(self.ibag, self.igen, self.insts[inst][1], self.insts[inst + 1][1])
            iglobal = izones[0] if izones and GEN_SAMPLE not in izones[0] else {}
            for iz in izones:
                if GEN_SAMPLE not in iz or not self._in_range(iz, GEN_KEY_RANGE, key) or not self._in_range(iz, GEN_VEL_RANGE, velocity):
                    continue
                merged_i = dict(iglobal)
                merged_i.update(iz)
                merged_p = dict(pglobal)
                merged_p.update(pz)
                return merged_i, merged_p
        return None


def signed(value):
    return value - 0x10000 if value >= 0x8000 else value


def timecents_to_seconds(tc):
    return 0.0 if tc <= -12000 else 2 ** (signed(tc) / 1200)


# --- SCSP conversions ---------------------------------------------------------

def attack_rate(seconds):
    """SCSP AR for an attack time. The time halves every two rate steps;
    rate 2 is about 8.1 s."""
    ms = seconds * 1000
    if ms < 1:
        return 31
    return int(max(0, min(31, round(2 - 2 * math.log2(ms / 8100)))))


def decay_rate(seconds):
    """SCSP D1R/D2R/RR for the time of a full-range (96 dB) decay; rate 2 is about 118 s."""
    ms = seconds * 1000
    if ms < 1:
        return 31
    return int(max(0, min(31, round(2 - 2 * math.log2(ms / 118200)))))


# --- sample extraction ----------------------------------------------------------

class Voice:
    """One sample of the bank, before and after resampling."""

    def __init__(self, sf, igens, pgens, drum):
        def gen(g, default=0):
            return signed(igens.get(g, default)) + signed(pgens.get(g, 0)) if g in igens or g in pgens else default

        header = sf.shdr[igens[GEN_SAMPLE]]
        _, start, end, loop_start, loop_end, rate, original_pitch, correction, _, _ = header
        start += gen(GEN_START) + 32768 * gen(GEN_START_COARSE)
        end += gen(GEN_END) + 32768 * gen(GEN_END_COARSE)
        loop_start += gen(GEN_LOOP_START) + 32768 * gen(GEN_LOOP_START_COARSE)
        loop_end += gen(GEN_LOOP_END) + 32768 * gen(GEN_LOOP_END_COARSE)

        self.key = (header, start, end, loop_start, loop_end)
        self.pcm = sf.samples[start:end].astype(np.float64)
        self.looped = (igens.get(GEN_SAMPLE_MODES, 0) & 3) in (1, 3) and loop_end > loop_start
        self.loop_start = loop_start - start
        self.loop_end = loop_end - start
        self.rate = rate
        root = igens.get(GEN_ROOT_KEY, 0xFFFF)
        self.root_key = root if root < 128 else (original_pitch if original_pitch < 128 else 60)
        self.tune_cents = correction + 100 * gen(GEN_COARSE_TUNE) + gen(GEN_FINE_TUNE)
        self.attenuation_db = max(0, gen(GEN_ATTENUATION)) / 10 * 0.4   # "centibels" as players apply them
        self.attack = timecents_to_seconds(gen(GEN_ATTACK, -12000)) + timecents_to_seconds(gen(GEN_HOLD, -12000))
        self.decay = timecents_to_seconds(gen(GEN_DECAY, -12000))
        self.sustain_db = max(0, gen(GEN_SUSTAIN)) / 10
        self.release = timecents_to_seconds(gen(GEN_RELEASE, -12000))
        self.pan = int(max(0, min(127, round(64 + gen(GEN_PAN) * 64 / 500))))
        self.shorten(MAX_SECONDS_DRUM if drum or not self.looped else MAX_SECONDS_MELODIC)

    def shorten(self, max_seconds):
        """Keep at most max_seconds of the sample. A looped sample that is
        longer gets a new short loop just before the cut: a whole number of
        cycles of its root pitch, where the waveform repeats best, with a
        crossfade at the loop point."""
        cut = int(max_seconds * self.rate)
        if not self.looped:
            self.pcm = self.pcm[:cut]
            return
        if self.loop_end <= cut:
            self.pcm = self.pcm[:self.loop_end]
            return

        period = self.rate / (440 * 2 ** ((self.root_key - 69) / 12))
        cycles = max(1, round(LOOP_SECONDS * self.rate / period))
        length = max(16, int(round(cycles * period)))
        cut = max(cut, 2 * length + 64)
        best, best_score = cut - length, -2.0
        # try loop ends near the cut; score: correlation of the loop's last
        # cycle with the cycle just before its start
        window = min(length, int(round(period)) or 1)
        for end in range(cut - length // 2, cut + 1):
            start = end - length
            a = self.pcm[end - window:end]
            b = self.pcm[start - window:start]
            if len(a) != window or len(b) != window:
                continue
            na, nb = np.linalg.norm(a), np.linalg.norm(b)
            score = float(np.dot(a, b) / (na * nb)) if na and nb else -1.0
            if score > best_score:
                best, best_score = end, score
        end = best
        start = end - length
        pcm = self.pcm[:end].copy()
        fade = min(length // 2, window)
        ramp = np.linspace(0, 1, fade)
        # the end of the loop blends into what precedes its start
        pcm[end - fade:end] = pcm[end - fade:end] * (1 - ramp) + self.pcm[start - fade:start] * ramp
        self.pcm = pcm
        self.loop_start, self.loop_end = start, end

    def kept_length(self):
        return len(self.pcm)

    def encode(self, scale):
        """Resample by scale (<= 1) and quantise; sets the output fields."""
        rate = min(self.rate * scale, MAX_RATE)
        if self.looped:
            # keep the loop an exact number of samples so the pitch stays right
            loop_len = self.loop_end - self.loop_start
            new_len = max(8, round(loop_len * rate / self.rate))
            rate = self.rate * new_len / loop_len
        rate = min(max(rate, min(MIN_RATE, self.rate)), max(MAX_RATE, self.rate * scale))
        source = self.pcm[:self.kept_length()]
        count = max(1, int(round(len(source) * rate / self.rate)))
        # a light low-pass before dropping samples, then linear interpolation
        step = self.rate / rate
        if step > 1.5:
            width = int(step)
            source = np.convolve(source, np.ones(width) / width, mode='same')
        positions = np.arange(count) * step
        out = np.interp(positions, np.arange(len(source)), source)
        if not self.looped and count > 16:
            fade = min(count // 8, int(rate * 0.02))
            out[-fade:] *= np.linspace(1, 0, fade)
        peak = max(1.0, float(np.abs(out).max()))
        out = np.clip(np.round(out * 127 / peak), -128, 127).astype(np.int8)
        gain_db = 20 * math.log10(32767 / peak)

        self.out = out
        self.out_rate = rate
        self.out_loop_start = int(round(self.loop_start * rate / self.rate)) if self.looped else 0
        self.out_loop_end = int(round(self.loop_end * rate / self.rate)) if self.looped else 0
        if self.looped:
            self.out_loop_end = min(self.out_loop_end, len(out))
            self.out_loop_start = min(self.out_loop_start, self.out_loop_end - 1)
        self.out_attenuation = int(max(0, min(255, round((self.attenuation_db + gain_db) / TL_STEP_DB))))
        self.out_pitch = int(round(1200 * math.log2(rate / OUTPUT_RATE) + self.tune_cents))
        return len(out)

    def entry(self, offset):
        decay_level = int(max(0, min(31, round(self.sustain_db / EG_RANGE_DB * 31))))
        return struct.pack('>IIIIhBBBBBBBBH',
                           offset, len(self.out), self.out_loop_start, self.out_loop_end,
                           max(-32768, min(32767, self.out_pitch)), self.root_key, self.out_attenuation,
                           attack_rate(self.attack), decay_rate(self.decay), decay_level,
                           0, decay_rate(self.release), self.pan, 0)


# --- what the game uses ---------------------------------------------------------

def used_notes(sound_pak):
    """Return ({program: Counter(notes)}, Counter(drum notes)) for the .C55 files."""
    files = xmi.read_pak(sound_pak)
    melodic = collections.defaultdict(collections.Counter)
    drums = collections.Counter()
    for name in sorted(n for n in files if n.endswith('.C55')):
        for evnt in xmi.sequences(files[name]):
            program = [0] * 16
            for _, kind, channel, a, b, _ in xmi.events(evnt):
                if kind == 0xC0:
                    program[channel] = a
                elif kind == 0x90 and b:
                    if channel == 9:
                        drums[a] += 1
                    else:
                        melodic[program[channel]][a] += 1
    return melodic, drums


def weighted_median(counter):
    total = sum(counter.values())
    seen = 0
    for note in sorted(counter):
        seen += counter[note]
        if seen * 2 >= total:
            return note
    return 60


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('sound_pak')
    parser.add_argument('soundfont')
    parser.add_argument('output')
    parser.add_argument('--budget', type=int, default=256 * 1024, help='bytes of samples (default 262144)')
    args = parser.parse_args()

    sf = SoundFont(args.soundfont)
    melodic, drums = used_notes(args.sound_pak)

    voices, by_key = [], {}
    melodic_map, drum_map = [0xFFFF] * 128, [0xFFFF] * 128

    def add(bank, program, key, drum):
        found = sf.find(bank, program, key)
        if found is None:
            print('  no sample for bank %d program %d key %d' % (bank, program, key))
            return 0xFFFF
        voice = Voice(sf, found[0], found[1], drum)
        if voice.key in by_key:
            return by_key[voice.key]
        by_key[voice.key] = len(voices)
        voices.append(voice)
        return len(voices) - 1

    for program, notes in sorted(melodic.items()):
        melodic_map[program] = add(0, program, weighted_median(notes), False)
    for note in sorted(drums):
        drum_map[note] = add(128, 0, note, True)

    # one resampling factor for everything, lowered until the bank fits
    scale = 1.0
    while True:
        total = 0
        for v in voices:
            length = v.encode(scale)
            total += length + (length & 1)
        if total <= args.budget or scale < 0.05:
            break
        scale *= 0.93

    header = b'DBNK' + struct.pack('>HH', 1, len(voices))
    header += struct.pack('>128H', *melodic_map) + struct.pack('>128H', *drum_map)
    entries, pcm = b'', b''
    for v in voices:
        entries += v.entry(len(pcm))
        pcm += v.out.tobytes()
        if len(pcm) & 1:
            pcm += b'\0'
    with open(args.output, 'wb') as f:
        f.write(header + entries + pcm)

    rates = [v.out_rate for v in voices]
    print('%s: %d samples (%d programs, %d drum notes), %d bytes of PCM, %d-%d Hz'
          % (args.output, len(voices), len(melodic), len(drums), len(pcm), min(rates), max(rates)))


if __name__ == '__main__':
    main()
