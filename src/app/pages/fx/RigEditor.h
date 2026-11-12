#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "../../ProjectDocument.h"
#include "../../EngineHost.h"
#include "../../SessionController.h"
#include "../../Widgets.h"
#include "core/RigModel.h"
#include "FileBrowserList.h"
#include "../../plugins/PluginManager.h"

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
    std::array<juce::String, rig::numPluginSlots> pluginText;
    bool canAddPlugin = false;         // shows a "+ Plugin" tile at the end
    std::function<void (int block)> onSelect, onToggle;
    std::function<void (int block, int newIndex)> onMove;   // newIndex: position in the full block order
    std::function<void()> onAddPlugin;
    std::function<void (int block)> onContext;   // right click on a tile

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    juce::ValueTree tree;
    int dragBlock = -1, dropIndex = -1;
    std::vector<int> visible() const;          // blocks shown, in order (empty plugin slots hidden)
    int numTiles() const { return (int) visible().size() + (canAddPlugin ? 1 : 0); }
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

/** Rig editor (NAM capture or built-in amp, cabinet IR, pedals, presets). With an empty track id it edits the live
    practice rig (the Effects & Amps page); with a track id it edits that track's amp (shown in its own window). */
class RigEditor : public juce::Component, private juce::ValueTree::Listener, private juce::Timer
{
public:
    RigEditor (ProjectDocument&, EngineHost&, SessionController&, juce::PropertiesFile&, const juce::String& trackId = {});
    std::function<void()> onTrackGone;   // the edited track was deleted (or the project replaced)
    juce::String windowTitle() const;
    ~RigEditor() override;
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
    bool active = false;
    const bool trackMode;

    // top row
    juce::ComboBox inputCombo, presetCombo;
    juce::Label titleLabel;
    juce::TextButton onButton, monitorButton, addRigButton { "Add NAM amp to this track" };
    juce::TextButton savePresetButton { "Save preset..." }, deletePresetButton { "Delete" }, resetButton { "Reset" };
    juce::TextButton copyButton { "Copy" }, pasteButton { "Paste" };
    juce::TextButton delayStereoButton { "Stereo repeats" };
    juce::Label targetHint, statusLabel, inLabel, outLabel;
    RigMeter inMeter, outMeter;
    ParamKnob inKnob, outKnob;

    // chain + selected block
    ChainView chain;
    juce::Label blockTitle, blockHelp;
    juce::TextButton blockOnButton, moveLeftButton { "<" }, moveRightButton { ">" };
    juce::OwnedArray<ParamKnob> knobs;

    // amp: model
    juce::TextButton loadModelButton { "Load model..." }, clearModelButton { "Use built-in amp" };
    juce::TextButton normaliseButton { "Normalise loudness" }, calibrateButton { "Calibrate input" };
    juce::Label modelLabel, modelInfo;
    FileBrowserList models { settings, "namFolder", "*.nam", "models" };
    FileBrowserList irs { settings, "irFolder", "*.wav;*.aif;*.aiff;*.flac", "impulse responses" };
    // plugin slots
    juce::TextButton pluginOpenButton { "Open plugin window" }, pluginReplaceButton { "Replace..." }, pluginRemoveButton { "Remove" },
                     pluginManagerButton { "Manage plugins..." };
    juce::Label pluginInfo;
    void showAddPluginMenu (int slot);
    // cab: impulse response
    juce::TextButton loadIrButton { "Load..." }, clearIrButton { "Remove" };
    juce::TextButton loadIr2Button { "Load..." }, clearIr2Button { "Remove" };
    juce::TextButton phase1Button { "Phase" }, phase2Button { "Phase" };
    juce::Label cab1Label, cab2Label;
    juce::Label irLabel;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::UndoManager* um() { return targetTrackId.isEmpty() ? nullptr : &doc.undo(); }

    void rebuildInputs();
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
    void setModelFile (const juce::File&);
    void setIrFile (const juce::File&, bool second = false);
    void copyRig();
    void pasteRig();
    void fileMenu (const juce::File&, bool cab);
    void blockMenu (int block);
    void setOnTarget (const juce::Identifier&, const juce::var&, const juce::String& undoName);

    std::function<void (bool cab)> onBrowseTone3000;   // set by the app: opens the TONE3000 browser
    PluginManager* plugins = nullptr;
public:
    /** Loads a downloaded file into this rig (amp model or cabinet IR). */
    void loadDownloaded (const juce::File& f, bool cab) { models.rescan(); irs.rescan(); if (cab) setIrFile (f); else setModelFile (f); }
    void setTone3000Handler (std::function<void (bool cab)> fn) { onBrowseTone3000 = std::move (fn); refreshAll(); }
    void setPluginManager (PluginManager* pm) { plugins = pm; refreshAll(); }
private:

    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
};
} // namespace ff
