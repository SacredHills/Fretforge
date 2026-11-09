#include "TrainingPage.h"
#include "core/TabModel.h"
#include "core/MetronomeDsp.h"
#include "engine/TabSynth.h"

namespace ff
{
namespace tr = training;
namespace tids = tab::ids;

// ===================================================================== practice log

PracticeLog::PracticeLog (juce::PropertiesFile& s) : settings (s)
{
    if (auto x = juce::parseXML (settings.getValue ("practiceLog")))
    {
        auto t = juce::ValueTree::fromXml (*x);
        if (t.hasType ("PRACTICE")) tree = t;
    }
}

juce::ValueTree PracticeLog::day (const juce::String& date, bool create)
{
    auto d = tree.getChildWithProperty ("date", date);
    if (! d.isValid() && create)
    {
        d = juce::ValueTree ("DAY");
        d.setProperty ("date", date, nullptr);
        tree.appendChild (d, nullptr);
    }
    return d;
}

void PracticeLog::addSeconds (const juce::String& technique, double seconds)
{
    auto d = day (dateString (juce::Time::getCurrentTime()), true);
    auto t = d.getChildWithProperty ("id", technique);
    if (! t.isValid())
    {
        t = juce::ValueTree ("T");
        t.setProperty ("id", technique, nullptr);
        d.appendChild (t, nullptr);
    }
    t.setProperty ("sec", (double) t.getProperty ("sec", 0.0) + seconds, nullptr);
}

void PracticeLog::recordTempo (const juce::String& technique, int bpm)
{
    juce::ValueTree r ("REC");
    r.setProperty ("id", technique, nullptr);
    r.setProperty ("bpm", bpm, nullptr);
    r.setProperty ("date", dateString (juce::Time::getCurrentTime()), nullptr);
    tree.appendChild (r, nullptr);
    save();
}

double PracticeLog::secondsOn (const juce::String& date) const
{
    double s = 0.0;
    for (auto t : tree.getChildWithProperty ("date", date)) s += (double) t.getProperty ("sec", 0.0);
    return s;
}

double PracticeLog::secondsOn (const juce::String& date, const juce::String& technique) const
{
    return (double) tree.getChildWithProperty ("date", date).getChildWithProperty ("id", technique).getProperty ("sec", 0.0);
}

double PracticeLog::totalSeconds() const
{
    double s = 0.0;
    for (auto d : tree)
        if (d.hasType ("DAY")) for (auto t : d) s += (double) t.getProperty ("sec", 0.0);
    return s;
}

int PracticeLog::bestTempo (const juce::String& technique) const
{
    int best = 0;
    for (auto r : tree)
        if (r.hasType ("REC") && r["id"].toString() == technique) best = juce::jmax (best, (int) r["bpm"]);
    return best;
}

std::vector<std::pair<juce::String, int>> PracticeLog::tempoHistory (const juce::String& technique) const
{
    std::vector<std::pair<juce::String, int>> out;
    for (auto r : tree)
        if (r.hasType ("REC") && r["id"].toString() == technique) out.emplace_back (r["date"].toString(), (int) r["bpm"]);
    return out;
}

int PracticeLog::streakDays() const
{
    auto t = juce::Time::getCurrentTime();
    if (secondsOn (dateString (t)) < 1.0) t -= juce::RelativeTime::days (1);   // today not started yet: count up to yesterday
    int n = 0;
    while (secondsOn (dateString (t)) >= 1.0 && n < 3650) { ++n; t -= juce::RelativeTime::days (1); }
    return n;
}

double PracticeLog::secondsLastDays (const juce::String& technique, int days) const
{
    double s = 0.0;
    auto t = juce::Time::getCurrentTime();
    for (int i = 0; i < days; ++i, t -= juce::RelativeTime::days (1)) s += secondsOn (dateString (t), technique);
    return s;
}

void PracticeLog::save()
{
    if (auto x = tree.createXml()) settings.setValue ("practiceLog", x->toString (juce::XmlElement::TextFormat().singleLine()));
}

// ===================================================================== page

static juce::String minutesText (double sec)
{
    const int m = (int) (sec / 60.0);
    return m >= 60 ? juce::String (m / 60) + " h " + juce::String (m % 60) + " min" : juce::String (m) + " min";
}

TrainingPage::TrainingPage (EngineHost& h, ProjectDocument& d, juce::PropertiesFile& s) : host (h), doc (d), settings (s), log (s)
{
    list.setModel (this);
    list.setRowHeight (38);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (list);

    title.setFont (titleFont (22.0f));
    title.setColour (juce::Label::textColourId, palette::text);
    addAndMakeVisible (title);
    styleLabel (tip, 13.0f, palette::textDim);
    addAndMakeVisible (tip);
    styleLabel (planDesc, 12.0f, palette::textDim);
    addAndMakeVisible (planDesc);
    bpmLabel.setFont (titleFont (30.0f));
    bpmLabel.setColour (juce::Label::textColourId, palette::accent);
    bpmLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (bpmLabel);
    styleLabel (statusLabel, 12.5f, palette::textDim);
    statusLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (statusLabel);

    planBox.addItem ("Free practice", 1);
    for (int i = 0; i < (int) tr::plans().size(); ++i) planBox.addItem (juce::String ("Plan: ") + tr::plans()[(size_t) i].name, i + 2);
    planBox.setSelectedId (settings.getIntValue ("trainingPlan", 2), juce::dontSendNotification);
    planBox.onChange = [this] { settings.setValue ("trainingPlan", planBox.getSelectedId()); refreshPlan(); };
    addAndMakeVisible (planBox);
    nextButton.setTooltip ("The next technique of the plan you have not practised today");
    nextButton.onClick = [this] {
        const int p = planBox.getSelectedId() - 2;
        if (! juce::isPositiveAndBelow (p, (int) tr::plans().size())) return;
        const auto& ids = tr::plans()[(size_t) p].techniques;
        const auto today = PracticeLog::dateString (juce::Time::getCurrentTime());
        int pick = -1;
        for (size_t i = 0; i < ids.size() && pick < 0; ++i)
            if (log.secondsOn (today, ids[i]) < 120.0) pick = tr::techniqueIndex (ids[i]);
        if (pick < 0) pick = tr::techniqueIndex (ids[(size_t) ((std::find_if (ids.begin(), ids.end(), [this] (const char* id) { return currentId() == id; }) - ids.begin() + 1) % (int) ids.size())]);
        selectTechnique (pick);
    };
    addAndMakeVisible (nextButton);

    auto caption = [this] (juce::Label& l, const char* text) {
        l.setText (text, juce::dontSendNotification);
        styleLabel (l, 11.5f, palette::textDim);
        addAndMakeVisible (l);
    };
    caption (positionCaption, "Position (fret)");
    caption (barsCaption, "Bars");
    caption (notesCaption, "Notes");
    caption (tuningCaption, "Tuning");
    caption (startCaption, "Start tempo");
    caption (goalCaption, "Goal tempo");
    caption (stepCaption, "Speed up by");
    caption (everyCaption, "Every");

    positionSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    positionSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 22);
    positionSlider.setRange (0, 15, 1);
    positionSlider.setValue (settings.getIntValue ("trainingPosition", 5), juce::dontSendNotification);
    positionSlider.onValueChange = [this] { ex.position = (int) positionSlider.getValue(); settings.setValue ("trainingPosition", ex.position); rebuild(); };
    addAndMakeVisible (positionSlider);

