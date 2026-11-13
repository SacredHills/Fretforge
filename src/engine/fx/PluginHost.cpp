#include "PluginHost.h"

#include <map>
#include <mutex>

namespace ff::fx
{
namespace
{
std::atomic<PluginHost*> theHost { nullptr };

// ---- crash protection state ----
/** Notices settings changes of one plugin instance (any thread). */
struct StateWatch final : public juce::AudioProcessorListener
{
    std::atomic<bool> dirty { true };   // a new plugin is captured once
    int backoff = 0, wait = 0;          // message thread: periodic captures to skip
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { dirty.store (true, std::memory_order_relaxed); }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override
    {
        if (d.parameterInfoChanged || d.programChanged || d.nonParameterStateChanged) dirty.store (true, std::memory_order_relaxed);
    }
};
struct Names
{
    juce::String name, id;
    std::shared_ptr<StateWatch> watch;
    // what the instance was last prepared for (message thread), so a device restart with the same format leaves it alone
    double preparedRate = 0.0;
    int preparedBlock = 0;
};
std::mutex registryMutex;
std::map<const void*, Names>& registry() { static std::map<const void*, Names> r; return r; }

struct AudioActivity { const juce::AudioPluginInstance* plugin = nullptr; };
thread_local AudioActivity audioActivity;

struct MessageActivity
{
    std::mutex mutex;
    juce::String name, id, what;
    std::atomic<int> depth { 0 };
    std::atomic<juce::uint32> started { 0 };
    std::atomic<bool> hangReported { false };
};
MessageActivity& msgActivity() { static MessageActivity m; return m; }

juce::File& reportFileRef() { static juce::File f; return f; }

juce::String reportText (const juce::String& name, const juce::String& id, const juce::String& what, bool hang, bool withStack)
{
    juce::String t;
    t << "plugin=" << name << "\nfile=" << id << "\nactivity=" << what << "\nkind=" << (hang ? "hang" : "crash") << "\n";
    t << "time=" << juce::Time::getCurrentTime().toString (true, true) << "\n";
    t << "os=" << juce::SystemStats::getOperatingSystemName() << "\n";
    if (withStack) t << "\n" << juce::SystemStats::getStackBacktrace();
    return t;
}

void crashHandler (void*)
{
    // best effort: the process is going down anyway
    juce::String name, id, what;
    if (auto* p = audioActivity.plugin)
    {
        what = "processing audio";
        if (registryMutex.try_lock())
        {
            if (auto it = registry().find (p); it != registry().end()) { name = it->second.name; id = it->second.id; }
            registryMutex.unlock();
        }
    }
    else if (auto& m = msgActivity(); m.depth.load() > 0 && m.mutex.try_lock())
    {
        name = m.name; id = m.id; what = m.what;
        m.mutex.unlock();
    }
    if (reportFileRef() != juce::File())
        reportFileRef().replaceWithText (reportText (name, id, what, false, true));
}

class Watchdog final : public juce::Thread
{
public:
    Watchdog() : juce::Thread ("RAVENYR plugin watchdog") {}
    void run() override
    {
        while (! threadShouldExit())
        {
            wait (500);
            auto& m = msgActivity();
            if (m.depth.load() <= 0 || m.hangReported.load()) continue;
            if (juce::Time::getMillisecondCounter() - m.started.load() < 8000) continue;
            juce::String name, id, what;
            {
                const std::lock_guard<std::mutex> lock (m.mutex);
                name = m.name; id = m.id; what = m.what;
            }
            m.hangReported = true;   // if the user force-quits now, the next start offers to disable the plugin
            if (reportFileRef() != juce::File()) reportFileRef().replaceWithText (reportText (name, id, what, true, false));
        }
    }
};
std::unique_ptr<Watchdog>& watchdog() { static std::unique_ptr<Watchdog> w; return w; }
}

void registerPluginInstance (juce::AudioPluginInstance* p, const juce::String& name, const juce::String& id)
{
    if (p == nullptr) return;
    auto watch = std::make_shared<StateWatch>();
    p->addListener (watch.get());
    const std::lock_guard<std::mutex> lock (registryMutex);
    registry()[p] = { name, id, watch, 0.0, 0 };   // an address reused by a later plugin is simply overwritten
}

void unregisterPluginInstance (juce::AudioPluginInstance* p)
{
    if (p == nullptr) return;
    std::shared_ptr<StateWatch> watch;
    {
        const std::lock_guard<std::mutex> lock (registryMutex);
        if (auto it = registry().find (p); it != registry().end()) { watch = it->second.watch; registry().erase (it); }
    }
    if (watch != nullptr) p->removeListener (watch.get());
}

bool pluginStateNeedsCapture (const juce::AudioPluginInstance& p)
{
    const std::lock_guard<std::mutex> lock (registryMutex);
    auto it = registry().find (&p);
    if (it == registry().end() || it->second.watch == nullptr) return true;
    auto& w = *it->second.watch;
    if (! w.dirty.load (std::memory_order_relaxed)) return false;
    if (w.wait > 0) { --w.wait; return false; }
    w.dirty.store (false, std::memory_order_relaxed);
    return true;
}

void markPluginStateChanged (const juce::AudioPluginInstance& p)
{
    const std::lock_guard<std::mutex> lock (registryMutex);
    if (auto it = registry().find (&p); it != registry().end() && it->second.watch != nullptr)
        it->second.watch->dirty.store (true, std::memory_order_relaxed);
}

void pluginStateCaptured (const juce::AudioPluginInstance& p, bool differed)
{
    const std::lock_guard<std::mutex> lock (registryMutex);
    auto it = registry().find (&p);
    if (it == registry().end() || it->second.watch == nullptr) return;
    auto& w = *it->second.watch;
    w.backoff = differed ? 0 : juce::jlimit (1, 30, w.backoff * 2);   // "changed" but the same state: ask less often
    w.wait = w.backoff;
}

PluginActivity::PluginActivity (const juce::String& name, const juce::String& id, const char* what)
{
    if (! juce::MessageManager::existsAndIsCurrentThread()) return;   // only the message thread is watched
    tracked = true;
    auto& m = msgActivity();
    if (m.depth.fetch_add (1) == 0)
    {
        outer = true;
        const std::lock_guard<std::mutex> lock (m.mutex);
        m.name = name; m.id = id; m.what = what;
        m.hangReported = false;
        m.started = juce::Time::getMillisecondCounter();
    }
}

static Names namesOf (const juce::AudioPluginInstance& p)
{
    {
        const std::lock_guard<std::mutex> lock (registryMutex);
        if (auto it = registry().find (&p); it != registry().end()) return it->second;
    }
    return { p.getName(), {}, nullptr, 0.0, 0 };
}

PluginActivity::PluginActivity (const juce::AudioPluginInstance& p, const char* what)
    : PluginActivity (namesOf (p).name, namesOf (p).id, what) {}

PluginActivity::~PluginActivity()
{
    if (! tracked) return;
    auto& m = msgActivity();
    if (outer && m.hangReported.load() && reportFileRef() != juce::File())
        reportFileRef().deleteFile();   // it was slow, not stuck
    m.depth.fetch_sub (1);
}

void installPluginCrashReporter (const juce::File& f)
{
    reportFileRef() = f;
    juce::SystemStats::setApplicationCrashHandler (crashHandler);
    if (watchdog() == nullptr)
    {
        watchdog() = std::make_unique<Watchdog>();
        watchdog()->startThread (juce::Thread::Priority::low);
    }
}

void shutdownPluginCrashReporter()
{
    if (watchdog() != nullptr) { watchdog()->stopThread (2000); watchdog().reset(); }
}

PluginCrashReport takePreviousCrashReport (const juce::File& f)
{
    PluginCrashReport r;
    if (! f.existsAsFile()) return r;
    r.exists = true;
    r.text = f.loadFileAsString();
    for (auto line : juce::StringArray::fromLines (r.text))
    {
        if (line.startsWith ("plugin=")) r.pluginName = line.fromFirstOccurrenceOf ("=", false, false);
        else if (line.startsWith ("file=")) r.fileOrIdentifier = line.fromFirstOccurrenceOf ("=", false, false);
        else if (line.startsWith ("activity=")) r.activity = line.fromFirstOccurrenceOf ("=", false, false);
        else if (line.startsWith ("kind=")) r.hang = line.endsWith ("hang");
    }
    const auto old = f.getSiblingFile (f.getFileNameWithoutExtension() + "-previous" + f.getFileExtension());
    old.deleteFile();
    f.moveFileTo (old);
    return r;
}

void setPluginHost (PluginHost* h) { theHost.store (h); }
PluginHost* pluginHost() { return theHost.load(); }

void restorePluginState (juce::AudioPluginInstance& p, const juce::String& base64)
{
    if (base64.isEmpty()) return;
    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding (base64) && mb.getSize() > 0)
    {
        PluginActivity activity (p, "restoring its settings");
        p.setStateInformation (mb.getData(), (int) mb.getSize());
    }
}

