#include "TrackWorkers.h"
#include <thread>

namespace ff
{
struct TrackWorkers::Worker final : public juce::Thread
{
    explicit Worker (TrackWorkers& o, int i) : juce::Thread ("RAVENYR track worker " + juce::String (i)), owner (o) {}
    void run() override
    {
        int joinedGeneration = -1;
        while (! threadShouldExit())
        {
            if (! owner.wake.try_acquire_for (std::chrono::milliseconds (100))) continue;
            if (threadShouldExit()) break;
            if (const int g = owner.workgroupGeneration.load(); g != joinedGeneration)
            {
                juce::AudioWorkgroup wg;
                {
                    const juce::ScopedLock sl (owner.workgroupLock);
                    wg = owner.workgroup;
                }
                token.reset();
                if (wg) wg.join (token);
                joinedGeneration = g;
            }
            owner.workLoop();
        }
        token.reset();
    }
    TrackWorkers& owner;
    juce::WorkgroupToken token;
};

TrackWorkers::TrackWorkers() = default;
TrackWorkers::~TrackWorkers() { stop(); }

int TrackWorkers::defaultThreadCount()
{
    const int cores = (int) std::thread::hardware_concurrency();
    return juce::jlimit (0, 7, cores - 1);
}

void TrackWorkers::start (int numThreads, double sampleRate, int blockSize)
{
    stop();
    for (int i = 0; i < numThreads; ++i)
    {
        auto w = std::make_unique<Worker> (*this, i);
        const auto options = juce::Thread::RealtimeOptions{}.withPriority (9)
                                 .withApproximateAudioProcessingTime (juce::jmax (32, blockSize), sampleRate > 0 ? sampleRate : 48000.0);
        if (! w->startRealtimeThread (options) && ! w->startThread (juce::Thread::Priority::highest)) continue;
        workers.push_back (std::move (w));
    }
    numWorkers = (int) workers.size();
}

void TrackWorkers::stop()
{
    numWorkers = 0;
    for (auto& w : workers) w->signalThreadShouldExit();
    wake.release ((std::ptrdiff_t) workers.size());
    for (auto& w : workers) w->stopThread (2000);
    workers.clear();
    while (wake.try_acquire()) {}
}

void TrackWorkers::setWorkgroup (juce::AudioWorkgroup wg)
{
    {
        const juce::ScopedLock sl (workgroupLock);
        workgroup = std::move (wg);
    }
    workgroupGeneration.fetch_add (1);
}

void TrackWorkers::workLoop() noexcept
{
    for (;;)
    {
        const int i = next.fetch_add (1, std::memory_order_acq_rel);
        if (i >= count.load (std::memory_order_relaxed)) return;
        job (context, i);
        remaining.fetch_sub (1, std::memory_order_acq_rel);
    }
}

void TrackWorkers::run (int n, Job j, void* ctx) noexcept
{
    if (n <= 0) return;
    const int helpers = juce::jmin (numWorkers.load (std::memory_order_relaxed), n - 1);
    if (helpers <= 0)
    {
        for (int i = 0; i < n; ++i) j (ctx, i);
        return;
    }
    job = j;
    context = ctx;
    count.store (n, std::memory_order_relaxed);
    remaining.store (n, std::memory_order_relaxed);
    next.store (0, std::memory_order_release);
    wake.release (helpers);
    workLoop();
    for (int spins = 0; remaining.load (std::memory_order_acquire) > 0; ++spins)
        if (spins > 64) std::this_thread::yield();
    next.store (1 << 30, std::memory_order_release);
}
} // namespace ff
