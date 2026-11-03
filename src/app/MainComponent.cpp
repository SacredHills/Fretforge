#include "MainComponent.h"

namespace ff
{
// ------------------------------------------------------------------ nav

void SectionNav::paint (juce::Graphics& g)
{
    g.fillAll (palette::panel);
    g.setColour (palette::outline);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

    for (int i = 0; i < names.size(); ++i)
    {
        auto row = juce::Rectangle<int> (0, 12 + i * rowHeight, getWidth() - 1, rowHeight);
        const bool sel = i == selected;
        if (sel || i == hover)
        {
            g.setColour (sel ? palette::panelHi : palette::panelHi.withAlpha (0.5f));
            g.fillRect (row);
        }
        if (sel)
        {
            g.setColour (palette::accent);
            g.fillRect (row.removeFromLeft (3));
        }
        auto text = row.reduced (16, 0);
        g.setColour (sel ? palette::text : palette::textDim);
        g.setFont (uiFont (14.5f, sel));
        g.drawText (names[i], text, juce::Justification::centredLeft);

        g.setFont (uiFont (10.5f));
        g.setColour (palette::textDim.withAlpha (0.7f));
        auto right = text.removeFromRight (40);
        g.drawText (i < 9 ? "Ctrl " + juce::String (i + 1) : juce::String(), right, juce::Justification::centredRight);
        if (! implemented[i])
        {
            g.setColour (palette::accentDim);
            g.fillEllipse (text.removeFromRight (16).withSizeKeepingCentre (6, 6).toFloat());
        }
    }
}

void SectionNav::mouseDown (const juce::MouseEvent& e)
{
    const int i = rowAt (e.y);
    if (juce::isPositiveAndBelow (i, names.size()) && onSelect)
        onSelect (i);
}

void SectionNav::mouseMove (const juce::MouseEvent& e)
{
    const int i = rowAt (e.y);
    hover = juce::isPositiveAndBelow (i, names.size()) ? i : -1;
    setTooltip (hover >= 0 && ! implemented[hover] ? "Planned for a later milestone" : juce::String());
    repaint();
}

// ------------------------------------------------------------------ main

MainComponent::MainComponent (juce::PropertiesFile& s) : settings (s)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);

    std::unique_ptr<juce::XmlElement> savedDevice (settings.getXmlValue ("audioDeviceState"));
    host.initialise (savedDevice.get());

    doc = std::make_unique<ProjectDocument> (host, settings);
    session = std::make_unique<SessionController> (host, *doc, settings);
    transport = std::make_unique<TransportBar> (host, *doc);
    studioPage = std::make_unique<StudioPage> (*doc, host, *session, settings);
    toolsPage = std::make_unique<ToolsPage> (host, *doc, settings);
    mixerPage = std::make_unique<MixerPage> (host, *doc, *session, settings);
    settingsPage = std::make_unique<SettingsPage> (host, *doc, settings);

    tabPage = std::make_unique<TabPage> (*doc, host, settings);
    drumPage = std::make_unique<DrumPage> (*doc, host, *session, settings);
    auto* training = milestonePages.add (new MilestonePage ("Training", 6, "Training centre",
        { "Rule-based exercise generator (23 techniques)", "Exercises as editable tab with playback", "Progressive tempo practice with thresholds",
          "Structured plans (fundamentals to death-metal picking)", "Practice tracking, streaks and speed records", "Progress charts and weak-area analysis" },
        "Available now: Tools > Metronome already includes a tempo trainer, random missing-beat rhythm training and a practice timer."));
    effectsPage = std::make_unique<EffectsPage> (*doc, host, *session, settings);
    auto* library = milestonePages.add (new MilestonePage ("Library", 7, "Songwriting & library",
        { "Riff library with tags (genre, tuning, BPM, key...)", "Chord progression library", "Example tabs, drum patterns, tutorials",
          "Preset & IR browser", "Song structure timeline", "Cross-project riff reuse" }));

    const juce::StringArray names { "Studio", "Mixer", "Guitar Tabs", "Drum Studio", "Training", "Effects & Amps", "Library", "Tools", "Settings" };
    pages = { studioPage.get(), mixerPage.get(), tabPage.get(), drumPage.get(), training, effectsPage.get(), library, toolsPage.get(), settingsPage.get() };
    nav.names = names;
    nav.implemented = { true, true, true, true, false, true, false, true, true };
    nav.onSelect = [this] (int i) { showSection (i); };
    addAndMakeVisible (nav);
    for (auto* p : pages)
        addChildComponent (p);

    addAndMakeVisible (*transport);
    for (auto* l : { &statusLeft, &statusRight })
    {
        styleLabel (*l, 12.0f);
        addAndMakeVisible (l);
    }
    statusRight.setJustificationType (juce::Justification::centredRight);

    doc->onStateChanged = [this] { updateTitleAndStatus(); };
    doc->onTreeReplaced = [this] {
        session->rebind();
        transport->rebind();
        studioPage->rebind();
        mixerPage->rebind();
        tabPage->rebind();
        drumPage->rebind();
        effectsPage->rebind();
        toolsPage->metronome.rebind();
    };
    transport->onRecord = [this] { session->toggleRecord(); };
    session->onError = [this] (const juce::String& t, const juce::String& m) { showError (t, m); };
    host.onDeviceChanged = [this] {
        toolsPage->tuner.refreshInputs();
        session->deviceChanged();
        studioPage->arrange.deviceChanged();
        effectsPage->setActive (currentSection == 5);
        if (auto xml = host.createStateXml())
            settings.setValue ("audioDeviceState", xml.get());
        updateTitleAndStatus();
    };

    commandManager.registerAllCommandsForTarget (this);
    commandManager.registerAllCommandsForTarget (juce::JUCEApplication::getInstance());
    if (auto km = settings.getXmlValue ("keyMappings"))
        commandManager.getKeyMappings()->restoreFromXml (*km);
    addKeyListener (commandManager.getKeyMappings());
    setWantsKeyboardFocus (true);

