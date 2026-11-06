#include "MetronomePanel.h"

namespace ff
{
using namespace project;

// ------------------------------------------------------------------ BeatStrip

juce::Rectangle<float> BeatStrip::cell (int i) const
{
    const float gap = 6.0f;
    const float w = juce::jmin (64.0f, (getWidth() - gap * (numBeats - 1)) / (float) juce::jmax (1, numBeats));
    const float total = w * numBeats + gap * (numBeats - 1);
    const float x0 = (getWidth() - total) * 0.5f;
    return { x0 + i * (w + gap), 0.0f, w, (float) getHeight() };
}

void BeatStrip::paint (juce::Graphics& g)
{
    for (int i = 0; i < numBeats; ++i)
    {
        auto c = cell (i);
        const auto lvl = i < levels.length() ? levels[i] : '1';
        const bool playing = i == currentBeat;
        auto base = lvl == '2' ? palette::accent : (lvl == '1' ? palette::panelHi.brighter (0.2f) : palette::well);
        if (playing)
            base = currentMuted ? palette::textDim : base.interpolatedWith (juce::Colours::white, 0.45f * flash);
        g.setColour (base);
        g.fillRoundedRectangle (c, 5.0f);
        g.setColour (playing ? palette::text : palette::outline);
        g.drawRoundedRectangle (c.reduced (0.5f), 5.0f, playing ? 2.0f : 1.0f);

        g.setColour (lvl == '2' ? juce::Colours::black : palette::text);
        g.setFont (uiFont (16.0f, true));
        g.drawText (juce::String (i + 1), c.withTrimmedBottom (c.getHeight() * 0.3f), juce::Justification::centred);
        g.setFont (uiFont (10.0f, true));
        g.drawText (lvl == '2' ? "ACCENT" : (lvl == '1' ? "BEAT" : "MUTE"), c.withTrimmedTop (c.getHeight() * 0.6f),
                    juce::Justification::centred);
    }
}

void BeatStrip::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < numBeats; ++i)
        if (cell (i).contains (e.position) && onBeatClicked)
            onBeatClicked (i);
}

// ------------------------------------------------------------------ MetronomePanel

juce::Label& MetronomePanel::addLabel (const juce::String& text)
{
    auto* l = labels.add (new juce::Label ({}, text));
    styleLabel (*l, 12.0f, palette::textDim, true);
    addAndMakeVisible (l);
    return *l;
}

static void setupSlider (juce::Slider& s, double lo, double hi, double step, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 22);
    s.setRange (lo, hi, step);
    s.setTextValueSuffix (suffix);
}

