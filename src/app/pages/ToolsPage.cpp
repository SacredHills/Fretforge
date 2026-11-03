#include "ToolsPage.h"

namespace ff
{
ToolsPage::ToolsPage (EngineHost& h, ProjectDocument& d, juce::PropertiesFile& s)
    : tuner (h, s), metronome (h, d, s)
{
    tabs.setTabBarDepth (34);
    tabs.setOutline (0);
    tabs.addTab ("Tuner", palette::bg, &tuner, false);
    tabs.addTab ("Metronome", palette::bg, &metronome, false);
    addAndMakeVisible (tabs);
}
} // namespace ff
