#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "../../ProjectDocument.h"
#include "../../EngineHost.h"
#include "../../SessionController.h"
#include "../../Widgets.h"
#include "core/RigModel.h"

namespace ff
{
/** A knob bound to one rig parameter of a RIG node. */
class ParamKnob : public juce::Component
{
public:
    ParamKnob();
    void bind (juce::ValueTree rigTree, int param, juce::UndoManager* um);
    void refresh();
    int param() const { return p; }
    void resized() override;
private:
    juce::Slider slider;
    juce::Label name, value;
    juce::ValueTree tree;
    int p = 0;
    juce::UndoManager* undo = nullptr;
    bool updating = false;
};

/** The signal chain: one tile per block in processing order. Click a tile to edit it, click its lamp to switch it
    on/off, drag it sideways to move it in the chain. */
class ChainView : public juce::Component
{
public:
    void setRig (juce::ValueTree rigTree) { tree = rigTree; repaint(); }
    int selected = rig::amp;
    juce::String ampText, cabText;     // subtitles of the amp and cabinet tiles
    std::function<void (int block)> onSelect, onToggle;
    std::function<void (int block, int newIndex)> onMove;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    juce::ValueTree tree;
    int dragBlock = -1, dropIndex = -1;
    juce::Rectangle<float> tileRect (int index) const;
    int indexAt (float x) const;
    juce::Rectangle<float> lampRect (int index) const { return tileRect (index).removeFromTop (26.0f).removeFromRight (26.0f).reduced (7.0f); }
};

/** Simple horizontal level bar fed from a rig's peak atomics. */
class RigMeter : public juce::Component
{
public:
    void push (float peak) { const float db = gainToDb (peak); level = db > level ? db : juce::jmax (db, level - 2.0f); repaint(); }
    void paint (juce::Graphics&) override;
private:
    float level = -90.0f;
};

/** Effects & Amps: edits the live practice rig or a track's rig insert (NAM capture or built-in amp, cabinet IR,
    pedals, presets). */
class EffectsPage : public juce::Component, private juce::ValueTree::Listener, private juce::Timer, private juce::ListBoxModel
{
public:
    EffectsPage (ProjectDocument&, EngineHost&, SessionController&, juce::PropertiesFile&);
    ~EffectsPage() override;
    void rebind();
    void setActive (bool);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ProjectDocument& doc;
    EngineHost& host;
    SessionController& session;
    juce::PropertiesFile& settings;
    juce::ValueTree root, liveTree;   // kept as members: ValueTree listeners belong to the ValueTree object
    juce::String targetTrackId;          // empty = the live rig
    juce::ValueTree target;              // the RIG node being edited (invalid: the track has no rig yet)
    bool active = false, chosenByUser = false;

    // top row
    juce::ComboBox targetCombo, inputCombo, presetCombo;
    juce::TextButton onButton, monitorButton, addRigButton { "Add amp rig to this track" };
    juce::TextButton savePresetButton { "Save preset..." }, deletePresetButton { "Delete" }, resetButton { "Reset" };
    juce::Label targetHint, statusLabel, inLabel, outLabel;
    RigMeter inMeter, outMeter;
    ParamKnob inKnob, outKnob;

    // chain + selected block
    ChainView chain;
    juce::Label blockTitle, blockHelp;
    juce::TextButton blockOnButton, moveLeftButton { "<" }, moveRightButton { ">" };
    juce::OwnedArray<ParamKnob> knobs;

    // amp: model
    juce::TextButton loadModelButton { "Load model..." }, clearModelButton { "Use built-in amp" }, folderButton { "Model folder..." };
    juce::TextButton normaliseButton { "Normalise loudness" };
    juce::Label modelLabel, modelInfo, browserTitle;
    juce::TextEditor searchBox;
    juce::ListBox modelList { "models", this };
    juce::Array<juce::File> allModels, shownModels;
    // cab: impulse response
    juce::TextButton loadIrButton { "Load IR..." }, clearIrButton { "Use built-in cabinet" };
    juce::Label irLabel;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::UndoManager* um() { return targetTrackId.isEmpty() ? nullptr : &doc.undo(); }

    void rebuildTargets();
    void selectTarget (const juce::String& trackId);
    void selectBlock (int block);
    void refreshAll();
    void refreshStatus();
    void layoutDetail (juce::Rectangle<int> area);
    void rebuildPresets();
    void applyPresetId (int id);
    void savePreset();
    void deletePreset();
    juce::File presetFolder() const;
    void chooseModelFolder();
    void scanModels();
    void filterModels();
    void setModelFile (const juce::File&);
    void setIrFile (const juce::File&);
    void setOnTarget (const juce::Identifier&, const juce::var&, const juce::String& undoName);

    // ListBoxModel
    int getNumRows() override { return shownModels.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
};
} // namespace ff
