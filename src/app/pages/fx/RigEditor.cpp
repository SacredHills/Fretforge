#include "RigEditor.h"
#include "engine/fx/NamModel.h"

namespace ff
{
using namespace ff::rig;
namespace tlids = ff::timeline::ids;

namespace
{
    const char* blockHelpText (int b)
    {
        switch (b)
        {
            case gate:   return "Mutes hiss and hum between notes. Raise the threshold until the noise disappears while you are not playing.";
            case comp:   return "Evens out picking dynamics and adds sustain. Mostly for clean and funk sounds, before the amp.";
            case drive:  return "A mid-focused overdrive pedal. With low drive and high level it tightens a high-gain amp.";
            case amp:    return "Load a Neural Amp Modeler capture (.nam) of a real amp or pedal, or use the built-in amp.";
            case eq:     return "Tone controls after the amp: bass 120 Hz, mid 650 Hz, treble 2.8 kHz, presence 5.5 kHz.";
            case cab:    return "A speaker cabinet. Load an impulse response (.wav) for a real cabinet; most NAM amp captures need one.";
            case chorus: return "Stereo chorus. From here on the signal is stereo.";
            case delay:  return "Echo with darker repeats as the tone is lowered. The right side repeats 11 ms later for width.";
            case reverb: return "Room / hall reverb.";
            default:     return "";
        }
    }

    juce::String shortPath (const juce::File& f, const juce::File& base)
    {
        return f.isAChildOf (base) ? f.getRelativePathFrom (base) : f.getFileName();
    }
}

// ================================================================ ParamKnob

ParamKnob::ParamKnob()
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (slider);
    for (auto* l : { &name, &value })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (l);
    }
    styleLabel (name, 12.5f, palette::text, true);
    styleLabel (value, 12.0f, palette::textDim);
    slider.onDragStart = [this] { if (undo != nullptr) undo->beginNewTransaction ("Change " + juce::String (info (p).label)); };
    slider.onValueChange = [this] {
        if (updating || ! tree.isValid()) return;
        set (tree, p, (float) slider.getValue(), undo);
        value.setText (formatValue (p, (float) slider.getValue()), juce::dontSendNotification);
    };
}

void ParamKnob::bind (juce::ValueTree t, int param, juce::UndoManager* u)
{
    tree = t;
    p = param;
    undo = u;
    const auto& pi = info (p);
    const juce::ScopedValueSetter<bool> svs (updating, true);
    slider.setRange (pi.min, pi.max, 0.0);
    if (std::abs (pi.centre - (pi.min + pi.max) * 0.5f) > 1.0e-3f)
        slider.setSkewFactorFromMidPoint (pi.centre);
    else
        slider.setSkewFactor (1.0);
    slider.setDoubleClickReturnValue (true, pi.def);
    name.setText (pi.label, juce::dontSendNotification);
    refresh();
}

void ParamKnob::refresh()
{
    if (! tree.isValid()) return;
    const juce::ScopedValueSetter<bool> svs (updating, true);
    const float v = get (tree, p);
    slider.setValue (v, juce::dontSendNotification);
    value.setText (formatValue (p, v), juce::dontSendNotification);
}

void ParamKnob::resized()
{
    auto r = getLocalBounds();
    name.setBounds (r.removeFromTop (16));
    value.setBounds (r.removeFromBottom (16));
    slider.setBounds (r.reduced (4, 0));
}

// ================================================================ ChainView

juce::Rectangle<float> ChainView::tileRect (int index) const
{
    const float gap = 12.0f;
    const float w = ((float) getWidth() - gap * (float) (numBlocks - 1)) / (float) numBlocks;
    return { (float) index * (w + gap), 0.0f, w, (float) getHeight() };
}

int ChainView::indexAt (float x) const
{
    for (int i = 0; i < numBlocks; ++i)
        if (x < tileRect (i).getRight() + 6.0f) return i;
    return numBlocks - 1;
}

