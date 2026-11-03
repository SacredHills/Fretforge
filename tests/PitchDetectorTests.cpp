#include <juce_core/juce_core.h>
#include "core/PitchDetector.h"
#include "core/MusicTheory.h"
#include <cmath>

namespace
{
    // Plucked-string-like test signal: harmonics with decaying amplitude, optional strong 2nd harmonic, noise.
    std::vector<float> pluck (double sr, double f0, int n, double secondHarmonic = 0.5, double noise = 0.0, int seed = 1)
    {
        std::vector<float> s ((size_t) n);
        juce::Random rnd (seed);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            double v = 0.0;
            for (int h = 1; h <= 8; ++h)
            {
                const double amp = (h == 2 ? secondHarmonic : 1.0 / h) * std::exp (-t * 1.5 * h);
                v += amp * std::sin (juce::MathConstants<double>::twoPi * f0 * h * t + h * 0.7);
            }
            s[(size_t) i] = (float) (0.3 * v + noise * (rnd.nextDouble() * 2 - 1));
        }
        return s;
    }

    double centsBetween (double a, double b) { return 1200.0 * std::log2 (a / b); }
}

struct PitchDetectorTests : juce::UnitTest
{
    PitchDetectorTests() : juce::UnitTest ("Pitch detector", "core") {}
    void runTest() override
    {
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            ff::PitchDetector pd;
            pd.prepare (sr);
            beginTest ("guitar & bass range @ " + juce::String (sr, 0) + " Hz");
            // B0 (5-string bass), E1, F#1 (8-string), E2, A2, G3, E4, 12th fret high E, 22nd fret
            for (int midi : { 23, 28, 30, 40, 45, 55, 64, 76, 86 })
            {
                const double f = ff::theory::midiToFrequency (midi);
                const auto sig = pluck (sr, f, pd.windowSize());
                const auto r = pd.detect (sig.data());
                expect (r.frequency > 0, "no pitch for " + ff::theory::midiNoteName (midi));
                const double err = std::abs (centsBetween (r.frequency, f));
                expect (err < 1.5, ff::theory::midiNoteName (midi) + " error " + juce::String (err, 3) + " cents");
                expect (r.clarity > 0.9, "low clarity " + juce::String (r.clarity));
            }
        }

        ff::PitchDetector pd;
        pd.prepare (48000.0);

        beginTest ("no octave error with dominant 2nd harmonic");
        {
            const double f = 82.4069;
            const auto sig = pluck (48000.0, f, pd.windowSize(), 2.0);
            const auto r = pd.detect (sig.data());
            expect (std::abs (centsBetween (r.frequency, f)) < 5.0, "got " + juce::String (r.frequency));
        }

        beginTest ("detuned string reports correct cents");
        {
            const double f = ff::theory::midiToFrequency (45.0 + 0.12); // A2 +12 cents
            const auto sig = pluck (48000.0, f, pd.windowSize());
            const auto n = ff::theory::nearestNote (pd.detect (sig.data()).frequency);
            expectEquals (n.midiNote, 45);
            expectWithinAbsoluteError (n.cents, 12.0, 1.0);
        }

        beginTest ("robust to moderate noise");
        {
            const double f = 196.0;
            const auto sig = pluck (48000.0, f, pd.windowSize(), 0.5, 0.05);
            const auto r = pd.detect (sig.data());
            expect (std::abs (centsBetween (r.frequency, f)) < 3.0);
        }

        beginTest ("white noise has low confidence; silence has none");
        {
            std::vector<float> noise ((size_t) pd.windowSize());
            juce::Random rnd (7);
            for (auto& s : noise) s = (float) (rnd.nextDouble() * 2 - 1) * 0.3f;
            expect (pd.detect (noise.data()).clarity < 0.6, "noise clarity too high");

            std::vector<float> silence ((size_t) pd.windowSize(), 0.0f);
            const auto r = pd.detect (silence.data());
            expectEquals (r.frequency, 0.0);
        }
    }
};
static PitchDetectorTests pitchDetectorTests;
