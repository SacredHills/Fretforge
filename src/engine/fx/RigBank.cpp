#include "RigBank.h"
#include "engine/AudioEngine.h"
#include "core/ProjectFile.h"
#include "core/Timeline.h"

namespace ff::fx
{
namespace
{
    const juce::String liveKey ("__live__");

    std::pair<double, int> engineFormat (const AudioEngine& e)
    {
        const double sr = e.getSampleRate() > 0 ? e.getSampleRate() : 48000.0;
        const int bs = e.getBlockSize() > 0 ? e.getBlockSize() : 512;
        return { sr, juce::jmax (bs, 512) };
    }
}

bool loadImpulseFile (const juce::File& f, juce::AudioBuffer<float>& out, double& sampleRate, juce::String& error, double maxSeconds)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr)
    {
        error = f.existsAsFile() ? f.getFileName() + " is not a readable audio file." : "Missing impulse response: " + f.getFullPathName();
        return false;
    }
    if (r->lengthInSamples <= 0 || r->sampleRate <= 0)
    {
        error = f.getFileName() + " is empty.";
        return false;
    }
    const int len = (int) juce::jmin (r->lengthInSamples, (juce::int64) (maxSeconds * r->sampleRate));
    juce::AudioBuffer<float> tmp ((int) juce::jmax (1u, r->numChannels), len);
    r->read (&tmp, 0, len, 0, true, true);
    out.setSize (1, len);
    out.clear();
    for (int c = 0; c < tmp.getNumChannels(); ++c)
        out.addFrom (0, 0, tmp, c, 0, len, 1.0f / (float) tmp.getNumChannels());
    sampleRate = r->sampleRate;
    if (out.getMagnitude (0, len) < 1.0e-6f)
    {
        error = f.getFileName() + " is silent.";
        return false;
    }
    return true;
}

std::shared_ptr<AmpRig> makeOfflineRig (const juce::ValueTree& t, double sr, int block, juce::StringArray& warnings)
{
    auto rig = std::make_shared<AmpRig>();
    rig->setParamsFrom (t);
    rig->prepare (sr, block);
    const auto nam = t[rig::ids::namFile].toString();
    if (nam.isNotEmpty())
    {
        juce::String err;
        if (auto m = NamModel::load (juce::File (nam), err))
        {
            m->prepare (sr, block);
            rig->setModel (std::move (m));
        }
        else warnings.add (err + " The built-in amp was used instead.");
    }
    const auto ir = t[rig::ids::irFile].toString();
    if (ir.isNotEmpty())
    {
        juce::AudioBuffer<float> b;
        double irRate = 0;
        juce::String err;
        if (loadImpulseFile (juce::File (ir), b, irRate, err))
        {
            rig->setImpulseResponse (std::move (b), irRate);
            rig->finishLoadingOffline();
        }
        else warnings.add (err + " The built-in cabinet filter was used instead.");
    }
    for (auto c : t)
    {
        if (! c.hasType (rig::ids::PLUGIN)) continue;
        const int slot = (int) c[rig::ids::slot];
        const auto name = c[rig::ids::name].toString();
        if (! juce::isPositiveAndBelow (slot, rig::numPluginSlots) || c[rig::ids::desc].toString().isEmpty()) continue;
        auto* host = pluginHost();
        if (host == nullptr) { warnings.add ("Plugin \"" + name + "\" was skipped (plugin hosting is not available here)."); continue; }
        juce::String err;
        auto inst = host->createBlocking (c[rig::ids::desc].toString(), sr, block, err);
        if (inst == nullptr) { warnings.add ("Plugin \"" + name + "\" could not be loaded" + (err.isNotEmpty() ? ": " + err : juce::String()) + "; it was skipped."); continue; }
        restorePluginState (*inst, c[rig::ids::state].toString());
        preparePlugin (*inst, sr, block);
        rig->setPlugin (slot, std::shared_ptr<juce::AudioPluginInstance> (inst.release()));
    }
    rig->reset();
    return rig;
}