    for (int b : { 1, 2, 4 }) barsBox.addItem (juce::String (b), b);
    barsBox.setSelectedId (settings.getIntValue ("trainingBars", 2), juce::dontSendNotification);
    barsBox.onChange = [this] { ex.bars = barsBox.getSelectedId(); settings.setValue ("trainingBars", ex.bars); rebuild(); };
    addAndMakeVisible (barsBox);
    notesBox.addItem ("8ths", 8);
    notesBox.addItem ("Triplets", 12);
    notesBox.addItem ("16ths", 16);
    notesBox.setSelectedId (settings.getIntValue ("trainingNotes", 16), juce::dontSendNotification);
    notesBox.onChange = [this] { ex.subdivision = notesBox.getSelectedId(); settings.setValue ("trainingNotes", ex.subdivision); rebuild(); };
    addAndMakeVisible (notesBox);
    const char* tunings[] { "Standard E", "Eb standard", "Drop D", "D standard", "Drop C", "7-string B" };
    for (int i = 0; i < 6; ++i) tuningBox.addItem (tunings[i], i + 1);
    tuningBox.setSelectedId (settings.getIntValue ("trainingTuning", 1), juce::dontSendNotification);
    tuningBox.onChange = [this] {
        settings.setValue ("trainingTuning", tuningBox.getSelectedId());
        switch (tuningBox.getSelectedId())
        {
            case 2: ex.tuning = { 39, 44, 49, 54, 58, 63 }; break;
            case 3: ex.tuning = { 38, 45, 50, 55, 59, 64 }; break;
            case 4: ex.tuning = { 38, 43, 48, 53, 57, 62 }; break;
            case 5: ex.tuning = { 36, 43, 48, 53, 57, 62 }; break;
            case 6: ex.tuning = { 35, 40, 45, 50, 55, 59, 64 }; break;
            default: ex.tuning = { 40, 45, 50, 55, 59, 64 }; break;
        }
        rebuild();
    };
    addAndMakeVisible (tuningBox);
    variationButton.setTooltip ("A different pattern for the same technique (shape, order, rhythm)");
    variationButton.onClick = [this] { ex.seed = ex.seed + 1 + (juce::uint32) juce::Random::getSystemRandom().nextInt (1000); rebuild(); };
    addAndMakeVisible (variationButton);

