#include <juce_core/juce_core.h>
#include "core/GuitarPro.h"
#include "core/ProjectFile.h"
#include "core/DrumModel.h"
// Optional: FF_PERF_GP=file.gp5 ff_tests  -> prints how long serialising / autosaving a project with that score takes
struct PerfProbe : juce::UnitTest
{
    PerfProbe() : juce::UnitTest ("Perf probe", "perf") {}
    void runTest() override
    {
        const auto f = juce::SystemStats::getEnvironmentVariable ("FF_PERF_GP", {});
        beginTest ("perf");
        if (f.isEmpty()) return;
        auto project = ff::project::createDefault ("Perf");
        auto score = ff::tab::getOrCreateScore (project);
        auto r = ff::tab::gp::importFile (score, juce::File (f), true, nullptr);
        int nodes = 0;
        std::function<void (const juce::ValueTree&)> count = [&] (const juce::ValueTree& t) { ++nodes; for (auto c : t) count (c); };
        count (project);
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto copy = project.createCopy();
        auto t1 = juce::Time::getMillisecondCounterHiRes();
        auto text = ff::project::serialise (copy);
        auto t2 = juce::Time::getMillisecondCounterHiRes();
        auto dir = juce::File::createTempFile ("perf");
        ff::project::Bundle::createStructure (dir);
        ff::project::Bundle::writeAutosave (project, dir);
        auto t3 = juce::Time::getMillisecondCounterHiRes();
        logMessage ("nodes " + juce::String (nodes) + "  text " + juce::String (text.length() / 1024) + " KB  copy " + juce::String (t1 - t0, 1)
                    + " ms  serialise " + juce::String (t2 - t1, 1) + " ms  autosave " + juce::String (t3 - t2, 1) + " ms");
        dir.deleteRecursively();
        expect (r.result.wasOk());
    }
};
static PerfProbe perfProbe;