MetronomePanel::MetronomePanel (EngineHost& h, ProjectDocument& d, juce::PropertiesFile& s)
    : host (h), doc (d), settings (s)
{
    auto& m = host.engine.metronome;

    setupSlider (bpmSlider, 20.0, 400.0, 1.0, " BPM");
    bpmSlider.onDragStart = [this] { doc.undo().beginNewTransaction ("Tempo"); };
    bpmSlider.onValueChange = [this] { if (! updating) doc.setTempo (bpmSlider.getValue()); };
    minusButton.onClick = [this] { doc.undo().beginNewTransaction ("Tempo"); doc.setTempo (std::round (doc.tempo()) - 1.0); };
    plusButton.onClick = [this] { doc.undo().beginNewTransaction ("Tempo"); doc.setTempo (std::round (doc.tempo()) + 1.0); };
    tapButton.onClick = [this] {
        const double bpm = tap.tap (juce::Time::getMillisecondCounterHiRes());
        if (bpm > 0.0) { if (tap.numTaps() == 2) doc.undo().beginNewTransaction ("Tap tempo"); doc.setTempo (bpm); }
    };
    startButton.setClickingTogglesState (false);
    startButton.onClick = [this] { togglePractice(); };
    startButton.setTooltip ("Start/stop the practice metronome (works with the transport stopped)");

    for (int i = 1; i <= 16; ++i) sigNum.addItem (juce::String (i), i);
    for (int x : { 2, 4, 8, 16 }) sigDen.addItem (juce::String (x), x);
    auto onSig = [this] { if (! updating) doc.setTimeSignature (sigNum.getSelectedId(), sigDen.getSelectedId()); };
    sigNum.onChange = onSig;
    sigDen.onChange = onSig;

    const std::pair<int, const char*> subs[] = { { 1, "None (beats only)" }, { 2, "Eighths (2)" }, { 3, "Triplets (3)" },
                                                 { 4, "Sixteenths (4)" }, { 5, "Quintuplets (5)" }, { 6, "Sextuplets (6)" },
                                                 { 8, "32nds (8)" } };
    for (auto& [id, name] : subs) subdivision.addItem (name, id);
    subdivision.onChange = [this] { if (! updating) doc.set (metroNode, ids::subdivision, subdivision.getSelectedId(), false); };

    for (int i = 0; i < MetronomeDsp::numSounds; ++i) sound.addItem (MetronomeDsp::soundName (i), i + 1);
    sound.onChange = [this] { if (! updating) doc.set (metroNode, ids::sound, sound.getSelectedId() - 1, false); };

    setupSlider (volumeSlider, 0.0, 100.0, 1.0, " %");
    volumeSlider.onValueChange = [this] { if (! updating) doc.set (metroNode, ids::volume, volumeSlider.getValue() / 100.0, false); };

    setupSlider (swingSlider, 0.0, 50.0, 1.0, " %");
    swingSlider.setTooltip ("Delays every second subdivision. 33% = triplet swing. Needs an even subdivision.");
    swingSlider.setValue (settings.getDoubleValue ("metroSwing", 0.0), juce::dontSendNotification);
    swingSlider.onValueChange = [this] { pushTrainerSettings(); };

    rampToggle.setToggleState (settings.getBoolValue ("rampEnabled", false), juce::dontSendNotification);
    setupSlider (rampStep, 1.0, 20.0, 1.0, " BPM");
    setupSlider (rampBars, 1.0, 32.0, 1.0, " bars");
    setupSlider (rampTarget, 30.0, 400.0, 1.0, " BPM");
    rampStep.setValue (settings.getDoubleValue ("rampStep", 5.0), juce::dontSendNotification);
    rampBars.setValue (settings.getDoubleValue ("rampBars", 4.0), juce::dontSendNotification);
    rampTarget.setValue (settings.getDoubleValue ("rampTarget", 160.0), juce::dontSendNotification);
    for (auto* sl : { &rampStep, &rampBars, &rampTarget })
        sl->onValueChange = [this] { pushTrainerSettings(); };
    rampToggle.onClick = [this] { pushTrainerSettings(); };

    setupSlider (missingSlider, 0.0, 80.0, 1.0, " %");
    missingSlider.setTooltip ("Chance that each beat (except beat 1) is silent, re-rolled every bar. Keep time through the gaps.");
    missingSlider.setValue (settings.getDoubleValue ("missingBeats", 0.0), juce::dontSendNotification);
    missingSlider.onValueChange = [this] { pushTrainerSettings(); };

    timerStart.onClick = [this] { timerRunning = ! timerRunning; lastTick = juce::Time::getMillisecondCounter(); };
    timerReset.onClick = [this] { practiceSeconds = 0.0; repaint (timerBox); };

    beats.onBeatClicked = [this] (int b) { cycleBeat (b); };

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &bpmSlider, &minusButton, &plusButton, &tapButton, &startButton, &sigNum, &sigDen, &subdivision, &swingSlider,
             &sound, &volumeSlider, &rampToggle, &rampStep, &rampBars, &rampTarget, &missingSlider, &timerStart, &timerReset, &beats })
        addAndMakeVisible (c);

    addLabel ("Time signature");   // 0
    addLabel ("Subdivision");      // 1
    addLabel ("Swing");            // 2
    addLabel ("Accents - click a beat to cycle accent / beat / mute"); // 3
    addLabel ("Click sound");      // 4
    addLabel ("Volume");           // 5
    addLabel ("Step");             // 6
    addLabel ("Every");            // 7
    addLabel ("Until");            // 8
    addLabel ("Missing beats");    // 9

    juce::ignoreUnused (m);
    pushTrainerSettings();
    rebind();
    startTimerHz (30);
}

MetronomePanel::~MetronomePanel()
{
    transportNode.removeListener (this);
    metroNode.removeListener (this);
}

void MetronomePanel::rebind()
{
    transportNode.removeListener (this);
    metroNode.removeListener (this);
    transportNode = doc.transport();
    metroNode = doc.metronome();
    transportNode.addListener (this);
    metroNode.addListener (this);
    refresh();
}

