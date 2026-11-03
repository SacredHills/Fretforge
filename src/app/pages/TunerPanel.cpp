#include "TunerPanel.h"
#include <cmath>

namespace ff
{
using namespace theory;
static constexpr int customTuningId = 1000;

TunerPanel::TunerPanel (EngineHost& h, juce::PropertiesFile& s)
    : host (h), settings (s), meter (h.engine.inputMeter, true)
{
    inputLabel.setText ("Input", juce::dontSendNotification);
    refLabel.setText ("Reference A4", juce::dontSendNotification);
    tuningLabel.setText ("Tuning", juce::dontSendNotification);
    modeLabel.setText ("Mode", juce::dontSendNotification);
    customLabel.setText ("Custom notes (low to high)", juce::dontSendNotification);
    for (auto* l : { &inputLabel, &refLabel, &tuningLabel, &modeLabel, &customLabel })
    {
        styleLabel (*l, 12.0f, palette::textDim, true);
        addAndMakeVisible (l);
    }
    styleLabel (statusLabel, 13.0f, palette::warn);
    statusLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (statusLabel);

    inputCombo.onChange = [this] {
        const int idx = inputCombo.getSelectedId() - 1;
        if (idx >= 0)
        {
            host.engine.analysisInputChannel = idx;
            settings.setValue ("tunerInput", idx);
        }
    };
    addAndMakeVisible (inputCombo);

    refSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    refSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
    refSlider.setRange (415.0, 466.0, 0.5);
    refSlider.setTextValueSuffix (" Hz");
    refSlider.setValue (settings.getDoubleValue ("tunerA4", 440.0), juce::dontSendNotification);
    refSlider.setDoubleClickReturnValue (true, 440.0);
    refSlider.onValueChange = [this] { settings.setValue ("tunerA4", refSlider.getValue()); };
    addAndMakeVisible (refSlider);

    modeCombo.addItem ("Chromatic", 1);
    modeCombo.addItem ("Instrument (nearest string)", 2);
    modeCombo.setSelectedId (settings.getIntValue ("tunerMode", 2), juce::dontSendNotification);
    modeCombo.onChange = [this] { settings.setValue ("tunerMode", modeCombo.getSelectedId()); repaint(); };
    addAndMakeVisible (modeCombo);

    juce::String lastCat;
    const auto& presets = tuningPresets();
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        if (presets[(size_t) i].category != lastCat)
        {
            lastCat = presets[(size_t) i].category;
            tuningCombo.addSectionHeading (lastCat);
        }
        tuningCombo.addItem (presets[(size_t) i].name, i + 1);
    }
    tuningCombo.addSeparator();
    tuningCombo.addItem ("Custom tuning...", customTuningId);
    tuningCombo.setSelectedId (settings.getIntValue ("tunerTuning", 1), juce::dontSendNotification);
    tuningCombo.onChange = [this] {
        settings.setValue ("tunerTuning", tuningCombo.getSelectedId());
        resized();
        repaint();
    };
    addAndMakeVisible (tuningCombo);

    customEditor.setText (settings.getValue ("tunerCustom", "D2 A2 D3 G3 A3 D4"), false);
    customEditor.setTooltip ("Space-separated notes, e.g. C2 G2 C3 F3 A3 D4");
    customEditor.onReturnKey = [this] { applyCustomTuning(); };
    customEditor.onFocusLost = [this] { applyCustomTuning(); };
    addChildComponent (customEditor);
    addChildComponent (customLabel);
    applyCustomTuning();

    meter.setTooltip ("Input level. Red LED = clipping; click to reset.");
    addAndMakeVisible (meter);

    refreshInputs();
}

TunerPanel::~TunerPanel()
{
    host.tuner.active = false;
}

void TunerPanel::updateActive()
{
    // Parents (page, tabs) can be hidden without notifying us, so the timer runs
    // permanently and re-evaluates isShowing() each tick; analysis only runs while visible.
    const bool showing = isShowing();
    if (showing && ! host.tuner.active.load())
        refreshInputs();
    host.tuner.active = showing;
    if (! isTimerRunning())
        startTimerHz (30);
}

void TunerPanel::refreshInputs()
{
    const auto names = host.activeInputChannelNames();
    inputCombo.clear (juce::dontSendNotification);
    for (int i = 0; i < names.size(); ++i)
        inputCombo.addItem (juce::String (i + 1) + ": " + names[i], i + 1);

    if (names.isEmpty())
    {
        inputCombo.setTextWhenNothingSelected ("No active inputs");
        statusLabel.setText (host.hasDevice() ? "The audio device has no active input channels. Enable one in Settings."
                                              : "No audio device. Choose your interface in Settings.",
                             juce::dontSendNotification);
        return;
    }
    statusLabel.setText ({}, juce::dontSendNotification);
    const int idx = juce::jlimit (0, names.size() - 1, settings.getIntValue ("tunerInput", 0));
    inputCombo.setSelectedId (idx + 1, juce::dontSendNotification);
    host.engine.analysisInputChannel = idx;
}