void ChainView::paint (juce::Graphics& g)
{
    if (! tree.isValid()) return;
    const auto order = getOrder (tree);
    for (int i = 0; i < numBlocks; ++i)
    {
        const int b = order[(size_t) i];
        auto r = tileRect (i);
        if (i > 0)
        {
            g.setColour (palette::outline);
            const float y = r.getCentreY();
            g.drawArrow ({ r.getX() - 11.0f, y, r.getX() - 1.0f, y }, 1.5f, 6.0f, 5.0f);
        }
        const bool on = get (tree, switchFor (b)) >= 0.5f;
        const bool sel = b == selected;
        g.setColour (b == dragBlock ? palette::panelHi.brighter (0.1f) : (sel ? palette::panelHi : palette::panel));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (sel ? palette::accent : palette::outline);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, sel ? 2.0f : 1.0f);

        auto lamp = lampRect (i);
        g.setColour (on ? palette::good : palette::well);
        g.fillEllipse (lamp);
        g.setColour (palette::outline);
        g.drawEllipse (lamp, 1.0f);

        auto text = r.reduced (8.0f, 6.0f);
        g.setColour (on ? palette::text : palette::textDim);
        g.setFont (uiFont (13.5f, true));
        g.drawText (blockName (b), text.removeFromTop (20.0f).withTrimmedRight (16.0f), juce::Justification::centredLeft, true);
        g.setFont (uiFont (11.5f));
        g.setColour (palette::textDim);
        juce::String sub;
        if (b == amp) sub = ampText;
        else if (b == cab) sub = cabText;
        else if (b == gate) sub = formatValue (gateThresh, get (tree, gateThresh));
        else if (b == drive) sub = "Drive " + formatValue (driveGain, get (tree, driveGain));
        else if (b == delay) sub = formatValue (delayTime, get (tree, delayTime));
        else if (b == reverb) sub = "Mix " + formatValue (reverbMix, get (tree, reverbMix));
        else if (b == chorus) sub = formatValue (chorusRate, get (tree, chorusRate));
        else if (b == comp) sub = formatValue (compRatio, get (tree, compRatio));
        else if (b == eq) sub = "4-band";
        g.drawFittedText (sub, text.toNearestInt(), juce::Justification::topLeft, 3, 0.9f);
    }
    if (dragBlock >= 0 && dropIndex >= 0)
    {
        auto r = tileRect (dropIndex);
        g.setColour (palette::accent);
        g.fillRect (juce::Rectangle<float> (r.getX() - 7.0f, 0.0f, 3.0f, (float) getHeight()));
    }
}

void ChainView::mouseDown (const juce::MouseEvent& e)
{
    if (! tree.isValid()) return;
    const int i = indexAt ((float) e.x);
    const int b = getOrder (tree)[(size_t) i];
    dragBlock = -1;
    dropIndex = -1;
    if (lampRect (i).expanded (4.0f).contains (e.position)) { if (onToggle) onToggle (b); return; }
    if (onSelect) onSelect (b);
}

void ChainView::mouseDrag (const juce::MouseEvent& e)
{
    if (! tree.isValid() || e.getDistanceFromDragStartX() == 0 || std::abs (e.getDistanceFromDragStartX()) < 8) return;
    const int from = indexAt ((float) e.getMouseDownX());
    dragBlock = getOrder (tree)[(size_t) from];
    dropIndex = indexAt ((float) e.x);
    repaint();
}

void ChainView::mouseUp (const juce::MouseEvent&)
{
    if (dragBlock >= 0 && dropIndex >= 0 && onMove) onMove (dragBlock, dropIndex);
    dragBlock = dropIndex = -1;
    repaint();
}

// ================================================================ RigMeter

void RigMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (palette::well);
    g.fillRoundedRectangle (r, 2.0f);
    const float n = juce::jlimit (0.0f, 1.0f, (level + 60.0f) / 60.0f);
    g.setColour (level > -1.0f ? palette::bad : (level > -9.0f ? palette::warn : palette::good));
    g.fillRoundedRectangle (r.withWidth (r.getWidth() * n), 2.0f);
}

// ================================================================ RigEditor