juce::String capturePluginState (juce::AudioPluginInstance& p)
{
    juce::MemoryBlock mb;
    PluginActivity activity (p, "saving its settings");
    p.getStateInformation (mb);
    return mb.getSize() > 0 ? mb.toBase64Encoding() : juce::String();
}

void runPluginStereo (juce::AudioPluginInstance& p, juce::AudioBuffer<float>& scratch, juce::MidiBuffer& midi,
                      float* L, float* R, int n, const float* scL, const float* scR) noexcept
{
    if (n > scratch.getNumSamples()) return;
    const int ins = p.getTotalNumInputChannels(), outs = p.getTotalNumOutputChannels();
    const int ch = juce::jmax (ins, outs, 1);
    if (ch > scratch.getNumChannels()) return;   // more channels than we can give it: pass through rather than crash
    float* const* chans = scratch.getArrayOfWritePointers();
    if (ins >= 2) { std::copy (L, L + n, chans[0]); std::copy (R, R + n, chans[1]); }
    else for (int i = 0; i < n; ++i) chans[0][i] = 0.5f * (L[i] + R[i]);
    for (int c = ins >= 2 ? 2 : 1; c < ch; ++c)
        juce::FloatVectorOperations::clear (chans[c], n);
    if (scL != nullptr && ins > 2)   // the side-chain input (the plugin's second input bus) follows the main pair
    {
        if (ins >= 4) { std::copy (scL, scL + n, chans[2]); std::copy (scR != nullptr ? scR : scL, (scR != nullptr ? scR : scL) + n, chans[3]); }
        else for (int i = 0; i < n; ++i) chans[2][i] = 0.5f * (scL[i] + (scR != nullptr ? scR[i] : scL[i]));
    }
    juce::AudioBuffer<float> view (chans, ch, n);
    midi.clear();
    {
        const juce::ScopedLock sl (p.getCallbackLock());
        if (p.isSuspended()) return;
        audioActivity.plugin = &p;
        p.processBlock (view, midi);
        audioActivity.plugin = nullptr;
    }
    if (outs >= 2) { std::copy (chans[0], chans[0] + n, L); std::copy (chans[1], chans[1] + n, R); }
    else if (outs == 1) { std::copy (chans[0], chans[0] + n, L); std::copy (chans[0], chans[0] + n, R); }
}

