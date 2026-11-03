#include "TabPage.h"
#include "core/MusicTheory.h"
#include "engine/TabSynth.h"

namespace ff
{
namespace tids = tab::ids;

static const char* sigNames[] = { "2/4", "3/4", "4/4", "5/4", "6/4", "7/4", "2/2", "3/8", "5/8", "6/8", "7/8", "9/8", "12/8" };

TabPage::TabPage (ProjectDocument& d, EngineHost& h, juce::PropertiesFile& s)
    : doc (d), host (h), settings (s), editor (d, h)
{
    viewport.setViewedComponent (&editor, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    editor.onStateChanged = [this] { refreshPalette(); ensureCursorVisible(); };

    trackCombo.onChange = [this] { if (! updating) editor.setTrackIndex (trackCombo.getSelectedItemIndex()); };
    trackCombo.setTooltip ("Track to view and edit (Ctrl+Up/Down)");
    addTrackButton.onClick = [this] { showAddTrackDialog(); };
    tuningButton.onClick = [this] { showTuningDialog(); };
    trackMenuButton.onClick = [this] { showTrackMenu(); };

    tempoLabel.setText ("Tempo", juce::dontSendNotification);
    speedLabel.setText ("Speed", juce::dontSendNotification);
    for (auto* l : { &tempoLabel, &speedLabel }) { styleLabel (*l, 12.0f, palette::textDim, true); addAndMakeVisible (l); }
    tempoSlider.setSliderStyle (juce::Slider::LinearBarVertical);
    tempoSlider.setRange (20.0, 400.0, 1.0);
    tempoSlider.setTextValueSuffix (" BPM");
    tempoSlider.setColour (juce::Slider::trackColourId, palette::well);
    tempoSlider.onDragStart = [this] { doc.undo().beginNewTransaction ("Tab tempo"); };
    tempoSlider.onValueChange = [this] { if (! updating) doc.set (score, tids::tempo, tempoSlider.getValue()); };
    tempoSlider.setTooltip ("Score tempo (drag or double-click to type)");

    for (int pct : { 25, 40, 50, 60, 70, 75, 80, 90, 100, 110, 125, 150 })
        speedCombo.addItem (juce::String (pct) + "%", pct);
    speedCombo.setSelectedId (100, juce::dontSendNotification);
    speedCombo.setTooltip ("Practice speed: slows playback without changing pitch");
    speedCombo.onChange = [this] {
        const double newSpeed = speedCombo.getSelectedId() / 100.0;
        if (active && host.engine.isPlaying())
            host.engine.locateSeconds (host.engine.getPositionSeconds() * speed / newSpeed);
        speed = newSpeed;
        pushEngineOverrides();
        rebuildSequence();
    };

    playButton.onClick = [this] { togglePlay(); };
    playButton.setTooltip ("Play from the cursor, or loop the selection (Space also plays/stops)");
    loopButton.setClickingTogglesState (true);
    loopButton.setTooltip ("Loop the selected beats (or the cursor's bar)");
    loopButton.onClick = [this] { pushEngineOverrides(); };
    clickButton.setClickingTogglesState (true);
    clickButton.setToggleState (settings.getBoolValue ("tabClick", false), juce::dontSendNotification);
    clickButton.onClick = [this] { settings.setValue ("tabClick", clickButton.getToggleState()); pushEngineOverrides(); };
    countButton.setClickingTogglesState (true);
    countButton.setToggleState (settings.getBoolValue ("tabCountIn", false), juce::dontSendNotification);
    countButton.onClick = [this] { settings.setValue ("tabCountIn", countButton.getToggleState()); };
    importButton.setTooltip ("Import MIDI (.mid) or ASCII tab (.txt / .tab)");
    importButton.onClick = [this] { importDialog(); };
    exportButton.onClick = [this] { exportDialog(); };

    for (auto* c : std::initializer_list<juce::Component*> { &trackCombo, &addTrackButton, &tuningButton, &trackMenuButton, &tempoSlider,
                                                             &speedCombo, &playButton, &loopButton, &clickButton, &countButton,
                                                             &importButton, &exportButton })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (c);
    }

    // palette
    const std::pair<int, const char*> durs[] = { { 1, "1" }, { 2, "1/2" }, { 4, "1/4" }, { 8, "1/8" }, { 16, "1/16" }, { 32, "1/32" }, { 64, "1/64" } };
    for (auto [dur, name] : durs)
    {
        auto* b = durButtons.add (new juce::TextButton (name));
        b->setTooltip ("Note value (+ / - keys)");
        b->onClick = [this, dur = dur] { editor.setDuration (dur); editor.grabKeyboardFocus(); };
    }
    dotButton.setTooltip ("Dotted (.)");
    tripletButton.setTooltip ("Triplet (T)");
    restButton.setTooltip ("Rest (R)");
    dotButton.onClick = [this] { editor.toggleDots(); editor.grabKeyboardFocus(); };
    tripletButton.onClick = [this] { editor.toggleTuplet(); editor.grabKeyboardFocus(); };
    restButton.onClick = [this] { editor.toggleRest(); editor.grabKeyboardFocus(); };

    struct Tech { const char* label; const char* tip; std::function<void (TabEditor&)> fn; };
    const std::vector<Tech> techs {
        { "H/P", "Hammer-on / pull-off to the next note (H)", [] (TabEditor& e) { e.toggleNoteFlag (tids::hammer, "Hammer-on"); } },
        { "Slide", "Slide: to next / in from below / out downwards (S)", [] (TabEditor& e) { e.cycleSlide(); } },
        { "Bend", "Bend: 1/2, full, 1 1/2, 2 (B)", [] (TabEditor& e) { e.cycleBend(); } },
        { "Vib", "Vibrato (V)", [] (TabEditor& e) { e.toggleNoteFlag (tids::vibrato, "Vibrato"); } },
        { "PM", "Palm mute (P)", [] (TabEditor& e) { e.toggleNoteFlag (tids::palmMute, "Palm mute"); } },
        { "X", "Dead note (X)", [] (TabEditor& e) { e.toggleNoteFlag (tids::dead, "Dead note"); } },
        { "( )", "Ghost note (G)", [] (TabEditor& e) { e.toggleNoteFlag (tids::ghost, "Ghost note"); } },
        { "NH", "Natural harmonic (N)", [] (TabEditor& e) { e.cycleHarmonic (tab::harmNatural); } },
        { "AH", "Artificial harmonic (A)", [] (TabEditor& e) { e.cycleHarmonic (tab::harmArtificial); } },
        { "LR", "Let ring (L)", [] (TabEditor& e) { e.toggleNoteFlag (tids::letRing, "Let ring"); } },
        { "Tap", "Tapping (Shift+T)", [] (TabEditor& e) { e.toggleNoteFlag (tids::tap, "Tap"); } },
        { "Tie", "Tie to the previous note (Z)", [] (TabEditor& e) { e.toggleNoteFlag (tids::tie, "Tie"); } },
        { ">", "Accent (>)", [] (TabEditor& e) { e.toggleNoteFlag (tids::accent, "Accent"); } },
        { "Dn", "Downstroke (D)", [] (TabEditor& e) { e.setStroke (tab::strokeDown); } },
        { "Up", "Upstroke (U)", [] (TabEditor& e) { e.setStroke (tab::strokeUp); } },
        { "Trem", "Tremolo picking (W)", [] (TabEditor& e) { e.toggleBeatFlag (tids::tremolo, "Tremolo picking"); } },
    };
    for (auto& t : techs)
    {
        auto* b = techButtons.add (new juce::TextButton (t.label));
        b->setTooltip (t.tip);
        b->onClick = [this, fn = t.fn] { fn (editor); editor.grabKeyboardFocus(); };
    }

    measAddButton.setTooltip ("Insert a bar after the cursor");
    measDelButton.setTooltip ("Delete the cursor's bar");
    transDownButton.setTooltip ("Transpose selection down a semitone");
    transUpButton.setTooltip ("Transpose selection up a semitone");
    measAddButton.onClick = [this] { editor.insertMeasure (true); editor.grabKeyboardFocus(); };
    measDelButton.onClick = [this] { editor.deleteMeasure(); editor.grabKeyboardFocus(); };
    transDownButton.onClick = [this] { editor.transpose (-1); editor.grabKeyboardFocus(); };
    transUpButton.onClick = [this] { editor.transpose (1); editor.grabKeyboardFocus(); };
    for (int i = 0; i < (int) std::size (sigNames); ++i)
        sigCombo.addItem (sigNames[i], i + 1);
    sigCombo.setTooltip ("Time signature of the cursor's bar");
    sigCombo.onChange = [this] {
        if (updating || sigCombo.getSelectedId() <= 0) return;
        const auto t = juce::StringArray::fromTokens (sigCombo.getText(), "/", "");
        auto meas = tab::measures (score).getChild (editor.cursor.bar);
        doc.undo().beginNewTransaction ("Time signature");
        doc.set (meas, tids::tsNum, t[0].getIntValue());
        doc.set (meas, tids::tsDen, t[1].getIntValue());
        editor.grabKeyboardFocus();
    };

    for (auto* b : durButtons) { b->setClickingTogglesState (false); b->setWantsKeyboardFocus (false); addAndMakeVisible (b); }
    for (auto* b : techButtons) { b->setWantsKeyboardFocus (false); addAndMakeVisible (b); }
    for (juce::Component* c : std::initializer_list<juce::Component*> { &dotButton, &tripletButton, &restButton, &measAddButton,
                                                                       &measDelButton, &transDownButton, &transUpButton, &sigCombo })
    {
        c->setWantsKeyboardFocus (false);
        addAndMakeVisible (c);
    }
    styleLabel (status, 12.0f);
    addAndMakeVisible (status);

    rebind();
    startTimerHz (30);
}

TabPage::~TabPage()
{
    root.removeListener (this);
}

void TabPage::rebind()
{
    root.removeListener (this);
    root = doc.tree();
    score = tab::getOrCreateScore (root);
    root.addListener (this);
    editor.setScore (score);
    refreshTracks();
    rebuildSequence();
}

void TabPage::structureChanged()
{
    refreshTracks();
    editor.layoutChanged();
    triggerAsyncUpdate();
}

void TabPage::valueTreePropertyChanged (juce::ValueTree& node, const juce::Identifier& id)
{
    if (! node.isAChildOf (score) && node != score)
        return;
    if (node == score && id == tids::tempo)
        pushEngineOverrides();
    if (node.hasType (tids::TABTRACK)) refreshTracks();
    editor.layoutChanged();
    refreshPalette();
    triggerAsyncUpdate();
}

void TabPage::refreshTracks()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    trackCombo.clear (juce::dontSendNotification);
    int i = 1;
    for (auto t : tab::tracks (score))
        trackCombo.addItem (juce::String (i) + ". " + t[tids::name].toString() + "  (" + juce::String (tab::numStrings (t)) + " strings)", i), ++i;
    trackCombo.setSelectedItemIndex (editor.getTrackIndex(), juce::dontSendNotification);
    tempoSlider.setValue ((double) score.getProperty (tids::tempo, 120.0), juce::dontSendNotification);
    const bool hasTrack = tab::tracks (score).getNumChildren() > 0;
    for (auto* c : std::initializer_list<juce::Component*> { &tuningButton, &trackMenuButton, &playButton, &exportButton })
        c->setEnabled (hasTrack);
    refreshPalette();
}