RigEditor::RigEditor (ProjectDocument& d, EngineHost& h, SessionController& s, juce::PropertiesFile& st, const juce::String& trackId)
    : doc (d), host (h), session (s), settings (st), targetTrackId (trackId), trackMode (trackId.isNotEmpty())
{
    for (auto* c : { &inputCombo, &presetCombo })
        addAndMakeVisible (c);
    styleLabel (titleLabel, 16.0f, palette::text, true);
    addAndMakeVisible (titleLabel);
    inputCombo.setTooltip ("Input the live rig plays");
    inputCombo.onChange = [this] {
        if (targetTrackId.isEmpty() && target.isValid())
            target.setProperty (ids::input, inputCombo.getSelectedId() - 2, nullptr);
    };
    presetCombo.setTextWhenNothingSelected ("Presets");
    presetCombo.onChange = [this] { applyPresetId (presetCombo.getSelectedId()); };

    onButton.setClickingTogglesState (false);
    onButton.onClick = [this] {
        if (! target.isValid()) return;
        setOnTarget (ids::enabled, ! rig::isEnabled (target), targetTrackId.isEmpty() ? "" : "Amp rig on/off");
    };
    monitorButton.setButtonText ("Monitor input");
    monitorButton.setTooltip ("Hear the track's input through its rig (same as the track's monitor button in the Studio)");
    monitorButton.onClick = [this] {
        auto t = session.findTrack (targetTrackId);
        if (t.isValid()) doc.set (t, tlids::monitor, ! (bool) t[tlids::monitor]);
    };
    addRigButton.onClick = [this] {
        auto t = session.findTrack (targetTrackId);
        if (! t.isValid()) return;
        doc.undo().beginNewTransaction ("Add amp rig");
        auto r = rig::find (t);
        if (! r.isValid()) { r = makeDefault(); t.appendChild (r, &doc.undo()); }
        else r.setProperty (ids::enabled, true, &doc.undo());
        selectTarget (targetTrackId);
    };
    savePresetButton.onClick = [this] { savePreset(); };
    deletePresetButton.onClick = [this] { deletePreset(); };
    resetButton.setTooltip ("All controls back to their defaults (keeps the model and IR)");
    resetButton.onClick = [this] {
        if (! target.isValid()) return;
        if (auto* u = um()) u->beginNewTransaction ("Reset rig");
        for (int p = 0; p < numParams; ++p) set (target, p, info (p).def, um());
        setOrder (target, defaultOrder(), um());
        target.setProperty (ids::rigName, juce::String(), um());
    };
    for (auto* b : { &onButton, &monitorButton, &addRigButton, &savePresetButton, &deletePresetButton, &resetButton })
        addAndMakeVisible (b);

    for (auto* l : { &targetHint, &statusLabel, &inLabel, &outLabel, &blockTitle, &blockHelp, &modelLabel, &modelInfo, &irLabel })
        addAndMakeVisible (l);
    styleLabel (targetHint, 12.5f);
    styleLabel (statusLabel, 12.5f);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    styleLabel (inLabel, 11.5f);
    styleLabel (outLabel, 11.5f);
    inLabel.setText ("IN", juce::dontSendNotification);
    outLabel.setText ("OUT", juce::dontSendNotification);
    styleLabel (blockTitle, 17.0f, palette::text, true);
    styleLabel (blockHelp, 12.5f);
    styleLabel (modelLabel, 15.0f, palette::accent, true);
    styleLabel (modelInfo, 12.5f);
    modelInfo.setJustificationType (juce::Justification::topLeft);
    styleLabel (irLabel, 13.0f, palette::text);
    irLabel.setJustificationType (juce::Justification::topLeft);

    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (inKnob);
    addAndMakeVisible (outKnob);

    addAndMakeVisible (chain);
    chain.onSelect = [this] (int b) { selectBlock (b); };
    chain.onToggle = [this] (int b) {
        if (! target.isValid()) return;
        if (auto* u = um()) u->beginNewTransaction (juce::String (blockName (b)) + " on/off");
        set (target, switchFor (b), get (target, switchFor (b)) >= 0.5f ? 0.0f : 1.0f, um());
        selectBlock (b);
    };
    chain.onMove = [this] (int b, int newIndex) {
        if (! target.isValid()) return;
        auto o = getOrder (target);
        std::vector<int> v (o.begin(), o.end());
        v.erase (std::find (v.begin(), v.end(), b));
        v.insert (v.begin() + juce::jlimit (0, (int) v.size(), newIndex), b);
        std::copy (v.begin(), v.end(), o.begin());
        if (auto* u = um()) u->beginNewTransaction ("Move " + juce::String (blockName (b)));
        setOrder (target, o, um());
    };

    blockOnButton.onClick = [this] { if (chain.onToggle) chain.onToggle (chain.selected); };
    moveLeftButton.setTooltip ("Move this block earlier in the chain");
    moveRightButton.setTooltip ("Move this block later in the chain");
    moveLeftButton.onClick = [this] { if (target.isValid()) { if (auto* u = um()) u->beginNewTransaction ("Move block"); moveBlock (target, chain.selected, -1, um()); } };
    moveRightButton.onClick = [this] { if (target.isValid()) { if (auto* u = um()) u->beginNewTransaction ("Move block"); moveBlock (target, chain.selected, +1, um()); } };
    for (auto* b : { &blockOnButton, &moveLeftButton, &moveRightButton, &loadModelButton, &clearModelButton,
                     &normaliseButton, &calibrateButton, &loadIrButton, &clearIrButton })
        addChildComponent (b);

    loadModelButton.onClick = [this] {
        const auto start = juce::File (settings.getValue ("namFolder", juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName()));
        chooser = std::make_unique<juce::FileChooser> ("Load a Neural Amp Modeler model", start, "*.nam");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) setModelFile (fc.getResult()); });
    };
    clearModelButton.onClick = [this] { setModelFile ({}); };
    calibrateButton.setTooltip ("Matches your interface's input level to the level the capture was recorded at, so the amp's gain "
                                "responds like the real one (needs a capture that stores its input level)");
    calibrateButton.onClick = [this] {
        if (! target.isValid()) return;
        if (auto* u = um()) u->beginNewTransaction ("Calibrate input");
        set (target, namCalibrate, get (target, namCalibrate) >= 0.5f ? 0.0f : 1.0f, um());
    };
    normaliseButton.setTooltip ("Levels every model to the same loudness (uses the loudness stored in the .nam file)");
    normaliseButton.onClick = [this] {
        if (! target.isValid()) return;
        if (auto* u = um()) u->beginNewTransaction ("Normalise loudness");
        set (target, namNormalize, get (target, namNormalize) >= 0.5f ? 0.0f : 1.0f, um());
    };
    loadIrButton.onClick = [this] {
        const auto start = juce::File (settings.getValue ("irFolder", juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName()));
        chooser = std::make_unique<juce::FileChooser> ("Load a cabinet impulse response", start, "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) setIrFile (fc.getResult()); });
    };
    clearIrButton.onClick = [this] { setIrFile ({}); };

    models.onPick = [this] (const juce::File& f) { setModelFile (f); };
    irs.onPick = [this] (const juce::File& f) { setIrFile (f); };
    for (auto* b : { &models, &irs })
    {
        b->extraButton.setButtonText ("TONE3000...");
        b->extraButton.setTooltip ("Browse and download from TONE3000");
        addChildComponent (b);
    }
    models.extraButton.onClick = [this] { if (onBrowseTone3000) onBrowseTone3000 (false); };
    irs.extraButton.onClick = [this] { if (onBrowseTone3000) onBrowseTone3000 (true); };

    session.rigs().onStatusChanged = [this] { refreshStatus(); };
    rebuildPresets();
    rebind();
    startTimerHz (30);
}