#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
#else
    menuBar = std::make_unique<juce::MenuBarComponent> (this);
    addAndMakeVisible (*menuBar);
#endif
    setApplicationCommandManagerToWatch (&commandManager);

    showSection (juce::jlimit (0, pages.size() - 1, settings.getIntValue ("lastSection", 0)));
    updateTitleAndStatus();
    setSize (1440, 900);
    startTimerHz (2);
}

MainComponent::~MainComponent()
{
    stopTimer();
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
#endif
    if (auto km = commandManager.getKeyMappings()->createXml (true))
        settings.setValue ("keyMappings", km.get());
    if (auto xml = host.createStateXml())
        settings.setValue ("audioDeviceState", xml.get());
    settings.setValue ("lastSection", currentSection);
    settings.saveIfNeeded();

    host.engine.stop();
    tabPage->setActive (false);
    drumPage->setActive (false);
    transport.reset();
    pages.clear();
    tabPage.reset();
    drumPage.reset();
    effectsPage.reset();
    milestonePages.clear();
    studioPage.reset();
    toolsPage.reset();
    mixerPage.reset();
    settingsPage.reset();
    session.reset();
    doc.reset();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MainComponent::showSection (int index)
{
    // the tab and drum sections drive the engine in score mode; switch the old one off first
    if (tabPage != nullptr && index != 2) tabPage->setActive (false);
    if (drumPage != nullptr && index != 3) drumPage->setActive (false);
    if (tabPage != nullptr && index == 2) tabPage->setActive (true);
    if (drumPage != nullptr && index == 3) drumPage->setActive (true);
    if (effectsPage != nullptr) effectsPage->setActive (index == 5);
    currentSection = index;
    nav.selected = index;
    nav.repaint();
    for (int i = 0; i < pages.size(); ++i)
        pages[i]->setVisible (i == index);
    commandManager.commandStatusChanged();
}

void MainComponent::updateTitleAndStatus()
{
    if (onTitleChanged)
        onTitleChanged ("FretForge Studio - " + doc->displayName());

    juce::String left = doc->displayName() + "   ";
    left << (doc->hasLocation() ? doc->getBundle().getFullPathName() : juce::String ("(not saved)"));
    if (doc->lastAutosave() != juce::Time())
        left << dotSep() << "autosaved " << doc->lastAutosave().formatted ("%H:%M:%S");
    statusLeft.setText (left, juce::dontSendNotification);

    statusRight.setText (host.lastError.isNotEmpty() ? host.lastError : host.deviceSummary(), juce::dontSendNotification);
    statusRight.setColour (juce::Label::textColourId, host.hasDevice() ? palette::textDim : palette::warn);
    if (studioPage) studioPage->refreshInfo();
    commandManager.commandStatusChanged();
}

void MainComponent::timerCallback()
{
    updateTitleAndStatus();
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (palette::bg);
    auto status = getLocalBounds().removeFromBottom (26);
    g.setColour (palette::panel);
    g.fillRect (status);
    g.setColour (palette::outline);
    g.drawHorizontalLine (status.getY(), 0.0f, (float) getWidth());
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    if (menuBar)
        menuBar->setBounds (r.removeFromTop (24));
    transport->setBounds (r.removeFromTop (64));
    auto status = r.removeFromBottom (26).reduced (10, 0);
    statusRight.setBounds (status.removeFromRight (status.getWidth() / 2));
    statusLeft.setBounds (status);

    nav.setBounds (r.removeFromLeft (200));
    for (auto* p : pages)
        p->setBounds (r);
}

// ------------------------------------------------------------------ file flows

void MainComponent::showError (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (title)
                                      .withMessage (message)
                                      .withButton ("OK")
                                      .withAssociatedComponent (this),
                                  nullptr);
}

