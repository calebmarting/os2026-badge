#!/usr/bin/env python3
"""Turn the easter-egg songs in songs/songs.json into src/songs_data.cpp.

Runs automatically before every build (scripts/build_songs.py), or by hand:

    python tools/song_convert.py

Each song in the manifest comes either from a MIDI file in songs/ or from
inline "tracks" (one string of NOTE:BEATS tokens per voice, R = rest). Notes
are given to the 12 piezos - one per badge - which the master plays as
told; chords share the piezos out, and past max_notes at once the top line and
the bass are kept. Standard library only, so PlatformIO's Python can run it.

Manifest fields per song:
    name         console name ("song NAME")
    trigger      capo-button taps that start it, as fret IDs, e.g. [6, 6, 6]
    voices       1..12 - each note normally gets 12 / voices piezos (badges):
                 one each at 12, pairs at 6, threes at 4
    spread       true (default): chords borrow piezos, see spread_timeline();
                 false: each voice keeps a fixed group of badges
    max_notes    with spread, most notes at once (default 12)
    midi         file in songs/ (skipped with a warning if it is missing)
    tracks, bpm  inline alternative to midi
    transpose    semitones, applied first
    min_note, max_note   MIDI note range; notes outside are moved by octaves
    playable_min, playable_max   the piezo's own limits (default 50..108,
                 ~147 Hz..4.2 kHz): a note outside them would not really sound,
                 so it is moved by octaves until it fits, whatever min/max say
    drums        piezos given to the drum channel (default 2 if the song has
                 drum notes, else 0); 0 drops the drums
    tempo        playback speed factor, 1.0 = as written
    max_seconds  cut the song off here, 0 = whole song
    gap_ms       silence before each note's end, so repeated notes re-sound
    channels     MIDI channels to play as tones (1-16); overrides auto_channels
    auto_channels  true: keep only the `voices` busiest tone channels
    rank_by      what "busiest" means: "time" (total sounding time, default)
                 or "notes" (note count)
    drum_channels  channels that are drums, default [10] (General MIDI)

To see what is in a MIDI file - each channel's instrument, note count and
sounding time, and which ones a song keeps:

    python tools/song_convert.py --channels SONGNAME
"""

import json
import os
import re
import struct
import sys

NOTE_BASE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
PIEZOS = 12             # one per badge; an event with no piezos only waits
PLAYABLE_MIN = 50       # D3, ~147 Hz: below this the piezo barely sounds
PLAYABLE_MAX = 108      # C8, ~4.2 kHz
DRUM_BASE = 128         # event note values from here up are drum hits

# Pseudo white noise per drum: the piezo is toggled at random intervals
# between center-spread and center+spread Hz for ms milliseconds (the same
# trick as examples/drum). Generated into the firmware, so this table is the
# only place these live. The piezo resonates near 4 kHz and says little below
# ~150 Hz, so "low" drums sit higher than their real pitch.
DRUM_SOUNDS = [
    # name, center Hz, spread Hz, ms
    ("kick", 160, 60, 70),
    ("snare", 1500, 1300, 90),
    ("closed hat", 6500, 2500, 25),
    ("open hat", 6500, 2500, 140),
    # Kept clear of the ~4 kHz resonance: a long burst across it rings shrill.
    ("crash", 9000, 2500, 380),
    ("ride", 5500, 1200, 140),
    ("low tom", 230, 40, 110),
    ("high tom", 340, 50, 100),
    ("clap", 1800, 1500, 45),
]
DRUM_INDEX = {name: i for i, (name, _, _, _) in enumerate(DRUM_SOUNDS)}

