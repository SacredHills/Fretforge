#include <juce_core/juce_core.h>
#include "core/TempoMap.h"

namespace
{
class TempoMapTests : public juce::UnitTest
{
public:
    TempoMapTests() : juce::UnitTest ("Tempo map", "Timeline") {}
    void runTest() override
    {
        beginTest ("constant tempo");
        {
            ff::TempoMap m;
            m.set (120.0, {});
            expect (m.isConstant());
            expectWithinAbsoluteError (m.secondsAtQuarter (4.0), 2.0, 1e-9);
            expectWithinAbsoluteError (m.quarterAtSeconds (3.0), 6.0, 1e-9);
        }
        beginTest ("a ramp integrates the tempo, both ways");
        {
            ff::TempoMap m;
            m.set (120.0, { { 10.0, 180.0 }, { 4.0, 120.0 } });   // flat 120 until 4 s, then a ramp to 180 at 10 s (unsorted input)
            expect (! m.isConstant());
            expectEquals (m.numPoints(), 3);
            expectWithinAbsoluteError (m.quarterAtSeconds (4.0), 8.0, 1e-9);
            expectWithinAbsoluteError (m.quarterAtSeconds (10.0), 8.0 + 6.0 * 150.0 / 60.0, 1e-9);   // average 150 bpm over 6 s
            expectWithinAbsoluteError (m.quarterAtSeconds (12.0), 8.0 + 15.0 + 2.0 * 3.0, 1e-9);     // then 180 bpm
            expectWithinAbsoluteError (m.bpmAtSeconds (7.0), 150.0, 1e-9);
            for (double t : { 0.0, 1.5, 4.0, 5.3, 7.0, 10.0, 13.7 })
                expectWithinAbsoluteError (m.secondsAtQuarter (m.quarterAtSeconds (t)), t, 1e-9);
        }
        beginTest ("points at the start or on top of each other are ignored");
        {
            ff::TempoMap m;
            m.set (100.0, { { 0.0, 200.0 }, { 4.0, 80.0 }, { 4.0, 90.0 } });
            expectEquals (m.numPoints(), 2);
            expectWithinAbsoluteError (m.baseBpm(), 100.0, 1e-9);
        }
    }
};
static TempoMapTests tempoMapTests;
}