void MainComponent::withChangesHandled (std::function<void()> continuation)
{
    if (! doc->isDirty())
    {
        continuation();
        return;
    }
    juce::Component::SafePointer<MainComponent> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Unsaved changes")
                                      .withMessage ("Save changes to \"" + doc->tree()[project::ids::name].toString() + "\" first?")
                                      .withButton ("Save")
                                      .withButton ("Don't Save")
                                      .withButton ("Cancel")
                                      .withAssociatedComponent (this),
                                  [safe, continuation] (int result) {
                                      if (safe == nullptr) return;
                                      if (result == 1) safe->saveThen (continuation);
                                      else if (result == 2) continuation();
                                  });
}

void MainComponent::saveThen (std::function<void()> continuation)
{
    if (! doc->hasLocation())
    {
        saveAsThen (continuation);
        return;
    }
    const auto r = doc->save();
    if (r.failed())
        showError ("Could not save project", r.getErrorMessage());
    else if (continuation)
        continuation();
}

void MainComponent::saveAsThen (std::function<void()> continuation)
{
    auto start = doc->hasLocation() ? doc->getBundle().getParentDirectory()
                                    : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Save project as", start.getChildFile (doc->tree()[project::ids::name].toString()), "*");
    juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, continuation] (const juce::FileChooser& fc) {
                              if (safe == nullptr) return;
                              const auto picked = fc.getResult();
                              if (picked == juce::File()) return;
                              const auto bundle = project::Bundle::normaliseBundlePath (picked);
                              if (bundle != safe->doc->getBundle() && bundle.exists())
                              {
                                  safe->showError ("Project already exists",
                                                   bundle.getFullPathName() + " already exists. Choose another name.");
                                  return;
                              }
                              const auto r = safe->doc->saveTo (bundle);
                              if (r.failed())
                                  safe->showError ("Could not save project", r.getErrorMessage());
                              else if (continuation)
                                  continuation();
                          });
}

void MainComponent::openProjectDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Open project (choose the .ffproj folder or its project.ffp)",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.ffp;*.ffproj");
    juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [safe] (const juce::FileChooser& fc) {
                              if (safe != nullptr && fc.getResult() != juce::File())
                                  safe->openProject (fc.getResult());
                          });
}