# General MIDI program names, so the channel listing can say what each is.
GM_PROGRAMS = (
    "Acoustic Grand Piano|Bright Piano|Electric Grand|Honky-tonk|E.Piano 1|E.Piano 2|Harpsichord|Clavinet|"
    "Celesta|Glockenspiel|Music Box|Vibraphone|Marimba|Xylophone|Tubular Bells|Dulcimer|"
    "Drawbar Organ|Percussive Organ|Rock Organ|Church Organ|Reed Organ|Accordion|Harmonica|Bandoneon|"
    "Nylon Guitar|Steel Guitar|Jazz Guitar|Clean Guitar|Muted Guitar|Overdrive Guitar|Distortion Guitar|"
    "Guitar Harmonics|Acoustic Bass|Finger Bass|Pick Bass|Fretless Bass|Slap Bass 1|Slap Bass 2|"
    "Synth Bass 1|Synth Bass 2|Violin|Viola|Cello|Contrabass|Tremolo Strings|Pizzicato Strings|"
    "Harp|Timpani|String Ensemble 1|String Ensemble 2|Synth Strings 1|Synth Strings 2|Choir Aahs|"
    "Voice Oohs|Synth Voice|Orchestra Hit|Trumpet|Trombone|Tuba|Muted Trumpet|French Horn|Brass Section|"
    "Synth Brass 1|Synth Brass 2|Soprano Sax|Alto Sax|Tenor Sax|Baritone Sax|Oboe|English Horn|Bassoon|"
    "Clarinet|Piccolo|Flute|Recorder|Pan Flute|Blown Bottle|Shakuhachi|Whistle|Ocarina|Square Lead|"
    "Saw Lead|Calliope Lead|Chiff Lead|Charang Lead|Voice Lead|Fifths Lead|Bass+Lead|New Age Pad|"
    "Warm Pad|Polysynth Pad|Choir Pad|Bowed Pad|Metallic Pad|Halo Pad|Sweep Pad|Rain FX|Soundtrack FX|"
    "Crystal FX|Atmosphere FX|Brightness FX|Goblins FX|Echoes FX|Sci-fi FX|Sitar|Banjo|Shamisen|Koto|"
    "Kalimba|Bagpipe|Fiddle|Shanai|Tinkle Bell|Agogo|Steel Drums|Woodblock|Taiko Drum|Melodic Tom|"
    "Synth Drum|Reverse Cymbal|Guitar Fret Noise|Breath Noise|Seashore|Bird Tweet|Telephone Ring|"
    "Helicopter|Applause|Gunshot"
).split("|")

# General MIDI percussion key -> drum sound.
GM_DRUMS = {}
for keys, name in (
    ((35, 36), "kick"),
    ((38, 40), "snare"),
    ((37, 39, 54, 56, 75, 76, 77), "clap"),
    ((42, 44), "closed hat"),
    ((46,), "open hat"),
    ((49, 52, 55, 57), "crash"),
    ((51, 53, 59), "ride"),
    ((41, 43, 45, 61, 64), "low tom"),
    ((47, 48, 50, 60, 62, 63), "high tom"),
):
    for key in keys:
        GM_DRUMS[key] = DRUM_INDEX[name]
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


def notes_from_midi(path, drum_channels):
    """Returns (notes, hits, programs): notes as (start ms, end ms, note,
    channel 1-16), drum hits from drum_channels as (ms, drum sound index), and
    the first program (instrument) set on each channel."""
    data = open(path, "rb").read()
    if data[:4] != b"MThd":
        raise ValueError("%s is not a standard MIDI file" % path)
    header_len = struct.unpack(">I", data[4:8])[0]
    _, track_count, division = struct.unpack(">HHH", data[8:14])
    if division & 0x8000:
        raise ValueError("%s uses SMPTE timing, which is not supported" % path)

    tempos = []   # (tick, microseconds per quarter note)
    programs = {}  # channel 1-16 -> first program number
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
                if kind == 0xC0:
                    programs.setdefault(channel + 1, data[j])
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
    hits = []
    for tick, is_on, channel, note in sorted(raw, key=lambda e: (e[0], e[1])):
        if channel + 1 in drum_channels:
            if is_on:
                hits.append((tick_to_ms(tick), GM_DRUMS.get(note, DRUM_INDEX["clap"])))
            continue
        if is_on:
            held.setdefault((channel, note), []).append(tick)
        elif held.get((channel, note)):
            start = held[(channel, note)].pop(0)
            notes.append((tick_to_ms(start), tick_to_ms(tick), note, channel + 1))
    return notes, hits, programs