void TabPage::refreshPalette()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    auto bar = tab::bar (editor.track(), editor.cursor.bar);
    auto beat = bar.getChild (editor.cursor.beat);
    const int dur = beat.isValid() ? (int) beat[tids::dur] : editor.entryDur;
    const int durs[] = { 1, 2, 4, 8, 16, 32, 64 };
    for (int i = 0; i < durButtons.size(); ++i)
        durButtons[i]->setToggleState (durs[i] == dur, juce::dontSendNotification);
    dotButton.setToggleState (beat.isValid() ? (int) beat[tids::dots] > 0 : editor.entryDots > 0, juce::dontSendNotification);
    tripletButton.setToggleState (beat.isValid() ? (int) beat[tids::tuplet] > 0 : editor.entryTuplet > 0, juce::dontSendNotification);
    restButton.setToggleState (beat.isValid() && (bool) beat[tids::rest], juce::dontSendNotification);

    auto note = tab::noteOnString (beat, editor.cursorString);
    const bool noteFlags[] = { (bool) note[tids::hammer], (int) note[tids::slide] != 0, (double) note[tids::bend] > 0.0, (bool) note[tids::vibrato],
                           (bool) note[tids::palmMute], (bool) note[tids::dead], (bool) note[tids::ghost], (int) note[tids::harmonic] == 1,
                           (int) note[tids::harmonic] == 2, (bool) note[tids::letRing], (bool) note[tids::tap], (bool) note[tids::tie],
                           (bool) note[tids::accent], (int) beat[tids::stroke] == 1, (int) beat[tids::stroke] == 2, (bool) beat[tids::tremolo] };
    for (int i = 0; i < techButtons.size(); ++i)
        techButtons[i]->setToggleState (noteFlags[i], juce::dontSendNotification);

    auto meas = tab::measures (score).getChild (editor.cursor.bar);
    const auto sig = juce::String ((int) meas.getProperty (tids::tsNum, 4)) + "/" + juce::String ((int) meas.getProperty (tids::tsDen, 4));
    sigCombo.setSelectedId (0, juce::dontSendNotification);
    for (int i = 0; i < sigCombo.getNumItems(); ++i)
        if (sigCombo.getItemText (i) == sig) sigCombo.setSelectedItemIndex (i, juce::dontSendNotification);
    if (sigCombo.getSelectedId() == 0) sigCombo.setText (sig, juce::dontSendNotification);

    if (editor.track().isValid())
    {
        const double content = tab::barContentQ (bar), len = tab::measureLengthQ (meas);
        juce::String st;
        st << "Bar " << editor.cursor.bar + 1 << " / " << tab::numMeasures (score) << dotSep() << "Beat " << editor.cursor.beat + 1
           << dotSep() << "String " << tab::numStrings (editor.track()) - editor.cursorString << dotSep()
           << "Bar holds " << juce::String (content, 2) << " of " << juce::String (len, 2) << " beats (quarters)";
        if (editor.hasSelection()) st << dotSep() << editor.selectedBeats().size() << " beats selected";
        status.setText (st, juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, content > len + 1e-6 ? palette::bad : palette::textDim);
    }
    else
        status.setText ({}, juce::dontSendNotification);
}

