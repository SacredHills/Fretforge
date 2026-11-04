#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include "AudioCache.h"
#include "Recorder.h"
#include "fx/AmpRig.h"
#include "fx/InsertChain.h"
#include <map>

namespace ff
{
/** One audible region of a clip, in engine samples. */
struct PlayPiece
{
    std::shared_ptr<const CachedAudio> audio;
    juce::int64 start = 0, end = 0;          // timeline samples [start, end)
    double srcAtStart = 0.0, step = 1.0;     // source sample position at `start`, source samples per engine sample
    float gain = 1.0f;
    juce::int64 fadeInStart = 0, fadeInEnd = 0, fadeOutStart = 0, fadeOutEnd = 0;
};

struct PlayTrack
{
    std::vector<PlayPiece> pieces;           // sorted by start
    float gainL = 1.0f, gainR = 1.0f;        // fader + pan
    bool audible = true;                     // after mute / solo
    bool monitor = false, armed = false;
    int inputLeft = -1, inputRight = -1;     // indices into the device's active input channels
    int meterSlot = 0;
    std::shared_ptr<TrackRecorder> recorder; // only while recording
    std::shared_ptr<fx::AmpRig> rig;         // amp rig insert (Effects & Amps), applied before fader / pan
    std::shared_ptr<fx::InsertChain> inserts; // plugin inserts, after the rig
};

/** Immutable snapshot of everything the audio thread needs to play the arrangement.
    Built on the message thread, swapped in atomically, destroyed on the message thread. */
struct PlaybackGraph
{
    std::vector<PlayTrack> tracks;
    double sampleRate = 48000.0;
    int missingClips = 0; // clips whose audio is not loaded (yet)
};

struct GraphBuildOptions
{
    double sampleRate = 48000.0;
    bool waitForAudio = false;   // export: load synchronously instead of skipping unloaded clips
    const std::map<juce::String, std::shared_ptr<TrackRecorder>>* recorders = nullptr; // by track id
    std::function<std::shared_ptr<fx::AmpRig> (const juce::ValueTree& track)> rigFor;    // amp rig per track (optional)
    std::function<std::shared_ptr<fx::InsertChain> (const juce::ValueTree& track)> insertsFor;
};

/** Builds the graph from the TRACKS node. Clip files are resolved relative to mediaBundle. */
std::unique_ptr<PlaybackGraph> buildPlaybackGraph (const juce::ValueTree& tracks, const juce::File& mediaBundle,
                                                   AudioCache& cache, const GraphBuildOptions& options);

/** Adds one track's clips for timeline range [t0, t0+n) into L/R (does not clear). Real-time safe. */
void renderTrackClips (const PlayTrack& track, juce::int64 t0, int n, float* left, float* right) noexcept;

/** Constant-power pan with unity at centre. */
void panGains (float pan, float gain, float& left, float& right) noexcept;
} // namespace ff