// ---------------------------------------------------------------------------------------------------------------

RigBank::RigBank (AudioEngine& e) : engine (e)
{
    live = rig::makeDefault();
    live.setProperty (rig::ids::enabled, false, nullptr);
    live.addListener (this);
    liveEntry.rig = makeRig();
    liveEntry.tree = live;
    syncEntry (liveEntry);
    engine.liveRig.store (liveEntry.rig.get());
    engine.setPrepareHook ([this] (double sr, int bs) { prepareAll (sr, bs); });
    startTimerHz (10);
}

RigBank::~RigBank()
{
    stopTimer();
    releaseEntryPlugins (liveEntry);
    for (auto& [k, e] : tracks) releaseEntryPlugins (e);
    *alive = false;
    engine.setPrepareHook (nullptr);
    engine.liveRigOn.store (false);
    engine.liveRig.store (nullptr);
    engine.waitForAudioCallback();
    loader.removeAllJobs (true, 5000);
    root.removeListener (this);
    live.removeListener (this);
}

std::shared_ptr<AmpRig> RigBank::makeRig()
{
    auto r = std::make_shared<AmpRig> (&convQueue);
    const auto [sr, bs] = engineFormat (engine);
    r->prepare (sr, bs);
    return r;
}

void RigBank::prepareAll (double sr, int block)
{
    const juce::ScopedLock sl (lock);
    block = juce::jmax (block, 512);
    liveEntry.rig->prepare (sr, block);
    for (auto& [id, e] : tracks) e.rig->prepare (sr, block);
}

void RigBank::rebind (juce::ValueTree projectRoot)
{
    root.removeListener (this);
    root = projectRoot;
    root.addListener (this);
    syncAll();
}

void RigBank::setLiveTree (juce::ValueTree t)
{
    if (! t.hasType (rig::ids::RIG)) return;
    live.copyPropertiesFrom (t, nullptr);   // keeps the same node (listeners, UI) and fires property changes
    live.removeAllChildren (nullptr);
    for (auto c : t) live.appendChild (c.createCopy(), nullptr);   // plugin slots
}

std::shared_ptr<AmpRig> RigBank::rigForTrack (const juce::String& id) const
{
    const juce::ScopedLock sl (lock);
    if (auto it = tracks.find (id); it != tracks.end()) return it->second.rig;
    return nullptr;
}

RigBank::Entry* RigBank::entryFor (const juce::ValueTree& t)
{
    if (t == live) return &liveEntry;
    for (auto& [id, e] : tracks)
        if (e.tree == t) return &e;
    return nullptr;
}

const RigBank::Entry* RigBank::entryFor (const juce::ValueTree& t) const
{
    return const_cast<RigBank*> (this)->entryFor (t);
}

RigBank::Status RigBank::statusFor (const juce::ValueTree& t) const
{
    if (auto* e = entryFor (t)) return e->status;
    Status s;
    s.modelPath = t[rig::ids::namFile].toString();
    s.irPath = t[rig::ids::irFile].toString();
    return s;
}

AmpRig* RigBank::rigFor (const juce::ValueTree& t) const
{
    auto* e = entryFor (t);
    return e != nullptr ? e->rig.get() : nullptr;
}

void RigBank::syncAll()
{
    bool changed = false;
    std::map<juce::String, juce::ValueTree> wanted;
    for (const auto& t : root.getChildWithName (project::ids::TRACKS))
        if (t.hasType (timeline::ids::TRACK))
            if (auto r = rig::find (t); rig::isEnabled (r))
                wanted[t[timeline::ids::id].toString()] = r;

    {
        const juce::ScopedLock sl (lock);
        for (auto it = tracks.begin(); it != tracks.end();)
        {
            if (auto w = wanted.find (it->first); w == wanted.end() || w->second != it->second.tree)
            {
                for (int sl = 0; sl < rig::numPluginSlots; ++sl)   // editors close; the plugins die with the rig
                    if (auto p = it->second.rig->getPlugin (sl); p != nullptr && onPluginRemoved) onPluginRemoved (p.get());
                it = tracks.erase (it);   // the playback graph may still hold the rig; it dies with the graph
                changed = true;
            }
            else ++it;
        }
    }
    for (auto& [id, t] : wanted)
    {
        if (tracks.count (id) > 0) { syncEntry (tracks[id]); continue; }
        Entry e;
        e.rig = makeRig();
        e.tree = t;
        {
            const juce::ScopedLock sl (lock);
            tracks[id] = std::move (e);
        }
        syncEntry (tracks[id]);
        changed = true;
    }
    if (changed && onRigsChanged) onRigsChanged();
}