void MainComponent::openProject (const juce::File& f)
{
    const auto bundle = f.existsAsFile() ? f.getParentDirectory() : f;
    if (! project::Bundle::isBundle (bundle))
    {
        showError ("Not a project", f.getFullPathName() + " is not a FretForge project folder.");
        return;
    }
    host.engine.stop();
    const auto r = doc->openBundle (bundle);
    if (r.failed())
        showError ("Problem opening project", r.getErrorMessage());
    showSection (0);
}

void MainComponent::openFromCommandLine (const juce::File& f)
{
    withChangesHandled ([this, f] { openProject (f); });
}

void MainComponent::requestQuit (std::function<void()> quit)
{
    withChangesHandled ([this, quit] {
        doc->endSession();
        quit();
    });
}

void MainComponent::runStartupChecks()
{
    juce::Component::SafePointer<MainComponent> safe (this);
    if (auto rec = doc->findCrashRecovery())
    {
        const auto where = rec->untitled ? juce::String ("an unsaved project") : rec->bundle.getFileNameWithoutExtension();
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::WarningIcon)
                                          .withTitle ("Recover unsaved work?")
                                          .withMessage ("FretForge Studio did not shut down cleanly. An autosave of " + where
                                                        + " is newer than the last save. Recover it?")
                                          .withButton ("Recover")
                                          .withButton ("Discard")
                                          .withAssociatedComponent (this),
                                      [safe, r = *rec] (int result) {
                                          if (safe == nullptr) return;
                                          if (result == 1)
                                          {
                                              const auto res = safe->doc->recoverFrom (r);
                                              if (res.failed()) safe->showError ("Recovery failed", res.getErrorMessage());
                                          }
                                          else
                                          {
                                              project::Bundle::discardAutosave (r.bundle);
                                              if (r.untitled)
                                                  r.bundle.getChildFile ("Audio").deleteRecursively();
                                          }
                                          safe->doc->beginSession();
                                      });
    }
    else
    {
        doc->beginSession();
    }

    if (! settings.getBoolValue ("firstRunDone", false))
    {
        settings.setValue ("firstRunDone", true);
        settingsPage->setWelcomeVisible (true);
        showSection (8);
    }
    else if (! host.hasDevice())
    {
        showSection (8);
    }
}

void MainComponent::showShortcutEditor()
{
    auto* editor = new juce::KeyMappingEditorComponent (*commandManager.getKeyMappings(), true);
    editor->setSize (560, 520);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (editor);
    o.dialogTitle = "Keyboard shortcuts";
    o.dialogBackgroundColour = palette::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    o.launchAsync();
}

