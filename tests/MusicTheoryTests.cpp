#include <juce_core/juce_core.h>
#include "core/MusicTheory.h"
#include "core/TapTempo.h"

using namespace ff::theory;

struct MusicTheoryTests : juce::UnitTest
{
    MusicTheoryTests() : juce::UnitTest ("Music theory", "core") {}
    void runTest() override
    {
        beginTest ("note <-> frequency");
        expectWithinAbsoluteError (midiToFrequency (69), 440.0, 1e-9);
        expectWithinAbsoluteError (midiToFrequency (40), 82.4069, 1e-3);   // low E
        expectWithinAbsoluteError (frequencyToMidi (440.0), 69.0, 1e-9);
        expectWithinAbsoluteError (midiToFrequency (69, 432.0), 432.0, 1e-9);

        beginTest ("note names");
        expectEquals (midiNoteName (40), juce::String ("E2"));
        expectEquals (midiNoteName (61, true), juce::String ("Db4"));
        expectEquals (parseNoteName ("E2"), 40);
        expectEquals (parseNoteName ("Bb1"), 34);
        expectEquals (parseNoteName ("f#3"), 54);
        expectEquals (parseNoteName ("B0"), 23);
        expectEquals (parseNoteName ("H2"), -1);
        expectEquals (parseNoteName ("E"), -1);

        beginTest ("nearest note & cents");
        auto n = nearestNote (midiToFrequency (45.2));
        expectEquals (n.midiNote, 45);
        expectWithinAbsoluteError (n.cents, 20.0, 1e-6);
        n = nearestNote (midiToFrequency (44.7));
        expectEquals (n.midiNote, 45);
        expectWithinAbsoluteError (n.cents, -30.0, 1e-6);

        beginTest ("tuning presets are well-formed");
        for (auto& t : tuningPresets())
        {
            expect (t.strings.size() >= 4 && t.strings.size() <= 8, t.name);
            for (size_t i = 0; i < t.strings.size(); ++i)
            {
                expect (t.strings[i] > 0, t.name + " has an unparsable string");
                if (i > 0) expect (t.strings[i] > t.strings[i - 1], t.name + " strings must ascend");
            }
        }
        const auto& std6 = tuningPresets().front();
        expectEquals (nearestStringIndex (std6, 110.0), 1);   // A2
        expectEquals (nearestStringIndex (std6, 330.0), 5);   // E4

        beginTest ("tap tempo");
        ff::TapTempo tap;
        expectEquals (tap.tap (0), 0.0);
        tap.tap (500); tap.tap (1000);
        expectWithinAbsoluteError (tap.tap (1500), 120.0, 1e-6);
        expectEquals (tap.tap (6000), 0.0); // long pause resets
        expectWithinAbsoluteError (tap.tap (6400), 150.0, 1e-6);
    }
};
static MusicTheoryTests musicTheoryTests;