void RigBank::syncEntry (Entry& e)
{
    e.rig->setParamsFrom (e.tree);
    if (&e == &liveEntry)
    {
        engine.liveRigOn.store (rig::isEnabled (live));
        engine.liveInput.store ((int) live.getProperty (rig::ids::input, -1));
    }
    if (e.tree[rig::ids::namFile].toString() != e.status.modelPath) requestModel (e, false);
    if (e.tree[rig::ids::irFile].toString() != e.status.irPath) requestIr (e, false);
    for (int sl = 0; sl < rig::numPluginSlots; ++sl)
    {
        const auto node = rig::pluginNode (e.tree, sl);
        if (node[rig::ids::desc].toString() != e.status.plugins[(size_t) sl].desc) requestPlugin (e, sl);
        else e.status.plugins[(size_t) sl].name = node[rig::ids::name].toString();
    }
}

juce::String RigBank::keyOf (const Entry& e) const
{
    return (&e == &liveEntry) ? liveKey : e.tree.getParent()[timeline::ids::id].toString();
}

RigBank::Entry* RigBank::entryByKey (const juce::String& key)
{
    if (key == liveKey) return &liveEntry;
    if (auto it = tracks.find (key); it != tracks.end()) return &it->second;
    return nullptr;
}

void RigBank::releasePlugin (std::shared_ptr<juce::AudioPluginInstance> old)
{
    if (old == nullptr) return;
    if (onPluginRemoved) onPluginRemoved (old.get());
    engine.waitForAudioCallback();   // the audio thread may still be inside it
    old->releaseResources();
    old.reset();
}

void RigBank::releaseEntryPlugins (Entry& e)
{
    for (int sl = 0; sl < rig::numPluginSlots; ++sl)
    {
        std::shared_ptr<juce::AudioPluginInstance> old;
        {
            const juce::ScopedLock lk (lock);
            old = e.rig->setPlugin (sl, nullptr);
        }
        releasePlugin (std::move (old));
    }
}

void RigBank::requestPlugin (Entry& e, int slot)
{
    auto& st = e.status.plugins[(size_t) slot];
    const auto node = rig::pluginNode (e.tree, slot);
    st = {};
    st.desc = node[rig::ids::desc].toString();
    st.name = node[rig::ids::name].toString();
    const int gen = ++generationCounter;
    e.pluginGeneration[(size_t) slot] = gen;
    if (st.desc.isEmpty())
    {
        std::shared_ptr<juce::AudioPluginInstance> old;
        {
            const juce::ScopedLock lk (lock);
            old = e.rig->setPlugin (slot, nullptr);
        }
        releasePlugin (std::move (old));
        if (onStatusChanged) onStatusChanged();
        return;
    }
    auto* host = pluginHost();
    if (host == nullptr)
    {
        st.error = "Plugin hosting is not available.";
        if (onStatusChanged) onStatusChanged();
        return;
    }
    st.loading = true;
    const auto [sr, bs] = engineFormat (engine);
    std::weak_ptr<bool> token = alive;
    const auto key = keyOf (e);
    host->createAsync (st.desc, sr, bs, [this, token, key, slot, gen] (std::unique_ptr<juce::AudioPluginInstance> inst, juce::String err) {
        if (token.expired()) return;
        auto* en = entryByKey (key);
        if (en == nullptr || en->pluginGeneration[(size_t) slot] != gen) return;   // superseded; inst is freed here
        auto& s2 = en->status.plugins[(size_t) slot];
        s2.loading = false;
        if (inst == nullptr)
        {
            s2.error = err.isNotEmpty() ? err : juce::String ("The plugin could not be loaded.");
            if (onStatusChanged) onStatusChanged();
            return;
        }
        restorePluginState (*inst, rig::pluginNode (en->tree, slot)[rig::ids::state].toString());
        const auto [r, b] = engineFormat (engine);
        std::shared_ptr<juce::AudioPluginInstance> old;
        {
            const juce::ScopedLock lk (lock);   // not while the device re-prepares the rigs
            preparePlugin (*inst, en->rig->getPreparedRate() > 0 ? en->rig->getPreparedRate() : r, juce::jmax (b, 512));
            s2.latency = inst->getLatencySamples();
            old = en->rig->setPlugin (slot, std::shared_ptr<juce::AudioPluginInstance> (inst.release()));
        }
        s2.loaded = true;
        releasePlugin (std::move (old));
        if (onStatusChanged) onStatusChanged();
    });
    if (onStatusChanged) onStatusChanged();
}