void MetronomePanel::refresh()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    bpmSlider.setValue (doc.tempo(), juce::dontSendNotification);
    sigNum.setSelectedId (doc.tsNum(), juce::dontSendNotification);
    sigDen.setSelectedId (doc.tsDen(), juce::dontSendNotification);
    subdivision.setSelectedId ((int) metroNode[ids::subdivision], juce::dontSendNotification);
    sound.setSelectedId ((int) metroNode[ids::sound] + 1, juce::dontSendNotification);
    volumeSlider.setValue ((double) metroNode[ids::volume] * 100.0, juce::dontSendNotification);
    swingSlider.setEnabled ((int) metroNode[ids::subdivision] % 2 == 0);
    beats.numBeats = doc.tsNum();
    beats.levels = metroNode[ids::accents].toString();
    beats.repaint();
    repaint (tempoBox);
}

void MetronomePanel::cycleBeat (int b)
{
    auto s = metroNode[ids::accents].toString().paddedRight ('1', MetronomeDsp::maxBeats);
    const auto c = s[b];
    const juce::juce_wchar next = c == '2' ? '1' : (c == '1' ? '0' : '2');
    s = s.substring (0, b) + juce::String::charToString (next) + s.substring (b + 1);
    doc.undo().beginNewTransaction ("Accent pattern");
    doc.set (metroNode, ids::accents, s);
}

void MetronomePanel::pushTrainerSettings()
{
    auto& m = host.engine.metronome;
    m.swing = (float) swingSlider.getValue() / 100.0f;
    m.rampEnabled = rampToggle.getToggleState();
    m.rampStepBpm = rampStep.getValue();
    m.rampEveryBars = (int) rampBars.getValue();
    m.rampTargetBpm = rampTarget.getValue();
    m.randomMuteProbability = (float) missingSlider.getValue() / 100.0f;
    for (auto* sl : { &rampStep, &rampBars, &rampTarget })
        sl->setEnabled (rampToggle.getToggleState());

    settings.setValue ("metroSwing", swingSlider.getValue());
    settings.setValue ("rampEnabled", rampToggle.getToggleState());
    settings.setValue ("rampStep", rampStep.getValue());
    settings.setValue ("rampBars", rampBars.getValue());
    settings.setValue ("rampTarget", rampTarget.getValue());
    settings.setValue ("missingBeats", missingSlider.getValue());
}

void MetronomePanel::togglePractice()
{
    host.engine.practiceMetronome = ! host.engine.practiceMetronome.load();
    if (host.engine.practiceMetronome && ! timerRunning)
    {
        timerRunning = true;
        lastTick = juce::Time::getMillisecondCounter();
    }
}

void MetronomePanel::timerCallback()
{
    auto& e = host.engine;
    const bool running = e.practiceMetronome.load() || (e.isPlaying() && e.clickWhilePlaying.load());
    startButton.setButtonText (e.practiceMetronome.load() ? "STOP" : "START");
    startButton.setToggleState (e.practiceMetronome.load(), juce::dontSendNotification);

    const auto bc = e.metronome.beatCounter.load();
    if (bc != lastBeatCounter)
    {
        lastBeatCounter = bc;
        beats.flash = 1.0f;
    }
    else
    {
        beats.flash *= 0.75f;
    }
    beats.currentBeat = running ? e.metronome.currentBeat.load() : -1;
    beats.currentMuted = e.metronome.currentBeatMuted.load();
    beats.repaint();

    const auto now = juce::Time::getMillisecondCounter();
    if (timerRunning)
        practiceSeconds += (now - lastTick) / 1000.0;
    lastTick = now;
    timerStart.setButtonText (timerRunning ? "Pause" : "Start");
    repaint (bpmReadout);
    repaint (timerBox);
}

void MetronomePanel::paint (juce::Graphics& g)
{
    g.fillAll (palette::bg);
    drawSectionPanel (g, tempoBox, "Tempo");
    drawSectionPanel (g, meterBox, "Meter & accents");
    drawSectionPanel (g, soundBox, "Sound");
    drawSectionPanel (g, trainerBox, "Tempo trainer");
    drawSectionPanel (g, rhythmBox, "Rhythm trainer");
    drawSectionPanel (g, timerBox, "Practice timer");

    auto& m = host.engine.metronome;
    const bool ramping = m.rampEnabled.load() && (host.engine.practiceMetronome.load() || host.engine.isPlaying());
    const double shown = ramping ? m.effectiveBpm.load() : doc.tempo();
    g.setColour (palette::text);
    g.setFont (uiFont (juce::jmin (84.0f, bpmReadout.getHeight() * 0.8f), true));
    g.drawText (juce::String (shown, juce::exactlyEqual (shown, std::floor (shown)) ? 0 : 1), bpmReadout, juce::Justification::centred);
    g.setColour (palette::textDim);
    g.setFont (uiFont (13.0f, true));
    g.drawText (ramping ? "BPM (TRAINER)" : "BPM", bpmReadout.withTrimmedTop (bpmReadout.getHeight() - 18), juce::Justification::centred);

    auto t = timerBox.reduced (14).withTrimmedTop (22).removeFromTop (48);
    const int secs = (int) practiceSeconds;
    g.setColour (timerRunning ? palette::accent : palette::text);
    g.setFont (monoFont (36.0f, true));
    g.drawText (juce::String (secs / 3600) + ":" + juce::String ((secs / 60) % 60).paddedLeft ('0', 2) + ":"
                    + juce::String (secs % 60).paddedLeft ('0', 2),
                t, juce::Justification::centred);
}

