#pragma once
#include "core/Timeline.h"
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

    // routing
    timeline::TrackKind kind = timeline::TrackKind::audio;
    int output = -1;                          // index of the bus track this one plays into, -1 = master
    struct Send { int target = -1; float gain = 0.0f; bool pre = false; };
    std::vector<Send> sends;                  // into FX / bus tracks
    int level = 0;                            // processing stage: a track's sources all have lower levels
    bool outputMuted = false;                 // solo: the track feeds a soloed bus / FX return but is not heard directly

    // audio-thread state
    struct Idle { juce::int64 silentIn = 0, quietOut = 0; bool sleeping = false; } idle;   // skip effects that have nothing to do
    int downstreamLatency = 0;                // latency of the buses this track plays through
    void resetState() noexcept { idle = {}; }
};

/** Immutable snapshot of everything the audio thread needs to play the arrangement.
    Built on the message thread, swapped in atomically, destroyed on the message thread. */
struct PlaybackGraph
{
    std::vector<PlayTrack> tracks;
    std::vector<int> order;
    std::shared_ptr<fx::InsertChain> masterInserts;   // plugins on the master bus (after every track)                  // track indices sorted by level (sources before the buses they feed)
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
    std::shared_ptr<fx::InsertChain> masterInserts;
};

/** Builds the graph from the TRACKS node. Clip files are resolved relative to mediaBundle. */
std::unique_ptr<PlaybackGraph> buildPlaybackGraph (const juce::ValueTree& tracks, const juce::File& mediaBundle,
                                                   AudioCache& cache, const GraphBuildOptions& options);

/** Adds one track's clips for timeline range [t0, t0+n) into L/R (does not clear). Real-time safe. */
void renderTrackClips (const PlayTrack& track, juce::int64 t0, int n, float* left, float* right) noexcept;

/** Samples the track's rig and inserts delay its sound by (audio thread). Playback reads the track's clips that much
    earlier, so recorded audio stays in time with the other tracks (live monitoring cannot be compensated). */
int trackLatency (const PlayTrack&) noexcept;
/** How far ahead to read a track's clips so it lines up after its own effects and the buses it plays through. */
inline int readAhead (const PlayTrack& t) noexcept { return trackLatency (t) + t.downstreamLatency; }

/** Constant-power pan with unity at centre. */
void panGains (float pan, float gain, float& left, float& right) noexcept;
} // namespace ff
