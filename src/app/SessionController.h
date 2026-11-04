#pragma once
#include "ProjectDocument.h"
#include "EngineHost.h"
#include "engine/AudioCache.h"
#include "engine/Recorder.h"
#include "engine/Exporter.h"
#include "engine/fx/RigBank.h"
#include "core/Timeline.h"

namespace ff
{
class PluginManager;
/** Glue between the project document and the real-time engine: rebuilds playback
    snapshots when the arrangement changes, owns the audio cache, runs recording
    sessions (recorders, disk writer, turning segments into clips) and imports media. */
class SessionController : private juce::ValueTree::Listener, private juce::AsyncUpdater, private juce::Timer
{
public:
    SessionController (EngineHost&, ProjectDocument&, juce::PropertiesFile&);
    ~SessionController() override;

    AudioCache cache;
    /** Amp rigs (track inserts and the live rig). */
    fx::RigBank& rigs() { return *rigBank; }

    void rebind();                     // project replaced
    void deviceChanged() { triggerAsyncUpdate(); }
    void rebuildGraphNow();

    juce::File fileFor (const juce::ValueTree& clip) const;
    std::shared_ptr<const CachedAudio> audioFor (const juce::ValueTree& clip);

    // ---- tracks ----
    juce::ValueTree addTrack (const juce::String& name = {}, int insertIndex = -1,
                              timeline::TrackKind kind = timeline::TrackKind::audio);
    /** Menu: audio / bus / FX track (added after `after`, or at the end). */
    void showAddTrackMenu (juce::Component* target, int insertIndex = -1);

    // ---- routing (outputs and sends) ----
    void setOutput (juce::ValueTree track, const juce::String& busId);
    /** Output and sends menu for a track: pick its bus, add sends, or make a new bus / FX track for it. */
    void showRoutingMenu (juce::ValueTree track, juce::Component* target);
    /** Level / pre-post / remove for one send. */
    void showSendMenu (juce::ValueTree track, juce::ValueTree send, juce::Component* target);
    /** "Master" or the bus name. */
    juce::String outputName (const juce::ValueTree& track) const;
    /** Called when an FX track is made, so the app can give it a reverb (optional). */
    std::function<void (juce::ValueTree fxTrack)> onFxTrackCreated;
    void deleteTrack (juce::ValueTree track);
    juce::ValueTree duplicateTrack (juce::ValueTree track);
    juce::ValueTree findTrack (const juce::String& id);
    juce::StringArray inputChoiceNames (juce::Array<std::pair<int, bool>>& values) const; // (channel, stereo)

    // ---- track selection (shared by arrange view and mixer) ----
    juce::Array<juce::ValueTree> selectedTracks;
    juce::ChangeBroadcaster selectionChanged;
    /** Plain click selects only `t`; Shift extends from the anchor; Ctrl/Cmd toggles. */
    void clickTrack (const juce::ValueTree& t, juce::ModifierKeys mods);
    void selectTrackRange (const juce::ValueTree& from, const juce::ValueTree& to);
    void setSelectedTracks (const juce::Array<juce::ValueTree>& tracks);
    bool isTrackSelected (const juce::ValueTree& t) const { return selectedTracks.contains (t); }
    /** The track an action on `clicked` should affect: the whole selection if `clicked` is in it. */
    juce::Array<juce::ValueTree> tracksForEdit (const juce::ValueTree& clicked) const;
    /** First selected track in track order (target for import/paste/add). */
    juce::ValueTree primaryTrack();
    juce::ValueTree selectionAnchor;
    /** Sets a property on every track in tracksForEdit(clicked). */
    void setOnTracks (const juce::ValueTree& clicked, const juce::Identifier& id, const juce::var& value, bool undoable,
                      const juce::String& undoName = {});

    // ---- amp rigs (NAM) ----
    /** Gives the track an enabled amp rig (undoable) and asks the app to open its window. */
    void addTrackRig (juce::ValueTree track);
    void removeTrackRig (juce::ValueTree track);
    bool trackHasRig (const juce::ValueTree& track) const;
    std::function<void (juce::ValueTree track)> onOpenRig;   // set by the app: shows the track's NAM window

    // ---- track insert plugins ----
    PluginManager* plugins = nullptr;                        // set by the app
    /** Menu of known plugins; the chosen one is added after the track's last insert. */
    void showAddInsertMenu (juce::ValueTree track, juce::Component* target);
    /** Open / bypass / move / replace / remove for one insert slot. */
    void showInsertMenu (juce::ValueTree track, int slot, juce::Component* target);
    void openInsert (juce::ValueTree track, int slot);
    void openInsertWhenLoaded (juce::ValueTree track, int slot, int triesLeft);

    // ---- media ----
    /** Copies files into the project's Audio folder and places clips on `track` from `atSec`. */
    juce::Result importFiles (const juce::Array<juce::File>& files, juce::ValueTree track, double atSec);

    // ---- recording ----
    void toggleRecord();
    bool isRecordingSession() const { return sessionActive; }
    double recordingStartSec() const { return recordStartSec; }
    juce::StringArray recordingTrackIds() const;

    std::function<void (const juce::String& title, const juce::String& message)> onError;
    std::function<void()> onArrangementChanged; // repaint request (audio loaded, recording state)

private:
    EngineHost& host;
    ProjectDocument& doc;
    juce::PropertiesFile& settings;
    juce::ValueTree root;

    std::unique_ptr<fx::RigBank> rigBank;
    DiskWriter diskWriter;
    std::map<juce::String, std::shared_ptr<TrackRecorder>> recorders;
    bool sessionActive = false;
    double recordStartSec = 0.0;
    juce::int64 latencySamples = 0;
    double recordRate = 48000.0;

    void finishRecording();
    void handleAsyncUpdate() override { rebuildGraphNow(); }
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { triggerAsyncUpdate(); }
    void valueTreeRedirected (juce::ValueTree&) override { triggerAsyncUpdate(); }
};
} // namespace ff
