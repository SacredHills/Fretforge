#include "TabModel.h"
#include "Timeline.h"
#include "MusicTheory.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <map>

namespace ff::tab
{
// ------------------------------------------------------------------ tunings

juce::String tuningToString (const std::vector<int>& notes)
{
    juce::StringArray s;
    for (int n : notes) s.add (juce::String (n));
    return s.joinIntoString (" ");
}

std::vector<int> tuningFromString (const juce::String& text)
{
    std::vector<int> r;
    for (auto& t : juce::StringArray::fromTokens (text, " ,", ""))
        if (t.containsOnly ("0123456789"))
            r.push_back (juce::jlimit (0, 127, t.getIntValue()));
    return r;
}

std::vector<int> trackTuning (const juce::ValueTree& track)
{
    auto t = tuningFromString (track[ids::tuning].toString());
    if (t.empty()) t = { 40, 45, 50, 55, 59, 64 };
    return t;
}

int numStrings (const juce::ValueTree& track) { return (int) trackTuning (track).size(); }

// ------------------------------------------------------------------ structure

juce::ValueTree findScore (const juce::ValueTree& project) { return project.getChildWithName (ids::SCORE); }
juce::ValueTree measures (const juce::ValueTree& score) { return score.getChildWithName (ids::MEASURES); }
juce::ValueTree tracks (const juce::ValueTree& score) { return score.getChildWithName (ids::TABTRACKS); }
int numMeasures (const juce::ValueTree& score) { return measures (score).getNumChildren(); }
juce::ValueTree bar (const juce::ValueTree& track, int measure) { return track.getChild (measure); }

juce::ValueTree getOrCreateScore (juce::ValueTree project)
{
    auto s = findScore (project);
    if (s.isValid())
    {
        if (! measures (s).isValid()) s.appendChild (juce::ValueTree (ids::MEASURES), nullptr);
        if (! tracks (s).isValid()) s.appendChild (juce::ValueTree (ids::TABTRACKS), nullptr);
        return s;
    }
    s = juce::ValueTree (ids::SCORE);
    s.setProperty (ids::title, "Untitled tab", nullptr);
    s.setProperty (ids::tempo, 120.0, nullptr);
    s.appendChild (juce::ValueTree (ids::MEASURES), nullptr);
    s.appendChild (juce::ValueTree (ids::TABTRACKS), nullptr);
    for (int i = 0; i < 4; ++i)
        addMeasure (s, -1, 4, 4, nullptr);
    project.appendChild (s, nullptr);
    return s;
}

juce::ValueTree makeBeat (int dur, bool rest)
{
    juce::ValueTree b (ids::BEAT);
    b.setProperty (ids::dur, dur, nullptr);
    if (rest) b.setProperty (ids::rest, true, nullptr);
    return b;
}

juce::ValueTree makeNote (int string, int fret)
{
    juce::ValueTree n (ids::NOTE);
    n.setProperty (ids::string, string, nullptr);
    n.setProperty (ids::fret, juce::jlimit (0, maxFret, fret), nullptr);
    return n;
}

juce::ValueTree addTrack (juce::ValueTree score, const juce::String& name, const std::vector<int>& tuning,
                          const juce::String& instrument, juce::UndoManager* um)
{
    juce::ValueTree t (ids::TABTRACK);
    t.setProperty (ids::id, timeline::newId(), nullptr);
    t.setProperty (ids::name, name, nullptr);
    t.setProperty (ids::instrument, instrument, nullptr);
    t.setProperty (ids::tuning, tuningToString (tuning), nullptr);
    t.setProperty (ids::capo, 0, nullptr);
    t.setProperty (ids::volume, 0.8, nullptr);
    t.setProperty (ids::pan, 0.0, nullptr);
    for (int i = 0; i < numMeasures (score); ++i)
        t.appendChild (juce::ValueTree (ids::BAR), nullptr);
    tracks (score).appendChild (t, um);
    return t;
}

juce::ValueTree addMeasure (juce::ValueTree score, int index, int tsNum, int tsDen, juce::UndoManager* um)
{
    juce::ValueTree m (ids::MEASURE);
    m.setProperty (ids::tsNum, tsNum, nullptr);
    m.setProperty (ids::tsDen, tsDen, nullptr);
    measures (score).addChild (m, index, um);
    for (auto t : tracks (score))
        t.addChild (juce::ValueTree (ids::BAR), index, um);
    return m;
}

void removeMeasure (juce::ValueTree score, int index, juce::UndoManager* um)
{
    if (numMeasures (score) <= 1 || ! juce::isPositiveAndBelow (index, numMeasures (score)))
        return;
    measures (score).removeChild (index, um);
    for (auto t : tracks (score))
        t.removeChild (index, um);
}

juce::ValueTree noteOnString (const juce::ValueTree& beat, int string)
{
    for (auto n : beat)
        if ((int) n[ids::string] == string)
            return n;
    return {};
}

// ------------------------------------------------------------------ timing

double beatLengthQ (int dur, int dots, int tuplet)
{
    double q = 4.0 / juce::jlimit (1, 128, dur);
    if (dots == 1) q *= 1.5;
    else if (dots >= 2) q *= 1.75;
    switch (tuplet)
    {
        case 3: q *= 2.0 / 3.0; break;
        case 5: q *= 4.0 / 5.0; break;
        case 6: q *= 4.0 / 6.0; break;
        case 7: q *= 4.0 / 7.0; break;
        default: break;
    }
    return q;
}

double beatLengthQ (const juce::ValueTree& b)
{
    return beatLengthQ ((int) b.getProperty (ids::dur, 4), (int) b[ids::dots], (int) b[ids::tuplet]);
}

double measureLengthQ (const juce::ValueTree& m)
{
    return (int) m.getProperty (ids::tsNum, 4) * 4.0 / (int) m.getProperty (ids::tsDen, 4);
}

double barContentQ (const juce::ValueTree& bar)
{
    double q = 0.0;
    for (auto b : bar) q += beatLengthQ (b);
    return q;
}

std::vector<double> measureStarts (const juce::ValueTree& score)
{
    std::vector<double> s { 0.0 };
    for (auto m : measures (score))
        s.push_back (s.back() + measureLengthQ (m));
    return s;
}

double measureTempo (const juce::ValueTree& score, int measure)
{
    double t = juce::jlimit (10.0, 999.0, (double) score.getProperty (ids::tempo, 120.0));
    auto ms = measures (score);
    for (int i = 0; i <= measure && i < ms.getNumChildren(); ++i)
        if ((double) ms.getChild (i)[ids::tempo] > 0.0)
            t = juce::jlimit (10.0, 999.0, (double) ms.getChild (i)[ids::tempo]);
    return t;
}

int measureKey (const juce::ValueTree& score, int measure)
{
    int k = 0;
    auto ms = measures (score);
    for (int i = 0; i <= measure && i < ms.getNumChildren(); ++i)
        if (ms.getChild (i).hasProperty (ids::key))
            k = juce::jlimit (-7, 7, (int) ms.getChild (i)[ids::key]);
    return k;
}

TempoMap tempoMap (const juce::ValueTree& score, double speed)
{
    TempoMap map;
    speed = juce::jlimit (0.1, 4.0, speed);
    const auto starts = measureStarts (score);
    double sec = 0.0, q = 0.0;
    double spq = 60.0 / (juce::jlimit (10.0, 999.0, (double) score.getProperty (ids::tempo, 120.0)) * speed);
    map.segments.push_back ({ 0.0, 0.0, spq });
    auto ms = measures (score);
    for (int i = 0; i < ms.getNumChildren(); ++i)
    {
        const double t = (double) ms.getChild (i)[ids::tempo];
        if (t <= 0.0) continue;
        const double mq = starts[(size_t) i];
        sec += (mq - q) * spq;
        q = mq;
        spq = 60.0 / (juce::jlimit (10.0, 999.0, t) * speed);
        if (map.segments.back().q >= mq - 1.0e-9) map.segments.back().secondsPerQ = spq;
        else map.segments.push_back ({ q, sec, spq });
    }
    return map;
}

double TempoMap::secondsAt (double q) const noexcept
{
    auto it = std::upper_bound (segments.begin(), segments.end(), q, [] (double v, const Segment& s) { return v < s.q; });
    const auto& s = it == segments.begin() ? segments.front() : *std::prev (it);
    return s.sec + (q - s.q) * s.secondsPerQ;
}

double TempoMap::quarterAt (double sec) const noexcept
{
    auto it = std::upper_bound (segments.begin(), segments.end(), sec, [] (double v, const Segment& s) { return v < s.sec; });
    const auto& s = it == segments.begin() ? segments.front() : *std::prev (it);
    return s.q + (sec - s.sec) / s.secondsPerQ;
}

double TempoMap::bpmAt (double q) const noexcept
{
    auto it = std::upper_bound (segments.begin(), segments.end(), q, [] (double v, const Segment& s) { return v < s.q; });
    const auto& s = it == segments.begin() ? segments.front() : *std::prev (it);
    return 60.0 / s.secondsPerQ;
}

float naturalHarmonicSemis (int fret)
{
    switch (fret)
    {
        case 12: return 12.0f;
        case 7: case 19: return 19.0f;
        case 5: case 24: return 24.0f;
        case 4: case 9: case 16: return 28.0f;
        case 3: return 31.0f;
        default: return 12.0f;
    }
}

// ------------------------------------------------------------------ compile

std::vector<NoteEvent> compile (const juce::ValueTree& score)
{
    std::vector<NoteEvent> events;
    const auto starts = measureStarts (score);
    int ti = 0;
    for (auto track : tracks (score))
    {
        const auto tuning = trackTuning (track);
        const int capo = (int) track[ids::capo];
        const int ns = (int) tuning.size();
        std::vector<int> last ((size_t) ns, -1);        // index of last event per string
        std::vector<bool> lastHammer ((size_t) ns, false), lastSlide ((size_t) ns, false), lastLetRing ((size_t) ns, false);

        for (int m = 0; m < track.getNumChildren() && m + 1 < (int) starts.size(); ++m)
        {
            double pos = starts[(size_t) m];
            for (auto beat : track.getChild (m))
            {
                const double len = beatLengthQ (beat);
                if (! (bool) beat[ids::rest])
                {
                    for (auto note : beat)
                    {
                        const int s = (int) note[ids::string];
                        if (! juce::isPositiveAndBelow (s, ns)) continue;
                        const int fret = (int) note[ids::fret];
                        const auto si = (size_t) s;

                        if ((bool) note[ids::tie] && last[si] >= 0)
                        {
                            auto& prev = events[(size_t) last[si]];
                            prev.lengthQ = juce::jmax (prev.lengthQ, pos + len - prev.startQ);
                            continue;
                        }

                        NoteEvent e;
                        e.startQ = pos;
                        e.lengthQ = (bool) note[ids::staccato] ? len * 0.5 : len;
                        e.track = ti;
                        e.string = s;
                        e.midi = tuning[si] + fret + capo;
                        e.harmonic = (int) note[ids::harmonic];
                        if (e.harmonic == harmNatural) e.midi = tuning[si] + capo + (int) naturalHarmonicSemis (fret);
                        else if (e.harmonic == harmArtificial) e.midi += 12;
                        e.dead = (bool) note[ids::dead];
                        e.ghost = (bool) note[ids::ghost];
                        e.palmMute = (bool) note[ids::palmMute];
                        e.vibrato = (bool) note[ids::vibrato];
                        e.bend = (float) (double) note[ids::bend];
                        e.prebend = (bool) note[ids::prebend];
                        e.legato = (last[si] >= 0 && lastHammer[si]) || (bool) note[ids::tap];
                        e.tremolo = (bool) beat[ids::tremolo];
                        e.whammy = (float) (double) beat[ids::whammy];
                        e.velocity = e.ghost ? 0.4f : ((bool) note[ids::accent] ? 1.0f : 0.8f);
                        if (e.legato) e.velocity *= 0.7f;

                        if (last[si] >= 0)
                        {
                            auto& prev = events[(size_t) last[si]];
                            if (lastSlide[si]) prev.slideToSemis = (float) (e.midi - prev.midi);
                            if (lastLetRing[si]) prev.lengthQ = pos - prev.startQ; // stops when the string is played again
                        }
                        if ((bool) note[ids::letRing])
                            e.lengthQ = juce::jmax (e.lengthQ, starts[(size_t) m + 1] - pos + 4.0);

                        last[si] = (int) events.size();
                        lastHammer[si] = (bool) note[ids::hammer];
                        lastSlide[si] = (int) note[ids::slide] == slideTo;
                        lastLetRing[si] = (bool) note[ids::letRing];
                        events.push_back (e);
                    }
                }
                pos += len;
            }
        }
        ++ti;
    }
    std::stable_sort (events.begin(), events.end(), [] (const NoteEvent& a, const NoteEvent& b) { return a.startQ < b.startQ; });
    return events;
}

// ------------------------------------------------------------------ editing helpers

int transposeBeats (const juce::Array<juce::ValueTree>& beats, const std::vector<int>& tuning, int semis, juce::UndoManager* um)
{
    int failed = 0;
    const int ns = (int) tuning.size();
    for (auto beat : beats)
    {
        std::vector<bool> used ((size_t) ns, false);
        std::vector<juce::ValueTree> pending;
        for (auto n : beat) pending.push_back (n);
        // process in pitch order matching direction so notes don't collide
        for (auto n : pending)
        {
            const int s = (int) n[ids::string], f = (int) n[ids::fret] + semis;
            if (f >= 0 && f <= maxFret && juce::isPositiveAndBelow (s, ns))
            {
                n.setProperty (ids::fret, f, um);
                used[(size_t) s] = true;
                continue;
            }
            if (! juce::isPositiveAndBelow (s, ns)) { ++failed; continue; }
            const int pitch = tuning[(size_t) s] + (int) n[ids::fret] + semis;
            bool moved = false;
            for (int s2 = 0; s2 < ns && ! moved; ++s2)
            {
                const int f2 = pitch - tuning[(size_t) s2];
                if (f2 >= 0 && f2 <= 24 && ! used[(size_t) s2] && ! noteOnString (beat, s2).isValid())
                {
                    n.setProperty (ids::string, s2, um);
                    n.setProperty (ids::fret, f2, um);
                    used[(size_t) s2] = true;
                    moved = true;
                }
            }
            if (! moved) ++failed;
        }
    }
    return failed;
}

std::vector<std::pair<int, int>> assignStrings (const std::vector<int>& notesIn, const std::vector<int>& tuning, int preferredFret)
{
    std::vector<int> notes = notesIn;
    std::sort (notes.begin(), notes.end());
    notes.erase (std::unique (notes.begin(), notes.end()), notes.end());
    const int ns = (int) tuning.size();
    while ((int) notes.size() > ns) notes.erase (notes.begin()); // drop lowest extras

    std::vector<std::pair<int, int>> best, cur;
    double bestCost = 1.0e9;
    std::vector<bool> used ((size_t) ns, false);

    std::function<void (size_t)> search = [&] (size_t i) {
        if (i == notes.size())
        {
            int lo = 99, hi = 0;
            double cost = 0.0;
            for (auto& [s, f] : cur)
            {
                cost += f * 0.3 + std::abs (f - preferredFret) * 0.4;
                if (f > 0) { lo = juce::jmin (lo, f); hi = juce::jmax (hi, f); }
            }
            if (hi > 0 && hi - lo > 4) cost += (hi - lo - 4) * 4.0; // hard stretch
            if (hi > 0) cost += (hi - lo) * 1.0;
            if (cost < bestCost) { bestCost = cost; best = cur; }
            return;
        }
        bool any = false;
        for (int s = 0; s < ns; ++s)
        {
            const int f = notes[i] - tuning[(size_t) s];
            if (used[(size_t) s] || f < 0 || f > 24) continue;
            any = true;
            used[(size_t) s] = true;
            cur.push_back ({ s, f });
            search (i + 1);
            cur.pop_back();
            used[(size_t) s] = false;
        }
        if (! any) search (i + 1); // unplayable pitch: skip it
    };
    search (0);
    return best;
}

// ------------------------------------------------------------------ MIDI export

juce::Result exportMidi (const juce::ValueTree& score, const juce::File& file)
{
    constexpr int tpq = 960;
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);

