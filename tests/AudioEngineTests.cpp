#include <juce_core/juce_core.h>
#include "engine/AudioEngine.h"
#include <cmath>

struct AudioEngineTests : juce::UnitTest
{
    AudioEngineTests() : juce::UnitTest ("Audio engine", "engine") {}

    void runBlocks (ff::AudioEngine& e, int blocks, int n, std::vector<float>& inBuf, std::vector<float>& l, std::vector<float>& r)
    {
        for (int b = 0; b < blocks; ++b)
        {
            const float* ins[1] = { inBuf.data() };
            float* outs[2] = { l.data(), r.data() };
            e.process (ins, 1, outs, 2, n);
        }
    }

    void runTest() override
    {
        const int n = 256;
        std::vector<float> in ((size_t) n), l ((size_t) n), r ((size_t) n);
        for (int i = 0; i < n; ++i) in[(size_t) i] = 0.5f * std::sin (i * 0.1f);

        beginTest ("silent output when idle, input metered and tapped");
        {
            ff::AudioEngine e;
            e.prepare (48000.0, n);
            runBlocks (e, 10, n, in, l, r);
            float mx = 0.0f;
            for (auto s : l) mx = juce::jmax (mx, std::abs (s));
            expectEquals (mx, 0.0f, "no fake output");
            expectWithinAbsoluteError (e.inputMeter.takePeak(), 0.5f, 0.01f);
            expect (! e.inputMeter.clipped.load());
            expectEquals (e.analysisFifo().numReady(), 10 * n);
            expect (! e.isPlaying());
        }

        beginTest ("transport advances sample and PPQ position only while playing");
        {
            ff::AudioEngine e;
            e.prepare (48000.0, n);
            e.setTempo (120.0);
            runBlocks (e, 4, n, in, l, r);
            expectEquals (e.getPositionSamples(), (juce::int64) 0);
            e.play();
            runBlocks (e, 375, n, in, l, r); // 96000 samples = 2 s = 4 quarters at 120
            expect (e.isPlaying());
            expectEquals (e.getPositionSamples(), (juce::int64) 96000);
            expectWithinAbsoluteError (e.getPositionPpq(), 4.0, 1e-9);
            e.stop();
            runBlocks (e, 2, n, in, l, r);
            expectEquals (e.getPositionSamples(), (juce::int64) 96000, "stop keeps position");
            e.returnToZero();
            runBlocks (e, 1, n, in, l, r);
            expectEquals (e.getPositionSamples(), (juce::int64) 0);
        }

        beginTest ("click only when enabled; master gain applied; clip detection");
        {
            ff::AudioEngine e;
            e.prepare (48000.0, n);
            e.play();
            runBlocks (e, 20, n, in, l, r);
            expectEquals (e.outputMeterL.takePeak(), 0.0f, "click disabled -> silence");

            e.returnToZero();
            e.clickWhilePlaying = true;
            e.masterGain = 0.5f;
            runBlocks (e, 1, n, in, l, r);       // relocate + gain ramp begins
            runBlocks (e, 200, n, in, l, r);
            e.outputMeterL.takePeak();
            e.returnToZero();
            runBlocks (e, 4, n, in, l, r);       // beat 1 accent at gain 0.5 * volume 0.8
            const float pk = e.outputMeterL.takePeak();
            expect (pk > 0.3f && pk < 0.45f, "peak " + juce::String (pk));

            std::vector<float> hot ((size_t) n, 1.0f);
            runBlocks (e, 1, n, hot, l, r);
            expect (e.inputMeter.clipped.load());
        }

        beginTest ("practice metronome runs with transport stopped");
        {
            ff::AudioEngine e;
            e.prepare (44100.0, n);
            e.practiceMetronome = true;
            runBlocks (e, 4, n, in, l, r);
            expect (e.outputMeterL.takePeak() > 0.1f);
            expectEquals (e.getPositionSamples(), (juce::int64) 0);
        }

        beginTest ("analysis input channel out of range is ignored safely");
        {
            ff::AudioEngine e;
            e.prepare (48000.0, n);
            e.analysisInputChannel = 5;
            runBlocks (e, 3, n, in, l, r);
            expectEquals (e.analysisFifo().numReady(), 0);
            float* outs[2] = { l.data(), nullptr };
            e.process (nullptr, 0, outs, 2, n); // null channel pointers must not crash
        }
    }
};
static AudioEngineTests audioEngineTests;
