#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../EngineHost.h"
#include "../Widgets.h"
#include "core/MusicTheory.h"

namespace ff
{
/** Real-time chromatic / instrument tuner driven by the TunerAnalyzer thread. */
class TunerPanel : public juce::Component, private juce::Timer
{
public:
    TunerPanel (EngineHost&, juce::PropertiesFile&);
    ~TunerPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override { updateActive(); }
    void parentHierarchyChanged() override { updateActive(); }

    void refreshInputs();

private:
    EngineHost& host;
    juce::PropertiesFile& settings;

    juce::Label inputLabel, refLabel, tuningLabel, modeLabel, customLabel, statusLabel;
    juce::ComboBox inputCombo, tuningCombo, modeCombo;
    juce::Slider refSlider;
    juce::TextEditor customEditor;
    MeterView meter;

    theory::Tuning customTuning;
    double displayCents = 0.0, lastFreq = 0.0, clarity = 0.0;
    int lastNote = -1, targetString = -1;
    juce::uint32 holdUntil = 0;

    void timerCallback() override;
    void updateActive();
    const theory::Tuning& currentTuning() const;
    bool instrumentMode() const { return modeCombo.getSelectedId() == 2; }
    void applyCustomTuning();
    juce::Rectangle<int> displayArea() const;
};
} // namespace ff
