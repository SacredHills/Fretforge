#include <juce_audio_basics/juce_audio_basics.h>
#include "core/TabModel.h"
#include "core/PitchDetector.h"
#include "core/ProjectFile.h"
#include "engine/TabSynth.h"
#include "engine/AudioEngine.h"
#include <cmath>

using namespace ff;
namespace t = ff::tab;

namespace
{
    juce::ValueTree note (int s, int f) { return t::makeNote (s, f); }
    juce::ValueTree beat (int dur, std::initializer_list<juce::ValueTree> notes)
    {
        auto b = t::makeBeat (dur, notes.size() == 0);
        for (auto n : notes) b.appendChild (n, nullptr);
        return b;
    }
    double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }

    double measurePitch (const std::vector<float>& sig, int from, double sr)
    {
        PitchDetector pd;
        pd.prepare (sr);
        return pd.detect (sig.data() + from).frequency;
    }
}

struct TabTests : juce::UnitTest
{
    TabTests() : juce::UnitTest ("Tablature", "core") {}

    void runTest() override
    {
        beginTest ("durations and measures");
        expectEquals (t::beatLengthQ (4, 0, 0), 1.0);
        expectEquals (t::beatLengthQ (8, 1, 0), 0.75);
        expectWithinAbsoluteError (t::beatLengthQ (8, 0, 3), 1.0 / 3.0, 1e-12);
        expectEquals (t::beatLengthQ (1, 0, 0), 4.0);
        expectEquals (t::beatLengthQ (16, 2, 0), 0.4375);

        auto project = project::createDefault ("T");
        auto score = t::getOrCreateScore (project);
        expectEquals (t::numMeasures (score), 4);
        t::measures (score).getChild (1).setProperty (t::ids::tsNum, 7, nullptr);
        t::measures (score).getChild (1).setProperty (t::ids::tsDen, 8, nullptr);
        const auto starts = t::measureStarts (score);
        expectEquals (starts[1], 4.0);
        expectEquals (starts[2], 7.5);
        expectEquals (starts[4], 15.5);

        beginTest ("tracks keep one bar per measure");
        auto gtr = t::addTrack (score, "Gtr", { 40, 45, 50, 55, 59, 64 }, "guitar", nullptr);
        auto bass = t::addTrack (score, "Bass", { 28, 33, 38, 43 }, "bass", nullptr);
        expectEquals (gtr.getNumChildren(), 4);
        t::addMeasure (score, 2, 4, 4, nullptr);
        expectEquals (gtr.getNumChildren(), 5);
        expectEquals (bass.getNumChildren(), 5);
        t::removeMeasure (score, 2, nullptr);
        expectEquals (gtr.getNumChildren(), 4);
        expectEquals (t::numStrings (bass), 4);

        beginTest ("compile: timing, capo, ties, let ring, slides, legato, harmonics");
        {
            auto b0 = t::bar (gtr, 0);
            auto hammer = note (0, 3);
            hammer.setProperty (t::ids::hammer, true, nullptr);
            b0.appendChild (beat (4, { note (0, 0) }), nullptr);       // q0: E2
            b0.appendChild (beat (8, { hammer }), nullptr);            // q1: G2, hammer into next
            b0.appendChild (beat (8, { note (0, 5) }), nullptr);       // q1.5: A2 legato
            auto slideNote = note (1, 2);
            slideNote.setProperty (t::ids::slide, (int) t::slideTo, nullptr);
            b0.appendChild (beat (4, { slideNote }), nullptr);         // q2: B2 slide to
            auto tied = note (1, 4);
            b0.appendChild (beat (8, { tied }), nullptr);              // q3: C#3
            auto tie = note (1, 4);
            tie.setProperty (t::ids::tie, true, nullptr);
            b0.appendChild (beat (8, { tie }), nullptr);               // q3.5 tie -> length 1
            auto b1 = t::bar (gtr, 1);
            auto ring = note (5, 0);
            ring.setProperty (t::ids::letRing, true, nullptr);
            b1.appendChild (beat (8, { ring }), nullptr);              // q4
            auto harm = note (2, 12);
            harm.setProperty (t::ids::harmonic, (int) t::harmNatural, nullptr);
            b1.appendChild (beat (8, { harm }), nullptr);              // q4.5
            b1.appendChild (beat (4, { note (5, 3) }), nullptr);       // q5: stops let ring

            auto ev = t::compile (score);
            expectEquals ((int) ev.size(), 8);
            expectEquals (ev[0].midi, 40);
            expectEquals (ev[1].startQ, 1.0);
            expect (! ev[1].legato && ev[2].legato, "hammer-on makes the next note legato");
            expectEquals (ev[3].slideToSemis, 2.0f);
            expectEquals (ev[4].lengthQ, 1.0);                         // tie merged
            expectEquals (ev[5].midi, 64);
            expectEquals (ev[5].lengthQ, 1.0);                         // let ring until q5
            expectEquals (ev[6].midi, 50 + 12);                        // 12th fret natural harmonic
            gtr.setProperty (t::ids::capo, 2, nullptr);
            expectEquals (t::compile (score)[0].midi, 42);
            gtr.setProperty (t::ids::capo, 0, nullptr);
        }

        beginTest ("string assignment finds playable chord shapes");
        {
            const std::vector<int> std6 { 40, 45, 50, 55, 59, 64 };
            auto e = t::assignStrings ({ 40, 47, 52, 56, 59, 64 }, std6);
            expectEquals ((int) e.size(), 6);
            int frets[6] = { -1, -1, -1, -1, -1, -1 };
            for (auto [s, f] : e) frets[s] = f;
            expect (frets[0] == 0 && frets[1] == 2 && frets[2] == 2 && frets[3] == 1 && frets[4] == 0 && frets[5] == 0,
                    "open E major expected");
            auto power = t::assignStrings ({ 45, 52 }, std6, 5);
            expectEquals ((int) power.size(), 2);
            expect (t::assignStrings ({ 20 }, std6).empty(), "below range is skipped");
        }

        beginTest ("transpose moves frets and re-strings notes that fall off the neck");
        {
            auto b = beat (4, { note (0, 1), note (5, 29) });
            juce::Array<juce::ValueTree> beats { b };
            const int failed = t::transposeBeats (beats, { 40, 45, 50, 55, 59, 64 }, -2, nullptr);
            // fret 1 - 2 is impossible on string 0 and the pitch (39) is below the instrument
            expectEquals (failed, 1);
            int count = 0;
            for (auto n : b) ++count;
            expectEquals (count, 2);
            expectEquals ((int) t::noteOnString (b, 5)[t::ids::fret], 27);
        }

        beginTest ("MIDI export / import round trip");
        {
            auto proj = project::createDefault ("M");
            auto sc = t::getOrCreateScore (proj);
            sc.setProperty (t::ids::tempo, 100.0, nullptr);
            auto tr = t::addTrack (sc, "Riff", { 40, 45, 50, 55, 59, 64 }, "guitar", nullptr);
            auto b0 = t::bar (tr, 0);
            for (int i = 0; i < 8; ++i)
                b0.appendChild (beat (8, { note (0, i % 2 == 0 ? 0 : 3) }), nullptr);
            t::bar (tr, 1).appendChild (beat (2, { note (1, 2), note (2, 2) }), nullptr);
            t::bar (tr, 1).appendChild (t::makeBeat (2, true), nullptr);

            auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ff_tab_test.mid");
            expect (t::exportMidi (sc, f).wasOk());

            auto proj2 = project::createDefault ("M2");
            auto sc2 = t::getOrCreateScore (proj2);
            auto r = t::importMidi (sc2, f, nullptr);
            expect (r.result.wasOk(), r.result.getErrorMessage());
            expectEquals (r.tracksAdded, 1);
            expectWithinAbsoluteError ((double) sc2[t::ids::tempo], 100.0, 0.01);
            const auto a = t::compile (sc), b = t::compile (sc2);
            expectEquals ((int) b.size(), (int) a.size());
            for (size_t i = 0; i < juce::jmin (a.size(), b.size()); ++i)
            {
                expectEquals (b[i].midi, a[i].midi);
                expectWithinAbsoluteError (b[i].startQ, a[i].startQ, 1e-9);
            }
            f.deleteFile();
        }

        beginTest ("ASCII tab import");
        {
            const juce::String ascii =
                "Intro riff\n"
                "e|-----------------|----------------|\n"
                "B|-----------------|----------------|\n"
                "G|-----------------|----------------|\n"
                "D|-----------------|-------7--------|\n"
                "A|-----3h5---------|----5---5-------|\n"
                "D|-0-0-------0-x---|-0--------------|\n";
            auto proj = project::createDefault ("A");
            auto sc = t::getOrCreateScore (proj);
            auto r = t::importAsciiTab (sc, ascii, "Intro", nullptr);
            expect (r.result.wasOk(), r.result.getErrorMessage());
            auto tr = t::tracks (sc).getChild (t::tracks (sc).getNumChildren() - 1);
            const auto tuning = t::trackTuning (tr);
            expectEquals (tuning[0], 38); // drop D detected from the label
            auto bar0 = t::bar (tr, 0);
            expectEquals (bar0.getChild (0).getChild (0)[t::ids::fret].toString(), juce::String ("0"));
            bool foundHammer = false, foundDead = false;
            for (auto b : bar0)
                for (auto n : b)
                {
                    foundHammer = foundHammer || (bool) n[t::ids::hammer];
                    foundDead = foundDead || (bool) n[t::ids::dead];
                }
            expect (foundHammer && foundDead);
            expectWithinAbsoluteError (t::barContentQ (bar0), 4.0, 1e-9);
            expect (t::importAsciiTab (sc, "no tab here", "x", nullptr).result.failed());
        }

        beginTest ("guitar synth plays in tune, bends and damps");
        {
            const double sr = 48000.0;
            GuitarSynth synth;
            synth.prepare (sr);
            SynthNote n;
            n.frequency = 110.0f;
            n.endSec = 2.0;
            synth.start (n);
            std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
            synth.render (l.data(), r.data(), 48000);
            const double f = measurePitch (l, 12000, sr);
            expect (std::abs (cents (f, 110.0)) < 6.0, "A2 at " + juce::String (f, 2));

            for (double target : { 82.41, 196.0, 659.26 })
            {
                synth.allNotesOff();
                std::vector<float> flush (48000, 0.0f), fr (48000, 0.0f);
                synth.render (flush.data(), fr.data(), 48000);
                SynthNote m;
                m.frequency = (float) target;
                m.endSec = 2.0;
                m.string = 3;
                synth.start (m);
                std::vector<float> a (24000, 0.0f), b (24000, 0.0f);
                synth.render (a.data(), b.data(), 24000);
                const double got = measurePitch (a, 6000, sr);
                expect (std::abs (cents (got, target)) < 8.0, juce::String (target) + " -> " + juce::String (got, 2));
            }

            // full bend: pitch reaches +2 semitones after the rise
            synth.allNotesOff();
            std::vector<float> flush (48000, 0.0f), fr (48000, 0.0f);
            synth.render (flush.data(), fr.data(), 48000);
            SynthNote bendNote;
            bendNote.frequency = 220.0f;
            bendNote.endSec = 1.0;
            bendNote.bend = 2.0f;
            bendNote.string = 4;
            synth.start (bendNote);
            std::vector<float> bl (48000, 0.0f), br (48000, 0.0f);
            synth.render (bl.data(), br.data(), 48000);
            const double bent = measurePitch (bl, 24000, sr);
            expect (std::abs (cents (bent, 220.0 * std::pow (2.0, 2.0 / 12.0))) < 15.0, "bend reached " + juce::String (bent, 2));

            // palm mute decays far faster than an open note
            auto energyAfter = [&] (bool pm) {
                GuitarSynth s2;
                s2.prepare (sr);
                SynthNote x;
                x.frequency = 110.0f;
                x.endSec = 3.0;
                x.palmMute = pm;
                s2.start (x);
                std::vector<float> xl (48000, 0.0f), xr (48000, 0.0f);
                s2.render (xl.data(), xr.data(), 48000);
                double e = 0.0;
                for (int i = 24000; i < 48000; ++i) e += xl[(size_t) i] * xl[(size_t) i];
                return e;
            };
            expect (energyAfter (true) < energyAfter (false) * 0.01, "palm mute should damp quickly");
        }

        beginTest ("engine plays a tab sequence in tab mode only");
        {
            auto seq = buildTabSequence (score, 1.0);
            expect (! seq->notes.empty());
            expectWithinAbsoluteError (seq->secondsPerQuarter, 0.5, 1e-9);
            auto half = buildTabSequence (score, 0.5);
            expectWithinAbsoluteError (half->notes[1].startSec, seq->notes[1].startSec * 2.0, 1e-9);

            AudioEngine e;
            e.prepare (48000.0, 512);
            e.submitTabSequence (buildTabSequence (score, 1.0));
            std::vector<float> l (512), r (512);
            float* outs[2] = { l.data(), r.data() };
            e.play();
            float pk = 0.0f;
            for (int i = 0; i < 40; ++i) { e.process (nullptr, 0, outs, 2, 512); pk = juce::jmax (pk, l[100]); }
            expectEquals (pk, 0.0f, "not in tab mode: silent");
            e.tabMode = true;
            e.returnToZero();
            pk = 0.0f;
            for (int i = 0; i < 40; ++i)
            {
                e.process (nullptr, 0, outs, 2, 512);
                for (auto v : l) pk = juce::jmax (pk, std::abs (v));
            }
            expect (pk > 0.01f, "tab synth output " + juce::String (pk));
            e.collectGarbage();
        }

        beginTest ("tempo and meter changes: tempo map, note times and score clicks");
        {
            juce::ValueTree project ("PROJECT");
            auto sc = t::getOrCreateScore (project);
            sc.setProperty (t::ids::tempo, 120.0, nullptr);
            while (t::numMeasures (sc) > 3) t::removeMeasure (sc, 3, nullptr);
            t::measures (sc).getChild (1).setProperty (t::ids::tempo, 60.0, nullptr);
            t::measures (sc).getChild (2).setProperty (t::ids::tsNum, 3, nullptr);
            t::measures (sc).getChild (2).setProperty (t::ids::tempo, 180.0, nullptr);
            auto tr = t::addTrack (sc, "G", { 40, 45, 50, 55, 59, 64 }, "guitar", nullptr);
            for (int m = 0; m < 3; ++m)
                t::bar (tr, m).appendChild (beat (4, { note (0, 5) }), nullptr);

            expectEquals (t::measureTempo (sc, 0), 120.0);
            expectEquals (t::measureTempo (sc, 1), 60.0);
            expectEquals (t::measureTempo (sc, 2), 180.0);
            const auto map = t::tempoMap (sc);
            // bar 1: 4 q at 0.5 s; bar 2: 4 q at 1 s; bar 3 starts at 2 + 4 = 6 s
            expectWithinAbsoluteError (map.secondsAt (4.0), 2.0, 1e-12);
            expectWithinAbsoluteError (map.secondsAt (8.0), 6.0, 1e-12);
            expectWithinAbsoluteError (map.secondsAt (9.0), 6.0 + 1.0 / 3.0, 1e-12);
            expectWithinAbsoluteError (map.quarterAt (4.0), 6.0, 1e-12);
            expectWithinAbsoluteError (map.quarterAt (map.secondsAt (10.3)), 10.3, 1e-9);
            expectEquals (map.bpmAt (5.0), 60.0);
            const auto slow = t::tempoMap (sc, 0.5);
            expectWithinAbsoluteError (slow.secondsAt (8.0), 12.0, 1e-12);

            auto seq = buildTabSequence (sc, 1.0);
            expectEquals ((int) seq->notes.size(), 3);
            expectWithinAbsoluteError (seq->notes[1].startSec, 2.0, 1e-9);
            expectWithinAbsoluteError (seq->notes[2].startSec, 6.0, 1e-9);
            expectWithinAbsoluteError (seq->notes[1].endSec - seq->notes[1].startSec, 1.0, 1e-9);
            expectEquals ((int) seq->clicks.size(), 4 + 4 + 3);
            expect (seq->clicks[0].accent && ! seq->clicks[1].accent && seq->clicks[4].accent && seq->clicks[8].accent);
            expectWithinAbsoluteError (seq->clicks[5].sec, 3.0, 1e-9);
            expectWithinAbsoluteError (seq->clicks[9].sec, 6.0 + 1.0 / 3.0, 1e-9);
            expectWithinAbsoluteError (seq->lengthSec, 7.0, 1e-9);

            // the engine clicks exactly on those beats (synth silenced by muting the track)
            tr.setProperty (t::ids::mute, true, nullptr);
            AudioEngine e;
            constexpr double sr = 48000.0;
            e.prepare (sr, 480);
            e.tabMode = true;
            e.clickWhilePlaying = true;
            e.submitTabSequence (buildTabSequence (sc, 1.0));
            std::vector<float> l (480), r (480), all;
            float* outs[2] = { l.data(), r.data() };
            e.play();
            for (int i = 0; i < (int) (7.2 * sr / 480); ++i)
            {
                e.process (nullptr, 0, outs, 2, 480);
                all.insert (all.end(), l.begin(), l.end());
            }
            std::vector<int> onsets;
            for (int i = 1; i < (int) all.size(); ++i)
                if (std::abs (all[(size_t) i]) > 1.0e-4f && (onsets.empty() || i - onsets.back() > 2400))
                {
                    bool silentBefore = true;
                    for (int k = juce::jmax (0, i - 200); k < i; ++k) silentBefore = silentBefore && std::abs (all[(size_t) k]) <= 1.0e-4f;
                    if (silentBefore || onsets.empty()) onsets.push_back (i);
                }
            expectEquals ((int) onsets.size(), 11, "click count");
            const double expected[] = { 0, 0.5, 1, 1.5, 2, 3, 4, 5, 6, 6 + 1.0 / 3.0, 6 + 2.0 / 3.0 };
            for (int i = 0; i < juce::jmin (11, (int) onsets.size()); ++i)
                expect (std::abs (onsets[(size_t) i] - expected[i] * sr) <= 2.0, "click " + juce::String (i) + " at " + juce::String (onsets[(size_t) i]));
            e.collectGarbage();
        }
    }
};
static TabTests tabTests;