void preparePlugin (juce::AudioPluginInstance& p, double sampleRate, int blockSize)
{
    // Already running at this rate and block size (the audio device restarted with the same format): leave it alone.
    // Changing buses on a running plugin is not allowed (plugins may stop processing or hang), and re-activating
    // is slow for big plugins.
    {
        const std::lock_guard<std::mutex> lock (registryMutex);
        if (auto it = registry().find (&p); it != registry().end())
        {
            if (it->second.preparedBlock == blockSize && std::abs (it->second.preparedRate - sampleRate) < 0.01) return;
            it->second.preparedRate = sampleRate;
            it->second.preparedBlock = blockSize;
        }
    }
    PluginActivity activity (p, "starting up");
    // (callers guarantee the audio thread is not inside this plugin: it is new, or the device is stopped)
    p.releaseResources();   // deactivate first: buses may only be changed while the plugin is not running
    // guitar chain: stereo in/out if the plugin can, otherwise mono, otherwise whatever it has; extra buses
    // (side-chains, aux outputs) are switched off where the plugin allows it
    const auto layout = p.getBusesLayout();
    if (p.getBusCount (true) > 0 || p.getBusCount (false) > 0)
    {
        auto make = [&layout] (juce::AudioChannelSet main, bool disableExtras)
        {
            auto l = layout;
            for (auto* buses : { &l.inputBuses, &l.outputBuses })
                for (int i = 0; i < buses->size(); ++i)
                    if (i == 0) buses->getReference (i) = main;
                    else if (disableExtras) buses->getReference (i) = juce::AudioChannelSet::disabled();
            return l;
        };
        const auto st = juce::AudioChannelSet::stereo(), mo = juce::AudioChannelSet::mono();
        // first choice: stereo main in/out, the side-chain (second input bus) on as a stereo pair, other extra outputs off
        auto sc = layout;
        bool hasSc = sc.inputBuses.size() > 1;
        for (int i = 0; i < sc.inputBuses.size(); ++i) sc.inputBuses.getReference (i) = i == 0 ? st : (i == 1 ? st : juce::AudioChannelSet::disabled());
        for (int i = 0; i < sc.outputBuses.size(); ++i) sc.outputBuses.getReference (i) = i == 0 ? st : juce::AudioChannelSet::disabled();
        if (hasSc && p.setBusesLayout (sc)) {}
        else if (! p.setBusesLayout (make (st, true)) && ! p.setBusesLayout (make (st, false))
            && ! p.setBusesLayout (make (mo, true)))
            p.setBusesLayout (make (mo, false));
    }
    p.setRateAndBufferSizeDetails (sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);
}
} // namespace ff::fx