    juce::MidiMessageSequence conductor;
    const double tempo = juce::jlimit (10.0, 999.0, (double) score.getProperty (ids::tempo, 120.0));
    conductor.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / tempo)), 0.0);
    const auto starts = measureStarts (score);
    int prevNum = 0, prevDen = 0, mi = 0;
    for (auto m : measures (score))
    {
        const int num = (int) m.getProperty (ids::tsNum, 4), den = (int) m.getProperty (ids::tsDen, 4);
        if (num != prevNum || den != prevDen)
            conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (num, den), starts[(size_t) mi] * tpq);
        if ((double) m[ids::tempo] > 0.0)
            conductor.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / juce::jlimit (10.0, 999.0, (double) m[ids::tempo]))),
                                starts[(size_t) mi] * tpq);
        prevNum = num; prevDen = den; ++mi;
    }
    auto titleMsg = juce::MidiMessage::textMetaEvent (3, score[ids::title].toString());
    conductor.addEvent (titleMsg, 0.0);
    mf.addTrack (conductor);

    const auto events = compile (score);
    int ti = 0;
    for (auto track : tracks (score))
    {
        int channel = 1 + (ti % 15);
        if (channel >= 10) ++channel; // skip the GM drum channel
        const bool bass = track[ids::instrument].toString() == "bass";
        juce::MidiMessageSequence seq;
        auto nameMsg = juce::MidiMessage::textMetaEvent (3, track[ids::name].toString());
        seq.addEvent (nameMsg, 0.0);
        seq.addEvent (juce::MidiMessage::programChange (channel, bass ? 33 : 29), 0.0);
        for (const auto& e : events)
        {
            if (e.track != ti) continue;
            const double on = e.startQ * tpq;
            const double off = (e.startQ + juce::jmax (0.05, e.dead ? 0.1 : e.lengthQ)) * tpq;
            const auto vel = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt ((e.dead ? 0.3f : e.velocity) * 127.0f));
            if (e.bend > 0.0f || e.prebend)
            {
                const int wheel = juce::jlimit (0, 16383, 8192 + juce::roundToInt (juce::jmin (2.0f, e.bend) / 2.0f * 8191.0f));
                seq.addEvent (juce::MidiMessage::pitchWheel (channel, e.prebend ? wheel : 8192), on - 1);
                if (! e.prebend) seq.addEvent (juce::MidiMessage::pitchWheel (channel, wheel), on + (off - on) * 0.35);
                seq.addEvent (juce::MidiMessage::pitchWheel (channel, 8192), off);
            }
            seq.addEvent (juce::MidiMessage::noteOn (channel, juce::jlimit (0, 127, e.midi), vel), on);
            seq.addEvent (juce::MidiMessage::noteOff (channel, juce::jlimit (0, 127, e.midi)), off);
        }
        seq.updateMatchedPairs();
        mf.addTrack (seq);
        ++ti;
    }

    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! mf.writeTo (out, 1))
        return juce::Result::fail ("Could not write " + file.getFullPathName());
    return juce::Result::ok();
}