    for (auto* sl : { &startSlider, &goalSlider })
    {
        sl->setSliderStyle (juce::Slider::LinearHorizontal);
        sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
        sl->setRange (30, 300, 1);
        sl->setTextValueSuffix (" BPM");
        addAndMakeVisible (sl);
    }
    startSlider.onValueChange = [this] {
        settings.setValue ("trainingStart_" + currentId(), (int) startSlider.getValue());
        if (! host.engine.isPlaying()) setTempo (startSlider.getValue());
    };
    goalSlider.onValueChange = [this] { settings.setValue ("trainingGoal_" + currentId(), (int) goalSlider.getValue()); updateStatus(); };
    for (int st : { 1, 2, 3, 5, 10 }) stepBox.addItem ("+" + juce::String (st) + " BPM", st);
    stepBox.setSelectedId (settings.getIntValue ("trainingStep", 2), juce::dontSendNotification);
    stepBox.onChange = [this] { settings.setValue ("trainingStep", stepBox.getSelectedId()); };
    for (int n : { 1, 2, 4, 8 }) everyBox.addItem (juce::String (n) + (n == 1 ? " loop" : " loops"), n);
    everyBox.setSelectedId (settings.getIntValue ("trainingEvery", 2), juce::dontSendNotification);
    everyBox.onChange = [this] { settings.setValue ("trainingEvery", everyBox.getSelectedId()); };
    addAndMakeVisible (stepBox);
    addAndMakeVisible (everyBox);
    autoSpeed.setToggleState (settings.getBoolValue ("trainingAuto", true), juce::dontSendNotification);
    autoSpeed.onClick = [this] { settings.setValue ("trainingAuto", autoSpeed.getToggleState()); };
    clickToggle.setToggleState (settings.getBoolValue ("trainingClick", true), juce::dontSendNotification);
    clickToggle.onClick = [this] { settings.setValue ("trainingClick", clickToggle.getToggleState()); pushEngine(); };
    countToggle.setToggleState (settings.getBoolValue ("trainingCount", true), juce::dontSendNotification);
    countToggle.onClick = [this] { settings.setValue ("trainingCount", countToggle.getToggleState()); };
    for (auto* t : { &autoSpeed, &clickToggle, &countToggle }) addAndMakeVisible (t);

    playButton.setColour (juce::TextButton::buttonColourId, palette::accent);
    playButton.setColour (juce::TextButton::textColourOffId, palette::onAccent);
    playButton.onClick = [this] { togglePlay(); };
    addAndMakeVisible (playButton);
    cleanButton.setTooltip ("You played the exercise cleanly at this tempo: it goes into your records");
    cleanButton.onClick = [this] {
        log.recordTempo (currentId(), juce::roundToInt (bpm));
        statusLabel.setText ("Recorded " + juce::String (juce::roundToInt (bpm)) + " BPM as clean", juce::dontSendNotification);
        repaint();
    };
    addAndMakeVisible (cleanButton);
    resetTempoButton.onClick = [this] { setTempo (startSlider.getValue()); loopsAtTempo = 0; };
    addAndMakeVisible (resetTempoButton);
    addAndMakeVisible (tabView);
    addChildComponent (neckView);

    ex.position = (int) positionSlider.getValue();
    ex.bars = barsBox.getSelectedId();
    ex.subdivision = notesBox.getSelectedId();
    tuningBox.onChange();
    const int last = juce::jmax (0, tr::techniqueIndex (settings.getValue ("trainingTechnique", "spider")));
    selectTechnique (last);
    refreshPlan();
}

TrainingPage::~TrainingPage()
{
    stopTimer();
    if (pendingSeconds > 0.0) { log.addSeconds (currentId(), pendingSeconds); log.save(); }
}

void TrainingPage::selectTechnique (int index)
{
    index = juce::jlimit (0, (int) tr::techniques().size() - 1, index);
    if (pendingSeconds > 0.0) { log.addSeconds (currentId(), pendingSeconds); pendingSeconds = 0.0; log.save(); }
    ex.technique = index;
    ex.seed = 1;
    const auto& t = tr::techniques()[(size_t) index];
    settings.setValue ("trainingTechnique", t.id);
    title.setText (t.name, juce::dontSendNotification);
    tip.setText (juce::String (t.category) + dotSep() + t.tip, juce::dontSendNotification);
    startSlider.setValue (settings.getIntValue ("trainingStart_" + juce::String (t.id), t.startBpm), juce::dontSendNotification);
    goalSlider.setValue (settings.getIntValue ("trainingGoal_" + juce::String (t.id), t.goalBpm), juce::dontSendNotification);
    list.selectRow (index, false, true);
    loopsAtTempo = 0;
    bpm = startSlider.getValue();
    rebuild();
    refreshPlan();
    repaint();
}

void TrainingPage::rebuild()
{
    score = tr::buildScore (ex, bpm);
    bpmLabel.setText (juce::String (juce::roundToInt (bpm)) + " BPM", juce::dontSendNotification);
    if (active)
    {
        host.engine.submitTabSequence (buildTabSequence (score, 1.0));
        pushEngine();
    }
    updateStatus();
    tabView.repaint();
    const auto d = tr::scaleDiagram (ex);
    const bool had = ! diagram.empty();
    diagram = d;
    positionCaption.setText (diagram.empty() ? juce::String ("Position (fret)")
                                             : "Key: " + juce::MidiMessage::getMidiNoteName (40 + ex.position, true, false, 3) + " minor",
                             juce::dontSendNotification);
    if (had != ! diagram.empty()) resized();
    neckView.repaint();
}

