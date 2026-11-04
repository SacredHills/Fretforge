#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <semaphore>

namespace ff
{
/** A small pool of real-time worker threads that help the audio thread process tracks in parallel.
    run() hands out job indices to the workers and the calling thread alike and returns when all are done.
    No allocation, locking or waiting on the message thread inside run(). */
class TrackWorkers
{
public:
    TrackWorkers();
    ~TrackWorkers();

    /** Message thread. numThreads = extra threads besides the audio thread (0 = everything on the audio thread). */
    void start (int numThreads, double sampleRate, int blockSize);
    void stop();
    int size() const noexcept { return numWorkers.load(); }
    /** Workers join the audio device's workgroup (macOS: keeps them on the performance cores). */
    void setWorkgroup (juce::AudioWorkgroup wg);

    using Job = void (*) (void* context, int index) noexcept;
    /** Audio thread: runs job(context, i) for i in [0, n). */
    void run (int n, Job job, void* context) noexcept;

    /** A sensible number of helper threads for this machine. */
    static int defaultThreadCount();

private:
    struct Worker;
    std::vector<std::unique_ptr<Worker>> workers;
    std::atomic<int> numWorkers { 0 };
    std::counting_semaphore<1 << 16> wake { 0 };

    std::atomic<int> next { 1 << 30 }, remaining { 0 };
    std::atomic<int> count { 0 };
    Job job = nullptr;
    void* context = nullptr;

    juce::AudioWorkgroup workgroup;
    std::atomic<int> workgroupGeneration { 0 };
    juce::CriticalSection workgroupLock;

    void workLoop() noexcept;
    friend struct Worker;
};
} // namespace ff
