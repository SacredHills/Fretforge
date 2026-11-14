#include "AudioCache.h"

namespace ff
{
void CachedAudio::computePeaks()
{
    const int ch = numChannels();
    const auto n = length();
    const auto blocks = (n + peakBlock - 1) / peakBlock;
    peaks.assign ((size_t) (blocks * ch * 2), 0.0f);
    for (juce::int64 b = 0; b < blocks; ++b)
    {
        const int s = (int) (b * peakBlock);
        const int len = (int) juce::jmin ((juce::int64) peakBlock, n - s);
        for (int c = 0; c < ch; ++c)
        {
            auto range = juce::FloatVectorOperations::findMinAndMax (buffer.getReadPointer (c, s), len);
            peaks[(size_t) ((b * ch + c) * 2)] = range.getStart();
            peaks[(size_t) ((b * ch + c) * 2 + 1)] = range.getEnd();
        }
    }
}

float CachedAudio::peakInRange (double startSec, double endSec) const
{
    const int ch = numChannels();
    const auto blocks = (juce::int64) peaks.size() / juce::jmax (1, ch * 2);
    const auto b0 = juce::jlimit ((juce::int64) 0, blocks, (juce::int64) (startSec * sampleRate / peakBlock));
    const auto b1 = juce::jlimit ((juce::int64) 0, blocks, (juce::int64) std::ceil (endSec * sampleRate / peakBlock));
    float pk = 0.0f;
    for (auto b = b0; b < b1; ++b)
        for (int c = 0; c < ch; ++c)
            pk = juce::jmax (pk, std::abs (peaks[(size_t) ((b * ch + c) * 2)]), std::abs (peaks[(size_t) ((b * ch + c) * 2 + 1)]));
    return pk;
}

AudioCache::AudioCache() : juce::Thread ("FF Audio Loader")
{
    formatManager.registerBasicFormats();
    startThread (juce::Thread::Priority::low);
}

AudioCache::~AudioCache()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (4000);
    cancelPendingUpdate();
}

std::shared_ptr<CachedAudio> AudioCache::decode (juce::AudioFormatManager& fm, const juce::File& f, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr)
    {
        error = "Cannot read audio file " + f.getFileName() + (f.existsAsFile() ? " (unsupported format)" : " (file missing)");
        return nullptr;
    }
    const auto len = reader->lengthInSamples;
    const int ch = (int) juce::jlimit (1u, 2u, reader->numChannels);
    if (len <= 0 || len * ch > (juce::int64) 1 << 29) // ~2 GB of floats
    {
        error = f.getFileName() + " is empty or too long to load.";
        return nullptr;
    }
    auto a = std::make_shared<CachedAudio>();
    a->file = f;
    a->sampleRate = reader->sampleRate;
    a->buffer.setSize (ch, (int) len);
    if (! reader->read (&a->buffer, 0, (int) len, 0, true, ch > 1))
    {
        error = "Error decoding " + f.getFileName();
        return nullptr;
    }
    if (reader->numChannels == 1 && ch == 2)
        a->buffer.copyFrom (1, 0, a->buffer, 0, 0, (int) len);
    a->computePeaks();
    return a;
}

std::shared_ptr<const CachedAudio> AudioCache::get (const juce::File& f)
{
    const juce::ScopedLock sl (lock);
    const auto key = f.getFullPathName();
    if (auto it = cache.find (key); it != cache.end())
        return it->second;
    if (! queue.contains (key) && ! failed.contains (key))
    {
        queue.add (key);
        wake.signal();
    }
    return nullptr;
}

std::shared_ptr<const CachedAudio> AudioCache::loadSync (const juce::File& f, juce::String* error)
{
    const auto key = f.getFullPathName();
    {
        const juce::ScopedLock sl (lock);
        if (auto it = cache.find (key); it != cache.end())
            return it->second;
    }
    juce::String err;
    std::shared_ptr<const CachedAudio> a = decode (formatManager, f, err);
    if (error) *error = err;
    if (a != nullptr)
    {
        const juce::ScopedLock sl (lock);
        cache[key] = a;
    }
    return a;
}

void AudioCache::invalidate (const juce::File& f)
{
    const juce::ScopedLock sl (lock);
    cache.erase (f.getFullPathName());
    failed.removeString (f.getFullPathName());
}

void AudioCache::clear()
{
    const juce::ScopedLock sl (lock);
    cache.clear();
    failed.clear();
}

bool AudioCache::isLoading() const
{
    const juce::ScopedLock sl (lock);
    return ! queue.isEmpty();
}

juce::StringArray AudioCache::loadingFiles() const
{
    juce::StringArray names;
    const juce::ScopedLock sl (lock);
    for (auto& q : queue) names.add (juce::File (q).getFileName());
    return names;
}

void AudioCache::run()
{
    juce::AudioFormatManager fm; // own instance: format readers are not shared across threads
    fm.registerBasicFormats();
    while (! threadShouldExit())
    {
        juce::String key;
        {
            const juce::ScopedLock sl (lock);
            if (! queue.isEmpty())
                key = queue[0];
        }
        if (key.isEmpty())
        {
            wake.wait (500);
            continue;
        }
        juce::String err;
        std::shared_ptr<const CachedAudio> a = decode (fm, juce::File (key), err);
        {
            const juce::ScopedLock sl (lock);
            queue.removeString (key);
            if (a != nullptr) cache[key] = a;
            else { failed.add (key); pendingErrors.add (err); }
        }
        juce::Logger::writeToLog (a != nullptr ? "Loaded audio " + key : "Audio load failed: " + err);
        triggerAsyncUpdate();
    }
}

void AudioCache::handleAsyncUpdate()
{
    juce::StringArray errs;
    {
        const juce::ScopedLock sl (lock);
        errs.swapWith (pendingErrors);
    }
    if (onError)
        for (auto& e : errs)
            onError (e);
    if (onLoaded)
        onLoaded();
}
} // namespace ff