RigEditor::~RigEditor()
{
    session.rigs().onStatusChanged = nullptr;
    root.removeListener (this);
    liveTree.removeListener (this);
}

void RigEditor::rebind()
{
    root.removeListener (this);
    root = doc.tree();
    root.addListener (this);
    liveTree.removeListener (this);
    liveTree = session.rigs().liveTree();
    liveTree.addListener (this);
    if (trackMode && ! session.findTrack (targetTrackId).isValid())
    {
        target = {};
        if (onTrackGone) juce::MessageManager::callAsync ([cb = onTrackGone] { cb(); });
        return;
    }
    selectTarget (targetTrackId);
}

void RigEditor::setActive (bool a)
{
    active = a;
    if (a) selectTarget (targetTrackId);
}

juce::String RigEditor::windowTitle() const
{
    if (! trackMode) return "Live rig";
    auto t = session.findTrack (targetTrackId);
    return "NAM amp - " + (t.isValid() ? t[tlids::name].toString() : juce::String ("track"));
}

void RigEditor::rebuildInputs()
{
    if (trackMode)
    {
        auto t = session.findTrack (targetTrackId);
        const int idx = doc.tracks().indexOf (t);
        titleLabel.setText ("Track " + juce::String (idx + 1) + ": " + t[tlids::name].toString(), juce::dontSendNotification);
        if (auto* w = findParentComponentOfClass<juce::DocumentWindow>()) w->setName (windowTitle());
    }
    else titleLabel.setText ("Live input (practice rig)", juce::dontSendNotification);

    inputCombo.clear (juce::dontSendNotification);
    inputCombo.addItem ("Tuner input", 1);
    const auto names = host.activeInputChannelNames();
    for (int c = 0; c < names.size(); ++c) inputCombo.addItem ("In " + juce::String (c + 1) + ": " + names[c], c + 2);
    inputCombo.setSelectedId ((int) session.rigs().liveTree().getProperty (ids::input, -1) + 2, juce::dontSendNotification);
}

void RigEditor::selectTarget (const juce::String& trackId)
{
    targetTrackId = trackId;
    if (trackId.isEmpty()) target = session.rigs().liveTree();
    else
    {
        auto r = rig::find (session.findTrack (trackId));
        target = rig::isEnabled (r) ? r : juce::ValueTree();
    }
    rebuildInputs();
    chain.setRig (target);
    inKnob.bind (target, inGain, um());
    outKnob.bind (target, outGain, um());
    selectBlock (chain.selected);
    refreshAll();
}

void RigEditor::selectBlock (int b)
{
    chain.selected = b;
    knobs.clear();
    if (target.isValid())
        for (int p = 0; p < numParams; ++p)
        {
            const auto& pi = info (p);
            if (pi.block != b || pi.isSwitch || p == namNormalize) continue;
            if (p == ampGain && ! target[ids::namFile].toString().isEmpty()) continue;   // built-in amp only
            if (p == calLevel && (target[ids::namFile].toString().isEmpty() || get (target, namCalibrate) < 0.5f)) continue;
            if (p == namCalibrate) continue;
            auto* k = knobs.add (new ParamKnob());
            k->bind (target, p, um());
            addAndMakeVisible (k);
        }
    refreshAll();
}