void TabPage::ensureCursorVisible()
{
    auto r = editor.cursorBounds();
    if (r.isEmpty()) return;
    const int top = viewport.getViewPositionY(), h = viewport.getViewHeight();
    if (r.getY() < top) viewport.setViewPosition (0, r.getY());
    else if (r.getBottom() > top + h) viewport.setViewPosition (0, r.getBottom() - h);
}

// ------------------------------------------------------------------ engine

void TabPage::setActive (bool shouldBeActive)
{
    if (active == shouldBeActive) return;
    active = shouldBeActive;
    if (active)
    {
        if (host.engine.isPlaying()) host.engine.stop();
        host.engine.tabMode = true;
        rebuildSequence();
        pushEngineOverrides();
        editor.grabKeyboardFocus();
    }
    else
    {
        restoreEngine();
    }
}

void TabPage::pushEngineOverrides()
{
    if (! active) return;
    auto& e = host.engine;
    const double tempo = (double) score.getProperty (tids::tempo, 120.0) * speed;
    e.setTempo (tempo);
    auto m0 = tab::measures (score).getChild (editor.cursor.bar);
    e.setTimeSignature ((int) m0.getProperty (tids::tsNum, 4), (int) m0.getProperty (tids::tsDen, 4));
    e.clickWhilePlaying = clickButton.getToggleState();
    e.punchEnabled = false;
    if (loopButton.getToggleState())
    {
        const auto [a, b] = editor.selectionSpanQ();
        const double spq = 60.0 / tempo;
        e.loopStartSec = a * spq;
        e.loopEndSec = b * spq;
        e.loopEnabled = true;
    }
    else
        e.loopEnabled = false;
}