// ------------------------------------------------------------------ commands

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& c)
{
    c.addArray ({ cmdNew, cmdOpen, cmdSave, cmdSaveAs, cmdReveal, cmdUndo, cmdRedo, cmdPlayStop, cmdReturnToZero,
                  cmdToggleClick, cmdTapTempo, cmdPracticeMetronome, cmdShortcuts, cmdAbout,
                  cmdRecord, cmdLoop, cmdPunch, cmdAddTrack, cmdImport, cmdExport, cmdSplit, cmdDuplicate, cmdDelete,
                  cmdCopy, cmdCut, cmdPaste, cmdSelectAll, cmdAddMarker, cmdZoomIn, cmdZoomOut, cmdZoomFit });
    for (int i = 0; i < 9; ++i)
        c.add (cmdSectionBase + i);
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    using KP = juce::KeyPress;
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    switch (id)
    {
        case cmdNew:   info.setInfo ("New Project", "Start a new untitled project", "File", 0); info.addDefaultKeypress ('n', cmd); break;
        case cmdOpen:  info.setInfo ("Open Project...", "Open a .ffproj project", "File", 0); info.addDefaultKeypress ('o', cmd); break;
        case cmdSave:  info.setInfo ("Save", "Save the project", "File", 0); info.addDefaultKeypress ('s', cmd); break;
        case cmdSaveAs: info.setInfo ("Save As...", "Save the project to a new location", "File", 0); info.addDefaultKeypress ('s', cmd | shift); break;
        case cmdReveal: info.setInfo ("Show Project Folder", "Reveal the project bundle", "File", 0); info.setActive (doc->hasLocation()); break;
        case cmdUndo:
            info.setInfo ("Undo " + doc->undo().getUndoDescription(), "Undo", "Edit", 0);
            info.setActive (doc->undo().canUndo());
            info.addDefaultKeypress ('z', cmd);
            break;
        case cmdRedo:
            info.setInfo ("Redo " + doc->undo().getRedoDescription(), "Redo", "Edit", 0);
            info.setActive (doc->undo().canRedo());
            info.addDefaultKeypress ('z', cmd | shift);
            info.addDefaultKeypress ('y', cmd);
            break;
        case cmdPlayStop: info.setInfo ("Play / Stop", "Start or stop the transport", "Transport", 0); info.addDefaultKeypress (KP::spaceKey, 0); break;
        case cmdReturnToZero: info.setInfo ("Return to Start", "Move the playhead to bar 1", "Transport", 0); info.addDefaultKeypress (KP::homeKey, 0); info.addDefaultKeypress (KP::returnKey, 0); break;
        case cmdToggleClick: info.setInfo ("Metronome Click", "Click while playing", "Transport", 0); info.setTicked ((bool) doc->metronome()[project::ids::enabled]); info.addDefaultKeypress ('c', 0); break;
        case cmdTapTempo: info.setInfo ("Tap Tempo", "Tap to set the tempo", "Transport", 0); info.addDefaultKeypress ('t', 0); break;
        case cmdPracticeMetronome: info.setInfo ("Practice Metronome", "Start/stop the free-running metronome", "Transport", 0); info.setTicked (host.engine.practiceMetronome.load()); info.addDefaultKeypress ('m', 0); break;
        case cmdShortcuts: info.setInfo ("Keyboard Shortcuts...", "View and customise shortcuts", "Help", 0); info.addDefaultKeypress ('/', cmd); break;
        case cmdAbout: info.setInfo ("About FretForge Studio", "Version information", "Help", 0); break;
        case cmdRecord: info.setInfo ("Record", "Record on armed tracks / punch out", "Transport", 0); info.setTicked (host.engine.isRecordRequested()); info.addDefaultKeypress ('r', 0); break;
        case cmdLoop: info.setInfo ("Loop", "Toggle the loop range", "Transport", 0); info.setTicked ((bool) doc->transport()[project::ids::loopEnabled]); info.addDefaultKeypress ('l', 0); break;
        case cmdPunch: info.setInfo ("Punch In/Out", "Record only inside the loop range", "Transport", 0); info.setTicked ((bool) doc->transport()[project::ids::punchEnabled]); info.addDefaultKeypress ('p', 0); break;
        case cmdAddTrack: info.setInfo ("Add Track", "Add an audio track", "Track", 0); info.addDefaultKeypress ('t', cmd); break;
        case cmdImport: info.setInfo ("Import Audio...", "Import audio files at the playhead", "Track", 0); info.addDefaultKeypress ('i', cmd); break;
        case cmdExport: info.setInfo ("Export Audio...", "Export the mix or stems", "File", 0); info.addDefaultKeypress ('e', cmd); break;
        case cmdSplit: info.setInfo ("Split at Playhead", "Split clips at the playhead", "Edit", 0); info.setActive (currentSection == 0); info.addDefaultKeypress ('s', 0); break;
        case cmdDuplicate: info.setInfo ("Duplicate", "Duplicate selected clips", "Edit", 0); info.setActive (currentSection == 0 && studioPage->arrange.hasSelection()); info.addDefaultKeypress ('d', cmd); break;
        case cmdDelete: info.setInfo ("Delete", "Delete selected clips", "Edit", 0); info.setActive (currentSection == 0 && studioPage->arrange.hasSelection()); info.addDefaultKeypress (KP::deleteKey, 0); info.addDefaultKeypress (KP::backspaceKey, 0); break;
        case cmdCopy: info.setInfo ("Copy", "Copy selected clips", "Edit", 0); info.setActive (currentSection == 0 && studioPage->arrange.hasSelection()); info.addDefaultKeypress ('c', cmd); break;
        case cmdCut: info.setInfo ("Cut", "Cut selected clips", "Edit", 0); info.setActive (currentSection == 0 && studioPage->arrange.hasSelection()); info.addDefaultKeypress ('x', cmd); break;
        case cmdPaste: info.setInfo ("Paste", "Paste clips at the playhead", "Edit", 0); info.setActive (currentSection == 0); info.addDefaultKeypress ('v', cmd); break;
        case cmdSelectAll: info.setInfo ("Select All Clips", "Select every clip", "Edit", 0); info.setActive (currentSection == 0); info.addDefaultKeypress ('a', cmd); break;
        case cmdAddMarker: info.setInfo ("Add Marker", "Add a marker at the playhead", "Edit", 0); info.addDefaultKeypress (KP::insertKey, 0); info.addDefaultKeypress ('k', 0); break;
        case cmdZoomIn: info.setInfo ("Zoom In", "Zoom the timeline in", "View", 0); info.addDefaultKeypress ('=', 0); info.addDefaultKeypress ('+', 0); break;
        case cmdZoomOut: info.setInfo ("Zoom Out", "Zoom the timeline out", "View", 0); info.addDefaultKeypress ('-', 0); break;
        case cmdZoomFit: info.setInfo ("Zoom to Fit", "Show the whole song", "View", 0); info.addDefaultKeypress ('f', 0); break;
        default:
            if (id >= cmdSectionBase && id < cmdSectionBase + 9)
            {
                const int i = id - cmdSectionBase;
                info.setInfo (nav.names[i], "Show " + nav.names[i], "View", 0);
                info.setTicked (currentSection == i);
                info.addDefaultKeypress ('1' + i, cmd);
            }
            break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    switch (info.commandID)
    {
        case cmdNew:  withChangesHandled ([this] { host.engine.stop(); doc->newUntitled(); showSection (0); }); return true;
        case cmdOpen: withChangesHandled ([this] { openProjectDialog(); }); return true;
        case cmdSave: saveThen (nullptr); return true;
        case cmdSaveAs: saveAsThen (nullptr); return true;
        case cmdReveal: if (doc->hasLocation()) doc->getBundle().revealToUser(); return true;
        case cmdUndo: doc->undo().undo(); return true;
        case cmdRedo: doc->undo().redo(); return true;
        case cmdPlayStop: if (currentSection == 2) tabPage->togglePlay(); else if (currentSection == 3) drumPage->togglePlay(); else host.engine.togglePlay(); return true;
        case cmdReturnToZero: host.engine.returnToZero(); return true;
        case cmdToggleClick: transport->toggleClick(); return true;
        case cmdTapTempo: transport->tapTempo(); return true;
        case cmdPracticeMetronome: toolsPage->metronome.togglePractice(); return true;
        case cmdShortcuts: showShortcutEditor(); return true;
        case cmdRecord: session->toggleRecord(); return true;
        case cmdLoop: transport->toggleLoop(); return true;
        case cmdPunch: transport->togglePunch(); return true;
        case cmdAddTrack: showSection (0); studioPage->showArrange(); studioPage->arrange.addTrack(); return true;
        case cmdImport: showSection (0); studioPage->showArrange(); studioPage->arrange.importAudioDialog(); return true;
        case cmdExport: ExportDialog::show (this, *doc, *session, host, settings); return true;
        case cmdSplit: studioPage->arrange.splitAtPlayhead(); return true;
        case cmdDuplicate: studioPage->arrange.duplicateSelection(); return true;
        case cmdDelete: studioPage->arrange.deleteSelection(); return true;
        case cmdCopy: studioPage->arrange.copySelection (false); return true;
        case cmdCut: studioPage->arrange.copySelection (true); return true;
        case cmdPaste: studioPage->arrange.paste(); return true;
        case cmdSelectAll: studioPage->arrange.selectAll(); return true;
        case cmdAddMarker: studioPage->arrange.addMarkerAtPlayhead(); return true;
        case cmdZoomIn: studioPage->arrange.zoom (1.5); return true;
        case cmdZoomOut: studioPage->arrange.zoom (1.0 / 1.5); return true;
        case cmdZoomFit: studioPage->arrange.zoomToFit(); return true;
        case cmdAbout:
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                              .withIconType (juce::MessageBoxIconType::InfoIcon)
                                              .withTitle ("FretForge Studio")
                                              .withMessage (juce::String ("Version ") + FF_VERSION_STRING
                                                            + "\nMilestone 2 - Recording studio\n\nBuilt with JUCE " + juce::SystemStats::getJUCEVersion().fromLastOccurrenceOf (" ", false, false)
                                                            + "\n" + juce::SystemStats::getOperatingSystemName())
                                              .withButton ("OK"),
                                          nullptr);
            return true;
        default:
            if (info.commandID >= cmdSectionBase && info.commandID < cmdSectionBase + 9)
            {
                showSection (info.commandID - cmdSectionBase);
                return true;
            }
            return false;
    }
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "Track", "Transport", "View", "Help" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    auto* cm = &commandManager;
    switch (index)
    {
        case 0:
        {
            m.addCommandItem (cm, cmdNew);
            m.addCommandItem (cm, cmdOpen);
            juce::PopupMenu recent;
            doc->recent.removeNonExistentFiles();
            doc->recent.createPopupMenuItems (recent, 0x3000, true, true);
            m.addSubMenu ("Open Recent", recent, recent.getNumItems() > 0);
            m.addSeparator();
            m.addCommandItem (cm, cmdSave);
            m.addCommandItem (cm, cmdSaveAs);
            m.addCommandItem (cm, cmdReveal);
            m.addSeparator();
            m.addCommandItem (cm, cmdExport);
#if ! JUCE_MAC
            m.addSeparator();
            m.addCommandItem (cm, juce::StandardApplicationCommandIDs::quit);
#endif
            break;
        }
        case 1:
            m.addCommandItem (cm, cmdUndo);
            m.addCommandItem (cm, cmdRedo);
            m.addSeparator();
            m.addCommandItem (cm, cmdCut);
            m.addCommandItem (cm, cmdCopy);
            m.addCommandItem (cm, cmdPaste);
            m.addCommandItem (cm, cmdDuplicate);
            m.addCommandItem (cm, cmdDelete);
            m.addCommandItem (cm, cmdSelectAll);
            m.addSeparator();
            m.addCommandItem (cm, cmdSplit);
            m.addCommandItem (cm, cmdAddMarker);
            break;
        case 2:
            m.addCommandItem (cm, cmdAddTrack);
            m.addCommandItem (cm, cmdImport);
            m.addSeparator();
            m.addCommandItem (cm, cmdExport);
            break;
        case 3:
            m.addCommandItem (cm, cmdPlayStop);
            m.addCommandItem (cm, cmdReturnToZero);
            m.addCommandItem (cm, cmdRecord);
            m.addSeparator();
            m.addCommandItem (cm, cmdLoop);
            m.addCommandItem (cm, cmdPunch);
            m.addSeparator();
            m.addCommandItem (cm, cmdToggleClick);
            m.addCommandItem (cm, cmdPracticeMetronome);
            m.addCommandItem (cm, cmdTapTempo);
            break;
        case 4:
            for (int i = 0; i < 9; ++i)
                m.addCommandItem (cm, cmdSectionBase + i);
            m.addSeparator();
            m.addCommandItem (cm, cmdZoomIn);
            m.addCommandItem (cm, cmdZoomOut);
            m.addCommandItem (cm, cmdZoomFit);
            break;
        case 5:
            m.addCommandItem (cm, cmdShortcuts);
            m.addCommandItem (cm, cmdAbout);
            break;
        default: break;
    }
    return m;
}

void MainComponent::menuItemSelected (int itemId, int)
{
    if (itemId >= 0x3000 && itemId < 0x3000 + 100)
    {
        const auto f = doc->recent.getFile (itemId - 0x3000);
        withChangesHandled ([this, f] { openProject (f); });
    }
}
} // namespace ff