void MetronomePanel::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto right = r.removeFromRight (juce::jmin (380, r.getWidth() / 3));
    r.removeFromRight (12);

    tempoBox = r.removeFromTop (r.getHeight() / 2 - 6);
    r.removeFromTop (12);
    meterBox = r;

    soundBox = right.removeFromTop (110);
    right.removeFromTop (12);
    trainerBox = right.removeFromTop (170);
    right.removeFromTop (12);
    rhythmBox = right.removeFromTop (80);
    right.removeFromTop (12);
    timerBox = right.removeFromTop (130);

    // tempo
    {
        auto b = tempoBox.reduced (16).withTrimmedTop (16);
        auto buttons = b.removeFromBottom (44);
        auto sliderRow = b.removeFromBottom (36);
        bpmReadout = b;
        minusButton.setBounds (sliderRow.removeFromLeft (36).reduced (0, 4));
        plusButton.setBounds (sliderRow.removeFromRight (36).reduced (0, 4));
        bpmSlider.setBounds (sliderRow.reduced (8, 4));
        const int w = buttons.getWidth();
        startButton.setBounds (buttons.removeFromLeft (w * 2 / 3).reduced (0, 2));
        buttons.removeFromLeft (8);
        tapButton.setBounds (buttons.reduced (0, 2));
    }
    // meter
    {
        auto b = meterBox.reduced (16).withTrimmedTop (18);
        auto row = b.removeFromTop (46);
        auto col = [&row] (int w) { auto c = row.removeFromLeft (w); row.removeFromLeft (14); return c; };
        auto c1 = col (140), c2 = col (180), c3 = row;
        labels[0]->setBounds (c1.removeFromTop (18));
        sigNum.setBounds (c1.removeFromLeft (64));
        sigDen.setBounds (c1.removeFromRight (64));
        labels[1]->setBounds (c2.removeFromTop (18));
        subdivision.setBounds (c2);
        labels[2]->setBounds (c3.removeFromTop (18));
        swingSlider.setBounds (c3);
        b.removeFromTop (12);
        labels[3]->setBounds (b.removeFromTop (18));
        beats.setBounds (b.removeFromTop (juce::jmin (70, b.getHeight())));
    }
    // sound
    {
        auto b = soundBox.reduced (14).withTrimmedTop (18);
        auto r1 = b.removeFromTop (30);
        labels[4]->setBounds (r1.removeFromLeft (90));
        sound.setBounds (r1.reduced (0, 2));
        b.removeFromTop (6);
        auto r2 = b.removeFromTop (30);
        labels[5]->setBounds (r2.removeFromLeft (90));
        volumeSlider.setBounds (r2);
    }
    // trainer
    {
        auto b = trainerBox.reduced (14).withTrimmedTop (18);
        rampToggle.setBounds (b.removeFromTop (28));
        for (int i = 0; i < 3; ++i)
        {
            b.removeFromTop (4);
            auto rr = b.removeFromTop (30);
            labels[6 + i]->setBounds (rr.removeFromLeft (60));
            (i == 0 ? rampStep : i == 1 ? rampBars : rampTarget).setBounds (rr);
        }
    }
    // rhythm
    {
        auto b = rhythmBox.reduced (14).withTrimmedTop (20);
        auto rr = b.removeFromTop (30);
        labels[9]->setBounds (rr.removeFromLeft (110));
        missingSlider.setBounds (rr);
    }
    // timer
    {
        auto b = timerBox.reduced (14).withTrimmedTop (22);
        b.removeFromTop (52);
        auto rr = b.removeFromTop (30);
        timerStart.setBounds (rr.removeFromLeft (rr.getWidth() / 2 - 4));
        rr.removeFromLeft (8);
        timerReset.setBounds (rr);
    }
}
} // namespace ff
