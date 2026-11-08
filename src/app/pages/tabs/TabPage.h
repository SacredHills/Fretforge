#pragma once
#include "TabEditor.h"
#include "../../Widgets.h"

namespace ff
{
/** Guitar Tabs section: track/tuning management, playback controls, entry palette and the editor. */
class TabPage : public juce::Component, private juce::ValueTree::Listener, private juce::AsyncUpdater, private juce::Timer,
                private juce::MidiInputCallback, public juce::FileDragAndDropTarget
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
    /** Imports a Guitar Pro, MusicXML, MIDI or ASCII tab file (asks whether to replace a non-empty score). */
    void importFile (const juce::File&);
    static bool canImport (const juce::File&);
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto& f : files) if (canImport (juce::File (f))) return true;
        return false;
    }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        for (auto& f : files) if (canImport (juce::File (f))) { importFile (juce::File (f)); return; }
    }

private:
    ProjectDocument& doc;
    EngineHost& host;
    juce::PropertiesFile& settings;
    juce::ValueTree root, score;
    bool active = false;
    double speed = 1.0;
    tab::TempoMap playMap;

    TabEditor editor;
    juce::Viewport viewport;

    // row 1
    juce::ComboBox trackCombo, speedCombo;
    juce::TextButton addTrackButton { "+ Track" }, tuningButton { "Tuning..." }, trackMenuButton { "Track..." };
    juce::Slider tempoSlider;
    juce::Label tempoLabel, speedLabel;
    PagePlayButton playButton { "Play tab", palette::accent };
    juce::TextButton loopButton { "Loop" }, loopSongButton { "Loop song" }, clickButton { "Click" }, countButton { "Count-in" };
    juce::TextButton importButton { "Import..." }, exportButton { "Export..." };
    juce::TextButton notationButton { "Notation" }, midiButton { "MIDI In" }, drumsButton { "Drums" };
    // row 2: entry palette
    juce::OwnedArray<NoteValueButton> durButtons;
    juce::TextButton dotButton { "." }, tripletButton { "3" }, restButton { "Rest" };
    juce::OwnedArray<juce::TextButton> techButtons;   // the common techniques; all of them are in techMenuButton
    juce::TextButton techMenuButton { "All techniques..." };
    struct Tech { juce::String label, name, key; std::function<void (TabEditor&)> fn; };
    std::vector<Tech> techs;
    static constexpr int numVisibleTechs = 5;
    void showTechMenu();
    std::array<bool, 16> techActive {};
    juce::TextButton transDownButton { "-1" }, transUpButton { "+1" };
    juce::ComboBox sigCombo;
    juce::TextButton barButton { "Bar..." }, textButton { "Text..." };
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
    void exportAs (int kind);  // 1 MIDI, 2 MusicXML, 3 PDF, 4 print via PDF
    void applyImport (const juce::File&, bool replace);
    void showBarDialog();
    void showBarMenu();
    void showBeatTextDialog();
    void setMidiInput (bool enabled);
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void midiNoteOn (int note);
    std::vector<int> midiChord;
    juce::uint32 lastMidiMs = 0;
    bool midiEnabled = false;
    void message (const juce::String& title, const juce::String& text);
    void ensureCursorVisible();

    void handleAsyncUpdate() override { rebuildSequence(); pushEngineOverrides(); }
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { structureChanged(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { structureChanged(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { structureChanged(); }
    void structureChanged();
};
} // namespace ff
