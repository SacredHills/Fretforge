#pragma once
#include "TabEditor.h"

namespace ff
{
/** Guitar Tabs section: track/tuning management, playback controls, entry palette and the editor. */
class TabPage : public juce::Component, private juce::ValueTree::Listener, private juce::AsyncUpdater, private juce::Timer
{
public:
    TabPage (ProjectDocument&, EngineHost&, juce::PropertiesFile&);
    ~TabPage() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void rebind();
    /** Called when the section becomes visible / hidden: switches the engine into tab playback. */
    void setActive (bool active);
    void togglePlay();
    bool isActive() const { return active; }

private:
    ProjectDocument& doc;
    EngineHost& host;
    juce::PropertiesFile& settings;
    juce::ValueTree root, score;
    bool active = false;
    double speed = 1.0;

    TabEditor editor;
    juce::Viewport viewport;

    // row 1
    juce::ComboBox trackCombo, speedCombo;
    juce::TextButton addTrackButton { "+ Track" }, tuningButton { "Tuning..." }, trackMenuButton { "Track..." };
    juce::Slider tempoSlider;
    juce::Label tempoLabel, speedLabel;
    juce::TextButton playButton { "Play" }, loopButton { "Loop" }, clickButton { "Click" }, countButton { "Count-in" };
    juce::TextButton importButton { "Import..." }, exportButton { "Export MIDI..." };
    // row 2: entry palette
    juce::OwnedArray<juce::TextButton> durButtons;
    juce::TextButton dotButton { "." }, tripletButton { "3" }, restButton { "Rest" };
    juce::OwnedArray<juce::TextButton> techButtons;
    juce::TextButton measAddButton { "+ Bar" }, measDelButton { "- Bar" }, transDownButton { "-1" }, transUpButton { "+1" };
    juce::ComboBox sigCombo;
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
    bool updating = false;

    void refreshTracks();
    void refreshPalette();
    void pushEngineOverrides();
    void restoreEngine();
    void rebuildSequence();
    void showAddTrackDialog();
    void showTuningDialog();
    void showTrackMenu();
    void importDialog();
    void exportDialog();
    void message (const juce::String& title, const juce::String& text);
    void ensureCursorVisible();

    void handleAsyncUpdate() override { rebuildSequence(); }
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { structureChanged(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { structureChanged(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { structureChanged(); }
    void structureChanged();
};
} // namespace ff