void RigEditor::refreshAll()
{
    const bool live = targetTrackId.isEmpty();
    const bool has = target.isValid();
    const auto status = session.rigs().statusFor (target);

    onButton.setVisible (has);
    onButton.setButtonText (live ? (rig::isEnabled (target) ? "Live monitoring ON" : "Live monitoring off")
                                 : (rig::isEnabled (target) ? "Rig ON" : "Rig off"));
    onButton.setToggleState (has && rig::isEnabled (target), juce::dontSendNotification);
    inputCombo.setVisible (live);
    auto track = session.findTrack (targetTrackId);
    monitorButton.setVisible (! live && has);
    monitorButton.setToggleState (track.isValid() && (bool) track[tlids::monitor], juce::dontSendNotification);
    addRigButton.setVisible (! live && ! has);
    for (auto* c : std::initializer_list<juce::Component*> { &presetCombo, &savePresetButton, &deletePresetButton, &resetButton, &chain,
                                                             &inKnob, &outKnob, &inMeter, &outMeter, &inLabel, &outLabel, &blockTitle, &blockHelp })
        c->setVisible (has);

    targetHint.setText (live ? "Live rig: your guitar input -> rig -> speakers, whatever the transport is doing. Use headphones if you play through a microphone."
                             : (has ? "Track insert: shapes this track's recorded audio and its live monitoring. Recordings stay dry, so you can change the amp later."
                                    : "This track has no amp rig. Add one to play and record through an amp, or to re-amp what is already recorded."),
                        juce::dontSendNotification);

    // chain subtitles
    if (status.modelLoading) chain.ampText = "Loading model...";
    else if (status.modelLoaded) chain.ampText = "NAM: " + status.modelInfo.displayName();
    else if (status.modelError.isNotEmpty()) chain.ampText = "Model failed - built-in amp";
    else chain.ampText = "Built-in amp";
    if (status.irLoading) chain.cabText = "Loading IR...";
    else if (status.irLoaded) chain.cabText = "IR: " + status.irName;
    else if (status.irError.isNotEmpty()) chain.cabText = "IR failed - built-in";
    else chain.cabText = "Built-in speaker";
    chain.repaint();

    // selected block
    const int b = chain.selected;
    blockTitle.setText (blockName (b), juce::dontSendNotification);
    blockHelp.setText (blockHelpText (b), juce::dontSendNotification);
    const bool on = has && get (target, switchFor (b)) >= 0.5f;
    blockOnButton.setButtonText (on ? "On" : "Off");
    blockOnButton.setToggleState (on, juce::dontSendNotification);
    for (auto* c : { &blockOnButton, &moveLeftButton, &moveRightButton }) c->setVisible (has);

    const bool isAmp = has && b == amp, isCab = has && b == cab;
    for (auto* c : std::initializer_list<juce::Component*> { &loadModelButton, &clearModelButton, &normaliseButton, &calibrateButton, &modelLabel,
                                                             &modelInfo, &models })
        c->setVisible (isAmp);
    for (auto* c : std::initializer_list<juce::Component*> { &loadIrButton, &clearIrButton, &irLabel, &irs })
        c->setVisible (isCab);
    for (auto* b : { &models, &irs })
        if (b->extraButton.isVisible() != (onBrowseTone3000 != nullptr)) { b->extraButton.setVisible (onBrowseTone3000 != nullptr); b->resized(); }
    normaliseButton.setToggleState (has && get (target, namNormalize) >= 0.5f, juce::dontSendNotification);
    calibrateButton.setToggleState (has && get (target, namCalibrate) >= 0.5f, juce::dontSendNotification);
    if (has)
    {
        models.setCurrent (juce::File (target[ids::namFile].toString().isEmpty() ? juce::String() : target[ids::namFile].toString()));
        irs.setCurrent (juce::File (target[ids::irFile].toString().isEmpty() ? juce::String() : target[ids::irFile].toString()));
    }

    if (isAmp)
    {
        juce::String text;
        if (status.modelLoading) modelLabel.setText ("Loading " + juce::File (status.modelPath).getFileName() + "...", juce::dontSendNotification);
        else if (status.modelLoaded) modelLabel.setText (status.modelInfo.displayName(), juce::dontSendNotification);
        else modelLabel.setText ("Built-in amp", juce::dontSendNotification);
        if (status.modelLoaded)
        {
            const auto& mi = status.modelInfo;
            juce::StringArray parts;
            if (mi.gearType.isNotEmpty()) parts.add (mi.gearType.substring (0, 1).toUpperCase() + mi.gearType.substring (1));
            if (mi.toneType.isNotEmpty()) parts.add (mi.toneType);
            parts.add (mi.architecture + (mi.modeledBy.isNotEmpty() ? ", captured by " + mi.modeledBy : juce::String()));
            text << parts.joinIntoString (" - ") << "\n";
            const double dev = host.engine.getSampleRate();
            text << "Trained at " << juce::String (mi.sampleRate / 1000.0, 1) << " kHz";
            if (dev > 0 && std::abs (dev - mi.sampleRate) > 0.5) text << ", resampled from " << juce::String (dev / 1000.0, 1) << " kHz";
            text << (mi.hasLoudness ? ", loudness " + juce::String (mi.loudness, 1) + " dB" : juce::String (", no loudness stored (normalising has no effect)"));
            text << "\n";
            if (mi.hasInputLevel)
            {
                text << "Captured with 0 dBFS = " << juce::String (mi.inputLevelDbu, 1) << " dBu at the input";
                if (get (target, namCalibrate) >= 0.5f)
                    text << "; calibrated: " << (get (target, calLevel) - mi.inputLevelDbu >= 0 ? "+" : "")
                         << juce::String (get (target, calLevel) - mi.inputLevelDbu, 1) << " dB into the model";
            }
            else text << "No input level stored in this capture: calibration has no effect";
            text << "\n" << status.modelPath;
        }
        else if (status.modelError.isNotEmpty())
            text << "Could not load the model: " << status.modelError << "\nThe built-in amp is used instead.";
        else
            text << "Two-stage overdriven preamp. For the sound of a real amp, load a NAM capture (.nam): thousands are free on TONE3000 (tone3000.com).";
        modelInfo.setText (text, juce::dontSendNotification);
        modelInfo.setColour (juce::Label::textColourId, status.modelError.isNotEmpty() ? palette::bad : palette::textDim);
        clearModelButton.setEnabled (status.modelPath.isNotEmpty());
        calibrateButton.setEnabled (status.modelLoaded);
    }
    if (isCab)
    {
        juce::String text;
        if (status.irLoading) text = "Loading " + juce::File (status.irPath).getFileName() + "...";
        else if (status.irLoaded) text = "Impulse response: " + status.irName + "  (" + juce::String (status.irSeconds * 1000.0, 0) + " ms)\n" + status.irPath;
        else if (status.irError.isNotEmpty()) text = "Could not load the IR: " + status.irError + "\nThe built-in speaker filter is used instead.";
        else text = "Built-in speaker filter (a basic 4x12-style curve). Load a cabinet impulse response (.wav) for a realistic cabinet.";
        irLabel.setText (text, juce::dontSendNotification);
        irLabel.setColour (juce::Label::textColourId, status.irError.isNotEmpty() ? palette::bad : palette::text);
        clearIrButton.setEnabled (status.irPath.isNotEmpty());
    }
    for (auto* k : knobs) k->refresh();
    inKnob.refresh();
    outKnob.refresh();
    resized();
    repaint();
}

