#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../EngineHost.h"
#include "../ProjectDocument.h"
#include "../Widgets.h"
#include "core/TapTempo.h"

namespace ff
{
/** Clickable beat lights: shows the playing beat and edits accent / normal / mute per beat. */
class BeatStrip : public juce::Component
{
public:
    std::function<void (int beat)> onBeatClicked;
    int numBeats = 4, currentBeat = -1;
    bool currentMuted = false;
    juce::String levels;      // one char per beat: '2' accent, '1' normal, '0' mute
    float flash = 0.0f;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    juce::Rectangle<float> cell (int i) const;
};

/** Dedicated practice metronome with tempo trainer, rhythm trainer and practice timer. */
class MetronomePanel : public juce::Component, private juce::Timer, private juce::ValueTree::Listener
{
public:
    MetronomePanel (EngineHost&, ProjectDocument&, juce::PropertiesFile&);
    ~MetronomePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void rebind();
    void togglePractice();

private:
    EngineHost& host;
    ProjectDocument& doc;
    juce::PropertiesFile& settings;
    juce::ValueTree transportNode, metroNode;

    juce::Slider bpmSlider, swingSlider, volumeSlider, rampStep, rampBars, rampTarget, missingSlider;
    juce::TextButton minusButton { "-" }, plusButton { "+" }, tapButton { "TAP" }, startButton { "START" };
    juce::ComboBox sigNum, sigDen, subdivision, sound;
    juce::ToggleButton rampToggle { "Increase tempo automatically" };
    juce::TextButton timerStart { "Start" }, timerReset { "Reset" };
    juce::OwnedArray<juce::Label> labels;
    BeatStrip beats;
    TapTempo tap;

    juce::Rectangle<int> tempoBox, meterBox, soundBox, trainerBox, rhythmBox, timerBox, bpmReadout;
    double practiceSeconds = 0.0;
    bool timerRunning = false;
    juce::uint32 lastTick = 0, lastBeatCounter = 0;
    bool updating = false;

    juce::Label& addLabel (const juce::String& text);
    void timerCallback() override;
    void refresh();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refresh(); }
    void cycleBeat (int beat);
    void pushTrainerSettings();
};
} // namespace ff
