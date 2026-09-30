"""Read Dune II's PAK archives and XMIDI (.XMI / .C55) music files.

XMIDI is an IFF container ("FORM XDIR", then "CAT XMID" holding one
"FORM XMID" per sequence) whose EVNT chunks differ from standard MIDI:
  - delays are runs of bytes below 0x80 that add up;
  - a note-on is followed by its duration (a MIDI variable-length number)
    and there are no note-off events.
"""

import struct


def read_pak(path):
    """Return {name: bytes} for a Westwood PAK archive."""
    data = open(path, 'rb').read()
    entries, pos = [], 0
    while True:
        offset = struct.unpack_from('<I', data, pos)[0]
        if offset == 0:
            break
        end = data.index(b'\0', pos + 4)
        entries.append((data[pos + 4:end].decode('ascii'), offset))
        pos = end + 1
    files = {}
    for i, (name, offset) in enumerate(entries):
        stop = entries[i + 1][1] if i + 1 < len(entries) else len(data)
        files[name.upper()] = data[offset:stop]
    return files


def _chunks(data, pos, end):
    while pos + 8 <= end:
        tag = data[pos:pos + 4]
        size = struct.unpack_from('>I', data, pos + 4)[0]
        yield tag, pos + 8, size
        pos += 8 + size + (size & 1)


def sequences(data):
    """Return the EVNT data of every sequence in an XMIDI file, in order."""
    result = []

    def walk(pos, end):
        for tag, start, size in _chunks(data, pos, end):
            if tag in (b'FORM', b'CAT '):
                walk(start + 4, start + size)
            elif tag == b'EVNT':
                result.append(data[start:start + size])

    walk(0, len(data))
    return result


def _vlq(data, pos):
    value = 0
    while True:
        b = data[pos]
        pos += 1
        value = (value << 7) | (b & 0x7F)
        if b < 0x80:
            return value, pos


def events(evnt):
    """Yield (time, kind, channel, a, b, duration) for an EVNT chunk.

    kind is the high nibble of the status byte (0x90 note on, 0xB0
    controller, 0xC0 program, 0xE0 pitch bend, ...), or 0xFF for meta
    events (channel = meta type). duration is only set for notes.
    """
    time, pos = 0, 0
    while pos < len(evnt):
        b = evnt[pos]
        if b < 0x80:
            time += b
            pos += 1
            continue
        pos += 1
        kind, channel = b & 0xF0, b & 0x0F
        if b == 0xFF:
            meta = evnt[pos]
            length, pos = _vlq(evnt, pos + 1)
            yield time, 0xFF, meta, evnt[pos:pos + length], None, None
            if meta == 0x2F:
                return
            pos += length
        elif b in (0xF0, 0xF7):
            length, pos = _vlq(evnt, pos)
            pos += length
        elif kind == 0x90:
            note, velocity = evnt[pos], evnt[pos + 1]
            duration, pos = _vlq(evnt, pos + 2)
            yield time, kind, channel, note, velocity, duration
        elif kind in (0xC0, 0xD0):
            yield time, kind, channel, evnt[pos], None, None
            pos += 1
        else:
            yield time, kind, channel, evnt[pos], evnt[pos + 1], None
            pos += 2