void TabPage::restoreEngine()
{
    auto& e = host.engine;
    if (e.isPlaying()) e.stop();
    e.tabMode = false;
    doc.refreshEngine(); // project tempo, meter, click and loop settings again
}

void TabPage::rebuildSequence()
{
    host.engine.submitTabSequence (buildTabSequence (score, speed));
}

void TabPage::togglePlay()
{
    auto& e = host.engine;
    if (e.isPlaying()) { e.stop(); return; }
    pushEngineOverrides();
    const double tempo = (double) score.getProperty (tids::tempo, 120.0) * speed;
    const double spq = 60.0 / tempo;
    const double startQ = loopButton.getToggleState() ? editor.selectionSpanQ().first : editor.cursorQ();
    e.locateSeconds (startQ * spq);
    juce::int64 countIn = 0;
    if (countButton.getToggleState() && e.getSampleRate() > 0)
    {
        auto meas = tab::measures (score).getChild (editor.cursor.bar);
        countIn = (juce::int64) ((int) meas.getProperty (tids::tsNum, 4)
                                 * MetronomeDsp::beatLengthSamples (e.getSampleRate(), tempo, (int) meas.getProperty (tids::tsDen, 4)));
    }
    e.playWithCountIn (countIn);
    editor.grabKeyboardFocus();
}