int TrainingPage::playingStep() const
{
    auto& e = host.engine;
    if (! active || ! e.isPlaying() || e.isCountingIn() || bpm <= 0.0) return -1;
    const double stepSec = 60.0 / bpm * 4.0 / (double) ex.subdivision;
    return (int) std::floor (e.getPositionSeconds() / stepSec + 1.0e-6);
}

void TrainingPage::NeckView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (palette::well);
    g.fillRoundedRectangle (r, 4.0f);
    const auto& d = page.diagram;
    if (d.empty()) return;
    const int nStr = (int) page.ex.tuning.size();
    int lo = 99, hi = 0;
    for (auto& n : d) { lo = juce::jmin (lo, n.fret); hi = juce::jmax (hi, n.fret); }
    // show the frets around the shape (at least 6), from the nut when the shape is low
    int first = juce::jmax (0, lo - 1), last = juce::jmax (hi + 1, first + 6);
    if (first <= 1) first = 0;
    const bool nut = first == 0;
    auto area = r.reduced (36.0f, 14.0f).withTrimmedBottom (14.0f);
    const int cells = last - first + (nut ? 0 : 1);   // fret spaces drawn
    const float fw = area.getWidth() / (float) juce::jmax (1, cells + (nut ? 1 : 0));
    const float sg = area.getHeight() / (float) juce::jmax (1, nStr - 1);
    // x of the middle of a fret space (fret 0 = left of the nut)
    auto xFor = [&] (int fret) { return nut ? area.getX() + ((float) fret + 0.5f) * fw : area.getX() + ((float) (fret - first) + 0.5f) * fw; };
    auto lineX = [&] (int fret) { return nut ? area.getX() + ((float) fret + 1.0f) * fw : area.getX() + (float) (fret - first + 1) * fw; };   // the fret wire after `fret`
    // inlays
    g.setColour (palette::outline.brighter (0.2f));
    for (int f = juce::jmax (1, first); f <= last; ++f)
    {
        const int m = f % 12;
        if (m == 3 || m == 5 || m == 7 || m == 9) g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ xFor (f), area.getCentreY() }));
        if (m == 0)
            for (float k : { -1.0f, 1.0f }) g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ xFor (f), area.getCentreY() + k * sg }));
    }
    // fret wires (the nut thicker) and fret numbers
    g.setFont (uiFont (10.0f));
    for (int f = nut ? 0 : first - 1; f <= last; ++f)
    {
        const float x = lineX (f);
        g.setColour (f == 0 ? palette::text.withAlpha (0.8f) : palette::outline.brighter (0.5f));
        g.fillRect (juce::Rectangle<float> (x - (f == 0 ? 2.0f : 0.75f), area.getY(), f == 0 ? 4.0f : 1.5f, area.getHeight()));
    }
    for (int f = juce::jmax (1, first); f <= last; ++f)
    {
        g.setColour (palette::textDim);
        g.drawText (juce::String (f), juce::Rectangle<float> (xFor (f) - 12.0f, area.getBottom() + 3.0f, 24.0f, 12.0f), juce::Justification::centred);
    }
    // strings: highest on top, with names
    for (int s = 0; s < nStr; ++s)
    {
        const float y = area.getY() + (float) (nStr - 1 - s) * sg;
        g.setColour (palette::textDim.withAlpha (0.7f));
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
        g.setColour (palette::textDim);
        g.drawText (juce::MidiMessage::getMidiNoteName (page.ex.tuning[(size_t) s], true, false, 3),
                    juce::Rectangle<float> (r.getX() + 4.0f, y - 7.0f, 28.0f, 14.0f), juce::Justification::centredLeft);
    }
    // the note being played
    int playS = -1, playF = -1;
    if (const int step = page.playingStep(); step >= 0)
    {
        const auto pat = tr::pattern (page.ex);
        if (! pat.empty() && ! pat[(size_t) step % pat.size()].notes.empty())
        {
            playS = pat[(size_t) step % pat.size()].notes[0].string;
            playF = pat[(size_t) step % pat.size()].notes[0].fret;
        }
    }
    // the notes
    const float dot = juce::jmin (fw * 0.72f, sg * 0.9f, 26.0f);
    for (auto& n : d)
    {
        const auto c = juce::Point<float> (n.fret == 0 && nut ? area.getX() - dot * 0.2f : xFor (n.fret), area.getY() + (float) (nStr - 1 - n.string) * sg);
        const bool root = juce::String (n.degree) == "1";
        const bool now = n.string == playS && n.fret == playF;
        auto b = juce::Rectangle<float> (dot, dot).withCentre (c);
        if (now) { g.setColour (palette::accent.withAlpha (0.35f)); g.fillEllipse (b.expanded (5.0f)); }
        g.setColour (root ? palette::graph : juce::Colour (0xff262626));
        g.fillEllipse (b);
        g.setColour (now ? palette::accent : root ? palette::graph.brighter (0.4f) : palette::text.withAlpha (0.55f));
        g.drawEllipse (b, now ? 2.0f : 1.0f);
        g.setColour (palette::text);
        g.setFont (uiFont (dot * 0.48f, true));
        g.drawText (n.degree != nullptr ? juce::String (n.degree) : juce::String(), b, juce::Justification::centred);
    }
}

