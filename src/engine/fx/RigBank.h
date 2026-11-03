#pragma once
#include "AmpRig.h"
#include <map>

namespace ff { class AudioEngine; }

namespace ff::fx
{
/** Reads an impulse response (any audio file JUCE can read) as mono, at most `maxSeconds` long. */
bool loadImpulseFile (const juce::File&, juce::AudioBuffer<float>& out, double& sampleRate, juce::String& error,
                      double maxSeconds = 3.0);

/** Builds a ready-to-run rig for offline rendering: model and IR are loaded synchronously.
    Problems (missing files, bad model) are added to `warnings`; the rig then uses its built-in amp / cab. */
std::shared_ptr<AmpRig> makeOfflineRig (const juce::ValueTree& rigTree, double sampleRate, int maxBlock,
                                        juce::StringArray& warnings);

/** Message-thread owner of the live rigs: one per track whose RIG is enabled, plus the live practice rig.

    Keeps the rigs in step with the project tree (parameters immediately, model / IR files loaded on a
    background thread and handed over without blocking the audio thread), re-prepares every rig when the audio
    device restarts and frees replaced models. */
class RigBank : private juce::ValueTree::Listener, private juce::Timer
{
public:
    explicit RigBank (AudioEngine&);
    ~RigBank() override;

    /** Project replaced. */
    void rebind (juce::ValueTree projectRoot);

    /** Rig for a track id (nullptr if the track has no enabled rig). */
    std::shared_ptr<AmpRig> rigForTrack (const juce::String& trackId) const;

    // ---- the live practice rig (input -> rig -> output, independent of tracks and transport) ----
    juce::ValueTree liveTree() const { return live; }
    /** Replaces the live rig's settings (from the app settings at start-up). */
    void setLiveTree (juce::ValueTree t);
    AmpRig& liveRig() { return *liveEntry.rig; }

    struct Status
    {
        juce::String modelPath, irPath;
        NamInfo modelInfo;
        bool modelLoaded = false, modelLoading = false, irLoaded = false, irLoading = false;
        juce::String modelError, irError;
        juce::String irName;
        double irSeconds = 0.0;
    };
    /** Load status for a RIG node (a track's or the live one). */
    Status statusFor (const juce::ValueTree& rigTree) const;
    /** The running rig for a RIG node (nullptr if none). */
    AmpRig* rigFor (const juce::ValueTree& rigTree) const;

    std::function<void()> onRigsChanged;    // a track rig was created or removed (rebuild the playback graph)
    std::function<void()> onStatusChanged;  // a model / IR finished loading or failed
    std::function<void()> onLiveChanged;    // the live rig's settings changed (persist them)

private:
    struct Entry
    {
        std::shared_ptr<AmpRig> rig;
        juce::ValueTree tree;
        Status status;
        int generation = 0;
    };

    AudioEngine& engine;
    juce::ValueTree root, live;
    std::map<juce::String, Entry> tracks;
    Entry liveEntry;
    int generationCounter = 0;
    juce::dsp::ConvolutionMessageQueue convQueue { 64 };
    juce::ThreadPool loader { juce::ThreadPoolOptions{}.withThreadName ("FF rig loader").withNumberOfThreads (1) };
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    mutable juce::CriticalSection lock;   // serialises rig creation / model hand-over with prepareAll()

    void prepareAll (double sr, int block);
    void syncAll();
    void syncEntry (Entry&);
    void requestModel (Entry&, bool force);
    void requestIr (Entry&, bool force);
    Entry* entryFor (const juce::ValueTree& rigTree);
    const Entry* entryFor (const juce::ValueTree& rigTree) const;
    std::shared_ptr<AmpRig> makeRig();

    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeRedirected (juce::ValueTree&) override { syncAll(); }
};
} // namespace ff::fx
