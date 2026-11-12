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
        beginTest ("a ramp over quarters, both ways");
        {
            ff::TempoMap m;
            m.set (120.0, { { 8.0, 180.0 }, { 4.0, 120.0 } });   // flat 120 to quarter 4, ramp to 180 at quarter 8 (unsorted)
            expectEquals (m.numPoints(), 3);
            expectWithinAbsoluteError (m.secondsAtQuarter (4.0), 2.0, 1e-9);
            expectWithinAbsoluteError (m.bpmAtQuarter (6.0), 150.0, 1e-9);
            expectWithinAbsoluteError (m.secondsAtQuarter (8.0), 2.0 + 60.0 * std::log (1.5) / 15.0, 1e-9);
            expectWithinAbsoluteError (m.bpmAtQuarter (12.0), 180.0, 1e-9);
            for (double t : { 0.0, 1.5, 2.0, 3.3, 4.0, 5.0, 9.7 })
                expectWithinAbsoluteError (m.secondsAtQuarter (m.quarterAtSeconds (t)), t, 1e-9);
        }
        beginTest ("points stack for an instant change, and stay on the grid");
        {
            ff::TempoMap m;
            m.set (120.0, { { 4.0, 120.0 }, { 4.0, 60.0 } });   // jump to 60 bpm at quarter 4
            expectEquals (m.numPoints(), 3);
            expectWithinAbsoluteError (m.secondsAtQuarter (4.0), 2.0, 1e-9);
            expectWithinAbsoluteError (m.secondsAtQuarter (5.0), 3.0, 1e-9);
            expectWithinAbsoluteError (m.bpmAtQuarter (3.9), 120.0, 1e-9);
            expectWithinAbsoluteError (m.bpmAtQuarter (4.0), 60.0, 1e-9);
            expectWithinAbsoluteError (m.quarterAtSeconds (3.0), 5.0, 1e-9);
            m.set (100.0, { { 0.0, 200.0 }, { 4.0, 80.0 } });    // a point at the start is ignored
            expectEquals (m.numPoints(), 2);
            expectWithinAbsoluteError (m.baseBpm(), 100.0, 1e-9);
        }
    }
};
static TempoMapTests tempoMapTests;
}
