#include "SessionController.h"
#include "plugins/PluginManager.h"

namespace ff
{
using namespace timeline;

SessionController::SessionController (EngineHost& h, ProjectDocument& d, juce::PropertiesFile& s)
    : host (h), doc (d), settings (s)
{
    cache.onLoaded = [this] {
        triggerAsyncUpdate();
        if (onArrangementChanged) onArrangementChanged();
    };
    cache.onError = [this] (const juce::String& e) { if (onError) onError ("Audio file problem", e); };
    rigBank = std::make_unique<fx::RigBank> (host.engine);
    if (auto xml = settings.getXmlValue ("liveRig"))
    {
        auto t = juce::ValueTree::fromXml (*xml);
        t.setProperty (rig::ids::enabled, false, nullptr);   // live monitoring always starts off (no surprise feedback)
        rigBank->setLiveTree (t);
    }
    rigBank->onRigsChanged = [this] { triggerAsyncUpdate(); };
    rigBank->onLiveChanged = [this] {
        auto t = rigBank->liveTree().createCopy();
        t.setProperty (rig::ids::enabled, false, nullptr);
        if (auto xml = t.createXml()) settings.setValue ("liveRig", xml.get());
    };
    rebind();
    startTimerHz (30);
}

SessionController::~SessionController()
{
    stopTimer();
    root.removeListener (this);
    if (sessionActive)
    {
        host.engine.stop();
        for (auto& [id, r] : recorders)
        {
            diskWriter.remove (r);
            r->finish();
        }
    }
}

void SessionController::rebind()
{
    root.removeListener (this);
    root = doc.tree();
    root.addListener (this);
    rigBank->rebind (root);
    selectedTracks.clear();
    selectionAnchor = {};
    if (auto first = doc.tracks().getChild (0); first.isValid())
        setSelectedTracks ({ first });
    selectionChanged.sendChangeMessage();
    rebuildGraphNow();
}

void SessionController::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType (ids::TRACK) && selectedTracks.contains (child))
    {
        selectedTracks.removeAllInstancesOf (child);
        if (selectionAnchor == child) selectionAnchor = selectedTracks.getFirst();
        selectionChanged.sendChangeMessage();
    }
    triggerAsyncUpdate();
}

// ------------------------------------------------------------------ selection

void SessionController::setSelectedTracks (const juce::Array<juce::ValueTree>& tracks)
{
    selectedTracks = tracks;
    if (! selectedTracks.contains (selectionAnchor))
        selectionAnchor = selectedTracks.getFirst();
    selectionChanged.sendChangeMessage();
}

void SessionController::selectTrackRange (const juce::ValueTree& from, const juce::ValueTree& to)
{
    auto tracks = doc.tracks();
    int a = tracks.indexOf (from), b = tracks.indexOf (to);
    if (a < 0) a = b;
    if (b < 0) return;
    juce::Array<juce::ValueTree> range;
    for (int i = juce::jmin (a, b); i <= juce::jmax (a, b); ++i)
        range.add (tracks.getChild (i));
    selectedTracks = range;
    selectionAnchor = from.isValid() ? from : to;
    selectionChanged.sendChangeMessage();
}

void SessionController::clickTrack (const juce::ValueTree& t, juce::ModifierKeys mods)
{
    if (! t.isValid()) return;
    if (mods.isShiftDown() && selectionAnchor.isValid())
    {
        const auto anchor = selectionAnchor;
        selectTrackRange (anchor, t);
        selectionAnchor = anchor;
        return;
    }
    if (mods.isCommandDown() || mods.isCtrlDown())
    {
        if (selectedTracks.contains (t)) selectedTracks.removeAllInstancesOf (t);
        else selectedTracks.add (t);
        selectionAnchor = t;
        selectionChanged.sendChangeMessage();
        return;
    }
    if (selectedTracks.size() == 1 && selectedTracks[0] == t) return;
    selectedTracks = { t };
    selectionAnchor = t;
    selectionChanged.sendChangeMessage();
}

juce::Array<juce::ValueTree> SessionController::tracksForEdit (const juce::ValueTree& clicked) const
{
    if (selectedTracks.contains (clicked) && selectedTracks.size() > 1)
        return selectedTracks;
    return { clicked };
}

juce::ValueTree SessionController::primaryTrack()
{
    for (auto t : doc.tracks())
        if (selectedTracks.contains (t))
            return t;
    return {};
}

void SessionController::setOnTracks (const juce::ValueTree& clicked, const juce::Identifier& id, const juce::var& value,
                                     bool undoable, const juce::String& undoName)
{
    if (undoable) doc.undo().beginNewTransaction (undoName);
    for (auto t : tracksForEdit (clicked))
        doc.set (t, id, value, undoable);
}