def channel_usage(notes):
    """channel -> (note count, total sounding ms)."""
    usage = {}
    for start, end, _, channel in notes:
        count, ms = usage.get(channel, (0, 0.0))
        usage[channel] = (count + 1, ms + (end - start))
    return usage


def chosen_channels(song, usage):
    """The tone channels a song keeps, or None for all of them."""
    if "channels" in song:
        return set(int(c) for c in song["channels"])
    if song.get("auto_channels"):
        rank_by = song.get("rank_by", "time")
        if rank_by not in ("time", "notes"):
            raise ValueError('%s: rank_by must be "time" or "notes"' % song["name"])
        key = (lambda c: usage[c][1]) if rank_by == "time" else (lambda c: usage[c][0])
        ranked = sorted(usage, key=key, reverse=True)
        return set(ranked[:int(song.get("voices", 6))])
    return None


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
    """Returns (voices, events, report) or None if the MIDI file is missing."""
    voices = int(song.get("voices", 6))
    if not 1 <= voices <= 12:
        raise ValueError("%s: voices must be 1..12" % song["name"])
    hits = []
    if "midi" in song:
        path = os.path.join(songs_dir, song["midi"])
        if not os.path.exists(path):
            return None
        notes, hits, _ = notes_from_midi(path, song.get("drum_channels", [10]))
        keep = chosen_channels(song, channel_usage(notes))
        if keep is not None:
            notes = [n for n in notes if n[3] in keep]
        notes = [n[:3] for n in notes]
    else:
        notes = notes_from_tracks(song["tracks"], float(song.get("bpm", 120)))
        keep = None

    transpose = int(song.get("transpose", 0))
    playable_low = int(song.get("playable_min", PLAYABLE_MIN))
    playable_high = int(song.get("playable_max", PLAYABLE_MAX))
    low = max(int(song.get("min_note", 52)), playable_low)
    high = min(int(song.get("max_note", 100)), playable_high)
    if high - low < 11:
        raise ValueError("%s: the note range %d..%d is narrower than an octave" % (song["name"], low, high))
    tempo = float(song.get("tempo", 1.0))
    limit = float(song.get("max_seconds", 0)) * 1000.0
    gap = float(song.get("gap_ms", 15))

    shaped = []
    moved = 0
    for start, end, note in notes:
        start, end = start / tempo, end / tempo
        if limit and start >= limit:
            continue
        note += transpose
        original = note
        while note < low:
            note += 12
        while note > high:
            note -= 12
        moved += note != original
        shaped.append((start, min(end, limit) if limit else end, note))

    hits = [(ms / tempo, kind) for ms, kind in hits if not limit or ms / tempo < limit]
    drums = int(song.get("drums", 2 if hits else 0)) if hits else 0
    if not 0 <= drums < PIEZOS:
        raise ValueError("%s: drums must be 0..11" % song["name"])
    if drums == 0:
        hits = []
    pool = PIEZOS - drums

    if song.get("spread", True):
        max_notes = int(song.get("max_notes", pool))
        if not 1 <= max_notes <= pool:
            raise ValueError("%s: max_notes must be 1..%d" % (song["name"], pool))
        timeline = spread_timeline(shaped, voices, max_notes, gap, pool)
    else:
        timeline = fixed_timeline(shaped, voices, gap, pool)
    timeline += drum_timeline(hits, pool)
    timeline.sort()

    events, now = [], 0
    for ms, _, mask, note in timeline:
        ms = round(ms)
        delta = ms - now
        while delta > MAX_DELTA:
            events.append((MAX_DELTA, 0, 0))
            delta -= MAX_DELTA
        events.append((max(0, delta), mask, note))
        now = ms

    report = []
    if keep is not None:
        report.append("channels %s" % ",".join(str(c) for c in sorted(keep)))
    if moved:
        report.append("%d notes moved into %d..%d" % (moved, low, high))
    if drums:
        report.append("%d drum hits on %d piezos" % (len(hits), drums))
    return voices, events, report


