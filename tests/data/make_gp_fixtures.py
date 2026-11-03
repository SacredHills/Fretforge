"""Generates the Guitar Pro fixtures used by tests/GuitarProTests.cpp.

Written with PyGuitarPro (an independent implementation of the format, LGPL-3.0), so the importer is checked
against files it did not write itself. Only the generated .gp3/.gp4/.gp5 files are committed.
    pip install pyguitarpro && python3 make_gp_fixtures.py
"""
import guitarpro as gp
from guitarpro import models as m

def build():
    song = m.Song()
    song.title = "Fixture Song"
    song.artist = "FretForge"
    song.tempo = 100
    song.tracks = []
    song.measureHeaders = []
    for i in range(4):
        h = m.MeasureHeader()
        h.number = i + 1
        h.timeSignature.numerator = 4
        h.timeSignature.denominator.value = 4
        song.measureHeaders.append(h)
    song.measureHeaders[1].isRepeatOpen = True
    song.measureHeaders[2].repeatClose = 1     # PyGuitarPro: number of repeats (GP5 writes plays)
    song.measureHeaders[0].marker = m.Marker(title="Intro")
    song.measureHeaders[0].keySignature = m.KeySignature.DMajor

    guitar = m.Track(song)
    guitar.name = "Lead"
    guitar.strings = [m.GuitarString(n, v) for n, v in enumerate([64, 59, 55, 50, 45, 40], 1)]
    guitar.channel.instrument = 29
    bass = m.Track(song)
    bass.name = "Bass"
    bass.strings = [m.GuitarString(n, v) for n, v in enumerate([43, 38, 33, 28], 1)]
    bass.channel.instrument = 33
    bass.channel.channel = 1
    bass.channel.effectChannel = 1
    song.tracks = [guitar, bass]

    for track in song.tracks:
        track.measures = [m.Measure(track, h) for h in song.measureHeaders]

    def add_beat(measure, dur, notes, **kw):
        voice = measure.voices[0]
        b = m.Beat(voice)
        b.duration = m.Duration(value=dur)
        for k, v in kw.items():
            setattr(b, k, v)
        for (string, fret, eff) in notes:
            n = m.Note(b)
            n.string = string        # 1 = highest
            n.value = fret
            n.type = m.NoteType.normal
            for k, v in (eff or {}).items():
                if k == 'type':
                    n.type = v
                else:
                    setattr(n.effect, k, v)
            b.notes.append(n)
        b.status = m.BeatStatus.normal if notes else m.BeatStatus.rest
        voice.beats.append(b)
        return b

    g = guitar.measures
    # bar 1: four quarters, techniques
    add_beat(g[0], 4, [(6, 3, None)])
    add_beat(g[0], 4, [(5, 5, {'hammer': True})])
    add_beat(g[0], 4, [(5, 7, None)])
    bend = m.BendEffect(type=m.BendType.bend, value=100,
                        points=[m.BendPoint(0, 0), m.BendPoint(6, 4), m.BendPoint(12, 4)])
    add_beat(g[0], 4, [(3, 7, {'bend': bend, 'vibrato': True})])
    # bar 2: eighth notes, palm mute, dead, chord
    for f in (0, 0, 2, 2):
        add_beat(g[1], 8, [(6, f, {'palmMute': True})])
    add_beat(g[1], 4, [(6, 0, {'type': m.NoteType.dead})])
    add_beat(g[1], 4, [(6, 3, None), (5, 2, None), (1, 3, None)])
    # bar 3: tempo change, slide, harmonic, tie
    b = add_beat(g[2], 2, [(4, 5, {'slides': [m.SlideType.shiftSlideTo]})])
    mix = m.MixTableChange()
    mix.tempo = m.MixTableItem(value=140, duration=0)
    b.effect.mixTableChange = mix
    add_beat(g[2], 4, [(4, 7, None)])
    add_beat(g[2], 4, [(3, 12, {'harmonic': m.NaturalHarmonic()})])
    # bar 4: whole note then tied
    add_beat(g[3], 2, [(2, 5, {'letRing': True})])
    add_beat(g[3], 2, [(2, 5, {'type': m.NoteType.tie})])

    for i, meas in enumerate(bass.measures):
        add_beat(meas, 1, [(4, [0, 0, 5, 3][i], None)])
    return song

if __name__ == '__main__':
    for ver, ext in (((3, 0, 0), 'gp3'), ((4, 0, 6), 'gp4'), ((5, 1, 0), 'gp5')):
        gp.write(build(), 'fixture.' + ext, version=ver)
        print('wrote fixture.' + ext)