void RigEditor::refreshStatus()
{
    refreshAll();
}

void RigEditor::setOnTarget (const juce::Identifier& id, const juce::var& v, const juce::String& undoName)
{
    if (! target.isValid()) return;
    if (auto* u = um(); u != nullptr && undoName.isNotEmpty()) u->beginNewTransaction (undoName);
    target.setProperty (id, v, um());
}

void RigEditor::setModelFile (const juce::File& f)
{
    if (! target.isValid()) return;
    setOnTarget (ids::namFile, f == juce::File() ? juce::String() : f.getFullPathName(), f == juce::File() ? "Use built-in amp" : "Load amp model");
    if (f != juce::File())
    {
        if (settings.getValue ("namFolder").isEmpty()) settings.setValue ("namFolder", f.getParentDirectory().getFullPathName());
        set (target, ampOn, 1.0f, um());
    }
    selectBlock (amp);
}

void RigEditor::setIrFile (const juce::File& f)
{
    if (! target.isValid()) return;
    setOnTarget (ids::irFile, f == juce::File() ? juce::String() : f.getFullPathName(), f == juce::File() ? "Use built-in cabinet" : "Load cabinet IR");
    if (f != juce::File())
    {
        settings.setValue ("irFolder", f.getParentDirectory().getFullPathName());
        set (target, cabOn, 1.0f, um());
    }
    refreshAll();
}

// ---------------------------------------------------------------- presets

juce::File RigEditor::presetFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("FretForge").getChildFile ("Rigs");
}

void RigEditor::rebuildPresets()
{
    presetCombo.clear (juce::dontSendNotification);
    presetCombo.addSectionHeading ("Built-in (no external files)");
    int id = 1;
    for (auto& p : factoryPresets()) presetCombo.addItem (p.name, id++);
    auto files = presetFolder().findChildFiles (juce::File::findFiles, false, "*.ffrig");
    files.sort();
    if (! files.isEmpty())
    {
        presetCombo.addSectionHeading ("My presets");
        for (int i = 0; i < files.size(); ++i) presetCombo.addItem (files[i].getFileNameWithoutExtension(), 100 + i);
    }
    const auto current = target.isValid() ? target[ids::rigName].toString() : juce::String();
    for (int i = 0; i < presetCombo.getNumItems(); ++i)
        if (presetCombo.getItemText (i) == current) presetCombo.setSelectedItemIndex (i, juce::dontSendNotification);
}

