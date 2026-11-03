#include "TransportBar.h"

namespace ff
{
using namespace project;

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool on = getToggleState();
    auto base = on ? (icon == Icon::metronome || icon == Icon::loop ? palette::accent : (icon == Icon::record ? palette::bad : palette::good))
                   : palette::panelHi;
    if (down) base = base.darker (0.2f);
    else if (highlighted) base = base.brighter (0.1f);
    g.setColour (base);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (palette::outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    const auto c = r.getCentre();
    const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.28f;
    g.setColour (on ? juce::Colours::black : palette::text);
    juce::Path p;
    switch (icon)
    {
        case Icon::play:    p.addTriangle (c.x - s * 0.8f, c.y - s, c.x - s * 0.8f, c.y + s, c.x + s, c.y); break;
        case Icon::stop:    p.addRectangle (c.x - s * 0.85f, c.y - s * 0.85f, s * 1.7f, s * 1.7f); break;
        case Icon::toStart:
            p.addRectangle (c.x - s, c.y - s, s * 0.35f, s * 2.0f);
            p.addTriangle (c.x + s, c.y - s, c.x + s, c.y + s, c.x - s * 0.5f, c.y);
            break;
        case Icon::metronome:
            p.addTriangle (c.x - s * 0.8f, c.y + s, c.x + s * 0.8f, c.y + s, c.x, c.y - s * 1.1f);
            g.fillPath (p);
            p.clear();
            g.setColour (on ? palette::accent : palette::panelHi);
            p.addLineSegment ({ c.x, c.y + s * 0.6f, c.x + s * 0.9f, c.y - s * 0.9f }, 2.0f);
            break;
        case Icon::record:
            g.setColour (on ? juce::Colours::white : palette::bad);
            p.addEllipse (c.x - s * 0.9f, c.y - s * 0.9f, s * 1.8f, s * 1.8f);
            break;
        case Icon::loop:
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, s, s * 0.75f, 0.0f, 0.5f, juce::MathConstants<float>::twoPi - 0.3f, true);
            g.strokePath (arc, juce::PathStrokeType (2.0f));
            const float ax = c.x + s * std::sin (0.5f), ay = c.y - s * 0.75f * std::cos (0.5f);
            p.addTriangle (ax - 4.0f, ay - 4.0f, ax + 3.0f, ay, ax - 4.0f, ay + 4.0f);
            break;
        }
    }
    g.fillPath (p);
}

