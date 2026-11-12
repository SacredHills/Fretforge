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
        beginTest ("tempo changes are steps, seconds and quarters convert both ways");
        {
            ff::TempoMap m;
            m.set (120.0, { { 8.0, 60.0 }, { 4.0, 240.0 } });   // unsorted on purpose
            expect (! m.isConstant());
            expectEquals (m.numSegments(), 3);
            expectWithinAbsoluteError (m.secondsAtQuarter (4.0), 2.0, 1e-9);        // 4 quarters at 120
            expectWithinAbsoluteError (m.secondsAtQuarter (8.0), 3.0, 1e-9);        // + 4 quarters at 240
            expectWithinAbsoluteError (m.secondsAtQuarter (10.0), 5.0, 1e-9);       // + 2 quarters at 60
            for (double q : { 0.0, 1.5, 4.0, 6.0, 8.0, 9.25, 20.0 })
                expectWithinAbsoluteError (m.quarterAtSeconds (m.secondsAtQuarter (q)), q, 1e-9);
            expectWithinAbsoluteError (m.bpmAtQuarter (5.0), 240.0, 1e-9);
            expectWithinAbsoluteError (m.bpmAtSeconds (4.0), 60.0, 1e-9);
        }
        beginTest ("changes at the start or duplicates are ignored");
        {
            ff::TempoMap m;
            m.set (100.0, { { 0.0, 200.0 }, { 4.0, 80.0 }, { 4.0, 90.0 } });
            expectEquals (m.numSegments(), 2);
            expectWithinAbsoluteError (m.baseBpm(), 100.0, 1e-9);
        }
    }
};
static TempoMapTests tempoMapTests;
}