void RigEditor::applyPresetId (int id)
{
    if (! target.isValid() || id <= 0) return;
    juce::var json;
    if (id < 100)
    {
        const auto f = factoryPresets();
        if (id - 1 < (int) f.size()) json = f[(size_t) (id - 1)].json;
    }
    else
    {
        auto files = presetFolder().findChildFiles (juce::File::findFiles, false, "*.ffrig");
        files.sort();
        if (id - 100 < files.size()) json = juce::JSON::parse (files[id - 100]);
    }
    if (! json.isObject()) return;
    if (auto* u = um()) u->beginNewTransaction ("Load preset");
    const auto missing = applyPreset (target, json, um());
    if (missing.size() > 0)
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Preset loaded with missing files")
                                          .withMessage ("These files were not found, so the built-in amp / cabinet is used instead:\n" + missing.joinIntoString ("\n"))
                                          .withButton ("OK").withAssociatedComponent (this), nullptr);
    selectBlock (chain.selected);
}

void RigEditor::savePreset()
{
    if (! target.isValid()) return;
    auto* w = new juce::AlertWindow ("Save rig preset", "Saved in " + presetFolder().getFullPathName(), juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", target[ids::rigName].toString().isNotEmpty() ? target[ids::rigName].toString() : "My rig", "Name");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<RigEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r) {
        if (r != 1 || safe == nullptr) return;
        const auto name = juce::File::createLegalFileName (w->getTextEditorContents ("name").trim());
        if (name.isEmpty()) return;
        auto folder = safe->presetFolder();
        folder.createDirectory();
        const auto f = folder.getChildFile (name + ".ffrig");
        if (! f.replaceWithText (juce::JSON::toString (toPreset (safe->target, name))))
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Could not save").withMessage ("Writing " + f.getFullPathName() + " failed.")
                                              .withButton ("OK"), nullptr);
            return;
        }
        safe->target.setProperty (ids::rigName, name, safe->um());
        safe->rebuildPresets();
    }), true);
}

void RigEditor::deletePreset()
{
    const int id = presetCombo.getSelectedId();
    if (id < 100)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Delete preset").withMessage ("Select one of your own presets first (built-in presets cannot be deleted).")
                                          .withButton ("OK").withAssociatedComponent (this), nullptr);
        return;
    }
    auto files = presetFolder().findChildFiles (juce::File::findFiles, false, "*.ffrig");
    files.sort();
    if (id - 100 >= files.size()) return;
    const auto f = files[id - 100];
    juce::Component::SafePointer<RigEditor> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Delete preset").withMessage ("Delete \"" + f.getFileNameWithoutExtension() + "\"?")
                                      .withButton ("Delete").withButton ("Cancel").withAssociatedComponent (this),
                                  [safe, f] (int r) {
                                      if (r != 1 || safe == nullptr) return;
                                      if (! f.moveToTrash()) f.deleteFile();
                                      safe->rebuildPresets();
                                  });
}

// ---------------------------------------------------------------- layout

void RigEditor::paint (juce::Graphics& g)
{
    g.fillAll (palette::bg);
    if (target.isValid())
    {
        auto r = getLocalBounds().reduced (8);
        r.removeFromTop (34 + 6 + 22 + 8 + 116 + 10);
        g.setColour (palette::panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
    }
}

void RigEditor::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto row = r.removeFromTop (34);
    auto take = [&row] (juce::Component& c, int w, int gap = 6) { c.setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); };
    auto takeRight = [&row] (juce::Component& c, int w, int gap = 6) { c.setBounds (row.removeFromRight (w)); row.removeFromRight (gap); };
    take (titleLabel, 260);
    if (onButton.isVisible()) take (onButton, 160);
    if (inputCombo.isVisible()) take (inputCombo, 210);
    if (monitorButton.isVisible()) take (monitorButton, 120);
    if (resetButton.isVisible())
    {
        takeRight (resetButton, 64);
        takeRight (deletePresetButton, 64);
        takeRight (savePresetButton, 110);
        takeRight (presetCombo, 200);
    }
    r.removeFromTop (6);
    auto hint = r.removeFromTop (22);
    statusLabel.setBounds (hint.removeFromRight (360));
    targetHint.setBounds (hint);
    r.removeFromTop (8);

    if (! target.isValid())
    {
        addRigButton.setBounds (r.removeFromTop (40).withSizeKeepingCentre (260, 36));
        return;
    }

    auto chainRow = r.removeFromTop (116);
    auto left = chainRow.removeFromLeft (84);
    auto right = chainRow.removeFromRight (84);
    inKnob.setBounds (left.removeFromTop (92));
    inLabel.setBounds (left.removeFromLeft (26));
    inMeter.setBounds (left.withSizeKeepingCentre (left.getWidth() - 4, 8));
    outKnob.setBounds (right.removeFromTop (92));
    outLabel.setBounds (right.removeFromLeft (30));
    outMeter.setBounds (right.withSizeKeepingCentre (right.getWidth() - 4, 8));
    chain.setBounds (chainRow.reduced (10, 0));
    r.removeFromTop (10);
    layoutDetail (r.reduced (14, 10));
}

