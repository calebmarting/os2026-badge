#!/usr/bin/env python3
"""Turn the easter-egg songs in songs/songs.json into src/songs_data.cpp.

Runs automatically before every build (scripts/build_songs.py), or by hand:

    python tools/song_convert.py

Each song in the manifest comes either from a MIDI file in songs/ or from
inline "tracks" (one string of NOTE:BEATS tokens per voice, R = rest). Notes
are spread across up to `voices` voices - the master plays each voice on a
group of badges - keeping the top line and the bass when a chord has more
notes than voices. Standard library only, so PlatformIO's Python can run it.

Manifest fields per song:
    name         console name ("song NAME")
    trigger      capo-button taps that start it, as fret IDs, e.g. [6, 6, 6]
    voices       1..12 - one per badge at 12, pairs at 6, threes at 4
    midi         file in songs/ (skipped with a warning if it is missing)
    tracks, bpm  inline alternative to midi
    transpose    semitones, applied first
    min_note, max_note   MIDI note range; notes outside are moved by octaves
    tempo        playback speed factor, 1.0 = as written
    max_seconds  cut the song off here, 0 = whole song
    gap_ms       silence before each note's end, so repeated notes re-sound
    channels     MIDI channels to keep (1-16), default all but drums (10)
"""

import json
import os
import struct
import sys

NOTE_BASE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
WAIT_VOICE = 0xFF       # an event that only advances time
MAX_DELTA = 0xFFFF      # ms per event delta (uint16 in the firmware)
WARN_BYTES = 30000


def parse_note(token):
    """'C#4', 'Eb5', 'R' -> MIDI note number (None for a rest). C4 = 60."""
    if token.upper() == "R":
        return None
    semi = NOTE_BASE[token[0].upper()]
    i = 1
    while i < len(token) and token[i] in "#b":
        semi += 1 if token[i] == "#" else -1
        i += 1
    return 12 * (int(token[i:]) + 1) + semi


def notes_from_tracks(tracks, bpm):
    ms_per_beat = 60000.0 / bpm
    notes = []
    for track in tracks:
        t = 0.0
        for token in track.split():
            name, beats = token.split(":")
            length = float(beats) * ms_per_beat
            note = parse_note(name)
            if note is not None:
                notes.append((t, t + length, note))
            t += length
    return notes


def read_varlen(data, i):
    value = 0
    while True:
        b = data[i]
        i += 1
        value = (value << 7) | (b & 0x7F)
        if not b & 0x80:
            return value, i


def notes_from_midi(path, channels):
    data = open(path, "rb").read()
    if data[:4] != b"MThd":
        raise ValueError("%s is not a standard MIDI file" % path)
    header_len = struct.unpack(">I", data[4:8])[0]
    _, track_count, division = struct.unpack(">HHH", data[8:14])
    if division & 0x8000:
        raise ValueError("%s uses SMPTE timing, which is not supported" % path)

    tempos = []   # (tick, microseconds per quarter note)
    raw = []      # (tick, is_on, channel, note)
    i = 8 + header_len
    for _ in range(track_count):
        while data[i:i + 4] != b"MTrk":    # skip any non-track chunk
            i += 8 + struct.unpack(">I", data[i + 4:i + 8])[0]
        length = struct.unpack(">I", data[i + 4:i + 8])[0]
        j, end = i + 8, i + 8 + length
        i = end
        tick = 0
        status = 0
        while j < end:
            delta, j = read_varlen(data, j)
            tick += delta
            b = data[j]
            if b == 0xFF:                   # meta event
                kind = data[j + 1]
                size, j = read_varlen(data, j + 2)
                payload = data[j:j + size]
                j += size
                if kind == 0x51 and size == 3:
                    tempos.append((tick, int.from_bytes(payload, "big")))
                elif kind == 0x2F:
                    break
                continue
            if b in (0xF0, 0xF7):           # sysex
                size, j = read_varlen(data, j + 1)
                j += size
                continue
            if b & 0x80:                    # else running status
                status = b
                j += 1
            kind, channel = status & 0xF0, status & 0x0F
            if kind in (0xC0, 0xD0):        # one data byte
                j += 1
                continue
            note, velocity = data[j], data[j + 1]
            j += 2
            if kind == 0x90 and velocity > 0:
                raw.append((tick, 1, channel, note))
            elif kind == 0x80 or kind == 0x90:
                raw.append((tick, 0, channel, note))

    tempos.sort()
    if not tempos or tempos[0][0] != 0:
        tempos.insert(0, (0, 500000))      # MIDI default: 120 bpm

    def tick_to_ms(tick):
        ms, last_tick, us_per_q = 0.0, 0, tempos[0][1]
        for t, tempo in tempos[1:]:
            if t > tick:
                break
            ms += (t - last_tick) * us_per_q / division / 1000.0
            last_tick, us_per_q = t, tempo
        return ms + (tick - last_tick) * us_per_q / division / 1000.0

    held = {}
    notes = []
    for tick, is_on, channel, note in sorted(raw, key=lambda e: (e[0], e[1])):
        if channels is not None and channel + 1 not in channels:
            continue
        if channels is None and channel == 9:   # General MIDI drums
            continue
        if is_on:
            held.setdefault((channel, note), []).append(tick)
        elif held.get((channel, note)):
            start = held[(channel, note)].pop(0)
            notes.append((tick_to_ms(start), tick_to_ms(tick), note))
    return notes