TransportBar::TransportBar (EngineHost& h, ProjectDocument& d)
    : host (h), doc (d), inMeter (h.engine.inputMeter, false), outL (h.engine.outputMeterL, false),
      outR (h.engine.outputMeterR, false)
{
    rtzButton.onClick = [this] { host.engine.returnToZero(); };
    playButton.onClick = [this] { host.engine.togglePlay(); };
    clickButton.onClick = [this] { toggleClick(); };
    recordButton.onClick = [this] { if (onRecord) onRecord(); };
    loopButton.onClick = [this] { toggleLoop(); };
    punchButton.onClick = [this] { togglePunch(); };
    countInButton.onClick = [this] {
        const int bars = ((int) metroNode[ids::countInBars] + 1) % 3;
        doc.set (metroNode, ids::countInBars, bars, false);
    };
    rtzButton.setTooltip ("Return to start (Home)");
    playButton.setTooltip ("Play / Stop (Space)");
    clickButton.setTooltip ("Metronome click while playing (C)");
    recordButton.setTooltip ("Record on armed tracks (R). Press again to punch out.");
    loopButton.setTooltip ("Loop the range set on the ruler (L). Recording in a loop makes one take per pass.");
    punchButton.setTooltip ("Punch: record only inside the ruler range");
    countInButton.setTooltip ("Count-in bars before recording starts from stop (click to cycle Off / 1 / 2)");
    punchButton.setClickingTogglesState (false);
    punchButton.setColour (juce::TextButton::buttonOnColourId, palette::bad);
    countInButton.setColour (juce::TextButton::buttonOnColourId, palette::accent);
    for (juce::Component* b : std::initializer_list<juce::Component*> { &rtzButton, &playButton, &recordButton, &loopButton, &clickButton,
                                                                       &punchButton, &countInButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    for (auto* l : { &barsLabel, &timeLabel })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setColour (juce::Label::backgroundColourId, palette::well);
        addAndMakeVisible (l);
    }
    barsLabel.setFont (monoFont (22.0f, true));
    barsLabel.setColour (juce::Label::textColourId, palette::accent);
    timeLabel.setFont (monoFont (14.0f));
    timeLabel.setColour (juce::Label::textColourId, palette::textDim);

    bpmCaption.setText ("TEMPO", juce::dontSendNotification);
    sigCaption.setText ("METER", juce::dontSendNotification);
    inCaption.setText ("IN", juce::dontSendNotification);
    outCaption.setText ("OUT", juce::dontSendNotification);
    for (auto* l : { &bpmCaption, &sigCaption, &inCaption, &outCaption, &cpuLabel })
    {
        styleLabel (*l, 11.0f, palette::textDim, true);
        addAndMakeVisible (l);
    }
    cpuLabel.setJustificationType (juce::Justification::centredRight);

    tempoSlider.setSliderStyle (juce::Slider::LinearBarVertical);
    tempoSlider.setRange (20.0, 400.0, 0.01);
    tempoSlider.setNumDecimalPlacesToDisplay (2);
    tempoSlider.setTextValueSuffix (" BPM");
    tempoSlider.setVelocityBasedMode (true);
    tempoSlider.setVelocityModeParameters (0.6, 1, 0.05, false);
    tempoSlider.setColour (juce::Slider::trackColourId, palette::well);
    tempoSlider.setTooltip ("Drag up/down or double-click to type a tempo (quarter-note BPM)");
    tempoSlider.onDragStart = [this] { doc.undo().beginNewTransaction ("Tempo"); };
    tempoSlider.onValueChange = [this] {
        if (! updating)
        {
            if (! tempoSlider.isMouseButtonDown()) doc.undo().beginNewTransaction ("Tempo");
            doc.setTempo (tempoSlider.getValue());
        }
    };
    addAndMakeVisible (tempoSlider);

    for (int i = 1; i <= 16; ++i) sigNum.addItem (juce::String (i), i);
    for (int d2 : { 2, 4, 8, 16 }) sigDen.addItem (juce::String (d2), d2);
    auto onSig = [this] {
        if (! updating && sigNum.getSelectedId() > 0 && sigDen.getSelectedId() > 0)
            doc.setTimeSignature (sigNum.getSelectedId(), sigDen.getSelectedId());
    };
    sigNum.onChange = onSig;
    sigDen.onChange = onSig;
    addAndMakeVisible (sigNum);
    addAndMakeVisible (sigDen);

    tapButton.setTooltip ("Tap tempo (T)");
    tapButton.onClick = [this] { tapTempo(); };
    addAndMakeVisible (tapButton);

    inMeter.setTooltip ("Input level of the analysis/tuner input. Click to reset the clip indicator.");
    for (auto* m : { &inMeter, &outL, &outR })
        addAndMakeVisible (m);

    rebind();
    startTimerHz (30);
}

TransportBar::~TransportBar()
{
    transportNode.removeListener (this);
    metroNode.removeListener (this);
}

void TransportBar::rebind()
{
    transportNode.removeListener (this);
    metroNode.removeListener (this);
    transportNode = doc.transport();
    metroNode = doc.metronome();
    transportNode.addListener (this);
    metroNode.addListener (this);
    refreshFromProject();
}

void TransportBar::refreshFromProject()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    tempoSlider.setValue (doc.tempo(), juce::dontSendNotification);
    sigNum.setSelectedId (doc.tsNum(), juce::dontSendNotification);
    sigDen.setSelectedId (doc.tsDen(), juce::dontSendNotification);
    clickButton.setToggleState ((bool) metroNode[ids::enabled], juce::dontSendNotification);
    loopButton.setToggleState ((bool) transportNode[ids::loopEnabled], juce::dontSendNotification);
    punchButton.setToggleState ((bool) transportNode[ids::punchEnabled], juce::dontSendNotification);
    const int bars = (int) metroNode[ids::countInBars];
    countInButton.setButtonText (bars == 0 ? "NO COUNT" : (bars == 1 ? "COUNT 1 BAR" : "COUNT 2 BARS"));
    countInButton.setToggleState (bars > 0, juce::dontSendNotification);
}

void TransportBar::toggleLoop()
{
    doc.set (transportNode, ids::loopEnabled, ! (bool) transportNode[ids::loopEnabled], false);
}

void TransportBar::togglePunch()
{
    doc.set (transportNode, ids::punchEnabled, ! (bool) transportNode[ids::punchEnabled], false);
}

void TransportBar::tapTempo()
{
    const double bpm = tap.tap (juce::Time::getMillisecondCounterHiRes());
    if (bpm > 0.0)
    {
        if (tap.numTaps() == 2) doc.undo().beginNewTransaction ("Tap tempo");
        doc.setTempo (bpm);
    }
}

void TransportBar::toggleClick()
{
    doc.set (metroNode, ids::enabled, ! (bool) metroNode[ids::enabled], false);
}