def drum_timeline(hits, pool):
    """Each hit goes to one of the drum piezos (badges pool..11 - never the
    master), preferring one that has finished its last hit."""
    piezos = list(range(pool, PIEZOS))
    busy_until = {b: -1.0 for b in piezos}
    timeline = []
    for ms, kind in sorted(hits):
        free = [b for b in piezos if busy_until[b] <= ms]
        b = free[0] if free else min(piezos, key=lambda x: busy_until[x])
        busy_until[b] = ms + DRUM_SOUNDS[kind][3]
        # Order 2: after any tone event in the same millisecond.
        timeline.append((ms, 2, 1 << b, DRUM_BASE + kind))
    return timeline


def off_time(start, end, gap):
    """When a note's piezos go quiet: gap_ms early, so a repeat re-sounds."""
    return end - gap if end - start > 2 * gap else start + (end - start) * 0.8


def group_mask(voice, voices, pool):
    """The fixed badge group of one voice: 12 voices = one each, 4 = threes."""
    return sum(1 << b for b in range(pool) if b * voices // pool == voice)


def mask_of(piezos):
    return sum(1 << b for b in piezos)


def fixed_timeline(notes, voices, gap, pool):
    """spread: false - each voice always plays on its own group of badges, and
    a chord with more notes than voices loses its middle notes."""
    timeline = []   # (ms, order, piezo mask, note); offs sort before ons
    for start, end, voice, note in allocate(notes, voices):
        # A note stolen the moment it started never sounds at all; left in,
        # its zero-length on/off would sort after its replacement and win.
        if end - start < 1:
            continue
        mask = group_mask(voice, voices, pool)
        timeline.append((start, 1, mask, note))
        timeline.append((off_time(start, end, gap), 0, mask, 0))
    return timeline


def spread_timeline(notes, voices, max_notes, gap, pool):
    """The default. The 12 piezos are one pool: a note normally gets
    12 // voices of them, but a chord with more notes than that shares the
    pool out as evenly as it can - a new note takes piezos from whichever note
    holds the most, never its last one - so up to max_notes notes sound at
    once. When notes end, their piezos go back to any note left short."""
    target = max(1, pool // voices)
    # Beyond max_notes at once, the outer lines still win over middle notes.
    kept = [(s, e, n) for s, e, _, n in allocate(notes, max_notes) if e - s >= 1]

    # Everything happening in the same millisecond is one batch: ends first,
    # then new notes (highest first), then any free piezos topped up - so a
    # piezo is never handed to a note that is itself ending in that instant.
    batches = {}
    for i, (start, end, note) in enumerate(kept):
        batches.setdefault(round(off_time(start, end, gap)), ([], []))[0].append(i)
        batches.setdefault(round(start), ([], []))[1].append(i)

    owner = [None] * pool
    held = {}       # note index -> the piezos it is sounding on
    timeline = []   # (ms, sequence, piezo mask, note): already in play order

    def emit(ms, piezos, note):
        timeline.append((ms, len(timeline), mask_of(piezos), note))

    def fair_share():
        return max(1, min(target, pool // max(1, len(held))))

    def give(i, piezos, ms):
        for b in piezos:
            owner[b] = i
        held[i].extend(piezos)
        emit(ms, piezos, kept[i][2])

    for ms in sorted(batches):
        ends, starts = batches[ms]
        for i in ends:
            freed = held.pop(i, [])
            for b in freed:
                owner[b] = None
            if freed:
                emit(ms, freed, 0)

        for i in sorted(starts, key=lambda i: -kept[i][2]):
            held[i] = []
            share = fair_share()
            take = [b for b in range(pool) if owner[b] is None][:share]
            while len(take) < share:
                donors = [j for j in held if j != i and len(held[j]) > len(take) + 1]
                if not donors:
                    break
                j = max(donors, key=lambda j: len(held[j]))
                take.append(held[j].pop())
            give(i, take, ms)

        free = [b for b in range(pool) if owner[b] is None]
        while free:
            short = [j for j in held if len(held[j]) < fair_share()]
            if not short:
                break
            j = min(short, key=lambda j: len(held[j]))
            give(j, [free.pop(0)], ms)
    return timeline


def c_name(name):
    return "".join(ch if ch.isalnum() else "_" for ch in name)


def load_manifest(path):
    """songs.json, forgiving trailing commas - easy to leave behind while
    editing by hand - and saying where any other mistake is."""
    text = open(path, encoding="utf-8").read()
    text = re.sub(r",(\s*[}\]])", r"\1", text)
    try:
        return json.loads(text)
    except json.JSONDecodeError as e:
        sys.exit("songs: %s line %d column %d: %s" % (path, e.lineno, e.colno, e.msg))


def build(manifest_path, out_path):
    songs_dir = os.path.dirname(manifest_path)
    songs = load_manifest(manifest_path)

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
        voices, events, report = result
        ident = c_name(song["name"])
        size = 5 * len(events)
        note = " (large - consider max_seconds)" if size > WARN_BYTES else ""
        layout = "shared piezos" if song.get("spread", True) else "fixed groups"
        print("songs: %s, %d voices, %s, %d events, %d bytes%s%s"
              % (song["name"], voices, layout, len(events), size,
                 "".join("; " + r for r in report), note))
        trigger = ", ".join(str(int(t)) for t in song.get("trigger", []))
        lines.append("static const uint8_t kTrigger_%s[] = {%s};" % (ident, trigger or "0"))
        lines.append("static const SongEvent kEvents_%s[] = {" % ident)
        for delta, mask, n in events:
            lines.append("    {%d, 0x%03X, %d}," % (delta, mask, n))
        lines.append("};")
        lines.append("")
        entries.append('    {"%s", kTrigger_%s, %d, %d, %d, kEvents_%s},'
                       % (song["name"], ident, len(song.get("trigger", [])), voices, len(events), ident))

    lines.append("const SongDrum kSongDrums[] = {")
    for name, center, spread, ms in DRUM_SOUNDS:
        lines.append("    {%d, %d, %d}, // %s" % (center, spread, ms, name))
    lines.append("};")
    lines.append("const uint8_t kSongDrumCount = %d;" % len(DRUM_SOUNDS))
    lines.append("")
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


def list_channels(manifest_path, name):
    """Prints every channel of a song's MIDI file, busiest first."""
    songs = [s for s in load_manifest(manifest_path) if s["name"] == name]
    if not songs or "midi" not in songs[0]:
        sys.exit("no MIDI song called %s in %s" % (name, manifest_path))
    song = songs[0]
    path = os.path.join(os.path.dirname(manifest_path), song["midi"])
    drum_channels = song.get("drum_channels", [10])
    notes, hits, programs = notes_from_midi(path, drum_channels)
    usage = channel_usage(notes)
    keep = chosen_channels(song, usage)

    print("%s (%s), %d voices:" % (name, song["midi"], int(song.get("voices", 6))))
    print("  ch  instrument            notes   seconds")
    for channel in sorted(usage, key=lambda c: usage[c][1], reverse=True):
        count, ms = usage[channel]
        program = programs.get(channel)
        instrument = GM_PROGRAMS[program] if program is not None else "(no program)"
        kept = keep is None or channel in keep
        print("  %2d  %-20s %6d  %8.1f  %s" % (channel, instrument, count, ms / 1000.0, "kept" if kept else "-"))
    for channel in drum_channels:
        print("  %2d  %-20s %6d  %8s  drums" % (channel, "(drum kit)", len(hits), ""))


if __name__ == "__main__":
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    manifest = os.path.join(here, "songs", "songs.json")
    if len(sys.argv) == 3 and sys.argv[1] == "--channels":
        list_channels(manifest, sys.argv[2])
    else:
        build(manifest, os.path.join(here, "src", "songs_data.cpp"))
