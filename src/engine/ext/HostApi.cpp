#include "HostApi.h"
#include "engine/AudioEngine.h"
#include "engine/PlaybackGraph.h"
#include "engine/fx/PluginHost.h"
#include <atomic>
#include <chrono>

namespace ff::hostapi
{
namespace
{
std::atomic<AudioEngine*> engine { nullptr };
std::atomic<juce::int64> wakeKey { 0 };
std::atomic<juce::int64> wakeUntilMs { 0 };

juce::int64 nowMs() noexcept
{
    return (juce::int64) std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

const PlayTrack* findTrack (juce::int64 key) noexcept
{
    auto* e = engine.load();
    if (e == nullptr || key == 0 || ! juce::MessageManager::existsAndIsCurrentThread()) return nullptr;
    if (const auto* g = e->latestGraph())
        for (const auto& t : g->tracks)
            if (t.key == key) return &t;
    return nullptr;
}

int64_t apiCurrentTrack() { return (int64_t) fx::CurrentTrackScope::current(); }
void apiLocate (int64_t s) { if (auto* e = engine.load()) e->locate ((juce::int64) s); }
void apiPlay() { if (auto* e = engine.load()) e->play(); }
void apiStop() { if (auto* e = engine.load()) e->stop(); }
int32_t apiIsPlaying() { auto* e = engine.load(); return e != nullptr && e->isPlaying() ? 1 : 0; }
double apiSampleRate() { auto* e = engine.load(); return e != nullptr ? e->getSampleRate() : 0.0; }

int64_t apiTrackLength (int64_t key)
{
    const auto* t = findTrack ((juce::int64) key);
    if (t == nullptr) return 0;
    juce::int64 end = 0;
    for (const auto& p : t->pieces) end = juce::jmax (end, p.end);
    return (int64_t) end;
}

int64_t apiReadTrack (int64_t key, int64_t start, int64_t num, float* left, float* right)
{
    const auto* t = findTrack ((juce::int64) key);
    if (t == nullptr || left == nullptr || num <= 0 || start < 0) return 0;
    constexpr int chunk = 8192;
    std::vector<float> l ((size_t) chunk), r ((size_t) chunk);
    for (int64_t done = 0; done < num;)
    {
        const int n = (int) juce::jmin<int64_t> (chunk, num - done);
        std::fill (l.begin(), l.begin() + n, 0.0f);
        std::fill (r.begin(), r.begin() + n, 0.0f);
        renderTrackClips (*t, (juce::int64) (start + done), n, l.data(), r.data());
        if (right != nullptr)
        {
            std::copy_n (l.data(), n, left + done);
            std::copy_n (r.data(), n, right + done);
        }
        else
            for (int i = 0; i < n; ++i) left[done + i] = 0.5f * (l[(size_t) i] + r[(size_t) i]);
        done += n;
    }
    return num;
}

void apiWakeTrack (int64_t key)
{
    wakeKey.store ((juce::int64) key);
    wakeUntilMs.store (nowMs() + 3000);
}

const RavenyrHostApiV1 apiTable {
    1, (int32_t) sizeof (RavenyrHostApiV1),
    apiCurrentTrack, apiLocate, apiPlay, apiStop, apiIsPlaying, apiSampleRate,
    apiTrackLength, apiReadTrack, apiWakeTrack
};
} // namespace

void attach (AudioEngine* e) noexcept { engine.store (e); }
const RavenyrHostApiV1* table() noexcept { return &apiTable; }

bool wakeRequested (juce::int64 key) noexcept
{
    return key != 0 && wakeKey.load (std::memory_order_relaxed) == key && nowMs() < wakeUntilMs.load (std::memory_order_relaxed);
}
} // namespace ff::hostapi
