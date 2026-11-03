#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "../EngineHost.h"
#include "../ProjectDocument.h"
#include "../Widgets.h"

namespace ff
{
/** Audio/MIDI device configuration and application preferences. */
class SettingsPage : public juce::Component
{
public:
    SettingsPage (EngineHost&, ProjectDocument&, juce::PropertiesFile&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setWelcomeVisible (bool shouldShow) { welcome = shouldShow; resized(); repaint(); }

private:
    EngineHost& host;
    ProjectDocument& doc;
    juce::PropertiesFile& settings;
    juce::AudioDeviceSelectorComponent selector;
    juce::Viewport selectorViewport;
    juce::Label autosaveLabel, backupsLabel, logLabel;
    juce::ComboBox autosaveCombo;
    juce::Slider backupsSlider;
    juce::TextButton revealLogs { "Show log folder" };
    juce::Rectangle<int> audioBox, appBox, welcomeBox;
    bool welcome = false;
};
} // namespace ff
