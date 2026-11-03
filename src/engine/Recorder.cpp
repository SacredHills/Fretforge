#include "Recorder.h"

namespace ff
{
std::shared_ptr<TrackRecorder> TrackRecorder::create (const juce::File& file, int numChannels, int inputLeft, int inputRight,
                                                      double sampleRate, int bitDepth, juce::String& error)
{
    if (! file.getParentDirectory().createDirectory())
    {
        error = "Cannot create folder " + file.getParentDirectory().getFullPathName();
        return nullptr;
    }
    auto stream = file.createOutputStream();
    if (stream == nullptr || stream->failedToOpen())
    {
        error = "Cannot create " + file.getFullPathName();
        return nullptr;
    }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> out (stream.release());
    auto* w = wav.createWriterFor (out.get(), sampleRate, (unsigned int) numChannels, bitDepth, {}, 0);
    if (w == nullptr)
    {
        error = "Cannot create a WAV writer for " + file.getFileName();
        return nullptr;
    }
    out.release(); // owned by the writer now

    std::shared_ptr<TrackRecorder> r (new TrackRecorder());
    r->file = file;
    r->numChannels = numChannels;
    r->inputLeft = inputLeft;
    r->inputRight = inputRight;
    r->sampleRate = sampleRate;
    r->writer.reset (w);
    const int fifoFrames = (int) (sampleRate * 8.0); // 8 s of headroom for slow disks
    r->fifo.prepare (fifoFrames * numChannels);
    r->interleaved.resize ((size_t) 16384 * (size_t) numChannels);
    r->planar.setSize (numChannels, 16384);
    return r;
}

TrackRecorder::~TrackRecorder()
{
    const juce::ScopedLock sl (writerLock);
    writer.reset();
}

void TrackRecorder::push (const float* left, const float* right, int n, juce::int64 timelinePos) noexcept
{
    if (! segmentOpen)
    {
        const int idx = numSegments.load (std::memory_order_relaxed);
        if (idx >= maxSegments)
            return;
        segments[(size_t) idx].timelineStart.store (timelinePos, std::memory_order_relaxed);
        segments[(size_t) idx].fileOffset.store (pushed, std::memory_order_relaxed);
        segments[(size_t) idx].length.store (0, std::memory_order_relaxed);
        numSegments.store (idx + 1, std::memory_order_release);
        segmentOpen = true;
    }

    float chunk[512];
    const int framesPerChunk = 512 / numChannels;
    for (int done = 0; done < n;)
    {
        const int frames = juce::jmin (framesPerChunk, n - done);
        for (int i = 0; i < frames; ++i)
        {
            if (numChannels == 1)
                chunk[i] = left != nullptr ? left[done + i] : 0.0f;
            else
            {
                chunk[2 * i] = left != nullptr ? left[done + i] : 0.0f;
                chunk[2 * i + 1] = right != nullptr ? right[done + i] : 0.0f;
            }
        }
        const int wrote = fifo.push (chunk, frames * numChannels);
        if (wrote < frames * numChannels)
            dropped.fetch_add ((frames * numChannels - wrote) / numChannels, std::memory_order_relaxed);
        done += frames;
    }
    pushed += n;
    auto& seg = segments[(size_t) (numSegments.load (std::memory_order_relaxed) - 1)];
    seg.length.store (seg.length.load (std::memory_order_relaxed) + n, std::memory_order_relaxed);
}

bool TrackRecorder::drain()
{
    const juce::ScopedLock sl (writerLock);
    if (writer == nullptr)
        return ! diskError;
    for (;;)
    {
        const int got = fifo.pop (interleaved.data(), (int) interleaved.size());
        if (got <= 0)
            break;
        const int frames = got / numChannels;
        for (int c = 0; c < numChannels; ++c)
        {
            auto* dst = planar.getWritePointer (c);
            for (int i = 0; i < frames; ++i)
                dst[i] = interleaved[(size_t) (i * numChannels + c)];
        }
        if (! writer->writeFromAudioSampleBuffer (planar, 0, frames))
            diskError = true;
        written.fetch_add (frames);
    }
    return ! diskError;
}

juce::Result TrackRecorder::finish()
{
    const bool ok = drain();
    {
        const juce::ScopedLock sl (writerLock);
        if (writer != nullptr)
            writer->flush();
        writer.reset();
    }
    if (! ok)
        return juce::Result::fail ("Disk error while recording to " + file.getFileName());
    if (dropped.load() > 0)
        return juce::Result::fail ("The disk could not keep up; " + juce::String (dropped.load()) + " samples were lost in "
                                   + file.getFileName() + ".");
    return juce::Result::ok();
}

void DiskWriter::add (std::shared_ptr<TrackRecorder> r)
{
    {
        const juce::ScopedLock sl (lock);
        active.push_back (std::move (r));
    }
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::high);
}

void DiskWriter::remove (const std::shared_ptr<TrackRecorder>& r)
{
    const juce::ScopedLock sl (lock);
    active.erase (std::remove (active.begin(), active.end(), r), active.end());
}

void DiskWriter::run()
{
    while (! threadShouldExit())
    {
        std::vector<std::shared_ptr<TrackRecorder>> snapshot;
        {
            const juce::ScopedLock sl (lock);
            snapshot = active;
        }
        for (auto& r : snapshot)
            if (! r->drain())
                diskErrorFlag = true;
        wait (20);
    }
}
} // namespace ff