// ------------------------------------------------------------------ helpers for building bars

namespace
{
    /** Splits a length in 16ths into standard note values (largest first, dotted allowed). */
    std::vector<std::pair<int, int>> splitSixteenths (int units)
    {
        static const std::pair<int, std::pair<int, int>> table[] = {
            { 16, { 1, 0 } }, { 12, { 2, 1 } }, { 8, { 2, 0 } }, { 6, { 4, 1 } }, { 4, { 4, 0 } },
            { 3, { 8, 1 } }, { 2, { 8, 0 } }, { 1, { 16, 0 } } };
        std::vector<std::pair<int, int>> out;
        while (units > 0)
            for (auto& [u, v] : table)
                if (u <= units) { out.push_back (v); units -= u; break; }
        return out;
    }

    void ensureMeasures (juce::ValueTree score, int count, int tsNum, int tsDen, juce::UndoManager* um)
    {
        while (numMeasures (score) < count)
            addMeasure (score, -1, tsNum, tsDen, um);
    }

    std::vector<int> defaultTuningFor (int lowest, int highest, juce::String& instrument)
    {
        instrument = "guitar";
        if (lowest >= 40) return { 40, 45, 50, 55, 59, 64 };
        if (highest < 62)
        {
            instrument = "bass";
            if (lowest < 28) return { 23, 28, 33, 38, 43 };
            return { 28, 33, 38, 43 };
        }
        if (lowest >= 35) return { 35, 40, 45, 50, 55, 59, 64 };
        return { 30, 35, 40, 45, 50, 55, 59, 64 };
    }
}