std::shared_ptr<juce::AudioPluginInstance> RigBank::pluginFor (const juce::ValueTree& t, int slot) const
{
    auto* e = entryFor (t);
    return e != nullptr ? e->rig->getPlugin (slot) : nullptr;
}

void RigBank::captureAllPluginStates()
{
    auto capture = [] (Entry& e) {
        for (int sl = 0; sl < rig::numPluginSlots; ++sl)
            if (auto p = e.rig->getPlugin (sl))
                if (auto node = rig::pluginNode (e.tree, sl); node.isValid())
                {
                    const auto st = capturePluginState (*p);
                    if (st != node[rig::ids::state].toString()) node.setProperty (rig::ids::state, st, nullptr);
                }
    };
    capture (liveEntry);
    for (auto& [k, e] : tracks) capture (e);
}

void RigBank::requestModel (Entry& e, bool)
{
    const auto path = e.tree[rig::ids::namFile].toString();
    const auto key = (&e == &liveEntry) ? liveKey : e.tree.getParent()[timeline::ids::id].toString();
    e.status.modelPath = path;
    e.status.modelError = {};
    e.status.modelLoaded = false;
    e.status.modelInfo = {};
    const int gen = ++generationCounter;
    e.generation = gen;
    if (path.isEmpty())
    {
        const juce::ScopedLock sl (lock);
        e.rig->clearModel();
        e.status.modelLoading = false;
        if (onStatusChanged) onStatusChanged();
        return;
    }
    e.status.modelLoading = true;
    const auto [sr, bs] = engineFormat (engine);
    std::weak_ptr<bool> token = alive;
    loader.addJob ([this, token, key, gen, path, sr = sr, bs = bs] {
        juce::String err;
        auto m = NamModel::load (juce::File (path), err);
        if (m != nullptr) m->prepare (sr, bs);
        auto holder = std::make_shared<std::unique_ptr<NamModel>> (std::move (m));
        juce::MessageManager::callAsync ([this, token, key, gen, holder, err] {
            if (token.expired()) return;
            Entry* e = key == liveKey ? &liveEntry : nullptr;
            if (e == nullptr)
                if (auto it = tracks.find (key); it != tracks.end()) e = &it->second;
            if (e == nullptr || e->generation != gen) return;   // superseded or the track's rig is gone
            e->status.modelLoading = false;
            if (*holder != nullptr)
            {
                e->status.modelInfo = (*holder)->info();
                e->status.modelLoaded = true;
                const juce::ScopedLock sl (lock);
                // the device may have restarted at another rate while loading: the rig flags it and we reload
                e->rig->setModel (std::move (*holder));
            }
            else
            {
                e->status.modelError = err;
                const juce::ScopedLock sl (lock);
                e->rig->clearModel();
            }
            if (onStatusChanged) onStatusChanged();
        });
    });
    if (onStatusChanged) onStatusChanged();
}

