#include <juce_core/juce_core.h>
#include "core/MetronomeDsp.h"
#include <cmath>

namespace
{
    // Renders `seconds` of click in blocks of `block` and returns onset sample indices.
    std::vector<int> renderOnsets (ff::MetronomeDsp& m, double sr, double seconds, int block, std::vector<float>* peaks = nullptr)
    {
        const int total = (int) (sr * seconds);
        std::vector<float> buf ((size_t) total, 0.0f);
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = juce::jmin (block, total - pos);
            float* ch[1] = { buf.data() + pos };
            m.process (ch, 1, n);
        }
        std::vector<int> onsets;
        bool silent = true;
        int silentRun = 1000;
        for (int i = 0; i < total; ++i)
        {
            const bool loud = std::abs (buf[(size_t) i]) > 1.0e-4f;
            if (loud && silentRun > 200)
            {
                onsets.push_back (i);
                if (peaks != nullptr)
                {
                    float pk = 0.0f;
                    for (int j = i; j < juce::jmin (total, i + 400); ++j) pk = juce::jmax (pk, std::abs (buf[(size_t) j]));
                    peaks->push_back (pk);
                }
            }
            silentRun = loud ? 0 : silentRun + 1;
            silent = ! loud;
        }
        juce::ignoreUnused (silent);
        return onsets;
    }
}

struct MetronomeTests : juce::UnitTest
{
    MetronomeTests() : juce::UnitTest ("Metronome", "core") {}
    void runTest() override
    {
        const double sr = 48000.0;

        beginTest ("sample-accurate beats independent of block size");
        for (int block : { 32, 64, 256, 441, 1024, 4096 })
        {
            ff::MetronomeDsp m;
            m.prepare (sr);
            m.bpm = 120.0;
            const auto on = renderOnsets (m, sr, 4.0, block);
            expectEquals ((int) on.size(), 8, "block " + juce::String (block));
            for (size_t i = 0; i < on.size(); ++i)
                expect (std::abs (on[i] - (int) i * 24000) <= 3, "onset " + juce::String ((int) i) + " at " + juce::String (on[i]));
        }

        beginTest ("accent on beat 1, mute levels respected");
        {
            ff::MetronomeDsp m;
            m.prepare (sr);
            m.bpm = 240.0;          // 12000 samples per beat
            m.beatsPerBar = 3;
            m.beatLevels[1] = ff::MetronomeDsp::mute;
            std::vector<float> peaks;
            const auto on = renderOnsets (m, sr, 1.5, 512, &peaks); // 6 beats -> 2 bars, beat 2 muted
            expectEquals ((int) on.size(), 4);
            if (on.size() == 4)
            {
                expect (on[0] <= 3);
                expect (std::abs (on[1] - 24000) <= 3);
                expect (std::abs (on[2] - 36000) <= 3);
                expect (peaks[0] > peaks[1] * 1.1f, "accent should be louder");
            }
        }

        beginTest ("compound meter: 6/8 at 120 quarter BPM clicks eighths");
        {
            ff::MetronomeDsp m;
            m.prepare (sr);
            m.bpm = 120.0;
            m.beatsPerBar = 6;
            m.beatUnit = 8;
            const auto on = renderOnsets (m, sr, 1.0, 256);
            expectEquals ((int) on.size(), 4); // 12000 samples per eighth
            expectEquals (ff::MetronomeDsp::beatLengthSamples (sr, 120.0, 8), 12000.0);
        }

        beginTest ("subdivision and swing");
        {
            ff::MetronomeDsp m;
            m.prepare (sr);
            m.bpm = 60.0; // 48000 per beat
            m.subdivision = 2;
            m.swing = 1.0f / 3.0f; // 2:1 triplet swing -> 32000 / 16000
            const auto on = renderOnsets (m, sr, 2.0, 333);
            expectEquals ((int) on.size(), 4);
            if (on.size() == 4)
            {
                expect (std::abs (on[1] - 32000) <= 3, juce::String (on[1]));
                expect (std::abs (on[2] - 48000) <= 3, juce::String (on[2]));
                expect (std::abs (on[3] - 80000) <= 3, juce::String (on[3]));
            }
        }

        beginTest ("tempo ramp increases BPM every N bars up to target");
        {
            ff::MetronomeDsp m;
            m.prepare (sr);
            m.bpm = 120.0;
            m.beatsPerBar = 4;
            m.rampEnabled = true;
            m.rampStepBpm = 10.0;
            m.rampEveryBars = 1;
            m.rampTargetBpm = 140.0;
            m.reset();
            renderOnsets (m, sr, 10.0, 512);
            expectWithinAbsoluteError (m.effectiveBpm.load(), 140.0, 1e-9);
            expect (m.barCounter.load() >= 4);
        }

        beginTest ("resetAtBeat lines up with musical position");
        {
            ff::MetronomeDsp m;
            m.prepare (sr);
            m.bpm = 120.0;
            m.resetAtBeat (5.5); // halfway through beat 2 of bar 2
            expectEquals (m.barCounter.load(), (juce::int64) 1);
            const auto on = renderOnsets (m, sr, 1.0, 256);
            expect (! on.empty() && std::abs (on[0] - 12000) <= 3, "first click should be half a beat later");
            expectEquals (m.barCounter.load(), (juce::int64) 2); // beats 3 and 4 played, bar wrapped
        }

        beginTest ("random missing beats keep the downbeat");
        {
            ff::MetronomeDsp m;
            m.prepare (sr);
            m.bpm = 240.0;
            m.randomMuteProbability = 1.0f;
            const auto on = renderOnsets (m, sr, 4.0, 512); // 4 bars; bar 1 unmuted (rolled after), later only downbeats
            expect (on.size() >= 4 && on.size() <= 7, juce::String ((int) on.size()));
        }

        beginTest ("every click sound is audible, also when switched while running");
        {
            ff::MetronomeDsp m;
            m.prepare (sr);
            m.bpm = 120.0;
            for (int snd = 0; snd < ff::MetronomeDsp::numSounds; ++snd)
            {
                m.sound = snd;
                const auto on = renderOnsets (m, sr, 2.0, 256);
                expect (on.size() >= 3, "sound " + juce::String (snd) + " gave " + juce::String ((int) on.size()) + " clicks");
            }
        }

        beginTest ("accent off: every beat has the same level");
        {
            ff::MetronomeDsp m;
            m.prepare (sr);
            m.bpm = 120.0;
            m.accentsEnabled = false;
            std::vector<float> pk;
            renderOnsets (m, sr, 4.0, 256, &pk);
            expect (pk.size() >= 7);
            for (auto v : pk) expect (std::abs (v - pk.front()) < 1.0e-4f, "uneven click level with accents off");
            m.accentsEnabled = true;
            m.reset();
            pk.clear();
            renderOnsets (m, sr, 2.0, 256, &pk);
            expect (pk.size() >= 2 && pk[0] > pk[1], "the accent should be louder than the beat");
        }
    }
};
static MetronomeTests metronomeTests;
