#pragma once
#include "TunerPanel.h"
#include "MetronomePanel.h"

namespace ff
{
/** Guitar toolbox: tuner and metronome (further tools arrive with Milestone 7). */
class ToolsPage : public juce::Component
{
public:
    ToolsPage (EngineHost&, ProjectDocument&, juce::PropertiesFile&);
    void resized() override { tabs.setBounds (getLocalBounds()); }
    void showTuner() { tabs.setCurrentTabIndex (0); }
    void showMetronome() { tabs.setCurrentTabIndex (1); }

    TunerPanel tuner;
    MetronomePanel metronome;

private:
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
};
} // namespace ff