void SessionController::valueTreePropertyChanged (juce::ValueTree& node, const juce::Identifier&)
{
    if (node.hasType (ids::TRACK) || node.hasType (ids::CLIP))
        triggerAsyncUpdate();
}

void SessionController::addTrackRig (juce::ValueTree track)
{
    if (! track.isValid()) return;
    auto r = rig::find (track);
    if (! rig::isEnabled (r))
    {
        doc.undo().beginNewTransaction ("Add NAM amp");
        if (! r.isValid())
        {
            auto fresh = rig::makeDefault();
            rig::set (fresh, rig::calLevel, (float) settings.getDoubleValue ("namInterfaceDbu", 12.0));   // the user's interface
            track.appendChild (fresh, &doc.undo());
        }
        else r.setProperty (rig::ids::enabled, true, &doc.undo());
    }
    if (onOpenRig) onOpenRig (track);
}

void SessionController::removeTrackRig (juce::ValueTree track)
{
    auto r = rig::find (track);
    if (! r.isValid()) return;
    doc.undo().beginNewTransaction ("Remove NAM amp");
    track.removeChild (r, &doc.undo());
}

void SessionController::showAddInsertMenu (juce::ValueTree track, juce::Component* target)
{
    if (plugins == nullptr || ! track.isValid()) return;
    if (rig::numInserts (track) >= rig::maxInserts)
    {
        if (onError) onError ("Plugins", "This track already has " + juce::String (rig::maxInserts) + " plugins.");
        return;
    }
    juce::PopupMenu m;
    plugins->addToMenu (m, 1);
    if (m.getNumItems() == 0) m.addItem (-1, "No plugins found yet", false);
    m.addSeparator();
    m.addItem (-2, "Scan for plugins / manage...");
    juce::Component::SafePointer<juce::Component> safeTarget (target);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target).withMousePosition(), [this, track, safeTarget] (int r) {
        if (r == 0) return;
        if (r == -2) { plugins->showManager(); return; }
        juce::String name;
        const auto desc = plugins->descriptionForMenuResult (r, 1, name);
        if (desc.isEmpty()) return;
        doc.undo().beginNewTransaction ("Add plugin " + name);
        const int slot = rig::addInsert (track, desc, name, &doc.undo());
        if (slot >= 0) openInsertWhenLoaded (track, slot, 40);   // its window opens once it has loaded
    });
}

void SessionController::openInsertWhenLoaded (juce::ValueTree track, int slot, int triesLeft)
{
    const auto st = rigBank->insertStatus (track, slot);
    if (st.loaded) { openInsert (track, slot); return; }
    if (st.error.isNotEmpty()) { if (onError) onError ("Plugin", st.name + " could not be loaded: " + st.error); return; }
    if (triesLeft > 0)
        juce::Timer::callAfterDelay (250, [this, track, slot, triesLeft] { openInsertWhenLoaded (track, slot, triesLeft - 1); });
}

void SessionController::openInsert (juce::ValueTree track, int slot)
{
    if (plugins == nullptr) return;
    if (auto p = rigBank->insertPlugin (track, slot))
        plugins->showEditor (p, rig::insertNode (track, slot)[rig::ids::name].toString() + "  (" + track[ids::name].toString() + ")");
}

void SessionController::showInsertMenu (juce::ValueTree track, int slot, juce::Component* target)
{
    const auto node = rig::insertNode (track, slot);
    if (! node.isValid()) return;
    const auto st = rigBank->insertStatus (track, slot);
    juce::PopupMenu m;
    m.addSectionHeader (node[rig::ids::name].toString() + (st.error.isNotEmpty() ? "  (not loaded)" : juce::String()));
    m.addItem (1, "Open plugin window", st.loaded);
    m.addItem (2, "Bypass", true, (bool) node[rig::ids::bypass]);
    m.addItem (3, "Move up", slot > 0);
    m.addItem (4, "Move down", slot < rig::numInserts (track) - 1);
    m.addItem (5, "Replace...");
    m.addItem (6, "Remove");
    if (st.error.isNotEmpty()) { m.addSeparator(); m.addItem (-1, st.error, false); }
    juce::Component::SafePointer<juce::Component> safeTarget (target);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target).withMousePosition(), [this, track, slot, safeTarget] (int r) {
        auto n = rig::insertNode (track, slot);
        if (! n.isValid()) return;
        if (r == 1) openInsert (track, slot);
        else if (r == 2) { doc.undo().beginNewTransaction ("Bypass plugin"); n.setProperty (rig::ids::bypass, ! (bool) n[rig::ids::bypass], &doc.undo()); }
        else if (r == 3 || r == 4) { doc.undo().beginNewTransaction ("Move plugin"); rig::moveInsert (track, slot, r == 3 ? -1 : 1, &doc.undo()); }
        else if (r == 6) { doc.undo().beginNewTransaction ("Remove plugin"); rig::removeInsert (track, slot, &doc.undo()); }
        else if (r == 5 && plugins != nullptr)
        {
            juce::PopupMenu pm;
            plugins->addToMenu (pm, 1);
            pm.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (safeTarget.getComponent()).withMousePosition(), [this, track, slot] (int rr) {
                juce::String name;
                const auto desc = plugins->descriptionForMenuResult (rr, 1, name);
                if (desc.isEmpty()) return;
                doc.undo().beginNewTransaction ("Replace plugin");
                rig::replaceInsert (track, slot, desc, name, &doc.undo());
            });
        }
    });
}

