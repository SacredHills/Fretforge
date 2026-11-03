#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "core/SpscFifo.h"
#include <array>
#include <atomic>
#include <memory>

namespace ff
{
/** Records one track's input into a single file. The audio thread pushes interleaved
    samples into a lock-free FIFO and notes "segments": each contiguous stretch of
    recording (a loop pass or punch range) with its timeline start and file offset.
    A DiskWriter thread drains the FIFO to disk. After recording stops, each segment
    becomes a clip pointing into the file (so loop passes become takes). */
class TrackRecorder
{
public:
    struct Segment
    {
        std::atomic<juce::int64> timelineStart { 0 }, fileOffset { 0 }, length { 0 };
    };
    static constexpr int maxSegments = 1024;

    /** Creates the file and writer (message thread). Returns nullptr and sets error on failure. */
    static std::shared_ptr<TrackRecorder> create (const juce::File& file, int numChannels, int inputLeft, int inputRight,
                                                  double sampleRate, int bitDepth, juce::String& error);
    ~TrackRecorder();

    juce::File file;
    int numChannels = 1, inputLeft = 0, inputRight = -1;
    double sampleRate = 48000.0;

    // ---- audio thread ----
    void push (const float* left, const float* right, int n, juce::int64 timelinePos) noexcept;
    void closeSegment() noexcept { segmentOpen = false; }

    // ---- disk thread / message thread ----
    /** Writes everything currently in the FIFO. Returns false on a disk error. */
    bool drain();
    /** Final drain and close (call only after the audio thread has stopped pushing). */
    juce::Result finish();
    int getNumSegments() const noexcept { return numSegments.load (std::memory_order_acquire); }
    const Segment& segment (int i) const noexcept { return segments[(size_t) i]; }
    juce::int64 droppedSamples() const noexcept { return dropped.load(); }
    juce::int64 samplesWritten() const noexcept { return written.load(); }

    juce::String trackIdentifier; // set by creator

private:
    TrackRecorder() = default;
    SpscFifo<float> fifo;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::CriticalSection writerLock;
    std::vector<float> interleaved;
    juce::AudioBuffer<float> planar;

    std::array<Segment, maxSegments> segments;
    std::atomic<int> numSegments { 0 };
    std::atomic<juce::int64> dropped { 0 }, written { 0 };
    juce::int64 pushed = 0;      // audio thread
    bool segmentOpen = false;    // audio thread
    bool diskError = false;
};

/** Background thread that drains all active recorders to disk every ~20 ms. */
class DiskWriter : private juce::Thread
{
public:
    DiskWriter() : juce::Thread ("FF Disk Writer") {}
    ~DiskWriter() override { stopThread (3000); }

    void add (std::shared_ptr<TrackRecorder>);
    void remove (const std::shared_ptr<TrackRecorder>&);
    std::atomic<bool> diskErrorFlag { false };

private:
    juce::CriticalSection lock;
    std::vector<std::shared_ptr<TrackRecorder>> active;
    void run() override;
};
} // namespace ff