void TunerPanel::applyCustomTuning()
{
    customTuning = { "Custom", "Custom", {} };
    for (auto& tok : juce::StringArray::fromTokens (customEditor.getText(), " ,", ""))
    {
        const int n = parseNoteName (tok);
        if (n >= 0)
            customTuning.strings.push_back (n);
    }
    std::sort (customTuning.strings.begin(), customTuning.strings.end());
    const bool ok = customTuning.strings.size() >= 1 && customTuning.strings.size() <= 12;
    customEditor.setColour (juce::TextEditor::outlineColourId, ok ? palette::outline : palette::bad);
    if (ok)
        settings.setValue ("tunerCustom", customEditor.getText());
    repaint();
}

const Tuning& TunerPanel::currentTuning() const
{
    const int id = tuningCombo.getSelectedId();
    const auto& presets = tuningPresets();
    if (id == customTuningId)
        return customTuning;
    return presets[(size_t) juce::jlimit (0, (int) presets.size() - 1, id - 1)];
}

void TunerPanel::timerCallback()
{
    if (isShowing() != host.tuner.active.load())
        updateActive();
    if (! isShowing())
        return;

    const double a4 = refSlider.getValue();
    const double f = host.tuner.frequency.load();
    clarity = host.tuner.clarity.load();
    meter.tick();

    const auto now = juce::Time::getMillisecondCounter();
    if (f > 0.0)
    {
        double cents;
        if (instrumentMode() && ! currentTuning().strings.empty())
        {
            targetString = nearestStringIndex (currentTuning(), f, a4);
            const int target = currentTuning().strings[(size_t) targetString];
            lastNote = target;
            cents = 1200.0 * std::log2 (f / midiToFrequency (target, a4));
        }
        else
        {
            const auto n = nearestNote (f, a4);
            lastNote = n.midiNote;
            cents = n.cents;
            targetString = -1;
        }
        // light smoothing keeps the needle readable without lagging behind the string
        displayCents = holdUntil < now ? cents : displayCents + 0.45 * (cents - displayCents);
        lastFreq = f;
        holdUntil = now + 900;
    }
    repaint (displayArea());
}

juce::Rectangle<int> TunerPanel::displayArea() const
{
    return getLocalBounds().reduced (16).withTrimmedRight (300);
}