void TrainingPage::pushEngine()
{
    if (! active) return;
    auto& e = host.engine;
    e.setTempo (bpm);
    e.setTimeSignature (4, 4);
    e.clickWhilePlaying = clickToggle.getToggleState();
    e.punchEnabled = false;
    e.loopStartSec = 0.0;
    e.loopEndSec = tab::tempoMap (score).secondsAt (tab::measureStarts (score).back());
    e.loopEnabled = true;
}

void TrainingPage::setTempo (double newBpm)
{
    bpm = juce::jlimit (30.0, 300.0, newBpm);
    rebuild();
}

void TrainingPage::setActive (bool shouldBeActive)
{
    if (active == shouldBeActive) return;
    active = shouldBeActive;
    auto& e = host.engine;
    if (e.isPlaying()) e.stop();
    if (active)
    {
        e.tabMode = true;
        rebuild();
        startTimerHz (20);
    }
    else
    {
        stopTimer();
        e.tabMode = false;
        doc.refreshEngine();
        if (pendingSeconds > 0.0) { log.addSeconds (currentId(), pendingSeconds); pendingSeconds = 0.0; log.save(); }
    }
}

void TrainingPage::togglePlay()
{
    auto& e = host.engine;
    if (! active) return;
    if (e.isPlaying()) { e.stop(); return; }
    loopsAtTempo = 0;
    rebuild();
    e.locateSeconds (0.0);
    lastPos = 0.0;
    juce::int64 countIn = 0;
    if (countToggle.getToggleState() && e.getSampleRate() > 0) countIn = (juce::int64) (4 * MetronomeDsp::beatLengthSamples (e.getSampleRate(), bpm, 4));
    e.playWithCountIn (countIn);
}

void TrainingPage::timerCallback()
{
    auto& e = host.engine;
    const bool playing = e.isPlaying();
    playButton.setButtonText (playing ? "Stop" : "Play");
    const auto now = juce::Time::getMillisecondCounter();
    if (playing && ! e.isCountingIn())
    {
        pendingSeconds += (now - lastTick) * 0.001;
        if (pendingSeconds > 15.0) { log.addSeconds (currentId(), pendingSeconds); pendingSeconds = 0.0; log.save(); repaint (statsBox); }
        const double pos = e.getPositionSeconds();
        if (pos + 0.05 < lastPos)   // wrapped: one more time round the exercise
        {
            ++loopsAtTempo;
            if (autoSpeed.getToggleState() && loopsAtTempo >= everyBox.getSelectedId() && bpm < goalSlider.getValue())
            {
                loopsAtTempo = 0;
                setTempo (juce::jmin (goalSlider.getValue(), bpm + stepBox.getSelectedId()));
            }
            updateStatus();
        }
        lastPos = pos;
        tabView.repaint();
        if (! diagram.empty()) neckView.repaint();
    }
    lastTick = now;
}

void TrainingPage::updateStatus()
{
    const int best = log.bestTempo (currentId());
    juce::String t;
    if (bpm >= goalSlider.getValue()) t = "Goal tempo reached";
    else t = "Loop " + juce::String (loopsAtTempo + 1) + " at this tempo";
    if (best > 0) t << dotSep() << "your best: " << best << " BPM";
    statusLabel.setText (t, juce::dontSendNotification);
}

void TrainingPage::refreshPlan()
{
    planChips.clear();
    const int p = planBox.getSelectedId() - 2;
    nextButton.setVisible (juce::isPositiveAndBelow (p, (int) tr::plans().size()));
    if (! juce::isPositiveAndBelow (p, (int) tr::plans().size()))
    {
        planDesc.setText ("Pick any technique on the left.", juce::dontSendNotification);
        resized();
        return;
    }
    const auto& plan = tr::plans()[(size_t) p];
    planDesc.setText (plan.description, juce::dontSendNotification);
    const auto today = PracticeLog::dateString (juce::Time::getCurrentTime());
    for (auto* id : plan.techniques)
    {
        const int idx = tr::techniqueIndex (id);
        if (idx < 0) continue;
        const bool done = log.secondsOn (today, id) >= 120.0;   // two minutes counts as done for today
        auto b = std::make_unique<juce::TextButton> ((done ? juce::String::fromUTF8 ("\xe2\x9c\x93 ") : juce::String()) + tr::techniques()[(size_t) idx].name);
        b->setClickingTogglesState (false);
        b->setToggleState (idx == ex.technique, juce::dontSendNotification);
        b->setColour (juce::TextButton::buttonColourId, done ? palette::good.withAlpha (0.25f) : palette::panelHi);
        b->setTooltip (done ? "Practised today" : "Not practised today yet (2 minutes counts)");
        b->onClick = [this, idx] { selectTechnique (idx); };
        addAndMakeVisible (*b);
        planChips.push_back (std::move (b));
    }
    resized();
}