bool SessionController::trackHasRig (const juce::ValueTree& track) const
{
    return rig::isEnabled (rig::find (track));
}

void SessionController::rebuildGraphNow()
{
    cancelPendingUpdate();
    GraphBuildOptions opt;
    opt.sampleRate = host.engine.getSampleRate() > 0 ? host.engine.getSampleRate() : 48000.0;
    opt.recorders = &recorders;
    opt.rigFor = [this] (const juce::ValueTree& t) { return rigBank->rigForTrack (t[ids::id].toString()); };
    opt.insertsFor = [this] (const juce::ValueTree& t) { return rigBank->insertsForTrack (t[ids::id].toString()); };
    host.engine.submitGraph (buildPlaybackGraph (doc.tracks(), doc.mediaBundle(), cache, opt));
}

juce::File SessionController::fileFor (const juce::ValueTree& clip) const
{
    return project::Bundle::fromPortablePath (doc.mediaBundle(), clip[ids::file].toString());
}

std::shared_ptr<const CachedAudio> SessionController::audioFor (const juce::ValueTree& clip)
{
    const auto f = fileFor (clip);
    return f == juce::File() ? nullptr : cache.get (f);
}

// ------------------------------------------------------------------ tracks

juce::ValueTree SessionController::addTrack (const juce::String& nameIn, int insertIndex)
{
    auto tracks = doc.tracks();
    if (tracks.getNumChildren() >= maxTracks)
    {
        if (onError) onError ("Track limit", "This version supports up to " + juce::String (maxTracks) + " tracks.");
        return {};
    }
    const auto name = nameIn.isNotEmpty() ? nameIn : "Track " + juce::String (tracks.getNumChildren() + 1);
    auto t = createTrack (name, tracks.getNumChildren());
    const auto inputs = host.activeInputChannelNames();
    t.setProperty (ids::inputChannel, inputs.isEmpty() ? -1 : 0, nullptr);
    doc.undo().beginNewTransaction ("Add track");
    tracks.addChild (t, insertIndex, &doc.undo());
    setSelectedTracks ({ t });
    return t;
}

void SessionController::deleteTrack (juce::ValueTree track)
{
    if (sessionActive && recorders.count (track[ids::id].toString()) > 0)
    {
        if (onError) onError ("Track is recording", "Stop recording before deleting this track.");
        return;
    }
    doc.undo().beginNewTransaction ("Delete track");
    doc.tracks().removeChild (track, &doc.undo());
}

juce::ValueTree SessionController::duplicateTrack (juce::ValueTree track)
{
    auto copy = track.createCopy();
    copy.setProperty (ids::id, newId(), nullptr);
    copy.setProperty (ids::name, track[ids::name].toString() + " copy", nullptr);
    copy.setProperty (ids::armed, false, nullptr);
    for (auto c : copy)
        c.setProperty (ids::id, newId(), nullptr);
    doc.undo().beginNewTransaction ("Duplicate track");
    doc.tracks().addChild (copy, doc.tracks().indexOf (track) + 1, &doc.undo());
    return copy;
}

juce::ValueTree SessionController::findTrack (const juce::String& id)
{
    for (auto t : doc.tracks())
        if (t[ids::id].toString() == id)
            return t;
    return {};
}