void RigBank::requestIr (Entry& e, bool)
{
    const auto path = e.tree[rig::ids::irFile].toString();
    const auto key = (&e == &liveEntry) ? liveKey : e.tree.getParent()[timeline::ids::id].toString();
    e.status.irPath = path;
    e.status.irError = {};
    e.status.irLoaded = false;
    e.status.irName = {};
    if (path.isEmpty())
    {
        e.rig->clearImpulseResponse();
        e.status.irLoading = false;
        if (onStatusChanged) onStatusChanged();
        return;
    }
    e.status.irLoading = true;
    std::weak_ptr<bool> token = alive;
    loader.addJob ([this, token, key, path] {
        auto buf = std::make_shared<juce::AudioBuffer<float>>();
        double rate = 0;
        juce::String err;
        const bool ok = loadImpulseFile (juce::File (path), *buf, rate, err);
        juce::MessageManager::callAsync ([this, token, key, path, buf, rate, err, ok] {
            if (token.expired()) return;
            Entry* e = key == liveKey ? &liveEntry : nullptr;
            if (e == nullptr)
                if (auto it = tracks.find (key); it != tracks.end()) e = &it->second;
            if (e == nullptr || e->status.irPath != path) return;
            e->status.irLoading = false;
            if (ok)
            {
                e->status.irLoaded = true;
                e->status.irName = juce::File (path).getFileNameWithoutExtension();
                e->status.irSeconds = buf->getNumSamples() / rate;
                const juce::ScopedLock sl (lock);
                e->rig->setImpulseResponse (std::move (*buf), rate);
            }
            else
            {
                e->status.irError = err;
                e->rig->clearImpulseResponse();
            }
            if (onStatusChanged) onStatusChanged();
        });
    });
    if (onStatusChanged) onStatusChanged();
}

void RigBank::timerCallback()
{
    auto service = [this] (Entry& e) {
        e.rig->collectGarbage();
        if (e.rig->modelNeedsPrepare.load() && ! e.status.modelLoading && e.status.modelPath.isNotEmpty())
        {
            e.rig->modelNeedsPrepare = false;
            requestModel (e, true);   // reload for the new device rate
        }
    };
    service (liveEntry);
    for (auto& [id, e] : tracks) service (e);
    if (++ticks % 20 == 0) captureAllPluginStates();
}

void RigBank::valueTreePropertyChanged (juce::ValueTree& node, const juce::Identifier& prop)
{
    if (node.hasType (rig::ids::PLUGIN))
    {
        if (node.getParent() == live && onLiveChanged) onLiveChanged();   // keep the live rig's plugin state saved
        return;
    }
    if (! node.hasType (rig::ids::RIG)) return;
    if (node == live)
    {
        syncEntry (liveEntry);
        if (onLiveChanged) onLiveChanged();
        return;
    }
    if (prop == rig::ids::enabled) { syncAll(); return; }
    if (auto* e = entryFor (node)) syncEntry (*e);
}

void RigBank::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    if (child.hasType (rig::ids::PLUGIN))
    {
        if (parent == live) { syncEntry (liveEntry); if (onLiveChanged) onLiveChanged(); }
        else if (auto* e = entryFor (parent)) syncEntry (*e);
        return;
    }
    if (child.hasType (rig::ids::RIG) || child.hasType (timeline::ids::TRACK) || child.hasType (project::ids::TRACKS)) syncAll();
}

void RigBank::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (child.hasType (rig::ids::PLUGIN))
    {
        if (parent == live) { syncEntry (liveEntry); if (onLiveChanged) onLiveChanged(); }
        else if (auto* e = entryFor (parent)) syncEntry (*e);
        return;
    }
    if (child.hasType (rig::ids::RIG) || child.hasType (timeline::ids::TRACK) || child.hasType (project::ids::TRACKS)) syncAll();
}
} // namespace ff::fx