void TrainingPage::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    const auto& t = tr::techniques()[(size_t) row];
    const bool newCategory = row == 0 || juce::String (tr::techniques()[(size_t) row - 1].category) != t.category;
    if (selected)
    {
        g.setColour (palette::panelHi);
        g.fillRect (0, 0, w, h);
        g.setColour (palette::accent);
        g.fillRect (0, 0, 3, h);
    }
    if (newCategory)
    {
        g.setColour (palette::outline);
        g.drawHorizontalLine (0, 6.0f, (float) w - 6.0f);
    }
    g.setColour (selected ? palette::text : palette::text.withAlpha (0.8f));
    g.setFont (uiFont (13.0f, selected));
    g.drawText (t.name, 12, 4, w - 16, 18, juce::Justification::centredLeft, true);
    g.setColour (palette::textDim.withAlpha (0.8f));
    g.setFont (uiFont (10.0f));
    const auto mins = log.secondsLastDays (t.id, 7) / 60.0;
    g.drawText (juce::String (t.category) + (mins >= 1.0 ? dotSep() + juce::String ((int) mins) + " min this week" : juce::String()),
                12, 22, w - 16, 12, juce::Justification::centredLeft, true);
}

void TrainingPage::selectedRowsChanged (int row)
{
    if (row >= 0 && row != ex.technique) selectTechnique (row);
}

// --------------------------------------------------------------------- tab view

void TrainingPage::TabView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (palette::well);
    g.fillRoundedRectangle (r, 4.0f);
    const auto& score = page.score;
    if (! score.isValid()) return;
    const auto track = tab::tracks (score).getChild (0);
    const int nStr = tab::numStrings (track);
    const int bars = tab::numMeasures (score);
    if (nStr <= 0 || bars <= 0) return;
    const auto area = r.reduced (40.0f, 22.0f);
    const float lineGap = area.getHeight() / (float) juce::jmax (1, nStr - 1);
    const float barW = area.getWidth() / (float) bars;
    g.setFont (monoFont (juce::jlimit (10.0f, 15.0f, lineGap * 0.62f), true));
    // strings (highest on top), string names
    const auto tuning = tab::trackTuning (track);
    for (int s = 0; s < nStr; ++s)
    {
        const float y = area.getY() + (float) (nStr - 1 - s) * lineGap;
        g.setColour (palette::outline.brighter (0.4f));
        g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
        g.setColour (palette::textDim);
        g.drawText (juce::MidiMessage::getMidiNoteName (tuning[(size_t) s], true, false, 3), juce::Rectangle<float> (r.getX() + 6.0f, y - 8.0f, 28.0f, 16.0f),
                    juce::Justification::centredLeft);
    }
    for (int b = 0; b <= bars; ++b)
    {
        g.setColour (palette::outline.brighter (b == 0 || b == bars ? 0.8f : 0.5f));
        const float x = area.getX() + (float) b * barW;
        g.drawLine (x, area.getY(), x, area.getBottom(), b == 0 || b == bars ? 2.0f : 1.0f);
    }
    // notes
    for (int b = 0; b < bars; ++b)
    {
        auto bar = tab::bar (track, b);
        const int beats = bar.getNumChildren();
        const float cellW = barW / (float) juce::jmax (1, beats);
        const auto noteFont = monoFont (juce::jlimit (8.0f, 15.0f, juce::jmin (lineGap * 0.62f, cellW * 0.62f)), true);
        g.setFont (noteFont);
        for (int i = 0; i < beats; ++i)
        {
            const auto beat = bar.getChild (i);
            const float x = area.getX() + (float) b * barW + ((float) i + 0.5f) / (float) beats * barW;
            if (i % (beats / 4 > 0 ? beats / 4 : 1) == 0)   // beat ticks
            {
                g.setColour (palette::outline.withAlpha (0.6f));
                g.drawVerticalLine (juce::roundToInt (x), area.getBottom() + 4.0f, area.getBottom() + 9.0f);
            }
            for (auto note : beat)
            {
                const int s = (int) note[tids::string];
                const float y = area.getY() + (float) (nStr - 1 - s) * lineGap;
                juce::String txt (juce::String ((int) note[tids::fret]));
                auto box = juce::Rectangle<float> (x - cellW * 0.5f, y - lineGap * 0.4f, cellW, lineGap * 0.8f);
                g.setColour (palette::well);
                g.fillRect (box.withSizeKeepingCentre (juce::GlyphArrangement::getStringWidth (noteFont, txt) + 4.0f, box.getHeight()));
                g.setColour ((bool) note[tids::accent] ? palette::graph.brighter (0.3f) : palette::text);
                g.drawText (txt, box, juce::Justification::centred);
                juce::String mark;
                if ((bool) note[tids::hammer]) mark = "h";
                else if ((bool) note[tids::tap]) mark = "T";
                else if ((double) note[tids::bend] > 0.0) mark = "b";
                else if ((bool) note[tids::vibrato]) mark = "~";
                else if ((int) note[tids::slide] != 0) mark = "/";
                else if ((bool) note[tids::palmMute]) mark = "PM";
                if (mark.isNotEmpty())
                {
                    g.setColour (palette::info);
                    g.setFont (uiFont (10.0f, true));
                    g.drawText (mark, juce::Rectangle<float> (x - 12.0f, y - lineGap * 0.4f - 13.0f, 24.0f, 12.0f), juce::Justification::centred);
                    g.setFont (noteFont);
                }
            }
        }
    }
    // playhead
    auto& e = page.host.engine;
    if (page.active && e.isPlaying() && ! e.isCountingIn())
    {
        const double len = juce::jmax (0.01, e.loopEndSec.load());
        const float x = area.getX() + (float) (std::fmod (e.getPositionSeconds(), len) / len) * area.getWidth();
        g.setColour (palette::accent);
        g.fillRect (juce::Rectangle<float> (x - 1.0f, r.getY() + 4.0f, 2.0f, r.getHeight() - 8.0f));
    }
}