void TransportBar::timerCallback()
{
    auto& e = host.engine;
    const bool playing = e.isPlaying();
    const bool recording = e.isRecordRequested();
    recordBlink = (recordBlink + 1) % 30;
    recordButton.setToggleState (recording && (! e.isCountingIn() || recordBlink < 15), juce::dontSendNotification);
    playButton.setToggleState (playing, juce::dontSendNotification);
    playButton.setIcon (playing ? IconButton::Icon::stop : IconButton::Icon::play);

    const double ppq = e.getPositionPpq();
    const int num = doc.tsNum(), den = doc.tsDen();
    const double qPerBar = num * 4.0 / den;
    const int bar = (int) std::floor (ppq / qPerBar) + 1;
    const double inBar = (ppq - (bar - 1) * qPerBar) * den / 4.0;
    const int beat = (int) std::floor (inBar) + 1;
    const int ticks = (int) ((inBar - std::floor (inBar)) * 960.0);
    barsLabel.setText (juce::String (bar).paddedLeft ('0', 3) + "." + juce::String (beat) + "." + juce::String (ticks).paddedLeft ('0', 3),
                       juce::dontSendNotification);

    const double sr = e.getSampleRate();
    const double secs = sr > 0 ? (double) e.getPositionSamples() / sr : 0.0;
    const int mins = (int) (secs / 60.0);
    timeLabel.setText (juce::String (mins).paddedLeft ('0', 2) + ":" + juce::String (secs - mins * 60.0, 3).paddedLeft ('0', 6),
                       juce::dontSendNotification);

    e.inputMeter.uiTake();
    e.outputMeterL.uiTake();
    e.outputMeterR.uiTake();
    inMeter.tick();
    outL.tick();
    outR.tick();

    const auto midi = host.midiActivity.load();
    if (midi != lastMidi) { lastMidi = midi; midiFlash = 6; }
    else if (midiFlash > 0) --midiFlash;

    const int xr = host.xruns();
    cpuLabel.setText ("CPU " + juce::String (juce::roundToInt (host.cpuUsage() * 100.0)) + "%"
                          + (xr > 0 ? "   XRUN " + juce::String (xr) : juce::String()),
                      juce::dontSendNotification);
    cpuLabel.setColour (juce::Label::textColourId, host.cpuUsage() > 0.8 || xr > 0 ? palette::bad : palette::textDim);
    repaint (getLocalBounds().removeFromRight (40));
}

void TransportBar::paint (juce::Graphics& g)
{
    g.setColour (palette::panel);
    g.fillRect (getLocalBounds());
    g.setColour (palette::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // MIDI activity LED (top-right)
    auto led = getLocalBounds().removeFromRight (36).withSizeKeepingCentre (26, 14);
    g.setColour (midiFlash > 0 ? palette::info : palette::well);
    g.fillRoundedRectangle (led.toFloat(), 3.0f);
    g.setColour (midiFlash > 0 ? juce::Colours::black : palette::textDim);
    g.setFont (uiFont (9.5f, true));
    g.drawText ("MIDI", led, juce::Justification::centred);
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    r.removeFromRight (30); // MIDI LED

    const int h = r.getHeight();
    rtzButton.setBounds (r.removeFromLeft (h));
    r.removeFromLeft (4);
    playButton.setBounds (r.removeFromLeft (h + 8));
    r.removeFromLeft (4);
    recordButton.setBounds (r.removeFromLeft (h));
    r.removeFromLeft (4);
    loopButton.setBounds (r.removeFromLeft (h));
    r.removeFromLeft (4);
    clickButton.setBounds (r.removeFromLeft (h));
    r.removeFromLeft (6);
    auto small = r.removeFromLeft (96);
    punchButton.setBounds (small.removeFromTop (h / 2 - 1));
    countInButton.setBounds (small.removeFromBottom (h / 2 - 1));
    r.removeFromLeft (12);

    auto pos = r.removeFromLeft (170);
    barsLabel.setBounds (pos.removeFromTop (h * 3 / 5));
    timeLabel.setBounds (pos);
    r.removeFromLeft (14);

    auto tempo = r.removeFromLeft (120);
    bpmCaption.setBounds (tempo.removeFromTop (14));
    tempoSlider.setBounds (tempo.reduced (0, 1));
    r.removeFromLeft (6);
    tapButton.setBounds (r.removeFromLeft (46).withTrimmedTop (14));
    r.removeFromLeft (14);

    auto sig = r.removeFromLeft (124);
    sigCaption.setBounds (sig.removeFromTop (14));
    sigNum.setBounds (sig.removeFromLeft (60).reduced (0, 1));
    sigDen.setBounds (sig.removeFromRight (60).reduced (0, 1));

    auto right = r.removeFromRight (juce::jmax (180, juce::jmin (300, r.getWidth() - 470)));
    cpuLabel.setBounds (right.removeFromRight (90));
    right.removeFromRight (10);
    auto meters = right;
    auto inRow = meters.removeFromTop (meters.getHeight() / 3);
    inCaption.setBounds (inRow.removeFromLeft (30));
    inMeter.setBounds (inRow.reduced (0, 2));
    meters.removeFromTop (2);
    auto outRow = meters;
    outCaption.setBounds (outRow.removeFromLeft (30));
    outL.setBounds (outRow.removeFromTop (outRow.getHeight() / 2).reduced (0, 1));
    outR.setBounds (outRow.reduced (0, 1));
}
} // namespace ff