juce::StringArray SessionController::inputChoiceNames (juce::Array<std::pair<int, bool>>& values) const
{
    juce::StringArray names;
    values.clear();
    names.add ("No input");
    values.add ({ -1, false });
    const auto ch = host.activeInputChannelNames();
    for (int i = 0; i < ch.size(); ++i)
    {
        names.add ("In " + juce::String (i + 1) + "  (" + ch[i] + ")");
        values.add ({ i, false });
    }
    for (int i = 0; i + 1 < ch.size(); i += 2)
    {
        names.add ("In " + juce::String (i + 1) + "+" + juce::String (i + 2) + "  stereo");
        values.add ({ i, true });
    }
    return names;
}

// ------------------------------------------------------------------ import

juce::Result SessionController::importFiles (const juce::Array<juce::File>& files, juce::ValueTree track, double atSec)
{
    // One file goes onto the given track (a new one if none). Several files each get their own
    // track, named after the file, all starting at the same position - like dragging stems in.
    auto tracks = doc.tracks();
    const auto bundle = doc.mediaBundle();
    auto r = project::Bundle::createStructure (bundle);
    if (r.failed())
        return r;

    juce::StringArray problems;
    juce::Array<juce::ValueTree> created;
    doc.undo().beginNewTransaction (files.size() > 1 ? "Import " + juce::String (files.size()) + " files" : juce::String ("Import audio"));
    int insertAt = track.isValid() ? tracks.indexOf (track) + 1 : tracks.getNumChildren();
    bool useGivenTrack = track.isValid();
    if (files.size() > 1 && track.isValid())
    {
        bool hasClips = false;
        for (auto c : track) hasClips = hasClips || c.hasType (ids::CLIP);
        useGivenTrack = ! hasClips; // fill an empty selected track with the first file
    }

    for (const auto& f : files)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (cache.formats().createReaderFor (f));
        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0)
        {
            problems.add (f.getFileName() + ": not a supported audio file (WAV, AIFF, FLAC, OGG, MP3).");
            continue;
        }
        const double len = (double) reader->lengthInSamples / reader->sampleRate;
        const bool stereoFile = reader->numChannels > 1;
        reader.reset();

        auto dst = bundle.getChildFile ("Audio").getChildFile (f.getFileName());
        if (! f.isAChildOf (bundle))
        {
            dst = dst.getNonexistentSibling (false);
            if (! f.copyFileTo (dst))
            {
                problems.add (f.getFileName() + ": could not be copied into the project.");
                continue;
            }
        }
        else
            dst = f;

        juce::ValueTree target;
        if (useGivenTrack)
        {
            target = track;
            useGivenTrack = false;
        }
        else
        {
            if (tracks.getNumChildren() >= maxTracks)
            {
                problems.add (f.getFileName() + ": track limit reached (" + juce::String (maxTracks) + ").");
                continue;
            }
            target = createTrack (f.getFileNameWithoutExtension(), tracks.getNumChildren());
            const auto inputs = host.activeInputChannelNames();
            target.setProperty (ids::inputChannel, inputs.isEmpty() ? -1 : 0, nullptr);
            target.setProperty (ids::inputStereo, stereoFile && inputs.size() > 1, nullptr);
            tracks.addChild (target, juce::jmin (insertAt++, tracks.getNumChildren()), &doc.undo());
        }
        created.add (target);

        auto clip = createClip (project::Bundle::toPortablePath (bundle, dst), f.getFileNameWithoutExtension(), atSec, len, 0.0);
        target.appendChild (clip, &doc.undo());
        cache.get (dst);
        juce::Logger::writeToLog ("Imported " + f.getFullPathName() + " as " + dst.getFullPathName());
    }
    if (! created.isEmpty())
        setSelectedTracks (created);
    return problems.isEmpty() ? juce::Result::ok() : juce::Result::fail (problems.joinIntoString ("\n"));
}

// ------------------------------------------------------------------ recording

juce::StringArray SessionController::recordingTrackIds() const
{
    juce::StringArray ids_;
    for (auto& [id, r] : recorders)
        ids_.add (id);
    return ids_;
}