// --------------------------------------------------------------------- stats

void TrainingPage::drawStats (juce::Graphics& g)
{
    auto r = statsBox.reduced (14).withTrimmedTop (26);
    const auto today = PracticeLog::dateString (juce::Time::getCurrentTime());
    auto stat = [&g, &r] (const juce::String& label, const juce::String& value) {
        auto row = r.removeFromTop (40);
        g.setColour (palette::textDim);
        g.setFont (uiFont (11.0f));
        g.drawText (label, row.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (palette::text);
        g.setFont (titleFont (19.0f));
        g.drawText (value, row, juce::Justification::centredLeft);
    };
    stat ("Today", minutesText (log.secondsOn (today) + pendingSeconds));
    const int streak = log.streakDays();
    stat ("Streak", juce::String (streak) + (streak == 1 ? " day" : " days"));
    stat ("All practice", minutesText (log.totalSeconds() + pendingSeconds));
    const int best = log.bestTempo (currentId());
    stat ("Best clean tempo (this technique)", best > 0 ? juce::String (best) + " BPM" : juce::String ("-"));

    // last 14 days
    r.removeFromTop (6);
    g.setColour (palette::textDim);
    g.setFont (uiFont (11.0f));
    g.drawText ("Minutes per day (14 days)", r.removeFromTop (16), juce::Justification::centredLeft);
    auto chart = r.removeFromTop (70).toFloat();
    double days[14];
    double mx = 1.0;
    auto t = juce::Time::getCurrentTime();
    for (int i = 13; i >= 0; --i, t -= juce::RelativeTime::days (1)) { days[i] = log.secondsOn (PracticeLog::dateString (t)) / 60.0; mx = juce::jmax (mx, days[i]); }
    days[13] += pendingSeconds / 60.0;
    mx = juce::jmax (mx, days[13]);
    const float bw = chart.getWidth() / 14.0f;
    for (int i = 0; i < 14; ++i)
    {
        const float hgt = (float) (days[i] / mx) * chart.getHeight();
        g.setColour (i == 13 ? palette::graph : palette::graph.withAlpha (0.45f));
        g.fillRect (juce::Rectangle<float> (chart.getX() + (float) i * bw + 1.0f, chart.getBottom() - hgt, bw - 2.0f, juce::jmax (1.0f, hgt)));
    }

    // tempo records for this technique
    r.removeFromTop (10);
    g.setColour (palette::textDim);
    g.drawText ("Clean tempo records (this technique)", r.removeFromTop (16), juce::Justification::centredLeft);
    auto line = r.removeFromTop (60).toFloat();
    const auto hist = log.tempoHistory (currentId());
    if (hist.size() >= 1)
    {
        int lo = 1000, hi = 0;
        for (auto& [d, b] : hist) { lo = juce::jmin (lo, b); hi = juce::jmax (hi, b); }
        if (hi == lo) { lo -= 10; hi += 10; }
        juce::Path p;
        for (size_t i = 0; i < hist.size(); ++i)
        {
            const float x = line.getX() + (hist.size() == 1 ? 0.5f : (float) i / (float) (hist.size() - 1)) * line.getWidth();
            const float y = line.getBottom() - (float) (hist[i].second - lo) / (float) (hi - lo) * line.getHeight();
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            g.setColour (palette::info);
            g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);
        }
        g.strokePath (p, juce::PathStrokeType (1.5f));
        g.setColour (palette::textDim);
        g.setFont (uiFont (10.0f));
        g.drawText (juce::String (hi), line.toNearestInt().removeFromTop (12), juce::Justification::topRight);
        g.drawText (juce::String (lo), line.toNearestInt().removeFromBottom (12), juce::Justification::bottomRight);
    }
    else
    {
        g.setFont (uiFont (11.0f));
        g.drawFittedText ("Press \"Played it clean\" when you get through the exercise cleanly.", line.toNearestInt(), juce::Justification::topLeft, 3);
    }

    // what to work on: the techniques of the plan (or all) practised least this week
    r.removeFromTop (10);
    g.setColour (palette::textDim);
    g.setFont (uiFont (11.0f));
    g.drawText ("Needs work (least practised this week)", r.removeFromTop (16), juce::Justification::centredLeft);
    std::vector<std::pair<double, int>> candidates;
    const int p = planBox.getSelectedId() - 2;
    if (juce::isPositiveAndBelow (p, (int) tr::plans().size()))
        for (auto* id : tr::plans()[(size_t) p].techniques) candidates.emplace_back (log.secondsLastDays (id, 7), tr::techniqueIndex (id));
    else
        for (int i = 0; i < (int) tr::techniques().size(); ++i) candidates.emplace_back (log.secondsLastDays (tr::techniques()[(size_t) i].id, 7), i);
    std::sort (candidates.begin(), candidates.end());
    g.setColour (palette::text);
    for (size_t i = 0; i < juce::jmin ((size_t) 3, candidates.size()) && r.getHeight() >= 16; ++i)
        g.drawText (juce::String::fromUTF8 ("\xe2\x80\xa2 ") + tr::techniques()[(size_t) candidates[i].second].name
                        + "  (" + juce::String ((int) (candidates[i].first / 60.0)) + " min)",
                    r.removeFromTop (17), juce::Justification::centredLeft, true);
}

