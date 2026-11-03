#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <map>
#include <memory>

namespace ff
{
/** Decoded audio held in memory, plus min/max peaks for waveform drawing. Immutable once built. */
struct CachedAudio
{
    juce::File file;
    juce::AudioBuffer<float> buffer;   // at most 2 channels
    double sampleRate = 0.0;
    static constexpr int peakBlock = 256;
    std::vector<float> peaks;          // per block: for each channel min, max
    int numChannels() const { return buffer.getNumChannels(); }
    juce::int64 length() const { return buffer.getNumSamples(); }
    double lengthSeconds() const { return sampleRate > 0 ? length() / sampleRate : 0.0; }
    void computePeaks();
    /** Max |sample| in a source range (seconds), using peaks. */
    float peakInRange (double startSec, double endSec) const;
};

/** Loads audio files into memory on a background thread. get() never blocks: it returns
    the cached audio or nullptr (queuing a load and calling onLoaded on the message thread
    when ready). loadSync() is for export/tests. Thread-safe.

    Design note: whole-file caching keeps the real-time path trivially safe (no disk access);
    disk streaming for very long sessions is a planned optimisation. */
class AudioCache : private juce::Thread, private juce::AsyncUpdater
{
public:
    AudioCache();
    ~AudioCache() override;

    std::shared_ptr<const CachedAudio> get (const juce::File&);
    std::shared_ptr<const CachedAudio> loadSync (const juce::File&, juce::String* error = nullptr);
    void invalidate (const juce::File&);
    void clear();
    bool isLoading() const;

    juce::AudioFormatManager& formats() { return formatManager; }
    std::function<void()> onLoaded;
    std::function<void (const juce::String&)> onError;

    static std::shared_ptr<CachedAudio> decode (juce::AudioFormatManager&, const juce::File&, juce::String& error);

private:
    juce::AudioFormatManager formatManager;
    mutable juce::CriticalSection lock;
    std::map<juce::String, std::shared_ptr<const CachedAudio>> cache;
    juce::StringArray queue, failed;
    juce::StringArray pendingErrors;
    juce::WaitableEvent wake;

    void run() override;
    void handleAsyncUpdate() override;
};
} // namespace ff