void SessionController::toggleRecord()
{
    auto& e = host.engine;
    if (sessionActive)
    {
        if (e.isRecordRequested())
            e.setRecording (false); // punch out; playback continues, clips are created when the engine confirms
        return;
    }

    if (! host.hasDevice() || e.getSampleRate() <= 0.0)
    {
        if (onError) onError ("Cannot record", "No audio device is running. Choose your interface in Settings.");
        return;
    }

    juce::Array<juce::ValueTree> armed;
    for (auto t : doc.tracks())
        if ((bool) t[ids::armed])
            armed.add (t);
    if (armed.isEmpty())
    {
        if (onError) onError ("No track armed", "Arm at least one track for recording (the R button in its header), "
                                                "and choose its input.");
        return;
    }

    const auto inputs = host.activeInputChannelNames();
    const auto bundle = doc.mediaBundle();
    project::Bundle::createStructure (bundle);
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
    recordRate = e.getSampleRate();

    std::map<juce::String, std::shared_ptr<TrackRecorder>> created;
    for (auto& t : armed)
    {
        const int inL = (int) t[ids::inputChannel];
        const bool stereo = (bool) t[ids::inputStereo];
        if (inL < 0 || inL >= inputs.size() || (stereo && inL + 1 >= inputs.size()))
        {
            if (onError) onError ("Input not available", "Track \"" + t[ids::name].toString()
                                                           + "\" has no valid input. Pick one in the track header.");
            return;
        }
        auto file = bundle.getChildFile ("Audio").getChildFile (juce::File::createLegalFileName (t[ids::name].toString())
                                                                 + " " + stamp + ".wav").getNonexistentSibling (false);
        juce::String err;
        auto rec = TrackRecorder::create (file, stereo ? 2 : 1, inL, stereo ? inL + 1 : -1, recordRate, 24, err);
        if (rec == nullptr)
        {
            for (auto& [id, r] : created) r->finish(), r->file.deleteFile();
            if (onError) onError ("Cannot record", err);
            return;
        }
        created[t[ids::id].toString()] = rec;
    }

    recorders = std::move (created);
    for (auto& [id, r] : recorders)
        diskWriter.add (r);
    rebuildGraphNow(); // the graph now carries the recorders

    if (auto* dev = host.deviceManager.getCurrentAudioDevice())
        latencySamples = dev->getInputLatencyInSamples() + dev->getOutputLatencyInSamples();

    juce::int64 countIn = 0;
    if (! e.isPlaying())
    {
        const int bars = (int) doc.metronome()[project::ids::countInBars];
        countIn = (juce::int64) (bars * doc.tsNum() * MetronomeDsp::beatLengthSamples (recordRate, doc.tempo(), doc.tsDen()));
    }
    recordStartSec = e.getPositionSeconds();
    sessionActive = true;
    e.setRecording (true, countIn);
    juce::Logger::writeToLog ("Recording started on " + juce::String ((int) recorders.size()) + " track(s), latency compensation "
                              + juce::String (latencySamples) + " samples");
    if (onArrangementChanged) onArrangementChanged();
}

void SessionController::timerCallback()
{
    host.engine.collectGarbage();
    if (! sessionActive)
        return;
    auto& e = host.engine;
    if (! e.isRecordRequested() && (! e.isRecordingActive() || ! host.hasDevice()))
        finishRecording();
    else if (diskWriter.diskErrorFlag.exchange (false))
        if (onError) onError ("Disk problem", "Writing the recording to disk failed. Check free space.");
}

void SessionController::finishRecording()
{
    sessionActive = false;
    auto finished = std::move (recorders);
    recorders.clear();
    rebuildGraphNow();

    juce::StringArray problems;
    int clipsMade = 0;
    doc.undo().beginNewTransaction ("Record");
    for (auto& [trackId, rec] : finished)
    {
        diskWriter.remove (rec);
        auto res = rec->finish();
        if (res.failed())
            problems.add (res.getErrorMessage());

        auto track = findTrack (trackId);
        const auto written = rec->samplesWritten();
        if (! track.isValid() || written <= 0 || rec->getNumSegments() == 0)
        {
            rec->file.deleteFile();
            continue;
        }

        const auto group = newId();
        int take = 0;
        for (int s = 0; s < rec->getNumSegments(); ++s)
        {
            const auto& seg = rec->segment (s);
            const auto fileOffset = seg.fileOffset.load() + latencySamples;
            const auto len = juce::jmin (seg.length.load(), written - fileOffset);
            if (len < (juce::int64) (0.02 * recordRate))
                continue;
            auto clip = createClip (project::Bundle::toPortablePath (doc.mediaBundle(), rec->file),
                                    track[ids::name].toString() + " take " + juce::String (++take),
                                    (double) seg.timelineStart.load() / recordRate, (double) len / recordRate,
                                    (double) fileOffset / recordRate);
            clip.setProperty (ids::takeGroup, group, nullptr);
            track.appendChild (clip, &doc.undo());
            ++clipsMade;
        }
        cache.invalidate (rec->file);
        cache.get (rec->file);
    }
    juce::Logger::writeToLog ("Recording finished: " + juce::String (clipsMade) + " clip(s)");
    if (! problems.isEmpty() && onError)
        onError ("Recording problem", problems.joinIntoString ("\n"));
    if (onArrangementChanged) onArrangementChanged();
}
} // namespace ff
