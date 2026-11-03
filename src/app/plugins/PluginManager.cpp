#include "PluginManager.h"
#include "../StudioLookAndFeel.h"
#include <condition_variable>
#include <queue>

namespace ff
{
// ===================================================================== child process side

class PluginScannerWorker final : private juce::ChildProcessWorker, private juce::AsyncUpdater
{
public:
    PluginScannerWorker() { juce::addDefaultFormatsToManager (formats); }
    using juce::ChildProcessWorker::initialiseFromCommandLine;

private:
    juce::AudioPluginFormatManager formats;
    std::mutex mutex;
    std::queue<juce::MemoryBlock> pending;

    void handleMessageFromCoordinator (const juce::MemoryBlock& mb) override
    {
        if (mb.isEmpty()) return;
        const std::lock_guard<std::mutex> lock (mutex);
        if (auto results = scan (mb); ! results.isEmpty()) send (results);
        else { pending.push (mb); triggerAsyncUpdate(); }   // formats that need the message thread
    }

    void handleConnectionLost() override { juce::JUCEApplicationBase::quit(); }

    void handleAsyncUpdate() override
    {
        for (;;)
        {
            const std::lock_guard<std::mutex> lock (mutex);
            if (pending.empty()) return;
            send (scan (pending.front()));
            pending.pop();
        }
    }

    juce::OwnedArray<juce::PluginDescription> scan (const juce::MemoryBlock& block)
    {
        juce::MemoryInputStream in (block, false);
        const auto formatName = in.readString();
        const auto identifier = in.readString();
        juce::PluginDescription pd;
        pd.fileOrIdentifier = identifier;
        juce::OwnedArray<juce::PluginDescription> results;
        for (auto* f : formats.getFormats())
            if (f->getName() == formatName
                && (juce::MessageManager::getInstance()->isThisTheMessageThread() || f->requiresUnblockedMessageThreadDuringCreation (pd)))
                f->findAllTypesForFile (results, identifier);
        return results;
    }