void TabPage::timerCallback()
{
    if (! active) return;
    auto& e = host.engine;
    const bool playing = e.isPlaying();
    playButton.setButtonText (playing ? "Stop" : "Play");
    playButton.setToggleState (playing, juce::dontSendNotification);
    if (playing && ! e.isCountingIn())
    {
        const double tempo = (double) score.getProperty (tids::tempo, 120.0) * speed;
        editor.setPlayheadQ (e.getPositionSeconds() * tempo / 60.0);
    }
    else
        editor.setPlayheadQ (-1.0);
}

// ------------------------------------------------------------------ dialogs

void TabPage::message (const juce::String& title, const juce::String& text)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle (title).withMessage (text).withButton ("OK").withAssociatedComponent (this),
                                  nullptr);
}

void TabPage::showAddTrackDialog()
{
    auto* w = new juce::AlertWindow ("Add tab track", "Choose the instrument and tuning.", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", "Guitar " + juce::String (tab::tracks (score).getNumChildren() + 1), "Name");
    juce::StringArray names;
    for (auto& t : theory::tuningPresets()) names.add (t.category + ":  " + t.name);
    w->addComboBox ("tuning", names, "Instrument & tuning");
    w->addButton ("Add", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<TabPage> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r) {
        if (safe == nullptr || r != 1) return;
        const auto& preset = theory::tuningPresets()[(size_t) juce::jmax (0, w->getComboBoxComponent ("tuning")->getSelectedItemIndex())];
        auto name = w->getTextEditorContents ("name").trim();
        if (name.isEmpty()) name = preset.name;
        safe->doc.undo().beginNewTransaction ("Add tab track");
        tab::addTrack (safe->score, name, preset.strings, preset.category.startsWith ("Bass") ? "bass" : "guitar", &safe->doc.undo());
        safe->refreshTracks();
        safe->editor.setTrackIndex (tab::tracks (safe->score).getNumChildren() - 1);
        safe->refreshTracks();
    }), true);
}

void TabPage::showTuningDialog()
{
    auto t = editor.track();
    if (! t.isValid()) return;
    const auto current = tab::trackTuning (t);
    auto* w = new juce::AlertWindow ("Tuning: " + t[tids::name].toString(),
                                     "Pick a preset or type the open strings from lowest to highest (e.g. D2 A2 D3 G3 B3 E4).",
                                     juce::MessageBoxIconType::NoIcon, this);
    juce::StringArray presetNames { "(custom)" };
    std::vector<const theory::Tuning*> matching;
    for (auto& p : theory::tuningPresets())
        if (p.strings.size() == current.size()) { presetNames.add (p.name); matching.push_back (&p); }
    w->addComboBox ("preset", presetNames, "Preset");
    juce::StringArray noteNames;
    for (int n : current) noteNames.add (theory::midiNoteName (n));
    w->addTextEditor ("notes", noteNames.joinIntoString (" "), "Strings");
    juce::StringArray capos { "No capo" };
    for (int i = 1; i <= 12; ++i) capos.add ("Capo " + juce::String (i));
    w->addComboBox ("capo", capos, "Capo");
    w->getComboBoxComponent ("capo")->setSelectedItemIndex ((int) t[tids::capo]);
    w->getComboBoxComponent ("preset")->setSelectedItemIndex (0);
    auto* presetBox = w->getComboBoxComponent ("preset");
    presetBox->onChange = [w, presetBox, matching] {
        const int i = presetBox->getSelectedItemIndex() - 1;
        if (! juce::isPositiveAndBelow (i, (int) matching.size())) return;
        juce::StringArray nn;
        for (int n : matching[(size_t) i]->strings) nn.add (theory::midiNoteName (n));
        if (auto* ed = w->getTextEditor ("notes")) ed->setText (nn.joinIntoString (" "));
    };
    w->addButton ("Apply", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<TabPage> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, t] (int r) mutable {
        if (safe == nullptr || r != 1) return;
        std::vector<int> notes;
        for (auto& tok : juce::StringArray::fromTokens (w->getTextEditorContents ("notes"), " ,", ""))
        {
            const int n = theory::parseNoteName (tok);
            if (n < 0) { safe->message ("Tuning", "\"" + tok + "\" is not a note name like E2 or F#1."); return; }
            notes.push_back (n);
        }
        if (notes.size() != tab::trackTuning (t).size())
        {
            safe->message ("Tuning", "Enter exactly " + juce::String ((int) tab::trackTuning (t).size()) + " notes (one per string).");
            return;
        }
        std::sort (notes.begin(), notes.end());
        safe->doc.undo().beginNewTransaction ("Change tuning");
        safe->doc.set (t, tids::tuning, tab::tuningToString (notes));
        safe->doc.set (t, tids::capo, w->getComboBoxComponent ("capo")->getSelectedItemIndex());
    }), true);
}