void RigEditor::layoutDetail (juce::Rectangle<int> r)
{
    auto title = r.removeFromTop (30);
    blockTitle.setBounds (title.removeFromLeft (180));
    blockOnButton.setBounds (title.removeFromLeft (64).reduced (0, 2));
    title.removeFromLeft (12);
    moveLeftButton.setBounds (title.removeFromLeft (32).reduced (0, 2));
    title.removeFromLeft (4);
    moveRightButton.setBounds (title.removeFromLeft (32).reduced (0, 2));
    blockHelp.setBounds (r.removeFromTop (22));
    r.removeFromTop (8);

    const int b = chain.selected;
    if (b == amp)
    {
        models.setBounds (r.removeFromRight (juce::jmin (440, r.getWidth() / 2)));
        r.removeFromRight (20);

        auto buttons = r.removeFromTop (30);
        loadModelButton.setBounds (buttons.removeFromLeft (120));
        buttons.removeFromLeft (6);
        clearModelButton.setBounds (buttons.removeFromLeft (136));
        buttons.removeFromLeft (6);
        normaliseButton.setBounds (buttons.removeFromLeft (150));
        buttons.removeFromLeft (6);
        calibrateButton.setBounds (buttons.removeFromLeft (124));
        r.removeFromTop (8);
        modelLabel.setBounds (r.removeFromTop (24));
        modelInfo.setBounds (r.removeFromTop (84));
        r.removeFromTop (6);
    }
    else if (b == cab)
    {
        irs.setBounds (r.removeFromRight (juce::jmin (440, r.getWidth() / 2)));
        r.removeFromRight (20);
        auto buttons = r.removeFromTop (30);
        loadIrButton.setBounds (buttons.removeFromLeft (120));
        buttons.removeFromLeft (6);
        clearIrButton.setBounds (buttons.removeFromLeft (170));
        r.removeFromTop (8);
        irLabel.setBounds (r.removeFromTop (60));
        r.removeFromTop (6);
    }
    auto knobRow = r.removeFromTop (110);
    for (auto* k : knobs)
    {
        k->setBounds (knobRow.removeFromLeft (96));
        knobRow.removeFromLeft (8);
    }
}

// ---------------------------------------------------------------- live updates

void RigEditor::timerCallback()
{
    if (! active || ! target.isValid()) return;
    if (auto* rigPtr = session.rigs().rigFor (target))
    {
        inMeter.push (rigPtr->inPeak.exchange (0.0f));
        outMeter.push (rigPtr->outPeak.exchange (0.0f));
        const bool running = targetTrackId.isEmpty() ? rig::isEnabled (target) : true;
        juce::String s;
        if (running)
        {
            s << "Rig CPU " << juce::String (rigPtr->cpuLoad.load() * 100.0f, 1) << " %";
            const double sr = host.engine.getSampleRate();
            if (const int lat = rigPtr->latency.load(); lat > 0 && sr > 0)
                s << "  -  resampling adds " << juce::String (lat * 1000.0 / sr, 2) << " ms";
            if (get (target, gateOn) >= 0.5f) s << "  -  gate " << (rigPtr->gateOpen.load() ? "open" : "closed");
        }
        statusLabel.setText (s, juce::dontSendNotification);
    }
}

void RigEditor::valueTreePropertyChanged (juce::ValueTree& node, const juce::Identifier& prop)
{
    if (node.hasType (ids::RIG))
    {
        if (node == target)
        {
            if (prop == ids::namFile) { selectBlock (chain.selected); return; }   // the built-in amp's gain knob comes / goes
            if (prop == ids::rigName) rebuildPresets();
            if (prop == juce::Identifier (info (calLevel).id)) settings.setValue ("namInterfaceDbu", (double) get (target, calLevel));
            if (prop == juce::Identifier (info (namCalibrate).id)) { selectBlock (chain.selected); return; }
            if (prop == ids::input) rebuildInputs();
            refreshAll();
        }
        else if (prop == ids::enabled && node.getParent()[tlids::id].toString() == targetTrackId)
            selectTarget (targetTrackId);
        else if (prop == ids::enabled) rebuildInputs();
        return;
    }
    if (node.hasType (tlids::TRACK))
    {
        if (prop == tlids::name) rebuildInputs();
        if (prop == tlids::monitor && node[tlids::id].toString() == targetTrackId) refreshAll();
    }
}

void RigEditor::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (tlids::TRACK) || child.hasType (ids::RIG)) selectTarget (targetTrackId);
}

void RigEditor::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (trackMode && child.hasType (tlids::TRACK) && child[tlids::id].toString() == targetTrackId)
    {
        target = {};
        if (onTrackGone) juce::MessageManager::callAsync ([cb = onTrackGone] { cb(); });
        return;
    }
    if (child.hasType (tlids::TRACK) || child.hasType (ids::RIG)) selectTarget (targetTrackId);
}
} // namespace ff