void TrainingPage::paint (juce::Graphics& g)
{
    g.fillAll (palette::bg);
    drawSectionPanel (g, leftBox, "Techniques");
    drawSectionPanel (g, mainBox, "Exercise");
    drawSectionPanel (g, trainerBox, "Tempo trainer");
    drawSectionPanel (g, statsBox, "Progress");
    drawStats (g);
}

void TrainingPage::resized()
{
    auto r = getLocalBounds().reduced (16);
    leftBox = r.removeFromLeft (250);
    r.removeFromLeft (12);
    statsBox = r.removeFromRight (290);
    r.removeFromRight (12);
    trainerBox = r.removeFromBottom (190);
    r.removeFromBottom (12);
    mainBox = r;

    list.setBounds (leftBox.reduced (6).withTrimmedTop (26));

    auto m = mainBox.reduced (14).withTrimmedTop (24);
    auto planRow = m.removeFromTop (28);
    planBox.setBounds (planRow.removeFromLeft (200));
    planRow.removeFromLeft (8);
    nextButton.setBounds (planRow.removeFromRight (120));
    planDesc.setBounds (planRow);
    m.removeFromTop (6);
    if (! planChips.empty())
    {
        auto chips = m.removeFromTop (28);
        const int w = chips.getWidth() / (int) planChips.size();
        for (auto& c : planChips) c->setBounds (chips.removeFromLeft (w).reduced (2, 0));
        m.removeFromTop (8);
    }
    title.setBounds (m.removeFromTop (32));
    tip.setBounds (m.removeFromTop (20));
    m.removeFromTop (8);
    auto opts = m.removeFromTop (44);
    auto col = [&opts] (int w) { auto c = opts.removeFromLeft (w); opts.removeFromLeft (10); return c; };
    auto c1 = col (180), c2 = col (64), c3 = col (104), c4 = col (130);
    positionCaption.setBounds (c1.removeFromTop (16)); positionSlider.setBounds (c1);
    barsCaption.setBounds (c2.removeFromTop (16)); barsBox.setBounds (c2.reduced (0, 2));
    notesCaption.setBounds (c3.removeFromTop (16)); notesBox.setBounds (c3.reduced (0, 2));
    tuningCaption.setBounds (c4.removeFromTop (16)); tuningBox.setBounds (c4.reduced (0, 2));
    variationButton.setBounds (opts.removeFromLeft (juce::jmin (130, opts.getWidth())).withTrimmedTop (16).reduced (0, 1));
    m.removeFromTop (12);
    neckView.setVisible (! diagram.empty());
    if (! diagram.empty())
    {
        neckView.setBounds (m.removeFromTop (juce::jmax (150, m.getHeight() * 45 / 100)));
        m.removeFromTop (8);
    }
    tabView.setBounds (m);

    auto t = trainerBox.reduced (14).withTrimmedTop (26);
    auto big = t.removeFromRight (230);
    bpmLabel.setBounds (big.removeFromTop (40));
    playButton.setBounds (big.removeFromTop (40).reduced (20, 2));
    statusLabel.setBounds (big.removeFromTop (22));
    auto row1 = t.removeFromTop (44);
    auto s1 = row1.removeFromLeft (row1.getWidth() / 2 - 6);
    row1.removeFromLeft (12);
    startCaption.setBounds (s1.removeFromTop (16)); startSlider.setBounds (s1);
    goalCaption.setBounds (row1.removeFromTop (16)); goalSlider.setBounds (row1);
    t.removeFromTop (8);
    auto row2 = t.removeFromTop (44);
    auto a = row2.removeFromLeft (120); stepCaption.setBounds (a.removeFromTop (16)); stepBox.setBounds (a.reduced (0, 2));
    row2.removeFromLeft (10);
    auto b = row2.removeFromLeft (110); everyCaption.setBounds (b.removeFromTop (16)); everyBox.setBounds (b.reduced (0, 2));
    row2.removeFromLeft (14);
    autoSpeed.setBounds (row2.removeFromLeft (190).withTrimmedTop (16));
    t.removeFromTop (8);
    auto row3 = t.removeFromTop (28);
    clickToggle.setBounds (row3.removeFromLeft (70));
    countToggle.setBounds (row3.removeFromLeft (96));
    row3.removeFromLeft (6);
    const int bw = juce::jmin (160, (row3.getWidth() - 8) / 2);
    cleanButton.setBounds (row3.removeFromLeft (bw));
    row3.removeFromLeft (8);
    resetTempoButton.setBounds (row3.removeFromLeft (bw));
}
} // namespace ff
