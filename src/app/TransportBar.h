#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ProjectDocument.h"
#include "EngineHost.h"
#include "Widgets.h"
#include "core/TapTempo.h"

namespace ff
{
/** Button drawing a vector icon (play, stop, return-to-zero, metronome). */
class IconButton : public juce::Button
{
public:
    enum class Icon { play, stop, toStart, metronome, record, loop };
    IconButton (const juce::String& name, Icon i) : juce::Button (name), icon (i) {}
    void setIcon (Icon i) { icon = i; repaint(); }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
private:
    Icon icon;
};

/** Always-visible transport: locate/play, click, position, tempo, meter, tap,
    real-time meters, CPU and device status. */
class TransportBar : public juce::Component, private juce::Timer, private juce::ValueTree::Listener
{
public:
    TransportBar (EngineHost&, ProjectDocument&);
    ~TransportBar() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    void rebind();              // after project replaced
    void tapTempo();
    void toggleClick();
    void toggleLoop();
    void togglePunch();
    std::function<void()> onRecord;

private:
    EngineHost& host;
    ProjectDocument& doc;
    juce::ValueTree transportNode, metroNode;

    IconButton rtzButton { "Return to start", IconButton::Icon::toStart };
    IconButton playButton { "Play / Stop", IconButton::Icon::play };
    IconButton clickButton { "Metronome click", IconButton::Icon::metronome };
    IconButton recordButton { "Record", IconButton::Icon::record };
    IconButton loopButton { "Loop", IconButton::Icon::loop };
    juce::TextButton punchButton { "PUNCH" }, countInButton { "COUNT-IN" };
    int recordBlink = 0;
    juce::Label barsLabel, timeLabel, bpmCaption, sigCaption;
    juce::Slider tempoSlider;
    juce::ComboBox sigNum, sigDen;
    juce::TextButton tapButton { "TAP" };
    MeterView inMeter, outL, outR;
    juce::Label inCaption, outCaption, cpuLabel;
    TapTempo tap;
    juce::uint32 lastMidi = 0;
    int midiFlash = 0;
    bool updating = false;

    void timerCallback() override;
    void refreshFromProject();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refreshFromProject(); }
};
} // namespace ff