def allocate(notes, voices):
    """Assign notes to voices; returns [start, end, voice, note] lists."""
    notes = sorted(notes, key=lambda n: (n[0], -n[2]))
    playing = [None] * voices       # index into out, per voice
    out = []
    for start, end, note in notes:
        for v in range(voices):
            if playing[v] is not None and out[playing[v]][1] <= start:
                playing[v] = None
        free = [v for v in range(voices) if playing[v] is None]
        if free:
            v = free[0]
        else:
            # Full: keep the highest and lowest lines. A new note above or
            # below everything steals a middle voice; a new middle note is
            # dropped.
            by_pitch = sorted(range(voices), key=lambda x: out[playing[x]][3])
            low, high = out[playing[by_pitch[0]]][3], out[playing[by_pitch[-1]]][3]
            if voices < 3 or low <= note <= high:
                continue
            v = by_pitch[len(by_pitch) // 2]
            out[playing[v]][1] = start
        playing[v] = len(out)
        out.append([start, end, v, note])
    return out


def song_events(song, songs_dir):
    voices = int(song.get("voices", 6))
    if not 1 <= voices <= 12:
        raise ValueError("%s: voices must be 1..12" % song["name"])
    if "midi" in song:
        path = os.path.join(songs_dir, song["midi"])
        if not os.path.exists(path):
            return None
        notes = notes_from_midi(path, song.get("channels"))
    else:
        notes = notes_from_tracks(song["tracks"], float(song.get("bpm", 120)))

    transpose = int(song.get("transpose", 0))
    low, high = int(song.get("min_note", 52)), int(song.get("max_note", 100))
    tempo = float(song.get("tempo", 1.0))
    limit = float(song.get("max_seconds", 0)) * 1000.0
    gap = float(song.get("gap_ms", 15))

    shaped = []
    for start, end, note in notes:
        start, end = start / tempo, end / tempo
        if limit and start >= limit:
            continue
        note += transpose
        while note < low:
            note += 12
        while note > high:
            note -= 12
        shaped.append((start, min(end, limit) if limit else end, note))

    timeline = []   # (ms, order, voice, note); offs sort before ons
    for start, end, voice, note in allocate(shaped, voices):
        # A note stolen the moment it started never sounds at all; left in,
        # its zero-length on/off would sort after its replacement and win.
        if end - start < 1:
            continue
        off = end - gap if end - start > 2 * gap else start + (end - start) * 0.8
        timeline.append((round(start), 1, voice, note))
        timeline.append((round(off), 0, voice, 0))
    timeline.sort()

    events, now = [], 0
    for ms, _, voice, note in timeline:
        delta = ms - now
        while delta > MAX_DELTA:
            events.append((MAX_DELTA, WAIT_VOICE, 0))
            delta -= MAX_DELTA
        events.append((max(0, delta), voice, note))
        now = ms
    return voices, events


def c_name(name):
    return "".join(ch if ch.isalnum() else "_" for ch in name)


def build(manifest_path, out_path):
    songs_dir = os.path.dirname(manifest_path)
    songs = json.load(open(manifest_path, encoding="utf-8"))

    lines = [
        "// GENERATED by tools/song_convert.py from songs/songs.json - do not edit.",
        '#include "songs.h"',
        "",
    ]
    entries = []
    for song in songs:
        result = song_events(song, songs_dir)
        if result is None:
            print("songs: %s skipped - songs/%s not found" % (song["name"], song["midi"]))
            continue
        voices, events = result
        ident = c_name(song["name"])
        size = 4 * len(events)
        note = " (large - consider max_seconds)" if size > WARN_BYTES else ""
        print("songs: %s, %d voices, %d events, %d bytes%s" % (song["name"], voices, len(events), size, note))
        trigger = ", ".join(str(int(t)) for t in song.get("trigger", []))
        lines.append("static const uint8_t kTrigger_%s[] = {%s};" % (ident, trigger or "0"))
        lines.append("static const SongEvent kEvents_%s[] = {" % ident)
        for delta, voice, n in events:
            lines.append("    {%d, %d, %d}," % (delta, voice, n))
        lines.append("};")
        lines.append("")
        entries.append('    {"%s", kTrigger_%s, %d, %d, %d, kEvents_%s},'
                       % (song["name"], ident, len(song.get("trigger", [])), voices, len(events), ident))

    lines.append("const Song kSongs[] = {")
    lines.extend(entries or ['    {"none", nullptr, 0, 1, 0, nullptr},'])
    lines.append("};")
    lines.append("const uint8_t kSongCount = %d;" % len(entries))
    text = "\n".join(lines) + "\n"

    # Only touch the file when it changed, so it does not force a rebuild.
    try:
        if open(out_path, encoding="utf-8").read() == text:
            return
    except OSError:
        pass
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


if __name__ == "__main__":
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    build(os.path.join(here, "songs", "songs.json"), os.path.join(here, "src", "songs_data.cpp"))
