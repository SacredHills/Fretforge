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

/** Records what an armed instrument track receives from MIDI keyboards. The audio thread pushes each event with its
    timeline position; the message thread drains them while recording and turns them into notes afterwards. */
class MidiRecorder
{
public:
    struct Event { juce::int64 pos = 0; juce::uint8 b[3] {}; juce::uint8 size = 0; };
    MidiRecorder() { fifo.prepare (16384); }

    // audio thread
    void push (juce::int64 pos, const juce::uint8* bytes, int size) noexcept
    {
        Event e;
        e.pos = pos;
        e.size = (juce::uint8) juce::jlimit (0, 3, size);
        for (int i = 0; i < e.size; ++i) e.b[i] = bytes[i];
        fifo.push (&e, 1);
    }
    std::atomic<juce::int64> lastPos { -1 };   // end of the last recorded block

    // message thread
    void drain();
    const std::vector<Event>& getEvents() const noexcept { return events; }

    struct Note { double start, length; int pitch, velocity, channel; };
    struct Pedal { double time; bool down; };
    /** Pairs note-ons with note-offs (notes still held end at `endSec`). Times in seconds = pos / sampleRate - shiftSec. */
    static void toNotes (const std::vector<Event>&, double sampleRate, double shiftSec, double endSec,
                         std::vector<Note>& notes, std::vector<Pedal>& pedal);

private:
    SpscFifo<Event> fifo;
    std::vector<Event> events;
};
} // namespace ff
