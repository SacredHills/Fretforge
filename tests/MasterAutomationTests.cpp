#include <juce_core/juce_core.h>
#include "engine/PlaybackGraph.h"
#include "engine/GraphMixer.h"

namespace
{
class MasterAutomationTests : public juce::UnitTest
{
public:
    MasterAutomationTests() : juce::UnitTest ("Master automation", "Mixer") {}
    void runTest() override
    {
        beginTest ("the master volume lane gives a linear gain, held outside its points");
        {
            ff::PlaybackGraph g;
            ff::PlayTrack::Lane l;
            l.target = ff::PlayTrack::Lane::volume;
            l.points = { { 1.0, 0.0f }, { 3.0, -20.0f } };
            g.masterLanes.push_back (l);
            expectWithinAbsoluteError (ff::applyMasterLanes (g, 0.0), 1.0f, 1.0e-5f);                      // held before the first point
            expectWithinAbsoluteError (ff::applyMasterLanes (g, 2.0), juce::Decibels::decibelsToGain (-10.0f), 1.0e-4f);
            expectWithinAbsoluteError (ff::applyMasterLanes (g, 9.0), juce::Decibels::decibelsToGain (-20.0f), 1.0e-4f);
        }
        beginTest ("a graph without lanes leaves the mix alone");
        {
            ff::PlaybackGraph g;
            expectWithinAbsoluteError (ff::applyMasterLanes (g, 1.0), 1.0f, 1.0e-9f);
        }
    }
};
static MasterAutomationTests masterAutomationTests;
}