    void send (const juce::OwnedArray<juce::PluginDescription>& results)
    {
        juce::XmlElement xml ("LIST");
        for (auto* d : results) xml.addChildElement (d->createXml().release());
        const auto s = xml.toString();
        sendMessageToCoordinator ({ s.toRawUTF8(), s.getNumBytesAsUTF8() });
    }
};

PluginScannerWorker* startPluginScannerWorker (const juce::String& commandLine)
{
    auto w = std::make_unique<PluginScannerWorker>();
    if (w->initialiseFromCommandLine (commandLine, pluginScannerProcessId)) return w.release();
    return nullptr;
}

void destroyPluginScannerWorker (PluginScannerWorker* w) { delete w; }

// ===================================================================== app side: out-of-process scanning

namespace
{
class ScannerProcess final : private juce::ChildProcessCoordinator
{
public:
    ScannerProcess()
    {
        launchWorkerProcess (juce::File::getSpecialLocation (juce::File::currentExecutableFile), pluginScannerProcessId, 0, 0);
    }
    enum class State { timeout, gotResult, lost };
    std::pair<State, std::unique_ptr<juce::XmlElement>> response()
    {
        std::unique_lock<std::mutex> lock (mutex);
        if (! cv.wait_for (lock, std::chrono::milliseconds (50), [this] { return got || lost; })) return { State::timeout, nullptr };
        const auto st = lost ? State::lost : State::gotResult;
        got = lost = false;
        return { st, std::move (xml) };
    }
    using juce::ChildProcessCoordinator::sendMessageToWorker;
private:
    void handleMessageFromWorker (const juce::MemoryBlock& mb) override
    {
        const std::lock_guard<std::mutex> lock (mutex);
        xml = juce::parseXML (mb.toString());
        got = true;
        cv.notify_one();
    }
    void handleConnectionLost() override
    {
        const std::lock_guard<std::mutex> lock (mutex);
        lost = true;
        cv.notify_one();
    }
    std::mutex mutex;
    std::condition_variable cv;
    std::unique_ptr<juce::XmlElement> xml;
    bool got = false, lost = false;
};

class OutOfProcessScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    bool findPluginTypesFor (juce::AudioPluginFormat& format, juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override
    {
        if (process == nullptr) process = std::make_unique<ScannerProcess>();
        juce::MemoryBlock block;
        {
            juce::MemoryOutputStream out (block, true);
            out.writeString (format.getName());
            out.writeString (fileOrIdentifier);
        }
        if (! process->sendMessageToWorker (block)) { process = nullptr; return false; }
        const auto started = juce::Time::getMillisecondCounter();
        for (;;)
        {
            if (shouldExit()) return true;
            auto [state, xml] = process->response();
            if (state == ScannerProcess::State::timeout)
            {
                if (juce::Time::getMillisecondCounter() - started > 60000) { process = nullptr; return false; }   // hung plugin
                continue;
            }
            if (xml != nullptr)
                for (auto* item : xml->getChildIterator())
                {
                    auto d = std::make_unique<juce::PluginDescription>();
                    if (d->loadFromXml (*item)) result.add (std::move (d));
                }
            if (state == ScannerProcess::State::lost) { process = nullptr; return false; }   // it crashed: mark as failed
            return true;
        }
    }
    void scanFinished() override { process = nullptr; }
private:
    std::unique_ptr<ScannerProcess> process;
};
}

// ===================================================================== editor windows

struct PluginManager::EditorWindow final : public juce::DocumentWindow
{
    EditorWindow (std::shared_ptr<juce::AudioPluginInstance> p, const juce::String& title, std::function<void()> closed)
        : juce::DocumentWindow (title, palette::bg, juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
          plugin (std::move (p)), onClosed (std::move (closed))
    {
        setUsingNativeTitleBar (true);
        juce::AudioProcessorEditor* ed = plugin->hasEditor() ? plugin->createEditorIfNeeded() : nullptr;
        if (ed == nullptr) ed = new juce::GenericAudioProcessorEditor (*plugin);
        editor.reset (ed);
        setContentNonOwned (ed, true);
        setResizable (ed->isResizable(), false);
        setAlwaysOnTop (true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }
    ~EditorWindow() override
    {
        clearContentComponent();
        if (editor != nullptr) plugin->editorBeingDeleted (editor.get());
        editor.reset();
    }
    void closeButtonPressed() override { if (onClosed) juce::MessageManager::callAsync (onClosed); }

    std::shared_ptr<juce::AudioPluginInstance> plugin;   // keeps the plugin alive while its editor is open
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::function<void()> onClosed;
};

// ===================================================================== manager

PluginManager::PluginManager (juce::PropertiesFile& s) : settings (s)
{
    juce::addDefaultFormatsToManager (formats);
    if (auto xml = settings.getXmlValue ("knownPlugins")) known.recreateFromXml (*xml);
    known.setCustomScanner (std::make_unique<OutOfProcessScanner>());
    known.addChangeListener (this);
    deadMansPedal = settings.getFile().getSiblingFile ("PluginScanCrashGuard.txt");
    fx::setPluginHost (this);
}

PluginManager::~PluginManager()
{
    fx::setPluginHost (nullptr);
    *alive = false;
    closeAllEditors();
    managerWindow.reset();
    known.removeChangeListener (this);
}

void PluginManager::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto xml = known.createXml()) settings.setValue ("knownPlugins", xml.get());
}

void PluginManager::addToMenu (juce::PopupMenu& menu, int) const
{
    juce::Array<juce::PluginDescription> effects;
    for (auto& d : known.getTypes())
        if (! d.isInstrument) effects.add (d);
    juce::KnownPluginList::addToMenu (menu, effects, juce::KnownPluginList::sortByManufacturer);
}

juce::String PluginManager::descriptionForMenuResult (int result, int, juce::String& name) const
{
    juce::Array<juce::PluginDescription> effects;
    for (auto& d : known.getTypes())
        if (! d.isInstrument) effects.add (d);
    const int idx = juce::KnownPluginList::getIndexChosenByMenu (effects, result);
    if (! juce::isPositiveAndBelow (idx, effects.size())) return {};
    name = effects[idx].name;
    return effects[idx].createXml()->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader());
}