void TabPage::showTrackMenu()
{
    auto t = editor.track();
    if (! t.isValid()) return;
    juce::PopupMenu m;
    m.addItem (1, "Rename...");
    m.addItem (2, "Mute", true, (bool) t[tids::mute]);
    m.addItem (3, "Solo", true, (bool) t[tids::solo]);
    juce::PopupMenu vol;
    for (int pct : { 100, 80, 60, 40, 20 })
        vol.addItem (100 + pct, juce::String (pct) + "%", true, std::abs ((double) t[tids::volume] * 100.0 - pct) < 1.0);
    m.addSubMenu ("Volume", vol);
    m.addSeparator();
    m.addItem (4, "Delete track");
    juce::Component::SafePointer<TabPage> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&trackMenuButton), [safe, t] (int r) mutable {
        if (safe == nullptr || r == 0) return;
        auto& um = safe->doc.undo();
        if (r == 1)
        {
            auto* w = new juce::AlertWindow ("Rename track", {}, juce::MessageBoxIconType::NoIcon, safe.getComponent());
            w->addTextEditor ("name", t[tids::name].toString());
            w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, t] (int res) mutable {
                const auto n = w->getTextEditorContents ("name").trim();
                if (safe != nullptr && res == 1 && n.isNotEmpty())
                {
                    safe->doc.undo().beginNewTransaction ("Rename track");
                    safe->doc.set (t, tids::name, n);
                }
            }), true);
        }
        else if (r == 2) safe->doc.set (t, tids::mute, ! (bool) t[tids::mute], false);
        else if (r == 3) safe->doc.set (t, tids::solo, ! (bool) t[tids::solo], false);
        else if (r >= 100) safe->doc.set (t, tids::volume, (r - 100) / 100.0, false);
        else if (r == 4)
        {
            um.beginNewTransaction ("Delete tab track");
            tab::tracks (safe->score).removeChild (t, &um);
            safe->editor.setTrackIndex (safe->editor.getTrackIndex());
        }
    });
}

void TabPage::importDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Import MIDI or ASCII tab", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                   "*.mid;*.midi;*.txt;*.tab");
    juce::Component::SafePointer<TabPage> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc) {
        if (safe == nullptr || fc.getResult() == juce::File()) return;
        const auto f = fc.getResult();
        safe->doc.undo().beginNewTransaction ("Import " + f.getFileName());
        tab::ImportResult r;
        if (f.hasFileExtension ("mid;midi"))
            r = tab::importMidi (safe->score, f, &safe->doc.undo());
        else if (f.getSize() > 4 * 1024 * 1024)
            r.result = juce::Result::fail ("That text file is too large to be a tab.");
        else
            r = tab::importAsciiTab (safe->score, f.loadFileAsString(), f.getFileNameWithoutExtension(), &safe->doc.undo());
        if (r.result.failed()) { safe->message ("Import failed", r.result.getErrorMessage()); return; }
        safe->refreshTracks();
        safe->editor.setTrackIndex (tab::tracks (safe->score).getNumChildren() - 1);
        safe->refreshTracks();
        juce::String msg = "Added " + juce::String (r.tracksAdded) + (r.tracksAdded == 1 ? " track." : " tracks.");
        if (! r.warnings.isEmpty()) msg << "\n\n" << r.warnings.joinIntoString ("\n");
        safe->message ("Import complete", msg);
    });
}