// ------------------------------------------------------------------ MIDI import

ImportResult importMidi (juce::ValueTree score, const juce::File& file, juce::UndoManager* um)
{
    ImportResult res;
    juce::FileInputStream in (file);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in))
    {
        res.result = juce::Result::fail ("Not a readable MIDI file.");
        return res;
    }
    const short tf = mf.getTimeFormat();
    if (tf <= 0)
    {
        res.result = juce::Result::fail ("SMPTE-timed MIDI files are not supported.");
        return res;
    }
    const double tpq = tf;
    bool scoreEmpty = true; // no notes written yet: adopt the file's tempo and meter
    for (auto tr : tracks (score))
        for (auto b : tr)
            for (auto beat : b)
                if (beat.getNumChildren() > 0) scoreEmpty = false;

    // tempo & meter
    int tsNum = 4, tsDen = 4;
    for (int t = 0; t < mf.getNumTracks(); ++t)
        for (auto* ev : *mf.getTrack (t))
        {
            const auto& m = ev->message;
            if (m.isTempoMetaEvent() && scoreEmpty && ev->message.getTimeStamp() < 1.0)
                score.setProperty (ids::tempo, juce::jlimit (10.0, 999.0, 60.0 / m.getTempoSecondsPerQuarterNote()), um);
            if (m.isTimeSignatureMetaEvent() && m.getTimeStamp() < 1.0)
                m.getTimeSignatureInfo (tsNum, tsDen);
        }
    if (scoreEmpty)
        for (auto meas : measures (score))
        {
            meas.setProperty (ids::tsNum, tsNum, um);
            meas.setProperty (ids::tsDen, tsDen, um);
        }

    const int barUnits = juce::roundToInt (tsNum * 16.0 / tsDen); // 16ths per bar (meter of the file)
    if (barUnits <= 0)
    {
        res.result = juce::Result::fail ("Unsupported time signature in MIDI file.");
        return res;
    }

    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();
        struct N { int start, end, pitch; };
        std::vector<N> notes;
        juce::String name;
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            const auto* ev = seq.getEventPointer (i);
            const auto& m = ev->message;
            if (m.isTrackNameEvent()) name = m.getTextFromTextMetaEvent();
            if (! m.isNoteOn() || m.getChannel() == 10) continue;
            const double offT = ev->noteOffObject != nullptr ? ev->noteOffObject->message.getTimeStamp() : m.getTimeStamp() + tpq / 4;
            const int s = juce::roundToInt (m.getTimeStamp() / (tpq / 4.0));
            const int e = juce::jmax (s + 1, juce::roundToInt (offT / (tpq / 4.0)));
            notes.push_back ({ s, e, m.getNoteNumber() });
        }
        if (notes.empty()) continue;

        int lo = 127, hi = 0, lastUnit = 0;
        for (auto& n : notes) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); lastUnit = juce::jmax (lastUnit, n.end); }
        juce::String instrument;
        const auto tuning = defaultTuningFor (lo, hi, instrument);
        const int bars = (lastUnit + barUnits - 1) / barUnits;
        if (bars > 2000)
        {
            res.warnings.add ("Track " + juce::String (t) + " is too long and was skipped.");
            continue;
        }
        ensureMeasures (score, bars, tsNum, tsDen, um);
        auto track = addTrack (score, name.isNotEmpty() ? name : "MIDI " + juce::String (t), tuning, instrument, um);

        std::map<int, std::vector<N>> byStart;
        for (auto& n : notes) byStart[n.start].push_back (n);

        int unplayable = 0, prevFret = 3;
        for (int b = 0; b < bars; ++b)
        {
            auto barNode = bar (track, b);
            const int b0 = b * barUnits, b1 = b0 + barUnits;
            int cursor = b0;
            auto it = byStart.lower_bound (b0);
            auto appendBeats = [&] (int units, const std::vector<std::pair<int, int>>& placed) {
                bool first = true;
                for (auto [dur, dots] : splitSixteenths (units))
                {
                    auto beat = makeBeat (dur, placed.empty());
                    if (dots) beat.setProperty (ids::dots, dots, nullptr);
                    for (auto& [s, f] : placed)
                    {
                        auto n = makeNote (s, f);
                        if (! first) n.setProperty (ids::tie, true, nullptr);
                        beat.appendChild (n, nullptr);
                    }
                    barNode.appendChild (beat, um);
                    first = false;
                }
            };
            while (cursor < b1)
            {
                const bool onset = it != byStart.end() && it->first == cursor;
                const auto nxt = onset ? std::next (it) : it;
                const int next = nxt != byStart.end() ? juce::jmin (b1, nxt->first) : b1;
                if (! onset)
                {
                    appendBeats (juce::jmax (1, next - cursor), {});
                    cursor = juce::jmax (cursor + 1, next);
                    continue;
                }
                std::vector<int> pitches;
                int chordEnd = cursor + 1;
                for (auto& n : it->second) { pitches.push_back (n.pitch); chordEnd = juce::jmax (chordEnd, n.end); }
                const auto placed = assignStrings (pitches, tuning, prevFret);
                unplayable += (int) pitches.size() - (int) placed.size();
                int sum = 0, cnt = 0;
                for (auto& [s, f] : placed) if (f > 0) { sum += f; ++cnt; }
                if (cnt > 0) prevFret = sum / cnt;
                const int noteEnd = juce::jlimit (cursor + 1, juce::jmax (cursor + 1, next), chordEnd);
                appendBeats (noteEnd - cursor, placed);
                if (next > noteEnd) appendBeats (next - noteEnd, {});
                cursor = juce::jmax (cursor + 1, next);
                it = nxt;
            }
        }
        if (unplayable > 0)
            res.warnings.add (juce::String (unplayable) + " note(s) in \"" + track[ids::name].toString() + "\" were outside the instrument's range.");
        ++res.tracksAdded;
    }
    if (res.tracksAdded == 0)
        res.result = juce::Result::fail ("The MIDI file contains no melodic notes.");
    else if (scoreEmpty)
    {
        // later tempo changes land on the nearest bar line
        const double barTicks = tpq * 4.0 * tsNum / tsDen;
        for (int t = 0; t < mf.getNumTracks(); ++t)
            for (auto* ev : *mf.getTrack (t))
                if (ev->message.isTempoMetaEvent() && ev->message.getTimeStamp() >= 1.0)
                {
                    const int bi = juce::roundToInt (ev->message.getTimeStamp() / barTicks);
                    if (juce::isPositiveAndBelow (bi, numMeasures (score)))
                        measures (score).getChild (bi).setProperty (ids::tempo,
                            juce::jlimit (10.0, 999.0, 60.0 / ev->message.getTempoSecondsPerQuarterNote()), um);
                }
    }
    return res;
}