void PluginManager::showManager()
{
    if (managerWindow == nullptr)
    {
        struct Window : juce::DocumentWindow
        {
            Window (juce::Component* c)
                : juce::DocumentWindow ("Plugins (" + juce::String (pluginFormatsLabel()) + ")", palette::bg,
                                        juce::DocumentWindow::closeButton)
            {
                setUsingNativeTitleBar (true);
                setContentOwned (c, true);
                setResizable (true, false);
                setResizeLimits (500, 300, 4000, 3000);
                centreWithSize (820, 560);
            }
            void closeButtonPressed() override { setVisible (false); }
        };
        auto* list = new juce::PluginListComponent (formats, known, deadMansPedal, &settings, true);
        list->setOptionsButtonText ("Scan / options...");
        managerWindow = std::make_unique<Window> (list);
    }
    managerWindow->setVisible (true);
    managerWindow->toFront (true);
}

void PluginManager::showEditor (std::shared_ptr<juce::AudioPluginInstance> p, const juce::String& title)
{
    if (p == nullptr) return;
    auto* key = p.get();
    if (auto it = editors.find (key); it != editors.end()) { it->second->setVisible (true); it->second->toFront (true); return; }
    std::weak_ptr<bool> token = alive;
    editors[key] = std::make_unique<EditorWindow> (p, title, [this, token, key] { if (! token.expired()) closeEditor (key); });
}

void PluginManager::closeEditor (juce::AudioPluginInstance* p)
{
    editors.erase (p);
}

void PluginManager::closeAllEditors()
{
    editors.clear();
}

void PluginManager::createAsync (const juce::String& descXml, double sr, int bs,
                                 std::function<void (std::unique_ptr<juce::AudioPluginInstance>, juce::String)> done)
{
    juce::PluginDescription d;
    auto xml = juce::parseXML (descXml);
    if (xml == nullptr || ! d.loadFromXml (*xml)) { done (nullptr, "The plugin description is not valid."); return; }
    std::weak_ptr<bool> token = alive;
    formats.createPluginInstanceAsync (d, sr, bs, [done, token, name = d.name] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err) {
        if (token.expired()) return;
        done (std::move (inst), inst == nullptr && err.isEmpty() ? "\"" + name + "\" could not be loaded (is it still installed?)" : err);
    });
}

std::unique_ptr<juce::AudioPluginInstance> PluginManager::createBlocking (const juce::String& descXml, double sr, int bs, juce::String& error)
{
    juce::PluginDescription d;
    auto xml = juce::parseXML (descXml);
    if (xml == nullptr || ! d.loadFromXml (*xml)) { error = "The plugin description is not valid."; return nullptr; }
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        return formats.createPluginInstance (d, sr, bs, error);

    struct Shared { juce::WaitableEvent done; std::unique_ptr<juce::AudioPluginInstance> inst; juce::String err; };
    auto shared = std::make_shared<Shared>();
    juce::MessageManager::callAsync ([this, d, sr, bs, shared, token = std::weak_ptr<bool> (alive)] {
        if (token.expired()) { shared->done.signal(); return; }
        formats.createPluginInstanceAsync (d, sr, bs, [shared] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& err) {
            shared->inst = std::move (inst);
            shared->err = err;
            shared->done.signal();
        });
    });
    if (! shared->done.wait (60000)) { error = "Timed out loading \"" + d.name + "\"."; return nullptr; }
    error = shared->err;
    return std::move (shared->inst);
}
} // namespace ff