void TabPage::exportDialog()
{
    const auto start = doc.hasLocation() ? doc.getBundle().getParentDirectory() : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Export MIDI", start.getChildFile (juce::File::createLegalFileName (doc.tree()["name"].toString()) + ".mid"), "*.mid");
    juce::Component::SafePointer<TabPage> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fc) {
                              if (safe == nullptr || fc.getResult() == juce::File()) return;
                              const auto f = fc.getResult().withFileExtension (".mid");
                              const auto r = tab::exportMidi (safe->score, f);
                              safe->message (r.wasOk() ? "Export complete" : "Export failed", r.wasOk() ? "Wrote " + f.getFullPathName() : r.getErrorMessage());
                          });
}

// ------------------------------------------------------------------ layout

void TabPage::paint (juce::Graphics& g)
{
    g.fillAll (palette::bg);
    g.setColour (palette::panel);
    g.fillRect (getLocalBounds().removeFromTop (84));
    g.setColour (palette::outline);
    g.drawHorizontalLine (42, 0.0f, (float) getWidth());
    g.drawHorizontalLine (84, 0.0f, (float) getWidth());
}

void TabPage::resized()
{
    auto r = getLocalBounds();
    auto row1 = r.removeFromTop (42).reduced (8, 7);
    trackCombo.setBounds (row1.removeFromLeft (230));
    row1.removeFromLeft (6);
    addTrackButton.setBounds (row1.removeFromLeft (72));
    row1.removeFromLeft (4);
    tuningButton.setBounds (row1.removeFromLeft (74));
    row1.removeFromLeft (4);
    trackMenuButton.setBounds (row1.removeFromLeft (64));
    row1.removeFromLeft (14);
    tempoLabel.setBounds (row1.removeFromLeft (44));
    tempoSlider.setBounds (row1.removeFromLeft (92));
    row1.removeFromLeft (10);
    speedLabel.setBounds (row1.removeFromLeft (44));
    speedCombo.setBounds (row1.removeFromLeft (76));
    row1.removeFromLeft (10);
    playButton.setBounds (row1.removeFromLeft (64));
    row1.removeFromLeft (4);
    loopButton.setBounds (row1.removeFromLeft (52));
    row1.removeFromLeft (4);
    clickButton.setBounds (row1.removeFromLeft (52));
    row1.removeFromLeft (4);
    countButton.setBounds (row1.removeFromLeft (72));
    exportButton.setBounds (row1.removeFromRight (104));
    row1.removeFromRight (4);
    importButton.setBounds (row1.removeFromRight (78));

    auto row2 = r.removeFromTop (42).reduced (8, 8);
    for (auto* b : durButtons) { b->setBounds (row2.removeFromLeft (38)); row2.removeFromLeft (2); }
    row2.removeFromLeft (4);
    dotButton.setBounds (row2.removeFromLeft (26)); row2.removeFromLeft (2);
    tripletButton.setBounds (row2.removeFromLeft (26)); row2.removeFromLeft (2);
    restButton.setBounds (row2.removeFromLeft (44));
    row2.removeFromLeft (12);
    transUpButton.setBounds (row2.removeFromRight (34)); row2.removeFromRight (2);
    transDownButton.setBounds (row2.removeFromRight (34)); row2.removeFromRight (8);
    sigCombo.setBounds (row2.removeFromRight (70)); row2.removeFromRight (4);
    measDelButton.setBounds (row2.removeFromRight (52)); row2.removeFromRight (2);
    measAddButton.setBounds (row2.removeFromRight (52)); row2.removeFromRight (10);
    const int tw = juce::jmax (30, (row2.getWidth() - 2 * techButtons.size()) / juce::jmax (1, techButtons.size()));
    for (auto* b : techButtons) { b->setBounds (row2.removeFromLeft (tw)); row2.removeFromLeft (2); }

    status.setBounds (r.removeFromBottom (24).reduced (10, 0));
    viewport.setBounds (r);
    editor.setSize (r.getWidth() - viewport.getScrollBarThickness(), juce::jmax (editor.getHeight(), 100));
}
} // namespace ff