void TunerPanel::paint (juce::Graphics& g)
{
    g.fillAll (palette::bg);
    auto area = displayArea();
    drawSectionPanel (g, area, "Tuner");
    auto r = area.reduced (20).withTrimmedTop (16);

    const auto now = juce::Time::getMillisecondCounter();
    const bool live = holdUntil > now && lastNote >= 0;
    const bool fresh = live && host.tuner.frequency.load() > 0.0;
    const double cents = displayCents;
    const bool inTune = live && std::abs (cents) < 3.0;
    const auto noteColour = ! live ? palette::textDim.withAlpha (0.4f) : (inTune ? palette::good : palette::text);

    // ---- note name ----
    auto top = r.removeFromTop (juce::jmin (150, r.getHeight() / 3));
    g.setColour (noteColour.withAlpha (fresh ? 1.0f : 0.6f));
    g.setFont (uiFont (live ? juce::jmin (110.0f, top.getHeight() * 0.85f) : 22.0f, true));
    const auto name = live ? pitchClassName (lastNote) : juce::String ("Play a string");
    g.drawText (name, top, juce::Justification::centred);
    if (live)
    {
        const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name);
        g.setFont (uiFont (28.0f, true));
        g.setColour (palette::textDim);
        const int x = top.getCentreX() + juce::roundToInt (w * 0.5f) + 4;
        g.drawText (juce::String ((int) std::floor (lastNote / 12.0) - 1),
                    juce::Rectangle<int> (x, top.getCentreY(), 60, top.getHeight() / 2), juce::Justification::topLeft);
    }

    // ---- needle arc ----
    auto dial = r.removeFromTop (juce::jmin (r.getHeight() - 90, r.getWidth() / 2)).toFloat();
    const auto centre = juce::Point<float> (dial.getCentreX(), dial.getBottom() - 6.0f);
    const float radius = juce::jmin (dial.getWidth() * 0.45f, dial.getHeight() - 10.0f);
    const float span = juce::MathConstants<float>::pi * 0.38f;

    for (int c = -50; c <= 50; c += 5)
    {
        const float a = span * c / 50.0f;
        const bool major = c % 10 == 0;
        const float r0 = radius * (major ? 0.86f : 0.9f);
        juce::Point<float> p0 (centre.x + r0 * std::sin (a), centre.y - r0 * std::cos (a));
        juce::Point<float> p1 (centre.x + radius * std::sin (a), centre.y - radius * std::cos (a));
        g.setColour (c == 0 ? palette::good : (std::abs (c) <= 5 ? palette::good.withAlpha (0.5f) : palette::outline.brighter (0.3f)));
        g.drawLine ({ p0, p1 }, c == 0 ? 3.0f : (major ? 2.0f : 1.0f));
        if (major && c != 0)
        {
            g.setFont (uiFont (11.0f));
            g.setColour (palette::textDim);
            const float rl = radius * 0.76f;
            g.drawText (juce::String (c > 0 ? "+" : "") + juce::String (c),
                        juce::Rectangle<float> (0, 0, 34, 14).withCentre ({ centre.x + rl * std::sin (a), centre.y - rl * std::cos (a) }),
                        juce::Justification::centred);
        }
    }

    if (live)
    {
        const float clamped = (float) juce::jlimit (-50.0, 50.0, cents);
        const float a = span * clamped / 50.0f;
        juce::Path needle;
        needle.addTriangle (-2.5f, 0.0f, 2.5f, 0.0f, 0.0f, -radius * 0.98f);
        g.setColour (inTune ? palette::good : (std::abs (cents) < 15.0 ? palette::warn : palette::bad));
        g.fillPath (needle, juce::AffineTransform::rotation (a).translated (centre));
    }
    g.setColour (palette::panelHi);
    g.fillEllipse (juce::Rectangle<float> (14, 14).withCentre (centre));

    // ---- readouts ----
    auto info = r.removeFromTop (34);
    g.setFont (monoFont (17.0f, true));
    g.setColour (live ? noteColour : palette::textDim);
    juce::String centsText = live ? ((cents >= 0 ? "+" : "") + juce::String (cents, 1) + " cents") : juce::String ("-- cents");
    if (live && std::abs (cents) > 50.0) centsText << (cents > 0 ? "  (sharp)" : "  (flat)");
    g.drawText (centsText, info.removeFromLeft (info.getWidth() / 2), juce::Justification::centred);
    g.setColour (palette::textDim);
    g.drawText (live ? juce::String (lastFreq, 2) + " Hz" : juce::String ("--- Hz"), info, juce::Justification::centred);

    // ---- confidence ----
    auto conf = r.removeFromTop (22).reduced (juce::jmax (0, r.getWidth() / 2 - 160), 4);
    g.setFont (uiFont (11.0f, true));
    g.setColour (palette::textDim);
    g.drawText ("CONFIDENCE", conf.removeFromLeft (84), juce::Justification::centredLeft);
    g.setColour (palette::well);
    g.fillRoundedRectangle (conf.toFloat(), 3.0f);
    const float c01 = (float) juce::jlimit (0.0, 1.0, (clarity - 0.5) / 0.5);
    g.setColour (clarity >= 0.8 ? palette::good : palette::warn);
    g.fillRoundedRectangle (conf.toFloat().withWidth (conf.getWidth() * c01), 3.0f);

    // ---- strings strip ----
    const auto& tuning = currentTuning();
    if (instrumentMode() && ! tuning.strings.empty())
    {
        auto strip = r.removeFromBottom (44).reduced (0, 4);
        const int n = (int) tuning.strings.size();
        const int w = juce::jmin (70, strip.getWidth() / n);
        auto row = strip.withSizeKeepingCentre (w * n, strip.getHeight());
        for (int i = 0; i < n; ++i)
        {
            auto cell = row.removeFromLeft (w).reduced (3, 0).toFloat();
            const bool active = live && i == targetString;
            g.setColour (active ? (inTune ? palette::good : palette::accent) : palette::panelHi);
            g.fillRoundedRectangle (cell, 4.0f);
            g.setColour (active ? juce::Colours::black : palette::text);
            g.setFont (uiFont (14.0f, true));
            g.drawText (midiNoteName (tuning.strings[(size_t) i]), cell, juce::Justification::centred);
        }
    }
}

void TunerPanel::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto side = r.removeFromRight (284);
    auto meterArea = side.removeFromRight (22);
    meter.setBounds (meterArea.withTrimmedTop (30).withTrimmedBottom (10));
    side.removeFromRight (12);

    auto row = [&side] (int h) { auto x = side.removeFromTop (h); side.removeFromTop (6); return x; };
    inputLabel.setBounds (row (18));
    inputCombo.setBounds (row (28));
    side.removeFromTop (8);
    modeLabel.setBounds (row (18));
    modeCombo.setBounds (row (28));
    side.removeFromTop (8);
    tuningLabel.setBounds (row (18));
    tuningCombo.setBounds (row (28));
    const bool custom = tuningCombo.getSelectedId() == customTuningId;
    customLabel.setVisible (custom);
    customEditor.setVisible (custom);
    if (custom)
    {
        customLabel.setBounds (row (18));
        customEditor.setBounds (row (28));
    }
    side.removeFromTop (8);
    refLabel.setBounds (row (18));
    refSlider.setBounds (row (28));
    side.removeFromTop (12);
    statusLabel.setBounds (row (60));
}
} // namespace ff