// ------------------------------------------------------------------ ASCII tab import

ImportResult importAsciiTab (juce::ValueTree score, const juce::String& text, const juce::String& name, juce::UndoManager* um)
{
    ImportResult res;
    auto lines = juce::StringArray::fromLines (text);

    auto isTabLine = [] (const juce::String& l) {
        const auto t = l.trim();
        if (t.length() < 6) return false;
        int dashes = 0, bad = 0;
        for (auto c : t)
        {
            if (c == '-') ++dashes;
            else if (! juce::String ("0123456789|hpbr/\\~xX()<>.^sSvt=*").containsChar (c) && ! juce::CharacterFunctions::isLetter (c) && c != ' ' && c != '#')
                ++bad;
        }
        return dashes >= 4 && bad == 0 && t.containsChar ('|');
    };

    // group consecutive tab lines into systems
    std::vector<juce::StringArray> systems;
    juce::StringArray cur;
    for (auto& l : lines)
    {
        if (isTabLine (l)) cur.add (l);
        else
        {
            if (cur.size() >= 4 && cur.size() <= 8) systems.push_back (cur);
            cur.clear();
        }
    }
    if (cur.size() >= 4 && cur.size() <= 8) systems.push_back (cur);
    if (systems.empty())
    {
        res.result = juce::Result::fail ("No ASCII tablature found (expected 4-8 lines like e|---0---|).");
        return res;
    }

    const int ns = systems[0].size();
    std::vector<int> tuning;
    juce::String instrument = "guitar";
    switch (ns)
    {
        case 4: tuning = { 28, 33, 38, 43 }; instrument = "bass"; break;
        case 5: tuning = { 23, 28, 33, 38, 43 }; instrument = "bass"; break;
        case 7: tuning = { 35, 40, 45, 50, 55, 59, 64 }; break;
        case 8: tuning = { 30, 35, 40, 45, 50, 55, 59, 64 }; break;
        default: tuning = { 40, 45, 50, 55, 59, 64 }; break;
    }
    // string labels (e.g. "D|") adjust the pitch class, keeping the nearest octave
    for (int i = 0; i < ns; ++i)
    {
        const auto label = systems[0][i].trim().upToFirstOccurrenceOf ("|", false, false).trim();
        if (label.isEmpty() || label.length() > 2) continue;
        const int pc = theory::parseNoteName (label.toUpperCase().substring (0, 1) + label.substring (1) + "4");
        if (pc < 0) continue;
        const size_t si = (size_t) (ns - 1 - i);
        const int def = tuning[si];
        int best = def, bestD = 99;
        for (int o = -2; o <= 2; ++o)
        {
            const int cand = (pc % 12) + 12 * ((def / 12) + o);
            if (std::abs (cand - def) < bestD) { bestD = std::abs (cand - def); best = cand; }
        }
        tuning[si] = best;
    }

    struct ParsedBeat { std::vector<std::pair<int, int>> notes; std::vector<juce::String> tech; };
    std::vector<std::vector<ParsedBeat>> parsedBars;

    for (auto& sys : systems)
    {
        if (sys.size() != ns) { res.warnings.add ("A tab system with a different number of strings was skipped."); continue; }
        juce::StringArray body;
        for (auto& l : sys) body.add (l.fromFirstOccurrenceOf ("|", false, false));
        int width = 0;
        for (auto& b : body) width = juce::jmax (width, b.length());
        std::vector<ParsedBeat> barBeats;
        for (int c = 0; c < width; ++c)
        {
            int barChars = 0;
            for (auto& b : body) if (c < b.length() && b[c] == '|') ++barChars;
            if (barChars * 2 > ns)
            {
                if (! barBeats.empty()) parsedBars.push_back (barBeats);
                barBeats.clear();
                continue;
            }
            ParsedBeat pb;
            int consumed = 1;
            for (int i = 0; i < ns; ++i)
            {
                const auto& b = body[i];
                if (c >= b.length()) continue;
                const auto ch = b[c];
                const bool prevDigit = c > 0 && juce::CharacterFunctions::isDigit (b[c - 1]);
                if (juce::CharacterFunctions::isDigit (ch) && ! prevDigit)
                {
                    int fret = ch - '0', len = 1;
                    if (c + 1 < b.length() && juce::CharacterFunctions::isDigit (b[c + 1]))
                    {
                        fret = fret * 10 + (b[c + 1] - '0');
                        len = 2;
                    }
                    consumed = juce::jmax (consumed, len);
                    const auto after = c + len < b.length() ? juce::String::charToString (b[c + len]) : juce::String();
                    const bool ghost = c > 0 && b[c - 1] == '(';
                    const bool harm = c > 0 && b[c - 1] == '<';
                    pb.notes.push_back ({ ns - 1 - i, juce::jlimit (0, maxFret, fret) });
                    pb.tech.push_back (after + (ghost ? "G" : "") + (harm ? "N" : ""));
                }
                else if ((ch == 'x' || ch == 'X') && ! prevDigit)
                {
                    pb.notes.push_back ({ ns - 1 - i, 0 });
                    pb.tech.push_back ("X");
                }
            }
            if (! pb.notes.empty())
                barBeats.push_back (pb);
            c += consumed - 1;
        }
        if (! barBeats.empty()) parsedBars.push_back (barBeats);
    }

    if (parsedBars.empty())
    {
        res.result = juce::Result::fail ("The tablature contains no notes.");
        return res;
    }

    ensureMeasures (score, (int) parsedBars.size(), 4, 4, um);
    auto track = addTrack (score, name, tuning, instrument, um);
    for (size_t b = 0; b < parsedBars.size(); ++b)
    {
        auto barNode = bar (track, (int) b);
        const auto& beats = parsedBars[b];
        const int n = (int) beats.size();
        const int dur = n <= 4 ? 4 : (n <= 8 ? 8 : (n <= 16 ? 16 : 32));
        for (auto& pb : beats)
        {
            auto beat = makeBeat (dur, false);
            for (size_t k = 0; k < pb.notes.size(); ++k)
            {
                auto note = makeNote (pb.notes[k].first, pb.notes[k].second);
                const auto& t = pb.tech[k];
                if (t.containsAnyOf ("hp")) note.setProperty (ids::hammer, true, nullptr);
                if (t.containsAnyOf ("/\\s")) note.setProperty (ids::slide, (int) slideTo, nullptr);
                if (t.containsChar ('b')) note.setProperty (ids::bend, 1.0, nullptr);
                if (t.containsChar ('~')) note.setProperty (ids::vibrato, true, nullptr);
                if (t.containsChar ('X')) note.setProperty (ids::dead, true, nullptr);
                if (t.containsChar ('G')) note.setProperty (ids::ghost, true, nullptr);
                if (t.containsChar ('N')) note.setProperty (ids::harmonic, (int) harmNatural, nullptr);
                beat.appendChild (note, nullptr);
            }
            barNode.appendChild (beat, um);
        }
        // fill the rest of the 4/4 bar with rests
        int used = (int) std::round (n * 16.0 / dur);
        if (used < 16)
            for (auto [d, dots] : splitSixteenths (16 - used))
            {
                auto r = makeBeat (d, true);
                if (dots) r.setProperty (ids::dots, dots, nullptr);
                barNode.appendChild (r, um);
            }
        else if (used > 16)
            res.warnings.add ("Bar " + juce::String ((int) b + 1) + " has more notes than fit in 4/4 at 1/32; check its rhythm.");
    }
    res.tracksAdded = 1;
    res.warnings.add ("ASCII tab has no rhythm information: durations were estimated from the number of notes per bar.");
    return res;
}
} // namespace ff::tab
